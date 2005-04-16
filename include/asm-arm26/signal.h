#ifndef _ASMARM_SIGNAL_H
#define _ASMARM_SIGNAL_H

#include <linux/types.h>

/* Avoid too many header ordering problems.  */
struct siginfo;

#ifdef __KERNEL__
/* Most things should be clean enough to redefine this at will, if care
   is taken to make libc match.  */

#define _NSIG		64
#define _NSIG_BPW	32
#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

typedef unsigned long old_sigset_t;		/* at least 32 bits */

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

#else
/* Here we must cater to libcs that poke about in kernel headers.  */

#define NSIG		32
typedef unsigned long sigset_t;

#endif /* __KERNEL__ */

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
#define	SIGUNUSED	31

/* These should not be considered constants from userland.  */
#define SIGRTMIN	32
#define SIGRTMAX	_NSIG

#define SIGSWI		32

/*
 * SA_FLAGS values:
 *
 * SA_NOCLDSTOP		flag to turn off SIGCHLD when children stop.
 * SA_NOCLDWAIT		flag on SIGCHLD to inhibit zombies.
 * SA_SIGINFO		deliver the signal with SIGINFO structs
 * SA_THIRTYTWO		delivers the signal in 32-bit mode, even if the task 
 *			is running in 26-bit.
 * SA_ONSTACK		allows alternate signal stacks (see sigaltstack(2)).
 * SA_RESTART		flag to get restarting signals (which were the default long ago)
 * SA_INTERRUPT		is a no-op, but left due to historical reasons. Use the
 * SA_NODEFER		prevents the current signal from being masked in the handler.
 * SA_RESETHAND		clears the handler when the signal is delivered.
 *
 * SA_ONESHOT and SA_NOMASK are the historical Linux names for the Single
 * Unix names RESETHAND and NODEFER respectively.
 */
#define SA_NOCLDSTOP	0x00000001
#define SA_NOCLDWAIT	0x00000002 /* not supported yet */
#define SA_SIGINFO	0x00000004
#define SA_THIRTYTWO	0x02000000
#define SA_RESTORER	0x04000000
#define SA_ONSTACK	0x08000000
#define SA_RESTART	0x10000000
#define SA_NODEFER	0x40000000
#define SA_RESETHAND	0x80000000

#define SA_NOMASK	SA_NODEFER
#define SA_ONESHOT	SA_RESETHAND
#define SA_INTERRUPT	0x20000000 /* dummy -- ignored */


/* 
 * sigaltstack controls
 */
#define SS_ONSTACK	1
#define SS_DISABLE	2

#define MINSIGSTKSZ	2048
#define SIGSTKSZ	8192

#ifdef __KERNEL__

/*
 * These values of sa_flags are used only by the kernel as part of the
 * irq handling routines.
 *
 * SA_INTERRUPT is also used by the irq handling routines.
 * SA_SHIRQ is for shared interrupt support on PCI and EISA.
 */
#define SA_PROBE		0x80000000
#define SA_SAMPLE_RANDOM	0x10000000
#define SA_IRQNOMASK		0x08000000
#define SA_SHIRQ		0x04000000
#endif

#define SIG_BLOCK          0	/* for blocking signals */
#define SIG_UNBLOCK        1	/* for unblocking signals */
#define SIG_SETMASK        2	/* for setting the signal mask */

/* Type of a signal handler.  */
typedef void (*__sighandler_t)(int);

#define SIG_DFL	((__sighandler_t)0)	/* default signal handling */
#define SIG_IGN	((__sighandler_t)1)	/* ignore signal */
#define SIG_ERR	((__sighandler_t)-1)	/* error return from signal */

#ifdef __KERNEL__
struct old_sigaction {
	__sighandler_t sa_handler;
	old_sigset_t sa_mask;
	unsigned long sa_flags;
	void (*sa_restorer)(void);
};

struct sigaction {
	__sighandler_t sa_handler;
	unsigned long sa_flags;
	void (*sa_restorer)(void);
	sigset_t sa_mask;		/* mask last for extensibility */
};

struct k_sigaction {
	struct sigaction sa;
};

#else
/* Here we must cater to libcs that poke about in kernel headers.  */

struct sigaction {
	union {
	  __sighandler_t _sa_handler;
	  void (*_sa_sigaction)(int, struct siginfo *, void *);
	} _u;
	sigset_t sa_mask;
	unsigned long sa_flags;
	void (*sa_restorer)(void);
};

#define sa_handler	_u._sa_handler
#define sa_sigaction	_u._sa_sigaction

#endif /* __KERNEL__ */

typedef struct sigaltstack {
	void *ss_sp;
	int ss_flags;
	size_t ss_size;
} stack_t;

#ifdef __KERNEL__
#include <asm/sigcontext.h>

#define sigmask(sig)	(1UL << ((sig) - 1))
//FIXME!!!
//#define HAVE_ARCH_GET_SIGNAL_TO_DELIVER

#endif


#ifdef __KERNEL__
#include <asm/sigcontext.h>
#define ptrace_signal_deliver(regs, cookie) do { } while (0)
#endif


#endif
