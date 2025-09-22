/*	$OpenBSD: sched.h,v 1.77 2025/06/09 10:57:46 claudio Exp $	*/
/* $NetBSD: sched.h,v 1.2 1999/02/28 18:14:58 ross Exp $ */

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ross Harvey.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)kern_clock.c	8.5 (Berkeley) 1/21/94
 */

#ifndef	_SYS_SCHED_H_
#define	_SYS_SCHED_H_

/*
 * Posix defines a <sched.h> which may want to include <sys/sched.h>
 */

/*
 * CPU states.
 * XXX Not really scheduler state, but no other good place to put
 * it right now, and it really is per-CPU.
 */
#define CP_USER		0
#define CP_NICE		1
#define CP_SYS		2
#define CP_SPIN		3
#define CP_INTR		4
#define CP_IDLE		5
#define CPUSTATES	6

struct cpustats {
	uint64_t	cs_time[CPUSTATES];	/* CPU state statistics */
	uint64_t	cs_flags;		/* see below */
};

#define CPUSTATS_ONLINE		0x0001	/* CPU is schedulable */

#ifdef	_KERNEL

#include <sys/clockintr.h>
#include <sys/queue.h>
#include <sys/pclock.h>

#define	SCHED_NQS	32			/* 32 run queues. */

struct smr_entry;

/*
 * Per-CPU scheduler state.
 *	o	owned (modified only) by this CPU
 */
struct schedstate_percpu {
	struct proc *spc_idleproc;	/* idle proc for this cpu */
	TAILQ_HEAD(prochead, proc) spc_qs[SCHED_NQS];
	TAILQ_HEAD(,proc) spc_deadproc;
	struct timespec spc_runtime;	/* time curproc started running */
	volatile int spc_schedflags;	/* flags; see below */
	u_int spc_schedticks;		/* ticks for schedclock() */
	struct pc_lock spc_cp_time_lock;
	u_int64_t spc_cp_time[CPUSTATES]; /* CPU state statistics */

	struct clockintr spc_itimer;	/* [o] itimer_update handle */
	struct clockintr spc_profclock;	/* [o] profclock handle */
	struct clockintr spc_roundrobin;/* [o] roundrobin handle */
	struct clockintr spc_statclock;	/* [o] statclock handle */

	u_int spc_nrun;			/* procs on the run queues */

	volatile uint32_t spc_whichqs;
	volatile u_int spc_spinning;	/* this cpu is currently spinning */

	SIMPLEQ_HEAD(, smr_entry) spc_deferred; /* deferred smr calls */
	u_int spc_ndeferred;		/* number of deferred smr calls */
	u_int spc_smrdepth;		/* level of smr nesting */
	u_char spc_smrexpedite;		/* if set, dispatch smr entries
					 * without delay */
	u_char spc_smrgp;		/* this CPU's view of grace period */
	volatile u_char spc_curpriority; /* [o] usrpri of curproc */
};

/* spc_flags */
#define SPCF_SEENRR             0x0001  /* process has seen roundrobin() */
#define SPCF_SHOULDYIELD        0x0002  /* process should yield the CPU */
#define SPCF_SWITCHCLEAR        (SPCF_SEENRR|SPCF_SHOULDYIELD)
#define SPCF_SHOULDHALT		0x0004	/* CPU should be vacated */
#define SPCF_HALTED		0x0008	/* CPU has been halted */
#define SPCF_PROFCLOCK		0x0010	/* profclock() was started */
#define SPCF_ITIMER		0x0020	/* itimer_update() was started */

#define	SCHED_PPQ	(128 / SCHED_NQS)	/* priorities per queue */
#define NICE_WEIGHT 2			/* priorities per nice level */
#define	ESTCPULIM(e) min((e), NICE_WEIGHT * PRIO_MAX - SCHED_PPQ)

extern uint64_t roundrobin_period;

struct proc;
void schedclock(struct proc *);
struct clockrequest;
void roundrobin(struct clockrequest *, void *, void *);
void scheduler_start(void);
void userret(struct proc *p);

struct cpu_info;
void sched_init(void);
void sched_init_cpu(struct cpu_info *);
void sched_idle(void *);
void sched_exit(struct proc *);
void sched_toidle(void);
void mi_switch(void);
void cpu_switchto(struct proc *, struct proc *);
struct proc *sched_chooseproc(void);
struct cpu_info *sched_choosecpu(struct proc *);
struct cpu_info *sched_choosecpu_fork(struct proc *parent, int);
void cpu_idle_enter(void);
void cpu_idle_cycle(void);
void cpu_idle_leave(void);
void sched_peg_curproc(struct cpu_info *ci);
void sched_unpeg_curproc(void);
void sched_barrier(struct cpu_info *ci);

int sysctl_hwsetperf(void *, size_t *, void *, size_t);
int sysctl_hwperfpolicy(void *, size_t *, void *, size_t);
int sysctl_hwsmt(void *, size_t *, void *, size_t);
int sysctl_hwncpuonline(void);

#ifdef MULTIPROCESSOR
void sched_start_secondary_cpus(void);
void sched_stop_secondary_cpus(void);
#endif

#define cpu_is_idle(ci)	((ci)->ci_schedstate.spc_whichqs == 0)
int	cpu_is_online(struct cpu_info *);

void setrunqueue(struct cpu_info *, struct proc *, uint8_t);
void remrunqueue(struct proc *);

/* Chargeback parents for the sins of their children.  */
#define scheduler_wait_hook(parent, child) do {				\
	(parent)->p_estcpu = ESTCPULIM((parent)->p_estcpu + (child)->p_estcpu);\
} while (0)

/* Allow other processes to progress */
#define	sched_pause(func) do {						\
	if (curcpu()->ci_schedstate.spc_schedflags & SPCF_SHOULDYIELD)	\
		func();							\
} while (0)

extern struct mutex sched_lock;

#define	SCHED_ASSERT_LOCKED()	MUTEX_ASSERT_LOCKED(&sched_lock)
#define	SCHED_ASSERT_UNLOCKED()	MUTEX_ASSERT_UNLOCKED(&sched_lock)

#define	SCHED_LOCK_INIT()	mtx_init(&sched_lock, IPL_SCHED)
#define	SCHED_LOCK()		mtx_enter(&sched_lock)
#define	SCHED_UNLOCK()		mtx_leave(&sched_lock)

#endif	/* _KERNEL */
#endif	/* _SYS_SCHED_H_ */
