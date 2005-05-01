#ifndef _ASM_PARISC_SIGNAL_H
#define _ASM_PARISC_SIGNAL_H

#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGIOT		 6
#define SIGEMT		 7
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGBUS		10
#define SIGSEGV		11
#define SIGSYS		12 /* Linux doesn't use this */
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGUSR1		16
#define SIGUSR2		17
#define SIGCHLD		18
#define SIGPWR		19
#define SIGVTALRM	20
#define SIGPROF		21
#define SIGIO		22
#define SIGPOLL		SIGIO
#define SIGWINCH	23
#define SIGSTOP		24
#define SIGTSTP		25
#define SIGCONT		26
#define SIGTTIN		27
#define SIGTTOU		28
#define SIGURG		29
#define SIGLOST		30 /* Linux doesn't use this either */
#define	SIGUNUSED	31
#define SIGRESERVE	SIGUNUSED

#define SIGXCPU		33
#define SIGXFSZ		34
#define SIGSTKFLT	36

/* These should not be considered constants from userland.  */
#define SIGRTMIN	37
#define SIGRTMAX	_NSIG /* it's 44 under HP/UX */

/*
 * SA_FLAGS values:
 *
 * SA_ONSTACK indicates that a registered stack_t will be used.
 * SA_INTERRUPT is a no-op, but left due to historical reasons. Use the
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_NOCLDSTOP flag to turn off SIGCHLD when children stop.
 * SA_RESETHAND clears the handler when the signal is delivered.
 * SA_NOCLDWAIT flag on SIGCHLD to inhibit zombies.
 * SA_NODEFER prevents the current signal from being masked in the handler.
 *
 * SA_ONESHOT and SA_NOMASK are the historical Linux names for the Single
 * Unix names RESETHAND and NODEFER respectively.
 */
#define SA_ONSTACK	0x00000001
#define SA_RESETHAND	0x00000004
#define SA_NOCLDSTOP	0x00000008
#define SA_SIGINFO	0x00000010
#define SA_NODEFER	0x00000020
#define SA_RESTART	0x00000040
#define SA_NOCLDWAIT	0x00000080
#define _SA_SIGGFAULT	0x00000100 /* HPUX */

#define SA_NOMASK	SA_NODEFER
#define SA_ONESHOT	SA_RESETHAND
#define SA_INTERRUPT	0x20000000 /* dummy -- ignored */

#define SA_RESTORER	0x04000000 /* obsolete -- ignored */

/* 
 * sigaltstack controls
 */
#define SS_ONSTACK	1
#define SS_DISABLE	2

#define MINSIGSTKSZ	2048
#define SIGSTKSZ	8192

#ifdef __KERNEL__

#define _NSIG		64
/* bits-per-word, where word apparently means 'long' not 'int' */
#define _NSIG_BPW	BITS_PER_LONG
#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

#endif /* __KERNEL__ */

#define SIG_BLOCK          0	/* for blocking signals */
#define SIG_UNBLOCK        1	/* for unblocking signals */
#define SIG_SETMASK        2	/* for setting the signal mask */

#define SIG_DFL	((__sighandler_t)0)	/* default signal handling */
#define SIG_IGN	((__sighandler_t)1)	/* ignore signal */
#define SIG_ERR	((__sighandler_t)-1)	/* error return from signal */

# ifndef __ASSEMBLY__

#  include <linux/types.h>

/* Avoid too many header ordering problems.  */
struct siginfo;

/* Type of a signal handler.  */
#ifdef __LP64__
/* function pointers on 64-bit parisc are pointers to little structs and the
 * compiler doesn't support code which changes or tests the address of
 * the function in the little struct.  This is really ugly -PB
 */
typedef char __user *__sighandler_t;
#else
typedef void __signalfn_t(int);
typedef __signalfn_t __user *__sighandler_t;
#endif

typedef struct sigaltstack {
	void __user *ss_sp;
	int ss_flags;
	size_t ss_size;
} stack_t;

#ifdef __KERNEL__

/* Most things should be clean enough to redefine this at will, if care
   is taken to make libc match.  */

typedef unsigned long old_sigset_t;		/* at least 32 bits */

typedef struct {
	/* next_signal() assumes this is a long - no choice */
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

#define ptrace_signal_deliver(regs, cookie) do { } while (0)

#include <asm/sigcontext.h>

#endif /* __KERNEL__ */
#endif /* !__ASSEMBLY */
#endif /* _ASM_PARISC_SIGNAL_H */
