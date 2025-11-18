// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The ChaCha stream cipher (RFC7539)
 *
 * Copyright (C) 2015 Martin Willi
 */

#include <crypto/algapi.h> // for crypto_xor_cpy
#include <crypto/chacha.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>

static void __maybe_unused
chacha_crypt_generic(struct chacha_state *state, u8 *dst, const u8 *src,
		     unsigned int bytes, int nrounds)
{
	/* aligned to potentially speed up crypto_xor() */
	u8 stream[CHACHA_BLOCK_SIZE] __aligned(sizeof(long));

	while (bytes >= CHACHA_BLOCK_SIZE) {
		chacha_block_generic(state, stream, nrounds);
		crypto_xor_cpy(dst, src, stream, CHACHA_BLOCK_SIZE);
		bytes -= CHACHA_BLOCK_SIZE;
		dst += CHACHA_BLOCK_SIZE;
		src += CHACHA_BLOCK_SIZE;
	}
	if (bytes) {
		chacha_block_generic(state, stream, nrounds);
		crypto_xor_cpy(dst, src, stream, bytes);
	}
}

#ifdef CONFIG_CRYPTO_LIB_CHACHA_ARCH
#include "chacha.h" /* $(SRCARCH)/chacha.h */
#else
#define chacha_crypt_arch chacha_crypt_generic
#define hchacha_block_arch hchacha_block_generic
#endif

void chacha_crypt(struct chacha_state *state, u8 *dst, const u8 *src,
		  unsigned int bytes, int nrounds)
{
	chacha_crypt_arch(state, dst, src, bytes, nrounds);
}
EXPORT_SYMBOL_GPL(chacha_crypt);

void hchacha_block(const struct chacha_state *state,
		   u32 out[HCHACHA_OUT_WORDS], int nrounds)
{
	hchacha_block_arch(state, out, nrounds);
}
EXPORT_SYMBOL_GPL(hchacha_block);

#ifdef chacha_mod_init_arch
static int __init chacha_mod_init(void)
{
	chacha_mod_init_arch();
	return 0;
}
subsys_initcall(chacha_mod_init);

static void __exit chacha_mod_exit(void)
{
}
module_exit(chacha_mod_exit);
#endif

MODULE_DESCRIPTION("ChaCha stream cipher (RFC7539)");
MODULE_LICENSE("GPL");
