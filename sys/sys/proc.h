/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1986, 1989, 1991, 1993
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
 *	@(#)proc.h	8.15 (Berkeley) 5/19/95
 * $FreeBSD$
 */

#ifndef _SYS_PROC_H_
#define	_SYS_PROC_H_

#include <sys/callout.h>		/* For struct callout. */
#include <sys/event.h>			/* For struct klist. */
#include <sys/condvar.h>
#ifndef _KERNEL
#include <sys/filedesc.h>
#endif
#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/lock_profile.h>
#include <sys/_mutex.h>
#include <sys/osd.h>
#include <sys/priority.h>
#include <sys/rtprio.h>			/* XXX. */
#include <sys/runq.h>
#include <sys/resource.h>
#include <sys/sigio.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#ifndef _KERNEL
#include <sys/time.h>			/* For structs itimerval, timeval. */
#else
#include <sys/pcpu.h>
#include <sys/systm.h>
#endif
#include <sys/ucontext.h>
#include <sys/ucred.h>
#include <sys/types.h>
#include <sys/_domainset.h>

#include <machine/proc.h>		/* Machine-dependent proc substruct. */
#ifdef _KERNEL
#include <machine/cpu.h>
#endif

/*
 * One structure allocated per session.
 *
 * List of locks
 * (m)		locked by s_mtx mtx
 * (e)		locked by proctree_lock sx
 * (c)		const until freeing
 */
struct session {
	u_int		s_count;	/* Ref cnt; pgrps in session - atomic. */
	struct proc	*s_leader;	/* (m + e) Session leader. */
	struct vnode	*s_ttyvp;	/* (m) Vnode of controlling tty. */
	struct cdev_priv *s_ttydp;	/* (m) Device of controlling tty.  */
	struct tty	*s_ttyp;	/* (e) Controlling tty. */
	pid_t		s_sid;		/* (c) Session ID. */
					/* (m) Setlogin() name: */
	char		s_login[roundup(MAXLOGNAME, sizeof(long))];
	struct mtx	s_mtx;		/* Mutex to protect members. */
};

/*
 * One structure allocated per process group.
 *
 * List of locks
 * (m)		locked by pg_mtx mtx
 * (e)		locked by proctree_lock sx
 * (c)		const until freeing
 */
struct pgrp {
	LIST_ENTRY(pgrp) pg_hash;	/* (e) Hash chain. */
	LIST_HEAD(, proc) pg_members;	/* (m + e) Pointer to pgrp members. */
	struct session	*pg_session;	/* (c) Pointer to session. */
	struct sigiolst	pg_sigiolst;	/* (m) List of sigio sources. */
	pid_t		pg_id;		/* (c) Process group id. */
	int		pg_jobc;	/* (m) Job control process count. */
	struct mtx	pg_mtx;		/* Mutex to protect members */
};

/*
 * pargs, used to hold a copy of the command line, if it had a sane length.
 */
struct pargs {
	u_int	ar_ref;		/* Reference count. */
	u_int	ar_length;	/* Length. */
	u_char	ar_args[1];	/* Arguments. */
};

/*-
 * Description of a process.
 *
 * This structure contains the information needed to manage a thread of
 * control, known in UN*X as a process; it has references to substructures
 * containing descriptions of things that the process uses, but may share
 * with related processes.  The process structure and the substructures
 * are always addressable except for those marked "(CPU)" below,
 * which might be addressable only on a processor on which the process
 * is running.
 *
 * Below is a key of locks used to protect each member of struct proc.  The
 * lock is indicated by a reference to a specific character in parens in the
 * associated comment.
 *      * - not yet protected
 *      a - only touched by curproc or parent during fork/wait
 *      b - created at fork, never changes
 *		(exception aiods switch vmspaces, but they are also
 *		marked 'P_SYSTEM' so hopefully it will be left alone)
 *      c - locked by proc mtx
 *      d - locked by allproc_lock lock
 *      e - locked by proctree_lock lock
 *      f - session mtx
 *      g - process group mtx
 *      h - callout_lock mtx
 *      i - by curproc or the master session mtx
 *      j - locked by proc slock
 *      k - only accessed by curthread
 *	k*- only accessed by curthread and from an interrupt
 *	kx- only accessed by curthread and by debugger
 *      l - the attaching proc or attaching proc parent
 *      m - Giant
 *      n - not locked, lazy
 *      o - ktrace lock
 *      q - td_contested lock
 *      r - p_peers lock
 *      s - see sleepq_switch(), sleeping_on_old_rtc(), and sleep(9)
 *      t - thread lock
 *	u - process stat lock
 *	w - process timer lock
 *      x - created at fork, only changes during single threading in exec
 *      y - created at first aio, doesn't change until exit or exec at which
 *          point we are single-threaded and only curthread changes it
 *      z - zombie threads lock
 *
 * If the locking key specifies two identifiers (for example, p_pptr) then
 * either lock is sufficient for read access, but both locks must be held
 * for write access.
 */
struct cpuset;
struct filecaps;
struct filemon;
struct kaioinfo;
struct kaudit_record;
struct kcov_info;
struct kdtrace_proc;
struct kdtrace_thread;
struct mqueue_notifier;
struct nlminfo;
struct p_sched;
struct proc;
struct procdesc;
struct racct;
struct sbuf;
struct sleepqueue;
struct socket;
struct syscall_args;
struct td_sched;
struct thread;
struct trapframe;
struct turnstile;
struct vm_map;
struct vm_map_entry;
struct epoch_tracker;

/*
 * XXX: Does this belong in resource.h or resourcevar.h instead?
 * Resource usage extension.  The times in rusage structs in the kernel are
 * never up to date.  The actual times are kept as runtimes and tick counts
 * (with control info in the "previous" times), and are converted when
 * userland asks for rusage info.  Backwards compatibility prevents putting
 * this directly in the user-visible rusage struct.
 *
 * Locking for p_rux: (cu) means (u) for p_rux and (c) for p_crux.
 * Locking for td_rux: (t) for all fields.
 */
struct rusage_ext {
	uint64_t	rux_runtime;    /* (cu) Real time. */
	uint64_t	rux_uticks;     /* (cu) Statclock hits in user mode. */
	uint64_t	rux_sticks;     /* (cu) Statclock hits in sys mode. */
	uint64_t	rux_iticks;     /* (cu) Statclock hits in intr mode. */
	uint64_t	rux_uu;         /* (c) Previous user time in usec. */
	uint64_t	rux_su;         /* (c) Previous sys time in usec. */
	uint64_t	rux_tu;         /* (c) Previous total time in usec. */
};

/*
 * Kernel runnable context (thread).
 * This is what is put to sleep and reactivated.
 * Thread context.  Processes may have multiple threads.
 */
struct thread {
	struct mtx	*volatile td_lock; /* replaces sched lock */
	struct proc	*td_proc;	/* (*) Associated process. */
	TAILQ_ENTRY(thread) td_plist;	/* (*) All threads in this proc. */
	TAILQ_ENTRY(thread) td_runq;	/* (t) Run queue. */
	TAILQ_ENTRY(thread) td_slpq;	/* (t) Sleep queue. */
	TAILQ_ENTRY(thread) td_lockq;	/* (t) Lock queue. */
	LIST_ENTRY(thread) td_hash;	/* (d) Hash chain. */
	struct cpuset	*td_cpuset;	/* (t) CPU affinity mask. */
	struct domainset_ref td_domain;	/* (a) NUMA policy */
	struct seltd	*td_sel;	/* Select queue/channel. */
	struct sleepqueue *td_sleepqueue; /* (k) Associated sleep queue. */
	struct turnstile *td_turnstile;	/* (k) Associated turnstile. */
	struct rl_q_entry *td_rlqe;	/* (k) Associated range lock entry. */
	struct umtx_q   *td_umtxq;	/* (c?) Link for when we're blocked. */
	lwpid_t		td_tid;		/* (b) Thread ID. */
	sigqueue_t	td_sigqueue;	/* (c) Sigs arrived, not delivered. */
#define	td_siglist	td_sigqueue.sq_signals
	u_char		td_lend_user_pri; /* (t) Lend user pri. */

/* Cleared during fork1() */
#define	td_startzero td_epochnest
	u_char		td_epochnest;	/* (k) Epoch nest counter. */
	int		td_flags;	/* (t) TDF_* flags. */
	int		td_inhibitors;	/* (t) Why can not run. */
	int		td_pflags;	/* (k) Private thread (TDP_*) flags. */
	int		td_dupfd;	/* (k) Ret value from fdopen. XXX */
	int		td_sqqueue;	/* (t) Sleepqueue queue blocked on. */
	void		*td_wchan;	/* (t) Sleep address. */
	const char	*td_wmesg;	/* (t) Reason for sleep. */
	volatile u_char td_owepreempt;  /* (k*) Preempt on last critical_exit */
	u_char		td_tsqueue;	/* (t) Turnstile queue blocked on. */
	short		td_locks;	/* (k) Debug: count of non-spin locks */
	short		td_rw_rlocks;	/* (k) Count of rwlock read locks. */
	short		td_sx_slocks;	/* (k) Count of sx shared locks. */
	short		td_lk_slocks;	/* (k) Count of lockmgr shared locks. */
	short		td_stopsched;	/* (k) Scheduler stopped. */
	struct turnstile *td_blocked;	/* (t) Lock thread is blocked on. */
	const char	*td_lockname;	/* (t) Name of lock blocked on. */
	LIST_HEAD(, turnstile) td_contested;	/* (q) Contested locks. */
	struct lock_list_entry *td_sleeplocks; /* (k) Held sleep locks. */
	int		td_intr_nesting_level; /* (k) Interrupt recursion. */
	int		td_pinned;	/* (k) Temporary cpu pin count. */
	struct ucred	*td_ucred;	/* (k) Reference to credentials. */
	struct plimit	*td_limit;	/* (k) Resource limits. */
	int		td_slptick;	/* (t) Time at sleep. */
	int		td_blktick;	/* (t) Time spent blocked. */
	int		td_swvoltick;	/* (t) Time at last SW_VOL switch. */
	int		td_swinvoltick;	/* (t) Time at last SW_INVOL switch. */
	u_int		td_cow;		/* (*) Number of copy-on-write faults */
	struct rusage	td_ru;		/* (t) rusage information. */
	struct rusage_ext td_rux;	/* (t) Internal rusage information. */
	uint64_t	td_incruntime;	/* (t) Cpu ticks to transfer to proc. */
	uint64_t	td_runtime;	/* (t) How many cpu ticks we've run. */
	u_int 		td_pticks;	/* (t) Statclock hits for profiling */
	u_int		td_sticks;	/* (t) Statclock hits in system mode. */
	u_int		td_iticks;	/* (t) Statclock hits in intr mode. */
	u_int		td_uticks;	/* (t) Statclock hits in user mode. */
	int		td_intrval;	/* (t) Return value for sleepq. */
	sigset_t	td_oldsigmask;	/* (k) Saved mask from pre sigpause. */
	volatile u_int	td_generation;	/* (k) For detection of preemption */
	stack_t		td_sigstk;	/* (k) Stack ptr and on-stack flag. */
	int		td_xsig;	/* (c) Signal for ptrace */
	u_long		td_profil_addr;	/* (k) Temporary addr until AST. */
	u_int		td_profil_ticks; /* (k) Temporary ticks until AST. */
	char		td_name[MAXCOMLEN + 1];	/* (*) Thread name. */
	struct file	*td_fpop;	/* (k) file referencing cdev under op */
	int		td_dbgflags;	/* (c) Userland debugger flags */
	siginfo_t	td_si;		/* (c) For debugger or core file */
	int		td_ng_outbound;	/* (k) Thread entered ng from above. */
	struct osd	td_osd;		/* (k) Object specific data. */
	struct vm_map_entry *td_map_def_user; /* (k) Deferred entries. */
	pid_t		td_dbg_forked;	/* (c) Child pid for debugger. */
	u_int		td_vp_reserv;	/* (k) Count of reserved vnodes. */
	int		td_no_sleeping;	/* (k) Sleeping disabled count. */
	void		*td_su;		/* (k) FFS SU private */
	sbintime_t	td_sleeptimo;	/* (t) Sleep timeout. */
	int		td_rtcgen;	/* (s) rtc_generation of abs. sleep */
	size_t		td_vslock_sz;	/* (k) amount of vslock-ed space */
	struct kcov_info *td_kcov_info;	/* (*) Kernel code coverage data */
#define	td_endzero td_sigmask

/* Copied during fork1() or create_thread(). */
#define	td_startcopy td_endzero
	sigset_t	td_sigmask;	/* (c) Current signal mask. */
	u_char		td_rqindex;	/* (t) Run queue index. */
	u_char		td_base_pri;	/* (t) Thread base kernel priority. */
	u_char		td_priority;	/* (t) Thread active priority. */
	u_char		td_pri_class;	/* (t) Scheduling class. */
	u_char		td_user_pri;	/* (t) User pri from estcpu and nice. */
	u_char		td_base_user_pri; /* (t) Base user pri */
	u_char		td_pre_epoch_prio; /* (k) User pri on entry to epoch */
	uintptr_t	td_rb_list;	/* (k) Robust list head. */
	uintptr_t	td_rbp_list;	/* (k) Robust priv list head. */
	uintptr_t	td_rb_inact;	/* (k) Current in-action mutex loc. */
	struct syscall_args td_sa;	/* (kx) Syscall parameters. Copied on
					   fork for child tracing. */
#define	td_endcopy td_pcb

/*
 * Fields that must be manually set in fork1() or create_thread()
 * or already have been set in the allocator, constructor, etc.
 */
	struct pcb	*td_pcb;	/* (k) Kernel VA of pcb and kstack. */
	enum td_states {
		TDS_INACTIVE = 0x0,
		TDS_INHIBITED,
		TDS_CAN_RUN,
		TDS_RUNQ,
		TDS_RUNNING
	} td_state;			/* (t) thread state */
	union {
		register_t	tdu_retval[2];
		off_t		tdu_off;
	} td_uretoff;			/* (k) Syscall aux returns. */
#define td_retval	td_uretoff.tdu_retval
	u_int		td_cowgen;	/* (k) Generation of COW pointers. */
	/* LP64 hole */
	struct callout	td_slpcallout;	/* (h) Callout for sleep. */
	struct trapframe *td_frame;	/* (k) */
	struct vm_object *td_kstack_obj;/* (a) Kstack object. */
	vm_offset_t	td_kstack;	/* (a) Kernel VA of kstack. */
	int		td_kstack_pages; /* (a) Size of the kstack. */
	volatile u_int	td_critnest;	/* (k*) Critical section nest level. */
	struct mdthread td_md;		/* (k) Any machine-dependent fields. */
	struct kaudit_record	*td_ar;	/* (k) Active audit record, if any. */
	struct lpohead	td_lprof[2];	/* (a) lock profiling objects. */
	struct kdtrace_thread	*td_dtrace; /* (*) DTrace-specific data. */
	int		td_errno;	/* Error returned by last syscall. */
	/* LP64 hole */
	struct vnet	*td_vnet;	/* (k) Effective vnet. */
	const char	*td_vnet_lpush;	/* (k) Debugging vnet push / pop. */
	struct trapframe *td_intr_frame;/* (k) Frame of the current irq */
	struct proc	*td_rfppwait_p;	/* (k) The vforked child */
	struct vm_page	**td_ma;	/* (k) uio pages held */
	int		td_ma_cnt;	/* (k) size of *td_ma */
	/* LP64 hole */
	void		*td_emuldata;	/* Emulator state data */
	int		td_lastcpu;	/* (t) Last cpu we were on. */
	int		td_oncpu;	/* (t) Which cpu we are on. */
	void		*td_lkpi_task;	/* LinuxKPI task struct pointer */
	struct epoch_tracker *td_et;	/* (k) compat KPI spare tracker */
	int		td_pmcpend;
};

struct thread0_storage {
	struct thread t0st_thread;
	uint64_t t0st_sched[10];
};

struct mtx *thread_lock_block(struct thread *);
void thread_lock_unblock(struct thread *, struct mtx *);
void thread_lock_set(struct thread *, struct mtx *);
#define	THREAD_LOCK_ASSERT(td, type)					\
do {									\
	struct mtx *__m = (td)->td_lock;				\
	if (__m != &blocked_lock)					\
		mtx_assert(__m, (type));				\
} while (0)

#ifdef INVARIANTS
#define	THREAD_LOCKPTR_ASSERT(td, lock)					\
do {									\
	struct mtx *__m = (td)->td_lock;				\
	KASSERT((__m == &blocked_lock || __m == (lock)),		\
	    ("Thread %p lock %p does not match %p", td, __m, (lock)));	\
} while (0)

#define	TD_LOCKS_INC(td)	((td)->td_locks++)
#define	TD_LOCKS_DEC(td) do {						\
	KASSERT(SCHEDULER_STOPPED_TD(td) || (td)->td_locks > 0,		\
	    ("thread %p owns no locks", (td)));				\
	(td)->td_locks--;						\
} while (0)
#else
#define	THREAD_LOCKPTR_ASSERT(td, lock)

#define	TD_LOCKS_INC(td)
#define	TD_LOCKS_DEC(td)
#endif

/*
 * Flags kept in td_flags:
 * To change these you MUST have the scheduler lock.
 */
#define	TDF_BORROWING	0x00000001 /* Thread is borrowing pri from another. */
#define	TDF_INPANIC	0x00000002 /* Caused a panic, let it drive crashdump. */
#define	TDF_INMEM	0x00000004 /* Thread's stack is in memory. */
#define	TDF_SINTR	0x00000008 /* Sleep is interruptible. */
#define	TDF_TIMEOUT	0x00000010 /* Timing out during sleep. */
#define	TDF_IDLETD	0x00000020 /* This is a per-CPU idle thread. */
#define	TDF_CANSWAP	0x00000040 /* Thread can be swapped. */
#define	TDF_SLEEPABORT	0x00000080 /* sleepq_abort was called. */
#define	TDF_KTH_SUSP	0x00000100 /* kthread is suspended */
#define	TDF_ALLPROCSUSP	0x00000200 /* suspended by SINGLE_ALLPROC */
#define	TDF_BOUNDARY	0x00000400 /* Thread suspended at user boundary */
#define	TDF_ASTPENDING	0x00000800 /* Thread has some asynchronous events. */
#define	TDF_UNUSED12	0x00001000 /* --available-- */
#define	TDF_SBDRY	0x00002000 /* Stop only on usermode boundary. */
#define	TDF_UPIBLOCKED	0x00004000 /* Thread blocked on user PI mutex. */
#define	TDF_NEEDSUSPCHK	0x00008000 /* Thread may need to suspend. */
#define	TDF_NEEDRESCHED	0x00010000 /* Thread needs to yield. */
#define	TDF_NEEDSIGCHK	0x00020000 /* Thread may need signal delivery. */
#define	TDF_NOLOAD	0x00040000 /* Ignore during load avg calculations. */
#define	TDF_SERESTART	0x00080000 /* ERESTART on stop attempts. */
#define	TDF_THRWAKEUP	0x00100000 /* Libthr thread must not suspend itself. */
#define	TDF_SEINTR	0x00200000 /* EINTR on stop attempts. */
#define	TDF_SWAPINREQ	0x00400000 /* Swapin request due to wakeup. */
#define	TDF_UNUSED23	0x00800000 /* --available-- */
#define	TDF_SCHED0	0x01000000 /* Reserved for scheduler private use */
#define	TDF_SCHED1	0x02000000 /* Reserved for scheduler private use */
#define	TDF_SCHED2	0x04000000 /* Reserved for scheduler private use */
#define	TDF_SCHED3	0x08000000 /* Reserved for scheduler private use */
#define	TDF_ALRMPEND	0x10000000 /* Pending SIGVTALRM needs to be posted. */
#define	TDF_PROFPEND	0x20000000 /* Pending SIGPROF needs to be posted. */
#define	TDF_MACPEND	0x40000000 /* AST-based MAC event pending. */

/* Userland debug flags */
#define	TDB_SUSPEND	0x00000001 /* Thread is suspended by debugger */
#define	TDB_XSIG	0x00000002 /* Thread is exchanging signal under trace */
#define	TDB_USERWR	0x00000004 /* Debugger modified memory or registers */
#define	TDB_SCE		0x00000008 /* Thread performs syscall enter */
#define	TDB_SCX		0x00000010 /* Thread performs syscall exit */
#define	TDB_EXEC	0x00000020 /* TDB_SCX from exec(2) family */
#define	TDB_FORK	0x00000040 /* TDB_SCX from fork(2) that created new
				      process */
#define	TDB_STOPATFORK	0x00000080 /* Stop at the return from fork (child
				      only) */
#define	TDB_CHILD	0x00000100 /* New child indicator for ptrace() */
#define	TDB_BORN	0x00000200 /* New LWP indicator for ptrace() */
#define	TDB_EXIT	0x00000400 /* Exiting LWP indicator for ptrace() */
#define	TDB_VFORK	0x00000800 /* vfork indicator for ptrace() */
#define	TDB_FSTP	0x00001000 /* The thread is PT_ATTACH leader */
#define	TDB_STEP	0x00002000 /* (x86) PSL_T set for PT_STEP */

/*
 * "Private" flags kept in td_pflags:
 * These are only written by curthread and thus need no locking.
 */
#define	TDP_OLDMASK	0x00000001 /* Need to restore mask after suspend. */
#define	TDP_INKTR	0x00000002 /* Thread is currently in KTR code. */
#define	TDP_INKTRACE	0x00000004 /* Thread is currently in KTRACE code. */
#define	TDP_BUFNEED	0x00000008 /* Do not recurse into the buf flush */
#define	TDP_COWINPROGRESS 0x00000010 /* Snapshot copy-on-write in progress. */
#define	TDP_ALTSTACK	0x00000020 /* Have alternate signal stack. */
#define	TDP_DEADLKTREAT	0x00000040 /* Lock acquisition - deadlock treatment. */
#define	TDP_NOFAULTING	0x00000080 /* Do not handle page faults. */
#define	TDP_UNUSED9	0x00000100 /* --available-- */
#define	TDP_OWEUPC	0x00000200 /* Call addupc() at next AST. */
#define	TDP_ITHREAD	0x00000400 /* Thread is an interrupt thread. */
#define	TDP_SYNCIO	0x00000800 /* Local override, disable async i/o. */
#define	TDP_SCHED1	0x00001000 /* Reserved for scheduler private use */
#define	TDP_SCHED2	0x00002000 /* Reserved for scheduler private use */
#define	TDP_SCHED3	0x00004000 /* Reserved for scheduler private use */
#define	TDP_SCHED4	0x00008000 /* Reserved for scheduler private use */
#define	TDP_GEOM	0x00010000 /* Settle GEOM before finishing syscall */
#define	TDP_SOFTDEP	0x00020000 /* Stuck processing softdep worklist */
#define	TDP_NORUNNINGBUF 0x00040000 /* Ignore runningbufspace check */
#define	TDP_WAKEUP	0x00080000 /* Don't sleep in umtx cond_wait */
#define	TDP_INBDFLUSH	0x00100000 /* Already in BO_BDFLUSH, do not recurse */
#define	TDP_KTHREAD	0x00200000 /* This is an official kernel thread */
#define	TDP_CALLCHAIN	0x00400000 /* Capture thread's callchain */
#define	TDP_IGNSUSP	0x00800000 /* Permission to ignore the MNTK_SUSPEND* */
#define	TDP_AUDITREC	0x01000000 /* Audit record pending on thread */
#define	TDP_RFPPWAIT	0x02000000 /* Handle RFPPWAIT on syscall exit */
#define	TDP_RESETSPUR	0x04000000 /* Reset spurious page fault history. */
#define	TDP_NERRNO	0x08000000 /* Last errno is already in td_errno */
#define	TDP_UIOHELD	0x10000000 /* Current uio has pages held in td_ma */
#define	TDP_FORKING	0x20000000 /* Thread is being created through fork() */
#define	TDP_EXECVMSPC	0x40000000 /* Execve destroyed old vmspace */

/*
 * Reasons that the current thread can not be run yet.
 * More than one may apply.
 */
#define	TDI_SUSPENDED	0x0001	/* On suspension queue. */
#define	TDI_SLEEPING	0x0002	/* Actually asleep! (tricky). */
#define	TDI_SWAPPED	0x0004	/* Stack not in mem.  Bad juju if run. */
#define	TDI_LOCK	0x0008	/* Stopped on a lock. */
#define	TDI_IWAIT	0x0010	/* Awaiting interrupt. */

#define	TD_IS_SLEEPING(td)	((td)->td_inhibitors & TDI_SLEEPING)
#define	TD_ON_SLEEPQ(td)	((td)->td_wchan != NULL)
#define	TD_IS_SUSPENDED(td)	((td)->td_inhibitors & TDI_SUSPENDED)
#define	TD_IS_SWAPPED(td)	((td)->td_inhibitors & TDI_SWAPPED)
#define	TD_ON_LOCK(td)		((td)->td_inhibitors & TDI_LOCK)
#define	TD_AWAITING_INTR(td)	((td)->td_inhibitors & TDI_IWAIT)
#define	TD_IS_RUNNING(td)	((td)->td_state == TDS_RUNNING)
#define	TD_ON_RUNQ(td)		((td)->td_state == TDS_RUNQ)
#define	TD_CAN_RUN(td)		((td)->td_state == TDS_CAN_RUN)
#define	TD_IS_INHIBITED(td)	((td)->td_state == TDS_INHIBITED)
#define	TD_ON_UPILOCK(td)	((td)->td_flags & TDF_UPIBLOCKED)
#define TD_IS_IDLETHREAD(td)	((td)->td_flags & TDF_IDLETD)

#define	KTDSTATE(td)							\
	(((td)->td_inhibitors & TDI_SLEEPING) != 0 ? "sleep"  :		\
	((td)->td_inhibitors & TDI_SUSPENDED) != 0 ? "suspended" :	\
	((td)->td_inhibitors & TDI_SWAPPED) != 0 ? "swapped" :		\
	((td)->td_inhibitors & TDI_LOCK) != 0 ? "blocked" :		\
	((td)->td_inhibitors & TDI_IWAIT) != 0 ? "iwait" : "yielding")

#define	TD_SET_INHIB(td, inhib) do {			\
	(td)->td_state = TDS_INHIBITED;			\
	(td)->td_inhibitors |= (inhib);			\
} while (0)

#define	TD_CLR_INHIB(td, inhib) do {			\
	if (((td)->td_inhibitors & (inhib)) &&		\
	    (((td)->td_inhibitors &= ~(inhib)) == 0))	\
		(td)->td_state = TDS_CAN_RUN;		\
} while (0)

#define	TD_SET_SLEEPING(td)	TD_SET_INHIB((td), TDI_SLEEPING)
#define	TD_SET_SWAPPED(td)	TD_SET_INHIB((td), TDI_SWAPPED)
#define	TD_SET_LOCK(td)		TD_SET_INHIB((td), TDI_LOCK)
#define	TD_SET_SUSPENDED(td)	TD_SET_INHIB((td), TDI_SUSPENDED)
#define	TD_SET_IWAIT(td)	TD_SET_INHIB((td), TDI_IWAIT)
#define	TD_SET_EXITING(td)	TD_SET_INHIB((td), TDI_EXITING)

#define	TD_CLR_SLEEPING(td)	TD_CLR_INHIB((td), TDI_SLEEPING)
#define	TD_CLR_SWAPPED(td)	TD_CLR_INHIB((td), TDI_SWAPPED)
#define	TD_CLR_LOCK(td)		TD_CLR_INHIB((td), TDI_LOCK)
#define	TD_CLR_SUSPENDED(td)	TD_CLR_INHIB((td), TDI_SUSPENDED)
#define	TD_CLR_IWAIT(td)	TD_CLR_INHIB((td), TDI_IWAIT)

#define	TD_SET_RUNNING(td)	(td)->td_state = TDS_RUNNING
#define	TD_SET_RUNQ(td)		(td)->td_state = TDS_RUNQ
#define	TD_SET_CAN_RUN(td)	(td)->td_state = TDS_CAN_RUN

#define	TD_SBDRY_INTR(td) \
    (((td)->td_flags & (TDF_SEINTR | TDF_SERESTART)) != 0)
#define	TD_SBDRY_ERRNO(td) \
    (((td)->td_flags & TDF_SEINTR) != 0 ? EINTR : ERESTART)

/*
 * Process structure.
 */
struct proc {
	LIST_ENTRY(proc) p_list;	/* (d) List of all processes. */
	TAILQ_HEAD(, thread) p_threads;	/* (c) all threads. */
	struct mtx	p_slock;	/* process spin lock */
	struct ucred	*p_ucred;	/* (c) Process owner's identity. */
	struct filedesc	*p_fd;		/* (b) Open files. */
	struct filedesc_to_leader *p_fdtol; /* (b) Tracking node */
	struct pstats	*p_stats;	/* (b) Accounting/statistics (CPU). */
	struct plimit	*p_limit;	/* (c) Resource limits. */
	struct callout	p_limco;	/* (c) Limit callout handle */
	struct sigacts	*p_sigacts;	/* (x) Signal actions, state (CPU). */

	int		p_flag;		/* (c) P_* flags. */
	int		p_flag2;	/* (c) P2_* flags. */
	enum p_states {
		PRS_NEW = 0,		/* In creation */
		PRS_NORMAL,		/* threads can be run. */
		PRS_ZOMBIE
	} p_state;			/* (j/c) Process status. */
	pid_t		p_pid;		/* (b) Process identifier. */
	LIST_ENTRY(proc) p_hash;	/* (d) Hash chain. */
	LIST_ENTRY(proc) p_pglist;	/* (g + e) List of processes in pgrp. */
	struct proc	*p_pptr;	/* (c + e) Pointer to parent process. */
	LIST_ENTRY(proc) p_sibling;	/* (e) List of sibling processes. */
	LIST_HEAD(, proc) p_children;	/* (e) Pointer to list of children. */
	struct proc	*p_reaper;	/* (e) My reaper. */
	LIST_HEAD(, proc) p_reaplist;	/* (e) List of my descendants
					       (if I am reaper). */
	LIST_ENTRY(proc) p_reapsibling;	/* (e) List of siblings - descendants of
					       the same reaper. */
	struct mtx	p_mtx;		/* (n) Lock for this struct. */
	struct mtx	p_statmtx;	/* Lock for the stats */
	struct mtx	p_itimmtx;	/* Lock for the virt/prof timers */
	struct mtx	p_profmtx;	/* Lock for the profiling */
	struct ksiginfo *p_ksi;	/* Locked by parent proc lock */
	sigqueue_t	p_sigqueue;	/* (c) Sigs not delivered to a td. */
#define p_siglist	p_sigqueue.sq_signals
	pid_t		p_oppid;	/* (c + e) Real parent pid. */

/* The following fields are all zeroed upon creation in fork. */
#define	p_startzero	p_vmspace
	struct vmspace	*p_vmspace;	/* (b) Address space. */
	u_int		p_swtick;	/* (c) Tick when swapped in or out. */
	u_int		p_cowgen;	/* (c) Generation of COW pointers. */
	struct itimerval p_realtimer;	/* (c) Alarm timer. */
	struct rusage	p_ru;		/* (a) Exit information. */
	struct rusage_ext p_rux;	/* (cu) Internal resource usage. */
	struct rusage_ext p_crux;	/* (c) Internal child resource usage. */
	int		p_profthreads;	/* (c) Num threads in addupc_task. */
	volatile int	p_exitthreads;	/* (j) Number of threads exiting */
	int		p_traceflag;	/* (o) Kernel trace points. */
	struct vnode	*p_tracevp;	/* (c + o) Trace to vnode. */
	struct ucred	*p_tracecred;	/* (o) Credentials to trace with. */
	struct vnode	*p_textvp;	/* (b) Vnode of executable. */
	u_int		p_lock;		/* (c) Proclock (prevent swap) count. */
	struct sigiolst	p_sigiolst;	/* (c) List of sigio sources. */
	int		p_sigparent;	/* (c) Signal to parent on exit. */
	int		p_sig;		/* (n) For core dump/debugger XXX. */
	u_long		p_code;		/* (n) For core dump/debugger XXX. */
	u_int		p_stops;	/* (c) Stop event bitmask. */
	u_int		p_stype;	/* (c) Stop event type. */
	char		p_step;		/* (c) Process is stopped. */
	u_char		p_pfsflags;	/* (c) Procfs flags. */
	u_int		p_ptevents;	/* (c + e) ptrace() event mask. */
	struct nlminfo	*p_nlminfo;	/* (?) Only used by/for lockd. */
	struct kaioinfo	*p_aioinfo;	/* (y) ASYNC I/O info. */
	struct thread	*p_singlethread;/* (c + j) If single threading this is it */
	int		p_suspcount;	/* (j) Num threads in suspended mode. */
	struct thread	*p_xthread;	/* (c) Trap thread */
	int		p_boundary_count;/* (j) Num threads at user boundary */
	int		p_pendingcnt;	/* how many signals are pending */
	struct itimers	*p_itimers;	/* (c) POSIX interval timers. */
	struct procdesc	*p_procdesc;	/* (e) Process descriptor, if any. */
	u_int		p_treeflag;	/* (e) P_TREE flags */
	int		p_pendingexits; /* (c) Count of pending thread exits. */
	struct filemon	*p_filemon;	/* (c) filemon-specific data. */
	int		p_pdeathsig;	/* (c) Signal from parent on exit. */
/* End area that is zeroed on creation. */
#define	p_endzero	p_magic

/* The following fields are all copied upon creation in fork. */
#define	p_startcopy	p_endzero
	u_int		p_magic;	/* (b) Magic number. */
	int		p_osrel;	/* (x) osreldate for the
					       binary (from ELF note, if any) */
	uint32_t	p_fctl0;	/* (x) ABI feature control, ELF note */
	char		p_comm[MAXCOMLEN + 1];	/* (x) Process name. */
	struct sysentvec *p_sysent;	/* (b) Syscall dispatch info. */
	struct pargs	*p_args;	/* (c) Process arguments. */
	rlim_t		p_cpulimit;	/* (c) Current CPU limit in seconds. */
	signed char	p_nice;		/* (c) Process "nice" value. */
	int		p_fibnum;	/* in this routing domain XXX MRT */
	pid_t		p_reapsubtree;	/* (e) Pid of the direct child of the
					       reaper which spawned
					       our subtree. */
	uint16_t	p_elf_machine;	/* (x) ELF machine type */
	uint64_t	p_elf_flags;	/* (x) ELF flags */
/* End area that is copied on creation. */
#define	p_endcopy	p_xexit

	u_int		p_xexit;	/* (c) Exit code. */
	u_int		p_xsig;		/* (c) Stop/kill sig. */
	struct pgrp	*p_pgrp;	/* (c + e) Pointer to process group. */
	struct knlist	*p_klist;	/* (c) Knotes attached to this proc. */
	int		p_numthreads;	/* (c) Number of threads. */
	struct mdproc	p_md;		/* Any machine-dependent fields. */
	struct callout	p_itcallout;	/* (h + c) Interval timer callout. */
	u_short		p_acflag;	/* (c) Accounting flags. */
	struct proc	*p_peers;	/* (r) */
	struct proc	*p_leader;	/* (b) */
	void		*p_emuldata;	/* (c) Emulator state data. */
	struct label	*p_label;	/* (*) Proc (not subject) MAC label. */
	STAILQ_HEAD(, ktr_request)	p_ktr;	/* (o) KTR event queue. */
	LIST_HEAD(, mqueue_notifier)	p_mqnotifier; /* (c) mqueue notifiers.*/
	struct kdtrace_proc	*p_dtrace; /* (*) DTrace-specific data. */
	struct cv	p_pwait;	/* (*) wait cv for exit/exec. */
	uint64_t	p_prev_runtime;	/* (c) Resource usage accounting. */
	struct racct	*p_racct;	/* (b) Resource accounting. */
	int		p_throttled;	/* (c) Flag for racct pcpu throttling */
	/*
	 * An orphan is the child that has been re-parented to the
	 * debugger as a result of attaching to it.  Need to keep
	 * track of them for parent to be able to collect the exit
	 * status of what used to be children.
	 */
	LIST_ENTRY(proc) p_orphan;	/* (e) List of orphan processes. */
	LIST_HEAD(, proc) p_orphans;	/* (e) Pointer to list of orphans. */
};

#define	p_session	p_pgrp->pg_session
#define	p_pgid		p_pgrp->pg_id

#define	NOCPU		(-1)	/* For when we aren't on a CPU. */
#define	NOCPU_OLD	(255)
#define	MAXCPU_OLD	(254)

#define	PROC_SLOCK(p)	mtx_lock_spin(&(p)->p_slock)
#define	PROC_SUNLOCK(p)	mtx_unlock_spin(&(p)->p_slock)
#define	PROC_SLOCK_ASSERT(p, type)	mtx_assert(&(p)->p_slock, (type))

#define	PROC_STATLOCK(p)	mtx_lock_spin(&(p)->p_statmtx)
#define	PROC_STATUNLOCK(p)	mtx_unlock_spin(&(p)->p_statmtx)
#define	PROC_STATLOCK_ASSERT(p, type)	mtx_assert(&(p)->p_statmtx, (type))

#define	PROC_ITIMLOCK(p)	mtx_lock_spin(&(p)->p_itimmtx)
#define	PROC_ITIMUNLOCK(p)	mtx_unlock_spin(&(p)->p_itimmtx)
#define	PROC_ITIMLOCK_ASSERT(p, type)	mtx_assert(&(p)->p_itimmtx, (type))

#define	PROC_PROFLOCK(p)	mtx_lock_spin(&(p)->p_profmtx)
#define	PROC_PROFUNLOCK(p)	mtx_unlock_spin(&(p)->p_profmtx)
#define	PROC_PROFLOCK_ASSERT(p, type)	mtx_assert(&(p)->p_profmtx, (type))

/* These flags are kept in p_flag. */
#define	P_ADVLOCK	0x00001	/* Process may hold a POSIX advisory lock. */
#define	P_CONTROLT	0x00002	/* Has a controlling terminal. */
#define	P_KPROC		0x00004	/* Kernel process. */
#define	P_UNUSED3	0x00008	/* --available-- */
#define	P_PPWAIT	0x00010	/* Parent is waiting for child to exec/exit. */
#define	P_PROFIL	0x00020	/* Has started profiling. */
#define	P_STOPPROF	0x00040	/* Has thread requesting to stop profiling. */
#define	P_HADTHREADS	0x00080	/* Has had threads (no cleanup shortcuts) */
#define	P_SUGID		0x00100	/* Had set id privileges since last exec. */
#define	P_SYSTEM	0x00200	/* System proc: no sigs, stats or swapping. */
#define	P_SINGLE_EXIT	0x00400	/* Threads suspending should exit, not wait. */
#define	P_TRACED	0x00800	/* Debugged process being traced. */
#define	P_WAITED	0x01000	/* Someone is waiting for us. */
#define	P_WEXIT		0x02000	/* Working on exiting. */
#define	P_EXEC		0x04000	/* Process called exec. */
#define	P_WKILLED	0x08000	/* Killed, go to kernel/user boundary ASAP. */
#define	P_CONTINUED	0x10000	/* Proc has continued from a stopped state. */
#define	P_STOPPED_SIG	0x20000	/* Stopped due to SIGSTOP/SIGTSTP. */
#define	P_STOPPED_TRACE	0x40000	/* Stopped because of tracing. */
#define	P_STOPPED_SINGLE 0x80000 /* Only 1 thread can continue (not to user). */
#define	P_PROTECTED	0x100000 /* Do not kill on memory overcommit. */
#define	P_SIGEVENT	0x200000 /* Process pending signals changed. */
#define	P_SINGLE_BOUNDARY 0x400000 /* Threads should suspend at user boundary. */
#define	P_HWPMC		0x800000 /* Process is using HWPMCs */
#define	P_JAILED	0x1000000 /* Process is in jail. */
#define	P_TOTAL_STOP	0x2000000 /* Stopped in stop_all_proc. */
#define	P_INEXEC	0x4000000 /* Process is in execve(). */
#define	P_STATCHILD	0x8000000 /* Child process stopped or exited. */
#define	P_INMEM		0x10000000 /* Loaded into memory. */
#define	P_SWAPPINGOUT	0x20000000 /* Process is being swapped out. */
#define	P_SWAPPINGIN	0x40000000 /* Process is being swapped in. */
#define	P_PPTRACE	0x80000000 /* PT_TRACEME by vforked child. */

#define	P_STOPPED	(P_STOPPED_SIG|P_STOPPED_SINGLE|P_STOPPED_TRACE)
#define	P_SHOULDSTOP(p)	((p)->p_flag & P_STOPPED)
#define	P_KILLED(p)	((p)->p_flag & P_WKILLED)

/* These flags are kept in p_flag2. */
#define	P2_INHERIT_PROTECTED 0x00000001 /* New children get P_PROTECTED. */
#define	P2_NOTRACE	0x00000002	/* No ptrace(2) attach or coredumps. */
#define	P2_NOTRACE_EXEC 0x00000004	/* Keep P2_NOPTRACE on exec(2). */
#define	P2_AST_SU	0x00000008	/* Handles SU ast for kthreads. */
#define	P2_PTRACE_FSTP	0x00000010 /* SIGSTOP from PT_ATTACH not yet handled. */
#define	P2_TRAPCAP	0x00000020	/* SIGTRAP on ENOTCAPABLE */
#define	P2_ASLR_ENABLE	0x00000040	/* Force enable ASLR. */
#define	P2_ASLR_DISABLE	0x00000080	/* Force disable ASLR. */
#define	P2_ASLR_IGNSTART 0x00000100	/* Enable ASLR to consume sbrk area. */

/* Flags protected by proctree_lock, kept in p_treeflags. */
#define	P_TREE_ORPHANED		0x00000001	/* Reparented, on orphan list */
#define	P_TREE_FIRST_ORPHAN	0x00000002	/* First element of orphan
						   list */
#define	P_TREE_REAPER		0x00000004	/* Reaper of subtree */

/*
 * These were process status values (p_stat), now they are only used in
 * legacy conversion code.
 */
#define	SIDL	1		/* Process being created by fork. */
#define	SRUN	2		/* Currently runnable. */
#define	SSLEEP	3		/* Sleeping on an address. */
#define	SSTOP	4		/* Process debugging or suspension. */
#define	SZOMB	5		/* Awaiting collection by parent. */
#define	SWAIT	6		/* Waiting for interrupt. */
#define	SLOCK	7		/* Blocked on a lock. */

#define	P_MAGIC		0xbeefface

#ifdef _KERNEL

/* Types and flags for mi_switch(). */
#define	SW_TYPE_MASK		0xff	/* First 8 bits are switch type */
#define	SWT_NONE		0	/* Unspecified switch. */
#define	SWT_PREEMPT		1	/* Switching due to preemption. */
#define	SWT_OWEPREEMPT		2	/* Switching due to owepreempt. */
#define	SWT_TURNSTILE		3	/* Turnstile contention. */
#define	SWT_SLEEPQ		4	/* Sleepq wait. */
#define	SWT_SLEEPQTIMO		5	/* Sleepq timeout wait. */
#define	SWT_RELINQUISH		6	/* yield call. */
#define	SWT_NEEDRESCHED		7	/* NEEDRESCHED was set. */
#define	SWT_IDLE		8	/* Switching from the idle thread. */
#define	SWT_IWAIT		9	/* Waiting for interrupts. */
#define	SWT_SUSPEND		10	/* Thread suspended. */
#define	SWT_REMOTEPREEMPT	11	/* Remote processor preempted. */
#define	SWT_REMOTEWAKEIDLE	12	/* Remote processor preempted idle. */
#define	SWT_COUNT		13	/* Number of switch types. */
/* Flags */
#define	SW_VOL		0x0100		/* Voluntary switch. */
#define	SW_INVOL	0x0200		/* Involuntary switch. */
#define SW_PREEMPT	0x0400		/* The invol switch is a preemption */

/* How values for thread_single(). */
#define	SINGLE_NO_EXIT	0
#define	SINGLE_EXIT	1
#define	SINGLE_BOUNDARY	2
#define	SINGLE_ALLPROC	3

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_PARGS);
MALLOC_DECLARE(M_PGRP);
MALLOC_DECLARE(M_SESSION);
MALLOC_DECLARE(M_SUBPROC);
#endif

#define	FOREACH_PROC_IN_SYSTEM(p)					\
	LIST_FOREACH((p), &allproc, p_list)
#define	FOREACH_THREAD_IN_PROC(p, td)					\
	TAILQ_FOREACH((td), &(p)->p_threads, td_plist)

#define	FIRST_THREAD_IN_PROC(p)	TAILQ_FIRST(&(p)->p_threads)

/*
 * We use process IDs <= pid_max <= PID_MAX; PID_MAX + 1 must also fit
 * in a pid_t, as it is used to represent "no process group".
 */
#define	PID_MAX		99999
#define	NO_PID		100000
extern pid_t pid_max;

#define	SESS_LEADER(p)	((p)->p_session->s_leader == (p))


#define	STOPEVENT(p, e, v) do {						\
	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,			\
 	    "checking stopevent %d", (e));				\
	if ((p)->p_stops & (e))	{					\
		PROC_LOCK(p);						\
		stopevent((p), (e), (v));				\
		PROC_UNLOCK(p);						\
	}								\
} while (0)
#define	_STOPEVENT(p, e, v) do {					\
	PROC_LOCK_ASSERT(p, MA_OWNED);					\
	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, &p->p_mtx.lock_object, \
 	    "checking stopevent %d", (e));				\
	if ((p)->p_stops & (e))						\
		stopevent((p), (e), (v));				\
} while (0)

/* Lock and unlock a process. */
#define	PROC_LOCK(p)	mtx_lock(&(p)->p_mtx)
#define	PROC_TRYLOCK(p)	mtx_trylock(&(p)->p_mtx)
#define	PROC_UNLOCK(p)	mtx_unlock(&(p)->p_mtx)
#define	PROC_LOCKED(p)	mtx_owned(&(p)->p_mtx)
#define	PROC_LOCK_ASSERT(p, type)	mtx_assert(&(p)->p_mtx, (type))

/* Lock and unlock a process group. */
#define	PGRP_LOCK(pg)	mtx_lock(&(pg)->pg_mtx)
#define	PGRP_UNLOCK(pg)	mtx_unlock(&(pg)->pg_mtx)
#define	PGRP_LOCKED(pg)	mtx_owned(&(pg)->pg_mtx)
#define	PGRP_LOCK_ASSERT(pg, type)	mtx_assert(&(pg)->pg_mtx, (type))

#define	PGRP_LOCK_PGSIGNAL(pg) do {					\
	if ((pg) != NULL)						\
		PGRP_LOCK(pg);						\
} while (0)
#define	PGRP_UNLOCK_PGSIGNAL(pg) do {					\
	if ((pg) != NULL)						\
		PGRP_UNLOCK(pg);					\
} while (0)

/* Lock and unlock a session. */
#define	SESS_LOCK(s)	mtx_lock(&(s)->s_mtx)
#define	SESS_UNLOCK(s)	mtx_unlock(&(s)->s_mtx)
#define	SESS_LOCKED(s)	mtx_owned(&(s)->s_mtx)
#define	SESS_LOCK_ASSERT(s, type)	mtx_assert(&(s)->s_mtx, (type))

/*
 * Non-zero p_lock ensures that:
 * - exit1() is not performed until p_lock reaches zero;
 * - the process' threads stack are not swapped out if they are currently
 *   not (P_INMEM).
 *
 * PHOLD() asserts that the process (except the current process) is
 * not exiting, increments p_lock and swaps threads stacks into memory,
 * if needed.
 * _PHOLD() is same as PHOLD(), it takes the process locked.
 * _PHOLD_LITE() also takes the process locked, but comparing with
 * _PHOLD(), it only guarantees that exit1() is not executed,
 * faultin() is not called.
 */
#define	PHOLD(p) do {							\
	PROC_LOCK(p);							\
	_PHOLD(p);							\
	PROC_UNLOCK(p);							\
} while (0)
#define	_PHOLD(p) do {							\
	PROC_LOCK_ASSERT((p), MA_OWNED);				\
	KASSERT(!((p)->p_flag & P_WEXIT) || (p) == curproc,		\
	    ("PHOLD of exiting process %p", p));			\
	(p)->p_lock++;							\
	if (((p)->p_flag & P_INMEM) == 0)				\
		faultin((p));						\
} while (0)
#define	_PHOLD_LITE(p) do {						\
	PROC_LOCK_ASSERT((p), MA_OWNED);				\
	KASSERT(!((p)->p_flag & P_WEXIT) || (p) == curproc,		\
	    ("PHOLD of exiting process %p", p));			\
	(p)->p_lock++;							\
} while (0)
#define	PROC_ASSERT_HELD(p) do {					\
	KASSERT((p)->p_lock > 0, ("process %p not held", p));		\
} while (0)

#define	PRELE(p) do {							\
	PROC_LOCK((p));							\
	_PRELE((p));							\
	PROC_UNLOCK((p));						\
} while (0)
#define	_PRELE(p) do {							\
	PROC_LOCK_ASSERT((p), MA_OWNED);				\
	PROC_ASSERT_HELD(p);						\
	(--(p)->p_lock);						\
	if (((p)->p_flag & P_WEXIT) && (p)->p_lock == 0)		\
		wakeup(&(p)->p_lock);					\
} while (0)
#define	PROC_ASSERT_NOT_HELD(p) do {					\
	KASSERT((p)->p_lock == 0, ("process %p held", p));		\
} while (0)

#define	PROC_UPDATE_COW(p) do {						\
	PROC_LOCK_ASSERT((p), MA_OWNED);				\
	(p)->p_cowgen++;						\
} while (0)

/* Check whether a thread is safe to be swapped out. */
#define	thread_safetoswapout(td)	((td)->td_flags & TDF_CANSWAP)

/* Control whether or not it is safe for curthread to sleep. */
#define	THREAD_NO_SLEEPING()		((curthread)->td_no_sleeping++)

#define	THREAD_SLEEPING_OK()		((curthread)->td_no_sleeping--)

#define	THREAD_CAN_SLEEP()		((curthread)->td_no_sleeping == 0)

#define	PIDHASH(pid)	(&pidhashtbl[(pid) & pidhash])
#define	PIDHASHLOCK(pid) (&pidhashtbl_lock[((pid) & pidhashlock)])
extern LIST_HEAD(pidhashhead, proc) *pidhashtbl;
extern struct sx *pidhashtbl_lock;
extern u_long pidhash;
extern u_long pidhashlock;
#define	TIDHASH(tid)	(&tidhashtbl[(tid) & tidhash])
extern LIST_HEAD(tidhashhead, thread) *tidhashtbl;
extern u_long tidhash;
extern struct rwlock tidhash_lock;

#define	PGRPHASH(pgid)	(&pgrphashtbl[(pgid) & pgrphash])
extern LIST_HEAD(pgrphashhead, pgrp) *pgrphashtbl;
extern u_long pgrphash;

extern struct sx allproc_lock;
extern int allproc_gen;
extern struct sx zombproc_lock;
extern struct sx proctree_lock;
extern struct mtx ppeers_lock;
extern struct mtx procid_lock;
extern struct proc proc0;		/* Process slot for swapper. */
extern struct thread0_storage thread0_st;	/* Primary thread in proc0. */
#define	thread0 (thread0_st.t0st_thread)
extern struct vmspace vmspace0;		/* VM space for proc0. */
extern int hogticks;			/* Limit on kernel cpu hogs. */
extern int lastpid;
extern int nprocs, maxproc;		/* Current and max number of procs. */
extern int maxprocperuid;		/* Max procs per uid. */
extern u_long ps_arg_cache_limit;

LIST_HEAD(proclist, proc);
TAILQ_HEAD(procqueue, proc);
TAILQ_HEAD(threadqueue, thread);
extern struct proclist allproc;		/* List of all processes. */
extern struct proclist zombproc;	/* List of zombie processes. */
extern struct proc *initproc, *pageproc; /* Process slots for init, pager. */

extern struct uma_zone *proc_zone;

struct	proc *pfind(pid_t);		/* Find process by id. */
struct	proc *pfind_any(pid_t);		/* Find (zombie) process by id. */
struct	pgrp *pgfind(pid_t);		/* Find process group by id. */
struct	proc *zpfind(pid_t);		/* Find zombie process by id. */

struct	fork_req {
	int		fr_flags;
	int		fr_pages;
	int 		*fr_pidp;
	struct proc 	**fr_procp;
	int 		*fr_pd_fd;
	int 		fr_pd_flags;
	struct filecaps	*fr_pd_fcaps;
};

/*
 * pget() flags.
 */
#define	PGET_HOLD	0x00001	/* Hold the process. */
#define	PGET_CANSEE	0x00002	/* Check against p_cansee(). */
#define	PGET_CANDEBUG	0x00004	/* Check against p_candebug(). */
#define	PGET_ISCURRENT	0x00008	/* Check that the found process is current. */
#define	PGET_NOTWEXIT	0x00010	/* Check that the process is not in P_WEXIT. */
#define	PGET_NOTINEXEC	0x00020	/* Check that the process is not in P_INEXEC. */
#define	PGET_NOTID	0x00040	/* Do not assume tid if pid > PID_MAX. */

#define	PGET_WANTREAD	(PGET_HOLD | PGET_CANDEBUG | PGET_NOTWEXIT)

int	pget(pid_t pid, int flags, struct proc **pp);

void	ast(struct trapframe *framep);
struct	thread *choosethread(void);
int	cr_cansee(struct ucred *u1, struct ucred *u2);
int	cr_canseesocket(struct ucred *cred, struct socket *so);
int	cr_canseeothergids(struct ucred *u1, struct ucred *u2);
int	cr_canseeotheruids(struct ucred *u1, struct ucred *u2);
int	cr_canseejailproc(struct ucred *u1, struct ucred *u2);
int	cr_cansignal(struct ucred *cred, struct proc *proc, int signum);
int	enterpgrp(struct proc *p, pid_t pgid, struct pgrp *pgrp,
	    struct session *sess);
int	enterthispgrp(struct proc *p, struct pgrp *pgrp);
void	faultin(struct proc *p);
void	fixjobc(struct proc *p, struct pgrp *pgrp, int entering);
int	fork1(struct thread *, struct fork_req *);
void	fork_rfppwait(struct thread *);
void	fork_exit(void (*)(void *, struct trapframe *), void *,
	    struct trapframe *);
void	fork_return(struct thread *, struct trapframe *);
int	inferior(struct proc *p);
void	kern_proc_vmmap_resident(struct vm_map *map, struct vm_map_entry *entry,
	    int *resident_count, bool *super);
void	kern_yield(int);
void 	kick_proc0(void);
void	killjobc(void);
int	leavepgrp(struct proc *p);
int	maybe_preempt(struct thread *td);
void	maybe_yield(void);
void	mi_switch(int flags, struct thread *newtd);
int	p_candebug(struct thread *td, struct proc *p);
int	p_cansee(struct thread *td, struct proc *p);
int	p_cansched(struct thread *td, struct proc *p);
int	p_cansignal(struct thread *td, struct proc *p, int signum);
int	p_canwait(struct thread *td, struct proc *p);
struct	pargs *pargs_alloc(int len);
void	pargs_drop(struct pargs *pa);
void	pargs_hold(struct pargs *pa);
int	proc_getargv(struct thread *td, struct proc *p, struct sbuf *sb);
int	proc_getauxv(struct thread *td, struct proc *p, struct sbuf *sb);
int	proc_getenvv(struct thread *td, struct proc *p, struct sbuf *sb);
void	procinit(void);
int	proc_iterate(int (*cb)(struct proc *, void *), void *cbarg);
void	proc_linkup0(struct proc *p, struct thread *td);
void	proc_linkup(struct proc *p, struct thread *td);
struct proc *proc_realparent(struct proc *child);
void	proc_reap(struct thread *td, struct proc *p, int *status, int options);
void	proc_reparent(struct proc *child, struct proc *newparent, bool set_oppid);
void	proc_set_traced(struct proc *p, bool stop);
void	proc_wkilled(struct proc *p);
struct	pstats *pstats_alloc(void);
void	pstats_fork(struct pstats *src, struct pstats *dst);
void	pstats_free(struct pstats *ps);
void	reaper_abandon_children(struct proc *p, bool exiting);
int	securelevel_ge(struct ucred *cr, int level);
int	securelevel_gt(struct ucred *cr, int level);
void	sess_hold(struct session *);
void	sess_release(struct session *);
int	setrunnable(struct thread *);
void	setsugid(struct proc *p);
int	should_yield(void);
int	sigonstack(size_t sp);
void	stopevent(struct proc *, u_int, u_int);
struct	thread *tdfind(lwpid_t, pid_t);
void	threadinit(void);
void	tidhash_add(struct thread *);
void	tidhash_remove(struct thread *);
void	cpu_idle(int);
int	cpu_idle_wakeup(int);
extern	void (*cpu_idle_hook)(sbintime_t);	/* Hook to machdep CPU idler. */
void	cpu_switch(struct thread *, struct thread *, struct mtx *);
void	cpu_throw(struct thread *, struct thread *) __dead2;
void	unsleep(struct thread *);
void	userret(struct thread *, struct trapframe *);

void	cpu_exit(struct thread *);
void	exit1(struct thread *, int, int) __dead2;
void	cpu_copy_thread(struct thread *td, struct thread *td0);
bool	cpu_exec_vmspace_reuse(struct proc *p, struct vm_map *map);
int	cpu_fetch_syscall_args(struct thread *td);
void	cpu_fork(struct thread *, struct proc *, struct thread *, int);
void	cpu_fork_kthread_handler(struct thread *, void (*)(void *), void *);
int	cpu_procctl(struct thread *td, int idtype, id_t id, int com,
	    void *data);
void	cpu_set_syscall_retval(struct thread *, int);
void	cpu_set_upcall(struct thread *, void (*)(void *), void *,
	    stack_t *);
int	cpu_set_user_tls(struct thread *, void *tls_base);
void	cpu_thread_alloc(struct thread *);
void	cpu_thread_clean(struct thread *);
void	cpu_thread_exit(struct thread *);
void	cpu_thread_free(struct thread *);
void	cpu_thread_swapin(struct thread *);
void	cpu_thread_swapout(struct thread *);
struct	thread *thread_alloc(int pages);
int	thread_alloc_stack(struct thread *, int pages);
void	thread_cow_get_proc(struct thread *newtd, struct proc *p);
void	thread_cow_get(struct thread *newtd, struct thread *td);
void	thread_cow_free(struct thread *td);
void	thread_cow_update(struct thread *td);
int	thread_create(struct thread *td, struct rtprio *rtp,
	    int (*initialize_thread)(struct thread *, void *), void *thunk);
void	thread_exit(void) __dead2;
void	thread_free(struct thread *td);
void	thread_link(struct thread *td, struct proc *p);
void	thread_reap(void);
int	thread_single(struct proc *p, int how);
void	thread_single_end(struct proc *p, int how);
void	thread_stash(struct thread *td);
void	thread_stopped(struct proc *p);
void	childproc_stopped(struct proc *child, int reason);
void	childproc_continued(struct proc *child);
void	childproc_exited(struct proc *child);
int	thread_suspend_check(int how);
bool	thread_suspend_check_needed(void);
void	thread_suspend_switch(struct thread *, struct proc *p);
void	thread_suspend_one(struct thread *td);
void	thread_unlink(struct thread *td);
void	thread_unsuspend(struct proc *p);
void	thread_wait(struct proc *p);
struct thread	*thread_find(struct proc *p, lwpid_t tid);

void	stop_all_proc(void);
void	resume_all_proc(void);

static __inline int
curthread_pflags_set(int flags)
{
	struct thread *td;
	int save;

	td = curthread;
	save = ~flags | (td->td_pflags & flags);
	td->td_pflags |= flags;
	return (save);
}

static __inline void
curthread_pflags_restore(int save)
{

	curthread->td_pflags &= save;
}

static __inline __pure2 struct td_sched *
td_get_sched(struct thread *td)
{

	return ((struct td_sched *)&td[1]);
}

extern void (*softdep_ast_cleanup)(struct thread *);
static __inline void
td_softdep_cleanup(struct thread *td)
{

	if (td->td_su != NULL && softdep_ast_cleanup != NULL)
		softdep_ast_cleanup(td);
}

#define	PROC_ID_PID	0
#define	PROC_ID_GROUP	1
#define	PROC_ID_SESSION	2
#define	PROC_ID_REAP	3

void	proc_id_set(int type, pid_t id);
void	proc_id_set_cond(int type, pid_t id);
void	proc_id_clear(int type, pid_t id);

#endif	/* _KERNEL */

#endif	/* !_SYS_PROC_H_ */
