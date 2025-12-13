/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SHA-1 accelerated using the sparc64 crypto opcodes
 *
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 * Copyright (c) Mathias Krause <minipli@googlemail.com>
 */

#include <asm/elf.h>
#include <asm/opcodes.h>
#include <asm/pstate.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_sha1_opcodes);

asmlinkage void sha1_sparc64_transform(struct sha1_block_state *state,
				       const u8 *data, size_t nblocks);

static void sha1_blocks(struct sha1_block_state *state,
			const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_sha1_opcodes))
		sha1_sparc64_transform(state, data, nblocks);
	else
		sha1_blocks_generic(state, data, nblocks);
}

#define sha1_mod_init_arch sha1_mod_init_arch
static void sha1_mod_init_arch(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_SHA1))
		return;

	static_branch_enable(&have_sha1_opcodes);
	pr_info("Using sparc64 sha1 opcode optimized SHA-1 implementation\n");
}
