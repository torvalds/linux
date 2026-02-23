// SPDX-License-Identifier: GPL-2.0
/*
 * dwarf-regs.c : Mapping of DWARF debug register numbers into register names.
 *
 * Written by: Masami Hiramatsu <mhiramat@kernel.org>
 */

#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <dwarf-regs.h>
#include <elf.h>
#include <errno.h>
#include <linux/kernel.h>

/* Define const char * {arch}_register_tbl[] */
#define DEFINE_DWARF_REGSTR_TABLE
#include "../arch/x86/include/dwarf-regs-table.h"
#include "../arch/arm/include/dwarf-regs-table.h"
#include "../arch/arm64/include/dwarf-regs-table.h"
#include "../arch/sh/include/dwarf-regs-table.h"
#include "../arch/powerpc/include/dwarf-regs-table.h"
#include "../arch/riscv/include/dwarf-regs-table.h"
#include "../arch/s390/include/dwarf-regs-table.h"
#include "../arch/sparc/include/dwarf-regs-table.h"
#include "../arch/xtensa/include/dwarf-regs-table.h"
#include "../arch/mips/include/dwarf-regs-table.h"
#include "../arch/loongarch/include/dwarf-regs-table.h"

/* Return architecture dependent register string (for kprobe-tracer) */
const char *get_dwarf_regstr(unsigned int n, unsigned int machine, unsigned int flags)
{
	#define __get_dwarf_regstr(tbl, n) (((n) < ARRAY_SIZE(tbl)) ? (tbl)[(n)] : NULL)

	if (machine == EM_NONE) {
		/* Generic arch - use host arch */
		machine = EM_HOST;
	}
	switch (machine) {
	case EM_386:
		return __get_dwarf_regstr(x86_32_regstr_tbl, n);
	case EM_X86_64:
		return __get_dwarf_regstr(x86_64_regstr_tbl, n);
	case EM_ARM:
		return __get_dwarf_regstr(arm_regstr_tbl, n);
	case EM_AARCH64:
		return __get_dwarf_regstr(aarch64_regstr_tbl, n);
	case EM_CSKY:
		return __get_csky_regstr(n, flags);
	case EM_SH:
		return __get_dwarf_regstr(sh_regstr_tbl, n);
	case EM_S390:
		return __get_dwarf_regstr(s390_regstr_tbl, n);
	case EM_PPC:
	case EM_PPC64:
		return __get_dwarf_regstr(powerpc_regstr_tbl, n);
	case EM_RISCV:
		return __get_dwarf_regstr(riscv_regstr_tbl, n);
	case EM_SPARC:
	case EM_SPARCV9:
		return __get_dwarf_regstr(sparc_regstr_tbl, n);
	case EM_XTENSA:
		return __get_dwarf_regstr(xtensa_regstr_tbl, n);
	case EM_MIPS:
		return __get_dwarf_regstr(mips_regstr_tbl, n);
	case EM_LOONGARCH:
		return __get_dwarf_regstr(loongarch_regstr_tbl, n);
	default:
		pr_err("ELF MACHINE %x is not supported.\n", machine);
	}
	return NULL;

	#undef __get_dwarf_regstr
}

static int __get_dwarf_regnum(const char *const *regstr, size_t num_regstr, const char *name)
{
	for (size_t i = 0; i < num_regstr; i++) {
		if (regstr[i] && !strcmp(regstr[i], name))
			return i;
	}
	return -ENOENT;
}

/* Return DWARF register number from architecture register name */
int get_dwarf_regnum(const char *name, unsigned int machine, unsigned int flags)
{
	char *regname = strdup(name);
	int reg = -1;
	char *p;

	#define _get_dwarf_regnum(tbl, name) __get_dwarf_regnum(tbl, ARRAY_SIZE(tbl), name)

	if (regname == NULL)
		return -EINVAL;

	/* For convenience, remove trailing characters */
	p = strpbrk(regname, " ,)");
	if (p)
		*p = '\0';

	if (machine == EM_NONE) {
		/* Generic arch - use host arch */
		machine = EM_HOST;
	}
	switch (machine) {
	case EM_X86_64:
		reg = __get_dwarf_regnum_x86_64(name);
		break;
	case EM_386:
		reg = __get_dwarf_regnum_i386(name);
		break;
	case EM_ARM:
		reg = _get_dwarf_regnum(arm_regstr_tbl, name);
		break;
	case EM_AARCH64:
		reg = _get_dwarf_regnum(aarch64_regstr_tbl, name);
		break;
	case EM_CSKY:
		reg = __get_csky_regnum(name, flags);
		break;
	case EM_SH:
		reg = _get_dwarf_regnum(sh_regstr_tbl, name);
		break;
	case EM_S390:
		reg = _get_dwarf_regnum(s390_regstr_tbl, name);
		break;
	case EM_PPC:
	case EM_PPC64:
		reg = _get_dwarf_regnum(powerpc_regstr_tbl, name);
		break;
	case EM_RISCV:
		reg = _get_dwarf_regnum(riscv_regstr_tbl, name);
		break;
	case EM_SPARC:
	case EM_SPARCV9:
		reg = _get_dwarf_regnum(sparc_regstr_tbl, name);
		break;
	case EM_XTENSA:
		reg = _get_dwarf_regnum(xtensa_regstr_tbl, name);
		break;
	case EM_MIPS:
		reg = _get_dwarf_regnum(mips_regstr_tbl, name);
		break;
	case EM_LOONGARCH:
		reg = _get_dwarf_regnum(loongarch_regstr_tbl, name);
		break;
	default:
		pr_err("ELF MACHINE %x is not supported.\n", machine);
	}
	free(regname);
	return reg;

	#undef _get_dwarf_regnum
}

static int get_libdw_frame_nregs(unsigned int machine, unsigned int flags __maybe_unused)
{
	switch (machine) {
	case EM_X86_64:
		return 17;
	case EM_386:
		return 9;
	case EM_ARM:
		return 16;
	case EM_AARCH64:
		return 97;
	case EM_CSKY:
		return 38;
	case EM_S390:
		return 32;
	case EM_PPC:
	case EM_PPC64:
		return 145;
	case EM_RISCV:
		return 66;
	case EM_SPARC:
	case EM_SPARCV9:
		return 103;
	case EM_LOONGARCH:
		return 74;
	case EM_MIPS:
		return 71;
	default:
		return 0;
	}
}

int get_dwarf_regnum_for_perf_regnum(int perf_regnum, unsigned int machine,
				     unsigned int flags, bool only_libdw_supported)
{
	int reg;

	switch (machine) {
	case EM_X86_64:
		reg = __get_dwarf_regnum_for_perf_regnum_x86_64(perf_regnum);
		break;
	case EM_386:
		reg = __get_dwarf_regnum_for_perf_regnum_i386(perf_regnum);
		break;
	case EM_ARM:
		reg = __get_dwarf_regnum_for_perf_regnum_arm(perf_regnum);
		break;
	case EM_AARCH64:
		reg = __get_dwarf_regnum_for_perf_regnum_arm64(perf_regnum);
		break;
	case EM_CSKY:
		reg = __get_dwarf_regnum_for_perf_regnum_csky(perf_regnum, flags);
		break;
	case EM_PPC:
	case EM_PPC64:
		reg = __get_dwarf_regnum_for_perf_regnum_powerpc(perf_regnum);
		break;
	case EM_RISCV:
		reg = __get_dwarf_regnum_for_perf_regnum_riscv(perf_regnum);
		break;
	case EM_S390:
		reg = __get_dwarf_regnum_for_perf_regnum_s390(perf_regnum);
		break;
	case EM_LOONGARCH:
		reg = __get_dwarf_regnum_for_perf_regnum_loongarch(perf_regnum);
		break;
	case EM_MIPS:
		reg = __get_dwarf_regnum_for_perf_regnum_mips(perf_regnum);
		break;
	default:
		pr_err("ELF MACHINE %x is not supported.\n", machine);
		return -ENOENT;
	}
	if (reg >= 0 && only_libdw_supported) {
		int nregs = get_libdw_frame_nregs(machine, flags);

		if (reg >= nregs)
			reg = -ENOENT;
	}
	return reg;
}
