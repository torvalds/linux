#ifndef _ASM_X86_64_UNWIND_H
#define _ASM_X86_64_UNWIND_H

/*
 * Copyright (C) 2002-2006 Novell, Inc.
 *	Jan Beulich <jbeulich@novell.com>
 * This code is released under version 2 of the GNU GPL.
 */

#ifdef CONFIG_STACK_UNWIND

#include <linux/sched.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>
#include <asm/vsyscall.h>

struct unwind_frame_info
{
	struct pt_regs regs;
	struct task_struct *task;
};

#define UNW_PC(frame)        (frame)->regs.rip
#define UNW_SP(frame)        (frame)->regs.rsp
#ifdef CONFIG_FRAME_POINTER
#define UNW_FP(frame)        (frame)->regs.rbp
#define FRAME_RETADDR_OFFSET 8
#define FRAME_LINK_OFFSET    0
#define STACK_BOTTOM(tsk)    (((tsk)->thread.rsp0 - 1) & ~(THREAD_SIZE - 1))
#define STACK_TOP(tsk)       ((tsk)->thread.rsp0)
#endif
/* Might need to account for the special exception and interrupt handling
   stacks here, since normally
	EXCEPTION_STACK_ORDER < THREAD_ORDER < IRQSTACK_ORDER,
   but the construct is needed only for getting across the stack switch to
   the interrupt stack - thus considering the IRQ stack itself is unnecessary,
   and the overhead of comparing against all exception handling stacks seems
   not desirable. */
#define STACK_LIMIT(ptr)     (((ptr) - 1) & ~(THREAD_SIZE - 1))

#define UNW_REGISTER_INFO \
	PTREGS_INFO(rax), \
	PTREGS_INFO(rdx), \
	PTREGS_INFO(rcx), \
	PTREGS_INFO(rbx), \
	PTREGS_INFO(rsi), \
	PTREGS_INFO(rdi), \
	PTREGS_INFO(rbp), \
	PTREGS_INFO(rsp), \
	PTREGS_INFO(r8), \
	PTREGS_INFO(r9), \
	PTREGS_INFO(r10), \
	PTREGS_INFO(r11), \
	PTREGS_INFO(r12), \
	PTREGS_INFO(r13), \
	PTREGS_INFO(r14), \
	PTREGS_INFO(r15), \
	PTREGS_INFO(rip)

static inline void arch_unw_init_frame_info(struct unwind_frame_info *info,
                                            /*const*/ struct pt_regs *regs)
{
	info->regs = *regs;
}

static inline void arch_unw_init_blocked(struct unwind_frame_info *info)
{
	extern const char thread_return[];

	memset(&info->regs, 0, sizeof(info->regs));
	info->regs.rip = (unsigned long)thread_return;
	info->regs.cs = __KERNEL_CS;
	__get_user(info->regs.rbp, (unsigned long *)info->task->thread.rsp);
	info->regs.rsp = info->task->thread.rsp;
	info->regs.ss = __KERNEL_DS;
}

extern int arch_unwind_init_running(struct unwind_frame_info *,
                                    int (*callback)(struct unwind_frame_info *,
                                                    void *arg),
                                    void *arg);

static inline int arch_unw_user_mode(const struct unwind_frame_info *info)
{
#if 0 /* This can only work when selector register saves/restores
         are properly annotated (and tracked in UNW_REGISTER_INFO). */
	return user_mode(&info->regs);
#else
	return (long)info->regs.rip >= 0
	       || (info->regs.rip >= VSYSCALL_START && info->regs.rip < VSYSCALL_END)
	       || (long)info->regs.rsp >= 0;
#endif
}

#else

#define UNW_PC(frame) ((void)(frame), 0)
#define UNW_SP(frame) ((void)(frame), 0)

static inline int arch_unw_user_mode(const void *info)
{
	return 0;
}

#endif

#endif /* _ASM_X86_64_UNWIND_H */
