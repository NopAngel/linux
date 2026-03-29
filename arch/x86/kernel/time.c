// SPDX-License-Identifier: GPL-2.0
/*
 * Core x86 time initialization and interrupt handling.
 * Original Authors: Linus Torvalds, Ingo Molnar, et al.
 */

#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i8253.h>
#include <linux/time.h>
#include <linux/export.h>

#include <asm/vsyscall.h>
#include <asm/x86_init.h>
#include <asm/i8259.h>
#include <asm/timer.h>
#include <asm/hpet.h>
#include <asm/time.h>

/**
 * profile_pc - Returns the instruction pointer for profiling
 * @regs: The saved registers from the interrupt
 */
unsigned long profile_pc(struct pt_regs *regs)
{
	return instruction_pointer(regs);
}
EXPORT_SYMBOL(profile_pc);

/**
 * timer_interrupt - Default IRQ handler for legacy timers (PIT/HPET)
 * Executes the registered event handler for the global clock device.
 */
static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	/* Trigger the generic clock event handler */
	global_clock_event->event_handler(global_clock_event);
	return IRQ_HANDLED;
}

/**
 * setup_default_timer_irq - Register the legacy IRQ0 for timing
 * Used even in modern systems for HPET legacy replacement mode.
 */
static void __init setup_default_timer_irq(void)
{
	/* IRQF_TIMER: Mark as a timer interrupt
	 * IRQF_IRQPOLL: Allow polling during early boot/crash
	 * IRQF_NOBALANCING: Keep it on the boot CPU
	 */
	const unsigned long flags = IRQF_NOBALANCING | IRQF_IRQPOLL | IRQF_TIMER;

	if (request_irq(0, timer_interrupt, flags, "timer", NULL))
		pr_err("time: Failed to register legacy timer interrupt (IRQ0)\n");
}

/**
 * hpet_time_init - Select and initialize the best available hardware timer
 */
void __init hpet_time_init(void)
{
	/* Attempt to enable HPET; fallback to legacy PIT if unavailable */
	if (!hpet_enable()) {
		if (!pit_timer_init()) {
			pr_warn("time: No functional hardware timer found (HPET/PIT)\n");
			return;
		}
	}

	setup_default_timer_irq();
}

/**
 * x86_late_time_init - Late-stage clock and TSC setup
 * Called after ioremap is available to finalize interrupt routing.
 */
static __init void x86_late_time_init(void)
{
	/* 1. Choose the interrupt delivery mode (PIC, IO-APIC, etc.) */
	x86_init.irqs.intr_mode_select();

	/* 2. Initialize the selected hardware timer (PIT/HPET) */
	x86_init.timers.timer_init();

	/* 3. Finalize interrupt delivery and calibrate TSC */
	x86_init.irqs.intr_mode_init();
	tsc_init();

	/* Enable specialized delays if supported (e.g., TPAUSE for power saving) */
	if (static_cpu_has(X86_FEATURE_WAITPKG))
		use_tpause_delay();
}

/**
 * time_init - Early time initialization hook
 * Sets the late_time_init pointer to be executed at the proper boot stage.
 */
void __init time_init(void)
{
	late_time_init = x86_late_time_init;
}

/**
 * clocksource_arch_init - Validate clocksource for vDSO compatibility
 * Ensures the clocksource mask is 64-bit to avoid overflow in userspace.
 */
void clocksource_arch_init(struct clocksource *cs)
{
	if (cs->vdso_clock_mode == VDSO_CLOCKMODE_NONE)
		return;

	if (cs->mask != CLOCKSOURCE_MASK(64)) {
		pr_warn("time: clocksource '%s' has invalid mask %016llx for vDSO; disabling vDSO support.\n",
			cs->name, cs->mask);
		cs->vdso_clock_mode = VDSO_CLOCKMODE_NONE;
	}
}
