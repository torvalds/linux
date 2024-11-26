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

#define __get_dwarf_regstr(tbl, n) (((n) < ARRAY_SIZE(tbl)) ? (tbl)[(n)] : NULL)

/* Return architecture dependent register string (for kprobe-tracer) */
const char *get_dwarf_regstr(unsigned int n, unsigned int machine, unsigned int flags)
{
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
		return get_csky_regstr(n, flags);
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
}

#if EM_HOST != EM_X86_64 && EM_HOST != EM_386
__weak int get_arch_regnum(const char *name __maybe_unused)
{
	return -ENOTSUP;
}
#endif

/* Return DWARF register number from architecture register name */
int get_dwarf_regnum(const char *name, unsigned int machine, unsigned int flags __maybe_unused)
{
	char *regname = strdup(name);
	int reg = -1;
	char *p;

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
#if EM_HOST != EM_X86_64 && EM_HOST != EM_386
	case EM_HOST:
		reg = get_arch_regnum(regname);
		break;
#endif
	case EM_X86_64:
		fallthrough;
	case EM_386:
		reg = get_x86_regnum(regname);
		break;
	default:
		pr_err("ELF MACHINE %x is not supported.\n", machine);
	}
	free(regname);
	return reg;
}
