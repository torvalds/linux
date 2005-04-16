#ifdef __KERNEL__
#ifndef __ASM_PPC_PROCESSOR_H
#define __ASM_PPC_PROCESSOR_H

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#include <linux/config.h>
#include <linux/stringify.h>

#include <asm/ptrace.h>
#include <asm/types.h>
#include <asm/mpc8xx.h>
#include <asm/reg.h>

/* We only need to define a new _MACH_xxx for machines which are part of
 * a configuration which supports more than one type of different machine.
 * This is currently limited to CONFIG_PPC_MULTIPLATFORM and CHRP/PReP/PMac.
 * -- Tom
 */
#define _MACH_prep	0x00000001
#define _MACH_Pmac	0x00000002	/* pmac or pmac clone (non-chrp) */
#define _MACH_chrp	0x00000004	/* chrp machine */

/* see residual.h for these */
#define _PREP_Motorola	0x01	/* motorola prep */
#define _PREP_Firm	0x02	/* firmworks prep */
#define _PREP_IBM	0x00	/* ibm prep */
#define _PREP_Bull	0x03	/* bull prep */

/* these are arbitrary */
#define _CHRP_Motorola	0x04	/* motorola chrp, the cobra */
#define _CHRP_IBM	0x05	/* IBM chrp, the longtrail and longtrail 2 */
#define _CHRP_Pegasos	0x06	/* Genesi/bplan's Pegasos and Pegasos2 */

#define _GLOBAL(n)\
	.stabs __stringify(n:F-1),N_FUN,0,0,n;\
	.globl n;\
n:

/*
 * this is the minimum allowable io space due to the location
 * of the io areas on prep (first one at 0x80000000) but
 * as soon as I get around to remapping the io areas with the BATs
 * to match the mac we can raise this. -- Cort
 */
#define TASK_SIZE	(CONFIG_TASK_SIZE)

#ifndef __ASSEMBLY__
#ifdef CONFIG_PPC_MULTIPLATFORM
extern int _machine;

/* what kind of prep workstation we are */
extern int _prep_type;
extern int _chrp_type;

/*
 * This is used to identify the board type from a given PReP board
 * vendor. Board revision is also made available.
 */
extern unsigned char ucSystemType;
extern unsigned char ucBoardRev;
extern unsigned char ucBoardRevMaj, ucBoardRevMin;
#else
#define _machine 0
#endif /* CONFIG_PPC_MULTIPLATFORM */

struct task_struct;
void start_thread(struct pt_regs *regs, unsigned long nip, unsigned long sp);
void release_thread(struct task_struct *);

/* Prepare to copy thread state - unlazy all lazy status */
extern void prepare_to_copy(struct task_struct *tsk);

/*
 * Create a new kernel thread.
 */
extern long kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

/* Lazy FPU handling on uni-processor */
extern struct task_struct *last_task_used_math;
extern struct task_struct *last_task_used_altivec;
extern struct task_struct *last_task_used_spe;

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(TASK_SIZE / 8 * 3)

typedef struct {
	unsigned long seg;
} mm_segment_t;

struct thread_struct {
	unsigned long	ksp;		/* Kernel stack pointer */
	struct pt_regs	*regs;		/* Pointer to saved register state */
	mm_segment_t	fs;		/* for get_fs() validation */
	void		*pgdir;		/* root of page-table tree */
	int		fpexc_mode;	/* floating-point exception mode */
	signed long	last_syscall;
#if defined(CONFIG_4xx) || defined (CONFIG_BOOKE)
	unsigned long	dbcr0;		/* debug control register values */
	unsigned long	dbcr1;
#endif
	double		fpr[32];	/* Complete floating point set */
	unsigned long	fpscr_pad;	/* fpr ... fpscr must be contiguous */
	unsigned long	fpscr;		/* Floating point status */
#ifdef CONFIG_ALTIVEC
	/* Complete AltiVec register set */
	vector128	vr[32] __attribute((aligned(16)));
	/* AltiVec status */
	vector128	vscr __attribute((aligned(16)));
	unsigned long	vrsave;
	int		used_vr;	/* set if process has used altivec */
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SPE
	unsigned long	evr[32];	/* upper 32-bits of SPE regs */
	u64		acc;		/* Accumulator */
	unsigned long	spefscr;	/* SPE & eFP status */
	int		used_spe;	/* set if process has used spe */
#endif /* CONFIG_SPE */
};

#define ARCH_MIN_TASKALIGN 16

#define INIT_SP		(sizeof(init_stack) + (unsigned long) &init_stack)

#define INIT_THREAD { \
	.ksp = INIT_SP, \
	.fs = KERNEL_DS, \
	.pgdir = swapper_pg_dir, \
	.fpexc_mode = MSR_FE0 | MSR_FE1, \
}

/*
 * Return saved PC of a blocked thread. For now, this is the "user" PC
 */
#define thread_saved_pc(tsk)	\
	((tsk)->thread.regs? (tsk)->thread.regs->nip: 0)

unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)	((tsk)->thread.regs? (tsk)->thread.regs->nip: 0)
#define KSTK_ESP(tsk)	((tsk)->thread.regs? (tsk)->thread.regs->gpr[1]: 0)

/* Get/set floating-point exception mode */
#define GET_FPEXC_CTL(tsk, adr)	get_fpexc_mode((tsk), (adr))
#define SET_FPEXC_CTL(tsk, val)	set_fpexc_mode((tsk), (val))

extern int get_fpexc_mode(struct task_struct *tsk, unsigned long adr);
extern int set_fpexc_mode(struct task_struct *tsk, unsigned int val);

static inline unsigned int __unpack_fe01(unsigned int msr_bits)
{
	return ((msr_bits & MSR_FE0) >> 10) | ((msr_bits & MSR_FE1) >> 8);
}

static inline unsigned int __pack_fe01(unsigned int fpmode)
{
	return ((fpmode << 10) & MSR_FE0) | ((fpmode << 8) & MSR_FE1);
}

/* in process.c - for early bootup debug -- Cort */
int ll_printk(const char *, ...);
void ll_puts(const char *);

/* In misc.c */
void _nmask_and_or_msr(unsigned long nmask, unsigned long or_val);

#define have_of (_machine == _MACH_chrp || _machine == _MACH_Pmac)

#define cpu_relax()	barrier()

/*
 * Prefetch macros.
 */
#define ARCH_HAS_PREFETCH
#define ARCH_HAS_PREFETCHW
#define ARCH_HAS_SPINLOCK_PREFETCH

extern inline void prefetch(const void *x)
{
	 __asm__ __volatile__ ("dcbt 0,%0" : : "r" (x));
}

extern inline void prefetchw(const void *x)
{
	 __asm__ __volatile__ ("dcbtst 0,%0" : : "r" (x));
}

#define spin_lock_prefetch(x)	prefetchw(x)

extern int emulate_altivec(struct pt_regs *regs);

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_PPC_PROCESSOR_H */
#endif /* __KERNEL__ */
