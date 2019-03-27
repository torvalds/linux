/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)vmmeter.h	8.2 (Berkeley) 7/10/94
 * $FreeBSD$
 */

#ifndef _SYS_VMMETER_H_
#define _SYS_VMMETER_H_

/*
 * This value is used by ps(1) to change sleep state flag from 'S' to
 * 'I' and by the sched process to set the alarm clock.
 */
#define	MAXSLP			20

struct vmtotal {
	uint64_t	t_vm;		/* total virtual memory */
	uint64_t	t_avm;		/* active virtual memory */
	uint64_t	t_rm;		/* total real memory in use */
	uint64_t	t_arm;		/* active real memory */
	uint64_t	t_vmshr;	/* shared virtual memory */
	uint64_t	t_avmshr;	/* active shared virtual memory */
	uint64_t	t_rmshr;	/* shared real memory */
	uint64_t	t_armshr;	/* active shared real memory */
	uint64_t	t_free;		/* free memory pages */
	int16_t		t_rq;		/* length of the run queue */
	int16_t		t_dw;		/* threads in ``disk wait'' (neg
					   priority) */
	int16_t		t_pw;		/* threads in page wait */
	int16_t		t_sl;		/* threads sleeping in core */
	int16_t		t_sw;		/* swapped out runnable/short
					   block threads */
	uint16_t	t_pad[3];
};

#if defined(_KERNEL) || defined(_WANT_VMMETER)
#include <sys/counter.h>

#ifdef _KERNEL
#define VMMETER_ALIGNED	__aligned(CACHE_LINE_SIZE)
#else
#define VMMETER_ALIGNED
#endif

/*
 * System wide statistics counters.
 * Locking:
 *      c - constant after initialization
 *      p - uses counter(9)
 */
struct vmmeter {
	/*
	 * General system activity.
	 */
	counter_u64_t v_swtch;		/* (p) context switches */
	counter_u64_t v_trap;		/* (p) calls to trap */
	counter_u64_t v_syscall;	/* (p) calls to syscall() */
	counter_u64_t v_intr;		/* (p) device interrupts */
	counter_u64_t v_soft;		/* (p) software interrupts */
	/*
	 * Virtual memory activity.
	 */
	counter_u64_t v_vm_faults;	/* (p) address memory faults */
	counter_u64_t v_io_faults;	/* (p) page faults requiring I/O */
	counter_u64_t v_cow_faults;	/* (p) copy-on-writes faults */
	counter_u64_t v_cow_optim;	/* (p) optimized COW faults */
	counter_u64_t v_zfod;		/* (p) pages zero filled on demand */
	counter_u64_t v_ozfod;		/* (p) optimized zero fill pages */
	counter_u64_t v_swapin;		/* (p) swap pager pageins */
	counter_u64_t v_swapout;	/* (p) swap pager pageouts */
	counter_u64_t v_swappgsin;	/* (p) swap pager pages paged in */
	counter_u64_t v_swappgsout;	/* (p) swap pager pages paged out */
	counter_u64_t v_vnodein;	/* (p) vnode pager pageins */
	counter_u64_t v_vnodeout;	/* (p) vnode pager pageouts */
	counter_u64_t v_vnodepgsin;	/* (p) vnode_pager pages paged in */
	counter_u64_t v_vnodepgsout;	/* (p) vnode pager pages paged out */
	counter_u64_t v_intrans;	/* (p) intransit blocking page faults */
	counter_u64_t v_reactivated;	/* (p) reactivated by the pagedaemon */
	counter_u64_t v_pdwakeups;	/* (p) times daemon has awaken */
	counter_u64_t v_pdpages;	/* (p) pages analyzed by daemon */
	counter_u64_t v_pdshortfalls;	/* (p) page reclamation shortfalls */

	counter_u64_t v_dfree;		/* (p) pages freed by daemon */
	counter_u64_t v_pfree;		/* (p) pages freed by processes */
	counter_u64_t v_tfree;		/* (p) total pages freed */
	/*
	 * Fork/vfork/rfork activity.
	 */
	counter_u64_t v_forks;		/* (p) fork() calls */
	counter_u64_t v_vforks;		/* (p) vfork() calls */
	counter_u64_t v_rforks;		/* (p) rfork() calls */
	counter_u64_t v_kthreads;	/* (p) fork() calls by kernel */
	counter_u64_t v_forkpages;	/* (p) pages affected by fork() */
	counter_u64_t v_vforkpages;	/* (p) pages affected by vfork() */
	counter_u64_t v_rforkpages;	/* (p) pages affected by rfork() */
	counter_u64_t v_kthreadpages;	/* (p) ... and by kernel fork() */
	counter_u64_t v_wire_count;	/* (p) pages wired down */
#define	VM_METER_NCOUNTERS	\
	(offsetof(struct vmmeter, v_page_size) / sizeof(counter_u64_t))
	/*
	 * Distribution of page usages.
	 */
	u_int v_page_size;	/* (c) page size in bytes */
	u_int v_page_count;	/* (c) total number of pages in system */
	u_int v_free_reserved;	/* (c) pages reserved for deadlock */
	u_int v_free_target;	/* (c) pages desired free */
	u_int v_free_min;	/* (c) pages desired free */
	u_int v_inactive_target; /* (c) pages desired inactive */
	u_int v_pageout_free_min;   /* (c) min pages reserved for kernel */
	u_int v_interrupt_free_min; /* (c) reserved pages for int code */
	u_int v_free_severe;	/* (c) severe page depletion point */
};
#endif /* _KERNEL || _WANT_VMMETER */

#ifdef _KERNEL

#include <sys/domainset.h>

extern struct vmmeter vm_cnt;
extern domainset_t all_domains;
extern domainset_t vm_min_domains;
extern domainset_t vm_severe_domains;

#define	VM_CNT_ADD(var, x)	counter_u64_add(vm_cnt.var, x)
#define	VM_CNT_INC(var)		VM_CNT_ADD(var, 1)
#define	VM_CNT_FETCH(var)	counter_u64_fetch(vm_cnt.var)

static inline void
vm_wire_add(int cnt)
{

	VM_CNT_ADD(v_wire_count, cnt);
}

static inline void
vm_wire_sub(int cnt)
{

	VM_CNT_ADD(v_wire_count, -cnt);
}

u_int vm_free_count(void);
static inline u_int
vm_wire_count(void)
{

	return (VM_CNT_FETCH(v_wire_count));
}

/*
 * Return TRUE if we are under our severe low-free-pages threshold
 *
 * These routines are typically used at the user<->system interface to determine
 * whether we need to block in order to avoid a low memory deadlock.
 */
static inline int
vm_page_count_severe(void)
{

	return (!DOMAINSET_EMPTY(&vm_severe_domains));
}

static inline int
vm_page_count_severe_domain(int domain)
{

	return (DOMAINSET_ISSET(domain, &vm_severe_domains));
}

static inline int
vm_page_count_severe_set(const domainset_t *mask)
{

	return (DOMAINSET_SUBSET(&vm_severe_domains, mask));
}

/*
 * Return TRUE if we are under our minimum low-free-pages threshold.
 *
 * These routines are typically used within the system to determine whether
 * we can execute potentially very expensive code in terms of memory.  It
 * is also used by the pageout daemon to calculate when to sleep, when
 * to wake waiters up, and when (after making a pass) to become more
 * desperate.
 */
static inline int
vm_page_count_min(void)
{

	return (!DOMAINSET_EMPTY(&vm_min_domains));
}

static inline int
vm_page_count_min_domain(int domain)
{

	return (DOMAINSET_ISSET(domain, &vm_min_domains));
}

static inline int
vm_page_count_min_set(const domainset_t *mask)
{

	return (DOMAINSET_SUBSET(&vm_min_domains, mask));
}

#endif	/* _KERNEL */
#endif	/* _SYS_VMMETER_H_ */
