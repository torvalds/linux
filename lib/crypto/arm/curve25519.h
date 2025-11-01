// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * Based on public domain code from Daniel J. Bernstein and Peter Schwabe. This
 * began from SUPERCOP's curve25519/neon2/scalarmult.s, but has subsequently been
 * manually reworked for use in kernel space.
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/internal/simd.h>
#include <linux/types.h>
#include <linux/jump_label.h>

asmlinkage void curve25519_neon(u8 mypublic[CURVE25519_KEY_SIZE],
				const u8 secret[CURVE25519_KEY_SIZE],
				const u8 basepoint[CURVE25519_KEY_SIZE]);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);

static void curve25519_arch(u8 out[CURVE25519_KEY_SIZE],
			    const u8 scalar[CURVE25519_KEY_SIZE],
			    const u8 point[CURVE25519_KEY_SIZE])
{
	if (static_branch_likely(&have_neon) && crypto_simd_usable()) {
		kernel_neon_begin();
		curve25519_neon(out, scalar, point);
		kernel_neon_end();
	} else {
		curve25519_generic(out, scalar, point);
	}
}

static void curve25519_base_arch(u8 pub[CURVE25519_KEY_SIZE],
				 const u8 secret[CURVE25519_KEY_SIZE])
{
	curve25519_arch(pub, secret, curve25519_base_point);
}

#define curve25519_mod_init_arch curve25519_mod_init_arch
static void curve25519_mod_init_arch(void)
{
	if (elf_hwcap & HWCAP_NEON)
		static_branch_enable(&have_neon);
}
