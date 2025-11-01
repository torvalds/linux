/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * MD5 accelerated using the sparc64 crypto opcodes
 *
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 * Copyright (c) Mathias Krause <minipli@googlemail.com>
 * Copyright (c) Cryptoapi developers.
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 */

#include <asm/elf.h>
#include <asm/opcodes.h>
#include <asm/pstate.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_md5_opcodes);

asmlinkage void md5_sparc64_transform(struct md5_block_state *state,
				      const u8 *data, size_t nblocks);

static void md5_blocks(struct md5_block_state *state,
		       const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_md5_opcodes)) {
		cpu_to_le32_array(state->h, ARRAY_SIZE(state->h));
		md5_sparc64_transform(state, data, nblocks);
		le32_to_cpu_array(state->h, ARRAY_SIZE(state->h));
	} else {
		md5_blocks_generic(state, data, nblocks);
	}
}

#define md5_mod_init_arch md5_mod_init_arch
static void md5_mod_init_arch(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_MD5))
		return;

	static_branch_enable(&have_md5_opcodes);
	pr_info("Using sparc64 md5 opcode optimized MD5 implementation\n");
}
