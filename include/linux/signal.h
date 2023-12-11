/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SIGNAL_H
#define _LINUX_SIGNAL_H

#include <linux/bug.h>
#include <linux/list.h>
#include <linux/signal_types.h>
#include <linux/string.h>

struct task_struct;

/* for sysctl */
extern int print_fatal_signals;

static inline void copy_siginfo(kernel_siginfo_t *to,
				const kernel_siginfo_t *from)
{
	memcpy(to, from, sizeof(*to));
}

static inline void clear_siginfo(kernel_siginfo_t *info)
{
	memset(info, 0, sizeof(*info));
}

#define SI_EXPANSION_SIZE (sizeof(struct siginfo) - sizeof(struct kernel_siginfo))

static inline void copy_siginfo_to_external(siginfo_t *to,
					    const kernel_siginfo_t *from)
{
	memcpy(to, from, sizeof(*from));
	memset(((char *)to) + sizeof(struct kernel_siginfo), 0,
		SI_EXPANSION_SIZE);
}

int copy_siginfo_to_user(siginfo_t __user *to, const kernel_siginfo_t *from);
int copy_siginfo_from_user(kernel_siginfo_t *to, const siginfo_t __user *from);

enum siginfo_layout {
	SIL_KILL,
	SIL_TIMER,
	SIL_POLL,
	SIL_FAULT,
	SIL_FAULT_TRAPNO,
	SIL_FAULT_MCEERR,
	SIL_FAULT_BNDERR,
	SIL_FAULT_PKUERR,
	SIL_FAULT_PERF_EVENT,
	SIL_CHLD,
	SIL_RT,
	SIL_SYS,
};

enum siginfo_layout siginfo_layout(unsigned sig, int si_code);

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

#endif /* __HAVE_ARCH_SIG_BITOPS */

static inline int sigisemptyset(sigset_t *set)
{
	switch (_NSIG_WORDS) {
	case 4:
		return (set->sig[3] | set->sig[2] |
			set->sig[1] | set->sig[0]) == 0;
	case 2:
		return (set->sig[1] | set->sig[0]) == 0;
	case 1:
		return set->sig[0] == 0;
	default:
		BUILD_BUG();
		return 0;
	}
}

static inline int sigequalsets(const sigset_t *set1, const sigset_t *set2)
{
	switch (_NSIG_WORDS) {
	case 4:
		return	(set1->sig[3] == set2->sig[3]) &&
			(set1->sig[2] == set2->sig[2]) &&
			(set1->sig[1] == set2->sig[1]) &&
			(set1->sig[0] == set2->sig[0]);
	case 2:
		return	(set1->sig[1] == set2->sig[1]) &&
			(set1->sig[0] == set2->sig[0]);
	case 1:
		return	set1->sig[0] == set2->sig[0];
	}
	return 0;
}

#define sigmask(sig)	(1UL << ((sig) - 1))

#ifndef __HAVE_ARCH_SIG_SETOPS

#define _SIG_SET_BINOP(name, op)					\
static inline void name(sigset_t *r, const sigset_t *a, const sigset_t *b) \
{									\
	unsigned long a0, a1, a2, a3, b0, b1, b2, b3;			\
									\
	switch (_NSIG_WORDS) {						\
	case 4:								\
		a3 = a->sig[3]; a2 = a->sig[2];				\
		b3 = b->sig[3]; b2 = b->sig[2];				\
		r->sig[3] = op(a3, b3);					\
		r->sig[2] = op(a2, b2);					\
		fallthrough;						\
	case 2:								\
		a1 = a->sig[1]; b1 = b->sig[1];				\
		r->sig[1] = op(a1, b1);					\
		fallthrough;						\
	case 1:								\
		a0 = a->sig[0]; b0 = b->sig[0];				\
		r->sig[0] = op(a0, b0);					\
		break;							\
	default:							\
		BUILD_BUG();						\
	}								\
}

#define _sig_or(x,y)	((x) | (y))
_SIG_SET_BINOP(sigorsets, _sig_or)

#define _sig_and(x,y)	((x) & (y))
_SIG_SET_BINOP(sigandsets, _sig_and)

#define _sig_andn(x,y)	((x) & ~(y))
_SIG_SET_BINOP(sigandnsets, _sig_andn)

#undef _SIG_SET_BINOP
#undef _sig_or
#undef _sig_and
#undef _sig_andn

#define _SIG_SET_OP(name, op)						\
static inline void name(sigset_t *set)					\
{									\
	switch (_NSIG_WORDS) {						\
	case 4:	set->sig[3] = op(set->sig[3]);				\
		set->sig[2] = op(set->sig[2]);				\
		fallthrough;						\
	case 2:	set->sig[1] = op(set->sig[1]);				\
		fallthrough;						\
	case 1:	set->sig[0] = op(set->sig[0]);				\
		    break;						\
	default:							\
		BUILD_BUG();						\
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
		fallthrough;
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
		fallthrough;
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
		break;
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
		break;
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

struct timespec;
struct pt_regs;
enum pid_type;

extern int next_signal(struct sigpending *pending, sigset_t *mask);
extern int do_send_sig_info(int sig, struct kernel_siginfo *info,
				struct task_struct *p, enum pid_type type);
extern int group_send_sig_info(int sig, struct kernel_siginfo *info,
			       struct task_struct *p, enum pid_type type);
extern int send_signal_locked(int sig, struct kernel_siginfo *info,
			      struct task_struct *p, enum pid_type type);
extern int sigprocmask(int, sigset_t *, sigset_t *);
extern void set_current_blocked(sigset_t *);
extern void __set_current_blocked(const sigset_t *);
extern int show_unhandled_signals;

extern bool get_signal(struct ksignal *ksig);
extern void signal_setup_done(int failed, struct ksignal *ksig, int stepping);
extern void exit_signals(struct task_struct *tsk);
extern void kernel_sigaction(int, __sighandler_t);

#define SIG_KTHREAD ((__force __sighandler_t)2)
#define SIG_KTHREAD_KERNEL ((__force __sighandler_t)3)

static inline void allow_signal(int sig)
{
	/*
	 * Kernel threads handle their own signals. Let the signal code
	 * know it'll be handled, so that they don't get converted to
	 * SIGKILL or just silently dropped.
	 */
	kernel_sigaction(sig, SIG_KTHREAD);
}

static inline void allow_kernel_signal(int sig)
{
	/*
	 * Kernel threads handle their own signals. Let the signal code
	 * know signals sent by the kernel will be handled, so that they
	 * don't get silently dropped.
	 */
	kernel_sigaction(sig, SIG_KTHREAD_KERNEL);
}

static inline void disallow_signal(int sig)
{
	kernel_sigaction(sig, SIG_IGN);
}

extern struct kmem_cache *sighand_cachep;

extern bool unhandled_signal(struct task_struct *tsk, int sig);

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

#define siginmask(sig, mask) \
	((sig) > 0 && (sig) < SIGRTMIN && (rt_sigmask(sig) & (mask)))

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

#define SIG_SPECIFIC_SICODES_MASK (\
	rt_sigmask(SIGILL)    |  rt_sigmask(SIGFPE)    | \
	rt_sigmask(SIGSEGV)   |  rt_sigmask(SIGBUS)    | \
	rt_sigmask(SIGTRAP)   |  rt_sigmask(SIGCHLD)   | \
	rt_sigmask(SIGPOLL)   |  rt_sigmask(SIGSYS)    | \
	SIGEMT_MASK                                    )

#define sig_kernel_only(sig)		siginmask(sig, SIG_KERNEL_ONLY_MASK)
#define sig_kernel_coredump(sig)	siginmask(sig, SIG_KERNEL_COREDUMP_MASK)
#define sig_kernel_ignore(sig)		siginmask(sig, SIG_KERNEL_IGNORE_MASK)
#define sig_kernel_stop(sig)		siginmask(sig, SIG_KERNEL_STOP_MASK)
#define sig_specific_sicodes(sig)	siginmask(sig, SIG_SPECIFIC_SICODES_MASK)

#define sig_fatal(t, signr) \
	(!siginmask(signr, SIG_KERNEL_IGNORE_MASK|SIG_KERNEL_STOP_MASK) && \
	 (t)->sighand->action[(signr)-1].sa.sa_handler == SIG_DFL)

void signals_init(void);

int restore_altstack(const stack_t __user *);
int __save_altstack(stack_t __user *, unsigned long);

#define unsafe_save_altstack(uss, sp, label) do { \
	stack_t __user *__uss = uss; \
	struct task_struct *t = current; \
	unsafe_put_user((void __user *)t->sas_ss_sp, &__uss->ss_sp, label); \
	unsafe_put_user(t->sas_ss_flags, &__uss->ss_flags, label); \
	unsafe_put_user(t->sas_ss_size, &__uss->ss_size, label); \
} while (0);

#ifdef CONFIG_DYNAMIC_SIGFRAME
bool sigaltstack_size_valid(size_t ss_size);
#else
static inline bool sigaltstack_size_valid(size_t size) { return true; }
#endif /* !CONFIG_DYNAMIC_SIGFRAME */

#ifdef CONFIG_PROC_FS
struct seq_file;
extern void render_sigset_t(struct seq_file *, const char *, sigset_t *);
#endif

#ifndef arch_untagged_si_addr
/*
 * Given a fault address and a signal and si_code which correspond to the
 * _sigfault union member, returns the address that must appear in si_addr if
 * the signal handler does not have SA_EXPOSE_TAGBITS enabled in sa_flags.
 */
static inline void __user *arch_untagged_si_addr(void __user *addr,
						 unsigned long sig,
						 unsigned long si_code)
{
	return addr;
}
#endif

#endif /* _LINUX_SIGNAL_H */
