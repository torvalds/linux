/*	$OpenBSD: cpu.h,v 1.109 2024/11/06 12:06:15 miod Exp $	*/
/*	$NetBSD: cpu.h,v 1.28 2001/06/14 22:56:58 thorpej Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)cpu.h	8.4 (Berkeley) 1/5/94
 */

#ifndef _MACHINE_CPU_H_
#define _MACHINE_CPU_H_

/*
 * CTL_MACHDEP definitions.
 */
		/*		1	formerly: booted kernel name */
#define	CPU_LED_BLINK		2	/* int: blink leds? */
#define	CPU_ALLOWAPERTURE	3	/* allow xf86 operations */
#define	CPU_CPUTYPE		4	/* cpu type */
#define	CPU_CECCERRORS		5	/* Correctable ECC errors */
#define	CPU_CECCLAST		6	/* Correctable ECC last fault addr */
		/*		7	formerly: soft reset via keyboard */
#define	CPU_MAXID		8	/* number of valid machdep ids */

#define	CTL_MACHDEP_NAMES {			\
	{ 0, 0 },				\
	{ 0, 0 },				\
	{ "led_blink", CTLTYPE_INT },		\
	{ "allowaperture", CTLTYPE_INT },	\
	{ "cputype", CTLTYPE_INT },		\
	{ "ceccerrs", CTLTYPE_INT },		\
	{ "cecclast", CTLTYPE_QUAD },		\
	{ 0, 0 },				\
}

#ifdef _KERNEL
/*
 * Exported definitions unique to SPARC cpu support.
 */

#include <machine/ctlreg.h>
#include <machine/frame.h>
#include <machine/intr.h>
#include <machine/psl.h>
#include <machine/reg.h>

#include <sys/clockintr.h>
#include <sys/sched.h>
#include <sys/srp.h>
#include <uvm/uvm_percpu.h>

/*
 * The cpu_info structure is part of a 64KB structure mapped both the kernel
 * pmap and a single locked TTE a CPUINFO_VA for that particular processor.
 * Each processor's cpu_info is accessible at CPUINFO_VA only for that
 * processor.  Other processors can access that through an additional mapping
 * in the kernel pmap.
 *
 * The 64KB page contains:
 *
 * cpu_info
 * interrupt stack (all remaining space)
 * idle PCB
 * idle stack (STACKSPACE - sizeof(PCB))
 * 32KB TSB
 */

struct cpu_info {
	/*
	 * SPARC cpu_info structures live at two VAs: one global
	 * VA (so each CPU can access any other CPU's cpu_info)
	 * and an alias VA CPUINFO_VA which is the same on each
	 * CPU and maps to that CPU's cpu_info.  Since the alias
	 * CPUINFO_VA is how we locate our cpu_info, we have to
	 * self-reference the global VA so that we can return it
	 * in the curcpu() macro.
	 */
	struct cpu_info * volatile ci_self;

	/* Most important fields first */
	struct proc		*ci_curproc;
	struct pcb		*ci_cpcb;	/* also initial stack */
	paddr_t			ci_cpcbpaddr;
	struct cpu_info		*ci_next;

	struct proc		*ci_fpproc;
	int			ci_cpuid;
	int			ci_flags;
	int			ci_upaid;
#ifdef MULTIPROCESSOR
	int			ci_itid;
	struct srp_hazard	ci_srp_hazards[SRP_HAZARD_NUM];
#define __HAVE_UVM_PERCPU
	struct uvm_pmr_cache	ci_uvm;		/* [o] page cache */
#endif
	int			ci_node;
	u_int32_t 		ci_randseed;
	struct schedstate_percpu ci_schedstate; /* scheduler state */

	int			ci_want_resched;
	int			ci_handled_intr_level;
	int			ci_idepth;
	struct intrhand		*ci_intrpending[16];
	struct clockqueue	ci_queue;
	struct intrhand		ci_tickintr;

	volatile int		ci_ddb_paused;
#define CI_DDB_RUNNING		0
#define CI_DDB_SHOULDSTOP	1
#define CI_DDB_STOPPED		2
#define CI_DDB_ENTERDDB		3
#define CI_DDB_INDDB		4

	/* Spinning up the CPU */
	void			(*ci_spinup)(void); /* spinup routine */
	void			*ci_initstack;
	paddr_t			ci_paddr;	/* Phys addr of this structure. */

#ifdef SUN4V
	struct rwindow		ci_rw;
	u_int64_t		ci_rwsp;

	paddr_t			ci_mmfsa;
	paddr_t			ci_cpumq;
	paddr_t			ci_devmq;

	paddr_t			ci_cpuset;
	paddr_t			ci_mondo;
#endif

	int			ci_pci_probe;
	int			ci_pci_fault;

#ifdef DIAGNOSTIC
	int	ci_mutex_level;
#endif
#ifdef GPROF
	struct gmonparam *ci_gmon;
	struct clockintr ci_gmonclock;
#endif
	char			ci_panicbuf[512];
};

#define CPUF_RUNNING	0x0001		/* CPU is running */

extern struct cpu_info *cpus;

#ifdef MULTIPROCESSOR

register struct cpu_info *__curcpu asm ("g7");

#define curcpu()	(__curcpu->ci_self)
#define cpu_number()	(__curcpu->ci_cpuid)

#define CPU_IS_PRIMARY(ci)	((ci)->ci_cpuid == 0)
#define CPU_IS_RUNNING(ci)	1
#define CPU_INFO_ITERATOR	int
#define CPU_INFO_FOREACH(cii, ci)					\
	for (cii = 0, ci = cpus; ci != NULL; ci = ci->ci_next)
#define CPU_INFO_UNIT(ci)	((ci)->ci_cpuid)
#define MAXCPUS	256

void	cpu_boot_secondary_processors(void);

void	sparc64_send_ipi(int, void (*)(void), u_int64_t, u_int64_t);
void	sparc64_broadcast_ipi(void (*)(void), u_int64_t, u_int64_t);

void	cpu_unidle(struct cpu_info *);

#else /* MULTIPROCESSOR */

#define	__curcpu	((struct cpu_info *)CPUINFO_VA)
#define curcpu()	__curcpu
#define cpu_number()	0

#define CPU_IS_PRIMARY(ci)	1
#define CPU_IS_RUNNING(ci)	1
#define CPU_INFO_ITERATOR	int
#define CPU_INFO_FOREACH(cii, ci)					\
	for (cii = 0, ci = curcpu(); ci != NULL; ci = NULL)
#define CPU_INFO_UNIT(ci)	0
#define MAXCPUS 1

#define cpu_unidle(ci)

#endif /* MULTIPROCESSOR */

#define curpcb		__curcpu->ci_cpcb
#define fpproc		__curcpu->ci_fpproc

static inline unsigned int
cpu_rnd_messybits(void)
{
	u_int64_t tick;

	__asm volatile("rd %%tick, %0" : "=r" (tick) :);

	return ((tick >> 32) ^ tick);
}

/*
 * On processors with multiple threads we force a thread switch.
 *
 * On UltraSPARC T2 and its successors, the optimal way to do this
 * seems to be to do three nop reads of %ccr.  This works on
 * UltraSPARC T1 as well, even though three nop casx operations seem
 * to be slightly more optimal.  Since these instructions are
 * effectively nops, executing them on earlier non-CMT processors is
 * harmless, so we make this the default.
 *
 * On SPARC T4 and later, we can use the processor-specific pause
 * instruction.
 *
 * On SPARC64 VI and its successors we execute the processor-specific
 * sleep instruction.
 */
#define CPU_BUSY_CYCLE()						\
do {									\
	__asm volatile(							\
		"999:	rd	%%ccr, %%g0			\n"	\
		"	rd	%%ccr, %%g0			\n" 	\
		"	rd	%%ccr, %%g0			\n" 	\
		"	.section .sun4v_pause_patch, \"ax\"	\n" 	\
		"	.word	999b				\n" 	\
		"	.word	0xb7802080	! pause	128	\n" 	\
		"	.word	999b + 4			\n" 	\
		"	nop					\n" 	\
		"	.word	999b + 8			\n" 	\
		"	nop					\n" 	\
		"	.previous				\n" 	\
		"	.section .sun4u_mtp_patch, \"ax\"	\n" 	\
		"	.word	999b				\n" 	\
		"	.word	0x81b01060	! sleep		\n" 	\
		"	.word	999b + 4			\n" 	\
		"	nop					\n" 	\
		"	.word	999b + 8			\n" 	\
		"	nop					\n" 	\
		"	.previous				\n" 	\
		: : : "memory");					\
} while (0)

/*
 * Arguments to clockintr_dispatch encapsulate the
 * previous machine state in an opaque clockframe.
 */
struct clockframe {
	struct trapframe t;
	int saved_intr_level;
};

#define	CLKF_USERMODE(framep)	(((framep)->t.tf_tstate & TSTATE_PRIV) == 0)
#define	CLKF_PC(framep)		((framep)->t.tf_pc)
#define	CLKF_INTR(framep)	((framep)->saved_intr_level != 0)

extern void (*cpu_start_clock)(void);

#define aston(p)	((p)->p_md.md_astpending = 1)

/*
 * Preempt the current process if in interrupt from user mode,
 * or after the current trap/syscall if in system mode.
 */
extern void need_resched(struct cpu_info *);
#define clear_resched(ci) (ci)->ci_want_resched = 0

/*
 * This is used during profiling to integrate system time.
 */
#define	PROC_PC(p)	((p)->p_md.md_tf->tf_pc)
#define	PROC_STACK(p)	((p)->p_md.md_tf->tf_out[6] + (2048-1))	/* BIAS */

/*
 * Give a profiling tick to the current process when the user profiling
 * buffer pages are invalid.  On the sparc, request an ast to send us
 * through trap(), marking the proc as needing a profiling tick.
 */
#define	need_proftick(p)	aston(p)

void signotify(struct proc *);

/* cpu.c */
int	cpu_myid(void);
/* machdep.c */
void	dumpconf(void);
caddr_t	reserve_dumppages(caddr_t);
/* clock.c */
struct timeval;
int	clockintr(void *);/* level 10 (clock) interrupt code */
/* locore.s */
struct fpstate;
void	savefpstate(struct fpstate *);
void	loadfpstate(struct fpstate *);
void	clearfpstate(void);
u_int64_t	probeget(paddr_t, int, int);
#define	 write_all_windows() __asm volatile("flushw" : : )
void	write_user_windows(void);
void 	proc_trampoline(void);
struct pcb;
void	snapshot(struct pcb *);
struct frame *getfp(void);
void	switchtoctx(int);
/* trap.c */
void	pmap_unuse_final(struct proc *);
int	rwindow_save(struct proc *);
/* vm_machdep.c */
void	fpusave_cpu(struct cpu_info *, int);
void	fpusave_proc(struct proc *, int);
/* fb.c */
void	fb_unblank(void);
/* ltc.c */
void	ltc_full_blast(void);
/* tda.c */
void	tda_full_blast(void);
/* emul.c */
int	emul_qf(int32_t, struct proc *, union sigval, struct trapframe *);
int	emul_popc(int32_t, struct proc *, union sigval, struct trapframe *);

/*
 *
 * The SPARC has a Trap Base Register (TBR) which holds the upper 20 bits
 * of the trap vector table.  The next eight bits are supplied by the
 * hardware when the trap occurs, and the bottom four bits are always
 * zero (so that we can shove up to 16 bytes of executable code---exactly
 * four instructions---into each trap vector).
 *
 * The hardware allocates half the trap vectors to hardware and half to
 * software.
 *
 * Traps have priorities assigned (lower number => higher priority).
 */

struct trapvec {
	int	tv_instr[8];		/* the eight instructions */
};
extern struct trapvec trapbase[];	/* the 256 vectors */

struct blink_led {
	void (*bl_func)(void *, int);
	void *bl_arg;
	SLIST_ENTRY(blink_led) bl_next;
};

extern void blink_led_register(struct blink_led *);

#ifdef MULTIPROCESSOR
#include <sys/mplock.h>
#endif

#endif /* _KERNEL */
#endif /* _MACHINE_CPU_H_ */
