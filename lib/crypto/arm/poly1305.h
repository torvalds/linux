/* SPDX-License-Identifier: GPL-2.0 */
/*
 * OpenSSL/Cryptogams accelerated Poly1305 transform for ARM
 *
 * Copyright (C) 2019 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <linux/cpufeature.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>

asmlinkage void poly1305_block_init(struct poly1305_block_state *state,
				    const u8 raw_key[POLY1305_BLOCK_SIZE]);
asmlinkage void poly1305_blocks_arm(struct poly1305_block_state *state,
				    const u8 *src, u32 len, u32 hibit);
asmlinkage void poly1305_blocks_neon(struct poly1305_block_state *state,
				     const u8 *src, u32 len, u32 hibit);
asmlinkage void poly1305_emit(const struct poly1305_state *state,
			      u8 digest[POLY1305_DIGEST_SIZE],
			      const u32 nonce[4]);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);

static void poly1305_blocks(struct poly1305_block_state *state, const u8 *src,
			    unsigned int len, u32 padbit)
{
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    static_branch_likely(&have_neon) && likely(may_use_simd())) {
		do {
			unsigned int todo = min_t(unsigned int, len, SZ_4K);

			kernel_neon_begin();
			poly1305_blocks_neon(state, src, todo, padbit);
			kernel_neon_end();

			len -= todo;
			src += todo;
		} while (len);
	} else
		poly1305_blocks_arm(state, src, len, padbit);
}

#ifdef CONFIG_KERNEL_MODE_NEON
#define poly1305_mod_init_arch poly1305_mod_init_arch
static void poly1305_mod_init_arch(void)
{
	if (elf_hwcap & HWCAP_NEON)
		static_branch_enable(&have_neon);
}
#endif /* CONFIG_KERNEL_MODE_NEON */
