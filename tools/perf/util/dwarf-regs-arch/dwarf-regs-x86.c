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
	{ "xmm0", 17},
	{ "xmm1", 18},
	{ "xmm2", 19},
	{ "xmm3", 20},
	{ "xmm4", 21},
	{ "xmm5", 22},
	{ "xmm6", 23},
	{ "xmm7", 24},
	{ "xmm8", 25},
	{ "xmm9", 26},
	{ "xmm10", 27},
	{ "xmm11", 28},
	{ "xmm12", 29},
	{ "xmm13", 30},
	{ "xmm14", 31},
	{ "xmm15", 32},
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
	// 56-47 - reserved
	{ "fs.base", 58},
	{ "gs.base", 59},
	// 60-61 - reserved
	{ "tr", 62}, // Task Register
	{ "ldtr", 63}, // LDT Register
	{ "mxcsr", 64}, // 128-bit Media Control and Status
	{ "fcw", 65}, // x87 Control Word
	{ "fsw", 66}, // x87 Status Word
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

	if (perf_regnum <  0 || perf_regnum > (int)ARRAY_SIZE(dwarf_i386_regnums) ||
	    dwarf_i386_regnums[perf_regnum] == 0)
		return -ENOENT;

	return dwarf_i386_regnums[perf_regnum];
}

int __get_dwarf_regnum_for_perf_regnum_x86_64(int perf_regnum)
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

	if (perf_regnum == 0)
		return 0;

	if (perf_regnum <  0 || perf_regnum > (int)ARRAY_SIZE(dwarf_x86_64_regnums) ||
	    dwarf_x86_64_regnums[perf_regnum] == 0)
		return -ENOENT;

	return dwarf_x86_64_regnums[perf_regnum];
}
