/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1986, 1988, 1991, 1993
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
 *	@(#)kern_shutdown.c	8.3 (Berkeley) 1/21/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_ekcd.h"
#include "opt_kdb.h"
#include "opt_panic.h"
#include "opt_sched.h"
#include "opt_watchdog.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/compressor.h>
#include <sys/cons.h>
#include <sys/eventhandler.h>
#include <sys/filedesc.h>
#include <sys/jail.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/kerneldump.h>
#include <sys/kthread.h>
#include <sys/ktr.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/taskqueue.h>
#include <sys/vnode.h>
#include <sys/watchdog.h>

#include <crypto/rijndael/rijndael-api-fst.h>
#include <crypto/sha2/sha256.h>

#include <ddb/ddb.h>

#include <machine/cpu.h>
#include <machine/dump.h>
#include <machine/pcb.h>
#include <machine/smp.h>

#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/swap_pager.h>

#include <sys/signalvar.h>

static MALLOC_DEFINE(M_DUMPER, "dumper", "dumper block buffer");

#ifndef PANIC_REBOOT_WAIT_TIME
#define PANIC_REBOOT_WAIT_TIME 15 /* default to 15 seconds */
#endif
static int panic_reboot_wait_time = PANIC_REBOOT_WAIT_TIME;
SYSCTL_INT(_kern, OID_AUTO, panic_reboot_wait_time, CTLFLAG_RWTUN,
    &panic_reboot_wait_time, 0,
    "Seconds to wait before rebooting after a panic");

/*
 * Note that stdarg.h and the ANSI style va_start macro is used for both
 * ANSI and traditional C compilers.
 */
#include <machine/stdarg.h>

#ifdef KDB
#ifdef KDB_UNATTENDED
static int debugger_on_panic = 0;
#else
static int debugger_on_panic = 1;
#endif
SYSCTL_INT(_debug, OID_AUTO, debugger_on_panic,
    CTLFLAG_RWTUN | CTLFLAG_SECURE,
    &debugger_on_panic, 0, "Run debugger on kernel panic");

int debugger_on_trap = 0;
SYSCTL_INT(_debug, OID_AUTO, debugger_on_trap,
    CTLFLAG_RWTUN | CTLFLAG_SECURE,
    &debugger_on_trap, 0, "Run debugger on kernel trap before panic");

#ifdef KDB_TRACE
static int trace_on_panic = 1;
static bool trace_all_panics = true;
#else
static int trace_on_panic = 0;
static bool trace_all_panics = false;
#endif
SYSCTL_INT(_debug, OID_AUTO, trace_on_panic,
    CTLFLAG_RWTUN | CTLFLAG_SECURE,
    &trace_on_panic, 0, "Print stack trace on kernel panic");
SYSCTL_BOOL(_debug, OID_AUTO, trace_all_panics, CTLFLAG_RWTUN,
    &trace_all_panics, 0, "Print stack traces on secondary kernel panics");
#endif /* KDB */

static int sync_on_panic = 0;
SYSCTL_INT(_kern, OID_AUTO, sync_on_panic, CTLFLAG_RWTUN,
	&sync_on_panic, 0, "Do a sync before rebooting from a panic");

static bool poweroff_on_panic = 0;
SYSCTL_BOOL(_kern, OID_AUTO, poweroff_on_panic, CTLFLAG_RWTUN,
	&poweroff_on_panic, 0, "Do a power off instead of a reboot on a panic");

static bool powercycle_on_panic = 0;
SYSCTL_BOOL(_kern, OID_AUTO, powercycle_on_panic, CTLFLAG_RWTUN,
	&powercycle_on_panic, 0, "Do a power cycle instead of a reboot on a panic");

static SYSCTL_NODE(_kern, OID_AUTO, shutdown, CTLFLAG_RW, 0,
    "Shutdown environment");

#ifndef DIAGNOSTIC
static int show_busybufs;
#else
static int show_busybufs = 1;
#endif
SYSCTL_INT(_kern_shutdown, OID_AUTO, show_busybufs, CTLFLAG_RW,
	&show_busybufs, 0, "");

int suspend_blocked = 0;
SYSCTL_INT(_kern, OID_AUTO, suspend_blocked, CTLFLAG_RW,
	&suspend_blocked, 0, "Block suspend due to a pending shutdown");

#ifdef EKCD
FEATURE(ekcd, "Encrypted kernel crash dumps support");

MALLOC_DEFINE(M_EKCD, "ekcd", "Encrypted kernel crash dumps data");

struct kerneldumpcrypto {
	uint8_t			kdc_encryption;
	uint8_t			kdc_iv[KERNELDUMP_IV_MAX_SIZE];
	keyInstance		kdc_ki;
	cipherInstance		kdc_ci;
	uint32_t		kdc_dumpkeysize;
	struct kerneldumpkey	kdc_dumpkey[];
};
#endif

struct kerneldumpcomp {
	uint8_t			kdc_format;
	struct compressor	*kdc_stream;
	uint8_t			*kdc_buf;
	size_t			kdc_resid;
};

static struct kerneldumpcomp *kerneldumpcomp_create(struct dumperinfo *di,
		    uint8_t compression);
static void	kerneldumpcomp_destroy(struct dumperinfo *di);
static int	kerneldumpcomp_write_cb(void *base, size_t len, off_t off, void *arg);

static int kerneldump_gzlevel = 6;
SYSCTL_INT(_kern, OID_AUTO, kerneldump_gzlevel, CTLFLAG_RWTUN,
    &kerneldump_gzlevel, 0,
    "Kernel crash dump compression level");

/*
 * Variable panicstr contains argument to first call to panic; used as flag
 * to indicate that the kernel has already called panic.
 */
const char *panicstr;

int dumping;				/* system is dumping */
int rebooting;				/* system is rebooting */
static struct dumperinfo dumper;	/* our selected dumper */

/* Context information for dump-debuggers. */
static struct pcb dumppcb;		/* Registers. */
lwpid_t dumptid;			/* Thread ID. */

static struct cdevsw reroot_cdevsw = {
     .d_version = D_VERSION,
     .d_name    = "reroot",
};

static void poweroff_wait(void *, int);
static void shutdown_halt(void *junk, int howto);
static void shutdown_panic(void *junk, int howto);
static void shutdown_reset(void *junk, int howto);
static int kern_reroot(void);

/* register various local shutdown events */
static void
shutdown_conf(void *unused)
{

	EVENTHANDLER_REGISTER(shutdown_final, poweroff_wait, NULL,
	    SHUTDOWN_PRI_FIRST);
	EVENTHANDLER_REGISTER(shutdown_final, shutdown_halt, NULL,
	    SHUTDOWN_PRI_LAST + 100);
	EVENTHANDLER_REGISTER(shutdown_final, shutdown_panic, NULL,
	    SHUTDOWN_PRI_LAST + 100);
	EVENTHANDLER_REGISTER(shutdown_final, shutdown_reset, NULL,
	    SHUTDOWN_PRI_LAST + 200);
}

SYSINIT(shutdown_conf, SI_SUB_INTRINSIC, SI_ORDER_ANY, shutdown_conf, NULL);

/*
 * The only reason this exists is to create the /dev/reroot/ directory,
 * used by reroot code in init(8) as a mountpoint for tmpfs.
 */
static void
reroot_conf(void *unused)
{
	int error;
	struct cdev *cdev;

	error = make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK, &cdev,
	    &reroot_cdevsw, NULL, UID_ROOT, GID_WHEEL, 0600, "reroot/reroot");
	if (error != 0) {
		printf("%s: failed to create device node, error %d",
		    __func__, error);
	}
}

SYSINIT(reroot_conf, SI_SUB_DEVFS, SI_ORDER_ANY, reroot_conf, NULL);

/*
 * The system call that results in a reboot.
 */
/* ARGSUSED */
int
sys_reboot(struct thread *td, struct reboot_args *uap)
{
	int error;

	error = 0;
#ifdef MAC
	error = mac_system_check_reboot(td->td_ucred, uap->opt);
#endif
	if (error == 0)
		error = priv_check(td, PRIV_REBOOT);
	if (error == 0) {
		if (uap->opt & RB_REROOT)
			error = kern_reroot();
		else
			kern_reboot(uap->opt);
	}
	return (error);
}

static void
shutdown_nice_task_fn(void *arg, int pending __unused)
{
	int howto;

	howto = (uintptr_t)arg;
	/* Send a signal to init(8) and have it shutdown the world. */
	PROC_LOCK(initproc);
	if (howto & RB_POWEROFF)
		kern_psignal(initproc, SIGUSR2);
	else if (howto & RB_POWERCYCLE)
		kern_psignal(initproc, SIGWINCH);
	else if (howto & RB_HALT)
		kern_psignal(initproc, SIGUSR1);
	else
		kern_psignal(initproc, SIGINT);
	PROC_UNLOCK(initproc);
}

static struct task shutdown_nice_task = TASK_INITIALIZER(0,
    &shutdown_nice_task_fn, NULL);

/*
 * Called by events that want to shut down.. e.g  <CTL><ALT><DEL> on a PC
 */
void
shutdown_nice(int howto)
{

	if (initproc != NULL && !SCHEDULER_STOPPED()) {
		shutdown_nice_task.ta_context = (void *)(uintptr_t)howto;
		taskqueue_enqueue(taskqueue_fast, &shutdown_nice_task);
	} else {
		/*
		 * No init(8) running, or scheduler would not allow it
		 * to run, so simply reboot.
		 */
		kern_reboot(howto | RB_NOSYNC);
	}
}

static void
print_uptime(void)
{
	int f;
	struct timespec ts;

	getnanouptime(&ts);
	printf("Uptime: ");
	f = 0;
	if (ts.tv_sec >= 86400) {
		printf("%ldd", (long)ts.tv_sec / 86400);
		ts.tv_sec %= 86400;
		f = 1;
	}
	if (f || ts.tv_sec >= 3600) {
		printf("%ldh", (long)ts.tv_sec / 3600);
		ts.tv_sec %= 3600;
		f = 1;
	}
	if (f || ts.tv_sec >= 60) {
		printf("%ldm", (long)ts.tv_sec / 60);
		ts.tv_sec %= 60;
		f = 1;
	}
	printf("%lds\n", (long)ts.tv_sec);
}

int
doadump(boolean_t textdump)
{
	boolean_t coredump;
	int error;

	error = 0;
	if (dumping)
		return (EBUSY);
	if (dumper.dumper == NULL)
		return (ENXIO);

	savectx(&dumppcb);
	dumptid = curthread->td_tid;
	dumping++;

	coredump = TRUE;
#ifdef DDB
	if (textdump && textdump_pending) {
		coredump = FALSE;
		textdump_dumpsys(&dumper);
	}
#endif
	if (coredump)
		error = dumpsys(&dumper);

	dumping--;
	return (error);
}

/*
 * Shutdown the system cleanly to prepare for reboot, halt, or power off.
 */
void
kern_reboot(int howto)
{
	static int once = 0;

	/*
	 * Normal paths here don't hold Giant, but we can wind up here
	 * unexpectedly with it held.  Drop it now so we don't have to
	 * drop and pick it up elsewhere. The paths it is locking will
	 * never be returned to, and it is preferable to preclude
	 * deadlock than to lock against code that won't ever
	 * continue.
	 */
	while (mtx_owned(&Giant))
		mtx_unlock(&Giant);

#if defined(SMP)
	/*
	 * Bind us to the first CPU so that all shutdown code runs there.  Some
	 * systems don't shutdown properly (i.e., ACPI power off) if we
	 * run on another processor.
	 */
	if (!SCHEDULER_STOPPED()) {
		thread_lock(curthread);
		sched_bind(curthread, CPU_FIRST());
		thread_unlock(curthread);
		KASSERT(PCPU_GET(cpuid) == CPU_FIRST(),
		    ("boot: not running on cpu 0"));
	}
#endif
	/* We're in the process of rebooting. */
	rebooting = 1;

	/* We are out of the debugger now. */
	kdb_active = 0;

	/*
	 * Do any callouts that should be done BEFORE syncing the filesystems.
	 */
	EVENTHANDLER_INVOKE(shutdown_pre_sync, howto);

	/* 
	 * Now sync filesystems
	 */
	if (!cold && (howto & RB_NOSYNC) == 0 && once == 0) {
		once = 1;
		bufshutdown(show_busybufs);
	}

	print_uptime();

	cngrab();

	/*
	 * Ok, now do things that assume all filesystem activity has
	 * been completed.
	 */
	EVENTHANDLER_INVOKE(shutdown_post_sync, howto);

	if ((howto & (RB_HALT|RB_DUMP)) == RB_DUMP && !cold && !dumping) 
		doadump(TRUE);

	/* Now that we're going to really halt the system... */
	EVENTHANDLER_INVOKE(shutdown_final, howto);

	for(;;) ;	/* safety against shutdown_reset not working */
	/* NOTREACHED */
}

/*
 * The system call that results in changing the rootfs.
 */
static int
kern_reroot(void)
{
	struct vnode *oldrootvnode, *vp;
	struct mount *mp, *devmp;
	int error;

	if (curproc != initproc)
		return (EPERM);

	/*
	 * Mark the filesystem containing currently-running executable
	 * (the temporary copy of init(8)) busy.
	 */
	vp = curproc->p_textvp;
	error = vn_lock(vp, LK_SHARED);
	if (error != 0)
		return (error);
	mp = vp->v_mount;
	error = vfs_busy(mp, MBF_NOWAIT);
	if (error != 0) {
		vfs_ref(mp);
		VOP_UNLOCK(vp, 0);
		error = vfs_busy(mp, 0);
		vn_lock(vp, LK_SHARED | LK_RETRY);
		vfs_rel(mp);
		if (error != 0) {
			VOP_UNLOCK(vp, 0);
			return (ENOENT);
		}
		if (vp->v_iflag & VI_DOOMED) {
			VOP_UNLOCK(vp, 0);
			vfs_unbusy(mp);
			return (ENOENT);
		}
	}
	VOP_UNLOCK(vp, 0);

	/*
	 * Remove the filesystem containing currently-running executable
	 * from the mount list, to prevent it from being unmounted
	 * by vfs_unmountall(), and to avoid confusing vfs_mountroot().
	 *
	 * Also preserve /dev - forcibly unmounting it could cause driver
	 * reinitialization.
	 */

	vfs_ref(rootdevmp);
	devmp = rootdevmp;
	rootdevmp = NULL;

	mtx_lock(&mountlist_mtx);
	TAILQ_REMOVE(&mountlist, mp, mnt_list);
	TAILQ_REMOVE(&mountlist, devmp, mnt_list);
	mtx_unlock(&mountlist_mtx);

	oldrootvnode = rootvnode;

	/*
	 * Unmount everything except for the two filesystems preserved above.
	 */
	vfs_unmountall();

	/*
	 * Add /dev back; vfs_mountroot() will move it into its new place.
	 */
	mtx_lock(&mountlist_mtx);
	TAILQ_INSERT_HEAD(&mountlist, devmp, mnt_list);
	mtx_unlock(&mountlist_mtx);
	rootdevmp = devmp;
	vfs_rel(rootdevmp);

	/*
	 * Mount the new rootfs.
	 */
	vfs_mountroot();

	/*
	 * Update all references to the old rootvnode.
	 */
	mountcheckdirs(oldrootvnode, rootvnode);

	/*
	 * Add the temporary filesystem back and unbusy it.
	 */
	mtx_lock(&mountlist_mtx);
	TAILQ_INSERT_TAIL(&mountlist, mp, mnt_list);
	mtx_unlock(&mountlist_mtx);
	vfs_unbusy(mp);

	return (0);
}

/*
 * If the shutdown was a clean halt, behave accordingly.
 */
static void
shutdown_halt(void *junk, int howto)
{

	if (howto & RB_HALT) {
		printf("\n");
		printf("The operating system has halted.\n");
		printf("Please press any key to reboot.\n\n");
		switch (cngetc()) {
		case -1:		/* No console, just die */
			cpu_halt();
			/* NOTREACHED */
		default:
			break;
		}
	}
}

/*
 * Check to see if the system paniced, pause and then reboot
 * according to the specified delay.
 */
static void
shutdown_panic(void *junk, int howto)
{
	int loop;

	if (howto & RB_DUMP) {
		if (panic_reboot_wait_time != 0) {
			if (panic_reboot_wait_time != -1) {
				printf("Automatic reboot in %d seconds - "
				       "press a key on the console to abort\n",
					panic_reboot_wait_time);
				for (loop = panic_reboot_wait_time * 10;
				     loop > 0; --loop) {
					DELAY(1000 * 100); /* 1/10th second */
					/* Did user type a key? */
					if (cncheckc() != -1)
						break;
				}
				if (!loop)
					return;
			}
		} else { /* zero time specified - reboot NOW */
			return;
		}
		printf("--> Press a key on the console to reboot,\n");
		printf("--> or switch off the system now.\n");
		cngetc();
	}
}

/*
 * Everything done, now reset
 */
static void
shutdown_reset(void *junk, int howto)
{

	printf("Rebooting...\n");
	DELAY(1000000);	/* wait 1 sec for printf's to complete and be read */

	/*
	 * Acquiring smp_ipi_mtx here has a double effect:
	 * - it disables interrupts avoiding CPU0 preemption
	 *   by fast handlers (thus deadlocking  against other CPUs)
	 * - it avoids deadlocks against smp_rendezvous() or, more 
	 *   generally, threads busy-waiting, with this spinlock held,
	 *   and waiting for responses by threads on other CPUs
	 *   (ie. smp_tlb_shootdown()).
	 *
	 * For the !SMP case it just needs to handle the former problem.
	 */
#ifdef SMP
	mtx_lock_spin(&smp_ipi_mtx);
#else
	spinlock_enter();
#endif

	/* cpu_boot(howto); */ /* doesn't do anything at the moment */
	cpu_reset();
	/* NOTREACHED */ /* assuming reset worked */
}

#if defined(WITNESS) || defined(INVARIANT_SUPPORT)
static int kassert_warn_only = 0;
#ifdef KDB
static int kassert_do_kdb = 0;
#endif
#ifdef KTR
static int kassert_do_ktr = 0;
#endif
static int kassert_do_log = 1;
static int kassert_log_pps_limit = 4;
static int kassert_log_mute_at = 0;
static int kassert_log_panic_at = 0;
static int kassert_suppress_in_panic = 0;
static int kassert_warnings = 0;

SYSCTL_NODE(_debug, OID_AUTO, kassert, CTLFLAG_RW, NULL, "kassert options");

#ifdef KASSERT_PANIC_OPTIONAL
#define KASSERT_RWTUN	CTLFLAG_RWTUN
#else
#define KASSERT_RWTUN	CTLFLAG_RDTUN
#endif

SYSCTL_INT(_debug_kassert, OID_AUTO, warn_only, KASSERT_RWTUN,
    &kassert_warn_only, 0,
    "KASSERT triggers a panic (0) or just a warning (1)");

#ifdef KDB
SYSCTL_INT(_debug_kassert, OID_AUTO, do_kdb, KASSERT_RWTUN,
    &kassert_do_kdb, 0, "KASSERT will enter the debugger");
#endif

#ifdef KTR
SYSCTL_UINT(_debug_kassert, OID_AUTO, do_ktr, KASSERT_RWTUN,
    &kassert_do_ktr, 0,
    "KASSERT does a KTR, set this to the KTRMASK you want");
#endif

SYSCTL_INT(_debug_kassert, OID_AUTO, do_log, KASSERT_RWTUN,
    &kassert_do_log, 0,
    "If warn_only is enabled, log (1) or do not log (0) assertion violations");

SYSCTL_INT(_debug_kassert, OID_AUTO, warnings, KASSERT_RWTUN,
    &kassert_warnings, 0, "number of KASSERTs that have been triggered");

SYSCTL_INT(_debug_kassert, OID_AUTO, log_panic_at, KASSERT_RWTUN,
    &kassert_log_panic_at, 0, "max number of KASSERTS before we will panic");

SYSCTL_INT(_debug_kassert, OID_AUTO, log_pps_limit, KASSERT_RWTUN,
    &kassert_log_pps_limit, 0, "limit number of log messages per second");

SYSCTL_INT(_debug_kassert, OID_AUTO, log_mute_at, KASSERT_RWTUN,
    &kassert_log_mute_at, 0, "max number of KASSERTS to log");

SYSCTL_INT(_debug_kassert, OID_AUTO, suppress_in_panic, KASSERT_RWTUN,
    &kassert_suppress_in_panic, 0,
    "KASSERTs will be suppressed while handling a panic");
#undef KASSERT_RWTUN

static int kassert_sysctl_kassert(SYSCTL_HANDLER_ARGS);

SYSCTL_PROC(_debug_kassert, OID_AUTO, kassert,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_SECURE, NULL, 0,
    kassert_sysctl_kassert, "I", "set to trigger a test kassert");

static int
kassert_sysctl_kassert(SYSCTL_HANDLER_ARGS)
{
	int error, i;

	error = sysctl_wire_old_buffer(req, sizeof(int));
	if (error == 0) {
		i = 0;
		error = sysctl_handle_int(oidp, &i, 0, req);
	}
	if (error != 0 || req->newptr == NULL)
		return (error);
	KASSERT(0, ("kassert_sysctl_kassert triggered kassert %d", i));
	return (0);
}

#ifdef KASSERT_PANIC_OPTIONAL
/*
 * Called by KASSERT, this decides if we will panic
 * or if we will log via printf and/or ktr.
 */
void
kassert_panic(const char *fmt, ...)
{
	static char buf[256];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	/*
	 * If we are suppressing secondary panics, log the warning but do not
	 * re-enter panic/kdb.
	 */
	if (panicstr != NULL && kassert_suppress_in_panic) {
		if (kassert_do_log) {
			printf("KASSERT failed: %s\n", buf);
#ifdef KDB
			if (trace_all_panics && trace_on_panic)
				kdb_backtrace();
#endif
		}
		return;
	}

	/*
	 * panic if we're not just warning, or if we've exceeded
	 * kassert_log_panic_at warnings.
	 */
	if (!kassert_warn_only ||
	    (kassert_log_panic_at > 0 &&
	     kassert_warnings >= kassert_log_panic_at)) {
		va_start(ap, fmt);
		vpanic(fmt, ap);
		/* NORETURN */
	}
#ifdef KTR
	if (kassert_do_ktr)
		CTR0(ktr_mask, buf);
#endif /* KTR */
	/*
	 * log if we've not yet met the mute limit.
	 */
	if (kassert_do_log &&
	    (kassert_log_mute_at == 0 ||
	     kassert_warnings < kassert_log_mute_at)) {
		static  struct timeval lasterr;
		static  int curerr;

		if (ppsratecheck(&lasterr, &curerr, kassert_log_pps_limit)) {
			printf("KASSERT failed: %s\n", buf);
			kdb_backtrace();
		}
	}
#ifdef KDB
	if (kassert_do_kdb) {
		kdb_enter(KDB_WHY_KASSERT, buf);
	}
#endif
	atomic_add_int(&kassert_warnings, 1);
}
#endif /* KASSERT_PANIC_OPTIONAL */
#endif

/*
 * Panic is called on unresolvable fatal errors.  It prints "panic: mesg",
 * and then reboots.  If we are called twice, then we avoid trying to sync
 * the disks as this often leads to recursive panics.
 */
void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vpanic(fmt, ap);
}

void
vpanic(const char *fmt, va_list ap)
{
#ifdef SMP
	cpuset_t other_cpus;
#endif
	struct thread *td = curthread;
	int bootopt, newpanic;
	static char buf[256];

	spinlock_enter();

#ifdef SMP
	/*
	 * stop_cpus_hard(other_cpus) should prevent multiple CPUs from
	 * concurrently entering panic.  Only the winner will proceed
	 * further.
	 */
	if (panicstr == NULL && !kdb_active) {
		other_cpus = all_cpus;
		CPU_CLR(PCPU_GET(cpuid), &other_cpus);
		stop_cpus_hard(other_cpus);
	}
#endif

	/*
	 * Ensure that the scheduler is stopped while panicking, even if panic
	 * has been entered from kdb.
	 */
	td->td_stopsched = 1;

	bootopt = RB_AUTOBOOT;
	newpanic = 0;
	if (panicstr)
		bootopt |= RB_NOSYNC;
	else {
		bootopt |= RB_DUMP;
		panicstr = fmt;
		newpanic = 1;
	}

	if (newpanic) {
		(void)vsnprintf(buf, sizeof(buf), fmt, ap);
		panicstr = buf;
		cngrab();
		printf("panic: %s\n", buf);
	} else {
		printf("panic: ");
		vprintf(fmt, ap);
		printf("\n");
	}
#ifdef SMP
	printf("cpuid = %d\n", PCPU_GET(cpuid));
#endif
	printf("time = %jd\n", (intmax_t )time_second);
#ifdef KDB
	if ((newpanic || trace_all_panics) && trace_on_panic)
		kdb_backtrace();
	if (debugger_on_panic)
		kdb_enter(KDB_WHY_PANIC, "panic");
#endif
	/*thread_lock(td); */
	td->td_flags |= TDF_INPANIC;
	/* thread_unlock(td); */
	if (!sync_on_panic)
		bootopt |= RB_NOSYNC;
	if (poweroff_on_panic)
		bootopt |= RB_POWEROFF;
	if (powercycle_on_panic)
		bootopt |= RB_POWERCYCLE;
	kern_reboot(bootopt);
}

/*
 * Support for poweroff delay.
 *
 * Please note that setting this delay too short might power off your machine
 * before the write cache on your hard disk has been flushed, leading to
 * soft-updates inconsistencies.
 */
#ifndef POWEROFF_DELAY
# define POWEROFF_DELAY 5000
#endif
static int poweroff_delay = POWEROFF_DELAY;

SYSCTL_INT(_kern_shutdown, OID_AUTO, poweroff_delay, CTLFLAG_RW,
    &poweroff_delay, 0, "Delay before poweroff to write disk caches (msec)");

static void
poweroff_wait(void *junk, int howto)
{

	if ((howto & (RB_POWEROFF | RB_POWERCYCLE)) == 0 || poweroff_delay <= 0)
		return;
	DELAY(poweroff_delay * 1000);
}

/*
 * Some system processes (e.g. syncer) need to be stopped at appropriate
 * points in their main loops prior to a system shutdown, so that they
 * won't interfere with the shutdown process (e.g. by holding a disk buf
 * to cause sync to fail).  For each of these system processes, register
 * shutdown_kproc() as a handler for one of shutdown events.
 */
static int kproc_shutdown_wait = 60;
SYSCTL_INT(_kern_shutdown, OID_AUTO, kproc_shutdown_wait, CTLFLAG_RW,
    &kproc_shutdown_wait, 0, "Max wait time (sec) to stop for each process");

void
kproc_shutdown(void *arg, int howto)
{
	struct proc *p;
	int error;

	if (panicstr)
		return;

	p = (struct proc *)arg;
	printf("Waiting (max %d seconds) for system process `%s' to stop... ",
	    kproc_shutdown_wait, p->p_comm);
	error = kproc_suspend(p, kproc_shutdown_wait * hz);

	if (error == EWOULDBLOCK)
		printf("timed out\n");
	else
		printf("done\n");
}

void
kthread_shutdown(void *arg, int howto)
{
	struct thread *td;
	int error;

	if (panicstr)
		return;

	td = (struct thread *)arg;
	printf("Waiting (max %d seconds) for system thread `%s' to stop... ",
	    kproc_shutdown_wait, td->td_name);
	error = kthread_suspend(td, kproc_shutdown_wait * hz);

	if (error == EWOULDBLOCK)
		printf("timed out\n");
	else
		printf("done\n");
}

static char dumpdevname[sizeof(((struct cdev*)NULL)->si_name)];
SYSCTL_STRING(_kern_shutdown, OID_AUTO, dumpdevname, CTLFLAG_RD,
    dumpdevname, 0, "Device for kernel dumps");

static int	_dump_append(struct dumperinfo *di, void *virtual,
		    vm_offset_t physical, size_t length);

#ifdef EKCD
static struct kerneldumpcrypto *
kerneldumpcrypto_create(size_t blocksize, uint8_t encryption,
    const uint8_t *key, uint32_t encryptedkeysize, const uint8_t *encryptedkey)
{
	struct kerneldumpcrypto *kdc;
	struct kerneldumpkey *kdk;
	uint32_t dumpkeysize;

	dumpkeysize = roundup2(sizeof(*kdk) + encryptedkeysize, blocksize);
	kdc = malloc(sizeof(*kdc) + dumpkeysize, M_EKCD, M_WAITOK | M_ZERO);

	arc4rand(kdc->kdc_iv, sizeof(kdc->kdc_iv), 0);

	kdc->kdc_encryption = encryption;
	switch (kdc->kdc_encryption) {
	case KERNELDUMP_ENC_AES_256_CBC:
		if (rijndael_makeKey(&kdc->kdc_ki, DIR_ENCRYPT, 256, key) <= 0)
			goto failed;
		break;
	default:
		goto failed;
	}

	kdc->kdc_dumpkeysize = dumpkeysize;
	kdk = kdc->kdc_dumpkey;
	kdk->kdk_encryption = kdc->kdc_encryption;
	memcpy(kdk->kdk_iv, kdc->kdc_iv, sizeof(kdk->kdk_iv));
	kdk->kdk_encryptedkeysize = htod32(encryptedkeysize);
	memcpy(kdk->kdk_encryptedkey, encryptedkey, encryptedkeysize);

	return (kdc);
failed:
	explicit_bzero(kdc, sizeof(*kdc) + dumpkeysize);
	free(kdc, M_EKCD);
	return (NULL);
}

static int
kerneldumpcrypto_init(struct kerneldumpcrypto *kdc)
{
	uint8_t hash[SHA256_DIGEST_LENGTH];
	SHA256_CTX ctx;
	struct kerneldumpkey *kdk;
	int error;

	error = 0;

	if (kdc == NULL)
		return (0);

	/*
	 * When a user enters ddb it can write a crash dump multiple times.
	 * Each time it should be encrypted using a different IV.
	 */
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, kdc->kdc_iv, sizeof(kdc->kdc_iv));
	SHA256_Final(hash, &ctx);
	bcopy(hash, kdc->kdc_iv, sizeof(kdc->kdc_iv));

	switch (kdc->kdc_encryption) {
	case KERNELDUMP_ENC_AES_256_CBC:
		if (rijndael_cipherInit(&kdc->kdc_ci, MODE_CBC,
		    kdc->kdc_iv) <= 0) {
			error = EINVAL;
			goto out;
		}
		break;
	default:
		error = EINVAL;
		goto out;
	}

	kdk = kdc->kdc_dumpkey;
	memcpy(kdk->kdk_iv, kdc->kdc_iv, sizeof(kdk->kdk_iv));
out:
	explicit_bzero(hash, sizeof(hash));
	return (error);
}

static uint32_t
kerneldumpcrypto_dumpkeysize(const struct kerneldumpcrypto *kdc)
{

	if (kdc == NULL)
		return (0);
	return (kdc->kdc_dumpkeysize);
}
#endif /* EKCD */

static struct kerneldumpcomp *
kerneldumpcomp_create(struct dumperinfo *di, uint8_t compression)
{
	struct kerneldumpcomp *kdcomp;
	int format;

	switch (compression) {
	case KERNELDUMP_COMP_GZIP:
		format = COMPRESS_GZIP;
		break;
	case KERNELDUMP_COMP_ZSTD:
		format = COMPRESS_ZSTD;
		break;
	default:
		return (NULL);
	}

	kdcomp = malloc(sizeof(*kdcomp), M_DUMPER, M_WAITOK | M_ZERO);
	kdcomp->kdc_format = compression;
	kdcomp->kdc_stream = compressor_init(kerneldumpcomp_write_cb,
	    format, di->maxiosize, kerneldump_gzlevel, di);
	if (kdcomp->kdc_stream == NULL) {
		free(kdcomp, M_DUMPER);
		return (NULL);
	}
	kdcomp->kdc_buf = malloc(di->maxiosize, M_DUMPER, M_WAITOK | M_NODUMP);
	return (kdcomp);
}

static void
kerneldumpcomp_destroy(struct dumperinfo *di)
{
	struct kerneldumpcomp *kdcomp;

	kdcomp = di->kdcomp;
	if (kdcomp == NULL)
		return;
	compressor_fini(kdcomp->kdc_stream);
	explicit_bzero(kdcomp->kdc_buf, di->maxiosize);
	free(kdcomp->kdc_buf, M_DUMPER);
	free(kdcomp, M_DUMPER);
}

/* Registration of dumpers */
int
set_dumper(struct dumperinfo *di, const char *devname, struct thread *td,
    uint8_t compression, uint8_t encryption, const uint8_t *key,
    uint32_t encryptedkeysize, const uint8_t *encryptedkey)
{
	size_t wantcopy;
	int error;

	error = priv_check(td, PRIV_SETDUMPER);
	if (error != 0)
		return (error);

	if (dumper.dumper != NULL)
		return (EBUSY);
	dumper = *di;
	dumper.blockbuf = NULL;
	dumper.kdcrypto = NULL;
	dumper.kdcomp = NULL;

	if (encryption != KERNELDUMP_ENC_NONE) {
#ifdef EKCD
		dumper.kdcrypto = kerneldumpcrypto_create(di->blocksize,
		    encryption, key, encryptedkeysize, encryptedkey);
		if (dumper.kdcrypto == NULL) {
			error = EINVAL;
			goto cleanup;
		}
#else
		error = EOPNOTSUPP;
		goto cleanup;
#endif
	}

	wantcopy = strlcpy(dumpdevname, devname, sizeof(dumpdevname));
	if (wantcopy >= sizeof(dumpdevname)) {
		printf("set_dumper: device name truncated from '%s' -> '%s'\n",
		    devname, dumpdevname);
	}

	if (compression != KERNELDUMP_COMP_NONE) {
		/*
		 * We currently can't support simultaneous encryption and
		 * compression.
		 */
		if (encryption != KERNELDUMP_ENC_NONE) {
			error = EOPNOTSUPP;
			goto cleanup;
		}
		dumper.kdcomp = kerneldumpcomp_create(&dumper, compression);
		if (dumper.kdcomp == NULL) {
			error = EINVAL;
			goto cleanup;
		}
	}

	dumper.blockbuf = malloc(di->blocksize, M_DUMPER, M_WAITOK | M_ZERO);
	return (0);

cleanup:
	(void)clear_dumper(td);
	return (error);
}

int
clear_dumper(struct thread *td)
{
	int error;

	error = priv_check(td, PRIV_SETDUMPER);
	if (error != 0)
		return (error);

#ifdef NETDUMP
	netdump_mbuf_drain();
#endif

#ifdef EKCD
	if (dumper.kdcrypto != NULL) {
		explicit_bzero(dumper.kdcrypto, sizeof(*dumper.kdcrypto) +
		    dumper.kdcrypto->kdc_dumpkeysize);
		free(dumper.kdcrypto, M_EKCD);
	}
#endif

	kerneldumpcomp_destroy(&dumper);

	if (dumper.blockbuf != NULL) {
		explicit_bzero(dumper.blockbuf, dumper.blocksize);
		free(dumper.blockbuf, M_DUMPER);
	}
	explicit_bzero(&dumper, sizeof(dumper));
	dumpdevname[0] = '\0';
	return (0);
}

static int
dump_check_bounds(struct dumperinfo *di, off_t offset, size_t length)
{

	if (di->mediasize > 0 && length != 0 && (offset < di->mediaoffset ||
	    offset - di->mediaoffset + length > di->mediasize)) {
		if (di->kdcomp != NULL && offset >= di->mediaoffset) {
			printf(
		    "Compressed dump failed to fit in device boundaries.\n");
			return (E2BIG);
		}

		printf("Attempt to write outside dump device boundaries.\n"
	    "offset(%jd), mediaoffset(%jd), length(%ju), mediasize(%jd).\n",
		    (intmax_t)offset, (intmax_t)di->mediaoffset,
		    (uintmax_t)length, (intmax_t)di->mediasize);
		return (ENOSPC);
	}
	if (length % di->blocksize != 0) {
		printf("Attempt to write partial block of length %ju.\n",
		    (uintmax_t)length);
		return (EINVAL);
	}
	if (offset % di->blocksize != 0) {
		printf("Attempt to write at unaligned offset %jd.\n",
		    (intmax_t)offset);
		return (EINVAL);
	}

	return (0);
}

#ifdef EKCD
static int
dump_encrypt(struct kerneldumpcrypto *kdc, uint8_t *buf, size_t size)
{

	switch (kdc->kdc_encryption) {
	case KERNELDUMP_ENC_AES_256_CBC:
		if (rijndael_blockEncrypt(&kdc->kdc_ci, &kdc->kdc_ki, buf,
		    8 * size, buf) <= 0) {
			return (EIO);
		}
		if (rijndael_cipherInit(&kdc->kdc_ci, MODE_CBC,
		    buf + size - 16 /* IV size for AES-256-CBC */) <= 0) {
			return (EIO);
		}
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

/* Encrypt data and call dumper. */
static int
dump_encrypted_write(struct dumperinfo *di, void *virtual,
    vm_offset_t physical, off_t offset, size_t length)
{
	static uint8_t buf[KERNELDUMP_BUFFER_SIZE];
	struct kerneldumpcrypto *kdc;
	int error;
	size_t nbytes;

	kdc = di->kdcrypto;

	while (length > 0) {
		nbytes = MIN(length, sizeof(buf));
		bcopy(virtual, buf, nbytes);

		if (dump_encrypt(kdc, buf, nbytes) != 0)
			return (EIO);

		error = dump_write(di, buf, physical, offset, nbytes);
		if (error != 0)
			return (error);

		offset += nbytes;
		virtual = (void *)((uint8_t *)virtual + nbytes);
		length -= nbytes;
	}

	return (0);
}
#endif /* EKCD */

static int
kerneldumpcomp_write_cb(void *base, size_t length, off_t offset, void *arg)
{
	struct dumperinfo *di;
	size_t resid, rlength;
	int error;

	di = arg;

	if (length % di->blocksize != 0) {
		/*
		 * This must be the final write after flushing the compression
		 * stream. Write as many full blocks as possible and stash the
		 * residual data in the dumper's block buffer. It will be
		 * padded and written in dump_finish().
		 */
		rlength = rounddown(length, di->blocksize);
		if (rlength != 0) {
			error = _dump_append(di, base, 0, rlength);
			if (error != 0)
				return (error);
		}
		resid = length - rlength;
		memmove(di->blockbuf, (uint8_t *)base + rlength, resid);
		di->kdcomp->kdc_resid = resid;
		return (EAGAIN);
	}
	return (_dump_append(di, base, 0, length));
}

/*
 * Write kernel dump headers at the beginning and end of the dump extent.
 * Write the kernel dump encryption key after the leading header if we were
 * configured to do so.
 */
static int
dump_write_headers(struct dumperinfo *di, struct kerneldumpheader *kdh)
{
#ifdef EKCD
	struct kerneldumpcrypto *kdc;
#endif
	void *buf, *key;
	size_t hdrsz;
	uint64_t extent;
	uint32_t keysize;
	int error;

	hdrsz = sizeof(*kdh);
	if (hdrsz > di->blocksize)
		return (ENOMEM);

#ifdef EKCD
	kdc = di->kdcrypto;
	key = kdc->kdc_dumpkey;
	keysize = kerneldumpcrypto_dumpkeysize(kdc);
#else
	key = NULL;
	keysize = 0;
#endif

	/*
	 * If the dump device has special handling for headers, let it take care
	 * of writing them out.
	 */
	if (di->dumper_hdr != NULL)
		return (di->dumper_hdr(di, kdh, key, keysize));

	if (hdrsz == di->blocksize)
		buf = kdh;
	else {
		buf = di->blockbuf;
		memset(buf, 0, di->blocksize);
		memcpy(buf, kdh, hdrsz);
	}

	extent = dtoh64(kdh->dumpextent);
#ifdef EKCD
	if (kdc != NULL) {
		error = dump_write(di, kdc->kdc_dumpkey, 0,
		    di->mediaoffset + di->mediasize - di->blocksize - extent -
		    keysize, keysize);
		if (error != 0)
			return (error);
	}
#endif

	error = dump_write(di, buf, 0,
	    di->mediaoffset + di->mediasize - 2 * di->blocksize - extent -
	    keysize, di->blocksize);
	if (error == 0)
		error = dump_write(di, buf, 0, di->mediaoffset + di->mediasize -
		    di->blocksize, di->blocksize);
	return (error);
}

/*
 * Don't touch the first SIZEOF_METADATA bytes on the dump device.  This is to
 * protect us from metadata and metadata from us.
 */
#define	SIZEOF_METADATA		(64 * 1024)

/*
 * Do some preliminary setup for a kernel dump: initialize state for encryption,
 * if requested, and make sure that we have enough space on the dump device.
 *
 * We set things up so that the dump ends before the last sector of the dump
 * device, at which the trailing header is written.
 *
 *     +-----------+------+-----+----------------------------+------+
 *     |           | lhdr | key |    ... kernel dump ...     | thdr |
 *     +-----------+------+-----+----------------------------+------+
 *                   1 blk  opt <------- dump extent --------> 1 blk
 *
 * Dumps written using dump_append() start at the beginning of the extent.
 * Uncompressed dumps will use the entire extent, but compressed dumps typically
 * will not. The true length of the dump is recorded in the leading and trailing
 * headers once the dump has been completed.
 *
 * The dump device may provide a callback, in which case it will initialize
 * dumpoff and take care of laying out the headers.
 */
int
dump_start(struct dumperinfo *di, struct kerneldumpheader *kdh)
{
	uint64_t dumpextent, span;
	uint32_t keysize;
	int error;

#ifdef EKCD
	error = kerneldumpcrypto_init(di->kdcrypto);
	if (error != 0)
		return (error);
	keysize = kerneldumpcrypto_dumpkeysize(di->kdcrypto);
#else
	error = 0;
	keysize = 0;
#endif

	if (di->dumper_start != NULL) {
		error = di->dumper_start(di);
	} else {
		dumpextent = dtoh64(kdh->dumpextent);
		span = SIZEOF_METADATA + dumpextent + 2 * di->blocksize +
		    keysize;
		if (di->mediasize < span) {
			if (di->kdcomp == NULL)
				return (E2BIG);

			/*
			 * We don't yet know how much space the compressed dump
			 * will occupy, so try to use the whole swap partition
			 * (minus the first 64KB) in the hope that the
			 * compressed dump will fit. If that doesn't turn out to
			 * be enough, the bounds checking in dump_write()
			 * will catch us and cause the dump to fail.
			 */
			dumpextent = di->mediasize - span + dumpextent;
			kdh->dumpextent = htod64(dumpextent);
		}

		/*
		 * The offset at which to begin writing the dump.
		 */
		di->dumpoff = di->mediaoffset + di->mediasize - di->blocksize -
		    dumpextent;
	}
	di->origdumpoff = di->dumpoff;
	return (error);
}

static int
_dump_append(struct dumperinfo *di, void *virtual, vm_offset_t physical,
    size_t length)
{
	int error;

#ifdef EKCD
	if (di->kdcrypto != NULL)
		error = dump_encrypted_write(di, virtual, physical, di->dumpoff,
		    length);
	else
#endif
		error = dump_write(di, virtual, physical, di->dumpoff, length);
	if (error == 0)
		di->dumpoff += length;
	return (error);
}

/*
 * Write to the dump device starting at dumpoff. When compression is enabled,
 * writes to the device will be performed using a callback that gets invoked
 * when the compression stream's output buffer is full.
 */
int
dump_append(struct dumperinfo *di, void *virtual, vm_offset_t physical,
    size_t length)
{
	void *buf;

	if (di->kdcomp != NULL) {
		/* Bounce through a buffer to avoid CRC errors. */
		if (length > di->maxiosize)
			return (EINVAL);
		buf = di->kdcomp->kdc_buf;
		memmove(buf, virtual, length);
		return (compressor_write(di->kdcomp->kdc_stream, buf, length));
	}
	return (_dump_append(di, virtual, physical, length));
}

/*
 * Write to the dump device at the specified offset.
 */
int
dump_write(struct dumperinfo *di, void *virtual, vm_offset_t physical,
    off_t offset, size_t length)
{
	int error;

	error = dump_check_bounds(di, offset, length);
	if (error != 0)
		return (error);
	return (di->dumper(di->priv, virtual, physical, offset, length));
}

/*
 * Perform kernel dump finalization: flush the compression stream, if necessary,
 * write the leading and trailing kernel dump headers now that we know the true
 * length of the dump, and optionally write the encryption key following the
 * leading header.
 */
int
dump_finish(struct dumperinfo *di, struct kerneldumpheader *kdh)
{
	int error;

	if (di->kdcomp != NULL) {
		error = compressor_flush(di->kdcomp->kdc_stream);
		if (error == EAGAIN) {
			/* We have residual data in di->blockbuf. */
			error = dump_write(di, di->blockbuf, 0, di->dumpoff,
			    di->blocksize);
			di->dumpoff += di->kdcomp->kdc_resid;
			di->kdcomp->kdc_resid = 0;
		}
		if (error != 0)
			return (error);

		/*
		 * We now know the size of the compressed dump, so update the
		 * header accordingly and recompute parity.
		 */
		kdh->dumplength = htod64(di->dumpoff - di->origdumpoff);
		kdh->parity = 0;
		kdh->parity = kerneldump_parity(kdh);

		compressor_reset(di->kdcomp->kdc_stream);
	}

	error = dump_write_headers(di, kdh);
	if (error != 0)
		return (error);

	(void)dump_write(di, NULL, 0, 0, 0);
	return (0);
}

void
dump_init_header(const struct dumperinfo *di, struct kerneldumpheader *kdh,
    char *magic, uint32_t archver, uint64_t dumplen)
{
	size_t dstsize;

	bzero(kdh, sizeof(*kdh));
	strlcpy(kdh->magic, magic, sizeof(kdh->magic));
	strlcpy(kdh->architecture, MACHINE_ARCH, sizeof(kdh->architecture));
	kdh->version = htod32(KERNELDUMPVERSION);
	kdh->architectureversion = htod32(archver);
	kdh->dumplength = htod64(dumplen);
	kdh->dumpextent = kdh->dumplength;
	kdh->dumptime = htod64(time_second);
#ifdef EKCD
	kdh->dumpkeysize = htod32(kerneldumpcrypto_dumpkeysize(di->kdcrypto));
#else
	kdh->dumpkeysize = 0;
#endif
	kdh->blocksize = htod32(di->blocksize);
	strlcpy(kdh->hostname, prison0.pr_hostname, sizeof(kdh->hostname));
	dstsize = sizeof(kdh->versionstring);
	if (strlcpy(kdh->versionstring, version, dstsize) >= dstsize)
		kdh->versionstring[dstsize - 2] = '\n';
	if (panicstr != NULL)
		strlcpy(kdh->panicstring, panicstr, sizeof(kdh->panicstring));
	if (di->kdcomp != NULL)
		kdh->compression = di->kdcomp->kdc_format;
	kdh->parity = kerneldump_parity(kdh);
}

#ifdef DDB
DB_SHOW_COMMAND(panic, db_show_panic)
{

	if (panicstr == NULL)
		db_printf("panicstr not set\n");
	else
		db_printf("panic: %s\n", panicstr);
}
#endif
