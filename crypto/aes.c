// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Crypto API support for AES block cipher
 *
 * Optimized version for Fork/Security development.
 * Copyright 2026 Google LLC
 */

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <linux/module.h>
#include <linux/kernel.h>

/* Ensure AES key structure meets crypto alignment requirements */
static_assert(__alignof__(struct aes_key) <= CRYPTO_MINALIGN);

/**
 * crypto_aes_setkey - Initialize the AES key schedule
 * @tfm: Crypto transformation object
 * @in_key: Raw key bytes provided by the user
 * @key_len: Length of the key (128, 192, or 256 bits)
 */
static int crypto_aes_setkey(struct crypto_tfm *tfm, const u8 *in_key,
			     unsigned int key_len)
{
	struct aes_key *key = crypto_tfm_ctx(tfm);

	/* Delegate key expansion to the internal AES library */
	return aes_preparekey(key, in_key, key_len);
}

/**
 * crypto_aes_encrypt - Encrypt a single 16-byte block
 * @tfm: Crypto transformation object containing the key
 * @out: Destination buffer for ciphertext
 * @in: Source buffer for plaintext
 */
static void crypto_aes_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct aes_key *key = crypto_tfm_ctx(tfm);

	/* Perform AES encryption on exactly one block */
	aes_encrypt(key, out, in);
}

/**
 * crypto_aes_decrypt - Decrypt a single 16-byte block
 * @tfm: Crypto transformation object containing the key
 * @out: Destination buffer for plaintext
 * @in: Source buffer for ciphertext
 */
static void crypto_aes_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct aes_key *key = crypto_tfm_ctx(tfm);

	/* Perform AES decryption on exactly one block */
	aes_decrypt(key, out, in);
}

/* Algorithm definition for the Crypto API registration */
static struct crypto_alg aes_alg = {
	.cra_name		= "aes",
	.cra_driver_name	= "aes-generic",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct aes_key),
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.cipher = {
			.cia_min_keysize	= AES_MIN_KEY_SIZE,
			.cia_max_keysize	= AES_MAX_KEY_SIZE,
			.cia_setkey		= crypto_aes_setkey,
			.cia_encrypt		= crypto_aes_encrypt,
			.cia_decrypt		= crypto_aes_decrypt,
		}
	}
};

static int __init crypto_aes_mod_init(void)
{
	/* Register the algorithm with the kernel crypto subsystem */
	return crypto_register_alg(&aes_alg);
}

static void __exit crypto_aes_mod_exit(void)
{
	/* Clean up by unregistering the algorithm on module unload */
	crypto_unregister_alg(&aes_alg);
}

module_init(crypto_aes_mod_init);
module_exit(crypto_aes_mod_exit);

MODULE_DESCRIPTION("Generic AES Block Cipher - Crypto API");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("NopAngel <Software Dev>");
MODULE_ALIAS_CRYPTO("aes");
