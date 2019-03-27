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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_FASTTRAP_IMPL_H
#define	_FASTTRAP_IMPL_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/dtrace.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/fasttrap.h>
#include <sys/fasttrap_isa.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Fasttrap Providers, Probes and Tracepoints
 *
 * Each Solaris process can have multiple providers -- the pid provider as
 * well as any number of user-level statically defined tracing (USDT)
 * providers. Those providers are each represented by a fasttrap_provider_t.
 * All providers for a given process have a pointer to a shared
 * fasttrap_proc_t. The fasttrap_proc_t has two states: active or defunct.
 * When the count of active providers goes to zero it becomes defunct; a
 * provider drops its active count when it is removed individually or as part
 * of a mass removal when a process exits or performs an exec.
 *
 * Each probe is represented by a fasttrap_probe_t which has a pointer to
 * its associated provider as well as a list of fasttrap_id_tp_t structures
 * which are tuples combining a fasttrap_id_t and a fasttrap_tracepoint_t.
 * A fasttrap_tracepoint_t represents the actual point of instrumentation
 * and it contains two lists of fasttrap_id_t structures (to be fired pre-
 * and post-instruction emulation) that identify the probes attached to the
 * tracepoint. Tracepoints also have a pointer to the fasttrap_proc_t for the
 * process they trace which is used when looking up a tracepoint both when a
 * probe fires and when enabling and disabling probes.
 *
 * It's important to note that probes are preallocated with the necessary
 * number of tracepoints, but that tracepoints can be shared by probes and
 * swapped between probes. If a probe's preallocated tracepoint is enabled
 * (and, therefore, the associated probe is enabled), and that probe is
 * then disabled, ownership of that tracepoint may be exchanged for an
 * unused tracepoint belonging to another probe that was attached to the
 * enabled tracepoint.
 *
 * On FreeBSD, fasttrap providers also maintain per-thread scratch space for use
 * by the ISA-specific fasttrap code. The fasttrap_scrblock_t type stores the
 * virtual address of a page-sized memory block that is mapped into a process'
 * address space. Each block is carved up into chunks (fasttrap_scrspace_t) for
 * use by individual threads, which keep the address of their scratch space
 * chunk in their struct kdtrace_thread. A thread's scratch space isn't released
 * until it exits.
 */

#ifndef illumos
typedef struct fasttrap_scrblock {
	vm_offset_t ftsb_addr;			/* address of a scratch block */
	LIST_ENTRY(fasttrap_scrblock) ftsb_next;/* next block in list */
} fasttrap_scrblock_t;
#define	FASTTRAP_SCRBLOCK_SIZE	PAGE_SIZE

typedef struct fasttrap_scrspace {
	uintptr_t ftss_addr;			/* scratch space address */
	LIST_ENTRY(fasttrap_scrspace) ftss_next;/* next in list */
} fasttrap_scrspace_t;
#define	FASTTRAP_SCRSPACE_SIZE	64
#endif

typedef struct fasttrap_proc {
	pid_t ftpc_pid;				/* process ID for this proc */
	uint64_t ftpc_acount;			/* count of active providers */
	uint64_t ftpc_rcount;			/* count of extant providers */
	kmutex_t ftpc_mtx;			/* lock on all but acount */
	struct fasttrap_proc *ftpc_next;	/* next proc in hash chain */
#ifndef illumos
	LIST_HEAD(, fasttrap_scrblock) ftpc_scrblks; /* mapped scratch blocks */
	LIST_HEAD(, fasttrap_scrspace) ftpc_fscr; /* free scratch space */
	LIST_HEAD(, fasttrap_scrspace) ftpc_ascr; /* used scratch space */
#endif
} fasttrap_proc_t;

typedef struct fasttrap_provider {
	pid_t ftp_pid;				/* process ID for this prov */
	char ftp_name[DTRACE_PROVNAMELEN];	/* prov name (w/o the pid) */
	dtrace_provider_id_t ftp_provid;	/* DTrace provider handle */
	uint_t ftp_marked;			/* mark for possible removal */
	uint_t ftp_retired;			/* mark when retired */
	kmutex_t ftp_mtx;			/* provider lock */
	kmutex_t ftp_cmtx;			/* lock on creating probes */
	uint64_t ftp_rcount;			/* enabled probes ref count */
	uint64_t ftp_ccount;			/* consumers creating probes */
	uint64_t ftp_mcount;			/* meta provider count */
	fasttrap_proc_t *ftp_proc;		/* shared proc for all provs */
	struct fasttrap_provider *ftp_next;	/* next prov in hash chain */
} fasttrap_provider_t;

typedef struct fasttrap_id fasttrap_id_t;
typedef struct fasttrap_probe fasttrap_probe_t;
typedef struct fasttrap_tracepoint fasttrap_tracepoint_t;

struct fasttrap_id {
	fasttrap_probe_t *fti_probe;		/* referrring probe */
	fasttrap_id_t *fti_next;		/* enabled probe list on tp */
	fasttrap_probe_type_t fti_ptype;	/* probe type */
};

typedef struct fasttrap_id_tp {
	fasttrap_id_t fit_id;
	fasttrap_tracepoint_t *fit_tp;
} fasttrap_id_tp_t;

struct fasttrap_probe {
	dtrace_id_t ftp_id;			/* DTrace probe identifier */
	pid_t ftp_pid;				/* pid for this probe */
	fasttrap_provider_t *ftp_prov;		/* this probe's provider */
	uintptr_t ftp_faddr;			/* associated function's addr */
	size_t ftp_fsize;			/* associated function's size */
	uint64_t ftp_gen;			/* modification generation */
	uint64_t ftp_ntps;			/* number of tracepoints */
	uint8_t *ftp_argmap;			/* native to translated args */
	uint8_t ftp_nargs;			/* translated argument count */
	uint8_t ftp_enabled;			/* is this probe enabled */
	char *ftp_xtypes;			/* translated types index */
	char *ftp_ntypes;			/* native types index */
	fasttrap_id_tp_t ftp_tps[1];		/* flexible array */
};

#define	FASTTRAP_ID_INDEX(id)	\
((fasttrap_id_tp_t *)(((char *)(id) - offsetof(fasttrap_id_tp_t, fit_id))) - \
&(id)->fti_probe->ftp_tps[0])

struct fasttrap_tracepoint {
	fasttrap_proc_t *ftt_proc;		/* associated process struct */
	uintptr_t ftt_pc;			/* address of tracepoint */
	pid_t ftt_pid;				/* pid of tracepoint */
	fasttrap_machtp_t ftt_mtp;		/* ISA-specific portion */
	fasttrap_id_t *ftt_ids;			/* NULL-terminated list */
	fasttrap_id_t *ftt_retids;		/* NULL-terminated list */
	fasttrap_tracepoint_t *ftt_next;	/* link in global hash */
};

typedef struct fasttrap_bucket {
	kmutex_t ftb_mtx;			/* bucket lock */
	void *ftb_data;				/* data payload */

	uint8_t ftb_pad[64 - sizeof (kmutex_t) - sizeof (void *)];
} fasttrap_bucket_t;

typedef struct fasttrap_hash {
	ulong_t fth_nent;			/* power-of-2 num. of entries */
	ulong_t fth_mask;			/* fth_nent - 1 */
	fasttrap_bucket_t *fth_table;		/* array of buckets */
} fasttrap_hash_t;

/*
 * If at some future point these assembly functions become observable by
 * DTrace, then these defines should become separate functions so that the
 * fasttrap provider doesn't trigger probes during internal operations.
 */
#define	fasttrap_copyout	copyout
#define	fasttrap_fuword32	fuword32
#define	fasttrap_suword32	suword32
#define	fasttrap_suword64	suword64

#ifdef __amd64__
#define	fasttrap_fulword	fuword64
#define	fasttrap_sulword	suword64
#else
#define	fasttrap_fulword	fuword32
#define	fasttrap_sulword	suword32
#endif

extern void fasttrap_sigtrap(proc_t *, kthread_t *, uintptr_t);
#ifndef illumos
extern fasttrap_scrspace_t *fasttrap_scraddr(struct thread *,
    fasttrap_proc_t *);
#endif

extern dtrace_id_t 		fasttrap_probe_id;
extern fasttrap_hash_t		fasttrap_tpoints;

#ifndef illumos
extern struct rmlock		fasttrap_tp_lock;
#endif

#define	FASTTRAP_TPOINTS_INDEX(pid, pc) \
	(((pc) / sizeof (fasttrap_instr_t) + (pid)) & fasttrap_tpoints.fth_mask)

/*
 * Must be implemented by fasttrap_isa.c
 */
extern int fasttrap_tracepoint_init(proc_t *, fasttrap_tracepoint_t *,
    uintptr_t, fasttrap_probe_type_t);
extern int fasttrap_tracepoint_install(proc_t *, fasttrap_tracepoint_t *);
extern int fasttrap_tracepoint_remove(proc_t *, fasttrap_tracepoint_t *);

struct trapframe;
extern int fasttrap_pid_probe(struct trapframe *);
extern int fasttrap_return_probe(struct trapframe *);

extern uint64_t fasttrap_pid_getarg(void *, dtrace_id_t, void *, int, int);
extern uint64_t fasttrap_usdt_getarg(void *, dtrace_id_t, void *, int, int);

#ifdef	__cplusplus
}
#endif

#endif	/* _FASTTRAP_IMPL_H */
