/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Poly1305 authenticator algorithm, RFC7539.
 *
 * Copyright 2023- IBM Corp. All rights reserved.
 */
#include <asm/switch_to.h>
#include <linux/cpufeature.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/unaligned.h>

asmlinkage void poly1305_p10le_4blocks(struct poly1305_block_state *state, const u8 *m, u32 mlen);
asmlinkage void poly1305_64s(struct poly1305_block_state *state, const u8 *m, u32 mlen, int highbit);
asmlinkage void poly1305_emit_64(const struct poly1305_state *state, const u32 nonce[4], u8 digest[POLY1305_DIGEST_SIZE]);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_p10);

static void vsx_begin(void)
{
	preempt_disable();
	enable_kernel_vsx();
}

static void vsx_end(void)
{
	disable_kernel_vsx();
	preempt_enable();
}

static void poly1305_block_init(struct poly1305_block_state *dctx,
				const u8 raw_key[POLY1305_BLOCK_SIZE])
{
	if (!static_key_enabled(&have_p10))
		return poly1305_block_init_generic(dctx, raw_key);

	dctx->h = (struct poly1305_state){};
	dctx->core_r.key.r64[0] = get_unaligned_le64(raw_key + 0);
	dctx->core_r.key.r64[1] = get_unaligned_le64(raw_key + 8);
}

static void poly1305_blocks(struct poly1305_block_state *state, const u8 *src,
			    unsigned int len, u32 padbit)
{
	if (!static_key_enabled(&have_p10))
		return poly1305_blocks_generic(state, src, len, padbit);
	vsx_begin();
	if (len >= POLY1305_BLOCK_SIZE * 4) {
		poly1305_p10le_4blocks(state, src, len);
		src += len - (len % (POLY1305_BLOCK_SIZE * 4));
		len %= POLY1305_BLOCK_SIZE * 4;
	}
	while (len >= POLY1305_BLOCK_SIZE) {
		poly1305_64s(state, src, POLY1305_BLOCK_SIZE, padbit);
		len -= POLY1305_BLOCK_SIZE;
		src += POLY1305_BLOCK_SIZE;
	}
	vsx_end();
}

static void poly1305_emit(const struct poly1305_state *state,
			  u8 digest[POLY1305_DIGEST_SIZE], const u32 nonce[4])
{
	if (!static_key_enabled(&have_p10))
		return poly1305_emit_generic(state, digest, nonce);
	poly1305_emit_64(state, nonce, digest);
}

#define poly1305_mod_init_arch poly1305_mod_init_arch
static void poly1305_mod_init_arch(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_31))
		static_branch_enable(&have_p10);
}
