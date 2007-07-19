#ifndef _LINUX_SIGNAL_H
#define _LINUX_SIGNAL_H

#include <asm/signal.h>
#include <asm/siginfo.h>

#ifdef __KERNEL__
#include <linux/list.h>

/*
 * Real Time signals may be queued.
 */

struct sigqueue {
	struct list_head list;
	int flags;
	siginfo_t info;
	struct user_struct *user;
};

/* flags values. */
#define SIGQUEUE_PREALLOC	1

struct sigpending {
	struct list_head list;
	sigset_t signal;
};

/*
 * Define some primitives to manipulate sigset_t.
 */

#ifndef __HAVE_ARCH_SIG_BITOPS
#include <linux/bitops.h>

/* We don't use <linux/bitops.h> for these because there is no need to
   be atomic.  */
static inline void sigaddset(sigset_t *set, int _sig)
{
	unsigned long sig = _sig - 1;
	if (_NSIG_WORDS == 1)
		set->sig[0] |= 1UL << sig;
	else
		set->sig[sig / _NSIG_BPW] |= 1UL << (sig % _NSIG_BPW);
}

static inline void sigdelset(sigset_t *set, int _sig)
{
	unsigned long sig = _sig - 1;
	if (_NSIG_WORDS == 1)
		set->sig[0] &= ~(1UL << sig);
	else
		set->sig[sig / _NSIG_BPW] &= ~(1UL << (sig % _NSIG_BPW));
}

static inline int sigismember(sigset_t *set, int _sig)
{
	unsigned long sig = _sig - 1;
	if (_NSIG_WORDS == 1)
		return 1 & (set->sig[0] >> sig);
	else
		return 1 & (set->sig[sig / _NSIG_BPW] >> (sig % _NSIG_BPW));
}

static inline int sigfindinword(unsigned long word)
{
	return ffz(~word);
}

#endif /* __HAVE_ARCH_SIG_BITOPS */

static inline int sigisemptyset(sigset_t *set)
{
	extern void _NSIG_WORDS_is_unsupported_size(void);
	switch (_NSIG_WORDS) {
	case 4:
		return (set->sig[3] | set->sig[2] |
			set->sig[1] | set->sig[0]) == 0;
	case 2:
		return (set->sig[1] | set->sig[0]) == 0;
	case 1:
		return set->sig[0] == 0;
	default:
		_NSIG_WORDS_is_unsupported_size();
		return 0;
	}
}

#define sigmask(sig)	(1UL << ((sig) - 1))

#ifndef __HAVE_ARCH_SIG_SETOPS
#include <linux/string.h>

#define _SIG_SET_BINOP(name, op)					\
static inline void name(sigset_t *r, const sigset_t *a, const sigset_t *b) \
{									\
	extern void _NSIG_WORDS_is_unsupported_size(void);		\
	unsigned long a0, a1, a2, a3, b0, b1, b2, b3;			\
									\
	switch (_NSIG_WORDS) {						\
	    case 4:							\
		a3 = a->sig[3]; a2 = a->sig[2];				\
		b3 = b->sig[3]; b2 = b->sig[2];				\
		r->sig[3] = op(a3, b3);					\
		r->sig[2] = op(a2, b2);					\
	    case 2:							\
		a1 = a->sig[1]; b1 = b->sig[1];				\
		r->sig[1] = op(a1, b1);					\
	    case 1:							\
		a0 = a->sig[0]; b0 = b->sig[0];				\
		r->sig[0] = op(a0, b0);					\
		break;							\
	    default:							\
		_NSIG_WORDS_is_unsupported_size();			\
	}								\
}

#define _sig_or(x,y)	((x) | (y))
_SIG_SET_BINOP(sigorsets, _sig_or)

#define _sig_and(x,y)	((x) & (y))
_SIG_SET_BINOP(sigandsets, _sig_and)

#define _sig_nand(x,y)	((x) & ~(y))
_SIG_SET_BINOP(signandsets, _sig_nand)

#undef _SIG_SET_BINOP
#undef _sig_or
#undef _sig_and
#undef _sig_nand

#define _SIG_SET_OP(name, op)						\
static inline void name(sigset_t *set)					\
{									\
	extern void _NSIG_WORDS_is_unsupported_size(void);		\
									\
	switch (_NSIG_WORDS) {						\
	    case 4: set->sig[3] = op(set->sig[3]);			\
		    set->sig[2] = op(set->sig[2]);			\
	    case 2: set->sig[1] = op(set->sig[1]);			\
	    case 1: set->sig[0] = op(set->sig[0]);			\
		    break;						\
	    default:							\
		_NSIG_WORDS_is_unsupported_size();			\
	}								\
}

#define _sig_not(x)	(~(x))
_SIG_SET_OP(signotset, _sig_not)

#undef _SIG_SET_OP
#undef _sig_not

static inline void sigemptyset(sigset_t *set)
{
	switch (_NSIG_WORDS) {
	default:
		memset(set, 0, sizeof(sigset_t));
		break;
	case 2: set->sig[1] = 0;
	case 1:	set->sig[0] = 0;
		break;
	}
}

static inline void sigfillset(sigset_t *set)
{
	switch (_NSIG_WORDS) {
	default:
		memset(set, -1, sizeof(sigset_t));
		break;
	case 2: set->sig[1] = -1;
	case 1:	set->sig[0] = -1;
		break;
	}
}

/* Some extensions for manipulating the low 32 signals in particular.  */

static inline void sigaddsetmask(sigset_t *set, unsigned long mask)
{
	set->sig[0] |= mask;
}

static inline void sigdelsetmask(sigset_t *set, unsigned long mask)
{
	set->sig[0] &= ~mask;
}

static inline int sigtestsetmask(sigset_t *set, unsigned long mask)
{
	return (set->sig[0] & mask) != 0;
}

static inline void siginitset(sigset_t *set, unsigned long mask)
{
	set->sig[0] = mask;
	switch (_NSIG_WORDS) {
	default:
		memset(&set->sig[1], 0, sizeof(long)*(_NSIG_WORDS-1));
		break;
	case 2: set->sig[1] = 0;
	case 1: ;
	}
}

static inline void siginitsetinv(sigset_t *set, unsigned long mask)
{
	set->sig[0] = ~mask;
	switch (_NSIG_WORDS) {
	default:
		memset(&set->sig[1], -1, sizeof(long)*(_NSIG_WORDS-1));
		break;
	case 2: set->sig[1] = -1;
	case 1: ;
	}
}

#endif /* __HAVE_ARCH_SIG_SETOPS */

static inline void init_sigpending(struct sigpending *sig)
{
	sigemptyset(&sig->signal);
	INIT_LIST_HEAD(&sig->list);
}

extern void flush_sigqueue(struct sigpending *queue);

/* Test if 'sig' is valid signal. Use this instead of testing _NSIG directly */
static inline int valid_signal(unsigned long sig)
{
	return sig <= _NSIG ? 1 : 0;
}

extern int next_signal(struct sigpending *pending, sigset_t *mask);
extern int group_send_sig_info(int sig, struct siginfo *info, struct task_struct *p);
extern int __group_send_sig_info(int, struct siginfo *, struct task_struct *);
extern long do_sigpending(void __user *, unsigned long);
extern int sigprocmask(int, sigset_t *, sigset_t *);

struct pt_regs;
extern int get_signal_to_deliver(siginfo_t *info, struct k_sigaction *return_ka, struct pt_regs *regs, void *cookie);

extern struct kmem_cache *sighand_cachep;

/*
 * In POSIX a signal is sent either to a specific thread (Linux task)
 * or to the process as a whole (Linux thread group).  How the signal
 * is sent determines whether it's to one thread or the whole group,
 * which determines which signal mask(s) are involved in blocking it
 * from being delivered until later.  When the signal is delivered,
 * either it's caught or ignored by a user handler or it has a default
 * effect that applies to the whole thread group (POSIX process).
 *
 * The possible effects an unblocked signal set to SIG_DFL can have are:
 *   ignore	- Nothing Happens
 *   terminate	- kill the process, i.e. all threads in the group,
 * 		  similar to exit_group.  The group leader (only) reports
 *		  WIFSIGNALED status to its parent.
 *   coredump	- write a core dump file describing all threads using
 *		  the same mm and then kill all those threads
 *   stop 	- stop all the threads in the group, i.e. TASK_STOPPED state
 *
 * SIGKILL and SIGSTOP cannot be caught, blocked, or ignored.
 * Other signals when not blocked and set to SIG_DFL behaves as follows.
 * The job control signals also have other special effects.
 *
 *	+--------------------+------------------+
 *	|  POSIX signal      |  default action  |
 *	+--------------------+------------------+
 *	|  SIGHUP            |  terminate	|
 *	|  SIGINT            |	terminate	|
 *	|  SIGQUIT           |	coredump 	|
 *	|  SIGILL            |	coredump 	|
 *	|  SIGTRAP           |	coredump 	|
 *	|  SIGABRT/SIGIOT    |	coredump 	|
 *	|  SIGBUS            |	coredump 	|
 *	|  SIGFPE            |	coredump 	|
 *	|  SIGKILL           |	terminate(+)	|
 *	|  SIGUSR1           |	terminate	|
 *	|  SIGSEGV           |	coredump 	|
 *	|  SIGUSR2           |	terminate	|
 *	|  SIGPIPE           |	terminate	|
 *	|  SIGALRM           |	terminate	|
 *	|  SIGTERM           |	terminate	|
 *	|  SIGCHLD           |	ignore   	|
 *	|  SIGCONT           |	ignore(*)	|
 *	|  SIGSTOP           |	stop(*)(+)  	|
 *	|  SIGTSTP           |	stop(*)  	|
 *	|  SIGTTIN           |	stop(*)  	|
 *	|  SIGTTOU           |	stop(*)  	|
 *	|  SIGURG            |	ignore   	|
 *	|  SIGXCPU           |	coredump 	|
 *	|  SIGXFSZ           |	coredump 	|
 *	|  SIGVTALRM         |	terminate	|
 *	|  SIGPROF           |	terminate	|
 *	|  SIGPOLL/SIGIO     |	terminate	|
 *	|  SIGSYS/SIGUNUSED  |	coredump 	|
 *	|  SIGSTKFLT         |	terminate	|
 *	|  SIGWINCH          |	ignore   	|
 *	|  SIGPWR            |	terminate	|
 *	|  SIGRTMIN-SIGRTMAX |	terminate       |
 *	+--------------------+------------------+
 *	|  non-POSIX signal  |  default action  |
 *	+--------------------+------------------+
 *	|  SIGEMT            |  coredump	|
 *	+--------------------+------------------+
 *
 * (+) For SIGKILL and SIGSTOP the action is "always", not just "default".
 * (*) Special job control effects:
 * When SIGCONT is sent, it resumes the process (all threads in the group)
 * from TASK_STOPPED state and also clears any pending/queued stop signals
 * (any of those marked with "stop(*)").  This happens regardless of blocking,
 * catching, or ignoring SIGCONT.  When any stop signal is sent, it clears
 * any pending/queued SIGCONT signals; this happens regardless of blocking,
 * catching, or ignored the stop signal, though (except for SIGSTOP) the
 * default action of stopping the process may happen later or never.
 */

#ifdef SIGEMT
#define SIGEMT_MASK	rt_sigmask(SIGEMT)
#else
#define SIGEMT_MASK	0
#endif

#if SIGRTMIN > BITS_PER_LONG
#define rt_sigmask(sig)	(1ULL << ((sig)-1))
#else
#define rt_sigmask(sig)	sigmask(sig)
#endif
#define siginmask(sig, mask) (rt_sigmask(sig) & (mask))

#define SIG_KERNEL_ONLY_MASK (\
	rt_sigmask(SIGKILL)   |  rt_sigmask(SIGSTOP))

#define SIG_KERNEL_STOP_MASK (\
	rt_sigmask(SIGSTOP)   |  rt_sigmask(SIGTSTP)   | \
	rt_sigmask(SIGTTIN)   |  rt_sigmask(SIGTTOU)   )

#define SIG_KERNEL_COREDUMP_MASK (\
        rt_sigmask(SIGQUIT)   |  rt_sigmask(SIGILL)    | \
	rt_sigmask(SIGTRAP)   |  rt_sigmask(SIGABRT)   | \
        rt_sigmask(SIGFPE)    |  rt_sigmask(SIGSEGV)   | \
	rt_sigmask(SIGBUS)    |  rt_sigmask(SIGSYS)    | \
        rt_sigmask(SIGXCPU)   |  rt_sigmask(SIGXFSZ)   | \
	SIGEMT_MASK				       )

#define SIG_KERNEL_IGNORE_MASK (\
        rt_sigmask(SIGCONT)   |  rt_sigmask(SIGCHLD)   | \
	rt_sigmask(SIGWINCH)  |  rt_sigmask(SIGURG)    )

#define sig_kernel_only(sig) \
	(((sig) < SIGRTMIN) && siginmask(sig, SIG_KERNEL_ONLY_MASK))
#define sig_kernel_coredump(sig) \
	(((sig) < SIGRTMIN) && siginmask(sig, SIG_KERNEL_COREDUMP_MASK))
#define sig_kernel_ignore(sig) \
	(((sig) < SIGRTMIN) && siginmask(sig, SIG_KERNEL_IGNORE_MASK))
#define sig_kernel_stop(sig) \
	(((sig) < SIGRTMIN) && siginmask(sig, SIG_KERNEL_STOP_MASK))

#define sig_needs_tasklist(sig)	((sig) == SIGCONT)

#define sig_user_defined(t, signr) \
	(((t)->sighand->action[(signr)-1].sa.sa_handler != SIG_DFL) &&	\
	 ((t)->sighand->action[(signr)-1].sa.sa_handler != SIG_IGN))

#define sig_fatal(t, signr) \
	(!siginmask(signr, SIG_KERNEL_IGNORE_MASK|SIG_KERNEL_STOP_MASK) && \
	 (t)->sighand->action[(signr)-1].sa.sa_handler == SIG_DFL)

#endif /* __KERNEL__ */

#endif /* _LINUX_SIGNAL_H */
