/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 1994-1995 Søren Schmidt
 * All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>

#include <security/audit/audit.h>

#include "opt_compat.h"

#ifdef COMPAT_LINUX32
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#endif
#include <compat/linux/linux_signal.h>
#include <compat/linux/linux_util.h>
#include <compat/linux/linux_emul.h>
#include <compat/linux/linux_misc.h>

static int	linux_do_tkill(struct thread *td, struct thread *tdt,
		    ksiginfo_t *ksi);
static void	sicode_to_lsicode(int si_code, int *lsi_code);


static void
linux_to_bsd_sigaction(l_sigaction_t *lsa, struct sigaction *bsa)
{

	linux_to_bsd_sigset(&lsa->lsa_mask, &bsa->sa_mask);
	bsa->sa_handler = PTRIN(lsa->lsa_handler);
	bsa->sa_flags = 0;
	if (lsa->lsa_flags & LINUX_SA_NOCLDSTOP)
		bsa->sa_flags |= SA_NOCLDSTOP;
	if (lsa->lsa_flags & LINUX_SA_NOCLDWAIT)
		bsa->sa_flags |= SA_NOCLDWAIT;
	if (lsa->lsa_flags & LINUX_SA_SIGINFO)
		bsa->sa_flags |= SA_SIGINFO;
	if (lsa->lsa_flags & LINUX_SA_ONSTACK)
		bsa->sa_flags |= SA_ONSTACK;
	if (lsa->lsa_flags & LINUX_SA_RESTART)
		bsa->sa_flags |= SA_RESTART;
	if (lsa->lsa_flags & LINUX_SA_ONESHOT)
		bsa->sa_flags |= SA_RESETHAND;
	if (lsa->lsa_flags & LINUX_SA_NOMASK)
		bsa->sa_flags |= SA_NODEFER;
}

static void
bsd_to_linux_sigaction(struct sigaction *bsa, l_sigaction_t *lsa)
{

	bsd_to_linux_sigset(&bsa->sa_mask, &lsa->lsa_mask);
#ifdef COMPAT_LINUX32
	lsa->lsa_handler = (uintptr_t)bsa->sa_handler;
#else
	lsa->lsa_handler = bsa->sa_handler;
#endif
	lsa->lsa_restorer = 0;		/* unsupported */
	lsa->lsa_flags = 0;
	if (bsa->sa_flags & SA_NOCLDSTOP)
		lsa->lsa_flags |= LINUX_SA_NOCLDSTOP;
	if (bsa->sa_flags & SA_NOCLDWAIT)
		lsa->lsa_flags |= LINUX_SA_NOCLDWAIT;
	if (bsa->sa_flags & SA_SIGINFO)
		lsa->lsa_flags |= LINUX_SA_SIGINFO;
	if (bsa->sa_flags & SA_ONSTACK)
		lsa->lsa_flags |= LINUX_SA_ONSTACK;
	if (bsa->sa_flags & SA_RESTART)
		lsa->lsa_flags |= LINUX_SA_RESTART;
	if (bsa->sa_flags & SA_RESETHAND)
		lsa->lsa_flags |= LINUX_SA_ONESHOT;
	if (bsa->sa_flags & SA_NODEFER)
		lsa->lsa_flags |= LINUX_SA_NOMASK;
}

int
linux_do_sigaction(struct thread *td, int linux_sig, l_sigaction_t *linux_nsa,
		   l_sigaction_t *linux_osa)
{
	struct sigaction act, oact, *nsa, *osa;
	int error, sig;

	if (!LINUX_SIG_VALID(linux_sig))
		return (EINVAL);

	osa = (linux_osa != NULL) ? &oact : NULL;
	if (linux_nsa != NULL) {
		nsa = &act;
		linux_to_bsd_sigaction(linux_nsa, nsa);
	} else
		nsa = NULL;
	sig = linux_to_bsd_signal(linux_sig);

	error = kern_sigaction(td, sig, nsa, osa, 0);
	if (error)
		return (error);

	if (linux_osa != NULL)
		bsd_to_linux_sigaction(osa, linux_osa);

	return (0);
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
int
linux_signal(struct thread *td, struct linux_signal_args *args)
{
	l_sigaction_t nsa, osa;
	int error;

#ifdef DEBUG
	if (ldebug(signal))
		printf(ARGS(signal, "%d, %p"),
		    args->sig, (void *)(uintptr_t)args->handler);
#endif

	nsa.lsa_handler = args->handler;
	nsa.lsa_flags = LINUX_SA_ONESHOT | LINUX_SA_NOMASK;
	LINUX_SIGEMPTYSET(nsa.lsa_mask);

	error = linux_do_sigaction(td, args->sig, &nsa, &osa);
	td->td_retval[0] = (int)(intptr_t)osa.lsa_handler;

	return (error);
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

int
linux_rt_sigaction(struct thread *td, struct linux_rt_sigaction_args *args)
{
	l_sigaction_t nsa, osa;
	int error;

#ifdef DEBUG
	if (ldebug(rt_sigaction))
		printf(ARGS(rt_sigaction, "%ld, %p, %p, %ld"),
		    (long)args->sig, (void *)args->act,
		    (void *)args->oact, (long)args->sigsetsize);
#endif

	if (args->sigsetsize != sizeof(l_sigset_t))
		return (EINVAL);

	if (args->act != NULL) {
		error = copyin(args->act, &nsa, sizeof(l_sigaction_t));
		if (error)
			return (error);
	}

	error = linux_do_sigaction(td, args->sig,
				   args->act ? &nsa : NULL,
				   args->oact ? &osa : NULL);

	if (args->oact != NULL && !error) {
		error = copyout(&osa, args->oact, sizeof(l_sigaction_t));
	}

	return (error);
}

static int
linux_do_sigprocmask(struct thread *td, int how, l_sigset_t *new,
		     l_sigset_t *old)
{
	sigset_t omask, nmask;
	sigset_t *nmaskp;
	int error;

	td->td_retval[0] = 0;

	switch (how) {
	case LINUX_SIG_BLOCK:
		how = SIG_BLOCK;
		break;
	case LINUX_SIG_UNBLOCK:
		how = SIG_UNBLOCK;
		break;
	case LINUX_SIG_SETMASK:
		how = SIG_SETMASK;
		break;
	default:
		return (EINVAL);
	}
	if (new != NULL) {
		linux_to_bsd_sigset(new, &nmask);
		nmaskp = &nmask;
	} else
		nmaskp = NULL;
	error = kern_sigprocmask(td, how, nmaskp, &omask, 0);
	if (error == 0 && old != NULL)
		bsd_to_linux_sigset(&omask, old);

	return (error);
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
int
linux_sigprocmask(struct thread *td, struct linux_sigprocmask_args *args)
{
	l_osigset_t mask;
	l_sigset_t set, oset;
	int error;

#ifdef DEBUG
	if (ldebug(sigprocmask))
		printf(ARGS(sigprocmask, "%d, *, *"), args->how);
#endif

	if (args->mask != NULL) {
		error = copyin(args->mask, &mask, sizeof(l_osigset_t));
		if (error)
			return (error);
		LINUX_SIGEMPTYSET(set);
		set.__mask = mask;
	}

	error = linux_do_sigprocmask(td, args->how,
				     args->mask ? &set : NULL,
				     args->omask ? &oset : NULL);

	if (args->omask != NULL && !error) {
		mask = oset.__mask;
		error = copyout(&mask, args->omask, sizeof(l_osigset_t));
	}

	return (error);
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

int
linux_rt_sigprocmask(struct thread *td, struct linux_rt_sigprocmask_args *args)
{
	l_sigset_t set, oset;
	int error;

#ifdef DEBUG
	if (ldebug(rt_sigprocmask))
		printf(ARGS(rt_sigprocmask, "%d, %p, %p, %ld"),
		    args->how, (void *)args->mask,
		    (void *)args->omask, (long)args->sigsetsize);
#endif

	if (args->sigsetsize != sizeof(l_sigset_t))
		return (EINVAL);

	if (args->mask != NULL) {
		error = copyin(args->mask, &set, sizeof(l_sigset_t));
		if (error)
			return (error);
	}

	error = linux_do_sigprocmask(td, args->how,
				     args->mask ? &set : NULL,
				     args->omask ? &oset : NULL);

	if (args->omask != NULL && !error) {
		error = copyout(&oset, args->omask, sizeof(l_sigset_t));
	}

	return (error);
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
int
linux_sgetmask(struct thread *td, struct linux_sgetmask_args *args)
{
	struct proc *p = td->td_proc;
	l_sigset_t mask;

#ifdef DEBUG
	if (ldebug(sgetmask))
		printf(ARGS(sgetmask, ""));
#endif

	PROC_LOCK(p);
	bsd_to_linux_sigset(&td->td_sigmask, &mask);
	PROC_UNLOCK(p);
	td->td_retval[0] = mask.__mask;
	return (0);
}

int
linux_ssetmask(struct thread *td, struct linux_ssetmask_args *args)
{
	struct proc *p = td->td_proc;
	l_sigset_t lset;
	sigset_t bset;

#ifdef DEBUG
	if (ldebug(ssetmask))
		printf(ARGS(ssetmask, "%08lx"), (unsigned long)args->mask);
#endif

	PROC_LOCK(p);
	bsd_to_linux_sigset(&td->td_sigmask, &lset);
	td->td_retval[0] = lset.__mask;
	LINUX_SIGEMPTYSET(lset);
	lset.__mask = args->mask;
	linux_to_bsd_sigset(&lset, &bset);
	td->td_sigmask = bset;
	SIG_CANTMASK(td->td_sigmask);
	signotify(td);
	PROC_UNLOCK(p);
	return (0);
}

int
linux_sigpending(struct thread *td, struct linux_sigpending_args *args)
{
	struct proc *p = td->td_proc;
	sigset_t bset;
	l_sigset_t lset;
	l_osigset_t mask;

#ifdef DEBUG
	if (ldebug(sigpending))
		printf(ARGS(sigpending, "*"));
#endif

	PROC_LOCK(p);
	bset = p->p_siglist;
	SIGSETOR(bset, td->td_siglist);
	SIGSETAND(bset, td->td_sigmask);
	PROC_UNLOCK(p);
	bsd_to_linux_sigset(&bset, &lset);
	mask = lset.__mask;
	return (copyout(&mask, args->mask, sizeof(mask)));
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

/*
 * MPSAFE
 */
int
linux_rt_sigpending(struct thread *td, struct linux_rt_sigpending_args *args)
{
	struct proc *p = td->td_proc;
	sigset_t bset;
	l_sigset_t lset;

	if (args->sigsetsize > sizeof(lset))
		return (EINVAL);
		/* NOT REACHED */

#ifdef DEBUG
	if (ldebug(rt_sigpending))
		printf(ARGS(rt_sigpending, "*"));
#endif

	PROC_LOCK(p);
	bset = p->p_siglist;
	SIGSETOR(bset, td->td_siglist);
	SIGSETAND(bset, td->td_sigmask);
	PROC_UNLOCK(p);
	bsd_to_linux_sigset(&bset, &lset);
	return (copyout(&lset, args->set, args->sigsetsize));
}

/*
 * MPSAFE
 */
int
linux_rt_sigtimedwait(struct thread *td,
	struct linux_rt_sigtimedwait_args *args)
{
	int error, sig;
	l_timeval ltv;
	struct timeval tv;
	struct timespec ts, *tsa;
	l_sigset_t lset;
	sigset_t bset;
	l_siginfo_t linfo;
	ksiginfo_t info;

#ifdef DEBUG
	if (ldebug(rt_sigtimedwait))
		printf(ARGS(rt_sigtimedwait, "*"));
#endif
	if (args->sigsetsize != sizeof(l_sigset_t))
		return (EINVAL);

	if ((error = copyin(args->mask, &lset, sizeof(lset))))
		return (error);
	linux_to_bsd_sigset(&lset, &bset);

	tsa = NULL;
	if (args->timeout) {
		if ((error = copyin(args->timeout, &ltv, sizeof(ltv))))
			return (error);
#ifdef DEBUG
		if (ldebug(rt_sigtimedwait))
			printf(LMSG("linux_rt_sigtimedwait: "
			    "incoming timeout (%jd/%jd)\n"),
			    (intmax_t)ltv.tv_sec, (intmax_t)ltv.tv_usec);
#endif
		tv.tv_sec = (long)ltv.tv_sec;
		tv.tv_usec = (suseconds_t)ltv.tv_usec;
		if (itimerfix(&tv)) {
			/*
			 * The timeout was invalid. Convert it to something
			 * valid that will act as it does under Linux.
			 */
			tv.tv_sec += tv.tv_usec / 1000000;
			tv.tv_usec %= 1000000;
			if (tv.tv_usec < 0) {
				tv.tv_sec -= 1;
				tv.tv_usec += 1000000;
			}
			if (tv.tv_sec < 0)
				timevalclear(&tv);
#ifdef DEBUG
			if (ldebug(rt_sigtimedwait))
				printf(LMSG("linux_rt_sigtimedwait: "
				    "converted timeout (%jd/%ld)\n"),
				    (intmax_t)tv.tv_sec, tv.tv_usec);
#endif
		}
		TIMEVAL_TO_TIMESPEC(&tv, &ts);
		tsa = &ts;
	}
	error = kern_sigtimedwait(td, bset, &info, tsa);
#ifdef DEBUG
	if (ldebug(rt_sigtimedwait))
		printf(LMSG("linux_rt_sigtimedwait: "
		    "sigtimedwait returning (%d)\n"), error);
#endif
	if (error)
		return (error);

	sig = bsd_to_linux_signal(info.ksi_signo);

	if (args->ptr) {
		memset(&linfo, 0, sizeof(linfo));
		ksiginfo_to_lsiginfo(&info, &linfo, sig);
		error = copyout(&linfo, args->ptr, sizeof(linfo));
	}
	if (error == 0)
		td->td_retval[0] = sig;

	return (error);
}

int
linux_kill(struct thread *td, struct linux_kill_args *args)
{
	struct kill_args /* {
	    int pid;
	    int signum;
	} */ tmp;

#ifdef DEBUG
	if (ldebug(kill))
		printf(ARGS(kill, "%d, %d"), args->pid, args->signum);
#endif

	/*
	 * Allow signal 0 as a means to check for privileges
	 */
	if (!LINUX_SIG_VALID(args->signum) && args->signum != 0)
		return (EINVAL);

	if (args->signum > 0)
		tmp.signum = linux_to_bsd_signal(args->signum);
	else
		tmp.signum = 0;

	tmp.pid = args->pid;
	return (sys_kill(td, &tmp));
}

static int
linux_do_tkill(struct thread *td, struct thread *tdt, ksiginfo_t *ksi)
{
	struct proc *p;
	int error;

	p = tdt->td_proc;
	AUDIT_ARG_SIGNUM(ksi->ksi_signo);
	AUDIT_ARG_PID(p->p_pid);
	AUDIT_ARG_PROCESS(p);

	error = p_cansignal(td, p, ksi->ksi_signo);
	if (error != 0 || ksi->ksi_signo == 0)
		goto out;

	tdksignal(tdt, ksi->ksi_signo, ksi);

out:
	PROC_UNLOCK(p);
	return (error);
}

int
linux_tgkill(struct thread *td, struct linux_tgkill_args *args)
{
	struct thread *tdt;
	ksiginfo_t ksi;
	int sig;

#ifdef DEBUG
	if (ldebug(tgkill))
		printf(ARGS(tgkill, "%d, %d, %d"),
		    args->tgid, args->pid, args->sig);
#endif

	if (args->pid <= 0 || args->tgid <=0)
		return (EINVAL);

	/*
	 * Allow signal 0 as a means to check for privileges
	 */
	if (!LINUX_SIG_VALID(args->sig) && args->sig != 0)
		return (EINVAL);

	if (args->sig > 0)
		sig = linux_to_bsd_signal(args->sig);
	else
		sig = 0;

	tdt = linux_tdfind(td, args->pid, args->tgid);
	if (tdt == NULL)
		return (ESRCH);

	ksiginfo_init(&ksi);
	ksi.ksi_signo = sig;
	ksi.ksi_code = SI_LWP;
	ksi.ksi_errno = 0;
	ksi.ksi_pid = td->td_proc->p_pid;
	ksi.ksi_uid = td->td_proc->p_ucred->cr_ruid;
	return (linux_do_tkill(td, tdt, &ksi));
}

/*
 * Deprecated since 2.5.75. Replaced by tgkill().
 */
int
linux_tkill(struct thread *td, struct linux_tkill_args *args)
{
	struct thread *tdt;
	ksiginfo_t ksi;
	int sig;

#ifdef DEBUG
	if (ldebug(tkill))
		printf(ARGS(tkill, "%i, %i"), args->tid, args->sig);
#endif
	if (args->tid <= 0)
		return (EINVAL);

	if (!LINUX_SIG_VALID(args->sig))
		return (EINVAL);

	sig = linux_to_bsd_signal(args->sig);

	tdt = linux_tdfind(td, args->tid, -1);
	if (tdt == NULL)
		return (ESRCH);

	ksiginfo_init(&ksi);
	ksi.ksi_signo = sig;
	ksi.ksi_code = SI_LWP;
	ksi.ksi_errno = 0;
	ksi.ksi_pid = td->td_proc->p_pid;
	ksi.ksi_uid = td->td_proc->p_ucred->cr_ruid;
	return (linux_do_tkill(td, tdt, &ksi));
}

void
ksiginfo_to_lsiginfo(const ksiginfo_t *ksi, l_siginfo_t *lsi, l_int sig)
{

	siginfo_to_lsiginfo(&ksi->ksi_info, lsi, sig);
}

static void
sicode_to_lsicode(int si_code, int *lsi_code)
{

	switch (si_code) {
	case SI_USER:
		*lsi_code = LINUX_SI_USER;
		break;
	case SI_KERNEL:
		*lsi_code = LINUX_SI_KERNEL;
		break;
	case SI_QUEUE:
		*lsi_code = LINUX_SI_QUEUE;
		break;
	case SI_TIMER:
		*lsi_code = LINUX_SI_TIMER;
		break;
	case SI_MESGQ:
		*lsi_code = LINUX_SI_MESGQ;
		break;
	case SI_ASYNCIO:
		*lsi_code = LINUX_SI_ASYNCIO;
		break;
	case SI_LWP:
		*lsi_code = LINUX_SI_TKILL;
		break;
	default:
		*lsi_code = si_code;
		break;
	}
}

void
siginfo_to_lsiginfo(const siginfo_t *si, l_siginfo_t *lsi, l_int sig)
{

	/* sig alredy converted */
	lsi->lsi_signo = sig;
	sicode_to_lsicode(si->si_code, &lsi->lsi_code);

	switch (si->si_code) {
	case SI_LWP:
		lsi->lsi_pid = si->si_pid;
		lsi->lsi_uid = si->si_uid;
		break;

	case SI_TIMER:
		lsi->lsi_int = si->si_value.sival_int;
		lsi->lsi_ptr = PTROUT(si->si_value.sival_ptr);
		lsi->lsi_tid = si->si_timerid;
		break;

	case SI_QUEUE:
		lsi->lsi_pid = si->si_pid;
		lsi->lsi_uid = si->si_uid;
		lsi->lsi_ptr = PTROUT(si->si_value.sival_ptr);
		break;

	case SI_ASYNCIO:
		lsi->lsi_int = si->si_value.sival_int;
		lsi->lsi_ptr = PTROUT(si->si_value.sival_ptr);
		break;

	default:
		switch (sig) {
		case LINUX_SIGPOLL:
			/* XXX si_fd? */
			lsi->lsi_band = si->si_band;
			break;

		case LINUX_SIGCHLD:
			lsi->lsi_errno = 0;
			lsi->lsi_pid = si->si_pid;
			lsi->lsi_uid = si->si_uid;

			if (si->si_code == CLD_STOPPED)
				lsi->lsi_status = bsd_to_linux_signal(si->si_status);
			else if (si->si_code == CLD_CONTINUED)
				lsi->lsi_status = bsd_to_linux_signal(SIGCONT);
			else
				lsi->lsi_status = si->si_status;
			break;

		case LINUX_SIGBUS:
		case LINUX_SIGILL:
		case LINUX_SIGFPE:
		case LINUX_SIGSEGV:
			lsi->lsi_addr = PTROUT(si->si_addr);
			break;

		default:
			lsi->lsi_pid = si->si_pid;
			lsi->lsi_uid = si->si_uid;
			if (sig >= LINUX_SIGRTMIN) {
				lsi->lsi_int = si->si_value.sival_int;
				lsi->lsi_ptr = PTROUT(si->si_value.sival_ptr);
			}
			break;
		}
		break;
	}
}

void
lsiginfo_to_ksiginfo(const l_siginfo_t *lsi, ksiginfo_t *ksi, int sig)
{

	ksi->ksi_signo = sig;
	ksi->ksi_code = lsi->lsi_code;	/* XXX. Convert. */
	ksi->ksi_pid = lsi->lsi_pid;
	ksi->ksi_uid = lsi->lsi_uid;
	ksi->ksi_status = lsi->lsi_status;
	ksi->ksi_addr = PTRIN(lsi->lsi_addr);
	ksi->ksi_info.si_value.sival_int = lsi->lsi_int;
}

int
linux_rt_sigqueueinfo(struct thread *td, struct linux_rt_sigqueueinfo_args *args)
{
	l_siginfo_t linfo;
	struct proc *p;
	ksiginfo_t ksi;
	int error;
	int sig;

	if (!LINUX_SIG_VALID(args->sig))
		return (EINVAL);

	error = copyin(args->info, &linfo, sizeof(linfo));
	if (error != 0)
		return (error);

	if (linfo.lsi_code >= 0)
		return (EPERM);

	sig = linux_to_bsd_signal(args->sig);

	error = ESRCH;
	if ((p = pfind_any(args->pid)) != NULL) {
		error = p_cansignal(td, p, sig);
		if (error != 0) {
			PROC_UNLOCK(p);
			return (error);
		}

		ksiginfo_init(&ksi);
		lsiginfo_to_ksiginfo(&linfo, &ksi, sig);
		error = tdsendsignal(p, NULL, sig, &ksi);
		PROC_UNLOCK(p);
	}

	return (error);
}

int
linux_rt_tgsigqueueinfo(struct thread *td, struct linux_rt_tgsigqueueinfo_args *args)
{
	l_siginfo_t linfo;
	struct thread *tds;
	ksiginfo_t ksi;
	int error;
	int sig;

	if (!LINUX_SIG_VALID(args->sig))
		return (EINVAL);

	error = copyin(args->uinfo, &linfo, sizeof(linfo));
	if (error != 0)
		return (error);

	if (linfo.lsi_code >= 0)
		return (EPERM);

	tds = linux_tdfind(td, args->tid, args->tgid);
	if (tds == NULL)
		return (ESRCH);

	sig = linux_to_bsd_signal(args->sig);
	ksiginfo_init(&ksi);
	lsiginfo_to_ksiginfo(&linfo, &ksi, sig);
	return (linux_do_tkill(td, tds, &ksi));
}
