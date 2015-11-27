#ifndef _PERF_DWARF_REGS_H_
#define _PERF_DWARF_REGS_H_

#ifdef HAVE_DWARF_SUPPORT
const char *get_arch_regstr(unsigned int n);
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
