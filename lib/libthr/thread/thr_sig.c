/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005, David Xu <davidxu@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/param.h>
#include <sys/types.h>
#include <sys/signalvar.h>
#include <sys/syscall.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "un-namespace.h"
#include "libc_private.h"

#include "libc_private.h"
#include "thr_private.h"

/* #define DEBUG_SIGNAL */
#ifdef DEBUG_SIGNAL
#define DBG_MSG		stdout_debug
#else
#define DBG_MSG(x...)
#endif

struct usigaction {
	struct sigaction sigact;
	struct urwlock   lock;
};

static struct usigaction _thr_sigact[_SIG_MAXSIG];

static inline struct usigaction *
__libc_sigaction_slot(int signo)
{

	return (&_thr_sigact[signo - 1]);
}

static void thr_sighandler(int, siginfo_t *, void *);
static void handle_signal(struct sigaction *, int, siginfo_t *, ucontext_t *);
static void check_deferred_signal(struct pthread *);
static void check_suspend(struct pthread *);
static void check_cancel(struct pthread *curthread, ucontext_t *ucp);

int	_sigtimedwait(const sigset_t *set, siginfo_t *info,
	const struct timespec * timeout);
int	_sigwaitinfo(const sigset_t *set, siginfo_t *info);
int	_sigwait(const sigset_t *set, int *sig);
int	_setcontext(const ucontext_t *);
int	_swapcontext(ucontext_t *, const ucontext_t *);

static const sigset_t _thr_deferset={{
	0xffffffff & ~(_SIG_BIT(SIGBUS)|_SIG_BIT(SIGILL)|_SIG_BIT(SIGFPE)|
	_SIG_BIT(SIGSEGV)|_SIG_BIT(SIGTRAP)|_SIG_BIT(SIGSYS)),
	0xffffffff,
	0xffffffff,
	0xffffffff}};

static const sigset_t _thr_maskset={{
	0xffffffff,
	0xffffffff,
	0xffffffff,
	0xffffffff}};

void
_thr_signal_block(struct pthread *curthread)
{
	
	if (curthread->sigblock > 0) {
		curthread->sigblock++;
		return;
	}
	__sys_sigprocmask(SIG_BLOCK, &_thr_maskset, &curthread->sigmask);
	curthread->sigblock++;
}

void
_thr_signal_unblock(struct pthread *curthread)
{
	if (--curthread->sigblock == 0)
		__sys_sigprocmask(SIG_SETMASK, &curthread->sigmask, NULL);
}

int
_thr_send_sig(struct pthread *thread, int sig)
{
	return thr_kill(thread->tid, sig);
}

static inline void
remove_thr_signals(sigset_t *set)
{
	if (SIGISMEMBER(*set, SIGCANCEL))
		SIGDELSET(*set, SIGCANCEL);
}

static const sigset_t *
thr_remove_thr_signals(const sigset_t *set, sigset_t *newset)
{
	*newset = *set;
	remove_thr_signals(newset);
	return (newset);
}

static void
sigcancel_handler(int sig __unused,
	siginfo_t *info __unused, ucontext_t *ucp)
{
	struct pthread *curthread = _get_curthread();
	int err;

	if (THR_IN_CRITICAL(curthread))
		return;
	err = errno;
	check_suspend(curthread);
	check_cancel(curthread, ucp);
	errno = err;
}

typedef void (*ohandler)(int sig, int code, struct sigcontext *scp,
    char *addr, __sighandler_t *catcher);

/*
 * The signal handler wrapper is entered with all signal masked.
 */
static void
thr_sighandler(int sig, siginfo_t *info, void *_ucp)
{
	struct pthread *curthread;
	ucontext_t *ucp;
	struct sigaction act;
	struct usigaction *usa;
	int err;

	err = errno;
	curthread = _get_curthread();
	ucp = _ucp;
	usa = __libc_sigaction_slot(sig);
	_thr_rwl_rdlock(&usa->lock);
	act = usa->sigact;
	_thr_rwl_unlock(&usa->lock);
	errno = err;
	curthread->deferred_run = 0;

	/*
	 * if a thread is in critical region, for example it holds low level locks,
	 * try to defer the signal processing, however if the signal is synchronous
	 * signal, it means a bad thing has happened, this is a programming error,
	 * resuming fault point can not help anything (normally causes deadloop),
	 * so here we let user code handle it immediately.
	 */
	if (THR_IN_CRITICAL(curthread) && SIGISMEMBER(_thr_deferset, sig)) {
		memcpy(&curthread->deferred_sigact, &act, sizeof(struct sigaction));
		memcpy(&curthread->deferred_siginfo, info, sizeof(siginfo_t));
		curthread->deferred_sigmask = ucp->uc_sigmask;
		/* mask all signals, we will restore it later. */
		ucp->uc_sigmask = _thr_deferset;
		return;
	}

	handle_signal(&act, sig, info, ucp);
}

static void
handle_signal(struct sigaction *actp, int sig, siginfo_t *info, ucontext_t *ucp)
{
	struct pthread *curthread = _get_curthread();
	ucontext_t uc2;
	__siginfohandler_t *sigfunc;
	int cancel_point;
	int cancel_async;
	int cancel_enable;
	int in_sigsuspend;
	int err;

	/* add previous level mask */
	SIGSETOR(actp->sa_mask, ucp->uc_sigmask);

	/* add this signal's mask */
	if (!(actp->sa_flags & SA_NODEFER))
		SIGADDSET(actp->sa_mask, sig);

	in_sigsuspend = curthread->in_sigsuspend;
	curthread->in_sigsuspend = 0;

	/*
	 * If thread is in deferred cancellation mode, disable cancellation
	 * in signal handler.
	 * If user signal handler calls a cancellation point function, e.g,
	 * it calls write() to write data to file, because write() is a
	 * cancellation point, the thread is immediately cancelled if 
	 * cancellation is pending, to avoid this problem while thread is in
	 * deferring mode, cancellation is temporarily disabled.
	 */
	cancel_point = curthread->cancel_point;
	cancel_async = curthread->cancel_async;
	cancel_enable = curthread->cancel_enable;
	curthread->cancel_point = 0;
	if (!cancel_async)
		curthread->cancel_enable = 0;

	/* restore correct mask before calling user handler */
	__sys_sigprocmask(SIG_SETMASK, &actp->sa_mask, NULL);

	sigfunc = actp->sa_sigaction;

	/*
	 * We have already reset cancellation point flags, so if user's code
	 * longjmp()s out of its signal handler, wish its jmpbuf was set
	 * outside of a cancellation point, in most cases, this would be
	 * true.  However, there is no way to save cancel_enable in jmpbuf,
	 * so after setjmps() returns once more, the user code may need to
	 * re-set cancel_enable flag by calling pthread_setcancelstate().
	 */
	if ((actp->sa_flags & SA_SIGINFO) != 0) {
		sigfunc(sig, info, ucp);
	} else {
		((ohandler)sigfunc)(sig, info->si_code,
		    (struct sigcontext *)ucp, info->si_addr,
		    (__sighandler_t *)sigfunc);
	}
	err = errno;

	curthread->in_sigsuspend = in_sigsuspend;
	curthread->cancel_point = cancel_point;
	curthread->cancel_enable = cancel_enable;

	memcpy(&uc2, ucp, sizeof(uc2));
	SIGDELSET(uc2.uc_sigmask, SIGCANCEL);

	/* reschedule cancellation */
	check_cancel(curthread, &uc2);
	errno = err;
	syscall(SYS_sigreturn, &uc2);
}

void
_thr_ast(struct pthread *curthread)
{

	if (!THR_IN_CRITICAL(curthread)) {
		check_deferred_signal(curthread);
		check_suspend(curthread);
		check_cancel(curthread, NULL);
	}
}

/* reschedule cancellation */
static void
check_cancel(struct pthread *curthread, ucontext_t *ucp)
{

	if (__predict_true(!curthread->cancel_pending ||
	    !curthread->cancel_enable || curthread->no_cancel))
		return;

	/*
 	 * Otherwise, we are in defer mode, and we are at
	 * cancel point, tell kernel to not block the current
	 * thread on next cancelable system call.
	 * 
	 * There are three cases we should call thr_wake() to
	 * turn on TDP_WAKEUP or send SIGCANCEL in kernel:
	 * 1) we are going to call a cancelable system call,
	 *    non-zero cancel_point means we are already in
	 *    cancelable state, next system call is cancelable.
	 * 2) because _thr_ast() may be called by
	 *    THR_CRITICAL_LEAVE() which is used by rtld rwlock
	 *    and any libthr internal locks, when rtld rwlock
	 *    is used, it is mostly caused by an unresolved PLT.
	 *    Those routines may clear the TDP_WAKEUP flag by
	 *    invoking some system calls, in those cases, we
	 *    also should reenable the flag.
	 * 3) thread is in sigsuspend(), and the syscall insists
	 *    on getting a signal before it agrees to return.
 	 */
	if (curthread->cancel_point) {
		if (curthread->in_sigsuspend && ucp) {
			SIGADDSET(ucp->uc_sigmask, SIGCANCEL);
			curthread->unblock_sigcancel = 1;
			_thr_send_sig(curthread, SIGCANCEL);
		} else
			thr_wake(curthread->tid);
	} else if (curthread->cancel_async) {
		/*
		 * asynchronous cancellation mode, act upon
		 * immediately.
		 */
		_pthread_exit_mask(PTHREAD_CANCELED,
		    ucp? &ucp->uc_sigmask : NULL);
	}
}

static void
check_deferred_signal(struct pthread *curthread)
{
	ucontext_t *uc;
	struct sigaction act;
	siginfo_t info;
	int uc_len;

	if (__predict_true(curthread->deferred_siginfo.si_signo == 0 ||
	    curthread->deferred_run))
		return;

	curthread->deferred_run = 1;
	uc_len = __getcontextx_size();
	uc = alloca(uc_len);
	getcontext(uc);
	if (curthread->deferred_siginfo.si_signo == 0) {
		curthread->deferred_run = 0;
		return;
	}
	__fillcontextx2((char *)uc);
	act = curthread->deferred_sigact;
	uc->uc_sigmask = curthread->deferred_sigmask;
	memcpy(&info, &curthread->deferred_siginfo, sizeof(siginfo_t));
	/* remove signal */
	curthread->deferred_siginfo.si_signo = 0;
	handle_signal(&act, info.si_signo, &info, uc);
}

static void
check_suspend(struct pthread *curthread)
{
	uint32_t cycle;

	if (__predict_true((curthread->flags &
		(THR_FLAGS_NEED_SUSPEND | THR_FLAGS_SUSPENDED))
		!= THR_FLAGS_NEED_SUSPEND))
		return;
	if (curthread == _single_thread)
		return;
	if (curthread->force_exit)
		return;

	/* 
	 * Blocks SIGCANCEL which other threads must send.
	 */
	_thr_signal_block(curthread);

	/*
	 * Increase critical_count, here we don't use THR_LOCK/UNLOCK
	 * because we are leaf code, we don't want to recursively call
	 * ourself.
	 */
	curthread->critical_count++;
	THR_UMUTEX_LOCK(curthread, &(curthread)->lock);
	while ((curthread->flags & THR_FLAGS_NEED_SUSPEND) != 0) {
		curthread->cycle++;
		cycle = curthread->cycle;

		/* Wake the thread suspending us. */
		_thr_umtx_wake(&curthread->cycle, INT_MAX, 0);

		/*
		 * if we are from pthread_exit, we don't want to
		 * suspend, just go and die.
		 */
		if (curthread->state == PS_DEAD)
			break;
		curthread->flags |= THR_FLAGS_SUSPENDED;
		THR_UMUTEX_UNLOCK(curthread, &(curthread)->lock);
		_thr_umtx_wait_uint(&curthread->cycle, cycle, NULL, 0);
		THR_UMUTEX_LOCK(curthread, &(curthread)->lock);
	}
	THR_UMUTEX_UNLOCK(curthread, &(curthread)->lock);
	curthread->critical_count--;

	_thr_signal_unblock(curthread);
}

void
_thr_signal_init(int dlopened)
{
	struct sigaction act, nact, oact;
	struct usigaction *usa;
	sigset_t oldset;
	int sig, error;

	if (dlopened) {
		__sys_sigprocmask(SIG_SETMASK, &_thr_maskset, &oldset);
		for (sig = 1; sig <= _SIG_MAXSIG; sig++) {
			if (sig == SIGCANCEL)
				continue;
			error = __sys_sigaction(sig, NULL, &oact);
			if (error == -1 || oact.sa_handler == SIG_DFL ||
			    oact.sa_handler == SIG_IGN)
				continue;
			usa = __libc_sigaction_slot(sig);
			usa->sigact = oact;
			nact = oact;
			remove_thr_signals(&usa->sigact.sa_mask);
			nact.sa_flags &= ~SA_NODEFER;
			nact.sa_flags |= SA_SIGINFO;
			nact.sa_sigaction = thr_sighandler;
			nact.sa_mask = _thr_maskset;
			(void)__sys_sigaction(sig, &nact, NULL);
		}
		__sys_sigprocmask(SIG_SETMASK, &oldset, NULL);
	}

	/* Install SIGCANCEL handler. */
	SIGFILLSET(act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = (__siginfohandler_t *)&sigcancel_handler;
	__sys_sigaction(SIGCANCEL, &act, NULL);

	/* Unblock SIGCANCEL */
	SIGEMPTYSET(act.sa_mask);
	SIGADDSET(act.sa_mask, SIGCANCEL);
	__sys_sigprocmask(SIG_UNBLOCK, &act.sa_mask, NULL);
}

void
_thr_sigact_unload(struct dl_phdr_info *phdr_info __unused)
{
#if 0
	struct pthread *curthread = _get_curthread();
	struct urwlock *rwlp;
	struct sigaction *actp;
	struct usigaction *usa;
	struct sigaction kact;
	void (*handler)(int);
	int sig;
 
	_thr_signal_block(curthread);
	for (sig = 1; sig <= _SIG_MAXSIG; sig++) {
		usa = __libc_sigaction_slot(sig);
		actp = &usa->sigact;
retry:
		handler = actp->sa_handler;
		if (handler != SIG_DFL && handler != SIG_IGN &&
		    __elf_phdr_match_addr(phdr_info, handler)) {
			rwlp = &usa->lock;
			_thr_rwl_wrlock(rwlp);
			if (handler != actp->sa_handler) {
				_thr_rwl_unlock(rwlp);
				goto retry;
			}
			actp->sa_handler = SIG_DFL;
			actp->sa_flags = SA_SIGINFO;
			SIGEMPTYSET(actp->sa_mask);
			if (__sys_sigaction(sig, NULL, &kact) == 0 &&
				kact.sa_handler != SIG_DFL &&
				kact.sa_handler != SIG_IGN)
				__sys_sigaction(sig, actp, NULL);
			_thr_rwl_unlock(rwlp);
		}
	}
	_thr_signal_unblock(curthread);
#endif
}

void
_thr_signal_prefork(void)
{
	int i;

	for (i = 1; i <= _SIG_MAXSIG; ++i)
		_thr_rwl_rdlock(&__libc_sigaction_slot(i)->lock);
}

void
_thr_signal_postfork(void)
{
	int i;

	for (i = 1; i <= _SIG_MAXSIG; ++i)
		_thr_rwl_unlock(&__libc_sigaction_slot(i)->lock);
}

void
_thr_signal_postfork_child(void)
{
	int i;

	for (i = 1; i <= _SIG_MAXSIG; ++i) {
		bzero(&__libc_sigaction_slot(i) -> lock,
		    sizeof(struct urwlock));
	}
}

void
_thr_signal_deinit(void)
{
}

int
__thr_sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
	struct sigaction newact, oldact, oldact2;
	sigset_t oldset;
	struct usigaction *usa;
	int ret, err;

	if (!_SIG_VALID(sig) || sig == SIGCANCEL) {
		errno = EINVAL;
		return (-1);
	}

	ret = 0;
	err = 0;
	usa = __libc_sigaction_slot(sig);

	__sys_sigprocmask(SIG_SETMASK, &_thr_maskset, &oldset);
	_thr_rwl_wrlock(&usa->lock);
 
	if (act != NULL) {
		oldact2 = usa->sigact;
		newact = *act;

 		/*
		 * if a new sig handler is SIG_DFL or SIG_IGN,
		 * don't remove old handler from __libc_sigact[],
		 * so deferred signals still can use the handlers,
		 * multiple threads invoking sigaction itself is
		 * a race condition, so it is not a problem.
		 */
		if (newact.sa_handler != SIG_DFL &&
		    newact.sa_handler != SIG_IGN) {
			usa->sigact = newact;
			remove_thr_signals(&usa->sigact.sa_mask);
			newact.sa_flags &= ~SA_NODEFER;
			newact.sa_flags |= SA_SIGINFO;
			newact.sa_sigaction = thr_sighandler;
			newact.sa_mask = _thr_maskset; /* mask all signals */
		}
		ret = __sys_sigaction(sig, &newact, &oldact);
		if (ret == -1) {
			err = errno;
			usa->sigact = oldact2;
		}
	} else if (oact != NULL) {
		ret = __sys_sigaction(sig, NULL, &oldact);
		err = errno;
	}

	if (oldact.sa_handler != SIG_DFL && oldact.sa_handler != SIG_IGN) {
		if (act != NULL)
			oldact = oldact2;
		else if (oact != NULL)
			oldact = usa->sigact;
	}

	_thr_rwl_unlock(&usa->lock);
	__sys_sigprocmask(SIG_SETMASK, &oldset, NULL);

	if (ret == 0) {
		if (oact != NULL)
			*oact = oldact;
	} else {
		errno = err;
	}
	return (ret);
}

int
__thr_sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
	const sigset_t *p = set;
	sigset_t newset;

	if (how != SIG_UNBLOCK) {
		if (set != NULL) {
			newset = *set;
			SIGDELSET(newset, SIGCANCEL);
			p = &newset;
		}
	}
	return (__sys_sigprocmask(how, p, oset));
}

__weak_reference(_pthread_sigmask, pthread_sigmask);

int
_pthread_sigmask(int how, const sigset_t *set, sigset_t *oset)
{

	if (__thr_sigprocmask(how, set, oset))
		return (errno);
	return (0);
}

int
_sigsuspend(const sigset_t * set)
{
	sigset_t newset;

	return (__sys_sigsuspend(thr_remove_thr_signals(set, &newset)));
}

int
__thr_sigsuspend(const sigset_t * set)
{
	struct pthread *curthread;
	sigset_t newset;
	int ret, old;

	curthread = _get_curthread();

	old = curthread->in_sigsuspend;
	curthread->in_sigsuspend = 1;
	_thr_cancel_enter(curthread);
	ret = __sys_sigsuspend(thr_remove_thr_signals(set, &newset));
	_thr_cancel_leave(curthread, 1);
	curthread->in_sigsuspend = old;
	if (curthread->unblock_sigcancel) {
		curthread->unblock_sigcancel = 0;
		SIGEMPTYSET(newset);
		SIGADDSET(newset, SIGCANCEL);
		__sys_sigprocmask(SIG_UNBLOCK, &newset, NULL);
	}

	return (ret);
}

int
_sigtimedwait(const sigset_t *set, siginfo_t *info,
	const struct timespec * timeout)
{
	sigset_t newset;

	return (__sys_sigtimedwait(thr_remove_thr_signals(set, &newset), info,
	    timeout));
}

/*
 * Cancellation behavior:
 *   Thread may be canceled at start, if thread got signal,
 *   it is not canceled.
 */
int
__thr_sigtimedwait(const sigset_t *set, siginfo_t *info,
    const struct timespec * timeout)
{
	struct pthread	*curthread = _get_curthread();
	sigset_t newset;
	int ret;

	_thr_cancel_enter(curthread);
	ret = __sys_sigtimedwait(thr_remove_thr_signals(set, &newset), info,
	    timeout);
	_thr_cancel_leave(curthread, (ret == -1));
	return (ret);
}

int
_sigwaitinfo(const sigset_t *set, siginfo_t *info)
{
	sigset_t newset;

	return (__sys_sigwaitinfo(thr_remove_thr_signals(set, &newset), info));
}

/*
 * Cancellation behavior:
 *   Thread may be canceled at start, if thread got signal,
 *   it is not canceled.
 */ 
int
__thr_sigwaitinfo(const sigset_t *set, siginfo_t *info)
{
	struct pthread	*curthread = _get_curthread();
	sigset_t newset;
	int ret;

	_thr_cancel_enter(curthread);
	ret = __sys_sigwaitinfo(thr_remove_thr_signals(set, &newset), info);
	_thr_cancel_leave(curthread, ret == -1);
	return (ret);
}

int
_sigwait(const sigset_t *set, int *sig)
{
	sigset_t newset;

	return (__sys_sigwait(thr_remove_thr_signals(set, &newset), sig));
}

/*
 * Cancellation behavior:
 *   Thread may be canceled at start, if thread got signal,
 *   it is not canceled.
 */ 
int
__thr_sigwait(const sigset_t *set, int *sig)
{
	struct pthread	*curthread = _get_curthread();
	sigset_t newset;
	int ret;

	do {
		_thr_cancel_enter(curthread);
		ret = __sys_sigwait(thr_remove_thr_signals(set, &newset), sig);
		_thr_cancel_leave(curthread, (ret != 0));
	} while (ret == EINTR);
	return (ret);
}

int
__thr_setcontext(const ucontext_t *ucp)
{
	ucontext_t uc;

	if (ucp == NULL) {
		errno = EINVAL;
		return (-1);
	}
	if (!SIGISMEMBER(ucp->uc_sigmask, SIGCANCEL))
		return (__sys_setcontext(ucp));
	(void) memcpy(&uc, ucp, sizeof(uc));
	SIGDELSET(uc.uc_sigmask, SIGCANCEL);
	return (__sys_setcontext(&uc));
}

int
__thr_swapcontext(ucontext_t *oucp, const ucontext_t *ucp)
{
	ucontext_t uc;

	if (oucp == NULL || ucp == NULL) {
		errno = EINVAL;
		return (-1);
	}
	if (SIGISMEMBER(ucp->uc_sigmask, SIGCANCEL)) {
		(void) memcpy(&uc, ucp, sizeof(uc));
		SIGDELSET(uc.uc_sigmask, SIGCANCEL);
		ucp = &uc;
	}
	return (__sys_swapcontext(oucp, ucp));
}
