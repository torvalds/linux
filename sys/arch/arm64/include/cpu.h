/* $OpenBSD: cpu.h,v 1.51 2025/02/11 22:27:09 kettenis Exp $ */
/*
 * Copyright (c) 2016 Dale Rahn <drahn@dalerahn.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _MACHINE_CPU_H_
#define _MACHINE_CPU_H_

/*
 * User-visible definitions
 */

/*
 * CTL_MACHDEP definitions.
 */
#define	CPU_COMPATIBLE		1	/* compatible property */
#define	CPU_ID_AA64ISAR0	2
#define	CPU_ID_AA64ISAR1	3
#define	CPU_ID_AA64ISAR2	4
#define	CPU_ID_AA64MMFR0	5
#define	CPU_ID_AA64MMFR1	6
#define	CPU_ID_AA64MMFR2	7
#define	CPU_ID_AA64PFR0		8
#define	CPU_ID_AA64PFR1		9
#define	CPU_ID_AA64SMFR0       10
#define	CPU_ID_AA64ZFR0	       11
#define	CPU_LIDACTION          12
#define	CPU_MAXID	       13	/* number of valid machdep ids */

#define	CTL_MACHDEP_NAMES { \
	{ 0, 0 }, \
	{ "compatible", CTLTYPE_STRING }, \
	{ "id_aa64isar0", CTLTYPE_QUAD }, \
	{ "id_aa64isar1", CTLTYPE_QUAD }, \
	{ "id_aa64isar2", CTLTYPE_QUAD }, \
	{ "id_aa64mmfr0", CTLTYPE_QUAD }, \
	{ "id_aa64mmfr1", CTLTYPE_QUAD }, \
	{ "id_aa64mmfr2", CTLTYPE_QUAD }, \
	{ "id_aa64pfr0", CTLTYPE_QUAD }, \
	{ "id_aa64pfr1", CTLTYPE_QUAD }, \
	{ "id_aa64smfr0", CTLTYPE_QUAD }, \
	{ "id_aa64zfr0", CTLTYPE_QUAD }, \
	{ "lidaction", CTLTYPE_INT }, \
}

#ifdef _KERNEL

/*
 * Kernel-only definitions
 */

extern uint64_t cpu_id_aa64isar0;
extern uint64_t cpu_id_aa64isar1;
extern uint64_t cpu_id_aa64isar2;
extern uint64_t cpu_id_aa64mmfr0;
extern uint64_t cpu_id_aa64mmfr1;
extern uint64_t cpu_id_aa64mmfr2;
extern uint64_t cpu_id_aa64pfr0;
extern uint64_t cpu_id_aa64pfr1;
extern uint64_t cpu_id_aa64zfr0;

void cpu_identify_cleanup(void);

#include <machine/intr.h>
#include <machine/frame.h>
#include <machine/armreg.h>

/* All the CLKF_* macros take a struct clockframe * as an argument. */

#define clockframe trapframe
/*
 * CLKF_USERMODE: Return TRUE/FALSE (1/0) depending on whether the
 * frame came from USR mode or not.
 */
#define CLKF_USERMODE(frame)	((frame->tf_elr & (1ul << 63)) == 0)

/*
 * CLKF_INTR: True if we took the interrupt from inside another
 * interrupt handler.
 */
#define CLKF_INTR(frame)	(curcpu()->ci_idepth > 1)

/*
 * CLKF_PC: Extract the program counter from a clockframe
 */
#define CLKF_PC(frame)		(frame->tf_elr)

/*
 * PROC_PC: Find out the program counter for the given process.
 */
#define PROC_PC(p)	((p)->p_addr->u_pcb.pcb_tf->tf_elr)
#define PROC_STACK(p)	((p)->p_addr->u_pcb.pcb_tf->tf_sp)

/*
 * Per-CPU information.  For now we assume one CPU.
 */

#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/sched.h>
#include <sys/srp.h>
#include <uvm/uvm_percpu.h>

struct cpu_info {
	struct device		*ci_dev; /* Device corresponding to this CPU */
	struct cpu_info		*ci_next;
	struct schedstate_percpu ci_schedstate; /* scheduler state */

	u_int32_t		ci_cpuid;
	uint64_t		ci_mpidr;
	uint64_t		ci_midr;
	u_int			ci_acpi_proc_id;
	int			ci_node;
	struct cpu_info		*ci_self;

#define __HAVE_CPU_TOPOLOGY
	u_int32_t		ci_smt_id;
	u_int32_t		ci_core_id;
	u_int32_t		ci_pkg_id;

	struct proc		*ci_curproc;
	struct pcb		*ci_curpcb;
	struct pmap		*ci_curpm;
	u_int32_t		ci_randseed;

	u_int32_t		ci_ctrl; /* The CPU control register */

	u_int64_t		ci_trampoline_vectors;

	uint32_t		ci_cpl;
	uint32_t		ci_ipending;
	uint32_t		ci_idepth;
#ifdef DIAGNOSTIC
	int			ci_mutex_level;
#endif
	int			ci_want_resched;

	void			(*ci_flush_bp)(void);
	void			(*ci_serror)(void);

	uint64_t		ci_ttbr1;
	vaddr_t			ci_el1_stkend;

	uint32_t		ci_psci_idle_latency;
	uint32_t		ci_psci_idle_param;
	uint32_t		ci_psci_suspend_param;

	struct opp_table	*ci_opp_table;
	volatile int		ci_opp_idx;
	volatile int		ci_opp_max;
	uint32_t		ci_cpu_supply;

	u_long			ci_prev_sleep;
	u_long			ci_last_itime;

#ifdef MULTIPROCESSOR
	struct srp_hazard	ci_srp_hazards[SRP_HAZARD_NUM];
#define __HAVE_UVM_PERCPU
	struct uvm_pmr_cache	ci_uvm;
	volatile int		ci_flags;

	volatile int		ci_ddb_paused;
#define CI_DDB_RUNNING		0
#define CI_DDB_SHOULDSTOP	1
#define CI_DDB_STOPPED		2
#define CI_DDB_ENTERDDB		3
#define CI_DDB_INDDB		4

#endif

#ifdef GPROF
	struct gmonparam	*ci_gmon;
	struct clockintr	ci_gmonclock;
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
	struct cpu_info *__ci = NULL;
	__asm volatile("mrs %0, tpidr_el1" : "=r" (__ci));
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
#define cpu_unidle(ci)
#else
#define cpu_number()		(curcpu()->ci_cpuid)
#define CPU_IS_PRIMARY(ci)	((ci) == &cpu_info_primary)
#define CPU_IS_RUNNING(ci)	((ci)->ci_flags & CPUF_RUNNING)
#define CPU_INFO_ITERATOR		int
#define CPU_INFO_FOREACH(cii, ci)	for (cii = 0, ci = cpu_info_list; \
					    ci != NULL; ci = ci->ci_next)
#define CPU_INFO_UNIT(ci)	((ci)->ci_dev ? (ci)->ci_dev->dv_unit : 0)
#define MAXCPUS	256

extern struct cpu_info *cpu_info[MAXCPUS];

void cpu_boot_secondary_processors(void);
#endif /* !MULTIPROCESSOR */

#define CPU_BUSY_CYCLE()	__asm volatile("yield" : : : "memory")

#define curpcb		curcpu()->ci_curpcb

static inline unsigned int
cpu_rnd_messybits(void)
{
	uint64_t val, rval;

	__asm volatile("mrs %0, CNTVCT_EL0; rbit %1, %0;"
	    : "=r" (val), "=r" (rval));

	return (val ^ rval);
}

/*
 * Scheduling glue
 */
#define aston(p)        ((p)->p_md.md_astpending = 1)
#define	setsoftast()	aston(curcpu()->ci_curproc)

/*
 * Notify the current process (p) that it has a signal pending,
 * process as soon as possible.
 */

#ifdef MULTIPROCESSOR
void cpu_unidle(struct cpu_info *ci);
#define signotify(p)            (aston(p), cpu_unidle((p)->p_cpu))
void cpu_kick(struct cpu_info *);
#else
#define cpu_kick(ci)
#define cpu_unidle(ci)
#define signotify(p)            setsoftast()
#endif

/*
 * Preempt the current process if in interrupt from user mode,
 * or after the current trap/syscall if in system mode.
 */
void need_resched(struct cpu_info *);
#define clear_resched(ci) 	((ci)->ci_want_resched = 0)

/*
 * Give a profiling tick to the current process when the user profiling
 * buffer pages are invalid.  On the i386, request an ast to send us
 * through trap(), marking the proc as needing a profiling tick.
 */
#define	need_proftick(p)	aston(p)

// asm code to start new kernel contexts.
void	proc_trampoline(void);

/*
 * Random cruft
 */
void	dumpconf(void);

// syscall.c
void svc_handler	(trapframe_t *);

// functions to manipulate interrupt state
static __inline void
restore_daif(uint32_t daif)
{
	__asm volatile ("msr daif, %x0":: "r"(daif));
}

static __inline void
enable_irq_daif(void)
{
	__asm volatile ("msr daifclr, #3");
}

static __inline void
disable_irq_daif(void)
{
	__asm volatile ("msr daifset, #3");
}

static __inline uint32_t
disable_irq_daif_ret(void)
{
	uint32_t daif;
	__asm volatile ("mrs %x0, daif": "=r"(daif));
	__asm volatile ("msr daifset, #3");
	return daif;
}

static inline void
intr_enable(void)
{
	enable_irq_daif();
}

static inline u_long
intr_disable(void)
{
	return disable_irq_daif_ret();
}

static inline void
intr_restore(u_long daif)
{
	restore_daif(daif);
}

void	cpu_halt(void);
int	cpu_suspend_primary(void);
void	cpu_resume_secondary(struct cpu_info *);

extern void (*cpu_idle_cycle_fcn)(void);
extern void (*cpu_suspend_cycle_fcn)(void);

void	cpu_wfi(void);

void	delay (unsigned);
#define	DELAY(x)	delay(x)

#endif /* _KERNEL */

#ifdef MULTIPROCESSOR
#include <sys/mplock.h>
#endif /* MULTIPROCESSOR */

#endif /* !_MACHINE_CPU_H_ */
