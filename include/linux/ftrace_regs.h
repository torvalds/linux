/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FTRACE_REGS_H
#define _LINUX_FTRACE_REGS_H

/*
 * For archs that just copy pt_regs in ftrace regs, it can use this default.
 * If an architecture does not use pt_regs, it must define all the below
 * accessor functions.
 */
#ifndef HAVE_ARCH_FTRACE_REGS
struct __arch_ftrace_regs {
	struct pt_regs		regs;
};

#define arch_ftrace_regs(fregs) ((struct __arch_ftrace_regs *)(fregs))

struct ftrace_regs;

#define ftrace_regs_get_instruction_pointer(fregs) \
	instruction_pointer(&arch_ftrace_regs(fregs)->regs)
#define ftrace_regs_get_argument(fregs, n) \
	regs_get_kernel_argument(&arch_ftrace_regs(fregs)->regs, n)
#define ftrace_regs_get_stack_pointer(fregs) \
	kernel_stack_pointer(&arch_ftrace_regs(fregs)->regs)
#define ftrace_regs_get_return_value(fregs) \
	regs_return_value(&arch_ftrace_regs(fregs)->regs)
#define ftrace_regs_set_return_value(fregs, ret) \
	regs_set_return_value(&arch_ftrace_regs(fregs)->regs, ret)
#define ftrace_override_function_with_return(fregs) \
	override_function_with_return(&arch_ftrace_regs(fregs)->regs)
#define ftrace_regs_query_register_offset(name) \
	regs_query_register_offset(name)

#endif /* HAVE_ARCH_FTRACE_REGS */

#endif /* _LINUX_FTRACE_REGS_H */
