/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2008 Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
 * Copyright (c) 2018 Matthew Macy
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/domainset.h>
#include <sys/eventhandler.h>
#include <sys/gtaskqueue.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>
#include <sys/pmclog.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/signalvar.h>
#include <sys/smp.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/vnode.h>

#include <sys/linker.h>		/* needs to be after <sys/malloc.h> */

#include <machine/atomic.h>
#include <machine/md_var.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

#include "hwpmc_soft.h"

#define PMC_EPOCH_ENTER() struct epoch_tracker pmc_et; epoch_enter_preempt(global_epoch_preempt, &pmc_et)
#define PMC_EPOCH_EXIT() epoch_exit_preempt(global_epoch_preempt, &pmc_et)

/*
 * Types
 */

enum pmc_flags {
	PMC_FLAG_NONE	  = 0x00, /* do nothing */
	PMC_FLAG_REMOVE   = 0x01, /* atomically remove entry from hash */
	PMC_FLAG_ALLOCATE = 0x02, /* add entry to hash if not found */
	PMC_FLAG_NOWAIT   = 0x04, /* do not wait for mallocs */
};

/*
 * The offset in sysent where the syscall is allocated.
 */

static int pmc_syscall_num = NO_SYSCALL;
struct pmc_cpu		**pmc_pcpu;	 /* per-cpu state */
pmc_value_t		*pmc_pcpu_saved; /* saved PMC values: CSW handling */

#define	PMC_PCPU_SAVED(C,R)	pmc_pcpu_saved[(R) + md->pmd_npmc*(C)]

struct mtx_pool		*pmc_mtxpool;
static int		*pmc_pmcdisp;	 /* PMC row dispositions */

#define	PMC_ROW_DISP_IS_FREE(R)		(pmc_pmcdisp[(R)] == 0)
#define	PMC_ROW_DISP_IS_THREAD(R)	(pmc_pmcdisp[(R)] > 0)
#define	PMC_ROW_DISP_IS_STANDALONE(R)	(pmc_pmcdisp[(R)] < 0)

#define	PMC_MARK_ROW_FREE(R) do {					  \
	pmc_pmcdisp[(R)] = 0;						  \
} while (0)

#define	PMC_MARK_ROW_STANDALONE(R) do {					  \
	KASSERT(pmc_pmcdisp[(R)] <= 0, ("[pmc,%d] row disposition error", \
		    __LINE__));						  \
	atomic_add_int(&pmc_pmcdisp[(R)], -1);				  \
	KASSERT(pmc_pmcdisp[(R)] >= (-pmc_cpu_max_active()),		  \
		("[pmc,%d] row disposition error", __LINE__));		  \
} while (0)

#define	PMC_UNMARK_ROW_STANDALONE(R) do { 				  \
	atomic_add_int(&pmc_pmcdisp[(R)], 1);				  \
	KASSERT(pmc_pmcdisp[(R)] <= 0, ("[pmc,%d] row disposition error", \
		    __LINE__));						  \
} while (0)

#define	PMC_MARK_ROW_THREAD(R) do {					  \
	KASSERT(pmc_pmcdisp[(R)] >= 0, ("[pmc,%d] row disposition error", \
		    __LINE__));						  \
	atomic_add_int(&pmc_pmcdisp[(R)], 1);				  \
} while (0)

#define	PMC_UNMARK_ROW_THREAD(R) do {					  \
	atomic_add_int(&pmc_pmcdisp[(R)], -1);				  \
	KASSERT(pmc_pmcdisp[(R)] >= 0, ("[pmc,%d] row disposition error", \
		    __LINE__));						  \
} while (0)


/* various event handlers */
static eventhandler_tag	pmc_exit_tag, pmc_fork_tag, pmc_kld_load_tag,
    pmc_kld_unload_tag;

/* Module statistics */
struct pmc_driverstats pmc_stats;


/* Machine/processor dependent operations */
static struct pmc_mdep  *md;

/*
 * Hash tables mapping owner processes and target threads to PMCs.
 */

struct mtx pmc_processhash_mtx;		/* spin mutex */
static u_long pmc_processhashmask;
static LIST_HEAD(pmc_processhash, pmc_process)	*pmc_processhash;

/*
 * Hash table of PMC owner descriptors.  This table is protected by
 * the shared PMC "sx" lock.
 */

static u_long pmc_ownerhashmask;
static LIST_HEAD(pmc_ownerhash, pmc_owner)	*pmc_ownerhash;

/*
 * List of PMC owners with system-wide sampling PMCs.
 */

static CK_LIST_HEAD(, pmc_owner)			pmc_ss_owners;

/*
 * List of free thread entries. This is protected by the spin
 * mutex.
 */
static struct mtx pmc_threadfreelist_mtx;	/* spin mutex */
static LIST_HEAD(, pmc_thread)			pmc_threadfreelist;
static int pmc_threadfreelist_entries=0;
#define	THREADENTRY_SIZE						\
(sizeof(struct pmc_thread) + (md->pmd_npmc * sizeof(struct pmc_threadpmcstate)))

/*
 * Task to free thread descriptors
 */
static struct grouptask free_gtask;

/*
 * A map of row indices to classdep structures.
 */
static struct pmc_classdep **pmc_rowindex_to_classdep;

/*
 * Prototypes
 */

#ifdef	HWPMC_DEBUG
static int	pmc_debugflags_sysctl_handler(SYSCTL_HANDLER_ARGS);
static int	pmc_debugflags_parse(char *newstr, char *fence);
#endif

static int	load(struct module *module, int cmd, void *arg);
static int	pmc_add_sample(ring_type_t ring, struct pmc *pm, struct trapframe *tf);
static void	pmc_add_thread_descriptors_from_proc(struct proc *p,
    struct pmc_process *pp);
static int	pmc_attach_process(struct proc *p, struct pmc *pm);
static struct pmc *pmc_allocate_pmc_descriptor(void);
static struct pmc_owner *pmc_allocate_owner_descriptor(struct proc *p);
static int	pmc_attach_one_process(struct proc *p, struct pmc *pm);
static int	pmc_can_allocate_rowindex(struct proc *p, unsigned int ri,
    int cpu);
static int	pmc_can_attach(struct pmc *pm, struct proc *p);
static void	pmc_capture_user_callchain(int cpu, int soft, struct trapframe *tf);
static void	pmc_cleanup(void);
static int	pmc_detach_process(struct proc *p, struct pmc *pm);
static int	pmc_detach_one_process(struct proc *p, struct pmc *pm,
    int flags);
static void	pmc_destroy_owner_descriptor(struct pmc_owner *po);
static void	pmc_destroy_pmc_descriptor(struct pmc *pm);
static void	pmc_destroy_process_descriptor(struct pmc_process *pp);
static struct pmc_owner *pmc_find_owner_descriptor(struct proc *p);
static int	pmc_find_pmc(pmc_id_t pmcid, struct pmc **pm);
static struct pmc *pmc_find_pmc_descriptor_in_process(struct pmc_owner *po,
    pmc_id_t pmc);
static struct pmc_process *pmc_find_process_descriptor(struct proc *p,
    uint32_t mode);
static struct pmc_thread *pmc_find_thread_descriptor(struct pmc_process *pp,
    struct thread *td, uint32_t mode);
static void	pmc_force_context_switch(void);
static void	pmc_link_target_process(struct pmc *pm,
    struct pmc_process *pp);
static void	pmc_log_all_process_mappings(struct pmc_owner *po);
static void	pmc_log_kernel_mappings(struct pmc *pm);
static void	pmc_log_process_mappings(struct pmc_owner *po, struct proc *p);
static void	pmc_maybe_remove_owner(struct pmc_owner *po);
static void	pmc_process_csw_in(struct thread *td);
static void	pmc_process_csw_out(struct thread *td);
static void	pmc_process_exit(void *arg, struct proc *p);
static void	pmc_process_fork(void *arg, struct proc *p1,
    struct proc *p2, int n);
static void	pmc_process_samples(int cpu, ring_type_t soft);
static void	pmc_release_pmc_descriptor(struct pmc *pmc);
static void	pmc_process_thread_add(struct thread *td);
static void	pmc_process_thread_delete(struct thread *td);
static void	pmc_process_thread_userret(struct thread *td);
static void	pmc_remove_owner(struct pmc_owner *po);
static void	pmc_remove_process_descriptor(struct pmc_process *pp);
static void	pmc_restore_cpu_binding(struct pmc_binding *pb);
static void	pmc_save_cpu_binding(struct pmc_binding *pb);
static void	pmc_select_cpu(int cpu);
static int	pmc_start(struct pmc *pm);
static int	pmc_stop(struct pmc *pm);
static int	pmc_syscall_handler(struct thread *td, void *syscall_args);
static struct pmc_thread *pmc_thread_descriptor_pool_alloc(void);
static void	pmc_thread_descriptor_pool_drain(void);
static void	pmc_thread_descriptor_pool_free(struct pmc_thread *pt);
static void	pmc_unlink_target_process(struct pmc *pmc,
    struct pmc_process *pp);
static int generic_switch_in(struct pmc_cpu *pc, struct pmc_process *pp);
static int generic_switch_out(struct pmc_cpu *pc, struct pmc_process *pp);
static struct pmc_mdep *pmc_generic_cpu_initialize(void);
static void pmc_generic_cpu_finalize(struct pmc_mdep *md);
static void pmc_post_callchain_callback(void);
static void pmc_process_threadcreate(struct thread *td);
static void pmc_process_threadexit(struct thread *td);
static void pmc_process_proccreate(struct proc *p);
static void pmc_process_allproc(struct pmc *pm);

/*
 * Kernel tunables and sysctl(8) interface.
 */

SYSCTL_DECL(_kern_hwpmc);
SYSCTL_NODE(_kern_hwpmc, OID_AUTO, stats, CTLFLAG_RW, 0, "HWPMC stats");


/* Stats. */
SYSCTL_COUNTER_U64(_kern_hwpmc_stats, OID_AUTO, intr_ignored, CTLFLAG_RW,
				   &pmc_stats.pm_intr_ignored, "# of interrupts ignored");
SYSCTL_COUNTER_U64(_kern_hwpmc_stats, OID_AUTO, intr_processed, CTLFLAG_RW,
				   &pmc_stats.pm_intr_processed, "# of interrupts processed");
SYSCTL_COUNTER_U64(_kern_hwpmc_stats, OID_AUTO, intr_bufferfull, CTLFLAG_RW,
				   &pmc_stats.pm_intr_bufferfull, "# of interrupts where buffer was full");
SYSCTL_COUNTER_U64(_kern_hwpmc_stats, OID_AUTO, syscalls, CTLFLAG_RW,
				   &pmc_stats.pm_syscalls, "# of syscalls");
SYSCTL_COUNTER_U64(_kern_hwpmc_stats, OID_AUTO, syscall_errors, CTLFLAG_RW,
				   &pmc_stats.pm_syscall_errors, "# of syscall_errors");
SYSCTL_COUNTER_U64(_kern_hwpmc_stats, OID_AUTO, buffer_requests, CTLFLAG_RW,
				   &pmc_stats.pm_buffer_requests, "# of buffer requests");
SYSCTL_COUNTER_U64(_kern_hwpmc_stats, OID_AUTO, buffer_requests_failed, CTLFLAG_RW,
				   &pmc_stats.pm_buffer_requests_failed, "# of buffer requests which failed");
SYSCTL_COUNTER_U64(_kern_hwpmc_stats, OID_AUTO, log_sweeps, CTLFLAG_RW,
				   &pmc_stats.pm_log_sweeps, "# of ?");
SYSCTL_COUNTER_U64(_kern_hwpmc_stats, OID_AUTO, merges, CTLFLAG_RW,
				   &pmc_stats.pm_merges, "# of times kernel stack was found for user trace");
SYSCTL_COUNTER_U64(_kern_hwpmc_stats, OID_AUTO, overwrites, CTLFLAG_RW,
				   &pmc_stats.pm_overwrites, "# of times a sample was overwritten before being logged");

static int pmc_callchaindepth = PMC_CALLCHAIN_DEPTH;
SYSCTL_INT(_kern_hwpmc, OID_AUTO, callchaindepth, CTLFLAG_RDTUN,
    &pmc_callchaindepth, 0, "depth of call chain records");

char pmc_cpuid[64];
SYSCTL_STRING(_kern_hwpmc, OID_AUTO, cpuid, CTLFLAG_RD,
	pmc_cpuid, 0, "cpu version string");
#ifdef	HWPMC_DEBUG
struct pmc_debugflags pmc_debugflags = PMC_DEBUG_DEFAULT_FLAGS;
char	pmc_debugstr[PMC_DEBUG_STRSIZE];
TUNABLE_STR(PMC_SYSCTL_NAME_PREFIX "debugflags", pmc_debugstr,
    sizeof(pmc_debugstr));
SYSCTL_PROC(_kern_hwpmc, OID_AUTO, debugflags,
    CTLTYPE_STRING | CTLFLAG_RWTUN | CTLFLAG_NOFETCH,
    0, 0, pmc_debugflags_sysctl_handler, "A", "debug flags");
#endif


/*
 * kern.hwpmc.hashrows -- determines the number of rows in the
 * of the hash table used to look up threads
 */

static int pmc_hashsize = PMC_HASH_SIZE;
SYSCTL_INT(_kern_hwpmc, OID_AUTO, hashsize, CTLFLAG_RDTUN,
    &pmc_hashsize, 0, "rows in hash tables");

/*
 * kern.hwpmc.nsamples --- number of PC samples/callchain stacks per CPU
 */

static int pmc_nsamples = PMC_NSAMPLES;
SYSCTL_INT(_kern_hwpmc, OID_AUTO, nsamples, CTLFLAG_RDTUN,
    &pmc_nsamples, 0, "number of PC samples per CPU");

static uint64_t pmc_sample_mask = PMC_NSAMPLES-1;

/*
 * kern.hwpmc.mtxpoolsize -- number of mutexes in the mutex pool.
 */

static int pmc_mtxpool_size = PMC_MTXPOOL_SIZE;
SYSCTL_INT(_kern_hwpmc, OID_AUTO, mtxpoolsize, CTLFLAG_RDTUN,
    &pmc_mtxpool_size, 0, "size of spin mutex pool");


/*
 * kern.hwpmc.threadfreelist_entries -- number of free entries
 */

SYSCTL_INT(_kern_hwpmc, OID_AUTO, threadfreelist_entries, CTLFLAG_RD,
    &pmc_threadfreelist_entries, 0, "number of avalable thread entries");


/*
 * kern.hwpmc.threadfreelist_max -- maximum number of free entries
 */

static int pmc_threadfreelist_max = PMC_THREADLIST_MAX;
SYSCTL_INT(_kern_hwpmc, OID_AUTO, threadfreelist_max, CTLFLAG_RW,
    &pmc_threadfreelist_max, 0,
    "maximum number of available thread entries before freeing some");


/*
 * security.bsd.unprivileged_syspmcs -- allow non-root processes to
 * allocate system-wide PMCs.
 *
 * Allowing unprivileged processes to allocate system PMCs is convenient
 * if system-wide measurements need to be taken concurrently with other
 * per-process measurements.  This feature is turned off by default.
 */

static int pmc_unprivileged_syspmcs = 0;
SYSCTL_INT(_security_bsd, OID_AUTO, unprivileged_syspmcs, CTLFLAG_RWTUN,
    &pmc_unprivileged_syspmcs, 0,
    "allow unprivileged process to allocate system PMCs");

/*
 * Hash function.  Discard the lower 2 bits of the pointer since
 * these are always zero for our uses.  The hash multiplier is
 * round((2^LONG_BIT) * ((sqrt(5)-1)/2)).
 */

#if	LONG_BIT == 64
#define	_PMC_HM		11400714819323198486u
#elif	LONG_BIT == 32
#define	_PMC_HM		2654435769u
#else
#error 	Must know the size of 'long' to compile
#endif

#define	PMC_HASH_PTR(P,M)	((((unsigned long) (P) >> 2) * _PMC_HM) & (M))

/*
 * Syscall structures
 */

/* The `sysent' for the new syscall */
static struct sysent pmc_sysent = {
	.sy_narg =	2,
	.sy_call =	pmc_syscall_handler,
};

static struct syscall_module_data pmc_syscall_mod = {
	.chainevh =	load,
	.chainarg =	NULL,
	.offset =	&pmc_syscall_num,
	.new_sysent =	&pmc_sysent,
	.old_sysent =	{ .sy_narg = 0, .sy_call = NULL },
	.flags =	SY_THR_STATIC_KLD,
};

static moduledata_t pmc_mod = {
	.name =		PMC_MODULE_NAME,
	.evhand =	syscall_module_handler,
	.priv =		&pmc_syscall_mod,
};

#ifdef EARLY_AP_STARTUP
DECLARE_MODULE(pmc, pmc_mod, SI_SUB_SYSCALLS, SI_ORDER_ANY);
#else
DECLARE_MODULE(pmc, pmc_mod, SI_SUB_SMP, SI_ORDER_ANY);
#endif
MODULE_VERSION(pmc, PMC_VERSION);

#ifdef	HWPMC_DEBUG
enum pmc_dbgparse_state {
	PMCDS_WS,		/* in whitespace */
	PMCDS_MAJOR,		/* seen a major keyword */
	PMCDS_MINOR
};

static int
pmc_debugflags_parse(char *newstr, char *fence)
{
	char c, *p, *q;
	struct pmc_debugflags *tmpflags;
	int error, found, *newbits, tmp;
	size_t kwlen;

	tmpflags = malloc(sizeof(*tmpflags), M_PMC, M_WAITOK|M_ZERO);

	p = newstr;
	error = 0;

	for (; p < fence && (c = *p); p++) {

		/* skip white space */
		if (c == ' ' || c == '\t')
			continue;

		/* look for a keyword followed by "=" */
		for (q = p; p < fence && (c = *p) && c != '='; p++)
			;
		if (c != '=') {
			error = EINVAL;
			goto done;
		}

		kwlen = p - q;
		newbits = NULL;

		/* lookup flag group name */
#define	DBG_SET_FLAG_MAJ(S,F)						\
		if (kwlen == sizeof(S)-1 && strncmp(q, S, kwlen) == 0)	\
			newbits = &tmpflags->pdb_ ## F;

		DBG_SET_FLAG_MAJ("cpu",		CPU);
		DBG_SET_FLAG_MAJ("csw",		CSW);
		DBG_SET_FLAG_MAJ("logging",	LOG);
		DBG_SET_FLAG_MAJ("module",	MOD);
		DBG_SET_FLAG_MAJ("md", 		MDP);
		DBG_SET_FLAG_MAJ("owner",	OWN);
		DBG_SET_FLAG_MAJ("pmc",		PMC);
		DBG_SET_FLAG_MAJ("process",	PRC);
		DBG_SET_FLAG_MAJ("sampling", 	SAM);

		if (newbits == NULL) {
			error = EINVAL;
			goto done;
		}

		p++;		/* skip the '=' */

		/* Now parse the individual flags */
		tmp = 0;
	newflag:
		for (q = p; p < fence && (c = *p); p++)
			if (c == ' ' || c == '\t' || c == ',')
				break;

		/* p == fence or c == ws or c == "," or c == 0 */

		if ((kwlen = p - q) == 0) {
			*newbits = tmp;
			continue;
		}

		found = 0;
#define	DBG_SET_FLAG_MIN(S,F)						\
		if (kwlen == sizeof(S)-1 && strncmp(q, S, kwlen) == 0)	\
			tmp |= found = (1 << PMC_DEBUG_MIN_ ## F)

		/* a '*' denotes all possible flags in the group */
		if (kwlen == 1 && *q == '*')
			tmp = found = ~0;
		/* look for individual flag names */
		DBG_SET_FLAG_MIN("allocaterow", ALR);
		DBG_SET_FLAG_MIN("allocate",	ALL);
		DBG_SET_FLAG_MIN("attach",	ATT);
		DBG_SET_FLAG_MIN("bind",	BND);
		DBG_SET_FLAG_MIN("config",	CFG);
		DBG_SET_FLAG_MIN("exec",	EXC);
		DBG_SET_FLAG_MIN("exit",	EXT);
		DBG_SET_FLAG_MIN("find",	FND);
		DBG_SET_FLAG_MIN("flush",	FLS);
		DBG_SET_FLAG_MIN("fork",	FRK);
		DBG_SET_FLAG_MIN("getbuf",	GTB);
		DBG_SET_FLAG_MIN("hook",	PMH);
		DBG_SET_FLAG_MIN("init",	INI);
		DBG_SET_FLAG_MIN("intr",	INT);
		DBG_SET_FLAG_MIN("linktarget",	TLK);
		DBG_SET_FLAG_MIN("mayberemove", OMR);
		DBG_SET_FLAG_MIN("ops",		OPS);
		DBG_SET_FLAG_MIN("read",	REA);
		DBG_SET_FLAG_MIN("register",	REG);
		DBG_SET_FLAG_MIN("release",	REL);
		DBG_SET_FLAG_MIN("remove",	ORM);
		DBG_SET_FLAG_MIN("sample",	SAM);
		DBG_SET_FLAG_MIN("scheduleio",	SIO);
		DBG_SET_FLAG_MIN("select",	SEL);
		DBG_SET_FLAG_MIN("signal",	SIG);
		DBG_SET_FLAG_MIN("swi",		SWI);
		DBG_SET_FLAG_MIN("swo",		SWO);
		DBG_SET_FLAG_MIN("start",	STA);
		DBG_SET_FLAG_MIN("stop",	STO);
		DBG_SET_FLAG_MIN("syscall",	PMS);
		DBG_SET_FLAG_MIN("unlinktarget", TUL);
		DBG_SET_FLAG_MIN("write",	WRI);
		if (found == 0) {
			/* unrecognized flag name */
			error = EINVAL;
			goto done;
		}

		if (c == 0 || c == ' ' || c == '\t') {	/* end of flag group */
			*newbits = tmp;
			continue;
		}

		p++;
		goto newflag;
	}

	/* save the new flag set */
	bcopy(tmpflags, &pmc_debugflags, sizeof(pmc_debugflags));

 done:
	free(tmpflags, M_PMC);
	return error;
}

static int
pmc_debugflags_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	char *fence, *newstr;
	int error;
	unsigned int n;

	(void) arg1; (void) arg2; /* unused parameters */

	n = sizeof(pmc_debugstr);
	newstr = malloc(n, M_PMC, M_WAITOK|M_ZERO);
	(void) strlcpy(newstr, pmc_debugstr, n);

	error = sysctl_handle_string(oidp, newstr, n, req);

	/* if there is a new string, parse and copy it */
	if (error == 0 && req->newptr != NULL) {
		fence = newstr + (n < req->newlen ? n : req->newlen + 1);
		if ((error = pmc_debugflags_parse(newstr, fence)) == 0)
			(void) strlcpy(pmc_debugstr, newstr,
			    sizeof(pmc_debugstr));
	}

	free(newstr, M_PMC);

	return error;
}
#endif

/*
 * Map a row index to a classdep structure and return the adjusted row
 * index for the PMC class index.
 */
static struct pmc_classdep *
pmc_ri_to_classdep(struct pmc_mdep *md, int ri, int *adjri)
{
	struct pmc_classdep *pcd;

	(void) md;

	KASSERT(ri >= 0 && ri < md->pmd_npmc,
	    ("[pmc,%d] illegal row-index %d", __LINE__, ri));

	pcd = pmc_rowindex_to_classdep[ri];

	KASSERT(pcd != NULL,
	    ("[pmc,%d] ri %d null pcd", __LINE__, ri));

	*adjri = ri - pcd->pcd_ri;

	KASSERT(*adjri >= 0 && *adjri < pcd->pcd_num,
	    ("[pmc,%d] adjusted row-index %d", __LINE__, *adjri));

	return (pcd);
}

/*
 * Concurrency Control
 *
 * The driver manages the following data structures:
 *
 *   - target process descriptors, one per target process
 *   - owner process descriptors (and attached lists), one per owner process
 *   - lookup hash tables for owner and target processes
 *   - PMC descriptors (and attached lists)
 *   - per-cpu hardware state
 *   - the 'hook' variable through which the kernel calls into
 *     this module
 *   - the machine hardware state (managed by the MD layer)
 *
 * These data structures are accessed from:
 *
 * - thread context-switch code
 * - interrupt handlers (possibly on multiple cpus)
 * - kernel threads on multiple cpus running on behalf of user
 *   processes doing system calls
 * - this driver's private kernel threads
 *
 * = Locks and Locking strategy =
 *
 * The driver uses four locking strategies for its operation:
 *
 * - The global SX lock "pmc_sx" is used to protect internal
 *   data structures.
 *
 *   Calls into the module by syscall() start with this lock being
 *   held in exclusive mode.  Depending on the requested operation,
 *   the lock may be downgraded to 'shared' mode to allow more
 *   concurrent readers into the module.  Calls into the module from
 *   other parts of the kernel acquire the lock in shared mode.
 *
 *   This SX lock is held in exclusive mode for any operations that
 *   modify the linkages between the driver's internal data structures.
 *
 *   The 'pmc_hook' function pointer is also protected by this lock.
 *   It is only examined with the sx lock held in exclusive mode.  The
 *   kernel module is allowed to be unloaded only with the sx lock held
 *   in exclusive mode.  In normal syscall handling, after acquiring the
 *   pmc_sx lock we first check that 'pmc_hook' is non-null before
 *   proceeding.  This prevents races between the thread unloading the module
 *   and other threads seeking to use the module.
 *
 * - Lookups of target process structures and owner process structures
 *   cannot use the global "pmc_sx" SX lock because these lookups need
 *   to happen during context switches and in other critical sections
 *   where sleeping is not allowed.  We protect these lookup tables
 *   with their own private spin-mutexes, "pmc_processhash_mtx" and
 *   "pmc_ownerhash_mtx".
 *
 * - Interrupt handlers work in a lock free manner.  At interrupt
 *   time, handlers look at the PMC pointer (phw->phw_pmc) configured
 *   when the PMC was started.  If this pointer is NULL, the interrupt
 *   is ignored after updating driver statistics.  We ensure that this
 *   pointer is set (using an atomic operation if necessary) before the
 *   PMC hardware is started.  Conversely, this pointer is unset atomically
 *   only after the PMC hardware is stopped.
 *
 *   We ensure that everything needed for the operation of an
 *   interrupt handler is available without it needing to acquire any
 *   locks.  We also ensure that a PMC's software state is destroyed only
 *   after the PMC is taken off hardware (on all CPUs).
 *
 * - Context-switch handling with process-private PMCs needs more
 *   care.
 *
 *   A given process may be the target of multiple PMCs.  For example,
 *   PMCATTACH and PMCDETACH may be requested by a process on one CPU
 *   while the target process is running on another.  A PMC could also
 *   be getting released because its owner is exiting.  We tackle
 *   these situations in the following manner:
 *
 *   - each target process structure 'pmc_process' has an array
 *     of 'struct pmc *' pointers, one for each hardware PMC.
 *
 *   - At context switch IN time, each "target" PMC in RUNNING state
 *     gets started on hardware and a pointer to each PMC is copied into
 *     the per-cpu phw array.  The 'runcount' for the PMC is
 *     incremented.
 *
 *   - At context switch OUT time, all process-virtual PMCs are stopped
 *     on hardware.  The saved value is added to the PMCs value field
 *     only if the PMC is in a non-deleted state (the PMCs state could
 *     have changed during the current time slice).
 *
 *     Note that since in-between a switch IN on a processor and a switch
 *     OUT, the PMC could have been released on another CPU.  Therefore
 *     context switch OUT always looks at the hardware state to turn
 *     OFF PMCs and will update a PMC's saved value only if reachable
 *     from the target process record.
 *
 *   - OP PMCRELEASE could be called on a PMC at any time (the PMC could
 *     be attached to many processes at the time of the call and could
 *     be active on multiple CPUs).
 *
 *     We prevent further scheduling of the PMC by marking it as in
 *     state 'DELETED'.  If the runcount of the PMC is non-zero then
 *     this PMC is currently running on a CPU somewhere.  The thread
 *     doing the PMCRELEASE operation waits by repeatedly doing a
 *     pause() till the runcount comes to zero.
 *
 * The contents of a PMC descriptor (struct pmc) are protected using
 * a spin-mutex.  In order to save space, we use a mutex pool.
 *
 * In terms of lock types used by witness(4), we use:
 * - Type "pmc-sx", used by the global SX lock.
 * - Type "pmc-sleep", for sleep mutexes used by logger threads.
 * - Type "pmc-per-proc", for protecting PMC owner descriptors.
 * - Type "pmc-leaf", used for all other spin mutexes.
 */

/*
 * save the cpu binding of the current kthread
 */

static void
pmc_save_cpu_binding(struct pmc_binding *pb)
{
	PMCDBG0(CPU,BND,2, "save-cpu");
	thread_lock(curthread);
	pb->pb_bound = sched_is_bound(curthread);
	pb->pb_cpu   = curthread->td_oncpu;
	thread_unlock(curthread);
	PMCDBG1(CPU,BND,2, "save-cpu cpu=%d", pb->pb_cpu);
}

/*
 * restore the cpu binding of the current thread
 */

static void
pmc_restore_cpu_binding(struct pmc_binding *pb)
{
	PMCDBG2(CPU,BND,2, "restore-cpu curcpu=%d restore=%d",
	    curthread->td_oncpu, pb->pb_cpu);
	thread_lock(curthread);
	if (pb->pb_bound)
		sched_bind(curthread, pb->pb_cpu);
	else
		sched_unbind(curthread);
	thread_unlock(curthread);
	PMCDBG0(CPU,BND,2, "restore-cpu done");
}

/*
 * move execution over the specified cpu and bind it there.
 */

static void
pmc_select_cpu(int cpu)
{
	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[pmc,%d] bad cpu number %d", __LINE__, cpu));

	/* Never move to an inactive CPU. */
	KASSERT(pmc_cpu_is_active(cpu), ("[pmc,%d] selecting inactive "
	    "CPU %d", __LINE__, cpu));

	PMCDBG1(CPU,SEL,2, "select-cpu cpu=%d", cpu);
	thread_lock(curthread);
	sched_bind(curthread, cpu);
	thread_unlock(curthread);

	KASSERT(curthread->td_oncpu == cpu,
	    ("[pmc,%d] CPU not bound [cpu=%d, curr=%d]", __LINE__,
		cpu, curthread->td_oncpu));

	PMCDBG1(CPU,SEL,2, "select-cpu cpu=%d ok", cpu);
}

/*
 * Force a context switch.
 *
 * We do this by pause'ing for 1 tick -- invoking mi_switch() is not
 * guaranteed to force a context switch.
 */

static void
pmc_force_context_switch(void)
{

	pause("pmcctx", 1);
}

uint64_t
pmc_rdtsc(void)
{
#if defined(__i386__) || defined(__amd64__)
	if (__predict_true(amd_feature & AMDID_RDTSCP))
		return rdtscp();
	else
		return rdtsc();
#else
	return get_cyclecount();
#endif
}

/*
 * Get the file name for an executable.  This is a simple wrapper
 * around vn_fullpath(9).
 */

static void
pmc_getfilename(struct vnode *v, char **fullpath, char **freepath)
{

	*fullpath = "unknown";
	*freepath = NULL;
	vn_fullpath(curthread, v, fullpath, freepath);
}

/*
 * remove an process owning PMCs
 */

void
pmc_remove_owner(struct pmc_owner *po)
{
	struct pmc *pm, *tmp;

	sx_assert(&pmc_sx, SX_XLOCKED);

	PMCDBG1(OWN,ORM,1, "remove-owner po=%p", po);

	/* Remove descriptor from the owner hash table */
	LIST_REMOVE(po, po_next);

	/* release all owned PMC descriptors */
	LIST_FOREACH_SAFE(pm, &po->po_pmcs, pm_next, tmp) {
		PMCDBG1(OWN,ORM,2, "pmc=%p", pm);
		KASSERT(pm->pm_owner == po,
		    ("[pmc,%d] owner %p != po %p", __LINE__, pm->pm_owner, po));

		pmc_release_pmc_descriptor(pm);	/* will unlink from the list */
		pmc_destroy_pmc_descriptor(pm);
	}

	KASSERT(po->po_sscount == 0,
	    ("[pmc,%d] SS count not zero", __LINE__));
	KASSERT(LIST_EMPTY(&po->po_pmcs),
	    ("[pmc,%d] PMC list not empty", __LINE__));

	/* de-configure the log file if present */
	if (po->po_flags & PMC_PO_OWNS_LOGFILE)
		pmclog_deconfigure_log(po);
}

/*
 * remove an owner process record if all conditions are met.
 */

static void
pmc_maybe_remove_owner(struct pmc_owner *po)
{

	PMCDBG1(OWN,OMR,1, "maybe-remove-owner po=%p", po);

	/*
	 * Remove owner record if
	 * - this process does not own any PMCs
	 * - this process has not allocated a system-wide sampling buffer
	 */

	if (LIST_EMPTY(&po->po_pmcs) &&
	    ((po->po_flags & PMC_PO_OWNS_LOGFILE) == 0)) {
		pmc_remove_owner(po);
		pmc_destroy_owner_descriptor(po);
	}
}

/*
 * Add an association between a target process and a PMC.
 */

static void
pmc_link_target_process(struct pmc *pm, struct pmc_process *pp)
{
	int ri;
	struct pmc_target *pt;
#ifdef INVARIANTS
	struct pmc_thread *pt_td;
#endif

	sx_assert(&pmc_sx, SX_XLOCKED);

	KASSERT(pm != NULL && pp != NULL,
	    ("[pmc,%d] Null pm %p or pp %p", __LINE__, pm, pp));
	KASSERT(PMC_IS_VIRTUAL_MODE(PMC_TO_MODE(pm)),
	    ("[pmc,%d] Attaching a non-process-virtual pmc=%p to pid=%d",
		__LINE__, pm, pp->pp_proc->p_pid));
	KASSERT(pp->pp_refcnt >= 0 && pp->pp_refcnt <= ((int) md->pmd_npmc - 1),
	    ("[pmc,%d] Illegal reference count %d for process record %p",
		__LINE__, pp->pp_refcnt, (void *) pp));

	ri = PMC_TO_ROWINDEX(pm);

	PMCDBG3(PRC,TLK,1, "link-target pmc=%p ri=%d pmc-process=%p",
	    pm, ri, pp);

#ifdef	HWPMC_DEBUG
	LIST_FOREACH(pt, &pm->pm_targets, pt_next)
	    if (pt->pt_process == pp)
		    KASSERT(0, ("[pmc,%d] pp %p already in pmc %p targets",
				__LINE__, pp, pm));
#endif

	pt = malloc(sizeof(struct pmc_target), M_PMC, M_WAITOK|M_ZERO);
	pt->pt_process = pp;

	LIST_INSERT_HEAD(&pm->pm_targets, pt, pt_next);

	atomic_store_rel_ptr((uintptr_t *)&pp->pp_pmcs[ri].pp_pmc,
	    (uintptr_t)pm);

	if (pm->pm_owner->po_owner == pp->pp_proc)
		pm->pm_flags |= PMC_F_ATTACHED_TO_OWNER;

	/*
	 * Initialize the per-process values at this row index.
	 */
	pp->pp_pmcs[ri].pp_pmcval = PMC_TO_MODE(pm) == PMC_MODE_TS ?
	    pm->pm_sc.pm_reloadcount : 0;

	pp->pp_refcnt++;

#ifdef INVARIANTS
	/* Confirm that the per-thread values at this row index are cleared. */
	if (PMC_TO_MODE(pm) == PMC_MODE_TS) {
		mtx_lock_spin(pp->pp_tdslock);
		LIST_FOREACH(pt_td, &pp->pp_tds, pt_next) {
			KASSERT(pt_td->pt_pmcs[ri].pt_pmcval == (pmc_value_t) 0,
			    ("[pmc,%d] pt_pmcval not cleared for pid=%d at "
			    "ri=%d", __LINE__, pp->pp_proc->p_pid, ri));
		}
		mtx_unlock_spin(pp->pp_tdslock);
	}
#endif
}

/*
 * Removes the association between a target process and a PMC.
 */

static void
pmc_unlink_target_process(struct pmc *pm, struct pmc_process *pp)
{
	int ri;
	struct proc *p;
	struct pmc_target *ptgt;
	struct pmc_thread *pt;

	sx_assert(&pmc_sx, SX_XLOCKED);

	KASSERT(pm != NULL && pp != NULL,
	    ("[pmc,%d] Null pm %p or pp %p", __LINE__, pm, pp));

	KASSERT(pp->pp_refcnt >= 1 && pp->pp_refcnt <= (int) md->pmd_npmc,
	    ("[pmc,%d] Illegal ref count %d on process record %p",
		__LINE__, pp->pp_refcnt, (void *) pp));

	ri = PMC_TO_ROWINDEX(pm);

	PMCDBG3(PRC,TUL,1, "unlink-target pmc=%p ri=%d pmc-process=%p",
	    pm, ri, pp);

	KASSERT(pp->pp_pmcs[ri].pp_pmc == pm,
	    ("[pmc,%d] PMC ri %d mismatch pmc %p pp->[ri] %p", __LINE__,
		ri, pm, pp->pp_pmcs[ri].pp_pmc));

	pp->pp_pmcs[ri].pp_pmc = NULL;
	pp->pp_pmcs[ri].pp_pmcval = (pmc_value_t) 0;

	/* Clear the per-thread values at this row index. */
	if (PMC_TO_MODE(pm) == PMC_MODE_TS) {
		mtx_lock_spin(pp->pp_tdslock);
		LIST_FOREACH(pt, &pp->pp_tds, pt_next)
			pt->pt_pmcs[ri].pt_pmcval = (pmc_value_t) 0;
		mtx_unlock_spin(pp->pp_tdslock);
	}

	/* Remove owner-specific flags */
	if (pm->pm_owner->po_owner == pp->pp_proc) {
		pp->pp_flags &= ~PMC_PP_ENABLE_MSR_ACCESS;
		pm->pm_flags &= ~PMC_F_ATTACHED_TO_OWNER;
	}

	pp->pp_refcnt--;

	/* Remove the target process from the PMC structure */
	LIST_FOREACH(ptgt, &pm->pm_targets, pt_next)
		if (ptgt->pt_process == pp)
			break;

	KASSERT(ptgt != NULL, ("[pmc,%d] process %p (pp: %p) not found "
		    "in pmc %p", __LINE__, pp->pp_proc, pp, pm));

	LIST_REMOVE(ptgt, pt_next);
	free(ptgt, M_PMC);

	/* if the PMC now lacks targets, send the owner a SIGIO */
	if (LIST_EMPTY(&pm->pm_targets)) {
		p = pm->pm_owner->po_owner;
		PROC_LOCK(p);
		kern_psignal(p, SIGIO);
		PROC_UNLOCK(p);

		PMCDBG2(PRC,SIG,2, "signalling proc=%p signal=%d", p,
		    SIGIO);
	}
}

/*
 * Check if PMC 'pm' may be attached to target process 't'.
 */

static int
pmc_can_attach(struct pmc *pm, struct proc *t)
{
	struct proc *o;		/* pmc owner */
	struct ucred *oc, *tc;	/* owner, target credentials */
	int decline_attach, i;

	/*
	 * A PMC's owner can always attach that PMC to itself.
	 */

	if ((o = pm->pm_owner->po_owner) == t)
		return 0;

	PROC_LOCK(o);
	oc = o->p_ucred;
	crhold(oc);
	PROC_UNLOCK(o);

	PROC_LOCK(t);
	tc = t->p_ucred;
	crhold(tc);
	PROC_UNLOCK(t);

	/*
	 * The effective uid of the PMC owner should match at least one
	 * of the {effective,real,saved} uids of the target process.
	 */

	decline_attach = oc->cr_uid != tc->cr_uid &&
	    oc->cr_uid != tc->cr_svuid &&
	    oc->cr_uid != tc->cr_ruid;

	/*
	 * Every one of the target's group ids, must be in the owner's
	 * group list.
	 */
	for (i = 0; !decline_attach && i < tc->cr_ngroups; i++)
		decline_attach = !groupmember(tc->cr_groups[i], oc);

	/* check the read and saved gids too */
	if (decline_attach == 0)
		decline_attach = !groupmember(tc->cr_rgid, oc) ||
		    !groupmember(tc->cr_svgid, oc);

	crfree(tc);
	crfree(oc);

	return !decline_attach;
}

/*
 * Attach a process to a PMC.
 */

static int
pmc_attach_one_process(struct proc *p, struct pmc *pm)
{
	int ri, error;
	char *fullpath, *freepath;
	struct pmc_process	*pp;

	sx_assert(&pmc_sx, SX_XLOCKED);

	PMCDBG5(PRC,ATT,2, "attach-one pm=%p ri=%d proc=%p (%d, %s)", pm,
	    PMC_TO_ROWINDEX(pm), p, p->p_pid, p->p_comm);

	/*
	 * Locate the process descriptor corresponding to process 'p',
	 * allocating space as needed.
	 *
	 * Verify that rowindex 'pm_rowindex' is free in the process
	 * descriptor.
	 *
	 * If not, allocate space for a descriptor and link the
	 * process descriptor and PMC.
	 */
	ri = PMC_TO_ROWINDEX(pm);

	/* mark process as using HWPMCs */
	PROC_LOCK(p);
	p->p_flag |= P_HWPMC;
	PROC_UNLOCK(p);

	if ((pp = pmc_find_process_descriptor(p, PMC_FLAG_ALLOCATE)) == NULL) {
		error = ENOMEM;
		goto fail;
	}

	if (pp->pp_pmcs[ri].pp_pmc == pm) {/* already present at slot [ri] */
		error = EEXIST;
		goto fail;
	}

	if (pp->pp_pmcs[ri].pp_pmc != NULL) {
		error = EBUSY;
		goto fail;
	}

	pmc_link_target_process(pm, pp);

	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)) &&
	    (pm->pm_flags & PMC_F_ATTACHED_TO_OWNER) == 0)
		pm->pm_flags |= PMC_F_NEEDS_LOGFILE;

	pm->pm_flags |= PMC_F_ATTACH_DONE; /* mark as attached */

	/* issue an attach event to a configured log file */
	if (pm->pm_owner->po_flags & PMC_PO_OWNS_LOGFILE) {
		if (p->p_flag & P_KPROC) {
			fullpath = kernelname;
			freepath = NULL;
		} else {
			pmc_getfilename(p->p_textvp, &fullpath, &freepath);
			pmclog_process_pmcattach(pm, p->p_pid, fullpath);
		}
		free(freepath, M_TEMP);
		if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
			pmc_log_process_mappings(pm->pm_owner, p);
	}

	return (0);
 fail:
	PROC_LOCK(p);
	p->p_flag &= ~P_HWPMC;
	PROC_UNLOCK(p);
	return (error);
}

/*
 * Attach a process and optionally its children
 */

static int
pmc_attach_process(struct proc *p, struct pmc *pm)
{
	int error;
	struct proc *top;

	sx_assert(&pmc_sx, SX_XLOCKED);

	PMCDBG5(PRC,ATT,1, "attach pm=%p ri=%d proc=%p (%d, %s)", pm,
	    PMC_TO_ROWINDEX(pm), p, p->p_pid, p->p_comm);


	/*
	 * If this PMC successfully allowed a GETMSR operation
	 * in the past, disallow further ATTACHes.
	 */

	if ((pm->pm_flags & PMC_PP_ENABLE_MSR_ACCESS) != 0)
		return EPERM;

	if ((pm->pm_flags & PMC_F_DESCENDANTS) == 0)
		return pmc_attach_one_process(p, pm);

	/*
	 * Traverse all child processes, attaching them to
	 * this PMC.
	 */

	sx_slock(&proctree_lock);

	top = p;

	for (;;) {
		if ((error = pmc_attach_one_process(p, pm)) != 0)
			break;
		if (!LIST_EMPTY(&p->p_children))
			p = LIST_FIRST(&p->p_children);
		else for (;;) {
			if (p == top)
				goto done;
			if (LIST_NEXT(p, p_sibling)) {
				p = LIST_NEXT(p, p_sibling);
				break;
			}
			p = p->p_pptr;
		}
	}

	if (error)
		(void) pmc_detach_process(top, pm);

 done:
	sx_sunlock(&proctree_lock);
	return error;
}

/*
 * Detach a process from a PMC.  If there are no other PMCs tracking
 * this process, remove the process structure from its hash table.  If
 * 'flags' contains PMC_FLAG_REMOVE, then free the process structure.
 */

static int
pmc_detach_one_process(struct proc *p, struct pmc *pm, int flags)
{
	int ri;
	struct pmc_process *pp;

	sx_assert(&pmc_sx, SX_XLOCKED);

	KASSERT(pm != NULL,
	    ("[pmc,%d] null pm pointer", __LINE__));

	ri = PMC_TO_ROWINDEX(pm);

	PMCDBG6(PRC,ATT,2, "detach-one pm=%p ri=%d proc=%p (%d, %s) flags=0x%x",
	    pm, ri, p, p->p_pid, p->p_comm, flags);

	if ((pp = pmc_find_process_descriptor(p, 0)) == NULL)
		return ESRCH;

	if (pp->pp_pmcs[ri].pp_pmc != pm)
		return EINVAL;

	pmc_unlink_target_process(pm, pp);

	/* Issue a detach entry if a log file is configured */
	if (pm->pm_owner->po_flags & PMC_PO_OWNS_LOGFILE)
		pmclog_process_pmcdetach(pm, p->p_pid);

	/*
	 * If there are no PMCs targeting this process, we remove its
	 * descriptor from the target hash table and unset the P_HWPMC
	 * flag in the struct proc.
	 */
	KASSERT(pp->pp_refcnt >= 0 && pp->pp_refcnt <= (int) md->pmd_npmc,
	    ("[pmc,%d] Illegal refcnt %d for process struct %p",
		__LINE__, pp->pp_refcnt, pp));

	if (pp->pp_refcnt != 0)	/* still a target of some PMC */
		return 0;

	pmc_remove_process_descriptor(pp);

	if (flags & PMC_FLAG_REMOVE)
		pmc_destroy_process_descriptor(pp);

	PROC_LOCK(p);
	p->p_flag &= ~P_HWPMC;
	PROC_UNLOCK(p);

	return 0;
}

/*
 * Detach a process and optionally its descendants from a PMC.
 */

static int
pmc_detach_process(struct proc *p, struct pmc *pm)
{
	struct proc *top;

	sx_assert(&pmc_sx, SX_XLOCKED);

	PMCDBG5(PRC,ATT,1, "detach pm=%p ri=%d proc=%p (%d, %s)", pm,
	    PMC_TO_ROWINDEX(pm), p, p->p_pid, p->p_comm);

	if ((pm->pm_flags & PMC_F_DESCENDANTS) == 0)
		return pmc_detach_one_process(p, pm, PMC_FLAG_REMOVE);

	/*
	 * Traverse all children, detaching them from this PMC.  We
	 * ignore errors since we could be detaching a PMC from a
	 * partially attached proc tree.
	 */

	sx_slock(&proctree_lock);

	top = p;

	for (;;) {
		(void) pmc_detach_one_process(p, pm, PMC_FLAG_REMOVE);

		if (!LIST_EMPTY(&p->p_children))
			p = LIST_FIRST(&p->p_children);
		else for (;;) {
			if (p == top)
				goto done;
			if (LIST_NEXT(p, p_sibling)) {
				p = LIST_NEXT(p, p_sibling);
				break;
			}
			p = p->p_pptr;
		}
	}

 done:
	sx_sunlock(&proctree_lock);

	if (LIST_EMPTY(&pm->pm_targets))
		pm->pm_flags &= ~PMC_F_ATTACH_DONE;

	return 0;
}


/*
 * Thread context switch IN
 */

static void
pmc_process_csw_in(struct thread *td)
{
	int cpu;
	unsigned int adjri, ri;
	struct pmc *pm;
	struct proc *p;
	struct pmc_cpu *pc;
	struct pmc_hw *phw;
	pmc_value_t newvalue;
	struct pmc_process *pp;
	struct pmc_thread *pt;
	struct pmc_classdep *pcd;

	p = td->td_proc;
	pt = NULL;
	if ((pp = pmc_find_process_descriptor(p, PMC_FLAG_NONE)) == NULL)
		return;

	KASSERT(pp->pp_proc == td->td_proc,
	    ("[pmc,%d] not my thread state", __LINE__));

	critical_enter(); /* no preemption from this point */

	cpu = PCPU_GET(cpuid); /* td->td_oncpu is invalid */

	PMCDBG5(CSW,SWI,1, "cpu=%d proc=%p (%d, %s) pp=%p", cpu, p,
	    p->p_pid, p->p_comm, pp);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[pmc,%d] weird CPU id %d", __LINE__, cpu));

	pc = pmc_pcpu[cpu];

	for (ri = 0; ri < md->pmd_npmc; ri++) {

		if ((pm = pp->pp_pmcs[ri].pp_pmc) == NULL)
			continue;

		KASSERT(PMC_IS_VIRTUAL_MODE(PMC_TO_MODE(pm)),
		    ("[pmc,%d] Target PMC in non-virtual mode (%d)",
			__LINE__, PMC_TO_MODE(pm)));

		KASSERT(PMC_TO_ROWINDEX(pm) == ri,
		    ("[pmc,%d] Row index mismatch pmc %d != ri %d",
			__LINE__, PMC_TO_ROWINDEX(pm), ri));

		/*
		 * Only PMCs that are marked as 'RUNNING' need
		 * be placed on hardware.
		 */

		if (pm->pm_state != PMC_STATE_RUNNING)
			continue;

		KASSERT(counter_u64_fetch(pm->pm_runcount) >= 0,
	    ("[pmc,%d] pm=%p runcount %ld", __LINE__, (void *) pm,
		 (unsigned long)counter_u64_fetch(pm->pm_runcount)));

		/* increment PMC runcount */
		counter_u64_add(pm->pm_runcount, 1);

		/* configure the HWPMC we are going to use. */
		pcd = pmc_ri_to_classdep(md, ri, &adjri);
		pcd->pcd_config_pmc(cpu, adjri, pm);

		phw = pc->pc_hwpmcs[ri];

		KASSERT(phw != NULL,
		    ("[pmc,%d] null hw pointer", __LINE__));

		KASSERT(phw->phw_pmc == pm,
		    ("[pmc,%d] hw->pmc %p != pmc %p", __LINE__,
			phw->phw_pmc, pm));

		/*
		 * Write out saved value and start the PMC.
		 *
		 * Sampling PMCs use a per-thread value, while
		 * counting mode PMCs use a per-pmc value that is
		 * inherited across descendants.
		 */
		if (PMC_TO_MODE(pm) == PMC_MODE_TS) {
			if (pt == NULL)
				pt = pmc_find_thread_descriptor(pp, td,
				    PMC_FLAG_NONE);

			KASSERT(pt != NULL,
			    ("[pmc,%d] No thread found for td=%p", __LINE__,
			    td));

			mtx_pool_lock_spin(pmc_mtxpool, pm);

			/*
			 * If we have a thread descriptor, use the per-thread
			 * counter in the descriptor. If not, we will use
			 * a per-process counter. 
			 *
			 * TODO: Remove the per-process "safety net" once
			 * we have thoroughly tested that we don't hit the
			 * above assert.
			 */
			if (pt != NULL) {
				if (pt->pt_pmcs[ri].pt_pmcval > 0)
					newvalue = pt->pt_pmcs[ri].pt_pmcval;
				else
					newvalue = pm->pm_sc.pm_reloadcount;
			} else {
				/*
				 * Use the saved value calculated after the most
				 * recent time a thread using the shared counter
				 * switched out. Reset the saved count in case
				 * another thread from this process switches in
				 * before any threads switch out.
				 */

				newvalue = pp->pp_pmcs[ri].pp_pmcval;
				pp->pp_pmcs[ri].pp_pmcval =
				    pm->pm_sc.pm_reloadcount;
			}
			mtx_pool_unlock_spin(pmc_mtxpool, pm);
			KASSERT(newvalue > 0 && newvalue <=
			    pm->pm_sc.pm_reloadcount,
			    ("[pmc,%d] pmcval outside of expected range cpu=%d "
			    "ri=%d pmcval=%jx pm_reloadcount=%jx", __LINE__,
			    cpu, ri, newvalue, pm->pm_sc.pm_reloadcount));
		} else {
			KASSERT(PMC_TO_MODE(pm) == PMC_MODE_TC,
			    ("[pmc,%d] illegal mode=%d", __LINE__,
			    PMC_TO_MODE(pm)));
			mtx_pool_lock_spin(pmc_mtxpool, pm);
			newvalue = PMC_PCPU_SAVED(cpu, ri) =
			    pm->pm_gv.pm_savedvalue;
			mtx_pool_unlock_spin(pmc_mtxpool, pm);
		}

		PMCDBG3(CSW,SWI,1,"cpu=%d ri=%d new=%jd", cpu, ri, newvalue);

		pcd->pcd_write_pmc(cpu, adjri, newvalue);

		/* If a sampling mode PMC, reset stalled state. */
		if (PMC_TO_MODE(pm) == PMC_MODE_TS)
			pm->pm_pcpu_state[cpu].pps_stalled = 0;

		/* Indicate that we desire this to run. */
		pm->pm_pcpu_state[cpu].pps_cpustate = 1;

		/* Start the PMC. */
		pcd->pcd_start_pmc(cpu, adjri);
	}

	/*
	 * perform any other architecture/cpu dependent thread
	 * switch-in actions.
	 */

	(void) (*md->pmd_switch_in)(pc, pp);

	critical_exit();

}

/*
 * Thread context switch OUT.
 */

static void
pmc_process_csw_out(struct thread *td)
{
	int cpu;
	int64_t tmp;
	struct pmc *pm;
	struct proc *p;
	enum pmc_mode mode;
	struct pmc_cpu *pc;
	pmc_value_t newvalue;
	unsigned int adjri, ri;
	struct pmc_process *pp;
	struct pmc_thread *pt = NULL;
	struct pmc_classdep *pcd;


	/*
	 * Locate our process descriptor; this may be NULL if
	 * this process is exiting and we have already removed
	 * the process from the target process table.
	 *
	 * Note that due to kernel preemption, multiple
	 * context switches may happen while the process is
	 * exiting.
	 *
	 * Note also that if the target process cannot be
	 * found we still need to deconfigure any PMCs that
	 * are currently running on hardware.
	 */

	p = td->td_proc;
	pp = pmc_find_process_descriptor(p, PMC_FLAG_NONE);

	/*
	 * save PMCs
	 */

	critical_enter();

	cpu = PCPU_GET(cpuid); /* td->td_oncpu is invalid */

	PMCDBG5(CSW,SWO,1, "cpu=%d proc=%p (%d, %s) pp=%p", cpu, p,
	    p->p_pid, p->p_comm, pp);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[pmc,%d weird CPU id %d", __LINE__, cpu));

	pc = pmc_pcpu[cpu];

	/*
	 * When a PMC gets unlinked from a target PMC, it will
	 * be removed from the target's pp_pmc[] array.
	 *
	 * However, on a MP system, the target could have been
	 * executing on another CPU at the time of the unlink.
	 * So, at context switch OUT time, we need to look at
	 * the hardware to determine if a PMC is scheduled on
	 * it.
	 */

	for (ri = 0; ri < md->pmd_npmc; ri++) {

		pcd = pmc_ri_to_classdep(md, ri, &adjri);
		pm  = NULL;
		(void) (*pcd->pcd_get_config)(cpu, adjri, &pm);

		if (pm == NULL)	/* nothing at this row index */
			continue;

		mode = PMC_TO_MODE(pm);
		if (!PMC_IS_VIRTUAL_MODE(mode))
			continue; /* not a process virtual PMC */

		KASSERT(PMC_TO_ROWINDEX(pm) == ri,
		    ("[pmc,%d] ri mismatch pmc(%d) ri(%d)",
			__LINE__, PMC_TO_ROWINDEX(pm), ri));

		/*
		 * Change desired state, and then stop if not stalled.
		 * This two-step dance should avoid race conditions where
		 * an interrupt re-enables the PMC after this code has
		 * already checked the pm_stalled flag.
		 */
		pm->pm_pcpu_state[cpu].pps_cpustate = 0;
		if (pm->pm_pcpu_state[cpu].pps_stalled == 0)
			pcd->pcd_stop_pmc(cpu, adjri);

		KASSERT(counter_u64_fetch(pm->pm_runcount) > 0,
			("[pmc,%d] pm=%p runcount %ld", __LINE__, (void *) pm,
			 (unsigned long)counter_u64_fetch(pm->pm_runcount)));

		/* reduce this PMC's runcount */
		counter_u64_add(pm->pm_runcount, -1);

		/*
		 * If this PMC is associated with this process,
		 * save the reading.
		 */

		if (pm->pm_state != PMC_STATE_DELETED && pp != NULL &&
		    pp->pp_pmcs[ri].pp_pmc != NULL) {
			KASSERT(pm == pp->pp_pmcs[ri].pp_pmc,
			    ("[pmc,%d] pm %p != pp_pmcs[%d] %p", __LINE__,
				pm, ri, pp->pp_pmcs[ri].pp_pmc));

			KASSERT(pp->pp_refcnt > 0,
			    ("[pmc,%d] pp refcnt = %d", __LINE__,
				pp->pp_refcnt));

			pcd->pcd_read_pmc(cpu, adjri, &newvalue);

			if (mode == PMC_MODE_TS) {
				PMCDBG3(CSW,SWO,1,"cpu=%d ri=%d val=%jd (samp)",
				    cpu, ri, newvalue);

				if (pt == NULL)
					pt = pmc_find_thread_descriptor(pp, td,
					    PMC_FLAG_NONE);

				KASSERT(pt != NULL,
				    ("[pmc,%d] No thread found for td=%p",
				    __LINE__, td));

				mtx_pool_lock_spin(pmc_mtxpool, pm);

				/*
				 * If we have a thread descriptor, save the
				 * per-thread counter in the descriptor. If not,
				 * we will update the per-process counter.
				 *
				 * TODO: Remove the per-process "safety net"
				 * once we have thoroughly tested that we
				 * don't hit the above assert.
				 */
				if (pt != NULL)
					pt->pt_pmcs[ri].pt_pmcval = newvalue;
				else {
					/*
					 * For sampling process-virtual PMCs,
					 * newvalue is the number of events to
					 * be seen until the next sampling
					 * interrupt. We can just add the events
					 * left from this invocation to the
					 * counter, then adjust in case we
					 * overflow our range.
					 *
					 * (Recall that we reload the counter
					 * every time we use it.)
					 */
					pp->pp_pmcs[ri].pp_pmcval += newvalue;
					if (pp->pp_pmcs[ri].pp_pmcval >
					    pm->pm_sc.pm_reloadcount)
						pp->pp_pmcs[ri].pp_pmcval -=
						    pm->pm_sc.pm_reloadcount;
				}
				mtx_pool_unlock_spin(pmc_mtxpool, pm);
			} else {
				tmp = newvalue - PMC_PCPU_SAVED(cpu,ri);

				PMCDBG3(CSW,SWO,1,"cpu=%d ri=%d tmp=%jd (count)",
				    cpu, ri, tmp);

				/*
				 * For counting process-virtual PMCs,
				 * we expect the count to be
				 * increasing monotonically, modulo a 64
				 * bit wraparound.
				 */
				KASSERT(tmp >= 0,
				    ("[pmc,%d] negative increment cpu=%d "
				     "ri=%d newvalue=%jx saved=%jx "
				     "incr=%jx", __LINE__, cpu, ri,
				     newvalue, PMC_PCPU_SAVED(cpu,ri), tmp));

				mtx_pool_lock_spin(pmc_mtxpool, pm);
				pm->pm_gv.pm_savedvalue += tmp;
				pp->pp_pmcs[ri].pp_pmcval += tmp;
				mtx_pool_unlock_spin(pmc_mtxpool, pm);

				if (pm->pm_flags & PMC_F_LOG_PROCCSW)
					pmclog_process_proccsw(pm, pp, tmp, td);
			}
		}

		/* mark hardware as free */
		pcd->pcd_config_pmc(cpu, adjri, NULL);
	}

	/*
	 * perform any other architecture/cpu dependent thread
	 * switch out functions.
	 */

	(void) (*md->pmd_switch_out)(pc, pp);

	critical_exit();
}

/*
 * A new thread for a process.
 */
static void
pmc_process_thread_add(struct thread *td)
{
	struct pmc_process *pmc;

	pmc = pmc_find_process_descriptor(td->td_proc, PMC_FLAG_NONE);
	if (pmc != NULL)
		pmc_find_thread_descriptor(pmc, td, PMC_FLAG_ALLOCATE);
}

/*
 * A thread delete for a process.
 */
static void
pmc_process_thread_delete(struct thread *td)
{
	struct pmc_process *pmc;

	pmc = pmc_find_process_descriptor(td->td_proc, PMC_FLAG_NONE);
	if (pmc != NULL)
		pmc_thread_descriptor_pool_free(pmc_find_thread_descriptor(pmc,
		    td, PMC_FLAG_REMOVE));
}

/*
 * A userret() call for a thread.
 */
static void
pmc_process_thread_userret(struct thread *td)
{
	sched_pin();
	pmc_capture_user_callchain(curcpu, PMC_UR, td->td_frame);
	sched_unpin();
}

/*
 * A mapping change for a process.
 */

static void
pmc_process_mmap(struct thread *td, struct pmckern_map_in *pkm)
{
	int ri;
	pid_t pid;
	char *fullpath, *freepath;
	const struct pmc *pm;
	struct pmc_owner *po;
	const struct pmc_process *pp;

	freepath = fullpath = NULL;
	MPASS(!in_epoch(global_epoch_preempt));
	pmc_getfilename((struct vnode *) pkm->pm_file, &fullpath, &freepath);

	pid = td->td_proc->p_pid;

	PMC_EPOCH_ENTER();
	/* Inform owners of all system-wide sampling PMCs. */
	CK_LIST_FOREACH(po, &pmc_ss_owners, po_ssnext)
	    if (po->po_flags & PMC_PO_OWNS_LOGFILE)
			pmclog_process_map_in(po, pid, pkm->pm_address, fullpath);

	if ((pp = pmc_find_process_descriptor(td->td_proc, 0)) == NULL)
		goto done;

	/*
	 * Inform sampling PMC owners tracking this process.
	 */
	for (ri = 0; ri < md->pmd_npmc; ri++)
		if ((pm = pp->pp_pmcs[ri].pp_pmc) != NULL &&
		    PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
			pmclog_process_map_in(pm->pm_owner,
			    pid, pkm->pm_address, fullpath);

  done:
	if (freepath)
		free(freepath, M_TEMP);
	PMC_EPOCH_EXIT();
}


/*
 * Log an munmap request.
 */

static void
pmc_process_munmap(struct thread *td, struct pmckern_map_out *pkm)
{
	int ri;
	pid_t pid;
	struct pmc_owner *po;
	const struct pmc *pm;
	const struct pmc_process *pp;

	pid = td->td_proc->p_pid;

	PMC_EPOCH_ENTER();
	CK_LIST_FOREACH(po, &pmc_ss_owners, po_ssnext)
	    if (po->po_flags & PMC_PO_OWNS_LOGFILE)
		pmclog_process_map_out(po, pid, pkm->pm_address,
		    pkm->pm_address + pkm->pm_size);
	PMC_EPOCH_EXIT();

	if ((pp = pmc_find_process_descriptor(td->td_proc, 0)) == NULL)
		return;

	for (ri = 0; ri < md->pmd_npmc; ri++)
		if ((pm = pp->pp_pmcs[ri].pp_pmc) != NULL &&
		    PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
			pmclog_process_map_out(pm->pm_owner, pid,
			    pkm->pm_address, pkm->pm_address + pkm->pm_size);
}

/*
 * Log mapping information about the kernel.
 */

static void
pmc_log_kernel_mappings(struct pmc *pm)
{
	struct pmc_owner *po;
	struct pmckern_map_in *km, *kmbase;

	MPASS(in_epoch(global_epoch_preempt) || sx_xlocked(&pmc_sx));
	KASSERT(PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)),
	    ("[pmc,%d] non-sampling PMC (%p) desires mapping information",
		__LINE__, (void *) pm));

	po = pm->pm_owner;

	if (po->po_flags & PMC_PO_INITIAL_MAPPINGS_DONE)
		return;
	if (PMC_TO_MODE(pm) == PMC_MODE_SS)
		pmc_process_allproc(pm);
	/*
	 * Log the current set of kernel modules.
	 */
	kmbase = linker_hwpmc_list_objects();
	for (km = kmbase; km->pm_file != NULL; km++) {
		PMCDBG2(LOG,REG,1,"%s %p", (char *) km->pm_file,
		    (void *) km->pm_address);
		pmclog_process_map_in(po, (pid_t) -1, km->pm_address,
		    km->pm_file);
	}
	free(kmbase, M_LINKER);

	po->po_flags |= PMC_PO_INITIAL_MAPPINGS_DONE;
}

/*
 * Log the mappings for a single process.
 */

static void
pmc_log_process_mappings(struct pmc_owner *po, struct proc *p)
{
	vm_map_t map;
	struct vnode *vp;
	struct vmspace *vm;
	vm_map_entry_t entry;
	vm_offset_t last_end;
	u_int last_timestamp;
	struct vnode *last_vp;
	vm_offset_t start_addr;
	vm_object_t obj, lobj, tobj;
	char *fullpath, *freepath;

	last_vp = NULL;
	last_end = (vm_offset_t) 0;
	fullpath = freepath = NULL;

	if ((vm = vmspace_acquire_ref(p)) == NULL)
		return;

	map = &vm->vm_map;
	vm_map_lock_read(map);

	for (entry = map->header.next; entry != &map->header; entry = entry->next) {

		if (entry == NULL) {
			PMCDBG2(LOG,OPS,2, "hwpmc: vm_map entry unexpectedly "
			    "NULL! pid=%d vm_map=%p\n", p->p_pid, map);
			break;
		}

		/*
		 * We only care about executable map entries.
		 */
		if ((entry->eflags & MAP_ENTRY_IS_SUB_MAP) ||
		    !(entry->protection & VM_PROT_EXECUTE) ||
		    (entry->object.vm_object == NULL)) {
			continue;
		}

		obj = entry->object.vm_object;
		VM_OBJECT_RLOCK(obj);

		/* 
		 * Walk the backing_object list to find the base
		 * (non-shadowed) vm_object.
		 */
		for (lobj = tobj = obj; tobj != NULL; tobj = tobj->backing_object) {
			if (tobj != obj)
				VM_OBJECT_RLOCK(tobj);
			if (lobj != obj)
				VM_OBJECT_RUNLOCK(lobj);
			lobj = tobj;
		}

		/*
		 * At this point lobj is the base vm_object and it is locked.
		 */
		if (lobj == NULL) {
			PMCDBG3(LOG,OPS,2, "hwpmc: lobj unexpectedly NULL! pid=%d "
			    "vm_map=%p vm_obj=%p\n", p->p_pid, map, obj);
			VM_OBJECT_RUNLOCK(obj);
			continue;
		}

		vp = vm_object_vnode(lobj);
		if (vp == NULL) {
			if (lobj != obj)
				VM_OBJECT_RUNLOCK(lobj);
			VM_OBJECT_RUNLOCK(obj);
			continue;
		}

		/*
		 * Skip contiguous regions that point to the same
		 * vnode, so we don't emit redundant MAP-IN
		 * directives.
		 */
		if (entry->start == last_end && vp == last_vp) {
			last_end = entry->end;
			if (lobj != obj)
				VM_OBJECT_RUNLOCK(lobj);
			VM_OBJECT_RUNLOCK(obj);
			continue;
		}

		/* 
		 * We don't want to keep the proc's vm_map or this
		 * vm_object locked while we walk the pathname, since
		 * vn_fullpath() can sleep.  However, if we drop the
		 * lock, it's possible for concurrent activity to
		 * modify the vm_map list.  To protect against this,
		 * we save the vm_map timestamp before we release the
		 * lock, and check it after we reacquire the lock
		 * below.
		 */
		start_addr = entry->start;
		last_end = entry->end;
		last_timestamp = map->timestamp;
		vm_map_unlock_read(map);

		vref(vp);
		if (lobj != obj)
			VM_OBJECT_RUNLOCK(lobj);

		VM_OBJECT_RUNLOCK(obj);

		freepath = NULL;
		pmc_getfilename(vp, &fullpath, &freepath);
		last_vp = vp;

		vrele(vp);

		vp = NULL;
		pmclog_process_map_in(po, p->p_pid, start_addr, fullpath);
		if (freepath)
			free(freepath, M_TEMP);

		vm_map_lock_read(map);

		/*
		 * If our saved timestamp doesn't match, this means
		 * that the vm_map was modified out from under us and
		 * we can't trust our current "entry" pointer.  Do a
		 * new lookup for this entry.  If there is no entry
		 * for this address range, vm_map_lookup_entry() will
		 * return the previous one, so we always want to go to
		 * entry->next on the next loop iteration.
		 * 
		 * There is an edge condition here that can occur if
		 * there is no entry at or before this address.  In
		 * this situation, vm_map_lookup_entry returns
		 * &map->header, which would cause our loop to abort
		 * without processing the rest of the map.  However,
		 * in practice this will never happen for process
		 * vm_map.  This is because the executable's text
		 * segment is the first mapping in the proc's address
		 * space, and this mapping is never removed until the
		 * process exits, so there will always be a non-header
		 * entry at or before the requested address for
		 * vm_map_lookup_entry to return.
		 */
		if (map->timestamp != last_timestamp)
			vm_map_lookup_entry(map, last_end - 1, &entry);
	}

	vm_map_unlock_read(map);
	vmspace_free(vm);
	return;
}

/*
 * Log mappings for all processes in the system.
 */

static void
pmc_log_all_process_mappings(struct pmc_owner *po)
{
	struct proc *p, *top;

	sx_assert(&pmc_sx, SX_XLOCKED);

	if ((p = pfind(1)) == NULL)
		panic("[pmc,%d] Cannot find init", __LINE__);

	PROC_UNLOCK(p);

	sx_slock(&proctree_lock);

	top = p;

	for (;;) {
		pmc_log_process_mappings(po, p);
		if (!LIST_EMPTY(&p->p_children))
			p = LIST_FIRST(&p->p_children);
		else for (;;) {
			if (p == top)
				goto done;
			if (LIST_NEXT(p, p_sibling)) {
				p = LIST_NEXT(p, p_sibling);
				break;
			}
			p = p->p_pptr;
		}
	}
 done:
	sx_sunlock(&proctree_lock);
}

/*
 * The 'hook' invoked from the kernel proper
 */


#ifdef	HWPMC_DEBUG
const char *pmc_hooknames[] = {
	/* these strings correspond to PMC_FN_* in <sys/pmckern.h> */
	"",
	"EXEC",
	"CSW-IN",
	"CSW-OUT",
	"SAMPLE",
	"UNUSED1",
	"UNUSED2",
	"MMAP",
	"MUNMAP",
	"CALLCHAIN-NMI",
	"CALLCHAIN-SOFT",
	"SOFTSAMPLING",
	"THR-CREATE",
	"THR-EXIT",
	"THR-USERRET",
	"THR-CREATE-LOG",
	"THR-EXIT-LOG",
	"PROC-CREATE-LOG"
};
#endif

static int
pmc_hook_handler(struct thread *td, int function, void *arg)
{
	int cpu;

	PMCDBG4(MOD,PMH,1, "hook td=%p func=%d \"%s\" arg=%p", td, function,
	    pmc_hooknames[function], arg);

	switch (function)
	{

	/*
	 * Process exec()
	 */

	case PMC_FN_PROCESS_EXEC:
	{
		char *fullpath, *freepath;
		unsigned int ri;
		int is_using_hwpmcs;
		struct pmc *pm;
		struct proc *p;
		struct pmc_owner *po;
		struct pmc_process *pp;
		struct pmckern_procexec *pk;

		sx_assert(&pmc_sx, SX_XLOCKED);

		p = td->td_proc;
		pmc_getfilename(p->p_textvp, &fullpath, &freepath);

		pk = (struct pmckern_procexec *) arg;

		PMC_EPOCH_ENTER();
		/* Inform owners of SS mode PMCs of the exec event. */
		CK_LIST_FOREACH(po, &pmc_ss_owners, po_ssnext)
		    if (po->po_flags & PMC_PO_OWNS_LOGFILE)
			    pmclog_process_procexec(po, PMC_ID_INVALID,
				p->p_pid, pk->pm_entryaddr, fullpath);
		PMC_EPOCH_EXIT();

		PROC_LOCK(p);
		is_using_hwpmcs = p->p_flag & P_HWPMC;
		PROC_UNLOCK(p);

		if (!is_using_hwpmcs) {
			if (freepath)
				free(freepath, M_TEMP);
			break;
		}

		/*
		 * PMCs are not inherited across an exec():  remove any
		 * PMCs that this process is the owner of.
		 */

		if ((po = pmc_find_owner_descriptor(p)) != NULL) {
			pmc_remove_owner(po);
			pmc_destroy_owner_descriptor(po);
		}

		/*
		 * If the process being exec'ed is not the target of any
		 * PMC, we are done.
		 */
		if ((pp = pmc_find_process_descriptor(p, 0)) == NULL) {
			if (freepath)
				free(freepath, M_TEMP);
			break;
		}

		/*
		 * Log the exec event to all monitoring owners.  Skip
		 * owners who have already received the event because
		 * they had system sampling PMCs active.
		 */
		for (ri = 0; ri < md->pmd_npmc; ri++)
			if ((pm = pp->pp_pmcs[ri].pp_pmc) != NULL) {
				po = pm->pm_owner;
				if (po->po_sscount == 0 &&
				    po->po_flags & PMC_PO_OWNS_LOGFILE)
					pmclog_process_procexec(po, pm->pm_id,
					    p->p_pid, pk->pm_entryaddr,
					    fullpath);
			}

		if (freepath)
			free(freepath, M_TEMP);


		PMCDBG4(PRC,EXC,1, "exec proc=%p (%d, %s) cred-changed=%d",
		    p, p->p_pid, p->p_comm, pk->pm_credentialschanged);

		if (pk->pm_credentialschanged == 0) /* no change */
			break;

		/*
		 * If the newly exec()'ed process has a different credential
		 * than before, allow it to be the target of a PMC only if
		 * the PMC's owner has sufficient privilege.
		 */

		for (ri = 0; ri < md->pmd_npmc; ri++)
			if ((pm = pp->pp_pmcs[ri].pp_pmc) != NULL)
				if (pmc_can_attach(pm, td->td_proc) != 0)
					pmc_detach_one_process(td->td_proc,
					    pm, PMC_FLAG_NONE);

		KASSERT(pp->pp_refcnt >= 0 && pp->pp_refcnt <= (int) md->pmd_npmc,
		    ("[pmc,%d] Illegal ref count %d on pp %p", __LINE__,
			pp->pp_refcnt, pp));

		/*
		 * If this process is no longer the target of any
		 * PMCs, we can remove the process entry and free
		 * up space.
		 */

		if (pp->pp_refcnt == 0) {
			pmc_remove_process_descriptor(pp);
			pmc_destroy_process_descriptor(pp);
			break;
		}

	}
	break;

	case PMC_FN_CSW_IN:
		pmc_process_csw_in(td);
		break;

	case PMC_FN_CSW_OUT:
		pmc_process_csw_out(td);
		break;

	/*
	 * Process accumulated PC samples.
	 *
	 * This function is expected to be called by hardclock() for
	 * each CPU that has accumulated PC samples.
	 *
	 * This function is to be executed on the CPU whose samples
	 * are being processed.
	 */
	case PMC_FN_DO_SAMPLES:

		/*
		 * Clear the cpu specific bit in the CPU mask before
		 * do the rest of the processing.  If the NMI handler
		 * gets invoked after the "atomic_clear_int()" call
		 * below but before "pmc_process_samples()" gets
		 * around to processing the interrupt, then we will
		 * come back here at the next hardclock() tick (and
		 * may find nothing to do if "pmc_process_samples()"
		 * had already processed the interrupt).  We don't
		 * lose the interrupt sample.
		 */
		DPCPU_SET(pmc_sampled, 0);
		cpu = PCPU_GET(cpuid);
		pmc_process_samples(cpu, PMC_HR);
		pmc_process_samples(cpu, PMC_SR);
		pmc_process_samples(cpu, PMC_UR);
		break;

	case PMC_FN_MMAP:
		pmc_process_mmap(td, (struct pmckern_map_in *) arg);
		break;

	case PMC_FN_MUNMAP:
		MPASS(in_epoch(global_epoch_preempt) || sx_xlocked(&pmc_sx));
		pmc_process_munmap(td, (struct pmckern_map_out *) arg);
		break;

	case PMC_FN_PROC_CREATE_LOG:
		pmc_process_proccreate((struct proc *)arg);
		break;

	case PMC_FN_USER_CALLCHAIN:
		/*
		 * Record a call chain.
		 */
		KASSERT(td == curthread, ("[pmc,%d] td != curthread",
		    __LINE__));

		pmc_capture_user_callchain(PCPU_GET(cpuid), PMC_HR,
		    (struct trapframe *) arg);

		KASSERT(td->td_pinned == 1,
			("[pmc,%d] invalid td_pinned value", __LINE__));
		sched_unpin();  /* Can migrate safely now. */

		td->td_pflags &= ~TDP_CALLCHAIN;
		break;

	case PMC_FN_USER_CALLCHAIN_SOFT:
		/*
		 * Record a call chain.
		 */
		KASSERT(td == curthread, ("[pmc,%d] td != curthread",
		    __LINE__));

		cpu = PCPU_GET(cpuid);
		pmc_capture_user_callchain(cpu, PMC_SR,
		    (struct trapframe *) arg);

		KASSERT(td->td_pinned == 1,
		    ("[pmc,%d] invalid td_pinned value", __LINE__));

		sched_unpin();  /* Can migrate safely now. */

		td->td_pflags &= ~TDP_CALLCHAIN;
		break;

	case PMC_FN_SOFT_SAMPLING:
		/*
		 * Call soft PMC sampling intr.
		 */
		pmc_soft_intr((struct pmckern_soft *) arg);
		break;

	case PMC_FN_THR_CREATE:
		pmc_process_thread_add(td);
		pmc_process_threadcreate(td);
		break;

	case PMC_FN_THR_CREATE_LOG:
		pmc_process_threadcreate(td);
		break;

	case PMC_FN_THR_EXIT:
		KASSERT(td == curthread, ("[pmc,%d] td != curthread",
		    __LINE__));
		pmc_process_thread_delete(td);
		pmc_process_threadexit(td);
		break;
	case PMC_FN_THR_EXIT_LOG:
		pmc_process_threadexit(td);
		break;
	case PMC_FN_THR_USERRET:
		KASSERT(td == curthread, ("[pmc,%d] td != curthread",
		    __LINE__));
		pmc_process_thread_userret(td);
		break;

	default:
#ifdef	HWPMC_DEBUG
		KASSERT(0, ("[pmc,%d] unknown hook %d\n", __LINE__, function));
#endif
		break;

	}

	return 0;
}

/*
 * allocate a 'struct pmc_owner' descriptor in the owner hash table.
 */

static struct pmc_owner *
pmc_allocate_owner_descriptor(struct proc *p)
{
	uint32_t hindex;
	struct pmc_owner *po;
	struct pmc_ownerhash *poh;

	hindex = PMC_HASH_PTR(p, pmc_ownerhashmask);
	poh = &pmc_ownerhash[hindex];

	/* allocate space for N pointers and one descriptor struct */
	po = malloc(sizeof(struct pmc_owner), M_PMC, M_WAITOK|M_ZERO);
	po->po_owner = p;
	LIST_INSERT_HEAD(poh, po, po_next); /* insert into hash table */

	TAILQ_INIT(&po->po_logbuffers);
	mtx_init(&po->po_mtx, "pmc-owner-mtx", "pmc-per-proc", MTX_SPIN);

	PMCDBG4(OWN,ALL,1, "allocate-owner proc=%p (%d, %s) pmc-owner=%p",
	    p, p->p_pid, p->p_comm, po);

	return po;
}

static void
pmc_destroy_owner_descriptor(struct pmc_owner *po)
{

	PMCDBG4(OWN,REL,1, "destroy-owner po=%p proc=%p (%d, %s)",
	    po, po->po_owner, po->po_owner->p_pid, po->po_owner->p_comm);

	mtx_destroy(&po->po_mtx);
	free(po, M_PMC);
}

/*
 * Allocate a thread descriptor from the free pool.
 *
 * NOTE: This *can* return NULL.
 */
static struct pmc_thread *
pmc_thread_descriptor_pool_alloc(void)
{
	struct pmc_thread *pt;

	mtx_lock_spin(&pmc_threadfreelist_mtx);
	if ((pt = LIST_FIRST(&pmc_threadfreelist)) != NULL) {
		LIST_REMOVE(pt, pt_next);
		pmc_threadfreelist_entries--;
	}
	mtx_unlock_spin(&pmc_threadfreelist_mtx);

	return (pt);
}

/*
 * Add a thread descriptor to the free pool. We use this instead of free()
 * to maintain a cache of free entries. Additionally, we can safely call
 * this function when we cannot call free(), such as in a critical section.
 * 
 */
static void
pmc_thread_descriptor_pool_free(struct pmc_thread *pt)
{

	if (pt == NULL)
		return;

	memset(pt, 0, THREADENTRY_SIZE);
	mtx_lock_spin(&pmc_threadfreelist_mtx);
	LIST_INSERT_HEAD(&pmc_threadfreelist, pt, pt_next);
	pmc_threadfreelist_entries++;
	if (pmc_threadfreelist_entries > pmc_threadfreelist_max)
		GROUPTASK_ENQUEUE(&free_gtask);
	mtx_unlock_spin(&pmc_threadfreelist_mtx);
}

/*
 * A callout to manage the free list.
 */
static void
pmc_thread_descriptor_pool_free_task(void *arg __unused)
{
	struct pmc_thread *pt;
	LIST_HEAD(, pmc_thread) tmplist;
	int delta;

	LIST_INIT(&tmplist);
	/* Determine what changes, if any, we need to make. */
	mtx_lock_spin(&pmc_threadfreelist_mtx);
	delta = pmc_threadfreelist_entries - pmc_threadfreelist_max;
	while (delta > 0 &&
		   (pt = LIST_FIRST(&pmc_threadfreelist)) != NULL) {
		delta--;
		LIST_REMOVE(pt, pt_next);
		LIST_INSERT_HEAD(&tmplist, pt, pt_next);
	}
	mtx_unlock_spin(&pmc_threadfreelist_mtx);

	/* If there are entries to free, free them. */
	while (!LIST_EMPTY(&tmplist)) {
		pt = LIST_FIRST(&tmplist);
		LIST_REMOVE(pt, pt_next);
		free(pt, M_PMC);
	}
}

/*
 * Drain the thread free pool, freeing all allocations.
 */
static void
pmc_thread_descriptor_pool_drain()
{
	struct pmc_thread *pt, *next;

	LIST_FOREACH_SAFE(pt, &pmc_threadfreelist, pt_next, next) {
		LIST_REMOVE(pt, pt_next);
		free(pt, M_PMC);
	}
}

/*
 * find the descriptor corresponding to thread 'td', adding or removing it
 * as specified by 'mode'.
 *
 * Note that this supports additional mode flags in addition to those
 * supported by pmc_find_process_descriptor():
 * PMC_FLAG_NOWAIT: Causes the function to not wait for mallocs.
 *     This makes it safe to call while holding certain other locks.
 */

static struct pmc_thread *
pmc_find_thread_descriptor(struct pmc_process *pp, struct thread *td,
    uint32_t mode)
{
	struct pmc_thread *pt = NULL, *ptnew = NULL;
	int wait_flag;

	KASSERT(td != NULL, ("[pmc,%d] called to add NULL td", __LINE__));

	/*
	 * Pre-allocate memory in the PMC_FLAG_ALLOCATE case prior to
	 * acquiring the lock.
	 */
	if (mode & PMC_FLAG_ALLOCATE) {
		if ((ptnew = pmc_thread_descriptor_pool_alloc()) == NULL) {
			wait_flag = M_WAITOK;
			if ((mode & PMC_FLAG_NOWAIT) || in_epoch(global_epoch_preempt))
				wait_flag = M_NOWAIT;

			ptnew = malloc(THREADENTRY_SIZE, M_PMC,
			    wait_flag|M_ZERO);
		}
	}

	mtx_lock_spin(pp->pp_tdslock);

	LIST_FOREACH(pt, &pp->pp_tds, pt_next)
		if (pt->pt_td == td)
			break;

	if ((mode & PMC_FLAG_REMOVE) && pt != NULL)
		LIST_REMOVE(pt, pt_next);

	if ((mode & PMC_FLAG_ALLOCATE) && pt == NULL && ptnew != NULL) {
		pt = ptnew;
		ptnew = NULL;
		pt->pt_td = td;
		LIST_INSERT_HEAD(&pp->pp_tds, pt, pt_next);
	}

	mtx_unlock_spin(pp->pp_tdslock);

	if (ptnew != NULL) {
		free(ptnew, M_PMC);
	}

	return pt;
}

/*
 * Try to add thread descriptors for each thread in a process.
 */

static void
pmc_add_thread_descriptors_from_proc(struct proc *p, struct pmc_process *pp)
{
	struct thread *curtd;
	struct pmc_thread **tdlist;
	int i, tdcnt, tdlistsz;

	KASSERT(!PROC_LOCKED(p), ("[pmc,%d] proc unexpectedly locked",
	    __LINE__));
	tdcnt = 32;
 restart:
	tdlistsz = roundup2(tdcnt, 32);

	tdcnt = 0;
	tdlist = malloc(sizeof(struct pmc_thread*) * tdlistsz, M_TEMP, M_WAITOK);

	PROC_LOCK(p);
	FOREACH_THREAD_IN_PROC(p, curtd)
		tdcnt++;
	if (tdcnt >= tdlistsz) {
		PROC_UNLOCK(p);
		free(tdlist, M_TEMP);
		goto restart;
	}
	/*
	 * Try to add each thread to the list without sleeping. If unable,
	 * add to a queue to retry after dropping the process lock.
	 */
	tdcnt = 0;
	FOREACH_THREAD_IN_PROC(p, curtd) {
		tdlist[tdcnt] = pmc_find_thread_descriptor(pp, curtd,
						   PMC_FLAG_ALLOCATE|PMC_FLAG_NOWAIT);
		if (tdlist[tdcnt] == NULL) {
			PROC_UNLOCK(p);
			for (i = 0; i <= tdcnt; i++)
				pmc_thread_descriptor_pool_free(tdlist[i]);
			free(tdlist, M_TEMP);
			goto restart;
		}
		tdcnt++;
	}
	PROC_UNLOCK(p);
	free(tdlist, M_TEMP);
}

/*
 * find the descriptor corresponding to process 'p', adding or removing it
 * as specified by 'mode'.
 */

static struct pmc_process *
pmc_find_process_descriptor(struct proc *p, uint32_t mode)
{
	uint32_t hindex;
	struct pmc_process *pp, *ppnew;
	struct pmc_processhash *pph;

	hindex = PMC_HASH_PTR(p, pmc_processhashmask);
	pph = &pmc_processhash[hindex];

	ppnew = NULL;

	/*
	 * Pre-allocate memory in the PMC_FLAG_ALLOCATE case since we
	 * cannot call malloc(9) once we hold a spin lock.
	 */
	if (mode & PMC_FLAG_ALLOCATE)
		ppnew = malloc(sizeof(struct pmc_process) + md->pmd_npmc *
		    sizeof(struct pmc_targetstate), M_PMC, M_WAITOK|M_ZERO);

	mtx_lock_spin(&pmc_processhash_mtx);
	LIST_FOREACH(pp, pph, pp_next)
	    if (pp->pp_proc == p)
		    break;

	if ((mode & PMC_FLAG_REMOVE) && pp != NULL)
		LIST_REMOVE(pp, pp_next);

	if ((mode & PMC_FLAG_ALLOCATE) && pp == NULL &&
	    ppnew != NULL) {
		ppnew->pp_proc = p;
		LIST_INIT(&ppnew->pp_tds);
		ppnew->pp_tdslock = mtx_pool_find(pmc_mtxpool, ppnew);
		LIST_INSERT_HEAD(pph, ppnew, pp_next);
		mtx_unlock_spin(&pmc_processhash_mtx);
		pp = ppnew;
		ppnew = NULL;

		/* Add thread descriptors for this process' current threads. */
		pmc_add_thread_descriptors_from_proc(p, pp);
	}
	else
		mtx_unlock_spin(&pmc_processhash_mtx);

	if (ppnew != NULL)
		free(ppnew, M_PMC);

	return pp;
}

/*
 * remove a process descriptor from the process hash table.
 */

static void
pmc_remove_process_descriptor(struct pmc_process *pp)
{
	KASSERT(pp->pp_refcnt == 0,
	    ("[pmc,%d] Removing process descriptor %p with count %d",
		__LINE__, pp, pp->pp_refcnt));

	mtx_lock_spin(&pmc_processhash_mtx);
	LIST_REMOVE(pp, pp_next);
	mtx_unlock_spin(&pmc_processhash_mtx);
}

/*
 * destroy a process descriptor.
 */

static void
pmc_destroy_process_descriptor(struct pmc_process *pp)
{
	struct pmc_thread *pmc_td;

	while ((pmc_td = LIST_FIRST(&pp->pp_tds)) != NULL) {
		LIST_REMOVE(pmc_td, pt_next);
		pmc_thread_descriptor_pool_free(pmc_td);
	}
	free(pp, M_PMC);
}


/*
 * find an owner descriptor corresponding to proc 'p'
 */

static struct pmc_owner *
pmc_find_owner_descriptor(struct proc *p)
{
	uint32_t hindex;
	struct pmc_owner *po;
	struct pmc_ownerhash *poh;

	hindex = PMC_HASH_PTR(p, pmc_ownerhashmask);
	poh = &pmc_ownerhash[hindex];

	po = NULL;
	LIST_FOREACH(po, poh, po_next)
	    if (po->po_owner == p)
		    break;

	PMCDBG5(OWN,FND,1, "find-owner proc=%p (%d, %s) hindex=0x%x -> "
	    "pmc-owner=%p", p, p->p_pid, p->p_comm, hindex, po);

	return po;
}

/*
 * pmc_allocate_pmc_descriptor
 *
 * Allocate a pmc descriptor and initialize its
 * fields.
 */

static struct pmc *
pmc_allocate_pmc_descriptor(void)
{
	struct pmc *pmc;

	pmc = malloc(sizeof(struct pmc), M_PMC, M_WAITOK|M_ZERO);
	pmc->pm_runcount = counter_u64_alloc(M_WAITOK);
	pmc->pm_pcpu_state = malloc(sizeof(struct pmc_pcpu_state)*mp_ncpus, M_PMC, M_WAITOK|M_ZERO);
	PMCDBG1(PMC,ALL,1, "allocate-pmc -> pmc=%p", pmc);

	return pmc;
}

/*
 * Destroy a pmc descriptor.
 */

static void
pmc_destroy_pmc_descriptor(struct pmc *pm)
{

	KASSERT(pm->pm_state == PMC_STATE_DELETED ||
	    pm->pm_state == PMC_STATE_FREE,
	    ("[pmc,%d] destroying non-deleted PMC", __LINE__));
	KASSERT(LIST_EMPTY(&pm->pm_targets),
	    ("[pmc,%d] destroying pmc with targets", __LINE__));
	KASSERT(pm->pm_owner == NULL,
	    ("[pmc,%d] destroying pmc attached to an owner", __LINE__));
	KASSERT(counter_u64_fetch(pm->pm_runcount) == 0,
	    ("[pmc,%d] pmc has non-zero run count %ld", __LINE__,
		 (unsigned long)counter_u64_fetch(pm->pm_runcount)));

	counter_u64_free(pm->pm_runcount);
	free(pm->pm_pcpu_state, M_PMC);
	free(pm, M_PMC);
}

static void
pmc_wait_for_pmc_idle(struct pmc *pm)
{
#ifdef INVARIANTS
	volatile int maxloop;

	maxloop = 100 * pmc_cpu_max();
#endif
	/*
	 * Loop (with a forced context switch) till the PMC's runcount
	 * comes down to zero.
	 */
	pmclog_flush(pm->pm_owner, 1);
	while (counter_u64_fetch(pm->pm_runcount) > 0) {
		pmclog_flush(pm->pm_owner, 1);
#ifdef INVARIANTS
		maxloop--;
		KASSERT(maxloop > 0,
		    ("[pmc,%d] (ri%d, rc%ld) waiting too long for "
			"pmc to be free", __LINE__,
			 PMC_TO_ROWINDEX(pm), (unsigned long)counter_u64_fetch(pm->pm_runcount)));
#endif
		pmc_force_context_switch();
	}
}

/*
 * This function does the following things:
 *
 *  - detaches the PMC from hardware
 *  - unlinks all target threads that were attached to it
 *  - removes the PMC from its owner's list
 *  - destroys the PMC private mutex
 *
 * Once this function completes, the given pmc pointer can be freed by
 * calling pmc_destroy_pmc_descriptor().
 */

static void
pmc_release_pmc_descriptor(struct pmc *pm)
{
	enum pmc_mode mode;
	struct pmc_hw *phw;
	u_int adjri, ri, cpu;
	struct pmc_owner *po;
	struct pmc_binding pb;
	struct pmc_process *pp;
	struct pmc_classdep *pcd;
	struct pmc_target *ptgt, *tmp;

	sx_assert(&pmc_sx, SX_XLOCKED);

	KASSERT(pm, ("[pmc,%d] null pmc", __LINE__));

	ri   = PMC_TO_ROWINDEX(pm);
	pcd  = pmc_ri_to_classdep(md, ri, &adjri);
	mode = PMC_TO_MODE(pm);

	PMCDBG3(PMC,REL,1, "release-pmc pmc=%p ri=%d mode=%d", pm, ri,
	    mode);

	/*
	 * First, we take the PMC off hardware.
	 */
	cpu = 0;
	if (PMC_IS_SYSTEM_MODE(mode)) {

		/*
		 * A system mode PMC runs on a specific CPU.  Switch
		 * to this CPU and turn hardware off.
		 */
		pmc_save_cpu_binding(&pb);

		cpu = PMC_TO_CPU(pm);

		pmc_select_cpu(cpu);

		/* switch off non-stalled CPUs */
		pm->pm_pcpu_state[cpu].pps_cpustate = 0;
		if (pm->pm_state == PMC_STATE_RUNNING &&
			pm->pm_pcpu_state[cpu].pps_stalled == 0) {

			phw = pmc_pcpu[cpu]->pc_hwpmcs[ri];

			KASSERT(phw->phw_pmc == pm,
			    ("[pmc, %d] pmc ptr ri(%d) hw(%p) pm(%p)",
				__LINE__, ri, phw->phw_pmc, pm));
			PMCDBG2(PMC,REL,2, "stopping cpu=%d ri=%d", cpu, ri);

			critical_enter();
			pcd->pcd_stop_pmc(cpu, adjri);
			critical_exit();
		}

		PMCDBG2(PMC,REL,2, "decfg cpu=%d ri=%d", cpu, ri);

		critical_enter();
		pcd->pcd_config_pmc(cpu, adjri, NULL);
		critical_exit();

		/* adjust the global and process count of SS mode PMCs */
		if (mode == PMC_MODE_SS && pm->pm_state == PMC_STATE_RUNNING) {
			po = pm->pm_owner;
			po->po_sscount--;
			if (po->po_sscount == 0) {
				atomic_subtract_rel_int(&pmc_ss_count, 1);
				CK_LIST_REMOVE(po, po_ssnext);
				epoch_wait_preempt(global_epoch_preempt);
			}
		}

		pm->pm_state = PMC_STATE_DELETED;

		pmc_restore_cpu_binding(&pb);

		/*
		 * We could have references to this PMC structure in
		 * the per-cpu sample queues.  Wait for the queue to
		 * drain.
		 */
		pmc_wait_for_pmc_idle(pm);

	} else if (PMC_IS_VIRTUAL_MODE(mode)) {

		/*
		 * A virtual PMC could be running on multiple CPUs at
		 * a given instant.
		 *
		 * By marking its state as DELETED, we ensure that
		 * this PMC is never further scheduled on hardware.
		 *
		 * Then we wait till all CPUs are done with this PMC.
		 */
		pm->pm_state = PMC_STATE_DELETED;


		/* Wait for the PMCs runcount to come to zero. */
		pmc_wait_for_pmc_idle(pm);

		/*
		 * At this point the PMC is off all CPUs and cannot be
		 * freshly scheduled onto a CPU.  It is now safe to
		 * unlink all targets from this PMC.  If a
		 * process-record's refcount falls to zero, we remove
		 * it from the hash table.  The module-wide SX lock
		 * protects us from races.
		 */
		LIST_FOREACH_SAFE(ptgt, &pm->pm_targets, pt_next, tmp) {
			pp = ptgt->pt_process;
			pmc_unlink_target_process(pm, pp); /* frees 'ptgt' */

			PMCDBG1(PMC,REL,3, "pp->refcnt=%d", pp->pp_refcnt);

			/*
			 * If the target process record shows that no
			 * PMCs are attached to it, reclaim its space.
			 */

			if (pp->pp_refcnt == 0) {
				pmc_remove_process_descriptor(pp);
				pmc_destroy_process_descriptor(pp);
			}
		}

		cpu = curthread->td_oncpu; /* setup cpu for pmd_release() */

	}

	/*
	 * Release any MD resources
	 */
	(void) pcd->pcd_release_pmc(cpu, adjri, pm);

	/*
	 * Update row disposition
	 */

	if (PMC_IS_SYSTEM_MODE(PMC_TO_MODE(pm)))
		PMC_UNMARK_ROW_STANDALONE(ri);
	else
		PMC_UNMARK_ROW_THREAD(ri);

	/* unlink from the owner's list */
	if (pm->pm_owner) {
		LIST_REMOVE(pm, pm_next);
		pm->pm_owner = NULL;
	}
}

/*
 * Register an owner and a pmc.
 */

static int
pmc_register_owner(struct proc *p, struct pmc *pmc)
{
	struct pmc_owner *po;

	sx_assert(&pmc_sx, SX_XLOCKED);

	if ((po = pmc_find_owner_descriptor(p)) == NULL)
		if ((po = pmc_allocate_owner_descriptor(p)) == NULL)
			return ENOMEM;

	KASSERT(pmc->pm_owner == NULL,
	    ("[pmc,%d] attempting to own an initialized PMC", __LINE__));
	pmc->pm_owner  = po;

	LIST_INSERT_HEAD(&po->po_pmcs, pmc, pm_next);

	PROC_LOCK(p);
	p->p_flag |= P_HWPMC;
	PROC_UNLOCK(p);

	if (po->po_flags & PMC_PO_OWNS_LOGFILE)
		pmclog_process_pmcallocate(pmc);

	PMCDBG2(PMC,REG,1, "register-owner pmc-owner=%p pmc=%p",
	    po, pmc);

	return 0;
}

/*
 * Return the current row disposition:
 * == 0 => FREE
 *  > 0 => PROCESS MODE
 *  < 0 => SYSTEM MODE
 */

int
pmc_getrowdisp(int ri)
{
	return pmc_pmcdisp[ri];
}

/*
 * Check if a PMC at row index 'ri' can be allocated to the current
 * process.
 *
 * Allocation can fail if:
 *   - the current process is already being profiled by a PMC at index 'ri',
 *     attached to it via OP_PMCATTACH.
 *   - the current process has already allocated a PMC at index 'ri'
 *     via OP_ALLOCATE.
 */

static int
pmc_can_allocate_rowindex(struct proc *p, unsigned int ri, int cpu)
{
	enum pmc_mode mode;
	struct pmc *pm;
	struct pmc_owner *po;
	struct pmc_process *pp;

	PMCDBG5(PMC,ALR,1, "can-allocate-rowindex proc=%p (%d, %s) ri=%d "
	    "cpu=%d", p, p->p_pid, p->p_comm, ri, cpu);

	/*
	 * We shouldn't have already allocated a process-mode PMC at
	 * row index 'ri'.
	 *
	 * We shouldn't have allocated a system-wide PMC on the same
	 * CPU and same RI.
	 */
	if ((po = pmc_find_owner_descriptor(p)) != NULL)
		LIST_FOREACH(pm, &po->po_pmcs, pm_next) {
		    if (PMC_TO_ROWINDEX(pm) == ri) {
			    mode = PMC_TO_MODE(pm);
			    if (PMC_IS_VIRTUAL_MODE(mode))
				    return EEXIST;
			    if (PMC_IS_SYSTEM_MODE(mode) &&
				(int) PMC_TO_CPU(pm) == cpu)
				    return EEXIST;
		    }
	        }

	/*
	 * We also shouldn't be the target of any PMC at this index
	 * since otherwise a PMC_ATTACH to ourselves will fail.
	 */
	if ((pp = pmc_find_process_descriptor(p, 0)) != NULL)
		if (pp->pp_pmcs[ri].pp_pmc)
			return EEXIST;

	PMCDBG4(PMC,ALR,2, "can-allocate-rowindex proc=%p (%d, %s) ri=%d ok",
	    p, p->p_pid, p->p_comm, ri);

	return 0;
}

/*
 * Check if a given PMC at row index 'ri' can be currently used in
 * mode 'mode'.
 */

static int
pmc_can_allocate_row(int ri, enum pmc_mode mode)
{
	enum pmc_disp	disp;

	sx_assert(&pmc_sx, SX_XLOCKED);

	PMCDBG2(PMC,ALR,1, "can-allocate-row ri=%d mode=%d", ri, mode);

	if (PMC_IS_SYSTEM_MODE(mode))
		disp = PMC_DISP_STANDALONE;
	else
		disp = PMC_DISP_THREAD;

	/*
	 * check disposition for PMC row 'ri':
	 *
	 * Expected disposition		Row-disposition		Result
	 *
	 * STANDALONE			STANDALONE or FREE	proceed
	 * STANDALONE			THREAD			fail
	 * THREAD			THREAD or FREE		proceed
	 * THREAD			STANDALONE		fail
	 */

	if (!PMC_ROW_DISP_IS_FREE(ri) &&
	    !(disp == PMC_DISP_THREAD && PMC_ROW_DISP_IS_THREAD(ri)) &&
	    !(disp == PMC_DISP_STANDALONE && PMC_ROW_DISP_IS_STANDALONE(ri)))
		return EBUSY;

	/*
	 * All OK
	 */

	PMCDBG2(PMC,ALR,2, "can-allocate-row ri=%d mode=%d ok", ri, mode);

	return 0;

}

/*
 * Find a PMC descriptor with user handle 'pmcid' for thread 'td'.
 */

static struct pmc *
pmc_find_pmc_descriptor_in_process(struct pmc_owner *po, pmc_id_t pmcid)
{
	struct pmc *pm;

	KASSERT(PMC_ID_TO_ROWINDEX(pmcid) < md->pmd_npmc,
	    ("[pmc,%d] Illegal pmc index %d (max %d)", __LINE__,
		PMC_ID_TO_ROWINDEX(pmcid), md->pmd_npmc));

	LIST_FOREACH(pm, &po->po_pmcs, pm_next)
	    if (pm->pm_id == pmcid)
		    return pm;

	return NULL;
}

static int
pmc_find_pmc(pmc_id_t pmcid, struct pmc **pmc)
{

	struct pmc *pm, *opm;
	struct pmc_owner *po;
	struct pmc_process *pp;

	PMCDBG1(PMC,FND,1, "find-pmc id=%d", pmcid);
	if (PMC_ID_TO_ROWINDEX(pmcid) >= md->pmd_npmc)
		return (EINVAL);

	if ((po = pmc_find_owner_descriptor(curthread->td_proc)) == NULL) {
		/*
		 * In case of PMC_F_DESCENDANTS child processes we will not find
		 * the current process in the owners hash list.  Find the owner
		 * process first and from there lookup the po.
		 */
		if ((pp = pmc_find_process_descriptor(curthread->td_proc,
		    PMC_FLAG_NONE)) == NULL) {
			return ESRCH;
		} else {
			opm = pp->pp_pmcs[PMC_ID_TO_ROWINDEX(pmcid)].pp_pmc;
			if (opm == NULL)
				return ESRCH;
			if ((opm->pm_flags & (PMC_F_ATTACHED_TO_OWNER|
			    PMC_F_DESCENDANTS)) != (PMC_F_ATTACHED_TO_OWNER|
			    PMC_F_DESCENDANTS))
				return ESRCH;
			po = opm->pm_owner;
		}
	}

	if ((pm = pmc_find_pmc_descriptor_in_process(po, pmcid)) == NULL)
		return EINVAL;

	PMCDBG2(PMC,FND,2, "find-pmc id=%d -> pmc=%p", pmcid, pm);

	*pmc = pm;
	return 0;
}

/*
 * Start a PMC.
 */

static int
pmc_start(struct pmc *pm)
{
	enum pmc_mode mode;
	struct pmc_owner *po;
	struct pmc_binding pb;
	struct pmc_classdep *pcd;
	int adjri, error, cpu, ri;

	KASSERT(pm != NULL,
	    ("[pmc,%d] null pm", __LINE__));

	mode = PMC_TO_MODE(pm);
	ri   = PMC_TO_ROWINDEX(pm);
	pcd  = pmc_ri_to_classdep(md, ri, &adjri);

	error = 0;

	PMCDBG3(PMC,OPS,1, "start pmc=%p mode=%d ri=%d", pm, mode, ri);

	po = pm->pm_owner;

	/*
	 * Disallow PMCSTART if a logfile is required but has not been
	 * configured yet.
	 */
	if ((pm->pm_flags & PMC_F_NEEDS_LOGFILE) &&
	    (po->po_flags & PMC_PO_OWNS_LOGFILE) == 0)
		return (EDOOFUS);	/* programming error */

	/*
	 * If this is a sampling mode PMC, log mapping information for
	 * the kernel modules that are currently loaded.
	 */
	if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
	    pmc_log_kernel_mappings(pm);

	if (PMC_IS_VIRTUAL_MODE(mode)) {

		/*
		 * If a PMCATTACH has never been done on this PMC,
		 * attach it to its owner process.
		 */

		if (LIST_EMPTY(&pm->pm_targets))
			error = (pm->pm_flags & PMC_F_ATTACH_DONE) ? ESRCH :
			    pmc_attach_process(po->po_owner, pm);

		/*
		 * If the PMC is attached to its owner, then force a context
		 * switch to ensure that the MD state gets set correctly.
		 */

		if (error == 0) {
			pm->pm_state = PMC_STATE_RUNNING;
			if (pm->pm_flags & PMC_F_ATTACHED_TO_OWNER)
				pmc_force_context_switch();
		}

		return (error);
	}


	/*
	 * A system-wide PMC.
	 *
	 * Add the owner to the global list if this is a system-wide
	 * sampling PMC.
	 */

	if (mode == PMC_MODE_SS) {
		/*
		 * Log mapping information for all existing processes in the
		 * system.  Subsequent mappings are logged as they happen;
		 * see pmc_process_mmap().
		 */
		if (po->po_logprocmaps == 0) {
			pmc_log_all_process_mappings(po);
			po->po_logprocmaps = 1;
		}
		po->po_sscount++;
		if (po->po_sscount == 1) {
			atomic_add_rel_int(&pmc_ss_count, 1);
			CK_LIST_INSERT_HEAD(&pmc_ss_owners, po, po_ssnext);
			PMCDBG1(PMC,OPS,1, "po=%p in global list", po);
		}
	}

	/*
	 * Move to the CPU associated with this
	 * PMC, and start the hardware.
	 */

	pmc_save_cpu_binding(&pb);

	cpu = PMC_TO_CPU(pm);

	if (!pmc_cpu_is_active(cpu))
		return (ENXIO);

	pmc_select_cpu(cpu);

	/*
	 * global PMCs are configured at allocation time
	 * so write out the initial value and start the PMC.
	 */

	pm->pm_state = PMC_STATE_RUNNING;

	critical_enter();
	if ((error = pcd->pcd_write_pmc(cpu, adjri,
		 PMC_IS_SAMPLING_MODE(mode) ?
		 pm->pm_sc.pm_reloadcount :
		 pm->pm_sc.pm_initial)) == 0) {
		/* If a sampling mode PMC, reset stalled state. */
		if (PMC_IS_SAMPLING_MODE(mode))
			pm->pm_pcpu_state[cpu].pps_stalled = 0;

		/* Indicate that we desire this to run. Start it. */
		pm->pm_pcpu_state[cpu].pps_cpustate = 1;
		error = pcd->pcd_start_pmc(cpu, adjri);
	}
	critical_exit();

	pmc_restore_cpu_binding(&pb);

	return (error);
}

/*
 * Stop a PMC.
 */

static int
pmc_stop(struct pmc *pm)
{
	struct pmc_owner *po;
	struct pmc_binding pb;
	struct pmc_classdep *pcd;
	int adjri, cpu, error, ri;

	KASSERT(pm != NULL, ("[pmc,%d] null pmc", __LINE__));

	PMCDBG3(PMC,OPS,1, "stop pmc=%p mode=%d ri=%d", pm,
	    PMC_TO_MODE(pm), PMC_TO_ROWINDEX(pm));

	pm->pm_state = PMC_STATE_STOPPED;

	/*
	 * If the PMC is a virtual mode one, changing the state to
	 * non-RUNNING is enough to ensure that the PMC never gets
	 * scheduled.
	 *
	 * If this PMC is current running on a CPU, then it will
	 * handled correctly at the time its target process is context
	 * switched out.
	 */

	if (PMC_IS_VIRTUAL_MODE(PMC_TO_MODE(pm)))
		return 0;

	/*
	 * A system-mode PMC.  Move to the CPU associated with
	 * this PMC, and stop the hardware.  We update the
	 * 'initial count' so that a subsequent PMCSTART will
	 * resume counting from the current hardware count.
	 */

	pmc_save_cpu_binding(&pb);

	cpu = PMC_TO_CPU(pm);

	KASSERT(cpu >= 0 && cpu < pmc_cpu_max(),
	    ("[pmc,%d] illegal cpu=%d", __LINE__, cpu));

	if (!pmc_cpu_is_active(cpu))
		return ENXIO;

	pmc_select_cpu(cpu);

	ri = PMC_TO_ROWINDEX(pm);
	pcd = pmc_ri_to_classdep(md, ri, &adjri);

	pm->pm_pcpu_state[cpu].pps_cpustate = 0;
	critical_enter();
	if ((error = pcd->pcd_stop_pmc(cpu, adjri)) == 0)
		error = pcd->pcd_read_pmc(cpu, adjri, &pm->pm_sc.pm_initial);
	critical_exit();

	pmc_restore_cpu_binding(&pb);

	po = pm->pm_owner;

	/* remove this owner from the global list of SS PMC owners */
	if (PMC_TO_MODE(pm) == PMC_MODE_SS) {
		po->po_sscount--;
		if (po->po_sscount == 0) {
			atomic_subtract_rel_int(&pmc_ss_count, 1);
			CK_LIST_REMOVE(po, po_ssnext);
			epoch_wait_preempt(global_epoch_preempt);
			PMCDBG1(PMC,OPS,2,"po=%p removed from global list", po);
		}
	}

	return (error);
}

static struct pmc_classdep *
pmc_class_to_classdep(enum pmc_class class)
{
	int n;

	for (n = 0; n < md->pmd_nclass; n++)
		if (md->pmd_classdep[n].pcd_class == class)
			return (&md->pmd_classdep[n]);
	return (NULL);
}

#if defined(HWPMC_DEBUG) && defined(KTR)
static const char *pmc_op_to_name[] = {
#undef	__PMC_OP
#define	__PMC_OP(N, D)	#N ,
	__PMC_OPS()
	NULL
};
#endif

/*
 * The syscall interface
 */

#define	PMC_GET_SX_XLOCK(...) do {		\
	sx_xlock(&pmc_sx);			\
	if (pmc_hook == NULL) {			\
		sx_xunlock(&pmc_sx);		\
		return __VA_ARGS__;		\
	}					\
} while (0)

#define	PMC_DOWNGRADE_SX() do {			\
	sx_downgrade(&pmc_sx);			\
	is_sx_downgraded = 1;			\
} while (0)

static int
pmc_syscall_handler(struct thread *td, void *syscall_args)
{
	int error, is_sx_downgraded, op;
	struct pmc_syscall_args *c;
	void *pmclog_proc_handle;
	void *arg;

	c = (struct pmc_syscall_args *)syscall_args;
	op = c->pmop_code;
	arg = c->pmop_data;
	/* PMC isn't set up yet */
	if (pmc_hook == NULL)
		return (EINVAL);
	if (op == PMC_OP_CONFIGURELOG) {
		/*
		 * We cannot create the logging process inside
		 * pmclog_configure_log() because there is a LOR
		 * between pmc_sx and process structure locks.
		 * Instead, pre-create the process and ignite the loop
		 * if everything is fine, otherwise direct the process
		 * to exit.
		 */
		error = pmclog_proc_create(td, &pmclog_proc_handle);
		if (error != 0)
			goto done_syscall;
	}

	PMC_GET_SX_XLOCK(ENOSYS);
	is_sx_downgraded = 0;
	PMCDBG3(MOD,PMS,1, "syscall op=%d \"%s\" arg=%p", op,
	    pmc_op_to_name[op], arg);

	error = 0;
	counter_u64_add(pmc_stats.pm_syscalls, 1);

	switch (op) {


	/*
	 * Configure a log file.
	 *
	 * XXX This OP will be reworked.
	 */

	case PMC_OP_CONFIGURELOG:
	{
		struct proc *p;
		struct pmc *pm;
		struct pmc_owner *po;
		struct pmc_op_configurelog cl;

		if ((error = copyin(arg, &cl, sizeof(cl))) != 0) {
			pmclog_proc_ignite(pmclog_proc_handle, NULL);
			break;
		}

		/* mark this process as owning a log file */
		p = td->td_proc;
		if ((po = pmc_find_owner_descriptor(p)) == NULL)
			if ((po = pmc_allocate_owner_descriptor(p)) == NULL) {
				pmclog_proc_ignite(pmclog_proc_handle, NULL);
				error = ENOMEM;
				break;
			}

		/*
		 * If a valid fd was passed in, try to configure that,
		 * otherwise if 'fd' was less than zero and there was
		 * a log file configured, flush its buffers and
		 * de-configure it.
		 */
		if (cl.pm_logfd >= 0) {
			error = pmclog_configure_log(md, po, cl.pm_logfd);
			pmclog_proc_ignite(pmclog_proc_handle, error == 0 ?
			    po : NULL);
		} else if (po->po_flags & PMC_PO_OWNS_LOGFILE) {
			pmclog_proc_ignite(pmclog_proc_handle, NULL);
			error = pmclog_close(po);
			if (error == 0) {
				LIST_FOREACH(pm, &po->po_pmcs, pm_next)
				    if (pm->pm_flags & PMC_F_NEEDS_LOGFILE &&
					pm->pm_state == PMC_STATE_RUNNING)
					    pmc_stop(pm);
				error = pmclog_deconfigure_log(po);
			}
		} else {
			pmclog_proc_ignite(pmclog_proc_handle, NULL);
			error = EINVAL;
		}
	}
	break;

	/*
	 * Flush a log file.
	 */

	case PMC_OP_FLUSHLOG:
	{
		struct pmc_owner *po;

		sx_assert(&pmc_sx, SX_XLOCKED);

		if ((po = pmc_find_owner_descriptor(td->td_proc)) == NULL) {
			error = EINVAL;
			break;
		}

		error = pmclog_flush(po, 0);
	}
	break;

	/*
	 * Close a log file.
	 */

	case PMC_OP_CLOSELOG:
	{
		struct pmc_owner *po;

		sx_assert(&pmc_sx, SX_XLOCKED);

		if ((po = pmc_find_owner_descriptor(td->td_proc)) == NULL) {
			error = EINVAL;
			break;
		}

		error = pmclog_close(po);
	}
	break;

	/*
	 * Retrieve hardware configuration.
	 */

	case PMC_OP_GETCPUINFO:	/* CPU information */
	{
		struct pmc_op_getcpuinfo gci;
		struct pmc_classinfo *pci;
		struct pmc_classdep *pcd;
		int cl;

		memset(&gci, 0, sizeof(gci));
		gci.pm_cputype = md->pmd_cputype;
		gci.pm_ncpu    = pmc_cpu_max();
		gci.pm_npmc    = md->pmd_npmc;
		gci.pm_nclass  = md->pmd_nclass;
		pci = gci.pm_classes;
		pcd = md->pmd_classdep;
		for (cl = 0; cl < md->pmd_nclass; cl++, pci++, pcd++) {
			pci->pm_caps  = pcd->pcd_caps;
			pci->pm_class = pcd->pcd_class;
			pci->pm_width = pcd->pcd_width;
			pci->pm_num   = pcd->pcd_num;
		}
		error = copyout(&gci, arg, sizeof(gci));
	}
	break;

	/*
	 * Retrieve soft events list.
	 */
	case PMC_OP_GETDYNEVENTINFO:
	{
		enum pmc_class			cl;
		enum pmc_event			ev;
		struct pmc_op_getdyneventinfo	*gei;
		struct pmc_dyn_event_descr	dev;
		struct pmc_soft			*ps;
		uint32_t			nevent;

		sx_assert(&pmc_sx, SX_LOCKED);

		gei = (struct pmc_op_getdyneventinfo *) arg;

		if ((error = copyin(&gei->pm_class, &cl, sizeof(cl))) != 0)
			break;

		/* Only SOFT class is dynamic. */
		if (cl != PMC_CLASS_SOFT) {
			error = EINVAL;
			break;
		}

		nevent = 0;
		for (ev = PMC_EV_SOFT_FIRST; (int)ev <= PMC_EV_SOFT_LAST; ev++) {
			ps = pmc_soft_ev_acquire(ev);
			if (ps == NULL)
				continue;
			bcopy(&ps->ps_ev, &dev, sizeof(dev));
			pmc_soft_ev_release(ps);

			error = copyout(&dev,
			    &gei->pm_events[nevent],
			    sizeof(struct pmc_dyn_event_descr));
			if (error != 0)
				break;
			nevent++;
		}
		if (error != 0)
			break;

		error = copyout(&nevent, &gei->pm_nevent,
		    sizeof(nevent));
	}
	break;

	/*
	 * Get module statistics
	 */

	case PMC_OP_GETDRIVERSTATS:
	{
		struct pmc_op_getdriverstats gms;
#define CFETCH(a, b, field) a.field = counter_u64_fetch(b.field)
		CFETCH(gms, pmc_stats, pm_intr_ignored);
		CFETCH(gms, pmc_stats, pm_intr_processed);
		CFETCH(gms, pmc_stats, pm_intr_bufferfull);
		CFETCH(gms, pmc_stats, pm_syscalls);
		CFETCH(gms, pmc_stats, pm_syscall_errors);
		CFETCH(gms, pmc_stats, pm_buffer_requests);
		CFETCH(gms, pmc_stats, pm_buffer_requests_failed);
		CFETCH(gms, pmc_stats, pm_log_sweeps);
#undef CFETCH
		error = copyout(&gms, arg, sizeof(gms));
	}
	break;


	/*
	 * Retrieve module version number
	 */

	case PMC_OP_GETMODULEVERSION:
	{
		uint32_t cv, modv;

		/* retrieve the client's idea of the ABI version */
		if ((error = copyin(arg, &cv, sizeof(uint32_t))) != 0)
			break;
		/* don't service clients newer than our driver */
		modv = PMC_VERSION;
		if ((cv & 0xFFFF0000) > (modv & 0xFFFF0000)) {
			error = EPROGMISMATCH;
			break;
		}
		error = copyout(&modv, arg, sizeof(int));
	}
	break;


	/*
	 * Retrieve the state of all the PMCs on a given
	 * CPU.
	 */

	case PMC_OP_GETPMCINFO:
	{
		int ari;
		struct pmc *pm;
		size_t pmcinfo_size;
		uint32_t cpu, n, npmc;
		struct pmc_owner *po;
		struct pmc_binding pb;
		struct pmc_classdep *pcd;
		struct pmc_info *p, *pmcinfo;
		struct pmc_op_getpmcinfo *gpi;

		PMC_DOWNGRADE_SX();

		gpi = (struct pmc_op_getpmcinfo *) arg;

		if ((error = copyin(&gpi->pm_cpu, &cpu, sizeof(cpu))) != 0)
			break;

		if (cpu >= pmc_cpu_max()) {
			error = EINVAL;
			break;
		}

		if (!pmc_cpu_is_active(cpu)) {
			error = ENXIO;
			break;
		}

		/* switch to CPU 'cpu' */
		pmc_save_cpu_binding(&pb);
		pmc_select_cpu(cpu);

		npmc = md->pmd_npmc;

		pmcinfo_size = npmc * sizeof(struct pmc_info);
		pmcinfo = malloc(pmcinfo_size, M_PMC, M_WAITOK | M_ZERO);

		p = pmcinfo;

		for (n = 0; n < md->pmd_npmc; n++, p++) {

			pcd = pmc_ri_to_classdep(md, n, &ari);

			KASSERT(pcd != NULL,
			    ("[pmc,%d] null pcd ri=%d", __LINE__, n));

			if ((error = pcd->pcd_describe(cpu, ari, p, &pm)) != 0)
				break;

			if (PMC_ROW_DISP_IS_STANDALONE(n))
				p->pm_rowdisp = PMC_DISP_STANDALONE;
			else if (PMC_ROW_DISP_IS_THREAD(n))
				p->pm_rowdisp = PMC_DISP_THREAD;
			else
				p->pm_rowdisp = PMC_DISP_FREE;

			p->pm_ownerpid = -1;

			if (pm == NULL)	/* no PMC associated */
				continue;

			po = pm->pm_owner;

			KASSERT(po->po_owner != NULL,
			    ("[pmc,%d] pmc_owner had a null proc pointer",
				__LINE__));

			p->pm_ownerpid = po->po_owner->p_pid;
			p->pm_mode     = PMC_TO_MODE(pm);
			p->pm_event    = pm->pm_event;
			p->pm_flags    = pm->pm_flags;

			if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)))
				p->pm_reloadcount =
				    pm->pm_sc.pm_reloadcount;
		}

		pmc_restore_cpu_binding(&pb);

		/* now copy out the PMC info collected */
		if (error == 0)
			error = copyout(pmcinfo, &gpi->pm_pmcs, pmcinfo_size);

		free(pmcinfo, M_PMC);
	}
	break;


	/*
	 * Set the administrative state of a PMC.  I.e. whether
	 * the PMC is to be used or not.
	 */

	case PMC_OP_PMCADMIN:
	{
		int cpu, ri;
		enum pmc_state request;
		struct pmc_cpu *pc;
		struct pmc_hw *phw;
		struct pmc_op_pmcadmin pma;
		struct pmc_binding pb;

		sx_assert(&pmc_sx, SX_XLOCKED);

		KASSERT(td == curthread,
		    ("[pmc,%d] td != curthread", __LINE__));

		error = priv_check(td, PRIV_PMC_MANAGE);
		if (error)
			break;

		if ((error = copyin(arg, &pma, sizeof(pma))) != 0)
			break;

		cpu = pma.pm_cpu;

		if (cpu < 0 || cpu >= (int) pmc_cpu_max()) {
			error = EINVAL;
			break;
		}

		if (!pmc_cpu_is_active(cpu)) {
			error = ENXIO;
			break;
		}

		request = pma.pm_state;

		if (request != PMC_STATE_DISABLED &&
		    request != PMC_STATE_FREE) {
			error = EINVAL;
			break;
		}

		ri = pma.pm_pmc; /* pmc id == row index */
		if (ri < 0 || ri >= (int) md->pmd_npmc) {
			error = EINVAL;
			break;
		}

		/*
		 * We can't disable a PMC with a row-index allocated
		 * for process virtual PMCs.
		 */

		if (PMC_ROW_DISP_IS_THREAD(ri) &&
		    request == PMC_STATE_DISABLED) {
			error = EBUSY;
			break;
		}

		/*
		 * otherwise, this PMC on this CPU is either free or
		 * in system-wide mode.
		 */

		pmc_save_cpu_binding(&pb);
		pmc_select_cpu(cpu);

		pc  = pmc_pcpu[cpu];
		phw = pc->pc_hwpmcs[ri];

		/*
		 * XXX do we need some kind of 'forced' disable?
		 */

		if (phw->phw_pmc == NULL) {
			if (request == PMC_STATE_DISABLED &&
			    (phw->phw_state & PMC_PHW_FLAG_IS_ENABLED)) {
				phw->phw_state &= ~PMC_PHW_FLAG_IS_ENABLED;
				PMC_MARK_ROW_STANDALONE(ri);
			} else if (request == PMC_STATE_FREE &&
			    (phw->phw_state & PMC_PHW_FLAG_IS_ENABLED) == 0) {
				phw->phw_state |=  PMC_PHW_FLAG_IS_ENABLED;
				PMC_UNMARK_ROW_STANDALONE(ri);
			}
			/* other cases are a no-op */
		} else
			error = EBUSY;

		pmc_restore_cpu_binding(&pb);
	}
	break;


	/*
	 * Allocate a PMC.
	 */

	case PMC_OP_PMCALLOCATE:
	{
		int adjri, n;
		u_int cpu;
		uint32_t caps;
		struct pmc *pmc;
		enum pmc_mode mode;
		struct pmc_hw *phw;
		struct pmc_binding pb;
		struct pmc_classdep *pcd;
		struct pmc_op_pmcallocate pa;

		if ((error = copyin(arg, &pa, sizeof(pa))) != 0)
			break;

		caps = pa.pm_caps;
		mode = pa.pm_mode;
		cpu  = pa.pm_cpu;

		if ((mode != PMC_MODE_SS  &&  mode != PMC_MODE_SC  &&
		     mode != PMC_MODE_TS  &&  mode != PMC_MODE_TC) ||
		    (cpu != (u_int) PMC_CPU_ANY && cpu >= pmc_cpu_max())) {
			error = EINVAL;
			break;
		}

		/*
		 * Virtual PMCs should only ask for a default CPU.
		 * System mode PMCs need to specify a non-default CPU.
		 */

		if ((PMC_IS_VIRTUAL_MODE(mode) && cpu != (u_int) PMC_CPU_ANY) ||
		    (PMC_IS_SYSTEM_MODE(mode) && cpu == (u_int) PMC_CPU_ANY)) {
			error = EINVAL;
			break;
		}

		/*
		 * Check that an inactive CPU is not being asked for.
		 */

		if (PMC_IS_SYSTEM_MODE(mode) && !pmc_cpu_is_active(cpu)) {
			error = ENXIO;
			break;
		}

		/*
		 * Refuse an allocation for a system-wide PMC if this
		 * process has been jailed, or if this process lacks
		 * super-user credentials and the sysctl tunable
		 * 'security.bsd.unprivileged_syspmcs' is zero.
		 */

		if (PMC_IS_SYSTEM_MODE(mode)) {
			if (jailed(curthread->td_ucred)) {
				error = EPERM;
				break;
			}
			if (!pmc_unprivileged_syspmcs) {
				error = priv_check(curthread,
				    PRIV_PMC_SYSTEM);
				if (error)
					break;
			}
		}

		/*
		 * Look for valid values for 'pm_flags'
		 */

		if ((pa.pm_flags & ~(PMC_F_DESCENDANTS | PMC_F_LOG_PROCCSW |
		    PMC_F_LOG_PROCEXIT | PMC_F_CALLCHAIN |
		    PMC_F_USERCALLCHAIN)) != 0) {
			error = EINVAL;
			break;
		}

		/* PMC_F_USERCALLCHAIN is only valid with PMC_F_CALLCHAIN */
		if ((pa.pm_flags & (PMC_F_CALLCHAIN | PMC_F_USERCALLCHAIN)) ==
		    PMC_F_USERCALLCHAIN) {
			error = EINVAL;
			break;
		}

		/* PMC_F_USERCALLCHAIN is only valid for sampling mode */
		if (pa.pm_flags & PMC_F_USERCALLCHAIN &&
			mode != PMC_MODE_TS && mode != PMC_MODE_SS) {
			error = EINVAL;
			break;
		}

		/* process logging options are not allowed for system PMCs */
		if (PMC_IS_SYSTEM_MODE(mode) && (pa.pm_flags &
		    (PMC_F_LOG_PROCCSW | PMC_F_LOG_PROCEXIT))) {
			error = EINVAL;
			break;
		}

		/*
		 * All sampling mode PMCs need to be able to interrupt the
		 * CPU.
		 */
		if (PMC_IS_SAMPLING_MODE(mode))
			caps |= PMC_CAP_INTERRUPT;

		/* A valid class specifier should have been passed in. */
		pcd = pmc_class_to_classdep(pa.pm_class);
		if (pcd == NULL) {
			error = EINVAL;
			break;
		}

		/* The requested PMC capabilities should be feasible. */
		if ((pcd->pcd_caps & caps) != caps) {
			error = EOPNOTSUPP;
			break;
		}

		PMCDBG4(PMC,ALL,2, "event=%d caps=0x%x mode=%d cpu=%d",
		    pa.pm_ev, caps, mode, cpu);

		pmc = pmc_allocate_pmc_descriptor();
		pmc->pm_id    = PMC_ID_MAKE_ID(cpu,pa.pm_mode,pa.pm_class,
		    PMC_ID_INVALID);
		pmc->pm_event = pa.pm_ev;
		pmc->pm_state = PMC_STATE_FREE;
		pmc->pm_caps  = caps;
		pmc->pm_flags = pa.pm_flags;

		/* XXX set lower bound on sampling for process counters */
		if (PMC_IS_SAMPLING_MODE(mode)) {
			/*
			 * Don't permit requested sample rate to be less than 1000
			 */
			if (pa.pm_count < 1000)
				log(LOG_WARNING,
					"pmcallocate: passed sample rate %ju - setting to 1000\n",
					(uintmax_t)pa.pm_count);
			pmc->pm_sc.pm_reloadcount = MAX(1000, pa.pm_count);
		} else
			pmc->pm_sc.pm_initial = pa.pm_count;

		/* switch thread to CPU 'cpu' */
		pmc_save_cpu_binding(&pb);

#define	PMC_IS_SHAREABLE_PMC(cpu, n)				\
	(pmc_pcpu[(cpu)]->pc_hwpmcs[(n)]->phw_state &		\
	 PMC_PHW_FLAG_IS_SHAREABLE)
#define	PMC_IS_UNALLOCATED(cpu, n)				\
	(pmc_pcpu[(cpu)]->pc_hwpmcs[(n)]->phw_pmc == NULL)

		if (PMC_IS_SYSTEM_MODE(mode)) {
			pmc_select_cpu(cpu);
			for (n = pcd->pcd_ri; n < (int) md->pmd_npmc; n++) {
				pcd = pmc_ri_to_classdep(md, n, &adjri);
				if (pmc_can_allocate_row(n, mode) == 0 &&
				    pmc_can_allocate_rowindex(
					    curthread->td_proc, n, cpu) == 0 &&
				    (PMC_IS_UNALLOCATED(cpu, n) ||
				     PMC_IS_SHAREABLE_PMC(cpu, n)) &&
				    pcd->pcd_allocate_pmc(cpu, adjri, pmc,
					&pa) == 0)
					break;
			}
		} else {
			/* Process virtual mode */
			for (n = pcd->pcd_ri; n < (int) md->pmd_npmc; n++) {
				pcd = pmc_ri_to_classdep(md, n, &adjri);
				if (pmc_can_allocate_row(n, mode) == 0 &&
				    pmc_can_allocate_rowindex(
					    curthread->td_proc, n,
					    PMC_CPU_ANY) == 0 &&
				    pcd->pcd_allocate_pmc(curthread->td_oncpu,
					adjri, pmc, &pa) == 0)
					break;
			}
		}

#undef	PMC_IS_UNALLOCATED
#undef	PMC_IS_SHAREABLE_PMC

		pmc_restore_cpu_binding(&pb);

		if (n == (int) md->pmd_npmc) {
			pmc_destroy_pmc_descriptor(pmc);
			pmc = NULL;
			error = EINVAL;
			break;
		}

		/* Fill in the correct value in the ID field */
		pmc->pm_id = PMC_ID_MAKE_ID(cpu,mode,pa.pm_class,n);

		PMCDBG5(PMC,ALL,2, "ev=%d class=%d mode=%d n=%d -> pmcid=%x",
		    pmc->pm_event, pa.pm_class, mode, n, pmc->pm_id);

		/* Process mode PMCs with logging enabled need log files */
		if (pmc->pm_flags & (PMC_F_LOG_PROCEXIT | PMC_F_LOG_PROCCSW))
			pmc->pm_flags |= PMC_F_NEEDS_LOGFILE;

		/* All system mode sampling PMCs require a log file */
		if (PMC_IS_SAMPLING_MODE(mode) && PMC_IS_SYSTEM_MODE(mode))
			pmc->pm_flags |= PMC_F_NEEDS_LOGFILE;

		/*
		 * Configure global pmc's immediately
		 */

		if (PMC_IS_SYSTEM_MODE(PMC_TO_MODE(pmc))) {

			pmc_save_cpu_binding(&pb);
			pmc_select_cpu(cpu);

			phw = pmc_pcpu[cpu]->pc_hwpmcs[n];
			pcd = pmc_ri_to_classdep(md, n, &adjri);

			if ((phw->phw_state & PMC_PHW_FLAG_IS_ENABLED) == 0 ||
			    (error = pcd->pcd_config_pmc(cpu, adjri, pmc)) != 0) {
				(void) pcd->pcd_release_pmc(cpu, adjri, pmc);
				pmc_destroy_pmc_descriptor(pmc);
				pmc = NULL;
				pmc_restore_cpu_binding(&pb);
				error = EPERM;
				break;
			}

			pmc_restore_cpu_binding(&pb);
		}

		pmc->pm_state    = PMC_STATE_ALLOCATED;
		pmc->pm_class	= pa.pm_class;

		/*
		 * mark row disposition
		 */

		if (PMC_IS_SYSTEM_MODE(mode))
			PMC_MARK_ROW_STANDALONE(n);
		else
			PMC_MARK_ROW_THREAD(n);

		/*
		 * Register this PMC with the current thread as its owner.
		 */

		if ((error =
		    pmc_register_owner(curthread->td_proc, pmc)) != 0) {
			pmc_release_pmc_descriptor(pmc);
			pmc_destroy_pmc_descriptor(pmc);
			pmc = NULL;
			break;
		}


		/*
		 * Return the allocated index.
		 */

		pa.pm_pmcid = pmc->pm_id;

		error = copyout(&pa, arg, sizeof(pa));
	}
	break;


	/*
	 * Attach a PMC to a process.
	 */

	case PMC_OP_PMCATTACH:
	{
		struct pmc *pm;
		struct proc *p;
		struct pmc_op_pmcattach a;

		sx_assert(&pmc_sx, SX_XLOCKED);

		if ((error = copyin(arg, &a, sizeof(a))) != 0)
			break;

		if (a.pm_pid < 0) {
			error = EINVAL;
			break;
		} else if (a.pm_pid == 0)
			a.pm_pid = td->td_proc->p_pid;

		if ((error = pmc_find_pmc(a.pm_pmc, &pm)) != 0)
			break;

		if (PMC_IS_SYSTEM_MODE(PMC_TO_MODE(pm))) {
			error = EINVAL;
			break;
		}

		/* PMCs may be (re)attached only when allocated or stopped */
		if (pm->pm_state == PMC_STATE_RUNNING) {
			error = EBUSY;
			break;
		} else if (pm->pm_state != PMC_STATE_ALLOCATED &&
		    pm->pm_state != PMC_STATE_STOPPED) {
			error = EINVAL;
			break;
		}

		/* lookup pid */
		if ((p = pfind(a.pm_pid)) == NULL) {
			error = ESRCH;
			break;
		}

		/*
		 * Ignore processes that are working on exiting.
		 */
		if (p->p_flag & P_WEXIT) {
			error = ESRCH;
			PROC_UNLOCK(p);	/* pfind() returns a locked process */
			break;
		}

		/*
		 * we are allowed to attach a PMC to a process if
		 * we can debug it.
		 */
		error = p_candebug(curthread, p);

		PROC_UNLOCK(p);

		if (error == 0)
			error = pmc_attach_process(p, pm);
	}
	break;


	/*
	 * Detach an attached PMC from a process.
	 */

	case PMC_OP_PMCDETACH:
	{
		struct pmc *pm;
		struct proc *p;
		struct pmc_op_pmcattach a;

		if ((error = copyin(arg, &a, sizeof(a))) != 0)
			break;

		if (a.pm_pid < 0) {
			error = EINVAL;
			break;
		} else if (a.pm_pid == 0)
			a.pm_pid = td->td_proc->p_pid;

		if ((error = pmc_find_pmc(a.pm_pmc, &pm)) != 0)
			break;

		if ((p = pfind(a.pm_pid)) == NULL) {
			error = ESRCH;
			break;
		}

		/*
		 * Treat processes that are in the process of exiting
		 * as if they were not present.
		 */

		if (p->p_flag & P_WEXIT)
			error = ESRCH;

		PROC_UNLOCK(p);	/* pfind() returns a locked process */

		if (error == 0)
			error = pmc_detach_process(p, pm);
	}
	break;


	/*
	 * Retrieve the MSR number associated with the counter
	 * 'pmc_id'.  This allows processes to directly use RDPMC
	 * instructions to read their PMCs, without the overhead of a
	 * system call.
	 */

	case PMC_OP_PMCGETMSR:
	{
		int adjri, ri;
		struct pmc *pm;
		struct pmc_target *pt;
		struct pmc_op_getmsr gm;
		struct pmc_classdep *pcd;

		PMC_DOWNGRADE_SX();

		if ((error = copyin(arg, &gm, sizeof(gm))) != 0)
			break;

		if ((error = pmc_find_pmc(gm.pm_pmcid, &pm)) != 0)
			break;

		/*
		 * The allocated PMC has to be a process virtual PMC,
		 * i.e., of type MODE_T[CS].  Global PMCs can only be
		 * read using the PMCREAD operation since they may be
		 * allocated on a different CPU than the one we could
		 * be running on at the time of the RDPMC instruction.
		 *
		 * The GETMSR operation is not allowed for PMCs that
		 * are inherited across processes.
		 */

		if (!PMC_IS_VIRTUAL_MODE(PMC_TO_MODE(pm)) ||
		    (pm->pm_flags & PMC_F_DESCENDANTS)) {
			error = EINVAL;
			break;
		}

		/*
		 * It only makes sense to use a RDPMC (or its
		 * equivalent instruction on non-x86 architectures) on
		 * a process that has allocated and attached a PMC to
		 * itself.  Conversely the PMC is only allowed to have
		 * one process attached to it -- its owner.
		 */

		if ((pt = LIST_FIRST(&pm->pm_targets)) == NULL ||
		    LIST_NEXT(pt, pt_next) != NULL ||
		    pt->pt_process->pp_proc != pm->pm_owner->po_owner) {
			error = EINVAL;
			break;
		}

		ri = PMC_TO_ROWINDEX(pm);
		pcd = pmc_ri_to_classdep(md, ri, &adjri);

		/* PMC class has no 'GETMSR' support */
		if (pcd->pcd_get_msr == NULL) {
			error = ENOSYS;
			break;
		}

		if ((error = (*pcd->pcd_get_msr)(adjri, &gm.pm_msr)) < 0)
			break;

		if ((error = copyout(&gm, arg, sizeof(gm))) < 0)
			break;

		/*
		 * Mark our process as using MSRs.  Update machine
		 * state using a forced context switch.
		 */

		pt->pt_process->pp_flags |= PMC_PP_ENABLE_MSR_ACCESS;
		pmc_force_context_switch();

	}
	break;

	/*
	 * Release an allocated PMC
	 */

	case PMC_OP_PMCRELEASE:
	{
		pmc_id_t pmcid;
		struct pmc *pm;
		struct pmc_owner *po;
		struct pmc_op_simple sp;

		/*
		 * Find PMC pointer for the named PMC.
		 *
		 * Use pmc_release_pmc_descriptor() to switch off the
		 * PMC, remove all its target threads, and remove the
		 * PMC from its owner's list.
		 *
		 * Remove the owner record if this is the last PMC
		 * owned.
		 *
		 * Free up space.
		 */

		if ((error = copyin(arg, &sp, sizeof(sp))) != 0)
			break;

		pmcid = sp.pm_pmcid;

		if ((error = pmc_find_pmc(pmcid, &pm)) != 0)
			break;

		po = pm->pm_owner;
		pmc_release_pmc_descriptor(pm);
		pmc_maybe_remove_owner(po);
		pmc_destroy_pmc_descriptor(pm);
	}
	break;


	/*
	 * Read and/or write a PMC.
	 */

	case PMC_OP_PMCRW:
	{
		int adjri;
		struct pmc *pm;
		uint32_t cpu, ri;
		pmc_value_t oldvalue;
		struct pmc_binding pb;
		struct pmc_op_pmcrw prw;
		struct pmc_classdep *pcd;
		struct pmc_op_pmcrw *pprw;

		PMC_DOWNGRADE_SX();

		if ((error = copyin(arg, &prw, sizeof(prw))) != 0)
			break;

		ri = 0;
		PMCDBG2(PMC,OPS,1, "rw id=%d flags=0x%x", prw.pm_pmcid,
		    prw.pm_flags);

		/* must have at least one flag set */
		if ((prw.pm_flags & (PMC_F_OLDVALUE|PMC_F_NEWVALUE)) == 0) {
			error = EINVAL;
			break;
		}

		/* locate pmc descriptor */
		if ((error = pmc_find_pmc(prw.pm_pmcid, &pm)) != 0)
			break;

		/* Can't read a PMC that hasn't been started. */
		if (pm->pm_state != PMC_STATE_ALLOCATED &&
		    pm->pm_state != PMC_STATE_STOPPED &&
		    pm->pm_state != PMC_STATE_RUNNING) {
			error = EINVAL;
			break;
		}

		/* writing a new value is allowed only for 'STOPPED' pmcs */
		if (pm->pm_state == PMC_STATE_RUNNING &&
		    (prw.pm_flags & PMC_F_NEWVALUE)) {
			error = EBUSY;
			break;
		}

		if (PMC_IS_VIRTUAL_MODE(PMC_TO_MODE(pm))) {

			/*
			 * If this PMC is attached to its owner (i.e.,
			 * the process requesting this operation) and
			 * is running, then attempt to get an
			 * upto-date reading from hardware for a READ.
			 * Writes are only allowed when the PMC is
			 * stopped, so only update the saved value
			 * field.
			 *
			 * If the PMC is not running, or is not
			 * attached to its owner, read/write to the
			 * savedvalue field.
			 */

			ri = PMC_TO_ROWINDEX(pm);
			pcd = pmc_ri_to_classdep(md, ri, &adjri);

			mtx_pool_lock_spin(pmc_mtxpool, pm);
			cpu = curthread->td_oncpu;

			if (prw.pm_flags & PMC_F_OLDVALUE) {
				if ((pm->pm_flags & PMC_F_ATTACHED_TO_OWNER) &&
				    (pm->pm_state == PMC_STATE_RUNNING))
					error = (*pcd->pcd_read_pmc)(cpu, adjri,
					    &oldvalue);
				else
					oldvalue = pm->pm_gv.pm_savedvalue;
			}
			if (prw.pm_flags & PMC_F_NEWVALUE)
				pm->pm_gv.pm_savedvalue = prw.pm_value;

			mtx_pool_unlock_spin(pmc_mtxpool, pm);

		} else { /* System mode PMCs */
			cpu = PMC_TO_CPU(pm);
			ri  = PMC_TO_ROWINDEX(pm);
			pcd = pmc_ri_to_classdep(md, ri, &adjri);

			if (!pmc_cpu_is_active(cpu)) {
				error = ENXIO;
				break;
			}

			/* move this thread to CPU 'cpu' */
			pmc_save_cpu_binding(&pb);
			pmc_select_cpu(cpu);

			critical_enter();
			/* save old value */
			if (prw.pm_flags & PMC_F_OLDVALUE)
				if ((error = (*pcd->pcd_read_pmc)(cpu, adjri,
					 &oldvalue)))
					goto error;
			/* write out new value */
			if (prw.pm_flags & PMC_F_NEWVALUE)
				error = (*pcd->pcd_write_pmc)(cpu, adjri,
				    prw.pm_value);
		error:
			critical_exit();
			pmc_restore_cpu_binding(&pb);
			if (error)
				break;
		}

		pprw = (struct pmc_op_pmcrw *) arg;

#ifdef	HWPMC_DEBUG
		if (prw.pm_flags & PMC_F_NEWVALUE)
			PMCDBG3(PMC,OPS,2, "rw id=%d new %jx -> old %jx",
			    ri, prw.pm_value, oldvalue);
		else if (prw.pm_flags & PMC_F_OLDVALUE)
			PMCDBG2(PMC,OPS,2, "rw id=%d -> old %jx", ri, oldvalue);
#endif

		/* return old value if requested */
		if (prw.pm_flags & PMC_F_OLDVALUE)
			if ((error = copyout(&oldvalue, &pprw->pm_value,
				 sizeof(prw.pm_value))))
				break;

	}
	break;


	/*
	 * Set the sampling rate for a sampling mode PMC and the
	 * initial count for a counting mode PMC.
	 */

	case PMC_OP_PMCSETCOUNT:
	{
		struct pmc *pm;
		struct pmc_op_pmcsetcount sc;

		PMC_DOWNGRADE_SX();

		if ((error = copyin(arg, &sc, sizeof(sc))) != 0)
			break;

		if ((error = pmc_find_pmc(sc.pm_pmcid, &pm)) != 0)
			break;

		if (pm->pm_state == PMC_STATE_RUNNING) {
			error = EBUSY;
			break;
		}

		if (PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm))) {
			/*
			 * Don't permit requested sample rate to be less than 1000
			 */
			if (sc.pm_count < 1000)
				log(LOG_WARNING,
					"pmcsetcount: passed sample rate %ju - setting to 1000\n",
					(uintmax_t)sc.pm_count);
			pm->pm_sc.pm_reloadcount = MAX(1000, sc.pm_count);
		} else
			pm->pm_sc.pm_initial = sc.pm_count;
	}
	break;


	/*
	 * Start a PMC.
	 */

	case PMC_OP_PMCSTART:
	{
		pmc_id_t pmcid;
		struct pmc *pm;
		struct pmc_op_simple sp;

		sx_assert(&pmc_sx, SX_XLOCKED);

		if ((error = copyin(arg, &sp, sizeof(sp))) != 0)
			break;

		pmcid = sp.pm_pmcid;

		if ((error = pmc_find_pmc(pmcid, &pm)) != 0)
			break;

		KASSERT(pmcid == pm->pm_id,
		    ("[pmc,%d] pmcid %x != id %x", __LINE__,
			pm->pm_id, pmcid));

		if (pm->pm_state == PMC_STATE_RUNNING) /* already running */
			break;
		else if (pm->pm_state != PMC_STATE_STOPPED &&
		    pm->pm_state != PMC_STATE_ALLOCATED) {
			error = EINVAL;
			break;
		}

		error = pmc_start(pm);
	}
	break;


	/*
	 * Stop a PMC.
	 */

	case PMC_OP_PMCSTOP:
	{
		pmc_id_t pmcid;
		struct pmc *pm;
		struct pmc_op_simple sp;

		PMC_DOWNGRADE_SX();

		if ((error = copyin(arg, &sp, sizeof(sp))) != 0)
			break;

		pmcid = sp.pm_pmcid;

		/*
		 * Mark the PMC as inactive and invoke the MD stop
		 * routines if needed.
		 */

		if ((error = pmc_find_pmc(pmcid, &pm)) != 0)
			break;

		KASSERT(pmcid == pm->pm_id,
		    ("[pmc,%d] pmc id %x != pmcid %x", __LINE__,
			pm->pm_id, pmcid));

		if (pm->pm_state == PMC_STATE_STOPPED) /* already stopped */
			break;
		else if (pm->pm_state != PMC_STATE_RUNNING) {
			error = EINVAL;
			break;
		}

		error = pmc_stop(pm);
	}
	break;


	/*
	 * Write a user supplied value to the log file.
	 */

	case PMC_OP_WRITELOG:
	{
		struct pmc_op_writelog wl;
		struct pmc_owner *po;

		PMC_DOWNGRADE_SX();

		if ((error = copyin(arg, &wl, sizeof(wl))) != 0)
			break;

		if ((po = pmc_find_owner_descriptor(td->td_proc)) == NULL) {
			error = EINVAL;
			break;
		}

		if ((po->po_flags & PMC_PO_OWNS_LOGFILE) == 0) {
			error = EINVAL;
			break;
		}

		error = pmclog_process_userlog(po, &wl);
	}
	break;


	default:
		error = EINVAL;
		break;
	}

	if (is_sx_downgraded)
		sx_sunlock(&pmc_sx);
	else
		sx_xunlock(&pmc_sx);
done_syscall:
	if (error)
		counter_u64_add(pmc_stats.pm_syscall_errors, 1);

	return (error);
}

/*
 * Helper functions
 */


/*
 * Mark the thread as needing callchain capture and post an AST.  The
 * actual callchain capture will be done in a context where it is safe
 * to take page faults.
 */

static void
pmc_post_callchain_callback(void)
{
	struct thread *td;

	td = curthread;

	/*
	 * If there is multiple PMCs for the same interrupt ignore new post
	 */
	if (td->td_pflags & TDP_CALLCHAIN)
		return;

	/*
	 * Mark this thread as needing callchain capture.
	 * `td->td_pflags' will be safe to touch because this thread
	 * was in user space when it was interrupted.
	 */
	td->td_pflags |= TDP_CALLCHAIN;

	/*
	 * Don't let this thread migrate between CPUs until callchain
	 * capture completes.
	 */
	sched_pin();

	return;
}

/*
 * Find a free slot in the per-cpu array of samples and capture the
 * current callchain there.  If a sample was successfully added, a bit
 * is set in mask 'pmc_cpumask' denoting that the DO_SAMPLES hook
 * needs to be invoked from the clock handler.
 *
 * This function is meant to be called from an NMI handler.  It cannot
 * use any of the locking primitives supplied by the OS.
 */

static int
pmc_add_sample(ring_type_t ring, struct pmc *pm, struct trapframe *tf)
{
	int error, cpu, callchaindepth, inuserspace;
	struct thread *td;
	struct pmc_sample *ps;
	struct pmc_samplebuffer *psb;

	error = 0;

	/*
	 * Allocate space for a sample buffer.
	 */
	cpu = curcpu;
	psb = pmc_pcpu[cpu]->pc_sb[ring];
	inuserspace = TRAPF_USERMODE(tf);
	ps = PMC_PROD_SAMPLE(psb);
	if (psb->ps_considx != psb->ps_prodidx &&
		ps->ps_nsamples) {	/* in use, reader hasn't caught up */
		pm->pm_pcpu_state[cpu].pps_stalled = 1;
		counter_u64_add(pmc_stats.pm_intr_bufferfull, 1);
		PMCDBG6(SAM,INT,1,"(spc) cpu=%d pm=%p tf=%p um=%d wr=%d rd=%d",
		    cpu, pm, (void *) tf, inuserspace,
		    (int) (psb->ps_prodidx & pmc_sample_mask),
		    (int) (psb->ps_considx & pmc_sample_mask));
		callchaindepth = 1;
		error = ENOMEM;
		goto done;
	}

	/* Fill in entry. */
	PMCDBG6(SAM,INT,1,"cpu=%d pm=%p tf=%p um=%d wr=%d rd=%d", cpu, pm,
	    (void *) tf, inuserspace,
	    (int) (psb->ps_prodidx & pmc_sample_mask),
	    (int) (psb->ps_considx & pmc_sample_mask));

	td = curthread;
	ps->ps_pmc = pm;
	ps->ps_td = td;
	ps->ps_pid = td->td_proc->p_pid;
	ps->ps_tid = td->td_tid;
	ps->ps_tsc = pmc_rdtsc();
	ps->ps_ticks = ticks;
	ps->ps_cpu = cpu;
	ps->ps_flags = inuserspace ? PMC_CC_F_USERSPACE : 0;

	callchaindepth = (pm->pm_flags & PMC_F_CALLCHAIN) ?
	    pmc_callchaindepth : 1;

	MPASS(ps->ps_pc != NULL);
	if (callchaindepth == 1)
		ps->ps_pc[0] = PMC_TRAPFRAME_TO_PC(tf);
	else {
		/*
		 * Kernel stack traversals can be done immediately,
		 * while we defer to an AST for user space traversals.
		 */
		if (!inuserspace) {
			callchaindepth =
			    pmc_save_kernel_callchain(ps->ps_pc,
				callchaindepth, tf);
		} else {
			pmc_post_callchain_callback();
			callchaindepth = PMC_USER_CALLCHAIN_PENDING;
		}
	}

	ps->ps_nsamples = callchaindepth;	/* mark entry as in use */
	if (ring == PMC_UR) {
		ps->ps_nsamples_actual = callchaindepth;	/* mark entry as in use */
		ps->ps_nsamples = PMC_USER_CALLCHAIN_PENDING;
	} else
		ps->ps_nsamples = callchaindepth;	/* mark entry as in use */

	KASSERT(counter_u64_fetch(pm->pm_runcount) >= 0,
	    ("[pmc,%d] pm=%p runcount %ld", __LINE__, (void *) pm,
		 (unsigned long)counter_u64_fetch(pm->pm_runcount)));

	counter_u64_add(pm->pm_runcount, 1);	/* hold onto PMC */
	/* increment write pointer */
	psb->ps_prodidx++;
 done:
	/* mark CPU as needing processing */
	if (callchaindepth != PMC_USER_CALLCHAIN_PENDING)
		DPCPU_SET(pmc_sampled, 1);

	return (error);
}

/*
 * Interrupt processing.
 *
 * This function is meant to be called from an NMI handler.  It cannot
 * use any of the locking primitives supplied by the OS.
 */

int
pmc_process_interrupt(int ring, struct pmc *pm, struct trapframe *tf)
{
	struct thread *td;

	td = curthread;
	if ((pm->pm_flags & PMC_F_USERCALLCHAIN) &&
	    (td->td_proc->p_flag & P_KPROC) == 0 &&
	    !TRAPF_USERMODE(tf)) {
		atomic_add_int(&td->td_pmcpend, 1);
		return (pmc_add_sample(PMC_UR, pm, tf));
	}
	return (pmc_add_sample(ring, pm, tf));
}

/*
 * Capture a user call chain.  This function will be called from ast()
 * before control returns to userland and before the process gets
 * rescheduled.
 */

static void
pmc_capture_user_callchain(int cpu, int ring, struct trapframe *tf)
{
	struct pmc *pm;
	struct thread *td;
	struct pmc_sample *ps;
	struct pmc_samplebuffer *psb;
	uint64_t considx, prodidx;
	int nsamples, nrecords, pass, iter;
#ifdef	INVARIANTS
	int ncallchains;
	int nfree;
	int start_ticks = ticks;
#endif
	psb = pmc_pcpu[cpu]->pc_sb[ring];
	td = curthread;

	KASSERT(td->td_pflags & TDP_CALLCHAIN,
	    ("[pmc,%d] Retrieving callchain for thread that doesn't want it",
		__LINE__));

#ifdef	INVARIANTS
	ncallchains = 0;
	nfree = 0;
#endif
	nrecords = INT_MAX;
	pass = 0;
 restart:
	if (ring == PMC_UR)
		nrecords = atomic_readandclear_32(&td->td_pmcpend);

	for (iter = 0, considx = psb->ps_considx, prodidx = psb->ps_prodidx;
	    considx < prodidx && iter < pmc_nsamples; considx++, iter++) {
		ps = PMC_CONS_SAMPLE_OFF(psb, considx);

	/*
	 * Iterate through all deferred callchain requests.
	 * Walk from the current read pointer to the current
	 * write pointer.
	 */

#ifdef	INVARIANTS
		if (ps->ps_nsamples == PMC_SAMPLE_FREE) {
			nfree++;
			continue;
		}

		if ((ps->ps_pmc == NULL) ||
		    (ps->ps_pmc->pm_state != PMC_STATE_RUNNING))
			nfree++;
#endif
		if (ps->ps_td != td ||
		   ps->ps_nsamples == PMC_USER_CALLCHAIN_PENDING ||
		   ps->ps_pmc->pm_state != PMC_STATE_RUNNING)
			continue;

		KASSERT(ps->ps_cpu == cpu,
		    ("[pmc,%d] cpu mismatch ps_cpu=%d pcpu=%d", __LINE__,
			ps->ps_cpu, PCPU_GET(cpuid)));

		pm = ps->ps_pmc;

		KASSERT(pm->pm_flags & PMC_F_CALLCHAIN,
		    ("[pmc,%d] Retrieving callchain for PMC that doesn't "
			"want it", __LINE__));

		KASSERT(counter_u64_fetch(pm->pm_runcount) > 0,
		    ("[pmc,%d] runcount %ld", __LINE__, (unsigned long)counter_u64_fetch(pm->pm_runcount)));

		if (ring == PMC_UR) {
			nsamples = ps->ps_nsamples_actual;
			counter_u64_add(pmc_stats.pm_merges, 1);
		} else
			nsamples = 0;

		/*
		 * Retrieve the callchain and mark the sample buffer
		 * as 'processable' by the timer tick sweep code.
		 */

#ifdef INVARIANTS
		ncallchains++;
#endif

		if (__predict_true(nsamples < pmc_callchaindepth - 1))
			nsamples += pmc_save_user_callchain(ps->ps_pc + nsamples,
		       pmc_callchaindepth - nsamples - 1, tf);

		/*
		 * We have to prevent hardclock from potentially overwriting
		 * this sample between when we read the value and when we set
		 * it
		 */
		spinlock_enter();
		/*
		 * Verify that the sample hasn't been dropped in the meantime
		 */
		if (ps->ps_nsamples == PMC_USER_CALLCHAIN_PENDING) {
			ps->ps_nsamples = nsamples;
			/*
			 * If we couldn't get a sample, simply drop the reference
			 */
			if (nsamples == 0)
				counter_u64_add(pm->pm_runcount, -1);
		}
		spinlock_exit();
		if (nrecords-- == 1)
			break;
	}
	if (__predict_false(ring == PMC_UR && td->td_pmcpend)) {
		if (pass == 0) {
			pass = 1;
			goto restart;
		}
		/* only collect samples for this part once */
		td->td_pmcpend = 0;
	}

#ifdef INVARIANTS
	if ((ticks - start_ticks) > hz)
		log(LOG_ERR, "%s took %d ticks\n", __func__, (ticks - start_ticks));
#endif

	/* mark CPU as needing processing */
	DPCPU_SET(pmc_sampled, 1);
}

/*
 * Process saved PC samples.
 */

static void
pmc_process_samples(int cpu, ring_type_t ring)
{
	struct pmc *pm;
	int adjri, n;
	struct thread *td;
	struct pmc_owner *po;
	struct pmc_sample *ps;
	struct pmc_classdep *pcd;
	struct pmc_samplebuffer *psb;
	uint64_t delta;

	KASSERT(PCPU_GET(cpuid) == cpu,
	    ("[pmc,%d] not on the correct CPU pcpu=%d cpu=%d", __LINE__,
		PCPU_GET(cpuid), cpu));

	psb = pmc_pcpu[cpu]->pc_sb[ring];
	delta = psb->ps_prodidx - psb->ps_considx;
	MPASS(delta <= pmc_nsamples);
	MPASS(psb->ps_considx <= psb->ps_prodidx);
	for (n = 0; psb->ps_considx < psb->ps_prodidx; psb->ps_considx++, n++) {
		ps = PMC_CONS_SAMPLE(psb);

		if (__predict_false(ps->ps_nsamples == PMC_SAMPLE_FREE))
			continue;
		pm = ps->ps_pmc;
		/* skip non-running samples */
		if (pm->pm_state != PMC_STATE_RUNNING)
			goto entrydone;

		KASSERT(counter_u64_fetch(pm->pm_runcount) > 0,
		    ("[pmc,%d] pm=%p runcount %ld", __LINE__, (void *) pm,
			 (unsigned long)counter_u64_fetch(pm->pm_runcount)));

		po = pm->pm_owner;

		KASSERT(PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)),
		    ("[pmc,%d] pmc=%p non-sampling mode=%d", __LINE__,
			pm, PMC_TO_MODE(pm)));


		/* If there is a pending AST wait for completion */
		if (ps->ps_nsamples == PMC_USER_CALLCHAIN_PENDING) {
			/* if we've been waiting more than 1 tick to 
			 * collect a callchain for this record then
			 * drop it and move on.
			 */
			if (ticks - ps->ps_ticks > 1) {
				/*
				 * track how often we hit this as it will
				 * preferentially lose user samples
				 * for long running system calls 
				 */
				counter_u64_add(pmc_stats.pm_overwrites, 1);
				goto entrydone;
			}
			/* Need a rescan at a later time. */
			DPCPU_SET(pmc_sampled, 1);
			break;
		}

		PMCDBG6(SAM,OPS,1,"cpu=%d pm=%p n=%d fl=%x wr=%d rd=%d", cpu,
		    pm, ps->ps_nsamples, ps->ps_flags,
		    (int) (psb->ps_prodidx & pmc_sample_mask),
		    (int) (psb->ps_considx & pmc_sample_mask));

		/*
		 * If this is a process-mode PMC that is attached to
		 * its owner, and if the PC is in user mode, update
		 * profiling statistics like timer-based profiling
		 * would have done.
		 *
		 * Otherwise, this is either a sampling-mode PMC that
		 * is attached to a different process than its owner,
		 * or a system-wide sampling PMC. Dispatch a log
		 * entry to the PMC's owner process.
		 */
		if (pm->pm_flags & PMC_F_ATTACHED_TO_OWNER) {
			if (ps->ps_flags & PMC_CC_F_USERSPACE) {
				td = FIRST_THREAD_IN_PROC(po->po_owner);
				addupc_intr(td, ps->ps_pc[0], 1);
			}
		} else
			pmclog_process_callchain(pm, ps);

	entrydone:
		ps->ps_nsamples = 0; /* mark entry as free */
		KASSERT(counter_u64_fetch(pm->pm_runcount) > 0,
				("[pmc,%d] pm=%p runcount %ld", __LINE__, (void *) pm,
				 (unsigned long)counter_u64_fetch(pm->pm_runcount)));

		counter_u64_add(pm->pm_runcount, -1);
	}

	counter_u64_add(pmc_stats.pm_log_sweeps, 1);

	/* Do not re-enable stalled PMCs if we failed to process any samples */
	if (n == 0)
		return;

	/*
	 * Restart any stalled sampling PMCs on this CPU.
	 *
	 * If the NMI handler sets the pm_stalled field of a PMC after
	 * the check below, we'll end up processing the stalled PMC at
	 * the next hardclock tick.
	 */
	for (n = 0; n < md->pmd_npmc; n++) {
		pcd = pmc_ri_to_classdep(md, n, &adjri);
		KASSERT(pcd != NULL,
		    ("[pmc,%d] null pcd ri=%d", __LINE__, n));
		(void) (*pcd->pcd_get_config)(cpu,adjri,&pm);

		if (pm == NULL ||			 /* !cfg'ed */
		    pm->pm_state != PMC_STATE_RUNNING || /* !active */
		    !PMC_IS_SAMPLING_MODE(PMC_TO_MODE(pm)) || /* !sampling */
			!pm->pm_pcpu_state[cpu].pps_cpustate  || /* !desired */
		    !pm->pm_pcpu_state[cpu].pps_stalled) /* !stalled */
			continue;

		pm->pm_pcpu_state[cpu].pps_stalled = 0;
		(*pcd->pcd_start_pmc)(cpu, adjri);
	}
}

/*
 * Event handlers.
 */

/*
 * Handle a process exit.
 *
 * Remove this process from all hash tables.  If this process
 * owned any PMCs, turn off those PMCs and deallocate them,
 * removing any associations with target processes.
 *
 * This function will be called by the last 'thread' of a
 * process.
 *
 * XXX This eventhandler gets called early in the exit process.
 * Consider using a 'hook' invocation from thread_exit() or equivalent
 * spot.  Another negative is that kse_exit doesn't seem to call
 * exit1() [??].
 *
 */

static void
pmc_process_exit(void *arg __unused, struct proc *p)
{
	struct pmc *pm;
	int adjri, cpu;
	unsigned int ri;
	int is_using_hwpmcs;
	struct pmc_owner *po;
	struct pmc_process *pp;
	struct pmc_classdep *pcd;
	pmc_value_t newvalue, tmp;

	PROC_LOCK(p);
	is_using_hwpmcs = p->p_flag & P_HWPMC;
	PROC_UNLOCK(p);

	/*
	 * Log a sysexit event to all SS PMC owners.
	 */
	PMC_EPOCH_ENTER();
	CK_LIST_FOREACH(po, &pmc_ss_owners, po_ssnext)
	    if (po->po_flags & PMC_PO_OWNS_LOGFILE)
		    pmclog_process_sysexit(po, p->p_pid);
	PMC_EPOCH_EXIT();

	if (!is_using_hwpmcs)
		return;

	PMC_GET_SX_XLOCK();
	PMCDBG3(PRC,EXT,1,"process-exit proc=%p (%d, %s)", p, p->p_pid,
	    p->p_comm);

	/*
	 * Since this code is invoked by the last thread in an exiting
	 * process, we would have context switched IN at some prior
	 * point.  However, with PREEMPTION, kernel mode context
	 * switches may happen any time, so we want to disable a
	 * context switch OUT till we get any PMCs targeting this
	 * process off the hardware.
	 *
	 * We also need to atomically remove this process'
	 * entry from our target process hash table, using
	 * PMC_FLAG_REMOVE.
	 */
	PMCDBG3(PRC,EXT,1, "process-exit proc=%p (%d, %s)", p, p->p_pid,
	    p->p_comm);

	critical_enter(); /* no preemption */

	cpu = curthread->td_oncpu;

	if ((pp = pmc_find_process_descriptor(p,
		 PMC_FLAG_REMOVE)) != NULL) {

		PMCDBG2(PRC,EXT,2,
		    "process-exit proc=%p pmc-process=%p", p, pp);

		/*
		 * The exiting process could the target of
		 * some PMCs which will be running on
		 * currently executing CPU.
		 *
		 * We need to turn these PMCs off like we
		 * would do at context switch OUT time.
		 */
		for (ri = 0; ri < md->pmd_npmc; ri++) {

			/*
			 * Pick up the pmc pointer from hardware
			 * state similar to the CSW_OUT code.
			 */
			pm = NULL;

			pcd = pmc_ri_to_classdep(md, ri, &adjri);

			(void) (*pcd->pcd_get_config)(cpu, adjri, &pm);

			PMCDBG2(PRC,EXT,2, "ri=%d pm=%p", ri, pm);

			if (pm == NULL ||
			    !PMC_IS_VIRTUAL_MODE(PMC_TO_MODE(pm)))
				continue;

			PMCDBG4(PRC,EXT,2, "ppmcs[%d]=%p pm=%p "
			    "state=%d", ri, pp->pp_pmcs[ri].pp_pmc,
			    pm, pm->pm_state);

			KASSERT(PMC_TO_ROWINDEX(pm) == ri,
			    ("[pmc,%d] ri mismatch pmc(%d) ri(%d)",
				__LINE__, PMC_TO_ROWINDEX(pm), ri));

			KASSERT(pm == pp->pp_pmcs[ri].pp_pmc,
			    ("[pmc,%d] pm %p != pp_pmcs[%d] %p",
				__LINE__, pm, ri, pp->pp_pmcs[ri].pp_pmc));

			KASSERT(counter_u64_fetch(pm->pm_runcount) > 0,
			    ("[pmc,%d] bad runcount ri %d rc %ld",
				 __LINE__, ri, (unsigned long)counter_u64_fetch(pm->pm_runcount)));

			/*
			 * Change desired state, and then stop if not
			 * stalled. This two-step dance should avoid
			 * race conditions where an interrupt re-enables
			 * the PMC after this code has already checked
			 * the pm_stalled flag.
			 */
			if (pm->pm_pcpu_state[cpu].pps_cpustate) {
				pm->pm_pcpu_state[cpu].pps_cpustate = 0;
				if (!pm->pm_pcpu_state[cpu].pps_stalled) {
					(void) pcd->pcd_stop_pmc(cpu, adjri);

					if (PMC_TO_MODE(pm) == PMC_MODE_TC) {
						pcd->pcd_read_pmc(cpu, adjri,
						    &newvalue);
						tmp = newvalue -
						    PMC_PCPU_SAVED(cpu,ri);

						mtx_pool_lock_spin(pmc_mtxpool,
						    pm);
						pm->pm_gv.pm_savedvalue += tmp;
						pp->pp_pmcs[ri].pp_pmcval +=
						    tmp;
						mtx_pool_unlock_spin(
						    pmc_mtxpool, pm);
					}
				}
			}

			KASSERT((int64_t) counter_u64_fetch(pm->pm_runcount) > 0,
			    ("[pmc,%d] runcount is %d", __LINE__, ri));

			counter_u64_add(pm->pm_runcount, -1);

			(void) pcd->pcd_config_pmc(cpu, adjri, NULL);
		}

		/*
		 * Inform the MD layer of this pseudo "context switch
		 * out"
		 */
		(void) md->pmd_switch_out(pmc_pcpu[cpu], pp);

		critical_exit(); /* ok to be pre-empted now */

		/*
		 * Unlink this process from the PMCs that are
		 * targeting it.  This will send a signal to
		 * all PMC owner's whose PMCs are orphaned.
		 *
		 * Log PMC value at exit time if requested.
		 */
		for (ri = 0; ri < md->pmd_npmc; ri++)
			if ((pm = pp->pp_pmcs[ri].pp_pmc) != NULL) {
				if (pm->pm_flags & PMC_F_NEEDS_LOGFILE &&
				    PMC_IS_COUNTING_MODE(PMC_TO_MODE(pm)))
					pmclog_process_procexit(pm, pp);
				pmc_unlink_target_process(pm, pp);
			}
		free(pp, M_PMC);

	} else
		critical_exit(); /* pp == NULL */


	/*
	 * If the process owned PMCs, free them up and free up
	 * memory.
	 */
	if ((po = pmc_find_owner_descriptor(p)) != NULL) {
		pmc_remove_owner(po);
		pmc_destroy_owner_descriptor(po);
	}

	sx_xunlock(&pmc_sx);
}

/*
 * Handle a process fork.
 *
 * If the parent process 'p1' is under HWPMC monitoring, then copy
 * over any attached PMCs that have 'do_descendants' semantics.
 */

static void
pmc_process_fork(void *arg __unused, struct proc *p1, struct proc *newproc,
    int flags)
{
	int is_using_hwpmcs;
	unsigned int ri;
	uint32_t do_descendants;
	struct pmc *pm;
	struct pmc_owner *po;
	struct pmc_process *ppnew, *ppold;

	(void) flags;		/* unused parameter */

	PROC_LOCK(p1);
	is_using_hwpmcs = p1->p_flag & P_HWPMC;
	PROC_UNLOCK(p1);

	/*
	 * If there are system-wide sampling PMCs active, we need to
	 * log all fork events to their owner's logs.
	 */
	PMC_EPOCH_ENTER();
	CK_LIST_FOREACH(po, &pmc_ss_owners, po_ssnext)
	    if (po->po_flags & PMC_PO_OWNS_LOGFILE) {
		    pmclog_process_procfork(po, p1->p_pid, newproc->p_pid);
			pmclog_process_proccreate(po, newproc, 1);
		}
	PMC_EPOCH_EXIT();

	if (!is_using_hwpmcs)
		return;

	PMC_GET_SX_XLOCK();
	PMCDBG4(PMC,FRK,1, "process-fork proc=%p (%d, %s) -> %p", p1,
	    p1->p_pid, p1->p_comm, newproc);

	/*
	 * If the parent process (curthread->td_proc) is a
	 * target of any PMCs, look for PMCs that are to be
	 * inherited, and link these into the new process
	 * descriptor.
	 */
	if ((ppold = pmc_find_process_descriptor(curthread->td_proc,
		 PMC_FLAG_NONE)) == NULL)
		goto done;		/* nothing to do */

	do_descendants = 0;
	for (ri = 0; ri < md->pmd_npmc; ri++)
		if ((pm = ppold->pp_pmcs[ri].pp_pmc) != NULL)
			do_descendants |= pm->pm_flags & PMC_F_DESCENDANTS;
	if (do_descendants == 0) /* nothing to do */
		goto done;

	/*
	 * Now mark the new process as being tracked by this driver.
	 */
	PROC_LOCK(newproc);
	newproc->p_flag |= P_HWPMC;
	PROC_UNLOCK(newproc);

	/* allocate a descriptor for the new process  */
	if ((ppnew = pmc_find_process_descriptor(newproc,
		 PMC_FLAG_ALLOCATE)) == NULL)
		goto done;

	/*
	 * Run through all PMCs that were targeting the old process
	 * and which specified F_DESCENDANTS and attach them to the
	 * new process.
	 *
	 * Log the fork event to all owners of PMCs attached to this
	 * process, if not already logged.
	 */
	for (ri = 0; ri < md->pmd_npmc; ri++)
		if ((pm = ppold->pp_pmcs[ri].pp_pmc) != NULL &&
		    (pm->pm_flags & PMC_F_DESCENDANTS)) {
			pmc_link_target_process(pm, ppnew);
			po = pm->pm_owner;
			if (po->po_sscount == 0 &&
			    po->po_flags & PMC_PO_OWNS_LOGFILE)
				pmclog_process_procfork(po, p1->p_pid,
				    newproc->p_pid);
		}

 done:
	sx_xunlock(&pmc_sx);
}

static void
pmc_process_threadcreate(struct thread *td)
{
	struct pmc_owner *po;

	PMC_EPOCH_ENTER();
	CK_LIST_FOREACH(po, &pmc_ss_owners, po_ssnext)
	    if (po->po_flags & PMC_PO_OWNS_LOGFILE)
			pmclog_process_threadcreate(po, td, 1);
	PMC_EPOCH_EXIT();
}

static void
pmc_process_threadexit(struct thread *td)
{
	struct pmc_owner *po;

	PMC_EPOCH_ENTER();
	CK_LIST_FOREACH(po, &pmc_ss_owners, po_ssnext)
	    if (po->po_flags & PMC_PO_OWNS_LOGFILE)
			pmclog_process_threadexit(po, td);
	PMC_EPOCH_EXIT();
}

static void
pmc_process_proccreate(struct proc *p)
{
	struct pmc_owner *po;

	PMC_EPOCH_ENTER();
	CK_LIST_FOREACH(po, &pmc_ss_owners, po_ssnext)
	    if (po->po_flags & PMC_PO_OWNS_LOGFILE)
			pmclog_process_proccreate(po, p, 1 /* sync */);
	PMC_EPOCH_EXIT();
}

static void
pmc_process_allproc(struct pmc *pm)
{
	struct pmc_owner *po;
	struct thread *td;
	struct proc *p;

	po = pm->pm_owner;
	if ((po->po_flags & PMC_PO_OWNS_LOGFILE) == 0)
		return;
	sx_slock(&allproc_lock);
	FOREACH_PROC_IN_SYSTEM(p) {
		pmclog_process_proccreate(po, p, 0 /* sync */);
		PROC_LOCK(p);
		FOREACH_THREAD_IN_PROC(p, td)
			pmclog_process_threadcreate(po, td, 0 /* sync */);
		PROC_UNLOCK(p);
	}
	sx_sunlock(&allproc_lock);
	pmclog_flush(po, 0);
}

static void
pmc_kld_load(void *arg __unused, linker_file_t lf)
{
	struct pmc_owner *po;

	/*
	 * Notify owners of system sampling PMCs about KLD operations.
	 */
	PMC_EPOCH_ENTER();
	CK_LIST_FOREACH(po, &pmc_ss_owners, po_ssnext)
		if (po->po_flags & PMC_PO_OWNS_LOGFILE)
			pmclog_process_map_in(po, (pid_t) -1,
			    (uintfptr_t) lf->address, lf->filename);
	PMC_EPOCH_EXIT();

	/*
	 * TODO: Notify owners of (all) process-sampling PMCs too.
	 */
}

static void
pmc_kld_unload(void *arg __unused, const char *filename __unused,
    caddr_t address, size_t size)
{
	struct pmc_owner *po;

	PMC_EPOCH_ENTER();
	CK_LIST_FOREACH(po, &pmc_ss_owners, po_ssnext)
		if (po->po_flags & PMC_PO_OWNS_LOGFILE)
			pmclog_process_map_out(po, (pid_t) -1,
			    (uintfptr_t) address, (uintfptr_t) address + size);
	PMC_EPOCH_EXIT();

	/*
	 * TODO: Notify owners of process-sampling PMCs.
	 */
}

/*
 * initialization
 */
static const char *
pmc_name_of_pmcclass(enum pmc_class class)
{

	switch (class) {
#undef	__PMC_CLASS
#define	__PMC_CLASS(S,V,D)						\
	case PMC_CLASS_##S:						\
		return #S;
	__PMC_CLASSES();
	default:
		return ("<unknown>");
	}
}

/*
 * Base class initializer: allocate structure and set default classes.
 */
struct pmc_mdep *
pmc_mdep_alloc(int nclasses)
{
	struct pmc_mdep *md;
	int	n;

	/* SOFT + md classes */
	n = 1 + nclasses;
	md = malloc(sizeof(struct pmc_mdep) + n *
	    sizeof(struct pmc_classdep), M_PMC, M_WAITOK|M_ZERO);
	md->pmd_nclass = n;

	/* Add base class. */
	pmc_soft_initialize(md);
	return md;
}

void
pmc_mdep_free(struct pmc_mdep *md)
{
	pmc_soft_finalize(md);
	free(md, M_PMC);
}

static int
generic_switch_in(struct pmc_cpu *pc, struct pmc_process *pp)
{
	(void) pc; (void) pp;

	return (0);
}

static int
generic_switch_out(struct pmc_cpu *pc, struct pmc_process *pp)
{
	(void) pc; (void) pp;

	return (0);
}

static struct pmc_mdep *
pmc_generic_cpu_initialize(void)
{
	struct pmc_mdep *md;

	md = pmc_mdep_alloc(0);

	md->pmd_cputype    = PMC_CPU_GENERIC;

	md->pmd_pcpu_init  = NULL;
	md->pmd_pcpu_fini  = NULL;
	md->pmd_switch_in  = generic_switch_in;
	md->pmd_switch_out = generic_switch_out;

	return (md);
}

static void
pmc_generic_cpu_finalize(struct pmc_mdep *md)
{
	(void) md;
}


static int
pmc_initialize(void)
{
	int c, cpu, error, n, ri;
	unsigned int maxcpu, domain;
	struct pcpu *pc;
	struct pmc_binding pb;
	struct pmc_sample *ps;
	struct pmc_classdep *pcd;
	struct pmc_samplebuffer *sb;

	md = NULL;
	error = 0;

	pmc_stats.pm_intr_ignored = counter_u64_alloc(M_WAITOK);
	pmc_stats.pm_intr_processed = counter_u64_alloc(M_WAITOK);
	pmc_stats.pm_intr_bufferfull = counter_u64_alloc(M_WAITOK);
	pmc_stats.pm_syscalls = counter_u64_alloc(M_WAITOK);
	pmc_stats.pm_syscall_errors = counter_u64_alloc(M_WAITOK);
	pmc_stats.pm_buffer_requests = counter_u64_alloc(M_WAITOK);
	pmc_stats.pm_buffer_requests_failed = counter_u64_alloc(M_WAITOK);
	pmc_stats.pm_log_sweeps = counter_u64_alloc(M_WAITOK);
	pmc_stats.pm_merges = counter_u64_alloc(M_WAITOK);
	pmc_stats.pm_overwrites = counter_u64_alloc(M_WAITOK);

#ifdef	HWPMC_DEBUG
	/* parse debug flags first */
	if (TUNABLE_STR_FETCH(PMC_SYSCTL_NAME_PREFIX "debugflags",
		pmc_debugstr, sizeof(pmc_debugstr)))
		pmc_debugflags_parse(pmc_debugstr,
		    pmc_debugstr+strlen(pmc_debugstr));
#endif

	PMCDBG1(MOD,INI,0, "PMC Initialize (version %x)", PMC_VERSION);

	/* check kernel version */
	if (pmc_kernel_version != PMC_VERSION) {
		if (pmc_kernel_version == 0)
			printf("hwpmc: this kernel has not been compiled with "
			    "'options HWPMC_HOOKS'.\n");
		else
			printf("hwpmc: kernel version (0x%x) does not match "
			    "module version (0x%x).\n", pmc_kernel_version,
			    PMC_VERSION);
		return EPROGMISMATCH;
	}

	/*
	 * check sysctl parameters
	 */

	if (pmc_hashsize <= 0) {
		(void) printf("hwpmc: tunable \"hashsize\"=%d must be "
		    "greater than zero.\n", pmc_hashsize);
		pmc_hashsize = PMC_HASH_SIZE;
	}

	if (pmc_nsamples <= 0 || pmc_nsamples > 65535) {
		(void) printf("hwpmc: tunable \"nsamples\"=%d out of "
		    "range.\n", pmc_nsamples);
		pmc_nsamples = PMC_NSAMPLES;
	}
	pmc_sample_mask = pmc_nsamples-1;

	if (pmc_callchaindepth <= 0 ||
	    pmc_callchaindepth > PMC_CALLCHAIN_DEPTH_MAX) {
		(void) printf("hwpmc: tunable \"callchaindepth\"=%d out of "
		    "range - using %d.\n", pmc_callchaindepth,
		    PMC_CALLCHAIN_DEPTH_MAX);
		pmc_callchaindepth = PMC_CALLCHAIN_DEPTH_MAX;
	}

	md = pmc_md_initialize();
	if (md == NULL) {
		/* Default to generic CPU. */
		md = pmc_generic_cpu_initialize();
		if (md == NULL)
			return (ENOSYS);
        }

	KASSERT(md->pmd_nclass >= 1 && md->pmd_npmc >= 1,
	    ("[pmc,%d] no classes or pmcs", __LINE__));

	/* Compute the map from row-indices to classdep pointers. */
	pmc_rowindex_to_classdep = malloc(sizeof(struct pmc_classdep *) *
	    md->pmd_npmc, M_PMC, M_WAITOK|M_ZERO);

	for (n = 0; n < md->pmd_npmc; n++)
		pmc_rowindex_to_classdep[n] = NULL;
	for (ri = c = 0; c < md->pmd_nclass; c++) {
		pcd = &md->pmd_classdep[c];
		for (n = 0; n < pcd->pcd_num; n++, ri++)
			pmc_rowindex_to_classdep[ri] = pcd;
	}

	KASSERT(ri == md->pmd_npmc,
	    ("[pmc,%d] npmc miscomputed: ri=%d, md->npmc=%d", __LINE__,
	    ri, md->pmd_npmc));

	maxcpu = pmc_cpu_max();

	/* allocate space for the per-cpu array */
	pmc_pcpu = malloc(maxcpu * sizeof(struct pmc_cpu *), M_PMC,
	    M_WAITOK|M_ZERO);

	/* per-cpu 'saved values' for managing process-mode PMCs */
	pmc_pcpu_saved = malloc(sizeof(pmc_value_t) * maxcpu * md->pmd_npmc,
	    M_PMC, M_WAITOK);

	/* Perform CPU-dependent initialization. */
	pmc_save_cpu_binding(&pb);
	error = 0;
	for (cpu = 0; error == 0 && cpu < maxcpu; cpu++) {
		if (!pmc_cpu_is_active(cpu))
			continue;
		pmc_select_cpu(cpu);
		pmc_pcpu[cpu] = malloc(sizeof(struct pmc_cpu) +
		    md->pmd_npmc * sizeof(struct pmc_hw *), M_PMC,
		    M_WAITOK|M_ZERO);
		if (md->pmd_pcpu_init)
			error = md->pmd_pcpu_init(md, cpu);
		for (n = 0; error == 0 && n < md->pmd_nclass; n++)
			error = md->pmd_classdep[n].pcd_pcpu_init(md, cpu);
	}
	pmc_restore_cpu_binding(&pb);

	if (error)
		return (error);

	/* allocate space for the sample array */
	for (cpu = 0; cpu < maxcpu; cpu++) {
		if (!pmc_cpu_is_active(cpu))
			continue;
		pc = pcpu_find(cpu);
		domain = pc->pc_domain;
		sb = malloc_domainset(sizeof(struct pmc_samplebuffer) +
		    pmc_nsamples * sizeof(struct pmc_sample), M_PMC,
		    DOMAINSET_PREF(domain), M_WAITOK | M_ZERO);

		KASSERT(pmc_pcpu[cpu] != NULL,
		    ("[pmc,%d] cpu=%d Null per-cpu data", __LINE__, cpu));

		sb->ps_callchains = malloc_domainset(pmc_callchaindepth *
		    pmc_nsamples * sizeof(uintptr_t), M_PMC,
		    DOMAINSET_PREF(domain), M_WAITOK | M_ZERO);

		for (n = 0, ps = sb->ps_samples; n < pmc_nsamples; n++, ps++)
			ps->ps_pc = sb->ps_callchains +
			    (n * pmc_callchaindepth);

		pmc_pcpu[cpu]->pc_sb[PMC_HR] = sb;

		sb = malloc_domainset(sizeof(struct pmc_samplebuffer) +
		    pmc_nsamples * sizeof(struct pmc_sample), M_PMC,
		    DOMAINSET_PREF(domain), M_WAITOK | M_ZERO);

		sb->ps_callchains = malloc_domainset(pmc_callchaindepth *
		    pmc_nsamples * sizeof(uintptr_t), M_PMC,
		    DOMAINSET_PREF(domain), M_WAITOK | M_ZERO);
		for (n = 0, ps = sb->ps_samples; n < pmc_nsamples; n++, ps++)
			ps->ps_pc = sb->ps_callchains +
			    (n * pmc_callchaindepth);

		pmc_pcpu[cpu]->pc_sb[PMC_SR] = sb;

		sb = malloc_domainset(sizeof(struct pmc_samplebuffer) +
		    pmc_nsamples * sizeof(struct pmc_sample), M_PMC,
		    DOMAINSET_PREF(domain), M_WAITOK | M_ZERO);
		sb->ps_callchains = malloc_domainset(pmc_callchaindepth *
		    pmc_nsamples * sizeof(uintptr_t), M_PMC,
		    DOMAINSET_PREF(domain), M_WAITOK | M_ZERO);
		for (n = 0, ps = sb->ps_samples; n < pmc_nsamples; n++, ps++)
			ps->ps_pc = sb->ps_callchains + n * pmc_callchaindepth;

		pmc_pcpu[cpu]->pc_sb[PMC_UR] = sb;
	}

	/* allocate space for the row disposition array */
	pmc_pmcdisp = malloc(sizeof(enum pmc_mode) * md->pmd_npmc,
	    M_PMC, M_WAITOK|M_ZERO);

	/* mark all PMCs as available */
	for (n = 0; n < (int) md->pmd_npmc; n++)
		PMC_MARK_ROW_FREE(n);

	/* allocate thread hash tables */
	pmc_ownerhash = hashinit(pmc_hashsize, M_PMC,
	    &pmc_ownerhashmask);

	pmc_processhash = hashinit(pmc_hashsize, M_PMC,
	    &pmc_processhashmask);
	mtx_init(&pmc_processhash_mtx, "pmc-process-hash", "pmc-leaf",
	    MTX_SPIN);

	CK_LIST_INIT(&pmc_ss_owners);
	pmc_ss_count = 0;

	/* allocate a pool of spin mutexes */
	pmc_mtxpool = mtx_pool_create("pmc-leaf", pmc_mtxpool_size,
	    MTX_SPIN);

	PMCDBG4(MOD,INI,1, "pmc_ownerhash=%p, mask=0x%lx "
	    "targethash=%p mask=0x%lx", pmc_ownerhash, pmc_ownerhashmask,
	    pmc_processhash, pmc_processhashmask);

	/* Initialize a spin mutex for the thread free list. */
	mtx_init(&pmc_threadfreelist_mtx, "pmc-threadfreelist", "pmc-leaf",
	    MTX_SPIN);

	/*
	 * Initialize the callout to monitor the thread free list.
	 * This callout will also handle the initial population of the list.
	 */
	taskqgroup_config_gtask_init(NULL, &free_gtask, pmc_thread_descriptor_pool_free_task, "thread descriptor pool free task");

	/* register process {exit,fork,exec} handlers */
	pmc_exit_tag = EVENTHANDLER_REGISTER(process_exit,
	    pmc_process_exit, NULL, EVENTHANDLER_PRI_ANY);
	pmc_fork_tag = EVENTHANDLER_REGISTER(process_fork,
	    pmc_process_fork, NULL, EVENTHANDLER_PRI_ANY);

	/* register kld event handlers */
	pmc_kld_load_tag = EVENTHANDLER_REGISTER(kld_load, pmc_kld_load,
	    NULL, EVENTHANDLER_PRI_ANY);
	pmc_kld_unload_tag = EVENTHANDLER_REGISTER(kld_unload, pmc_kld_unload,
	    NULL, EVENTHANDLER_PRI_ANY);

	/* initialize logging */
	pmclog_initialize();

	/* set hook functions */
	pmc_intr = md->pmd_intr;
	wmb();
	pmc_hook = pmc_hook_handler;

	if (error == 0) {
		printf(PMC_MODULE_NAME ":");
		for (n = 0; n < (int) md->pmd_nclass; n++) {
			pcd = &md->pmd_classdep[n];
			printf(" %s/%d/%d/0x%b",
			    pmc_name_of_pmcclass(pcd->pcd_class),
			    pcd->pcd_num,
			    pcd->pcd_width,
			    pcd->pcd_caps,
			    "\20"
			    "\1INT\2USR\3SYS\4EDG\5THR"
			    "\6REA\7WRI\10INV\11QUA\12PRC"
			    "\13TAG\14CSC");
		}
		printf("\n");
	}

	return (error);
}

/* prepare to be unloaded */
static void
pmc_cleanup(void)
{
	int c, cpu;
	unsigned int maxcpu;
	struct pmc_ownerhash *ph;
	struct pmc_owner *po, *tmp;
	struct pmc_binding pb;
#ifdef	HWPMC_DEBUG
	struct pmc_processhash *prh;
#endif

	PMCDBG0(MOD,INI,0, "cleanup");

	/* switch off sampling */
	CPU_FOREACH(cpu)
		DPCPU_ID_SET(cpu, pmc_sampled, 0);
	pmc_intr = NULL;

	sx_xlock(&pmc_sx);
	if (pmc_hook == NULL) {	/* being unloaded already */
		sx_xunlock(&pmc_sx);
		return;
	}

	pmc_hook = NULL; /* prevent new threads from entering module */

	/* deregister event handlers */
	EVENTHANDLER_DEREGISTER(process_fork, pmc_fork_tag);
	EVENTHANDLER_DEREGISTER(process_exit, pmc_exit_tag);
	EVENTHANDLER_DEREGISTER(kld_load, pmc_kld_load_tag);
	EVENTHANDLER_DEREGISTER(kld_unload, pmc_kld_unload_tag);

	/* send SIGBUS to all owner threads, free up allocations */
	if (pmc_ownerhash)
		for (ph = pmc_ownerhash;
		     ph <= &pmc_ownerhash[pmc_ownerhashmask];
		     ph++) {
			LIST_FOREACH_SAFE(po, ph, po_next, tmp) {
				pmc_remove_owner(po);

				/* send SIGBUS to owner processes */
				PMCDBG3(MOD,INI,2, "cleanup signal proc=%p "
				    "(%d, %s)", po->po_owner,
				    po->po_owner->p_pid,
				    po->po_owner->p_comm);

				PROC_LOCK(po->po_owner);
				kern_psignal(po->po_owner, SIGBUS);
				PROC_UNLOCK(po->po_owner);

				pmc_destroy_owner_descriptor(po);
			}
		}

	/* reclaim allocated data structures */
	mtx_destroy(&pmc_threadfreelist_mtx);
	pmc_thread_descriptor_pool_drain();

	if (pmc_mtxpool)
		mtx_pool_destroy(&pmc_mtxpool);

	mtx_destroy(&pmc_processhash_mtx);
	taskqgroup_config_gtask_deinit(&free_gtask);
	if (pmc_processhash) {
#ifdef	HWPMC_DEBUG
		struct pmc_process *pp;

		PMCDBG0(MOD,INI,3, "destroy process hash");
		for (prh = pmc_processhash;
		     prh <= &pmc_processhash[pmc_processhashmask];
		     prh++)
			LIST_FOREACH(pp, prh, pp_next)
			    PMCDBG1(MOD,INI,3, "pid=%d", pp->pp_proc->p_pid);
#endif

		hashdestroy(pmc_processhash, M_PMC, pmc_processhashmask);
		pmc_processhash = NULL;
	}

	if (pmc_ownerhash) {
		PMCDBG0(MOD,INI,3, "destroy owner hash");
		hashdestroy(pmc_ownerhash, M_PMC, pmc_ownerhashmask);
		pmc_ownerhash = NULL;
	}

	KASSERT(CK_LIST_EMPTY(&pmc_ss_owners),
	    ("[pmc,%d] Global SS owner list not empty", __LINE__));
	KASSERT(pmc_ss_count == 0,
	    ("[pmc,%d] Global SS count not empty", __LINE__));

 	/* do processor and pmc-class dependent cleanup */
	maxcpu = pmc_cpu_max();

	PMCDBG0(MOD,INI,3, "md cleanup");
	if (md) {
		pmc_save_cpu_binding(&pb);
		for (cpu = 0; cpu < maxcpu; cpu++) {
			PMCDBG2(MOD,INI,1,"pmc-cleanup cpu=%d pcs=%p",
			    cpu, pmc_pcpu[cpu]);
			if (!pmc_cpu_is_active(cpu) || pmc_pcpu[cpu] == NULL)
				continue;
			pmc_select_cpu(cpu);
			for (c = 0; c < md->pmd_nclass; c++)
				md->pmd_classdep[c].pcd_pcpu_fini(md, cpu);
			if (md->pmd_pcpu_fini)
				md->pmd_pcpu_fini(md, cpu);
		}

		if (md->pmd_cputype == PMC_CPU_GENERIC)
			pmc_generic_cpu_finalize(md);
		else
			pmc_md_finalize(md);

		pmc_mdep_free(md);
		md = NULL;
		pmc_restore_cpu_binding(&pb);
	}

	/* Free per-cpu descriptors. */
	for (cpu = 0; cpu < maxcpu; cpu++) {
		if (!pmc_cpu_is_active(cpu))
			continue;
		KASSERT(pmc_pcpu[cpu]->pc_sb[PMC_HR] != NULL,
		    ("[pmc,%d] Null hw cpu sample buffer cpu=%d", __LINE__,
			cpu));
		KASSERT(pmc_pcpu[cpu]->pc_sb[PMC_SR] != NULL,
		    ("[pmc,%d] Null sw cpu sample buffer cpu=%d", __LINE__,
			cpu));
		KASSERT(pmc_pcpu[cpu]->pc_sb[PMC_UR] != NULL,
		    ("[pmc,%d] Null userret cpu sample buffer cpu=%d", __LINE__,
			cpu));
		free_domain(pmc_pcpu[cpu]->pc_sb[PMC_HR]->ps_callchains, M_PMC);
		free_domain(pmc_pcpu[cpu]->pc_sb[PMC_HR], M_PMC);
		free_domain(pmc_pcpu[cpu]->pc_sb[PMC_SR]->ps_callchains, M_PMC);
		free_domain(pmc_pcpu[cpu]->pc_sb[PMC_SR], M_PMC);
		free_domain(pmc_pcpu[cpu]->pc_sb[PMC_UR]->ps_callchains, M_PMC);
		free_domain(pmc_pcpu[cpu]->pc_sb[PMC_UR], M_PMC);
		free_domain(pmc_pcpu[cpu], M_PMC);
	}

	free(pmc_pcpu, M_PMC);
	pmc_pcpu = NULL;

	free(pmc_pcpu_saved, M_PMC);
	pmc_pcpu_saved = NULL;

	if (pmc_pmcdisp) {
		free(pmc_pmcdisp, M_PMC);
		pmc_pmcdisp = NULL;
	}

	if (pmc_rowindex_to_classdep) {
		free(pmc_rowindex_to_classdep, M_PMC);
		pmc_rowindex_to_classdep = NULL;
	}

	pmclog_shutdown();
	counter_u64_free(pmc_stats.pm_intr_ignored);
	counter_u64_free(pmc_stats.pm_intr_processed);
	counter_u64_free(pmc_stats.pm_intr_bufferfull);
	counter_u64_free(pmc_stats.pm_syscalls);
	counter_u64_free(pmc_stats.pm_syscall_errors);
	counter_u64_free(pmc_stats.pm_buffer_requests);
	counter_u64_free(pmc_stats.pm_buffer_requests_failed);
	counter_u64_free(pmc_stats.pm_log_sweeps);
	counter_u64_free(pmc_stats.pm_merges);
	counter_u64_free(pmc_stats.pm_overwrites);
	sx_xunlock(&pmc_sx); 	/* we are done */
}

/*
 * The function called at load/unload.
 */

static int
load (struct module *module __unused, int cmd, void *arg __unused)
{
	int error;

	error = 0;

	switch (cmd) {
	case MOD_LOAD :
		/* initialize the subsystem */
		error = pmc_initialize();
		if (error != 0)
			break;
		PMCDBG2(MOD,INI,1, "syscall=%d maxcpu=%d",
		    pmc_syscall_num, pmc_cpu_max());
		break;


	case MOD_UNLOAD :
	case MOD_SHUTDOWN:
		pmc_cleanup();
		PMCDBG0(MOD,INI,1, "unloaded");
		break;

	default :
		error = EINVAL;	/* XXX should panic(9) */
		break;
	}

	return error;
}
