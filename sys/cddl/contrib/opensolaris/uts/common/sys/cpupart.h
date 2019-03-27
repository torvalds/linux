/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 1996, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2017 RackTop Systems.
 */

#ifndef	_SYS_CPUPART_H
#define	_SYS_CPUPART_H

#include <sys/types.h>
#include <sys/processor.h>
#include <sys/cpuvar.h>
#include <sys/disp.h>
#include <sys/pset.h>
#include <sys/lgrp.h>
#include <sys/lgrp_user.h>
#include <sys/pg.h>
#include <sys/bitset.h>
#include <sys/time.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(_KERNEL) || defined(_FAKE_KERNEL)

typedef int	cpupartid_t;

/*
 * Special partition id.
 */
#define	CP_DEFAULT	0

/*
 * Flags for cpupart_list()
 */
#define	CP_ALL		0		/* return all cpu partitions */
#define	CP_NONEMPTY	1		/* return only non-empty ones */

typedef struct cpupart {
	disp_t		cp_kp_queue;	/* partition-wide kpreempt queue */
	cpupartid_t	cp_id;		/* partition ID */
	int		cp_ncpus;	/* number of online processors */
	struct cpupart	*cp_next;	/* next partition in list */
	struct cpupart	*cp_prev;	/* previous partition in list */
	struct cpu	*cp_cpulist;	/* processor list */
	struct kstat	*cp_kstat;	/* per-partition statistics */

	/*
	 * cp_nrunnable and cp_nrunning are used to calculate load average.
	 */
	uint_t		cp_nrunnable;	/* current # of runnable threads */
	uint_t		cp_nrunning;	/* current # of running threads */

	/*
	 * cp_updates, cp_nrunnable_cum, cp_nwaiting_cum, and cp_hp_avenrun
	 * are used to generate kstat information on an as-needed basis.
	 */
	uint64_t	cp_updates;	/* number of statistics updates */
	uint64_t	cp_nrunnable_cum; /* cum. # of runnable threads */
	uint64_t	cp_nwaiting_cum;  /* cum. # of waiting threads */

	struct loadavg_s cp_loadavg;	/* cpupart loadavg */

	klgrpset_t	cp_lgrpset;	/* set of lgroups on which this */
					/*    partition has cpus */
	lpl_t		*cp_lgrploads;	/* table of load averages for this  */
					/*    partition, indexed by lgrp ID */
	int		cp_nlgrploads;	/* size of cp_lgrploads table */
	uint64_t	cp_hp_avenrun[3]; /* high-precision load average */
	uint_t		cp_attr;	/* bitmask of attributes */
	lgrp_gen_t	cp_gen;		/* generation number */
	lgrp_id_t	cp_lgrp_hint;	/* last home lgroup chosen */
	bitset_t	cp_cmt_pgs;	/* CMT PGs represented */
	bitset_t	cp_haltset;	/* halted CPUs */
} cpupart_t;

typedef struct cpupart_kstat {
	kstat_named_t	cpk_updates;		/* number of updates */
	kstat_named_t	cpk_runnable;		/* cum # of runnable threads */
	kstat_named_t	cpk_waiting;		/* cum # waiting for I/O */
	kstat_named_t	cpk_ncpus;		/* current # of CPUs */
	kstat_named_t	cpk_avenrun_1min;	/* 1-minute load average */
	kstat_named_t	cpk_avenrun_5min;	/* 5-minute load average */
	kstat_named_t	cpk_avenrun_15min;	/* 15-minute load average */
} cpupart_kstat_t;

/*
 * Macro to obtain the maximum run priority for the global queue associated
 * with given cpu partition.
 */
#define	CP_MAXRUNPRI(cp)	((cp)->cp_kp_queue.disp_maxrunpri)

/*
 * This macro is used to determine if the given thread must surrender
 * CPU to higher priority runnable threads on one of its dispatch queues.
 * This should really be defined in <sys/disp.h> but it is not because
 * including <sys/cpupart.h> there would cause recursive includes.
 */
#define	DISP_MUST_SURRENDER(t)				\
	((DISP_MAXRUNPRI(t) > DISP_PRIO(t)) ||		\
	(CP_MAXRUNPRI(t->t_cpupart) > DISP_PRIO(t)))

extern cpupart_t	cp_default;
extern cpupart_t	*cp_list_head;
extern uint_t		cp_numparts;
extern uint_t		cp_numparts_nonempty;

/*
 * Each partition contains a bitset that indicates which CPUs are halted and
 * which ones are running. Given the growing number of CPUs in current and
 * future platforms, it's important to fanout each CPU within its partition's
 * haltset to prevent contention due to false sharing. The fanout factor
 * is platform specific, and declared accordingly.
 */
extern uint_t cp_haltset_fanout;

extern void	cpupart_initialize_default();
extern cpupart_t *cpupart_find(psetid_t);
extern int	cpupart_create(psetid_t *);
extern int	cpupart_destroy(psetid_t);
extern psetid_t	cpupart_query_cpu(cpu_t *);
extern int	cpupart_attach_cpu(psetid_t, cpu_t *, int);
extern int	cpupart_get_cpus(psetid_t *, processorid_t *, uint_t *);
extern int	cpupart_bind_thread(kthread_id_t, psetid_t, int, void *,
    void *);
extern void	cpupart_kpqalloc(pri_t);
extern int	cpupart_get_loadavg(psetid_t, int *, int);
extern uint_t	cpupart_list(psetid_t *, uint_t, int);
extern int	cpupart_setattr(psetid_t, uint_t);
extern int	cpupart_getattr(psetid_t, uint_t *);

#endif	/* _KERNEL || _FAKE_KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CPUPART_H */
