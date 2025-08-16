/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SHA-256 accelerated using the sparc64 sha256 opcodes
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * SHA224 Support Copyright 2007 Intel Corporation <jonathan.lynch@intel.com>
 */

#include <asm/elf.h>
#include <asm/opcodes.h>
#include <asm/pstate.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_sha256_opcodes);

asmlinkage void sha256_sparc64_transform(struct sha256_block_state *state,
					 const u8 *data, size_t nblocks);

static void sha256_blocks(struct sha256_block_state *state,
			  const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_sha256_opcodes))
		sha256_sparc64_transform(state, data, nblocks);
	else
		sha256_blocks_generic(state, data, nblocks);
}

#define sha256_mod_init_arch sha256_mod_init_arch
static void sha256_mod_init_arch(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_SHA256))
		return;

	static_branch_enable(&have_sha256_opcodes);
	pr_info("Using sparc64 sha256 opcode optimized SHA-256/SHA-224 implementation\n");
}
