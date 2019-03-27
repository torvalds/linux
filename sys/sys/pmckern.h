/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2007, Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * PMC interface used by the base kernel.
 */

#ifndef _SYS_PMCKERN_H_
#define _SYS_PMCKERN_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/sx.h>
#include <sys/pmc.h>

#include <machine/cpufunc.h>

#define	PMC_FN_PROCESS_EXEC		1
#define	PMC_FN_CSW_IN			2
#define	PMC_FN_CSW_OUT			3
#define	PMC_FN_DO_SAMPLES		4
#define	PMC_FN_UNUSED1			5
#define	PMC_FN_UNUSED2			6
#define	PMC_FN_MMAP			7
#define	PMC_FN_MUNMAP			8
#define	PMC_FN_USER_CALLCHAIN		9
#define	PMC_FN_USER_CALLCHAIN_SOFT	10
#define	PMC_FN_SOFT_SAMPLING		11
#define	PMC_FN_THR_CREATE		12
#define	PMC_FN_THR_EXIT			13
#define	PMC_FN_THR_USERRET		14
#define	PMC_FN_THR_CREATE_LOG		15
#define	PMC_FN_THR_EXIT_LOG		16
#define	PMC_FN_PROC_CREATE_LOG		17

typedef enum ring_type {
        PMC_HR = 0,	/* Hardware ring buffer */
		PMC_SR = 1,	/* Software ring buffer */
        PMC_UR = 2,	/* userret ring buffer */
		PMC_NUM_SR = PMC_UR+1
} ring_type_t;

struct pmckern_procexec {
	int		pm_credentialschanged;
	uintfptr_t	pm_entryaddr;
};

struct pmckern_map_in {
	void		*pm_file;	/* filename or vnode pointer */
	uintfptr_t	pm_address;	/* address object is loaded at */
};

struct pmckern_map_out {
	uintfptr_t	pm_address;	/* start address of region */
	size_t		pm_size;	/* size of unmapped region */
};

struct pmckern_soft {
	enum pmc_event		pm_ev;
	int			pm_cpu;
	struct trapframe 	*pm_tf;
};

/*
 * Soft PMC.
 */

#define PMC_SOFT_DEFINE_EX(prov, mod, func, name, alloc, release)		\
	struct pmc_soft pmc_##prov##_##mod##_##func##_##name =			\
	    { 0, alloc, release, { #prov "_" #mod "_" #func "." #name, 0 } };	\
	SYSINIT(pmc_##prov##_##mod##_##func##_##name##_init, SI_SUB_KDTRACE, 	\
	    SI_ORDER_SECOND + 1, pmc_soft_ev_register, 				\
	    &pmc_##prov##_##mod##_##func##_##name );				\
	SYSUNINIT(pmc_##prov##_##mod##_##func##_##name##_uninit, 		\
	    SI_SUB_KDTRACE, SI_ORDER_SECOND + 1, pmc_soft_ev_deregister,	\
	    &pmc_##prov##_##mod##_##func##_##name )

#define PMC_SOFT_DEFINE(prov, mod, func, name)					\
	PMC_SOFT_DEFINE_EX(prov, mod, func, name, NULL, NULL)

#define PMC_SOFT_DECLARE(prov, mod, func, name)					\
	extern struct pmc_soft pmc_##prov##_##mod##_##func##_##name

/*
 * PMC_SOFT_CALL can be used anywhere in the kernel.
 * Require md defined PMC_FAKE_TRAPFRAME.
 */
#ifdef PMC_FAKE_TRAPFRAME
#define PMC_SOFT_CALL(pr, mo, fu, na)						\
do {										\
	if (__predict_false(pmc_##pr##_##mo##_##fu##_##na.ps_running)) {	\
		struct pmckern_soft ks;						\
		register_t intr;						\
		intr = intr_disable();						\
		PMC_FAKE_TRAPFRAME(&pmc_tf[curcpu]);				\
		ks.pm_ev = pmc_##pr##_##mo##_##fu##_##na.ps_ev.pm_ev_code;	\
		ks.pm_cpu = PCPU_GET(cpuid);					\
		ks.pm_tf = &pmc_tf[curcpu];					\
		PMC_CALL_HOOK_UNLOCKED(curthread,				\
		    PMC_FN_SOFT_SAMPLING, (void *) &ks);			\
		intr_restore(intr);						\
	}									\
} while (0)
#else
#define PMC_SOFT_CALL(pr, mo, fu, na)						\
do {										\
} while (0)
#endif

/*
 * PMC_SOFT_CALL_TF need to be used carefully.
 * Userland capture will be done during AST processing.
 */
#define PMC_SOFT_CALL_TF(pr, mo, fu, na, tf)					\
do {										\
	if (__predict_false(pmc_##pr##_##mo##_##fu##_##na.ps_running)) {	\
		struct pmckern_soft ks;						\
		register_t intr;						\
		intr = intr_disable();						\
		ks.pm_ev = pmc_##pr##_##mo##_##fu##_##na.ps_ev.pm_ev_code;	\
		ks.pm_cpu = PCPU_GET(cpuid);					\
		ks.pm_tf = tf;							\
		PMC_CALL_HOOK_UNLOCKED(curthread,				\
		    PMC_FN_SOFT_SAMPLING, (void *) &ks);			\
		intr_restore(intr);						\
	}									\
} while (0)

struct pmc_soft {
	int				ps_running;
	void				(*ps_alloc)(void);
	void				(*ps_release)(void);
	struct pmc_dyn_event_descr	ps_ev;
};

struct pmclog_buffer;

struct pmc_domain_buffer_header {
	struct mtx pdbh_mtx;
	TAILQ_HEAD(, pmclog_buffer) pdbh_head;
	struct pmclog_buffer *pdbh_plbs;
	int pdbh_ncpus;
} __aligned(CACHE_LINE_SIZE);

/* hook */
extern int (*pmc_hook)(struct thread *_td, int _function, void *_arg);
extern int (*pmc_intr)(struct trapframe *_frame);

/* SX lock protecting the hook */
extern struct sx pmc_sx;

/* Per-cpu flags indicating availability of sampling data */
DPCPU_DECLARE(uint8_t, pmc_sampled);

/* Count of system-wide sampling PMCs in existence */
extern volatile int pmc_ss_count;

/* kernel version number */
extern const int pmc_kernel_version;

/* PMC soft per cpu trapframe */
extern struct trapframe pmc_tf[MAXCPU];

/* per domain buffer header list */
extern struct pmc_domain_buffer_header *pmc_dom_hdrs[MAXMEMDOM];

/* Quick check if preparatory work is necessary */
#define	PMC_HOOK_INSTALLED(cmd)	__predict_false(pmc_hook != NULL)

/* Hook invocation; for use within the kernel */
#define	PMC_CALL_HOOK(t, cmd, arg)		\
do {								\
    struct epoch_tracker et;						\
	epoch_enter_preempt(global_epoch_preempt, &et);		\
	if (pmc_hook != NULL)			\
		(pmc_hook)((t), (cmd), (arg));	\
	epoch_exit_preempt(global_epoch_preempt, &et);	\
} while (0)

/* Hook invocation that needs an exclusive lock */
#define	PMC_CALL_HOOK_X(t, cmd, arg)		\
do {						\
	sx_xlock(&pmc_sx);			\
	if (pmc_hook != NULL)			\
		(pmc_hook)((t), (cmd), (arg));	\
	sx_xunlock(&pmc_sx);			\
} while (0)

/*
 * Some hook invocations (e.g., from context switch and clock handling
 * code) need to be lock-free.
 */
#define	PMC_CALL_HOOK_UNLOCKED(t, cmd, arg)	\
do {						\
	if (pmc_hook != NULL)				\
		(pmc_hook)((t), (cmd), (arg));	\
} while (0)

#define	PMC_SWITCH_CONTEXT(t,cmd)	PMC_CALL_HOOK_UNLOCKED(t,cmd,NULL)

/* Check if a process is using HWPMCs.*/
#define PMC_PROC_IS_USING_PMCS(p)				\
	(__predict_false(p->p_flag & P_HWPMC))

#define PMC_THREAD_HAS_SAMPLES(td)				\
	(__predict_false((td)->td_pmcpend))

/* Check if a thread have pending user capture. */
#define PMC_IS_PENDING_CALLCHAIN(p)				\
	(__predict_false((p)->td_pflags & TDP_CALLCHAIN))

#define	PMC_SYSTEM_SAMPLING_ACTIVE()		(pmc_ss_count > 0)

/* Check if a CPU has recorded samples. */
#define	PMC_CPU_HAS_SAMPLES(C)	(__predict_false(DPCPU_ID_GET((C), pmc_sampled)))

/*
 * Helper functions.
 */
int		pmc_cpu_is_disabled(int _cpu);  /* deprecated */
int		pmc_cpu_is_active(int _cpu);
int		pmc_cpu_is_present(int _cpu);
int		pmc_cpu_is_primary(int _cpu);
unsigned int	pmc_cpu_max(void);

#ifdef	INVARIANTS
int		pmc_cpu_max_active(void);
#endif

/*
 * Soft events functions.
 */
void pmc_soft_ev_register(struct pmc_soft *ps);
void pmc_soft_ev_deregister(struct pmc_soft *ps);
struct pmc_soft *pmc_soft_ev_acquire(enum pmc_event ev);
void pmc_soft_ev_release(struct pmc_soft *ps);

#endif /* _SYS_PMCKERN_H_ */
