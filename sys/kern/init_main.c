/*	$OpenBSD: init_main.c,v 1.330 2025/09/09 09:16:18 bluhm Exp $	*/
/*	$NetBSD: init_main.c,v 1.84.4.1 1996/06/02 09:08:06 mrg Exp $	*/

/*
 * Copyright (c) 1995 Christopher G. Demetriou.  All rights reserved.
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
 *	@(#)init_main.c	8.9 (Berkeley) 1/21/94
 */

#include <sys/param.h>
#include <sys/filedesc.h>
#include <sys/errno.h>
#include <sys/kthread.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/clockintr.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/tty.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/socketvar.h>
#include <sys/lockf.h>
#include <sys/reboot.h>
#ifdef SYSVSHM
#include <sys/shm.h>
#endif
#ifdef SYSVSEM
#include <sys/sem.h>
#endif
#ifdef SYSVMSG
#include <sys/msg.h>
#endif
#include <sys/domain.h>
#include <sys/event.h>
#include <sys/msgbuf.h>
#include <sys/mbuf.h>
#include <sys/pipe.h>
#include <sys/witness.h>
#include <sys/smr.h>
#include <sys/evcount.h>

#include <sys/syscallargs.h>

#include <uvm/uvm_extern.h>

#include <net/if.h>
#include <net/rtable.h>

#if defined(CRYPTO)
#include <crypto/cryptodev.h>
#include <crypto/cryptosoft.h>
#endif

#if defined(KUBSAN)
extern void kubsan_init(void);
#endif

#if defined(NFSSERVER) || defined(NFSCLIENT)
extern void nfs_init(void);
#endif

#include "stoeplitz.h"
#if NSTOEPLITZ > 0
extern void stoeplitz_init(void);
#endif

#include "mpath.h"
#include "vscsi.h"
#include "softraid.h"

const char	copyright[] =
"Copyright (c) 1982, 1986, 1989, 1991, 1993\n"
"\tThe Regents of the University of California.  All rights reserved.\n"
"Copyright (c) 1995-2025 OpenBSD. All rights reserved.  https://www.OpenBSD.org\n";

/* Components of the first process -- never freed. */
struct	session session0;
struct	pgrp pgrp0;
struct	proc proc0;
struct	process process0;
struct	plimit limit0;
struct	vmspace vmspace0;
struct	sigacts sigacts0;
struct	process *initprocess;
struct	proc *reaperproc;

extern	struct user *proc0paddr;

struct	vnode *rootvp, *swapdev_vp;
int	boothowto;
int	db_active = 0;
int	ncpus =  1;
int	ncpusfound = 1;			/* number of cpus we find */
volatile int start_init_exec;		/* semaphore for start_init() */

#if !defined(NO_PROPOLICE)
long	__guard_local __attribute__((section(".openbsd.randomdata")));
#endif

/* XXX return int so gcc -Werror won't complain */
int	main(void *);
void	check_console(struct proc *);
void	start_init(void *);
void	db_ctf_init(void);
void	prof_init(void);
void	init_exec(void);
void	futex_init(void);
void	tslp_init(void);
void	taskq_init(void);
void	timeout_proc_init(void);
void	pool_gc_pages(void *);
void	percpu_init(void);

#ifdef DIAGNOSTIC
int pdevinit_done = 0;
#endif

/*
 * System startup; initialize the world, create process 0, mount root
 * filesystem, and fork to create init and pagedaemon.  Most of the
 * hard work is done in the lower-level initialization routines including
 * startup(), which does memory initialization and autoconfiguration.
 */
/* XXX return int, so gcc -Werror won't complain */
int
main(void *framep)
{
	struct proc *p;
	struct process *pr;
	struct pdevinit *pdev;
	extern struct pdevinit pdevinit[];
	extern void disk_init(void);

	/*
	 * Initialize the current process pointer (curproc) before
	 * any possible traps/probes to simplify trap processing.
	 */
	curproc = p = &proc0;
	p->p_cpu = curcpu();

	/*
	 * Initialize timeouts.
	 */
	timeout_startup();

	/*
	 * Attempt to find console and initialize
	 * in case of early panic or other messages.
	 */
	config_init();		/* init autoconfiguration data structures */
	consinit();

	printf("%s\n", copyright);

#ifdef KUBSAN
	/* Initialize kubsan. */
	kubsan_init();
#endif

	WITNESS_INITIALIZE();

	KERNEL_LOCK_INIT();
	SCHED_LOCK_INIT();

	rw_obj_init();
	uvm_init();
	disk_init();		/* must come before autoconfiguration */
	tty_init();		/* initialise tty's */
	cpu_startup();

	random_start(boothowto & RB_GOODRANDOM);	/* Start the flow */

	/*
	 * Initialize mbuf's.  Do this now because we might attempt to
	 * allocate mbufs or mbuf clusters during autoconfiguration.
	 */
	mbinit();

#if NSTOEPLITZ > 0
	stoeplitz_init();
#endif

	/* Initialize sockets. */
	soinit();

	/* Initialize SRP subsystem. */
	srp_startup();

	/* Initialize SMR subsystem. */
	smr_startup();

	/*
	 * Initialize process and pgrp structures.
	 */
	procinit();

	/* Initialize file locking. */
	lf_init();

	/*
	 * Initialize filedescriptors.
	 */
	filedesc_init();

	/*
	 * Initialize pipes.
	 */
	pipe_init();

	/*
	 * Initialize kqueues.
	 */
	kqueue_init();

	/*
	 * Initialize futexes.
	 */
	futex_init();
	tslp_init();

	/* Create credentials. */
	p->p_ucred = crget();
	p->p_ucred->cr_ngroups = 1;	/* group 0 */

	/*
	 * Create process 0 (the swapper).
	 */
	pr = &process0;
	process_initialize(pr, p);

	LIST_INSERT_HEAD(&allprocess, pr, ps_list);
	LIST_INSERT_HEAD(PIDHASH(0), pr, ps_hash);
	atomic_setbits_int(&pr->ps_flags, PS_SYSTEM);

	/* Set the default routing table/domain. */
	process0.ps_rtableid = 0;

	LIST_INSERT_HEAD(&allproc, p, p_list);
	pr->ps_pgrp = &pgrp0;
	LIST_INSERT_HEAD(TIDHASH(0), p, p_hash);
	LIST_INSERT_HEAD(PGRPHASH(0), &pgrp0, pg_hash);
	LIST_INIT(&pgrp0.pg_members);
	LIST_INSERT_HEAD(&pgrp0.pg_members, pr, ps_pglist);

	pgrp0.pg_session = &session0;
	session0.s_count = 1;
	session0.s_leader = pr;

	atomic_setbits_int(&p->p_flag, P_SYSTEM);
	p->p_stat = SONPROC;
	pr->ps_nice = NZERO;
	strlcpy(pr->ps_comm, "swapper", sizeof(pr->ps_comm));

	/* Init timeouts. */
	timeout_set(&p->p_sleep_to, endtsleep, p);

	/* Initialize signal state for process 0. */
	signal_init();
	siginit(&sigacts0);
	pr->ps_sigacts = &sigacts0;

	/* Create the file descriptor table. */
	p->p_fd = pr->ps_fd = fdinit();

	/* Create the limits structures. */
	lim_startup(&limit0);
	pr->ps_limit = &limit0;

	/* Allocate a prototype map so we have something to fork. */
	uvmspace_init(&vmspace0, pmap_kernel(), round_page(VM_MIN_ADDRESS),
	    trunc_page(VM_MAX_ADDRESS), TRUE, TRUE);
	p->p_vmspace = pr->ps_vmspace = &vmspace0;

	p->p_addr = proc0paddr;				/* XXX */

	/*
	 * Charge root for one process.
	 */
	(void)chgproccnt(0, 1);

	/* Initialize run queues */
	sched_init();
	sleep_queue_init();
	clockqueue_init(&curcpu()->ci_queue);
	sched_init_cpu(curcpu());
	p->p_cpu->ci_randseed = (arc4random() & 0x7fffffff) + 1;

	/* Initialize timeouts in process context. */
	timeout_proc_init();

	/* Initialize task queues */
	taskq_init();

	/* Initialize the interface/address trees */
	ifinit();
	softnet_init();

	/* Lock the kernel on behalf of proc0. */
	KERNEL_LOCK();

#if NMPATH > 0
	/* Attach mpath before hardware */
	config_rootfound("mpath", NULL);
#endif

	/* Configure the devices */
	cpu_configure();

	/* Configure virtual memory system, set vm rlimits. */
	uvm_init_limits(&limit0);

	/* Per CPU memory allocation */
	percpu_init();

	/* Reduce softnet threads to number of CPU */
	softnet_percpu();

	/* Initialize the file systems. */
#if defined(NFSSERVER) || defined(NFSCLIENT)
	nfs_init();			/* initialize server/shared data */
#endif
	vfsinit();

	/* Start real time and statistics clocks. */
	initclocks();

#ifdef SYSVSHM
	/* Initialize System V style shared memory. */
	shminit();
#endif

#ifdef SYSVSEM
	/* Initialize System V style semaphores. */
	seminit();
#endif

#ifdef SYSVMSG
	/* Initialize System V style message queues. */
	msginit();
#endif

	/* Create default routing table before attaching lo0. */
	rtable_init();

	/* Attach pseudo-devices. */
	for (pdev = pdevinit; pdev->pdev_attach != NULL; pdev++)
		if (pdev->pdev_count > 0)
			(*pdev->pdev_attach)(pdev->pdev_count);
#ifdef DIAGNOSTIC
	pdevinit_done = 1;
#endif

#ifdef CRYPTO
	crypto_init();
	swcr_init();
#endif /* CRYPTO */

	/*
	 * Initialize protocols.
	 */
	domaininit();

	initconsbuf();

#if defined(GPROF) || defined(DDBPROF)
	/* Initialize kernel profiling. */
	prof_init();
#endif

	/* Enable per-CPU data. */
	mbcpuinit();
	kqueue_init_percpu();
	pmap_init_percpu();
	uvm_init_percpu();
	evcount_init_percpu();

	/* init exec */
	init_exec();

	/* Start the scheduler */
	scheduler_start();

	/*
	 * Create process 1 (init(8)).  We do this now, as Unix has
	 * historically had init be process 1, and changing this would
	 * probably upset a lot of people.
	 *
	 * Note that process 1 won't immediately exec init(8), but will
	 * wait for us to inform it that the root file system has been
	 * mounted.
	 */
	{
		struct proc *initproc;

		if (fork1(p, FORK_FORK, start_init, NULL, NULL, &initproc))
			panic("fork init");
		initprocess = initproc->p_p;
	}

	/*
	 * Create any kernel threads whose creation was deferred because
	 * initprocess had not yet been created.
	 */
	kthread_run_deferred_queue();

	/*
	 * Now that device driver threads have been created, wait for
	 * them to finish any deferred autoconfiguration.  Note we don't
	 * need to lock this semaphore, since we haven't booted any
	 * secondary processors, yet.
	 */
	while (config_pending)
		tsleep_nsec(&config_pending, PWAIT, "cfpend", INFSLP);

	dostartuphooks();

#if NVSCSI > 0
	config_rootfound("vscsi", NULL);
#endif
#if NSOFTRAID > 0
	config_rootfound("softraid", NULL);
#endif

	/* Configure root/swap devices */
	diskconf();

#ifdef DDB
	/* Make debug symbols available in ddb. */
	db_ctf_init();
#endif

	if (mountroot == NULL || ((*mountroot)() != 0))
		panic("cannot mount root");

	TAILQ_FIRST(&mountlist)->mnt_flag |= MNT_ROOTFS;

	/* Get the vnode for '/'.  Set p->p_fd->fd_cdir to reference it. */
	if (VFS_ROOT(TAILQ_FIRST(&mountlist), &rootvnode))
		panic("cannot find root vnode");
	p->p_fd->fd_cdir = rootvnode;
	vref(p->p_fd->fd_cdir);
	VOP_UNLOCK(rootvnode);
	p->p_fd->fd_rdir = NULL;

	/*
	 * Now that root is mounted, we can fixup initprocess's CWD
	 * info.  All other processes are kthreads, which merely
	 * share proc0's CWD info.
	 */
	initprocess->ps_fd->fd_cdir = rootvnode;
	vref(initprocess->ps_fd->fd_cdir);
	initprocess->ps_fd->fd_rdir = NULL;

	/*
	 * Now can look at time, having had a chance to verify the time
	 * from the file system. 
	 */
	LIST_FOREACH(pr, &allprocess, ps_list) {
		nanouptime(&pr->ps_start);
	}
	nanouptime(&curcpu()->ci_schedstate.spc_runtime);

	uvm_swap_init();

	/* Create the pageout daemon kernel thread. */
	if (kthread_create(uvm_pageout, NULL, NULL, "pagedaemon"))
		panic("fork pagedaemon");

	/* Create the reaper daemon kernel thread. */
	if (kthread_create(reaper, NULL, &reaperproc, "reaper"))
		panic("fork reaper");

	/* Create the cleaner daemon kernel thread. */
	if (kthread_create(buf_daemon, NULL, &cleanerproc, "cleaner"))
		panic("fork cleaner");

	/* Create the update daemon kernel thread. */
	if (kthread_create(syncer_thread, NULL, &syncerproc, "update"))
		panic("fork update");

	/* Create the aiodone daemon kernel thread. */ 
	if (kthread_create(uvm_aiodone_daemon, NULL, NULL, "aiodoned"))
		panic("fork aiodoned");

#if !defined(__hppa__)
	/* Create the page zeroing kernel thread. */
	if (kthread_create(uvm_pagezero_thread, NULL, NULL, "zerothread"))
		panic("fork zerothread");
#endif

#if defined(MULTIPROCESSOR)
	/* Boot the secondary processors. */
	cpu_boot_secondary_processors();
#endif

	/* Now that all CPUs partake in scheduling, start SMR thread. */
	smr_startup_thread();

	config_process_deferred_mountroot();

	/*
	 * Okay, now we can let init(8) exec!  It's off to userland!
	 */
	start_init_exec = 1;
	wakeup((void *)&start_init_exec);

	/*
	 * Start the idle pool page garbage collector
	 */
#if defined(MULTIPROCESSOR)
	pool_gc_pages(NULL);
#endif

	start_periodic_resettodr();

        /*
         * proc0: nothing to do, back to sleep
         */
        while (1)
                tsleep_nsec(&proc0, PVM, "scheduler", INFSLP);
	/* NOTREACHED */
}

/*
 * List of paths to try when searching for "init".
 */
static char *initpaths[] = {
	"/sbin/init",
	"/sbin/oinit",
	"/sbin/init.bak",
	NULL,
};

void
check_console(struct proc *p)
{
	struct nameidata nd;
	int error;

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, "/dev/console", p);
	error = namei(&nd);
	if (error) {
		if (error == ENOENT)
			printf("warning: /dev/console does not exist\n");
		else
			printf("warning: /dev/console error %d\n", error);
	} else
		vrele(nd.ni_vp);
}

/*
 * Start the initial user process; try exec'ing each pathname in "initpaths".
 * The program is invoked with one argument containing the boot flags.
 */
void
start_init(void *arg)
{
	struct proc *p = arg;
	vaddr_t addr;
	struct sys_execve_args /* {
		syscallarg(const char *) path;
		syscallarg(char *const *) argp;
		syscallarg(char *const *) envp;
	} */ args;
	int options, error;
	long i;
	register_t retval[2];
	char flags[4], *flagsp;
	char **pathp, *path, *ucp, **uap, *arg0, *arg1 = NULL;

	/*
	 * Now in process 1.
	 */

	/*
	 * Wait for main() to tell us that it's safe to exec.
	 */
	while (start_init_exec == 0)
		tsleep_nsec(&start_init_exec, PWAIT, "initexec", INFSLP);

	check_console(p);

	/* process 0 ignores SIGCHLD, but we can't */
	p->p_p->ps_sigacts->ps_sigflags = 0;

	/*
	 * Need just enough stack to hold the faked-up "execve()" arguments.
	 */
#ifdef MACHINE_STACK_GROWS_UP
	addr = USRSTACK;
#else
	addr = USRSTACK - PAGE_SIZE;
#endif
	p->p_vmspace->vm_maxsaddr = (caddr_t)addr;
	p->p_vmspace->vm_minsaddr = (caddr_t)(addr + PAGE_SIZE);
	if (uvm_map(&p->p_vmspace->vm_map, &addr, PAGE_SIZE, 
	    NULL, UVM_UNKNOWN_OFFSET, 0,
	    UVM_MAPFLAG(PROT_READ | PROT_WRITE, PROT_MASK, MAP_INHERIT_COPY,
	    MADV_NORMAL,
	    UVM_FLAG_FIXED|UVM_FLAG_OVERLAY|UVM_FLAG_COPYONW|UVM_FLAG_STACK)))
		panic("init: couldn't allocate argument space");

	for (pathp = &initpaths[0]; (path = *pathp) != NULL; pathp++) {
#ifdef MACHINE_STACK_GROWS_UP
		ucp = (char *)addr;
#else
		ucp = (char *)(addr + PAGE_SIZE);
#endif
		/*
		 * Construct the boot flag argument.
		 */
		flagsp = flags;
		*flagsp++ = '-';
		options = 0;

		if (boothowto & RB_SINGLE) {
			*flagsp++ = 's';
			options = 1;
		}
#ifdef notyet
		if (boothowto & RB_FASTBOOT) {
			*flagsp++ = 'f';
			options = 1;
		}
#endif

		/*
		 * Move out the flags (arg 1), if necessary.
		 */
		if (options != 0) {
			*flagsp++ = '\0';
			i = flagsp - flags;
#ifdef DEBUG
			printf("init: copying out flags `%s' %ld\n", flags, i);
#endif
#ifdef MACHINE_STACK_GROWS_UP
			arg1 = ucp;
			(void)copyout((caddr_t)flags, (caddr_t)ucp, i);
			ucp += i;
#else
			(void)copyout((caddr_t)flags, (caddr_t)(ucp -= i), i);
			arg1 = ucp;
#endif
		}

		/*
		 * Move out the file name (also arg 0).
		 */
		i = strlen(path) + 1;
#ifdef DEBUG
		printf("init: copying out path `%s' %ld\n", path, i);
#endif
#ifdef MACHINE_STACK_GROWS_UP
		arg0 = ucp;
		(void)copyout((caddr_t)path, (caddr_t)ucp, i);
		ucp += i;
		ucp = (caddr_t)ALIGN((u_long)ucp);
		uap = (char **)ucp + 3;
#else
		(void)copyout((caddr_t)path, (caddr_t)(ucp -= i), i);
		arg0 = ucp;
		uap = (char **)((u_long)ucp & ~ALIGNBYTES);
#endif

		/*
		 * Move out the arg pointers.
		 */
		i = 0;
		copyout(&i, (caddr_t)--uap, sizeof(register_t)); /* terminator */
		if (options != 0)
			copyout(&arg1, (caddr_t)--uap, sizeof(register_t));
		copyout(&arg0, (caddr_t)--uap, sizeof(register_t));

		/*
		 * Point at the arguments.
		 */
		SCARG(&args, path) = arg0;
		SCARG(&args, argp) = uap;
		SCARG(&args, envp) = NULL;

		/*
		 * Now try to exec the program.  If can't for any reason
		 * other than it doesn't exist, complain.
		 */
		if ((error = sys_execve(p, &args, retval)) == EJUSTRETURN) {
			KERNEL_UNLOCK();
			return;
		}
		if (error != ENOENT)
			printf("exec %s: error %d\n", path, error);
	}
	printf("init: not found\n");
	panic("no init");
}
