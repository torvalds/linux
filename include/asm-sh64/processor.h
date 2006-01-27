#ifndef __ASM_SH64_PROCESSOR_H
#define __ASM_SH64_PROCESSOR_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/processor.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 * Copyright (C) 2004  Richard Curnow
 *
 */

#include <asm/page.h>

#ifndef __ASSEMBLY__

#include <asm/types.h>
#include <asm/cache.h>
#include <asm/registers.h>
#include <linux/threads.h>

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ \
void *pc; \
unsigned long long __dummy = 0; \
__asm__("gettr	tr0, %1\n\t" \
	"pta	4, tr0\n\t" \
	"gettr	tr0, %0\n\t" \
	"ptabs	%1, tr0\n\t"	\
	:"=r" (pc), "=r" (__dummy) \
	: "1" (__dummy)); \
pc; })

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 */
enum cpu_type {
	CPU_SH5_101,
	CPU_SH5_103,
	CPU_SH_NONE
};

/*
 * TLB information structure
 *
 * Defined for both I and D tlb, per-processor.
 */
struct tlb_info {
	unsigned long long next;
	unsigned long long first;
	unsigned long long last;

	unsigned int entries;
	unsigned int step;

	unsigned long flags;
};

struct sh_cpuinfo {
	enum cpu_type type;
	unsigned long loops_per_jiffy;

	char	hard_math;

	unsigned long *pgd_quick;
	unsigned long *pmd_quick;
	unsigned long *pte_quick;
	unsigned long pgtable_cache_sz;
	unsigned int cpu_clock, master_clock, bus_clock, module_clock;

	/* Cache info */
	struct cache_info icache;
	struct cache_info dcache;

	/* TLB info */
	struct tlb_info itlb;
	struct tlb_info dtlb;
};

extern struct sh_cpuinfo boot_cpu_data;

#define cpu_data (&boot_cpu_data)
#define current_cpu_data boot_cpu_data

#endif

/*
 * User space process size: 2GB - 4k.
 */
#define TASK_SIZE	0x7ffff000UL

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(TASK_SIZE / 3)

/*
 * Bit of SR register
 *
 * FD-bit:
 *     When it's set, it means the processor doesn't have right to use FPU,
 *     and it results exception when the floating operation is executed.
 *
 * IMASK-bit:
 *     Interrupt level mask
 *
 * STEP-bit:
 *     Single step bit
 *
 */
#define SR_FD    0x00008000

#if defined(CONFIG_SH64_SR_WATCH)
#define SR_MMU   0x84000000
#else
#define SR_MMU   0x80000000
#endif

#define SR_IMASK 0x000000f0
#define SR_SSTEP 0x08000000

#ifndef __ASSEMBLY__

/*
 * FPU structure and data : require 8-byte alignment as we need to access it
   with fld.p, fst.p
 */

struct sh_fpu_hard_struct {
	unsigned long fp_regs[64];
	unsigned int fpscr;
	/* long status; * software status information */
};

#if 0
/* Dummy fpu emulator  */
struct sh_fpu_soft_struct {
	unsigned long long fp_regs[32];
	unsigned int fpscr;
	unsigned char lookahead;
	unsigned long entry_pc;
};
#endif

union sh_fpu_union {
	struct sh_fpu_hard_struct hard;
	/* 'hard' itself only produces 32 bit alignment, yet we need
	   to access it using 64 bit load/store as well. */
	unsigned long long alignment_dummy;
};

struct thread_struct {
	unsigned long sp;
	unsigned long pc;
	/* This stores the address of the pt_regs built during a context
	   switch, or of the register save area built for a kernel mode
	   exception.  It is used for backtracing the stack of a sleeping task
	   or one that traps in kernel mode. */
        struct pt_regs *kregs;
	/* This stores the address of the pt_regs constructed on entry from
	   user mode.  It is a fixed value over the lifetime of a process, or
	   NULL for a kernel thread. */
	struct pt_regs *uregs;

	unsigned long trap_no, error_code;
	unsigned long address;
	/* Hardware debugging registers may come here */

	/* floating point info */
	union sh_fpu_union fpu;
};

#define INIT_MMAP \
{ &init_mm, 0, 0, NULL, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL }

extern  struct pt_regs fake_swapper_regs;

#define INIT_THREAD  {				\
	.sp		= sizeof(init_stack) +	\
			  (long) &init_stack,	\
	.pc		= 0,			\
        .kregs		= &fake_swapper_regs,	\
	.uregs	        = NULL,			\
	.trap_no	= 0,			\
	.error_code	= 0,			\
	.address	= 0,			\
	.fpu		= { { { 0, } }, }	\
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
#define SR_USER (SR_MMU | SR_FD)

#define start_thread(regs, new_pc, new_sp) 	 		\
	set_fs(USER_DS);			 		\
	regs->sr = SR_USER;	/* User mode. */ 		\
	regs->pc = new_pc - 4;	/* Compensate syscall exit */	\
	regs->pc |= 1;		/* Set SHmedia ! */		\
	regs->regs[18] = 0;   		 	 		\
	regs->regs[15] = new_sp

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);
/*
 * create a kernel thread without removing it from tasklists
 */
extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);


/* Copy and release all segment info associated with a VM */
#define copy_segments(p, mm)	do { } while (0)
#define release_segments(mm)	do { } while (0)
#define forget_segments()	do { } while (0)
#define prepare_to_copy(tsk)	do { } while (0)
/*
 * FPU lazy state save handling.
 */

static inline void release_fpu(void)
{
	unsigned long long __dummy;

	/* Set FD flag in SR */
	__asm__ __volatile__("getcon	" __SR ", %0\n\t"
			     "or	%0, %1, %0\n\t"
			     "putcon	%0, " __SR "\n\t"
			     : "=&r" (__dummy)
			     : "r" (SR_FD));
}

static inline void grab_fpu(void)
{
	unsigned long long __dummy;

	/* Clear out FD flag in SR */
	__asm__ __volatile__("getcon	" __SR ", %0\n\t"
			     "and	%0, %1, %0\n\t"
			     "putcon	%0, " __SR "\n\t"
			     : "=&r" (__dummy)
			     : "r" (~SR_FD));
}

/* Round to nearest, no exceptions on inexact, overflow, underflow,
   zero-divide, invalid.  Configure option for whether to flush denorms to
   zero, or except if a denorm is encountered.  */
#if defined(CONFIG_SH64_FPU_DENORM_FLUSH)
#define FPSCR_INIT  0x00040000
#else
#define FPSCR_INIT  0x00000000
#endif

/* Save the current FP regs */
void fpsave(struct sh_fpu_hard_struct *fpregs);

/* Initialise the FP state of a task */
void fpinit(struct sh_fpu_hard_struct *fpregs);

extern struct task_struct *last_task_used_math;

/*
 * Return saved PC of a blocked thread.
 */
#define thread_saved_pc(tsk)	(tsk->thread.pc)

extern unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)  ((tsk)->thread.pc)
#define KSTK_ESP(tsk)  ((tsk)->thread.sp)

#define cpu_relax()	do { } while (0)

#endif	/* __ASSEMBLY__ */
#endif /* __ASM_SH64_PROCESSOR_H */

