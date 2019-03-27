/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * Copyright (c) 2005 Robert N. M. Watson
 * All rights reserved.
 *
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
 * Copyright (c) 1994 Christopher G. Demetriou
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
 *	@(#)kern_acct.c	8.1 (Berkeley) 6/14/93
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/acct.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/syslog.h>
#include <sys/sysproto.h>
#include <sys/tty.h>
#include <sys/vnode.h>

#include <security/mac/mac_framework.h>

_Static_assert(sizeof(struct acctv3) - offsetof(struct acctv3, ac_trailer) ==
    sizeof(struct acctv2) - offsetof(struct acctv2, ac_trailer), "trailer");
_Static_assert(sizeof(struct acctv3) - offsetof(struct acctv3, ac_len2) ==
    sizeof(struct acctv2) - offsetof(struct acctv2, ac_len2), "len2");

/*
 * The routines implemented in this file are described in:
 *      Leffler, et al.: The Design and Implementation of the 4.3BSD
 *	    UNIX Operating System (Addison Welley, 1989)
 * on pages 62-63.
 * On May 2007 the historic 3 bits base 8 exponent, 13 bit fraction
 * compt_t representation described in the above reference was replaced
 * with that of IEEE-754 floats.
 *
 * Arguably, to simplify accounting operations, this mechanism should
 * be replaced by one in which an accounting log file (similar to /dev/klog)
 * is read by a user process, etc.  However, that has its own problems.
 */

/* Floating point definitions from <float.h>. */
#define FLT_MANT_DIG    24              /* p */
#define FLT_MAX_EXP     128             /* emax */

/*
 * Internal accounting functions.
 * The former's operation is described in Leffler, et al., and the latter
 * was provided by UCB with the 4.4BSD-Lite release
 */
static uint32_t	encode_timeval(struct timeval);
static uint32_t	encode_long(long);
static void	acctwatch(void);
static void	acct_thread(void *);
static int	acct_disable(struct thread *, int);

/*
 * Accounting vnode pointer, saved vnode pointer, and flags for each.
 * acct_sx protects against changes to the active vnode and credentials
 * while accounting records are being committed to disk.
 */
static int		 acct_configured;
static int		 acct_suspended;
static struct vnode	*acct_vp;
static struct ucred	*acct_cred;
static struct plimit	*acct_limit;
static int		 acct_flags;
static struct sx	 acct_sx;

SX_SYSINIT(acct, &acct_sx, "acct_sx");

/*
 * State of the accounting kthread.
 */
static int		 acct_state;

#define	ACCT_RUNNING	1	/* Accounting kthread is running. */
#define	ACCT_EXITREQ	2	/* Accounting kthread should exit. */

/*
 * Values associated with enabling and disabling accounting
 */
static int acctsuspend = 2;	/* stop accounting when < 2% free space left */
SYSCTL_INT(_kern, OID_AUTO, acct_suspend, CTLFLAG_RW,
	&acctsuspend, 0, "percentage of free disk space below which accounting stops");

static int acctresume = 4;	/* resume when free space risen to > 4% */
SYSCTL_INT(_kern, OID_AUTO, acct_resume, CTLFLAG_RW,
	&acctresume, 0, "percentage of free disk space above which accounting resumes");

static int acctchkfreq = 15;	/* frequency (in seconds) to check space */

static int
sysctl_acct_chkfreq(SYSCTL_HANDLER_ARGS)
{
	int error, value;

	/* Write out the old value. */
	error = SYSCTL_OUT(req, &acctchkfreq, sizeof(int));
	if (error || req->newptr == NULL)
		return (error);

	/* Read in and verify the new value. */
	error = SYSCTL_IN(req, &value, sizeof(int));
	if (error)
		return (error);
	if (value <= 0)
		return (EINVAL);
	acctchkfreq = value;
	return (0);
}
SYSCTL_PROC(_kern, OID_AUTO, acct_chkfreq, CTLTYPE_INT|CTLFLAG_RW,
    &acctchkfreq, 0, sysctl_acct_chkfreq, "I",
    "frequency for checking the free space");

SYSCTL_INT(_kern, OID_AUTO, acct_configured, CTLFLAG_RD, &acct_configured, 0,
	"Accounting configured or not");

SYSCTL_INT(_kern, OID_AUTO, acct_suspended, CTLFLAG_RD, &acct_suspended, 0,
	"Accounting suspended or not");

/*
 * Accounting system call.  Written based on the specification and previous
 * implementation done by Mark Tinguely.
 */
int
sys_acct(struct thread *td, struct acct_args *uap)
{
	struct nameidata nd;
	int error, flags, i, replacing;

	error = priv_check(td, PRIV_ACCT);
	if (error)
		return (error);

	/*
	 * If accounting is to be started to a file, open that file for
	 * appending and make sure it's a 'normal'.
	 */
	if (uap->path != NULL) {
		NDINIT(&nd, LOOKUP, NOFOLLOW | AUDITVNODE1,
		    UIO_USERSPACE, uap->path, td);
		flags = FWRITE | O_APPEND;
		error = vn_open(&nd, &flags, 0, NULL);
		if (error)
			return (error);
		NDFREE(&nd, NDF_ONLY_PNBUF);
#ifdef MAC
		error = mac_system_check_acct(td->td_ucred, nd.ni_vp);
		if (error) {
			VOP_UNLOCK(nd.ni_vp, 0);
			vn_close(nd.ni_vp, flags, td->td_ucred, td);
			return (error);
		}
#endif
		VOP_UNLOCK(nd.ni_vp, 0);
		if (nd.ni_vp->v_type != VREG) {
			vn_close(nd.ni_vp, flags, td->td_ucred, td);
			return (EACCES);
		}
#ifdef MAC
	} else {
		error = mac_system_check_acct(td->td_ucred, NULL);
		if (error)
			return (error);
#endif
	}

	/*
	 * Disallow concurrent access to the accounting vnode while we swap
	 * it out, in order to prevent access after close.
	 */
	sx_xlock(&acct_sx);

	/*
	 * Don't log spurious disable/enable messages if we are
	 * switching from one accounting file to another due to log
	 * rotation.
	 */
	replacing = (acct_vp != NULL && uap->path != NULL);

	/*
	 * If accounting was previously enabled, kill the old space-watcher,
	 * close the file, and (if no new file was specified, leave).  Reset
	 * the suspended state regardless of whether accounting remains
	 * enabled.
	 */
	acct_suspended = 0;
	if (acct_vp != NULL)
		error = acct_disable(td, !replacing);
	if (uap->path == NULL) {
		if (acct_state & ACCT_RUNNING) {
			acct_state |= ACCT_EXITREQ;
			wakeup(&acct_state);
		}
		sx_xunlock(&acct_sx);
		return (error);
	}

	/*
	 * Create our own plimit object without limits. It will be assigned
	 * to exiting processes.
	 */
	acct_limit = lim_alloc();
	for (i = 0; i < RLIM_NLIMITS; i++)
		acct_limit->pl_rlimit[i].rlim_cur =
		    acct_limit->pl_rlimit[i].rlim_max = RLIM_INFINITY;

	/*
	 * Save the new accounting file vnode, and schedule the new
	 * free space watcher.
	 */
	acct_vp = nd.ni_vp;
	acct_cred = crhold(td->td_ucred);
	acct_flags = flags;
	if (acct_state & ACCT_RUNNING)
		acct_state &= ~ACCT_EXITREQ;
	else {
		/*
		 * Try to start up an accounting kthread.  We may start more
		 * than one, but if so the extras will commit suicide as
		 * soon as they start up.
		 */
		error = kproc_create(acct_thread, NULL, NULL, 0, 0,
		    "accounting");
		if (error) {
			(void) acct_disable(td, 0);
			sx_xunlock(&acct_sx);
			log(LOG_NOTICE, "Unable to start accounting thread\n");
			return (error);
		}
	}
	acct_configured = 1;
	sx_xunlock(&acct_sx);
	if (!replacing)
		log(LOG_NOTICE, "Accounting enabled\n");
	return (error);
}

/*
 * Disable currently in-progress accounting by closing the vnode, dropping
 * our reference to the credential, and clearing the vnode's flags.
 */
static int
acct_disable(struct thread *td, int logging)
{
	int error;

	sx_assert(&acct_sx, SX_XLOCKED);
	error = vn_close(acct_vp, acct_flags, acct_cred, td);
	crfree(acct_cred);
	lim_free(acct_limit);
	acct_configured = 0;
	acct_vp = NULL;
	acct_cred = NULL;
	acct_flags = 0;
	if (logging)
		log(LOG_NOTICE, "Accounting disabled\n");
	return (error);
}

/*
 * Write out process accounting information, on process exit.
 * Data to be written out is specified in Leffler, et al.
 * and are enumerated below.  (They're also noted in the system
 * "acct.h" header file.)
 */
int
acct_process(struct thread *td)
{
	struct acctv3 acct;
	struct timeval ut, st, tmp;
	struct plimit *oldlim;
	struct proc *p;
	struct rusage ru;
	int t, ret;

	/*
	 * Lockless check of accounting condition before doing the hard
	 * work.
	 */
	if (acct_vp == NULL || acct_suspended)
		return (0);

	sx_slock(&acct_sx);

	/*
	 * If accounting isn't enabled, don't bother.  Have to check again
	 * once we own the lock in case we raced with disabling of accounting
	 * by another thread.
	 */
	if (acct_vp == NULL || acct_suspended) {
		sx_sunlock(&acct_sx);
		return (0);
	}

	p = td->td_proc;

	/*
	 * Get process accounting information.
	 */

	sx_slock(&proctree_lock);
	PROC_LOCK(p);

	/* (1) The terminal from which the process was started */
	if ((p->p_flag & P_CONTROLT) && p->p_pgrp->pg_session->s_ttyp)
		acct.ac_tty = tty_udev(p->p_pgrp->pg_session->s_ttyp);
	else
		acct.ac_tty = NODEV;
	sx_sunlock(&proctree_lock);

	/* (2) The name of the command that ran */
	bcopy(p->p_comm, acct.ac_comm, sizeof acct.ac_comm);

	/* (3) The amount of user and system time that was used */
	rufetchcalc(p, &ru, &ut, &st);
	acct.ac_utime = encode_timeval(ut);
	acct.ac_stime = encode_timeval(st);

	/* (4) The elapsed time the command ran (and its starting time) */
	getboottime(&tmp);
	timevaladd(&tmp, &p->p_stats->p_start);
	acct.ac_btime = tmp.tv_sec;
	microuptime(&tmp);
	timevalsub(&tmp, &p->p_stats->p_start);
	acct.ac_etime = encode_timeval(tmp);

	/* (5) The average amount of memory used */
	tmp = ut;
	timevaladd(&tmp, &st);
	/* Convert tmp (i.e. u + s) into hz units to match ru_i*. */
	t = tmp.tv_sec * hz + tmp.tv_usec / tick;
	if (t)
		acct.ac_mem = encode_long((ru.ru_ixrss + ru.ru_idrss +
		    + ru.ru_isrss) / t);
	else
		acct.ac_mem = 0;

	/* (6) The number of disk I/O operations done */
	acct.ac_io = encode_long(ru.ru_inblock + ru.ru_oublock);

	/* (7) The UID and GID of the process */
	acct.ac_uid = p->p_ucred->cr_ruid;
	acct.ac_gid = p->p_ucred->cr_rgid;

	/* (8) The boolean flags that tell how the process terminated, etc. */
	acct.ac_flagx = p->p_acflag;

	/* Setup ancillary structure fields. */
	acct.ac_flagx |= ANVER;
	acct.ac_zero = 0;
	acct.ac_version = 3;
	acct.ac_len = acct.ac_len2 = sizeof(acct);

	/*
	 * Eliminate rlimits (file size limit in particular).
	 */
	oldlim = p->p_limit;
	p->p_limit = lim_hold(acct_limit);
	PROC_UNLOCK(p);
	lim_free(oldlim);

	/*
	 * Write the accounting information to the file.
	 */
	ret = vn_rdwr(UIO_WRITE, acct_vp, (caddr_t)&acct, sizeof (acct),
	    (off_t)0, UIO_SYSSPACE, IO_APPEND|IO_UNIT, acct_cred, NOCRED,
	    NULL, td);
	sx_sunlock(&acct_sx);
	return (ret);
}

/* FLOAT_CONVERSION_START (Regression testing; don't remove this line.) */

/* Convert timevals and longs into IEEE-754 bit patterns. */

/* Mantissa mask (MSB is implied, so subtract 1). */
#define MANT_MASK ((1 << (FLT_MANT_DIG - 1)) - 1)

/*
 * We calculate integer values to a precision of approximately
 * 28 bits.
 * This is high-enough precision to fill the 24 float bits
 * and low-enough to avoid overflowing the 32 int bits.
 */
#define CALC_BITS 28

/* log_2(1000000). */
#define LOG2_1M 20

/*
 * Convert the elements of a timeval into a 32-bit word holding
 * the bits of a IEEE-754 float.
 * The float value represents the timeval's value in microsecond units.
 */
static uint32_t
encode_timeval(struct timeval tv)
{
	int log2_s;
	int val, exp;	/* Unnormalized value and exponent */
	int norm_exp;	/* Normalized exponent */
	int shift;

	/*
	 * First calculate value and exponent to about CALC_BITS precision.
	 * Note that the following conditionals have been ordered so that
	 * the most common cases appear first.
	 */
	if (tv.tv_sec == 0) {
		if (tv.tv_usec == 0)
			return (0);
		exp = 0;
		val = tv.tv_usec;
	} else {
		/*
		 * Calculate the value to a precision of approximately
		 * CALC_BITS.
		 */
		log2_s = fls(tv.tv_sec) - 1;
		if (log2_s + LOG2_1M < CALC_BITS) {
			exp = 0;
			val = 1000000 * tv.tv_sec + tv.tv_usec;
		} else {
			exp = log2_s + LOG2_1M - CALC_BITS;
			val = (unsigned int)(((uint64_t)1000000 * tv.tv_sec +
			    tv.tv_usec) >> exp);
		}
	}
	/* Now normalize and pack the value into an IEEE-754 float. */
	norm_exp = fls(val) - 1;
	shift = FLT_MANT_DIG - norm_exp - 1;
#ifdef ACCT_DEBUG
	printf("val=%d exp=%d shift=%d log2(val)=%d\n",
	    val, exp, shift, norm_exp);
	printf("exp=%x mant=%x\n", FLT_MAX_EXP - 1 + exp + norm_exp,
	    ((shift > 0 ? (val << shift) : (val >> -shift)) & MANT_MASK));
#endif
	return (((FLT_MAX_EXP - 1 + exp + norm_exp) << (FLT_MANT_DIG - 1)) |
	    ((shift > 0 ? val << shift : val >> -shift) & MANT_MASK));
}

/*
 * Convert a non-negative long value into the bit pattern of
 * an IEEE-754 float value.
 */
static uint32_t
encode_long(long val)
{
	int norm_exp;	/* Normalized exponent */
	int shift;

	if (val == 0)
		return (0);
	if (val < 0) {
		log(LOG_NOTICE,
		    "encode_long: negative value %ld in accounting record\n",
		    val);
		val = LONG_MAX;
	}
	norm_exp = fls(val) - 1;
	shift = FLT_MANT_DIG - norm_exp - 1;
#ifdef ACCT_DEBUG
	printf("val=%d shift=%d log2(val)=%d\n",
	    val, shift, norm_exp);
	printf("exp=%x mant=%x\n", FLT_MAX_EXP - 1 + exp + norm_exp,
	    ((shift > 0 ? (val << shift) : (val >> -shift)) & MANT_MASK));
#endif
	return (((FLT_MAX_EXP - 1 + norm_exp) << (FLT_MANT_DIG - 1)) |
	    ((shift > 0 ? val << shift : val >> -shift) & MANT_MASK));
}

/* FLOAT_CONVERSION_END (Regression testing; don't remove this line.) */

/*
 * Periodically check the filesystem to see if accounting
 * should be turned on or off.  Beware the case where the vnode
 * has been vgone()'d out from underneath us, e.g. when the file
 * system containing the accounting file has been forcibly unmounted.
 */
/* ARGSUSED */
static void
acctwatch(void)
{
	struct statfs *sp;

	sx_assert(&acct_sx, SX_XLOCKED);

	/*
	 * If accounting was disabled before our kthread was scheduled,
	 * then acct_vp might be NULL.  If so, just ask our kthread to
	 * exit and return.
	 */
	if (acct_vp == NULL) {
		acct_state |= ACCT_EXITREQ;
		return;
	}

	/*
	 * If our vnode is no longer valid, tear it down and signal the
	 * accounting thread to die.
	 */
	if (acct_vp->v_type == VBAD) {
		(void) acct_disable(NULL, 1);
		acct_state |= ACCT_EXITREQ;
		return;
	}

	/*
	 * Stopping here is better than continuing, maybe it will be VBAD
	 * next time around.
	 */
	sp = malloc(sizeof(struct statfs), M_STATFS, M_WAITOK);
	if (VFS_STATFS(acct_vp->v_mount, sp) < 0) {
		free(sp, M_STATFS);
		return;
	}
	if (acct_suspended) {
		if (sp->f_bavail > (int64_t)(acctresume * sp->f_blocks /
		    100)) {
			acct_suspended = 0;
			log(LOG_NOTICE, "Accounting resumed\n");
		}
	} else {
		if (sp->f_bavail <= (int64_t)(acctsuspend * sp->f_blocks /
		    100)) {
			acct_suspended = 1;
			log(LOG_NOTICE, "Accounting suspended\n");
		}
	}
	free(sp, M_STATFS);
}

/*
 * The main loop for the dedicated kernel thread that periodically calls
 * acctwatch().
 */
static void
acct_thread(void *dummy)
{
	u_char pri;

	/* This is a low-priority kernel thread. */
	pri = PRI_MAX_KERN;
	thread_lock(curthread);
	sched_prio(curthread, pri);
	thread_unlock(curthread);

	/* If another accounting kthread is already running, just die. */
	sx_xlock(&acct_sx);
	if (acct_state & ACCT_RUNNING) {
		sx_xunlock(&acct_sx);
		kproc_exit(0);
	}
	acct_state |= ACCT_RUNNING;

	/* Loop until we are asked to exit. */
	while (!(acct_state & ACCT_EXITREQ)) {

		/* Perform our periodic checks. */
		acctwatch();

		/*
		 * We check this flag again before sleeping since the
		 * acctwatch() might have shut down accounting and asked us
		 * to exit.
		 */
		if (!(acct_state & ACCT_EXITREQ)) {
			sx_sleep(&acct_state, &acct_sx, 0, "-",
			    acctchkfreq * hz);
		}
	}

	/*
	 * Acknowledge the exit request and shutdown.  We clear both the
	 * exit request and running flags.
	 */
	acct_state = 0;
	sx_xunlock(&acct_sx);
	kproc_exit(0);
}
