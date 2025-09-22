/*	$OpenBSD: cpu.h,v 1.67 2024/06/09 21:15:29 jca Exp $	*/
/*	$NetBSD: cpu.h,v 1.34 2003/06/23 11:01:08 martin Exp $	*/

/*
 * Copyright (c) 1994-1996 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * cpu.h
 *
 * CPU specific symbols
 *
 * Created      : 18/09/94
 *
 * Based on kate/katelib/arm6.h
 */

#ifndef _ARM_CPU_H_
#define _ARM_CPU_H_

/*
 * User-visible definitions
 */

/*  CTL_MACHDEP definitions. */
		/*		1	   formerly int: CPU_DEBUG */
		/*		2	   formerly string: CPU_BOOTED_DEVICE */
		/*		3	   formerly string: CPU_BOOTED_KERNEL */
#define	CPU_CONSDEV		4	/* struct: dev_t of our console */
#define	CPU_POWERSAVE		5	/* int: use CPU powersave mode */
#define	CPU_ALLOWAPERTURE	6	/* int: allow mmap of /dev/xf86 */
		/*		7	   formerly int: apmwarn */
		/*		8	   formerly int: keyboard reset */
		/*		9	   formerly int: CPU_ZTSRAWMODE */
		/*		10	   formerly struct: CPU_ZTSSCALE */
#define	CPU_MAXSPEED		11	/* int: number of valid machdep ids */
		/*		12	   formerly int: CPU_LIDSUSPEND */
#define CPU_LIDACTION		13	/* action caused by lid close */
#define	CPU_COMPATIBLE		14	/* compatible property */
#define	CPU_MAXID		15	/* number of valid machdep ids */

#define	CTL_MACHDEP_NAMES { \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "console_device", CTLTYPE_STRUCT }, \
	{ "powersave", CTLTYPE_INT }, \
	{ "allowaperture", CTLTYPE_INT }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ 0, 0 }, \
	{ "maxspeed", CTLTYPE_INT }, \
	{ 0, 0 }, \
	{ "lidaction", CTLTYPE_INT }, \
	{ "compatible", CTLTYPE_STRING }, \
}

#ifdef _KERNEL

/*
 * Kernel-only definitions
 */

#include <arm/cpuconf.h>

#include <machine/intr.h>
#include <machine/frame.h>
#include <machine/pcb.h>
#include <arm/armreg.h>

/* 1 == use cpu_sleep(), 0 == don't */
extern int cpu_do_powersave;

/* All the CLKF_* macros take a struct clockframe * as an argument. */

/*
 * CLKF_USERMODE: Return TRUE/FALSE (1/0) depending on whether the
 * frame came from USR mode or not.
 */
#define CLKF_USERMODE(frame)	((frame->if_spsr & PSR_MODE) == PSR_USR32_MODE)

/*
 * CLKF_INTR: True if we took the interrupt from inside another
 * interrupt handler.
 */
#define CLKF_INTR(frame)	(curcpu()->ci_idepth > 1) 

/*
 * CLKF_PC: Extract the program counter from a clockframe
 */
#define CLKF_PC(frame)		(frame->if_pc)

/*
 * PROC_PC: Find out the program counter for the given process.
 */
#define PROC_PC(p)	((p)->p_addr->u_pcb.pcb_tf->tf_pc)
#define PROC_STACK(p)	((p)->p_addr->u_pcb.pcb_tf->tf_usr_sp)

/* The address of the vector page. */
extern vaddr_t vector_page;
void	arm32_vector_init(vaddr_t, int);

#define	ARM_VEC_RESET			(1 << 0)
#define	ARM_VEC_UNDEFINED		(1 << 1)
#define	ARM_VEC_SWI			(1 << 2)
#define	ARM_VEC_PREFETCH_ABORT		(1 << 3)
#define	ARM_VEC_DATA_ABORT		(1 << 4)
#define	ARM_VEC_ADDRESS_EXCEPTION	(1 << 5)
#define	ARM_VEC_IRQ			(1 << 6)
#define	ARM_VEC_FIQ			(1 << 7)

#define	ARM_NVEC			8
#define	ARM_VEC_ALL			0xffffffff

/*
 * Per-CPU information.  For now we assume one CPU.
 */

#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/sched.h>
#include <sys/srp.h>

struct cpu_info {
	struct device		*ci_dev; /* Device corresponding to this CPU */
	struct cpu_info		*ci_next;
	struct schedstate_percpu ci_schedstate; /* scheduler state */

	u_int32_t		ci_cpuid;
	uint64_t		ci_mpidr;
	int			ci_node;
	struct cpu_info		*ci_self;

	struct proc		*ci_curproc;
	struct proc		*ci_fpuproc;
	u_int32_t		ci_randseed;

	struct pcb		*ci_curpcb;
	struct pcb		*ci_idle_pcb;

	uint32_t		ci_cpl;
	uint32_t		ci_ipending;
	uint32_t		ci_idepth;
#ifdef DIAGNOSTIC
	int			ci_mutex_level;
#endif
	int			ci_want_resched;

	void			(*ci_flush_bp)(void);

	struct opp_table	*ci_opp_table;
	volatile int		ci_opp_idx;
	volatile int		ci_opp_max;
	uint32_t		ci_cpu_supply;

#ifdef MULTIPROCESSOR
	struct srp_hazard	ci_srp_hazards[SRP_HAZARD_NUM];
	volatile int		ci_flags;
	uint32_t		ci_ttbr0;
	vaddr_t			ci_pl1_stkend;
	vaddr_t			ci_irq_stkend;
	vaddr_t			ci_abt_stkend;
	vaddr_t			ci_und_stkend;
#endif

#ifdef GPROF
	struct gmonparam *ci_gmon;
	struct clockintr ci_gmonclock;
#endif
	struct clockqueue	ci_queue;
	char			ci_panicbuf[512];
};

#define CPUF_PRIMARY 		(1<<0)
#define CPUF_AP	 		(1<<1)
#define CPUF_IDENTIFY		(1<<2)
#define CPUF_IDENTIFIED		(1<<3)
#define CPUF_PRESENT		(1<<4)
#define CPUF_GO			(1<<5)
#define CPUF_RUNNING		(1<<6)

static inline struct cpu_info *
curcpu(void)
{
	struct cpu_info *__ci;
	__asm volatile("mrc	p15, 0, %0, c13, c0, 4" : "=r" (__ci));
	return (__ci);
}

extern struct cpu_info cpu_info_primary;
extern struct cpu_info *cpu_info_list;

#ifndef MULTIPROCESSOR
#define cpu_number()	0
#define CPU_IS_PRIMARY(ci)	1
#define CPU_IS_RUNNING(ci)	1
#define CPU_INFO_ITERATOR	int
#define CPU_INFO_FOREACH(cii, ci) \
	for (cii = 0, ci = curcpu(); ci != NULL; ci = NULL)
#define CPU_INFO_UNIT(ci)	0
#define MAXCPUS	1
#define cpu_kick(ci)
#define cpu_unidle(ci)
#else
#define cpu_number()		(curcpu()->ci_cpuid)
#define CPU_IS_PRIMARY(ci)	((ci) == &cpu_info_primary)
#define CPU_IS_RUNNING(ci)	((ci)->ci_flags & CPUF_RUNNING)
#define CPU_INFO_ITERATOR		int
#define CPU_INFO_FOREACH(cii, ci)	for (cii = 0, ci = cpu_info_list; \
					    ci != NULL; ci = ci->ci_next)
#define CPU_INFO_UNIT(ci)	((ci)->ci_dev ? (ci)->ci_dev->dv_unit : 0)
#define MAXCPUS	4
void cpu_kick(struct cpu_info *);
void cpu_unidle(struct cpu_info *ci);

extern struct cpu_info *cpu_info[MAXCPUS];

void cpu_boot_secondary_processors(void);
#endif /* !MULTIPROCESSOR */

#define CPU_BUSY_CYCLE()	__asm volatile ("" ::: "memory")

#define curpcb		curcpu()->ci_curpcb

unsigned int cpu_rnd_messybits(void);

/*
 * Scheduling glue
 */

extern int astpending;
#define setsoftast() (astpending = 1)

/*
 * Notify the current process (p) that it has a signal pending,
 * process as soon as possible.
 */

#define signotify(p)            setsoftast()

/*
 * Preempt the current process if in interrupt from user mode,
 * or after the current trap/syscall if in system mode.
 */
extern int want_resched;	/* resched() was called */
#define	need_resched(ci)	(want_resched = 1, setsoftast())
#define clear_resched(ci) 	want_resched = 0

/*
 * Give a profiling tick to the current process when the user profiling
 * buffer pages are invalid.  On the i386, request an ast to send us
 * through trap(), marking the proc as needing a profiling tick.
 */
#define	need_proftick(p)	setsoftast()

/*
 * cpu device glue (belongs in cpuvar.h)
 */

int	cpu_alloc_idle_pcb(struct cpu_info *);

/*
 * Random cruft
 */

/* cpuswitch.S */
struct pcb;
void	savectx		(struct pcb *pcb);

/* machdep.h */
void bootsync		(int);

/* fault.c */
int badaddr_read	(void *, size_t, void *);

/* syscall.c */
void swi_handler	(trapframe_t *);

/* machine_machdep.c */
void board_startup(void);

static inline u_long
intr_disable(void)
{
	uint32_t cpsr;

	__asm volatile ("mrs %0, cpsr" : "=r"(cpsr));
	__asm volatile ("msr cpsr_c, %0" :: "r"(cpsr | PSR_I));

	return cpsr;
}

static inline void
intr_restore(u_long cpsr)
{
	__asm volatile ("msr cpsr_c, %0" :: "r"(cpsr));
}

#endif /* _KERNEL */

#ifdef MULTIPROCESSOR
#include <sys/mplock.h>
#endif /* MULTIPROCESSOR */

#endif /* !_ARM_CPU_H_ */
