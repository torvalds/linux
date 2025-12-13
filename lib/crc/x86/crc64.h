// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CRC64 using [V]PCLMULQDQ instructions
 *
 * Copyright 2025 Google LLC
 */

#include "crc-pclmul-template.h"

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_pclmulqdq);

DECLARE_CRC_PCLMUL_FUNCS(crc64_msb, u64);
DECLARE_CRC_PCLMUL_FUNCS(crc64_lsb, u64);

static inline u64 crc64_be_arch(u64 crc, const u8 *p, size_t len)
{
	CRC_PCLMUL(crc, p, len, crc64_msb, crc64_msb_0x42f0e1eba9ea3693_consts,
		   have_pclmulqdq);
	return crc64_be_generic(crc, p, len);
}

static inline u64 crc64_nvme_arch(u64 crc, const u8 *p, size_t len)
{
	CRC_PCLMUL(crc, p, len, crc64_lsb, crc64_lsb_0x9a6c9329ac4bc9b5_consts,
		   have_pclmulqdq);
	return crc64_nvme_generic(crc, p, len);
}

#define crc64_mod_init_arch crc64_mod_init_arch
static void crc64_mod_init_arch(void)
{
	if (boot_cpu_has(X86_FEATURE_PCLMULQDQ)) {
		static_branch_enable(&have_pclmulqdq);
		if (have_vpclmul()) {
			if (have_avx512()) {
				static_call_update(crc64_msb_pclmul,
						   crc64_msb_vpclmul_avx512);
				static_call_update(crc64_lsb_pclmul,
						   crc64_lsb_vpclmul_avx512);
			} else {
				static_call_update(crc64_msb_pclmul,
						   crc64_msb_vpclmul_avx2);
				static_call_update(crc64_lsb_pclmul,
						   crc64_lsb_vpclmul_avx2);
			}
		}
	}
}
