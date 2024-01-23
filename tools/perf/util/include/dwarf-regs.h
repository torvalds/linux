/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_DWARF_REGS_H_
#define _PERF_DWARF_REGS_H_

#define DWARF_REG_PC  0xd3af9c /* random number */
#define DWARF_REG_FB  0xd3affb /* random number */

#ifdef HAVE_DWARF_SUPPORT
const char *get_arch_regstr(unsigned int n);
/*
 * get_dwarf_regstr - Returns ftrace register string from DWARF regnum
 * n: DWARF register number
 * machine: ELF machine signature (EM_*)
 */
const char *get_dwarf_regstr(unsigned int n, unsigned int machine);

int get_arch_regnum(const char *name);
/*
 * get_dwarf_regnum - Returns DWARF regnum from register name
 * name: architecture register name
 * machine: ELF machine signature (EM_*)
 */
int get_dwarf_regnum(const char *name, unsigned int machine);

#else /* HAVE_DWARF_SUPPORT */

static inline int get_dwarf_regnum(const char *name __maybe_unused,
				   unsigned int machine __maybe_unused)
{
	return -1;
}
#endif

#ifdef HAVE_ARCH_REGS_QUERY_REGISTER_OFFSET
/*
 * Arch should support fetching the offset of a register in pt_regs
 * by its name. See kernel's regs_query_register_offset in
 * arch/xxx/kernel/ptrace.c.
 */
int regs_query_register_offset(const char *name);
#endif
#endif
