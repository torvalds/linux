// SPDX-License-Identifier: GPL-2.0
/*
 * crc32-mips.c - CRC32 and CRC32C using optional MIPSr6 instructions
 *
 * Module based on arm64/crypto/crc32-arm.c
 *
 * Copyright (C) 2014 Linaro Ltd <yazen.ghannam@linaro.org>
 * Copyright (C) 2018 MIPS Tech, LLC
 */

#include <linux/cpufeature.h>
#include <asm/mipsregs.h>
#include <linux/unaligned.h>

#ifndef TOOLCHAIN_SUPPORTS_CRC
#define _ASM_SET_CRC(OP, SZ, TYPE)					  \
_ASM_MACRO_3R(OP, rt, rs, rt2,						  \
	".ifnc	\\rt, \\rt2\n\t"					  \
	".error	\"invalid operands \\\"" #OP " \\rt,\\rs,\\rt2\\\"\"\n\t" \
	".endif\n\t"							  \
	_ASM_INSN_IF_MIPS(0x7c00000f | (__rt << 16) | (__rs << 21) |	  \
			  ((SZ) <<  6) | ((TYPE) << 8))			  \
	_ASM_INSN32_IF_MM(0x00000030 | (__rs << 16) | (__rt << 21) |	  \
			  ((SZ) << 14) | ((TYPE) << 3)))
#define _ASM_UNSET_CRC(op, SZ, TYPE) ".purgem " #op "\n\t"
#else /* !TOOLCHAIN_SUPPORTS_CRC */
#define _ASM_SET_CRC(op, SZ, TYPE) ".set\tcrc\n\t"
#define _ASM_UNSET_CRC(op, SZ, TYPE)
#endif

#define __CRC32(crc, value, op, SZ, TYPE)		\
do {							\
	__asm__ __volatile__(				\
		".set	push\n\t"			\
		_ASM_SET_CRC(op, SZ, TYPE)		\
		#op "	%0, %1, %0\n\t"			\
		_ASM_UNSET_CRC(op, SZ, TYPE)		\
		".set	pop"				\
		: "+r" (crc)				\
		: "r" (value));				\
} while (0)

#define _CRC32_crc32b(crc, value)	__CRC32(crc, value, crc32b, 0, 0)
#define _CRC32_crc32h(crc, value)	__CRC32(crc, value, crc32h, 1, 0)
#define _CRC32_crc32w(crc, value)	__CRC32(crc, value, crc32w, 2, 0)
#define _CRC32_crc32d(crc, value)	__CRC32(crc, value, crc32d, 3, 0)
#define _CRC32_crc32cb(crc, value)	__CRC32(crc, value, crc32cb, 0, 1)
#define _CRC32_crc32ch(crc, value)	__CRC32(crc, value, crc32ch, 1, 1)
#define _CRC32_crc32cw(crc, value)	__CRC32(crc, value, crc32cw, 2, 1)
#define _CRC32_crc32cd(crc, value)	__CRC32(crc, value, crc32cd, 3, 1)

#define _CRC32(crc, value, size, op) \
	_CRC32_##op##size(crc, value)

#define CRC32(crc, value, size) \
	_CRC32(crc, value, size, crc32)

#define CRC32C(crc, value, size) \
	_CRC32(crc, value, size, crc32c)

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_crc32);

static inline u32 crc32_le_arch(u32 crc, const u8 *p, size_t len)
{
	if (!static_branch_likely(&have_crc32))
		return crc32_le_base(crc, p, len);

	if (IS_ENABLED(CONFIG_64BIT)) {
		for (; len >= sizeof(u64); p += sizeof(u64), len -= sizeof(u64)) {
			u64 value = get_unaligned_le64(p);

			CRC32(crc, value, d);
		}

		if (len & sizeof(u32)) {
			u32 value = get_unaligned_le32(p);

			CRC32(crc, value, w);
			p += sizeof(u32);
		}
	} else {
		for (; len >= sizeof(u32); len -= sizeof(u32)) {
			u32 value = get_unaligned_le32(p);

			CRC32(crc, value, w);
			p += sizeof(u32);
		}
	}

	if (len & sizeof(u16)) {
		u16 value = get_unaligned_le16(p);

		CRC32(crc, value, h);
		p += sizeof(u16);
	}

	if (len & sizeof(u8)) {
		u8 value = *p++;

		CRC32(crc, value, b);
	}

	return crc;
}

static inline u32 crc32c_arch(u32 crc, const u8 *p, size_t len)
{
	if (!static_branch_likely(&have_crc32))
		return crc32c_base(crc, p, len);

	if (IS_ENABLED(CONFIG_64BIT)) {
		for (; len >= sizeof(u64); p += sizeof(u64), len -= sizeof(u64)) {
			u64 value = get_unaligned_le64(p);

			CRC32C(crc, value, d);
		}

		if (len & sizeof(u32)) {
			u32 value = get_unaligned_le32(p);

			CRC32C(crc, value, w);
			p += sizeof(u32);
		}
	} else {
		for (; len >= sizeof(u32); len -= sizeof(u32)) {
			u32 value = get_unaligned_le32(p);

			CRC32C(crc, value, w);
			p += sizeof(u32);
		}
	}

	if (len & sizeof(u16)) {
		u16 value = get_unaligned_le16(p);

		CRC32C(crc, value, h);
		p += sizeof(u16);
	}

	if (len & sizeof(u8)) {
		u8 value = *p++;

		CRC32C(crc, value, b);
	}
	return crc;
}

#define crc32_be_arch crc32_be_base /* not implemented on this arch */

#define crc32_mod_init_arch crc32_mod_init_arch
static void crc32_mod_init_arch(void)
{
	if (cpu_have_feature(cpu_feature(MIPS_CRC32)))
		static_branch_enable(&have_crc32);
}

static inline u32 crc32_optimizations_arch(void)
{
	if (static_key_enabled(&have_crc32))
		return CRC32_LE_OPTIMIZATION | CRC32C_OPTIMIZATION;
	return 0;
}
