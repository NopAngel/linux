// SPDX-License-Identifier: GPL-2.0
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/memblock.h>
#include <linux/cc_platform.h>
#include <linux/pgtable.h>

#include <asm/set_memory.h>
#include <asm/realmode.h>
#include <asm/tlbflush.h>
#include <asm/crash.h>
#include <asm/msr.h>
#include <asm/sev.h>

/* Global pointers for real mode trampoline management */
struct real_mode_header *real_mode_header;
u32 *trampoline_cr4_features;

/* Page Directory Entry for booting additional CPUs (APs) */
pgd_t trampoline_pgd_entry;

/**
 * load_trampoline_pgtable - Switch to the trampoline page table
 * Required before transitioning back to real-mode during CPU boot.
 */
void load_trampoline_pgtable(void)
{
#ifdef CONFIG_X86_32
	/* 32-bit uses the initial page table directly */
	load_cr3(initial_page_table);
#else
	/*
	 * PCID must be disabled before exiting to real-mode to avoid
	 * hardware transition failures.
	 */
	if (boot_cpu_has(X86_FEATURE_PCID))
		cr4_clear_bits(X86_CR4_PCIDE);

	/* Switch to the dedicated trampoline PGD */
	write_cr3(real_mode_header->trampoline_pgd);
#endif

	/* Flush TLB to ensure no stale global entries interfere with the trampoline */
	__flush_tlb_all();
}

/**
 * reserve_real_mode - Allocate low memory for the AP trampoline
 * Must be called early, before the slab allocator is ready.
 */
void __init reserve_real_mode(void)
{
	phys_addr_t mem;
	size_t size = real_mode_size_needed();

	if (!size)
		return;

	/* Ensure we are called before slab is up */
	WARN_ON(slab_is_available());

	/* * AP code MUST be under 1MB to be reachable in 16-bit real mode.
	 * Alignment is set to PAGE_SIZE for memory protection compatibility.
	 */
	mem = memblock_phys_alloc_range(size, PAGE_SIZE, 0, 1<<20);
	if (!mem)
		pr_info("Real-mode trampoline: No sub-1M memory available\n");
	else
		set_real_mode_mem(mem);

	/* Reserve the entire first 1MB to prevent corruption during boot */
	memblock_reserve(0, SZ_1M);
	memblock_clear_kho_scratch(0, SZ_1M);
}

static void __init sme_sev_setup_real_mode(struct trampoline_header *th)
{
#ifdef CONFIG_AMD_MEM_ENCRYPT
	/* Set SME flag if host memory encryption is active */
	if (cc_platform_has(CC_ATTR_HOST_MEM_ENCRYPT))
		th->flags |= TH_FLAGS_SME_ACTIVE;

	/* SEV-ES guest: bypass CPU verification to avoid early #VC exceptions */
	if (cc_platform_has(CC_ATTR_GUEST_STATE_ENCRYPT)) {
		th->start = (u64) secondary_startup_64_no_verify;

		if (sev_es_setup_ap_jump_table(real_mode_header))
			panic("SEV-ES: Failed to initialize AP Jump Table");
	}
#endif
}

/**
 * setup_real_mode - Prepare and relocate the real-mode code blob
 */
static void __init setup_real_mode(void)
{
	u16 real_mode_seg;
	const u32 *rel;
	u32 count;
	unsigned char *base;
	unsigned long phys_base;
	struct trampoline_header *trampoline_header;
	size_t size = PAGE_ALIGN(real_mode_blob_end - real_mode_blob);

#ifdef CONFIG_X86_64
	u64 *trampoline_pgd;
	u64 efer;
	int i;
#endif

	base = (unsigned char *)real_mode_header;

	/* Decrypt memory if SME is active for proper multi-CPU visibility */
	if (cc_platform_has(CC_ATTR_HOST_MEM_ENCRYPT))
		set_memory_decrypted((unsigned long)base, size >> PAGE_SHIFT);

	/* Copy the real-mode binary blob to the reserved low memory */
	memcpy(base, real_mode_blob, size);

	phys_base = __pa(base);
	real_mode_seg = phys_base >> 4;

	/* Relocate 16-bit segments and 32-bit linear addresses in the blob */
	rel = (u32 *) real_mode_relocs;

	count = *rel++;
	while (count--) {
		u16 *seg = (u16 *) (base + *rel++);
		*seg = real_mode_seg;
	}

	count = *rel++;
	while (count--) {
		u32 *ptr = (u32 *) (base + *rel++);
		*ptr += phys_base;
	}

	/* Initialize trampoline header after relocation is complete */
	trampoline_header = (struct trampoline_header *)__va(real_mode_header->trampoline_header);

#ifdef CONFIG_X86_32
	trampoline_header->start = __pa_symbol(startup_32_smp);
	trampoline_header->gdt_limit = __BOOT_DS + 7;
	trampoline_header->gdt_base = __pa_symbol(boot_gdt);
#else
	/* Mask out EFER.LMA to prevent #GP on certain AMD processors during WRMSR */
	rdmsrq(MSR_EFER, efer);
	trampoline_header->efer = efer & ~EFER_LMA;

	trampoline_header->start = (u64) secondary_startup_64;
	trampoline_cr4_features = &trampoline_header->cr4;
	*trampoline_cr4_features = mmu_cr4_features;

	trampoline_header->flags = 0;
	trampoline_lock = &trampoline_header->lock;
	*trampoline_lock = 0;

	/* Setup trampoline PGD and clone kernel mappings for AP boot */
	trampoline_pgd = (u64 *) __va(real_mode_header->trampoline_pgd);
	trampoline_pgd[0] = trampoline_pgd_entry.pgd;

	for (i = pgd_index(__PAGE_OFFSET); i < PTRS_PER_PGD; i++)
		trampoline_pgd[i] = init_top_pgt[i].pgd;
#endif

	sme_sev_setup_real_mode(trampoline_header);
}

/**
 * set_real_mode_permissions - Apply NX/RO/X protections to trampoline memory
 */
static void __init set_real_mode_permissions(void)
{
	unsigned char *base = (unsigned char *) real_mode_header;
	size_t size = PAGE_ALIGN(real_mode_blob_end - real_mode_blob);

	size_t ro_size = PAGE_ALIGN(real_mode_header->ro_end) - __pa(base);
	size_t text_size = PAGE_ALIGN(real_mode_header->ro_end) - real_mode_header->text_start;
	unsigned long text_start = (unsigned long) __va(real_mode_header->text_start);

	/* Default to No-Execute, then mark specific sections as Read-Only or Executable */
	set_memory_nx((unsigned long) base, size >> PAGE_SHIFT);
	set_memory_ro((unsigned long) base, ro_size >> PAGE_SHIFT);
	set_memory_x((unsigned long) text_start, text_size >> PAGE_SHIFT);
}

void __init init_real_mode(void)
{
	if (!real_mode_header)
		panic("Real-mode: Initialization failed (header not allocated)");

	setup_real_mode();
	set_real_mode_permissions();
}

static int __init do_init_real_mode(void)
{
	x86_platform.realmode_init();
	return 0;
}
early_initcall(do_init_real_mode);
