// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CRC-T10DIF using [V]PCLMULQDQ instructions
 *
 * Copyright 2024 Google LLC
 */

#include "crc-pclmul-template.h"

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_pclmulqdq);

DECLARE_CRC_PCLMUL_FUNCS(crc16_msb, u16);

static inline u16 crc_t10dif_arch(u16 crc, const u8 *p, size_t len)
{
	CRC_PCLMUL(crc, p, len, crc16_msb, crc16_msb_0x8bb7_consts,
		   have_pclmulqdq);
	return crc_t10dif_generic(crc, p, len);
}

#define crc_t10dif_mod_init_arch crc_t10dif_mod_init_arch
static void crc_t10dif_mod_init_arch(void)
{
	if (boot_cpu_has(X86_FEATURE_PCLMULQDQ)) {
		static_branch_enable(&have_pclmulqdq);
		if (have_vpclmul()) {
			if (have_avx512())
				static_call_update(crc16_msb_pclmul,
						   crc16_msb_vpclmul_avx512);
			else
				static_call_update(crc16_msb_pclmul,
						   crc16_msb_vpclmul_avx2);
		}
	}
}
