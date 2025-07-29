// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RISC-V optimized CRC32 functions
 *
 * Copyright 2025 Google LLC
 */

#include <asm/hwcap.h>
#include <asm/alternative-macros.h>

#include "crc-clmul.h"

static inline u32 crc32_le_arch(u32 crc, const u8 *p, size_t len)
{
	if (riscv_has_extension_likely(RISCV_ISA_EXT_ZBC))
		return crc32_lsb_clmul(crc, p, len,
				       &crc32_lsb_0xedb88320_consts);
	return crc32_le_base(crc, p, len);
}

static inline u32 crc32_be_arch(u32 crc, const u8 *p, size_t len)
{
	if (riscv_has_extension_likely(RISCV_ISA_EXT_ZBC))
		return crc32_msb_clmul(crc, p, len,
				       &crc32_msb_0x04c11db7_consts);
	return crc32_be_base(crc, p, len);
}

static inline u32 crc32c_arch(u32 crc, const u8 *p, size_t len)
{
	if (riscv_has_extension_likely(RISCV_ISA_EXT_ZBC))
		return crc32_lsb_clmul(crc, p, len,
				       &crc32_lsb_0x82f63b78_consts);
	return crc32c_base(crc, p, len);
}

static inline u32 crc32_optimizations_arch(void)
{
	if (riscv_has_extension_likely(RISCV_ISA_EXT_ZBC))
		return CRC32_LE_OPTIMIZATION |
		       CRC32_BE_OPTIMIZATION |
		       CRC32C_OPTIMIZATION;
	return 0;
}
