/*
 * Copyright (C) 1994 Linus Torvalds
 */

#ifndef __ASM_I386_PROCESSOR_H
#define __ASM_I386_PROCESSOR_H

#include <asm/vm86.h>
#include <asm/math_emu.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/sigcontext.h>
#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/system.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <asm/desc_defs.h>

/*
 * the following now lives in the per cpu area:
 * extern	int cpu_llc_id[NR_CPUS];
 */
DECLARE_PER_CPU(u8, cpu_llc_id);

/*
 * User space process size: 3GB (default).
 */
#define TASK_SIZE	(PAGE_OFFSET)

#define INIT_THREAD  {							\
	.sp0 = sizeof(init_stack) + (long)&init_stack,			\
	.vm86_info = NULL,						\
	.sysenter_cs = __KERNEL_CS,					\
	.io_bitmap_ptr = NULL,						\
	.fs = __KERNEL_PERCPU,						\
}

/*
 * Note that the .io_bitmap member must be extra-big. This is because
 * the CPU will access an additional byte beyond the end of the IO
 * permission bitmap. The extra byte must be all 1 bits, and must
 * be within the limit.
 */
#define INIT_TSS  {							\
	.x86_tss = {							\
		.sp0		= sizeof(init_stack) + (long)&init_stack, \
		.ss0		= __KERNEL_DS,				\
		.ss1		= __KERNEL_CS,				\
		.io_bitmap_base	= INVALID_IO_BITMAP_OFFSET,		\
	 },								\
	.io_bitmap	= { [ 0 ... IO_BITMAP_LONGS] = ~0 },		\
}

#define start_thread(regs, new_eip, new_esp) do {		\
	__asm__("movl %0,%%gs": :"r" (0));			\
	regs->fs = 0;						\
	set_fs(USER_DS);					\
	regs->ds = __USER_DS;					\
	regs->es = __USER_DS;					\
	regs->ss = __USER_DS;					\
	regs->cs = __USER_CS;					\
	regs->ip = new_eip;					\
	regs->sp = new_esp;					\
} while (0)


extern unsigned long thread_saved_pc(struct task_struct *tsk);

#define THREAD_SIZE_LONGS      (THREAD_SIZE/sizeof(unsigned long))
#define KSTK_TOP(info)                                                 \
({                                                                     \
       unsigned long *__ptr = (unsigned long *)(info);                 \
       (unsigned long)(&__ptr[THREAD_SIZE_LONGS]);                     \
})

/*
 * The below -8 is to reserve 8 bytes on top of the ring0 stack.
 * This is necessary to guarantee that the entire "struct pt_regs"
 * is accessable even if the CPU haven't stored the SS/ESP registers
 * on the stack (interrupt gate does not save these registers
 * when switching to the same priv ring).
 * Therefore beware: accessing the ss/esp fields of the
 * "struct pt_regs" is possible, but they may contain the
 * completely wrong values.
 */
#define task_pt_regs(task)                                             \
({                                                                     \
       struct pt_regs *__regs__;                                       \
       __regs__ = (struct pt_regs *)(KSTK_TOP(task_stack_page(task))-8); \
       __regs__ - 1;                                                   \
})

#define KSTK_ESP(task) (task_pt_regs(task)->sp)

#endif /* __ASM_I386_PROCESSOR_H */
