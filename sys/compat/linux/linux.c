/*-
 * Copyright (c) 2015 Dmitry Chagin
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
#include <sys/signalvar.h>

#include <compat/linux/linux.h>


static int bsd_to_linux_sigtbl[LINUX_SIGTBLSZ] = {
	LINUX_SIGHUP,	/* SIGHUP */
	LINUX_SIGINT,	/* SIGINT */
	LINUX_SIGQUIT,	/* SIGQUIT */
	LINUX_SIGILL,	/* SIGILL */
	LINUX_SIGTRAP,	/* SIGTRAP */
	LINUX_SIGABRT,	/* SIGABRT */
	0,		/* SIGEMT */
	LINUX_SIGFPE,	/* SIGFPE */
	LINUX_SIGKILL,	/* SIGKILL */
	LINUX_SIGBUS,	/* SIGBUS */
	LINUX_SIGSEGV,	/* SIGSEGV */
	LINUX_SIGSYS,	/* SIGSYS */
	LINUX_SIGPIPE,	/* SIGPIPE */
	LINUX_SIGALRM,	/* SIGALRM */
	LINUX_SIGTERM,	/* SIGTERM */
	LINUX_SIGURG,	/* SIGURG */
	LINUX_SIGSTOP,	/* SIGSTOP */
	LINUX_SIGTSTP,	/* SIGTSTP */
	LINUX_SIGCONT,	/* SIGCONT */
	LINUX_SIGCHLD,	/* SIGCHLD */
	LINUX_SIGTTIN,	/* SIGTTIN */
	LINUX_SIGTTOU,	/* SIGTTOU */
	LINUX_SIGIO,	/* SIGIO */
	LINUX_SIGXCPU,	/* SIGXCPU */
	LINUX_SIGXFSZ,	/* SIGXFSZ */
	LINUX_SIGVTALRM,/* SIGVTALRM */
	LINUX_SIGPROF,	/* SIGPROF */
	LINUX_SIGWINCH,	/* SIGWINCH */
	0,		/* SIGINFO */
	LINUX_SIGUSR1,	/* SIGUSR1 */
	LINUX_SIGUSR2	/* SIGUSR2 */
};

static int linux_to_bsd_sigtbl[LINUX_SIGTBLSZ] = {
	SIGHUP,		/* LINUX_SIGHUP */
	SIGINT,		/* LINUX_SIGINT */
	SIGQUIT,	/* LINUX_SIGQUIT */
	SIGILL,		/* LINUX_SIGILL */
	SIGTRAP,	/* LINUX_SIGTRAP */
	SIGABRT,	/* LINUX_SIGABRT */
	SIGBUS,		/* LINUX_SIGBUS */
	SIGFPE,		/* LINUX_SIGFPE */
	SIGKILL,	/* LINUX_SIGKILL */
	SIGUSR1,	/* LINUX_SIGUSR1 */
	SIGSEGV,	/* LINUX_SIGSEGV */
	SIGUSR2,	/* LINUX_SIGUSR2 */
	SIGPIPE,	/* LINUX_SIGPIPE */
	SIGALRM,	/* LINUX_SIGALRM */
	SIGTERM,	/* LINUX_SIGTERM */
	SIGBUS,		/* LINUX_SIGSTKFLT */
	SIGCHLD,	/* LINUX_SIGCHLD */
	SIGCONT,	/* LINUX_SIGCONT */
	SIGSTOP,	/* LINUX_SIGSTOP */
	SIGTSTP,	/* LINUX_SIGTSTP */
	SIGTTIN,	/* LINUX_SIGTTIN */
	SIGTTOU,	/* LINUX_SIGTTOU */
	SIGURG,		/* LINUX_SIGURG */
	SIGXCPU,	/* LINUX_SIGXCPU */
	SIGXFSZ,	/* LINUX_SIGXFSZ */
	SIGVTALRM,	/* LINUX_SIGVTALARM */
	SIGPROF,	/* LINUX_SIGPROF */
	SIGWINCH,	/* LINUX_SIGWINCH */
	SIGIO,		/* LINUX_SIGIO */
	/*
	 * FreeBSD does not have SIGPWR signal, map Linux SIGPWR signal
	 * to the first unused FreeBSD signal number. Since Linux supports
	 * signals from 1 to 64 we are ok here as our SIGRTMIN = 65.
	 */
	SIGRTMIN,	/* LINUX_SIGPWR */
	SIGSYS		/* LINUX_SIGSYS */
};

/*
 * Map Linux RT signals to the FreeBSD RT signals.
 */
static inline int
linux_to_bsd_rt_signal(int sig)
{

	return (SIGRTMIN + 1 + sig - LINUX_SIGRTMIN);
}

static inline int
bsd_to_linux_rt_signal(int sig)
{

	return (sig - SIGRTMIN - 1 + LINUX_SIGRTMIN);
}

int
linux_to_bsd_signal(int sig)
{

	KASSERT(sig > 0 && sig <= LINUX_SIGRTMAX, ("invalid Linux signal %d\n", sig));

	if (sig < LINUX_SIGRTMIN)
		return (linux_to_bsd_sigtbl[_SIG_IDX(sig)]);

	return (linux_to_bsd_rt_signal(sig));
}

int
bsd_to_linux_signal(int sig)
{

	if (sig <= LINUX_SIGTBLSZ)
		return (bsd_to_linux_sigtbl[_SIG_IDX(sig)]);
	if (sig == SIGRTMIN)
		return (LINUX_SIGPWR);

	return (bsd_to_linux_rt_signal(sig));
}

int
linux_to_bsd_sigaltstack(int lsa)
{
	int bsa = 0;

	if (lsa & LINUX_SS_DISABLE)
		bsa |= SS_DISABLE;
	/*
	 * Linux ignores SS_ONSTACK flag for ss
	 * parameter while FreeBSD prohibits it.
	 */
	return (bsa);
}

int
bsd_to_linux_sigaltstack(int bsa)
{
	int lsa = 0;

	if (bsa & SS_DISABLE)
		lsa |= LINUX_SS_DISABLE;
	if (bsa & SS_ONSTACK)
		lsa |= LINUX_SS_ONSTACK;
	return (lsa);
}

void
linux_to_bsd_sigset(l_sigset_t *lss, sigset_t *bss)
{
	int b, l;

	SIGEMPTYSET(*bss);
	for (l = 1; l <= LINUX_SIGRTMAX; l++) {
		if (LINUX_SIGISMEMBER(*lss, l)) {
			b = linux_to_bsd_signal(l);
			if (b)
				SIGADDSET(*bss, b);
		}
	}
}

void
bsd_to_linux_sigset(sigset_t *bss, l_sigset_t *lss)
{
	int b, l;

	LINUX_SIGEMPTYSET(*lss);
	for (b = 1; b <= SIGRTMAX; b++) {
		if (SIGISMEMBER(*bss, b)) {
			l = bsd_to_linux_signal(b);
			if (l)
				LINUX_SIGADDSET(*lss, l);
		}
	}
}
