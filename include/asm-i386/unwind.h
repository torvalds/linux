#ifndef _ASM_I386_UNWIND_H
#define _ASM_I386_UNWIND_H

/*
 * Copyright (C) 2002-2006 Novell, Inc.
 *	Jan Beulich <jbeulich@novell.com>
 * This code is released under version 2 of the GNU GPL.
 */

#ifdef CONFIG_STACK_UNWIND

#include <linux/sched.h>
#include <asm/fixmap.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

struct unwind_frame_info
{
	struct pt_regs regs;
	struct task_struct *task;
	unsigned call_frame:1;
};

#define UNW_PC(frame)        (frame)->regs.eip
#define UNW_SP(frame)        (frame)->regs.esp
#ifdef CONFIG_FRAME_POINTER
#define UNW_FP(frame)        (frame)->regs.ebp
#define FRAME_RETADDR_OFFSET 4
#define FRAME_LINK_OFFSET    0
#define STACK_BOTTOM(tsk)    STACK_LIMIT((tsk)->thread.esp0)
#define STACK_TOP(tsk)       ((tsk)->thread.esp0)
#else
#define UNW_FP(frame) ((void)(frame), 0)
#endif
#define STACK_LIMIT(ptr)     (((ptr) - 1) & ~(THREAD_SIZE - 1))

#define UNW_REGISTER_INFO \
	PTREGS_INFO(eax), \
	PTREGS_INFO(ecx), \
	PTREGS_INFO(edx), \
	PTREGS_INFO(ebx), \
	PTREGS_INFO(esp), \
	PTREGS_INFO(ebp), \
	PTREGS_INFO(esi), \
	PTREGS_INFO(edi), \
	PTREGS_INFO(eip)

#define UNW_DEFAULT_RA(raItem, dataAlign) \
	((raItem).where == Memory && \
	 !((raItem).value * (dataAlign) + 4))

static inline void arch_unw_init_frame_info(struct unwind_frame_info *info,
                                            /*const*/ struct pt_regs *regs)
{
	if (user_mode_vm(regs))
		info->regs = *regs;
	else {
		memcpy(&info->regs, regs, offsetof(struct pt_regs, esp));
		info->regs.esp = (unsigned long)&regs->esp;
		info->regs.xss = __KERNEL_DS;
	}
}

static inline void arch_unw_init_blocked(struct unwind_frame_info *info)
{
	memset(&info->regs, 0, sizeof(info->regs));
	info->regs.eip = info->task->thread.eip;
	info->regs.xcs = __KERNEL_CS;
	__get_user(info->regs.ebp, (long *)info->task->thread.esp);
	info->regs.esp = info->task->thread.esp;
	info->regs.xss = __KERNEL_DS;
	info->regs.xds = __USER_DS;
	info->regs.xes = __USER_DS;
	info->regs.xgs = __KERNEL_PDA;
}

extern asmlinkage int arch_unwind_init_running(struct unwind_frame_info *,
                                               asmlinkage int (*callback)(struct unwind_frame_info *,
                                                                          void *arg),
                                               void *arg);

static inline int arch_unw_user_mode(/*const*/ struct unwind_frame_info *info)
{
	return user_mode_vm(&info->regs)
	       || info->regs.eip < PAGE_OFFSET
	       || (info->regs.eip >= __fix_to_virt(FIX_VDSO)
	           && info->regs.eip < __fix_to_virt(FIX_VDSO) + PAGE_SIZE)
	       || info->regs.esp < PAGE_OFFSET;
}

#else

#define UNW_PC(frame) ((void)(frame), 0)
#define UNW_SP(frame) ((void)(frame), 0)
#define UNW_FP(frame) ((void)(frame), 0)

static inline int arch_unw_user_mode(const void *info)
{
	return 0;
}

#endif

#endif /* _ASM_I386_UNWIND_H */
