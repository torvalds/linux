/*
 * include/asm-sh/processor.h
 *
 * Copyright (C) 1999, 2000  Niibe Yutaka
 * Copyright (C) 2002, 2003  Paul Mundt
 */

#ifndef __ASM_SH_PROCESSOR_H
#define __ASM_SH_PROCESSOR_H
#ifdef __KERNEL__

#include <linux/compiler.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/cache.h>
#include <asm/ptrace.h>
#include <asm/cpu-features.h>

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ void *pc; __asm__("mova	1f, %0\n1:":"=z" (pc)); pc; })

/* Core Processor Version Register */
#define CCN_PVR		0xff000030
#define CCN_CVR		0xff000040
#define CCN_PRR		0xff000044

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 *
 *  Each one of these also needs a CONFIG_CPU_SUBTYPE_xxx entry
 *  in arch/sh/mm/Kconfig, as well as an entry in arch/sh/kernel/setup.c
 *  for parsing the subtype in get_cpu_subtype().
 */
enum cpu_type {
	/* SH-2 types */
	CPU_SH7604, CPU_SH7619,

	/* SH-2A types */
	CPU_SH7206,

	/* SH-3 types */
	CPU_SH7705, CPU_SH7706, CPU_SH7707,
	CPU_SH7708, CPU_SH7708S, CPU_SH7708R,
	CPU_SH7709, CPU_SH7709A, CPU_SH7710,
	CPU_SH7729, CPU_SH7300,

	/* SH-4 types */
	CPU_SH7750, CPU_SH7750S, CPU_SH7750R, CPU_SH7751, CPU_SH7751R,
	CPU_SH7760, CPU_ST40RA, CPU_ST40GX1, CPU_SH4_202, CPU_SH4_501,

	/* SH-4A types */
	CPU_SH73180, CPU_SH7343, CPU_SH7770, CPU_SH7780, CPU_SH7781,
	CPU_SH7785,

	/* Unknown subtype */
	CPU_SH_NONE
};

struct sh_cpuinfo {
	unsigned int type;
	unsigned long loops_per_jiffy;

	struct cache_info icache;	/* Primary I-cache */
	struct cache_info dcache;	/* Primary D-cache */
	struct cache_info scache;	/* Secondary cache */

	unsigned long flags;
} __attribute__ ((aligned(SMP_CACHE_BYTES)));

extern struct sh_cpuinfo boot_cpu_data;

#ifdef CONFIG_SMP
extern struct sh_cpuinfo cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]
#else
#define cpu_data (&boot_cpu_data)
#define current_cpu_data boot_cpu_data
#endif

/*
 * User space process size: 2GB.
 *
 * Since SH7709 and SH7750 have "area 7", we can't use 0x7c000000--0x7fffffff
 */
#define TASK_SIZE	0x7c000000UL

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
 */
#define SR_FD		0x00008000
#define SR_DSP		0x00001000
#define SR_IMASK	0x000000f0

/*
 * FPU structure and data
 */

struct sh_fpu_hard_struct {
	unsigned long fp_regs[16];
	unsigned long xfp_regs[16];
	unsigned long fpscr;
	unsigned long fpul;

	long status; /* software status information */
};

/* Dummy fpu emulator  */
struct sh_fpu_soft_struct {
	unsigned long fp_regs[16];
	unsigned long xfp_regs[16];
	unsigned long fpscr;
	unsigned long fpul;

	unsigned char lookahead;
	unsigned long entry_pc;
};

union sh_fpu_union {
	struct sh_fpu_hard_struct hard;
	struct sh_fpu_soft_struct soft;
};

struct thread_struct {
	/* Saved registers when thread is descheduled */
	unsigned long sp;
	unsigned long pc;

	/* Hardware debugging registers */
	unsigned long ubc_pc;

	/* floating point info */
	union sh_fpu_union fpu;
};

typedef struct {
	unsigned long seg;
} mm_segment_t;

/* Count of active tasks with UBC settings */
extern int ubc_usercnt;

#define INIT_THREAD  {						\
	.sp = sizeof(init_stack) + (long) &init_stack,		\
}

/*
 * Do necessary setup to start up a newly executed thread.
 */
#define start_thread(regs, new_pc, new_sp)	 \
	set_fs(USER_DS);			 \
	regs->pr = 0;				 \
	regs->sr = SR_FD;	/* User mode. */ \
	regs->pc = new_pc;			 \
	regs->regs[15] = new_sp

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)	do { } while (0)

/*
 * create a kernel thread without removing it from tasklists
 */
extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

/* Copy and release all segment info associated with a VM */
#define copy_segments(p, mm)	do { } while(0)
#define release_segments(mm)	do { } while(0)

/*
 * FPU lazy state save handling.
 */

static __inline__ void disable_fpu(void)
{
	unsigned long __dummy;

	/* Set FD flag in SR */
	__asm__ __volatile__("stc	sr, %0\n\t"
			     "or	%1, %0\n\t"
			     "ldc	%0, sr"
			     : "=&r" (__dummy)
			     : "r" (SR_FD));
}

static __inline__ void enable_fpu(void)
{
	unsigned long __dummy;

	/* Clear out FD flag in SR */
	__asm__ __volatile__("stc	sr, %0\n\t"
			     "and	%1, %0\n\t"
			     "ldc	%0, sr"
			     : "=&r" (__dummy)
			     : "r" (~SR_FD));
}

static __inline__ void release_fpu(struct pt_regs *regs)
{
	regs->sr |= SR_FD;
}

static __inline__ void grab_fpu(struct pt_regs *regs)
{
	regs->sr &= ~SR_FD;
}

#ifdef CONFIG_CPU_SH4
extern void save_fpu(struct task_struct *__tsk, struct pt_regs *regs);
#else
#define save_fpu(tsk)	do { } while (0)
#endif

#define unlazy_fpu(tsk, regs) do {			\
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU)) {	\
		save_fpu(tsk, regs);			\
	}						\
} while (0)

#define clear_fpu(tsk, regs) do {				\
	if (test_tsk_thread_flag(tsk, TIF_USEDFPU)) {		\
		clear_tsk_thread_flag(tsk, TIF_USEDFPU);	\
		release_fpu(regs);				\
	}							\
} while (0)

/* Double presision, NANS as NANS, rounding to nearest, no exceptions */
#define FPSCR_INIT  0x00080000

#define	FPSCR_CAUSE_MASK	0x0001f000	/* Cause bits */
#define	FPSCR_FLAG_MASK		0x0000007c	/* Flag bits */

/*
 * Return saved PC of a blocked thread.
 */
#define thread_saved_pc(tsk)	(tsk->thread.pc)

void show_trace(struct task_struct *tsk, unsigned long *sp,
		struct pt_regs *regs);
extern unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)  (task_pt_regs(tsk)->pc)
#define KSTK_ESP(tsk)  (task_pt_regs(tsk)->regs[15])

#define cpu_sleep()	__asm__ __volatile__ ("sleep" : : : "memory")
#define cpu_relax()	barrier()

#if defined(CONFIG_CPU_SH2A) || defined(CONFIG_CPU_SH3) || \
    defined(CONFIG_CPU_SH4)
#define PREFETCH_STRIDE		L1_CACHE_BYTES
#define ARCH_HAS_PREFETCH
#define ARCH_HAS_PREFETCHW
static inline void prefetch(void *x)
{
	__asm__ __volatile__ ("pref @%0\n\t" : : "r" (x) : "memory");
}

#define prefetchw(x)	prefetch(x)
#endif

#ifdef CONFIG_VSYSCALL
extern int vsyscall_init(void);
#else
#define vsyscall_init() do { } while (0)
#endif

#endif /* __KERNEL__ */
#endif /* __ASM_SH_PROCESSOR_H */
