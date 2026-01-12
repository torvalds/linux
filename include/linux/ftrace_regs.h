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
#define ftrace_regs_get_frame_pointer(fregs) \
	frame_pointer(&arch_ftrace_regs(fregs)->regs)

static __always_inline void
ftrace_partial_regs_update(struct ftrace_regs *fregs, struct pt_regs *regs) { }

#else

/*
 * ftrace_partial_regs_update - update the original ftrace_regs from regs
 * @fregs: The ftrace_regs to update from @regs
 * @regs: The partial regs from ftrace_partial_regs() that was updated
 *
 * Some architectures have the partial regs living in the ftrace_regs
 * structure, whereas other architectures need to make a different copy
 * of the @regs. If a partial @regs is retrieved by ftrace_partial_regs() and
 * if the code using @regs updates a field (like the instruction pointer or
 * stack pointer) it may need to propagate that change to the original @fregs
 * it retrieved the partial @regs from. Use this function to guarantee that
 * update happens.
 */
static __always_inline void
ftrace_partial_regs_update(struct ftrace_regs *fregs, struct pt_regs *regs)
{
	ftrace_regs_set_instruction_pointer(fregs, instruction_pointer(regs));
	ftrace_regs_set_return_value(fregs, regs_return_value(regs));
}

#endif /* HAVE_ARCH_FTRACE_REGS */

/* This can be overridden by the architectures */
#ifndef FTRACE_REGS_MAX_ARGS
# define FTRACE_REGS_MAX_ARGS	6
#endif

#endif /* _LINUX_FTRACE_REGS_H */
