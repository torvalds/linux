// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RISC-V optimized CRC64 functions
 *
 * Copyright 2025 Google LLC
 */

#include <asm/hwcap.h>
#include <asm/alternative-macros.h>

#include "crc-clmul.h"

static inline u64 crc64_be_arch(u64 crc, const u8 *p, size_t len)
{
	if (riscv_has_extension_likely(RISCV_ISA_EXT_ZBC))
		return crc64_msb_clmul(crc, p, len,
				       &crc64_msb_0x42f0e1eba9ea3693_consts);
	return crc64_be_generic(crc, p, len);
}

static inline u64 crc64_nvme_arch(u64 crc, const u8 *p, size_t len)
{
	if (riscv_has_extension_likely(RISCV_ISA_EXT_ZBC))
		return crc64_lsb_clmul(crc, p, len,
				       &crc64_lsb_0x9a6c9329ac4bc9b5_consts);
	return crc64_nvme_generic(crc, p, len);
}
