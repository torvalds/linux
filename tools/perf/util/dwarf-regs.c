/*
 * dwarf-regs.c : Mapping of DWARF debug register numbers into register names.
 *
 * Written by: Masami Hiramatsu <mhiramat@kernel.org>
 */

#include <util.h>
#include <debug.h>
#include <dwarf-regs.h>
#include <elf.h>

#ifndef EM_AARCH64
#define EM_AARCH64	183  /* ARM 64 bit */
#endif

/* Define const char * {arch}_register_tbl[] */
#define DEFINE_DWARF_REGSTR_TABLE
#include "../arch/x86/include/dwarf-regs-table.h"
#include "../arch/arm/include/dwarf-regs-table.h"
#include "../arch/arm64/include/dwarf-regs-table.h"
#include "../arch/sh/include/dwarf-regs-table.h"
#include "../arch/powerpc/include/dwarf-regs-table.h"
#include "../arch/s390/include/dwarf-regs-table.h"
#include "../arch/sparc/include/dwarf-regs-table.h"
#include "../arch/xtensa/include/dwarf-regs-table.h"

#define __get_dwarf_regstr(tbl, n) (((n) < ARRAY_SIZE(tbl)) ? (tbl)[(n)] : NULL)

/* Return architecture dependent register string (for kprobe-tracer) */
const char *get_dwarf_regstr(unsigned int n, unsigned int machine)
{
	switch (machine) {
	case EM_NONE:	/* Generic arch - use host arch */
		return get_arch_regstr(n);
	case EM_386:
		return __get_dwarf_regstr(x86_32_regstr_tbl, n);
	case EM_X86_64:
		return __get_dwarf_regstr(x86_64_regstr_tbl, n);
	case EM_ARM:
		return __get_dwarf_regstr(arm_regstr_tbl, n);
	case EM_AARCH64:
		return __get_dwarf_regstr(aarch64_regstr_tbl, n);
	case EM_SH:
		return __get_dwarf_regstr(sh_regstr_tbl, n);
	case EM_S390:
		return __get_dwarf_regstr(s390_regstr_tbl, n);
	case EM_PPC:
	case EM_PPC64:
		return __get_dwarf_regstr(powerpc_regstr_tbl, n);
	case EM_SPARC:
	case EM_SPARCV9:
		return __get_dwarf_regstr(sparc_regstr_tbl, n);
	case EM_XTENSA:
		return __get_dwarf_regstr(xtensa_regstr_tbl, n);
	default:
		pr_err("ELF MACHINE %x is not supported.\n", machine);
	}
	return NULL;
}
