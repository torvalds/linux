#ifndef _ASM_IA64_PTRACE_H
#define _ASM_IA64_PTRACE_H

/*
 * Copyright (C) 1998-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2003 Intel Co
 *	Suresh Siddha <suresh.b.siddha@intel.com>
 *	Fenghua Yu <fenghua.yu@intel.com>
 *	Arun Sharma <arun.sharma@intel.com>
 *
 * 12/07/98	S. Eranian	added pt_regs & switch_stack
 * 12/21/98	D. Mosberger	updated to match latest code
 *  6/17/99	D. Mosberger	added second unat member to "struct switch_stack"
 *
 */
/*
 * When a user process is blocked, its state looks as follows:
 *
 *            +----------------------+	-------	IA64_STK_OFFSET
 *     	      |			     |	 ^
 *            | struct pt_regs       |	 |
 *	      |			     |	 |
 *            +----------------------+	 |
 *	      |			     |	 |
 *     	      |	   memory stack	     |	 |
 *	      |	(growing downwards)  |	 |
 *	      //.....................//	 |
 *					 |
 *	      //.....................//	 |
 *	      |			     |	 |
 *            +----------------------+	 |
 *            | struct switch_stack  |	 |
 *	      |			     |	 |
 *	      +----------------------+	 |
 *	      |			     |	 |
 *	      //.....................//	 |
 *					 |
 *	      //.....................//	 |
 *	      |			     |	 |
 *	      |	 register stack	     |	 |
 *	      |	(growing upwards)    |	 |
 *            |			     |	 |
 *	      +----------------------+	 |  ---	IA64_RBS_OFFSET
 *            |  struct thread_info  |	 |  ^
 *	      +----------------------+	 |  |
 *	      |			     |	 |  |
 *            |  struct task_struct  |	 |  |
 * current -> |			     |   |  |
 *	      +----------------------+ -------
 *
 * Note that ar.ec is not saved explicitly in pt_reg or switch_stack.
 * This is because ar.ec is saved as part of ar.pfs.
 */


#include <asm/fpu.h>

#ifdef __KERNEL__
#ifndef ASM_OFFSETS_C
#include <asm/asm-offsets.h>
#endif

/*
 * Base-2 logarithm of number of pages to allocate per task structure
 * (including register backing store and memory stack):
 */
#if defined(CONFIG_IA64_PAGE_SIZE_4KB)
# define KERNEL_STACK_SIZE_ORDER		3
#elif defined(CONFIG_IA64_PAGE_SIZE_8KB)
# define KERNEL_STACK_SIZE_ORDER		2
#elif defined(CONFIG_IA64_PAGE_SIZE_16KB)
# define KERNEL_STACK_SIZE_ORDER		1
#else
# define KERNEL_STACK_SIZE_ORDER		0
#endif

#define IA64_RBS_OFFSET			((IA64_TASK_SIZE + IA64_THREAD_INFO_SIZE + 15) & ~15)
#define IA64_STK_OFFSET			((1 << KERNEL_STACK_SIZE_ORDER)*PAGE_SIZE)

#define KERNEL_STACK_SIZE		IA64_STK_OFFSET

#endif /* __KERNEL__ */

#ifndef __ASSEMBLY__

/*
 * This struct defines the way the registers are saved on system
 * calls.
 *
 * We don't save all floating point register because the kernel
 * is compiled to use only a very small subset, so the other are
 * untouched.
 *
 * THIS STRUCTURE MUST BE A MULTIPLE 16-BYTE IN SIZE
 * (because the memory stack pointer MUST ALWAYS be aligned this way)
 *
 */
struct pt_regs {
	/* The following registers are saved by SAVE_MIN: */
	unsigned long b6;		/* scratch */
	unsigned long b7;		/* scratch */

	unsigned long ar_csd;           /* used by cmp8xchg16 (scratch) */
	unsigned long ar_ssd;           /* reserved for future use (scratch) */

	unsigned long r8;		/* scratch (return value register 0) */
	unsigned long r9;		/* scratch (return value register 1) */
	unsigned long r10;		/* scratch (return value register 2) */
	unsigned long r11;		/* scratch (return value register 3) */

	unsigned long cr_ipsr;		/* interrupted task's psr */
	unsigned long cr_iip;		/* interrupted task's instruction pointer */
	/*
	 * interrupted task's function state; if bit 63 is cleared, it
	 * contains syscall's ar.pfs.pfm:
	 */
	unsigned long cr_ifs;

	unsigned long ar_unat;		/* interrupted task's NaT register (preserved) */
	unsigned long ar_pfs;		/* prev function state  */
	unsigned long ar_rsc;		/* RSE configuration */
	/* The following two are valid only if cr_ipsr.cpl > 0 || ti->flags & _TIF_MCA_INIT */
	unsigned long ar_rnat;		/* RSE NaT */
	unsigned long ar_bspstore;	/* RSE bspstore */

	unsigned long pr;		/* 64 predicate registers (1 bit each) */
	unsigned long b0;		/* return pointer (bp) */
	unsigned long loadrs;		/* size of dirty partition << 16 */

	unsigned long r1;		/* the gp pointer */
	unsigned long r12;		/* interrupted task's memory stack pointer */
	unsigned long r13;		/* thread pointer */

	unsigned long ar_fpsr;		/* floating point status (preserved) */
	unsigned long r15;		/* scratch */

	/* The remaining registers are NOT saved for system calls.  */

	unsigned long r14;		/* scratch */
	unsigned long r2;		/* scratch */
	unsigned long r3;		/* scratch */

	/* The following registers are saved by SAVE_REST: */
	unsigned long r16;		/* scratch */
	unsigned long r17;		/* scratch */
	unsigned long r18;		/* scratch */
	unsigned long r19;		/* scratch */
	unsigned long r20;		/* scratch */
	unsigned long r21;		/* scratch */
	unsigned long r22;		/* scratch */
	unsigned long r23;		/* scratch */
	unsigned long r24;		/* scratch */
	unsigned long r25;		/* scratch */
	unsigned long r26;		/* scratch */
	unsigned long r27;		/* scratch */
	unsigned long r28;		/* scratch */
	unsigned long r29;		/* scratch */
	unsigned long r30;		/* scratch */
	unsigned long r31;		/* scratch */

	unsigned long ar_ccv;		/* compare/exchange value (scratch) */

	/*
	 * Floating point registers that the kernel considers scratch:
	 */
	struct ia64_fpreg f6;		/* scratch */
	struct ia64_fpreg f7;		/* scratch */
	struct ia64_fpreg f8;		/* scratch */
	struct ia64_fpreg f9;		/* scratch */
	struct ia64_fpreg f10;		/* scratch */
	struct ia64_fpreg f11;		/* scratch */
};

/*
 * This structure contains the addition registers that need to
 * preserved across a context switch.  This generally consists of
 * "preserved" registers.
 */
struct switch_stack {
	unsigned long caller_unat;	/* user NaT collection register (preserved) */
	unsigned long ar_fpsr;		/* floating-point status register */

	struct ia64_fpreg f2;		/* preserved */
	struct ia64_fpreg f3;		/* preserved */
	struct ia64_fpreg f4;		/* preserved */
	struct ia64_fpreg f5;		/* preserved */

	struct ia64_fpreg f12;		/* scratch, but untouched by kernel */
	struct ia64_fpreg f13;		/* scratch, but untouched by kernel */
	struct ia64_fpreg f14;		/* scratch, but untouched by kernel */
	struct ia64_fpreg f15;		/* scratch, but untouched by kernel */
	struct ia64_fpreg f16;		/* preserved */
	struct ia64_fpreg f17;		/* preserved */
	struct ia64_fpreg f18;		/* preserved */
	struct ia64_fpreg f19;		/* preserved */
	struct ia64_fpreg f20;		/* preserved */
	struct ia64_fpreg f21;		/* preserved */
	struct ia64_fpreg f22;		/* preserved */
	struct ia64_fpreg f23;		/* preserved */
	struct ia64_fpreg f24;		/* preserved */
	struct ia64_fpreg f25;		/* preserved */
	struct ia64_fpreg f26;		/* preserved */
	struct ia64_fpreg f27;		/* preserved */
	struct ia64_fpreg f28;		/* preserved */
	struct ia64_fpreg f29;		/* preserved */
	struct ia64_fpreg f30;		/* preserved */
	struct ia64_fpreg f31;		/* preserved */

	unsigned long r4;		/* preserved */
	unsigned long r5;		/* preserved */
	unsigned long r6;		/* preserved */
	unsigned long r7;		/* preserved */

	unsigned long b0;		/* so we can force a direct return in copy_thread */
	unsigned long b1;
	unsigned long b2;
	unsigned long b3;
	unsigned long b4;
	unsigned long b5;

	unsigned long ar_pfs;		/* previous function state */
	unsigned long ar_lc;		/* loop counter (preserved) */
	unsigned long ar_unat;		/* NaT bits for r4-r7 */
	unsigned long ar_rnat;		/* RSE NaT collection register */
	unsigned long ar_bspstore;	/* RSE dirty base (preserved) */
	unsigned long pr;		/* 64 predicate registers (1 bit each) */
};

#ifdef __KERNEL__

#include <asm/current.h>
#include <asm/page.h>

#define __ARCH_SYS_PTRACE	1

/*
 * We use the ia64_psr(regs)->ri to determine which of the three
 * instructions in bundle (16 bytes) took the sample. Generate
 * the canonical representation by adding to instruction pointer.
 */
# define instruction_pointer(regs) ((regs)->cr_iip + ia64_psr(regs)->ri)

#define regs_return_value(regs) ((regs)->r8)

/* Conserve space in histogram by encoding slot bits in address
 * bits 2 and 3 rather than bits 0 and 1.
 */
#define profile_pc(regs)						\
({									\
	unsigned long __ip = instruction_pointer(regs);			\
	(__ip & ~3UL) + ((__ip & 3UL) << 2);				\
})

  /* given a pointer to a task_struct, return the user's pt_regs */
# define task_pt_regs(t)		(((struct pt_regs *) ((char *) (t) + IA64_STK_OFFSET)) - 1)
# define ia64_psr(regs)			((struct ia64_psr *) &(regs)->cr_ipsr)
# define user_mode(regs)		(((struct ia64_psr *) &(regs)->cr_ipsr)->cpl != 0)
# define user_stack(task,regs)	((long) regs - (long) task == IA64_STK_OFFSET - sizeof(*regs))
# define fsys_mode(task,regs)					\
  ({								\
	  struct task_struct *_task = (task);			\
	  struct pt_regs *_regs = (regs);			\
	  !user_mode(_regs) && user_stack(_task, _regs);	\
  })

  /*
   * System call handlers that, upon successful completion, need to return a negative value
   * should call force_successful_syscall_return() right before returning.  On architectures
   * where the syscall convention provides for a separate error flag (e.g., alpha, ia64,
   * ppc{,64}, sparc{,64}, possibly others), this macro can be used to ensure that the error
   * flag will not get set.  On architectures which do not support a separate error flag,
   * the macro is a no-op and the spurious error condition needs to be filtered out by some
   * other means (e.g., in user-level, by passing an extra argument to the syscall handler,
   * or something along those lines).
   *
   * On ia64, we can clear the user's pt_regs->r8 to force a successful syscall.
   */
# define force_successful_syscall_return()	(task_pt_regs(current)->r8 = 0)

  struct task_struct;			/* forward decl */
  struct unw_frame_info;		/* forward decl */

  extern void show_regs (struct pt_regs *);
  extern void ia64_do_show_stack (struct unw_frame_info *, void *);
  extern unsigned long ia64_get_user_rbs_end (struct task_struct *, struct pt_regs *,
					      unsigned long *);
  extern long ia64_peek (struct task_struct *, struct switch_stack *, unsigned long,
			 unsigned long, long *);
  extern long ia64_poke (struct task_struct *, struct switch_stack *, unsigned long,
			 unsigned long, long);
  extern void ia64_flush_fph (struct task_struct *);
  extern void ia64_sync_fph (struct task_struct *);
  extern long ia64_sync_user_rbs (struct task_struct *, struct switch_stack *,
				  unsigned long, unsigned long);

  /* get nat bits for scratch registers such that bit N==1 iff scratch register rN is a NaT */
  extern unsigned long ia64_get_scratch_nat_bits (struct pt_regs *pt, unsigned long scratch_unat);
  /* put nat bits for scratch registers such that scratch register rN is a NaT iff bit N==1 */
  extern unsigned long ia64_put_scratch_nat_bits (struct pt_regs *pt, unsigned long nat);

  extern void ia64_increment_ip (struct pt_regs *pt);
  extern void ia64_decrement_ip (struct pt_regs *pt);

#endif /* !__KERNEL__ */

/* pt_all_user_regs is used for PTRACE_GETREGS PTRACE_SETREGS */
struct pt_all_user_regs {
	unsigned long nat;
	unsigned long cr_iip;
	unsigned long cfm;
	unsigned long cr_ipsr;
	unsigned long pr;

	unsigned long gr[32];
	unsigned long br[8];
	unsigned long ar[128];
	struct ia64_fpreg fr[128];
};

#endif /* !__ASSEMBLY__ */

/* indices to application-registers array in pt_all_user_regs */
#define PT_AUR_RSC	16
#define PT_AUR_BSP	17
#define PT_AUR_BSPSTORE	18
#define PT_AUR_RNAT	19
#define PT_AUR_CCV	32
#define PT_AUR_UNAT	36
#define PT_AUR_FPSR	40
#define PT_AUR_PFS	64
#define PT_AUR_LC	65
#define PT_AUR_EC	66

/*
 * The numbers chosen here are somewhat arbitrary but absolutely MUST
 * not overlap with any of the number assigned in <linux/ptrace.h>.
 */
#define PTRACE_SINGLEBLOCK	12	/* resume execution until next branch */
#define PTRACE_OLD_GETSIGINFO	13	/* (replaced by PTRACE_GETSIGINFO in <linux/ptrace.h>)  */
#define PTRACE_OLD_SETSIGINFO	14	/* (replaced by PTRACE_SETSIGINFO in <linux/ptrace.h>)  */
#define PTRACE_GETREGS		18	/* get all registers (pt_all_user_regs) in one shot */
#define PTRACE_SETREGS		19	/* set all registers (pt_all_user_regs) in one shot */

#define PTRACE_OLDSETOPTIONS	21

#endif /* _ASM_IA64_PTRACE_H */
