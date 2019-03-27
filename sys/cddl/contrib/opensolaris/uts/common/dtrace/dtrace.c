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
 *
 * $FreeBSD$
 */

/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2016, Joyent, Inc. All rights reserved.
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 */

/*
 * DTrace - Dynamic Tracing for Solaris
 *
 * This is the implementation of the Solaris Dynamic Tracing framework
 * (DTrace).  The user-visible interface to DTrace is described at length in
 * the "Solaris Dynamic Tracing Guide".  The interfaces between the libdtrace
 * library, the in-kernel DTrace framework, and the DTrace providers are
 * described in the block comments in the <sys/dtrace.h> header file.  The
 * internal architecture of DTrace is described in the block comments in the
 * <sys/dtrace_impl.h> header file.  The comments contained within the DTrace
 * implementation very much assume mastery of all of these sources; if one has
 * an unanswered question about the implementation, one should consult them
 * first.
 *
 * The functions here are ordered roughly as follows:
 *
 *   - Probe context functions
 *   - Probe hashing functions
 *   - Non-probe context utility functions
 *   - Matching functions
 *   - Provider-to-Framework API functions
 *   - Probe management functions
 *   - DIF object functions
 *   - Format functions
 *   - Predicate functions
 *   - ECB functions
 *   - Buffer functions
 *   - Enabling functions
 *   - DOF functions
 *   - Anonymous enabling functions
 *   - Consumer state functions
 *   - Helper functions
 *   - Hook functions
 *   - Driver cookbook functions
 *
 * Each group of functions begins with a block comment labelled the "DTrace
 * [Group] Functions", allowing one to find each block by searching forward
 * on capital-f functions.
 */
#include <sys/errno.h>
#ifndef illumos
#include <sys/time.h>
#endif
#include <sys/stat.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/systm.h>
#ifdef illumos
#include <sys/ddi.h>
#include <sys/sunddi.h>
#endif
#include <sys/cpuvar.h>
#include <sys/kmem.h>
#ifdef illumos
#include <sys/strsubr.h>
#endif
#include <sys/sysmacros.h>
#include <sys/dtrace_impl.h>
#include <sys/atomic.h>
#include <sys/cmn_err.h>
#ifdef illumos
#include <sys/mutex_impl.h>
#include <sys/rwlock_impl.h>
#endif
#include <sys/ctf_api.h>
#ifdef illumos
#include <sys/panic.h>
#include <sys/priv_impl.h>
#endif
#include <sys/policy.h>
#ifdef illumos
#include <sys/cred_impl.h>
#include <sys/procfs_isa.h>
#endif
#include <sys/taskq.h>
#ifdef illumos
#include <sys/mkdev.h>
#include <sys/kdi.h>
#endif
#include <sys/zone.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "strtolctype.h"

/* FreeBSD includes: */
#ifndef illumos
#include <sys/callout.h>
#include <sys/ctype.h>
#include <sys/eventhandler.h>
#include <sys/limits.h>
#include <sys/linker.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/ptrace.h>
#include <sys/random.h>
#include <sys/rwlock.h>
#include <sys/sx.h>
#include <sys/sysctl.h>

#include <sys/dtrace_bsd.h>

#include <netinet/in.h>

#include "dtrace_cddl.h"
#include "dtrace_debug.c"
#endif

#include "dtrace_xoroshiro128_plus.h"

/*
 * DTrace Tunable Variables
 *
 * The following variables may be tuned by adding a line to /etc/system that
 * includes both the name of the DTrace module ("dtrace") and the name of the
 * variable.  For example:
 *
 *   set dtrace:dtrace_destructive_disallow = 1
 *
 * In general, the only variables that one should be tuning this way are those
 * that affect system-wide DTrace behavior, and for which the default behavior
 * is undesirable.  Most of these variables are tunable on a per-consumer
 * basis using DTrace options, and need not be tuned on a system-wide basis.
 * When tuning these variables, avoid pathological values; while some attempt
 * is made to verify the integrity of these variables, they are not considered
 * part of the supported interface to DTrace, and they are therefore not
 * checked comprehensively.  Further, these variables should not be tuned
 * dynamically via "mdb -kw" or other means; they should only be tuned via
 * /etc/system.
 */
int		dtrace_destructive_disallow = 0;
#ifndef illumos
/* Positive logic version of dtrace_destructive_disallow for loader tunable */
int		dtrace_allow_destructive = 1;
#endif
dtrace_optval_t	dtrace_nonroot_maxsize = (16 * 1024 * 1024);
size_t		dtrace_difo_maxsize = (256 * 1024);
dtrace_optval_t	dtrace_dof_maxsize = (8 * 1024 * 1024);
size_t		dtrace_statvar_maxsize = (16 * 1024);
size_t		dtrace_actions_max = (16 * 1024);
size_t		dtrace_retain_max = 1024;
dtrace_optval_t	dtrace_helper_actions_max = 128;
dtrace_optval_t	dtrace_helper_providers_max = 32;
dtrace_optval_t	dtrace_dstate_defsize = (1 * 1024 * 1024);
size_t		dtrace_strsize_default = 256;
dtrace_optval_t	dtrace_cleanrate_default = 9900990;		/* 101 hz */
dtrace_optval_t	dtrace_cleanrate_min = 200000;			/* 5000 hz */
dtrace_optval_t	dtrace_cleanrate_max = (uint64_t)60 * NANOSEC;	/* 1/minute */
dtrace_optval_t	dtrace_aggrate_default = NANOSEC;		/* 1 hz */
dtrace_optval_t	dtrace_statusrate_default = NANOSEC;		/* 1 hz */
dtrace_optval_t dtrace_statusrate_max = (hrtime_t)10 * NANOSEC;	 /* 6/minute */
dtrace_optval_t	dtrace_switchrate_default = NANOSEC;		/* 1 hz */
dtrace_optval_t	dtrace_nspec_default = 1;
dtrace_optval_t	dtrace_specsize_default = 32 * 1024;
dtrace_optval_t dtrace_stackframes_default = 20;
dtrace_optval_t dtrace_ustackframes_default = 20;
dtrace_optval_t dtrace_jstackframes_default = 50;
dtrace_optval_t dtrace_jstackstrsize_default = 512;
int		dtrace_msgdsize_max = 128;
hrtime_t	dtrace_chill_max = MSEC2NSEC(500);		/* 500 ms */
hrtime_t	dtrace_chill_interval = NANOSEC;		/* 1000 ms */
int		dtrace_devdepth_max = 32;
int		dtrace_err_verbose;
hrtime_t	dtrace_deadman_interval = NANOSEC;
hrtime_t	dtrace_deadman_timeout = (hrtime_t)10 * NANOSEC;
hrtime_t	dtrace_deadman_user = (hrtime_t)30 * NANOSEC;
hrtime_t	dtrace_unregister_defunct_reap = (hrtime_t)60 * NANOSEC;
#ifndef illumos
int		dtrace_memstr_max = 4096;
#endif

/*
 * DTrace External Variables
 *
 * As dtrace(7D) is a kernel module, any DTrace variables are obviously
 * available to DTrace consumers via the backtick (`) syntax.  One of these,
 * dtrace_zero, is made deliberately so:  it is provided as a source of
 * well-known, zero-filled memory.  While this variable is not documented,
 * it is used by some translators as an implementation detail.
 */
const char	dtrace_zero[256] = { 0 };	/* zero-filled memory */

/*
 * DTrace Internal Variables
 */
#ifdef illumos
static dev_info_t	*dtrace_devi;		/* device info */
#endif
#ifdef illumos
static vmem_t		*dtrace_arena;		/* probe ID arena */
static vmem_t		*dtrace_minor;		/* minor number arena */
#else
static taskq_t		*dtrace_taskq;		/* task queue */
static struct unrhdr	*dtrace_arena;		/* Probe ID number.     */
#endif
static dtrace_probe_t	**dtrace_probes;	/* array of all probes */
static int		dtrace_nprobes;		/* number of probes */
static dtrace_provider_t *dtrace_provider;	/* provider list */
static dtrace_meta_t	*dtrace_meta_pid;	/* user-land meta provider */
static int		dtrace_opens;		/* number of opens */
static int		dtrace_helpers;		/* number of helpers */
static int		dtrace_getf;		/* number of unpriv getf()s */
#ifdef illumos
static void		*dtrace_softstate;	/* softstate pointer */
#endif
static dtrace_hash_t	*dtrace_bymod;		/* probes hashed by module */
static dtrace_hash_t	*dtrace_byfunc;		/* probes hashed by function */
static dtrace_hash_t	*dtrace_byname;		/* probes hashed by name */
static dtrace_toxrange_t *dtrace_toxrange;	/* toxic range array */
static int		dtrace_toxranges;	/* number of toxic ranges */
static int		dtrace_toxranges_max;	/* size of toxic range array */
static dtrace_anon_t	dtrace_anon;		/* anonymous enabling */
static kmem_cache_t	*dtrace_state_cache;	/* cache for dynamic state */
static uint64_t		dtrace_vtime_references; /* number of vtimestamp refs */
static kthread_t	*dtrace_panicked;	/* panicking thread */
static dtrace_ecb_t	*dtrace_ecb_create_cache; /* cached created ECB */
static dtrace_genid_t	dtrace_probegen;	/* current probe generation */
static dtrace_helpers_t *dtrace_deferred_pid;	/* deferred helper list */
static dtrace_enabling_t *dtrace_retained;	/* list of retained enablings */
static dtrace_genid_t	dtrace_retained_gen;	/* current retained enab gen */
static dtrace_dynvar_t	dtrace_dynhash_sink;	/* end of dynamic hash chains */
static int		dtrace_dynvar_failclean; /* dynvars failed to clean */
#ifndef illumos
static struct mtx	dtrace_unr_mtx;
MTX_SYSINIT(dtrace_unr_mtx, &dtrace_unr_mtx, "Unique resource identifier", MTX_DEF);
static eventhandler_tag	dtrace_kld_load_tag;
static eventhandler_tag	dtrace_kld_unload_try_tag;
#endif

/*
 * DTrace Locking
 * DTrace is protected by three (relatively coarse-grained) locks:
 *
 * (1) dtrace_lock is required to manipulate essentially any DTrace state,
 *     including enabling state, probes, ECBs, consumer state, helper state,
 *     etc.  Importantly, dtrace_lock is _not_ required when in probe context;
 *     probe context is lock-free -- synchronization is handled via the
 *     dtrace_sync() cross call mechanism.
 *
 * (2) dtrace_provider_lock is required when manipulating provider state, or
 *     when provider state must be held constant.
 *
 * (3) dtrace_meta_lock is required when manipulating meta provider state, or
 *     when meta provider state must be held constant.
 *
 * The lock ordering between these three locks is dtrace_meta_lock before
 * dtrace_provider_lock before dtrace_lock.  (In particular, there are
 * several places where dtrace_provider_lock is held by the framework as it
 * calls into the providers -- which then call back into the framework,
 * grabbing dtrace_lock.)
 *
 * There are two other locks in the mix:  mod_lock and cpu_lock.  With respect
 * to dtrace_provider_lock and dtrace_lock, cpu_lock continues its historical
 * role as a coarse-grained lock; it is acquired before both of these locks.
 * With respect to dtrace_meta_lock, its behavior is stranger:  cpu_lock must
 * be acquired _between_ dtrace_meta_lock and any other DTrace locks.
 * mod_lock is similar with respect to dtrace_provider_lock in that it must be
 * acquired _between_ dtrace_provider_lock and dtrace_lock.
 */
static kmutex_t		dtrace_lock;		/* probe state lock */
static kmutex_t		dtrace_provider_lock;	/* provider state lock */
static kmutex_t		dtrace_meta_lock;	/* meta-provider state lock */

#ifndef illumos
/* XXX FreeBSD hacks. */
#define cr_suid		cr_svuid
#define cr_sgid		cr_svgid
#define	ipaddr_t	in_addr_t
#define mod_modname	pathname
#define vuprintf	vprintf
#define ttoproc(_a)	((_a)->td_proc)
#define crgetzoneid(_a)	0
#define SNOCD		0
#define CPU_ON_INTR(_a)	0

#define PRIV_EFFECTIVE		(1 << 0)
#define PRIV_DTRACE_KERNEL	(1 << 1)
#define PRIV_DTRACE_PROC	(1 << 2)
#define PRIV_DTRACE_USER	(1 << 3)
#define PRIV_PROC_OWNER		(1 << 4)
#define PRIV_PROC_ZONE		(1 << 5)
#define PRIV_ALL		~0

SYSCTL_DECL(_debug_dtrace);
SYSCTL_DECL(_kern_dtrace);
#endif

#ifdef illumos
#define curcpu	CPU->cpu_id
#endif


/*
 * DTrace Provider Variables
 *
 * These are the variables relating to DTrace as a provider (that is, the
 * provider of the BEGIN, END, and ERROR probes).
 */
static dtrace_pattr_t	dtrace_provider_attr = {
{ DTRACE_STABILITY_STABLE, DTRACE_STABILITY_STABLE, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_STABLE, DTRACE_STABILITY_STABLE, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_STABLE, DTRACE_STABILITY_STABLE, DTRACE_CLASS_COMMON },
};

static void
dtrace_nullop(void)
{}

static dtrace_pops_t dtrace_provider_ops = {
	.dtps_provide =	(void (*)(void *, dtrace_probedesc_t *))dtrace_nullop,
	.dtps_provide_module =	(void (*)(void *, modctl_t *))dtrace_nullop,
	.dtps_enable =	(void (*)(void *, dtrace_id_t, void *))dtrace_nullop,
	.dtps_disable =	(void (*)(void *, dtrace_id_t, void *))dtrace_nullop,
	.dtps_suspend =	(void (*)(void *, dtrace_id_t, void *))dtrace_nullop,
	.dtps_resume =	(void (*)(void *, dtrace_id_t, void *))dtrace_nullop,
	.dtps_getargdesc =	NULL,
	.dtps_getargval =	NULL,
	.dtps_usermode =	NULL,
	.dtps_destroy =	(void (*)(void *, dtrace_id_t, void *))dtrace_nullop,
};

static dtrace_id_t	dtrace_probeid_begin;	/* special BEGIN probe */
static dtrace_id_t	dtrace_probeid_end;	/* special END probe */
dtrace_id_t		dtrace_probeid_error;	/* special ERROR probe */

/*
 * DTrace Helper Tracing Variables
 *
 * These variables should be set dynamically to enable helper tracing.  The
 * only variables that should be set are dtrace_helptrace_enable (which should
 * be set to a non-zero value to allocate helper tracing buffers on the next
 * open of /dev/dtrace) and dtrace_helptrace_disable (which should be set to a
 * non-zero value to deallocate helper tracing buffers on the next close of
 * /dev/dtrace).  When (and only when) helper tracing is disabled, the
 * buffer size may also be set via dtrace_helptrace_bufsize.
 */
int			dtrace_helptrace_enable = 0;
int			dtrace_helptrace_disable = 0;
int			dtrace_helptrace_bufsize = 16 * 1024 * 1024;
uint32_t		dtrace_helptrace_nlocals;
static dtrace_helptrace_t *dtrace_helptrace_buffer;
static uint32_t		dtrace_helptrace_next = 0;
static int		dtrace_helptrace_wrapped = 0;

/*
 * DTrace Error Hashing
 *
 * On DEBUG kernels, DTrace will track the errors that has seen in a hash
 * table.  This is very useful for checking coverage of tests that are
 * expected to induce DIF or DOF processing errors, and may be useful for
 * debugging problems in the DIF code generator or in DOF generation .  The
 * error hash may be examined with the ::dtrace_errhash MDB dcmd.
 */
#ifdef DEBUG
static dtrace_errhash_t	dtrace_errhash[DTRACE_ERRHASHSZ];
static const char *dtrace_errlast;
static kthread_t *dtrace_errthread;
static kmutex_t dtrace_errlock;
#endif

/*
 * DTrace Macros and Constants
 *
 * These are various macros that are useful in various spots in the
 * implementation, along with a few random constants that have no meaning
 * outside of the implementation.  There is no real structure to this cpp
 * mishmash -- but is there ever?
 */
#define	DTRACE_HASHSTR(hash, probe)	\
	dtrace_hash_str(*((char **)((uintptr_t)(probe) + (hash)->dth_stroffs)))

#define	DTRACE_HASHNEXT(hash, probe)	\
	(dtrace_probe_t **)((uintptr_t)(probe) + (hash)->dth_nextoffs)

#define	DTRACE_HASHPREV(hash, probe)	\
	(dtrace_probe_t **)((uintptr_t)(probe) + (hash)->dth_prevoffs)

#define	DTRACE_HASHEQ(hash, lhs, rhs)	\
	(strcmp(*((char **)((uintptr_t)(lhs) + (hash)->dth_stroffs)), \
	    *((char **)((uintptr_t)(rhs) + (hash)->dth_stroffs))) == 0)

#define	DTRACE_AGGHASHSIZE_SLEW		17

#define	DTRACE_V4MAPPED_OFFSET		(sizeof (uint32_t) * 3)

/*
 * The key for a thread-local variable consists of the lower 61 bits of the
 * t_did, plus the 3 bits of the highest active interrupt above LOCK_LEVEL.
 * We add DIF_VARIABLE_MAX to t_did to assure that the thread key is never
 * equal to a variable identifier.  This is necessary (but not sufficient) to
 * assure that global associative arrays never collide with thread-local
 * variables.  To guarantee that they cannot collide, we must also define the
 * order for keying dynamic variables.  That order is:
 *
 *   [ key0 ] ... [ keyn ] [ variable-key ] [ tls-key ]
 *
 * Because the variable-key and the tls-key are in orthogonal spaces, there is
 * no way for a global variable key signature to match a thread-local key
 * signature.
 */
#ifdef illumos
#define	DTRACE_TLS_THRKEY(where) { \
	uint_t intr = 0; \
	uint_t actv = CPU->cpu_intr_actv >> (LOCK_LEVEL + 1); \
	for (; actv; actv >>= 1) \
		intr++; \
	ASSERT(intr < (1 << 3)); \
	(where) = ((curthread->t_did + DIF_VARIABLE_MAX) & \
	    (((uint64_t)1 << 61) - 1)) | ((uint64_t)intr << 61); \
}
#else
#define	DTRACE_TLS_THRKEY(where) { \
	solaris_cpu_t *_c = &solaris_cpu[curcpu]; \
	uint_t intr = 0; \
	uint_t actv = _c->cpu_intr_actv; \
	for (; actv; actv >>= 1) \
		intr++; \
	ASSERT(intr < (1 << 3)); \
	(where) = ((curthread->td_tid + DIF_VARIABLE_MAX) & \
	    (((uint64_t)1 << 61) - 1)) | ((uint64_t)intr << 61); \
}
#endif

#define	DT_BSWAP_8(x)	((x) & 0xff)
#define	DT_BSWAP_16(x)	((DT_BSWAP_8(x) << 8) | DT_BSWAP_8((x) >> 8))
#define	DT_BSWAP_32(x)	((DT_BSWAP_16(x) << 16) | DT_BSWAP_16((x) >> 16))
#define	DT_BSWAP_64(x)	((DT_BSWAP_32(x) << 32) | DT_BSWAP_32((x) >> 32))

#define	DT_MASK_LO 0x00000000FFFFFFFFULL

#define	DTRACE_STORE(type, tomax, offset, what) \
	*((type *)((uintptr_t)(tomax) + (uintptr_t)offset)) = (type)(what);

#ifndef __x86
#define	DTRACE_ALIGNCHECK(addr, size, flags)				\
	if (addr & (size - 1)) {					\
		*flags |= CPU_DTRACE_BADALIGN;				\
		cpu_core[curcpu].cpuc_dtrace_illval = addr;	\
		return (0);						\
	}
#else
#define	DTRACE_ALIGNCHECK(addr, size, flags)
#endif

/*
 * Test whether a range of memory starting at testaddr of size testsz falls
 * within the range of memory described by addr, sz.  We take care to avoid
 * problems with overflow and underflow of the unsigned quantities, and
 * disallow all negative sizes.  Ranges of size 0 are allowed.
 */
#define	DTRACE_INRANGE(testaddr, testsz, baseaddr, basesz) \
	((testaddr) - (uintptr_t)(baseaddr) < (basesz) && \
	(testaddr) + (testsz) - (uintptr_t)(baseaddr) <= (basesz) && \
	(testaddr) + (testsz) >= (testaddr))

#define	DTRACE_RANGE_REMAIN(remp, addr, baseaddr, basesz)		\
do {									\
	if ((remp) != NULL) {						\
		*(remp) = (uintptr_t)(baseaddr) + (basesz) - (addr);	\
	}								\
_NOTE(CONSTCOND) } while (0)


/*
 * Test whether alloc_sz bytes will fit in the scratch region.  We isolate
 * alloc_sz on the righthand side of the comparison in order to avoid overflow
 * or underflow in the comparison with it.  This is simpler than the INRANGE
 * check above, because we know that the dtms_scratch_ptr is valid in the
 * range.  Allocations of size zero are allowed.
 */
#define	DTRACE_INSCRATCH(mstate, alloc_sz) \
	((mstate)->dtms_scratch_base + (mstate)->dtms_scratch_size - \
	(mstate)->dtms_scratch_ptr >= (alloc_sz))

#define	DTRACE_LOADFUNC(bits)						\
/*CSTYLED*/								\
uint##bits##_t								\
dtrace_load##bits(uintptr_t addr)					\
{									\
	size_t size = bits / NBBY;					\
	/*CSTYLED*/							\
	uint##bits##_t rval;						\
	int i;								\
	volatile uint16_t *flags = (volatile uint16_t *)		\
	    &cpu_core[curcpu].cpuc_dtrace_flags;			\
									\
	DTRACE_ALIGNCHECK(addr, size, flags);				\
									\
	for (i = 0; i < dtrace_toxranges; i++) {			\
		if (addr >= dtrace_toxrange[i].dtt_limit)		\
			continue;					\
									\
		if (addr + size <= dtrace_toxrange[i].dtt_base)		\
			continue;					\
									\
		/*							\
		 * This address falls within a toxic region; return 0.	\
		 */							\
		*flags |= CPU_DTRACE_BADADDR;				\
		cpu_core[curcpu].cpuc_dtrace_illval = addr;		\
		return (0);						\
	}								\
									\
	*flags |= CPU_DTRACE_NOFAULT;					\
	/*CSTYLED*/							\
	rval = *((volatile uint##bits##_t *)addr);			\
	*flags &= ~CPU_DTRACE_NOFAULT;					\
									\
	return (!(*flags & CPU_DTRACE_FAULT) ? rval : 0);		\
}

#ifdef _LP64
#define	dtrace_loadptr	dtrace_load64
#else
#define	dtrace_loadptr	dtrace_load32
#endif

#define	DTRACE_DYNHASH_FREE	0
#define	DTRACE_DYNHASH_SINK	1
#define	DTRACE_DYNHASH_VALID	2

#define	DTRACE_MATCH_NEXT	0
#define	DTRACE_MATCH_DONE	1
#define	DTRACE_ANCHORED(probe)	((probe)->dtpr_func[0] != '\0')
#define	DTRACE_STATE_ALIGN	64

#define	DTRACE_FLAGS2FLT(flags)						\
	(((flags) & CPU_DTRACE_BADADDR) ? DTRACEFLT_BADADDR :		\
	((flags) & CPU_DTRACE_ILLOP) ? DTRACEFLT_ILLOP :		\
	((flags) & CPU_DTRACE_DIVZERO) ? DTRACEFLT_DIVZERO :		\
	((flags) & CPU_DTRACE_KPRIV) ? DTRACEFLT_KPRIV :		\
	((flags) & CPU_DTRACE_UPRIV) ? DTRACEFLT_UPRIV :		\
	((flags) & CPU_DTRACE_TUPOFLOW) ?  DTRACEFLT_TUPOFLOW :		\
	((flags) & CPU_DTRACE_BADALIGN) ?  DTRACEFLT_BADALIGN :		\
	((flags) & CPU_DTRACE_NOSCRATCH) ?  DTRACEFLT_NOSCRATCH :	\
	((flags) & CPU_DTRACE_BADSTACK) ?  DTRACEFLT_BADSTACK :		\
	DTRACEFLT_UNKNOWN)

#define	DTRACEACT_ISSTRING(act)						\
	((act)->dta_kind == DTRACEACT_DIFEXPR &&			\
	(act)->dta_difo->dtdo_rtype.dtdt_kind == DIF_TYPE_STRING)

/* Function prototype definitions: */
static size_t dtrace_strlen(const char *, size_t);
static dtrace_probe_t *dtrace_probe_lookup_id(dtrace_id_t id);
static void dtrace_enabling_provide(dtrace_provider_t *);
static int dtrace_enabling_match(dtrace_enabling_t *, int *);
static void dtrace_enabling_matchall(void);
static void dtrace_enabling_reap(void);
static dtrace_state_t *dtrace_anon_grab(void);
static uint64_t dtrace_helper(int, dtrace_mstate_t *,
    dtrace_state_t *, uint64_t, uint64_t);
static dtrace_helpers_t *dtrace_helpers_create(proc_t *);
static void dtrace_buffer_drop(dtrace_buffer_t *);
static int dtrace_buffer_consumed(dtrace_buffer_t *, hrtime_t when);
static intptr_t dtrace_buffer_reserve(dtrace_buffer_t *, size_t, size_t,
    dtrace_state_t *, dtrace_mstate_t *);
static int dtrace_state_option(dtrace_state_t *, dtrace_optid_t,
    dtrace_optval_t);
static int dtrace_ecb_create_enable(dtrace_probe_t *, void *);
static void dtrace_helper_provider_destroy(dtrace_helper_provider_t *);
uint16_t dtrace_load16(uintptr_t);
uint32_t dtrace_load32(uintptr_t);
uint64_t dtrace_load64(uintptr_t);
uint8_t dtrace_load8(uintptr_t);
void dtrace_dynvar_clean(dtrace_dstate_t *);
dtrace_dynvar_t *dtrace_dynvar(dtrace_dstate_t *, uint_t, dtrace_key_t *,
    size_t, dtrace_dynvar_op_t, dtrace_mstate_t *, dtrace_vstate_t *);
uintptr_t dtrace_dif_varstr(uintptr_t, dtrace_state_t *, dtrace_mstate_t *);
static int dtrace_priv_proc(dtrace_state_t *);
static void dtrace_getf_barrier(void);
static int dtrace_canload_remains(uint64_t, size_t, size_t *,
    dtrace_mstate_t *, dtrace_vstate_t *);
static int dtrace_canstore_remains(uint64_t, size_t, size_t *,
    dtrace_mstate_t *, dtrace_vstate_t *);

/*
 * DTrace Probe Context Functions
 *
 * These functions are called from probe context.  Because probe context is
 * any context in which C may be called, arbitrarily locks may be held,
 * interrupts may be disabled, we may be in arbitrary dispatched state, etc.
 * As a result, functions called from probe context may only call other DTrace
 * support functions -- they may not interact at all with the system at large.
 * (Note that the ASSERT macro is made probe-context safe by redefining it in
 * terms of dtrace_assfail(), a probe-context safe function.) If arbitrary
 * loads are to be performed from probe context, they _must_ be in terms of
 * the safe dtrace_load*() variants.
 *
 * Some functions in this block are not actually called from probe context;
 * for these functions, there will be a comment above the function reading
 * "Note:  not called from probe context."
 */
void
dtrace_panic(const char *format, ...)
{
	va_list alist;

	va_start(alist, format);
#ifdef __FreeBSD__
	vpanic(format, alist);
#else
	dtrace_vpanic(format, alist);
#endif
	va_end(alist);
}

int
dtrace_assfail(const char *a, const char *f, int l)
{
	dtrace_panic("assertion failed: %s, file: %s, line: %d", a, f, l);

	/*
	 * We just need something here that even the most clever compiler
	 * cannot optimize away.
	 */
	return (a[(uintptr_t)f]);
}

/*
 * Atomically increment a specified error counter from probe context.
 */
static void
dtrace_error(uint32_t *counter)
{
	/*
	 * Most counters stored to in probe context are per-CPU counters.
	 * However, there are some error conditions that are sufficiently
	 * arcane that they don't merit per-CPU storage.  If these counters
	 * are incremented concurrently on different CPUs, scalability will be
	 * adversely affected -- but we don't expect them to be white-hot in a
	 * correctly constructed enabling...
	 */
	uint32_t oval, nval;

	do {
		oval = *counter;

		if ((nval = oval + 1) == 0) {
			/*
			 * If the counter would wrap, set it to 1 -- assuring
			 * that the counter is never zero when we have seen
			 * errors.  (The counter must be 32-bits because we
			 * aren't guaranteed a 64-bit compare&swap operation.)
			 * To save this code both the infamy of being fingered
			 * by a priggish news story and the indignity of being
			 * the target of a neo-puritan witch trial, we're
			 * carefully avoiding any colorful description of the
			 * likelihood of this condition -- but suffice it to
			 * say that it is only slightly more likely than the
			 * overflow of predicate cache IDs, as discussed in
			 * dtrace_predicate_create().
			 */
			nval = 1;
		}
	} while (dtrace_cas32(counter, oval, nval) != oval);
}

/*
 * Use the DTRACE_LOADFUNC macro to define functions for each of loading a
 * uint8_t, a uint16_t, a uint32_t and a uint64_t.
 */
/* BEGIN CSTYLED */
DTRACE_LOADFUNC(8)
DTRACE_LOADFUNC(16)
DTRACE_LOADFUNC(32)
DTRACE_LOADFUNC(64)
/* END CSTYLED */

static int
dtrace_inscratch(uintptr_t dest, size_t size, dtrace_mstate_t *mstate)
{
	if (dest < mstate->dtms_scratch_base)
		return (0);

	if (dest + size < dest)
		return (0);

	if (dest + size > mstate->dtms_scratch_ptr)
		return (0);

	return (1);
}

static int
dtrace_canstore_statvar(uint64_t addr, size_t sz, size_t *remain,
    dtrace_statvar_t **svars, int nsvars)
{
	int i;
	size_t maxglobalsize, maxlocalsize;

	if (nsvars == 0)
		return (0);

	maxglobalsize = dtrace_statvar_maxsize + sizeof (uint64_t);
	maxlocalsize = maxglobalsize * NCPU;

	for (i = 0; i < nsvars; i++) {
		dtrace_statvar_t *svar = svars[i];
		uint8_t scope;
		size_t size;

		if (svar == NULL || (size = svar->dtsv_size) == 0)
			continue;

		scope = svar->dtsv_var.dtdv_scope;

		/*
		 * We verify that our size is valid in the spirit of providing
		 * defense in depth:  we want to prevent attackers from using
		 * DTrace to escalate an orthogonal kernel heap corruption bug
		 * into the ability to store to arbitrary locations in memory.
		 */
		VERIFY((scope == DIFV_SCOPE_GLOBAL && size <= maxglobalsize) ||
		    (scope == DIFV_SCOPE_LOCAL && size <= maxlocalsize));

		if (DTRACE_INRANGE(addr, sz, svar->dtsv_data,
		    svar->dtsv_size)) {
			DTRACE_RANGE_REMAIN(remain, addr, svar->dtsv_data,
			    svar->dtsv_size);
			return (1);
		}
	}

	return (0);
}

/*
 * Check to see if the address is within a memory region to which a store may
 * be issued.  This includes the DTrace scratch areas, and any DTrace variable
 * region.  The caller of dtrace_canstore() is responsible for performing any
 * alignment checks that are needed before stores are actually executed.
 */
static int
dtrace_canstore(uint64_t addr, size_t sz, dtrace_mstate_t *mstate,
    dtrace_vstate_t *vstate)
{
	return (dtrace_canstore_remains(addr, sz, NULL, mstate, vstate));
}

/*
 * Implementation of dtrace_canstore which communicates the upper bound of the
 * allowed memory region.
 */
static int
dtrace_canstore_remains(uint64_t addr, size_t sz, size_t *remain,
    dtrace_mstate_t *mstate, dtrace_vstate_t *vstate)
{
	/*
	 * First, check to see if the address is in scratch space...
	 */
	if (DTRACE_INRANGE(addr, sz, mstate->dtms_scratch_base,
	    mstate->dtms_scratch_size)) {
		DTRACE_RANGE_REMAIN(remain, addr, mstate->dtms_scratch_base,
		    mstate->dtms_scratch_size);
		return (1);
	}

	/*
	 * Now check to see if it's a dynamic variable.  This check will pick
	 * up both thread-local variables and any global dynamically-allocated
	 * variables.
	 */
	if (DTRACE_INRANGE(addr, sz, vstate->dtvs_dynvars.dtds_base,
	    vstate->dtvs_dynvars.dtds_size)) {
		dtrace_dstate_t *dstate = &vstate->dtvs_dynvars;
		uintptr_t base = (uintptr_t)dstate->dtds_base +
		    (dstate->dtds_hashsize * sizeof (dtrace_dynhash_t));
		uintptr_t chunkoffs;
		dtrace_dynvar_t *dvar;

		/*
		 * Before we assume that we can store here, we need to make
		 * sure that it isn't in our metadata -- storing to our
		 * dynamic variable metadata would corrupt our state.  For
		 * the range to not include any dynamic variable metadata,
		 * it must:
		 *
		 *	(1) Start above the hash table that is at the base of
		 *	the dynamic variable space
		 *
		 *	(2) Have a starting chunk offset that is beyond the
		 *	dtrace_dynvar_t that is at the base of every chunk
		 *
		 *	(3) Not span a chunk boundary
		 *
		 *	(4) Not be in the tuple space of a dynamic variable
		 *
		 */
		if (addr < base)
			return (0);

		chunkoffs = (addr - base) % dstate->dtds_chunksize;

		if (chunkoffs < sizeof (dtrace_dynvar_t))
			return (0);

		if (chunkoffs + sz > dstate->dtds_chunksize)
			return (0);

		dvar = (dtrace_dynvar_t *)((uintptr_t)addr - chunkoffs);

		if (dvar->dtdv_hashval == DTRACE_DYNHASH_FREE)
			return (0);

		if (chunkoffs < sizeof (dtrace_dynvar_t) +
		    ((dvar->dtdv_tuple.dtt_nkeys - 1) * sizeof (dtrace_key_t)))
			return (0);

		DTRACE_RANGE_REMAIN(remain, addr, dvar, dstate->dtds_chunksize);
		return (1);
	}

	/*
	 * Finally, check the static local and global variables.  These checks
	 * take the longest, so we perform them last.
	 */
	if (dtrace_canstore_statvar(addr, sz, remain,
	    vstate->dtvs_locals, vstate->dtvs_nlocals))
		return (1);

	if (dtrace_canstore_statvar(addr, sz, remain,
	    vstate->dtvs_globals, vstate->dtvs_nglobals))
		return (1);

	return (0);
}


/*
 * Convenience routine to check to see if the address is within a memory
 * region in which a load may be issued given the user's privilege level;
 * if not, it sets the appropriate error flags and loads 'addr' into the
 * illegal value slot.
 *
 * DTrace subroutines (DIF_SUBR_*) should use this helper to implement
 * appropriate memory access protection.
 */
static int
dtrace_canload(uint64_t addr, size_t sz, dtrace_mstate_t *mstate,
    dtrace_vstate_t *vstate)
{
	return (dtrace_canload_remains(addr, sz, NULL, mstate, vstate));
}

/*
 * Implementation of dtrace_canload which communicates the uppoer bound of the
 * allowed memory region.
 */
static int
dtrace_canload_remains(uint64_t addr, size_t sz, size_t *remain,
    dtrace_mstate_t *mstate, dtrace_vstate_t *vstate)
{
	volatile uintptr_t *illval = &cpu_core[curcpu].cpuc_dtrace_illval;
	file_t *fp;

	/*
	 * If we hold the privilege to read from kernel memory, then
	 * everything is readable.
	 */
	if ((mstate->dtms_access & DTRACE_ACCESS_KERNEL) != 0) {
		DTRACE_RANGE_REMAIN(remain, addr, addr, sz);
		return (1);
	}

	/*
	 * You can obviously read that which you can store.
	 */
	if (dtrace_canstore_remains(addr, sz, remain, mstate, vstate))
		return (1);

	/*
	 * We're allowed to read from our own string table.
	 */
	if (DTRACE_INRANGE(addr, sz, mstate->dtms_difo->dtdo_strtab,
	    mstate->dtms_difo->dtdo_strlen)) {
		DTRACE_RANGE_REMAIN(remain, addr,
		    mstate->dtms_difo->dtdo_strtab,
		    mstate->dtms_difo->dtdo_strlen);
		return (1);
	}

	if (vstate->dtvs_state != NULL &&
	    dtrace_priv_proc(vstate->dtvs_state)) {
		proc_t *p;

		/*
		 * When we have privileges to the current process, there are
		 * several context-related kernel structures that are safe to
		 * read, even absent the privilege to read from kernel memory.
		 * These reads are safe because these structures contain only
		 * state that (1) we're permitted to read, (2) is harmless or
		 * (3) contains pointers to additional kernel state that we're
		 * not permitted to read (and as such, do not present an
		 * opportunity for privilege escalation).  Finally (and
		 * critically), because of the nature of their relation with
		 * the current thread context, the memory associated with these
		 * structures cannot change over the duration of probe context,
		 * and it is therefore impossible for this memory to be
		 * deallocated and reallocated as something else while it's
		 * being operated upon.
		 */
		if (DTRACE_INRANGE(addr, sz, curthread, sizeof (kthread_t))) {
			DTRACE_RANGE_REMAIN(remain, addr, curthread,
			    sizeof (kthread_t));
			return (1);
		}

		if ((p = curthread->t_procp) != NULL && DTRACE_INRANGE(addr,
		    sz, curthread->t_procp, sizeof (proc_t))) {
			DTRACE_RANGE_REMAIN(remain, addr, curthread->t_procp,
			    sizeof (proc_t));
			return (1);
		}

		if (curthread->t_cred != NULL && DTRACE_INRANGE(addr, sz,
		    curthread->t_cred, sizeof (cred_t))) {
			DTRACE_RANGE_REMAIN(remain, addr, curthread->t_cred,
			    sizeof (cred_t));
			return (1);
		}

#ifdef illumos
		if (p != NULL && p->p_pidp != NULL && DTRACE_INRANGE(addr, sz,
		    &(p->p_pidp->pid_id), sizeof (pid_t))) {
			DTRACE_RANGE_REMAIN(remain, addr, &(p->p_pidp->pid_id),
			    sizeof (pid_t));
			return (1);
		}

		if (curthread->t_cpu != NULL && DTRACE_INRANGE(addr, sz,
		    curthread->t_cpu, offsetof(cpu_t, cpu_pause_thread))) {
			DTRACE_RANGE_REMAIN(remain, addr, curthread->t_cpu,
			    offsetof(cpu_t, cpu_pause_thread));
			return (1);
		}
#endif
	}

	if ((fp = mstate->dtms_getf) != NULL) {
		uintptr_t psz = sizeof (void *);
		vnode_t *vp;
		vnodeops_t *op;

		/*
		 * When getf() returns a file_t, the enabling is implicitly
		 * granted the (transient) right to read the returned file_t
		 * as well as the v_path and v_op->vnop_name of the underlying
		 * vnode.  These accesses are allowed after a successful
		 * getf() because the members that they refer to cannot change
		 * once set -- and the barrier logic in the kernel's closef()
		 * path assures that the file_t and its referenced vode_t
		 * cannot themselves be stale (that is, it impossible for
		 * either dtms_getf itself or its f_vnode member to reference
		 * freed memory).
		 */
		if (DTRACE_INRANGE(addr, sz, fp, sizeof (file_t))) {
			DTRACE_RANGE_REMAIN(remain, addr, fp, sizeof (file_t));
			return (1);
		}

		if ((vp = fp->f_vnode) != NULL) {
			size_t slen;
#ifdef illumos
			if (DTRACE_INRANGE(addr, sz, &vp->v_path, psz)) {
				DTRACE_RANGE_REMAIN(remain, addr, &vp->v_path,
				    psz);
				return (1);
			}
			slen = strlen(vp->v_path) + 1;
			if (DTRACE_INRANGE(addr, sz, vp->v_path, slen)) {
				DTRACE_RANGE_REMAIN(remain, addr, vp->v_path,
				    slen);
				return (1);
			}
#endif

			if (DTRACE_INRANGE(addr, sz, &vp->v_op, psz)) {
				DTRACE_RANGE_REMAIN(remain, addr, &vp->v_op,
				    psz);
				return (1);
			}

#ifdef illumos
			if ((op = vp->v_op) != NULL &&
			    DTRACE_INRANGE(addr, sz, &op->vnop_name, psz)) {
				DTRACE_RANGE_REMAIN(remain, addr,
				    &op->vnop_name, psz);
				return (1);
			}

			if (op != NULL && op->vnop_name != NULL &&
			    DTRACE_INRANGE(addr, sz, op->vnop_name,
			    (slen = strlen(op->vnop_name) + 1))) {
				DTRACE_RANGE_REMAIN(remain, addr,
				    op->vnop_name, slen);
				return (1);
			}
#endif
		}
	}

	DTRACE_CPUFLAG_SET(CPU_DTRACE_KPRIV);
	*illval = addr;
	return (0);
}

/*
 * Convenience routine to check to see if a given string is within a memory
 * region in which a load may be issued given the user's privilege level;
 * this exists so that we don't need to issue unnecessary dtrace_strlen()
 * calls in the event that the user has all privileges.
 */
static int
dtrace_strcanload(uint64_t addr, size_t sz, size_t *remain,
    dtrace_mstate_t *mstate, dtrace_vstate_t *vstate)
{
	size_t rsize;

	/*
	 * If we hold the privilege to read from kernel memory, then
	 * everything is readable.
	 */
	if ((mstate->dtms_access & DTRACE_ACCESS_KERNEL) != 0) {
		DTRACE_RANGE_REMAIN(remain, addr, addr, sz);
		return (1);
	}

	/*
	 * Even if the caller is uninterested in querying the remaining valid
	 * range, it is required to ensure that the access is allowed.
	 */
	if (remain == NULL) {
		remain = &rsize;
	}
	if (dtrace_canload_remains(addr, 0, remain, mstate, vstate)) {
		size_t strsz;
		/*
		 * Perform the strlen after determining the length of the
		 * memory region which is accessible.  This prevents timing
		 * information from being used to find NULs in memory which is
		 * not accessible to the caller.
		 */
		strsz = 1 + dtrace_strlen((char *)(uintptr_t)addr,
		    MIN(sz, *remain));
		if (strsz <= *remain) {
			return (1);
		}
	}

	return (0);
}

/*
 * Convenience routine to check to see if a given variable is within a memory
 * region in which a load may be issued given the user's privilege level.
 */
static int
dtrace_vcanload(void *src, dtrace_diftype_t *type, size_t *remain,
    dtrace_mstate_t *mstate, dtrace_vstate_t *vstate)
{
	size_t sz;
	ASSERT(type->dtdt_flags & DIF_TF_BYREF);

	/*
	 * Calculate the max size before performing any checks since even
	 * DTRACE_ACCESS_KERNEL-credentialed callers expect that this function
	 * return the max length via 'remain'.
	 */
	if (type->dtdt_kind == DIF_TYPE_STRING) {
		dtrace_state_t *state = vstate->dtvs_state;

		if (state != NULL) {
			sz = state->dts_options[DTRACEOPT_STRSIZE];
		} else {
			/*
			 * In helper context, we have a NULL state; fall back
			 * to using the system-wide default for the string size
			 * in this case.
			 */
			sz = dtrace_strsize_default;
		}
	} else {
		sz = type->dtdt_size;
	}

	/*
	 * If we hold the privilege to read from kernel memory, then
	 * everything is readable.
	 */
	if ((mstate->dtms_access & DTRACE_ACCESS_KERNEL) != 0) {
		DTRACE_RANGE_REMAIN(remain, (uintptr_t)src, src, sz);
		return (1);
	}

	if (type->dtdt_kind == DIF_TYPE_STRING) {
		return (dtrace_strcanload((uintptr_t)src, sz, remain, mstate,
		    vstate));
	}
	return (dtrace_canload_remains((uintptr_t)src, sz, remain, mstate,
	    vstate));
}

/*
 * Convert a string to a signed integer using safe loads.
 *
 * NOTE: This function uses various macros from strtolctype.h to manipulate
 * digit values, etc -- these have all been checked to ensure they make
 * no additional function calls.
 */
static int64_t
dtrace_strtoll(char *input, int base, size_t limit)
{
	uintptr_t pos = (uintptr_t)input;
	int64_t val = 0;
	int x;
	boolean_t neg = B_FALSE;
	char c, cc, ccc;
	uintptr_t end = pos + limit;

	/*
	 * Consume any whitespace preceding digits.
	 */
	while ((c = dtrace_load8(pos)) == ' ' || c == '\t')
		pos++;

	/*
	 * Handle an explicit sign if one is present.
	 */
	if (c == '-' || c == '+') {
		if (c == '-')
			neg = B_TRUE;
		c = dtrace_load8(++pos);
	}

	/*
	 * Check for an explicit hexadecimal prefix ("0x" or "0X") and skip it
	 * if present.
	 */
	if (base == 16 && c == '0' && ((cc = dtrace_load8(pos + 1)) == 'x' ||
	    cc == 'X') && isxdigit(ccc = dtrace_load8(pos + 2))) {
		pos += 2;
		c = ccc;
	}

	/*
	 * Read in contiguous digits until the first non-digit character.
	 */
	for (; pos < end && c != '\0' && lisalnum(c) && (x = DIGIT(c)) < base;
	    c = dtrace_load8(++pos))
		val = val * base + x;

	return (neg ? -val : val);
}

/*
 * Compare two strings using safe loads.
 */
static int
dtrace_strncmp(char *s1, char *s2, size_t limit)
{
	uint8_t c1, c2;
	volatile uint16_t *flags;

	if (s1 == s2 || limit == 0)
		return (0);

	flags = (volatile uint16_t *)&cpu_core[curcpu].cpuc_dtrace_flags;

	do {
		if (s1 == NULL) {
			c1 = '\0';
		} else {
			c1 = dtrace_load8((uintptr_t)s1++);
		}

		if (s2 == NULL) {
			c2 = '\0';
		} else {
			c2 = dtrace_load8((uintptr_t)s2++);
		}

		if (c1 != c2)
			return (c1 - c2);
	} while (--limit && c1 != '\0' && !(*flags & CPU_DTRACE_FAULT));

	return (0);
}

/*
 * Compute strlen(s) for a string using safe memory accesses.  The additional
 * len parameter is used to specify a maximum length to ensure completion.
 */
static size_t
dtrace_strlen(const char *s, size_t lim)
{
	uint_t len;

	for (len = 0; len != lim; len++) {
		if (dtrace_load8((uintptr_t)s++) == '\0')
			break;
	}

	return (len);
}

/*
 * Check if an address falls within a toxic region.
 */
static int
dtrace_istoxic(uintptr_t kaddr, size_t size)
{
	uintptr_t taddr, tsize;
	int i;

	for (i = 0; i < dtrace_toxranges; i++) {
		taddr = dtrace_toxrange[i].dtt_base;
		tsize = dtrace_toxrange[i].dtt_limit - taddr;

		if (kaddr - taddr < tsize) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
			cpu_core[curcpu].cpuc_dtrace_illval = kaddr;
			return (1);
		}

		if (taddr - kaddr < size) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
			cpu_core[curcpu].cpuc_dtrace_illval = taddr;
			return (1);
		}
	}

	return (0);
}

/*
 * Copy src to dst using safe memory accesses.  The src is assumed to be unsafe
 * memory specified by the DIF program.  The dst is assumed to be safe memory
 * that we can store to directly because it is managed by DTrace.  As with
 * standard bcopy, overlapping copies are handled properly.
 */
static void
dtrace_bcopy(const void *src, void *dst, size_t len)
{
	if (len != 0) {
		uint8_t *s1 = dst;
		const uint8_t *s2 = src;

		if (s1 <= s2) {
			do {
				*s1++ = dtrace_load8((uintptr_t)s2++);
			} while (--len != 0);
		} else {
			s2 += len;
			s1 += len;

			do {
				*--s1 = dtrace_load8((uintptr_t)--s2);
			} while (--len != 0);
		}
	}
}

/*
 * Copy src to dst using safe memory accesses, up to either the specified
 * length, or the point that a nul byte is encountered.  The src is assumed to
 * be unsafe memory specified by the DIF program.  The dst is assumed to be
 * safe memory that we can store to directly because it is managed by DTrace.
 * Unlike dtrace_bcopy(), overlapping regions are not handled.
 */
static void
dtrace_strcpy(const void *src, void *dst, size_t len)
{
	if (len != 0) {
		uint8_t *s1 = dst, c;
		const uint8_t *s2 = src;

		do {
			*s1++ = c = dtrace_load8((uintptr_t)s2++);
		} while (--len != 0 && c != '\0');
	}
}

/*
 * Copy src to dst, deriving the size and type from the specified (BYREF)
 * variable type.  The src is assumed to be unsafe memory specified by the DIF
 * program.  The dst is assumed to be DTrace variable memory that is of the
 * specified type; we assume that we can store to directly.
 */
static void
dtrace_vcopy(void *src, void *dst, dtrace_diftype_t *type, size_t limit)
{
	ASSERT(type->dtdt_flags & DIF_TF_BYREF);

	if (type->dtdt_kind == DIF_TYPE_STRING) {
		dtrace_strcpy(src, dst, MIN(type->dtdt_size, limit));
	} else {
		dtrace_bcopy(src, dst, MIN(type->dtdt_size, limit));
	}
}

/*
 * Compare s1 to s2 using safe memory accesses.  The s1 data is assumed to be
 * unsafe memory specified by the DIF program.  The s2 data is assumed to be
 * safe memory that we can access directly because it is managed by DTrace.
 */
static int
dtrace_bcmp(const void *s1, const void *s2, size_t len)
{
	volatile uint16_t *flags;

	flags = (volatile uint16_t *)&cpu_core[curcpu].cpuc_dtrace_flags;

	if (s1 == s2)
		return (0);

	if (s1 == NULL || s2 == NULL)
		return (1);

	if (s1 != s2 && len != 0) {
		const uint8_t *ps1 = s1;
		const uint8_t *ps2 = s2;

		do {
			if (dtrace_load8((uintptr_t)ps1++) != *ps2++)
				return (1);
		} while (--len != 0 && !(*flags & CPU_DTRACE_FAULT));
	}
	return (0);
}

/*
 * Zero the specified region using a simple byte-by-byte loop.  Note that this
 * is for safe DTrace-managed memory only.
 */
static void
dtrace_bzero(void *dst, size_t len)
{
	uchar_t *cp;

	for (cp = dst; len != 0; len--)
		*cp++ = 0;
}

static void
dtrace_add_128(uint64_t *addend1, uint64_t *addend2, uint64_t *sum)
{
	uint64_t result[2];

	result[0] = addend1[0] + addend2[0];
	result[1] = addend1[1] + addend2[1] +
	    (result[0] < addend1[0] || result[0] < addend2[0] ? 1 : 0);

	sum[0] = result[0];
	sum[1] = result[1];
}

/*
 * Shift the 128-bit value in a by b. If b is positive, shift left.
 * If b is negative, shift right.
 */
static void
dtrace_shift_128(uint64_t *a, int b)
{
	uint64_t mask;

	if (b == 0)
		return;

	if (b < 0) {
		b = -b;
		if (b >= 64) {
			a[0] = a[1] >> (b - 64);
			a[1] = 0;
		} else {
			a[0] >>= b;
			mask = 1LL << (64 - b);
			mask -= 1;
			a[0] |= ((a[1] & mask) << (64 - b));
			a[1] >>= b;
		}
	} else {
		if (b >= 64) {
			a[1] = a[0] << (b - 64);
			a[0] = 0;
		} else {
			a[1] <<= b;
			mask = a[0] >> (64 - b);
			a[1] |= mask;
			a[0] <<= b;
		}
	}
}

/*
 * The basic idea is to break the 2 64-bit values into 4 32-bit values,
 * use native multiplication on those, and then re-combine into the
 * resulting 128-bit value.
 *
 * (hi1 << 32 + lo1) * (hi2 << 32 + lo2) =
 *     hi1 * hi2 << 64 +
 *     hi1 * lo2 << 32 +
 *     hi2 * lo1 << 32 +
 *     lo1 * lo2
 */
static void
dtrace_multiply_128(uint64_t factor1, uint64_t factor2, uint64_t *product)
{
	uint64_t hi1, hi2, lo1, lo2;
	uint64_t tmp[2];

	hi1 = factor1 >> 32;
	hi2 = factor2 >> 32;

	lo1 = factor1 & DT_MASK_LO;
	lo2 = factor2 & DT_MASK_LO;

	product[0] = lo1 * lo2;
	product[1] = hi1 * hi2;

	tmp[0] = hi1 * lo2;
	tmp[1] = 0;
	dtrace_shift_128(tmp, 32);
	dtrace_add_128(product, tmp, product);

	tmp[0] = hi2 * lo1;
	tmp[1] = 0;
	dtrace_shift_128(tmp, 32);
	dtrace_add_128(product, tmp, product);
}

/*
 * This privilege check should be used by actions and subroutines to
 * verify that the user credentials of the process that enabled the
 * invoking ECB match the target credentials
 */
static int
dtrace_priv_proc_common_user(dtrace_state_t *state)
{
	cred_t *cr, *s_cr = state->dts_cred.dcr_cred;

	/*
	 * We should always have a non-NULL state cred here, since if cred
	 * is null (anonymous tracing), we fast-path bypass this routine.
	 */
	ASSERT(s_cr != NULL);

	if ((cr = CRED()) != NULL &&
	    s_cr->cr_uid == cr->cr_uid &&
	    s_cr->cr_uid == cr->cr_ruid &&
	    s_cr->cr_uid == cr->cr_suid &&
	    s_cr->cr_gid == cr->cr_gid &&
	    s_cr->cr_gid == cr->cr_rgid &&
	    s_cr->cr_gid == cr->cr_sgid)
		return (1);

	return (0);
}

/*
 * This privilege check should be used by actions and subroutines to
 * verify that the zone of the process that enabled the invoking ECB
 * matches the target credentials
 */
static int
dtrace_priv_proc_common_zone(dtrace_state_t *state)
{
#ifdef illumos
	cred_t *cr, *s_cr = state->dts_cred.dcr_cred;

	/*
	 * We should always have a non-NULL state cred here, since if cred
	 * is null (anonymous tracing), we fast-path bypass this routine.
	 */
	ASSERT(s_cr != NULL);

	if ((cr = CRED()) != NULL && s_cr->cr_zone == cr->cr_zone)
		return (1);

	return (0);
#else
	return (1);
#endif
}

/*
 * This privilege check should be used by actions and subroutines to
 * verify that the process has not setuid or changed credentials.
 */
static int
dtrace_priv_proc_common_nocd(void)
{
	proc_t *proc;

	if ((proc = ttoproc(curthread)) != NULL &&
	    !(proc->p_flag & SNOCD))
		return (1);

	return (0);
}

static int
dtrace_priv_proc_destructive(dtrace_state_t *state)
{
	int action = state->dts_cred.dcr_action;

	if (((action & DTRACE_CRA_PROC_DESTRUCTIVE_ALLZONE) == 0) &&
	    dtrace_priv_proc_common_zone(state) == 0)
		goto bad;

	if (((action & DTRACE_CRA_PROC_DESTRUCTIVE_ALLUSER) == 0) &&
	    dtrace_priv_proc_common_user(state) == 0)
		goto bad;

	if (((action & DTRACE_CRA_PROC_DESTRUCTIVE_CREDCHG) == 0) &&
	    dtrace_priv_proc_common_nocd() == 0)
		goto bad;

	return (1);

bad:
	cpu_core[curcpu].cpuc_dtrace_flags |= CPU_DTRACE_UPRIV;

	return (0);
}

static int
dtrace_priv_proc_control(dtrace_state_t *state)
{
	if (state->dts_cred.dcr_action & DTRACE_CRA_PROC_CONTROL)
		return (1);

	if (dtrace_priv_proc_common_zone(state) &&
	    dtrace_priv_proc_common_user(state) &&
	    dtrace_priv_proc_common_nocd())
		return (1);

	cpu_core[curcpu].cpuc_dtrace_flags |= CPU_DTRACE_UPRIV;

	return (0);
}

static int
dtrace_priv_proc(dtrace_state_t *state)
{
	if (state->dts_cred.dcr_action & DTRACE_CRA_PROC)
		return (1);

	cpu_core[curcpu].cpuc_dtrace_flags |= CPU_DTRACE_UPRIV;

	return (0);
}

static int
dtrace_priv_kernel(dtrace_state_t *state)
{
	if (state->dts_cred.dcr_action & DTRACE_CRA_KERNEL)
		return (1);

	cpu_core[curcpu].cpuc_dtrace_flags |= CPU_DTRACE_KPRIV;

	return (0);
}

static int
dtrace_priv_kernel_destructive(dtrace_state_t *state)
{
	if (state->dts_cred.dcr_action & DTRACE_CRA_KERNEL_DESTRUCTIVE)
		return (1);

	cpu_core[curcpu].cpuc_dtrace_flags |= CPU_DTRACE_KPRIV;

	return (0);
}

/*
 * Determine if the dte_cond of the specified ECB allows for processing of
 * the current probe to continue.  Note that this routine may allow continued
 * processing, but with access(es) stripped from the mstate's dtms_access
 * field.
 */
static int
dtrace_priv_probe(dtrace_state_t *state, dtrace_mstate_t *mstate,
    dtrace_ecb_t *ecb)
{
	dtrace_probe_t *probe = ecb->dte_probe;
	dtrace_provider_t *prov = probe->dtpr_provider;
	dtrace_pops_t *pops = &prov->dtpv_pops;
	int mode = DTRACE_MODE_NOPRIV_DROP;

	ASSERT(ecb->dte_cond);

#ifdef illumos
	if (pops->dtps_mode != NULL) {
		mode = pops->dtps_mode(prov->dtpv_arg,
		    probe->dtpr_id, probe->dtpr_arg);

		ASSERT((mode & DTRACE_MODE_USER) ||
		    (mode & DTRACE_MODE_KERNEL));
		ASSERT((mode & DTRACE_MODE_NOPRIV_RESTRICT) ||
		    (mode & DTRACE_MODE_NOPRIV_DROP));
	}

	/*
	 * If the dte_cond bits indicate that this consumer is only allowed to
	 * see user-mode firings of this probe, call the provider's dtps_mode()
	 * entry point to check that the probe was fired while in a user
	 * context.  If that's not the case, use the policy specified by the
	 * provider to determine if we drop the probe or merely restrict
	 * operation.
	 */
	if (ecb->dte_cond & DTRACE_COND_USERMODE) {
		ASSERT(mode != DTRACE_MODE_NOPRIV_DROP);

		if (!(mode & DTRACE_MODE_USER)) {
			if (mode & DTRACE_MODE_NOPRIV_DROP)
				return (0);

			mstate->dtms_access &= ~DTRACE_ACCESS_ARGS;
		}
	}
#endif

	/*
	 * This is more subtle than it looks. We have to be absolutely certain
	 * that CRED() isn't going to change out from under us so it's only
	 * legit to examine that structure if we're in constrained situations.
	 * Currently, the only times we'll this check is if a non-super-user
	 * has enabled the profile or syscall providers -- providers that
	 * allow visibility of all processes. For the profile case, the check
	 * above will ensure that we're examining a user context.
	 */
	if (ecb->dte_cond & DTRACE_COND_OWNER) {
		cred_t *cr;
		cred_t *s_cr = state->dts_cred.dcr_cred;
		proc_t *proc;

		ASSERT(s_cr != NULL);

		if ((cr = CRED()) == NULL ||
		    s_cr->cr_uid != cr->cr_uid ||
		    s_cr->cr_uid != cr->cr_ruid ||
		    s_cr->cr_uid != cr->cr_suid ||
		    s_cr->cr_gid != cr->cr_gid ||
		    s_cr->cr_gid != cr->cr_rgid ||
		    s_cr->cr_gid != cr->cr_sgid ||
		    (proc = ttoproc(curthread)) == NULL ||
		    (proc->p_flag & SNOCD)) {
			if (mode & DTRACE_MODE_NOPRIV_DROP)
				return (0);

#ifdef illumos
			mstate->dtms_access &= ~DTRACE_ACCESS_PROC;
#endif
		}
	}

#ifdef illumos
	/*
	 * If our dte_cond is set to DTRACE_COND_ZONEOWNER and we are not
	 * in our zone, check to see if our mode policy is to restrict rather
	 * than to drop; if to restrict, strip away both DTRACE_ACCESS_PROC
	 * and DTRACE_ACCESS_ARGS
	 */
	if (ecb->dte_cond & DTRACE_COND_ZONEOWNER) {
		cred_t *cr;
		cred_t *s_cr = state->dts_cred.dcr_cred;

		ASSERT(s_cr != NULL);

		if ((cr = CRED()) == NULL ||
		    s_cr->cr_zone->zone_id != cr->cr_zone->zone_id) {
			if (mode & DTRACE_MODE_NOPRIV_DROP)
				return (0);

			mstate->dtms_access &=
			    ~(DTRACE_ACCESS_PROC | DTRACE_ACCESS_ARGS);
		}
	}
#endif

	return (1);
}

/*
 * Note:  not called from probe context.  This function is called
 * asynchronously (and at a regular interval) from outside of probe context to
 * clean the dirty dynamic variable lists on all CPUs.  Dynamic variable
 * cleaning is explained in detail in <sys/dtrace_impl.h>.
 */
void
dtrace_dynvar_clean(dtrace_dstate_t *dstate)
{
	dtrace_dynvar_t *dirty;
	dtrace_dstate_percpu_t *dcpu;
	dtrace_dynvar_t **rinsep;
	int i, j, work = 0;

	for (i = 0; i < NCPU; i++) {
		dcpu = &dstate->dtds_percpu[i];
		rinsep = &dcpu->dtdsc_rinsing;

		/*
		 * If the dirty list is NULL, there is no dirty work to do.
		 */
		if (dcpu->dtdsc_dirty == NULL)
			continue;

		if (dcpu->dtdsc_rinsing != NULL) {
			/*
			 * If the rinsing list is non-NULL, then it is because
			 * this CPU was selected to accept another CPU's
			 * dirty list -- and since that time, dirty buffers
			 * have accumulated.  This is a highly unlikely
			 * condition, but we choose to ignore the dirty
			 * buffers -- they'll be picked up a future cleanse.
			 */
			continue;
		}

		if (dcpu->dtdsc_clean != NULL) {
			/*
			 * If the clean list is non-NULL, then we're in a
			 * situation where a CPU has done deallocations (we
			 * have a non-NULL dirty list) but no allocations (we
			 * also have a non-NULL clean list).  We can't simply
			 * move the dirty list into the clean list on this
			 * CPU, yet we also don't want to allow this condition
			 * to persist, lest a short clean list prevent a
			 * massive dirty list from being cleaned (which in
			 * turn could lead to otherwise avoidable dynamic
			 * drops).  To deal with this, we look for some CPU
			 * with a NULL clean list, NULL dirty list, and NULL
			 * rinsing list -- and then we borrow this CPU to
			 * rinse our dirty list.
			 */
			for (j = 0; j < NCPU; j++) {
				dtrace_dstate_percpu_t *rinser;

				rinser = &dstate->dtds_percpu[j];

				if (rinser->dtdsc_rinsing != NULL)
					continue;

				if (rinser->dtdsc_dirty != NULL)
					continue;

				if (rinser->dtdsc_clean != NULL)
					continue;

				rinsep = &rinser->dtdsc_rinsing;
				break;
			}

			if (j == NCPU) {
				/*
				 * We were unable to find another CPU that
				 * could accept this dirty list -- we are
				 * therefore unable to clean it now.
				 */
				dtrace_dynvar_failclean++;
				continue;
			}
		}

		work = 1;

		/*
		 * Atomically move the dirty list aside.
		 */
		do {
			dirty = dcpu->dtdsc_dirty;

			/*
			 * Before we zap the dirty list, set the rinsing list.
			 * (This allows for a potential assertion in
			 * dtrace_dynvar():  if a free dynamic variable appears
			 * on a hash chain, either the dirty list or the
			 * rinsing list for some CPU must be non-NULL.)
			 */
			*rinsep = dirty;
			dtrace_membar_producer();
		} while (dtrace_casptr(&dcpu->dtdsc_dirty,
		    dirty, NULL) != dirty);
	}

	if (!work) {
		/*
		 * We have no work to do; we can simply return.
		 */
		return;
	}

	dtrace_sync();

	for (i = 0; i < NCPU; i++) {
		dcpu = &dstate->dtds_percpu[i];

		if (dcpu->dtdsc_rinsing == NULL)
			continue;

		/*
		 * We are now guaranteed that no hash chain contains a pointer
		 * into this dirty list; we can make it clean.
		 */
		ASSERT(dcpu->dtdsc_clean == NULL);
		dcpu->dtdsc_clean = dcpu->dtdsc_rinsing;
		dcpu->dtdsc_rinsing = NULL;
	}

	/*
	 * Before we actually set the state to be DTRACE_DSTATE_CLEAN, make
	 * sure that all CPUs have seen all of the dtdsc_clean pointers.
	 * This prevents a race whereby a CPU incorrectly decides that
	 * the state should be something other than DTRACE_DSTATE_CLEAN
	 * after dtrace_dynvar_clean() has completed.
	 */
	dtrace_sync();

	dstate->dtds_state = DTRACE_DSTATE_CLEAN;
}

/*
 * Depending on the value of the op parameter, this function looks-up,
 * allocates or deallocates an arbitrarily-keyed dynamic variable.  If an
 * allocation is requested, this function will return a pointer to a
 * dtrace_dynvar_t corresponding to the allocated variable -- or NULL if no
 * variable can be allocated.  If NULL is returned, the appropriate counter
 * will be incremented.
 */
dtrace_dynvar_t *
dtrace_dynvar(dtrace_dstate_t *dstate, uint_t nkeys,
    dtrace_key_t *key, size_t dsize, dtrace_dynvar_op_t op,
    dtrace_mstate_t *mstate, dtrace_vstate_t *vstate)
{
	uint64_t hashval = DTRACE_DYNHASH_VALID;
	dtrace_dynhash_t *hash = dstate->dtds_hash;
	dtrace_dynvar_t *free, *new_free, *next, *dvar, *start, *prev = NULL;
	processorid_t me = curcpu, cpu = me;
	dtrace_dstate_percpu_t *dcpu = &dstate->dtds_percpu[me];
	size_t bucket, ksize;
	size_t chunksize = dstate->dtds_chunksize;
	uintptr_t kdata, lock, nstate;
	uint_t i;

	ASSERT(nkeys != 0);

	/*
	 * Hash the key.  As with aggregations, we use Jenkins' "One-at-a-time"
	 * algorithm.  For the by-value portions, we perform the algorithm in
	 * 16-bit chunks (as opposed to 8-bit chunks).  This speeds things up a
	 * bit, and seems to have only a minute effect on distribution.  For
	 * the by-reference data, we perform "One-at-a-time" iterating (safely)
	 * over each referenced byte.  It's painful to do this, but it's much
	 * better than pathological hash distribution.  The efficacy of the
	 * hashing algorithm (and a comparison with other algorithms) may be
	 * found by running the ::dtrace_dynstat MDB dcmd.
	 */
	for (i = 0; i < nkeys; i++) {
		if (key[i].dttk_size == 0) {
			uint64_t val = key[i].dttk_value;

			hashval += (val >> 48) & 0xffff;
			hashval += (hashval << 10);
			hashval ^= (hashval >> 6);

			hashval += (val >> 32) & 0xffff;
			hashval += (hashval << 10);
			hashval ^= (hashval >> 6);

			hashval += (val >> 16) & 0xffff;
			hashval += (hashval << 10);
			hashval ^= (hashval >> 6);

			hashval += val & 0xffff;
			hashval += (hashval << 10);
			hashval ^= (hashval >> 6);
		} else {
			/*
			 * This is incredibly painful, but it beats the hell
			 * out of the alternative.
			 */
			uint64_t j, size = key[i].dttk_size;
			uintptr_t base = (uintptr_t)key[i].dttk_value;

			if (!dtrace_canload(base, size, mstate, vstate))
				break;

			for (j = 0; j < size; j++) {
				hashval += dtrace_load8(base + j);
				hashval += (hashval << 10);
				hashval ^= (hashval >> 6);
			}
		}
	}

	if (DTRACE_CPUFLAG_ISSET(CPU_DTRACE_FAULT))
		return (NULL);

	hashval += (hashval << 3);
	hashval ^= (hashval >> 11);
	hashval += (hashval << 15);

	/*
	 * There is a remote chance (ideally, 1 in 2^31) that our hashval
	 * comes out to be one of our two sentinel hash values.  If this
	 * actually happens, we set the hashval to be a value known to be a
	 * non-sentinel value.
	 */
	if (hashval == DTRACE_DYNHASH_FREE || hashval == DTRACE_DYNHASH_SINK)
		hashval = DTRACE_DYNHASH_VALID;

	/*
	 * Yes, it's painful to do a divide here.  If the cycle count becomes
	 * important here, tricks can be pulled to reduce it.  (However, it's
	 * critical that hash collisions be kept to an absolute minimum;
	 * they're much more painful than a divide.)  It's better to have a
	 * solution that generates few collisions and still keeps things
	 * relatively simple.
	 */
	bucket = hashval % dstate->dtds_hashsize;

	if (op == DTRACE_DYNVAR_DEALLOC) {
		volatile uintptr_t *lockp = &hash[bucket].dtdh_lock;

		for (;;) {
			while ((lock = *lockp) & 1)
				continue;

			if (dtrace_casptr((volatile void *)lockp,
			    (volatile void *)lock, (volatile void *)(lock + 1)) == (void *)lock)
				break;
		}

		dtrace_membar_producer();
	}

top:
	prev = NULL;
	lock = hash[bucket].dtdh_lock;

	dtrace_membar_consumer();

	start = hash[bucket].dtdh_chain;
	ASSERT(start != NULL && (start->dtdv_hashval == DTRACE_DYNHASH_SINK ||
	    start->dtdv_hashval != DTRACE_DYNHASH_FREE ||
	    op != DTRACE_DYNVAR_DEALLOC));

	for (dvar = start; dvar != NULL; dvar = dvar->dtdv_next) {
		dtrace_tuple_t *dtuple = &dvar->dtdv_tuple;
		dtrace_key_t *dkey = &dtuple->dtt_key[0];

		if (dvar->dtdv_hashval != hashval) {
			if (dvar->dtdv_hashval == DTRACE_DYNHASH_SINK) {
				/*
				 * We've reached the sink, and therefore the
				 * end of the hash chain; we can kick out of
				 * the loop knowing that we have seen a valid
				 * snapshot of state.
				 */
				ASSERT(dvar->dtdv_next == NULL);
				ASSERT(dvar == &dtrace_dynhash_sink);
				break;
			}

			if (dvar->dtdv_hashval == DTRACE_DYNHASH_FREE) {
				/*
				 * We've gone off the rails:  somewhere along
				 * the line, one of the members of this hash
				 * chain was deleted.  Note that we could also
				 * detect this by simply letting this loop run
				 * to completion, as we would eventually hit
				 * the end of the dirty list.  However, we
				 * want to avoid running the length of the
				 * dirty list unnecessarily (it might be quite
				 * long), so we catch this as early as
				 * possible by detecting the hash marker.  In
				 * this case, we simply set dvar to NULL and
				 * break; the conditional after the loop will
				 * send us back to top.
				 */
				dvar = NULL;
				break;
			}

			goto next;
		}

		if (dtuple->dtt_nkeys != nkeys)
			goto next;

		for (i = 0; i < nkeys; i++, dkey++) {
			if (dkey->dttk_size != key[i].dttk_size)
				goto next; /* size or type mismatch */

			if (dkey->dttk_size != 0) {
				if (dtrace_bcmp(
				    (void *)(uintptr_t)key[i].dttk_value,
				    (void *)(uintptr_t)dkey->dttk_value,
				    dkey->dttk_size))
					goto next;
			} else {
				if (dkey->dttk_value != key[i].dttk_value)
					goto next;
			}
		}

		if (op != DTRACE_DYNVAR_DEALLOC)
			return (dvar);

		ASSERT(dvar->dtdv_next == NULL ||
		    dvar->dtdv_next->dtdv_hashval != DTRACE_DYNHASH_FREE);

		if (prev != NULL) {
			ASSERT(hash[bucket].dtdh_chain != dvar);
			ASSERT(start != dvar);
			ASSERT(prev->dtdv_next == dvar);
			prev->dtdv_next = dvar->dtdv_next;
		} else {
			if (dtrace_casptr(&hash[bucket].dtdh_chain,
			    start, dvar->dtdv_next) != start) {
				/*
				 * We have failed to atomically swing the
				 * hash table head pointer, presumably because
				 * of a conflicting allocation on another CPU.
				 * We need to reread the hash chain and try
				 * again.
				 */
				goto top;
			}
		}

		dtrace_membar_producer();

		/*
		 * Now set the hash value to indicate that it's free.
		 */
		ASSERT(hash[bucket].dtdh_chain != dvar);
		dvar->dtdv_hashval = DTRACE_DYNHASH_FREE;

		dtrace_membar_producer();

		/*
		 * Set the next pointer to point at the dirty list, and
		 * atomically swing the dirty pointer to the newly freed dvar.
		 */
		do {
			next = dcpu->dtdsc_dirty;
			dvar->dtdv_next = next;
		} while (dtrace_casptr(&dcpu->dtdsc_dirty, next, dvar) != next);

		/*
		 * Finally, unlock this hash bucket.
		 */
		ASSERT(hash[bucket].dtdh_lock == lock);
		ASSERT(lock & 1);
		hash[bucket].dtdh_lock++;

		return (NULL);
next:
		prev = dvar;
		continue;
	}

	if (dvar == NULL) {
		/*
		 * If dvar is NULL, it is because we went off the rails:
		 * one of the elements that we traversed in the hash chain
		 * was deleted while we were traversing it.  In this case,
		 * we assert that we aren't doing a dealloc (deallocs lock
		 * the hash bucket to prevent themselves from racing with
		 * one another), and retry the hash chain traversal.
		 */
		ASSERT(op != DTRACE_DYNVAR_DEALLOC);
		goto top;
	}

	if (op != DTRACE_DYNVAR_ALLOC) {
		/*
		 * If we are not to allocate a new variable, we want to
		 * return NULL now.  Before we return, check that the value
		 * of the lock word hasn't changed.  If it has, we may have
		 * seen an inconsistent snapshot.
		 */
		if (op == DTRACE_DYNVAR_NOALLOC) {
			if (hash[bucket].dtdh_lock != lock)
				goto top;
		} else {
			ASSERT(op == DTRACE_DYNVAR_DEALLOC);
			ASSERT(hash[bucket].dtdh_lock == lock);
			ASSERT(lock & 1);
			hash[bucket].dtdh_lock++;
		}

		return (NULL);
	}

	/*
	 * We need to allocate a new dynamic variable.  The size we need is the
	 * size of dtrace_dynvar plus the size of nkeys dtrace_key_t's plus the
	 * size of any auxiliary key data (rounded up to 8-byte alignment) plus
	 * the size of any referred-to data (dsize).  We then round the final
	 * size up to the chunksize for allocation.
	 */
	for (ksize = 0, i = 0; i < nkeys; i++)
		ksize += P2ROUNDUP(key[i].dttk_size, sizeof (uint64_t));

	/*
	 * This should be pretty much impossible, but could happen if, say,
	 * strange DIF specified the tuple.  Ideally, this should be an
	 * assertion and not an error condition -- but that requires that the
	 * chunksize calculation in dtrace_difo_chunksize() be absolutely
	 * bullet-proof.  (That is, it must not be able to be fooled by
	 * malicious DIF.)  Given the lack of backwards branches in DIF,
	 * solving this would presumably not amount to solving the Halting
	 * Problem -- but it still seems awfully hard.
	 */
	if (sizeof (dtrace_dynvar_t) + sizeof (dtrace_key_t) * (nkeys - 1) +
	    ksize + dsize > chunksize) {
		dcpu->dtdsc_drops++;
		return (NULL);
	}

	nstate = DTRACE_DSTATE_EMPTY;

	do {
retry:
		free = dcpu->dtdsc_free;

		if (free == NULL) {
			dtrace_dynvar_t *clean = dcpu->dtdsc_clean;
			void *rval;

			if (clean == NULL) {
				/*
				 * We're out of dynamic variable space on
				 * this CPU.  Unless we have tried all CPUs,
				 * we'll try to allocate from a different
				 * CPU.
				 */
				switch (dstate->dtds_state) {
				case DTRACE_DSTATE_CLEAN: {
					void *sp = &dstate->dtds_state;

					if (++cpu >= NCPU)
						cpu = 0;

					if (dcpu->dtdsc_dirty != NULL &&
					    nstate == DTRACE_DSTATE_EMPTY)
						nstate = DTRACE_DSTATE_DIRTY;

					if (dcpu->dtdsc_rinsing != NULL)
						nstate = DTRACE_DSTATE_RINSING;

					dcpu = &dstate->dtds_percpu[cpu];

					if (cpu != me)
						goto retry;

					(void) dtrace_cas32(sp,
					    DTRACE_DSTATE_CLEAN, nstate);

					/*
					 * To increment the correct bean
					 * counter, take another lap.
					 */
					goto retry;
				}

				case DTRACE_DSTATE_DIRTY:
					dcpu->dtdsc_dirty_drops++;
					break;

				case DTRACE_DSTATE_RINSING:
					dcpu->dtdsc_rinsing_drops++;
					break;

				case DTRACE_DSTATE_EMPTY:
					dcpu->dtdsc_drops++;
					break;
				}

				DTRACE_CPUFLAG_SET(CPU_DTRACE_DROP);
				return (NULL);
			}

			/*
			 * The clean list appears to be non-empty.  We want to
			 * move the clean list to the free list; we start by
			 * moving the clean pointer aside.
			 */
			if (dtrace_casptr(&dcpu->dtdsc_clean,
			    clean, NULL) != clean) {
				/*
				 * We are in one of two situations:
				 *
				 *  (a)	The clean list was switched to the
				 *	free list by another CPU.
				 *
				 *  (b)	The clean list was added to by the
				 *	cleansing cyclic.
				 *
				 * In either of these situations, we can
				 * just reattempt the free list allocation.
				 */
				goto retry;
			}

			ASSERT(clean->dtdv_hashval == DTRACE_DYNHASH_FREE);

			/*
			 * Now we'll move the clean list to our free list.
			 * It's impossible for this to fail:  the only way
			 * the free list can be updated is through this
			 * code path, and only one CPU can own the clean list.
			 * Thus, it would only be possible for this to fail if
			 * this code were racing with dtrace_dynvar_clean().
			 * (That is, if dtrace_dynvar_clean() updated the clean
			 * list, and we ended up racing to update the free
			 * list.)  This race is prevented by the dtrace_sync()
			 * in dtrace_dynvar_clean() -- which flushes the
			 * owners of the clean lists out before resetting
			 * the clean lists.
			 */
			dcpu = &dstate->dtds_percpu[me];
			rval = dtrace_casptr(&dcpu->dtdsc_free, NULL, clean);
			ASSERT(rval == NULL);
			goto retry;
		}

		dvar = free;
		new_free = dvar->dtdv_next;
	} while (dtrace_casptr(&dcpu->dtdsc_free, free, new_free) != free);

	/*
	 * We have now allocated a new chunk.  We copy the tuple keys into the
	 * tuple array and copy any referenced key data into the data space
	 * following the tuple array.  As we do this, we relocate dttk_value
	 * in the final tuple to point to the key data address in the chunk.
	 */
	kdata = (uintptr_t)&dvar->dtdv_tuple.dtt_key[nkeys];
	dvar->dtdv_data = (void *)(kdata + ksize);
	dvar->dtdv_tuple.dtt_nkeys = nkeys;

	for (i = 0; i < nkeys; i++) {
		dtrace_key_t *dkey = &dvar->dtdv_tuple.dtt_key[i];
		size_t kesize = key[i].dttk_size;

		if (kesize != 0) {
			dtrace_bcopy(
			    (const void *)(uintptr_t)key[i].dttk_value,
			    (void *)kdata, kesize);
			dkey->dttk_value = kdata;
			kdata += P2ROUNDUP(kesize, sizeof (uint64_t));
		} else {
			dkey->dttk_value = key[i].dttk_value;
		}

		dkey->dttk_size = kesize;
	}

	ASSERT(dvar->dtdv_hashval == DTRACE_DYNHASH_FREE);
	dvar->dtdv_hashval = hashval;
	dvar->dtdv_next = start;

	if (dtrace_casptr(&hash[bucket].dtdh_chain, start, dvar) == start)
		return (dvar);

	/*
	 * The cas has failed.  Either another CPU is adding an element to
	 * this hash chain, or another CPU is deleting an element from this
	 * hash chain.  The simplest way to deal with both of these cases
	 * (though not necessarily the most efficient) is to free our
	 * allocated block and re-attempt it all.  Note that the free is
	 * to the dirty list and _not_ to the free list.  This is to prevent
	 * races with allocators, above.
	 */
	dvar->dtdv_hashval = DTRACE_DYNHASH_FREE;

	dtrace_membar_producer();

	do {
		free = dcpu->dtdsc_dirty;
		dvar->dtdv_next = free;
	} while (dtrace_casptr(&dcpu->dtdsc_dirty, free, dvar) != free);

	goto top;
}

/*ARGSUSED*/
static void
dtrace_aggregate_min(uint64_t *oval, uint64_t nval, uint64_t arg)
{
	if ((int64_t)nval < (int64_t)*oval)
		*oval = nval;
}

/*ARGSUSED*/
static void
dtrace_aggregate_max(uint64_t *oval, uint64_t nval, uint64_t arg)
{
	if ((int64_t)nval > (int64_t)*oval)
		*oval = nval;
}

static void
dtrace_aggregate_quantize(uint64_t *quanta, uint64_t nval, uint64_t incr)
{
	int i, zero = DTRACE_QUANTIZE_ZEROBUCKET;
	int64_t val = (int64_t)nval;

	if (val < 0) {
		for (i = 0; i < zero; i++) {
			if (val <= DTRACE_QUANTIZE_BUCKETVAL(i)) {
				quanta[i] += incr;
				return;
			}
		}
	} else {
		for (i = zero + 1; i < DTRACE_QUANTIZE_NBUCKETS; i++) {
			if (val < DTRACE_QUANTIZE_BUCKETVAL(i)) {
				quanta[i - 1] += incr;
				return;
			}
		}

		quanta[DTRACE_QUANTIZE_NBUCKETS - 1] += incr;
		return;
	}

	ASSERT(0);
}

static void
dtrace_aggregate_lquantize(uint64_t *lquanta, uint64_t nval, uint64_t incr)
{
	uint64_t arg = *lquanta++;
	int32_t base = DTRACE_LQUANTIZE_BASE(arg);
	uint16_t step = DTRACE_LQUANTIZE_STEP(arg);
	uint16_t levels = DTRACE_LQUANTIZE_LEVELS(arg);
	int32_t val = (int32_t)nval, level;

	ASSERT(step != 0);
	ASSERT(levels != 0);

	if (val < base) {
		/*
		 * This is an underflow.
		 */
		lquanta[0] += incr;
		return;
	}

	level = (val - base) / step;

	if (level < levels) {
		lquanta[level + 1] += incr;
		return;
	}

	/*
	 * This is an overflow.
	 */
	lquanta[levels + 1] += incr;
}

static int
dtrace_aggregate_llquantize_bucket(uint16_t factor, uint16_t low,
    uint16_t high, uint16_t nsteps, int64_t value)
{
	int64_t this = 1, last, next;
	int base = 1, order;

	ASSERT(factor <= nsteps);
	ASSERT(nsteps % factor == 0);

	for (order = 0; order < low; order++)
		this *= factor;

	/*
	 * If our value is less than our factor taken to the power of the
	 * low order of magnitude, it goes into the zeroth bucket.
	 */
	if (value < (last = this))
		return (0);

	for (this *= factor; order <= high; order++) {
		int nbuckets = this > nsteps ? nsteps : this;

		if ((next = this * factor) < this) {
			/*
			 * We should not generally get log/linear quantizations
			 * with a high magnitude that allows 64-bits to
			 * overflow, but we nonetheless protect against this
			 * by explicitly checking for overflow, and clamping
			 * our value accordingly.
			 */
			value = this - 1;
		}

		if (value < this) {
			/*
			 * If our value lies within this order of magnitude,
			 * determine its position by taking the offset within
			 * the order of magnitude, dividing by the bucket
			 * width, and adding to our (accumulated) base.
			 */
			return (base + (value - last) / (this / nbuckets));
		}

		base += nbuckets - (nbuckets / factor);
		last = this;
		this = next;
	}

	/*
	 * Our value is greater than or equal to our factor taken to the
	 * power of one plus the high magnitude -- return the top bucket.
	 */
	return (base);
}

static void
dtrace_aggregate_llquantize(uint64_t *llquanta, uint64_t nval, uint64_t incr)
{
	uint64_t arg = *llquanta++;
	uint16_t factor = DTRACE_LLQUANTIZE_FACTOR(arg);
	uint16_t low = DTRACE_LLQUANTIZE_LOW(arg);
	uint16_t high = DTRACE_LLQUANTIZE_HIGH(arg);
	uint16_t nsteps = DTRACE_LLQUANTIZE_NSTEP(arg);

	llquanta[dtrace_aggregate_llquantize_bucket(factor,
	    low, high, nsteps, nval)] += incr;
}

/*ARGSUSED*/
static void
dtrace_aggregate_avg(uint64_t *data, uint64_t nval, uint64_t arg)
{
	data[0]++;
	data[1] += nval;
}

/*ARGSUSED*/
static void
dtrace_aggregate_stddev(uint64_t *data, uint64_t nval, uint64_t arg)
{
	int64_t snval = (int64_t)nval;
	uint64_t tmp[2];

	data[0]++;
	data[1] += nval;

	/*
	 * What we want to say here is:
	 *
	 * data[2] += nval * nval;
	 *
	 * But given that nval is 64-bit, we could easily overflow, so
	 * we do this as 128-bit arithmetic.
	 */
	if (snval < 0)
		snval = -snval;

	dtrace_multiply_128((uint64_t)snval, (uint64_t)snval, tmp);
	dtrace_add_128(data + 2, tmp, data + 2);
}

/*ARGSUSED*/
static void
dtrace_aggregate_count(uint64_t *oval, uint64_t nval, uint64_t arg)
{
	*oval = *oval + 1;
}

/*ARGSUSED*/
static void
dtrace_aggregate_sum(uint64_t *oval, uint64_t nval, uint64_t arg)
{
	*oval += nval;
}

/*
 * Aggregate given the tuple in the principal data buffer, and the aggregating
 * action denoted by the specified dtrace_aggregation_t.  The aggregation
 * buffer is specified as the buf parameter.  This routine does not return
 * failure; if there is no space in the aggregation buffer, the data will be
 * dropped, and a corresponding counter incremented.
 */
static void
dtrace_aggregate(dtrace_aggregation_t *agg, dtrace_buffer_t *dbuf,
    intptr_t offset, dtrace_buffer_t *buf, uint64_t expr, uint64_t arg)
{
	dtrace_recdesc_t *rec = &agg->dtag_action.dta_rec;
	uint32_t i, ndx, size, fsize;
	uint32_t align = sizeof (uint64_t) - 1;
	dtrace_aggbuffer_t *agb;
	dtrace_aggkey_t *key;
	uint32_t hashval = 0, limit, isstr;
	caddr_t tomax, data, kdata;
	dtrace_actkind_t action;
	dtrace_action_t *act;
	uintptr_t offs;

	if (buf == NULL)
		return;

	if (!agg->dtag_hasarg) {
		/*
		 * Currently, only quantize() and lquantize() take additional
		 * arguments, and they have the same semantics:  an increment
		 * value that defaults to 1 when not present.  If additional
		 * aggregating actions take arguments, the setting of the
		 * default argument value will presumably have to become more
		 * sophisticated...
		 */
		arg = 1;
	}

	action = agg->dtag_action.dta_kind - DTRACEACT_AGGREGATION;
	size = rec->dtrd_offset - agg->dtag_base;
	fsize = size + rec->dtrd_size;

	ASSERT(dbuf->dtb_tomax != NULL);
	data = dbuf->dtb_tomax + offset + agg->dtag_base;

	if ((tomax = buf->dtb_tomax) == NULL) {
		dtrace_buffer_drop(buf);
		return;
	}

	/*
	 * The metastructure is always at the bottom of the buffer.
	 */
	agb = (dtrace_aggbuffer_t *)(tomax + buf->dtb_size -
	    sizeof (dtrace_aggbuffer_t));

	if (buf->dtb_offset == 0) {
		/*
		 * We just kludge up approximately 1/8th of the size to be
		 * buckets.  If this guess ends up being routinely
		 * off-the-mark, we may need to dynamically readjust this
		 * based on past performance.
		 */
		uintptr_t hashsize = (buf->dtb_size >> 3) / sizeof (uintptr_t);

		if ((uintptr_t)agb - hashsize * sizeof (dtrace_aggkey_t *) <
		    (uintptr_t)tomax || hashsize == 0) {
			/*
			 * We've been given a ludicrously small buffer;
			 * increment our drop count and leave.
			 */
			dtrace_buffer_drop(buf);
			return;
		}

		/*
		 * And now, a pathetic attempt to try to get a an odd (or
		 * perchance, a prime) hash size for better hash distribution.
		 */
		if (hashsize > (DTRACE_AGGHASHSIZE_SLEW << 3))
			hashsize -= DTRACE_AGGHASHSIZE_SLEW;

		agb->dtagb_hashsize = hashsize;
		agb->dtagb_hash = (dtrace_aggkey_t **)((uintptr_t)agb -
		    agb->dtagb_hashsize * sizeof (dtrace_aggkey_t *));
		agb->dtagb_free = (uintptr_t)agb->dtagb_hash;

		for (i = 0; i < agb->dtagb_hashsize; i++)
			agb->dtagb_hash[i] = NULL;
	}

	ASSERT(agg->dtag_first != NULL);
	ASSERT(agg->dtag_first->dta_intuple);

	/*
	 * Calculate the hash value based on the key.  Note that we _don't_
	 * include the aggid in the hashing (but we will store it as part of
	 * the key).  The hashing algorithm is Bob Jenkins' "One-at-a-time"
	 * algorithm: a simple, quick algorithm that has no known funnels, and
	 * gets good distribution in practice.  The efficacy of the hashing
	 * algorithm (and a comparison with other algorithms) may be found by
	 * running the ::dtrace_aggstat MDB dcmd.
	 */
	for (act = agg->dtag_first; act->dta_intuple; act = act->dta_next) {
		i = act->dta_rec.dtrd_offset - agg->dtag_base;
		limit = i + act->dta_rec.dtrd_size;
		ASSERT(limit <= size);
		isstr = DTRACEACT_ISSTRING(act);

		for (; i < limit; i++) {
			hashval += data[i];
			hashval += (hashval << 10);
			hashval ^= (hashval >> 6);

			if (isstr && data[i] == '\0')
				break;
		}
	}

	hashval += (hashval << 3);
	hashval ^= (hashval >> 11);
	hashval += (hashval << 15);

	/*
	 * Yes, the divide here is expensive -- but it's generally the least
	 * of the performance issues given the amount of data that we iterate
	 * over to compute hash values, compare data, etc.
	 */
	ndx = hashval % agb->dtagb_hashsize;

	for (key = agb->dtagb_hash[ndx]; key != NULL; key = key->dtak_next) {
		ASSERT((caddr_t)key >= tomax);
		ASSERT((caddr_t)key < tomax + buf->dtb_size);

		if (hashval != key->dtak_hashval || key->dtak_size != size)
			continue;

		kdata = key->dtak_data;
		ASSERT(kdata >= tomax && kdata < tomax + buf->dtb_size);

		for (act = agg->dtag_first; act->dta_intuple;
		    act = act->dta_next) {
			i = act->dta_rec.dtrd_offset - agg->dtag_base;
			limit = i + act->dta_rec.dtrd_size;
			ASSERT(limit <= size);
			isstr = DTRACEACT_ISSTRING(act);

			for (; i < limit; i++) {
				if (kdata[i] != data[i])
					goto next;

				if (isstr && data[i] == '\0')
					break;
			}
		}

		if (action != key->dtak_action) {
			/*
			 * We are aggregating on the same value in the same
			 * aggregation with two different aggregating actions.
			 * (This should have been picked up in the compiler,
			 * so we may be dealing with errant or devious DIF.)
			 * This is an error condition; we indicate as much,
			 * and return.
			 */
			DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
			return;
		}

		/*
		 * This is a hit:  we need to apply the aggregator to
		 * the value at this key.
		 */
		agg->dtag_aggregate((uint64_t *)(kdata + size), expr, arg);
		return;
next:
		continue;
	}

	/*
	 * We didn't find it.  We need to allocate some zero-filled space,
	 * link it into the hash table appropriately, and apply the aggregator
	 * to the (zero-filled) value.
	 */
	offs = buf->dtb_offset;
	while (offs & (align - 1))
		offs += sizeof (uint32_t);

	/*
	 * If we don't have enough room to both allocate a new key _and_
	 * its associated data, increment the drop count and return.
	 */
	if ((uintptr_t)tomax + offs + fsize >
	    agb->dtagb_free - sizeof (dtrace_aggkey_t)) {
		dtrace_buffer_drop(buf);
		return;
	}

	/*CONSTCOND*/
	ASSERT(!(sizeof (dtrace_aggkey_t) & (sizeof (uintptr_t) - 1)));
	key = (dtrace_aggkey_t *)(agb->dtagb_free - sizeof (dtrace_aggkey_t));
	agb->dtagb_free -= sizeof (dtrace_aggkey_t);

	key->dtak_data = kdata = tomax + offs;
	buf->dtb_offset = offs + fsize;

	/*
	 * Now copy the data across.
	 */
	*((dtrace_aggid_t *)kdata) = agg->dtag_id;

	for (i = sizeof (dtrace_aggid_t); i < size; i++)
		kdata[i] = data[i];

	/*
	 * Because strings are not zeroed out by default, we need to iterate
	 * looking for actions that store strings, and we need to explicitly
	 * pad these strings out with zeroes.
	 */
	for (act = agg->dtag_first; act->dta_intuple; act = act->dta_next) {
		int nul;

		if (!DTRACEACT_ISSTRING(act))
			continue;

		i = act->dta_rec.dtrd_offset - agg->dtag_base;
		limit = i + act->dta_rec.dtrd_size;
		ASSERT(limit <= size);

		for (nul = 0; i < limit; i++) {
			if (nul) {
				kdata[i] = '\0';
				continue;
			}

			if (data[i] != '\0')
				continue;

			nul = 1;
		}
	}

	for (i = size; i < fsize; i++)
		kdata[i] = 0;

	key->dtak_hashval = hashval;
	key->dtak_size = size;
	key->dtak_action = action;
	key->dtak_next = agb->dtagb_hash[ndx];
	agb->dtagb_hash[ndx] = key;

	/*
	 * Finally, apply the aggregator.
	 */
	*((uint64_t *)(key->dtak_data + size)) = agg->dtag_initial;
	agg->dtag_aggregate((uint64_t *)(key->dtak_data + size), expr, arg);
}

/*
 * Given consumer state, this routine finds a speculation in the INACTIVE
 * state and transitions it into the ACTIVE state.  If there is no speculation
 * in the INACTIVE state, 0 is returned.  In this case, no error counter is
 * incremented -- it is up to the caller to take appropriate action.
 */
static int
dtrace_speculation(dtrace_state_t *state)
{
	int i = 0;
	dtrace_speculation_state_t current;
	uint32_t *stat = &state->dts_speculations_unavail, count;

	while (i < state->dts_nspeculations) {
		dtrace_speculation_t *spec = &state->dts_speculations[i];

		current = spec->dtsp_state;

		if (current != DTRACESPEC_INACTIVE) {
			if (current == DTRACESPEC_COMMITTINGMANY ||
			    current == DTRACESPEC_COMMITTING ||
			    current == DTRACESPEC_DISCARDING)
				stat = &state->dts_speculations_busy;
			i++;
			continue;
		}

		if (dtrace_cas32((uint32_t *)&spec->dtsp_state,
		    current, DTRACESPEC_ACTIVE) == current)
			return (i + 1);
	}

	/*
	 * We couldn't find a speculation.  If we found as much as a single
	 * busy speculation buffer, we'll attribute this failure as "busy"
	 * instead of "unavail".
	 */
	do {
		count = *stat;
	} while (dtrace_cas32(stat, count, count + 1) != count);

	return (0);
}

/*
 * This routine commits an active speculation.  If the specified speculation
 * is not in a valid state to perform a commit(), this routine will silently do
 * nothing.  The state of the specified speculation is transitioned according
 * to the state transition diagram outlined in <sys/dtrace_impl.h>
 */
static void
dtrace_speculation_commit(dtrace_state_t *state, processorid_t cpu,
    dtrace_specid_t which)
{
	dtrace_speculation_t *spec;
	dtrace_buffer_t *src, *dest;
	uintptr_t daddr, saddr, dlimit, slimit;
	dtrace_speculation_state_t current, new = 0;
	intptr_t offs;
	uint64_t timestamp;

	if (which == 0)
		return;

	if (which > state->dts_nspeculations) {
		cpu_core[cpu].cpuc_dtrace_flags |= CPU_DTRACE_ILLOP;
		return;
	}

	spec = &state->dts_speculations[which - 1];
	src = &spec->dtsp_buffer[cpu];
	dest = &state->dts_buffer[cpu];

	do {
		current = spec->dtsp_state;

		if (current == DTRACESPEC_COMMITTINGMANY)
			break;

		switch (current) {
		case DTRACESPEC_INACTIVE:
		case DTRACESPEC_DISCARDING:
			return;

		case DTRACESPEC_COMMITTING:
			/*
			 * This is only possible if we are (a) commit()'ing
			 * without having done a prior speculate() on this CPU
			 * and (b) racing with another commit() on a different
			 * CPU.  There's nothing to do -- we just assert that
			 * our offset is 0.
			 */
			ASSERT(src->dtb_offset == 0);
			return;

		case DTRACESPEC_ACTIVE:
			new = DTRACESPEC_COMMITTING;
			break;

		case DTRACESPEC_ACTIVEONE:
			/*
			 * This speculation is active on one CPU.  If our
			 * buffer offset is non-zero, we know that the one CPU
			 * must be us.  Otherwise, we are committing on a
			 * different CPU from the speculate(), and we must
			 * rely on being asynchronously cleaned.
			 */
			if (src->dtb_offset != 0) {
				new = DTRACESPEC_COMMITTING;
				break;
			}
			/*FALLTHROUGH*/

		case DTRACESPEC_ACTIVEMANY:
			new = DTRACESPEC_COMMITTINGMANY;
			break;

		default:
			ASSERT(0);
		}
	} while (dtrace_cas32((uint32_t *)&spec->dtsp_state,
	    current, new) != current);

	/*
	 * We have set the state to indicate that we are committing this
	 * speculation.  Now reserve the necessary space in the destination
	 * buffer.
	 */
	if ((offs = dtrace_buffer_reserve(dest, src->dtb_offset,
	    sizeof (uint64_t), state, NULL)) < 0) {
		dtrace_buffer_drop(dest);
		goto out;
	}

	/*
	 * We have sufficient space to copy the speculative buffer into the
	 * primary buffer.  First, modify the speculative buffer, filling
	 * in the timestamp of all entries with the current time.  The data
	 * must have the commit() time rather than the time it was traced,
	 * so that all entries in the primary buffer are in timestamp order.
	 */
	timestamp = dtrace_gethrtime();
	saddr = (uintptr_t)src->dtb_tomax;
	slimit = saddr + src->dtb_offset;
	while (saddr < slimit) {
		size_t size;
		dtrace_rechdr_t *dtrh = (dtrace_rechdr_t *)saddr;

		if (dtrh->dtrh_epid == DTRACE_EPIDNONE) {
			saddr += sizeof (dtrace_epid_t);
			continue;
		}
		ASSERT3U(dtrh->dtrh_epid, <=, state->dts_necbs);
		size = state->dts_ecbs[dtrh->dtrh_epid - 1]->dte_size;

		ASSERT3U(saddr + size, <=, slimit);
		ASSERT3U(size, >=, sizeof (dtrace_rechdr_t));
		ASSERT3U(DTRACE_RECORD_LOAD_TIMESTAMP(dtrh), ==, UINT64_MAX);

		DTRACE_RECORD_STORE_TIMESTAMP(dtrh, timestamp);

		saddr += size;
	}

	/*
	 * Copy the buffer across.  (Note that this is a
	 * highly subobtimal bcopy(); in the unlikely event that this becomes
	 * a serious performance issue, a high-performance DTrace-specific
	 * bcopy() should obviously be invented.)
	 */
	daddr = (uintptr_t)dest->dtb_tomax + offs;
	dlimit = daddr + src->dtb_offset;
	saddr = (uintptr_t)src->dtb_tomax;

	/*
	 * First, the aligned portion.
	 */
	while (dlimit - daddr >= sizeof (uint64_t)) {
		*((uint64_t *)daddr) = *((uint64_t *)saddr);

		daddr += sizeof (uint64_t);
		saddr += sizeof (uint64_t);
	}

	/*
	 * Now any left-over bit...
	 */
	while (dlimit - daddr)
		*((uint8_t *)daddr++) = *((uint8_t *)saddr++);

	/*
	 * Finally, commit the reserved space in the destination buffer.
	 */
	dest->dtb_offset = offs + src->dtb_offset;

out:
	/*
	 * If we're lucky enough to be the only active CPU on this speculation
	 * buffer, we can just set the state back to DTRACESPEC_INACTIVE.
	 */
	if (current == DTRACESPEC_ACTIVE ||
	    (current == DTRACESPEC_ACTIVEONE && new == DTRACESPEC_COMMITTING)) {
		uint32_t rval = dtrace_cas32((uint32_t *)&spec->dtsp_state,
		    DTRACESPEC_COMMITTING, DTRACESPEC_INACTIVE);

		ASSERT(rval == DTRACESPEC_COMMITTING);
	}

	src->dtb_offset = 0;
	src->dtb_xamot_drops += src->dtb_drops;
	src->dtb_drops = 0;
}

/*
 * This routine discards an active speculation.  If the specified speculation
 * is not in a valid state to perform a discard(), this routine will silently
 * do nothing.  The state of the specified speculation is transitioned
 * according to the state transition diagram outlined in <sys/dtrace_impl.h>
 */
static void
dtrace_speculation_discard(dtrace_state_t *state, processorid_t cpu,
    dtrace_specid_t which)
{
	dtrace_speculation_t *spec;
	dtrace_speculation_state_t current, new = 0;
	dtrace_buffer_t *buf;

	if (which == 0)
		return;

	if (which > state->dts_nspeculations) {
		cpu_core[cpu].cpuc_dtrace_flags |= CPU_DTRACE_ILLOP;
		return;
	}

	spec = &state->dts_speculations[which - 1];
	buf = &spec->dtsp_buffer[cpu];

	do {
		current = spec->dtsp_state;

		switch (current) {
		case DTRACESPEC_INACTIVE:
		case DTRACESPEC_COMMITTINGMANY:
		case DTRACESPEC_COMMITTING:
		case DTRACESPEC_DISCARDING:
			return;

		case DTRACESPEC_ACTIVE:
		case DTRACESPEC_ACTIVEMANY:
			new = DTRACESPEC_DISCARDING;
			break;

		case DTRACESPEC_ACTIVEONE:
			if (buf->dtb_offset != 0) {
				new = DTRACESPEC_INACTIVE;
			} else {
				new = DTRACESPEC_DISCARDING;
			}
			break;

		default:
			ASSERT(0);
		}
	} while (dtrace_cas32((uint32_t *)&spec->dtsp_state,
	    current, new) != current);

	buf->dtb_offset = 0;
	buf->dtb_drops = 0;
}

/*
 * Note:  not called from probe context.  This function is called
 * asynchronously from cross call context to clean any speculations that are
 * in the COMMITTINGMANY or DISCARDING states.  These speculations may not be
 * transitioned back to the INACTIVE state until all CPUs have cleaned the
 * speculation.
 */
static void
dtrace_speculation_clean_here(dtrace_state_t *state)
{
	dtrace_icookie_t cookie;
	processorid_t cpu = curcpu;
	dtrace_buffer_t *dest = &state->dts_buffer[cpu];
	dtrace_specid_t i;

	cookie = dtrace_interrupt_disable();

	if (dest->dtb_tomax == NULL) {
		dtrace_interrupt_enable(cookie);
		return;
	}

	for (i = 0; i < state->dts_nspeculations; i++) {
		dtrace_speculation_t *spec = &state->dts_speculations[i];
		dtrace_buffer_t *src = &spec->dtsp_buffer[cpu];

		if (src->dtb_tomax == NULL)
			continue;

		if (spec->dtsp_state == DTRACESPEC_DISCARDING) {
			src->dtb_offset = 0;
			continue;
		}

		if (spec->dtsp_state != DTRACESPEC_COMMITTINGMANY)
			continue;

		if (src->dtb_offset == 0)
			continue;

		dtrace_speculation_commit(state, cpu, i + 1);
	}

	dtrace_interrupt_enable(cookie);
}

/*
 * Note:  not called from probe context.  This function is called
 * asynchronously (and at a regular interval) to clean any speculations that
 * are in the COMMITTINGMANY or DISCARDING states.  If it discovers that there
 * is work to be done, it cross calls all CPUs to perform that work;
 * COMMITMANY and DISCARDING speculations may not be transitioned back to the
 * INACTIVE state until they have been cleaned by all CPUs.
 */
static void
dtrace_speculation_clean(dtrace_state_t *state)
{
	int work = 0, rv;
	dtrace_specid_t i;

	for (i = 0; i < state->dts_nspeculations; i++) {
		dtrace_speculation_t *spec = &state->dts_speculations[i];

		ASSERT(!spec->dtsp_cleaning);

		if (spec->dtsp_state != DTRACESPEC_DISCARDING &&
		    spec->dtsp_state != DTRACESPEC_COMMITTINGMANY)
			continue;

		work++;
		spec->dtsp_cleaning = 1;
	}

	if (!work)
		return;

	dtrace_xcall(DTRACE_CPUALL,
	    (dtrace_xcall_t)dtrace_speculation_clean_here, state);

	/*
	 * We now know that all CPUs have committed or discarded their
	 * speculation buffers, as appropriate.  We can now set the state
	 * to inactive.
	 */
	for (i = 0; i < state->dts_nspeculations; i++) {
		dtrace_speculation_t *spec = &state->dts_speculations[i];
		dtrace_speculation_state_t current, new;

		if (!spec->dtsp_cleaning)
			continue;

		current = spec->dtsp_state;
		ASSERT(current == DTRACESPEC_DISCARDING ||
		    current == DTRACESPEC_COMMITTINGMANY);

		new = DTRACESPEC_INACTIVE;

		rv = dtrace_cas32((uint32_t *)&spec->dtsp_state, current, new);
		ASSERT(rv == current);
		spec->dtsp_cleaning = 0;
	}
}

/*
 * Called as part of a speculate() to get the speculative buffer associated
 * with a given speculation.  Returns NULL if the specified speculation is not
 * in an ACTIVE state.  If the speculation is in the ACTIVEONE state -- and
 * the active CPU is not the specified CPU -- the speculation will be
 * atomically transitioned into the ACTIVEMANY state.
 */
static dtrace_buffer_t *
dtrace_speculation_buffer(dtrace_state_t *state, processorid_t cpuid,
    dtrace_specid_t which)
{
	dtrace_speculation_t *spec;
	dtrace_speculation_state_t current, new = 0;
	dtrace_buffer_t *buf;

	if (which == 0)
		return (NULL);

	if (which > state->dts_nspeculations) {
		cpu_core[cpuid].cpuc_dtrace_flags |= CPU_DTRACE_ILLOP;
		return (NULL);
	}

	spec = &state->dts_speculations[which - 1];
	buf = &spec->dtsp_buffer[cpuid];

	do {
		current = spec->dtsp_state;

		switch (current) {
		case DTRACESPEC_INACTIVE:
		case DTRACESPEC_COMMITTINGMANY:
		case DTRACESPEC_DISCARDING:
			return (NULL);

		case DTRACESPEC_COMMITTING:
			ASSERT(buf->dtb_offset == 0);
			return (NULL);

		case DTRACESPEC_ACTIVEONE:
			/*
			 * This speculation is currently active on one CPU.
			 * Check the offset in the buffer; if it's non-zero,
			 * that CPU must be us (and we leave the state alone).
			 * If it's zero, assume that we're starting on a new
			 * CPU -- and change the state to indicate that the
			 * speculation is active on more than one CPU.
			 */
			if (buf->dtb_offset != 0)
				return (buf);

			new = DTRACESPEC_ACTIVEMANY;
			break;

		case DTRACESPEC_ACTIVEMANY:
			return (buf);

		case DTRACESPEC_ACTIVE:
			new = DTRACESPEC_ACTIVEONE;
			break;

		default:
			ASSERT(0);
		}
	} while (dtrace_cas32((uint32_t *)&spec->dtsp_state,
	    current, new) != current);

	ASSERT(new == DTRACESPEC_ACTIVEONE || new == DTRACESPEC_ACTIVEMANY);
	return (buf);
}

/*
 * Return a string.  In the event that the user lacks the privilege to access
 * arbitrary kernel memory, we copy the string out to scratch memory so that we
 * don't fail access checking.
 *
 * dtrace_dif_variable() uses this routine as a helper for various
 * builtin values such as 'execname' and 'probefunc.'
 */
uintptr_t
dtrace_dif_varstr(uintptr_t addr, dtrace_state_t *state,
    dtrace_mstate_t *mstate)
{
	uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];
	uintptr_t ret;
	size_t strsz;

	/*
	 * The easy case: this probe is allowed to read all of memory, so
	 * we can just return this as a vanilla pointer.
	 */
	if ((mstate->dtms_access & DTRACE_ACCESS_KERNEL) != 0)
		return (addr);

	/*
	 * This is the tougher case: we copy the string in question from
	 * kernel memory into scratch memory and return it that way: this
	 * ensures that we won't trip up when access checking tests the
	 * BYREF return value.
	 */
	strsz = dtrace_strlen((char *)addr, size) + 1;

	if (mstate->dtms_scratch_ptr + strsz >
	    mstate->dtms_scratch_base + mstate->dtms_scratch_size) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
		return (0);
	}

	dtrace_strcpy((const void *)addr, (void *)mstate->dtms_scratch_ptr,
	    strsz);
	ret = mstate->dtms_scratch_ptr;
	mstate->dtms_scratch_ptr += strsz;
	return (ret);
}

/*
 * Return a string from a memoy address which is known to have one or
 * more concatenated, individually zero terminated, sub-strings.
 * In the event that the user lacks the privilege to access
 * arbitrary kernel memory, we copy the string out to scratch memory so that we
 * don't fail access checking.
 *
 * dtrace_dif_variable() uses this routine as a helper for various
 * builtin values such as 'execargs'.
 */
static uintptr_t
dtrace_dif_varstrz(uintptr_t addr, size_t strsz, dtrace_state_t *state,
    dtrace_mstate_t *mstate)
{
	char *p;
	size_t i;
	uintptr_t ret;

	if (mstate->dtms_scratch_ptr + strsz >
	    mstate->dtms_scratch_base + mstate->dtms_scratch_size) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
		return (0);
	}

	dtrace_bcopy((const void *)addr, (void *)mstate->dtms_scratch_ptr,
	    strsz);

	/* Replace sub-string termination characters with a space. */
	for (p = (char *) mstate->dtms_scratch_ptr, i = 0; i < strsz - 1;
	    p++, i++)
		if (*p == '\0')
			*p = ' ';

	ret = mstate->dtms_scratch_ptr;
	mstate->dtms_scratch_ptr += strsz;
	return (ret);
}

/*
 * This function implements the DIF emulator's variable lookups.  The emulator
 * passes a reserved variable identifier and optional built-in array index.
 */
static uint64_t
dtrace_dif_variable(dtrace_mstate_t *mstate, dtrace_state_t *state, uint64_t v,
    uint64_t ndx)
{
	/*
	 * If we're accessing one of the uncached arguments, we'll turn this
	 * into a reference in the args array.
	 */
	if (v >= DIF_VAR_ARG0 && v <= DIF_VAR_ARG9) {
		ndx = v - DIF_VAR_ARG0;
		v = DIF_VAR_ARGS;
	}

	switch (v) {
	case DIF_VAR_ARGS:
		ASSERT(mstate->dtms_present & DTRACE_MSTATE_ARGS);
		if (ndx >= sizeof (mstate->dtms_arg) /
		    sizeof (mstate->dtms_arg[0])) {
			int aframes = mstate->dtms_probe->dtpr_aframes + 2;
			dtrace_provider_t *pv;
			uint64_t val;

			pv = mstate->dtms_probe->dtpr_provider;
			if (pv->dtpv_pops.dtps_getargval != NULL)
				val = pv->dtpv_pops.dtps_getargval(pv->dtpv_arg,
				    mstate->dtms_probe->dtpr_id,
				    mstate->dtms_probe->dtpr_arg, ndx, aframes);
			else
				val = dtrace_getarg(ndx, aframes);

			/*
			 * This is regrettably required to keep the compiler
			 * from tail-optimizing the call to dtrace_getarg().
			 * The condition always evaluates to true, but the
			 * compiler has no way of figuring that out a priori.
			 * (None of this would be necessary if the compiler
			 * could be relied upon to _always_ tail-optimize
			 * the call to dtrace_getarg() -- but it can't.)
			 */
			if (mstate->dtms_probe != NULL)
				return (val);

			ASSERT(0);
		}

		return (mstate->dtms_arg[ndx]);

#ifdef illumos
	case DIF_VAR_UREGS: {
		klwp_t *lwp;

		if (!dtrace_priv_proc(state))
			return (0);

		if ((lwp = curthread->t_lwp) == NULL) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
			cpu_core[curcpu].cpuc_dtrace_illval = NULL;
			return (0);
		}

		return (dtrace_getreg(lwp->lwp_regs, ndx));
		return (0);
	}
#else
	case DIF_VAR_UREGS: {
		struct trapframe *tframe;

		if (!dtrace_priv_proc(state))
			return (0);

		if ((tframe = curthread->td_frame) == NULL) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
			cpu_core[curcpu].cpuc_dtrace_illval = 0;
			return (0);
		}

		return (dtrace_getreg(tframe, ndx));
	}
#endif

	case DIF_VAR_CURTHREAD:
		if (!dtrace_priv_proc(state))
			return (0);
		return ((uint64_t)(uintptr_t)curthread);

	case DIF_VAR_TIMESTAMP:
		if (!(mstate->dtms_present & DTRACE_MSTATE_TIMESTAMP)) {
			mstate->dtms_timestamp = dtrace_gethrtime();
			mstate->dtms_present |= DTRACE_MSTATE_TIMESTAMP;
		}
		return (mstate->dtms_timestamp);

	case DIF_VAR_VTIMESTAMP:
		ASSERT(dtrace_vtime_references != 0);
		return (curthread->t_dtrace_vtime);

	case DIF_VAR_WALLTIMESTAMP:
		if (!(mstate->dtms_present & DTRACE_MSTATE_WALLTIMESTAMP)) {
			mstate->dtms_walltimestamp = dtrace_gethrestime();
			mstate->dtms_present |= DTRACE_MSTATE_WALLTIMESTAMP;
		}
		return (mstate->dtms_walltimestamp);

#ifdef illumos
	case DIF_VAR_IPL:
		if (!dtrace_priv_kernel(state))
			return (0);
		if (!(mstate->dtms_present & DTRACE_MSTATE_IPL)) {
			mstate->dtms_ipl = dtrace_getipl();
			mstate->dtms_present |= DTRACE_MSTATE_IPL;
		}
		return (mstate->dtms_ipl);
#endif

	case DIF_VAR_EPID:
		ASSERT(mstate->dtms_present & DTRACE_MSTATE_EPID);
		return (mstate->dtms_epid);

	case DIF_VAR_ID:
		ASSERT(mstate->dtms_present & DTRACE_MSTATE_PROBE);
		return (mstate->dtms_probe->dtpr_id);

	case DIF_VAR_STACKDEPTH:
		if (!dtrace_priv_kernel(state))
			return (0);
		if (!(mstate->dtms_present & DTRACE_MSTATE_STACKDEPTH)) {
			int aframes = mstate->dtms_probe->dtpr_aframes + 2;

			mstate->dtms_stackdepth = dtrace_getstackdepth(aframes);
			mstate->dtms_present |= DTRACE_MSTATE_STACKDEPTH;
		}
		return (mstate->dtms_stackdepth);

	case DIF_VAR_USTACKDEPTH:
		if (!dtrace_priv_proc(state))
			return (0);
		if (!(mstate->dtms_present & DTRACE_MSTATE_USTACKDEPTH)) {
			/*
			 * See comment in DIF_VAR_PID.
			 */
			if (DTRACE_ANCHORED(mstate->dtms_probe) &&
			    CPU_ON_INTR(CPU)) {
				mstate->dtms_ustackdepth = 0;
			} else {
				DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
				mstate->dtms_ustackdepth =
				    dtrace_getustackdepth();
				DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
			}
			mstate->dtms_present |= DTRACE_MSTATE_USTACKDEPTH;
		}
		return (mstate->dtms_ustackdepth);

	case DIF_VAR_CALLER:
		if (!dtrace_priv_kernel(state))
			return (0);
		if (!(mstate->dtms_present & DTRACE_MSTATE_CALLER)) {
			int aframes = mstate->dtms_probe->dtpr_aframes + 2;

			if (!DTRACE_ANCHORED(mstate->dtms_probe)) {
				/*
				 * If this is an unanchored probe, we are
				 * required to go through the slow path:
				 * dtrace_caller() only guarantees correct
				 * results for anchored probes.
				 */
				pc_t caller[2] = {0, 0};

				dtrace_getpcstack(caller, 2, aframes,
				    (uint32_t *)(uintptr_t)mstate->dtms_arg[0]);
				mstate->dtms_caller = caller[1];
			} else if ((mstate->dtms_caller =
			    dtrace_caller(aframes)) == -1) {
				/*
				 * We have failed to do this the quick way;
				 * we must resort to the slower approach of
				 * calling dtrace_getpcstack().
				 */
				pc_t caller = 0;

				dtrace_getpcstack(&caller, 1, aframes, NULL);
				mstate->dtms_caller = caller;
			}

			mstate->dtms_present |= DTRACE_MSTATE_CALLER;
		}
		return (mstate->dtms_caller);

	case DIF_VAR_UCALLER:
		if (!dtrace_priv_proc(state))
			return (0);

		if (!(mstate->dtms_present & DTRACE_MSTATE_UCALLER)) {
			uint64_t ustack[3];

			/*
			 * dtrace_getupcstack() fills in the first uint64_t
			 * with the current PID.  The second uint64_t will
			 * be the program counter at user-level.  The third
			 * uint64_t will contain the caller, which is what
			 * we're after.
			 */
			ustack[2] = 0;
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
			dtrace_getupcstack(ustack, 3);
			DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
			mstate->dtms_ucaller = ustack[2];
			mstate->dtms_present |= DTRACE_MSTATE_UCALLER;
		}

		return (mstate->dtms_ucaller);

	case DIF_VAR_PROBEPROV:
		ASSERT(mstate->dtms_present & DTRACE_MSTATE_PROBE);
		return (dtrace_dif_varstr(
		    (uintptr_t)mstate->dtms_probe->dtpr_provider->dtpv_name,
		    state, mstate));

	case DIF_VAR_PROBEMOD:
		ASSERT(mstate->dtms_present & DTRACE_MSTATE_PROBE);
		return (dtrace_dif_varstr(
		    (uintptr_t)mstate->dtms_probe->dtpr_mod,
		    state, mstate));

	case DIF_VAR_PROBEFUNC:
		ASSERT(mstate->dtms_present & DTRACE_MSTATE_PROBE);
		return (dtrace_dif_varstr(
		    (uintptr_t)mstate->dtms_probe->dtpr_func,
		    state, mstate));

	case DIF_VAR_PROBENAME:
		ASSERT(mstate->dtms_present & DTRACE_MSTATE_PROBE);
		return (dtrace_dif_varstr(
		    (uintptr_t)mstate->dtms_probe->dtpr_name,
		    state, mstate));

	case DIF_VAR_PID:
		if (!dtrace_priv_proc(state))
			return (0);

#ifdef illumos
		/*
		 * Note that we are assuming that an unanchored probe is
		 * always due to a high-level interrupt.  (And we're assuming
		 * that there is only a single high level interrupt.)
		 */
		if (DTRACE_ANCHORED(mstate->dtms_probe) && CPU_ON_INTR(CPU))
			return (pid0.pid_id);

		/*
		 * It is always safe to dereference one's own t_procp pointer:
		 * it always points to a valid, allocated proc structure.
		 * Further, it is always safe to dereference the p_pidp member
		 * of one's own proc structure.  (These are truisms becuase
		 * threads and processes don't clean up their own state --
		 * they leave that task to whomever reaps them.)
		 */
		return ((uint64_t)curthread->t_procp->p_pidp->pid_id);
#else
		return ((uint64_t)curproc->p_pid);
#endif

	case DIF_VAR_PPID:
		if (!dtrace_priv_proc(state))
			return (0);

#ifdef illumos
		/*
		 * See comment in DIF_VAR_PID.
		 */
		if (DTRACE_ANCHORED(mstate->dtms_probe) && CPU_ON_INTR(CPU))
			return (pid0.pid_id);

		/*
		 * It is always safe to dereference one's own t_procp pointer:
		 * it always points to a valid, allocated proc structure.
		 * (This is true because threads don't clean up their own
		 * state -- they leave that task to whomever reaps them.)
		 */
		return ((uint64_t)curthread->t_procp->p_ppid);
#else
		if (curproc->p_pid == proc0.p_pid)
			return (curproc->p_pid);
		else
			return (curproc->p_pptr->p_pid);
#endif

	case DIF_VAR_TID:
#ifdef illumos
		/*
		 * See comment in DIF_VAR_PID.
		 */
		if (DTRACE_ANCHORED(mstate->dtms_probe) && CPU_ON_INTR(CPU))
			return (0);
#endif

		return ((uint64_t)curthread->t_tid);

	case DIF_VAR_EXECARGS: {
		struct pargs *p_args = curthread->td_proc->p_args;

		if (p_args == NULL)
			return(0);

		return (dtrace_dif_varstrz(
		    (uintptr_t) p_args->ar_args, p_args->ar_length, state, mstate));
	}

	case DIF_VAR_EXECNAME:
#ifdef illumos
		if (!dtrace_priv_proc(state))
			return (0);

		/*
		 * See comment in DIF_VAR_PID.
		 */
		if (DTRACE_ANCHORED(mstate->dtms_probe) && CPU_ON_INTR(CPU))
			return ((uint64_t)(uintptr_t)p0.p_user.u_comm);

		/*
		 * It is always safe to dereference one's own t_procp pointer:
		 * it always points to a valid, allocated proc structure.
		 * (This is true because threads don't clean up their own
		 * state -- they leave that task to whomever reaps them.)
		 */
		return (dtrace_dif_varstr(
		    (uintptr_t)curthread->t_procp->p_user.u_comm,
		    state, mstate));
#else
		return (dtrace_dif_varstr(
		    (uintptr_t) curthread->td_proc->p_comm, state, mstate));
#endif

	case DIF_VAR_ZONENAME:
#ifdef illumos
		if (!dtrace_priv_proc(state))
			return (0);

		/*
		 * See comment in DIF_VAR_PID.
		 */
		if (DTRACE_ANCHORED(mstate->dtms_probe) && CPU_ON_INTR(CPU))
			return ((uint64_t)(uintptr_t)p0.p_zone->zone_name);

		/*
		 * It is always safe to dereference one's own t_procp pointer:
		 * it always points to a valid, allocated proc structure.
		 * (This is true because threads don't clean up their own
		 * state -- they leave that task to whomever reaps them.)
		 */
		return (dtrace_dif_varstr(
		    (uintptr_t)curthread->t_procp->p_zone->zone_name,
		    state, mstate));
#elif defined(__FreeBSD__)
	/*
	 * On FreeBSD, we introduce compatibility to zonename by falling through
	 * into jailname.
	 */
	case DIF_VAR_JAILNAME:
		if (!dtrace_priv_kernel(state))
			return (0);

		return (dtrace_dif_varstr(
		    (uintptr_t)curthread->td_ucred->cr_prison->pr_name,
		    state, mstate));

	case DIF_VAR_JID:
		if (!dtrace_priv_kernel(state))
			return (0);

		return ((uint64_t)curthread->td_ucred->cr_prison->pr_id);
#else
		return (0);
#endif

	case DIF_VAR_UID:
		if (!dtrace_priv_proc(state))
			return (0);

#ifdef illumos
		/*
		 * See comment in DIF_VAR_PID.
		 */
		if (DTRACE_ANCHORED(mstate->dtms_probe) && CPU_ON_INTR(CPU))
			return ((uint64_t)p0.p_cred->cr_uid);

		/*
		 * It is always safe to dereference one's own t_procp pointer:
		 * it always points to a valid, allocated proc structure.
		 * (This is true because threads don't clean up their own
		 * state -- they leave that task to whomever reaps them.)
		 *
		 * Additionally, it is safe to dereference one's own process
		 * credential, since this is never NULL after process birth.
		 */
		return ((uint64_t)curthread->t_procp->p_cred->cr_uid);
#else
		return ((uint64_t)curthread->td_ucred->cr_uid);
#endif

	case DIF_VAR_GID:
		if (!dtrace_priv_proc(state))
			return (0);

#ifdef illumos
		/*
		 * See comment in DIF_VAR_PID.
		 */
		if (DTRACE_ANCHORED(mstate->dtms_probe) && CPU_ON_INTR(CPU))
			return ((uint64_t)p0.p_cred->cr_gid);

		/*
		 * It is always safe to dereference one's own t_procp pointer:
		 * it always points to a valid, allocated proc structure.
		 * (This is true because threads don't clean up their own
		 * state -- they leave that task to whomever reaps them.)
		 *
		 * Additionally, it is safe to dereference one's own process
		 * credential, since this is never NULL after process birth.
		 */
		return ((uint64_t)curthread->t_procp->p_cred->cr_gid);
#else
		return ((uint64_t)curthread->td_ucred->cr_gid);
#endif

	case DIF_VAR_ERRNO: {
#ifdef illumos
		klwp_t *lwp;
		if (!dtrace_priv_proc(state))
			return (0);

		/*
		 * See comment in DIF_VAR_PID.
		 */
		if (DTRACE_ANCHORED(mstate->dtms_probe) && CPU_ON_INTR(CPU))
			return (0);

		/*
		 * It is always safe to dereference one's own t_lwp pointer in
		 * the event that this pointer is non-NULL.  (This is true
		 * because threads and lwps don't clean up their own state --
		 * they leave that task to whomever reaps them.)
		 */
		if ((lwp = curthread->t_lwp) == NULL)
			return (0);

		return ((uint64_t)lwp->lwp_errno);
#else
		return (curthread->td_errno);
#endif
	}
#ifndef illumos
	case DIF_VAR_CPU: {
		return curcpu;
	}
#endif
	default:
		DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
		return (0);
	}
}


typedef enum dtrace_json_state {
	DTRACE_JSON_REST = 1,
	DTRACE_JSON_OBJECT,
	DTRACE_JSON_STRING,
	DTRACE_JSON_STRING_ESCAPE,
	DTRACE_JSON_STRING_ESCAPE_UNICODE,
	DTRACE_JSON_COLON,
	DTRACE_JSON_COMMA,
	DTRACE_JSON_VALUE,
	DTRACE_JSON_IDENTIFIER,
	DTRACE_JSON_NUMBER,
	DTRACE_JSON_NUMBER_FRAC,
	DTRACE_JSON_NUMBER_EXP,
	DTRACE_JSON_COLLECT_OBJECT
} dtrace_json_state_t;

/*
 * This function possesses just enough knowledge about JSON to extract a single
 * value from a JSON string and store it in the scratch buffer.  It is able
 * to extract nested object values, and members of arrays by index.
 *
 * elemlist is a list of JSON keys, stored as packed NUL-terminated strings, to
 * be looked up as we descend into the object tree.  e.g.
 *
 *    foo[0].bar.baz[32] --> "foo" NUL "0" NUL "bar" NUL "baz" NUL "32" NUL
 *       with nelems = 5.
 *
 * The run time of this function must be bounded above by strsize to limit the
 * amount of work done in probe context.  As such, it is implemented as a
 * simple state machine, reading one character at a time using safe loads
 * until we find the requested element, hit a parsing error or run off the
 * end of the object or string.
 *
 * As there is no way for a subroutine to return an error without interrupting
 * clause execution, we simply return NULL in the event of a missing key or any
 * other error condition.  Each NULL return in this function is commented with
 * the error condition it represents -- parsing or otherwise.
 *
 * The set of states for the state machine closely matches the JSON
 * specification (http://json.org/).  Briefly:
 *
 *   DTRACE_JSON_REST:
 *     Skip whitespace until we find either a top-level Object, moving
 *     to DTRACE_JSON_OBJECT; or an Array, moving to DTRACE_JSON_VALUE.
 *
 *   DTRACE_JSON_OBJECT:
 *     Locate the next key String in an Object.  Sets a flag to denote
 *     the next String as a key string and moves to DTRACE_JSON_STRING.
 *
 *   DTRACE_JSON_COLON:
 *     Skip whitespace until we find the colon that separates key Strings
 *     from their values.  Once found, move to DTRACE_JSON_VALUE.
 *
 *   DTRACE_JSON_VALUE:
 *     Detects the type of the next value (String, Number, Identifier, Object
 *     or Array) and routes to the states that process that type.  Here we also
 *     deal with the element selector list if we are requested to traverse down
 *     into the object tree.
 *
 *   DTRACE_JSON_COMMA:
 *     Skip whitespace until we find the comma that separates key-value pairs
 *     in Objects (returning to DTRACE_JSON_OBJECT) or values in Arrays
 *     (similarly DTRACE_JSON_VALUE).  All following literal value processing
 *     states return to this state at the end of their value, unless otherwise
 *     noted.
 *
 *   DTRACE_JSON_NUMBER, DTRACE_JSON_NUMBER_FRAC, DTRACE_JSON_NUMBER_EXP:
 *     Processes a Number literal from the JSON, including any exponent
 *     component that may be present.  Numbers are returned as strings, which
 *     may be passed to strtoll() if an integer is required.
 *
 *   DTRACE_JSON_IDENTIFIER:
 *     Processes a "true", "false" or "null" literal in the JSON.
 *
 *   DTRACE_JSON_STRING, DTRACE_JSON_STRING_ESCAPE,
 *   DTRACE_JSON_STRING_ESCAPE_UNICODE:
 *     Processes a String literal from the JSON, whether the String denotes
 *     a key, a value or part of a larger Object.  Handles all escape sequences
 *     present in the specification, including four-digit unicode characters,
 *     but merely includes the escape sequence without converting it to the
 *     actual escaped character.  If the String is flagged as a key, we
 *     move to DTRACE_JSON_COLON rather than DTRACE_JSON_COMMA.
 *
 *   DTRACE_JSON_COLLECT_OBJECT:
 *     This state collects an entire Object (or Array), correctly handling
 *     embedded strings.  If the full element selector list matches this nested
 *     object, we return the Object in full as a string.  If not, we use this
 *     state to skip to the next value at this level and continue processing.
 *
 * NOTE: This function uses various macros from strtolctype.h to manipulate
 * digit values, etc -- these have all been checked to ensure they make
 * no additional function calls.
 */
static char *
dtrace_json(uint64_t size, uintptr_t json, char *elemlist, int nelems,
    char *dest)
{
	dtrace_json_state_t state = DTRACE_JSON_REST;
	int64_t array_elem = INT64_MIN;
	int64_t array_pos = 0;
	uint8_t escape_unicount = 0;
	boolean_t string_is_key = B_FALSE;
	boolean_t collect_object = B_FALSE;
	boolean_t found_key = B_FALSE;
	boolean_t in_array = B_FALSE;
	uint32_t braces = 0, brackets = 0;
	char *elem = elemlist;
	char *dd = dest;
	uintptr_t cur;

	for (cur = json; cur < json + size; cur++) {
		char cc = dtrace_load8(cur);
		if (cc == '\0')
			return (NULL);

		switch (state) {
		case DTRACE_JSON_REST:
			if (isspace(cc))
				break;

			if (cc == '{') {
				state = DTRACE_JSON_OBJECT;
				break;
			}

			if (cc == '[') {
				in_array = B_TRUE;
				array_pos = 0;
				array_elem = dtrace_strtoll(elem, 10, size);
				found_key = array_elem == 0 ? B_TRUE : B_FALSE;
				state = DTRACE_JSON_VALUE;
				break;
			}

			/*
			 * ERROR: expected to find a top-level object or array.
			 */
			return (NULL);
		case DTRACE_JSON_OBJECT:
			if (isspace(cc))
				break;

			if (cc == '"') {
				state = DTRACE_JSON_STRING;
				string_is_key = B_TRUE;
				break;
			}

			/*
			 * ERROR: either the object did not start with a key
			 * string, or we've run off the end of the object
			 * without finding the requested key.
			 */
			return (NULL);
		case DTRACE_JSON_STRING:
			if (cc == '\\') {
				*dd++ = '\\';
				state = DTRACE_JSON_STRING_ESCAPE;
				break;
			}

			if (cc == '"') {
				if (collect_object) {
					/*
					 * We don't reset the dest here, as
					 * the string is part of a larger
					 * object being collected.
					 */
					*dd++ = cc;
					collect_object = B_FALSE;
					state = DTRACE_JSON_COLLECT_OBJECT;
					break;
				}
				*dd = '\0';
				dd = dest; /* reset string buffer */
				if (string_is_key) {
					if (dtrace_strncmp(dest, elem,
					    size) == 0)
						found_key = B_TRUE;
				} else if (found_key) {
					if (nelems > 1) {
						/*
						 * We expected an object, not
						 * this string.
						 */
						return (NULL);
					}
					return (dest);
				}
				state = string_is_key ? DTRACE_JSON_COLON :
				    DTRACE_JSON_COMMA;
				string_is_key = B_FALSE;
				break;
			}

			*dd++ = cc;
			break;
		case DTRACE_JSON_STRING_ESCAPE:
			*dd++ = cc;
			if (cc == 'u') {
				escape_unicount = 0;
				state = DTRACE_JSON_STRING_ESCAPE_UNICODE;
			} else {
				state = DTRACE_JSON_STRING;
			}
			break;
		case DTRACE_JSON_STRING_ESCAPE_UNICODE:
			if (!isxdigit(cc)) {
				/*
				 * ERROR: invalid unicode escape, expected
				 * four valid hexidecimal digits.
				 */
				return (NULL);
			}

			*dd++ = cc;
			if (++escape_unicount == 4)
				state = DTRACE_JSON_STRING;
			break;
		case DTRACE_JSON_COLON:
			if (isspace(cc))
				break;

			if (cc == ':') {
				state = DTRACE_JSON_VALUE;
				break;
			}

			/*
			 * ERROR: expected a colon.
			 */
			return (NULL);
		case DTRACE_JSON_COMMA:
			if (isspace(cc))
				break;

			if (cc == ',') {
				if (in_array) {
					state = DTRACE_JSON_VALUE;
					if (++array_pos == array_elem)
						found_key = B_TRUE;
				} else {
					state = DTRACE_JSON_OBJECT;
				}
				break;
			}

			/*
			 * ERROR: either we hit an unexpected character, or
			 * we reached the end of the object or array without
			 * finding the requested key.
			 */
			return (NULL);
		case DTRACE_JSON_IDENTIFIER:
			if (islower(cc)) {
				*dd++ = cc;
				break;
			}

			*dd = '\0';
			dd = dest; /* reset string buffer */

			if (dtrace_strncmp(dest, "true", 5) == 0 ||
			    dtrace_strncmp(dest, "false", 6) == 0 ||
			    dtrace_strncmp(dest, "null", 5) == 0) {
				if (found_key) {
					if (nelems > 1) {
						/*
						 * ERROR: We expected an object,
						 * not this identifier.
						 */
						return (NULL);
					}
					return (dest);
				} else {
					cur--;
					state = DTRACE_JSON_COMMA;
					break;
				}
			}

			/*
			 * ERROR: we did not recognise the identifier as one
			 * of those in the JSON specification.
			 */
			return (NULL);
		case DTRACE_JSON_NUMBER:
			if (cc == '.') {
				*dd++ = cc;
				state = DTRACE_JSON_NUMBER_FRAC;
				break;
			}

			if (cc == 'x' || cc == 'X') {
				/*
				 * ERROR: specification explicitly excludes
				 * hexidecimal or octal numbers.
				 */
				return (NULL);
			}

			/* FALLTHRU */
		case DTRACE_JSON_NUMBER_FRAC:
			if (cc == 'e' || cc == 'E') {
				*dd++ = cc;
				state = DTRACE_JSON_NUMBER_EXP;
				break;
			}

			if (cc == '+' || cc == '-') {
				/*
				 * ERROR: expect sign as part of exponent only.
				 */
				return (NULL);
			}
			/* FALLTHRU */
		case DTRACE_JSON_NUMBER_EXP:
			if (isdigit(cc) || cc == '+' || cc == '-') {
				*dd++ = cc;
				break;
			}

			*dd = '\0';
			dd = dest; /* reset string buffer */
			if (found_key) {
				if (nelems > 1) {
					/*
					 * ERROR: We expected an object, not
					 * this number.
					 */
					return (NULL);
				}
				return (dest);
			}

			cur--;
			state = DTRACE_JSON_COMMA;
			break;
		case DTRACE_JSON_VALUE:
			if (isspace(cc))
				break;

			if (cc == '{' || cc == '[') {
				if (nelems > 1 && found_key) {
					in_array = cc == '[' ? B_TRUE : B_FALSE;
					/*
					 * If our element selector directs us
					 * to descend into this nested object,
					 * then move to the next selector
					 * element in the list and restart the
					 * state machine.
					 */
					while (*elem != '\0')
						elem++;
					elem++; /* skip the inter-element NUL */
					nelems--;
					dd = dest;
					if (in_array) {
						state = DTRACE_JSON_VALUE;
						array_pos = 0;
						array_elem = dtrace_strtoll(
						    elem, 10, size);
						found_key = array_elem == 0 ?
						    B_TRUE : B_FALSE;
					} else {
						found_key = B_FALSE;
						state = DTRACE_JSON_OBJECT;
					}
					break;
				}

				/*
				 * Otherwise, we wish to either skip this
				 * nested object or return it in full.
				 */
				if (cc == '[')
					brackets = 1;
				else
					braces = 1;
				*dd++ = cc;
				state = DTRACE_JSON_COLLECT_OBJECT;
				break;
			}

			if (cc == '"') {
				state = DTRACE_JSON_STRING;
				break;
			}

			if (islower(cc)) {
				/*
				 * Here we deal with true, false and null.
				 */
				*dd++ = cc;
				state = DTRACE_JSON_IDENTIFIER;
				break;
			}

			if (cc == '-' || isdigit(cc)) {
				*dd++ = cc;
				state = DTRACE_JSON_NUMBER;
				break;
			}

			/*
			 * ERROR: unexpected character at start of value.
			 */
			return (NULL);
		case DTRACE_JSON_COLLECT_OBJECT:
			if (cc == '\0')
				/*
				 * ERROR: unexpected end of input.
				 */
				return (NULL);

			*dd++ = cc;
			if (cc == '"') {
				collect_object = B_TRUE;
				state = DTRACE_JSON_STRING;
				break;
			}

			if (cc == ']') {
				if (brackets-- == 0) {
					/*
					 * ERROR: unbalanced brackets.
					 */
					return (NULL);
				}
			} else if (cc == '}') {
				if (braces-- == 0) {
					/*
					 * ERROR: unbalanced braces.
					 */
					return (NULL);
				}
			} else if (cc == '{') {
				braces++;
			} else if (cc == '[') {
				brackets++;
			}

			if (brackets == 0 && braces == 0) {
				if (found_key) {
					*dd = '\0';
					return (dest);
				}
				dd = dest; /* reset string buffer */
				state = DTRACE_JSON_COMMA;
			}
			break;
		}
	}
	return (NULL);
}

/*
 * Emulate the execution of DTrace ID subroutines invoked by the call opcode.
 * Notice that we don't bother validating the proper number of arguments or
 * their types in the tuple stack.  This isn't needed because all argument
 * interpretation is safe because of our load safety -- the worst that can
 * happen is that a bogus program can obtain bogus results.
 */
static void
dtrace_dif_subr(uint_t subr, uint_t rd, uint64_t *regs,
    dtrace_key_t *tupregs, int nargs,
    dtrace_mstate_t *mstate, dtrace_state_t *state)
{
	volatile uint16_t *flags = &cpu_core[curcpu].cpuc_dtrace_flags;
	volatile uintptr_t *illval = &cpu_core[curcpu].cpuc_dtrace_illval;
	dtrace_vstate_t *vstate = &state->dts_vstate;

#ifdef illumos
	union {
		mutex_impl_t mi;
		uint64_t mx;
	} m;

	union {
		krwlock_t ri;
		uintptr_t rw;
	} r;
#else
	struct thread *lowner;
	union {
		struct lock_object *li;
		uintptr_t lx;
	} l;
#endif

	switch (subr) {
	case DIF_SUBR_RAND:
		regs[rd] = dtrace_xoroshiro128_plus_next(
		    state->dts_rstate[curcpu]);
		break;

#ifdef illumos
	case DIF_SUBR_MUTEX_OWNED:
		if (!dtrace_canload(tupregs[0].dttk_value, sizeof (kmutex_t),
		    mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		m.mx = dtrace_load64(tupregs[0].dttk_value);
		if (MUTEX_TYPE_ADAPTIVE(&m.mi))
			regs[rd] = MUTEX_OWNER(&m.mi) != MUTEX_NO_OWNER;
		else
			regs[rd] = LOCK_HELD(&m.mi.m_spin.m_spinlock);
		break;

	case DIF_SUBR_MUTEX_OWNER:
		if (!dtrace_canload(tupregs[0].dttk_value, sizeof (kmutex_t),
		    mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		m.mx = dtrace_load64(tupregs[0].dttk_value);
		if (MUTEX_TYPE_ADAPTIVE(&m.mi) &&
		    MUTEX_OWNER(&m.mi) != MUTEX_NO_OWNER)
			regs[rd] = (uintptr_t)MUTEX_OWNER(&m.mi);
		else
			regs[rd] = 0;
		break;

	case DIF_SUBR_MUTEX_TYPE_ADAPTIVE:
		if (!dtrace_canload(tupregs[0].dttk_value, sizeof (kmutex_t),
		    mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		m.mx = dtrace_load64(tupregs[0].dttk_value);
		regs[rd] = MUTEX_TYPE_ADAPTIVE(&m.mi);
		break;

	case DIF_SUBR_MUTEX_TYPE_SPIN:
		if (!dtrace_canload(tupregs[0].dttk_value, sizeof (kmutex_t),
		    mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		m.mx = dtrace_load64(tupregs[0].dttk_value);
		regs[rd] = MUTEX_TYPE_SPIN(&m.mi);
		break;

	case DIF_SUBR_RW_READ_HELD: {
		uintptr_t tmp;

		if (!dtrace_canload(tupregs[0].dttk_value, sizeof (uintptr_t),
		    mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		r.rw = dtrace_loadptr(tupregs[0].dttk_value);
		regs[rd] = _RW_READ_HELD(&r.ri, tmp);
		break;
	}

	case DIF_SUBR_RW_WRITE_HELD:
		if (!dtrace_canload(tupregs[0].dttk_value, sizeof (krwlock_t),
		    mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		r.rw = dtrace_loadptr(tupregs[0].dttk_value);
		regs[rd] = _RW_WRITE_HELD(&r.ri);
		break;

	case DIF_SUBR_RW_ISWRITER:
		if (!dtrace_canload(tupregs[0].dttk_value, sizeof (krwlock_t),
		    mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		r.rw = dtrace_loadptr(tupregs[0].dttk_value);
		regs[rd] = _RW_ISWRITER(&r.ri);
		break;

#else /* !illumos */
	case DIF_SUBR_MUTEX_OWNED:
		if (!dtrace_canload(tupregs[0].dttk_value,
			sizeof (struct lock_object), mstate, vstate)) {
			regs[rd] = 0;
			break;
		}
		l.lx = dtrace_loadptr((uintptr_t)&tupregs[0].dttk_value);
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		regs[rd] = LOCK_CLASS(l.li)->lc_owner(l.li, &lowner);
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
		break;

	case DIF_SUBR_MUTEX_OWNER:
		if (!dtrace_canload(tupregs[0].dttk_value,
			sizeof (struct lock_object), mstate, vstate)) {
			regs[rd] = 0;
			break;
		}
		l.lx = dtrace_loadptr((uintptr_t)&tupregs[0].dttk_value);
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		LOCK_CLASS(l.li)->lc_owner(l.li, &lowner);
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
		regs[rd] = (uintptr_t)lowner;
		break;

	case DIF_SUBR_MUTEX_TYPE_ADAPTIVE:
		if (!dtrace_canload(tupregs[0].dttk_value, sizeof (struct mtx),
		    mstate, vstate)) {
			regs[rd] = 0;
			break;
		}
		l.lx = dtrace_loadptr((uintptr_t)&tupregs[0].dttk_value);
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		regs[rd] = (LOCK_CLASS(l.li)->lc_flags & LC_SLEEPLOCK) != 0;
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
		break;

	case DIF_SUBR_MUTEX_TYPE_SPIN:
		if (!dtrace_canload(tupregs[0].dttk_value, sizeof (struct mtx),
		    mstate, vstate)) {
			regs[rd] = 0;
			break;
		}
		l.lx = dtrace_loadptr((uintptr_t)&tupregs[0].dttk_value);
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		regs[rd] = (LOCK_CLASS(l.li)->lc_flags & LC_SPINLOCK) != 0;
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
		break;

	case DIF_SUBR_RW_READ_HELD: 
	case DIF_SUBR_SX_SHARED_HELD: 
		if (!dtrace_canload(tupregs[0].dttk_value, sizeof (uintptr_t),
		    mstate, vstate)) {
			regs[rd] = 0;
			break;
		}
		l.lx = dtrace_loadptr((uintptr_t)&tupregs[0].dttk_value);
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		regs[rd] = LOCK_CLASS(l.li)->lc_owner(l.li, &lowner) &&
		    lowner == NULL;
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
		break;

	case DIF_SUBR_RW_WRITE_HELD:
	case DIF_SUBR_SX_EXCLUSIVE_HELD:
		if (!dtrace_canload(tupregs[0].dttk_value, sizeof (uintptr_t),
		    mstate, vstate)) {
			regs[rd] = 0;
			break;
		}
		l.lx = dtrace_loadptr(tupregs[0].dttk_value);
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		regs[rd] = LOCK_CLASS(l.li)->lc_owner(l.li, &lowner) &&
		    lowner != NULL;
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
		break;

	case DIF_SUBR_RW_ISWRITER:
	case DIF_SUBR_SX_ISEXCLUSIVE:
		if (!dtrace_canload(tupregs[0].dttk_value, sizeof (uintptr_t),
		    mstate, vstate)) {
			regs[rd] = 0;
			break;
		}
		l.lx = dtrace_loadptr(tupregs[0].dttk_value);
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		LOCK_CLASS(l.li)->lc_owner(l.li, &lowner);
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
		regs[rd] = (lowner == curthread);
		break;
#endif /* illumos */

	case DIF_SUBR_BCOPY: {
		/*
		 * We need to be sure that the destination is in the scratch
		 * region -- no other region is allowed.
		 */
		uintptr_t src = tupregs[0].dttk_value;
		uintptr_t dest = tupregs[1].dttk_value;
		size_t size = tupregs[2].dttk_value;

		if (!dtrace_inscratch(dest, size, mstate)) {
			*flags |= CPU_DTRACE_BADADDR;
			*illval = regs[rd];
			break;
		}

		if (!dtrace_canload(src, size, mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		dtrace_bcopy((void *)src, (void *)dest, size);
		break;
	}

	case DIF_SUBR_ALLOCA:
	case DIF_SUBR_COPYIN: {
		uintptr_t dest = P2ROUNDUP(mstate->dtms_scratch_ptr, 8);
		uint64_t size =
		    tupregs[subr == DIF_SUBR_ALLOCA ? 0 : 1].dttk_value;
		size_t scratch_size = (dest - mstate->dtms_scratch_ptr) + size;

		/*
		 * This action doesn't require any credential checks since
		 * probes will not activate in user contexts to which the
		 * enabling user does not have permissions.
		 */

		/*
		 * Rounding up the user allocation size could have overflowed
		 * a large, bogus allocation (like -1ULL) to 0.
		 */
		if (scratch_size < size ||
		    !DTRACE_INSCRATCH(mstate, scratch_size)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
			regs[rd] = 0;
			break;
		}

		if (subr == DIF_SUBR_COPYIN) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
			dtrace_copyin(tupregs[0].dttk_value, dest, size, flags);
			DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
		}

		mstate->dtms_scratch_ptr += scratch_size;
		regs[rd] = dest;
		break;
	}

	case DIF_SUBR_COPYINTO: {
		uint64_t size = tupregs[1].dttk_value;
		uintptr_t dest = tupregs[2].dttk_value;

		/*
		 * This action doesn't require any credential checks since
		 * probes will not activate in user contexts to which the
		 * enabling user does not have permissions.
		 */
		if (!dtrace_inscratch(dest, size, mstate)) {
			*flags |= CPU_DTRACE_BADADDR;
			*illval = regs[rd];
			break;
		}

		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		dtrace_copyin(tupregs[0].dttk_value, dest, size, flags);
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
		break;
	}

	case DIF_SUBR_COPYINSTR: {
		uintptr_t dest = mstate->dtms_scratch_ptr;
		uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];

		if (nargs > 1 && tupregs[1].dttk_value < size)
			size = tupregs[1].dttk_value + 1;

		/*
		 * This action doesn't require any credential checks since
		 * probes will not activate in user contexts to which the
		 * enabling user does not have permissions.
		 */
		if (!DTRACE_INSCRATCH(mstate, size)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
			regs[rd] = 0;
			break;
		}

		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
		dtrace_copyinstr(tupregs[0].dttk_value, dest, size, flags);
		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);

		((char *)dest)[size - 1] = '\0';
		mstate->dtms_scratch_ptr += size;
		regs[rd] = dest;
		break;
	}

#ifdef illumos
	case DIF_SUBR_MSGSIZE:
	case DIF_SUBR_MSGDSIZE: {
		uintptr_t baddr = tupregs[0].dttk_value, daddr;
		uintptr_t wptr, rptr;
		size_t count = 0;
		int cont = 0;

		while (baddr != 0 && !(*flags & CPU_DTRACE_FAULT)) {

			if (!dtrace_canload(baddr, sizeof (mblk_t), mstate,
			    vstate)) {
				regs[rd] = 0;
				break;
			}

			wptr = dtrace_loadptr(baddr +
			    offsetof(mblk_t, b_wptr));

			rptr = dtrace_loadptr(baddr +
			    offsetof(mblk_t, b_rptr));

			if (wptr < rptr) {
				*flags |= CPU_DTRACE_BADADDR;
				*illval = tupregs[0].dttk_value;
				break;
			}

			daddr = dtrace_loadptr(baddr +
			    offsetof(mblk_t, b_datap));

			baddr = dtrace_loadptr(baddr +
			    offsetof(mblk_t, b_cont));

			/*
			 * We want to prevent against denial-of-service here,
			 * so we're only going to search the list for
			 * dtrace_msgdsize_max mblks.
			 */
			if (cont++ > dtrace_msgdsize_max) {
				*flags |= CPU_DTRACE_ILLOP;
				break;
			}

			if (subr == DIF_SUBR_MSGDSIZE) {
				if (dtrace_load8(daddr +
				    offsetof(dblk_t, db_type)) != M_DATA)
					continue;
			}

			count += wptr - rptr;
		}

		if (!(*flags & CPU_DTRACE_FAULT))
			regs[rd] = count;

		break;
	}
#endif

	case DIF_SUBR_PROGENYOF: {
		pid_t pid = tupregs[0].dttk_value;
		proc_t *p;
		int rval = 0;

		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);

		for (p = curthread->t_procp; p != NULL; p = p->p_parent) {
#ifdef illumos
			if (p->p_pidp->pid_id == pid) {
#else
			if (p->p_pid == pid) {
#endif
				rval = 1;
				break;
			}
		}

		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);

		regs[rd] = rval;
		break;
	}

	case DIF_SUBR_SPECULATION:
		regs[rd] = dtrace_speculation(state);
		break;

	case DIF_SUBR_COPYOUT: {
		uintptr_t kaddr = tupregs[0].dttk_value;
		uintptr_t uaddr = tupregs[1].dttk_value;
		uint64_t size = tupregs[2].dttk_value;

		if (!dtrace_destructive_disallow &&
		    dtrace_priv_proc_control(state) &&
		    !dtrace_istoxic(kaddr, size) &&
		    dtrace_canload(kaddr, size, mstate, vstate)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
			dtrace_copyout(kaddr, uaddr, size, flags);
			DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
		}
		break;
	}

	case DIF_SUBR_COPYOUTSTR: {
		uintptr_t kaddr = tupregs[0].dttk_value;
		uintptr_t uaddr = tupregs[1].dttk_value;
		uint64_t size = tupregs[2].dttk_value;
		size_t lim;

		if (!dtrace_destructive_disallow &&
		    dtrace_priv_proc_control(state) &&
		    !dtrace_istoxic(kaddr, size) &&
		    dtrace_strcanload(kaddr, size, &lim, mstate, vstate)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
			dtrace_copyoutstr(kaddr, uaddr, lim, flags);
			DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
		}
		break;
	}

	case DIF_SUBR_STRLEN: {
		size_t size = state->dts_options[DTRACEOPT_STRSIZE];
		uintptr_t addr = (uintptr_t)tupregs[0].dttk_value;
		size_t lim;

		if (!dtrace_strcanload(addr, size, &lim, mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		regs[rd] = dtrace_strlen((char *)addr, lim);
		break;
	}

	case DIF_SUBR_STRCHR:
	case DIF_SUBR_STRRCHR: {
		/*
		 * We're going to iterate over the string looking for the
		 * specified character.  We will iterate until we have reached
		 * the string length or we have found the character.  If this
		 * is DIF_SUBR_STRRCHR, we will look for the last occurrence
		 * of the specified character instead of the first.
		 */
		uintptr_t addr = tupregs[0].dttk_value;
		uintptr_t addr_limit;
		uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];
		size_t lim;
		char c, target = (char)tupregs[1].dttk_value;

		if (!dtrace_strcanload(addr, size, &lim, mstate, vstate)) {
			regs[rd] = 0;
			break;
		}
		addr_limit = addr + lim;

		for (regs[rd] = 0; addr < addr_limit; addr++) {
			if ((c = dtrace_load8(addr)) == target) {
				regs[rd] = addr;

				if (subr == DIF_SUBR_STRCHR)
					break;
			}

			if (c == '\0')
				break;
		}
		break;
	}

	case DIF_SUBR_STRSTR:
	case DIF_SUBR_INDEX:
	case DIF_SUBR_RINDEX: {
		/*
		 * We're going to iterate over the string looking for the
		 * specified string.  We will iterate until we have reached
		 * the string length or we have found the string.  (Yes, this
		 * is done in the most naive way possible -- but considering
		 * that the string we're searching for is likely to be
		 * relatively short, the complexity of Rabin-Karp or similar
		 * hardly seems merited.)
		 */
		char *addr = (char *)(uintptr_t)tupregs[0].dttk_value;
		char *substr = (char *)(uintptr_t)tupregs[1].dttk_value;
		uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];
		size_t len = dtrace_strlen(addr, size);
		size_t sublen = dtrace_strlen(substr, size);
		char *limit = addr + len, *orig = addr;
		int notfound = subr == DIF_SUBR_STRSTR ? 0 : -1;
		int inc = 1;

		regs[rd] = notfound;

		if (!dtrace_canload((uintptr_t)addr, len + 1, mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		if (!dtrace_canload((uintptr_t)substr, sublen + 1, mstate,
		    vstate)) {
			regs[rd] = 0;
			break;
		}

		/*
		 * strstr() and index()/rindex() have similar semantics if
		 * both strings are the empty string: strstr() returns a
		 * pointer to the (empty) string, and index() and rindex()
		 * both return index 0 (regardless of any position argument).
		 */
		if (sublen == 0 && len == 0) {
			if (subr == DIF_SUBR_STRSTR)
				regs[rd] = (uintptr_t)addr;
			else
				regs[rd] = 0;
			break;
		}

		if (subr != DIF_SUBR_STRSTR) {
			if (subr == DIF_SUBR_RINDEX) {
				limit = orig - 1;
				addr += len;
				inc = -1;
			}

			/*
			 * Both index() and rindex() take an optional position
			 * argument that denotes the starting position.
			 */
			if (nargs == 3) {
				int64_t pos = (int64_t)tupregs[2].dttk_value;

				/*
				 * If the position argument to index() is
				 * negative, Perl implicitly clamps it at
				 * zero.  This semantic is a little surprising
				 * given the special meaning of negative
				 * positions to similar Perl functions like
				 * substr(), but it appears to reflect a
				 * notion that index() can start from a
				 * negative index and increment its way up to
				 * the string.  Given this notion, Perl's
				 * rindex() is at least self-consistent in
				 * that it implicitly clamps positions greater
				 * than the string length to be the string
				 * length.  Where Perl completely loses
				 * coherence, however, is when the specified
				 * substring is the empty string ("").  In
				 * this case, even if the position is
				 * negative, rindex() returns 0 -- and even if
				 * the position is greater than the length,
				 * index() returns the string length.  These
				 * semantics violate the notion that index()
				 * should never return a value less than the
				 * specified position and that rindex() should
				 * never return a value greater than the
				 * specified position.  (One assumes that
				 * these semantics are artifacts of Perl's
				 * implementation and not the results of
				 * deliberate design -- it beggars belief that
				 * even Larry Wall could desire such oddness.)
				 * While in the abstract one would wish for
				 * consistent position semantics across
				 * substr(), index() and rindex() -- or at the
				 * very least self-consistent position
				 * semantics for index() and rindex() -- we
				 * instead opt to keep with the extant Perl
				 * semantics, in all their broken glory.  (Do
				 * we have more desire to maintain Perl's
				 * semantics than Perl does?  Probably.)
				 */
				if (subr == DIF_SUBR_RINDEX) {
					if (pos < 0) {
						if (sublen == 0)
							regs[rd] = 0;
						break;
					}

					if (pos > len)
						pos = len;
				} else {
					if (pos < 0)
						pos = 0;

					if (pos >= len) {
						if (sublen == 0)
							regs[rd] = len;
						break;
					}
				}

				addr = orig + pos;
			}
		}

		for (regs[rd] = notfound; addr != limit; addr += inc) {
			if (dtrace_strncmp(addr, substr, sublen) == 0) {
				if (subr != DIF_SUBR_STRSTR) {
					/*
					 * As D index() and rindex() are
					 * modeled on Perl (and not on awk),
					 * we return a zero-based (and not a
					 * one-based) index.  (For you Perl
					 * weenies: no, we're not going to add
					 * $[ -- and shouldn't you be at a con
					 * or something?)
					 */
					regs[rd] = (uintptr_t)(addr - orig);
					break;
				}

				ASSERT(subr == DIF_SUBR_STRSTR);
				regs[rd] = (uintptr_t)addr;
				break;
			}
		}

		break;
	}

	case DIF_SUBR_STRTOK: {
		uintptr_t addr = tupregs[0].dttk_value;
		uintptr_t tokaddr = tupregs[1].dttk_value;
		uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];
		uintptr_t limit, toklimit;
		size_t clim;
		uint8_t c = 0, tokmap[32];	 /* 256 / 8 */
		char *dest = (char *)mstate->dtms_scratch_ptr;
		int i;

		/*
		 * Check both the token buffer and (later) the input buffer,
		 * since both could be non-scratch addresses.
		 */
		if (!dtrace_strcanload(tokaddr, size, &clim, mstate, vstate)) {
			regs[rd] = 0;
			break;
		}
		toklimit = tokaddr + clim;

		if (!DTRACE_INSCRATCH(mstate, size)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
			regs[rd] = 0;
			break;
		}

		if (addr == 0) {
			/*
			 * If the address specified is NULL, we use our saved
			 * strtok pointer from the mstate.  Note that this
			 * means that the saved strtok pointer is _only_
			 * valid within multiple enablings of the same probe --
			 * it behaves like an implicit clause-local variable.
			 */
			addr = mstate->dtms_strtok;
			limit = mstate->dtms_strtok_limit;
		} else {
			/*
			 * If the user-specified address is non-NULL we must
			 * access check it.  This is the only time we have
			 * a chance to do so, since this address may reside
			 * in the string table of this clause-- future calls
			 * (when we fetch addr from mstate->dtms_strtok)
			 * would fail this access check.
			 */
			if (!dtrace_strcanload(addr, size, &clim, mstate,
			    vstate)) {
				regs[rd] = 0;
				break;
			}
			limit = addr + clim;
		}

		/*
		 * First, zero the token map, and then process the token
		 * string -- setting a bit in the map for every character
		 * found in the token string.
		 */
		for (i = 0; i < sizeof (tokmap); i++)
			tokmap[i] = 0;

		for (; tokaddr < toklimit; tokaddr++) {
			if ((c = dtrace_load8(tokaddr)) == '\0')
				break;

			ASSERT((c >> 3) < sizeof (tokmap));
			tokmap[c >> 3] |= (1 << (c & 0x7));
		}

		for (; addr < limit; addr++) {
			/*
			 * We're looking for a character that is _not_
			 * contained in the token string.
			 */
			if ((c = dtrace_load8(addr)) == '\0')
				break;

			if (!(tokmap[c >> 3] & (1 << (c & 0x7))))
				break;
		}

		if (c == '\0') {
			/*
			 * We reached the end of the string without finding
			 * any character that was not in the token string.
			 * We return NULL in this case, and we set the saved
			 * address to NULL as well.
			 */
			regs[rd] = 0;
			mstate->dtms_strtok = 0;
			mstate->dtms_strtok_limit = 0;
			break;
		}

		/*
		 * From here on, we're copying into the destination string.
		 */
		for (i = 0; addr < limit && i < size - 1; addr++) {
			if ((c = dtrace_load8(addr)) == '\0')
				break;

			if (tokmap[c >> 3] & (1 << (c & 0x7)))
				break;

			ASSERT(i < size);
			dest[i++] = c;
		}

		ASSERT(i < size);
		dest[i] = '\0';
		regs[rd] = (uintptr_t)dest;
		mstate->dtms_scratch_ptr += size;
		mstate->dtms_strtok = addr;
		mstate->dtms_strtok_limit = limit;
		break;
	}

	case DIF_SUBR_SUBSTR: {
		uintptr_t s = tupregs[0].dttk_value;
		uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];
		char *d = (char *)mstate->dtms_scratch_ptr;
		int64_t index = (int64_t)tupregs[1].dttk_value;
		int64_t remaining = (int64_t)tupregs[2].dttk_value;
		size_t len = dtrace_strlen((char *)s, size);
		int64_t i;

		if (!dtrace_canload(s, len + 1, mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		if (!DTRACE_INSCRATCH(mstate, size)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
			regs[rd] = 0;
			break;
		}

		if (nargs <= 2)
			remaining = (int64_t)size;

		if (index < 0) {
			index += len;

			if (index < 0 && index + remaining > 0) {
				remaining += index;
				index = 0;
			}
		}

		if (index >= len || index < 0) {
			remaining = 0;
		} else if (remaining < 0) {
			remaining += len - index;
		} else if (index + remaining > size) {
			remaining = size - index;
		}

		for (i = 0; i < remaining; i++) {
			if ((d[i] = dtrace_load8(s + index + i)) == '\0')
				break;
		}

		d[i] = '\0';

		mstate->dtms_scratch_ptr += size;
		regs[rd] = (uintptr_t)d;
		break;
	}

	case DIF_SUBR_JSON: {
		uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];
		uintptr_t json = tupregs[0].dttk_value;
		size_t jsonlen = dtrace_strlen((char *)json, size);
		uintptr_t elem = tupregs[1].dttk_value;
		size_t elemlen = dtrace_strlen((char *)elem, size);

		char *dest = (char *)mstate->dtms_scratch_ptr;
		char *elemlist = (char *)mstate->dtms_scratch_ptr + jsonlen + 1;
		char *ee = elemlist;
		int nelems = 1;
		uintptr_t cur;

		if (!dtrace_canload(json, jsonlen + 1, mstate, vstate) ||
		    !dtrace_canload(elem, elemlen + 1, mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		if (!DTRACE_INSCRATCH(mstate, jsonlen + 1 + elemlen + 1)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
			regs[rd] = 0;
			break;
		}

		/*
		 * Read the element selector and split it up into a packed list
		 * of strings.
		 */
		for (cur = elem; cur < elem + elemlen; cur++) {
			char cc = dtrace_load8(cur);

			if (cur == elem && cc == '[') {
				/*
				 * If the first element selector key is
				 * actually an array index then ignore the
				 * bracket.
				 */
				continue;
			}

			if (cc == ']')
				continue;

			if (cc == '.' || cc == '[') {
				nelems++;
				cc = '\0';
			}

			*ee++ = cc;
		}
		*ee++ = '\0';

		if ((regs[rd] = (uintptr_t)dtrace_json(size, json, elemlist,
		    nelems, dest)) != 0)
			mstate->dtms_scratch_ptr += jsonlen + 1;
		break;
	}

	case DIF_SUBR_TOUPPER:
	case DIF_SUBR_TOLOWER: {
		uintptr_t s = tupregs[0].dttk_value;
		uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];
		char *dest = (char *)mstate->dtms_scratch_ptr, c;
		size_t len = dtrace_strlen((char *)s, size);
		char lower, upper, convert;
		int64_t i;

		if (subr == DIF_SUBR_TOUPPER) {
			lower = 'a';
			upper = 'z';
			convert = 'A';
		} else {
			lower = 'A';
			upper = 'Z';
			convert = 'a';
		}

		if (!dtrace_canload(s, len + 1, mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		if (!DTRACE_INSCRATCH(mstate, size)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
			regs[rd] = 0;
			break;
		}

		for (i = 0; i < size - 1; i++) {
			if ((c = dtrace_load8(s + i)) == '\0')
				break;

			if (c >= lower && c <= upper)
				c = convert + (c - lower);

			dest[i] = c;
		}

		ASSERT(i < size);
		dest[i] = '\0';
		regs[rd] = (uintptr_t)dest;
		mstate->dtms_scratch_ptr += size;
		break;
	}

#ifdef illumos
	case DIF_SUBR_GETMAJOR:
#ifdef _LP64
		regs[rd] = (tupregs[0].dttk_value >> NBITSMINOR64) & MAXMAJ64;
#else
		regs[rd] = (tupregs[0].dttk_value >> NBITSMINOR) & MAXMAJ;
#endif
		break;

	case DIF_SUBR_GETMINOR:
#ifdef _LP64
		regs[rd] = tupregs[0].dttk_value & MAXMIN64;
#else
		regs[rd] = tupregs[0].dttk_value & MAXMIN;
#endif
		break;

	case DIF_SUBR_DDI_PATHNAME: {
		/*
		 * This one is a galactic mess.  We are going to roughly
		 * emulate ddi_pathname(), but it's made more complicated
		 * by the fact that we (a) want to include the minor name and
		 * (b) must proceed iteratively instead of recursively.
		 */
		uintptr_t dest = mstate->dtms_scratch_ptr;
		uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];
		char *start = (char *)dest, *end = start + size - 1;
		uintptr_t daddr = tupregs[0].dttk_value;
		int64_t minor = (int64_t)tupregs[1].dttk_value;
		char *s;
		int i, len, depth = 0;

		/*
		 * Due to all the pointer jumping we do and context we must
		 * rely upon, we just mandate that the user must have kernel
		 * read privileges to use this routine.
		 */
		if ((mstate->dtms_access & DTRACE_ACCESS_KERNEL) == 0) {
			*flags |= CPU_DTRACE_KPRIV;
			*illval = daddr;
			regs[rd] = 0;
		}

		if (!DTRACE_INSCRATCH(mstate, size)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
			regs[rd] = 0;
			break;
		}

		*end = '\0';

		/*
		 * We want to have a name for the minor.  In order to do this,
		 * we need to walk the minor list from the devinfo.  We want
		 * to be sure that we don't infinitely walk a circular list,
		 * so we check for circularity by sending a scout pointer
		 * ahead two elements for every element that we iterate over;
		 * if the list is circular, these will ultimately point to the
		 * same element.  You may recognize this little trick as the
		 * answer to a stupid interview question -- one that always
		 * seems to be asked by those who had to have it laboriously
		 * explained to them, and who can't even concisely describe
		 * the conditions under which one would be forced to resort to
		 * this technique.  Needless to say, those conditions are
		 * found here -- and probably only here.  Is this the only use
		 * of this infamous trick in shipping, production code?  If it
		 * isn't, it probably should be...
		 */
		if (minor != -1) {
			uintptr_t maddr = dtrace_loadptr(daddr +
			    offsetof(struct dev_info, devi_minor));

			uintptr_t next = offsetof(struct ddi_minor_data, next);
			uintptr_t name = offsetof(struct ddi_minor_data,
			    d_minor) + offsetof(struct ddi_minor, name);
			uintptr_t dev = offsetof(struct ddi_minor_data,
			    d_minor) + offsetof(struct ddi_minor, dev);
			uintptr_t scout;

			if (maddr != NULL)
				scout = dtrace_loadptr(maddr + next);

			while (maddr != NULL && !(*flags & CPU_DTRACE_FAULT)) {
				uint64_t m;
#ifdef _LP64
				m = dtrace_load64(maddr + dev) & MAXMIN64;
#else
				m = dtrace_load32(maddr + dev) & MAXMIN;
#endif
				if (m != minor) {
					maddr = dtrace_loadptr(maddr + next);

					if (scout == NULL)
						continue;

					scout = dtrace_loadptr(scout + next);

					if (scout == NULL)
						continue;

					scout = dtrace_loadptr(scout + next);

					if (scout == NULL)
						continue;

					if (scout == maddr) {
						*flags |= CPU_DTRACE_ILLOP;
						break;
					}

					continue;
				}

				/*
				 * We have the minor data.  Now we need to
				 * copy the minor's name into the end of the
				 * pathname.
				 */
				s = (char *)dtrace_loadptr(maddr + name);
				len = dtrace_strlen(s, size);

				if (*flags & CPU_DTRACE_FAULT)
					break;

				if (len != 0) {
					if ((end -= (len + 1)) < start)
						break;

					*end = ':';
				}

				for (i = 1; i <= len; i++)
					end[i] = dtrace_load8((uintptr_t)s++);
				break;
			}
		}

		while (daddr != NULL && !(*flags & CPU_DTRACE_FAULT)) {
			ddi_node_state_t devi_state;

			devi_state = dtrace_load32(daddr +
			    offsetof(struct dev_info, devi_node_state));

			if (*flags & CPU_DTRACE_FAULT)
				break;

			if (devi_state >= DS_INITIALIZED) {
				s = (char *)dtrace_loadptr(daddr +
				    offsetof(struct dev_info, devi_addr));
				len = dtrace_strlen(s, size);

				if (*flags & CPU_DTRACE_FAULT)
					break;

				if (len != 0) {
					if ((end -= (len + 1)) < start)
						break;

					*end = '@';
				}

				for (i = 1; i <= len; i++)
					end[i] = dtrace_load8((uintptr_t)s++);
			}

			/*
			 * Now for the node name...
			 */
			s = (char *)dtrace_loadptr(daddr +
			    offsetof(struct dev_info, devi_node_name));

			daddr = dtrace_loadptr(daddr +
			    offsetof(struct dev_info, devi_parent));

			/*
			 * If our parent is NULL (that is, if we're the root
			 * node), we're going to use the special path
			 * "devices".
			 */
			if (daddr == 0)
				s = "devices";

			len = dtrace_strlen(s, size);
			if (*flags & CPU_DTRACE_FAULT)
				break;

			if ((end -= (len + 1)) < start)
				break;

			for (i = 1; i <= len; i++)
				end[i] = dtrace_load8((uintptr_t)s++);
			*end = '/';

			if (depth++ > dtrace_devdepth_max) {
				*flags |= CPU_DTRACE_ILLOP;
				break;
			}
		}

		if (end < start)
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);

		if (daddr == 0) {
			regs[rd] = (uintptr_t)end;
			mstate->dtms_scratch_ptr += size;
		}

		break;
	}
#endif

	case DIF_SUBR_STRJOIN: {
		char *d = (char *)mstate->dtms_scratch_ptr;
		uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];
		uintptr_t s1 = tupregs[0].dttk_value;
		uintptr_t s2 = tupregs[1].dttk_value;
		int i = 0, j = 0;
		size_t lim1, lim2;
		char c;

		if (!dtrace_strcanload(s1, size, &lim1, mstate, vstate) ||
		    !dtrace_strcanload(s2, size, &lim2, mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		if (!DTRACE_INSCRATCH(mstate, size)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
			regs[rd] = 0;
			break;
		}

		for (;;) {
			if (i >= size) {
				DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
				regs[rd] = 0;
				break;
			}
			c = (i >= lim1) ? '\0' : dtrace_load8(s1++);
			if ((d[i++] = c) == '\0') {
				i--;
				break;
			}
		}

		for (;;) {
			if (i >= size) {
				DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
				regs[rd] = 0;
				break;
			}

			c = (j++ >= lim2) ? '\0' : dtrace_load8(s2++);
			if ((d[i++] = c) == '\0')
				break;
		}

		if (i < size) {
			mstate->dtms_scratch_ptr += i;
			regs[rd] = (uintptr_t)d;
		}

		break;
	}

	case DIF_SUBR_STRTOLL: {
		uintptr_t s = tupregs[0].dttk_value;
		uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];
		size_t lim;
		int base = 10;

		if (nargs > 1) {
			if ((base = tupregs[1].dttk_value) <= 1 ||
			    base > ('z' - 'a' + 1) + ('9' - '0' + 1)) {
				*flags |= CPU_DTRACE_ILLOP;
				break;
			}
		}

		if (!dtrace_strcanload(s, size, &lim, mstate, vstate)) {
			regs[rd] = INT64_MIN;
			break;
		}

		regs[rd] = dtrace_strtoll((char *)s, base, lim);
		break;
	}

	case DIF_SUBR_LLTOSTR: {
		int64_t i = (int64_t)tupregs[0].dttk_value;
		uint64_t val, digit;
		uint64_t size = 65;	/* enough room for 2^64 in binary */
		char *end = (char *)mstate->dtms_scratch_ptr + size - 1;
		int base = 10;

		if (nargs > 1) {
			if ((base = tupregs[1].dttk_value) <= 1 ||
			    base > ('z' - 'a' + 1) + ('9' - '0' + 1)) {
				*flags |= CPU_DTRACE_ILLOP;
				break;
			}
		}

		val = (base == 10 && i < 0) ? i * -1 : i;

		if (!DTRACE_INSCRATCH(mstate, size)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
			regs[rd] = 0;
			break;
		}

		for (*end-- = '\0'; val; val /= base) {
			if ((digit = val % base) <= '9' - '0') {
				*end-- = '0' + digit;
			} else {
				*end-- = 'a' + (digit - ('9' - '0') - 1);
			}
		}

		if (i == 0 && base == 16)
			*end-- = '0';

		if (base == 16)
			*end-- = 'x';

		if (i == 0 || base == 8 || base == 16)
			*end-- = '0';

		if (i < 0 && base == 10)
			*end-- = '-';

		regs[rd] = (uintptr_t)end + 1;
		mstate->dtms_scratch_ptr += size;
		break;
	}

	case DIF_SUBR_HTONS:
	case DIF_SUBR_NTOHS:
#if BYTE_ORDER == BIG_ENDIAN
		regs[rd] = (uint16_t)tupregs[0].dttk_value;
#else
		regs[rd] = DT_BSWAP_16((uint16_t)tupregs[0].dttk_value);
#endif
		break;


	case DIF_SUBR_HTONL:
	case DIF_SUBR_NTOHL:
#if BYTE_ORDER == BIG_ENDIAN
		regs[rd] = (uint32_t)tupregs[0].dttk_value;
#else
		regs[rd] = DT_BSWAP_32((uint32_t)tupregs[0].dttk_value);
#endif
		break;


	case DIF_SUBR_HTONLL:
	case DIF_SUBR_NTOHLL:
#if BYTE_ORDER == BIG_ENDIAN
		regs[rd] = (uint64_t)tupregs[0].dttk_value;
#else
		regs[rd] = DT_BSWAP_64((uint64_t)tupregs[0].dttk_value);
#endif
		break;


	case DIF_SUBR_DIRNAME:
	case DIF_SUBR_BASENAME: {
		char *dest = (char *)mstate->dtms_scratch_ptr;
		uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];
		uintptr_t src = tupregs[0].dttk_value;
		int i, j, len = dtrace_strlen((char *)src, size);
		int lastbase = -1, firstbase = -1, lastdir = -1;
		int start, end;

		if (!dtrace_canload(src, len + 1, mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		if (!DTRACE_INSCRATCH(mstate, size)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
			regs[rd] = 0;
			break;
		}

		/*
		 * The basename and dirname for a zero-length string is
		 * defined to be "."
		 */
		if (len == 0) {
			len = 1;
			src = (uintptr_t)".";
		}

		/*
		 * Start from the back of the string, moving back toward the
		 * front until we see a character that isn't a slash.  That
		 * character is the last character in the basename.
		 */
		for (i = len - 1; i >= 0; i--) {
			if (dtrace_load8(src + i) != '/')
				break;
		}

		if (i >= 0)
			lastbase = i;

		/*
		 * Starting from the last character in the basename, move
		 * towards the front until we find a slash.  The character
		 * that we processed immediately before that is the first
		 * character in the basename.
		 */
		for (; i >= 0; i--) {
			if (dtrace_load8(src + i) == '/')
				break;
		}

		if (i >= 0)
			firstbase = i + 1;

		/*
		 * Now keep going until we find a non-slash character.  That
		 * character is the last character in the dirname.
		 */
		for (; i >= 0; i--) {
			if (dtrace_load8(src + i) != '/')
				break;
		}

		if (i >= 0)
			lastdir = i;

		ASSERT(!(lastbase == -1 && firstbase != -1));
		ASSERT(!(firstbase == -1 && lastdir != -1));

		if (lastbase == -1) {
			/*
			 * We didn't find a non-slash character.  We know that
			 * the length is non-zero, so the whole string must be
			 * slashes.  In either the dirname or the basename
			 * case, we return '/'.
			 */
			ASSERT(firstbase == -1);
			firstbase = lastbase = lastdir = 0;
		}

		if (firstbase == -1) {
			/*
			 * The entire string consists only of a basename
			 * component.  If we're looking for dirname, we need
			 * to change our string to be just "."; if we're
			 * looking for a basename, we'll just set the first
			 * character of the basename to be 0.
			 */
			if (subr == DIF_SUBR_DIRNAME) {
				ASSERT(lastdir == -1);
				src = (uintptr_t)".";
				lastdir = 0;
			} else {
				firstbase = 0;
			}
		}

		if (subr == DIF_SUBR_DIRNAME) {
			if (lastdir == -1) {
				/*
				 * We know that we have a slash in the name --
				 * or lastdir would be set to 0, above.  And
				 * because lastdir is -1, we know that this
				 * slash must be the first character.  (That
				 * is, the full string must be of the form
				 * "/basename".)  In this case, the last
				 * character of the directory name is 0.
				 */
				lastdir = 0;
			}

			start = 0;
			end = lastdir;
		} else {
			ASSERT(subr == DIF_SUBR_BASENAME);
			ASSERT(firstbase != -1 && lastbase != -1);
			start = firstbase;
			end = lastbase;
		}

		for (i = start, j = 0; i <= end && j < size - 1; i++, j++)
			dest[j] = dtrace_load8(src + i);

		dest[j] = '\0';
		regs[rd] = (uintptr_t)dest;
		mstate->dtms_scratch_ptr += size;
		break;
	}

	case DIF_SUBR_GETF: {
		uintptr_t fd = tupregs[0].dttk_value;
		struct filedesc *fdp;
		file_t *fp;

		if (!dtrace_priv_proc(state)) {
			regs[rd] = 0;
			break;
		}
		fdp = curproc->p_fd;
		FILEDESC_SLOCK(fdp);
		fp = fget_locked(fdp, fd);
		mstate->dtms_getf = fp;
		regs[rd] = (uintptr_t)fp;
		FILEDESC_SUNLOCK(fdp);
		break;
	}

	case DIF_SUBR_CLEANPATH: {
		char *dest = (char *)mstate->dtms_scratch_ptr, c;
		uint64_t size = state->dts_options[DTRACEOPT_STRSIZE];
		uintptr_t src = tupregs[0].dttk_value;
		size_t lim;
		int i = 0, j = 0;
#ifdef illumos
		zone_t *z;
#endif

		if (!dtrace_strcanload(src, size, &lim, mstate, vstate)) {
			regs[rd] = 0;
			break;
		}

		if (!DTRACE_INSCRATCH(mstate, size)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
			regs[rd] = 0;
			break;
		}

		/*
		 * Move forward, loading each character.
		 */
		do {
			c = (i >= lim) ? '\0' : dtrace_load8(src + i++);
next:
			if (j + 5 >= size)	/* 5 = strlen("/..c\0") */
				break;

			if (c != '/') {
				dest[j++] = c;
				continue;
			}

			c = (i >= lim) ? '\0' : dtrace_load8(src + i++);

			if (c == '/') {
				/*
				 * We have two slashes -- we can just advance
				 * to the next character.
				 */
				goto next;
			}

			if (c != '.') {
				/*
				 * This is not "." and it's not ".." -- we can
				 * just store the "/" and this character and
				 * drive on.
				 */
				dest[j++] = '/';
				dest[j++] = c;
				continue;
			}

			c = (i >= lim) ? '\0' : dtrace_load8(src + i++);

			if (c == '/') {
				/*
				 * This is a "/./" component.  We're not going
				 * to store anything in the destination buffer;
				 * we're just going to go to the next component.
				 */
				goto next;
			}

			if (c != '.') {
				/*
				 * This is not ".." -- we can just store the
				 * "/." and this character and continue
				 * processing.
				 */
				dest[j++] = '/';
				dest[j++] = '.';
				dest[j++] = c;
				continue;
			}

			c = (i >= lim) ? '\0' : dtrace_load8(src + i++);

			if (c != '/' && c != '\0') {
				/*
				 * This is not ".." -- it's "..[mumble]".
				 * We'll store the "/.." and this character
				 * and continue processing.
				 */
				dest[j++] = '/';
				dest[j++] = '.';
				dest[j++] = '.';
				dest[j++] = c;
				continue;
			}

			/*
			 * This is "/../" or "/..\0".  We need to back up
			 * our destination pointer until we find a "/".
			 */
			i--;
			while (j != 0 && dest[--j] != '/')
				continue;

			if (c == '\0')
				dest[++j] = '/';
		} while (c != '\0');

		dest[j] = '\0';

#ifdef illumos
		if (mstate->dtms_getf != NULL &&
		    !(mstate->dtms_access & DTRACE_ACCESS_KERNEL) &&
		    (z = state->dts_cred.dcr_cred->cr_zone) != kcred->cr_zone) {
			/*
			 * If we've done a getf() as a part of this ECB and we
			 * don't have kernel access (and we're not in the global
			 * zone), check if the path we cleaned up begins with
			 * the zone's root path, and trim it off if so.  Note
			 * that this is an output cleanliness issue, not a
			 * security issue: knowing one's zone root path does
			 * not enable privilege escalation.
			 */
			if (strstr(dest, z->zone_rootpath) == dest)
				dest += strlen(z->zone_rootpath) - 1;
		}
#endif

		regs[rd] = (uintptr_t)dest;
		mstate->dtms_scratch_ptr += size;
		break;
	}

	case DIF_SUBR_INET_NTOA:
	case DIF_SUBR_INET_NTOA6:
	case DIF_SUBR_INET_NTOP: {
		size_t size;
		int af, argi, i;
		char *base, *end;

		if (subr == DIF_SUBR_INET_NTOP) {
			af = (int)tupregs[0].dttk_value;
			argi = 1;
		} else {
			af = subr == DIF_SUBR_INET_NTOA ? AF_INET: AF_INET6;
			argi = 0;
		}

		if (af == AF_INET) {
			ipaddr_t ip4;
			uint8_t *ptr8, val;

			if (!dtrace_canload(tupregs[argi].dttk_value,
			    sizeof (ipaddr_t), mstate, vstate)) {
				regs[rd] = 0;
				break;
			}

			/*
			 * Safely load the IPv4 address.
			 */
			ip4 = dtrace_load32(tupregs[argi].dttk_value);

			/*
			 * Check an IPv4 string will fit in scratch.
			 */
			size = INET_ADDRSTRLEN;
			if (!DTRACE_INSCRATCH(mstate, size)) {
				DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
				regs[rd] = 0;
				break;
			}
			base = (char *)mstate->dtms_scratch_ptr;
			end = (char *)mstate->dtms_scratch_ptr + size - 1;

			/*
			 * Stringify as a dotted decimal quad.
			 */
			*end-- = '\0';
			ptr8 = (uint8_t *)&ip4;
			for (i = 3; i >= 0; i--) {
				val = ptr8[i];

				if (val == 0) {
					*end-- = '0';
				} else {
					for (; val; val /= 10) {
						*end-- = '0' + (val % 10);
					}
				}

				if (i > 0)
					*end-- = '.';
			}
			ASSERT(end + 1 >= base);

		} else if (af == AF_INET6) {
			struct in6_addr ip6;
			int firstzero, tryzero, numzero, v6end;
			uint16_t val;
			const char digits[] = "0123456789abcdef";

			/*
			 * Stringify using RFC 1884 convention 2 - 16 bit
			 * hexadecimal values with a zero-run compression.
			 * Lower case hexadecimal digits are used.
			 * 	eg, fe80::214:4fff:fe0b:76c8.
			 * The IPv4 embedded form is returned for inet_ntop,
			 * just the IPv4 string is returned for inet_ntoa6.
			 */

			if (!dtrace_canload(tupregs[argi].dttk_value,
			    sizeof (struct in6_addr), mstate, vstate)) {
				regs[rd] = 0;
				break;
			}

			/*
			 * Safely load the IPv6 address.
			 */
			dtrace_bcopy(
			    (void *)(uintptr_t)tupregs[argi].dttk_value,
			    (void *)(uintptr_t)&ip6, sizeof (struct in6_addr));

			/*
			 * Check an IPv6 string will fit in scratch.
			 */
			size = INET6_ADDRSTRLEN;
			if (!DTRACE_INSCRATCH(mstate, size)) {
				DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
				regs[rd] = 0;
				break;
			}
			base = (char *)mstate->dtms_scratch_ptr;
			end = (char *)mstate->dtms_scratch_ptr + size - 1;
			*end-- = '\0';

			/*
			 * Find the longest run of 16 bit zero values
			 * for the single allowed zero compression - "::".
			 */
			firstzero = -1;
			tryzero = -1;
			numzero = 1;
			for (i = 0; i < sizeof (struct in6_addr); i++) {
#ifdef illumos
				if (ip6._S6_un._S6_u8[i] == 0 &&
#else
				if (ip6.__u6_addr.__u6_addr8[i] == 0 &&
#endif
				    tryzero == -1 && i % 2 == 0) {
					tryzero = i;
					continue;
				}

				if (tryzero != -1 &&
#ifdef illumos
				    (ip6._S6_un._S6_u8[i] != 0 ||
#else
				    (ip6.__u6_addr.__u6_addr8[i] != 0 ||
#endif
				    i == sizeof (struct in6_addr) - 1)) {

					if (i - tryzero <= numzero) {
						tryzero = -1;
						continue;
					}

					firstzero = tryzero;
					numzero = i - i % 2 - tryzero;
					tryzero = -1;

#ifdef illumos
					if (ip6._S6_un._S6_u8[i] == 0 &&
#else
					if (ip6.__u6_addr.__u6_addr8[i] == 0 &&
#endif
					    i == sizeof (struct in6_addr) - 1)
						numzero += 2;
				}
			}
			ASSERT(firstzero + numzero <= sizeof (struct in6_addr));

			/*
			 * Check for an IPv4 embedded address.
			 */
			v6end = sizeof (struct in6_addr) - 2;
			if (IN6_IS_ADDR_V4MAPPED(&ip6) ||
			    IN6_IS_ADDR_V4COMPAT(&ip6)) {
				for (i = sizeof (struct in6_addr) - 1;
				    i >= DTRACE_V4MAPPED_OFFSET; i--) {
					ASSERT(end >= base);

#ifdef illumos
					val = ip6._S6_un._S6_u8[i];
#else
					val = ip6.__u6_addr.__u6_addr8[i];
#endif

					if (val == 0) {
						*end-- = '0';
					} else {
						for (; val; val /= 10) {
							*end-- = '0' + val % 10;
						}
					}

					if (i > DTRACE_V4MAPPED_OFFSET)
						*end-- = '.';
				}

				if (subr == DIF_SUBR_INET_NTOA6)
					goto inetout;

				/*
				 * Set v6end to skip the IPv4 address that
				 * we have already stringified.
				 */
				v6end = 10;
			}

			/*
			 * Build the IPv6 string by working through the
			 * address in reverse.
			 */
			for (i = v6end; i >= 0; i -= 2) {
				ASSERT(end >= base);

				if (i == firstzero + numzero - 2) {
					*end-- = ':';
					*end-- = ':';
					i -= numzero - 2;
					continue;
				}

				if (i < 14 && i != firstzero - 2)
					*end-- = ':';

#ifdef illumos
				val = (ip6._S6_un._S6_u8[i] << 8) +
				    ip6._S6_un._S6_u8[i + 1];
#else
				val = (ip6.__u6_addr.__u6_addr8[i] << 8) +
				    ip6.__u6_addr.__u6_addr8[i + 1];
#endif

				if (val == 0) {
					*end-- = '0';
				} else {
					for (; val; val /= 16) {
						*end-- = digits[val % 16];
					}
				}
			}
			ASSERT(end + 1 >= base);

		} else {
			/*
			 * The user didn't use AH_INET or AH_INET6.
			 */
			DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
			regs[rd] = 0;
			break;
		}

inetout:	regs[rd] = (uintptr_t)end + 1;
		mstate->dtms_scratch_ptr += size;
		break;
	}

	case DIF_SUBR_MEMREF: {
		uintptr_t size = 2 * sizeof(uintptr_t);
		uintptr_t *memref = (uintptr_t *) P2ROUNDUP(mstate->dtms_scratch_ptr, sizeof(uintptr_t));
		size_t scratch_size = ((uintptr_t) memref - mstate->dtms_scratch_ptr) + size;

		/* address and length */
		memref[0] = tupregs[0].dttk_value;
		memref[1] = tupregs[1].dttk_value;

		regs[rd] = (uintptr_t) memref;
		mstate->dtms_scratch_ptr += scratch_size;
		break;
	}

#ifndef illumos
	case DIF_SUBR_MEMSTR: {
		char *str = (char *)mstate->dtms_scratch_ptr;
		uintptr_t mem = tupregs[0].dttk_value;
		char c = tupregs[1].dttk_value;
		size_t size = tupregs[2].dttk_value;
		uint8_t n;
		int i;

		regs[rd] = 0;

		if (size == 0)
			break;

		if (!dtrace_canload(mem, size - 1, mstate, vstate))
			break;

		if (!DTRACE_INSCRATCH(mstate, size)) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
			break;
		}

		if (dtrace_memstr_max != 0 && size > dtrace_memstr_max) {
			*flags |= CPU_DTRACE_ILLOP;
			break;
		}

		for (i = 0; i < size - 1; i++) {
			n = dtrace_load8(mem++);
			str[i] = (n == 0) ? c : n;
		}
		str[size - 1] = 0;

		regs[rd] = (uintptr_t)str;
		mstate->dtms_scratch_ptr += size;
		break;
	}
#endif
	}
}

/*
 * Emulate the execution of DTrace IR instructions specified by the given
 * DIF object.  This function is deliberately void of assertions as all of
 * the necessary checks are handled by a call to dtrace_difo_validate().
 */
static uint64_t
dtrace_dif_emulate(dtrace_difo_t *difo, dtrace_mstate_t *mstate,
    dtrace_vstate_t *vstate, dtrace_state_t *state)
{
	const dif_instr_t *text = difo->dtdo_buf;
	const uint_t textlen = difo->dtdo_len;
	const char *strtab = difo->dtdo_strtab;
	const uint64_t *inttab = difo->dtdo_inttab;

	uint64_t rval = 0;
	dtrace_statvar_t *svar;
	dtrace_dstate_t *dstate = &vstate->dtvs_dynvars;
	dtrace_difv_t *v;
	volatile uint16_t *flags = &cpu_core[curcpu].cpuc_dtrace_flags;
	volatile uintptr_t *illval = &cpu_core[curcpu].cpuc_dtrace_illval;

	dtrace_key_t tupregs[DIF_DTR_NREGS + 2]; /* +2 for thread and id */
	uint64_t regs[DIF_DIR_NREGS];
	uint64_t *tmp;

	uint8_t cc_n = 0, cc_z = 0, cc_v = 0, cc_c = 0;
	int64_t cc_r;
	uint_t pc = 0, id, opc = 0;
	uint8_t ttop = 0;
	dif_instr_t instr;
	uint_t r1, r2, rd;

	/*
	 * We stash the current DIF object into the machine state: we need it
	 * for subsequent access checking.
	 */
	mstate->dtms_difo = difo;

	regs[DIF_REG_R0] = 0; 		/* %r0 is fixed at zero */

	while (pc < textlen && !(*flags & CPU_DTRACE_FAULT)) {
		opc = pc;

		instr = text[pc++];
		r1 = DIF_INSTR_R1(instr);
		r2 = DIF_INSTR_R2(instr);
		rd = DIF_INSTR_RD(instr);

		switch (DIF_INSTR_OP(instr)) {
		case DIF_OP_OR:
			regs[rd] = regs[r1] | regs[r2];
			break;
		case DIF_OP_XOR:
			regs[rd] = regs[r1] ^ regs[r2];
			break;
		case DIF_OP_AND:
			regs[rd] = regs[r1] & regs[r2];
			break;
		case DIF_OP_SLL:
			regs[rd] = regs[r1] << regs[r2];
			break;
		case DIF_OP_SRL:
			regs[rd] = regs[r1] >> regs[r2];
			break;
		case DIF_OP_SUB:
			regs[rd] = regs[r1] - regs[r2];
			break;
		case DIF_OP_ADD:
			regs[rd] = regs[r1] + regs[r2];
			break;
		case DIF_OP_MUL:
			regs[rd] = regs[r1] * regs[r2];
			break;
		case DIF_OP_SDIV:
			if (regs[r2] == 0) {
				regs[rd] = 0;
				*flags |= CPU_DTRACE_DIVZERO;
			} else {
				regs[rd] = (int64_t)regs[r1] /
				    (int64_t)regs[r2];
			}
			break;

		case DIF_OP_UDIV:
			if (regs[r2] == 0) {
				regs[rd] = 0;
				*flags |= CPU_DTRACE_DIVZERO;
			} else {
				regs[rd] = regs[r1] / regs[r2];
			}
			break;

		case DIF_OP_SREM:
			if (regs[r2] == 0) {
				regs[rd] = 0;
				*flags |= CPU_DTRACE_DIVZERO;
			} else {
				regs[rd] = (int64_t)regs[r1] %
				    (int64_t)regs[r2];
			}
			break;

		case DIF_OP_UREM:
			if (regs[r2] == 0) {
				regs[rd] = 0;
				*flags |= CPU_DTRACE_DIVZERO;
			} else {
				regs[rd] = regs[r1] % regs[r2];
			}
			break;

		case DIF_OP_NOT:
			regs[rd] = ~regs[r1];
			break;
		case DIF_OP_MOV:
			regs[rd] = regs[r1];
			break;
		case DIF_OP_CMP:
			cc_r = regs[r1] - regs[r2];
			cc_n = cc_r < 0;
			cc_z = cc_r == 0;
			cc_v = 0;
			cc_c = regs[r1] < regs[r2];
			break;
		case DIF_OP_TST:
			cc_n = cc_v = cc_c = 0;
			cc_z = regs[r1] == 0;
			break;
		case DIF_OP_BA:
			pc = DIF_INSTR_LABEL(instr);
			break;
		case DIF_OP_BE:
			if (cc_z)
				pc = DIF_INSTR_LABEL(instr);
			break;
		case DIF_OP_BNE:
			if (cc_z == 0)
				pc = DIF_INSTR_LABEL(instr);
			break;
		case DIF_OP_BG:
			if ((cc_z | (cc_n ^ cc_v)) == 0)
				pc = DIF_INSTR_LABEL(instr);
			break;
		case DIF_OP_BGU:
			if ((cc_c | cc_z) == 0)
				pc = DIF_INSTR_LABEL(instr);
			break;
		case DIF_OP_BGE:
			if ((cc_n ^ cc_v) == 0)
				pc = DIF_INSTR_LABEL(instr);
			break;
		case DIF_OP_BGEU:
			if (cc_c == 0)
				pc = DIF_INSTR_LABEL(instr);
			break;
		case DIF_OP_BL:
			if (cc_n ^ cc_v)
				pc = DIF_INSTR_LABEL(instr);
			break;
		case DIF_OP_BLU:
			if (cc_c)
				pc = DIF_INSTR_LABEL(instr);
			break;
		case DIF_OP_BLE:
			if (cc_z | (cc_n ^ cc_v))
				pc = DIF_INSTR_LABEL(instr);
			break;
		case DIF_OP_BLEU:
			if (cc_c | cc_z)
				pc = DIF_INSTR_LABEL(instr);
			break;
		case DIF_OP_RLDSB:
			if (!dtrace_canload(regs[r1], 1, mstate, vstate))
				break;
			/*FALLTHROUGH*/
		case DIF_OP_LDSB:
			regs[rd] = (int8_t)dtrace_load8(regs[r1]);
			break;
		case DIF_OP_RLDSH:
			if (!dtrace_canload(regs[r1], 2, mstate, vstate))
				break;
			/*FALLTHROUGH*/
		case DIF_OP_LDSH:
			regs[rd] = (int16_t)dtrace_load16(regs[r1]);
			break;
		case DIF_OP_RLDSW:
			if (!dtrace_canload(regs[r1], 4, mstate, vstate))
				break;
			/*FALLTHROUGH*/
		case DIF_OP_LDSW:
			regs[rd] = (int32_t)dtrace_load32(regs[r1]);
			break;
		case DIF_OP_RLDUB:
			if (!dtrace_canload(regs[r1], 1, mstate, vstate))
				break;
			/*FALLTHROUGH*/
		case DIF_OP_LDUB:
			regs[rd] = dtrace_load8(regs[r1]);
			break;
		case DIF_OP_RLDUH:
			if (!dtrace_canload(regs[r1], 2, mstate, vstate))
				break;
			/*FALLTHROUGH*/
		case DIF_OP_LDUH:
			regs[rd] = dtrace_load16(regs[r1]);
			break;
		case DIF_OP_RLDUW:
			if (!dtrace_canload(regs[r1], 4, mstate, vstate))
				break;
			/*FALLTHROUGH*/
		case DIF_OP_LDUW:
			regs[rd] = dtrace_load32(regs[r1]);
			break;
		case DIF_OP_RLDX:
			if (!dtrace_canload(regs[r1], 8, mstate, vstate))
				break;
			/*FALLTHROUGH*/
		case DIF_OP_LDX:
			regs[rd] = dtrace_load64(regs[r1]);
			break;
		case DIF_OP_ULDSB:
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
			regs[rd] = (int8_t)
			    dtrace_fuword8((void *)(uintptr_t)regs[r1]);
			DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
			break;
		case DIF_OP_ULDSH:
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
			regs[rd] = (int16_t)
			    dtrace_fuword16((void *)(uintptr_t)regs[r1]);
			DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
			break;
		case DIF_OP_ULDSW:
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
			regs[rd] = (int32_t)
			    dtrace_fuword32((void *)(uintptr_t)regs[r1]);
			DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
			break;
		case DIF_OP_ULDUB:
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
			regs[rd] =
			    dtrace_fuword8((void *)(uintptr_t)regs[r1]);
			DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
			break;
		case DIF_OP_ULDUH:
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
			regs[rd] =
			    dtrace_fuword16((void *)(uintptr_t)regs[r1]);
			DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
			break;
		case DIF_OP_ULDUW:
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
			regs[rd] =
			    dtrace_fuword32((void *)(uintptr_t)regs[r1]);
			DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
			break;
		case DIF_OP_ULDX:
			DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
			regs[rd] =
			    dtrace_fuword64((void *)(uintptr_t)regs[r1]);
			DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
			break;
		case DIF_OP_RET:
			rval = regs[rd];
			pc = textlen;
			break;
		case DIF_OP_NOP:
			break;
		case DIF_OP_SETX:
			regs[rd] = inttab[DIF_INSTR_INTEGER(instr)];
			break;
		case DIF_OP_SETS:
			regs[rd] = (uint64_t)(uintptr_t)
			    (strtab + DIF_INSTR_STRING(instr));
			break;
		case DIF_OP_SCMP: {
			size_t sz = state->dts_options[DTRACEOPT_STRSIZE];
			uintptr_t s1 = regs[r1];
			uintptr_t s2 = regs[r2];
			size_t lim1, lim2;

			if (s1 != 0 &&
			    !dtrace_strcanload(s1, sz, &lim1, mstate, vstate))
				break;
			if (s2 != 0 &&
			    !dtrace_strcanload(s2, sz, &lim2, mstate, vstate))
				break;

			cc_r = dtrace_strncmp((char *)s1, (char *)s2,
			    MIN(lim1, lim2));

			cc_n = cc_r < 0;
			cc_z = cc_r == 0;
			cc_v = cc_c = 0;
			break;
		}
		case DIF_OP_LDGA:
			regs[rd] = dtrace_dif_variable(mstate, state,
			    r1, regs[r2]);
			break;
		case DIF_OP_LDGS:
			id = DIF_INSTR_VAR(instr);

			if (id >= DIF_VAR_OTHER_UBASE) {
				uintptr_t a;

				id -= DIF_VAR_OTHER_UBASE;
				svar = vstate->dtvs_globals[id];
				ASSERT(svar != NULL);
				v = &svar->dtsv_var;

				if (!(v->dtdv_type.dtdt_flags & DIF_TF_BYREF)) {
					regs[rd] = svar->dtsv_data;
					break;
				}

				a = (uintptr_t)svar->dtsv_data;

				if (*(uint8_t *)a == UINT8_MAX) {
					/*
					 * If the 0th byte is set to UINT8_MAX
					 * then this is to be treated as a
					 * reference to a NULL variable.
					 */
					regs[rd] = 0;
				} else {
					regs[rd] = a + sizeof (uint64_t);
				}

				break;
			}

			regs[rd] = dtrace_dif_variable(mstate, state, id, 0);
			break;

		case DIF_OP_STGS:
			id = DIF_INSTR_VAR(instr);

			ASSERT(id >= DIF_VAR_OTHER_UBASE);
			id -= DIF_VAR_OTHER_UBASE;

			VERIFY(id < vstate->dtvs_nglobals);
			svar = vstate->dtvs_globals[id];
			ASSERT(svar != NULL);
			v = &svar->dtsv_var;

			if (v->dtdv_type.dtdt_flags & DIF_TF_BYREF) {
				uintptr_t a = (uintptr_t)svar->dtsv_data;
				size_t lim;

				ASSERT(a != 0);
				ASSERT(svar->dtsv_size != 0);

				if (regs[rd] == 0) {
					*(uint8_t *)a = UINT8_MAX;
					break;
				} else {
					*(uint8_t *)a = 0;
					a += sizeof (uint64_t);
				}
				if (!dtrace_vcanload(
				    (void *)(uintptr_t)regs[rd], &v->dtdv_type,
				    &lim, mstate, vstate))
					break;

				dtrace_vcopy((void *)(uintptr_t)regs[rd],
				    (void *)a, &v->dtdv_type, lim);
				break;
			}

			svar->dtsv_data = regs[rd];
			break;

		case DIF_OP_LDTA:
			/*
			 * There are no DTrace built-in thread-local arrays at
			 * present.  This opcode is saved for future work.
			 */
			*flags |= CPU_DTRACE_ILLOP;
			regs[rd] = 0;
			break;

		case DIF_OP_LDLS:
			id = DIF_INSTR_VAR(instr);

			if (id < DIF_VAR_OTHER_UBASE) {
				/*
				 * For now, this has no meaning.
				 */
				regs[rd] = 0;
				break;
			}

			id -= DIF_VAR_OTHER_UBASE;

			ASSERT(id < vstate->dtvs_nlocals);
			ASSERT(vstate->dtvs_locals != NULL);

			svar = vstate->dtvs_locals[id];
			ASSERT(svar != NULL);
			v = &svar->dtsv_var;

			if (v->dtdv_type.dtdt_flags & DIF_TF_BYREF) {
				uintptr_t a = (uintptr_t)svar->dtsv_data;
				size_t sz = v->dtdv_type.dtdt_size;
				size_t lim;

				sz += sizeof (uint64_t);
				ASSERT(svar->dtsv_size == NCPU * sz);
				a += curcpu * sz;

				if (*(uint8_t *)a == UINT8_MAX) {
					/*
					 * If the 0th byte is set to UINT8_MAX
					 * then this is to be treated as a
					 * reference to a NULL variable.
					 */
					regs[rd] = 0;
				} else {
					regs[rd] = a + sizeof (uint64_t);
				}

				break;
			}

			ASSERT(svar->dtsv_size == NCPU * sizeof (uint64_t));
			tmp = (uint64_t *)(uintptr_t)svar->dtsv_data;
			regs[rd] = tmp[curcpu];
			break;

		case DIF_OP_STLS:
			id = DIF_INSTR_VAR(instr);

			ASSERT(id >= DIF_VAR_OTHER_UBASE);
			id -= DIF_VAR_OTHER_UBASE;
			VERIFY(id < vstate->dtvs_nlocals);

			ASSERT(vstate->dtvs_locals != NULL);
			svar = vstate->dtvs_locals[id];
			ASSERT(svar != NULL);
			v = &svar->dtsv_var;

			if (v->dtdv_type.dtdt_flags & DIF_TF_BYREF) {
				uintptr_t a = (uintptr_t)svar->dtsv_data;
				size_t sz = v->dtdv_type.dtdt_size;
				size_t lim;

				sz += sizeof (uint64_t);
				ASSERT(svar->dtsv_size == NCPU * sz);
				a += curcpu * sz;

				if (regs[rd] == 0) {
					*(uint8_t *)a = UINT8_MAX;
					break;
				} else {
					*(uint8_t *)a = 0;
					a += sizeof (uint64_t);
				}

				if (!dtrace_vcanload(
				    (void *)(uintptr_t)regs[rd], &v->dtdv_type,
				    &lim, mstate, vstate))
					break;

				dtrace_vcopy((void *)(uintptr_t)regs[rd],
				    (void *)a, &v->dtdv_type, lim);
				break;
			}

			ASSERT(svar->dtsv_size == NCPU * sizeof (uint64_t));
			tmp = (uint64_t *)(uintptr_t)svar->dtsv_data;
			tmp[curcpu] = regs[rd];
			break;

		case DIF_OP_LDTS: {
			dtrace_dynvar_t *dvar;
			dtrace_key_t *key;

			id = DIF_INSTR_VAR(instr);
			ASSERT(id >= DIF_VAR_OTHER_UBASE);
			id -= DIF_VAR_OTHER_UBASE;
			v = &vstate->dtvs_tlocals[id];

			key = &tupregs[DIF_DTR_NREGS];
			key[0].dttk_value = (uint64_t)id;
			key[0].dttk_size = 0;
			DTRACE_TLS_THRKEY(key[1].dttk_value);
			key[1].dttk_size = 0;

			dvar = dtrace_dynvar(dstate, 2, key,
			    sizeof (uint64_t), DTRACE_DYNVAR_NOALLOC,
			    mstate, vstate);

			if (dvar == NULL) {
				regs[rd] = 0;
				break;
			}

			if (v->dtdv_type.dtdt_flags & DIF_TF_BYREF) {
				regs[rd] = (uint64_t)(uintptr_t)dvar->dtdv_data;
			} else {
				regs[rd] = *((uint64_t *)dvar->dtdv_data);
			}

			break;
		}

		case DIF_OP_STTS: {
			dtrace_dynvar_t *dvar;
			dtrace_key_t *key;

			id = DIF_INSTR_VAR(instr);
			ASSERT(id >= DIF_VAR_OTHER_UBASE);
			id -= DIF_VAR_OTHER_UBASE;
			VERIFY(id < vstate->dtvs_ntlocals);

			key = &tupregs[DIF_DTR_NREGS];
			key[0].dttk_value = (uint64_t)id;
			key[0].dttk_size = 0;
			DTRACE_TLS_THRKEY(key[1].dttk_value);
			key[1].dttk_size = 0;
			v = &vstate->dtvs_tlocals[id];

			dvar = dtrace_dynvar(dstate, 2, key,
			    v->dtdv_type.dtdt_size > sizeof (uint64_t) ?
			    v->dtdv_type.dtdt_size : sizeof (uint64_t),
			    regs[rd] ? DTRACE_DYNVAR_ALLOC :
			    DTRACE_DYNVAR_DEALLOC, mstate, vstate);

			/*
			 * Given that we're storing to thread-local data,
			 * we need to flush our predicate cache.
			 */
			curthread->t_predcache = 0;

			if (dvar == NULL)
				break;

			if (v->dtdv_type.dtdt_flags & DIF_TF_BYREF) {
				size_t lim;

				if (!dtrace_vcanload(
				    (void *)(uintptr_t)regs[rd],
				    &v->dtdv_type, &lim, mstate, vstate))
					break;

				dtrace_vcopy((void *)(uintptr_t)regs[rd],
				    dvar->dtdv_data, &v->dtdv_type, lim);
			} else {
				*((uint64_t *)dvar->dtdv_data) = regs[rd];
			}

			break;
		}

		case DIF_OP_SRA:
			regs[rd] = (int64_t)regs[r1] >> regs[r2];
			break;

		case DIF_OP_CALL:
			dtrace_dif_subr(DIF_INSTR_SUBR(instr), rd,
			    regs, tupregs, ttop, mstate, state);
			break;

		case DIF_OP_PUSHTR:
			if (ttop == DIF_DTR_NREGS) {
				*flags |= CPU_DTRACE_TUPOFLOW;
				break;
			}

			if (r1 == DIF_TYPE_STRING) {
				/*
				 * If this is a string type and the size is 0,
				 * we'll use the system-wide default string
				 * size.  Note that we are _not_ looking at
				 * the value of the DTRACEOPT_STRSIZE option;
				 * had this been set, we would expect to have
				 * a non-zero size value in the "pushtr".
				 */
				tupregs[ttop].dttk_size =
				    dtrace_strlen((char *)(uintptr_t)regs[rd],
				    regs[r2] ? regs[r2] :
				    dtrace_strsize_default) + 1;
			} else {
				if (regs[r2] > LONG_MAX) {
					*flags |= CPU_DTRACE_ILLOP;
					break;
				}

				tupregs[ttop].dttk_size = regs[r2];
			}

			tupregs[ttop++].dttk_value = regs[rd];
			break;

		case DIF_OP_PUSHTV:
			if (ttop == DIF_DTR_NREGS) {
				*flags |= CPU_DTRACE_TUPOFLOW;
				break;
			}

			tupregs[ttop].dttk_value = regs[rd];
			tupregs[ttop++].dttk_size = 0;
			break;

		case DIF_OP_POPTS:
			if (ttop != 0)
				ttop--;
			break;

		case DIF_OP_FLUSHTS:
			ttop = 0;
			break;

		case DIF_OP_LDGAA:
		case DIF_OP_LDTAA: {
			dtrace_dynvar_t *dvar;
			dtrace_key_t *key = tupregs;
			uint_t nkeys = ttop;

			id = DIF_INSTR_VAR(instr);
			ASSERT(id >= DIF_VAR_OTHER_UBASE);
			id -= DIF_VAR_OTHER_UBASE;

			key[nkeys].dttk_value = (uint64_t)id;
			key[nkeys++].dttk_size = 0;

			if (DIF_INSTR_OP(instr) == DIF_OP_LDTAA) {
				DTRACE_TLS_THRKEY(key[nkeys].dttk_value);
				key[nkeys++].dttk_size = 0;
				VERIFY(id < vstate->dtvs_ntlocals);
				v = &vstate->dtvs_tlocals[id];
			} else {
				VERIFY(id < vstate->dtvs_nglobals);
				v = &vstate->dtvs_globals[id]->dtsv_var;
			}

			dvar = dtrace_dynvar(dstate, nkeys, key,
			    v->dtdv_type.dtdt_size > sizeof (uint64_t) ?
			    v->dtdv_type.dtdt_size : sizeof (uint64_t),
			    DTRACE_DYNVAR_NOALLOC, mstate, vstate);

			if (dvar == NULL) {
				regs[rd] = 0;
				break;
			}

			if (v->dtdv_type.dtdt_flags & DIF_TF_BYREF) {
				regs[rd] = (uint64_t)(uintptr_t)dvar->dtdv_data;
			} else {
				regs[rd] = *((uint64_t *)dvar->dtdv_data);
			}

			break;
		}

		case DIF_OP_STGAA:
		case DIF_OP_STTAA: {
			dtrace_dynvar_t *dvar;
			dtrace_key_t *key = tupregs;
			uint_t nkeys = ttop;

			id = DIF_INSTR_VAR(instr);
			ASSERT(id >= DIF_VAR_OTHER_UBASE);
			id -= DIF_VAR_OTHER_UBASE;

			key[nkeys].dttk_value = (uint64_t)id;
			key[nkeys++].dttk_size = 0;

			if (DIF_INSTR_OP(instr) == DIF_OP_STTAA) {
				DTRACE_TLS_THRKEY(key[nkeys].dttk_value);
				key[nkeys++].dttk_size = 0;
				VERIFY(id < vstate->dtvs_ntlocals);
				v = &vstate->dtvs_tlocals[id];
			} else {
				VERIFY(id < vstate->dtvs_nglobals);
				v = &vstate->dtvs_globals[id]->dtsv_var;
			}

			dvar = dtrace_dynvar(dstate, nkeys, key,
			    v->dtdv_type.dtdt_size > sizeof (uint64_t) ?
			    v->dtdv_type.dtdt_size : sizeof (uint64_t),
			    regs[rd] ? DTRACE_DYNVAR_ALLOC :
			    DTRACE_DYNVAR_DEALLOC, mstate, vstate);

			if (dvar == NULL)
				break;

			if (v->dtdv_type.dtdt_flags & DIF_TF_BYREF) {
				size_t lim;

				if (!dtrace_vcanload(
				    (void *)(uintptr_t)regs[rd], &v->dtdv_type,
				    &lim, mstate, vstate))
					break;

				dtrace_vcopy((void *)(uintptr_t)regs[rd],
				    dvar->dtdv_data, &v->dtdv_type, lim);
			} else {
				*((uint64_t *)dvar->dtdv_data) = regs[rd];
			}

			break;
		}

		case DIF_OP_ALLOCS: {
			uintptr_t ptr = P2ROUNDUP(mstate->dtms_scratch_ptr, 8);
			size_t size = ptr - mstate->dtms_scratch_ptr + regs[r1];

			/*
			 * Rounding up the user allocation size could have
			 * overflowed large, bogus allocations (like -1ULL) to
			 * 0.
			 */
			if (size < regs[r1] ||
			    !DTRACE_INSCRATCH(mstate, size)) {
				DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
				regs[rd] = 0;
				break;
			}

			dtrace_bzero((void *) mstate->dtms_scratch_ptr, size);
			mstate->dtms_scratch_ptr += size;
			regs[rd] = ptr;
			break;
		}

		case DIF_OP_COPYS:
			if (!dtrace_canstore(regs[rd], regs[r2],
			    mstate, vstate)) {
				*flags |= CPU_DTRACE_BADADDR;
				*illval = regs[rd];
				break;
			}

			if (!dtrace_canload(regs[r1], regs[r2], mstate, vstate))
				break;

			dtrace_bcopy((void *)(uintptr_t)regs[r1],
			    (void *)(uintptr_t)regs[rd], (size_t)regs[r2]);
			break;

		case DIF_OP_STB:
			if (!dtrace_canstore(regs[rd], 1, mstate, vstate)) {
				*flags |= CPU_DTRACE_BADADDR;
				*illval = regs[rd];
				break;
			}
			*((uint8_t *)(uintptr_t)regs[rd]) = (uint8_t)regs[r1];
			break;

		case DIF_OP_STH:
			if (!dtrace_canstore(regs[rd], 2, mstate, vstate)) {
				*flags |= CPU_DTRACE_BADADDR;
				*illval = regs[rd];
				break;
			}
			if (regs[rd] & 1) {
				*flags |= CPU_DTRACE_BADALIGN;
				*illval = regs[rd];
				break;
			}
			*((uint16_t *)(uintptr_t)regs[rd]) = (uint16_t)regs[r1];
			break;

		case DIF_OP_STW:
			if (!dtrace_canstore(regs[rd], 4, mstate, vstate)) {
				*flags |= CPU_DTRACE_BADADDR;
				*illval = regs[rd];
				break;
			}
			if (regs[rd] & 3) {
				*flags |= CPU_DTRACE_BADALIGN;
				*illval = regs[rd];
				break;
			}
			*((uint32_t *)(uintptr_t)regs[rd]) = (uint32_t)regs[r1];
			break;

		case DIF_OP_STX:
			if (!dtrace_canstore(regs[rd], 8, mstate, vstate)) {
				*flags |= CPU_DTRACE_BADADDR;
				*illval = regs[rd];
				break;
			}
			if (regs[rd] & 7) {
				*flags |= CPU_DTRACE_BADALIGN;
				*illval = regs[rd];
				break;
			}
			*((uint64_t *)(uintptr_t)regs[rd]) = regs[r1];
			break;
		}
	}

	if (!(*flags & CPU_DTRACE_FAULT))
		return (rval);

	mstate->dtms_fltoffs = opc * sizeof (dif_instr_t);
	mstate->dtms_present |= DTRACE_MSTATE_FLTOFFS;

	return (0);
}

static void
dtrace_action_breakpoint(dtrace_ecb_t *ecb)
{
	dtrace_probe_t *probe = ecb->dte_probe;
	dtrace_provider_t *prov = probe->dtpr_provider;
	char c[DTRACE_FULLNAMELEN + 80], *str;
	char *msg = "dtrace: breakpoint action at probe ";
	char *ecbmsg = " (ecb ";
	uintptr_t mask = (0xf << (sizeof (uintptr_t) * NBBY / 4));
	uintptr_t val = (uintptr_t)ecb;
	int shift = (sizeof (uintptr_t) * NBBY) - 4, i = 0;

	if (dtrace_destructive_disallow)
		return;

	/*
	 * It's impossible to be taking action on the NULL probe.
	 */
	ASSERT(probe != NULL);

	/*
	 * This is a poor man's (destitute man's?) sprintf():  we want to
	 * print the provider name, module name, function name and name of
	 * the probe, along with the hex address of the ECB with the breakpoint
	 * action -- all of which we must place in the character buffer by
	 * hand.
	 */
	while (*msg != '\0')
		c[i++] = *msg++;

	for (str = prov->dtpv_name; *str != '\0'; str++)
		c[i++] = *str;
	c[i++] = ':';

	for (str = probe->dtpr_mod; *str != '\0'; str++)
		c[i++] = *str;
	c[i++] = ':';

	for (str = probe->dtpr_func; *str != '\0'; str++)
		c[i++] = *str;
	c[i++] = ':';

	for (str = probe->dtpr_name; *str != '\0'; str++)
		c[i++] = *str;

	while (*ecbmsg != '\0')
		c[i++] = *ecbmsg++;

	while (shift >= 0) {
		mask = (uintptr_t)0xf << shift;

		if (val >= ((uintptr_t)1 << shift))
			c[i++] = "0123456789abcdef"[(val & mask) >> shift];
		shift -= 4;
	}

	c[i++] = ')';
	c[i] = '\0';

#ifdef illumos
	debug_enter(c);
#else
	kdb_enter(KDB_WHY_DTRACE, "breakpoint action");
#endif
}

static void
dtrace_action_panic(dtrace_ecb_t *ecb)
{
	dtrace_probe_t *probe = ecb->dte_probe;

	/*
	 * It's impossible to be taking action on the NULL probe.
	 */
	ASSERT(probe != NULL);

	if (dtrace_destructive_disallow)
		return;

	if (dtrace_panicked != NULL)
		return;

	if (dtrace_casptr(&dtrace_panicked, NULL, curthread) != NULL)
		return;

	/*
	 * We won the right to panic.  (We want to be sure that only one
	 * thread calls panic() from dtrace_probe(), and that panic() is
	 * called exactly once.)
	 */
	dtrace_panic("dtrace: panic action at probe %s:%s:%s:%s (ecb %p)",
	    probe->dtpr_provider->dtpv_name, probe->dtpr_mod,
	    probe->dtpr_func, probe->dtpr_name, (void *)ecb);
}

static void
dtrace_action_raise(uint64_t sig)
{
	if (dtrace_destructive_disallow)
		return;

	if (sig >= NSIG) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
		return;
	}

#ifdef illumos
	/*
	 * raise() has a queue depth of 1 -- we ignore all subsequent
	 * invocations of the raise() action.
	 */
	if (curthread->t_dtrace_sig == 0)
		curthread->t_dtrace_sig = (uint8_t)sig;

	curthread->t_sig_check = 1;
	aston(curthread);
#else
	struct proc *p = curproc;
	PROC_LOCK(p);
	kern_psignal(p, sig);
	PROC_UNLOCK(p);
#endif
}

static void
dtrace_action_stop(void)
{
	if (dtrace_destructive_disallow)
		return;

#ifdef illumos
	if (!curthread->t_dtrace_stop) {
		curthread->t_dtrace_stop = 1;
		curthread->t_sig_check = 1;
		aston(curthread);
	}
#else
	struct proc *p = curproc;
	PROC_LOCK(p);
	kern_psignal(p, SIGSTOP);
	PROC_UNLOCK(p);
#endif
}

static void
dtrace_action_chill(dtrace_mstate_t *mstate, hrtime_t val)
{
	hrtime_t now;
	volatile uint16_t *flags;
#ifdef illumos
	cpu_t *cpu = CPU;
#else
	cpu_t *cpu = &solaris_cpu[curcpu];
#endif

	if (dtrace_destructive_disallow)
		return;

	flags = (volatile uint16_t *)&cpu_core[curcpu].cpuc_dtrace_flags;

	now = dtrace_gethrtime();

	if (now - cpu->cpu_dtrace_chillmark > dtrace_chill_interval) {
		/*
		 * We need to advance the mark to the current time.
		 */
		cpu->cpu_dtrace_chillmark = now;
		cpu->cpu_dtrace_chilled = 0;
	}

	/*
	 * Now check to see if the requested chill time would take us over
	 * the maximum amount of time allowed in the chill interval.  (Or
	 * worse, if the calculation itself induces overflow.)
	 */
	if (cpu->cpu_dtrace_chilled + val > dtrace_chill_max ||
	    cpu->cpu_dtrace_chilled + val < cpu->cpu_dtrace_chilled) {
		*flags |= CPU_DTRACE_ILLOP;
		return;
	}

	while (dtrace_gethrtime() - now < val)
		continue;

	/*
	 * Normally, we assure that the value of the variable "timestamp" does
	 * not change within an ECB.  The presence of chill() represents an
	 * exception to this rule, however.
	 */
	mstate->dtms_present &= ~DTRACE_MSTATE_TIMESTAMP;
	cpu->cpu_dtrace_chilled += val;
}

static void
dtrace_action_ustack(dtrace_mstate_t *mstate, dtrace_state_t *state,
    uint64_t *buf, uint64_t arg)
{
	int nframes = DTRACE_USTACK_NFRAMES(arg);
	int strsize = DTRACE_USTACK_STRSIZE(arg);
	uint64_t *pcs = &buf[1], *fps;
	char *str = (char *)&pcs[nframes];
	int size, offs = 0, i, j;
	size_t rem;
	uintptr_t old = mstate->dtms_scratch_ptr, saved;
	uint16_t *flags = &cpu_core[curcpu].cpuc_dtrace_flags;
	char *sym;

	/*
	 * Should be taking a faster path if string space has not been
	 * allocated.
	 */
	ASSERT(strsize != 0);

	/*
	 * We will first allocate some temporary space for the frame pointers.
	 */
	fps = (uint64_t *)P2ROUNDUP(mstate->dtms_scratch_ptr, 8);
	size = (uintptr_t)fps - mstate->dtms_scratch_ptr +
	    (nframes * sizeof (uint64_t));

	if (!DTRACE_INSCRATCH(mstate, size)) {
		/*
		 * Not enough room for our frame pointers -- need to indicate
		 * that we ran out of scratch space.
		 */
		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOSCRATCH);
		return;
	}

	mstate->dtms_scratch_ptr += size;
	saved = mstate->dtms_scratch_ptr;

	/*
	 * Now get a stack with both program counters and frame pointers.
	 */
	DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
	dtrace_getufpstack(buf, fps, nframes + 1);
	DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);

	/*
	 * If that faulted, we're cooked.
	 */
	if (*flags & CPU_DTRACE_FAULT)
		goto out;

	/*
	 * Now we want to walk up the stack, calling the USTACK helper.  For
	 * each iteration, we restore the scratch pointer.
	 */
	for (i = 0; i < nframes; i++) {
		mstate->dtms_scratch_ptr = saved;

		if (offs >= strsize)
			break;

		sym = (char *)(uintptr_t)dtrace_helper(
		    DTRACE_HELPER_ACTION_USTACK,
		    mstate, state, pcs[i], fps[i]);

		/*
		 * If we faulted while running the helper, we're going to
		 * clear the fault and null out the corresponding string.
		 */
		if (*flags & CPU_DTRACE_FAULT) {
			*flags &= ~CPU_DTRACE_FAULT;
			str[offs++] = '\0';
			continue;
		}

		if (sym == NULL) {
			str[offs++] = '\0';
			continue;
		}

		if (!dtrace_strcanload((uintptr_t)sym, strsize, &rem, mstate,
		    &(state->dts_vstate))) {
			str[offs++] = '\0';
			continue;
		}

		DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);

		/*
		 * Now copy in the string that the helper returned to us.
		 */
		for (j = 0; offs + j < strsize && j < rem; j++) {
			if ((str[offs + j] = sym[j]) == '\0')
				break;
		}

		DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);

		offs += j + 1;
	}

	if (offs >= strsize) {
		/*
		 * If we didn't have room for all of the strings, we don't
		 * abort processing -- this needn't be a fatal error -- but we
		 * still want to increment a counter (dts_stkstroverflows) to
		 * allow this condition to be warned about.  (If this is from
		 * a jstack() action, it is easily tuned via jstackstrsize.)
		 */
		dtrace_error(&state->dts_stkstroverflows);
	}

	while (offs < strsize)
		str[offs++] = '\0';

out:
	mstate->dtms_scratch_ptr = old;
}

static void
dtrace_store_by_ref(dtrace_difo_t *dp, caddr_t tomax, size_t size,
    size_t *valoffsp, uint64_t *valp, uint64_t end, int intuple, int dtkind)
{
	volatile uint16_t *flags;
	uint64_t val = *valp;
	size_t valoffs = *valoffsp;

	flags = (volatile uint16_t *)&cpu_core[curcpu].cpuc_dtrace_flags;
	ASSERT(dtkind == DIF_TF_BYREF || dtkind == DIF_TF_BYUREF);

	/*
	 * If this is a string, we're going to only load until we find the zero
	 * byte -- after which we'll store zero bytes.
	 */
	if (dp->dtdo_rtype.dtdt_kind == DIF_TYPE_STRING) {
		char c = '\0' + 1;
		size_t s;

		for (s = 0; s < size; s++) {
			if (c != '\0' && dtkind == DIF_TF_BYREF) {
				c = dtrace_load8(val++);
			} else if (c != '\0' && dtkind == DIF_TF_BYUREF) {
				DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
				c = dtrace_fuword8((void *)(uintptr_t)val++);
				DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
				if (*flags & CPU_DTRACE_FAULT)
					break;
			}

			DTRACE_STORE(uint8_t, tomax, valoffs++, c);

			if (c == '\0' && intuple)
				break;
		}
	} else {
		uint8_t c;
		while (valoffs < end) {
			if (dtkind == DIF_TF_BYREF) {
				c = dtrace_load8(val++);
			} else if (dtkind == DIF_TF_BYUREF) {
				DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
				c = dtrace_fuword8((void *)(uintptr_t)val++);
				DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
				if (*flags & CPU_DTRACE_FAULT)
					break;
			}

			DTRACE_STORE(uint8_t, tomax,
			    valoffs++, c);
		}
	}

	*valp = val;
	*valoffsp = valoffs;
}

/*
 * Disables interrupts and sets the per-thread inprobe flag. When DEBUG is
 * defined, we also assert that we are not recursing unless the probe ID is an
 * error probe.
 */
static dtrace_icookie_t
dtrace_probe_enter(dtrace_id_t id)
{
	dtrace_icookie_t cookie;

	cookie = dtrace_interrupt_disable();

	/*
	 * Unless this is an ERROR probe, we are not allowed to recurse in
	 * dtrace_probe(). Recursing into DTrace probe usually means that a
	 * function is instrumented that should not have been instrumented or
	 * that the ordering guarantee of the records will be violated,
	 * resulting in unexpected output. If there is an exception to this
	 * assertion, a new case should be added.
	 */
	ASSERT(curthread->t_dtrace_inprobe == 0 ||
	    id == dtrace_probeid_error);
	curthread->t_dtrace_inprobe = 1;

	return (cookie);
}

/*
 * Clears the per-thread inprobe flag and enables interrupts.
 */
static void
dtrace_probe_exit(dtrace_icookie_t cookie)
{

	curthread->t_dtrace_inprobe = 0;
	dtrace_interrupt_enable(cookie);
}

/*
 * If you're looking for the epicenter of DTrace, you just found it.  This
 * is the function called by the provider to fire a probe -- from which all
 * subsequent probe-context DTrace activity emanates.
 */
void
dtrace_probe(dtrace_id_t id, uintptr_t arg0, uintptr_t arg1,
    uintptr_t arg2, uintptr_t arg3, uintptr_t arg4)
{
	processorid_t cpuid;
	dtrace_icookie_t cookie;
	dtrace_probe_t *probe;
	dtrace_mstate_t mstate;
	dtrace_ecb_t *ecb;
	dtrace_action_t *act;
	intptr_t offs;
	size_t size;
	int vtime, onintr;
	volatile uint16_t *flags;
	hrtime_t now;

	if (panicstr != NULL)
		return;

#ifdef illumos
	/*
	 * Kick out immediately if this CPU is still being born (in which case
	 * curthread will be set to -1) or the current thread can't allow
	 * probes in its current context.
	 */
	if (((uintptr_t)curthread & 1) || (curthread->t_flag & T_DONTDTRACE))
		return;
#endif

	cookie = dtrace_probe_enter(id);
	probe = dtrace_probes[id - 1];
	cpuid = curcpu;
	onintr = CPU_ON_INTR(CPU);

	if (!onintr && probe->dtpr_predcache != DTRACE_CACHEIDNONE &&
	    probe->dtpr_predcache == curthread->t_predcache) {
		/*
		 * We have hit in the predicate cache; we know that
		 * this predicate would evaluate to be false.
		 */
		dtrace_probe_exit(cookie);
		return;
	}

#ifdef illumos
	if (panic_quiesce) {
#else
	if (panicstr != NULL) {
#endif
		/*
		 * We don't trace anything if we're panicking.
		 */
		dtrace_probe_exit(cookie);
		return;
	}

	now = mstate.dtms_timestamp = dtrace_gethrtime();
	mstate.dtms_present = DTRACE_MSTATE_TIMESTAMP;
	vtime = dtrace_vtime_references != 0;

	if (vtime && curthread->t_dtrace_start)
		curthread->t_dtrace_vtime += now - curthread->t_dtrace_start;

	mstate.dtms_difo = NULL;
	mstate.dtms_probe = probe;
	mstate.dtms_strtok = 0;
	mstate.dtms_arg[0] = arg0;
	mstate.dtms_arg[1] = arg1;
	mstate.dtms_arg[2] = arg2;
	mstate.dtms_arg[3] = arg3;
	mstate.dtms_arg[4] = arg4;

	flags = (volatile uint16_t *)&cpu_core[cpuid].cpuc_dtrace_flags;

	for (ecb = probe->dtpr_ecb; ecb != NULL; ecb = ecb->dte_next) {
		dtrace_predicate_t *pred = ecb->dte_predicate;
		dtrace_state_t *state = ecb->dte_state;
		dtrace_buffer_t *buf = &state->dts_buffer[cpuid];
		dtrace_buffer_t *aggbuf = &state->dts_aggbuffer[cpuid];
		dtrace_vstate_t *vstate = &state->dts_vstate;
		dtrace_provider_t *prov = probe->dtpr_provider;
		uint64_t tracememsize = 0;
		int committed = 0;
		caddr_t tomax;

		/*
		 * A little subtlety with the following (seemingly innocuous)
		 * declaration of the automatic 'val':  by looking at the
		 * code, you might think that it could be declared in the
		 * action processing loop, below.  (That is, it's only used in
		 * the action processing loop.)  However, it must be declared
		 * out of that scope because in the case of DIF expression
		 * arguments to aggregating actions, one iteration of the
		 * action loop will use the last iteration's value.
		 */
		uint64_t val = 0;

		mstate.dtms_present = DTRACE_MSTATE_ARGS | DTRACE_MSTATE_PROBE;
		mstate.dtms_getf = NULL;

		*flags &= ~CPU_DTRACE_ERROR;

		if (prov == dtrace_provider) {
			/*
			 * If dtrace itself is the provider of this probe,
			 * we're only going to continue processing the ECB if
			 * arg0 (the dtrace_state_t) is equal to the ECB's
			 * creating state.  (This prevents disjoint consumers
			 * from seeing one another's metaprobes.)
			 */
			if (arg0 != (uint64_t)(uintptr_t)state)
				continue;
		}

		if (state->dts_activity != DTRACE_ACTIVITY_ACTIVE) {
			/*
			 * We're not currently active.  If our provider isn't
			 * the dtrace pseudo provider, we're not interested.
			 */
			if (prov != dtrace_provider)
				continue;

			/*
			 * Now we must further check if we are in the BEGIN
			 * probe.  If we are, we will only continue processing
			 * if we're still in WARMUP -- if one BEGIN enabling
			 * has invoked the exit() action, we don't want to
			 * evaluate subsequent BEGIN enablings.
			 */
			if (probe->dtpr_id == dtrace_probeid_begin &&
			    state->dts_activity != DTRACE_ACTIVITY_WARMUP) {
				ASSERT(state->dts_activity ==
				    DTRACE_ACTIVITY_DRAINING);
				continue;
			}
		}

		if (ecb->dte_cond) {
			/*
			 * If the dte_cond bits indicate that this
			 * consumer is only allowed to see user-mode firings
			 * of this probe, call the provider's dtps_usermode()
			 * entry point to check that the probe was fired
			 * while in a user context. Skip this ECB if that's
			 * not the case.
			 */
			if ((ecb->dte_cond & DTRACE_COND_USERMODE) &&
			    prov->dtpv_pops.dtps_usermode(prov->dtpv_arg,
			    probe->dtpr_id, probe->dtpr_arg) == 0)
				continue;

#ifdef illumos
			/*
			 * This is more subtle than it looks. We have to be
			 * absolutely certain that CRED() isn't going to
			 * change out from under us so it's only legit to
			 * examine that structure if we're in constrained
			 * situations. Currently, the only times we'll this
			 * check is if a non-super-user has enabled the
			 * profile or syscall providers -- providers that
			 * allow visibility of all processes. For the
			 * profile case, the check above will ensure that
			 * we're examining a user context.
			 */
			if (ecb->dte_cond & DTRACE_COND_OWNER) {
				cred_t *cr;
				cred_t *s_cr =
				    ecb->dte_state->dts_cred.dcr_cred;
				proc_t *proc;

				ASSERT(s_cr != NULL);

				if ((cr = CRED()) == NULL ||
				    s_cr->cr_uid != cr->cr_uid ||
				    s_cr->cr_uid != cr->cr_ruid ||
				    s_cr->cr_uid != cr->cr_suid ||
				    s_cr->cr_gid != cr->cr_gid ||
				    s_cr->cr_gid != cr->cr_rgid ||
				    s_cr->cr_gid != cr->cr_sgid ||
				    (proc = ttoproc(curthread)) == NULL ||
				    (proc->p_flag & SNOCD))
					continue;
			}

			if (ecb->dte_cond & DTRACE_COND_ZONEOWNER) {
				cred_t *cr;
				cred_t *s_cr =
				    ecb->dte_state->dts_cred.dcr_cred;

				ASSERT(s_cr != NULL);

				if ((cr = CRED()) == NULL ||
				    s_cr->cr_zone->zone_id !=
				    cr->cr_zone->zone_id)
					continue;
			}
#endif
		}

		if (now - state->dts_alive > dtrace_deadman_timeout) {
			/*
			 * We seem to be dead.  Unless we (a) have kernel
			 * destructive permissions (b) have explicitly enabled
			 * destructive actions and (c) destructive actions have
			 * not been disabled, we're going to transition into
			 * the KILLED state, from which no further processing
			 * on this state will be performed.
			 */
			if (!dtrace_priv_kernel_destructive(state) ||
			    !state->dts_cred.dcr_destructive ||
			    dtrace_destructive_disallow) {
				void *activity = &state->dts_activity;
				dtrace_activity_t current;

				do {
					current = state->dts_activity;
				} while (dtrace_cas32(activity, current,
				    DTRACE_ACTIVITY_KILLED) != current);

				continue;
			}
		}

		if ((offs = dtrace_buffer_reserve(buf, ecb->dte_needed,
		    ecb->dte_alignment, state, &mstate)) < 0)
			continue;

		tomax = buf->dtb_tomax;
		ASSERT(tomax != NULL);

		if (ecb->dte_size != 0) {
			dtrace_rechdr_t dtrh;
			if (!(mstate.dtms_present & DTRACE_MSTATE_TIMESTAMP)) {
				mstate.dtms_timestamp = dtrace_gethrtime();
				mstate.dtms_present |= DTRACE_MSTATE_TIMESTAMP;
			}
			ASSERT3U(ecb->dte_size, >=, sizeof (dtrace_rechdr_t));
			dtrh.dtrh_epid = ecb->dte_epid;
			DTRACE_RECORD_STORE_TIMESTAMP(&dtrh,
			    mstate.dtms_timestamp);
			*((dtrace_rechdr_t *)(tomax + offs)) = dtrh;
		}

		mstate.dtms_epid = ecb->dte_epid;
		mstate.dtms_present |= DTRACE_MSTATE_EPID;

		if (state->dts_cred.dcr_visible & DTRACE_CRV_KERNEL)
			mstate.dtms_access = DTRACE_ACCESS_KERNEL;
		else
			mstate.dtms_access = 0;

		if (pred != NULL) {
			dtrace_difo_t *dp = pred->dtp_difo;
			uint64_t rval;

			rval = dtrace_dif_emulate(dp, &mstate, vstate, state);

			if (!(*flags & CPU_DTRACE_ERROR) && !rval) {
				dtrace_cacheid_t cid = probe->dtpr_predcache;

				if (cid != DTRACE_CACHEIDNONE && !onintr) {
					/*
					 * Update the predicate cache...
					 */
					ASSERT(cid == pred->dtp_cacheid);
					curthread->t_predcache = cid;
				}

				continue;
			}
		}

		for (act = ecb->dte_action; !(*flags & CPU_DTRACE_ERROR) &&
		    act != NULL; act = act->dta_next) {
			size_t valoffs;
			dtrace_difo_t *dp;
			dtrace_recdesc_t *rec = &act->dta_rec;

			size = rec->dtrd_size;
			valoffs = offs + rec->dtrd_offset;

			if (DTRACEACT_ISAGG(act->dta_kind)) {
				uint64_t v = 0xbad;
				dtrace_aggregation_t *agg;

				agg = (dtrace_aggregation_t *)act;

				if ((dp = act->dta_difo) != NULL)
					v = dtrace_dif_emulate(dp,
					    &mstate, vstate, state);

				if (*flags & CPU_DTRACE_ERROR)
					continue;

				/*
				 * Note that we always pass the expression
				 * value from the previous iteration of the
				 * action loop.  This value will only be used
				 * if there is an expression argument to the
				 * aggregating action, denoted by the
				 * dtag_hasarg field.
				 */
				dtrace_aggregate(agg, buf,
				    offs, aggbuf, v, val);
				continue;
			}

			switch (act->dta_kind) {
			case DTRACEACT_STOP:
				if (dtrace_priv_proc_destructive(state))
					dtrace_action_stop();
				continue;

			case DTRACEACT_BREAKPOINT:
				if (dtrace_priv_kernel_destructive(state))
					dtrace_action_breakpoint(ecb);
				continue;

			case DTRACEACT_PANIC:
				if (dtrace_priv_kernel_destructive(state))
					dtrace_action_panic(ecb);
				continue;

			case DTRACEACT_STACK:
				if (!dtrace_priv_kernel(state))
					continue;

				dtrace_getpcstack((pc_t *)(tomax + valoffs),
				    size / sizeof (pc_t), probe->dtpr_aframes,
				    DTRACE_ANCHORED(probe) ? NULL :
				    (uint32_t *)arg0);
				continue;

			case DTRACEACT_JSTACK:
			case DTRACEACT_USTACK:
				if (!dtrace_priv_proc(state))
					continue;

				/*
				 * See comment in DIF_VAR_PID.
				 */
				if (DTRACE_ANCHORED(mstate.dtms_probe) &&
				    CPU_ON_INTR(CPU)) {
					int depth = DTRACE_USTACK_NFRAMES(
					    rec->dtrd_arg) + 1;

					dtrace_bzero((void *)(tomax + valoffs),
					    DTRACE_USTACK_STRSIZE(rec->dtrd_arg)
					    + depth * sizeof (uint64_t));

					continue;
				}

				if (DTRACE_USTACK_STRSIZE(rec->dtrd_arg) != 0 &&
				    curproc->p_dtrace_helpers != NULL) {
					/*
					 * This is the slow path -- we have
					 * allocated string space, and we're
					 * getting the stack of a process that
					 * has helpers.  Call into a separate
					 * routine to perform this processing.
					 */
					dtrace_action_ustack(&mstate, state,
					    (uint64_t *)(tomax + valoffs),
					    rec->dtrd_arg);
					continue;
				}

				DTRACE_CPUFLAG_SET(CPU_DTRACE_NOFAULT);
				dtrace_getupcstack((uint64_t *)
				    (tomax + valoffs),
				    DTRACE_USTACK_NFRAMES(rec->dtrd_arg) + 1);
				DTRACE_CPUFLAG_CLEAR(CPU_DTRACE_NOFAULT);
				continue;

			default:
				break;
			}

			dp = act->dta_difo;
			ASSERT(dp != NULL);

			val = dtrace_dif_emulate(dp, &mstate, vstate, state);

			if (*flags & CPU_DTRACE_ERROR)
				continue;

			switch (act->dta_kind) {
			case DTRACEACT_SPECULATE: {
				dtrace_rechdr_t *dtrh;

				ASSERT(buf == &state->dts_buffer[cpuid]);
				buf = dtrace_speculation_buffer(state,
				    cpuid, val);

				if (buf == NULL) {
					*flags |= CPU_DTRACE_DROP;
					continue;
				}

				offs = dtrace_buffer_reserve(buf,
				    ecb->dte_needed, ecb->dte_alignment,
				    state, NULL);

				if (offs < 0) {
					*flags |= CPU_DTRACE_DROP;
					continue;
				}

				tomax = buf->dtb_tomax;
				ASSERT(tomax != NULL);

				if (ecb->dte_size == 0)
					continue;

				ASSERT3U(ecb->dte_size, >=,
				    sizeof (dtrace_rechdr_t));
				dtrh = ((void *)(tomax + offs));
				dtrh->dtrh_epid = ecb->dte_epid;
				/*
				 * When the speculation is committed, all of
				 * the records in the speculative buffer will
				 * have their timestamps set to the commit
				 * time.  Until then, it is set to a sentinel
				 * value, for debugability.
				 */
				DTRACE_RECORD_STORE_TIMESTAMP(dtrh, UINT64_MAX);
				continue;
			}

			case DTRACEACT_PRINTM: {
				/* The DIF returns a 'memref'. */
				uintptr_t *memref = (uintptr_t *)(uintptr_t) val;

				/* Get the size from the memref. */
				size = memref[1];

				/*
				 * Check if the size exceeds the allocated
				 * buffer size.
				 */
				if (size + sizeof(uintptr_t) > dp->dtdo_rtype.dtdt_size) {
					/* Flag a drop! */
					*flags |= CPU_DTRACE_DROP;
					continue;
				}

				/* Store the size in the buffer first. */
				DTRACE_STORE(uintptr_t, tomax,
				    valoffs, size);

				/*
				 * Offset the buffer address to the start
				 * of the data.
				 */
				valoffs += sizeof(uintptr_t);

				/*
				 * Reset to the memory address rather than
				 * the memref array, then let the BYREF
				 * code below do the work to store the 
				 * memory data in the buffer.
				 */
				val = memref[0];
				break;
			}

			case DTRACEACT_CHILL:
				if (dtrace_priv_kernel_destructive(state))
					dtrace_action_chill(&mstate, val);
				continue;

			case DTRACEACT_RAISE:
				if (dtrace_priv_proc_destructive(state))
					dtrace_action_raise(val);
				continue;

			case DTRACEACT_COMMIT:
				ASSERT(!committed);

				/*
				 * We need to commit our buffer state.
				 */
				if (ecb->dte_size)
					buf->dtb_offset = offs + ecb->dte_size;
				buf = &state->dts_buffer[cpuid];
				dtrace_speculation_commit(state, cpuid, val);
				committed = 1;
				continue;

			case DTRACEACT_DISCARD:
				dtrace_speculation_discard(state, cpuid, val);
				continue;

			case DTRACEACT_DIFEXPR:
			case DTRACEACT_LIBACT:
			case DTRACEACT_PRINTF:
			case DTRACEACT_PRINTA:
			case DTRACEACT_SYSTEM:
			case DTRACEACT_FREOPEN:
			case DTRACEACT_TRACEMEM:
				break;

			case DTRACEACT_TRACEMEM_DYNSIZE:
				tracememsize = val;
				break;

			case DTRACEACT_SYM:
			case DTRACEACT_MOD:
				if (!dtrace_priv_kernel(state))
					continue;
				break;

			case DTRACEACT_USYM:
			case DTRACEACT_UMOD:
			case DTRACEACT_UADDR: {
#ifdef illumos
				struct pid *pid = curthread->t_procp->p_pidp;
#endif

				if (!dtrace_priv_proc(state))
					continue;

				DTRACE_STORE(uint64_t, tomax,
#ifdef illumos
				    valoffs, (uint64_t)pid->pid_id);
#else
				    valoffs, (uint64_t) curproc->p_pid);
#endif
				DTRACE_STORE(uint64_t, tomax,
				    valoffs + sizeof (uint64_t), val);

				continue;
			}

			case DTRACEACT_EXIT: {
				/*
				 * For the exit action, we are going to attempt
				 * to atomically set our activity to be
				 * draining.  If this fails (either because
				 * another CPU has beat us to the exit action,
				 * or because our current activity is something
				 * other than ACTIVE or WARMUP), we will
				 * continue.  This assures that the exit action
				 * can be successfully recorded at most once
				 * when we're in the ACTIVE state.  If we're
				 * encountering the exit() action while in
				 * COOLDOWN, however, we want to honor the new
				 * status code.  (We know that we're the only
				 * thread in COOLDOWN, so there is no race.)
				 */
				void *activity = &state->dts_activity;
				dtrace_activity_t current = state->dts_activity;

				if (current == DTRACE_ACTIVITY_COOLDOWN)
					break;

				if (current != DTRACE_ACTIVITY_WARMUP)
					current = DTRACE_ACTIVITY_ACTIVE;

				if (dtrace_cas32(activity, current,
				    DTRACE_ACTIVITY_DRAINING) != current) {
					*flags |= CPU_DTRACE_DROP;
					continue;
				}

				break;
			}

			default:
				ASSERT(0);
			}

			if (dp->dtdo_rtype.dtdt_flags & DIF_TF_BYREF ||
			    dp->dtdo_rtype.dtdt_flags & DIF_TF_BYUREF) {
				uintptr_t end = valoffs + size;

				if (tracememsize != 0 &&
				    valoffs + tracememsize < end) {
					end = valoffs + tracememsize;
					tracememsize = 0;
				}

				if (dp->dtdo_rtype.dtdt_flags & DIF_TF_BYREF &&
				    !dtrace_vcanload((void *)(uintptr_t)val,
				    &dp->dtdo_rtype, NULL, &mstate, vstate))
					continue;

				dtrace_store_by_ref(dp, tomax, size, &valoffs,
				    &val, end, act->dta_intuple,
				    dp->dtdo_rtype.dtdt_flags & DIF_TF_BYREF ?
				    DIF_TF_BYREF: DIF_TF_BYUREF);
				continue;
			}

			switch (size) {
			case 0:
				break;

			case sizeof (uint8_t):
				DTRACE_STORE(uint8_t, tomax, valoffs, val);
				break;
			case sizeof (uint16_t):
				DTRACE_STORE(uint16_t, tomax, valoffs, val);
				break;
			case sizeof (uint32_t):
				DTRACE_STORE(uint32_t, tomax, valoffs, val);
				break;
			case sizeof (uint64_t):
				DTRACE_STORE(uint64_t, tomax, valoffs, val);
				break;
			default:
				/*
				 * Any other size should have been returned by
				 * reference, not by value.
				 */
				ASSERT(0);
				break;
			}
		}

		if (*flags & CPU_DTRACE_DROP)
			continue;

		if (*flags & CPU_DTRACE_FAULT) {
			int ndx;
			dtrace_action_t *err;

			buf->dtb_errors++;

			if (probe->dtpr_id == dtrace_probeid_error) {
				/*
				 * There's nothing we can do -- we had an
				 * error on the error probe.  We bump an
				 * error counter to at least indicate that
				 * this condition happened.
				 */
				dtrace_error(&state->dts_dblerrors);
				continue;
			}

			if (vtime) {
				/*
				 * Before recursing on dtrace_probe(), we
				 * need to explicitly clear out our start
				 * time to prevent it from being accumulated
				 * into t_dtrace_vtime.
				 */
				curthread->t_dtrace_start = 0;
			}

			/*
			 * Iterate over the actions to figure out which action
			 * we were processing when we experienced the error.
			 * Note that act points _past_ the faulting action; if
			 * act is ecb->dte_action, the fault was in the
			 * predicate, if it's ecb->dte_action->dta_next it's
			 * in action #1, and so on.
			 */
			for (err = ecb->dte_action, ndx = 0;
			    err != act; err = err->dta_next, ndx++)
				continue;

			dtrace_probe_error(state, ecb->dte_epid, ndx,
			    (mstate.dtms_present & DTRACE_MSTATE_FLTOFFS) ?
			    mstate.dtms_fltoffs : -1, DTRACE_FLAGS2FLT(*flags),
			    cpu_core[cpuid].cpuc_dtrace_illval);

			continue;
		}

		if (!committed)
			buf->dtb_offset = offs + ecb->dte_size;
	}

	if (vtime)
		curthread->t_dtrace_start = dtrace_gethrtime();

	dtrace_probe_exit(cookie);
}

/*
 * DTrace Probe Hashing Functions
 *
 * The functions in this section (and indeed, the functions in remaining
 * sections) are not _called_ from probe context.  (Any exceptions to this are
 * marked with a "Note:".)  Rather, they are called from elsewhere in the
 * DTrace framework to look-up probes in, add probes to and remove probes from
 * the DTrace probe hashes.  (Each probe is hashed by each element of the
 * probe tuple -- allowing for fast lookups, regardless of what was
 * specified.)
 */
static uint_t
dtrace_hash_str(const char *p)
{
	unsigned int g;
	uint_t hval = 0;

	while (*p) {
		hval = (hval << 4) + *p++;
		if ((g = (hval & 0xf0000000)) != 0)
			hval ^= g >> 24;
		hval &= ~g;
	}
	return (hval);
}

static dtrace_hash_t *
dtrace_hash_create(uintptr_t stroffs, uintptr_t nextoffs, uintptr_t prevoffs)
{
	dtrace_hash_t *hash = kmem_zalloc(sizeof (dtrace_hash_t), KM_SLEEP);

	hash->dth_stroffs = stroffs;
	hash->dth_nextoffs = nextoffs;
	hash->dth_prevoffs = prevoffs;

	hash->dth_size = 1;
	hash->dth_mask = hash->dth_size - 1;

	hash->dth_tab = kmem_zalloc(hash->dth_size *
	    sizeof (dtrace_hashbucket_t *), KM_SLEEP);

	return (hash);
}

static void
dtrace_hash_destroy(dtrace_hash_t *hash)
{
#ifdef DEBUG
	int i;

	for (i = 0; i < hash->dth_size; i++)
		ASSERT(hash->dth_tab[i] == NULL);
#endif

	kmem_free(hash->dth_tab,
	    hash->dth_size * sizeof (dtrace_hashbucket_t *));
	kmem_free(hash, sizeof (dtrace_hash_t));
}

static void
dtrace_hash_resize(dtrace_hash_t *hash)
{
	int size = hash->dth_size, i, ndx;
	int new_size = hash->dth_size << 1;
	int new_mask = new_size - 1;
	dtrace_hashbucket_t **new_tab, *bucket, *next;

	ASSERT((new_size & new_mask) == 0);

	new_tab = kmem_zalloc(new_size * sizeof (void *), KM_SLEEP);

	for (i = 0; i < size; i++) {
		for (bucket = hash->dth_tab[i]; bucket != NULL; bucket = next) {
			dtrace_probe_t *probe = bucket->dthb_chain;

			ASSERT(probe != NULL);
			ndx = DTRACE_HASHSTR(hash, probe) & new_mask;

			next = bucket->dthb_next;
			bucket->dthb_next = new_tab[ndx];
			new_tab[ndx] = bucket;
		}
	}

	kmem_free(hash->dth_tab, hash->dth_size * sizeof (void *));
	hash->dth_tab = new_tab;
	hash->dth_size = new_size;
	hash->dth_mask = new_mask;
}

static void
dtrace_hash_add(dtrace_hash_t *hash, dtrace_probe_t *new)
{
	int hashval = DTRACE_HASHSTR(hash, new);
	int ndx = hashval & hash->dth_mask;
	dtrace_hashbucket_t *bucket = hash->dth_tab[ndx];
	dtrace_probe_t **nextp, **prevp;

	for (; bucket != NULL; bucket = bucket->dthb_next) {
		if (DTRACE_HASHEQ(hash, bucket->dthb_chain, new))
			goto add;
	}

	if ((hash->dth_nbuckets >> 1) > hash->dth_size) {
		dtrace_hash_resize(hash);
		dtrace_hash_add(hash, new);
		return;
	}

	bucket = kmem_zalloc(sizeof (dtrace_hashbucket_t), KM_SLEEP);
	bucket->dthb_next = hash->dth_tab[ndx];
	hash->dth_tab[ndx] = bucket;
	hash->dth_nbuckets++;

add:
	nextp = DTRACE_HASHNEXT(hash, new);
	ASSERT(*nextp == NULL && *(DTRACE_HASHPREV(hash, new)) == NULL);
	*nextp = bucket->dthb_chain;

	if (bucket->dthb_chain != NULL) {
		prevp = DTRACE_HASHPREV(hash, bucket->dthb_chain);
		ASSERT(*prevp == NULL);
		*prevp = new;
	}

	bucket->dthb_chain = new;
	bucket->dthb_len++;
}

static dtrace_probe_t *
dtrace_hash_lookup(dtrace_hash_t *hash, dtrace_probe_t *template)
{
	int hashval = DTRACE_HASHSTR(hash, template);
	int ndx = hashval & hash->dth_mask;
	dtrace_hashbucket_t *bucket = hash->dth_tab[ndx];

	for (; bucket != NULL; bucket = bucket->dthb_next) {
		if (DTRACE_HASHEQ(hash, bucket->dthb_chain, template))
			return (bucket->dthb_chain);
	}

	return (NULL);
}

static int
dtrace_hash_collisions(dtrace_hash_t *hash, dtrace_probe_t *template)
{
	int hashval = DTRACE_HASHSTR(hash, template);
	int ndx = hashval & hash->dth_mask;
	dtrace_hashbucket_t *bucket = hash->dth_tab[ndx];

	for (; bucket != NULL; bucket = bucket->dthb_next) {
		if (DTRACE_HASHEQ(hash, bucket->dthb_chain, template))
			return (bucket->dthb_len);
	}

	return (0);
}

static void
dtrace_hash_remove(dtrace_hash_t *hash, dtrace_probe_t *probe)
{
	int ndx = DTRACE_HASHSTR(hash, probe) & hash->dth_mask;
	dtrace_hashbucket_t *bucket = hash->dth_tab[ndx];

	dtrace_probe_t **prevp = DTRACE_HASHPREV(hash, probe);
	dtrace_probe_t **nextp = DTRACE_HASHNEXT(hash, probe);

	/*
	 * Find the bucket that we're removing this probe from.
	 */
	for (; bucket != NULL; bucket = bucket->dthb_next) {
		if (DTRACE_HASHEQ(hash, bucket->dthb_chain, probe))
			break;
	}

	ASSERT(bucket != NULL);

	if (*prevp == NULL) {
		if (*nextp == NULL) {
			/*
			 * The removed probe was the only probe on this
			 * bucket; we need to remove the bucket.
			 */
			dtrace_hashbucket_t *b = hash->dth_tab[ndx];

			ASSERT(bucket->dthb_chain == probe);
			ASSERT(b != NULL);

			if (b == bucket) {
				hash->dth_tab[ndx] = bucket->dthb_next;
			} else {
				while (b->dthb_next != bucket)
					b = b->dthb_next;
				b->dthb_next = bucket->dthb_next;
			}

			ASSERT(hash->dth_nbuckets > 0);
			hash->dth_nbuckets--;
			kmem_free(bucket, sizeof (dtrace_hashbucket_t));
			return;
		}

		bucket->dthb_chain = *nextp;
	} else {
		*(DTRACE_HASHNEXT(hash, *prevp)) = *nextp;
	}

	if (*nextp != NULL)
		*(DTRACE_HASHPREV(hash, *nextp)) = *prevp;
}

/*
 * DTrace Utility Functions
 *
 * These are random utility functions that are _not_ called from probe context.
 */
static int
dtrace_badattr(const dtrace_attribute_t *a)
{
	return (a->dtat_name > DTRACE_STABILITY_MAX ||
	    a->dtat_data > DTRACE_STABILITY_MAX ||
	    a->dtat_class > DTRACE_CLASS_MAX);
}

/*
 * Return a duplicate copy of a string.  If the specified string is NULL,
 * this function returns a zero-length string.
 */
static char *
dtrace_strdup(const char *str)
{
	char *new = kmem_zalloc((str != NULL ? strlen(str) : 0) + 1, KM_SLEEP);

	if (str != NULL)
		(void) strcpy(new, str);

	return (new);
}

#define	DTRACE_ISALPHA(c)	\
	(((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))

static int
dtrace_badname(const char *s)
{
	char c;

	if (s == NULL || (c = *s++) == '\0')
		return (0);

	if (!DTRACE_ISALPHA(c) && c != '-' && c != '_' && c != '.')
		return (1);

	while ((c = *s++) != '\0') {
		if (!DTRACE_ISALPHA(c) && (c < '0' || c > '9') &&
		    c != '-' && c != '_' && c != '.' && c != '`')
			return (1);
	}

	return (0);
}

static void
dtrace_cred2priv(cred_t *cr, uint32_t *privp, uid_t *uidp, zoneid_t *zoneidp)
{
	uint32_t priv;

#ifdef illumos
	if (cr == NULL || PRIV_POLICY_ONLY(cr, PRIV_ALL, B_FALSE)) {
		/*
		 * For DTRACE_PRIV_ALL, the uid and zoneid don't matter.
		 */
		priv = DTRACE_PRIV_ALL;
	} else {
		*uidp = crgetuid(cr);
		*zoneidp = crgetzoneid(cr);

		priv = 0;
		if (PRIV_POLICY_ONLY(cr, PRIV_DTRACE_KERNEL, B_FALSE))
			priv |= DTRACE_PRIV_KERNEL | DTRACE_PRIV_USER;
		else if (PRIV_POLICY_ONLY(cr, PRIV_DTRACE_USER, B_FALSE))
			priv |= DTRACE_PRIV_USER;
		if (PRIV_POLICY_ONLY(cr, PRIV_DTRACE_PROC, B_FALSE))
			priv |= DTRACE_PRIV_PROC;
		if (PRIV_POLICY_ONLY(cr, PRIV_PROC_OWNER, B_FALSE))
			priv |= DTRACE_PRIV_OWNER;
		if (PRIV_POLICY_ONLY(cr, PRIV_PROC_ZONE, B_FALSE))
			priv |= DTRACE_PRIV_ZONEOWNER;
	}
#else
	priv = DTRACE_PRIV_ALL;
#endif

	*privp = priv;
}

#ifdef DTRACE_ERRDEBUG
static void
dtrace_errdebug(const char *str)
{
	int hval = dtrace_hash_str(str) % DTRACE_ERRHASHSZ;
	int occupied = 0;

	mutex_enter(&dtrace_errlock);
	dtrace_errlast = str;
	dtrace_errthread = curthread;

	while (occupied++ < DTRACE_ERRHASHSZ) {
		if (dtrace_errhash[hval].dter_msg == str) {
			dtrace_errhash[hval].dter_count++;
			goto out;
		}

		if (dtrace_errhash[hval].dter_msg != NULL) {
			hval = (hval + 1) % DTRACE_ERRHASHSZ;
			continue;
		}

		dtrace_errhash[hval].dter_msg = str;
		dtrace_errhash[hval].dter_count = 1;
		goto out;
	}

	panic("dtrace: undersized error hash");
out:
	mutex_exit(&dtrace_errlock);
}
#endif

/*
 * DTrace Matching Functions
 *
 * These functions are used to match groups of probes, given some elements of
 * a probe tuple, or some globbed expressions for elements of a probe tuple.
 */
static int
dtrace_match_priv(const dtrace_probe_t *prp, uint32_t priv, uid_t uid,
    zoneid_t zoneid)
{
	if (priv != DTRACE_PRIV_ALL) {
		uint32_t ppriv = prp->dtpr_provider->dtpv_priv.dtpp_flags;
		uint32_t match = priv & ppriv;

		/*
		 * No PRIV_DTRACE_* privileges...
		 */
		if ((priv & (DTRACE_PRIV_PROC | DTRACE_PRIV_USER |
		    DTRACE_PRIV_KERNEL)) == 0)
			return (0);

		/*
		 * No matching bits, but there were bits to match...
		 */
		if (match == 0 && ppriv != 0)
			return (0);

		/*
		 * Need to have permissions to the process, but don't...
		 */
		if (((ppriv & ~match) & DTRACE_PRIV_OWNER) != 0 &&
		    uid != prp->dtpr_provider->dtpv_priv.dtpp_uid) {
			return (0);
		}

		/*
		 * Need to be in the same zone unless we possess the
		 * privilege to examine all zones.
		 */
		if (((ppriv & ~match) & DTRACE_PRIV_ZONEOWNER) != 0 &&
		    zoneid != prp->dtpr_provider->dtpv_priv.dtpp_zoneid) {
			return (0);
		}
	}

	return (1);
}

/*
 * dtrace_match_probe compares a dtrace_probe_t to a pre-compiled key, which
 * consists of input pattern strings and an ops-vector to evaluate them.
 * This function returns >0 for match, 0 for no match, and <0 for error.
 */
static int
dtrace_match_probe(const dtrace_probe_t *prp, const dtrace_probekey_t *pkp,
    uint32_t priv, uid_t uid, zoneid_t zoneid)
{
	dtrace_provider_t *pvp = prp->dtpr_provider;
	int rv;

	if (pvp->dtpv_defunct)
		return (0);

	if ((rv = pkp->dtpk_pmatch(pvp->dtpv_name, pkp->dtpk_prov, 0)) <= 0)
		return (rv);

	if ((rv = pkp->dtpk_mmatch(prp->dtpr_mod, pkp->dtpk_mod, 0)) <= 0)
		return (rv);

	if ((rv = pkp->dtpk_fmatch(prp->dtpr_func, pkp->dtpk_func, 0)) <= 0)
		return (rv);

	if ((rv = pkp->dtpk_nmatch(prp->dtpr_name, pkp->dtpk_name, 0)) <= 0)
		return (rv);

	if (dtrace_match_priv(prp, priv, uid, zoneid) == 0)
		return (0);

	return (rv);
}

/*
 * dtrace_match_glob() is a safe kernel implementation of the gmatch(3GEN)
 * interface for matching a glob pattern 'p' to an input string 's'.  Unlike
 * libc's version, the kernel version only applies to 8-bit ASCII strings.
 * In addition, all of the recursion cases except for '*' matching have been
 * unwound.  For '*', we still implement recursive evaluation, but a depth
 * counter is maintained and matching is aborted if we recurse too deep.
 * The function returns 0 if no match, >0 if match, and <0 if recursion error.
 */
static int
dtrace_match_glob(const char *s, const char *p, int depth)
{
	const char *olds;
	char s1, c;
	int gs;

	if (depth > DTRACE_PROBEKEY_MAXDEPTH)
		return (-1);

	if (s == NULL)
		s = ""; /* treat NULL as empty string */

top:
	olds = s;
	s1 = *s++;

	if (p == NULL)
		return (0);

	if ((c = *p++) == '\0')
		return (s1 == '\0');

	switch (c) {
	case '[': {
		int ok = 0, notflag = 0;
		char lc = '\0';

		if (s1 == '\0')
			return (0);

		if (*p == '!') {
			notflag = 1;
			p++;
		}

		if ((c = *p++) == '\0')
			return (0);

		do {
			if (c == '-' && lc != '\0' && *p != ']') {
				if ((c = *p++) == '\0')
					return (0);
				if (c == '\\' && (c = *p++) == '\0')
					return (0);

				if (notflag) {
					if (s1 < lc || s1 > c)
						ok++;
					else
						return (0);
				} else if (lc <= s1 && s1 <= c)
					ok++;

			} else if (c == '\\' && (c = *p++) == '\0')
				return (0);

			lc = c; /* save left-hand 'c' for next iteration */

			if (notflag) {
				if (s1 != c)
					ok++;
				else
					return (0);
			} else if (s1 == c)
				ok++;

			if ((c = *p++) == '\0')
				return (0);

		} while (c != ']');

		if (ok)
			goto top;

		return (0);
	}

	case '\\':
		if ((c = *p++) == '\0')
			return (0);
		/*FALLTHRU*/

	default:
		if (c != s1)
			return (0);
		/*FALLTHRU*/

	case '?':
		if (s1 != '\0')
			goto top;
		return (0);

	case '*':
		while (*p == '*')
			p++; /* consecutive *'s are identical to a single one */

		if (*p == '\0')
			return (1);

		for (s = olds; *s != '\0'; s++) {
			if ((gs = dtrace_match_glob(s, p, depth + 1)) != 0)
				return (gs);
		}

		return (0);
	}
}

/*ARGSUSED*/
static int
dtrace_match_string(const char *s, const char *p, int depth)
{
	return (s != NULL && strcmp(s, p) == 0);
}

/*ARGSUSED*/
static int
dtrace_match_nul(const char *s, const char *p, int depth)
{
	return (1); /* always match the empty pattern */
}

/*ARGSUSED*/
static int
dtrace_match_nonzero(const char *s, const char *p, int depth)
{
	return (s != NULL && s[0] != '\0');
}

static int
dtrace_match(const dtrace_probekey_t *pkp, uint32_t priv, uid_t uid,
    zoneid_t zoneid, int (*matched)(dtrace_probe_t *, void *), void *arg)
{
	dtrace_probe_t template, *probe;
	dtrace_hash_t *hash = NULL;
	int len, best = INT_MAX, nmatched = 0;
	dtrace_id_t i;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	/*
	 * If the probe ID is specified in the key, just lookup by ID and
	 * invoke the match callback once if a matching probe is found.
	 */
	if (pkp->dtpk_id != DTRACE_IDNONE) {
		if ((probe = dtrace_probe_lookup_id(pkp->dtpk_id)) != NULL &&
		    dtrace_match_probe(probe, pkp, priv, uid, zoneid) > 0) {
			(void) (*matched)(probe, arg);
			nmatched++;
		}
		return (nmatched);
	}

	template.dtpr_mod = (char *)pkp->dtpk_mod;
	template.dtpr_func = (char *)pkp->dtpk_func;
	template.dtpr_name = (char *)pkp->dtpk_name;

	/*
	 * We want to find the most distinct of the module name, function
	 * name, and name.  So for each one that is not a glob pattern or
	 * empty string, we perform a lookup in the corresponding hash and
	 * use the hash table with the fewest collisions to do our search.
	 */
	if (pkp->dtpk_mmatch == &dtrace_match_string &&
	    (len = dtrace_hash_collisions(dtrace_bymod, &template)) < best) {
		best = len;
		hash = dtrace_bymod;
	}

	if (pkp->dtpk_fmatch == &dtrace_match_string &&
	    (len = dtrace_hash_collisions(dtrace_byfunc, &template)) < best) {
		best = len;
		hash = dtrace_byfunc;
	}

	if (pkp->dtpk_nmatch == &dtrace_match_string &&
	    (len = dtrace_hash_collisions(dtrace_byname, &template)) < best) {
		best = len;
		hash = dtrace_byname;
	}

	/*
	 * If we did not select a hash table, iterate over every probe and
	 * invoke our callback for each one that matches our input probe key.
	 */
	if (hash == NULL) {
		for (i = 0; i < dtrace_nprobes; i++) {
			if ((probe = dtrace_probes[i]) == NULL ||
			    dtrace_match_probe(probe, pkp, priv, uid,
			    zoneid) <= 0)
				continue;

			nmatched++;

			if ((*matched)(probe, arg) != DTRACE_MATCH_NEXT)
				break;
		}

		return (nmatched);
	}

	/*
	 * If we selected a hash table, iterate over each probe of the same key
	 * name and invoke the callback for every probe that matches the other
	 * attributes of our input probe key.
	 */
	for (probe = dtrace_hash_lookup(hash, &template); probe != NULL;
	    probe = *(DTRACE_HASHNEXT(hash, probe))) {

		if (dtrace_match_probe(probe, pkp, priv, uid, zoneid) <= 0)
			continue;

		nmatched++;

		if ((*matched)(probe, arg) != DTRACE_MATCH_NEXT)
			break;
	}

	return (nmatched);
}

/*
 * Return the function pointer dtrace_probecmp() should use to compare the
 * specified pattern with a string.  For NULL or empty patterns, we select
 * dtrace_match_nul().  For glob pattern strings, we use dtrace_match_glob().
 * For non-empty non-glob strings, we use dtrace_match_string().
 */
static dtrace_probekey_f *
dtrace_probekey_func(const char *p)
{
	char c;

	if (p == NULL || *p == '\0')
		return (&dtrace_match_nul);

	while ((c = *p++) != '\0') {
		if (c == '[' || c == '?' || c == '*' || c == '\\')
			return (&dtrace_match_glob);
	}

	return (&dtrace_match_string);
}

/*
 * Build a probe comparison key for use with dtrace_match_probe() from the
 * given probe description.  By convention, a null key only matches anchored
 * probes: if each field is the empty string, reset dtpk_fmatch to
 * dtrace_match_nonzero().
 */
static void
dtrace_probekey(dtrace_probedesc_t *pdp, dtrace_probekey_t *pkp)
{
	pkp->dtpk_prov = pdp->dtpd_provider;
	pkp->dtpk_pmatch = dtrace_probekey_func(pdp->dtpd_provider);

	pkp->dtpk_mod = pdp->dtpd_mod;
	pkp->dtpk_mmatch = dtrace_probekey_func(pdp->dtpd_mod);

	pkp->dtpk_func = pdp->dtpd_func;
	pkp->dtpk_fmatch = dtrace_probekey_func(pdp->dtpd_func);

	pkp->dtpk_name = pdp->dtpd_name;
	pkp->dtpk_nmatch = dtrace_probekey_func(pdp->dtpd_name);

	pkp->dtpk_id = pdp->dtpd_id;

	if (pkp->dtpk_id == DTRACE_IDNONE &&
	    pkp->dtpk_pmatch == &dtrace_match_nul &&
	    pkp->dtpk_mmatch == &dtrace_match_nul &&
	    pkp->dtpk_fmatch == &dtrace_match_nul &&
	    pkp->dtpk_nmatch == &dtrace_match_nul)
		pkp->dtpk_fmatch = &dtrace_match_nonzero;
}

/*
 * DTrace Provider-to-Framework API Functions
 *
 * These functions implement much of the Provider-to-Framework API, as
 * described in <sys/dtrace.h>.  The parts of the API not in this section are
 * the functions in the API for probe management (found below), and
 * dtrace_probe() itself (found above).
 */

/*
 * Register the calling provider with the DTrace framework.  This should
 * generally be called by DTrace providers in their attach(9E) entry point.
 */
int
dtrace_register(const char *name, const dtrace_pattr_t *pap, uint32_t priv,
    cred_t *cr, const dtrace_pops_t *pops, void *arg, dtrace_provider_id_t *idp)
{
	dtrace_provider_t *provider;

	if (name == NULL || pap == NULL || pops == NULL || idp == NULL) {
		cmn_err(CE_WARN, "failed to register provider '%s': invalid "
		    "arguments", name ? name : "<NULL>");
		return (EINVAL);
	}

	if (name[0] == '\0' || dtrace_badname(name)) {
		cmn_err(CE_WARN, "failed to register provider '%s': invalid "
		    "provider name", name);
		return (EINVAL);
	}

	if ((pops->dtps_provide == NULL && pops->dtps_provide_module == NULL) ||
	    pops->dtps_enable == NULL || pops->dtps_disable == NULL ||
	    pops->dtps_destroy == NULL ||
	    ((pops->dtps_resume == NULL) != (pops->dtps_suspend == NULL))) {
		cmn_err(CE_WARN, "failed to register provider '%s': invalid "
		    "provider ops", name);
		return (EINVAL);
	}

	if (dtrace_badattr(&pap->dtpa_provider) ||
	    dtrace_badattr(&pap->dtpa_mod) ||
	    dtrace_badattr(&pap->dtpa_func) ||
	    dtrace_badattr(&pap->dtpa_name) ||
	    dtrace_badattr(&pap->dtpa_args)) {
		cmn_err(CE_WARN, "failed to register provider '%s': invalid "
		    "provider attributes", name);
		return (EINVAL);
	}

	if (priv & ~DTRACE_PRIV_ALL) {
		cmn_err(CE_WARN, "failed to register provider '%s': invalid "
		    "privilege attributes", name);
		return (EINVAL);
	}

	if ((priv & DTRACE_PRIV_KERNEL) &&
	    (priv & (DTRACE_PRIV_USER | DTRACE_PRIV_OWNER)) &&
	    pops->dtps_usermode == NULL) {
		cmn_err(CE_WARN, "failed to register provider '%s': need "
		    "dtps_usermode() op for given privilege attributes", name);
		return (EINVAL);
	}

	provider = kmem_zalloc(sizeof (dtrace_provider_t), KM_SLEEP);
	provider->dtpv_name = kmem_alloc(strlen(name) + 1, KM_SLEEP);
	(void) strcpy(provider->dtpv_name, name);

	provider->dtpv_attr = *pap;
	provider->dtpv_priv.dtpp_flags = priv;
	if (cr != NULL) {
		provider->dtpv_priv.dtpp_uid = crgetuid(cr);
		provider->dtpv_priv.dtpp_zoneid = crgetzoneid(cr);
	}
	provider->dtpv_pops = *pops;

	if (pops->dtps_provide == NULL) {
		ASSERT(pops->dtps_provide_module != NULL);
		provider->dtpv_pops.dtps_provide =
		    (void (*)(void *, dtrace_probedesc_t *))dtrace_nullop;
	}

	if (pops->dtps_provide_module == NULL) {
		ASSERT(pops->dtps_provide != NULL);
		provider->dtpv_pops.dtps_provide_module =
		    (void (*)(void *, modctl_t *))dtrace_nullop;
	}

	if (pops->dtps_suspend == NULL) {
		ASSERT(pops->dtps_resume == NULL);
		provider->dtpv_pops.dtps_suspend =
		    (void (*)(void *, dtrace_id_t, void *))dtrace_nullop;
		provider->dtpv_pops.dtps_resume =
		    (void (*)(void *, dtrace_id_t, void *))dtrace_nullop;
	}

	provider->dtpv_arg = arg;
	*idp = (dtrace_provider_id_t)provider;

	if (pops == &dtrace_provider_ops) {
		ASSERT(MUTEX_HELD(&dtrace_provider_lock));
		ASSERT(MUTEX_HELD(&dtrace_lock));
		ASSERT(dtrace_anon.dta_enabling == NULL);

		/*
		 * We make sure that the DTrace provider is at the head of
		 * the provider chain.
		 */
		provider->dtpv_next = dtrace_provider;
		dtrace_provider = provider;
		return (0);
	}

	mutex_enter(&dtrace_provider_lock);
	mutex_enter(&dtrace_lock);

	/*
	 * If there is at least one provider registered, we'll add this
	 * provider after the first provider.
	 */
	if (dtrace_provider != NULL) {
		provider->dtpv_next = dtrace_provider->dtpv_next;
		dtrace_provider->dtpv_next = provider;
	} else {
		dtrace_provider = provider;
	}

	if (dtrace_retained != NULL) {
		dtrace_enabling_provide(provider);

		/*
		 * Now we need to call dtrace_enabling_matchall() -- which
		 * will acquire cpu_lock and dtrace_lock.  We therefore need
		 * to drop all of our locks before calling into it...
		 */
		mutex_exit(&dtrace_lock);
		mutex_exit(&dtrace_provider_lock);
		dtrace_enabling_matchall();

		return (0);
	}

	mutex_exit(&dtrace_lock);
	mutex_exit(&dtrace_provider_lock);

	return (0);
}

/*
 * Unregister the specified provider from the DTrace framework.  This should
 * generally be called by DTrace providers in their detach(9E) entry point.
 */
int
dtrace_unregister(dtrace_provider_id_t id)
{
	dtrace_provider_t *old = (dtrace_provider_t *)id;
	dtrace_provider_t *prev = NULL;
	int i, self = 0, noreap = 0;
	dtrace_probe_t *probe, *first = NULL;

	if (old->dtpv_pops.dtps_enable ==
	    (void (*)(void *, dtrace_id_t, void *))dtrace_nullop) {
		/*
		 * If DTrace itself is the provider, we're called with locks
		 * already held.
		 */
		ASSERT(old == dtrace_provider);
#ifdef illumos
		ASSERT(dtrace_devi != NULL);
#endif
		ASSERT(MUTEX_HELD(&dtrace_provider_lock));
		ASSERT(MUTEX_HELD(&dtrace_lock));
		self = 1;

		if (dtrace_provider->dtpv_next != NULL) {
			/*
			 * There's another provider here; return failure.
			 */
			return (EBUSY);
		}
	} else {
		mutex_enter(&dtrace_provider_lock);
#ifdef illumos
		mutex_enter(&mod_lock);
#endif
		mutex_enter(&dtrace_lock);
	}

	/*
	 * If anyone has /dev/dtrace open, or if there are anonymous enabled
	 * probes, we refuse to let providers slither away, unless this
	 * provider has already been explicitly invalidated.
	 */
	if (!old->dtpv_defunct &&
	    (dtrace_opens || (dtrace_anon.dta_state != NULL &&
	    dtrace_anon.dta_state->dts_necbs > 0))) {
		if (!self) {
			mutex_exit(&dtrace_lock);
#ifdef illumos
			mutex_exit(&mod_lock);
#endif
			mutex_exit(&dtrace_provider_lock);
		}
		return (EBUSY);
	}

	/*
	 * Attempt to destroy the probes associated with this provider.
	 */
	for (i = 0; i < dtrace_nprobes; i++) {
		if ((probe = dtrace_probes[i]) == NULL)
			continue;

		if (probe->dtpr_provider != old)
			continue;

		if (probe->dtpr_ecb == NULL)
			continue;

		/*
		 * If we are trying to unregister a defunct provider, and the
		 * provider was made defunct within the interval dictated by
		 * dtrace_unregister_defunct_reap, we'll (asynchronously)
		 * attempt to reap our enablings.  To denote that the provider
		 * should reattempt to unregister itself at some point in the
		 * future, we will return a differentiable error code (EAGAIN
		 * instead of EBUSY) in this case.
		 */
		if (dtrace_gethrtime() - old->dtpv_defunct >
		    dtrace_unregister_defunct_reap)
			noreap = 1;

		if (!self) {
			mutex_exit(&dtrace_lock);
#ifdef illumos
			mutex_exit(&mod_lock);
#endif
			mutex_exit(&dtrace_provider_lock);
		}

		if (noreap)
			return (EBUSY);

		(void) taskq_dispatch(dtrace_taskq,
		    (task_func_t *)dtrace_enabling_reap, NULL, TQ_SLEEP);

		return (EAGAIN);
	}

	/*
	 * All of the probes for this provider are disabled; we can safely
	 * remove all of them from their hash chains and from the probe array.
	 */
	for (i = 0; i < dtrace_nprobes; i++) {
		if ((probe = dtrace_probes[i]) == NULL)
			continue;

		if (probe->dtpr_provider != old)
			continue;

		dtrace_probes[i] = NULL;

		dtrace_hash_remove(dtrace_bymod, probe);
		dtrace_hash_remove(dtrace_byfunc, probe);
		dtrace_hash_remove(dtrace_byname, probe);

		if (first == NULL) {
			first = probe;
			probe->dtpr_nextmod = NULL;
		} else {
			probe->dtpr_nextmod = first;
			first = probe;
		}
	}

	/*
	 * The provider's probes have been removed from the hash chains and
	 * from the probe array.  Now issue a dtrace_sync() to be sure that
	 * everyone has cleared out from any probe array processing.
	 */
	dtrace_sync();

	for (probe = first; probe != NULL; probe = first) {
		first = probe->dtpr_nextmod;

		old->dtpv_pops.dtps_destroy(old->dtpv_arg, probe->dtpr_id,
		    probe->dtpr_arg);
		kmem_free(probe->dtpr_mod, strlen(probe->dtpr_mod) + 1);
		kmem_free(probe->dtpr_func, strlen(probe->dtpr_func) + 1);
		kmem_free(probe->dtpr_name, strlen(probe->dtpr_name) + 1);
#ifdef illumos
		vmem_free(dtrace_arena, (void *)(uintptr_t)(probe->dtpr_id), 1);
#else
		free_unr(dtrace_arena, probe->dtpr_id);
#endif
		kmem_free(probe, sizeof (dtrace_probe_t));
	}

	if ((prev = dtrace_provider) == old) {
#ifdef illumos
		ASSERT(self || dtrace_devi == NULL);
		ASSERT(old->dtpv_next == NULL || dtrace_devi == NULL);
#endif
		dtrace_provider = old->dtpv_next;
	} else {
		while (prev != NULL && prev->dtpv_next != old)
			prev = prev->dtpv_next;

		if (prev == NULL) {
			panic("attempt to unregister non-existent "
			    "dtrace provider %p\n", (void *)id);
		}

		prev->dtpv_next = old->dtpv_next;
	}

	if (!self) {
		mutex_exit(&dtrace_lock);
#ifdef illumos
		mutex_exit(&mod_lock);
#endif
		mutex_exit(&dtrace_provider_lock);
	}

	kmem_free(old->dtpv_name, strlen(old->dtpv_name) + 1);
	kmem_free(old, sizeof (dtrace_provider_t));

	return (0);
}

/*
 * Invalidate the specified provider.  All subsequent probe lookups for the
 * specified provider will fail, but its probes will not be removed.
 */
void
dtrace_invalidate(dtrace_provider_id_t id)
{
	dtrace_provider_t *pvp = (dtrace_provider_t *)id;

	ASSERT(pvp->dtpv_pops.dtps_enable !=
	    (void (*)(void *, dtrace_id_t, void *))dtrace_nullop);

	mutex_enter(&dtrace_provider_lock);
	mutex_enter(&dtrace_lock);

	pvp->dtpv_defunct = dtrace_gethrtime();

	mutex_exit(&dtrace_lock);
	mutex_exit(&dtrace_provider_lock);
}

/*
 * Indicate whether or not DTrace has attached.
 */
int
dtrace_attached(void)
{
	/*
	 * dtrace_provider will be non-NULL iff the DTrace driver has
	 * attached.  (It's non-NULL because DTrace is always itself a
	 * provider.)
	 */
	return (dtrace_provider != NULL);
}

/*
 * Remove all the unenabled probes for the given provider.  This function is
 * not unlike dtrace_unregister(), except that it doesn't remove the provider
 * -- just as many of its associated probes as it can.
 */
int
dtrace_condense(dtrace_provider_id_t id)
{
	dtrace_provider_t *prov = (dtrace_provider_t *)id;
	int i;
	dtrace_probe_t *probe;

	/*
	 * Make sure this isn't the dtrace provider itself.
	 */
	ASSERT(prov->dtpv_pops.dtps_enable !=
	    (void (*)(void *, dtrace_id_t, void *))dtrace_nullop);

	mutex_enter(&dtrace_provider_lock);
	mutex_enter(&dtrace_lock);

	/*
	 * Attempt to destroy the probes associated with this provider.
	 */
	for (i = 0; i < dtrace_nprobes; i++) {
		if ((probe = dtrace_probes[i]) == NULL)
			continue;

		if (probe->dtpr_provider != prov)
			continue;

		if (probe->dtpr_ecb != NULL)
			continue;

		dtrace_probes[i] = NULL;

		dtrace_hash_remove(dtrace_bymod, probe);
		dtrace_hash_remove(dtrace_byfunc, probe);
		dtrace_hash_remove(dtrace_byname, probe);

		prov->dtpv_pops.dtps_destroy(prov->dtpv_arg, i + 1,
		    probe->dtpr_arg);
		kmem_free(probe->dtpr_mod, strlen(probe->dtpr_mod) + 1);
		kmem_free(probe->dtpr_func, strlen(probe->dtpr_func) + 1);
		kmem_free(probe->dtpr_name, strlen(probe->dtpr_name) + 1);
		kmem_free(probe, sizeof (dtrace_probe_t));
#ifdef illumos
		vmem_free(dtrace_arena, (void *)((uintptr_t)i + 1), 1);
#else
		free_unr(dtrace_arena, i + 1);
#endif
	}

	mutex_exit(&dtrace_lock);
	mutex_exit(&dtrace_provider_lock);

	return (0);
}

/*
 * DTrace Probe Management Functions
 *
 * The functions in this section perform the DTrace probe management,
 * including functions to create probes, look-up probes, and call into the
 * providers to request that probes be provided.  Some of these functions are
 * in the Provider-to-Framework API; these functions can be identified by the
 * fact that they are not declared "static".
 */

/*
 * Create a probe with the specified module name, function name, and name.
 */
dtrace_id_t
dtrace_probe_create(dtrace_provider_id_t prov, const char *mod,
    const char *func, const char *name, int aframes, void *arg)
{
	dtrace_probe_t *probe, **probes;
	dtrace_provider_t *provider = (dtrace_provider_t *)prov;
	dtrace_id_t id;

	if (provider == dtrace_provider) {
		ASSERT(MUTEX_HELD(&dtrace_lock));
	} else {
		mutex_enter(&dtrace_lock);
	}

#ifdef illumos
	id = (dtrace_id_t)(uintptr_t)vmem_alloc(dtrace_arena, 1,
	    VM_BESTFIT | VM_SLEEP);
#else
	id = alloc_unr(dtrace_arena);
#endif
	probe = kmem_zalloc(sizeof (dtrace_probe_t), KM_SLEEP);

	probe->dtpr_id = id;
	probe->dtpr_gen = dtrace_probegen++;
	probe->dtpr_mod = dtrace_strdup(mod);
	probe->dtpr_func = dtrace_strdup(func);
	probe->dtpr_name = dtrace_strdup(name);
	probe->dtpr_arg = arg;
	probe->dtpr_aframes = aframes;
	probe->dtpr_provider = provider;

	dtrace_hash_add(dtrace_bymod, probe);
	dtrace_hash_add(dtrace_byfunc, probe);
	dtrace_hash_add(dtrace_byname, probe);

	if (id - 1 >= dtrace_nprobes) {
		size_t osize = dtrace_nprobes * sizeof (dtrace_probe_t *);
		size_t nsize = osize << 1;

		if (nsize == 0) {
			ASSERT(osize == 0);
			ASSERT(dtrace_probes == NULL);
			nsize = sizeof (dtrace_probe_t *);
		}

		probes = kmem_zalloc(nsize, KM_SLEEP);

		if (dtrace_probes == NULL) {
			ASSERT(osize == 0);
			dtrace_probes = probes;
			dtrace_nprobes = 1;
		} else {
			dtrace_probe_t **oprobes = dtrace_probes;

			bcopy(oprobes, probes, osize);
			dtrace_membar_producer();
			dtrace_probes = probes;

			dtrace_sync();

			/*
			 * All CPUs are now seeing the new probes array; we can
			 * safely free the old array.
			 */
			kmem_free(oprobes, osize);
			dtrace_nprobes <<= 1;
		}

		ASSERT(id - 1 < dtrace_nprobes);
	}

	ASSERT(dtrace_probes[id - 1] == NULL);
	dtrace_probes[id - 1] = probe;

	if (provider != dtrace_provider)
		mutex_exit(&dtrace_lock);

	return (id);
}

static dtrace_probe_t *
dtrace_probe_lookup_id(dtrace_id_t id)
{
	ASSERT(MUTEX_HELD(&dtrace_lock));

	if (id == 0 || id > dtrace_nprobes)
		return (NULL);

	return (dtrace_probes[id - 1]);
}

static int
dtrace_probe_lookup_match(dtrace_probe_t *probe, void *arg)
{
	*((dtrace_id_t *)arg) = probe->dtpr_id;

	return (DTRACE_MATCH_DONE);
}

/*
 * Look up a probe based on provider and one or more of module name, function
 * name and probe name.
 */
dtrace_id_t
dtrace_probe_lookup(dtrace_provider_id_t prid, char *mod,
    char *func, char *name)
{
	dtrace_probekey_t pkey;
	dtrace_id_t id;
	int match;

	pkey.dtpk_prov = ((dtrace_provider_t *)prid)->dtpv_name;
	pkey.dtpk_pmatch = &dtrace_match_string;
	pkey.dtpk_mod = mod;
	pkey.dtpk_mmatch = mod ? &dtrace_match_string : &dtrace_match_nul;
	pkey.dtpk_func = func;
	pkey.dtpk_fmatch = func ? &dtrace_match_string : &dtrace_match_nul;
	pkey.dtpk_name = name;
	pkey.dtpk_nmatch = name ? &dtrace_match_string : &dtrace_match_nul;
	pkey.dtpk_id = DTRACE_IDNONE;

	mutex_enter(&dtrace_lock);
	match = dtrace_match(&pkey, DTRACE_PRIV_ALL, 0, 0,
	    dtrace_probe_lookup_match, &id);
	mutex_exit(&dtrace_lock);

	ASSERT(match == 1 || match == 0);
	return (match ? id : 0);
}

/*
 * Returns the probe argument associated with the specified probe.
 */
void *
dtrace_probe_arg(dtrace_provider_id_t id, dtrace_id_t pid)
{
	dtrace_probe_t *probe;
	void *rval = NULL;

	mutex_enter(&dtrace_lock);

	if ((probe = dtrace_probe_lookup_id(pid)) != NULL &&
	    probe->dtpr_provider == (dtrace_provider_t *)id)
		rval = probe->dtpr_arg;

	mutex_exit(&dtrace_lock);

	return (rval);
}

/*
 * Copy a probe into a probe description.
 */
static void
dtrace_probe_description(const dtrace_probe_t *prp, dtrace_probedesc_t *pdp)
{
	bzero(pdp, sizeof (dtrace_probedesc_t));
	pdp->dtpd_id = prp->dtpr_id;

	(void) strncpy(pdp->dtpd_provider,
	    prp->dtpr_provider->dtpv_name, DTRACE_PROVNAMELEN - 1);

	(void) strncpy(pdp->dtpd_mod, prp->dtpr_mod, DTRACE_MODNAMELEN - 1);
	(void) strncpy(pdp->dtpd_func, prp->dtpr_func, DTRACE_FUNCNAMELEN - 1);
	(void) strncpy(pdp->dtpd_name, prp->dtpr_name, DTRACE_NAMELEN - 1);
}

/*
 * Called to indicate that a probe -- or probes -- should be provided by a
 * specfied provider.  If the specified description is NULL, the provider will
 * be told to provide all of its probes.  (This is done whenever a new
 * consumer comes along, or whenever a retained enabling is to be matched.) If
 * the specified description is non-NULL, the provider is given the
 * opportunity to dynamically provide the specified probe, allowing providers
 * to support the creation of probes on-the-fly.  (So-called _autocreated_
 * probes.)  If the provider is NULL, the operations will be applied to all
 * providers; if the provider is non-NULL the operations will only be applied
 * to the specified provider.  The dtrace_provider_lock must be held, and the
 * dtrace_lock must _not_ be held -- the provider's dtps_provide() operation
 * will need to grab the dtrace_lock when it reenters the framework through
 * dtrace_probe_lookup(), dtrace_probe_create(), etc.
 */
static void
dtrace_probe_provide(dtrace_probedesc_t *desc, dtrace_provider_t *prv)
{
#ifdef illumos
	modctl_t *ctl;
#endif
	int all = 0;

	ASSERT(MUTEX_HELD(&dtrace_provider_lock));

	if (prv == NULL) {
		all = 1;
		prv = dtrace_provider;
	}

	do {
		/*
		 * First, call the blanket provide operation.
		 */
		prv->dtpv_pops.dtps_provide(prv->dtpv_arg, desc);

#ifdef illumos
		/*
		 * Now call the per-module provide operation.  We will grab
		 * mod_lock to prevent the list from being modified.  Note
		 * that this also prevents the mod_busy bits from changing.
		 * (mod_busy can only be changed with mod_lock held.)
		 */
		mutex_enter(&mod_lock);

		ctl = &modules;
		do {
			if (ctl->mod_busy || ctl->mod_mp == NULL)
				continue;

			prv->dtpv_pops.dtps_provide_module(prv->dtpv_arg, ctl);

		} while ((ctl = ctl->mod_next) != &modules);

		mutex_exit(&mod_lock);
#endif
	} while (all && (prv = prv->dtpv_next) != NULL);
}

#ifdef illumos
/*
 * Iterate over each probe, and call the Framework-to-Provider API function
 * denoted by offs.
 */
static void
dtrace_probe_foreach(uintptr_t offs)
{
	dtrace_provider_t *prov;
	void (*func)(void *, dtrace_id_t, void *);
	dtrace_probe_t *probe;
	dtrace_icookie_t cookie;
	int i;

	/*
	 * We disable interrupts to walk through the probe array.  This is
	 * safe -- the dtrace_sync() in dtrace_unregister() assures that we
	 * won't see stale data.
	 */
	cookie = dtrace_interrupt_disable();

	for (i = 0; i < dtrace_nprobes; i++) {
		if ((probe = dtrace_probes[i]) == NULL)
			continue;

		if (probe->dtpr_ecb == NULL) {
			/*
			 * This probe isn't enabled -- don't call the function.
			 */
			continue;
		}

		prov = probe->dtpr_provider;
		func = *((void(**)(void *, dtrace_id_t, void *))
		    ((uintptr_t)&prov->dtpv_pops + offs));

		func(prov->dtpv_arg, i + 1, probe->dtpr_arg);
	}

	dtrace_interrupt_enable(cookie);
}
#endif

static int
dtrace_probe_enable(dtrace_probedesc_t *desc, dtrace_enabling_t *enab)
{
	dtrace_probekey_t pkey;
	uint32_t priv;
	uid_t uid;
	zoneid_t zoneid;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	dtrace_ecb_create_cache = NULL;

	if (desc == NULL) {
		/*
		 * If we're passed a NULL description, we're being asked to
		 * create an ECB with a NULL probe.
		 */
		(void) dtrace_ecb_create_enable(NULL, enab);
		return (0);
	}

	dtrace_probekey(desc, &pkey);
	dtrace_cred2priv(enab->dten_vstate->dtvs_state->dts_cred.dcr_cred,
	    &priv, &uid, &zoneid);

	return (dtrace_match(&pkey, priv, uid, zoneid, dtrace_ecb_create_enable,
	    enab));
}

/*
 * DTrace Helper Provider Functions
 */
static void
dtrace_dofattr2attr(dtrace_attribute_t *attr, const dof_attr_t dofattr)
{
	attr->dtat_name = DOF_ATTR_NAME(dofattr);
	attr->dtat_data = DOF_ATTR_DATA(dofattr);
	attr->dtat_class = DOF_ATTR_CLASS(dofattr);
}

static void
dtrace_dofprov2hprov(dtrace_helper_provdesc_t *hprov,
    const dof_provider_t *dofprov, char *strtab)
{
	hprov->dthpv_provname = strtab + dofprov->dofpv_name;
	dtrace_dofattr2attr(&hprov->dthpv_pattr.dtpa_provider,
	    dofprov->dofpv_provattr);
	dtrace_dofattr2attr(&hprov->dthpv_pattr.dtpa_mod,
	    dofprov->dofpv_modattr);
	dtrace_dofattr2attr(&hprov->dthpv_pattr.dtpa_func,
	    dofprov->dofpv_funcattr);
	dtrace_dofattr2attr(&hprov->dthpv_pattr.dtpa_name,
	    dofprov->dofpv_nameattr);
	dtrace_dofattr2attr(&hprov->dthpv_pattr.dtpa_args,
	    dofprov->dofpv_argsattr);
}

static void
dtrace_helper_provide_one(dof_helper_t *dhp, dof_sec_t *sec, pid_t pid)
{
	uintptr_t daddr = (uintptr_t)dhp->dofhp_dof;
	dof_hdr_t *dof = (dof_hdr_t *)daddr;
	dof_sec_t *str_sec, *prb_sec, *arg_sec, *off_sec, *enoff_sec;
	dof_provider_t *provider;
	dof_probe_t *probe;
	uint32_t *off, *enoff;
	uint8_t *arg;
	char *strtab;
	uint_t i, nprobes;
	dtrace_helper_provdesc_t dhpv;
	dtrace_helper_probedesc_t dhpb;
	dtrace_meta_t *meta = dtrace_meta_pid;
	dtrace_mops_t *mops = &meta->dtm_mops;
	void *parg;

	provider = (dof_provider_t *)(uintptr_t)(daddr + sec->dofs_offset);
	str_sec = (dof_sec_t *)(uintptr_t)(daddr + dof->dofh_secoff +
	    provider->dofpv_strtab * dof->dofh_secsize);
	prb_sec = (dof_sec_t *)(uintptr_t)(daddr + dof->dofh_secoff +
	    provider->dofpv_probes * dof->dofh_secsize);
	arg_sec = (dof_sec_t *)(uintptr_t)(daddr + dof->dofh_secoff +
	    provider->dofpv_prargs * dof->dofh_secsize);
	off_sec = (dof_sec_t *)(uintptr_t)(daddr + dof->dofh_secoff +
	    provider->dofpv_proffs * dof->dofh_secsize);

	strtab = (char *)(uintptr_t)(daddr + str_sec->dofs_offset);
	off = (uint32_t *)(uintptr_t)(daddr + off_sec->dofs_offset);
	arg = (uint8_t *)(uintptr_t)(daddr + arg_sec->dofs_offset);
	enoff = NULL;

	/*
	 * See dtrace_helper_provider_validate().
	 */
	if (dof->dofh_ident[DOF_ID_VERSION] != DOF_VERSION_1 &&
	    provider->dofpv_prenoffs != DOF_SECT_NONE) {
		enoff_sec = (dof_sec_t *)(uintptr_t)(daddr + dof->dofh_secoff +
		    provider->dofpv_prenoffs * dof->dofh_secsize);
		enoff = (uint32_t *)(uintptr_t)(daddr + enoff_sec->dofs_offset);
	}

	nprobes = prb_sec->dofs_size / prb_sec->dofs_entsize;

	/*
	 * Create the provider.
	 */
	dtrace_dofprov2hprov(&dhpv, provider, strtab);

	if ((parg = mops->dtms_provide_pid(meta->dtm_arg, &dhpv, pid)) == NULL)
		return;

	meta->dtm_count++;

	/*
	 * Create the probes.
	 */
	for (i = 0; i < nprobes; i++) {
		probe = (dof_probe_t *)(uintptr_t)(daddr +
		    prb_sec->dofs_offset + i * prb_sec->dofs_entsize);

		/* See the check in dtrace_helper_provider_validate(). */
		if (strlen(strtab + probe->dofpr_func) >= DTRACE_FUNCNAMELEN)
			continue;

		dhpb.dthpb_mod = dhp->dofhp_mod;
		dhpb.dthpb_func = strtab + probe->dofpr_func;
		dhpb.dthpb_name = strtab + probe->dofpr_name;
		dhpb.dthpb_base = probe->dofpr_addr;
		dhpb.dthpb_offs = off + probe->dofpr_offidx;
		dhpb.dthpb_noffs = probe->dofpr_noffs;
		if (enoff != NULL) {
			dhpb.dthpb_enoffs = enoff + probe->dofpr_enoffidx;
			dhpb.dthpb_nenoffs = probe->dofpr_nenoffs;
		} else {
			dhpb.dthpb_enoffs = NULL;
			dhpb.dthpb_nenoffs = 0;
		}
		dhpb.dthpb_args = arg + probe->dofpr_argidx;
		dhpb.dthpb_nargc = probe->dofpr_nargc;
		dhpb.dthpb_xargc = probe->dofpr_xargc;
		dhpb.dthpb_ntypes = strtab + probe->dofpr_nargv;
		dhpb.dthpb_xtypes = strtab + probe->dofpr_xargv;

		mops->dtms_create_probe(meta->dtm_arg, parg, &dhpb);
	}
}

static void
dtrace_helper_provide(dof_helper_t *dhp, pid_t pid)
{
	uintptr_t daddr = (uintptr_t)dhp->dofhp_dof;
	dof_hdr_t *dof = (dof_hdr_t *)daddr;
	int i;

	ASSERT(MUTEX_HELD(&dtrace_meta_lock));

	for (i = 0; i < dof->dofh_secnum; i++) {
		dof_sec_t *sec = (dof_sec_t *)(uintptr_t)(daddr +
		    dof->dofh_secoff + i * dof->dofh_secsize);

		if (sec->dofs_type != DOF_SECT_PROVIDER)
			continue;

		dtrace_helper_provide_one(dhp, sec, pid);
	}

	/*
	 * We may have just created probes, so we must now rematch against
	 * any retained enablings.  Note that this call will acquire both
	 * cpu_lock and dtrace_lock; the fact that we are holding
	 * dtrace_meta_lock now is what defines the ordering with respect to
	 * these three locks.
	 */
	dtrace_enabling_matchall();
}

static void
dtrace_helper_provider_remove_one(dof_helper_t *dhp, dof_sec_t *sec, pid_t pid)
{
	uintptr_t daddr = (uintptr_t)dhp->dofhp_dof;
	dof_hdr_t *dof = (dof_hdr_t *)daddr;
	dof_sec_t *str_sec;
	dof_provider_t *provider;
	char *strtab;
	dtrace_helper_provdesc_t dhpv;
	dtrace_meta_t *meta = dtrace_meta_pid;
	dtrace_mops_t *mops = &meta->dtm_mops;

	provider = (dof_provider_t *)(uintptr_t)(daddr + sec->dofs_offset);
	str_sec = (dof_sec_t *)(uintptr_t)(daddr + dof->dofh_secoff +
	    provider->dofpv_strtab * dof->dofh_secsize);

	strtab = (char *)(uintptr_t)(daddr + str_sec->dofs_offset);

	/*
	 * Create the provider.
	 */
	dtrace_dofprov2hprov(&dhpv, provider, strtab);

	mops->dtms_remove_pid(meta->dtm_arg, &dhpv, pid);

	meta->dtm_count--;
}

static void
dtrace_helper_provider_remove(dof_helper_t *dhp, pid_t pid)
{
	uintptr_t daddr = (uintptr_t)dhp->dofhp_dof;
	dof_hdr_t *dof = (dof_hdr_t *)daddr;
	int i;

	ASSERT(MUTEX_HELD(&dtrace_meta_lock));

	for (i = 0; i < dof->dofh_secnum; i++) {
		dof_sec_t *sec = (dof_sec_t *)(uintptr_t)(daddr +
		    dof->dofh_secoff + i * dof->dofh_secsize);

		if (sec->dofs_type != DOF_SECT_PROVIDER)
			continue;

		dtrace_helper_provider_remove_one(dhp, sec, pid);
	}
}

/*
 * DTrace Meta Provider-to-Framework API Functions
 *
 * These functions implement the Meta Provider-to-Framework API, as described
 * in <sys/dtrace.h>.
 */
int
dtrace_meta_register(const char *name, const dtrace_mops_t *mops, void *arg,
    dtrace_meta_provider_id_t *idp)
{
	dtrace_meta_t *meta;
	dtrace_helpers_t *help, *next;
	int i;

	*idp = DTRACE_METAPROVNONE;

	/*
	 * We strictly don't need the name, but we hold onto it for
	 * debuggability. All hail error queues!
	 */
	if (name == NULL) {
		cmn_err(CE_WARN, "failed to register meta-provider: "
		    "invalid name");
		return (EINVAL);
	}

	if (mops == NULL ||
	    mops->dtms_create_probe == NULL ||
	    mops->dtms_provide_pid == NULL ||
	    mops->dtms_remove_pid == NULL) {
		cmn_err(CE_WARN, "failed to register meta-register %s: "
		    "invalid ops", name);
		return (EINVAL);
	}

	meta = kmem_zalloc(sizeof (dtrace_meta_t), KM_SLEEP);
	meta->dtm_mops = *mops;
	meta->dtm_name = kmem_alloc(strlen(name) + 1, KM_SLEEP);
	(void) strcpy(meta->dtm_name, name);
	meta->dtm_arg = arg;

	mutex_enter(&dtrace_meta_lock);
	mutex_enter(&dtrace_lock);

	if (dtrace_meta_pid != NULL) {
		mutex_exit(&dtrace_lock);
		mutex_exit(&dtrace_meta_lock);
		cmn_err(CE_WARN, "failed to register meta-register %s: "
		    "user-land meta-provider exists", name);
		kmem_free(meta->dtm_name, strlen(meta->dtm_name) + 1);
		kmem_free(meta, sizeof (dtrace_meta_t));
		return (EINVAL);
	}

	dtrace_meta_pid = meta;
	*idp = (dtrace_meta_provider_id_t)meta;

	/*
	 * If there are providers and probes ready to go, pass them
	 * off to the new meta provider now.
	 */

	help = dtrace_deferred_pid;
	dtrace_deferred_pid = NULL;

	mutex_exit(&dtrace_lock);

	while (help != NULL) {
		for (i = 0; i < help->dthps_nprovs; i++) {
			dtrace_helper_provide(&help->dthps_provs[i]->dthp_prov,
			    help->dthps_pid);
		}

		next = help->dthps_next;
		help->dthps_next = NULL;
		help->dthps_prev = NULL;
		help->dthps_deferred = 0;
		help = next;
	}

	mutex_exit(&dtrace_meta_lock);

	return (0);
}

int
dtrace_meta_unregister(dtrace_meta_provider_id_t id)
{
	dtrace_meta_t **pp, *old = (dtrace_meta_t *)id;

	mutex_enter(&dtrace_meta_lock);
	mutex_enter(&dtrace_lock);

	if (old == dtrace_meta_pid) {
		pp = &dtrace_meta_pid;
	} else {
		panic("attempt to unregister non-existent "
		    "dtrace meta-provider %p\n", (void *)old);
	}

	if (old->dtm_count != 0) {
		mutex_exit(&dtrace_lock);
		mutex_exit(&dtrace_meta_lock);
		return (EBUSY);
	}

	*pp = NULL;

	mutex_exit(&dtrace_lock);
	mutex_exit(&dtrace_meta_lock);

	kmem_free(old->dtm_name, strlen(old->dtm_name) + 1);
	kmem_free(old, sizeof (dtrace_meta_t));

	return (0);
}


/*
 * DTrace DIF Object Functions
 */
static int
dtrace_difo_err(uint_t pc, const char *format, ...)
{
	if (dtrace_err_verbose) {
		va_list alist;

		(void) uprintf("dtrace DIF object error: [%u]: ", pc);
		va_start(alist, format);
		(void) vuprintf(format, alist);
		va_end(alist);
	}

#ifdef DTRACE_ERRDEBUG
	dtrace_errdebug(format);
#endif
	return (1);
}

/*
 * Validate a DTrace DIF object by checking the IR instructions.  The following
 * rules are currently enforced by dtrace_difo_validate():
 *
 * 1. Each instruction must have a valid opcode
 * 2. Each register, string, variable, or subroutine reference must be valid
 * 3. No instruction can modify register %r0 (must be zero)
 * 4. All instruction reserved bits must be set to zero
 * 5. The last instruction must be a "ret" instruction
 * 6. All branch targets must reference a valid instruction _after_ the branch
 */
static int
dtrace_difo_validate(dtrace_difo_t *dp, dtrace_vstate_t *vstate, uint_t nregs,
    cred_t *cr)
{
	int err = 0, i;
	int (*efunc)(uint_t pc, const char *, ...) = dtrace_difo_err;
	int kcheckload;
	uint_t pc;
	int maxglobal = -1, maxlocal = -1, maxtlocal = -1;

	kcheckload = cr == NULL ||
	    (vstate->dtvs_state->dts_cred.dcr_visible & DTRACE_CRV_KERNEL) == 0;

	dp->dtdo_destructive = 0;

	for (pc = 0; pc < dp->dtdo_len && err == 0; pc++) {
		dif_instr_t instr = dp->dtdo_buf[pc];

		uint_t r1 = DIF_INSTR_R1(instr);
		uint_t r2 = DIF_INSTR_R2(instr);
		uint_t rd = DIF_INSTR_RD(instr);
		uint_t rs = DIF_INSTR_RS(instr);
		uint_t label = DIF_INSTR_LABEL(instr);
		uint_t v = DIF_INSTR_VAR(instr);
		uint_t subr = DIF_INSTR_SUBR(instr);
		uint_t type = DIF_INSTR_TYPE(instr);
		uint_t op = DIF_INSTR_OP(instr);

		switch (op) {
		case DIF_OP_OR:
		case DIF_OP_XOR:
		case DIF_OP_AND:
		case DIF_OP_SLL:
		case DIF_OP_SRL:
		case DIF_OP_SRA:
		case DIF_OP_SUB:
		case DIF_OP_ADD:
		case DIF_OP_MUL:
		case DIF_OP_SDIV:
		case DIF_OP_UDIV:
		case DIF_OP_SREM:
		case DIF_OP_UREM:
		case DIF_OP_COPYS:
			if (r1 >= nregs)
				err += efunc(pc, "invalid register %u\n", r1);
			if (r2 >= nregs)
				err += efunc(pc, "invalid register %u\n", r2);
			if (rd >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			if (rd == 0)
				err += efunc(pc, "cannot write to %r0\n");
			break;
		case DIF_OP_NOT:
		case DIF_OP_MOV:
		case DIF_OP_ALLOCS:
			if (r1 >= nregs)
				err += efunc(pc, "invalid register %u\n", r1);
			if (r2 != 0)
				err += efunc(pc, "non-zero reserved bits\n");
			if (rd >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			if (rd == 0)
				err += efunc(pc, "cannot write to %r0\n");
			break;
		case DIF_OP_LDSB:
		case DIF_OP_LDSH:
		case DIF_OP_LDSW:
		case DIF_OP_LDUB:
		case DIF_OP_LDUH:
		case DIF_OP_LDUW:
		case DIF_OP_LDX:
			if (r1 >= nregs)
				err += efunc(pc, "invalid register %u\n", r1);
			if (r2 != 0)
				err += efunc(pc, "non-zero reserved bits\n");
			if (rd >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			if (rd == 0)
				err += efunc(pc, "cannot write to %r0\n");
			if (kcheckload)
				dp->dtdo_buf[pc] = DIF_INSTR_LOAD(op +
				    DIF_OP_RLDSB - DIF_OP_LDSB, r1, rd);
			break;
		case DIF_OP_RLDSB:
		case DIF_OP_RLDSH:
		case DIF_OP_RLDSW:
		case DIF_OP_RLDUB:
		case DIF_OP_RLDUH:
		case DIF_OP_RLDUW:
		case DIF_OP_RLDX:
			if (r1 >= nregs)
				err += efunc(pc, "invalid register %u\n", r1);
			if (r2 != 0)
				err += efunc(pc, "non-zero reserved bits\n");
			if (rd >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			if (rd == 0)
				err += efunc(pc, "cannot write to %r0\n");
			break;
		case DIF_OP_ULDSB:
		case DIF_OP_ULDSH:
		case DIF_OP_ULDSW:
		case DIF_OP_ULDUB:
		case DIF_OP_ULDUH:
		case DIF_OP_ULDUW:
		case DIF_OP_ULDX:
			if (r1 >= nregs)
				err += efunc(pc, "invalid register %u\n", r1);
			if (r2 != 0)
				err += efunc(pc, "non-zero reserved bits\n");
			if (rd >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			if (rd == 0)
				err += efunc(pc, "cannot write to %r0\n");
			break;
		case DIF_OP_STB:
		case DIF_OP_STH:
		case DIF_OP_STW:
		case DIF_OP_STX:
			if (r1 >= nregs)
				err += efunc(pc, "invalid register %u\n", r1);
			if (r2 != 0)
				err += efunc(pc, "non-zero reserved bits\n");
			if (rd >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			if (rd == 0)
				err += efunc(pc, "cannot write to 0 address\n");
			break;
		case DIF_OP_CMP:
		case DIF_OP_SCMP:
			if (r1 >= nregs)
				err += efunc(pc, "invalid register %u\n", r1);
			if (r2 >= nregs)
				err += efunc(pc, "invalid register %u\n", r2);
			if (rd != 0)
				err += efunc(pc, "non-zero reserved bits\n");
			break;
		case DIF_OP_TST:
			if (r1 >= nregs)
				err += efunc(pc, "invalid register %u\n", r1);
			if (r2 != 0 || rd != 0)
				err += efunc(pc, "non-zero reserved bits\n");
			break;
		case DIF_OP_BA:
		case DIF_OP_BE:
		case DIF_OP_BNE:
		case DIF_OP_BG:
		case DIF_OP_BGU:
		case DIF_OP_BGE:
		case DIF_OP_BGEU:
		case DIF_OP_BL:
		case DIF_OP_BLU:
		case DIF_OP_BLE:
		case DIF_OP_BLEU:
			if (label >= dp->dtdo_len) {
				err += efunc(pc, "invalid branch target %u\n",
				    label);
			}
			if (label <= pc) {
				err += efunc(pc, "backward branch to %u\n",
				    label);
			}
			break;
		case DIF_OP_RET:
			if (r1 != 0 || r2 != 0)
				err += efunc(pc, "non-zero reserved bits\n");
			if (rd >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			break;
		case DIF_OP_NOP:
		case DIF_OP_POPTS:
		case DIF_OP_FLUSHTS:
			if (r1 != 0 || r2 != 0 || rd != 0)
				err += efunc(pc, "non-zero reserved bits\n");
			break;
		case DIF_OP_SETX:
			if (DIF_INSTR_INTEGER(instr) >= dp->dtdo_intlen) {
				err += efunc(pc, "invalid integer ref %u\n",
				    DIF_INSTR_INTEGER(instr));
			}
			if (rd >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			if (rd == 0)
				err += efunc(pc, "cannot write to %r0\n");
			break;
		case DIF_OP_SETS:
			if (DIF_INSTR_STRING(instr) >= dp->dtdo_strlen) {
				err += efunc(pc, "invalid string ref %u\n",
				    DIF_INSTR_STRING(instr));
			}
			if (rd >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			if (rd == 0)
				err += efunc(pc, "cannot write to %r0\n");
			break;
		case DIF_OP_LDGA:
		case DIF_OP_LDTA:
			if (r1 > DIF_VAR_ARRAY_MAX)
				err += efunc(pc, "invalid array %u\n", r1);
			if (r2 >= nregs)
				err += efunc(pc, "invalid register %u\n", r2);
			if (rd >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			if (rd == 0)
				err += efunc(pc, "cannot write to %r0\n");
			break;
		case DIF_OP_LDGS:
		case DIF_OP_LDTS:
		case DIF_OP_LDLS:
		case DIF_OP_LDGAA:
		case DIF_OP_LDTAA:
			if (v < DIF_VAR_OTHER_MIN || v > DIF_VAR_OTHER_MAX)
				err += efunc(pc, "invalid variable %u\n", v);
			if (rd >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			if (rd == 0)
				err += efunc(pc, "cannot write to %r0\n");
			break;
		case DIF_OP_STGS:
		case DIF_OP_STTS:
		case DIF_OP_STLS:
		case DIF_OP_STGAA:
		case DIF_OP_STTAA:
			if (v < DIF_VAR_OTHER_UBASE || v > DIF_VAR_OTHER_MAX)
				err += efunc(pc, "invalid variable %u\n", v);
			if (rs >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			break;
		case DIF_OP_CALL:
			if (subr > DIF_SUBR_MAX)
				err += efunc(pc, "invalid subr %u\n", subr);
			if (rd >= nregs)
				err += efunc(pc, "invalid register %u\n", rd);
			if (rd == 0)
				err += efunc(pc, "cannot write to %r0\n");

			if (subr == DIF_SUBR_COPYOUT ||
			    subr == DIF_SUBR_COPYOUTSTR) {
				dp->dtdo_destructive = 1;
			}

			if (subr == DIF_SUBR_GETF) {
				/*
				 * If we have a getf() we need to record that
				 * in our state.  Note that our state can be
				 * NULL if this is a helper -- but in that
				 * case, the call to getf() is itself illegal,
				 * and will be caught (slightly later) when
				 * the helper is validated.
				 */
				if (vstate->dtvs_state != NULL)
					vstate->dtvs_state->dts_getf++;
			}

			break;
		case DIF_OP_PUSHTR:
			if (type != DIF_TYPE_STRING && type != DIF_TYPE_CTF)
				err += efunc(pc, "invalid ref type %u\n", type);
			if (r2 >= nregs)
				err += efunc(pc, "invalid register %u\n", r2);
			if (rs >= nregs)
				err += efunc(pc, "invalid register %u\n", rs);
			break;
		case DIF_OP_PUSHTV:
			if (type != DIF_TYPE_CTF)
				err += efunc(pc, "invalid val type %u\n", type);
			if (r2 >= nregs)
				err += efunc(pc, "invalid register %u\n", r2);
			if (rs >= nregs)
				err += efunc(pc, "invalid register %u\n", rs);
			break;
		default:
			err += efunc(pc, "invalid opcode %u\n",
			    DIF_INSTR_OP(instr));
		}
	}

	if (dp->dtdo_len != 0 &&
	    DIF_INSTR_OP(dp->dtdo_buf[dp->dtdo_len - 1]) != DIF_OP_RET) {
		err += efunc(dp->dtdo_len - 1,
		    "expected 'ret' as last DIF instruction\n");
	}

	if (!(dp->dtdo_rtype.dtdt_flags & (DIF_TF_BYREF | DIF_TF_BYUREF))) {
		/*
		 * If we're not returning by reference, the size must be either
		 * 0 or the size of one of the base types.
		 */
		switch (dp->dtdo_rtype.dtdt_size) {
		case 0:
		case sizeof (uint8_t):
		case sizeof (uint16_t):
		case sizeof (uint32_t):
		case sizeof (uint64_t):
			break;

		default:
			err += efunc(dp->dtdo_len - 1, "bad return size\n");
		}
	}

	for (i = 0; i < dp->dtdo_varlen && err == 0; i++) {
		dtrace_difv_t *v = &dp->dtdo_vartab[i], *existing = NULL;
		dtrace_diftype_t *vt, *et;
		uint_t id, ndx;

		if (v->dtdv_scope != DIFV_SCOPE_GLOBAL &&
		    v->dtdv_scope != DIFV_SCOPE_THREAD &&
		    v->dtdv_scope != DIFV_SCOPE_LOCAL) {
			err += efunc(i, "unrecognized variable scope %d\n",
			    v->dtdv_scope);
			break;
		}

		if (v->dtdv_kind != DIFV_KIND_ARRAY &&
		    v->dtdv_kind != DIFV_KIND_SCALAR) {
			err += efunc(i, "unrecognized variable type %d\n",
			    v->dtdv_kind);
			break;
		}

		if ((id = v->dtdv_id) > DIF_VARIABLE_MAX) {
			err += efunc(i, "%d exceeds variable id limit\n", id);
			break;
		}

		if (id < DIF_VAR_OTHER_UBASE)
			continue;

		/*
		 * For user-defined variables, we need to check that this
		 * definition is identical to any previous definition that we
		 * encountered.
		 */
		ndx = id - DIF_VAR_OTHER_UBASE;

		switch (v->dtdv_scope) {
		case DIFV_SCOPE_GLOBAL:
			if (maxglobal == -1 || ndx > maxglobal)
				maxglobal = ndx;

			if (ndx < vstate->dtvs_nglobals) {
				dtrace_statvar_t *svar;

				if ((svar = vstate->dtvs_globals[ndx]) != NULL)
					existing = &svar->dtsv_var;
			}

			break;

		case DIFV_SCOPE_THREAD:
			if (maxtlocal == -1 || ndx > maxtlocal)
				maxtlocal = ndx;

			if (ndx < vstate->dtvs_ntlocals)
				existing = &vstate->dtvs_tlocals[ndx];
			break;

		case DIFV_SCOPE_LOCAL:
			if (maxlocal == -1 || ndx > maxlocal)
				maxlocal = ndx;

			if (ndx < vstate->dtvs_nlocals) {
				dtrace_statvar_t *svar;

				if ((svar = vstate->dtvs_locals[ndx]) != NULL)
					existing = &svar->dtsv_var;
			}

			break;
		}

		vt = &v->dtdv_type;

		if (vt->dtdt_flags & DIF_TF_BYREF) {
			if (vt->dtdt_size == 0) {
				err += efunc(i, "zero-sized variable\n");
				break;
			}

			if ((v->dtdv_scope == DIFV_SCOPE_GLOBAL ||
			    v->dtdv_scope == DIFV_SCOPE_LOCAL) &&
			    vt->dtdt_size > dtrace_statvar_maxsize) {
				err += efunc(i, "oversized by-ref static\n");
				break;
			}
		}

		if (existing == NULL || existing->dtdv_id == 0)
			continue;

		ASSERT(existing->dtdv_id == v->dtdv_id);
		ASSERT(existing->dtdv_scope == v->dtdv_scope);

		if (existing->dtdv_kind != v->dtdv_kind)
			err += efunc(i, "%d changed variable kind\n", id);

		et = &existing->dtdv_type;

		if (vt->dtdt_flags != et->dtdt_flags) {
			err += efunc(i, "%d changed variable type flags\n", id);
			break;
		}

		if (vt->dtdt_size != 0 && vt->dtdt_size != et->dtdt_size) {
			err += efunc(i, "%d changed variable type size\n", id);
			break;
		}
	}

	for (pc = 0; pc < dp->dtdo_len && err == 0; pc++) {
		dif_instr_t instr = dp->dtdo_buf[pc];

		uint_t v = DIF_INSTR_VAR(instr);
		uint_t op = DIF_INSTR_OP(instr);

		switch (op) {
		case DIF_OP_LDGS:
		case DIF_OP_LDGAA:
		case DIF_OP_STGS:
		case DIF_OP_STGAA:
			if (v > DIF_VAR_OTHER_UBASE + maxglobal)
				err += efunc(pc, "invalid variable %u\n", v);
			break;
		case DIF_OP_LDTS:
		case DIF_OP_LDTAA:
		case DIF_OP_STTS:
		case DIF_OP_STTAA:
			if (v > DIF_VAR_OTHER_UBASE + maxtlocal)
				err += efunc(pc, "invalid variable %u\n", v);
			break;
		case DIF_OP_LDLS:
		case DIF_OP_STLS:
			if (v > DIF_VAR_OTHER_UBASE + maxlocal)
				err += efunc(pc, "invalid variable %u\n", v);
			break;
		default:
			break;
		}
	}

	return (err);
}

/*
 * Validate a DTrace DIF object that it is to be used as a helper.  Helpers
 * are much more constrained than normal DIFOs.  Specifically, they may
 * not:
 *
 * 1. Make calls to subroutines other than copyin(), copyinstr() or
 *    miscellaneous string routines
 * 2. Access DTrace variables other than the args[] array, and the
 *    curthread, pid, ppid, tid, execname, zonename, uid and gid variables.
 * 3. Have thread-local variables.
 * 4. Have dynamic variables.
 */
static int
dtrace_difo_validate_helper(dtrace_difo_t *dp)
{
	int (*efunc)(uint_t pc, const char *, ...) = dtrace_difo_err;
	int err = 0;
	uint_t pc;

	for (pc = 0; pc < dp->dtdo_len; pc++) {
		dif_instr_t instr = dp->dtdo_buf[pc];

		uint_t v = DIF_INSTR_VAR(instr);
		uint_t subr = DIF_INSTR_SUBR(instr);
		uint_t op = DIF_INSTR_OP(instr);

		switch (op) {
		case DIF_OP_OR:
		case DIF_OP_XOR:
		case DIF_OP_AND:
		case DIF_OP_SLL:
		case DIF_OP_SRL:
		case DIF_OP_SRA:
		case DIF_OP_SUB:
		case DIF_OP_ADD:
		case DIF_OP_MUL:
		case DIF_OP_SDIV:
		case DIF_OP_UDIV:
		case DIF_OP_SREM:
		case DIF_OP_UREM:
		case DIF_OP_COPYS:
		case DIF_OP_NOT:
		case DIF_OP_MOV:
		case DIF_OP_RLDSB:
		case DIF_OP_RLDSH:
		case DIF_OP_RLDSW:
		case DIF_OP_RLDUB:
		case DIF_OP_RLDUH:
		case DIF_OP_RLDUW:
		case DIF_OP_RLDX:
		case DIF_OP_ULDSB:
		case DIF_OP_ULDSH:
		case DIF_OP_ULDSW:
		case DIF_OP_ULDUB:
		case DIF_OP_ULDUH:
		case DIF_OP_ULDUW:
		case DIF_OP_ULDX:
		case DIF_OP_STB:
		case DIF_OP_STH:
		case DIF_OP_STW:
		case DIF_OP_STX:
		case DIF_OP_ALLOCS:
		case DIF_OP_CMP:
		case DIF_OP_SCMP:
		case DIF_OP_TST:
		case DIF_OP_BA:
		case DIF_OP_BE:
		case DIF_OP_BNE:
		case DIF_OP_BG:
		case DIF_OP_BGU:
		case DIF_OP_BGE:
		case DIF_OP_BGEU:
		case DIF_OP_BL:
		case DIF_OP_BLU:
		case DIF_OP_BLE:
		case DIF_OP_BLEU:
		case DIF_OP_RET:
		case DIF_OP_NOP:
		case DIF_OP_POPTS:
		case DIF_OP_FLUSHTS:
		case DIF_OP_SETX:
		case DIF_OP_SETS:
		case DIF_OP_LDGA:
		case DIF_OP_LDLS:
		case DIF_OP_STGS:
		case DIF_OP_STLS:
		case DIF_OP_PUSHTR:
		case DIF_OP_PUSHTV:
			break;

		case DIF_OP_LDGS:
			if (v >= DIF_VAR_OTHER_UBASE)
				break;

			if (v >= DIF_VAR_ARG0 && v <= DIF_VAR_ARG9)
				break;

			if (v == DIF_VAR_CURTHREAD || v == DIF_VAR_PID ||
			    v == DIF_VAR_PPID || v == DIF_VAR_TID ||
			    v == DIF_VAR_EXECARGS ||
			    v == DIF_VAR_EXECNAME || v == DIF_VAR_ZONENAME ||
			    v == DIF_VAR_UID || v == DIF_VAR_GID)
				break;

			err += efunc(pc, "illegal variable %u\n", v);
			break;

		case DIF_OP_LDTA:
		case DIF_OP_LDTS:
		case DIF_OP_LDGAA:
		case DIF_OP_LDTAA:
			err += efunc(pc, "illegal dynamic variable load\n");
			break;

		case DIF_OP_STTS:
		case DIF_OP_STGAA:
		case DIF_OP_STTAA:
			err += efunc(pc, "illegal dynamic variable store\n");
			break;

		case DIF_OP_CALL:
			if (subr == DIF_SUBR_ALLOCA ||
			    subr == DIF_SUBR_BCOPY ||
			    subr == DIF_SUBR_COPYIN ||
			    subr == DIF_SUBR_COPYINTO ||
			    subr == DIF_SUBR_COPYINSTR ||
			    subr == DIF_SUBR_INDEX ||
			    subr == DIF_SUBR_INET_NTOA ||
			    subr == DIF_SUBR_INET_NTOA6 ||
			    subr == DIF_SUBR_INET_NTOP ||
			    subr == DIF_SUBR_JSON ||
			    subr == DIF_SUBR_LLTOSTR ||
			    subr == DIF_SUBR_STRTOLL ||
			    subr == DIF_SUBR_RINDEX ||
			    subr == DIF_SUBR_STRCHR ||
			    subr == DIF_SUBR_STRJOIN ||
			    subr == DIF_SUBR_STRRCHR ||
			    subr == DIF_SUBR_STRSTR ||
			    subr == DIF_SUBR_HTONS ||
			    subr == DIF_SUBR_HTONL ||
			    subr == DIF_SUBR_HTONLL ||
			    subr == DIF_SUBR_NTOHS ||
			    subr == DIF_SUBR_NTOHL ||
			    subr == DIF_SUBR_NTOHLL ||
			    subr == DIF_SUBR_MEMREF)
				break;
#ifdef __FreeBSD__
			if (subr == DIF_SUBR_MEMSTR)
				break;
#endif

			err += efunc(pc, "invalid subr %u\n", subr);
			break;

		default:
			err += efunc(pc, "invalid opcode %u\n",
			    DIF_INSTR_OP(instr));
		}
	}

	return (err);
}

/*
 * Returns 1 if the expression in the DIF object can be cached on a per-thread
 * basis; 0 if not.
 */
static int
dtrace_difo_cacheable(dtrace_difo_t *dp)
{
	int i;

	if (dp == NULL)
		return (0);

	for (i = 0; i < dp->dtdo_varlen; i++) {
		dtrace_difv_t *v = &dp->dtdo_vartab[i];

		if (v->dtdv_scope != DIFV_SCOPE_GLOBAL)
			continue;

		switch (v->dtdv_id) {
		case DIF_VAR_CURTHREAD:
		case DIF_VAR_PID:
		case DIF_VAR_TID:
		case DIF_VAR_EXECARGS:
		case DIF_VAR_EXECNAME:
		case DIF_VAR_ZONENAME:
			break;

		default:
			return (0);
		}
	}

	/*
	 * This DIF object may be cacheable.  Now we need to look for any
	 * array loading instructions, any memory loading instructions, or
	 * any stores to thread-local variables.
	 */
	for (i = 0; i < dp->dtdo_len; i++) {
		uint_t op = DIF_INSTR_OP(dp->dtdo_buf[i]);

		if ((op >= DIF_OP_LDSB && op <= DIF_OP_LDX) ||
		    (op >= DIF_OP_ULDSB && op <= DIF_OP_ULDX) ||
		    (op >= DIF_OP_RLDSB && op <= DIF_OP_RLDX) ||
		    op == DIF_OP_LDGA || op == DIF_OP_STTS)
			return (0);
	}

	return (1);
}

static void
dtrace_difo_hold(dtrace_difo_t *dp)
{
	int i;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	dp->dtdo_refcnt++;
	ASSERT(dp->dtdo_refcnt != 0);

	/*
	 * We need to check this DIF object for references to the variable
	 * DIF_VAR_VTIMESTAMP.
	 */
	for (i = 0; i < dp->dtdo_varlen; i++) {
		dtrace_difv_t *v = &dp->dtdo_vartab[i];

		if (v->dtdv_id != DIF_VAR_VTIMESTAMP)
			continue;

		if (dtrace_vtime_references++ == 0)
			dtrace_vtime_enable();
	}
}

/*
 * This routine calculates the dynamic variable chunksize for a given DIF
 * object.  The calculation is not fool-proof, and can probably be tricked by
 * malicious DIF -- but it works for all compiler-generated DIF.  Because this
 * calculation is likely imperfect, dtrace_dynvar() is able to gracefully fail
 * if a dynamic variable size exceeds the chunksize.
 */
static void
dtrace_difo_chunksize(dtrace_difo_t *dp, dtrace_vstate_t *vstate)
{
	uint64_t sval = 0;
	dtrace_key_t tupregs[DIF_DTR_NREGS + 2]; /* +2 for thread and id */
	const dif_instr_t *text = dp->dtdo_buf;
	uint_t pc, srd = 0;
	uint_t ttop = 0;
	size_t size, ksize;
	uint_t id, i;

	for (pc = 0; pc < dp->dtdo_len; pc++) {
		dif_instr_t instr = text[pc];
		uint_t op = DIF_INSTR_OP(instr);
		uint_t rd = DIF_INSTR_RD(instr);
		uint_t r1 = DIF_INSTR_R1(instr);
		uint_t nkeys = 0;
		uchar_t scope = 0;

		dtrace_key_t *key = tupregs;

		switch (op) {
		case DIF_OP_SETX:
			sval = dp->dtdo_inttab[DIF_INSTR_INTEGER(instr)];
			srd = rd;
			continue;

		case DIF_OP_STTS:
			key = &tupregs[DIF_DTR_NREGS];
			key[0].dttk_size = 0;
			key[1].dttk_size = 0;
			nkeys = 2;
			scope = DIFV_SCOPE_THREAD;
			break;

		case DIF_OP_STGAA:
		case DIF_OP_STTAA:
			nkeys = ttop;

			if (DIF_INSTR_OP(instr) == DIF_OP_STTAA)
				key[nkeys++].dttk_size = 0;

			key[nkeys++].dttk_size = 0;

			if (op == DIF_OP_STTAA) {
				scope = DIFV_SCOPE_THREAD;
			} else {
				scope = DIFV_SCOPE_GLOBAL;
			}

			break;

		case DIF_OP_PUSHTR:
			if (ttop == DIF_DTR_NREGS)
				return;

			if ((srd == 0 || sval == 0) && r1 == DIF_TYPE_STRING) {
				/*
				 * If the register for the size of the "pushtr"
				 * is %r0 (or the value is 0) and the type is
				 * a string, we'll use the system-wide default
				 * string size.
				 */
				tupregs[ttop++].dttk_size =
				    dtrace_strsize_default;
			} else {
				if (srd == 0)
					return;

				if (sval > LONG_MAX)
					return;

				tupregs[ttop++].dttk_size = sval;
			}

			break;

		case DIF_OP_PUSHTV:
			if (ttop == DIF_DTR_NREGS)
				return;

			tupregs[ttop++].dttk_size = 0;
			break;

		case DIF_OP_FLUSHTS:
			ttop = 0;
			break;

		case DIF_OP_POPTS:
			if (ttop != 0)
				ttop--;
			break;
		}

		sval = 0;
		srd = 0;

		if (nkeys == 0)
			continue;

		/*
		 * We have a dynamic variable allocation; calculate its size.
		 */
		for (ksize = 0, i = 0; i < nkeys; i++)
			ksize += P2ROUNDUP(key[i].dttk_size, sizeof (uint64_t));

		size = sizeof (dtrace_dynvar_t);
		size += sizeof (dtrace_key_t) * (nkeys - 1);
		size += ksize;

		/*
		 * Now we need to determine the size of the stored data.
		 */
		id = DIF_INSTR_VAR(instr);

		for (i = 0; i < dp->dtdo_varlen; i++) {
			dtrace_difv_t *v = &dp->dtdo_vartab[i];

			if (v->dtdv_id == id && v->dtdv_scope == scope) {
				size += v->dtdv_type.dtdt_size;
				break;
			}
		}

		if (i == dp->dtdo_varlen)
			return;

		/*
		 * We have the size.  If this is larger than the chunk size
		 * for our dynamic variable state, reset the chunk size.
		 */
		size = P2ROUNDUP(size, sizeof (uint64_t));

		/*
		 * Before setting the chunk size, check that we're not going
		 * to set it to a negative value...
		 */
		if (size > LONG_MAX)
			return;

		/*
		 * ...and make certain that we didn't badly overflow.
		 */
		if (size < ksize || size < sizeof (dtrace_dynvar_t))
			return;

		if (size > vstate->dtvs_dynvars.dtds_chunksize)
			vstate->dtvs_dynvars.dtds_chunksize = size;
	}
}

static void
dtrace_difo_init(dtrace_difo_t *dp, dtrace_vstate_t *vstate)
{
	int i, oldsvars, osz, nsz, otlocals, ntlocals;
	uint_t id;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(dp->dtdo_buf != NULL && dp->dtdo_len != 0);

	for (i = 0; i < dp->dtdo_varlen; i++) {
		dtrace_difv_t *v = &dp->dtdo_vartab[i];
		dtrace_statvar_t *svar, ***svarp = NULL;
		size_t dsize = 0;
		uint8_t scope = v->dtdv_scope;
		int *np = NULL;

		if ((id = v->dtdv_id) < DIF_VAR_OTHER_UBASE)
			continue;

		id -= DIF_VAR_OTHER_UBASE;

		switch (scope) {
		case DIFV_SCOPE_THREAD:
			while (id >= (otlocals = vstate->dtvs_ntlocals)) {
				dtrace_difv_t *tlocals;

				if ((ntlocals = (otlocals << 1)) == 0)
					ntlocals = 1;

				osz = otlocals * sizeof (dtrace_difv_t);
				nsz = ntlocals * sizeof (dtrace_difv_t);

				tlocals = kmem_zalloc(nsz, KM_SLEEP);

				if (osz != 0) {
					bcopy(vstate->dtvs_tlocals,
					    tlocals, osz);
					kmem_free(vstate->dtvs_tlocals, osz);
				}

				vstate->dtvs_tlocals = tlocals;
				vstate->dtvs_ntlocals = ntlocals;
			}

			vstate->dtvs_tlocals[id] = *v;
			continue;

		case DIFV_SCOPE_LOCAL:
			np = &vstate->dtvs_nlocals;
			svarp = &vstate->dtvs_locals;

			if (v->dtdv_type.dtdt_flags & DIF_TF_BYREF)
				dsize = NCPU * (v->dtdv_type.dtdt_size +
				    sizeof (uint64_t));
			else
				dsize = NCPU * sizeof (uint64_t);

			break;

		case DIFV_SCOPE_GLOBAL:
			np = &vstate->dtvs_nglobals;
			svarp = &vstate->dtvs_globals;

			if (v->dtdv_type.dtdt_flags & DIF_TF_BYREF)
				dsize = v->dtdv_type.dtdt_size +
				    sizeof (uint64_t);

			break;

		default:
			ASSERT(0);
		}

		while (id >= (oldsvars = *np)) {
			dtrace_statvar_t **statics;
			int newsvars, oldsize, newsize;

			if ((newsvars = (oldsvars << 1)) == 0)
				newsvars = 1;

			oldsize = oldsvars * sizeof (dtrace_statvar_t *);
			newsize = newsvars * sizeof (dtrace_statvar_t *);

			statics = kmem_zalloc(newsize, KM_SLEEP);

			if (oldsize != 0) {
				bcopy(*svarp, statics, oldsize);
				kmem_free(*svarp, oldsize);
			}

			*svarp = statics;
			*np = newsvars;
		}

		if ((svar = (*svarp)[id]) == NULL) {
			svar = kmem_zalloc(sizeof (dtrace_statvar_t), KM_SLEEP);
			svar->dtsv_var = *v;

			if ((svar->dtsv_size = dsize) != 0) {
				svar->dtsv_data = (uint64_t)(uintptr_t)
				    kmem_zalloc(dsize, KM_SLEEP);
			}

			(*svarp)[id] = svar;
		}

		svar->dtsv_refcnt++;
	}

	dtrace_difo_chunksize(dp, vstate);
	dtrace_difo_hold(dp);
}

static dtrace_difo_t *
dtrace_difo_duplicate(dtrace_difo_t *dp, dtrace_vstate_t *vstate)
{
	dtrace_difo_t *new;
	size_t sz;

	ASSERT(dp->dtdo_buf != NULL);
	ASSERT(dp->dtdo_refcnt != 0);

	new = kmem_zalloc(sizeof (dtrace_difo_t), KM_SLEEP);

	ASSERT(dp->dtdo_buf != NULL);
	sz = dp->dtdo_len * sizeof (dif_instr_t);
	new->dtdo_buf = kmem_alloc(sz, KM_SLEEP);
	bcopy(dp->dtdo_buf, new->dtdo_buf, sz);
	new->dtdo_len = dp->dtdo_len;

	if (dp->dtdo_strtab != NULL) {
		ASSERT(dp->dtdo_strlen != 0);
		new->dtdo_strtab = kmem_alloc(dp->dtdo_strlen, KM_SLEEP);
		bcopy(dp->dtdo_strtab, new->dtdo_strtab, dp->dtdo_strlen);
		new->dtdo_strlen = dp->dtdo_strlen;
	}

	if (dp->dtdo_inttab != NULL) {
		ASSERT(dp->dtdo_intlen != 0);
		sz = dp->dtdo_intlen * sizeof (uint64_t);
		new->dtdo_inttab = kmem_alloc(sz, KM_SLEEP);
		bcopy(dp->dtdo_inttab, new->dtdo_inttab, sz);
		new->dtdo_intlen = dp->dtdo_intlen;
	}

	if (dp->dtdo_vartab != NULL) {
		ASSERT(dp->dtdo_varlen != 0);
		sz = dp->dtdo_varlen * sizeof (dtrace_difv_t);
		new->dtdo_vartab = kmem_alloc(sz, KM_SLEEP);
		bcopy(dp->dtdo_vartab, new->dtdo_vartab, sz);
		new->dtdo_varlen = dp->dtdo_varlen;
	}

	dtrace_difo_init(new, vstate);
	return (new);
}

static void
dtrace_difo_destroy(dtrace_difo_t *dp, dtrace_vstate_t *vstate)
{
	int i;

	ASSERT(dp->dtdo_refcnt == 0);

	for (i = 0; i < dp->dtdo_varlen; i++) {
		dtrace_difv_t *v = &dp->dtdo_vartab[i];
		dtrace_statvar_t *svar, **svarp = NULL;
		uint_t id;
		uint8_t scope = v->dtdv_scope;
		int *np = NULL;

		switch (scope) {
		case DIFV_SCOPE_THREAD:
			continue;

		case DIFV_SCOPE_LOCAL:
			np = &vstate->dtvs_nlocals;
			svarp = vstate->dtvs_locals;
			break;

		case DIFV_SCOPE_GLOBAL:
			np = &vstate->dtvs_nglobals;
			svarp = vstate->dtvs_globals;
			break;

		default:
			ASSERT(0);
		}

		if ((id = v->dtdv_id) < DIF_VAR_OTHER_UBASE)
			continue;

		id -= DIF_VAR_OTHER_UBASE;
		ASSERT(id < *np);

		svar = svarp[id];
		ASSERT(svar != NULL);
		ASSERT(svar->dtsv_refcnt > 0);

		if (--svar->dtsv_refcnt > 0)
			continue;

		if (svar->dtsv_size != 0) {
			ASSERT(svar->dtsv_data != 0);
			kmem_free((void *)(uintptr_t)svar->dtsv_data,
			    svar->dtsv_size);
		}

		kmem_free(svar, sizeof (dtrace_statvar_t));
		svarp[id] = NULL;
	}

	if (dp->dtdo_buf != NULL)
		kmem_free(dp->dtdo_buf, dp->dtdo_len * sizeof (dif_instr_t));
	if (dp->dtdo_inttab != NULL)
		kmem_free(dp->dtdo_inttab, dp->dtdo_intlen * sizeof (uint64_t));
	if (dp->dtdo_strtab != NULL)
		kmem_free(dp->dtdo_strtab, dp->dtdo_strlen);
	if (dp->dtdo_vartab != NULL)
		kmem_free(dp->dtdo_vartab, dp->dtdo_varlen * sizeof (dtrace_difv_t));

	kmem_free(dp, sizeof (dtrace_difo_t));
}

static void
dtrace_difo_release(dtrace_difo_t *dp, dtrace_vstate_t *vstate)
{
	int i;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(dp->dtdo_refcnt != 0);

	for (i = 0; i < dp->dtdo_varlen; i++) {
		dtrace_difv_t *v = &dp->dtdo_vartab[i];

		if (v->dtdv_id != DIF_VAR_VTIMESTAMP)
			continue;

		ASSERT(dtrace_vtime_references > 0);
		if (--dtrace_vtime_references == 0)
			dtrace_vtime_disable();
	}

	if (--dp->dtdo_refcnt == 0)
		dtrace_difo_destroy(dp, vstate);
}

/*
 * DTrace Format Functions
 */
static uint16_t
dtrace_format_add(dtrace_state_t *state, char *str)
{
	char *fmt, **new;
	uint16_t ndx, len = strlen(str) + 1;

	fmt = kmem_zalloc(len, KM_SLEEP);
	bcopy(str, fmt, len);

	for (ndx = 0; ndx < state->dts_nformats; ndx++) {
		if (state->dts_formats[ndx] == NULL) {
			state->dts_formats[ndx] = fmt;
			return (ndx + 1);
		}
	}

	if (state->dts_nformats == USHRT_MAX) {
		/*
		 * This is only likely if a denial-of-service attack is being
		 * attempted.  As such, it's okay to fail silently here.
		 */
		kmem_free(fmt, len);
		return (0);
	}

	/*
	 * For simplicity, we always resize the formats array to be exactly the
	 * number of formats.
	 */
	ndx = state->dts_nformats++;
	new = kmem_alloc((ndx + 1) * sizeof (char *), KM_SLEEP);

	if (state->dts_formats != NULL) {
		ASSERT(ndx != 0);
		bcopy(state->dts_formats, new, ndx * sizeof (char *));
		kmem_free(state->dts_formats, ndx * sizeof (char *));
	}

	state->dts_formats = new;
	state->dts_formats[ndx] = fmt;

	return (ndx + 1);
}

static void
dtrace_format_remove(dtrace_state_t *state, uint16_t format)
{
	char *fmt;

	ASSERT(state->dts_formats != NULL);
	ASSERT(format <= state->dts_nformats);
	ASSERT(state->dts_formats[format - 1] != NULL);

	fmt = state->dts_formats[format - 1];
	kmem_free(fmt, strlen(fmt) + 1);
	state->dts_formats[format - 1] = NULL;
}

static void
dtrace_format_destroy(dtrace_state_t *state)
{
	int i;

	if (state->dts_nformats == 0) {
		ASSERT(state->dts_formats == NULL);
		return;
	}

	ASSERT(state->dts_formats != NULL);

	for (i = 0; i < state->dts_nformats; i++) {
		char *fmt = state->dts_formats[i];

		if (fmt == NULL)
			continue;

		kmem_free(fmt, strlen(fmt) + 1);
	}

	kmem_free(state->dts_formats, state->dts_nformats * sizeof (char *));
	state->dts_nformats = 0;
	state->dts_formats = NULL;
}

/*
 * DTrace Predicate Functions
 */
static dtrace_predicate_t *
dtrace_predicate_create(dtrace_difo_t *dp)
{
	dtrace_predicate_t *pred;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(dp->dtdo_refcnt != 0);

	pred = kmem_zalloc(sizeof (dtrace_predicate_t), KM_SLEEP);
	pred->dtp_difo = dp;
	pred->dtp_refcnt = 1;

	if (!dtrace_difo_cacheable(dp))
		return (pred);

	if (dtrace_predcache_id == DTRACE_CACHEIDNONE) {
		/*
		 * This is only theoretically possible -- we have had 2^32
		 * cacheable predicates on this machine.  We cannot allow any
		 * more predicates to become cacheable:  as unlikely as it is,
		 * there may be a thread caching a (now stale) predicate cache
		 * ID. (N.B.: the temptation is being successfully resisted to
		 * have this cmn_err() "Holy shit -- we executed this code!")
		 */
		return (pred);
	}

	pred->dtp_cacheid = dtrace_predcache_id++;

	return (pred);
}

static void
dtrace_predicate_hold(dtrace_predicate_t *pred)
{
	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(pred->dtp_difo != NULL && pred->dtp_difo->dtdo_refcnt != 0);
	ASSERT(pred->dtp_refcnt > 0);

	pred->dtp_refcnt++;
}

static void
dtrace_predicate_release(dtrace_predicate_t *pred, dtrace_vstate_t *vstate)
{
	dtrace_difo_t *dp = pred->dtp_difo;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(dp != NULL && dp->dtdo_refcnt != 0);
	ASSERT(pred->dtp_refcnt > 0);

	if (--pred->dtp_refcnt == 0) {
		dtrace_difo_release(pred->dtp_difo, vstate);
		kmem_free(pred, sizeof (dtrace_predicate_t));
	}
}

/*
 * DTrace Action Description Functions
 */
static dtrace_actdesc_t *
dtrace_actdesc_create(dtrace_actkind_t kind, uint32_t ntuple,
    uint64_t uarg, uint64_t arg)
{
	dtrace_actdesc_t *act;

#ifdef illumos
	ASSERT(!DTRACEACT_ISPRINTFLIKE(kind) || (arg != NULL &&
	    arg >= KERNELBASE) || (arg == NULL && kind == DTRACEACT_PRINTA));
#endif

	act = kmem_zalloc(sizeof (dtrace_actdesc_t), KM_SLEEP);
	act->dtad_kind = kind;
	act->dtad_ntuple = ntuple;
	act->dtad_uarg = uarg;
	act->dtad_arg = arg;
	act->dtad_refcnt = 1;

	return (act);
}

static void
dtrace_actdesc_hold(dtrace_actdesc_t *act)
{
	ASSERT(act->dtad_refcnt >= 1);
	act->dtad_refcnt++;
}

static void
dtrace_actdesc_release(dtrace_actdesc_t *act, dtrace_vstate_t *vstate)
{
	dtrace_actkind_t kind = act->dtad_kind;
	dtrace_difo_t *dp;

	ASSERT(act->dtad_refcnt >= 1);

	if (--act->dtad_refcnt != 0)
		return;

	if ((dp = act->dtad_difo) != NULL)
		dtrace_difo_release(dp, vstate);

	if (DTRACEACT_ISPRINTFLIKE(kind)) {
		char *str = (char *)(uintptr_t)act->dtad_arg;

#ifdef illumos
		ASSERT((str != NULL && (uintptr_t)str >= KERNELBASE) ||
		    (str == NULL && act->dtad_kind == DTRACEACT_PRINTA));
#endif

		if (str != NULL)
			kmem_free(str, strlen(str) + 1);
	}

	kmem_free(act, sizeof (dtrace_actdesc_t));
}

/*
 * DTrace ECB Functions
 */
static dtrace_ecb_t *
dtrace_ecb_add(dtrace_state_t *state, dtrace_probe_t *probe)
{
	dtrace_ecb_t *ecb;
	dtrace_epid_t epid;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	ecb = kmem_zalloc(sizeof (dtrace_ecb_t), KM_SLEEP);
	ecb->dte_predicate = NULL;
	ecb->dte_probe = probe;

	/*
	 * The default size is the size of the default action: recording
	 * the header.
	 */
	ecb->dte_size = ecb->dte_needed = sizeof (dtrace_rechdr_t);
	ecb->dte_alignment = sizeof (dtrace_epid_t);

	epid = state->dts_epid++;

	if (epid - 1 >= state->dts_necbs) {
		dtrace_ecb_t **oecbs = state->dts_ecbs, **ecbs;
		int necbs = state->dts_necbs << 1;

		ASSERT(epid == state->dts_necbs + 1);

		if (necbs == 0) {
			ASSERT(oecbs == NULL);
			necbs = 1;
		}

		ecbs = kmem_zalloc(necbs * sizeof (*ecbs), KM_SLEEP);

		if (oecbs != NULL)
			bcopy(oecbs, ecbs, state->dts_necbs * sizeof (*ecbs));

		dtrace_membar_producer();
		state->dts_ecbs = ecbs;

		if (oecbs != NULL) {
			/*
			 * If this state is active, we must dtrace_sync()
			 * before we can free the old dts_ecbs array:  we're
			 * coming in hot, and there may be active ring
			 * buffer processing (which indexes into the dts_ecbs
			 * array) on another CPU.
			 */
			if (state->dts_activity != DTRACE_ACTIVITY_INACTIVE)
				dtrace_sync();

			kmem_free(oecbs, state->dts_necbs * sizeof (*ecbs));
		}

		dtrace_membar_producer();
		state->dts_necbs = necbs;
	}

	ecb->dte_state = state;

	ASSERT(state->dts_ecbs[epid - 1] == NULL);
	dtrace_membar_producer();
	state->dts_ecbs[(ecb->dte_epid = epid) - 1] = ecb;

	return (ecb);
}

static void
dtrace_ecb_enable(dtrace_ecb_t *ecb)
{
	dtrace_probe_t *probe = ecb->dte_probe;

	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(ecb->dte_next == NULL);

	if (probe == NULL) {
		/*
		 * This is the NULL probe -- there's nothing to do.
		 */
		return;
	}

	if (probe->dtpr_ecb == NULL) {
		dtrace_provider_t *prov = probe->dtpr_provider;

		/*
		 * We're the first ECB on this probe.
		 */
		probe->dtpr_ecb = probe->dtpr_ecb_last = ecb;

		if (ecb->dte_predicate != NULL)
			probe->dtpr_predcache = ecb->dte_predicate->dtp_cacheid;

		prov->dtpv_pops.dtps_enable(prov->dtpv_arg,
		    probe->dtpr_id, probe->dtpr_arg);
	} else {
		/*
		 * This probe is already active.  Swing the last pointer to
		 * point to the new ECB, and issue a dtrace_sync() to assure
		 * that all CPUs have seen the change.
		 */
		ASSERT(probe->dtpr_ecb_last != NULL);
		probe->dtpr_ecb_last->dte_next = ecb;
		probe->dtpr_ecb_last = ecb;
		probe->dtpr_predcache = 0;

		dtrace_sync();
	}
}

static int
dtrace_ecb_resize(dtrace_ecb_t *ecb)
{
	dtrace_action_t *act;
	uint32_t curneeded = UINT32_MAX;
	uint32_t aggbase = UINT32_MAX;

	/*
	 * If we record anything, we always record the dtrace_rechdr_t.  (And
	 * we always record it first.)
	 */
	ecb->dte_size = sizeof (dtrace_rechdr_t);
	ecb->dte_alignment = sizeof (dtrace_epid_t);

	for (act = ecb->dte_action; act != NULL; act = act->dta_next) {
		dtrace_recdesc_t *rec = &act->dta_rec;
		ASSERT(rec->dtrd_size > 0 || rec->dtrd_alignment == 1);

		ecb->dte_alignment = MAX(ecb->dte_alignment,
		    rec->dtrd_alignment);

		if (DTRACEACT_ISAGG(act->dta_kind)) {
			dtrace_aggregation_t *agg = (dtrace_aggregation_t *)act;

			ASSERT(rec->dtrd_size != 0);
			ASSERT(agg->dtag_first != NULL);
			ASSERT(act->dta_prev->dta_intuple);
			ASSERT(aggbase != UINT32_MAX);
			ASSERT(curneeded != UINT32_MAX);

			agg->dtag_base = aggbase;

			curneeded = P2ROUNDUP(curneeded, rec->dtrd_alignment);
			rec->dtrd_offset = curneeded;
			if (curneeded + rec->dtrd_size < curneeded)
				return (EINVAL);
			curneeded += rec->dtrd_size;
			ecb->dte_needed = MAX(ecb->dte_needed, curneeded);

			aggbase = UINT32_MAX;
			curneeded = UINT32_MAX;
		} else if (act->dta_intuple) {
			if (curneeded == UINT32_MAX) {
				/*
				 * This is the first record in a tuple.  Align
				 * curneeded to be at offset 4 in an 8-byte
				 * aligned block.
				 */
				ASSERT(act->dta_prev == NULL ||
				    !act->dta_prev->dta_intuple);
				ASSERT3U(aggbase, ==, UINT32_MAX);
				curneeded = P2PHASEUP(ecb->dte_size,
				    sizeof (uint64_t), sizeof (dtrace_aggid_t));

				aggbase = curneeded - sizeof (dtrace_aggid_t);
				ASSERT(IS_P2ALIGNED(aggbase,
				    sizeof (uint64_t)));
			}
			curneeded = P2ROUNDUP(curneeded, rec->dtrd_alignment);
			rec->dtrd_offset = curneeded;
			if (curneeded + rec->dtrd_size < curneeded)
				return (EINVAL);
			curneeded += rec->dtrd_size;
		} else {
			/* tuples must be followed by an aggregation */
			ASSERT(act->dta_prev == NULL ||
			    !act->dta_prev->dta_intuple);

			ecb->dte_size = P2ROUNDUP(ecb->dte_size,
			    rec->dtrd_alignment);
			rec->dtrd_offset = ecb->dte_size;
			if (ecb->dte_size + rec->dtrd_size < ecb->dte_size)
				return (EINVAL);
			ecb->dte_size += rec->dtrd_size;
			ecb->dte_needed = MAX(ecb->dte_needed, ecb->dte_size);
		}
	}

	if ((act = ecb->dte_action) != NULL &&
	    !(act->dta_kind == DTRACEACT_SPECULATE && act->dta_next == NULL) &&
	    ecb->dte_size == sizeof (dtrace_rechdr_t)) {
		/*
		 * If the size is still sizeof (dtrace_rechdr_t), then all
		 * actions store no data; set the size to 0.
		 */
		ecb->dte_size = 0;
	}

	ecb->dte_size = P2ROUNDUP(ecb->dte_size, sizeof (dtrace_epid_t));
	ecb->dte_needed = P2ROUNDUP(ecb->dte_needed, (sizeof (dtrace_epid_t)));
	ecb->dte_state->dts_needed = MAX(ecb->dte_state->dts_needed,
	    ecb->dte_needed);
	return (0);
}

static dtrace_action_t *
dtrace_ecb_aggregation_create(dtrace_ecb_t *ecb, dtrace_actdesc_t *desc)
{
	dtrace_aggregation_t *agg;
	size_t size = sizeof (uint64_t);
	int ntuple = desc->dtad_ntuple;
	dtrace_action_t *act;
	dtrace_recdesc_t *frec;
	dtrace_aggid_t aggid;
	dtrace_state_t *state = ecb->dte_state;

	agg = kmem_zalloc(sizeof (dtrace_aggregation_t), KM_SLEEP);
	agg->dtag_ecb = ecb;

	ASSERT(DTRACEACT_ISAGG(desc->dtad_kind));

	switch (desc->dtad_kind) {
	case DTRACEAGG_MIN:
		agg->dtag_initial = INT64_MAX;
		agg->dtag_aggregate = dtrace_aggregate_min;
		break;

	case DTRACEAGG_MAX:
		agg->dtag_initial = INT64_MIN;
		agg->dtag_aggregate = dtrace_aggregate_max;
		break;

	case DTRACEAGG_COUNT:
		agg->dtag_aggregate = dtrace_aggregate_count;
		break;

	case DTRACEAGG_QUANTIZE:
		agg->dtag_aggregate = dtrace_aggregate_quantize;
		size = (((sizeof (uint64_t) * NBBY) - 1) * 2 + 1) *
		    sizeof (uint64_t);
		break;

	case DTRACEAGG_LQUANTIZE: {
		uint16_t step = DTRACE_LQUANTIZE_STEP(desc->dtad_arg);
		uint16_t levels = DTRACE_LQUANTIZE_LEVELS(desc->dtad_arg);

		agg->dtag_initial = desc->dtad_arg;
		agg->dtag_aggregate = dtrace_aggregate_lquantize;

		if (step == 0 || levels == 0)
			goto err;

		size = levels * sizeof (uint64_t) + 3 * sizeof (uint64_t);
		break;
	}

	case DTRACEAGG_LLQUANTIZE: {
		uint16_t factor = DTRACE_LLQUANTIZE_FACTOR(desc->dtad_arg);
		uint16_t low = DTRACE_LLQUANTIZE_LOW(desc->dtad_arg);
		uint16_t high = DTRACE_LLQUANTIZE_HIGH(desc->dtad_arg);
		uint16_t nsteps = DTRACE_LLQUANTIZE_NSTEP(desc->dtad_arg);
		int64_t v;

		agg->dtag_initial = desc->dtad_arg;
		agg->dtag_aggregate = dtrace_aggregate_llquantize;

		if (factor < 2 || low >= high || nsteps < factor)
			goto err;

		/*
		 * Now check that the number of steps evenly divides a power
		 * of the factor.  (This assures both integer bucket size and
		 * linearity within each magnitude.)
		 */
		for (v = factor; v < nsteps; v *= factor)
			continue;

		if ((v % nsteps) || (nsteps % factor))
			goto err;

		size = (dtrace_aggregate_llquantize_bucket(factor,
		    low, high, nsteps, INT64_MAX) + 2) * sizeof (uint64_t);
		break;
	}

	case DTRACEAGG_AVG:
		agg->dtag_aggregate = dtrace_aggregate_avg;
		size = sizeof (uint64_t) * 2;
		break;

	case DTRACEAGG_STDDEV:
		agg->dtag_aggregate = dtrace_aggregate_stddev;
		size = sizeof (uint64_t) * 4;
		break;

	case DTRACEAGG_SUM:
		agg->dtag_aggregate = dtrace_aggregate_sum;
		break;

	default:
		goto err;
	}

	agg->dtag_action.dta_rec.dtrd_size = size;

	if (ntuple == 0)
		goto err;

	/*
	 * We must make sure that we have enough actions for the n-tuple.
	 */
	for (act = ecb->dte_action_last; act != NULL; act = act->dta_prev) {
		if (DTRACEACT_ISAGG(act->dta_kind))
			break;

		if (--ntuple == 0) {
			/*
			 * This is the action with which our n-tuple begins.
			 */
			agg->dtag_first = act;
			goto success;
		}
	}

	/*
	 * This n-tuple is short by ntuple elements.  Return failure.
	 */
	ASSERT(ntuple != 0);
err:
	kmem_free(agg, sizeof (dtrace_aggregation_t));
	return (NULL);

success:
	/*
	 * If the last action in the tuple has a size of zero, it's actually
	 * an expression argument for the aggregating action.
	 */
	ASSERT(ecb->dte_action_last != NULL);
	act = ecb->dte_action_last;

	if (act->dta_kind == DTRACEACT_DIFEXPR) {
		ASSERT(act->dta_difo != NULL);

		if (act->dta_difo->dtdo_rtype.dtdt_size == 0)
			agg->dtag_hasarg = 1;
	}

	/*
	 * We need to allocate an id for this aggregation.
	 */
#ifdef illumos
	aggid = (dtrace_aggid_t)(uintptr_t)vmem_alloc(state->dts_aggid_arena, 1,
	    VM_BESTFIT | VM_SLEEP);
#else
	aggid = alloc_unr(state->dts_aggid_arena);
#endif

	if (aggid - 1 >= state->dts_naggregations) {
		dtrace_aggregation_t **oaggs = state->dts_aggregations;
		dtrace_aggregation_t **aggs;
		int naggs = state->dts_naggregations << 1;
		int onaggs = state->dts_naggregations;

		ASSERT(aggid == state->dts_naggregations + 1);

		if (naggs == 0) {
			ASSERT(oaggs == NULL);
			naggs = 1;
		}

		aggs = kmem_zalloc(naggs * sizeof (*aggs), KM_SLEEP);

		if (oaggs != NULL) {
			bcopy(oaggs, aggs, onaggs * sizeof (*aggs));
			kmem_free(oaggs, onaggs * sizeof (*aggs));
		}

		state->dts_aggregations = aggs;
		state->dts_naggregations = naggs;
	}

	ASSERT(state->dts_aggregations[aggid - 1] == NULL);
	state->dts_aggregations[(agg->dtag_id = aggid) - 1] = agg;

	frec = &agg->dtag_first->dta_rec;
	if (frec->dtrd_alignment < sizeof (dtrace_aggid_t))
		frec->dtrd_alignment = sizeof (dtrace_aggid_t);

	for (act = agg->dtag_first; act != NULL; act = act->dta_next) {
		ASSERT(!act->dta_intuple);
		act->dta_intuple = 1;
	}

	return (&agg->dtag_action);
}

static void
dtrace_ecb_aggregation_destroy(dtrace_ecb_t *ecb, dtrace_action_t *act)
{
	dtrace_aggregation_t *agg = (dtrace_aggregation_t *)act;
	dtrace_state_t *state = ecb->dte_state;
	dtrace_aggid_t aggid = agg->dtag_id;

	ASSERT(DTRACEACT_ISAGG(act->dta_kind));
#ifdef illumos
	vmem_free(state->dts_aggid_arena, (void *)(uintptr_t)aggid, 1);
#else
	free_unr(state->dts_aggid_arena, aggid);
#endif

	ASSERT(state->dts_aggregations[aggid - 1] == agg);
	state->dts_aggregations[aggid - 1] = NULL;

	kmem_free(agg, sizeof (dtrace_aggregation_t));
}

static int
dtrace_ecb_action_add(dtrace_ecb_t *ecb, dtrace_actdesc_t *desc)
{
	dtrace_action_t *action, *last;
	dtrace_difo_t *dp = desc->dtad_difo;
	uint32_t size = 0, align = sizeof (uint8_t), mask;
	uint16_t format = 0;
	dtrace_recdesc_t *rec;
	dtrace_state_t *state = ecb->dte_state;
	dtrace_optval_t *opt = state->dts_options, nframes = 0, strsize;
	uint64_t arg = desc->dtad_arg;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(ecb->dte_action == NULL || ecb->dte_action->dta_refcnt == 1);

	if (DTRACEACT_ISAGG(desc->dtad_kind)) {
		/*
		 * If this is an aggregating action, there must be neither
		 * a speculate nor a commit on the action chain.
		 */
		dtrace_action_t *act;

		for (act = ecb->dte_action; act != NULL; act = act->dta_next) {
			if (act->dta_kind == DTRACEACT_COMMIT)
				return (EINVAL);

			if (act->dta_kind == DTRACEACT_SPECULATE)
				return (EINVAL);
		}

		action = dtrace_ecb_aggregation_create(ecb, desc);

		if (action == NULL)
			return (EINVAL);
	} else {
		if (DTRACEACT_ISDESTRUCTIVE(desc->dtad_kind) ||
		    (desc->dtad_kind == DTRACEACT_DIFEXPR &&
		    dp != NULL && dp->dtdo_destructive)) {
			state->dts_destructive = 1;
		}

		switch (desc->dtad_kind) {
		case DTRACEACT_PRINTF:
		case DTRACEACT_PRINTA:
		case DTRACEACT_SYSTEM:
		case DTRACEACT_FREOPEN:
		case DTRACEACT_DIFEXPR:
			/*
			 * We know that our arg is a string -- turn it into a
			 * format.
			 */
			if (arg == 0) {
				ASSERT(desc->dtad_kind == DTRACEACT_PRINTA ||
				    desc->dtad_kind == DTRACEACT_DIFEXPR);
				format = 0;
			} else {
				ASSERT(arg != 0);
#ifdef illumos
				ASSERT(arg > KERNELBASE);
#endif
				format = dtrace_format_add(state,
				    (char *)(uintptr_t)arg);
			}

			/*FALLTHROUGH*/
		case DTRACEACT_LIBACT:
		case DTRACEACT_TRACEMEM:
		case DTRACEACT_TRACEMEM_DYNSIZE:
			if (dp == NULL)
				return (EINVAL);

			if ((size = dp->dtdo_rtype.dtdt_size) != 0)
				break;

			if (dp->dtdo_rtype.dtdt_kind == DIF_TYPE_STRING) {
				if (!(dp->dtdo_rtype.dtdt_flags & DIF_TF_BYREF))
					return (EINVAL);

				size = opt[DTRACEOPT_STRSIZE];
			}

			break;

		case DTRACEACT_STACK:
			if ((nframes = arg) == 0) {
				nframes = opt[DTRACEOPT_STACKFRAMES];
				ASSERT(nframes > 0);
				arg = nframes;
			}

			size = nframes * sizeof (pc_t);
			break;

		case DTRACEACT_JSTACK:
			if ((strsize = DTRACE_USTACK_STRSIZE(arg)) == 0)
				strsize = opt[DTRACEOPT_JSTACKSTRSIZE];

			if ((nframes = DTRACE_USTACK_NFRAMES(arg)) == 0)
				nframes = opt[DTRACEOPT_JSTACKFRAMES];

			arg = DTRACE_USTACK_ARG(nframes, strsize);

			/*FALLTHROUGH*/
		case DTRACEACT_USTACK:
			if (desc->dtad_kind != DTRACEACT_JSTACK &&
			    (nframes = DTRACE_USTACK_NFRAMES(arg)) == 0) {
				strsize = DTRACE_USTACK_STRSIZE(arg);
				nframes = opt[DTRACEOPT_USTACKFRAMES];
				ASSERT(nframes > 0);
				arg = DTRACE_USTACK_ARG(nframes, strsize);
			}

			/*
			 * Save a slot for the pid.
			 */
			size = (nframes + 1) * sizeof (uint64_t);
			size += DTRACE_USTACK_STRSIZE(arg);
			size = P2ROUNDUP(size, (uint32_t)(sizeof (uintptr_t)));

			break;

		case DTRACEACT_SYM:
		case DTRACEACT_MOD:
			if (dp == NULL || ((size = dp->dtdo_rtype.dtdt_size) !=
			    sizeof (uint64_t)) ||
			    (dp->dtdo_rtype.dtdt_flags & DIF_TF_BYREF))
				return (EINVAL);
			break;

		case DTRACEACT_USYM:
		case DTRACEACT_UMOD:
		case DTRACEACT_UADDR:
			if (dp == NULL ||
			    (dp->dtdo_rtype.dtdt_size != sizeof (uint64_t)) ||
			    (dp->dtdo_rtype.dtdt_flags & DIF_TF_BYREF))
				return (EINVAL);

			/*
			 * We have a slot for the pid, plus a slot for the
			 * argument.  To keep things simple (aligned with
			 * bitness-neutral sizing), we store each as a 64-bit
			 * quantity.
			 */
			size = 2 * sizeof (uint64_t);
			break;

		case DTRACEACT_STOP:
		case DTRACEACT_BREAKPOINT:
		case DTRACEACT_PANIC:
			break;

		case DTRACEACT_CHILL:
		case DTRACEACT_DISCARD:
		case DTRACEACT_RAISE:
			if (dp == NULL)
				return (EINVAL);
			break;

		case DTRACEACT_EXIT:
			if (dp == NULL ||
			    (size = dp->dtdo_rtype.dtdt_size) != sizeof (int) ||
			    (dp->dtdo_rtype.dtdt_flags & DIF_TF_BYREF))
				return (EINVAL);
			break;

		case DTRACEACT_SPECULATE:
			if (ecb->dte_size > sizeof (dtrace_rechdr_t))
				return (EINVAL);

			if (dp == NULL)
				return (EINVAL);

			state->dts_speculates = 1;
			break;

		case DTRACEACT_PRINTM:
		    	size = dp->dtdo_rtype.dtdt_size;
			break;

		case DTRACEACT_COMMIT: {
			dtrace_action_t *act = ecb->dte_action;

			for (; act != NULL; act = act->dta_next) {
				if (act->dta_kind == DTRACEACT_COMMIT)
					return (EINVAL);
			}

			if (dp == NULL)
				return (EINVAL);
			break;
		}

		default:
			return (EINVAL);
		}

		if (size != 0 || desc->dtad_kind == DTRACEACT_SPECULATE) {
			/*
			 * If this is a data-storing action or a speculate,
			 * we must be sure that there isn't a commit on the
			 * action chain.
			 */
			dtrace_action_t *act = ecb->dte_action;

			for (; act != NULL; act = act->dta_next) {
				if (act->dta_kind == DTRACEACT_COMMIT)
					return (EINVAL);
			}
		}

		action = kmem_zalloc(sizeof (dtrace_action_t), KM_SLEEP);
		action->dta_rec.dtrd_size = size;
	}

	action->dta_refcnt = 1;
	rec = &action->dta_rec;
	size = rec->dtrd_size;

	for (mask = sizeof (uint64_t) - 1; size != 0 && mask > 0; mask >>= 1) {
		if (!(size & mask)) {
			align = mask + 1;
			break;
		}
	}

	action->dta_kind = desc->dtad_kind;

	if ((action->dta_difo = dp) != NULL)
		dtrace_difo_hold(dp);

	rec->dtrd_action = action->dta_kind;
	rec->dtrd_arg = arg;
	rec->dtrd_uarg = desc->dtad_uarg;
	rec->dtrd_alignment = (uint16_t)align;
	rec->dtrd_format = format;

	if ((last = ecb->dte_action_last) != NULL) {
		ASSERT(ecb->dte_action != NULL);
		action->dta_prev = last;
		last->dta_next = action;
	} else {
		ASSERT(ecb->dte_action == NULL);
		ecb->dte_action = action;
	}

	ecb->dte_action_last = action;

	return (0);
}

static void
dtrace_ecb_action_remove(dtrace_ecb_t *ecb)
{
	dtrace_action_t *act = ecb->dte_action, *next;
	dtrace_vstate_t *vstate = &ecb->dte_state->dts_vstate;
	dtrace_difo_t *dp;
	uint16_t format;

	if (act != NULL && act->dta_refcnt > 1) {
		ASSERT(act->dta_next == NULL || act->dta_next->dta_refcnt == 1);
		act->dta_refcnt--;
	} else {
		for (; act != NULL; act = next) {
			next = act->dta_next;
			ASSERT(next != NULL || act == ecb->dte_action_last);
			ASSERT(act->dta_refcnt == 1);

			if ((format = act->dta_rec.dtrd_format) != 0)
				dtrace_format_remove(ecb->dte_state, format);

			if ((dp = act->dta_difo) != NULL)
				dtrace_difo_release(dp, vstate);

			if (DTRACEACT_ISAGG(act->dta_kind)) {
				dtrace_ecb_aggregation_destroy(ecb, act);
			} else {
				kmem_free(act, sizeof (dtrace_action_t));
			}
		}
	}

	ecb->dte_action = NULL;
	ecb->dte_action_last = NULL;
	ecb->dte_size = 0;
}

static void
dtrace_ecb_disable(dtrace_ecb_t *ecb)
{
	/*
	 * We disable the ECB by removing it from its probe.
	 */
	dtrace_ecb_t *pecb, *prev = NULL;
	dtrace_probe_t *probe = ecb->dte_probe;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	if (probe == NULL) {
		/*
		 * This is the NULL probe; there is nothing to disable.
		 */
		return;
	}

	for (pecb = probe->dtpr_ecb; pecb != NULL; pecb = pecb->dte_next) {
		if (pecb == ecb)
			break;
		prev = pecb;
	}

	ASSERT(pecb != NULL);

	if (prev == NULL) {
		probe->dtpr_ecb = ecb->dte_next;
	} else {
		prev->dte_next = ecb->dte_next;
	}

	if (ecb == probe->dtpr_ecb_last) {
		ASSERT(ecb->dte_next == NULL);
		probe->dtpr_ecb_last = prev;
	}

	/*
	 * The ECB has been disconnected from the probe; now sync to assure
	 * that all CPUs have seen the change before returning.
	 */
	dtrace_sync();

	if (probe->dtpr_ecb == NULL) {
		/*
		 * That was the last ECB on the probe; clear the predicate
		 * cache ID for the probe, disable it and sync one more time
		 * to assure that we'll never hit it again.
		 */
		dtrace_provider_t *prov = probe->dtpr_provider;

		ASSERT(ecb->dte_next == NULL);
		ASSERT(probe->dtpr_ecb_last == NULL);
		probe->dtpr_predcache = DTRACE_CACHEIDNONE;
		prov->dtpv_pops.dtps_disable(prov->dtpv_arg,
		    probe->dtpr_id, probe->dtpr_arg);
		dtrace_sync();
	} else {
		/*
		 * There is at least one ECB remaining on the probe.  If there
		 * is _exactly_ one, set the probe's predicate cache ID to be
		 * the predicate cache ID of the remaining ECB.
		 */
		ASSERT(probe->dtpr_ecb_last != NULL);
		ASSERT(probe->dtpr_predcache == DTRACE_CACHEIDNONE);

		if (probe->dtpr_ecb == probe->dtpr_ecb_last) {
			dtrace_predicate_t *p = probe->dtpr_ecb->dte_predicate;

			ASSERT(probe->dtpr_ecb->dte_next == NULL);

			if (p != NULL)
				probe->dtpr_predcache = p->dtp_cacheid;
		}

		ecb->dte_next = NULL;
	}
}

static void
dtrace_ecb_destroy(dtrace_ecb_t *ecb)
{
	dtrace_state_t *state = ecb->dte_state;
	dtrace_vstate_t *vstate = &state->dts_vstate;
	dtrace_predicate_t *pred;
	dtrace_epid_t epid = ecb->dte_epid;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(ecb->dte_next == NULL);
	ASSERT(ecb->dte_probe == NULL || ecb->dte_probe->dtpr_ecb != ecb);

	if ((pred = ecb->dte_predicate) != NULL)
		dtrace_predicate_release(pred, vstate);

	dtrace_ecb_action_remove(ecb);

	ASSERT(state->dts_ecbs[epid - 1] == ecb);
	state->dts_ecbs[epid - 1] = NULL;

	kmem_free(ecb, sizeof (dtrace_ecb_t));
}

static dtrace_ecb_t *
dtrace_ecb_create(dtrace_state_t *state, dtrace_probe_t *probe,
    dtrace_enabling_t *enab)
{
	dtrace_ecb_t *ecb;
	dtrace_predicate_t *pred;
	dtrace_actdesc_t *act;
	dtrace_provider_t *prov;
	dtrace_ecbdesc_t *desc = enab->dten_current;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(state != NULL);

	ecb = dtrace_ecb_add(state, probe);
	ecb->dte_uarg = desc->dted_uarg;

	if ((pred = desc->dted_pred.dtpdd_predicate) != NULL) {
		dtrace_predicate_hold(pred);
		ecb->dte_predicate = pred;
	}

	if (probe != NULL) {
		/*
		 * If the provider shows more leg than the consumer is old
		 * enough to see, we need to enable the appropriate implicit
		 * predicate bits to prevent the ecb from activating at
		 * revealing times.
		 *
		 * Providers specifying DTRACE_PRIV_USER at register time
		 * are stating that they need the /proc-style privilege
		 * model to be enforced, and this is what DTRACE_COND_OWNER
		 * and DTRACE_COND_ZONEOWNER will then do at probe time.
		 */
		prov = probe->dtpr_provider;
		if (!(state->dts_cred.dcr_visible & DTRACE_CRV_ALLPROC) &&
		    (prov->dtpv_priv.dtpp_flags & DTRACE_PRIV_USER))
			ecb->dte_cond |= DTRACE_COND_OWNER;

		if (!(state->dts_cred.dcr_visible & DTRACE_CRV_ALLZONE) &&
		    (prov->dtpv_priv.dtpp_flags & DTRACE_PRIV_USER))
			ecb->dte_cond |= DTRACE_COND_ZONEOWNER;

		/*
		 * If the provider shows us kernel innards and the user
		 * is lacking sufficient privilege, enable the
		 * DTRACE_COND_USERMODE implicit predicate.
		 */
		if (!(state->dts_cred.dcr_visible & DTRACE_CRV_KERNEL) &&
		    (prov->dtpv_priv.dtpp_flags & DTRACE_PRIV_KERNEL))
			ecb->dte_cond |= DTRACE_COND_USERMODE;
	}

	if (dtrace_ecb_create_cache != NULL) {
		/*
		 * If we have a cached ecb, we'll use its action list instead
		 * of creating our own (saving both time and space).
		 */
		dtrace_ecb_t *cached = dtrace_ecb_create_cache;
		dtrace_action_t *act = cached->dte_action;

		if (act != NULL) {
			ASSERT(act->dta_refcnt > 0);
			act->dta_refcnt++;
			ecb->dte_action = act;
			ecb->dte_action_last = cached->dte_action_last;
			ecb->dte_needed = cached->dte_needed;
			ecb->dte_size = cached->dte_size;
			ecb->dte_alignment = cached->dte_alignment;
		}

		return (ecb);
	}

	for (act = desc->dted_action; act != NULL; act = act->dtad_next) {
		if ((enab->dten_error = dtrace_ecb_action_add(ecb, act)) != 0) {
			dtrace_ecb_destroy(ecb);
			return (NULL);
		}
	}

	if ((enab->dten_error = dtrace_ecb_resize(ecb)) != 0) {
		dtrace_ecb_destroy(ecb);
		return (NULL);
	}

	return (dtrace_ecb_create_cache = ecb);
}

static int
dtrace_ecb_create_enable(dtrace_probe_t *probe, void *arg)
{
	dtrace_ecb_t *ecb;
	dtrace_enabling_t *enab = arg;
	dtrace_state_t *state = enab->dten_vstate->dtvs_state;

	ASSERT(state != NULL);

	if (probe != NULL && probe->dtpr_gen < enab->dten_probegen) {
		/*
		 * This probe was created in a generation for which this
		 * enabling has previously created ECBs; we don't want to
		 * enable it again, so just kick out.
		 */
		return (DTRACE_MATCH_NEXT);
	}

	if ((ecb = dtrace_ecb_create(state, probe, enab)) == NULL)
		return (DTRACE_MATCH_DONE);

	dtrace_ecb_enable(ecb);
	return (DTRACE_MATCH_NEXT);
}

static dtrace_ecb_t *
dtrace_epid2ecb(dtrace_state_t *state, dtrace_epid_t id)
{
	dtrace_ecb_t *ecb;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	if (id == 0 || id > state->dts_necbs)
		return (NULL);

	ASSERT(state->dts_necbs > 0 && state->dts_ecbs != NULL);
	ASSERT((ecb = state->dts_ecbs[id - 1]) == NULL || ecb->dte_epid == id);

	return (state->dts_ecbs[id - 1]);
}

static dtrace_aggregation_t *
dtrace_aggid2agg(dtrace_state_t *state, dtrace_aggid_t id)
{
	dtrace_aggregation_t *agg;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	if (id == 0 || id > state->dts_naggregations)
		return (NULL);

	ASSERT(state->dts_naggregations > 0 && state->dts_aggregations != NULL);
	ASSERT((agg = state->dts_aggregations[id - 1]) == NULL ||
	    agg->dtag_id == id);

	return (state->dts_aggregations[id - 1]);
}

/*
 * DTrace Buffer Functions
 *
 * The following functions manipulate DTrace buffers.  Most of these functions
 * are called in the context of establishing or processing consumer state;
 * exceptions are explicitly noted.
 */

/*
 * Note:  called from cross call context.  This function switches the two
 * buffers on a given CPU.  The atomicity of this operation is assured by
 * disabling interrupts while the actual switch takes place; the disabling of
 * interrupts serializes the execution with any execution of dtrace_probe() on
 * the same CPU.
 */
static void
dtrace_buffer_switch(dtrace_buffer_t *buf)
{
	caddr_t tomax = buf->dtb_tomax;
	caddr_t xamot = buf->dtb_xamot;
	dtrace_icookie_t cookie;
	hrtime_t now;

	ASSERT(!(buf->dtb_flags & DTRACEBUF_NOSWITCH));
	ASSERT(!(buf->dtb_flags & DTRACEBUF_RING));

	cookie = dtrace_interrupt_disable();
	now = dtrace_gethrtime();
	buf->dtb_tomax = xamot;
	buf->dtb_xamot = tomax;
	buf->dtb_xamot_drops = buf->dtb_drops;
	buf->dtb_xamot_offset = buf->dtb_offset;
	buf->dtb_xamot_errors = buf->dtb_errors;
	buf->dtb_xamot_flags = buf->dtb_flags;
	buf->dtb_offset = 0;
	buf->dtb_drops = 0;
	buf->dtb_errors = 0;
	buf->dtb_flags &= ~(DTRACEBUF_ERROR | DTRACEBUF_DROPPED);
	buf->dtb_interval = now - buf->dtb_switched;
	buf->dtb_switched = now;
	dtrace_interrupt_enable(cookie);
}

/*
 * Note:  called from cross call context.  This function activates a buffer
 * on a CPU.  As with dtrace_buffer_switch(), the atomicity of the operation
 * is guaranteed by the disabling of interrupts.
 */
static void
dtrace_buffer_activate(dtrace_state_t *state)
{
	dtrace_buffer_t *buf;
	dtrace_icookie_t cookie = dtrace_interrupt_disable();

	buf = &state->dts_buffer[curcpu];

	if (buf->dtb_tomax != NULL) {
		/*
		 * We might like to assert that the buffer is marked inactive,
		 * but this isn't necessarily true:  the buffer for the CPU
		 * that processes the BEGIN probe has its buffer activated
		 * manually.  In this case, we take the (harmless) action
		 * re-clearing the bit INACTIVE bit.
		 */
		buf->dtb_flags &= ~DTRACEBUF_INACTIVE;
	}

	dtrace_interrupt_enable(cookie);
}

#ifdef __FreeBSD__
/*
 * Activate the specified per-CPU buffer.  This is used instead of
 * dtrace_buffer_activate() when APs have not yet started, i.e. when
 * activating anonymous state.
 */
static void
dtrace_buffer_activate_cpu(dtrace_state_t *state, int cpu)
{

	if (state->dts_buffer[cpu].dtb_tomax != NULL)
		state->dts_buffer[cpu].dtb_flags &= ~DTRACEBUF_INACTIVE;
}
#endif

static int
dtrace_buffer_alloc(dtrace_buffer_t *bufs, size_t size, int flags,
    processorid_t cpu, int *factor)
{
#ifdef illumos
	cpu_t *cp;
#endif
	dtrace_buffer_t *buf;
	int allocated = 0, desired = 0;

#ifdef illumos
	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(MUTEX_HELD(&dtrace_lock));

	*factor = 1;

	if (size > dtrace_nonroot_maxsize &&
	    !PRIV_POLICY_CHOICE(CRED(), PRIV_ALL, B_FALSE))
		return (EFBIG);

	cp = cpu_list;

	do {
		if (cpu != DTRACE_CPUALL && cpu != cp->cpu_id)
			continue;

		buf = &bufs[cp->cpu_id];

		/*
		 * If there is already a buffer allocated for this CPU, it
		 * is only possible that this is a DR event.  In this case,
		 */
		if (buf->dtb_tomax != NULL) {
			ASSERT(buf->dtb_size == size);
			continue;
		}

		ASSERT(buf->dtb_xamot == NULL);

		if ((buf->dtb_tomax = kmem_zalloc(size,
		    KM_NOSLEEP | KM_NORMALPRI)) == NULL)
			goto err;

		buf->dtb_size = size;
		buf->dtb_flags = flags;
		buf->dtb_offset = 0;
		buf->dtb_drops = 0;

		if (flags & DTRACEBUF_NOSWITCH)
			continue;

		if ((buf->dtb_xamot = kmem_zalloc(size,
		    KM_NOSLEEP | KM_NORMALPRI)) == NULL)
			goto err;
	} while ((cp = cp->cpu_next) != cpu_list);

	return (0);

err:
	cp = cpu_list;

	do {
		if (cpu != DTRACE_CPUALL && cpu != cp->cpu_id)
			continue;

		buf = &bufs[cp->cpu_id];
		desired += 2;

		if (buf->dtb_xamot != NULL) {
			ASSERT(buf->dtb_tomax != NULL);
			ASSERT(buf->dtb_size == size);
			kmem_free(buf->dtb_xamot, size);
			allocated++;
		}

		if (buf->dtb_tomax != NULL) {
			ASSERT(buf->dtb_size == size);
			kmem_free(buf->dtb_tomax, size);
			allocated++;
		}

		buf->dtb_tomax = NULL;
		buf->dtb_xamot = NULL;
		buf->dtb_size = 0;
	} while ((cp = cp->cpu_next) != cpu_list);
#else
	int i;

	*factor = 1;
#if defined(__aarch64__) || defined(__amd64__) || defined(__arm__) || \
    defined(__mips__) || defined(__powerpc__) || defined(__riscv)
	/*
	 * FreeBSD isn't good at limiting the amount of memory we
	 * ask to malloc, so let's place a limit here before trying
	 * to do something that might well end in tears at bedtime.
	 */
	if (size > physmem * PAGE_SIZE / (128 * (mp_maxid + 1)))
		return (ENOMEM);
#endif

	ASSERT(MUTEX_HELD(&dtrace_lock));
	CPU_FOREACH(i) {
		if (cpu != DTRACE_CPUALL && cpu != i)
			continue;

		buf = &bufs[i];

		/*
		 * If there is already a buffer allocated for this CPU, it
		 * is only possible that this is a DR event.  In this case,
		 * the buffer size must match our specified size.
		 */
		if (buf->dtb_tomax != NULL) {
			ASSERT(buf->dtb_size == size);
			continue;
		}

		ASSERT(buf->dtb_xamot == NULL);

		if ((buf->dtb_tomax = kmem_zalloc(size,
		    KM_NOSLEEP | KM_NORMALPRI)) == NULL)
			goto err;

		buf->dtb_size = size;
		buf->dtb_flags = flags;
		buf->dtb_offset = 0;
		buf->dtb_drops = 0;

		if (flags & DTRACEBUF_NOSWITCH)
			continue;

		if ((buf->dtb_xamot = kmem_zalloc(size,
		    KM_NOSLEEP | KM_NORMALPRI)) == NULL)
			goto err;
	}

	return (0);

err:
	/*
	 * Error allocating memory, so free the buffers that were
	 * allocated before the failed allocation.
	 */
	CPU_FOREACH(i) {
		if (cpu != DTRACE_CPUALL && cpu != i)
			continue;

		buf = &bufs[i];
		desired += 2;

		if (buf->dtb_xamot != NULL) {
			ASSERT(buf->dtb_tomax != NULL);
			ASSERT(buf->dtb_size == size);
			kmem_free(buf->dtb_xamot, size);
			allocated++;
		}

		if (buf->dtb_tomax != NULL) {
			ASSERT(buf->dtb_size == size);
			kmem_free(buf->dtb_tomax, size);
			allocated++;
		}

		buf->dtb_tomax = NULL;
		buf->dtb_xamot = NULL;
		buf->dtb_size = 0;

	}
#endif
	*factor = desired / (allocated > 0 ? allocated : 1);

	return (ENOMEM);
}

/*
 * Note:  called from probe context.  This function just increments the drop
 * count on a buffer.  It has been made a function to allow for the
 * possibility of understanding the source of mysterious drop counts.  (A
 * problem for which one may be particularly disappointed that DTrace cannot
 * be used to understand DTrace.)
 */
static void
dtrace_buffer_drop(dtrace_buffer_t *buf)
{
	buf->dtb_drops++;
}

/*
 * Note:  called from probe context.  This function is called to reserve space
 * in a buffer.  If mstate is non-NULL, sets the scratch base and size in the
 * mstate.  Returns the new offset in the buffer, or a negative value if an
 * error has occurred.
 */
static intptr_t
dtrace_buffer_reserve(dtrace_buffer_t *buf, size_t needed, size_t align,
    dtrace_state_t *state, dtrace_mstate_t *mstate)
{
	intptr_t offs = buf->dtb_offset, soffs;
	intptr_t woffs;
	caddr_t tomax;
	size_t total;

	if (buf->dtb_flags & DTRACEBUF_INACTIVE)
		return (-1);

	if ((tomax = buf->dtb_tomax) == NULL) {
		dtrace_buffer_drop(buf);
		return (-1);
	}

	if (!(buf->dtb_flags & (DTRACEBUF_RING | DTRACEBUF_FILL))) {
		while (offs & (align - 1)) {
			/*
			 * Assert that our alignment is off by a number which
			 * is itself sizeof (uint32_t) aligned.
			 */
			ASSERT(!((align - (offs & (align - 1))) &
			    (sizeof (uint32_t) - 1)));
			DTRACE_STORE(uint32_t, tomax, offs, DTRACE_EPIDNONE);
			offs += sizeof (uint32_t);
		}

		if ((soffs = offs + needed) > buf->dtb_size) {
			dtrace_buffer_drop(buf);
			return (-1);
		}

		if (mstate == NULL)
			return (offs);

		mstate->dtms_scratch_base = (uintptr_t)tomax + soffs;
		mstate->dtms_scratch_size = buf->dtb_size - soffs;
		mstate->dtms_scratch_ptr = mstate->dtms_scratch_base;

		return (offs);
	}

	if (buf->dtb_flags & DTRACEBUF_FILL) {
		if (state->dts_activity != DTRACE_ACTIVITY_COOLDOWN &&
		    (buf->dtb_flags & DTRACEBUF_FULL))
			return (-1);
		goto out;
	}

	total = needed + (offs & (align - 1));

	/*
	 * For a ring buffer, life is quite a bit more complicated.  Before
	 * we can store any padding, we need to adjust our wrapping offset.
	 * (If we've never before wrapped or we're not about to, no adjustment
	 * is required.)
	 */
	if ((buf->dtb_flags & DTRACEBUF_WRAPPED) ||
	    offs + total > buf->dtb_size) {
		woffs = buf->dtb_xamot_offset;

		if (offs + total > buf->dtb_size) {
			/*
			 * We can't fit in the end of the buffer.  First, a
			 * sanity check that we can fit in the buffer at all.
			 */
			if (total > buf->dtb_size) {
				dtrace_buffer_drop(buf);
				return (-1);
			}

			/*
			 * We're going to be storing at the top of the buffer,
			 * so now we need to deal with the wrapped offset.  We
			 * only reset our wrapped offset to 0 if it is
			 * currently greater than the current offset.  If it
			 * is less than the current offset, it is because a
			 * previous allocation induced a wrap -- but the
			 * allocation didn't subsequently take the space due
			 * to an error or false predicate evaluation.  In this
			 * case, we'll just leave the wrapped offset alone: if
			 * the wrapped offset hasn't been advanced far enough
			 * for this allocation, it will be adjusted in the
			 * lower loop.
			 */
			if (buf->dtb_flags & DTRACEBUF_WRAPPED) {
				if (woffs >= offs)
					woffs = 0;
			} else {
				woffs = 0;
			}

			/*
			 * Now we know that we're going to be storing to the
			 * top of the buffer and that there is room for us
			 * there.  We need to clear the buffer from the current
			 * offset to the end (there may be old gunk there).
			 */
			while (offs < buf->dtb_size)
				tomax[offs++] = 0;

			/*
			 * We need to set our offset to zero.  And because we
			 * are wrapping, we need to set the bit indicating as
			 * much.  We can also adjust our needed space back
			 * down to the space required by the ECB -- we know
			 * that the top of the buffer is aligned.
			 */
			offs = 0;
			total = needed;
			buf->dtb_flags |= DTRACEBUF_WRAPPED;
		} else {
			/*
			 * There is room for us in the buffer, so we simply
			 * need to check the wrapped offset.
			 */
			if (woffs < offs) {
				/*
				 * The wrapped offset is less than the offset.
				 * This can happen if we allocated buffer space
				 * that induced a wrap, but then we didn't
				 * subsequently take the space due to an error
				 * or false predicate evaluation.  This is
				 * okay; we know that _this_ allocation isn't
				 * going to induce a wrap.  We still can't
				 * reset the wrapped offset to be zero,
				 * however: the space may have been trashed in
				 * the previous failed probe attempt.  But at
				 * least the wrapped offset doesn't need to
				 * be adjusted at all...
				 */
				goto out;
			}
		}

		while (offs + total > woffs) {
			dtrace_epid_t epid = *(uint32_t *)(tomax + woffs);
			size_t size;

			if (epid == DTRACE_EPIDNONE) {
				size = sizeof (uint32_t);
			} else {
				ASSERT3U(epid, <=, state->dts_necbs);
				ASSERT(state->dts_ecbs[epid - 1] != NULL);

				size = state->dts_ecbs[epid - 1]->dte_size;
			}

			ASSERT(woffs + size <= buf->dtb_size);
			ASSERT(size != 0);

			if (woffs + size == buf->dtb_size) {
				/*
				 * We've reached the end of the buffer; we want
				 * to set the wrapped offset to 0 and break
				 * out.  However, if the offs is 0, then we're
				 * in a strange edge-condition:  the amount of
				 * space that we want to reserve plus the size
				 * of the record that we're overwriting is
				 * greater than the size of the buffer.  This
				 * is problematic because if we reserve the
				 * space but subsequently don't consume it (due
				 * to a failed predicate or error) the wrapped
				 * offset will be 0 -- yet the EPID at offset 0
				 * will not be committed.  This situation is
				 * relatively easy to deal with:  if we're in
				 * this case, the buffer is indistinguishable
				 * from one that hasn't wrapped; we need only
				 * finish the job by clearing the wrapped bit,
				 * explicitly setting the offset to be 0, and
				 * zero'ing out the old data in the buffer.
				 */
				if (offs == 0) {
					buf->dtb_flags &= ~DTRACEBUF_WRAPPED;
					buf->dtb_offset = 0;
					woffs = total;

					while (woffs < buf->dtb_size)
						tomax[woffs++] = 0;
				}

				woffs = 0;
				break;
			}

			woffs += size;
		}

		/*
		 * We have a wrapped offset.  It may be that the wrapped offset
		 * has become zero -- that's okay.
		 */
		buf->dtb_xamot_offset = woffs;
	}

out:
	/*
	 * Now we can plow the buffer with any necessary padding.
	 */
	while (offs & (align - 1)) {
		/*
		 * Assert that our alignment is off by a number which
		 * is itself sizeof (uint32_t) aligned.
		 */
		ASSERT(!((align - (offs & (align - 1))) &
		    (sizeof (uint32_t) - 1)));
		DTRACE_STORE(uint32_t, tomax, offs, DTRACE_EPIDNONE);
		offs += sizeof (uint32_t);
	}

	if (buf->dtb_flags & DTRACEBUF_FILL) {
		if (offs + needed > buf->dtb_size - state->dts_reserve) {
			buf->dtb_flags |= DTRACEBUF_FULL;
			return (-1);
		}
	}

	if (mstate == NULL)
		return (offs);

	/*
	 * For ring buffers and fill buffers, the scratch space is always
	 * the inactive buffer.
	 */
	mstate->dtms_scratch_base = (uintptr_t)buf->dtb_xamot;
	mstate->dtms_scratch_size = buf->dtb_size;
	mstate->dtms_scratch_ptr = mstate->dtms_scratch_base;

	return (offs);
}

static void
dtrace_buffer_polish(dtrace_buffer_t *buf)
{
	ASSERT(buf->dtb_flags & DTRACEBUF_RING);
	ASSERT(MUTEX_HELD(&dtrace_lock));

	if (!(buf->dtb_flags & DTRACEBUF_WRAPPED))
		return;

	/*
	 * We need to polish the ring buffer.  There are three cases:
	 *
	 * - The first (and presumably most common) is that there is no gap
	 *   between the buffer offset and the wrapped offset.  In this case,
	 *   there is nothing in the buffer that isn't valid data; we can
	 *   mark the buffer as polished and return.
	 *
	 * - The second (less common than the first but still more common
	 *   than the third) is that there is a gap between the buffer offset
	 *   and the wrapped offset, and the wrapped offset is larger than the
	 *   buffer offset.  This can happen because of an alignment issue, or
	 *   can happen because of a call to dtrace_buffer_reserve() that
	 *   didn't subsequently consume the buffer space.  In this case,
	 *   we need to zero the data from the buffer offset to the wrapped
	 *   offset.
	 *
	 * - The third (and least common) is that there is a gap between the
	 *   buffer offset and the wrapped offset, but the wrapped offset is
	 *   _less_ than the buffer offset.  This can only happen because a
	 *   call to dtrace_buffer_reserve() induced a wrap, but the space
	 *   was not subsequently consumed.  In this case, we need to zero the
	 *   space from the offset to the end of the buffer _and_ from the
	 *   top of the buffer to the wrapped offset.
	 */
	if (buf->dtb_offset < buf->dtb_xamot_offset) {
		bzero(buf->dtb_tomax + buf->dtb_offset,
		    buf->dtb_xamot_offset - buf->dtb_offset);
	}

	if (buf->dtb_offset > buf->dtb_xamot_offset) {
		bzero(buf->dtb_tomax + buf->dtb_offset,
		    buf->dtb_size - buf->dtb_offset);
		bzero(buf->dtb_tomax, buf->dtb_xamot_offset);
	}
}

/*
 * This routine determines if data generated at the specified time has likely
 * been entirely consumed at user-level.  This routine is called to determine
 * if an ECB on a defunct probe (but for an active enabling) can be safely
 * disabled and destroyed.
 */
static int
dtrace_buffer_consumed(dtrace_buffer_t *bufs, hrtime_t when)
{
	int i;

	for (i = 0; i < NCPU; i++) {
		dtrace_buffer_t *buf = &bufs[i];

		if (buf->dtb_size == 0)
			continue;

		if (buf->dtb_flags & DTRACEBUF_RING)
			return (0);

		if (!buf->dtb_switched && buf->dtb_offset != 0)
			return (0);

		if (buf->dtb_switched - buf->dtb_interval < when)
			return (0);
	}

	return (1);
}

static void
dtrace_buffer_free(dtrace_buffer_t *bufs)
{
	int i;

	for (i = 0; i < NCPU; i++) {
		dtrace_buffer_t *buf = &bufs[i];

		if (buf->dtb_tomax == NULL) {
			ASSERT(buf->dtb_xamot == NULL);
			ASSERT(buf->dtb_size == 0);
			continue;
		}

		if (buf->dtb_xamot != NULL) {
			ASSERT(!(buf->dtb_flags & DTRACEBUF_NOSWITCH));
			kmem_free(buf->dtb_xamot, buf->dtb_size);
		}

		kmem_free(buf->dtb_tomax, buf->dtb_size);
		buf->dtb_size = 0;
		buf->dtb_tomax = NULL;
		buf->dtb_xamot = NULL;
	}
}

/*
 * DTrace Enabling Functions
 */
static dtrace_enabling_t *
dtrace_enabling_create(dtrace_vstate_t *vstate)
{
	dtrace_enabling_t *enab;

	enab = kmem_zalloc(sizeof (dtrace_enabling_t), KM_SLEEP);
	enab->dten_vstate = vstate;

	return (enab);
}

static void
dtrace_enabling_add(dtrace_enabling_t *enab, dtrace_ecbdesc_t *ecb)
{
	dtrace_ecbdesc_t **ndesc;
	size_t osize, nsize;

	/*
	 * We can't add to enablings after we've enabled them, or after we've
	 * retained them.
	 */
	ASSERT(enab->dten_probegen == 0);
	ASSERT(enab->dten_next == NULL && enab->dten_prev == NULL);

	if (enab->dten_ndesc < enab->dten_maxdesc) {
		enab->dten_desc[enab->dten_ndesc++] = ecb;
		return;
	}

	osize = enab->dten_maxdesc * sizeof (dtrace_enabling_t *);

	if (enab->dten_maxdesc == 0) {
		enab->dten_maxdesc = 1;
	} else {
		enab->dten_maxdesc <<= 1;
	}

	ASSERT(enab->dten_ndesc < enab->dten_maxdesc);

	nsize = enab->dten_maxdesc * sizeof (dtrace_enabling_t *);
	ndesc = kmem_zalloc(nsize, KM_SLEEP);
	bcopy(enab->dten_desc, ndesc, osize);
	if (enab->dten_desc != NULL)
		kmem_free(enab->dten_desc, osize);

	enab->dten_desc = ndesc;
	enab->dten_desc[enab->dten_ndesc++] = ecb;
}

static void
dtrace_enabling_addlike(dtrace_enabling_t *enab, dtrace_ecbdesc_t *ecb,
    dtrace_probedesc_t *pd)
{
	dtrace_ecbdesc_t *new;
	dtrace_predicate_t *pred;
	dtrace_actdesc_t *act;

	/*
	 * We're going to create a new ECB description that matches the
	 * specified ECB in every way, but has the specified probe description.
	 */
	new = kmem_zalloc(sizeof (dtrace_ecbdesc_t), KM_SLEEP);

	if ((pred = ecb->dted_pred.dtpdd_predicate) != NULL)
		dtrace_predicate_hold(pred);

	for (act = ecb->dted_action; act != NULL; act = act->dtad_next)
		dtrace_actdesc_hold(act);

	new->dted_action = ecb->dted_action;
	new->dted_pred = ecb->dted_pred;
	new->dted_probe = *pd;
	new->dted_uarg = ecb->dted_uarg;

	dtrace_enabling_add(enab, new);
}

static void
dtrace_enabling_dump(dtrace_enabling_t *enab)
{
	int i;

	for (i = 0; i < enab->dten_ndesc; i++) {
		dtrace_probedesc_t *desc = &enab->dten_desc[i]->dted_probe;

#ifdef __FreeBSD__
		printf("dtrace: enabling probe %d (%s:%s:%s:%s)\n", i,
		    desc->dtpd_provider, desc->dtpd_mod,
		    desc->dtpd_func, desc->dtpd_name);
#else
		cmn_err(CE_NOTE, "enabling probe %d (%s:%s:%s:%s)", i,
		    desc->dtpd_provider, desc->dtpd_mod,
		    desc->dtpd_func, desc->dtpd_name);
#endif
	}
}

static void
dtrace_enabling_destroy(dtrace_enabling_t *enab)
{
	int i;
	dtrace_ecbdesc_t *ep;
	dtrace_vstate_t *vstate = enab->dten_vstate;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	for (i = 0; i < enab->dten_ndesc; i++) {
		dtrace_actdesc_t *act, *next;
		dtrace_predicate_t *pred;

		ep = enab->dten_desc[i];

		if ((pred = ep->dted_pred.dtpdd_predicate) != NULL)
			dtrace_predicate_release(pred, vstate);

		for (act = ep->dted_action; act != NULL; act = next) {
			next = act->dtad_next;
			dtrace_actdesc_release(act, vstate);
		}

		kmem_free(ep, sizeof (dtrace_ecbdesc_t));
	}

	if (enab->dten_desc != NULL)
		kmem_free(enab->dten_desc,
		    enab->dten_maxdesc * sizeof (dtrace_enabling_t *));

	/*
	 * If this was a retained enabling, decrement the dts_nretained count
	 * and take it off of the dtrace_retained list.
	 */
	if (enab->dten_prev != NULL || enab->dten_next != NULL ||
	    dtrace_retained == enab) {
		ASSERT(enab->dten_vstate->dtvs_state != NULL);
		ASSERT(enab->dten_vstate->dtvs_state->dts_nretained > 0);
		enab->dten_vstate->dtvs_state->dts_nretained--;
		dtrace_retained_gen++;
	}

	if (enab->dten_prev == NULL) {
		if (dtrace_retained == enab) {
			dtrace_retained = enab->dten_next;

			if (dtrace_retained != NULL)
				dtrace_retained->dten_prev = NULL;
		}
	} else {
		ASSERT(enab != dtrace_retained);
		ASSERT(dtrace_retained != NULL);
		enab->dten_prev->dten_next = enab->dten_next;
	}

	if (enab->dten_next != NULL) {
		ASSERT(dtrace_retained != NULL);
		enab->dten_next->dten_prev = enab->dten_prev;
	}

	kmem_free(enab, sizeof (dtrace_enabling_t));
}

static int
dtrace_enabling_retain(dtrace_enabling_t *enab)
{
	dtrace_state_t *state;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(enab->dten_next == NULL && enab->dten_prev == NULL);
	ASSERT(enab->dten_vstate != NULL);

	state = enab->dten_vstate->dtvs_state;
	ASSERT(state != NULL);

	/*
	 * We only allow each state to retain dtrace_retain_max enablings.
	 */
	if (state->dts_nretained >= dtrace_retain_max)
		return (ENOSPC);

	state->dts_nretained++;
	dtrace_retained_gen++;

	if (dtrace_retained == NULL) {
		dtrace_retained = enab;
		return (0);
	}

	enab->dten_next = dtrace_retained;
	dtrace_retained->dten_prev = enab;
	dtrace_retained = enab;

	return (0);
}

static int
dtrace_enabling_replicate(dtrace_state_t *state, dtrace_probedesc_t *match,
    dtrace_probedesc_t *create)
{
	dtrace_enabling_t *new, *enab;
	int found = 0, err = ENOENT;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(strlen(match->dtpd_provider) < DTRACE_PROVNAMELEN);
	ASSERT(strlen(match->dtpd_mod) < DTRACE_MODNAMELEN);
	ASSERT(strlen(match->dtpd_func) < DTRACE_FUNCNAMELEN);
	ASSERT(strlen(match->dtpd_name) < DTRACE_NAMELEN);

	new = dtrace_enabling_create(&state->dts_vstate);

	/*
	 * Iterate over all retained enablings, looking for enablings that
	 * match the specified state.
	 */
	for (enab = dtrace_retained; enab != NULL; enab = enab->dten_next) {
		int i;

		/*
		 * dtvs_state can only be NULL for helper enablings -- and
		 * helper enablings can't be retained.
		 */
		ASSERT(enab->dten_vstate->dtvs_state != NULL);

		if (enab->dten_vstate->dtvs_state != state)
			continue;

		/*
		 * Now iterate over each probe description; we're looking for
		 * an exact match to the specified probe description.
		 */
		for (i = 0; i < enab->dten_ndesc; i++) {
			dtrace_ecbdesc_t *ep = enab->dten_desc[i];
			dtrace_probedesc_t *pd = &ep->dted_probe;

			if (strcmp(pd->dtpd_provider, match->dtpd_provider))
				continue;

			if (strcmp(pd->dtpd_mod, match->dtpd_mod))
				continue;

			if (strcmp(pd->dtpd_func, match->dtpd_func))
				continue;

			if (strcmp(pd->dtpd_name, match->dtpd_name))
				continue;

			/*
			 * We have a winning probe!  Add it to our growing
			 * enabling.
			 */
			found = 1;
			dtrace_enabling_addlike(new, ep, create);
		}
	}

	if (!found || (err = dtrace_enabling_retain(new)) != 0) {
		dtrace_enabling_destroy(new);
		return (err);
	}

	return (0);
}

static void
dtrace_enabling_retract(dtrace_state_t *state)
{
	dtrace_enabling_t *enab, *next;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	/*
	 * Iterate over all retained enablings, destroy the enablings retained
	 * for the specified state.
	 */
	for (enab = dtrace_retained; enab != NULL; enab = next) {
		next = enab->dten_next;

		/*
		 * dtvs_state can only be NULL for helper enablings -- and
		 * helper enablings can't be retained.
		 */
		ASSERT(enab->dten_vstate->dtvs_state != NULL);

		if (enab->dten_vstate->dtvs_state == state) {
			ASSERT(state->dts_nretained > 0);
			dtrace_enabling_destroy(enab);
		}
	}

	ASSERT(state->dts_nretained == 0);
}

static int
dtrace_enabling_match(dtrace_enabling_t *enab, int *nmatched)
{
	int i = 0;
	int matched = 0;

	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(MUTEX_HELD(&dtrace_lock));

	for (i = 0; i < enab->dten_ndesc; i++) {
		dtrace_ecbdesc_t *ep = enab->dten_desc[i];

		enab->dten_current = ep;
		enab->dten_error = 0;

		matched += dtrace_probe_enable(&ep->dted_probe, enab);

		if (enab->dten_error != 0) {
			/*
			 * If we get an error half-way through enabling the
			 * probes, we kick out -- perhaps with some number of
			 * them enabled.  Leaving enabled probes enabled may
			 * be slightly confusing for user-level, but we expect
			 * that no one will attempt to actually drive on in
			 * the face of such errors.  If this is an anonymous
			 * enabling (indicated with a NULL nmatched pointer),
			 * we cmn_err() a message.  We aren't expecting to
			 * get such an error -- such as it can exist at all,
			 * it would be a result of corrupted DOF in the driver
			 * properties.
			 */
			if (nmatched == NULL) {
				cmn_err(CE_WARN, "dtrace_enabling_match() "
				    "error on %p: %d", (void *)ep,
				    enab->dten_error);
			}

			return (enab->dten_error);
		}
	}

	enab->dten_probegen = dtrace_probegen;
	if (nmatched != NULL)
		*nmatched = matched;

	return (0);
}

static void
dtrace_enabling_matchall(void)
{
	dtrace_enabling_t *enab;

	mutex_enter(&cpu_lock);
	mutex_enter(&dtrace_lock);

	/*
	 * Iterate over all retained enablings to see if any probes match
	 * against them.  We only perform this operation on enablings for which
	 * we have sufficient permissions by virtue of being in the global zone
	 * or in the same zone as the DTrace client.  Because we can be called
	 * after dtrace_detach() has been called, we cannot assert that there
	 * are retained enablings.  We can safely load from dtrace_retained,
	 * however:  the taskq_destroy() at the end of dtrace_detach() will
	 * block pending our completion.
	 */
	for (enab = dtrace_retained; enab != NULL; enab = enab->dten_next) {
#ifdef illumos
		cred_t *cr = enab->dten_vstate->dtvs_state->dts_cred.dcr_cred;

		if (INGLOBALZONE(curproc) ||
		    cr != NULL && getzoneid() == crgetzoneid(cr))
#endif
			(void) dtrace_enabling_match(enab, NULL);
	}

	mutex_exit(&dtrace_lock);
	mutex_exit(&cpu_lock);
}

/*
 * If an enabling is to be enabled without having matched probes (that is, if
 * dtrace_state_go() is to be called on the underlying dtrace_state_t), the
 * enabling must be _primed_ by creating an ECB for every ECB description.
 * This must be done to assure that we know the number of speculations, the
 * number of aggregations, the minimum buffer size needed, etc. before we
 * transition out of DTRACE_ACTIVITY_INACTIVE.  To do this without actually
 * enabling any probes, we create ECBs for every ECB decription, but with a
 * NULL probe -- which is exactly what this function does.
 */
static void
dtrace_enabling_prime(dtrace_state_t *state)
{
	dtrace_enabling_t *enab;
	int i;

	for (enab = dtrace_retained; enab != NULL; enab = enab->dten_next) {
		ASSERT(enab->dten_vstate->dtvs_state != NULL);

		if (enab->dten_vstate->dtvs_state != state)
			continue;

		/*
		 * We don't want to prime an enabling more than once, lest
		 * we allow a malicious user to induce resource exhaustion.
		 * (The ECBs that result from priming an enabling aren't
		 * leaked -- but they also aren't deallocated until the
		 * consumer state is destroyed.)
		 */
		if (enab->dten_primed)
			continue;

		for (i = 0; i < enab->dten_ndesc; i++) {
			enab->dten_current = enab->dten_desc[i];
			(void) dtrace_probe_enable(NULL, enab);
		}

		enab->dten_primed = 1;
	}
}

/*
 * Called to indicate that probes should be provided due to retained
 * enablings.  This is implemented in terms of dtrace_probe_provide(), but it
 * must take an initial lap through the enabling calling the dtps_provide()
 * entry point explicitly to allow for autocreated probes.
 */
static void
dtrace_enabling_provide(dtrace_provider_t *prv)
{
	int i, all = 0;
	dtrace_probedesc_t desc;
	dtrace_genid_t gen;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(MUTEX_HELD(&dtrace_provider_lock));

	if (prv == NULL) {
		all = 1;
		prv = dtrace_provider;
	}

	do {
		dtrace_enabling_t *enab;
		void *parg = prv->dtpv_arg;

retry:
		gen = dtrace_retained_gen;
		for (enab = dtrace_retained; enab != NULL;
		    enab = enab->dten_next) {
			for (i = 0; i < enab->dten_ndesc; i++) {
				desc = enab->dten_desc[i]->dted_probe;
				mutex_exit(&dtrace_lock);
				prv->dtpv_pops.dtps_provide(parg, &desc);
				mutex_enter(&dtrace_lock);
				/*
				 * Process the retained enablings again if
				 * they have changed while we weren't holding
				 * dtrace_lock.
				 */
				if (gen != dtrace_retained_gen)
					goto retry;
			}
		}
	} while (all && (prv = prv->dtpv_next) != NULL);

	mutex_exit(&dtrace_lock);
	dtrace_probe_provide(NULL, all ? NULL : prv);
	mutex_enter(&dtrace_lock);
}

/*
 * Called to reap ECBs that are attached to probes from defunct providers.
 */
static void
dtrace_enabling_reap(void)
{
	dtrace_provider_t *prov;
	dtrace_probe_t *probe;
	dtrace_ecb_t *ecb;
	hrtime_t when;
	int i;

	mutex_enter(&cpu_lock);
	mutex_enter(&dtrace_lock);

	for (i = 0; i < dtrace_nprobes; i++) {
		if ((probe = dtrace_probes[i]) == NULL)
			continue;

		if (probe->dtpr_ecb == NULL)
			continue;

		prov = probe->dtpr_provider;

		if ((when = prov->dtpv_defunct) == 0)
			continue;

		/*
		 * We have ECBs on a defunct provider:  we want to reap these
		 * ECBs to allow the provider to unregister.  The destruction
		 * of these ECBs must be done carefully:  if we destroy the ECB
		 * and the consumer later wishes to consume an EPID that
		 * corresponds to the destroyed ECB (and if the EPID metadata
		 * has not been previously consumed), the consumer will abort
		 * processing on the unknown EPID.  To reduce (but not, sadly,
		 * eliminate) the possibility of this, we will only destroy an
		 * ECB for a defunct provider if, for the state that
		 * corresponds to the ECB:
		 *
		 *  (a)	There is no speculative tracing (which can effectively
		 *	cache an EPID for an arbitrary amount of time).
		 *
		 *  (b)	The principal buffers have been switched twice since the
		 *	provider became defunct.
		 *
		 *  (c)	The aggregation buffers are of zero size or have been
		 *	switched twice since the provider became defunct.
		 *
		 * We use dts_speculates to determine (a) and call a function
		 * (dtrace_buffer_consumed()) to determine (b) and (c).  Note
		 * that as soon as we've been unable to destroy one of the ECBs
		 * associated with the probe, we quit trying -- reaping is only
		 * fruitful in as much as we can destroy all ECBs associated
		 * with the defunct provider's probes.
		 */
		while ((ecb = probe->dtpr_ecb) != NULL) {
			dtrace_state_t *state = ecb->dte_state;
			dtrace_buffer_t *buf = state->dts_buffer;
			dtrace_buffer_t *aggbuf = state->dts_aggbuffer;

			if (state->dts_speculates)
				break;

			if (!dtrace_buffer_consumed(buf, when))
				break;

			if (!dtrace_buffer_consumed(aggbuf, when))
				break;

			dtrace_ecb_disable(ecb);
			ASSERT(probe->dtpr_ecb != ecb);
			dtrace_ecb_destroy(ecb);
		}
	}

	mutex_exit(&dtrace_lock);
	mutex_exit(&cpu_lock);
}

/*
 * DTrace DOF Functions
 */
/*ARGSUSED*/
static void
dtrace_dof_error(dof_hdr_t *dof, const char *str)
{
	if (dtrace_err_verbose)
		cmn_err(CE_WARN, "failed to process DOF: %s", str);

#ifdef DTRACE_ERRDEBUG
	dtrace_errdebug(str);
#endif
}

/*
 * Create DOF out of a currently enabled state.  Right now, we only create
 * DOF containing the run-time options -- but this could be expanded to create
 * complete DOF representing the enabled state.
 */
static dof_hdr_t *
dtrace_dof_create(dtrace_state_t *state)
{
	dof_hdr_t *dof;
	dof_sec_t *sec;
	dof_optdesc_t *opt;
	int i, len = sizeof (dof_hdr_t) +
	    roundup(sizeof (dof_sec_t), sizeof (uint64_t)) +
	    sizeof (dof_optdesc_t) * DTRACEOPT_MAX;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	dof = kmem_zalloc(len, KM_SLEEP);
	dof->dofh_ident[DOF_ID_MAG0] = DOF_MAG_MAG0;
	dof->dofh_ident[DOF_ID_MAG1] = DOF_MAG_MAG1;
	dof->dofh_ident[DOF_ID_MAG2] = DOF_MAG_MAG2;
	dof->dofh_ident[DOF_ID_MAG3] = DOF_MAG_MAG3;

	dof->dofh_ident[DOF_ID_MODEL] = DOF_MODEL_NATIVE;
	dof->dofh_ident[DOF_ID_ENCODING] = DOF_ENCODE_NATIVE;
	dof->dofh_ident[DOF_ID_VERSION] = DOF_VERSION;
	dof->dofh_ident[DOF_ID_DIFVERS] = DIF_VERSION;
	dof->dofh_ident[DOF_ID_DIFIREG] = DIF_DIR_NREGS;
	dof->dofh_ident[DOF_ID_DIFTREG] = DIF_DTR_NREGS;

	dof->dofh_flags = 0;
	dof->dofh_hdrsize = sizeof (dof_hdr_t);
	dof->dofh_secsize = sizeof (dof_sec_t);
	dof->dofh_secnum = 1;	/* only DOF_SECT_OPTDESC */
	dof->dofh_secoff = sizeof (dof_hdr_t);
	dof->dofh_loadsz = len;
	dof->dofh_filesz = len;
	dof->dofh_pad = 0;

	/*
	 * Fill in the option section header...
	 */
	sec = (dof_sec_t *)((uintptr_t)dof + sizeof (dof_hdr_t));
	sec->dofs_type = DOF_SECT_OPTDESC;
	sec->dofs_align = sizeof (uint64_t);
	sec->dofs_flags = DOF_SECF_LOAD;
	sec->dofs_entsize = sizeof (dof_optdesc_t);

	opt = (dof_optdesc_t *)((uintptr_t)sec +
	    roundup(sizeof (dof_sec_t), sizeof (uint64_t)));

	sec->dofs_offset = (uintptr_t)opt - (uintptr_t)dof;
	sec->dofs_size = sizeof (dof_optdesc_t) * DTRACEOPT_MAX;

	for (i = 0; i < DTRACEOPT_MAX; i++) {
		opt[i].dofo_option = i;
		opt[i].dofo_strtab = DOF_SECIDX_NONE;
		opt[i].dofo_value = state->dts_options[i];
	}

	return (dof);
}

static dof_hdr_t *
dtrace_dof_copyin(uintptr_t uarg, int *errp)
{
	dof_hdr_t hdr, *dof;

	ASSERT(!MUTEX_HELD(&dtrace_lock));

	/*
	 * First, we're going to copyin() the sizeof (dof_hdr_t).
	 */
	if (copyin((void *)uarg, &hdr, sizeof (hdr)) != 0) {
		dtrace_dof_error(NULL, "failed to copyin DOF header");
		*errp = EFAULT;
		return (NULL);
	}

	/*
	 * Now we'll allocate the entire DOF and copy it in -- provided
	 * that the length isn't outrageous.
	 */
	if (hdr.dofh_loadsz >= dtrace_dof_maxsize) {
		dtrace_dof_error(&hdr, "load size exceeds maximum");
		*errp = E2BIG;
		return (NULL);
	}

	if (hdr.dofh_loadsz < sizeof (hdr)) {
		dtrace_dof_error(&hdr, "invalid load size");
		*errp = EINVAL;
		return (NULL);
	}

	dof = kmem_alloc(hdr.dofh_loadsz, KM_SLEEP);

	if (copyin((void *)uarg, dof, hdr.dofh_loadsz) != 0 ||
	    dof->dofh_loadsz != hdr.dofh_loadsz) {
		kmem_free(dof, hdr.dofh_loadsz);
		*errp = EFAULT;
		return (NULL);
	}

	return (dof);
}

#ifdef __FreeBSD__
static dof_hdr_t *
dtrace_dof_copyin_proc(struct proc *p, uintptr_t uarg, int *errp)
{
	dof_hdr_t hdr, *dof;
	struct thread *td;
	size_t loadsz;

	ASSERT(!MUTEX_HELD(&dtrace_lock));

	td = curthread;

	/*
	 * First, we're going to copyin() the sizeof (dof_hdr_t).
	 */
	if (proc_readmem(td, p, uarg, &hdr, sizeof(hdr)) != sizeof(hdr)) {
		dtrace_dof_error(NULL, "failed to copyin DOF header");
		*errp = EFAULT;
		return (NULL);
	}

	/*
	 * Now we'll allocate the entire DOF and copy it in -- provided
	 * that the length isn't outrageous.
	 */
	if (hdr.dofh_loadsz >= dtrace_dof_maxsize) {
		dtrace_dof_error(&hdr, "load size exceeds maximum");
		*errp = E2BIG;
		return (NULL);
	}
	loadsz = (size_t)hdr.dofh_loadsz;

	if (loadsz < sizeof (hdr)) {
		dtrace_dof_error(&hdr, "invalid load size");
		*errp = EINVAL;
		return (NULL);
	}

	dof = kmem_alloc(loadsz, KM_SLEEP);

	if (proc_readmem(td, p, uarg, dof, loadsz) != loadsz ||
	    dof->dofh_loadsz != loadsz) {
		kmem_free(dof, hdr.dofh_loadsz);
		*errp = EFAULT;
		return (NULL);
	}

	return (dof);
}

static __inline uchar_t
dtrace_dof_char(char c)
{

	switch (c) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		return (c - '0');
	case 'A':
	case 'B':
	case 'C':
	case 'D':
	case 'E':
	case 'F':
		return (c - 'A' + 10);
	case 'a':
	case 'b':
	case 'c':
	case 'd':
	case 'e':
	case 'f':
		return (c - 'a' + 10);
	}
	/* Should not reach here. */
	return (UCHAR_MAX);
}
#endif /* __FreeBSD__ */

static dof_hdr_t *
dtrace_dof_property(const char *name)
{
#ifdef __FreeBSD__
	uint8_t *dofbuf;
	u_char *data, *eol;
	caddr_t doffile;
	size_t bytes, len, i;
	dof_hdr_t *dof;
	u_char c1, c2;

	dof = NULL;

	doffile = preload_search_by_type("dtrace_dof");
	if (doffile == NULL)
		return (NULL);

	data = preload_fetch_addr(doffile);
	len = preload_fetch_size(doffile);
	for (;;) {
		/* Look for the end of the line. All lines end in a newline. */
		eol = memchr(data, '\n', len);
		if (eol == NULL)
			return (NULL);

		if (strncmp(name, data, strlen(name)) == 0)
			break;

		eol++; /* skip past the newline */
		len -= eol - data;
		data = eol;
	}

	/* We've found the data corresponding to the specified key. */

	data += strlen(name) + 1; /* skip past the '=' */
	len = eol - data;
	if (len % 2 != 0) {
		dtrace_dof_error(NULL, "invalid DOF encoding length");
		goto doferr;
	}
	bytes = len / 2;
	if (bytes < sizeof(dof_hdr_t)) {
		dtrace_dof_error(NULL, "truncated header");
		goto doferr;
	}

	/*
	 * Each byte is represented by the two ASCII characters in its hex
	 * representation.
	 */
	dofbuf = malloc(bytes, M_SOLARIS, M_WAITOK);
	for (i = 0; i < bytes; i++) {
		c1 = dtrace_dof_char(data[i * 2]);
		c2 = dtrace_dof_char(data[i * 2 + 1]);
		if (c1 == UCHAR_MAX || c2 == UCHAR_MAX) {
			dtrace_dof_error(NULL, "invalid hex char in DOF");
			goto doferr;
		}
		dofbuf[i] = c1 * 16 + c2;
	}

	dof = (dof_hdr_t *)dofbuf;
	if (bytes < dof->dofh_loadsz) {
		dtrace_dof_error(NULL, "truncated DOF");
		goto doferr;
	}

	if (dof->dofh_loadsz >= dtrace_dof_maxsize) {
		dtrace_dof_error(NULL, "oversized DOF");
		goto doferr;
	}

	return (dof);

doferr:
	free(dof, M_SOLARIS);
	return (NULL);
#else /* __FreeBSD__ */
	uchar_t *buf;
	uint64_t loadsz;
	unsigned int len, i;
	dof_hdr_t *dof;

	/*
	 * Unfortunately, array of values in .conf files are always (and
	 * only) interpreted to be integer arrays.  We must read our DOF
	 * as an integer array, and then squeeze it into a byte array.
	 */
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dtrace_devi, 0,
	    (char *)name, (int **)&buf, &len) != DDI_PROP_SUCCESS)
		return (NULL);

	for (i = 0; i < len; i++)
		buf[i] = (uchar_t)(((int *)buf)[i]);

	if (len < sizeof (dof_hdr_t)) {
		ddi_prop_free(buf);
		dtrace_dof_error(NULL, "truncated header");
		return (NULL);
	}

	if (len < (loadsz = ((dof_hdr_t *)buf)->dofh_loadsz)) {
		ddi_prop_free(buf);
		dtrace_dof_error(NULL, "truncated DOF");
		return (NULL);
	}

	if (loadsz >= dtrace_dof_maxsize) {
		ddi_prop_free(buf);
		dtrace_dof_error(NULL, "oversized DOF");
		return (NULL);
	}

	dof = kmem_alloc(loadsz, KM_SLEEP);
	bcopy(buf, dof, loadsz);
	ddi_prop_free(buf);

	return (dof);
#endif /* !__FreeBSD__ */
}

static void
dtrace_dof_destroy(dof_hdr_t *dof)
{
	kmem_free(dof, dof->dofh_loadsz);
}

/*
 * Return the dof_sec_t pointer corresponding to a given section index.  If the
 * index is not valid, dtrace_dof_error() is called and NULL is returned.  If
 * a type other than DOF_SECT_NONE is specified, the header is checked against
 * this type and NULL is returned if the types do not match.
 */
static dof_sec_t *
dtrace_dof_sect(dof_hdr_t *dof, uint32_t type, dof_secidx_t i)
{
	dof_sec_t *sec = (dof_sec_t *)(uintptr_t)
	    ((uintptr_t)dof + dof->dofh_secoff + i * dof->dofh_secsize);

	if (i >= dof->dofh_secnum) {
		dtrace_dof_error(dof, "referenced section index is invalid");
		return (NULL);
	}

	if (!(sec->dofs_flags & DOF_SECF_LOAD)) {
		dtrace_dof_error(dof, "referenced section is not loadable");
		return (NULL);
	}

	if (type != DOF_SECT_NONE && type != sec->dofs_type) {
		dtrace_dof_error(dof, "referenced section is the wrong type");
		return (NULL);
	}

	return (sec);
}

static dtrace_probedesc_t *
dtrace_dof_probedesc(dof_hdr_t *dof, dof_sec_t *sec, dtrace_probedesc_t *desc)
{
	dof_probedesc_t *probe;
	dof_sec_t *strtab;
	uintptr_t daddr = (uintptr_t)dof;
	uintptr_t str;
	size_t size;

	if (sec->dofs_type != DOF_SECT_PROBEDESC) {
		dtrace_dof_error(dof, "invalid probe section");
		return (NULL);
	}

	if (sec->dofs_align != sizeof (dof_secidx_t)) {
		dtrace_dof_error(dof, "bad alignment in probe description");
		return (NULL);
	}

	if (sec->dofs_offset + sizeof (dof_probedesc_t) > dof->dofh_loadsz) {
		dtrace_dof_error(dof, "truncated probe description");
		return (NULL);
	}

	probe = (dof_probedesc_t *)(uintptr_t)(daddr + sec->dofs_offset);
	strtab = dtrace_dof_sect(dof, DOF_SECT_STRTAB, probe->dofp_strtab);

	if (strtab == NULL)
		return (NULL);

	str = daddr + strtab->dofs_offset;
	size = strtab->dofs_size;

	if (probe->dofp_provider >= strtab->dofs_size) {
		dtrace_dof_error(dof, "corrupt probe provider");
		return (NULL);
	}

	(void) strncpy(desc->dtpd_provider,
	    (char *)(str + probe->dofp_provider),
	    MIN(DTRACE_PROVNAMELEN - 1, size - probe->dofp_provider));

	if (probe->dofp_mod >= strtab->dofs_size) {
		dtrace_dof_error(dof, "corrupt probe module");
		return (NULL);
	}

	(void) strncpy(desc->dtpd_mod, (char *)(str + probe->dofp_mod),
	    MIN(DTRACE_MODNAMELEN - 1, size - probe->dofp_mod));

	if (probe->dofp_func >= strtab->dofs_size) {
		dtrace_dof_error(dof, "corrupt probe function");
		return (NULL);
	}

	(void) strncpy(desc->dtpd_func, (char *)(str + probe->dofp_func),
	    MIN(DTRACE_FUNCNAMELEN - 1, size - probe->dofp_func));

	if (probe->dofp_name >= strtab->dofs_size) {
		dtrace_dof_error(dof, "corrupt probe name");
		return (NULL);
	}

	(void) strncpy(desc->dtpd_name, (char *)(str + probe->dofp_name),
	    MIN(DTRACE_NAMELEN - 1, size - probe->dofp_name));

	return (desc);
}

static dtrace_difo_t *
dtrace_dof_difo(dof_hdr_t *dof, dof_sec_t *sec, dtrace_vstate_t *vstate,
    cred_t *cr)
{
	dtrace_difo_t *dp;
	size_t ttl = 0;
	dof_difohdr_t *dofd;
	uintptr_t daddr = (uintptr_t)dof;
	size_t max = dtrace_difo_maxsize;
	int i, l, n;

	static const struct {
		int section;
		int bufoffs;
		int lenoffs;
		int entsize;
		int align;
		const char *msg;
	} difo[] = {
		{ DOF_SECT_DIF, offsetof(dtrace_difo_t, dtdo_buf),
		offsetof(dtrace_difo_t, dtdo_len), sizeof (dif_instr_t),
		sizeof (dif_instr_t), "multiple DIF sections" },

		{ DOF_SECT_INTTAB, offsetof(dtrace_difo_t, dtdo_inttab),
		offsetof(dtrace_difo_t, dtdo_intlen), sizeof (uint64_t),
		sizeof (uint64_t), "multiple integer tables" },

		{ DOF_SECT_STRTAB, offsetof(dtrace_difo_t, dtdo_strtab),
		offsetof(dtrace_difo_t, dtdo_strlen), 0,
		sizeof (char), "multiple string tables" },

		{ DOF_SECT_VARTAB, offsetof(dtrace_difo_t, dtdo_vartab),
		offsetof(dtrace_difo_t, dtdo_varlen), sizeof (dtrace_difv_t),
		sizeof (uint_t), "multiple variable tables" },

		{ DOF_SECT_NONE, 0, 0, 0, 0, NULL }
	};

	if (sec->dofs_type != DOF_SECT_DIFOHDR) {
		dtrace_dof_error(dof, "invalid DIFO header section");
		return (NULL);
	}

	if (sec->dofs_align != sizeof (dof_secidx_t)) {
		dtrace_dof_error(dof, "bad alignment in DIFO header");
		return (NULL);
	}

	if (sec->dofs_size < sizeof (dof_difohdr_t) ||
	    sec->dofs_size % sizeof (dof_secidx_t)) {
		dtrace_dof_error(dof, "bad size in DIFO header");
		return (NULL);
	}

	dofd = (dof_difohdr_t *)(uintptr_t)(daddr + sec->dofs_offset);
	n = (sec->dofs_size - sizeof (*dofd)) / sizeof (dof_secidx_t) + 1;

	dp = kmem_zalloc(sizeof (dtrace_difo_t), KM_SLEEP);
	dp->dtdo_rtype = dofd->dofd_rtype;

	for (l = 0; l < n; l++) {
		dof_sec_t *subsec;
		void **bufp;
		uint32_t *lenp;

		if ((subsec = dtrace_dof_sect(dof, DOF_SECT_NONE,
		    dofd->dofd_links[l])) == NULL)
			goto err; /* invalid section link */

		if (ttl + subsec->dofs_size > max) {
			dtrace_dof_error(dof, "exceeds maximum size");
			goto err;
		}

		ttl += subsec->dofs_size;

		for (i = 0; difo[i].section != DOF_SECT_NONE; i++) {
			if (subsec->dofs_type != difo[i].section)
				continue;

			if (!(subsec->dofs_flags & DOF_SECF_LOAD)) {
				dtrace_dof_error(dof, "section not loaded");
				goto err;
			}

			if (subsec->dofs_align != difo[i].align) {
				dtrace_dof_error(dof, "bad alignment");
				goto err;
			}

			bufp = (void **)((uintptr_t)dp + difo[i].bufoffs);
			lenp = (uint32_t *)((uintptr_t)dp + difo[i].lenoffs);

			if (*bufp != NULL) {
				dtrace_dof_error(dof, difo[i].msg);
				goto err;
			}

			if (difo[i].entsize != subsec->dofs_entsize) {
				dtrace_dof_error(dof, "entry size mismatch");
				goto err;
			}

			if (subsec->dofs_entsize != 0 &&
			    (subsec->dofs_size % subsec->dofs_entsize) != 0) {
				dtrace_dof_error(dof, "corrupt entry size");
				goto err;
			}

			*lenp = subsec->dofs_size;
			*bufp = kmem_alloc(subsec->dofs_size, KM_SLEEP);
			bcopy((char *)(uintptr_t)(daddr + subsec->dofs_offset),
			    *bufp, subsec->dofs_size);

			if (subsec->dofs_entsize != 0)
				*lenp /= subsec->dofs_entsize;

			break;
		}

		/*
		 * If we encounter a loadable DIFO sub-section that is not
		 * known to us, assume this is a broken program and fail.
		 */
		if (difo[i].section == DOF_SECT_NONE &&
		    (subsec->dofs_flags & DOF_SECF_LOAD)) {
			dtrace_dof_error(dof, "unrecognized DIFO subsection");
			goto err;
		}
	}

	if (dp->dtdo_buf == NULL) {
		/*
		 * We can't have a DIF object without DIF text.
		 */
		dtrace_dof_error(dof, "missing DIF text");
		goto err;
	}

	/*
	 * Before we validate the DIF object, run through the variable table
	 * looking for the strings -- if any of their size are under, we'll set
	 * their size to be the system-wide default string size.  Note that
	 * this should _not_ happen if the "strsize" option has been set --
	 * in this case, the compiler should have set the size to reflect the
	 * setting of the option.
	 */
	for (i = 0; i < dp->dtdo_varlen; i++) {
		dtrace_difv_t *v = &dp->dtdo_vartab[i];
		dtrace_diftype_t *t = &v->dtdv_type;

		if (v->dtdv_id < DIF_VAR_OTHER_UBASE)
			continue;

		if (t->dtdt_kind == DIF_TYPE_STRING && t->dtdt_size == 0)
			t->dtdt_size = dtrace_strsize_default;
	}

	if (dtrace_difo_validate(dp, vstate, DIF_DIR_NREGS, cr) != 0)
		goto err;

	dtrace_difo_init(dp, vstate);
	return (dp);

err:
	kmem_free(dp->dtdo_buf, dp->dtdo_len * sizeof (dif_instr_t));
	kmem_free(dp->dtdo_inttab, dp->dtdo_intlen * sizeof (uint64_t));
	kmem_free(dp->dtdo_strtab, dp->dtdo_strlen);
	kmem_free(dp->dtdo_vartab, dp->dtdo_varlen * sizeof (dtrace_difv_t));

	kmem_free(dp, sizeof (dtrace_difo_t));
	return (NULL);
}

static dtrace_predicate_t *
dtrace_dof_predicate(dof_hdr_t *dof, dof_sec_t *sec, dtrace_vstate_t *vstate,
    cred_t *cr)
{
	dtrace_difo_t *dp;

	if ((dp = dtrace_dof_difo(dof, sec, vstate, cr)) == NULL)
		return (NULL);

	return (dtrace_predicate_create(dp));
}

static dtrace_actdesc_t *
dtrace_dof_actdesc(dof_hdr_t *dof, dof_sec_t *sec, dtrace_vstate_t *vstate,
    cred_t *cr)
{
	dtrace_actdesc_t *act, *first = NULL, *last = NULL, *next;
	dof_actdesc_t *desc;
	dof_sec_t *difosec;
	size_t offs;
	uintptr_t daddr = (uintptr_t)dof;
	uint64_t arg;
	dtrace_actkind_t kind;

	if (sec->dofs_type != DOF_SECT_ACTDESC) {
		dtrace_dof_error(dof, "invalid action section");
		return (NULL);
	}

	if (sec->dofs_offset + sizeof (dof_actdesc_t) > dof->dofh_loadsz) {
		dtrace_dof_error(dof, "truncated action description");
		return (NULL);
	}

	if (sec->dofs_align != sizeof (uint64_t)) {
		dtrace_dof_error(dof, "bad alignment in action description");
		return (NULL);
	}

	if (sec->dofs_size < sec->dofs_entsize) {
		dtrace_dof_error(dof, "section entry size exceeds total size");
		return (NULL);
	}

	if (sec->dofs_entsize != sizeof (dof_actdesc_t)) {
		dtrace_dof_error(dof, "bad entry size in action description");
		return (NULL);
	}

	if (sec->dofs_size / sec->dofs_entsize > dtrace_actions_max) {
		dtrace_dof_error(dof, "actions exceed dtrace_actions_max");
		return (NULL);
	}

	for (offs = 0; offs < sec->dofs_size; offs += sec->dofs_entsize) {
		desc = (dof_actdesc_t *)(daddr +
		    (uintptr_t)sec->dofs_offset + offs);
		kind = (dtrace_actkind_t)desc->dofa_kind;

		if ((DTRACEACT_ISPRINTFLIKE(kind) &&
		    (kind != DTRACEACT_PRINTA ||
		    desc->dofa_strtab != DOF_SECIDX_NONE)) ||
		    (kind == DTRACEACT_DIFEXPR &&
		    desc->dofa_strtab != DOF_SECIDX_NONE)) {
			dof_sec_t *strtab;
			char *str, *fmt;
			uint64_t i;

			/*
			 * The argument to these actions is an index into the
			 * DOF string table.  For printf()-like actions, this
			 * is the format string.  For print(), this is the
			 * CTF type of the expression result.
			 */
			if ((strtab = dtrace_dof_sect(dof,
			    DOF_SECT_STRTAB, desc->dofa_strtab)) == NULL)
				goto err;

			str = (char *)((uintptr_t)dof +
			    (uintptr_t)strtab->dofs_offset);

			for (i = desc->dofa_arg; i < strtab->dofs_size; i++) {
				if (str[i] == '\0')
					break;
			}

			if (i >= strtab->dofs_size) {
				dtrace_dof_error(dof, "bogus format string");
				goto err;
			}

			if (i == desc->dofa_arg) {
				dtrace_dof_error(dof, "empty format string");
				goto err;
			}

			i -= desc->dofa_arg;
			fmt = kmem_alloc(i + 1, KM_SLEEP);
			bcopy(&str[desc->dofa_arg], fmt, i + 1);
			arg = (uint64_t)(uintptr_t)fmt;
		} else {
			if (kind == DTRACEACT_PRINTA) {
				ASSERT(desc->dofa_strtab == DOF_SECIDX_NONE);
				arg = 0;
			} else {
				arg = desc->dofa_arg;
			}
		}

		act = dtrace_actdesc_create(kind, desc->dofa_ntuple,
		    desc->dofa_uarg, arg);

		if (last != NULL) {
			last->dtad_next = act;
		} else {
			first = act;
		}

		last = act;

		if (desc->dofa_difo == DOF_SECIDX_NONE)
			continue;

		if ((difosec = dtrace_dof_sect(dof,
		    DOF_SECT_DIFOHDR, desc->dofa_difo)) == NULL)
			goto err;

		act->dtad_difo = dtrace_dof_difo(dof, difosec, vstate, cr);

		if (act->dtad_difo == NULL)
			goto err;
	}

	ASSERT(first != NULL);
	return (first);

err:
	for (act = first; act != NULL; act = next) {
		next = act->dtad_next;
		dtrace_actdesc_release(act, vstate);
	}

	return (NULL);
}

static dtrace_ecbdesc_t *
dtrace_dof_ecbdesc(dof_hdr_t *dof, dof_sec_t *sec, dtrace_vstate_t *vstate,
    cred_t *cr)
{
	dtrace_ecbdesc_t *ep;
	dof_ecbdesc_t *ecb;
	dtrace_probedesc_t *desc;
	dtrace_predicate_t *pred = NULL;

	if (sec->dofs_size < sizeof (dof_ecbdesc_t)) {
		dtrace_dof_error(dof, "truncated ECB description");
		return (NULL);
	}

	if (sec->dofs_align != sizeof (uint64_t)) {
		dtrace_dof_error(dof, "bad alignment in ECB description");
		return (NULL);
	}

	ecb = (dof_ecbdesc_t *)((uintptr_t)dof + (uintptr_t)sec->dofs_offset);
	sec = dtrace_dof_sect(dof, DOF_SECT_PROBEDESC, ecb->dofe_probes);

	if (sec == NULL)
		return (NULL);

	ep = kmem_zalloc(sizeof (dtrace_ecbdesc_t), KM_SLEEP);
	ep->dted_uarg = ecb->dofe_uarg;
	desc = &ep->dted_probe;

	if (dtrace_dof_probedesc(dof, sec, desc) == NULL)
		goto err;

	if (ecb->dofe_pred != DOF_SECIDX_NONE) {
		if ((sec = dtrace_dof_sect(dof,
		    DOF_SECT_DIFOHDR, ecb->dofe_pred)) == NULL)
			goto err;

		if ((pred = dtrace_dof_predicate(dof, sec, vstate, cr)) == NULL)
			goto err;

		ep->dted_pred.dtpdd_predicate = pred;
	}

	if (ecb->dofe_actions != DOF_SECIDX_NONE) {
		if ((sec = dtrace_dof_sect(dof,
		    DOF_SECT_ACTDESC, ecb->dofe_actions)) == NULL)
			goto err;

		ep->dted_action = dtrace_dof_actdesc(dof, sec, vstate, cr);

		if (ep->dted_action == NULL)
			goto err;
	}

	return (ep);

err:
	if (pred != NULL)
		dtrace_predicate_release(pred, vstate);
	kmem_free(ep, sizeof (dtrace_ecbdesc_t));
	return (NULL);
}

/*
 * Apply the relocations from the specified 'sec' (a DOF_SECT_URELHDR) to the
 * specified DOF.  SETX relocations are computed using 'ubase', the base load
 * address of the object containing the DOF, and DOFREL relocations are relative
 * to the relocation offset within the DOF.
 */
static int
dtrace_dof_relocate(dof_hdr_t *dof, dof_sec_t *sec, uint64_t ubase,
    uint64_t udaddr)
{
	uintptr_t daddr = (uintptr_t)dof;
	uintptr_t ts_end;
	dof_relohdr_t *dofr =
	    (dof_relohdr_t *)(uintptr_t)(daddr + sec->dofs_offset);
	dof_sec_t *ss, *rs, *ts;
	dof_relodesc_t *r;
	uint_t i, n;

	if (sec->dofs_size < sizeof (dof_relohdr_t) ||
	    sec->dofs_align != sizeof (dof_secidx_t)) {
		dtrace_dof_error(dof, "invalid relocation header");
		return (-1);
	}

	ss = dtrace_dof_sect(dof, DOF_SECT_STRTAB, dofr->dofr_strtab);
	rs = dtrace_dof_sect(dof, DOF_SECT_RELTAB, dofr->dofr_relsec);
	ts = dtrace_dof_sect(dof, DOF_SECT_NONE, dofr->dofr_tgtsec);
	ts_end = (uintptr_t)ts + sizeof (dof_sec_t);

	if (ss == NULL || rs == NULL || ts == NULL)
		return (-1); /* dtrace_dof_error() has been called already */

	if (rs->dofs_entsize < sizeof (dof_relodesc_t) ||
	    rs->dofs_align != sizeof (uint64_t)) {
		dtrace_dof_error(dof, "invalid relocation section");
		return (-1);
	}

	r = (dof_relodesc_t *)(uintptr_t)(daddr + rs->dofs_offset);
	n = rs->dofs_size / rs->dofs_entsize;

	for (i = 0; i < n; i++) {
		uintptr_t taddr = daddr + ts->dofs_offset + r->dofr_offset;

		switch (r->dofr_type) {
		case DOF_RELO_NONE:
			break;
		case DOF_RELO_SETX:
		case DOF_RELO_DOFREL:
			if (r->dofr_offset >= ts->dofs_size || r->dofr_offset +
			    sizeof (uint64_t) > ts->dofs_size) {
				dtrace_dof_error(dof, "bad relocation offset");
				return (-1);
			}

			if (taddr >= (uintptr_t)ts && taddr < ts_end) {
				dtrace_dof_error(dof, "bad relocation offset");
				return (-1);
			}

			if (!IS_P2ALIGNED(taddr, sizeof (uint64_t))) {
				dtrace_dof_error(dof, "misaligned setx relo");
				return (-1);
			}

			if (r->dofr_type == DOF_RELO_SETX)
				*(uint64_t *)taddr += ubase;
			else
				*(uint64_t *)taddr +=
				    udaddr + ts->dofs_offset + r->dofr_offset;
			break;
		default:
			dtrace_dof_error(dof, "invalid relocation type");
			return (-1);
		}

		r = (dof_relodesc_t *)((uintptr_t)r + rs->dofs_entsize);
	}

	return (0);
}

/*
 * The dof_hdr_t passed to dtrace_dof_slurp() should be a partially validated
 * header:  it should be at the front of a memory region that is at least
 * sizeof (dof_hdr_t) in size -- and then at least dof_hdr.dofh_loadsz in
 * size.  It need not be validated in any other way.
 */
static int
dtrace_dof_slurp(dof_hdr_t *dof, dtrace_vstate_t *vstate, cred_t *cr,
    dtrace_enabling_t **enabp, uint64_t ubase, uint64_t udaddr, int noprobes)
{
	uint64_t len = dof->dofh_loadsz, seclen;
	uintptr_t daddr = (uintptr_t)dof;
	dtrace_ecbdesc_t *ep;
	dtrace_enabling_t *enab;
	uint_t i;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(dof->dofh_loadsz >= sizeof (dof_hdr_t));

	/*
	 * Check the DOF header identification bytes.  In addition to checking
	 * valid settings, we also verify that unused bits/bytes are zeroed so
	 * we can use them later without fear of regressing existing binaries.
	 */
	if (bcmp(&dof->dofh_ident[DOF_ID_MAG0],
	    DOF_MAG_STRING, DOF_MAG_STRLEN) != 0) {
		dtrace_dof_error(dof, "DOF magic string mismatch");
		return (-1);
	}

	if (dof->dofh_ident[DOF_ID_MODEL] != DOF_MODEL_ILP32 &&
	    dof->dofh_ident[DOF_ID_MODEL] != DOF_MODEL_LP64) {
		dtrace_dof_error(dof, "DOF has invalid data model");
		return (-1);
	}

	if (dof->dofh_ident[DOF_ID_ENCODING] != DOF_ENCODE_NATIVE) {
		dtrace_dof_error(dof, "DOF encoding mismatch");
		return (-1);
	}

	if (dof->dofh_ident[DOF_ID_VERSION] != DOF_VERSION_1 &&
	    dof->dofh_ident[DOF_ID_VERSION] != DOF_VERSION_2) {
		dtrace_dof_error(dof, "DOF version mismatch");
		return (-1);
	}

	if (dof->dofh_ident[DOF_ID_DIFVERS] != DIF_VERSION_2) {
		dtrace_dof_error(dof, "DOF uses unsupported instruction set");
		return (-1);
	}

	if (dof->dofh_ident[DOF_ID_DIFIREG] > DIF_DIR_NREGS) {
		dtrace_dof_error(dof, "DOF uses too many integer registers");
		return (-1);
	}

	if (dof->dofh_ident[DOF_ID_DIFTREG] > DIF_DTR_NREGS) {
		dtrace_dof_error(dof, "DOF uses too many tuple registers");
		return (-1);
	}

	for (i = DOF_ID_PAD; i < DOF_ID_SIZE; i++) {
		if (dof->dofh_ident[i] != 0) {
			dtrace_dof_error(dof, "DOF has invalid ident byte set");
			return (-1);
		}
	}

	if (dof->dofh_flags & ~DOF_FL_VALID) {
		dtrace_dof_error(dof, "DOF has invalid flag bits set");
		return (-1);
	}

	if (dof->dofh_secsize == 0) {
		dtrace_dof_error(dof, "zero section header size");
		return (-1);
	}

	/*
	 * Check that the section headers don't exceed the amount of DOF
	 * data.  Note that we cast the section size and number of sections
	 * to uint64_t's to prevent possible overflow in the multiplication.
	 */
	seclen = (uint64_t)dof->dofh_secnum * (uint64_t)dof->dofh_secsize;

	if (dof->dofh_secoff > len || seclen > len ||
	    dof->dofh_secoff + seclen > len) {
		dtrace_dof_error(dof, "truncated section headers");
		return (-1);
	}

	if (!IS_P2ALIGNED(dof->dofh_secoff, sizeof (uint64_t))) {
		dtrace_dof_error(dof, "misaligned section headers");
		return (-1);
	}

	if (!IS_P2ALIGNED(dof->dofh_secsize, sizeof (uint64_t))) {
		dtrace_dof_error(dof, "misaligned section size");
		return (-1);
	}

	/*
	 * Take an initial pass through the section headers to be sure that
	 * the headers don't have stray offsets.  If the 'noprobes' flag is
	 * set, do not permit sections relating to providers, probes, or args.
	 */
	for (i = 0; i < dof->dofh_secnum; i++) {
		dof_sec_t *sec = (dof_sec_t *)(daddr +
		    (uintptr_t)dof->dofh_secoff + i * dof->dofh_secsize);

		if (noprobes) {
			switch (sec->dofs_type) {
			case DOF_SECT_PROVIDER:
			case DOF_SECT_PROBES:
			case DOF_SECT_PRARGS:
			case DOF_SECT_PROFFS:
				dtrace_dof_error(dof, "illegal sections "
				    "for enabling");
				return (-1);
			}
		}

		if (DOF_SEC_ISLOADABLE(sec->dofs_type) &&
		    !(sec->dofs_flags & DOF_SECF_LOAD)) {
			dtrace_dof_error(dof, "loadable section with load "
			    "flag unset");
			return (-1);
		}

		if (!(sec->dofs_flags & DOF_SECF_LOAD))
			continue; /* just ignore non-loadable sections */

		if (!ISP2(sec->dofs_align)) {
			dtrace_dof_error(dof, "bad section alignment");
			return (-1);
		}

		if (sec->dofs_offset & (sec->dofs_align - 1)) {
			dtrace_dof_error(dof, "misaligned section");
			return (-1);
		}

		if (sec->dofs_offset > len || sec->dofs_size > len ||
		    sec->dofs_offset + sec->dofs_size > len) {
			dtrace_dof_error(dof, "corrupt section header");
			return (-1);
		}

		if (sec->dofs_type == DOF_SECT_STRTAB && *((char *)daddr +
		    sec->dofs_offset + sec->dofs_size - 1) != '\0') {
			dtrace_dof_error(dof, "non-terminating string table");
			return (-1);
		}
	}

	/*
	 * Take a second pass through the sections and locate and perform any
	 * relocations that are present.  We do this after the first pass to
	 * be sure that all sections have had their headers validated.
	 */
	for (i = 0; i < dof->dofh_secnum; i++) {
		dof_sec_t *sec = (dof_sec_t *)(daddr +
		    (uintptr_t)dof->dofh_secoff + i * dof->dofh_secsize);

		if (!(sec->dofs_flags & DOF_SECF_LOAD))
			continue; /* skip sections that are not loadable */

		switch (sec->dofs_type) {
		case DOF_SECT_URELHDR:
			if (dtrace_dof_relocate(dof, sec, ubase, udaddr) != 0)
				return (-1);
			break;
		}
	}

	if ((enab = *enabp) == NULL)
		enab = *enabp = dtrace_enabling_create(vstate);

	for (i = 0; i < dof->dofh_secnum; i++) {
		dof_sec_t *sec = (dof_sec_t *)(daddr +
		    (uintptr_t)dof->dofh_secoff + i * dof->dofh_secsize);

		if (sec->dofs_type != DOF_SECT_ECBDESC)
			continue;

		if ((ep = dtrace_dof_ecbdesc(dof, sec, vstate, cr)) == NULL) {
			dtrace_enabling_destroy(enab);
			*enabp = NULL;
			return (-1);
		}

		dtrace_enabling_add(enab, ep);
	}

	return (0);
}

/*
 * Process DOF for any options.  This routine assumes that the DOF has been
 * at least processed by dtrace_dof_slurp().
 */
static int
dtrace_dof_options(dof_hdr_t *dof, dtrace_state_t *state)
{
	int i, rval;
	uint32_t entsize;
	size_t offs;
	dof_optdesc_t *desc;

	for (i = 0; i < dof->dofh_secnum; i++) {
		dof_sec_t *sec = (dof_sec_t *)((uintptr_t)dof +
		    (uintptr_t)dof->dofh_secoff + i * dof->dofh_secsize);

		if (sec->dofs_type != DOF_SECT_OPTDESC)
			continue;

		if (sec->dofs_align != sizeof (uint64_t)) {
			dtrace_dof_error(dof, "bad alignment in "
			    "option description");
			return (EINVAL);
		}

		if ((entsize = sec->dofs_entsize) == 0) {
			dtrace_dof_error(dof, "zeroed option entry size");
			return (EINVAL);
		}

		if (entsize < sizeof (dof_optdesc_t)) {
			dtrace_dof_error(dof, "bad option entry size");
			return (EINVAL);
		}

		for (offs = 0; offs < sec->dofs_size; offs += entsize) {
			desc = (dof_optdesc_t *)((uintptr_t)dof +
			    (uintptr_t)sec->dofs_offset + offs);

			if (desc->dofo_strtab != DOF_SECIDX_NONE) {
				dtrace_dof_error(dof, "non-zero option string");
				return (EINVAL);
			}

			if (desc->dofo_value == DTRACEOPT_UNSET) {
				dtrace_dof_error(dof, "unset option");
				return (EINVAL);
			}

			if ((rval = dtrace_state_option(state,
			    desc->dofo_option, desc->dofo_value)) != 0) {
				dtrace_dof_error(dof, "rejected option");
				return (rval);
			}
		}
	}

	return (0);
}

/*
 * DTrace Consumer State Functions
 */
static int
dtrace_dstate_init(dtrace_dstate_t *dstate, size_t size)
{
	size_t hashsize, maxper, min, chunksize = dstate->dtds_chunksize;
	void *base;
	uintptr_t limit;
	dtrace_dynvar_t *dvar, *next, *start;
	int i;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(dstate->dtds_base == NULL && dstate->dtds_percpu == NULL);

	bzero(dstate, sizeof (dtrace_dstate_t));

	if ((dstate->dtds_chunksize = chunksize) == 0)
		dstate->dtds_chunksize = DTRACE_DYNVAR_CHUNKSIZE;

	VERIFY(dstate->dtds_chunksize < LONG_MAX);

	if (size < (min = dstate->dtds_chunksize + sizeof (dtrace_dynhash_t)))
		size = min;

	if ((base = kmem_zalloc(size, KM_NOSLEEP | KM_NORMALPRI)) == NULL)
		return (ENOMEM);

	dstate->dtds_size = size;
	dstate->dtds_base = base;
	dstate->dtds_percpu = kmem_cache_alloc(dtrace_state_cache, KM_SLEEP);
	bzero(dstate->dtds_percpu, NCPU * sizeof (dtrace_dstate_percpu_t));

	hashsize = size / (dstate->dtds_chunksize + sizeof (dtrace_dynhash_t));

	if (hashsize != 1 && (hashsize & 1))
		hashsize--;

	dstate->dtds_hashsize = hashsize;
	dstate->dtds_hash = dstate->dtds_base;

	/*
	 * Set all of our hash buckets to point to the single sink, and (if
	 * it hasn't already been set), set the sink's hash value to be the
	 * sink sentinel value.  The sink is needed for dynamic variable
	 * lookups to know that they have iterated over an entire, valid hash
	 * chain.
	 */
	for (i = 0; i < hashsize; i++)
		dstate->dtds_hash[i].dtdh_chain = &dtrace_dynhash_sink;

	if (dtrace_dynhash_sink.dtdv_hashval != DTRACE_DYNHASH_SINK)
		dtrace_dynhash_sink.dtdv_hashval = DTRACE_DYNHASH_SINK;

	/*
	 * Determine number of active CPUs.  Divide free list evenly among
	 * active CPUs.
	 */
	start = (dtrace_dynvar_t *)
	    ((uintptr_t)base + hashsize * sizeof (dtrace_dynhash_t));
	limit = (uintptr_t)base + size;

	VERIFY((uintptr_t)start < limit);
	VERIFY((uintptr_t)start >= (uintptr_t)base);

	maxper = (limit - (uintptr_t)start) / NCPU;
	maxper = (maxper / dstate->dtds_chunksize) * dstate->dtds_chunksize;

#ifndef illumos
	CPU_FOREACH(i) {
#else
	for (i = 0; i < NCPU; i++) {
#endif
		dstate->dtds_percpu[i].dtdsc_free = dvar = start;

		/*
		 * If we don't even have enough chunks to make it once through
		 * NCPUs, we're just going to allocate everything to the first
		 * CPU.  And if we're on the last CPU, we're going to allocate
		 * whatever is left over.  In either case, we set the limit to
		 * be the limit of the dynamic variable space.
		 */
		if (maxper == 0 || i == NCPU - 1) {
			limit = (uintptr_t)base + size;
			start = NULL;
		} else {
			limit = (uintptr_t)start + maxper;
			start = (dtrace_dynvar_t *)limit;
		}

		VERIFY(limit <= (uintptr_t)base + size);

		for (;;) {
			next = (dtrace_dynvar_t *)((uintptr_t)dvar +
			    dstate->dtds_chunksize);

			if ((uintptr_t)next + dstate->dtds_chunksize >= limit)
				break;

			VERIFY((uintptr_t)dvar >= (uintptr_t)base &&
			    (uintptr_t)dvar <= (uintptr_t)base + size);
			dvar->dtdv_next = next;
			dvar = next;
		}

		if (maxper == 0)
			break;
	}

	return (0);
}

static void
dtrace_dstate_fini(dtrace_dstate_t *dstate)
{
	ASSERT(MUTEX_HELD(&cpu_lock));

	if (dstate->dtds_base == NULL)
		return;

	kmem_free(dstate->dtds_base, dstate->dtds_size);
	kmem_cache_free(dtrace_state_cache, dstate->dtds_percpu);
}

static void
dtrace_vstate_fini(dtrace_vstate_t *vstate)
{
	/*
	 * Logical XOR, where are you?
	 */
	ASSERT((vstate->dtvs_nglobals == 0) ^ (vstate->dtvs_globals != NULL));

	if (vstate->dtvs_nglobals > 0) {
		kmem_free(vstate->dtvs_globals, vstate->dtvs_nglobals *
		    sizeof (dtrace_statvar_t *));
	}

	if (vstate->dtvs_ntlocals > 0) {
		kmem_free(vstate->dtvs_tlocals, vstate->dtvs_ntlocals *
		    sizeof (dtrace_difv_t));
	}

	ASSERT((vstate->dtvs_nlocals == 0) ^ (vstate->dtvs_locals != NULL));

	if (vstate->dtvs_nlocals > 0) {
		kmem_free(vstate->dtvs_locals, vstate->dtvs_nlocals *
		    sizeof (dtrace_statvar_t *));
	}
}

#ifdef illumos
static void
dtrace_state_clean(dtrace_state_t *state)
{
	if (state->dts_activity == DTRACE_ACTIVITY_INACTIVE)
		return;

	dtrace_dynvar_clean(&state->dts_vstate.dtvs_dynvars);
	dtrace_speculation_clean(state);
}

static void
dtrace_state_deadman(dtrace_state_t *state)
{
	hrtime_t now;

	dtrace_sync();

	now = dtrace_gethrtime();

	if (state != dtrace_anon.dta_state &&
	    now - state->dts_laststatus >= dtrace_deadman_user)
		return;

	/*
	 * We must be sure that dts_alive never appears to be less than the
	 * value upon entry to dtrace_state_deadman(), and because we lack a
	 * dtrace_cas64(), we cannot store to it atomically.  We thus instead
	 * store INT64_MAX to it, followed by a memory barrier, followed by
	 * the new value.  This assures that dts_alive never appears to be
	 * less than its true value, regardless of the order in which the
	 * stores to the underlying storage are issued.
	 */
	state->dts_alive = INT64_MAX;
	dtrace_membar_producer();
	state->dts_alive = now;
}
#else	/* !illumos */
static void
dtrace_state_clean(void *arg)
{
	dtrace_state_t *state = arg;
	dtrace_optval_t *opt = state->dts_options;

	if (state->dts_activity == DTRACE_ACTIVITY_INACTIVE)
		return;

	dtrace_dynvar_clean(&state->dts_vstate.dtvs_dynvars);
	dtrace_speculation_clean(state);

	callout_reset(&state->dts_cleaner, hz * opt[DTRACEOPT_CLEANRATE] / NANOSEC,
	    dtrace_state_clean, state);
}

static void
dtrace_state_deadman(void *arg)
{
	dtrace_state_t *state = arg;
	hrtime_t now;

	dtrace_sync();

	dtrace_debug_output();

	now = dtrace_gethrtime();

	if (state != dtrace_anon.dta_state &&
	    now - state->dts_laststatus >= dtrace_deadman_user)
		return;

	/*
	 * We must be sure that dts_alive never appears to be less than the
	 * value upon entry to dtrace_state_deadman(), and because we lack a
	 * dtrace_cas64(), we cannot store to it atomically.  We thus instead
	 * store INT64_MAX to it, followed by a memory barrier, followed by
	 * the new value.  This assures that dts_alive never appears to be
	 * less than its true value, regardless of the order in which the
	 * stores to the underlying storage are issued.
	 */
	state->dts_alive = INT64_MAX;
	dtrace_membar_producer();
	state->dts_alive = now;

	callout_reset(&state->dts_deadman, hz * dtrace_deadman_interval / NANOSEC,
	    dtrace_state_deadman, state);
}
#endif	/* illumos */

static dtrace_state_t *
#ifdef illumos
dtrace_state_create(dev_t *devp, cred_t *cr)
#else
dtrace_state_create(struct cdev *dev, struct ucred *cred __unused)
#endif
{
#ifdef illumos
	minor_t minor;
	major_t major;
#else
	cred_t *cr = NULL;
	int m = 0;
#endif
	char c[30];
	dtrace_state_t *state;
	dtrace_optval_t *opt;
	int bufsize = NCPU * sizeof (dtrace_buffer_t), i;
	int cpu_it;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(MUTEX_HELD(&cpu_lock));

#ifdef illumos
	minor = (minor_t)(uintptr_t)vmem_alloc(dtrace_minor, 1,
	    VM_BESTFIT | VM_SLEEP);

	if (ddi_soft_state_zalloc(dtrace_softstate, minor) != DDI_SUCCESS) {
		vmem_free(dtrace_minor, (void *)(uintptr_t)minor, 1);
		return (NULL);
	}

	state = ddi_get_soft_state(dtrace_softstate, minor);
#else
	if (dev != NULL) {
		cr = dev->si_cred;
		m = dev2unit(dev);
	}

	/* Allocate memory for the state. */
	state = kmem_zalloc(sizeof(dtrace_state_t), KM_SLEEP);
#endif

	state->dts_epid = DTRACE_EPIDNONE + 1;

	(void) snprintf(c, sizeof (c), "dtrace_aggid_%d", m);
#ifdef illumos
	state->dts_aggid_arena = vmem_create(c, (void *)1, UINT32_MAX, 1,
	    NULL, NULL, NULL, 0, VM_SLEEP | VMC_IDENTIFIER);

	if (devp != NULL) {
		major = getemajor(*devp);
	} else {
		major = ddi_driver_major(dtrace_devi);
	}

	state->dts_dev = makedevice(major, minor);

	if (devp != NULL)
		*devp = state->dts_dev;
#else
	state->dts_aggid_arena = new_unrhdr(1, INT_MAX, &dtrace_unr_mtx);
	state->dts_dev = dev;
#endif

	/*
	 * We allocate NCPU buffers.  On the one hand, this can be quite
	 * a bit of memory per instance (nearly 36K on a Starcat).  On the
	 * other hand, it saves an additional memory reference in the probe
	 * path.
	 */
	state->dts_buffer = kmem_zalloc(bufsize, KM_SLEEP);
	state->dts_aggbuffer = kmem_zalloc(bufsize, KM_SLEEP);

	/*
         * Allocate and initialise the per-process per-CPU random state.
	 * SI_SUB_RANDOM < SI_SUB_DTRACE_ANON therefore entropy device is
         * assumed to be seeded at this point (if from Fortuna seed file).
	 */
	(void) read_random(&state->dts_rstate[0], 2 * sizeof(uint64_t));
	for (cpu_it = 1; cpu_it < NCPU; cpu_it++) {
		/*
		 * Each CPU is assigned a 2^64 period, non-overlapping
		 * subsequence.
		 */
		dtrace_xoroshiro128_plus_jump(state->dts_rstate[cpu_it-1],
		    state->dts_rstate[cpu_it]); 
	}

#ifdef illumos
	state->dts_cleaner = CYCLIC_NONE;
	state->dts_deadman = CYCLIC_NONE;
#else
	callout_init(&state->dts_cleaner, 1);
	callout_init(&state->dts_deadman, 1);
#endif
	state->dts_vstate.dtvs_state = state;

	for (i = 0; i < DTRACEOPT_MAX; i++)
		state->dts_options[i] = DTRACEOPT_UNSET;

	/*
	 * Set the default options.
	 */
	opt = state->dts_options;
	opt[DTRACEOPT_BUFPOLICY] = DTRACEOPT_BUFPOLICY_SWITCH;
	opt[DTRACEOPT_BUFRESIZE] = DTRACEOPT_BUFRESIZE_AUTO;
	opt[DTRACEOPT_NSPEC] = dtrace_nspec_default;
	opt[DTRACEOPT_SPECSIZE] = dtrace_specsize_default;
	opt[DTRACEOPT_CPU] = (dtrace_optval_t)DTRACE_CPUALL;
	opt[DTRACEOPT_STRSIZE] = dtrace_strsize_default;
	opt[DTRACEOPT_STACKFRAMES] = dtrace_stackframes_default;
	opt[DTRACEOPT_USTACKFRAMES] = dtrace_ustackframes_default;
	opt[DTRACEOPT_CLEANRATE] = dtrace_cleanrate_default;
	opt[DTRACEOPT_AGGRATE] = dtrace_aggrate_default;
	opt[DTRACEOPT_SWITCHRATE] = dtrace_switchrate_default;
	opt[DTRACEOPT_STATUSRATE] = dtrace_statusrate_default;
	opt[DTRACEOPT_JSTACKFRAMES] = dtrace_jstackframes_default;
	opt[DTRACEOPT_JSTACKSTRSIZE] = dtrace_jstackstrsize_default;

	state->dts_activity = DTRACE_ACTIVITY_INACTIVE;

	/*
	 * Depending on the user credentials, we set flag bits which alter probe
	 * visibility or the amount of destructiveness allowed.  In the case of
	 * actual anonymous tracing, or the possession of all privileges, all of
	 * the normal checks are bypassed.
	 */
	if (cr == NULL || PRIV_POLICY_ONLY(cr, PRIV_ALL, B_FALSE)) {
		state->dts_cred.dcr_visible = DTRACE_CRV_ALL;
		state->dts_cred.dcr_action = DTRACE_CRA_ALL;
	} else {
		/*
		 * Set up the credentials for this instantiation.  We take a
		 * hold on the credential to prevent it from disappearing on
		 * us; this in turn prevents the zone_t referenced by this
		 * credential from disappearing.  This means that we can
		 * examine the credential and the zone from probe context.
		 */
		crhold(cr);
		state->dts_cred.dcr_cred = cr;

		/*
		 * CRA_PROC means "we have *some* privilege for dtrace" and
		 * unlocks the use of variables like pid, zonename, etc.
		 */
		if (PRIV_POLICY_ONLY(cr, PRIV_DTRACE_USER, B_FALSE) ||
		    PRIV_POLICY_ONLY(cr, PRIV_DTRACE_PROC, B_FALSE)) {
			state->dts_cred.dcr_action |= DTRACE_CRA_PROC;
		}

		/*
		 * dtrace_user allows use of syscall and profile providers.
		 * If the user also has proc_owner and/or proc_zone, we
		 * extend the scope to include additional visibility and
		 * destructive power.
		 */
		if (PRIV_POLICY_ONLY(cr, PRIV_DTRACE_USER, B_FALSE)) {
			if (PRIV_POLICY_ONLY(cr, PRIV_PROC_OWNER, B_FALSE)) {
				state->dts_cred.dcr_visible |=
				    DTRACE_CRV_ALLPROC;

				state->dts_cred.dcr_action |=
				    DTRACE_CRA_PROC_DESTRUCTIVE_ALLUSER;
			}

			if (PRIV_POLICY_ONLY(cr, PRIV_PROC_ZONE, B_FALSE)) {
				state->dts_cred.dcr_visible |=
				    DTRACE_CRV_ALLZONE;

				state->dts_cred.dcr_action |=
				    DTRACE_CRA_PROC_DESTRUCTIVE_ALLZONE;
			}

			/*
			 * If we have all privs in whatever zone this is,
			 * we can do destructive things to processes which
			 * have altered credentials.
			 */
#ifdef illumos
			if (priv_isequalset(priv_getset(cr, PRIV_EFFECTIVE),
			    cr->cr_zone->zone_privset)) {
				state->dts_cred.dcr_action |=
				    DTRACE_CRA_PROC_DESTRUCTIVE_CREDCHG;
			}
#endif
		}

		/*
		 * Holding the dtrace_kernel privilege also implies that
		 * the user has the dtrace_user privilege from a visibility
		 * perspective.  But without further privileges, some
		 * destructive actions are not available.
		 */
		if (PRIV_POLICY_ONLY(cr, PRIV_DTRACE_KERNEL, B_FALSE)) {
			/*
			 * Make all probes in all zones visible.  However,
			 * this doesn't mean that all actions become available
			 * to all zones.
			 */
			state->dts_cred.dcr_visible |= DTRACE_CRV_KERNEL |
			    DTRACE_CRV_ALLPROC | DTRACE_CRV_ALLZONE;

			state->dts_cred.dcr_action |= DTRACE_CRA_KERNEL |
			    DTRACE_CRA_PROC;
			/*
			 * Holding proc_owner means that destructive actions
			 * for *this* zone are allowed.
			 */
			if (PRIV_POLICY_ONLY(cr, PRIV_PROC_OWNER, B_FALSE))
				state->dts_cred.dcr_action |=
				    DTRACE_CRA_PROC_DESTRUCTIVE_ALLUSER;

			/*
			 * Holding proc_zone means that destructive actions
			 * for this user/group ID in all zones is allowed.
			 */
			if (PRIV_POLICY_ONLY(cr, PRIV_PROC_ZONE, B_FALSE))
				state->dts_cred.dcr_action |=
				    DTRACE_CRA_PROC_DESTRUCTIVE_ALLZONE;

#ifdef illumos
			/*
			 * If we have all privs in whatever zone this is,
			 * we can do destructive things to processes which
			 * have altered credentials.
			 */
			if (priv_isequalset(priv_getset(cr, PRIV_EFFECTIVE),
			    cr->cr_zone->zone_privset)) {
				state->dts_cred.dcr_action |=
				    DTRACE_CRA_PROC_DESTRUCTIVE_CREDCHG;
			}
#endif
		}

		/*
		 * Holding the dtrace_proc privilege gives control over fasttrap
		 * and pid providers.  We need to grant wider destructive
		 * privileges in the event that the user has proc_owner and/or
		 * proc_zone.
		 */
		if (PRIV_POLICY_ONLY(cr, PRIV_DTRACE_PROC, B_FALSE)) {
			if (PRIV_POLICY_ONLY(cr, PRIV_PROC_OWNER, B_FALSE))
				state->dts_cred.dcr_action |=
				    DTRACE_CRA_PROC_DESTRUCTIVE_ALLUSER;

			if (PRIV_POLICY_ONLY(cr, PRIV_PROC_ZONE, B_FALSE))
				state->dts_cred.dcr_action |=
				    DTRACE_CRA_PROC_DESTRUCTIVE_ALLZONE;
		}
	}

	return (state);
}

static int
dtrace_state_buffer(dtrace_state_t *state, dtrace_buffer_t *buf, int which)
{
	dtrace_optval_t *opt = state->dts_options, size;
	processorid_t cpu = 0;;
	int flags = 0, rval, factor, divisor = 1;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(MUTEX_HELD(&cpu_lock));
	ASSERT(which < DTRACEOPT_MAX);
	ASSERT(state->dts_activity == DTRACE_ACTIVITY_INACTIVE ||
	    (state == dtrace_anon.dta_state &&
	    state->dts_activity == DTRACE_ACTIVITY_ACTIVE));

	if (opt[which] == DTRACEOPT_UNSET || opt[which] == 0)
		return (0);

	if (opt[DTRACEOPT_CPU] != DTRACEOPT_UNSET)
		cpu = opt[DTRACEOPT_CPU];

	if (which == DTRACEOPT_SPECSIZE)
		flags |= DTRACEBUF_NOSWITCH;

	if (which == DTRACEOPT_BUFSIZE) {
		if (opt[DTRACEOPT_BUFPOLICY] == DTRACEOPT_BUFPOLICY_RING)
			flags |= DTRACEBUF_RING;

		if (opt[DTRACEOPT_BUFPOLICY] == DTRACEOPT_BUFPOLICY_FILL)
			flags |= DTRACEBUF_FILL;

		if (state != dtrace_anon.dta_state ||
		    state->dts_activity != DTRACE_ACTIVITY_ACTIVE)
			flags |= DTRACEBUF_INACTIVE;
	}

	for (size = opt[which]; size >= sizeof (uint64_t); size /= divisor) {
		/*
		 * The size must be 8-byte aligned.  If the size is not 8-byte
		 * aligned, drop it down by the difference.
		 */
		if (size & (sizeof (uint64_t) - 1))
			size -= size & (sizeof (uint64_t) - 1);

		if (size < state->dts_reserve) {
			/*
			 * Buffers always must be large enough to accommodate
			 * their prereserved space.  We return E2BIG instead
			 * of ENOMEM in this case to allow for user-level
			 * software to differentiate the cases.
			 */
			return (E2BIG);
		}

		rval = dtrace_buffer_alloc(buf, size, flags, cpu, &factor);

		if (rval != ENOMEM) {
			opt[which] = size;
			return (rval);
		}

		if (opt[DTRACEOPT_BUFRESIZE] == DTRACEOPT_BUFRESIZE_MANUAL)
			return (rval);

		for (divisor = 2; divisor < factor; divisor <<= 1)
			continue;
	}

	return (ENOMEM);
}

static int
dtrace_state_buffers(dtrace_state_t *state)
{
	dtrace_speculation_t *spec = state->dts_speculations;
	int rval, i;

	if ((rval = dtrace_state_buffer(state, state->dts_buffer,
	    DTRACEOPT_BUFSIZE)) != 0)
		return (rval);

	if ((rval = dtrace_state_buffer(state, state->dts_aggbuffer,
	    DTRACEOPT_AGGSIZE)) != 0)
		return (rval);

	for (i = 0; i < state->dts_nspeculations; i++) {
		if ((rval = dtrace_state_buffer(state,
		    spec[i].dtsp_buffer, DTRACEOPT_SPECSIZE)) != 0)
			return (rval);
	}

	return (0);
}

static void
dtrace_state_prereserve(dtrace_state_t *state)
{
	dtrace_ecb_t *ecb;
	dtrace_probe_t *probe;

	state->dts_reserve = 0;

	if (state->dts_options[DTRACEOPT_BUFPOLICY] != DTRACEOPT_BUFPOLICY_FILL)
		return;

	/*
	 * If our buffer policy is a "fill" buffer policy, we need to set the
	 * prereserved space to be the space required by the END probes.
	 */
	probe = dtrace_probes[dtrace_probeid_end - 1];
	ASSERT(probe != NULL);

	for (ecb = probe->dtpr_ecb; ecb != NULL; ecb = ecb->dte_next) {
		if (ecb->dte_state != state)
			continue;

		state->dts_reserve += ecb->dte_needed + ecb->dte_alignment;
	}
}

static int
dtrace_state_go(dtrace_state_t *state, processorid_t *cpu)
{
	dtrace_optval_t *opt = state->dts_options, sz, nspec;
	dtrace_speculation_t *spec;
	dtrace_buffer_t *buf;
#ifdef illumos
	cyc_handler_t hdlr;
	cyc_time_t when;
#endif
	int rval = 0, i, bufsize = NCPU * sizeof (dtrace_buffer_t);
	dtrace_icookie_t cookie;

	mutex_enter(&cpu_lock);
	mutex_enter(&dtrace_lock);

	if (state->dts_activity != DTRACE_ACTIVITY_INACTIVE) {
		rval = EBUSY;
		goto out;
	}

	/*
	 * Before we can perform any checks, we must prime all of the
	 * retained enablings that correspond to this state.
	 */
	dtrace_enabling_prime(state);

	if (state->dts_destructive && !state->dts_cred.dcr_destructive) {
		rval = EACCES;
		goto out;
	}

	dtrace_state_prereserve(state);

	/*
	 * Now we want to do is try to allocate our speculations.
	 * We do not automatically resize the number of speculations; if
	 * this fails, we will fail the operation.
	 */
	nspec = opt[DTRACEOPT_NSPEC];
	ASSERT(nspec != DTRACEOPT_UNSET);

	if (nspec > INT_MAX) {
		rval = ENOMEM;
		goto out;
	}

	spec = kmem_zalloc(nspec * sizeof (dtrace_speculation_t),
	    KM_NOSLEEP | KM_NORMALPRI);

	if (spec == NULL) {
		rval = ENOMEM;
		goto out;
	}

	state->dts_speculations = spec;
	state->dts_nspeculations = (int)nspec;

	for (i = 0; i < nspec; i++) {
		if ((buf = kmem_zalloc(bufsize,
		    KM_NOSLEEP | KM_NORMALPRI)) == NULL) {
			rval = ENOMEM;
			goto err;
		}

		spec[i].dtsp_buffer = buf;
	}

	if (opt[DTRACEOPT_GRABANON] != DTRACEOPT_UNSET) {
		if (dtrace_anon.dta_state == NULL) {
			rval = ENOENT;
			goto out;
		}

		if (state->dts_necbs != 0) {
			rval = EALREADY;
			goto out;
		}

		state->dts_anon = dtrace_anon_grab();
		ASSERT(state->dts_anon != NULL);
		state = state->dts_anon;

		/*
		 * We want "grabanon" to be set in the grabbed state, so we'll
		 * copy that option value from the grabbing state into the
		 * grabbed state.
		 */
		state->dts_options[DTRACEOPT_GRABANON] =
		    opt[DTRACEOPT_GRABANON];

		*cpu = dtrace_anon.dta_beganon;

		/*
		 * If the anonymous state is active (as it almost certainly
		 * is if the anonymous enabling ultimately matched anything),
		 * we don't allow any further option processing -- but we
		 * don't return failure.
		 */
		if (state->dts_activity != DTRACE_ACTIVITY_INACTIVE)
			goto out;
	}

	if (opt[DTRACEOPT_AGGSIZE] != DTRACEOPT_UNSET &&
	    opt[DTRACEOPT_AGGSIZE] != 0) {
		if (state->dts_aggregations == NULL) {
			/*
			 * We're not going to create an aggregation buffer
			 * because we don't have any ECBs that contain
			 * aggregations -- set this option to 0.
			 */
			opt[DTRACEOPT_AGGSIZE] = 0;
		} else {
			/*
			 * If we have an aggregation buffer, we must also have
			 * a buffer to use as scratch.
			 */
			if (opt[DTRACEOPT_BUFSIZE] == DTRACEOPT_UNSET ||
			    opt[DTRACEOPT_BUFSIZE] < state->dts_needed) {
				opt[DTRACEOPT_BUFSIZE] = state->dts_needed;
			}
		}
	}

	if (opt[DTRACEOPT_SPECSIZE] != DTRACEOPT_UNSET &&
	    opt[DTRACEOPT_SPECSIZE] != 0) {
		if (!state->dts_speculates) {
			/*
			 * We're not going to create speculation buffers
			 * because we don't have any ECBs that actually
			 * speculate -- set the speculation size to 0.
			 */
			opt[DTRACEOPT_SPECSIZE] = 0;
		}
	}

	/*
	 * The bare minimum size for any buffer that we're actually going to
	 * do anything to is sizeof (uint64_t).
	 */
	sz = sizeof (uint64_t);

	if ((state->dts_needed != 0 && opt[DTRACEOPT_BUFSIZE] < sz) ||
	    (state->dts_speculates && opt[DTRACEOPT_SPECSIZE] < sz) ||
	    (state->dts_aggregations != NULL && opt[DTRACEOPT_AGGSIZE] < sz)) {
		/*
		 * A buffer size has been explicitly set to 0 (or to a size
		 * that will be adjusted to 0) and we need the space -- we
		 * need to return failure.  We return ENOSPC to differentiate
		 * it from failing to allocate a buffer due to failure to meet
		 * the reserve (for which we return E2BIG).
		 */
		rval = ENOSPC;
		goto out;
	}

	if ((rval = dtrace_state_buffers(state)) != 0)
		goto err;

	if ((sz = opt[DTRACEOPT_DYNVARSIZE]) == DTRACEOPT_UNSET)
		sz = dtrace_dstate_defsize;

	do {
		rval = dtrace_dstate_init(&state->dts_vstate.dtvs_dynvars, sz);

		if (rval == 0)
			break;

		if (opt[DTRACEOPT_BUFRESIZE] == DTRACEOPT_BUFRESIZE_MANUAL)
			goto err;
	} while (sz >>= 1);

	opt[DTRACEOPT_DYNVARSIZE] = sz;

	if (rval != 0)
		goto err;

	if (opt[DTRACEOPT_STATUSRATE] > dtrace_statusrate_max)
		opt[DTRACEOPT_STATUSRATE] = dtrace_statusrate_max;

	if (opt[DTRACEOPT_CLEANRATE] == 0)
		opt[DTRACEOPT_CLEANRATE] = dtrace_cleanrate_max;

	if (opt[DTRACEOPT_CLEANRATE] < dtrace_cleanrate_min)
		opt[DTRACEOPT_CLEANRATE] = dtrace_cleanrate_min;

	if (opt[DTRACEOPT_CLEANRATE] > dtrace_cleanrate_max)
		opt[DTRACEOPT_CLEANRATE] = dtrace_cleanrate_max;

	state->dts_alive = state->dts_laststatus = dtrace_gethrtime();
#ifdef illumos
	hdlr.cyh_func = (cyc_func_t)dtrace_state_clean;
	hdlr.cyh_arg = state;
	hdlr.cyh_level = CY_LOW_LEVEL;

	when.cyt_when = 0;
	when.cyt_interval = opt[DTRACEOPT_CLEANRATE];

	state->dts_cleaner = cyclic_add(&hdlr, &when);

	hdlr.cyh_func = (cyc_func_t)dtrace_state_deadman;
	hdlr.cyh_arg = state;
	hdlr.cyh_level = CY_LOW_LEVEL;

	when.cyt_when = 0;
	when.cyt_interval = dtrace_deadman_interval;

	state->dts_deadman = cyclic_add(&hdlr, &when);
#else
	callout_reset(&state->dts_cleaner, hz * opt[DTRACEOPT_CLEANRATE] / NANOSEC,
	    dtrace_state_clean, state);
	callout_reset(&state->dts_deadman, hz * dtrace_deadman_interval / NANOSEC,
	    dtrace_state_deadman, state);
#endif

	state->dts_activity = DTRACE_ACTIVITY_WARMUP;

#ifdef illumos
	if (state->dts_getf != 0 &&
	    !(state->dts_cred.dcr_visible & DTRACE_CRV_KERNEL)) {
		/*
		 * We don't have kernel privs but we have at least one call
		 * to getf(); we need to bump our zone's count, and (if
		 * this is the first enabling to have an unprivileged call
		 * to getf()) we need to hook into closef().
		 */
		state->dts_cred.dcr_cred->cr_zone->zone_dtrace_getf++;

		if (dtrace_getf++ == 0) {
			ASSERT(dtrace_closef == NULL);
			dtrace_closef = dtrace_getf_barrier;
		}
	}
#endif

	/*
	 * Now it's time to actually fire the BEGIN probe.  We need to disable
	 * interrupts here both to record the CPU on which we fired the BEGIN
	 * probe (the data from this CPU will be processed first at user
	 * level) and to manually activate the buffer for this CPU.
	 */
	cookie = dtrace_interrupt_disable();
	*cpu = curcpu;
	ASSERT(state->dts_buffer[*cpu].dtb_flags & DTRACEBUF_INACTIVE);
	state->dts_buffer[*cpu].dtb_flags &= ~DTRACEBUF_INACTIVE;

	dtrace_probe(dtrace_probeid_begin,
	    (uint64_t)(uintptr_t)state, 0, 0, 0, 0);
	dtrace_interrupt_enable(cookie);
	/*
	 * We may have had an exit action from a BEGIN probe; only change our
	 * state to ACTIVE if we're still in WARMUP.
	 */
	ASSERT(state->dts_activity == DTRACE_ACTIVITY_WARMUP ||
	    state->dts_activity == DTRACE_ACTIVITY_DRAINING);

	if (state->dts_activity == DTRACE_ACTIVITY_WARMUP)
		state->dts_activity = DTRACE_ACTIVITY_ACTIVE;

#ifdef __FreeBSD__
	/*
	 * We enable anonymous tracing before APs are started, so we must
	 * activate buffers using the current CPU.
	 */
	if (state == dtrace_anon.dta_state)
		for (int i = 0; i < NCPU; i++)
			dtrace_buffer_activate_cpu(state, i);
	else
		dtrace_xcall(DTRACE_CPUALL,
		    (dtrace_xcall_t)dtrace_buffer_activate, state);
#else
	/*
	 * Regardless of whether or not now we're in ACTIVE or DRAINING, we
	 * want each CPU to transition its principal buffer out of the
	 * INACTIVE state.  Doing this assures that no CPU will suddenly begin
	 * processing an ECB halfway down a probe's ECB chain; all CPUs will
	 * atomically transition from processing none of a state's ECBs to
	 * processing all of them.
	 */
	dtrace_xcall(DTRACE_CPUALL,
	    (dtrace_xcall_t)dtrace_buffer_activate, state);
#endif
	goto out;

err:
	dtrace_buffer_free(state->dts_buffer);
	dtrace_buffer_free(state->dts_aggbuffer);

	if ((nspec = state->dts_nspeculations) == 0) {
		ASSERT(state->dts_speculations == NULL);
		goto out;
	}

	spec = state->dts_speculations;
	ASSERT(spec != NULL);

	for (i = 0; i < state->dts_nspeculations; i++) {
		if ((buf = spec[i].dtsp_buffer) == NULL)
			break;

		dtrace_buffer_free(buf);
		kmem_free(buf, bufsize);
	}

	kmem_free(spec, nspec * sizeof (dtrace_speculation_t));
	state->dts_nspeculations = 0;
	state->dts_speculations = NULL;

out:
	mutex_exit(&dtrace_lock);
	mutex_exit(&cpu_lock);

	return (rval);
}

static int
dtrace_state_stop(dtrace_state_t *state, processorid_t *cpu)
{
	dtrace_icookie_t cookie;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	if (state->dts_activity != DTRACE_ACTIVITY_ACTIVE &&
	    state->dts_activity != DTRACE_ACTIVITY_DRAINING)
		return (EINVAL);

	/*
	 * We'll set the activity to DTRACE_ACTIVITY_DRAINING, and issue a sync
	 * to be sure that every CPU has seen it.  See below for the details
	 * on why this is done.
	 */
	state->dts_activity = DTRACE_ACTIVITY_DRAINING;
	dtrace_sync();

	/*
	 * By this point, it is impossible for any CPU to be still processing
	 * with DTRACE_ACTIVITY_ACTIVE.  We can thus set our activity to
	 * DTRACE_ACTIVITY_COOLDOWN and know that we're not racing with any
	 * other CPU in dtrace_buffer_reserve().  This allows dtrace_probe()
	 * and callees to know that the activity is DTRACE_ACTIVITY_COOLDOWN
	 * iff we're in the END probe.
	 */
	state->dts_activity = DTRACE_ACTIVITY_COOLDOWN;
	dtrace_sync();
	ASSERT(state->dts_activity == DTRACE_ACTIVITY_COOLDOWN);

	/*
	 * Finally, we can release the reserve and call the END probe.  We
	 * disable interrupts across calling the END probe to allow us to
	 * return the CPU on which we actually called the END probe.  This
	 * allows user-land to be sure that this CPU's principal buffer is
	 * processed last.
	 */
	state->dts_reserve = 0;

	cookie = dtrace_interrupt_disable();
	*cpu = curcpu;
	dtrace_probe(dtrace_probeid_end,
	    (uint64_t)(uintptr_t)state, 0, 0, 0, 0);
	dtrace_interrupt_enable(cookie);

	state->dts_activity = DTRACE_ACTIVITY_STOPPED;
	dtrace_sync();

#ifdef illumos
	if (state->dts_getf != 0 &&
	    !(state->dts_cred.dcr_visible & DTRACE_CRV_KERNEL)) {
		/*
		 * We don't have kernel privs but we have at least one call
		 * to getf(); we need to lower our zone's count, and (if
		 * this is the last enabling to have an unprivileged call
		 * to getf()) we need to clear the closef() hook.
		 */
		ASSERT(state->dts_cred.dcr_cred->cr_zone->zone_dtrace_getf > 0);
		ASSERT(dtrace_closef == dtrace_getf_barrier);
		ASSERT(dtrace_getf > 0);

		state->dts_cred.dcr_cred->cr_zone->zone_dtrace_getf--;

		if (--dtrace_getf == 0)
			dtrace_closef = NULL;
	}
#endif

	return (0);
}

static int
dtrace_state_option(dtrace_state_t *state, dtrace_optid_t option,
    dtrace_optval_t val)
{
	ASSERT(MUTEX_HELD(&dtrace_lock));

	if (state->dts_activity != DTRACE_ACTIVITY_INACTIVE)
		return (EBUSY);

	if (option >= DTRACEOPT_MAX)
		return (EINVAL);

	if (option != DTRACEOPT_CPU && val < 0)
		return (EINVAL);

	switch (option) {
	case DTRACEOPT_DESTRUCTIVE:
		if (dtrace_destructive_disallow)
			return (EACCES);

		state->dts_cred.dcr_destructive = 1;
		break;

	case DTRACEOPT_BUFSIZE:
	case DTRACEOPT_DYNVARSIZE:
	case DTRACEOPT_AGGSIZE:
	case DTRACEOPT_SPECSIZE:
	case DTRACEOPT_STRSIZE:
		if (val < 0)
			return (EINVAL);

		if (val >= LONG_MAX) {
			/*
			 * If this is an otherwise negative value, set it to
			 * the highest multiple of 128m less than LONG_MAX.
			 * Technically, we're adjusting the size without
			 * regard to the buffer resizing policy, but in fact,
			 * this has no effect -- if we set the buffer size to
			 * ~LONG_MAX and the buffer policy is ultimately set to
			 * be "manual", the buffer allocation is guaranteed to
			 * fail, if only because the allocation requires two
			 * buffers.  (We set the the size to the highest
			 * multiple of 128m because it ensures that the size
			 * will remain a multiple of a megabyte when
			 * repeatedly halved -- all the way down to 15m.)
			 */
			val = LONG_MAX - (1 << 27) + 1;
		}
	}

	state->dts_options[option] = val;

	return (0);
}

static void
dtrace_state_destroy(dtrace_state_t *state)
{
	dtrace_ecb_t *ecb;
	dtrace_vstate_t *vstate = &state->dts_vstate;
#ifdef illumos
	minor_t minor = getminor(state->dts_dev);
#endif
	int i, bufsize = NCPU * sizeof (dtrace_buffer_t);
	dtrace_speculation_t *spec = state->dts_speculations;
	int nspec = state->dts_nspeculations;
	uint32_t match;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(MUTEX_HELD(&cpu_lock));

	/*
	 * First, retract any retained enablings for this state.
	 */
	dtrace_enabling_retract(state);
	ASSERT(state->dts_nretained == 0);

	if (state->dts_activity == DTRACE_ACTIVITY_ACTIVE ||
	    state->dts_activity == DTRACE_ACTIVITY_DRAINING) {
		/*
		 * We have managed to come into dtrace_state_destroy() on a
		 * hot enabling -- almost certainly because of a disorderly
		 * shutdown of a consumer.  (That is, a consumer that is
		 * exiting without having called dtrace_stop().) In this case,
		 * we're going to set our activity to be KILLED, and then
		 * issue a sync to be sure that everyone is out of probe
		 * context before we start blowing away ECBs.
		 */
		state->dts_activity = DTRACE_ACTIVITY_KILLED;
		dtrace_sync();
	}

	/*
	 * Release the credential hold we took in dtrace_state_create().
	 */
	if (state->dts_cred.dcr_cred != NULL)
		crfree(state->dts_cred.dcr_cred);

	/*
	 * Now we can safely disable and destroy any enabled probes.  Because
	 * any DTRACE_PRIV_KERNEL probes may actually be slowing our progress
	 * (especially if they're all enabled), we take two passes through the
	 * ECBs:  in the first, we disable just DTRACE_PRIV_KERNEL probes, and
	 * in the second we disable whatever is left over.
	 */
	for (match = DTRACE_PRIV_KERNEL; ; match = 0) {
		for (i = 0; i < state->dts_necbs; i++) {
			if ((ecb = state->dts_ecbs[i]) == NULL)
				continue;

			if (match && ecb->dte_probe != NULL) {
				dtrace_probe_t *probe = ecb->dte_probe;
				dtrace_provider_t *prov = probe->dtpr_provider;

				if (!(prov->dtpv_priv.dtpp_flags & match))
					continue;
			}

			dtrace_ecb_disable(ecb);
			dtrace_ecb_destroy(ecb);
		}

		if (!match)
			break;
	}

	/*
	 * Before we free the buffers, perform one more sync to assure that
	 * every CPU is out of probe context.
	 */
	dtrace_sync();

	dtrace_buffer_free(state->dts_buffer);
	dtrace_buffer_free(state->dts_aggbuffer);

	for (i = 0; i < nspec; i++)
		dtrace_buffer_free(spec[i].dtsp_buffer);

#ifdef illumos
	if (state->dts_cleaner != CYCLIC_NONE)
		cyclic_remove(state->dts_cleaner);

	if (state->dts_deadman != CYCLIC_NONE)
		cyclic_remove(state->dts_deadman);
#else
	callout_stop(&state->dts_cleaner);
	callout_drain(&state->dts_cleaner);
	callout_stop(&state->dts_deadman);
	callout_drain(&state->dts_deadman);
#endif

	dtrace_dstate_fini(&vstate->dtvs_dynvars);
	dtrace_vstate_fini(vstate);
	if (state->dts_ecbs != NULL)
		kmem_free(state->dts_ecbs, state->dts_necbs * sizeof (dtrace_ecb_t *));

	if (state->dts_aggregations != NULL) {
#ifdef DEBUG
		for (i = 0; i < state->dts_naggregations; i++)
			ASSERT(state->dts_aggregations[i] == NULL);
#endif
		ASSERT(state->dts_naggregations > 0);
		kmem_free(state->dts_aggregations,
		    state->dts_naggregations * sizeof (dtrace_aggregation_t *));
	}

	kmem_free(state->dts_buffer, bufsize);
	kmem_free(state->dts_aggbuffer, bufsize);

	for (i = 0; i < nspec; i++)
		kmem_free(spec[i].dtsp_buffer, bufsize);

	if (spec != NULL)
		kmem_free(spec, nspec * sizeof (dtrace_speculation_t));

	dtrace_format_destroy(state);

	if (state->dts_aggid_arena != NULL) {
#ifdef illumos
		vmem_destroy(state->dts_aggid_arena);
#else
		delete_unrhdr(state->dts_aggid_arena);
#endif
		state->dts_aggid_arena = NULL;
	}
#ifdef illumos
	ddi_soft_state_free(dtrace_softstate, minor);
	vmem_free(dtrace_minor, (void *)(uintptr_t)minor, 1);
#endif
}

/*
 * DTrace Anonymous Enabling Functions
 */
static dtrace_state_t *
dtrace_anon_grab(void)
{
	dtrace_state_t *state;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	if ((state = dtrace_anon.dta_state) == NULL) {
		ASSERT(dtrace_anon.dta_enabling == NULL);
		return (NULL);
	}

	ASSERT(dtrace_anon.dta_enabling != NULL);
	ASSERT(dtrace_retained != NULL);

	dtrace_enabling_destroy(dtrace_anon.dta_enabling);
	dtrace_anon.dta_enabling = NULL;
	dtrace_anon.dta_state = NULL;

	return (state);
}

static void
dtrace_anon_property(void)
{
	int i, rv;
	dtrace_state_t *state;
	dof_hdr_t *dof;
	char c[32];		/* enough for "dof-data-" + digits */

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(MUTEX_HELD(&cpu_lock));

	for (i = 0; ; i++) {
		(void) snprintf(c, sizeof (c), "dof-data-%d", i);

		dtrace_err_verbose = 1;

		if ((dof = dtrace_dof_property(c)) == NULL) {
			dtrace_err_verbose = 0;
			break;
		}

#ifdef illumos
		/*
		 * We want to create anonymous state, so we need to transition
		 * the kernel debugger to indicate that DTrace is active.  If
		 * this fails (e.g. because the debugger has modified text in
		 * some way), we won't continue with the processing.
		 */
		if (kdi_dtrace_set(KDI_DTSET_DTRACE_ACTIVATE) != 0) {
			cmn_err(CE_NOTE, "kernel debugger active; anonymous "
			    "enabling ignored.");
			dtrace_dof_destroy(dof);
			break;
		}
#endif

		/*
		 * If we haven't allocated an anonymous state, we'll do so now.
		 */
		if ((state = dtrace_anon.dta_state) == NULL) {
			state = dtrace_state_create(NULL, NULL);
			dtrace_anon.dta_state = state;

			if (state == NULL) {
				/*
				 * This basically shouldn't happen:  the only
				 * failure mode from dtrace_state_create() is a
				 * failure of ddi_soft_state_zalloc() that
				 * itself should never happen.  Still, the
				 * interface allows for a failure mode, and
				 * we want to fail as gracefully as possible:
				 * we'll emit an error message and cease
				 * processing anonymous state in this case.
				 */
				cmn_err(CE_WARN, "failed to create "
				    "anonymous state");
				dtrace_dof_destroy(dof);
				break;
			}
		}

		rv = dtrace_dof_slurp(dof, &state->dts_vstate, CRED(),
		    &dtrace_anon.dta_enabling, 0, 0, B_TRUE);

		if (rv == 0)
			rv = dtrace_dof_options(dof, state);

		dtrace_err_verbose = 0;
		dtrace_dof_destroy(dof);

		if (rv != 0) {
			/*
			 * This is malformed DOF; chuck any anonymous state
			 * that we created.
			 */
			ASSERT(dtrace_anon.dta_enabling == NULL);
			dtrace_state_destroy(state);
			dtrace_anon.dta_state = NULL;
			break;
		}

		ASSERT(dtrace_anon.dta_enabling != NULL);
	}

	if (dtrace_anon.dta_enabling != NULL) {
		int rval;

		/*
		 * dtrace_enabling_retain() can only fail because we are
		 * trying to retain more enablings than are allowed -- but
		 * we only have one anonymous enabling, and we are guaranteed
		 * to be allowed at least one retained enabling; we assert
		 * that dtrace_enabling_retain() returns success.
		 */
		rval = dtrace_enabling_retain(dtrace_anon.dta_enabling);
		ASSERT(rval == 0);

		dtrace_enabling_dump(dtrace_anon.dta_enabling);
	}
}

/*
 * DTrace Helper Functions
 */
static void
dtrace_helper_trace(dtrace_helper_action_t *helper,
    dtrace_mstate_t *mstate, dtrace_vstate_t *vstate, int where)
{
	uint32_t size, next, nnext, i;
	dtrace_helptrace_t *ent, *buffer;
	uint16_t flags = cpu_core[curcpu].cpuc_dtrace_flags;

	if ((buffer = dtrace_helptrace_buffer) == NULL)
		return;

	ASSERT(vstate->dtvs_nlocals <= dtrace_helptrace_nlocals);

	/*
	 * What would a tracing framework be without its own tracing
	 * framework?  (Well, a hell of a lot simpler, for starters...)
	 */
	size = sizeof (dtrace_helptrace_t) + dtrace_helptrace_nlocals *
	    sizeof (uint64_t) - sizeof (uint64_t);

	/*
	 * Iterate until we can allocate a slot in the trace buffer.
	 */
	do {
		next = dtrace_helptrace_next;

		if (next + size < dtrace_helptrace_bufsize) {
			nnext = next + size;
		} else {
			nnext = size;
		}
	} while (dtrace_cas32(&dtrace_helptrace_next, next, nnext) != next);

	/*
	 * We have our slot; fill it in.
	 */
	if (nnext == size) {
		dtrace_helptrace_wrapped++;
		next = 0;
	}

	ent = (dtrace_helptrace_t *)((uintptr_t)buffer + next);
	ent->dtht_helper = helper;
	ent->dtht_where = where;
	ent->dtht_nlocals = vstate->dtvs_nlocals;

	ent->dtht_fltoffs = (mstate->dtms_present & DTRACE_MSTATE_FLTOFFS) ?
	    mstate->dtms_fltoffs : -1;
	ent->dtht_fault = DTRACE_FLAGS2FLT(flags);
	ent->dtht_illval = cpu_core[curcpu].cpuc_dtrace_illval;

	for (i = 0; i < vstate->dtvs_nlocals; i++) {
		dtrace_statvar_t *svar;

		if ((svar = vstate->dtvs_locals[i]) == NULL)
			continue;

		ASSERT(svar->dtsv_size >= NCPU * sizeof (uint64_t));
		ent->dtht_locals[i] =
		    ((uint64_t *)(uintptr_t)svar->dtsv_data)[curcpu];
	}
}

static uint64_t
dtrace_helper(int which, dtrace_mstate_t *mstate,
    dtrace_state_t *state, uint64_t arg0, uint64_t arg1)
{
	uint16_t *flags = &cpu_core[curcpu].cpuc_dtrace_flags;
	uint64_t sarg0 = mstate->dtms_arg[0];
	uint64_t sarg1 = mstate->dtms_arg[1];
	uint64_t rval = 0;
	dtrace_helpers_t *helpers = curproc->p_dtrace_helpers;
	dtrace_helper_action_t *helper;
	dtrace_vstate_t *vstate;
	dtrace_difo_t *pred;
	int i, trace = dtrace_helptrace_buffer != NULL;

	ASSERT(which >= 0 && which < DTRACE_NHELPER_ACTIONS);

	if (helpers == NULL)
		return (0);

	if ((helper = helpers->dthps_actions[which]) == NULL)
		return (0);

	vstate = &helpers->dthps_vstate;
	mstate->dtms_arg[0] = arg0;
	mstate->dtms_arg[1] = arg1;

	/*
	 * Now iterate over each helper.  If its predicate evaluates to 'true',
	 * we'll call the corresponding actions.  Note that the below calls
	 * to dtrace_dif_emulate() may set faults in machine state.  This is
	 * okay:  our caller (the outer dtrace_dif_emulate()) will simply plow
	 * the stored DIF offset with its own (which is the desired behavior).
	 * Also, note the calls to dtrace_dif_emulate() may allocate scratch
	 * from machine state; this is okay, too.
	 */
	for (; helper != NULL; helper = helper->dtha_next) {
		if ((pred = helper->dtha_predicate) != NULL) {
			if (trace)
				dtrace_helper_trace(helper, mstate, vstate, 0);

			if (!dtrace_dif_emulate(pred, mstate, vstate, state))
				goto next;

			if (*flags & CPU_DTRACE_FAULT)
				goto err;
		}

		for (i = 0; i < helper->dtha_nactions; i++) {
			if (trace)
				dtrace_helper_trace(helper,
				    mstate, vstate, i + 1);

			rval = dtrace_dif_emulate(helper->dtha_actions[i],
			    mstate, vstate, state);

			if (*flags & CPU_DTRACE_FAULT)
				goto err;
		}

next:
		if (trace)
			dtrace_helper_trace(helper, mstate, vstate,
			    DTRACE_HELPTRACE_NEXT);
	}

	if (trace)
		dtrace_helper_trace(helper, mstate, vstate,
		    DTRACE_HELPTRACE_DONE);

	/*
	 * Restore the arg0 that we saved upon entry.
	 */
	mstate->dtms_arg[0] = sarg0;
	mstate->dtms_arg[1] = sarg1;

	return (rval);

err:
	if (trace)
		dtrace_helper_trace(helper, mstate, vstate,
		    DTRACE_HELPTRACE_ERR);

	/*
	 * Restore the arg0 that we saved upon entry.
	 */
	mstate->dtms_arg[0] = sarg0;
	mstate->dtms_arg[1] = sarg1;

	return (0);
}

static void
dtrace_helper_action_destroy(dtrace_helper_action_t *helper,
    dtrace_vstate_t *vstate)
{
	int i;

	if (helper->dtha_predicate != NULL)
		dtrace_difo_release(helper->dtha_predicate, vstate);

	for (i = 0; i < helper->dtha_nactions; i++) {
		ASSERT(helper->dtha_actions[i] != NULL);
		dtrace_difo_release(helper->dtha_actions[i], vstate);
	}

	kmem_free(helper->dtha_actions,
	    helper->dtha_nactions * sizeof (dtrace_difo_t *));
	kmem_free(helper, sizeof (dtrace_helper_action_t));
}

static int
dtrace_helper_destroygen(dtrace_helpers_t *help, int gen)
{
	proc_t *p = curproc;
	dtrace_vstate_t *vstate;
	int i;

	if (help == NULL)
		help = p->p_dtrace_helpers;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	if (help == NULL || gen > help->dthps_generation)
		return (EINVAL);

	vstate = &help->dthps_vstate;

	for (i = 0; i < DTRACE_NHELPER_ACTIONS; i++) {
		dtrace_helper_action_t *last = NULL, *h, *next;

		for (h = help->dthps_actions[i]; h != NULL; h = next) {
			next = h->dtha_next;

			if (h->dtha_generation == gen) {
				if (last != NULL) {
					last->dtha_next = next;
				} else {
					help->dthps_actions[i] = next;
				}

				dtrace_helper_action_destroy(h, vstate);
			} else {
				last = h;
			}
		}
	}

	/*
	 * Interate until we've cleared out all helper providers with the
	 * given generation number.
	 */
	for (;;) {
		dtrace_helper_provider_t *prov;

		/*
		 * Look for a helper provider with the right generation. We
		 * have to start back at the beginning of the list each time
		 * because we drop dtrace_lock. It's unlikely that we'll make
		 * more than two passes.
		 */
		for (i = 0; i < help->dthps_nprovs; i++) {
			prov = help->dthps_provs[i];

			if (prov->dthp_generation == gen)
				break;
		}

		/*
		 * If there were no matches, we're done.
		 */
		if (i == help->dthps_nprovs)
			break;

		/*
		 * Move the last helper provider into this slot.
		 */
		help->dthps_nprovs--;
		help->dthps_provs[i] = help->dthps_provs[help->dthps_nprovs];
		help->dthps_provs[help->dthps_nprovs] = NULL;

		mutex_exit(&dtrace_lock);

		/*
		 * If we have a meta provider, remove this helper provider.
		 */
		mutex_enter(&dtrace_meta_lock);
		if (dtrace_meta_pid != NULL) {
			ASSERT(dtrace_deferred_pid == NULL);
			dtrace_helper_provider_remove(&prov->dthp_prov,
			    p->p_pid);
		}
		mutex_exit(&dtrace_meta_lock);

		dtrace_helper_provider_destroy(prov);

		mutex_enter(&dtrace_lock);
	}

	return (0);
}

static int
dtrace_helper_validate(dtrace_helper_action_t *helper)
{
	int err = 0, i;
	dtrace_difo_t *dp;

	if ((dp = helper->dtha_predicate) != NULL)
		err += dtrace_difo_validate_helper(dp);

	for (i = 0; i < helper->dtha_nactions; i++)
		err += dtrace_difo_validate_helper(helper->dtha_actions[i]);

	return (err == 0);
}

static int
dtrace_helper_action_add(int which, dtrace_ecbdesc_t *ep,
    dtrace_helpers_t *help)
{
	dtrace_helper_action_t *helper, *last;
	dtrace_actdesc_t *act;
	dtrace_vstate_t *vstate;
	dtrace_predicate_t *pred;
	int count = 0, nactions = 0, i;

	if (which < 0 || which >= DTRACE_NHELPER_ACTIONS)
		return (EINVAL);

	last = help->dthps_actions[which];
	vstate = &help->dthps_vstate;

	for (count = 0; last != NULL; last = last->dtha_next) {
		count++;
		if (last->dtha_next == NULL)
			break;
	}

	/*
	 * If we already have dtrace_helper_actions_max helper actions for this
	 * helper action type, we'll refuse to add a new one.
	 */
	if (count >= dtrace_helper_actions_max)
		return (ENOSPC);

	helper = kmem_zalloc(sizeof (dtrace_helper_action_t), KM_SLEEP);
	helper->dtha_generation = help->dthps_generation;

	if ((pred = ep->dted_pred.dtpdd_predicate) != NULL) {
		ASSERT(pred->dtp_difo != NULL);
		dtrace_difo_hold(pred->dtp_difo);
		helper->dtha_predicate = pred->dtp_difo;
	}

	for (act = ep->dted_action; act != NULL; act = act->dtad_next) {
		if (act->dtad_kind != DTRACEACT_DIFEXPR)
			goto err;

		if (act->dtad_difo == NULL)
			goto err;

		nactions++;
	}

	helper->dtha_actions = kmem_zalloc(sizeof (dtrace_difo_t *) *
	    (helper->dtha_nactions = nactions), KM_SLEEP);

	for (act = ep->dted_action, i = 0; act != NULL; act = act->dtad_next) {
		dtrace_difo_hold(act->dtad_difo);
		helper->dtha_actions[i++] = act->dtad_difo;
	}

	if (!dtrace_helper_validate(helper))
		goto err;

	if (last == NULL) {
		help->dthps_actions[which] = helper;
	} else {
		last->dtha_next = helper;
	}

	if (vstate->dtvs_nlocals > dtrace_helptrace_nlocals) {
		dtrace_helptrace_nlocals = vstate->dtvs_nlocals;
		dtrace_helptrace_next = 0;
	}

	return (0);
err:
	dtrace_helper_action_destroy(helper, vstate);
	return (EINVAL);
}

static void
dtrace_helper_provider_register(proc_t *p, dtrace_helpers_t *help,
    dof_helper_t *dofhp)
{
	ASSERT(MUTEX_NOT_HELD(&dtrace_lock));

	mutex_enter(&dtrace_meta_lock);
	mutex_enter(&dtrace_lock);

	if (!dtrace_attached() || dtrace_meta_pid == NULL) {
		/*
		 * If the dtrace module is loaded but not attached, or if
		 * there aren't isn't a meta provider registered to deal with
		 * these provider descriptions, we need to postpone creating
		 * the actual providers until later.
		 */

		if (help->dthps_next == NULL && help->dthps_prev == NULL &&
		    dtrace_deferred_pid != help) {
			help->dthps_deferred = 1;
			help->dthps_pid = p->p_pid;
			help->dthps_next = dtrace_deferred_pid;
			help->dthps_prev = NULL;
			if (dtrace_deferred_pid != NULL)
				dtrace_deferred_pid->dthps_prev = help;
			dtrace_deferred_pid = help;
		}

		mutex_exit(&dtrace_lock);

	} else if (dofhp != NULL) {
		/*
		 * If the dtrace module is loaded and we have a particular
		 * helper provider description, pass that off to the
		 * meta provider.
		 */

		mutex_exit(&dtrace_lock);

		dtrace_helper_provide(dofhp, p->p_pid);

	} else {
		/*
		 * Otherwise, just pass all the helper provider descriptions
		 * off to the meta provider.
		 */

		int i;
		mutex_exit(&dtrace_lock);

		for (i = 0; i < help->dthps_nprovs; i++) {
			dtrace_helper_provide(&help->dthps_provs[i]->dthp_prov,
			    p->p_pid);
		}
	}

	mutex_exit(&dtrace_meta_lock);
}

static int
dtrace_helper_provider_add(dof_helper_t *dofhp, dtrace_helpers_t *help, int gen)
{
	dtrace_helper_provider_t *hprov, **tmp_provs;
	uint_t tmp_maxprovs, i;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(help != NULL);

	/*
	 * If we already have dtrace_helper_providers_max helper providers,
	 * we're refuse to add a new one.
	 */
	if (help->dthps_nprovs >= dtrace_helper_providers_max)
		return (ENOSPC);

	/*
	 * Check to make sure this isn't a duplicate.
	 */
	for (i = 0; i < help->dthps_nprovs; i++) {
		if (dofhp->dofhp_addr ==
		    help->dthps_provs[i]->dthp_prov.dofhp_addr)
			return (EALREADY);
	}

	hprov = kmem_zalloc(sizeof (dtrace_helper_provider_t), KM_SLEEP);
	hprov->dthp_prov = *dofhp;
	hprov->dthp_ref = 1;
	hprov->dthp_generation = gen;

	/*
	 * Allocate a bigger table for helper providers if it's already full.
	 */
	if (help->dthps_maxprovs == help->dthps_nprovs) {
		tmp_maxprovs = help->dthps_maxprovs;
		tmp_provs = help->dthps_provs;

		if (help->dthps_maxprovs == 0)
			help->dthps_maxprovs = 2;
		else
			help->dthps_maxprovs *= 2;
		if (help->dthps_maxprovs > dtrace_helper_providers_max)
			help->dthps_maxprovs = dtrace_helper_providers_max;

		ASSERT(tmp_maxprovs < help->dthps_maxprovs);

		help->dthps_provs = kmem_zalloc(help->dthps_maxprovs *
		    sizeof (dtrace_helper_provider_t *), KM_SLEEP);

		if (tmp_provs != NULL) {
			bcopy(tmp_provs, help->dthps_provs, tmp_maxprovs *
			    sizeof (dtrace_helper_provider_t *));
			kmem_free(tmp_provs, tmp_maxprovs *
			    sizeof (dtrace_helper_provider_t *));
		}
	}

	help->dthps_provs[help->dthps_nprovs] = hprov;
	help->dthps_nprovs++;

	return (0);
}

static void
dtrace_helper_provider_destroy(dtrace_helper_provider_t *hprov)
{
	mutex_enter(&dtrace_lock);

	if (--hprov->dthp_ref == 0) {
		dof_hdr_t *dof;
		mutex_exit(&dtrace_lock);
		dof = (dof_hdr_t *)(uintptr_t)hprov->dthp_prov.dofhp_dof;
		dtrace_dof_destroy(dof);
		kmem_free(hprov, sizeof (dtrace_helper_provider_t));
	} else {
		mutex_exit(&dtrace_lock);
	}
}

static int
dtrace_helper_provider_validate(dof_hdr_t *dof, dof_sec_t *sec)
{
	uintptr_t daddr = (uintptr_t)dof;
	dof_sec_t *str_sec, *prb_sec, *arg_sec, *off_sec, *enoff_sec;
	dof_provider_t *provider;
	dof_probe_t *probe;
	uint8_t *arg;
	char *strtab, *typestr;
	dof_stridx_t typeidx;
	size_t typesz;
	uint_t nprobes, j, k;

	ASSERT(sec->dofs_type == DOF_SECT_PROVIDER);

	if (sec->dofs_offset & (sizeof (uint_t) - 1)) {
		dtrace_dof_error(dof, "misaligned section offset");
		return (-1);
	}

	/*
	 * The section needs to be large enough to contain the DOF provider
	 * structure appropriate for the given version.
	 */
	if (sec->dofs_size <
	    ((dof->dofh_ident[DOF_ID_VERSION] == DOF_VERSION_1) ?
	    offsetof(dof_provider_t, dofpv_prenoffs) :
	    sizeof (dof_provider_t))) {
		dtrace_dof_error(dof, "provider section too small");
		return (-1);
	}

	provider = (dof_provider_t *)(uintptr_t)(daddr + sec->dofs_offset);
	str_sec = dtrace_dof_sect(dof, DOF_SECT_STRTAB, provider->dofpv_strtab);
	prb_sec = dtrace_dof_sect(dof, DOF_SECT_PROBES, provider->dofpv_probes);
	arg_sec = dtrace_dof_sect(dof, DOF_SECT_PRARGS, provider->dofpv_prargs);
	off_sec = dtrace_dof_sect(dof, DOF_SECT_PROFFS, provider->dofpv_proffs);

	if (str_sec == NULL || prb_sec == NULL ||
	    arg_sec == NULL || off_sec == NULL)
		return (-1);

	enoff_sec = NULL;

	if (dof->dofh_ident[DOF_ID_VERSION] != DOF_VERSION_1 &&
	    provider->dofpv_prenoffs != DOF_SECT_NONE &&
	    (enoff_sec = dtrace_dof_sect(dof, DOF_SECT_PRENOFFS,
	    provider->dofpv_prenoffs)) == NULL)
		return (-1);

	strtab = (char *)(uintptr_t)(daddr + str_sec->dofs_offset);

	if (provider->dofpv_name >= str_sec->dofs_size ||
	    strlen(strtab + provider->dofpv_name) >= DTRACE_PROVNAMELEN) {
		dtrace_dof_error(dof, "invalid provider name");
		return (-1);
	}

	if (prb_sec->dofs_entsize == 0 ||
	    prb_sec->dofs_entsize > prb_sec->dofs_size) {
		dtrace_dof_error(dof, "invalid entry size");
		return (-1);
	}

	if (prb_sec->dofs_entsize & (sizeof (uintptr_t) - 1)) {
		dtrace_dof_error(dof, "misaligned entry size");
		return (-1);
	}

	if (off_sec->dofs_entsize != sizeof (uint32_t)) {
		dtrace_dof_error(dof, "invalid entry size");
		return (-1);
	}

	if (off_sec->dofs_offset & (sizeof (uint32_t) - 1)) {
		dtrace_dof_error(dof, "misaligned section offset");
		return (-1);
	}

	if (arg_sec->dofs_entsize != sizeof (uint8_t)) {
		dtrace_dof_error(dof, "invalid entry size");
		return (-1);
	}

	arg = (uint8_t *)(uintptr_t)(daddr + arg_sec->dofs_offset);

	nprobes = prb_sec->dofs_size / prb_sec->dofs_entsize;

	/*
	 * Take a pass through the probes to check for errors.
	 */
	for (j = 0; j < nprobes; j++) {
		probe = (dof_probe_t *)(uintptr_t)(daddr +
		    prb_sec->dofs_offset + j * prb_sec->dofs_entsize);

		if (probe->dofpr_func >= str_sec->dofs_size) {
			dtrace_dof_error(dof, "invalid function name");
			return (-1);
		}

		if (strlen(strtab + probe->dofpr_func) >= DTRACE_FUNCNAMELEN) {
			dtrace_dof_error(dof, "function name too long");
			/*
			 * Keep going if the function name is too long.
			 * Unlike provider and probe names, we cannot reasonably
			 * impose restrictions on function names, since they're
			 * a property of the code being instrumented. We will
			 * skip this probe in dtrace_helper_provide_one().
			 */
		}

		if (probe->dofpr_name >= str_sec->dofs_size ||
		    strlen(strtab + probe->dofpr_name) >= DTRACE_NAMELEN) {
			dtrace_dof_error(dof, "invalid probe name");
			return (-1);
		}

		/*
		 * The offset count must not wrap the index, and the offsets
		 * must also not overflow the section's data.
		 */
		if (probe->dofpr_offidx + probe->dofpr_noffs <
		    probe->dofpr_offidx ||
		    (probe->dofpr_offidx + probe->dofpr_noffs) *
		    off_sec->dofs_entsize > off_sec->dofs_size) {
			dtrace_dof_error(dof, "invalid probe offset");
			return (-1);
		}

		if (dof->dofh_ident[DOF_ID_VERSION] != DOF_VERSION_1) {
			/*
			 * If there's no is-enabled offset section, make sure
			 * there aren't any is-enabled offsets. Otherwise
			 * perform the same checks as for probe offsets
			 * (immediately above).
			 */
			if (enoff_sec == NULL) {
				if (probe->dofpr_enoffidx != 0 ||
				    probe->dofpr_nenoffs != 0) {
					dtrace_dof_error(dof, "is-enabled "
					    "offsets with null section");
					return (-1);
				}
			} else if (probe->dofpr_enoffidx +
			    probe->dofpr_nenoffs < probe->dofpr_enoffidx ||
			    (probe->dofpr_enoffidx + probe->dofpr_nenoffs) *
			    enoff_sec->dofs_entsize > enoff_sec->dofs_size) {
				dtrace_dof_error(dof, "invalid is-enabled "
				    "offset");
				return (-1);
			}

			if (probe->dofpr_noffs + probe->dofpr_nenoffs == 0) {
				dtrace_dof_error(dof, "zero probe and "
				    "is-enabled offsets");
				return (-1);
			}
		} else if (probe->dofpr_noffs == 0) {
			dtrace_dof_error(dof, "zero probe offsets");
			return (-1);
		}

		if (probe->dofpr_argidx + probe->dofpr_xargc <
		    probe->dofpr_argidx ||
		    (probe->dofpr_argidx + probe->dofpr_xargc) *
		    arg_sec->dofs_entsize > arg_sec->dofs_size) {
			dtrace_dof_error(dof, "invalid args");
			return (-1);
		}

		typeidx = probe->dofpr_nargv;
		typestr = strtab + probe->dofpr_nargv;
		for (k = 0; k < probe->dofpr_nargc; k++) {
			if (typeidx >= str_sec->dofs_size) {
				dtrace_dof_error(dof, "bad "
				    "native argument type");
				return (-1);
			}

			typesz = strlen(typestr) + 1;
			if (typesz > DTRACE_ARGTYPELEN) {
				dtrace_dof_error(dof, "native "
				    "argument type too long");
				return (-1);
			}
			typeidx += typesz;
			typestr += typesz;
		}

		typeidx = probe->dofpr_xargv;
		typestr = strtab + probe->dofpr_xargv;
		for (k = 0; k < probe->dofpr_xargc; k++) {
			if (arg[probe->dofpr_argidx + k] > probe->dofpr_nargc) {
				dtrace_dof_error(dof, "bad "
				    "native argument index");
				return (-1);
			}

			if (typeidx >= str_sec->dofs_size) {
				dtrace_dof_error(dof, "bad "
				    "translated argument type");
				return (-1);
			}

			typesz = strlen(typestr) + 1;
			if (typesz > DTRACE_ARGTYPELEN) {
				dtrace_dof_error(dof, "translated argument "
				    "type too long");
				return (-1);
			}

			typeidx += typesz;
			typestr += typesz;
		}
	}

	return (0);
}

static int
dtrace_helper_slurp(dof_hdr_t *dof, dof_helper_t *dhp, struct proc *p)
{
	dtrace_helpers_t *help;
	dtrace_vstate_t *vstate;
	dtrace_enabling_t *enab = NULL;
	int i, gen, rv, nhelpers = 0, nprovs = 0, destroy = 1;
	uintptr_t daddr = (uintptr_t)dof;

	ASSERT(MUTEX_HELD(&dtrace_lock));

	if ((help = p->p_dtrace_helpers) == NULL)
		help = dtrace_helpers_create(p);

	vstate = &help->dthps_vstate;

	if ((rv = dtrace_dof_slurp(dof, vstate, NULL, &enab, dhp->dofhp_addr,
	    dhp->dofhp_dof, B_FALSE)) != 0) {
		dtrace_dof_destroy(dof);
		return (rv);
	}

	/*
	 * Look for helper providers and validate their descriptions.
	 */
	for (i = 0; i < dof->dofh_secnum; i++) {
		dof_sec_t *sec = (dof_sec_t *)(uintptr_t)(daddr +
		    dof->dofh_secoff + i * dof->dofh_secsize);

		if (sec->dofs_type != DOF_SECT_PROVIDER)
			continue;

		if (dtrace_helper_provider_validate(dof, sec) != 0) {
			dtrace_enabling_destroy(enab);
			dtrace_dof_destroy(dof);
			return (-1);
		}

		nprovs++;
	}

	/*
	 * Now we need to walk through the ECB descriptions in the enabling.
	 */
	for (i = 0; i < enab->dten_ndesc; i++) {
		dtrace_ecbdesc_t *ep = enab->dten_desc[i];
		dtrace_probedesc_t *desc = &ep->dted_probe;

		if (strcmp(desc->dtpd_provider, "dtrace") != 0)
			continue;

		if (strcmp(desc->dtpd_mod, "helper") != 0)
			continue;

		if (strcmp(desc->dtpd_func, "ustack") != 0)
			continue;

		if ((rv = dtrace_helper_action_add(DTRACE_HELPER_ACTION_USTACK,
		    ep, help)) != 0) {
			/*
			 * Adding this helper action failed -- we are now going
			 * to rip out the entire generation and return failure.
			 */
			(void) dtrace_helper_destroygen(help,
			    help->dthps_generation);
			dtrace_enabling_destroy(enab);
			dtrace_dof_destroy(dof);
			return (-1);
		}

		nhelpers++;
	}

	if (nhelpers < enab->dten_ndesc)
		dtrace_dof_error(dof, "unmatched helpers");

	gen = help->dthps_generation++;
	dtrace_enabling_destroy(enab);

	if (nprovs > 0) {
		/*
		 * Now that this is in-kernel, we change the sense of the
		 * members:  dofhp_dof denotes the in-kernel copy of the DOF
		 * and dofhp_addr denotes the address at user-level.
		 */
		dhp->dofhp_addr = dhp->dofhp_dof;
		dhp->dofhp_dof = (uint64_t)(uintptr_t)dof;

		if (dtrace_helper_provider_add(dhp, help, gen) == 0) {
			mutex_exit(&dtrace_lock);
			dtrace_helper_provider_register(p, help, dhp);
			mutex_enter(&dtrace_lock);

			destroy = 0;
		}
	}

	if (destroy)
		dtrace_dof_destroy(dof);

	return (gen);
}

static dtrace_helpers_t *
dtrace_helpers_create(proc_t *p)
{
	dtrace_helpers_t *help;

	ASSERT(MUTEX_HELD(&dtrace_lock));
	ASSERT(p->p_dtrace_helpers == NULL);

	help = kmem_zalloc(sizeof (dtrace_helpers_t), KM_SLEEP);
	help->dthps_actions = kmem_zalloc(sizeof (dtrace_helper_action_t *) *
	    DTRACE_NHELPER_ACTIONS, KM_SLEEP);

	p->p_dtrace_helpers = help;
	dtrace_helpers++;

	return (help);
}

#ifdef illumos
static
#endif
void
dtrace_helpers_destroy(proc_t *p)
{
	dtrace_helpers_t *help;
	dtrace_vstate_t *vstate;
#ifdef illumos
	proc_t *p = curproc;
#endif
	int i;

	mutex_enter(&dtrace_lock);

	ASSERT(p->p_dtrace_helpers != NULL);
	ASSERT(dtrace_helpers > 0);

	help = p->p_dtrace_helpers;
	vstate = &help->dthps_vstate;

	/*
	 * We're now going to lose the help from this process.
	 */
	p->p_dtrace_helpers = NULL;
	dtrace_sync();

	/*
	 * Destory the helper actions.
	 */
	for (i = 0; i < DTRACE_NHELPER_ACTIONS; i++) {
		dtrace_helper_action_t *h, *next;

		for (h = help->dthps_actions[i]; h != NULL; h = next) {
			next = h->dtha_next;
			dtrace_helper_action_destroy(h, vstate);
			h = next;
		}
	}

	mutex_exit(&dtrace_lock);

	/*
	 * Destroy the helper providers.
	 */
	if (help->dthps_maxprovs > 0) {
		mutex_enter(&dtrace_meta_lock);
		if (dtrace_meta_pid != NULL) {
			ASSERT(dtrace_deferred_pid == NULL);

			for (i = 0; i < help->dthps_nprovs; i++) {
				dtrace_helper_provider_remove(
				    &help->dthps_provs[i]->dthp_prov, p->p_pid);
			}
		} else {
			mutex_enter(&dtrace_lock);
			ASSERT(help->dthps_deferred == 0 ||
			    help->dthps_next != NULL ||
			    help->dthps_prev != NULL ||
			    help == dtrace_deferred_pid);

			/*
			 * Remove the helper from the deferred list.
			 */
			if (help->dthps_next != NULL)
				help->dthps_next->dthps_prev = help->dthps_prev;
			if (help->dthps_prev != NULL)
				help->dthps_prev->dthps_next = help->dthps_next;
			if (dtrace_deferred_pid == help) {
				dtrace_deferred_pid = help->dthps_next;
				ASSERT(help->dthps_prev == NULL);
			}

			mutex_exit(&dtrace_lock);
		}

		mutex_exit(&dtrace_meta_lock);

		for (i = 0; i < help->dthps_nprovs; i++) {
			dtrace_helper_provider_destroy(help->dthps_provs[i]);
		}

		kmem_free(help->dthps_provs, help->dthps_maxprovs *
		    sizeof (dtrace_helper_provider_t *));
	}

	mutex_enter(&dtrace_lock);

	dtrace_vstate_fini(&help->dthps_vstate);
	kmem_free(help->dthps_actions,
	    sizeof (dtrace_helper_action_t *) * DTRACE_NHELPER_ACTIONS);
	kmem_free(help, sizeof (dtrace_helpers_t));

	--dtrace_helpers;
	mutex_exit(&dtrace_lock);
}

#ifdef illumos
static
#endif
void
dtrace_helpers_duplicate(proc_t *from, proc_t *to)
{
	dtrace_helpers_t *help, *newhelp;
	dtrace_helper_action_t *helper, *new, *last;
	dtrace_difo_t *dp;
	dtrace_vstate_t *vstate;
	int i, j, sz, hasprovs = 0;

	mutex_enter(&dtrace_lock);
	ASSERT(from->p_dtrace_helpers != NULL);
	ASSERT(dtrace_helpers > 0);

	help = from->p_dtrace_helpers;
	newhelp = dtrace_helpers_create(to);
	ASSERT(to->p_dtrace_helpers != NULL);

	newhelp->dthps_generation = help->dthps_generation;
	vstate = &newhelp->dthps_vstate;

	/*
	 * Duplicate the helper actions.
	 */
	for (i = 0; i < DTRACE_NHELPER_ACTIONS; i++) {
		if ((helper = help->dthps_actions[i]) == NULL)
			continue;

		for (last = NULL; helper != NULL; helper = helper->dtha_next) {
			new = kmem_zalloc(sizeof (dtrace_helper_action_t),
			    KM_SLEEP);
			new->dtha_generation = helper->dtha_generation;

			if ((dp = helper->dtha_predicate) != NULL) {
				dp = dtrace_difo_duplicate(dp, vstate);
				new->dtha_predicate = dp;
			}

			new->dtha_nactions = helper->dtha_nactions;
			sz = sizeof (dtrace_difo_t *) * new->dtha_nactions;
			new->dtha_actions = kmem_alloc(sz, KM_SLEEP);

			for (j = 0; j < new->dtha_nactions; j++) {
				dtrace_difo_t *dp = helper->dtha_actions[j];

				ASSERT(dp != NULL);
				dp = dtrace_difo_duplicate(dp, vstate);
				new->dtha_actions[j] = dp;
			}

			if (last != NULL) {
				last->dtha_next = new;
			} else {
				newhelp->dthps_actions[i] = new;
			}

			last = new;
		}
	}

	/*
	 * Duplicate the helper providers and register them with the
	 * DTrace framework.
	 */
	if (help->dthps_nprovs > 0) {
		newhelp->dthps_nprovs = help->dthps_nprovs;
		newhelp->dthps_maxprovs = help->dthps_nprovs;
		newhelp->dthps_provs = kmem_alloc(newhelp->dthps_nprovs *
		    sizeof (dtrace_helper_provider_t *), KM_SLEEP);
		for (i = 0; i < newhelp->dthps_nprovs; i++) {
			newhelp->dthps_provs[i] = help->dthps_provs[i];
			newhelp->dthps_provs[i]->dthp_ref++;
		}

		hasprovs = 1;
	}

	mutex_exit(&dtrace_lock);

	if (hasprovs)
		dtrace_helper_provider_register(to, newhelp, NULL);
}

/*
 * DTrace Hook Functions
 */
static void
dtrace_module_loaded(modctl_t *ctl)
{
	dtrace_provider_t *prv;

	mutex_enter(&dtrace_provider_lock);
#ifdef illumos
	mutex_enter(&mod_lock);
#endif

#ifdef illumos
	ASSERT(ctl->mod_busy);
#endif

	/*
	 * We're going to call each providers per-module provide operation
	 * specifying only this module.
	 */
	for (prv = dtrace_provider; prv != NULL; prv = prv->dtpv_next)
		prv->dtpv_pops.dtps_provide_module(prv->dtpv_arg, ctl);

#ifdef illumos
	mutex_exit(&mod_lock);
#endif
	mutex_exit(&dtrace_provider_lock);

	/*
	 * If we have any retained enablings, we need to match against them.
	 * Enabling probes requires that cpu_lock be held, and we cannot hold
	 * cpu_lock here -- it is legal for cpu_lock to be held when loading a
	 * module.  (In particular, this happens when loading scheduling
	 * classes.)  So if we have any retained enablings, we need to dispatch
	 * our task queue to do the match for us.
	 */
	mutex_enter(&dtrace_lock);

	if (dtrace_retained == NULL) {
		mutex_exit(&dtrace_lock);
		return;
	}

	(void) taskq_dispatch(dtrace_taskq,
	    (task_func_t *)dtrace_enabling_matchall, NULL, TQ_SLEEP);

	mutex_exit(&dtrace_lock);

	/*
	 * And now, for a little heuristic sleaze:  in general, we want to
	 * match modules as soon as they load.  However, we cannot guarantee
	 * this, because it would lead us to the lock ordering violation
	 * outlined above.  The common case, of course, is that cpu_lock is
	 * _not_ held -- so we delay here for a clock tick, hoping that that's
	 * long enough for the task queue to do its work.  If it's not, it's
	 * not a serious problem -- it just means that the module that we
	 * just loaded may not be immediately instrumentable.
	 */
	delay(1);
}

static void
#ifdef illumos
dtrace_module_unloaded(modctl_t *ctl)
#else
dtrace_module_unloaded(modctl_t *ctl, int *error)
#endif
{
	dtrace_probe_t template, *probe, *first, *next;
	dtrace_provider_t *prov;
#ifndef illumos
	char modname[DTRACE_MODNAMELEN];
	size_t len;
#endif

#ifdef illumos
	template.dtpr_mod = ctl->mod_modname;
#else
	/* Handle the fact that ctl->filename may end in ".ko". */
	strlcpy(modname, ctl->filename, sizeof(modname));
	len = strlen(ctl->filename);
	if (len > 3 && strcmp(modname + len - 3, ".ko") == 0)
		modname[len - 3] = '\0';
	template.dtpr_mod = modname;
#endif

	mutex_enter(&dtrace_provider_lock);
#ifdef illumos
	mutex_enter(&mod_lock);
#endif
	mutex_enter(&dtrace_lock);

#ifndef illumos
	if (ctl->nenabled > 0) {
		/* Don't allow unloads if a probe is enabled. */
		mutex_exit(&dtrace_provider_lock);
		mutex_exit(&dtrace_lock);
		*error = -1;
		printf(
	"kldunload: attempt to unload module that has DTrace probes enabled\n");
		return;
	}
#endif

	if (dtrace_bymod == NULL) {
		/*
		 * The DTrace module is loaded (obviously) but not attached;
		 * we don't have any work to do.
		 */
		mutex_exit(&dtrace_provider_lock);
#ifdef illumos
		mutex_exit(&mod_lock);
#endif
		mutex_exit(&dtrace_lock);
		return;
	}

	for (probe = first = dtrace_hash_lookup(dtrace_bymod, &template);
	    probe != NULL; probe = probe->dtpr_nextmod) {
		if (probe->dtpr_ecb != NULL) {
			mutex_exit(&dtrace_provider_lock);
#ifdef illumos
			mutex_exit(&mod_lock);
#endif
			mutex_exit(&dtrace_lock);

			/*
			 * This shouldn't _actually_ be possible -- we're
			 * unloading a module that has an enabled probe in it.
			 * (It's normally up to the provider to make sure that
			 * this can't happen.)  However, because dtps_enable()
			 * doesn't have a failure mode, there can be an
			 * enable/unload race.  Upshot:  we don't want to
			 * assert, but we're not going to disable the
			 * probe, either.
			 */
			if (dtrace_err_verbose) {
#ifdef illumos
				cmn_err(CE_WARN, "unloaded module '%s' had "
				    "enabled probes", ctl->mod_modname);
#else
				cmn_err(CE_WARN, "unloaded module '%s' had "
				    "enabled probes", modname);
#endif
			}

			return;
		}
	}

	probe = first;

	for (first = NULL; probe != NULL; probe = next) {
		ASSERT(dtrace_probes[probe->dtpr_id - 1] == probe);

		dtrace_probes[probe->dtpr_id - 1] = NULL;

		next = probe->dtpr_nextmod;
		dtrace_hash_remove(dtrace_bymod, probe);
		dtrace_hash_remove(dtrace_byfunc, probe);
		dtrace_hash_remove(dtrace_byname, probe);

		if (first == NULL) {
			first = probe;
			probe->dtpr_nextmod = NULL;
		} else {
			probe->dtpr_nextmod = first;
			first = probe;
		}
	}

	/*
	 * We've removed all of the module's probes from the hash chains and
	 * from the probe array.  Now issue a dtrace_sync() to be sure that
	 * everyone has cleared out from any probe array processing.
	 */
	dtrace_sync();

	for (probe = first; probe != NULL; probe = first) {
		first = probe->dtpr_nextmod;
		prov = probe->dtpr_provider;
		prov->dtpv_pops.dtps_destroy(prov->dtpv_arg, probe->dtpr_id,
		    probe->dtpr_arg);
		kmem_free(probe->dtpr_mod, strlen(probe->dtpr_mod) + 1);
		kmem_free(probe->dtpr_func, strlen(probe->dtpr_func) + 1);
		kmem_free(probe->dtpr_name, strlen(probe->dtpr_name) + 1);
#ifdef illumos
		vmem_free(dtrace_arena, (void *)(uintptr_t)probe->dtpr_id, 1);
#else
		free_unr(dtrace_arena, probe->dtpr_id);
#endif
		kmem_free(probe, sizeof (dtrace_probe_t));
	}

	mutex_exit(&dtrace_lock);
#ifdef illumos
	mutex_exit(&mod_lock);
#endif
	mutex_exit(&dtrace_provider_lock);
}

#ifndef illumos
static void
dtrace_kld_load(void *arg __unused, linker_file_t lf)
{

	dtrace_module_loaded(lf);
}

static void
dtrace_kld_unload_try(void *arg __unused, linker_file_t lf, int *error)
{

	if (*error != 0)
		/* We already have an error, so don't do anything. */
		return;
	dtrace_module_unloaded(lf, error);
}
#endif

#ifdef illumos
static void
dtrace_suspend(void)
{
	dtrace_probe_foreach(offsetof(dtrace_pops_t, dtps_suspend));
}

static void
dtrace_resume(void)
{
	dtrace_probe_foreach(offsetof(dtrace_pops_t, dtps_resume));
}
#endif

static int
dtrace_cpu_setup(cpu_setup_t what, processorid_t cpu)
{
	ASSERT(MUTEX_HELD(&cpu_lock));
	mutex_enter(&dtrace_lock);

	switch (what) {
	case CPU_CONFIG: {
		dtrace_state_t *state;
		dtrace_optval_t *opt, rs, c;

		/*
		 * For now, we only allocate a new buffer for anonymous state.
		 */
		if ((state = dtrace_anon.dta_state) == NULL)
			break;

		if (state->dts_activity != DTRACE_ACTIVITY_ACTIVE)
			break;

		opt = state->dts_options;
		c = opt[DTRACEOPT_CPU];

		if (c != DTRACE_CPUALL && c != DTRACEOPT_UNSET && c != cpu)
			break;

		/*
		 * Regardless of what the actual policy is, we're going to
		 * temporarily set our resize policy to be manual.  We're
		 * also going to temporarily set our CPU option to denote
		 * the newly configured CPU.
		 */
		rs = opt[DTRACEOPT_BUFRESIZE];
		opt[DTRACEOPT_BUFRESIZE] = DTRACEOPT_BUFRESIZE_MANUAL;
		opt[DTRACEOPT_CPU] = (dtrace_optval_t)cpu;

		(void) dtrace_state_buffers(state);

		opt[DTRACEOPT_BUFRESIZE] = rs;
		opt[DTRACEOPT_CPU] = c;

		break;
	}

	case CPU_UNCONFIG:
		/*
		 * We don't free the buffer in the CPU_UNCONFIG case.  (The
		 * buffer will be freed when the consumer exits.)
		 */
		break;

	default:
		break;
	}

	mutex_exit(&dtrace_lock);
	return (0);
}

#ifdef illumos
static void
dtrace_cpu_setup_initial(processorid_t cpu)
{
	(void) dtrace_cpu_setup(CPU_CONFIG, cpu);
}
#endif

static void
dtrace_toxrange_add(uintptr_t base, uintptr_t limit)
{
	if (dtrace_toxranges >= dtrace_toxranges_max) {
		int osize, nsize;
		dtrace_toxrange_t *range;

		osize = dtrace_toxranges_max * sizeof (dtrace_toxrange_t);

		if (osize == 0) {
			ASSERT(dtrace_toxrange == NULL);
			ASSERT(dtrace_toxranges_max == 0);
			dtrace_toxranges_max = 1;
		} else {
			dtrace_toxranges_max <<= 1;
		}

		nsize = dtrace_toxranges_max * sizeof (dtrace_toxrange_t);
		range = kmem_zalloc(nsize, KM_SLEEP);

		if (dtrace_toxrange != NULL) {
			ASSERT(osize != 0);
			bcopy(dtrace_toxrange, range, osize);
			kmem_free(dtrace_toxrange, osize);
		}

		dtrace_toxrange = range;
	}

	ASSERT(dtrace_toxrange[dtrace_toxranges].dtt_base == 0);
	ASSERT(dtrace_toxrange[dtrace_toxranges].dtt_limit == 0);

	dtrace_toxrange[dtrace_toxranges].dtt_base = base;
	dtrace_toxrange[dtrace_toxranges].dtt_limit = limit;
	dtrace_toxranges++;
}

static void
dtrace_getf_barrier()
{
#ifdef illumos
	/*
	 * When we have unprivileged (that is, non-DTRACE_CRV_KERNEL) enablings
	 * that contain calls to getf(), this routine will be called on every
	 * closef() before either the underlying vnode is released or the
	 * file_t itself is freed.  By the time we are here, it is essential
	 * that the file_t can no longer be accessed from a call to getf()
	 * in probe context -- that assures that a dtrace_sync() can be used
	 * to clear out any enablings referring to the old structures.
	 */
	if (curthread->t_procp->p_zone->zone_dtrace_getf != 0 ||
	    kcred->cr_zone->zone_dtrace_getf != 0)
		dtrace_sync();
#endif
}

/*
 * DTrace Driver Cookbook Functions
 */
#ifdef illumos
/*ARGSUSED*/
static int
dtrace_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	dtrace_provider_id_t id;
	dtrace_state_t *state = NULL;
	dtrace_enabling_t *enab;

	mutex_enter(&cpu_lock);
	mutex_enter(&dtrace_provider_lock);
	mutex_enter(&dtrace_lock);

	if (ddi_soft_state_init(&dtrace_softstate,
	    sizeof (dtrace_state_t), 0) != 0) {
		cmn_err(CE_NOTE, "/dev/dtrace failed to initialize soft state");
		mutex_exit(&cpu_lock);
		mutex_exit(&dtrace_provider_lock);
		mutex_exit(&dtrace_lock);
		return (DDI_FAILURE);
	}

	if (ddi_create_minor_node(devi, DTRACEMNR_DTRACE, S_IFCHR,
	    DTRACEMNRN_DTRACE, DDI_PSEUDO, NULL) == DDI_FAILURE ||
	    ddi_create_minor_node(devi, DTRACEMNR_HELPER, S_IFCHR,
	    DTRACEMNRN_HELPER, DDI_PSEUDO, NULL) == DDI_FAILURE) {
		cmn_err(CE_NOTE, "/dev/dtrace couldn't create minor nodes");
		ddi_remove_minor_node(devi, NULL);
		ddi_soft_state_fini(&dtrace_softstate);
		mutex_exit(&cpu_lock);
		mutex_exit(&dtrace_provider_lock);
		mutex_exit(&dtrace_lock);
		return (DDI_FAILURE);
	}

	ddi_report_dev(devi);
	dtrace_devi = devi;

	dtrace_modload = dtrace_module_loaded;
	dtrace_modunload = dtrace_module_unloaded;
	dtrace_cpu_init = dtrace_cpu_setup_initial;
	dtrace_helpers_cleanup = dtrace_helpers_destroy;
	dtrace_helpers_fork = dtrace_helpers_duplicate;
	dtrace_cpustart_init = dtrace_suspend;
	dtrace_cpustart_fini = dtrace_resume;
	dtrace_debugger_init = dtrace_suspend;
	dtrace_debugger_fini = dtrace_resume;

	register_cpu_setup_func((cpu_setup_func_t *)dtrace_cpu_setup, NULL);

	ASSERT(MUTEX_HELD(&cpu_lock));

	dtrace_arena = vmem_create("dtrace", (void *)1, UINT32_MAX, 1,
	    NULL, NULL, NULL, 0, VM_SLEEP | VMC_IDENTIFIER);
	dtrace_minor = vmem_create("dtrace_minor", (void *)DTRACEMNRN_CLONE,
	    UINT32_MAX - DTRACEMNRN_CLONE, 1, NULL, NULL, NULL, 0,
	    VM_SLEEP | VMC_IDENTIFIER);
	dtrace_taskq = taskq_create("dtrace_taskq", 1, maxclsyspri,
	    1, INT_MAX, 0);

	dtrace_state_cache = kmem_cache_create("dtrace_state_cache",
	    sizeof (dtrace_dstate_percpu_t) * NCPU, DTRACE_STATE_ALIGN,
	    NULL, NULL, NULL, NULL, NULL, 0);

	ASSERT(MUTEX_HELD(&cpu_lock));
	dtrace_bymod = dtrace_hash_create(offsetof(dtrace_probe_t, dtpr_mod),
	    offsetof(dtrace_probe_t, dtpr_nextmod),
	    offsetof(dtrace_probe_t, dtpr_prevmod));

	dtrace_byfunc = dtrace_hash_create(offsetof(dtrace_probe_t, dtpr_func),
	    offsetof(dtrace_probe_t, dtpr_nextfunc),
	    offsetof(dtrace_probe_t, dtpr_prevfunc));

	dtrace_byname = dtrace_hash_create(offsetof(dtrace_probe_t, dtpr_name),
	    offsetof(dtrace_probe_t, dtpr_nextname),
	    offsetof(dtrace_probe_t, dtpr_prevname));

	if (dtrace_retain_max < 1) {
		cmn_err(CE_WARN, "illegal value (%lu) for dtrace_retain_max; "
		    "setting to 1", dtrace_retain_max);
		dtrace_retain_max = 1;
	}

	/*
	 * Now discover our toxic ranges.
	 */
	dtrace_toxic_ranges(dtrace_toxrange_add);

	/*
	 * Before we register ourselves as a provider to our own framework,
	 * we would like to assert that dtrace_provider is NULL -- but that's
	 * not true if we were loaded as a dependency of a DTrace provider.
	 * Once we've registered, we can assert that dtrace_provider is our
	 * pseudo provider.
	 */
	(void) dtrace_register("dtrace", &dtrace_provider_attr,
	    DTRACE_PRIV_NONE, 0, &dtrace_provider_ops, NULL, &id);

	ASSERT(dtrace_provider != NULL);
	ASSERT((dtrace_provider_id_t)dtrace_provider == id);

	dtrace_probeid_begin = dtrace_probe_create((dtrace_provider_id_t)
	    dtrace_provider, NULL, NULL, "BEGIN", 0, NULL);
	dtrace_probeid_end = dtrace_probe_create((dtrace_provider_id_t)
	    dtrace_provider, NULL, NULL, "END", 0, NULL);
	dtrace_probeid_error = dtrace_probe_create((dtrace_provider_id_t)
	    dtrace_provider, NULL, NULL, "ERROR", 1, NULL);

	dtrace_anon_property();
	mutex_exit(&cpu_lock);

	/*
	 * If there are already providers, we must ask them to provide their
	 * probes, and then match any anonymous enabling against them.  Note
	 * that there should be no other retained enablings at this time:
	 * the only retained enablings at this time should be the anonymous
	 * enabling.
	 */
	if (dtrace_anon.dta_enabling != NULL) {
		ASSERT(dtrace_retained == dtrace_anon.dta_enabling);

		dtrace_enabling_provide(NULL);
		state = dtrace_anon.dta_state;

		/*
		 * We couldn't hold cpu_lock across the above call to
		 * dtrace_enabling_provide(), but we must hold it to actually
		 * enable the probes.  We have to drop all of our locks, pick
		 * up cpu_lock, and regain our locks before matching the
		 * retained anonymous enabling.
		 */
		mutex_exit(&dtrace_lock);
		mutex_exit(&dtrace_provider_lock);

		mutex_enter(&cpu_lock);
		mutex_enter(&dtrace_provider_lock);
		mutex_enter(&dtrace_lock);

		if ((enab = dtrace_anon.dta_enabling) != NULL)
			(void) dtrace_enabling_match(enab, NULL);

		mutex_exit(&cpu_lock);
	}

	mutex_exit(&dtrace_lock);
	mutex_exit(&dtrace_provider_lock);

	if (state != NULL) {
		/*
		 * If we created any anonymous state, set it going now.
		 */
		(void) dtrace_state_go(state, &dtrace_anon.dta_beganon);
	}

	return (DDI_SUCCESS);
}
#endif	/* illumos */

#ifndef illumos
static void dtrace_dtr(void *);
#endif

/*ARGSUSED*/
static int
#ifdef illumos
dtrace_open(dev_t *devp, int flag, int otyp, cred_t *cred_p)
#else
dtrace_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
#endif
{
	dtrace_state_t *state;
	uint32_t priv;
	uid_t uid;
	zoneid_t zoneid;

#ifdef illumos
	if (getminor(*devp) == DTRACEMNRN_HELPER)
		return (0);

	/*
	 * If this wasn't an open with the "helper" minor, then it must be
	 * the "dtrace" minor.
	 */
	if (getminor(*devp) == DTRACEMNRN_DTRACE)
		return (ENXIO);
#else
	cred_t *cred_p = NULL;
	cred_p = dev->si_cred;

	/*
	 * If no DTRACE_PRIV_* bits are set in the credential, then the
	 * caller lacks sufficient permission to do anything with DTrace.
	 */
	dtrace_cred2priv(cred_p, &priv, &uid, &zoneid);
	if (priv == DTRACE_PRIV_NONE) {
#endif

		return (EACCES);
	}

	/*
	 * Ask all providers to provide all their probes.
	 */
	mutex_enter(&dtrace_provider_lock);
	dtrace_probe_provide(NULL, NULL);
	mutex_exit(&dtrace_provider_lock);

	mutex_enter(&cpu_lock);
	mutex_enter(&dtrace_lock);
	dtrace_opens++;
	dtrace_membar_producer();

#ifdef illumos
	/*
	 * If the kernel debugger is active (that is, if the kernel debugger
	 * modified text in some way), we won't allow the open.
	 */
	if (kdi_dtrace_set(KDI_DTSET_DTRACE_ACTIVATE) != 0) {
		dtrace_opens--;
		mutex_exit(&cpu_lock);
		mutex_exit(&dtrace_lock);
		return (EBUSY);
	}

	if (dtrace_helptrace_enable && dtrace_helptrace_buffer == NULL) {
		/*
		 * If DTrace helper tracing is enabled, we need to allocate the
		 * trace buffer and initialize the values.
		 */
		dtrace_helptrace_buffer =
		    kmem_zalloc(dtrace_helptrace_bufsize, KM_SLEEP);
		dtrace_helptrace_next = 0;
		dtrace_helptrace_wrapped = 0;
		dtrace_helptrace_enable = 0;
	}

	state = dtrace_state_create(devp, cred_p);
#else
	state = dtrace_state_create(dev, NULL);
	devfs_set_cdevpriv(state, dtrace_dtr);
#endif

	mutex_exit(&cpu_lock);

	if (state == NULL) {
#ifdef illumos
		if (--dtrace_opens == 0 && dtrace_anon.dta_enabling == NULL)
			(void) kdi_dtrace_set(KDI_DTSET_DTRACE_DEACTIVATE);
#else
		--dtrace_opens;
#endif
		mutex_exit(&dtrace_lock);
		return (EAGAIN);
	}

	mutex_exit(&dtrace_lock);

	return (0);
}

/*ARGSUSED*/
#ifdef illumos
static int
dtrace_close(dev_t dev, int flag, int otyp, cred_t *cred_p)
#else
static void
dtrace_dtr(void *data)
#endif
{
#ifdef illumos
	minor_t minor = getminor(dev);
	dtrace_state_t *state;
#endif
	dtrace_helptrace_t *buf = NULL;

#ifdef illumos
	if (minor == DTRACEMNRN_HELPER)
		return (0);

	state = ddi_get_soft_state(dtrace_softstate, minor);
#else
	dtrace_state_t *state = data;
#endif

	mutex_enter(&cpu_lock);
	mutex_enter(&dtrace_lock);

#ifdef illumos
	if (state->dts_anon)
#else
	if (state != NULL && state->dts_anon)
#endif
	{
		/*
		 * There is anonymous state. Destroy that first.
		 */
		ASSERT(dtrace_anon.dta_state == NULL);
		dtrace_state_destroy(state->dts_anon);
	}

	if (dtrace_helptrace_disable) {
		/*
		 * If we have been told to disable helper tracing, set the
		 * buffer to NULL before calling into dtrace_state_destroy();
		 * we take advantage of its dtrace_sync() to know that no
		 * CPU is in probe context with enabled helper tracing
		 * after it returns.
		 */
		buf = dtrace_helptrace_buffer;
		dtrace_helptrace_buffer = NULL;
	}

#ifdef illumos
	dtrace_state_destroy(state);
#else
	if (state != NULL) {
		dtrace_state_destroy(state);
		kmem_free(state, 0);
	}
#endif
	ASSERT(dtrace_opens > 0);

#ifdef illumos
	/*
	 * Only relinquish control of the kernel debugger interface when there
	 * are no consumers and no anonymous enablings.
	 */
	if (--dtrace_opens == 0 && dtrace_anon.dta_enabling == NULL)
		(void) kdi_dtrace_set(KDI_DTSET_DTRACE_DEACTIVATE);
#else
	--dtrace_opens;
#endif

	if (buf != NULL) {
		kmem_free(buf, dtrace_helptrace_bufsize);
		dtrace_helptrace_disable = 0;
	}

	mutex_exit(&dtrace_lock);
	mutex_exit(&cpu_lock);

#ifdef illumos
	return (0);
#endif
}

#ifdef illumos
/*ARGSUSED*/
static int
dtrace_ioctl_helper(int cmd, intptr_t arg, int *rv)
{
	int rval;
	dof_helper_t help, *dhp = NULL;

	switch (cmd) {
	case DTRACEHIOC_ADDDOF:
		if (copyin((void *)arg, &help, sizeof (help)) != 0) {
			dtrace_dof_error(NULL, "failed to copyin DOF helper");
			return (EFAULT);
		}

		dhp = &help;
		arg = (intptr_t)help.dofhp_dof;
		/*FALLTHROUGH*/

	case DTRACEHIOC_ADD: {
		dof_hdr_t *dof = dtrace_dof_copyin(arg, &rval);

		if (dof == NULL)
			return (rval);

		mutex_enter(&dtrace_lock);

		/*
		 * dtrace_helper_slurp() takes responsibility for the dof --
		 * it may free it now or it may save it and free it later.
		 */
		if ((rval = dtrace_helper_slurp(dof, dhp)) != -1) {
			*rv = rval;
			rval = 0;
		} else {
			rval = EINVAL;
		}

		mutex_exit(&dtrace_lock);
		return (rval);
	}

	case DTRACEHIOC_REMOVE: {
		mutex_enter(&dtrace_lock);
		rval = dtrace_helper_destroygen(NULL, arg);
		mutex_exit(&dtrace_lock);

		return (rval);
	}

	default:
		break;
	}

	return (ENOTTY);
}

/*ARGSUSED*/
static int
dtrace_ioctl(dev_t dev, int cmd, intptr_t arg, int md, cred_t *cr, int *rv)
{
	minor_t minor = getminor(dev);
	dtrace_state_t *state;
	int rval;

	if (minor == DTRACEMNRN_HELPER)
		return (dtrace_ioctl_helper(cmd, arg, rv));

	state = ddi_get_soft_state(dtrace_softstate, minor);

	if (state->dts_anon) {
		ASSERT(dtrace_anon.dta_state == NULL);
		state = state->dts_anon;
	}

	switch (cmd) {
	case DTRACEIOC_PROVIDER: {
		dtrace_providerdesc_t pvd;
		dtrace_provider_t *pvp;

		if (copyin((void *)arg, &pvd, sizeof (pvd)) != 0)
			return (EFAULT);

		pvd.dtvd_name[DTRACE_PROVNAMELEN - 1] = '\0';
		mutex_enter(&dtrace_provider_lock);

		for (pvp = dtrace_provider; pvp != NULL; pvp = pvp->dtpv_next) {
			if (strcmp(pvp->dtpv_name, pvd.dtvd_name) == 0)
				break;
		}

		mutex_exit(&dtrace_provider_lock);

		if (pvp == NULL)
			return (ESRCH);

		bcopy(&pvp->dtpv_priv, &pvd.dtvd_priv, sizeof (dtrace_ppriv_t));
		bcopy(&pvp->dtpv_attr, &pvd.dtvd_attr, sizeof (dtrace_pattr_t));

		if (copyout(&pvd, (void *)arg, sizeof (pvd)) != 0)
			return (EFAULT);

		return (0);
	}

	case DTRACEIOC_EPROBE: {
		dtrace_eprobedesc_t epdesc;
		dtrace_ecb_t *ecb;
		dtrace_action_t *act;
		void *buf;
		size_t size;
		uintptr_t dest;
		int nrecs;

		if (copyin((void *)arg, &epdesc, sizeof (epdesc)) != 0)
			return (EFAULT);

		mutex_enter(&dtrace_lock);

		if ((ecb = dtrace_epid2ecb(state, epdesc.dtepd_epid)) == NULL) {
			mutex_exit(&dtrace_lock);
			return (EINVAL);
		}

		if (ecb->dte_probe == NULL) {
			mutex_exit(&dtrace_lock);
			return (EINVAL);
		}

		epdesc.dtepd_probeid = ecb->dte_probe->dtpr_id;
		epdesc.dtepd_uarg = ecb->dte_uarg;
		epdesc.dtepd_size = ecb->dte_size;

		nrecs = epdesc.dtepd_nrecs;
		epdesc.dtepd_nrecs = 0;
		for (act = ecb->dte_action; act != NULL; act = act->dta_next) {
			if (DTRACEACT_ISAGG(act->dta_kind) || act->dta_intuple)
				continue;

			epdesc.dtepd_nrecs++;
		}

		/*
		 * Now that we have the size, we need to allocate a temporary
		 * buffer in which to store the complete description.  We need
		 * the temporary buffer to be able to drop dtrace_lock()
		 * across the copyout(), below.
		 */
		size = sizeof (dtrace_eprobedesc_t) +
		    (epdesc.dtepd_nrecs * sizeof (dtrace_recdesc_t));

		buf = kmem_alloc(size, KM_SLEEP);
		dest = (uintptr_t)buf;

		bcopy(&epdesc, (void *)dest, sizeof (epdesc));
		dest += offsetof(dtrace_eprobedesc_t, dtepd_rec[0]);

		for (act = ecb->dte_action; act != NULL; act = act->dta_next) {
			if (DTRACEACT_ISAGG(act->dta_kind) || act->dta_intuple)
				continue;

			if (nrecs-- == 0)
				break;

			bcopy(&act->dta_rec, (void *)dest,
			    sizeof (dtrace_recdesc_t));
			dest += sizeof (dtrace_recdesc_t);
		}

		mutex_exit(&dtrace_lock);

		if (copyout(buf, (void *)arg, dest - (uintptr_t)buf) != 0) {
			kmem_free(buf, size);
			return (EFAULT);
		}

		kmem_free(buf, size);
		return (0);
	}

	case DTRACEIOC_AGGDESC: {
		dtrace_aggdesc_t aggdesc;
		dtrace_action_t *act;
		dtrace_aggregation_t *agg;
		int nrecs;
		uint32_t offs;
		dtrace_recdesc_t *lrec;
		void *buf;
		size_t size;
		uintptr_t dest;

		if (copyin((void *)arg, &aggdesc, sizeof (aggdesc)) != 0)
			return (EFAULT);

		mutex_enter(&dtrace_lock);

		if ((agg = dtrace_aggid2agg(state, aggdesc.dtagd_id)) == NULL) {
			mutex_exit(&dtrace_lock);
			return (EINVAL);
		}

		aggdesc.dtagd_epid = agg->dtag_ecb->dte_epid;

		nrecs = aggdesc.dtagd_nrecs;
		aggdesc.dtagd_nrecs = 0;

		offs = agg->dtag_base;
		lrec = &agg->dtag_action.dta_rec;
		aggdesc.dtagd_size = lrec->dtrd_offset + lrec->dtrd_size - offs;

		for (act = agg->dtag_first; ; act = act->dta_next) {
			ASSERT(act->dta_intuple ||
			    DTRACEACT_ISAGG(act->dta_kind));

			/*
			 * If this action has a record size of zero, it
			 * denotes an argument to the aggregating action.
			 * Because the presence of this record doesn't (or
			 * shouldn't) affect the way the data is interpreted,
			 * we don't copy it out to save user-level the
			 * confusion of dealing with a zero-length record.
			 */
			if (act->dta_rec.dtrd_size == 0) {
				ASSERT(agg->dtag_hasarg);
				continue;
			}

			aggdesc.dtagd_nrecs++;

			if (act == &agg->dtag_action)
				break;
		}

		/*
		 * Now that we have the size, we need to allocate a temporary
		 * buffer in which to store the complete description.  We need
		 * the temporary buffer to be able to drop dtrace_lock()
		 * across the copyout(), below.
		 */
		size = sizeof (dtrace_aggdesc_t) +
		    (aggdesc.dtagd_nrecs * sizeof (dtrace_recdesc_t));

		buf = kmem_alloc(size, KM_SLEEP);
		dest = (uintptr_t)buf;

		bcopy(&aggdesc, (void *)dest, sizeof (aggdesc));
		dest += offsetof(dtrace_aggdesc_t, dtagd_rec[0]);

		for (act = agg->dtag_first; ; act = act->dta_next) {
			dtrace_recdesc_t rec = act->dta_rec;

			/*
			 * See the comment in the above loop for why we pass
			 * over zero-length records.
			 */
			if (rec.dtrd_size == 0) {
				ASSERT(agg->dtag_hasarg);
				continue;
			}

			if (nrecs-- == 0)
				break;

			rec.dtrd_offset -= offs;
			bcopy(&rec, (void *)dest, sizeof (rec));
			dest += sizeof (dtrace_recdesc_t);

			if (act == &agg->dtag_action)
				break;
		}

		mutex_exit(&dtrace_lock);

		if (copyout(buf, (void *)arg, dest - (uintptr_t)buf) != 0) {
			kmem_free(buf, size);
			return (EFAULT);
		}

		kmem_free(buf, size);
		return (0);
	}

	case DTRACEIOC_ENABLE: {
		dof_hdr_t *dof;
		dtrace_enabling_t *enab = NULL;
		dtrace_vstate_t *vstate;
		int err = 0;

		*rv = 0;

		/*
		 * If a NULL argument has been passed, we take this as our
		 * cue to reevaluate our enablings.
		 */
		if (arg == NULL) {
			dtrace_enabling_matchall();

			return (0);
		}

		if ((dof = dtrace_dof_copyin(arg, &rval)) == NULL)
			return (rval);

		mutex_enter(&cpu_lock);
		mutex_enter(&dtrace_lock);
		vstate = &state->dts_vstate;

		if (state->dts_activity != DTRACE_ACTIVITY_INACTIVE) {
			mutex_exit(&dtrace_lock);
			mutex_exit(&cpu_lock);
			dtrace_dof_destroy(dof);
			return (EBUSY);
		}

		if (dtrace_dof_slurp(dof, vstate, cr, &enab, 0, B_TRUE) != 0) {
			mutex_exit(&dtrace_lock);
			mutex_exit(&cpu_lock);
			dtrace_dof_destroy(dof);
			return (EINVAL);
		}

		if ((rval = dtrace_dof_options(dof, state)) != 0) {
			dtrace_enabling_destroy(enab);
			mutex_exit(&dtrace_lock);
			mutex_exit(&cpu_lock);
			dtrace_dof_destroy(dof);
			return (rval);
		}

		if ((err = dtrace_enabling_match(enab, rv)) == 0) {
			err = dtrace_enabling_retain(enab);
		} else {
			dtrace_enabling_destroy(enab);
		}

		mutex_exit(&cpu_lock);
		mutex_exit(&dtrace_lock);
		dtrace_dof_destroy(dof);

		return (err);
	}

	case DTRACEIOC_REPLICATE: {
		dtrace_repldesc_t desc;
		dtrace_probedesc_t *match = &desc.dtrpd_match;
		dtrace_probedesc_t *create = &desc.dtrpd_create;
		int err;

		if (copyin((void *)arg, &desc, sizeof (desc)) != 0)
			return (EFAULT);

		match->dtpd_provider[DTRACE_PROVNAMELEN - 1] = '\0';
		match->dtpd_mod[DTRACE_MODNAMELEN - 1] = '\0';
		match->dtpd_func[DTRACE_FUNCNAMELEN - 1] = '\0';
		match->dtpd_name[DTRACE_NAMELEN - 1] = '\0';

		create->dtpd_provider[DTRACE_PROVNAMELEN - 1] = '\0';
		create->dtpd_mod[DTRACE_MODNAMELEN - 1] = '\0';
		create->dtpd_func[DTRACE_FUNCNAMELEN - 1] = '\0';
		create->dtpd_name[DTRACE_NAMELEN - 1] = '\0';

		mutex_enter(&dtrace_lock);
		err = dtrace_enabling_replicate(state, match, create);
		mutex_exit(&dtrace_lock);

		return (err);
	}

	case DTRACEIOC_PROBEMATCH:
	case DTRACEIOC_PROBES: {
		dtrace_probe_t *probe = NULL;
		dtrace_probedesc_t desc;
		dtrace_probekey_t pkey;
		dtrace_id_t i;
		int m = 0;
		uint32_t priv;
		uid_t uid;
		zoneid_t zoneid;

		if (copyin((void *)arg, &desc, sizeof (desc)) != 0)
			return (EFAULT);

		desc.dtpd_provider[DTRACE_PROVNAMELEN - 1] = '\0';
		desc.dtpd_mod[DTRACE_MODNAMELEN - 1] = '\0';
		desc.dtpd_func[DTRACE_FUNCNAMELEN - 1] = '\0';
		desc.dtpd_name[DTRACE_NAMELEN - 1] = '\0';

		/*
		 * Before we attempt to match this probe, we want to give
		 * all providers the opportunity to provide it.
		 */
		if (desc.dtpd_id == DTRACE_IDNONE) {
			mutex_enter(&dtrace_provider_lock);
			dtrace_probe_provide(&desc, NULL);
			mutex_exit(&dtrace_provider_lock);
			desc.dtpd_id++;
		}

		if (cmd == DTRACEIOC_PROBEMATCH)  {
			dtrace_probekey(&desc, &pkey);
			pkey.dtpk_id = DTRACE_IDNONE;
		}

		dtrace_cred2priv(cr, &priv, &uid, &zoneid);

		mutex_enter(&dtrace_lock);

		if (cmd == DTRACEIOC_PROBEMATCH) {
			for (i = desc.dtpd_id; i <= dtrace_nprobes; i++) {
				if ((probe = dtrace_probes[i - 1]) != NULL &&
				    (m = dtrace_match_probe(probe, &pkey,
				    priv, uid, zoneid)) != 0)
					break;
			}

			if (m < 0) {
				mutex_exit(&dtrace_lock);
				return (EINVAL);
			}

		} else {
			for (i = desc.dtpd_id; i <= dtrace_nprobes; i++) {
				if ((probe = dtrace_probes[i - 1]) != NULL &&
				    dtrace_match_priv(probe, priv, uid, zoneid))
					break;
			}
		}

		if (probe == NULL) {
			mutex_exit(&dtrace_lock);
			return (ESRCH);
		}

		dtrace_probe_description(probe, &desc);
		mutex_exit(&dtrace_lock);

		if (copyout(&desc, (void *)arg, sizeof (desc)) != 0)
			return (EFAULT);

		return (0);
	}

	case DTRACEIOC_PROBEARG: {
		dtrace_argdesc_t desc;
		dtrace_probe_t *probe;
		dtrace_provider_t *prov;

		if (copyin((void *)arg, &desc, sizeof (desc)) != 0)
			return (EFAULT);

		if (desc.dtargd_id == DTRACE_IDNONE)
			return (EINVAL);

		if (desc.dtargd_ndx == DTRACE_ARGNONE)
			return (EINVAL);

		mutex_enter(&dtrace_provider_lock);
		mutex_enter(&mod_lock);
		mutex_enter(&dtrace_lock);

		if (desc.dtargd_id > dtrace_nprobes) {
			mutex_exit(&dtrace_lock);
			mutex_exit(&mod_lock);
			mutex_exit(&dtrace_provider_lock);
			return (EINVAL);
		}

		if ((probe = dtrace_probes[desc.dtargd_id - 1]) == NULL) {
			mutex_exit(&dtrace_lock);
			mutex_exit(&mod_lock);
			mutex_exit(&dtrace_provider_lock);
			return (EINVAL);
		}

		mutex_exit(&dtrace_lock);

		prov = probe->dtpr_provider;

		if (prov->dtpv_pops.dtps_getargdesc == NULL) {
			/*
			 * There isn't any typed information for this probe.
			 * Set the argument number to DTRACE_ARGNONE.
			 */
			desc.dtargd_ndx = DTRACE_ARGNONE;
		} else {
			desc.dtargd_native[0] = '\0';
			desc.dtargd_xlate[0] = '\0';
			desc.dtargd_mapping = desc.dtargd_ndx;

			prov->dtpv_pops.dtps_getargdesc(prov->dtpv_arg,
			    probe->dtpr_id, probe->dtpr_arg, &desc);
		}

		mutex_exit(&mod_lock);
		mutex_exit(&dtrace_provider_lock);

		if (copyout(&desc, (void *)arg, sizeof (desc)) != 0)
			return (EFAULT);

		return (0);
	}

	case DTRACEIOC_GO: {
		processorid_t cpuid;
		rval = dtrace_state_go(state, &cpuid);

		if (rval != 0)
			return (rval);

		if (copyout(&cpuid, (void *)arg, sizeof (cpuid)) != 0)
			return (EFAULT);

		return (0);
	}

	case DTRACEIOC_STOP: {
		processorid_t cpuid;

		mutex_enter(&dtrace_lock);
		rval = dtrace_state_stop(state, &cpuid);
		mutex_exit(&dtrace_lock);

		if (rval != 0)
			return (rval);

		if (copyout(&cpuid, (void *)arg, sizeof (cpuid)) != 0)
			return (EFAULT);

		return (0);
	}

	case DTRACEIOC_DOFGET: {
		dof_hdr_t hdr, *dof;
		uint64_t len;

		if (copyin((void *)arg, &hdr, sizeof (hdr)) != 0)
			return (EFAULT);

		mutex_enter(&dtrace_lock);
		dof = dtrace_dof_create(state);
		mutex_exit(&dtrace_lock);

		len = MIN(hdr.dofh_loadsz, dof->dofh_loadsz);
		rval = copyout(dof, (void *)arg, len);
		dtrace_dof_destroy(dof);

		return (rval == 0 ? 0 : EFAULT);
	}

	case DTRACEIOC_AGGSNAP:
	case DTRACEIOC_BUFSNAP: {
		dtrace_bufdesc_t desc;
		caddr_t cached;
		dtrace_buffer_t *buf;

		if (copyin((void *)arg, &desc, sizeof (desc)) != 0)
			return (EFAULT);

		if (desc.dtbd_cpu < 0 || desc.dtbd_cpu >= NCPU)
			return (EINVAL);

		mutex_enter(&dtrace_lock);

		if (cmd == DTRACEIOC_BUFSNAP) {
			buf = &state->dts_buffer[desc.dtbd_cpu];
		} else {
			buf = &state->dts_aggbuffer[desc.dtbd_cpu];
		}

		if (buf->dtb_flags & (DTRACEBUF_RING | DTRACEBUF_FILL)) {
			size_t sz = buf->dtb_offset;

			if (state->dts_activity != DTRACE_ACTIVITY_STOPPED) {
				mutex_exit(&dtrace_lock);
				return (EBUSY);
			}

			/*
			 * If this buffer has already been consumed, we're
			 * going to indicate that there's nothing left here
			 * to consume.
			 */
			if (buf->dtb_flags & DTRACEBUF_CONSUMED) {
				mutex_exit(&dtrace_lock);

				desc.dtbd_size = 0;
				desc.dtbd_drops = 0;
				desc.dtbd_errors = 0;
				desc.dtbd_oldest = 0;
				sz = sizeof (desc);

				if (copyout(&desc, (void *)arg, sz) != 0)
					return (EFAULT);

				return (0);
			}

			/*
			 * If this is a ring buffer that has wrapped, we want
			 * to copy the whole thing out.
			 */
			if (buf->dtb_flags & DTRACEBUF_WRAPPED) {
				dtrace_buffer_polish(buf);
				sz = buf->dtb_size;
			}

			if (copyout(buf->dtb_tomax, desc.dtbd_data, sz) != 0) {
				mutex_exit(&dtrace_lock);
				return (EFAULT);
			}

			desc.dtbd_size = sz;
			desc.dtbd_drops = buf->dtb_drops;
			desc.dtbd_errors = buf->dtb_errors;
			desc.dtbd_oldest = buf->dtb_xamot_offset;
			desc.dtbd_timestamp = dtrace_gethrtime();

			mutex_exit(&dtrace_lock);

			if (copyout(&desc, (void *)arg, sizeof (desc)) != 0)
				return (EFAULT);

			buf->dtb_flags |= DTRACEBUF_CONSUMED;

			return (0);
		}

		if (buf->dtb_tomax == NULL) {
			ASSERT(buf->dtb_xamot == NULL);
			mutex_exit(&dtrace_lock);
			return (ENOENT);
		}

		cached = buf->dtb_tomax;
		ASSERT(!(buf->dtb_flags & DTRACEBUF_NOSWITCH));

		dtrace_xcall(desc.dtbd_cpu,
		    (dtrace_xcall_t)dtrace_buffer_switch, buf);

		state->dts_errors += buf->dtb_xamot_errors;

		/*
		 * If the buffers did not actually switch, then the cross call
		 * did not take place -- presumably because the given CPU is
		 * not in the ready set.  If this is the case, we'll return
		 * ENOENT.
		 */
		if (buf->dtb_tomax == cached) {
			ASSERT(buf->dtb_xamot != cached);
			mutex_exit(&dtrace_lock);
			return (ENOENT);
		}

		ASSERT(cached == buf->dtb_xamot);

		/*
		 * We have our snapshot; now copy it out.
		 */
		if (copyout(buf->dtb_xamot, desc.dtbd_data,
		    buf->dtb_xamot_offset) != 0) {
			mutex_exit(&dtrace_lock);
			return (EFAULT);
		}

		desc.dtbd_size = buf->dtb_xamot_offset;
		desc.dtbd_drops = buf->dtb_xamot_drops;
		desc.dtbd_errors = buf->dtb_xamot_errors;
		desc.dtbd_oldest = 0;
		desc.dtbd_timestamp = buf->dtb_switched;

		mutex_exit(&dtrace_lock);

		/*
		 * Finally, copy out the buffer description.
		 */
		if (copyout(&desc, (void *)arg, sizeof (desc)) != 0)
			return (EFAULT);

		return (0);
	}

	case DTRACEIOC_CONF: {
		dtrace_conf_t conf;

		bzero(&conf, sizeof (conf));
		conf.dtc_difversion = DIF_VERSION;
		conf.dtc_difintregs = DIF_DIR_NREGS;
		conf.dtc_diftupregs = DIF_DTR_NREGS;
		conf.dtc_ctfmodel = CTF_MODEL_NATIVE;

		if (copyout(&conf, (void *)arg, sizeof (conf)) != 0)
			return (EFAULT);

		return (0);
	}

	case DTRACEIOC_STATUS: {
		dtrace_status_t stat;
		dtrace_dstate_t *dstate;
		int i, j;
		uint64_t nerrs;

		/*
		 * See the comment in dtrace_state_deadman() for the reason
		 * for setting dts_laststatus to INT64_MAX before setting
		 * it to the correct value.
		 */
		state->dts_laststatus = INT64_MAX;
		dtrace_membar_producer();
		state->dts_laststatus = dtrace_gethrtime();

		bzero(&stat, sizeof (stat));

		mutex_enter(&dtrace_lock);

		if (state->dts_activity == DTRACE_ACTIVITY_INACTIVE) {
			mutex_exit(&dtrace_lock);
			return (ENOENT);
		}

		if (state->dts_activity == DTRACE_ACTIVITY_DRAINING)
			stat.dtst_exiting = 1;

		nerrs = state->dts_errors;
		dstate = &state->dts_vstate.dtvs_dynvars;

		for (i = 0; i < NCPU; i++) {
			dtrace_dstate_percpu_t *dcpu = &dstate->dtds_percpu[i];

			stat.dtst_dyndrops += dcpu->dtdsc_drops;
			stat.dtst_dyndrops_dirty += dcpu->dtdsc_dirty_drops;
			stat.dtst_dyndrops_rinsing += dcpu->dtdsc_rinsing_drops;

			if (state->dts_buffer[i].dtb_flags & DTRACEBUF_FULL)
				stat.dtst_filled++;

			nerrs += state->dts_buffer[i].dtb_errors;

			for (j = 0; j < state->dts_nspeculations; j++) {
				dtrace_speculation_t *spec;
				dtrace_buffer_t *buf;

				spec = &state->dts_speculations[j];
				buf = &spec->dtsp_buffer[i];
				stat.dtst_specdrops += buf->dtb_xamot_drops;
			}
		}

		stat.dtst_specdrops_busy = state->dts_speculations_busy;
		stat.dtst_specdrops_unavail = state->dts_speculations_unavail;
		stat.dtst_stkstroverflows = state->dts_stkstroverflows;
		stat.dtst_dblerrors = state->dts_dblerrors;
		stat.dtst_killed =
		    (state->dts_activity == DTRACE_ACTIVITY_KILLED);
		stat.dtst_errors = nerrs;

		mutex_exit(&dtrace_lock);

		if (copyout(&stat, (void *)arg, sizeof (stat)) != 0)
			return (EFAULT);

		return (0);
	}

	case DTRACEIOC_FORMAT: {
		dtrace_fmtdesc_t fmt;
		char *str;
		int len;

		if (copyin((void *)arg, &fmt, sizeof (fmt)) != 0)
			return (EFAULT);

		mutex_enter(&dtrace_lock);

		if (fmt.dtfd_format == 0 ||
		    fmt.dtfd_format > state->dts_nformats) {
			mutex_exit(&dtrace_lock);
			return (EINVAL);
		}

		/*
		 * Format strings are allocated contiguously and they are
		 * never freed; if a format index is less than the number
		 * of formats, we can assert that the format map is non-NULL
		 * and that the format for the specified index is non-NULL.
		 */
		ASSERT(state->dts_formats != NULL);
		str = state->dts_formats[fmt.dtfd_format - 1];
		ASSERT(str != NULL);

		len = strlen(str) + 1;

		if (len > fmt.dtfd_length) {
			fmt.dtfd_length = len;

			if (copyout(&fmt, (void *)arg, sizeof (fmt)) != 0) {
				mutex_exit(&dtrace_lock);
				return (EINVAL);
			}
		} else {
			if (copyout(str, fmt.dtfd_string, len) != 0) {
				mutex_exit(&dtrace_lock);
				return (EINVAL);
			}
		}

		mutex_exit(&dtrace_lock);
		return (0);
	}

	default:
		break;
	}

	return (ENOTTY);
}

/*ARGSUSED*/
static int
dtrace_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	dtrace_state_t *state;

	switch (cmd) {
	case DDI_DETACH:
		break;

	case DDI_SUSPEND:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	mutex_enter(&cpu_lock);
	mutex_enter(&dtrace_provider_lock);
	mutex_enter(&dtrace_lock);

	ASSERT(dtrace_opens == 0);

	if (dtrace_helpers > 0) {
		mutex_exit(&dtrace_provider_lock);
		mutex_exit(&dtrace_lock);
		mutex_exit(&cpu_lock);
		return (DDI_FAILURE);
	}

	if (dtrace_unregister((dtrace_provider_id_t)dtrace_provider) != 0) {
		mutex_exit(&dtrace_provider_lock);
		mutex_exit(&dtrace_lock);
		mutex_exit(&cpu_lock);
		return (DDI_FAILURE);
	}

	dtrace_provider = NULL;

	if ((state = dtrace_anon_grab()) != NULL) {
		/*
		 * If there were ECBs on this state, the provider should
		 * have not been allowed to detach; assert that there is
		 * none.
		 */
		ASSERT(state->dts_necbs == 0);
		dtrace_state_destroy(state);

		/*
		 * If we're being detached with anonymous state, we need to
		 * indicate to the kernel debugger that DTrace is now inactive.
		 */
		(void) kdi_dtrace_set(KDI_DTSET_DTRACE_DEACTIVATE);
	}

	bzero(&dtrace_anon, sizeof (dtrace_anon_t));
	unregister_cpu_setup_func((cpu_setup_func_t *)dtrace_cpu_setup, NULL);
	dtrace_cpu_init = NULL;
	dtrace_helpers_cleanup = NULL;
	dtrace_helpers_fork = NULL;
	dtrace_cpustart_init = NULL;
	dtrace_cpustart_fini = NULL;
	dtrace_debugger_init = NULL;
	dtrace_debugger_fini = NULL;
	dtrace_modload = NULL;
	dtrace_modunload = NULL;

	ASSERT(dtrace_getf == 0);
	ASSERT(dtrace_closef == NULL);

	mutex_exit(&cpu_lock);

	kmem_free(dtrace_probes, dtrace_nprobes * sizeof (dtrace_probe_t *));
	dtrace_probes = NULL;
	dtrace_nprobes = 0;

	dtrace_hash_destroy(dtrace_bymod);
	dtrace_hash_destroy(dtrace_byfunc);
	dtrace_hash_destroy(dtrace_byname);
	dtrace_bymod = NULL;
	dtrace_byfunc = NULL;
	dtrace_byname = NULL;

	kmem_cache_destroy(dtrace_state_cache);
	vmem_destroy(dtrace_minor);
	vmem_destroy(dtrace_arena);

	if (dtrace_toxrange != NULL) {
		kmem_free(dtrace_toxrange,
		    dtrace_toxranges_max * sizeof (dtrace_toxrange_t));
		dtrace_toxrange = NULL;
		dtrace_toxranges = 0;
		dtrace_toxranges_max = 0;
	}

	ddi_remove_minor_node(dtrace_devi, NULL);
	dtrace_devi = NULL;

	ddi_soft_state_fini(&dtrace_softstate);

	ASSERT(dtrace_vtime_references == 0);
	ASSERT(dtrace_opens == 0);
	ASSERT(dtrace_retained == NULL);

	mutex_exit(&dtrace_lock);
	mutex_exit(&dtrace_provider_lock);

	/*
	 * We don't destroy the task queue until after we have dropped our
	 * locks (taskq_destroy() may block on running tasks).  To prevent
	 * attempting to do work after we have effectively detached but before
	 * the task queue has been destroyed, all tasks dispatched via the
	 * task queue must check that DTrace is still attached before
	 * performing any operation.
	 */
	taskq_destroy(dtrace_taskq);
	dtrace_taskq = NULL;

	return (DDI_SUCCESS);
}
#endif

#ifdef illumos
/*ARGSUSED*/
static int
dtrace_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = (void *)dtrace_devi;
		error = DDI_SUCCESS;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}
#endif

#ifdef illumos
static struct cb_ops dtrace_cb_ops = {
	dtrace_open,		/* open */
	dtrace_close,		/* close */
	nulldev,		/* strategy */
	nulldev,		/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	dtrace_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* streamtab  */
	D_NEW | D_MP		/* Driver compatibility flag */
};

static struct dev_ops dtrace_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt */
	dtrace_info,		/* get_dev_info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	dtrace_attach,		/* attach */
	dtrace_detach,		/* detach */
	nodev,			/* reset */
	&dtrace_cb_ops,		/* driver operations */
	NULL,			/* bus operations */
	nodev			/* dev power */
};

static struct modldrv modldrv = {
	&mod_driverops,		/* module type (this is a pseudo driver) */
	"Dynamic Tracing",	/* name of module */
	&dtrace_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}
#else

static d_ioctl_t	dtrace_ioctl;
static d_ioctl_t	dtrace_ioctl_helper;
static void		dtrace_load(void *);
static int		dtrace_unload(void);
static struct cdev	*dtrace_dev;
static struct cdev	*helper_dev;

void dtrace_invop_init(void);
void dtrace_invop_uninit(void);

static struct cdevsw dtrace_cdevsw = {
	.d_version	= D_VERSION,
	.d_ioctl	= dtrace_ioctl,
	.d_open		= dtrace_open,
	.d_name		= "dtrace",
};

static struct cdevsw helper_cdevsw = {
	.d_version	= D_VERSION,
	.d_ioctl	= dtrace_ioctl_helper,
	.d_name		= "helper",
};

#include <dtrace_anon.c>
#include <dtrace_ioctl.c>
#include <dtrace_load.c>
#include <dtrace_modevent.c>
#include <dtrace_sysctl.c>
#include <dtrace_unload.c>
#include <dtrace_vtime.c>
#include <dtrace_hacks.c>
#include <dtrace_isa.c>

SYSINIT(dtrace_load, SI_SUB_DTRACE, SI_ORDER_FIRST, dtrace_load, NULL);
SYSUNINIT(dtrace_unload, SI_SUB_DTRACE, SI_ORDER_FIRST, dtrace_unload, NULL);
SYSINIT(dtrace_anon_init, SI_SUB_DTRACE_ANON, SI_ORDER_FIRST, dtrace_anon_init, NULL);

DEV_MODULE(dtrace, dtrace_modevent, NULL);
MODULE_VERSION(dtrace, 1);
MODULE_DEPEND(dtrace, opensolaris, 1, 1, 1);
#endif
