/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995 Terrence R. Lambert
 * All rights reserved.
 *
 * Copyright (c) 1982, 1986, 1989, 1991, 1992, 1993
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)init_main.c	8.9 (Berkeley) 1/21/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_init_path.h"
#include "opt_verbose_sysinit.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/epoch.h>
#include <sys/exec.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/imgact.h>
#include <sys/jail.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/loginclass.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/systm.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/sysent.h>
#include <sys/reboot.h>
#include <sys/sched.h>
#include <sys/sx.h>
#include <sys/sysproto.h>
#include <sys/vmmeter.h>
#include <sys/unistd.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/cpuset.h>

#include <machine/cpu.h>

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <sys/copyright.h>

#include <ddb/ddb.h>
#include <ddb/db_sym.h>

void mi_startup(void);				/* Should be elsewhere */

/* Components of the first process -- never freed. */
static struct session session0;
static struct pgrp pgrp0;
struct	proc proc0;
struct thread0_storage thread0_st __aligned(32);
struct	vmspace vmspace0;
struct	proc *initproc;

int
linux_alloc_current_noop(struct thread *td __unused, int flags __unused)
{
	return (0);
}
int (*lkpi_alloc_current)(struct thread *, int) = linux_alloc_current_noop;


#ifndef BOOTHOWTO
#define	BOOTHOWTO	0
#endif
int	boothowto = BOOTHOWTO;	/* initialized so that it can be patched */
SYSCTL_INT(_debug, OID_AUTO, boothowto, CTLFLAG_RD, &boothowto, 0,
	"Boot control flags, passed from loader");

#ifndef BOOTVERBOSE
#define	BOOTVERBOSE	0
#endif
int	bootverbose = BOOTVERBOSE;
SYSCTL_INT(_debug, OID_AUTO, bootverbose, CTLFLAG_RW, &bootverbose, 0,
	"Control the output of verbose kernel messages");

#ifdef VERBOSE_SYSINIT
/*
 * We'll use the defined value of VERBOSE_SYSINIT from the kernel config to
 * dictate the default VERBOSE_SYSINIT behavior.  Significant values for this
 * option and associated tunable are:
 * - 0, 'compiled in but silent by default'
 * - 1, 'compiled in but verbose by default' (default)
 */
int	verbose_sysinit = VERBOSE_SYSINIT;
TUNABLE_INT("debug.verbose_sysinit", &verbose_sysinit);
#endif

#ifdef INVARIANTS
FEATURE(invariants, "Kernel compiled with INVARIANTS, may affect performance");
#endif

/*
 * This ensures that there is at least one entry so that the sysinit_set
 * symbol is not undefined.  A sybsystem ID of SI_SUB_DUMMY is never
 * executed.
 */
SYSINIT(placeholder, SI_SUB_DUMMY, SI_ORDER_ANY, NULL, NULL);

/*
 * The sysinit table itself.  Items are checked off as the are run.
 * If we want to register new sysinit types, add them to newsysinit.
 */
SET_DECLARE(sysinit_set, struct sysinit);
struct sysinit **sysinit, **sysinit_end;
struct sysinit **newsysinit, **newsysinit_end;

EVENTHANDLER_LIST_DECLARE(process_init);
EVENTHANDLER_LIST_DECLARE(thread_init);
EVENTHANDLER_LIST_DECLARE(process_ctor);
EVENTHANDLER_LIST_DECLARE(thread_ctor);

/*
 * Merge a new sysinit set into the current set, reallocating it if
 * necessary.  This can only be called after malloc is running.
 */
void
sysinit_add(struct sysinit **set, struct sysinit **set_end)
{
	struct sysinit **newset;
	struct sysinit **sipp;
	struct sysinit **xipp;
	int count;

	count = set_end - set;
	if (newsysinit)
		count += newsysinit_end - newsysinit;
	else
		count += sysinit_end - sysinit;
	newset = malloc(count * sizeof(*sipp), M_TEMP, M_NOWAIT);
	if (newset == NULL)
		panic("cannot malloc for sysinit");
	xipp = newset;
	if (newsysinit)
		for (sipp = newsysinit; sipp < newsysinit_end; sipp++)
			*xipp++ = *sipp;
	else
		for (sipp = sysinit; sipp < sysinit_end; sipp++)
			*xipp++ = *sipp;
	for (sipp = set; sipp < set_end; sipp++)
		*xipp++ = *sipp;
	if (newsysinit)
		free(newsysinit, M_TEMP);
	newsysinit = newset;
	newsysinit_end = newset + count;
}

#if defined (DDB) && defined(VERBOSE_SYSINIT)
static const char *
symbol_name(vm_offset_t va, db_strategy_t strategy)
{
	const char *name;
	c_db_sym_t sym;
	db_expr_t  offset;

	if (va == 0)
		return (NULL);
	sym = db_search_symbol(va, strategy, &offset);
	if (offset != 0)
		return (NULL);
	db_symbol_values(sym, &name, NULL);
	return (name);
}
#endif

/*
 * System startup; initialize the world, create process 0, mount root
 * filesystem, and fork to create init and pagedaemon.  Most of the
 * hard work is done in the lower-level initialization routines including
 * startup(), which does memory initialization and autoconfiguration.
 *
 * This allows simple addition of new kernel subsystems that require
 * boot time initialization.  It also allows substitution of subsystem
 * (for instance, a scheduler, kernel profiler, or VM system) by object
 * module.  Finally, it allows for optional "kernel threads".
 */
void
mi_startup(void)
{

	struct sysinit **sipp;	/* system initialization*/
	struct sysinit **xipp;	/* interior loop of sort*/
	struct sysinit *save;	/* bubble*/

#if defined(VERBOSE_SYSINIT)
	int last;
	int verbose;
#endif

	TSENTER();

	if (boothowto & RB_VERBOSE)
		bootverbose++;

	if (sysinit == NULL) {
		sysinit = SET_BEGIN(sysinit_set);
		sysinit_end = SET_LIMIT(sysinit_set);
	}

restart:
	/*
	 * Perform a bubble sort of the system initialization objects by
	 * their subsystem (primary key) and order (secondary key).
	 */
	for (sipp = sysinit; sipp < sysinit_end; sipp++) {
		for (xipp = sipp + 1; xipp < sysinit_end; xipp++) {
			if ((*sipp)->subsystem < (*xipp)->subsystem ||
			     ((*sipp)->subsystem == (*xipp)->subsystem &&
			      (*sipp)->order <= (*xipp)->order))
				continue;	/* skip*/
			save = *sipp;
			*sipp = *xipp;
			*xipp = save;
		}
	}

#if defined(VERBOSE_SYSINIT)
	last = SI_SUB_COPYRIGHT;
	verbose = 0;
#if !defined(DDB)
	printf("VERBOSE_SYSINIT: DDB not enabled, symbol lookups disabled.\n");
#endif
#endif

	/*
	 * Traverse the (now) ordered list of system initialization tasks.
	 * Perform each task, and continue on to the next task.
	 */
	for (sipp = sysinit; sipp < sysinit_end; sipp++) {

		if ((*sipp)->subsystem == SI_SUB_DUMMY)
			continue;	/* skip dummy task(s)*/

		if ((*sipp)->subsystem == SI_SUB_DONE)
			continue;

#if defined(VERBOSE_SYSINIT)
		if ((*sipp)->subsystem > last && verbose_sysinit != 0) {
			verbose = 1;
			last = (*sipp)->subsystem;
			printf("subsystem %x\n", last);
		}
		if (verbose) {
#if defined(DDB)
			const char *func, *data;

			func = symbol_name((vm_offset_t)(*sipp)->func,
			    DB_STGY_PROC);
			data = symbol_name((vm_offset_t)(*sipp)->udata,
			    DB_STGY_ANY);
			if (func != NULL && data != NULL)
				printf("   %s(&%s)... ", func, data);
			else if (func != NULL)
				printf("   %s(%p)... ", func, (*sipp)->udata);
			else
#endif
				printf("   %p(%p)... ", (*sipp)->func,
				    (*sipp)->udata);
		}
#endif

		/* Call function */
		(*((*sipp)->func))((*sipp)->udata);

#if defined(VERBOSE_SYSINIT)
		if (verbose)
			printf("done.\n");
#endif

		/* Check off the one we're just done */
		(*sipp)->subsystem = SI_SUB_DONE;

		/* Check if we've installed more sysinit items via KLD */
		if (newsysinit != NULL) {
			if (sysinit != SET_BEGIN(sysinit_set))
				free(sysinit, M_TEMP);
			sysinit = newsysinit;
			sysinit_end = newsysinit_end;
			newsysinit = NULL;
			newsysinit_end = NULL;
			goto restart;
		}
	}

	TSEXIT();	/* Here so we don't overlap with start_init. */

	mtx_assert(&Giant, MA_OWNED | MA_NOTRECURSED);
	mtx_unlock(&Giant);

	/*
	 * Now hand over this thread to swapper.
	 */
	swapper();
	/* NOTREACHED*/
}

static void
print_caddr_t(void *data)
{
	printf("%s", (char *)data);
}

static void
print_version(void *data __unused)
{
	int len;

	/* Strip a trailing newline from version. */
	len = strlen(version);
	while (len > 0 && version[len - 1] == '\n')
		len--;
	printf("%.*s %s\n", len, version, machine);
	printf("%s\n", compiler_version);
}

SYSINIT(announce, SI_SUB_COPYRIGHT, SI_ORDER_FIRST, print_caddr_t,
    copyright);
SYSINIT(trademark, SI_SUB_COPYRIGHT, SI_ORDER_SECOND, print_caddr_t,
    trademark);
SYSINIT(version, SI_SUB_COPYRIGHT, SI_ORDER_THIRD, print_version, NULL);

#ifdef WITNESS
static char wit_warn[] =
     "WARNING: WITNESS option enabled, expect reduced performance.\n";
SYSINIT(witwarn, SI_SUB_COPYRIGHT, SI_ORDER_THIRD + 1,
   print_caddr_t, wit_warn);
SYSINIT(witwarn2, SI_SUB_LAST, SI_ORDER_THIRD + 1,
   print_caddr_t, wit_warn);
#endif

#ifdef DIAGNOSTIC
static char diag_warn[] =
     "WARNING: DIAGNOSTIC option enabled, expect reduced performance.\n";
SYSINIT(diagwarn, SI_SUB_COPYRIGHT, SI_ORDER_THIRD + 2,
    print_caddr_t, diag_warn);
SYSINIT(diagwarn2, SI_SUB_LAST, SI_ORDER_THIRD + 2,
    print_caddr_t, diag_warn);
#endif

static int
null_fetch_syscall_args(struct thread *td __unused)
{

	panic("null_fetch_syscall_args");
}

static void
null_set_syscall_retval(struct thread *td __unused, int error __unused)
{

	panic("null_set_syscall_retval");
}

struct sysentvec null_sysvec = {
	.sv_size	= 0,
	.sv_table	= NULL,
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= NULL,
	.sv_sendsig	= NULL,
	.sv_sigcode	= NULL,
	.sv_szsigcode	= NULL,
	.sv_name	= "null",
	.sv_coredump	= NULL,
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= 0,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= VM_MAXUSER_ADDRESS,
	.sv_usrstack	= USRSTACK,
	.sv_psstrings	= PS_STRINGS,
	.sv_stackprot	= VM_PROT_ALL,
	.sv_copyout_strings	= NULL,
	.sv_setregs	= NULL,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= 0,
	.sv_set_syscall_retval = null_set_syscall_retval,
	.sv_fetch_syscall_args = null_fetch_syscall_args,
	.sv_syscallnames = NULL,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
};

/*
 * The two following SYSINIT's are proc0 specific glue code.  I am not
 * convinced that they can not be safely combined, but their order of
 * operation has been maintained as the same as the original init_main.c
 * for right now.
 */
/* ARGSUSED*/
static void
proc0_init(void *dummy __unused)
{
	struct proc *p;
	struct thread *td;
	struct ucred *newcred;
	struct uidinfo tmpuinfo;
	struct loginclass tmplc = {
		.lc_name = "",
	};
	vm_paddr_t pageablemem;
	int i;

	GIANT_REQUIRED;
	p = &proc0;
	td = &thread0;

	/*
	 * Initialize magic number and osrel.
	 */
	p->p_magic = P_MAGIC;
	p->p_osrel = osreldate;

	/*
	 * Initialize thread and process structures.
	 */
	procinit();	/* set up proc zone */
	threadinit();	/* set up UMA zones */

	/*
	 * Initialise scheduler resources.
	 * Add scheduler specific parts to proc, thread as needed.
	 */
	schedinit();	/* scheduler gets its house in order */

	/*
	 * Create process 0 (the swapper).
	 */
	LIST_INSERT_HEAD(&allproc, p, p_list);
	LIST_INSERT_HEAD(PIDHASH(0), p, p_hash);
	mtx_init(&pgrp0.pg_mtx, "process group", NULL, MTX_DEF | MTX_DUPOK);
	p->p_pgrp = &pgrp0;
	LIST_INSERT_HEAD(PGRPHASH(0), &pgrp0, pg_hash);
	LIST_INIT(&pgrp0.pg_members);
	LIST_INSERT_HEAD(&pgrp0.pg_members, p, p_pglist);

	pgrp0.pg_session = &session0;
	mtx_init(&session0.s_mtx, "session", NULL, MTX_DEF);
	refcount_init(&session0.s_count, 1);
	session0.s_leader = p;

	p->p_sysent = &null_sysvec;
	p->p_flag = P_SYSTEM | P_INMEM | P_KPROC;
	p->p_flag2 = 0;
	p->p_state = PRS_NORMAL;
	p->p_klist = knlist_alloc(&p->p_mtx);
	STAILQ_INIT(&p->p_ktr);
	p->p_nice = NZERO;
	/* pid_max cannot be greater than PID_MAX */
	td->td_tid = PID_MAX + 1;
	LIST_INSERT_HEAD(TIDHASH(td->td_tid), td, td_hash);
	td->td_state = TDS_RUNNING;
	td->td_pri_class = PRI_TIMESHARE;
	td->td_user_pri = PUSER;
	td->td_base_user_pri = PUSER;
	td->td_lend_user_pri = PRI_MAX;
	td->td_priority = PVM;
	td->td_base_pri = PVM;
	td->td_oncpu = curcpu;
	td->td_flags = TDF_INMEM;
	td->td_pflags = TDP_KTHREAD;
	td->td_cpuset = cpuset_thread0();
	td->td_domain.dr_policy = td->td_cpuset->cs_domain;
	epoch_thread_init(td);
	prison0_init();
	p->p_peers = 0;
	p->p_leader = p;
	p->p_reaper = p;
	p->p_treeflag |= P_TREE_REAPER;
	LIST_INIT(&p->p_reaplist);

	strncpy(p->p_comm, "kernel", sizeof (p->p_comm));
	strncpy(td->td_name, "swapper", sizeof (td->td_name));

	callout_init_mtx(&p->p_itcallout, &p->p_mtx, 0);
	callout_init_mtx(&p->p_limco, &p->p_mtx, 0);
	callout_init(&td->td_slpcallout, 1);

	/* Create credentials. */
	newcred = crget();
	newcred->cr_ngroups = 1;	/* group 0 */
	/* A hack to prevent uifind from tripping over NULL pointers. */
	curthread->td_ucred = newcred;
	tmpuinfo.ui_uid = 1;
	newcred->cr_uidinfo = newcred->cr_ruidinfo = &tmpuinfo;
	newcred->cr_uidinfo = uifind(0);
	newcred->cr_ruidinfo = uifind(0);
	newcred->cr_loginclass = &tmplc;
	newcred->cr_loginclass = loginclass_find("default");
	/* End hack. creds get properly set later with thread_cow_get_proc */
	curthread->td_ucred = NULL;
	newcred->cr_prison = &prison0;
	proc_set_cred_init(p, newcred);
#ifdef AUDIT
	audit_cred_kproc0(newcred);
#endif
#ifdef MAC
	mac_cred_create_swapper(newcred);
#endif
	/* Create sigacts. */
	p->p_sigacts = sigacts_alloc();

	/* Initialize signal state for process 0. */
	siginit(&proc0);

	/* Create the file descriptor table. */
	p->p_fd = fdinit(NULL, false);
	p->p_fdtol = NULL;

	/* Create the limits structures. */
	p->p_limit = lim_alloc();
	for (i = 0; i < RLIM_NLIMITS; i++)
		p->p_limit->pl_rlimit[i].rlim_cur =
		    p->p_limit->pl_rlimit[i].rlim_max = RLIM_INFINITY;
	p->p_limit->pl_rlimit[RLIMIT_NOFILE].rlim_cur =
	    p->p_limit->pl_rlimit[RLIMIT_NOFILE].rlim_max = maxfiles;
	p->p_limit->pl_rlimit[RLIMIT_NPROC].rlim_cur =
	    p->p_limit->pl_rlimit[RLIMIT_NPROC].rlim_max = maxproc;
	p->p_limit->pl_rlimit[RLIMIT_DATA].rlim_cur = dfldsiz;
	p->p_limit->pl_rlimit[RLIMIT_DATA].rlim_max = maxdsiz;
	p->p_limit->pl_rlimit[RLIMIT_STACK].rlim_cur = dflssiz;
	p->p_limit->pl_rlimit[RLIMIT_STACK].rlim_max = maxssiz;
	/* Cast to avoid overflow on i386/PAE. */
	pageablemem = ptoa((vm_paddr_t)vm_free_count());
	p->p_limit->pl_rlimit[RLIMIT_RSS].rlim_cur =
	    p->p_limit->pl_rlimit[RLIMIT_RSS].rlim_max = pageablemem;
	p->p_limit->pl_rlimit[RLIMIT_MEMLOCK].rlim_cur = pageablemem / 3;
	p->p_limit->pl_rlimit[RLIMIT_MEMLOCK].rlim_max = pageablemem;
	p->p_cpulimit = RLIM_INFINITY;

	PROC_LOCK(p);
	thread_cow_get_proc(td, p);
	PROC_UNLOCK(p);

	/* Initialize resource accounting structures. */
	racct_create(&p->p_racct);

	p->p_stats = pstats_alloc();

	/* Allocate a prototype map so we have something to fork. */
	p->p_vmspace = &vmspace0;
	vmspace0.vm_refcnt = 1;
	pmap_pinit0(vmspace_pmap(&vmspace0));

	/*
	 * proc0 is not expected to enter usermode, so there is no special
	 * handling for sv_minuser here, like is done for exec_new_vmspace().
	 */
	vm_map_init(&vmspace0.vm_map, vmspace_pmap(&vmspace0),
	    p->p_sysent->sv_minuser, p->p_sysent->sv_maxuser);

	/*
	 * Call the init and ctor for the new thread and proc.  We wait
	 * to do this until all other structures are fairly sane.
	 */
	EVENTHANDLER_DIRECT_INVOKE(process_init, p);
	EVENTHANDLER_DIRECT_INVOKE(thread_init, td);
	EVENTHANDLER_DIRECT_INVOKE(process_ctor, p);
	EVENTHANDLER_DIRECT_INVOKE(thread_ctor, td);

	/*
	 * Charge root for one process.
	 */
	(void)chgproccnt(p->p_ucred->cr_ruidinfo, 1, 0);
	PROC_LOCK(p);
	racct_add_force(p, RACCT_NPROC, 1);
	PROC_UNLOCK(p);
}
SYSINIT(p0init, SI_SUB_INTRINSIC, SI_ORDER_FIRST, proc0_init, NULL);

/* ARGSUSED*/
static void
proc0_post(void *dummy __unused)
{
	struct timespec ts;
	struct proc *p;
	struct rusage ru;
	struct thread *td;

	/*
	 * Now we can look at the time, having had a chance to verify the
	 * time from the filesystem.  Pretend that proc0 started now.
	 */
	sx_slock(&allproc_lock);
	FOREACH_PROC_IN_SYSTEM(p) {
		PROC_LOCK(p);
		if (p->p_state == PRS_NEW) {
			PROC_UNLOCK(p);
			continue;
		}
		microuptime(&p->p_stats->p_start);
		PROC_STATLOCK(p);
		rufetch(p, &ru);	/* Clears thread stats */
		p->p_rux.rux_runtime = 0;
		p->p_rux.rux_uticks = 0;
		p->p_rux.rux_sticks = 0;
		p->p_rux.rux_iticks = 0;
		PROC_STATUNLOCK(p);
		FOREACH_THREAD_IN_PROC(p, td) {
			td->td_runtime = 0;
		}
		PROC_UNLOCK(p);
	}
	sx_sunlock(&allproc_lock);
	PCPU_SET(switchtime, cpu_ticks());
	PCPU_SET(switchticks, ticks);

	/*
	 * Give the ``random'' number generator a thump.
	 */
	nanotime(&ts);
	srandom(ts.tv_sec ^ ts.tv_nsec);
}
SYSINIT(p0post, SI_SUB_INTRINSIC_POST, SI_ORDER_FIRST, proc0_post, NULL);

static void
random_init(void *dummy __unused)
{

	/*
	 * After CPU has been started we have some randomness on most
	 * platforms via get_cyclecount().  For platforms that don't
	 * we will reseed random(9) in proc0_post() as well.
	 */
	srandom(get_cyclecount());
}
SYSINIT(random, SI_SUB_RANDOM, SI_ORDER_FIRST, random_init, NULL);

/*
 ***************************************************************************
 ****
 **** The following SYSINIT's and glue code should be moved to the
 **** respective files on a per subsystem basis.
 ****
 ***************************************************************************
 */

/*
 * List of paths to try when searching for "init".
 */
static char init_path[MAXPATHLEN] =
#ifdef	INIT_PATH
    __XSTRING(INIT_PATH);
#else
    "/sbin/init:/sbin/oinit:/sbin/init.bak:/rescue/init";
#endif
SYSCTL_STRING(_kern, OID_AUTO, init_path, CTLFLAG_RD, init_path, 0,
	"Path used to search the init process");

/*
 * Shutdown timeout of init(8).
 * Unused within kernel, but used to control init(8), hence do not remove.
 */
#ifndef INIT_SHUTDOWN_TIMEOUT
#define INIT_SHUTDOWN_TIMEOUT 120
#endif
static int init_shutdown_timeout = INIT_SHUTDOWN_TIMEOUT;
SYSCTL_INT(_kern, OID_AUTO, init_shutdown_timeout,
	CTLFLAG_RW, &init_shutdown_timeout, 0, "Shutdown timeout of init(8). "
	"Unused within kernel, but used to control init(8)");

/*
 * Start the initial user process; try exec'ing each pathname in init_path.
 * The program is invoked with one argument containing the boot flags.
 */
static void
start_init(void *dummy)
{
	struct image_args args;
	int error;
	char *var, *path;
	char *free_init_path, *tmp_init_path;
	struct thread *td;
	struct proc *p;
	struct vmspace *oldvmspace;

	TSENTER();	/* Here so we don't overlap with mi_startup. */

	td = curthread;
	p = td->td_proc;

	vfs_mountroot();

	/* Wipe GELI passphrase from the environment. */
	kern_unsetenv("kern.geom.eli.passphrase");

	if ((var = kern_getenv("init_path")) != NULL) {
		strlcpy(init_path, var, sizeof(init_path));
		freeenv(var);
	}
	free_init_path = tmp_init_path = strdup(init_path, M_TEMP);
	
	while ((path = strsep(&tmp_init_path, ":")) != NULL) {
		if (bootverbose)
			printf("start_init: trying %s\n", path);
			
		memset(&args, 0, sizeof(args));
		error = exec_alloc_args(&args);
		if (error != 0)
			panic("%s: Can't allocate space for init arguments %d",
			    __func__, error);

		error = exec_args_add_fname(&args, path, UIO_SYSSPACE);
		if (error != 0)
			panic("%s: Can't add fname %d", __func__, error);
		error = exec_args_add_arg(&args, path, UIO_SYSSPACE);
		if (error != 0)
			panic("%s: Can't add argv[0] %d", __func__, error);
		if (boothowto & RB_SINGLE)
			error = exec_args_add_arg(&args, "-s", UIO_SYSSPACE);
		if (error != 0)
			panic("%s: Can't add argv[0] %d", __func__, error);

		/*
		 * Now try to exec the program.  If can't for any reason
		 * other than it doesn't exist, complain.
		 *
		 * Otherwise, return via fork_trampoline() all the way
		 * to user mode as init!
		 */
		KASSERT((td->td_pflags & TDP_EXECVMSPC) == 0,
		    ("nested execve"));
		oldvmspace = td->td_proc->p_vmspace;
		error = kern_execve(td, &args, NULL);
		KASSERT(error != 0,
		    ("kern_execve returned success, not EJUSTRETURN"));
		if (error == EJUSTRETURN) {
			if ((td->td_pflags & TDP_EXECVMSPC) != 0) {
				KASSERT(p->p_vmspace != oldvmspace,
				    ("oldvmspace still used"));
				vmspace_free(oldvmspace);
				td->td_pflags &= ~TDP_EXECVMSPC;
			}
			free(free_init_path, M_TEMP);
			TSEXIT();
			return;
		}
		if (error != ENOENT)
			printf("exec %s: error %d\n", path, error);
	}
	free(free_init_path, M_TEMP);
	printf("init: not found in path %s\n", init_path);
	panic("no init");
}

/*
 * Like kproc_create(), but runs in its own address space.
 * We do this early to reserve pid 1.
 *
 * Note special case - do not make it runnable yet.  Other work
 * in progress will change this more.
 */
static void
create_init(const void *udata __unused)
{
	struct fork_req fr;
	struct ucred *newcred, *oldcred;
	struct thread *td;
	int error;

	bzero(&fr, sizeof(fr));
	fr.fr_flags = RFFDG | RFPROC | RFSTOPPED;
	fr.fr_procp = &initproc;
	error = fork1(&thread0, &fr);
	if (error)
		panic("cannot fork init: %d\n", error);
	KASSERT(initproc->p_pid == 1, ("create_init: initproc->p_pid != 1"));
	/* divorce init's credentials from the kernel's */
	newcred = crget();
	sx_xlock(&proctree_lock);
	PROC_LOCK(initproc);
	initproc->p_flag |= P_SYSTEM | P_INMEM;
	initproc->p_treeflag |= P_TREE_REAPER;
	oldcred = initproc->p_ucred;
	crcopy(newcred, oldcred);
#ifdef MAC
	mac_cred_create_init(newcred);
#endif
#ifdef AUDIT
	audit_cred_proc1(newcred);
#endif
	proc_set_cred(initproc, newcred);
	td = FIRST_THREAD_IN_PROC(initproc);
	crfree(td->td_ucred);
	td->td_ucred = crhold(initproc->p_ucred);
	PROC_UNLOCK(initproc);
	sx_xunlock(&proctree_lock);
	crfree(oldcred);
	cpu_fork_kthread_handler(FIRST_THREAD_IN_PROC(initproc),
	    start_init, NULL);
}
SYSINIT(init, SI_SUB_CREATE_INIT, SI_ORDER_FIRST, create_init, NULL);

/*
 * Make it runnable now.
 */
static void
kick_init(const void *udata __unused)
{
	struct thread *td;

	td = FIRST_THREAD_IN_PROC(initproc);
	thread_lock(td);
	TD_SET_CAN_RUN(td);
	sched_add(td, SRQ_BORING);
	thread_unlock(td);
}
SYSINIT(kickinit, SI_SUB_KTHREAD_INIT, SI_ORDER_MIDDLE, kick_init, NULL);
