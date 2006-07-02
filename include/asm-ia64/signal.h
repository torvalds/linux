#ifndef _ASM_IA64_SIGNAL_H
#define _ASM_IA64_SIGNAL_H

/*
 * Modified 1998-2001, 2003
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 *
 * Unfortunately, this file is being included by bits/signal.h in
 * glibc-2.x.  Hence the #ifdef __KERNEL__ ugliness.
 */

#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGIOT		 6
#define SIGBUS		 7
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGUSR1		10
#define SIGSEGV		11
#define SIGUSR2		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGSTKFLT	16
#define SIGCHLD		17
#define SIGCONT		18
#define SIGSTOP		19
#define SIGTSTP		20
#define SIGTTIN		21
#define SIGTTOU		22
#define SIGURG		23
#define SIGXCPU		24
#define SIGXFSZ		25
#define SIGVTALRM	26
#define SIGPROF		27
#define SIGWINCH	28
#define SIGIO		29
#define SIGPOLL		SIGIO
/*
#define SIGLOST		29
*/
#define SIGPWR		30
#define SIGSYS		31
/* signal 31 is no longer "unused", but the SIGUNUSED macro remains for backwards compatibility */
#define	SIGUNUSED	31

/* These should not be considered constants from userland.  */
#define SIGRTMIN	32
#define SIGRTMAX	_NSIG

/*
 * SA_FLAGS values:
 *
 * SA_ONSTACK indicates that a registered stack_t will be used.
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_NOCLDSTOP flag to turn off SIGCHLD when children stop.
 * SA_RESETHAND clears the handler when the signal is delivered.
 * SA_NOCLDWAIT flag on SIGCHLD to inhibit zombies.
 * SA_NODEFER prevents the current signal from being masked in the handler.
 *
 * SA_ONESHOT and SA_NOMASK are the historical Linux names for the Single
 * Unix names RESETHAND and NODEFER respectively.
 */
#define SA_NOCLDSTOP	0x00000001
#define SA_NOCLDWAIT	0x00000002
#define SA_SIGINFO	0x00000004
#define SA_ONSTACK	0x08000000
#define SA_RESTART	0x10000000
#define SA_NODEFER	0x40000000
#define SA_RESETHAND	0x80000000

#define SA_NOMASK	SA_NODEFER
#define SA_ONESHOT	SA_RESETHAND

#define SA_RESTORER	0x04000000

/*
 * sigaltstack controls
 */
#define SS_ONSTACK	1
#define SS_DISABLE	2

/*
 * The minimum stack size needs to be fairly large because we want to
 * be sure that an app compiled for today's CPUs will continue to run
 * on all future CPU models.  The CPU model matters because the signal
 * frame needs to have space for the complete machine state, including
 * all physical stacked registers.  The number of physical stacked
 * registers is CPU model dependent, but given that the width of
 * ar.rsc.loadrs is 14 bits, we can assume that they'll never take up
 * more than 16KB of space.
 */
#if 1
  /*
   * This is a stupid typo: the value was _meant_ to be 131072 (0x20000), but I typed it
   * in wrong. ;-(  To preserve backwards compatibility, we leave the kernel at the
   * incorrect value and fix libc only.
   */
# define MINSIGSTKSZ	131027	/* min. stack size for sigaltstack() */
#else
# define MINSIGSTKSZ	131072	/* min. stack size for sigaltstack() */
#endif
#define SIGSTKSZ	262144	/* default stack size for sigaltstack() */

#ifdef __KERNEL__

#define _NSIG		64
#define _NSIG_BPW	64
#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

#endif /* __KERNEL__ */

#include <asm-generic/signal.h>

# ifndef __ASSEMBLY__

#  include <linux/types.h>

/* Avoid too many header ordering problems.  */
struct siginfo;

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	size_t ss_size;
} stack_t;

#ifdef __KERNEL__

/* Most things should be clean enough to redefine this at will, if care
   is taken to make libc match.  */

typedef unsigned long old_sigset_t;

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

struct sigaction {
	__sighandler_t sa_handler;
	unsigned long sa_flags;
	sigset_t sa_mask;		/* mask last for extensibility */
};

struct k_sigaction {
	struct sigaction sa;
};

#  include <asm/sigcontext.h>

#define ptrace_signal_deliver(regs, cookie) do { } while (0)

#endif /* __KERNEL__ */

# endif /* !__ASSEMBLY__ */
#endif /* _ASM_IA64_SIGNAL_H */
