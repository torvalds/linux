/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SHA-512 accelerated using the sparc64 sha512 opcodes
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2003 Kyle McMartin <kyle@debian.org>
 */

#include <asm/elf.h>
#include <asm/opcodes.h>
#include <asm/pstate.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_sha512_opcodes);

asmlinkage void sha512_sparc64_transform(struct sha512_block_state *state,
					 const u8 *data, size_t nblocks);

static void sha512_blocks(struct sha512_block_state *state,
			  const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_sha512_opcodes))
		sha512_sparc64_transform(state, data, nblocks);
	else
		sha512_blocks_generic(state, data, nblocks);
}

#define sha512_mod_init_arch sha512_mod_init_arch
static void sha512_mod_init_arch(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_SHA512))
		return;

	static_branch_enable(&have_sha512_opcodes);
	pr_info("Using sparc64 sha512 opcode optimized SHA-512/SHA-384 implementation\n");
}
