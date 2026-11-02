// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dwarf-regs.c : Mapping of DWARF debug register numbers into register names.
 * Extracted from probe-finder.c
 *
 * Written by Masami Hiramatsu <mhiramat@redhat.com>
 */

#include <errno.h> /* for EINVAL */
#include <string.h> /* for strcmp */
#include <linux/kernel.h> /* for ARRAY_SIZE */
#include <dwarf-regs.h>
#include "../../../arch/x86/include/uapi/asm/perf_regs.h"

struct dwarf_regs_idx {
	const char *name;
	int dwarf_regnum;
};

static const struct dwarf_regs_idx i386_regidx_table[] = {
	{ "eax", 0 }, { "ax", 0 }, { "al", 0 },
	{ "ecx", 1 }, { "cx", 1 }, { "cl", 1 },
	{ "edx", 2 }, { "dx", 2 }, { "dl", 2 },
	{ "ebx", 3 }, { "bx", 3 }, { "bl", 3 },
	{ "esp", 4 }, { "sp", 4 }, { "$stack", 4},
	{ "ebp", 5 }, { "bp", 5 },
	{ "esi", 6 }, { "si", 6 },
	{ "edi", 7 }, { "di", 7 },
	// 8 - Return Address RA
	{ "eflags", 9}, { "flags", 9},
	// 10 - reserved
	{ "st0", 11},
	{ "st1", 12},
	{ "st2", 13},
	{ "st3", 14},
	{ "st4", 15},
	{ "st5", 16},
	{ "st6", 17},
	{ "st7", 18},
	// 19-20 - reserved
	{ "xmm0", 21},
	{ "xmm1", 22},
	{ "xmm2", 23},
	{ "xmm3", 24},
	{ "xmm4", 25},
	{ "xmm5", 26},
	{ "xmm6", 27},
	{ "xmm7", 28},
	{ "mm0", 29},
	{ "mm1", 30},
	{ "mm2", 31},
	{ "mm3", 32},
	{ "mm4", 33},
	{ "mm5", 34},
	{ "mm6", 35},
	{ "mm7", 36},
	// 37-38 - unknown
	{ "mxcsr", 39}, // 128-bit Media Control and Status
	{ "es", 40},
	{ "cs", 41},
	{ "ss", 42},
	{ "ds", 43},
	{ "fs", 44},
	{ "gs", 45},
	// 46-47 - reserved
	{ "tr", 48}, // Task Register
	{ "ldtr", 49}, // LDT Register
	// 50-92 - reserved
	{ "fs.base", 92},
	{ "gs.base", 93},
	// End of regular dwarf registers.
	{ "eip", DWARF_REG_PC }, { "ip", DWARF_REG_PC },
};

static const struct dwarf_regs_idx x86_64_regidx_table[] = {
	{ "rax", 0 }, { "eax", 0 }, { "ax", 0 }, { "al", 0 },
	{ "rdx", 1 }, { "edx", 1 }, { "dx", 1 }, { "dl", 1 },
	{ "rcx", 2 }, { "ecx", 2 }, { "cx", 2 }, { "cl", 2 },
	{ "rbx", 3 }, { "edx", 3 }, { "bx", 3 }, { "bl", 3 },
	{ "rsi", 4 }, { "esi", 4 }, { "si", 4 }, { "sil", 4 },
	{ "rdi", 5 }, { "edi", 5 }, { "di", 5 }, { "dil", 5 },
	{ "rbp", 6 }, { "ebp", 6 }, { "bp", 6 }, { "bpl", 6 },
	{ "rsp", 7 }, { "esp", 7 }, { "sp", 7 }, { "spl", 7 },
	{ "r8", 8 }, { "r8d", 8 }, { "r8w", 8 }, { "r8b", 8 },
	{ "r9", 9 }, { "r9d", 9 }, { "r9w", 9 }, { "r9b", 9 },
	{ "r10", 10 }, { "r10d", 10 }, { "r10w", 10 }, { "r10b", 10 },
	{ "r11", 11 }, { "r11d", 11 }, { "r11w", 11 }, { "r11b", 11 },
	{ "r12", 12 }, { "r12d", 12 }, { "r12w", 12 }, { "r12b", 12 },
	{ "r13", 13 }, { "r13d", 13 }, { "r13w", 13 }, { "r13b", 13 },
	{ "r14", 14 }, { "r14d", 14 }, { "r14w", 14 }, { "r14b", 14 },
	{ "r15", 15 }, { "r15d", 15 }, { "r15w", 15 }, { "r15b", 15 },
	// 16 - Return Address RA
	{ "zmm0", 17 }, { "ymm0", 17 }, { "xmm0", 17 },
	{ "zmm1", 18 }, { "ymm1", 18 }, { "xmm1", 18 },
	{ "zmm2", 19 }, { "ymm2", 19 }, { "xmm2", 19 },
	{ "zmm3", 20 }, { "ymm3", 20 }, { "xmm3", 20 },
	{ "zmm4", 21 }, { "ymm4", 21 }, { "xmm4", 21 },
	{ "zmm5", 22 }, { "ymm5", 22 }, { "xmm5", 22 },
	{ "zmm6", 23 }, { "ymm6", 23 }, { "xmm6", 23 },
	{ "zmm7", 24 }, { "ymm7", 24 }, { "xmm7", 24 },
	{ "zmm8", 25 }, { "ymm8", 25 }, { "xmm8", 25 },
	{ "zmm9", 26 }, { "ymm9", 26 }, { "xmm9", 26 },
	{ "zmm10", 27 }, { "ymm10", 27 }, { "xmm10", 27 },
	{ "zmm11", 28 }, { "ymm11", 28 }, { "xmm11", 28 },
	{ "zmm12", 29 }, { "ymm12", 29 }, { "xmm12", 29 },
	{ "zmm13", 30 }, { "ymm13", 30 }, { "xmm13", 30 },
	{ "zmm14", 31 }, { "ymm14", 31 }, { "xmm14", 31 },
	{ "zmm15", 32 }, { "ymm15", 32 }, { "xmm15", 32 },
	{ "st0", 33},
	{ "st1", 34},
	{ "st2", 35},
	{ "st3", 36},
	{ "st4", 37},
	{ "st5", 38},
	{ "st6", 39},
	{ "st7", 40},
	{ "mm0", 41},
	{ "mm1", 42},
	{ "mm2", 43},
	{ "mm3", 44},
	{ "mm4", 45},
	{ "mm5", 46},
	{ "mm6", 47},
	{ "mm7", 48},
	{ "rflags", 49}, { "eflags", 49}, { "flags", 49},
	{ "es", 50},
	{ "cs", 51},
	{ "ss", 52},
	{ "ds", 53},
	{ "fs", 54},
	{ "gs", 55},
	// 56-57 - reserved
	{ "fs.base", 58},
	{ "gs.base", 59},
	// 60-61 - reserved
	{ "tr", 62}, // Task Register
	{ "ldtr", 63}, // LDT Register
	{ "mxcsr", 64}, // 128-bit Media Control and Status
	{ "fcw", 65}, // x87 Control Word
	{ "fsw", 66}, // x87 Status Word
	// 67-82 - Upper Vector Registers 16–31
	{ "zmm16", 67 }, { "ymm16", 67 }, { "xmm16", 67 },
	{ "zmm17", 68 }, { "ymm17", 68 }, { "xmm17", 68 },
	{ "zmm18", 69 }, { "ymm18", 69 }, { "xmm18", 69 },
	{ "zmm19", 70 }, { "ymm19", 70 }, { "xmm19", 70 },
	{ "zmm20", 71 }, { "ymm20", 71 }, { "xmm20", 71 },
	{ "zmm21", 72 }, { "ymm21", 72 }, { "xmm21", 72 },
	{ "zmm22", 73 }, { "ymm22", 73 }, { "xmm22", 73 },
	{ "zmm23", 74 }, { "ymm23", 74 }, { "xmm23", 74 },
	{ "zmm24", 75 }, { "ymm24", 75 }, { "xmm24", 75 },
	{ "zmm25", 76 }, { "ymm25", 76 }, { "xmm25", 76 },
	{ "zmm26", 77 }, { "ymm26", 77 }, { "xmm26", 77 },
	{ "zmm27", 78 }, { "ymm27", 78 }, { "xmm27", 78 },
	{ "zmm28", 79 }, { "ymm28", 79 }, { "xmm28", 79 },
	{ "zmm29", 80 }, { "ymm29", 80 }, { "xmm29", 80 },
	{ "zmm30", 81 }, { "ymm30", 81 }, { "xmm30", 81 },
	{ "zmm31", 82 }, { "ymm31", 82 }, { "xmm31", 82 },
	// 118-125 - Vector Mask Registers 0–7
	{ "k0", 118 },
	{ "k1", 119 },
	{ "k2", 120 },
	{ "k3", 121 },
	{ "k4", 122 },
	{ "k5", 123 },
	{ "k6", 124 },
	{ "k7", 125 },
	// 130-145 - APX Integer Registers 16-31
	{ "r16", 130 }, { "r16d", 130 }, { "r16w", 130 }, { "r16b", 130 },
	{ "r17", 131 }, { "r17d", 131 }, { "r17w", 131 }, { "r17b", 131 },
	{ "r18", 132 }, { "r18d", 132 }, { "r18w", 132 }, { "r18b", 132 },
	{ "r19", 133 }, { "r19d", 133 }, { "r19w", 133 }, { "r19b", 133 },
	{ "r20", 134 }, { "r20d", 134 }, { "r20w", 134 }, { "r20b", 134 },
	{ "r21", 135 }, { "r21d", 135 }, { "r21w", 135 }, { "r21b", 135 },
	{ "r22", 136 }, { "r22d", 136 }, { "r22w", 136 }, { "r22b", 136 },
	{ "r23", 137 }, { "r23d", 137 }, { "r23w", 137 }, { "r23b", 137 },
	{ "r24", 138 }, { "r24d", 138 }, { "r24w", 138 }, { "r24b", 138 },
	{ "r25", 139 }, { "r25d", 139 }, { "r25w", 139 }, { "r25b", 139 },
	{ "r26", 140 }, { "r26d", 140 }, { "r26w", 140 }, { "r26b", 140 },
	{ "r27", 141 }, { "r27d", 141 }, { "r27w", 141 }, { "r27b", 141 },
	{ "r28", 142 }, { "r28d", 142 }, { "r28w", 142 }, { "r28b", 142 },
	{ "r29", 143 }, { "r29d", 143 }, { "r29w", 143 }, { "r29b", 143 },
	{ "r30", 144 }, { "r30d", 144 }, { "r30w", 144 }, { "r30b", 144 },
	{ "r31", 145 }, { "r31d", 145 }, { "r31w", 145 }, { "r31b", 145 },
	// End of regular dwarf registers.
	{ "rip", DWARF_REG_PC }, { "eip", DWARF_REG_PC }, { "ip", DWARF_REG_PC },
};

static int get_regnum(const struct dwarf_regs_idx *entries, size_t num_entries, const char *name)
{
	if (*name != '%')
		return -EINVAL;

	name++;
	for (size_t i = 0; i < num_entries; i++) {
		if (!strcmp(entries[i].name, name))
			return entries[i].dwarf_regnum;
	}
	return -ENOENT;
}

int __get_dwarf_regnum_i386(const char *name)
{
	return get_regnum(i386_regidx_table, ARRAY_SIZE(i386_regidx_table), name);
}

int __get_dwarf_regnum_x86_64(const char *name)
{
	return get_regnum(x86_64_regidx_table, ARRAY_SIZE(x86_64_regidx_table), name);
}

int __get_dwarf_regnum_for_perf_regnum_i386(int perf_regnum)
{
	static const int dwarf_i386_regnums[] = {
		[PERF_REG_X86_AX] = 0,
		[PERF_REG_X86_BX] = 3,
		[PERF_REG_X86_CX] = 1,
		[PERF_REG_X86_DX] = 2,
		[PERF_REG_X86_SI] = 6,
		[PERF_REG_X86_DI] = 7,
		[PERF_REG_X86_BP] = 5,
		[PERF_REG_X86_SP] = 4,
		[PERF_REG_X86_IP] = 8,
		[PERF_REG_X86_FLAGS] = 9,
		[PERF_REG_X86_CS] = 41,
		[PERF_REG_X86_SS] = 42,
		[PERF_REG_X86_DS] = 43,
		[PERF_REG_X86_ES] = 40,
		[PERF_REG_X86_FS] = 44,
		[PERF_REG_X86_GS] = 45,
		[PERF_REG_X86_XMM0] = 21,
		[PERF_REG_X86_XMM1] = 22,
		[PERF_REG_X86_XMM2] = 23,
		[PERF_REG_X86_XMM3] = 24,
		[PERF_REG_X86_XMM4] = 25,
		[PERF_REG_X86_XMM5] = 26,
		[PERF_REG_X86_XMM6] = 27,
		[PERF_REG_X86_XMM7] = 28,
	};

	if (perf_regnum == 0)
		return 0;

	if (perf_regnum < 0 || perf_regnum >= (int)ARRAY_SIZE(dwarf_i386_regnums) ||
	    dwarf_i386_regnums[perf_regnum] == 0)
		return -ENOENT;

	return dwarf_i386_regnums[perf_regnum];
}

int __get_dwarf_regnum_for_perf_regnum_x86_64(int perf_regnum, int abi)
{
	static const int dwarf_x86_64_regnums[] = {
		[PERF_REG_X86_AX] = 0,
		[PERF_REG_X86_BX] = 3,
		[PERF_REG_X86_CX] = 2,
		[PERF_REG_X86_DX] = 1,
		[PERF_REG_X86_SI] = 4,
		[PERF_REG_X86_DI] = 5,
		[PERF_REG_X86_BP] = 6,
		[PERF_REG_X86_SP] = 7,
		[PERF_REG_X86_IP] = 16,
		[PERF_REG_X86_FLAGS] = 49,
		[PERF_REG_X86_CS] = 51,
		[PERF_REG_X86_SS] = 52,
		[PERF_REG_X86_DS] = 53,
		[PERF_REG_X86_ES] = 50,
		[PERF_REG_X86_FS] = 54,
		[PERF_REG_X86_GS] = 55,
		[PERF_REG_X86_R8] = 8,
		[PERF_REG_X86_R9] = 9,
		[PERF_REG_X86_R10] = 10,
		[PERF_REG_X86_R11] = 11,
		[PERF_REG_X86_R12] = 12,
		[PERF_REG_X86_R13] = 13,
		[PERF_REG_X86_R14] = 14,
		[PERF_REG_X86_R15] = 15,
		[PERF_REG_X86_XMM0] = 17,
		[PERF_REG_X86_XMM1] = 18,
		[PERF_REG_X86_XMM2] = 19,
		[PERF_REG_X86_XMM3] = 20,
		[PERF_REG_X86_XMM4] = 21,
		[PERF_REG_X86_XMM5] = 22,
		[PERF_REG_X86_XMM6] = 23,
		[PERF_REG_X86_XMM7] = 24,
		[PERF_REG_X86_XMM8] = 25,
		[PERF_REG_X86_XMM9] = 26,
		[PERF_REG_X86_XMM10] = 27,
		[PERF_REG_X86_XMM11] = 28,
		[PERF_REG_X86_XMM12] = 29,
		[PERF_REG_X86_XMM13] = 30,
		[PERF_REG_X86_XMM14] = 31,
		[PERF_REG_X86_XMM15] = 32,
	};
	static const int dwarf_x86_64_regnums_apx[] = {
		[PERF_REG_X86_AX] = 0,
		[PERF_REG_X86_BX] = 3,
		[PERF_REG_X86_CX] = 2,
		[PERF_REG_X86_DX] = 1,
		[PERF_REG_X86_SI] = 4,
		[PERF_REG_X86_DI] = 5,
		[PERF_REG_X86_BP] = 6,
		[PERF_REG_X86_SP] = 7,
		[PERF_REG_X86_IP] = 16,
		[PERF_REG_X86_FLAGS] = 49,
		[PERF_REG_X86_CS] = 51,
		[PERF_REG_X86_SS] = 52,
		[PERF_REG_X86_DS] = 53,
		[PERF_REG_X86_ES] = 50,
		[PERF_REG_X86_FS] = 54,
		[PERF_REG_X86_GS] = 55,
		[PERF_REG_X86_R8] = 8,
		[PERF_REG_X86_R9] = 9,
		[PERF_REG_X86_R10] = 10,
		[PERF_REG_X86_R11] = 11,
		[PERF_REG_X86_R12] = 12,
		[PERF_REG_X86_R13] = 13,
		[PERF_REG_X86_R14] = 14,
		[PERF_REG_X86_R15] = 15,
		[PERF_REG_X86_R16] = 130,
		[PERF_REG_X86_R17] = 131,
		[PERF_REG_X86_R18] = 132,
		[PERF_REG_X86_R19] = 133,
		[PERF_REG_X86_R20] = 134,
		[PERF_REG_X86_R21] = 135,
		[PERF_REG_X86_R22] = 136,
		[PERF_REG_X86_R23] = 137,
		[PERF_REG_X86_R24] = 138,
		[PERF_REG_X86_R25] = 139,
		[PERF_REG_X86_R26] = 140,
		[PERF_REG_X86_R27] = 141,
		[PERF_REG_X86_R28] = 142,
		[PERF_REG_X86_R29] = 143,
		[PERF_REG_X86_R30] = 144,
		[PERF_REG_X86_R31] = 145,
	};

	if (perf_regnum == 0)
		return 0;

	if (perf_regnum < 0)
		return -ENOENT;

	if (!(abi & PERF_SAMPLE_REGS_ABI_SIMD) &&
	    (perf_regnum >= (int)ARRAY_SIZE(dwarf_x86_64_regnums) ||
	     dwarf_x86_64_regnums[perf_regnum] == 0))
		return -ENOENT;

	if ((abi & PERF_SAMPLE_REGS_ABI_SIMD) &&
	    (perf_regnum >= (int)ARRAY_SIZE(dwarf_x86_64_regnums_apx) ||
	     dwarf_x86_64_regnums_apx[perf_regnum] == 0))
		return -ENOENT;

	return abi & PERF_SAMPLE_REGS_ABI_SIMD ?
			dwarf_x86_64_regnums_apx[perf_regnum] :
			dwarf_x86_64_regnums[perf_regnum];
}
