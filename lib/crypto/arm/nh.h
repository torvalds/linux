/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM32 accelerated implementation of NH
 *
 * Copyright 2018 Google LLC
 */

#include <asm/neon.h>
#include <asm/simd.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);

asmlinkage void nh_neon(const u32 *key, const u8 *message, size_t message_len,
			__le64 hash[NH_NUM_PASSES]);

static bool nh_arch(const u32 *key, const u8 *message, size_t message_len,
		    __le64 hash[NH_NUM_PASSES])
{
	if (static_branch_likely(&have_neon) && message_len >= 64 &&
	    may_use_simd()) {
		scoped_ksimd()
			nh_neon(key, message, message_len, hash);
		return true;
	}
	return false;
}

#define nh_mod_init_arch nh_mod_init_arch
static void nh_mod_init_arch(void)
{
	if (elf_hwcap & HWCAP_NEON)
		static_branch_enable(&have_neon);
}
