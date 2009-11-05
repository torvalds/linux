#ifndef _LINUX_HW_BREAKPOINT_H
#define _LINUX_HW_BREAKPOINT_H


#ifdef	__KERNEL__
#include <linux/list.h>
#include <linux/types.h>
#include <linux/kallsyms.h>

/**
 * struct hw_breakpoint - unified kernel/user-space hardware breakpoint
 * @triggered: callback invoked after target address access
 * @info: arch-specific breakpoint info (address, length, and type)
 *
 * %hw_breakpoint structures are the kernel's way of representing
 * hardware breakpoints.  These are data breakpoints
 * (also known as "watchpoints", triggered on data access), and the breakpoint's
 * target address can be located in either kernel space or user space.
 *
 * The breakpoint's address, length, and type are highly
 * architecture-specific.  The values are encoded in the @info field; you
 * specify them when registering the breakpoint.  To examine the encoded
 * values use hw_breakpoint_get_{kaddress,uaddress,len,type}(), declared
 * below.
 *
 * The address is specified as a regular kernel pointer (for kernel-space
 * breakponts) or as an %__user pointer (for user-space breakpoints).
 * With register_user_hw_breakpoint(), the address must refer to a
 * location in user space.  The breakpoint will be active only while the
 * requested task is running.  Conversely with
 * register_kernel_hw_breakpoint(), the address must refer to a location
 * in kernel space, and the breakpoint will be active on all CPUs
 * regardless of the current task.
 *
 * The length is the breakpoint's extent in bytes, which is subject to
 * certain limitations.  include/asm/hw_breakpoint.h contains macros
 * defining the available lengths for a specific architecture.  Note that
 * the address's alignment must match the length.  The breakpoint will
 * catch accesses to any byte in the range from address to address +
 * (length - 1).
 *
 * The breakpoint's type indicates the sort of access that will cause it
 * to trigger.  Possible values may include:
 *
 * 	%HW_BREAKPOINT_RW (triggered on read or write access),
 * 	%HW_BREAKPOINT_WRITE (triggered on write access), and
 * 	%HW_BREAKPOINT_READ (triggered on read access).
 *
 * Appropriate macros are defined in include/asm/hw_breakpoint.h; not all
 * possibilities are available on all architectures.  Execute breakpoints
 * must have length equal to the special value %HW_BREAKPOINT_LEN_EXECUTE.
 *
 * When a breakpoint gets hit, the @triggered callback is
 * invoked in_interrupt with a pointer to the %hw_breakpoint structure and the
 * processor registers.
 * Data breakpoints occur after the memory access has taken place.
 * Breakpoints are disabled during execution @triggered, to avoid
 * recursive traps and allow unhindered access to breakpointed memory.
 *
 * This sample code sets a breakpoint on pid_max and registers a callback
 * function for writes to that variable.  Note that it is not portable
 * as written, because not all architectures support HW_BREAKPOINT_LEN_4.
 *
 * ----------------------------------------------------------------------
 *
 * #include <asm/hw_breakpoint.h>
 *
 * struct hw_breakpoint my_bp;
 *
 * static void my_triggered(struct hw_breakpoint *bp, struct pt_regs *regs)
 * {
 * 	printk(KERN_DEBUG "Inside triggered routine of breakpoint exception\n");
 * 	dump_stack();
 *  	.......<more debugging output>........
 * }
 *
 * static struct hw_breakpoint my_bp;
 *
 * static int init_module(void)
 * {
 *	..........<do anything>............
 *	my_bp.info.type = HW_BREAKPOINT_WRITE;
 *	my_bp.info.len = HW_BREAKPOINT_LEN_4;
 *
 *	my_bp.installed = (void *)my_bp_installed;
 *
 *	rc = register_kernel_hw_breakpoint(&my_bp);
 *	..........<do anything>............
 * }
 *
 * static void cleanup_module(void)
 * {
 *	..........<do anything>............
 *	unregister_kernel_hw_breakpoint(&my_bp);
 *	..........<do anything>............
 * }
 *
 * ----------------------------------------------------------------------
 */
struct hw_breakpoint {
	void (*triggered)(struct hw_breakpoint *, struct pt_regs *);
	struct arch_hw_breakpoint info;
};

/*
 * len and type values are defined in include/asm/hw_breakpoint.h.
 * Available values vary according to the architecture.  On i386 the
 * possibilities are:
 *
 *	HW_BREAKPOINT_LEN_1
 *	HW_BREAKPOINT_LEN_2
 *	HW_BREAKPOINT_LEN_4
 *	HW_BREAKPOINT_RW
 *	HW_BREAKPOINT_READ
 *
 * On other architectures HW_BREAKPOINT_LEN_8 may be available, and the
 * 1-, 2-, and 4-byte lengths may be unavailable.  There also may be
 * HW_BREAKPOINT_WRITE.  You can use #ifdef to check at compile time.
 */

extern int register_user_hw_breakpoint(struct task_struct *tsk,
					struct hw_breakpoint *bp);
extern int modify_user_hw_breakpoint(struct task_struct *tsk,
					struct hw_breakpoint *bp);
extern void unregister_user_hw_breakpoint(struct task_struct *tsk,
						struct hw_breakpoint *bp);
/*
 * Kernel breakpoints are not associated with any particular thread.
 */
extern int register_kernel_hw_breakpoint(struct hw_breakpoint *bp);
extern void unregister_kernel_hw_breakpoint(struct hw_breakpoint *bp);

extern unsigned int hbp_kernel_pos;

#endif	/* __KERNEL__ */
#endif	/* _LINUX_HW_BREAKPOINT_H */
