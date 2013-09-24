/*
 *  linux/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-02  Modified for POSIX.1b signals by Richard Henderson
 *
 *  2003-06-02  Jim Houston - Concurrent Computer Corp.
 *		Changes to use preallocated sigqueue structures
 *		to allow signals to be sent reliably.
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/binfmts.h>
#include <linux/coredump.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/ptrace.h>
#include <linux/signal.h>
#include <linux/signalfd.h>
#include <linux/ratelimit.h>
#include <linux/tracehook.h>
#include <linux/capability.h>
#include <linux/freezer.h>
#include <linux/pid_namespace.h>
#include <linux/nsproxy.h>
#include <linux/user_namespace.h>
#include <linux/uprobes.h>
#include <linux/compat.h>
#include <linux/cn_proc.h>
#define CREATE_TRACE_POINTS
#include <trace/events/signal.h>

#include <asm/param.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/siginfo.h>
#include <asm/cacheflush.h>
#include "audit.h"	/* audit_signal_info() */

/*
 * SLAB caches for signal bits.
 */

static struct kmem_cache *sigqueue_cachep;

int print_fatal_signals __read_mostly;

static void __user *sig_handler(struct task_struct *t, int sig)
{
	return t->sighand->action[sig - 1].sa.sa_handler;
}

static int sig_handler_ignored(void __user *handler, int sig)
{
	/* Is it explicitly or implicitly ignored? */
	return handler == SIG_IGN ||
		(handler == SIG_DFL && sig_kernel_ignore(sig));
}

static int sig_task_ignored(struct task_struct *t, int sig, bool force)
{
	void __user *handler;

	handler = sig_handler(t, sig);

	if (unlikely(t->signal->flags & SIGNAL_UNKILLABLE) &&
			handler == SIG_DFL && !force)
		return 1;

	return sig_handler_ignored(handler, sig);
}

static int sig_ignored(struct task_struct *t, int sig, bool force)
{
	/*
	 * Blocked signals are never ignored, since the
	 * signal handler may change by the time it is
	 * unblocked.
	 */
	if (sigismember(&t->blocked, sig) || sigismember(&t->real_blocked, sig))
		return 0;

	if (!sig_task_ignored(t, sig, force))
		return 0;

	/*
	 * Tracers may want to know about even ignored signals.
	 */
	return !t->ptrace;
}

/*
 * Re-calculate pending state from the set of locally pending
 * signals, globally pending signals, and blocked signals.
 */
static inline int has_pending_signals(sigset_t *signal, sigset_t *blocked)
{
	unsigned long ready;
	long i;

	switch (_NSIG_WORDS) {
	default:
		for (i = _NSIG_WORDS, ready = 0; --i >= 0 ;)
			ready |= signal->sig[i] &~ blocked->sig[i];
		break;

	case 4: ready  = signal->sig[3] &~ blocked->sig[3];
		ready |= signal->sig[2] &~ blocked->sig[2];
		ready |= signal->sig[1] &~ blocked->sig[1];
		ready |= signal->sig[0] &~ blocked->sig[0];
		break;

	case 2: ready  = signal->sig[1] &~ blocked->sig[1];
		ready |= signal->sig[0] &~ blocked->sig[0];
		break;

	case 1: ready  = signal->sig[0] &~ blocked->sig[0];
	}
	return ready !=	0;
}

#define PENDING(p,b) has_pending_signals(&(p)->signal, (b))

static int recalc_sigpending_tsk(struct task_struct *t)
{
	if ((t->jobctl & JOBCTL_PENDING_MASK) ||
	    PENDING(&t->pending, &t->blocked) ||
	    PENDING(&t->signal->shared_pending, &t->blocked)) {
		set_tsk_thread_flag(t, TIF_SIGPENDING);
		return 1;
	}
	/*
	 * We must never clear the flag in another thread, or in current
	 * when it's possible the current syscall is returning -ERESTART*.
	 * So we don't clear it here, and only callers who know they should do.
	 */
	return 0;
}

/*
 * After recalculating TIF_SIGPENDING, we need to make sure the task wakes up.
 * This is superfluous when called on current, the wakeup is a harmless no-op.
 */
void recalc_sigpending_and_wake(struct task_struct *t)
{
	if (recalc_sigpending_tsk(t))
		signal_wake_up(t, 0);
}

void recalc_sigpending(void)
{
	if (!recalc_sigpending_tsk(current) && !freezing(current))
		clear_thread_flag(TIF_SIGPENDING);

}

/* Given the mask, find the first available signal that should be serviced. */

#define SYNCHRONOUS_MASK \
	(sigmask(SIGSEGV) | sigmask(SIGBUS) | sigmask(SIGILL) | \
	 sigmask(SIGTRAP) | sigmask(SIGFPE) | sigmask(SIGSYS))

int next_signal(struct sigpending *pending, sigset_t *mask)
{
	unsigned long i, *s, *m, x;
	int sig = 0;

	s = pending->signal.sig;
	m = mask->sig;

	/*
	 * Handle the first word specially: it contains the
	 * synchronous signals that need to be dequeued first.
	 */
	x = *s &~ *m;
	if (x) {
		if (x & SYNCHRONOUS_MASK)
			x &= SYNCHRONOUS_MASK;
		sig = ffz(~x) + 1;
		return sig;
	}

	switch (_NSIG_WORDS) {
	default:
		for (i = 1; i < _NSIG_WORDS; ++i) {
			x = *++s &~ *++m;
			if (!x)
				continue;
			sig = ffz(~x) + i*_NSIG_BPW + 1;
			break;
		}
		break;

	case 2:
		x = s[1] &~ m[1];
		if (!x)
			break;
		sig = ffz(~x) + _NSIG_BPW + 1;
		break;

	case 1:
		/* Nothing to do */
		break;
	}

	return sig;
}

static inline void print_dropped_signal(int sig)
{
	static DEFINE_RATELIMIT_STATE(ratelimit_state, 5 * HZ, 10);

	if (!print_fatal_signals)
		return;

	if (!__ratelimit(&ratelimit_state))
		return;

	printk(KERN_INFO "%s/%d: reached RLIMIT_SIGPENDING, dropped signal %d\n",
				current->comm, current->pid, sig);
}

/**
 * task_set_jobctl_pending - set jobctl pending bits
 * @task: target task
 * @mask: pending bits to set
 *
 * Clear @mask from @task->jobctl.  @mask must be subset of
 * %JOBCTL_PENDING_MASK | %JOBCTL_STOP_CONSUME | %JOBCTL_STOP_SIGMASK |
 * %JOBCTL_TRAPPING.  If stop signo is being set, the existing signo is
 * cleared.  If @task is already being killed or exiting, this function
 * becomes noop.
 *
 * CONTEXT:
 * Must be called with @task->sighand->siglock held.
 *
 * RETURNS:
 * %true if @mask is set, %false if made noop because @task was dying.
 */
bool task_set_jobctl_pending(struct task_struct *task, unsigned int mask)
{
	BUG_ON(mask & ~(JOBCTL_PENDING_MASK | JOBCTL_STOP_CONSUME |
			JOBCTL_STOP_SIGMASK | JOBCTL_TRAPPING));
	BUG_ON((mask & JOBCTL_TRAPPING) && !(mask & JOBCTL_PENDING_MASK));

	if (unlikely(fatal_signal_pending(task) || (task->flags & PF_EXITING)))
		return false;

	if (mask & JOBCTL_STOP_SIGMASK)
		task->jobctl &= ~JOBCTL_STOP_SIGMASK;

	task->jobctl |= mask;
	return true;
}

/**
 * task_clear_jobctl_trapping - clear jobctl trapping bit
 * @task: target task
 *
 * If JOBCTL_TRAPPING is set, a ptracer is waiting for us to enter TRACED.
 * Clear it and wake up the ptracer.  Note that we don't need any further
 * locking.  @task->siglock guarantees that @task->parent points to the
 * ptracer.
 *
 * CONTEXT:
 * Must be called with @task->sighand->siglock held.
 */
void task_clear_jobctl_trapping(struct task_struct *task)
{
	if (unlikely(task->jobctl & JOBCTL_TRAPPING)) {
		task->jobctl &= ~JOBCTL_TRAPPING;
		wake_up_bit(&task->jobctl, JOBCTL_TRAPPING_BIT);
	}
}

/**
 * task_clear_jobctl_pending - clear jobctl pending bits
 * @task: target task
 * @mask: pending bits to clear
 *
 * Clear @mask from @task->jobctl.  @mask must be subset of
 * %JOBCTL_PENDING_MASK.  If %JOBCTL_STOP_PENDING is being cleared, other
 * STOP bits are cleared together.
 *
 * If clearing of @mask leaves no stop or trap pending, this function calls
 * task_clear_jobctl_trapping().
 *
 * CONTEXT:
 * Must be called with @task->sighand->siglock held.
 */
void task_clear_jobctl_pending(struct task_struct *task, unsigned int mask)
{
	BUG_ON(mask & ~JOBCTL_PENDING_MASK);

	if (mask & JOBCTL_STOP_PENDING)
		mask |= JOBCTL_STOP_CONSUME | JOBCTL_STOP_DEQUEUED;

	task->jobctl &= ~mask;

	if (!(task->jobctl & JOBCTL_PENDING_MASK))
		task_clear_jobctl_trapping(task);
}

/**
 * task_participate_group_stop - participate in a group stop
 * @task: task participating in a group stop
 *
 * @task has %JOBCTL_STOP_PENDING set and is participating in a group stop.
 * Group stop states are cleared and the group stop count is consumed if
 * %JOBCTL_STOP_CONSUME was set.  If the consumption completes the group
 * stop, the appropriate %SIGNAL_* flags are set.
 *
 * CONTEXT:
 * Must be called with @task->sighand->siglock held.
 *
 * RETURNS:
 * %true if group stop completion should be notified to the parent, %false
 * otherwise.
 */
static bool task_participate_group_stop(struct task_struct *task)
{
	struct signal_struct *sig = task->signal;
	bool consume = task->jobctl & JOBCTL_STOP_CONSUME;

	WARN_ON_ONCE(!(task->jobctl & JOBCTL_STOP_PENDING));

	task_clear_jobctl_pending(task, JOBCTL_STOP_PENDING);

	if (!consume)
		return false;

	if (!WARN_ON_ONCE(sig->group_stop_count == 0))
		sig->group_stop_count--;

	/*
	 * Tell the caller to notify completion iff we are entering into a
	 * fresh group stop.  Read comment in do_signal_stop() for details.
	 */
	if (!sig->group_stop_count && !(sig->flags & SIGNAL_STOP_STOPPED)) {
		sig->flags = SIGNAL_STOP_STOPPED;
		return true;
	}
	return false;
}

/*
 * allocate a new signal queue record
 * - this may be called without locks if and only if t == current, otherwise an
 *   appropriate lock must be held to stop the target task from exiting
 */
static struct sigqueue *
__sigqueue_alloc(int sig, struct task_struct *t, gfp_t flags, int override_rlimit)
{
	struct sigqueue *q = NULL;
	struct user_struct *user;

	/*
	 * Protect access to @t credentials. This can go away when all
	 * callers hold rcu read lock.
	 */
	rcu_read_lock();
	user = get_uid(__task_cred(t)->user);
	atomic_inc(&user->sigpending);
	rcu_read_unlock();

	if (override_rlimit ||
	    atomic_read(&user->sigpending) <=
			task_rlimit(t, RLIMIT_SIGPENDING)) {
		q = kmem_cache_alloc(sigqueue_cachep, flags);
	} else {
		print_dropped_signal(sig);
	}

	if (unlikely(q == NULL)) {
		atomic_dec(&user->sigpending);
		free_uid(user);
	} else {
		INIT_LIST_HEAD(&q->list);
		q->flags = 0;
		q->user = user;
	}

	return q;
}

static void __sigqueue_free(struct sigqueue *q)
{
	if (q->flags & SIGQUEUE_PREALLOC)
		return;
	atomic_dec(&q->user->sigpending);
	free_uid(q->user);
	kmem_cache_free(sigqueue_cachep, q);
}

void flush_sigqueue(struct sigpending *queue)
{
	struct sigqueue *q;

	sigemptyset(&queue->signal);
	while (!list_empty(&queue->list)) {
		q = list_entry(queue->list.next, struct sigqueue , list);
		list_del_init(&q->list);
		__sigqueue_free(q);
	}
}

/*
 * Flush all pending signals for a task.
 */
void __flush_signals(struct task_struct *t)
{
	clear_tsk_thread_flag(t, TIF_SIGPENDING);
	flush_sigqueue(&t->pending);
	flush_sigqueue(&t->signal->shared_pending);
}

void flush_signals(struct task_struct *t)
{
	unsigned long flags;

	spin_lock_irqsave(&t->sighand->siglock, flags);
	__flush_signals(t);
	spin_unlock_irqrestore(&t->sighand->siglock, flags);
}

static void __flush_itimer_signals(struct sigpending *pending)
{
	sigset_t signal, retain;
	struct sigqueue *q, *n;

	signal = pending->signal;
	sigemptyset(&retain);

	list_for_each_entry_safe(q, n, &pending->list, list) {
		int sig = q->info.si_signo;

		if (likely(q->info.si_code != SI_TIMER)) {
			sigaddset(&retain, sig);
		} else {
			sigdelset(&signal, sig);
			list_del_init(&q->list);
			__sigqueue_free(q);
		}
	}

	sigorsets(&pending->signal, &signal, &retain);
}

void flush_itimer_signals(void)
{
	struct task_struct *tsk = current;
	unsigned long flags;

	spin_lock_irqsave(&tsk->sighand->siglock, flags);
	__flush_itimer_signals(&tsk->pending);
	__flush_itimer_signals(&tsk->signal->shared_pending);
	spin_unlock_irqrestore(&tsk->sighand->siglock, flags);
}

void ignore_signals(struct task_struct *t)
{
	int i;

	for (i = 0; i < _NSIG; ++i)
		t->sighand->action[i].sa.sa_handler = SIG_IGN;

	flush_signals(t);
}

/*
 * Flush all handlers for a task.
 */

void
flush_signal_handlers(struct task_struct *t, int force_default)
{
	int i;
	struct k_sigaction *ka = &t->sighand->action[0];
	for (i = _NSIG ; i != 0 ; i--) {
		if (force_default || ka->sa.sa_handler != SIG_IGN)
			ka->sa.sa_handler = SIG_DFL;
		ka->sa.sa_flags = 0;
#ifdef __ARCH_HAS_SA_RESTORER
		ka->sa.sa_restorer = NULL;
#endif
		sigemptyset(&ka->sa.sa_mask);
		ka++;
	}
}

int unhandled_signal(struct task_struct *tsk, int sig)
{
	void __user *handler = tsk->sighand->action[sig-1].sa.sa_handler;
	if (is_global_init(tsk))
		return 1;
	if (handler != SIG_IGN && handler != SIG_DFL)
		return 0;
	/* if ptraced, let the tracer determine */
	return !tsk->ptrace;
}

/*
 * Notify the system that a driver wants to block all signals for this
 * process, and wants to be notified if any signals at all were to be
 * sent/acted upon.  If the notifier routine returns non-zero, then the
 * signal will be acted upon after all.  If the notifier routine returns 0,
 * then then signal will be blocked.  Only one block per process is
 * allowed.  priv is a pointer to private data that the notifier routine
 * can use to determine if the signal should be blocked or not.
 */
void
block_all_signals(int (*notifier)(void *priv), void *priv, sigset_t *mask)
{
	unsigned long flags;

	spin_lock_irqsave(&current->sighand->siglock, flags);
	current->notifier_mask = mask;
	current->notifier_data = priv;
	current->notifier = notifier;
	spin_unlock_irqrestore(&current->sighand->siglock, flags);
}

/* Notify the system that blocking has ended. */

void
unblock_all_signals(void)
{
	unsigned long flags;

	spin_lock_irqsave(&current->sighand->siglock, flags);
	current->notifier = NULL;
	current->notifier_data = NULL;
	recalc_sigpending();
	spin_unlock_irqrestore(&current->sighand->siglock, flags);
}

static void collect_signal(int sig, struct sigpending *list, siginfo_t *info)
{
	struct sigqueue *q, *first = NULL;

	/*
	 * Collect the siginfo appropriate to this signal.  Check if
	 * there is another siginfo for the same signal.
	*/
	list_for_each_entry(q, &list->list, list) {
		if (q->info.si_signo == sig) {
			if (first)
				goto still_pending;
			first = q;
		}
	}

	sigdelset(&list->signal, sig);

	if (first) {
still_pending:
		list_del_init(&first->list);
		copy_siginfo(info, &first->info);
		__sigqueue_free(first);
	} else {
		/*
		 * Ok, it wasn't in the queue.  This must be
		 * a fast-pathed signal or we must have been
		 * out of queue space.  So zero out the info.
		 */
		info->si_signo = sig;
		info->si_errno = 0;
		info->si_code = SI_USER;
		info->si_pid = 0;
		info->si_uid = 0;
	}
}

static int __dequeue_signal(struct sigpending *pending, sigset_t *mask,
			siginfo_t *info)
{
	int sig = next_signal(pending, mask);

	if (sig) {
		if (current->notifier) {
			if (sigismember(current->notifier_mask, sig)) {
				if (!(current->notifier)(current->notifier_data)) {
					clear_thread_flag(TIF_SIGPENDING);
					return 0;
				}
			}
		}

		collect_signal(sig, pending, info);
	}

	return sig;
}

/*
 * Dequeue a signal and return the element to the caller, which is
 * expected to free it.
 *
 * All callers have to hold the siglock.
 */
int dequeue_signal(struct task_struct *tsk, sigset_t *mask, siginfo_t *info)
{
	int signr;

	/* We only dequeue private signals from ourselves, we don't let
	 * signalfd steal them
	 */
	signr = __dequeue_signal(&tsk->pending, mask, info);
	if (!signr) {
		signr = __dequeue_signal(&tsk->signal->shared_pending,
					 mask, info);
		/*
		 * itimer signal ?
		 *
		 * itimers are process shared and we restart periodic
		 * itimers in the signal delivery path to prevent DoS
		 * attacks in the high resolution timer case. This is
		 * compliant with the old way of self-restarting
		 * itimers, as the SIGALRM is a legacy signal and only
		 * queued once. Changing the restart behaviour to
		 * restart the timer in the signal dequeue path is
		 * reducing the timer noise on heavy loaded !highres
		 * systems too.
		 */
		if (unlikely(signr == SIGALRM)) {
			struct hrtimer *tmr = &tsk->signal->real_timer;

			if (!hrtimer_is_queued(tmr) &&
			    tsk->signal->it_real_incr.tv64 != 0) {
				hrtimer_forward(tmr, tmr->base->get_time(),
						tsk->signal->it_real_incr);
				hrtimer_restart(tmr);
			}
		}
	}

	recalc_sigpending();
	if (!signr)
		return 0;

	if (unlikely(sig_kernel_stop(signr))) {
		/*
		 * Set a marker that we have dequeued a stop signal.  Our
		 * caller might release the siglock and then the pending
		 * stop signal it is about to process is no longer in the
		 * pending bitmasks, but must still be cleared by a SIGCONT
		 * (and overruled by a SIGKILL).  So those cases clear this
		 * shared flag after we've set it.  Note that this flag may
		 * remain set after the signal we return is ignored or
		 * handled.  That doesn't matter because its only purpose
		 * is to alert stop-signal processing code when another
		 * processor has come along and cleared the flag.
		 */
		current->jobctl |= JOBCTL_STOP_DEQUEUED;
	}
	if ((info->si_code & __SI_MASK) == __SI_TIMER && info->si_sys_private) {
		/*
		 * Release the siglock to ensure proper locking order
		 * of timer locks outside of siglocks.  Note, we leave
		 * irqs disabled here, since the posix-timers code is
		 * about to disable them again anyway.
		 */
		spin_unlock(&tsk->sighand->siglock);
		do_schedule_next_timer(info);
		spin_lock(&tsk->sighand->siglock);
	}
	return signr;
}

/*
 * Tell a process that it has a new active signal..
 *
 * NOTE! we rely on the previous spin_lock to
 * lock interrupts for us! We can only be called with
 * "siglock" held, and the local interrupt must
 * have been disabled when that got acquired!
 *
 * No need to set need_resched since signal event passing
 * goes through ->blocked
 */
void signal_wake_up_state(struct task_struct *t, unsigned int state)
{
	set_tsk_thread_flag(t, TIF_SIGPENDING);
	/*
	 * TASK_WAKEKILL also means wake it up in the stopped/traced/killable
	 * case. We don't check t->state here because there is a race with it
	 * executing another processor and just now entering stopped state.
	 * By using wake_up_state, we ensure the process will wake up and
	 * handle its death signal.
	 */
	if (!wake_up_state(t, state | TASK_INTERRUPTIBLE))
		kick_process(t);
}

/*
 * Remove signals in mask from the pending set and queue.
 * Returns 1 if any signals were found.
 *
 * All callers must be holding the siglock.
 *
 * This version takes a sigset mask and looks at all signals,
 * not just those in the first mask word.
 */
static int rm_from_queue_full(sigset_t *mask, struct sigpending *s)
{
	struct sigqueue *q, *n;
	sigset_t m;

	sigandsets(&m, mask, &s->signal);
	if (sigisemptyset(&m))
		return 0;

	sigandnsets(&s->signal, &s->signal, mask);
	list_for_each_entry_safe(q, n, &s->list, list) {
		if (sigismember(mask, q->info.si_signo)) {
			list_del_init(&q->list);
			__sigqueue_free(q);
		}
	}
	return 1;
}
/*
 * Remove signals in mask from the pending set and queue.
 * Returns 1 if any signals were found.
 *
 * All callers must be holding the siglock.
 */
static int rm_from_queue(unsigned long mask, struct sigpending *s)
{
	struct sigqueue *q, *n;

	if (!sigtestsetmask(&s->signal, mask))
		return 0;

	sigdelsetmask(&s->signal, mask);
	list_for_each_entry_safe(q, n, &s->list, list) {
		if (q->info.si_signo < SIGRTMIN &&
		    (mask & sigmask(q->info.si_signo))) {
			list_del_init(&q->list);
			__sigqueue_free(q);
		}
	}
	return 1;
}

static inline int is_si_special(const struct siginfo *info)
{
	return info <= SEND_SIG_FORCED;
}

static inline bool si_fromuser(const struct siginfo *info)
{
	return info == SEND_SIG_NOINFO ||
		(!is_si_special(info) && SI_FROMUSER(info));
}

/*
 * called with RCU read lock from check_kill_permission()
 */
static int kill_ok_by_cred(struct task_struct *t)
{
	const struct cred *cred = current_cred();
	const struct cred *tcred = __task_cred(t);

	if (uid_eq(cred->euid, tcred->suid) ||
	    uid_eq(cred->euid, tcred->uid)  ||
	    uid_eq(cred->uid,  tcred->suid) ||
	    uid_eq(cred->uid,  tcred->uid))
		return 1;

	if (ns_capable(tcred->user_ns, CAP_KILL))
		return 1;

	return 0;
}

/*
 * Bad permissions for sending the signal
 * - the caller must hold the RCU read lock
 */
static int check_kill_permission(int sig, struct siginfo *info,
				 struct task_struct *t)
{
	struct pid *sid;
	int error;

	if (!valid_signal(sig))
		return -EINVAL;

	if (!si_fromuser(info))
		return 0;

	error = audit_signal_info(sig, t); /* Let audit system see the signal */
	if (error)
		return error;

	if (!same_thread_group(current, t) &&
	    !kill_ok_by_cred(t)) {
		switch (sig) {
		case SIGCONT:
			sid = task_session(t);
			/*
			 * We don't return the error if sid == NULL. The
			 * task was unhashed, the caller must notice this.
			 */
			if (!sid || sid == task_session(current))
				break;
		default:
			return -EPERM;
		}
	}

	return security_task_kill(t, info, sig, 0);
}

/**
 * ptrace_trap_notify - schedule trap to notify ptracer
 * @t: tracee wanting to notify tracer
 *
 * This function schedules sticky ptrace trap which is cleared on the next
 * TRAP_STOP to notify ptracer of an event.  @t must have been seized by
 * ptracer.
 *
 * If @t is running, STOP trap will be taken.  If trapped for STOP and
 * ptracer is listening for events, tracee is woken up so that it can
 * re-trap for the new event.  If trapped otherwise, STOP trap will be
 * eventually taken without returning to userland after the existing traps
 * are finished by PTRACE_CONT.
 *
 * CONTEXT:
 * Must be called with @task->sighand->siglock held.
 */
static void ptrace_trap_notify(struct task_struct *t)
{
	WARN_ON_ONCE(!(t->ptrace & PT_SEIZED));
	assert_spin_locked(&t->sighand->siglock);

	task_set_jobctl_pending(t, JOBCTL_TRAP_NOTIFY);
	ptrace_signal_wake_up(t, t->jobctl & JOBCTL_LISTENING);
}

/*
 * Handle magic process-wide effects of stop/continue signals. Unlike
 * the signal actions, these happen immediately at signal-generation
 * time regardless of blocking, ignoring, or handling.  This does the
 * actual continuing for SIGCONT, but not the actual stopping for stop
 * signals. The process stop is done as a signal action for SIG_DFL.
 *
 * Returns true if the signal should be actually delivered, otherwise
 * it should be dropped.
 */
static bool prepare_signal(int sig, struct task_struct *p, bool force)
{
	struct signal_struct *signal = p->signal;
	struct task_struct *t;

	if (signal->flags & (SIGNAL_GROUP_EXIT | SIGNAL_GROUP_COREDUMP)) {
		if (signal->flags & SIGNAL_GROUP_COREDUMP)
			return sig == SIGKILL;
		/*
		 * The process is in the middle of dying, nothing to do.
		 */
	} else if (sig_kernel_stop(sig)) {
		/*
		 * This is a stop signal.  Remove SIGCONT from all queues.
		 */
		rm_from_queue(sigmask(SIGCONT), &signal->shared_pending);
		t = p;
		do {
			rm_from_queue(sigmask(SIGCONT), &t->pending);
		} while_each_thread(p, t);
	} else if (sig == SIGCONT) {
		unsigned int why;
		/*
		 * Remove all stop signals from all queues, wake all threads.
		 */
		rm_from_queue(SIG_KERNEL_STOP_MASK, &signal->shared_pending);
		t = p;
		do {
			task_clear_jobctl_pending(t, JOBCTL_STOP_PENDING);
			rm_from_queue(SIG_KERNEL_STOP_MASK, &t->pending);
			if (likely(!(t->ptrace & PT_SEIZED)))
				wake_up_state(t, __TASK_STOPPED);
			else
				ptrace_trap_notify(t);
		} while_each_thread(p, t);

		/*
		 * Notify the parent with CLD_CONTINUED if we were stopped.
		 *
		 * If we were in the middle of a group stop, we pretend it
		 * was already finished, and then continued. Since SIGCHLD
		 * doesn't queue we report only CLD_STOPPED, as if the next
		 * CLD_CONTINUED was dropped.
		 */
		why = 0;
		if (signal->flags & SIGNAL_STOP_STOPPED)
			why |= SIGNAL_CLD_CONTINUED;
		else if (signal->group_stop_count)
			why |= SIGNAL_CLD_STOPPED;

		if (why) {
			/*
			 * The first thread which returns from do_signal_stop()
			 * will take ->siglock, notice SIGNAL_CLD_MASK, and
			 * notify its parent. See get_signal_to_deliver().
			 */
			signal->flags = why | SIGNAL_STOP_CONTINUED;
			signal->group_stop_count = 0;
			signal->group_exit_code = 0;
		}
	}

	return !sig_ignored(p, sig, force);
}

/*
 * Test if P wants to take SIG.  After we've checked all threads with this,
 * it's equivalent to finding no threads not blocking SIG.  Any threads not
 * blocking SIG were ruled out because they are not running and already
 * have pending signals.  Such threads will dequeue from the shared queue
 * as soon as they're available, so putting the signal on the shared queue
 * will be equivalent to sending it to one such thread.
 */
static inline int wants_signal(int sig, struct task_struct *p)
{
	if (sigismember(&p->blocked, sig))
		return 0;
	if (p->flags & PF_EXITING)
		return 0;
	if (sig == SIGKILL)
		return 1;
	if (task_is_stopped_or_traced(p))
		return 0;
	return task_curr(p) || !signal_pending(p);
}

static void complete_signal(int sig, struct task_struct *p, int group)
{
	struct signal_struct *signal = p->signal;
	struct task_struct *t;

	/*
	 * Now find a thread we can wake up to take the signal off the queue.
	 *
	 * If the main thread wants the signal, it gets first crack.
	 * Probably the least surprising to the average bear.
	 */
	if (wants_signal(sig, p))
		t = p;
	else if (!group || thread_group_empty(p))
		/*
		 * There is just one thread and it does not need to be woken.
		 * It will dequeue unblocked signals before it runs again.
		 */
		return;
	else {
		/*
		 * Otherwise try to find a suitable thread.
		 */
		t = signal->curr_target;
		while (!wants_signal(sig, t)) {
			t = next_thread(t);
			if (t == signal->curr_target)
				/*
				 * No thread needs to be woken.
				 * Any eligible threads will see
				 * the signal in the queue soon.
				 */
				return;
		}
		signal->curr_target = t;
	}

	/*
	 * Found a killable thread.  If the signal will be fatal,
	 * then start taking the whole group down immediately.
	 */
	if (sig_fatal(p, sig) &&
	    !(signal->flags & (SIGNAL_UNKILLABLE | SIGNAL_GROUP_EXIT)) &&
	    !sigismember(&t->real_blocked, sig) &&
	    (sig == SIGKILL || !t->ptrace)) {
		/*
		 * This signal will be fatal to the whole group.
		 */
		if (!sig_kernel_coredump(sig)) {
			/*
			 * Start a group exit and wake everybody up.
			 * This way we don't have other threads
			 * running and doing things after a slower
			 * thread has the fatal signal pending.
			 */
			signal->flags = SIGNAL_GROUP_EXIT;
			signal->group_exit_code = sig;
			signal->group_stop_count = 0;
			t = p;
			do {
				task_clear_jobctl_pending(t, JOBCTL_PENDING_MASK);
				sigaddset(&t->pending.signal, SIGKILL);
				signal_wake_up(t, 1);
			} while_each_thread(p, t);
			return;
		}
	}

	/*
	 * The signal is already in the shared-pending queue.
	 * Tell the chosen thread to wake up and dequeue it.
	 */
	signal_wake_up(t, sig == SIGKILL);
	return;
}

static inline int legacy_queue(struct sigpending *signals, int sig)
{
	return (sig < SIGRTMIN) && sigismember(&signals->signal, sig);
}

#ifdef CONFIG_USER_NS
static inline void userns_fixup_signal_uid(struct siginfo *info, struct task_struct *t)
{
	if (current_user_ns() == task_cred_xxx(t, user_ns))
		return;

	if (SI_FROMKERNEL(info))
		return;

	rcu_read_lock();
	info->si_uid = from_kuid_munged(task_cred_xxx(t, user_ns),
					make_kuid(current_user_ns(), info->si_uid));
	rcu_read_unlock();
}
#else
static inline void userns_fixup_signal_uid(struct siginfo *info, struct task_struct *t)
{
	return;
}
#endif

static int __send_signal(int sig, struct siginfo *info, struct task_struct *t,
			int group, int from_ancestor_ns)
{
	struct sigpending *pending;
	struct sigqueue *q;
	int override_rlimit;
	int ret = 0, result;

	assert_spin_locked(&t->sighand->siglock);

	result = TRACE_SIGNAL_IGNORED;
	if (!prepare_signal(sig, t,
			from_ancestor_ns || (info == SEND_SIG_FORCED)))
		goto ret;

	pending = group ? &t->signal->shared_pending : &t->pending;
	/*
	 * Short-circuit ignored signals and support queuing
	 * exactly one non-rt signal, so that we can get more
	 * detailed information about the cause of the signal.
	 */
	result = TRACE_SIGNAL_ALREADY_PENDING;
	if (legacy_queue(pending, sig))
		goto ret;

	result = TRACE_SIGNAL_DELIVERED;
	/*
	 * fast-pathed signals for kernel-internal things like SIGSTOP
	 * or SIGKILL.
	 */
	if (info == SEND_SIG_FORCED)
		goto out_set;

	/*
	 * Real-time signals must be queued if sent by sigqueue, or
	 * some other real-time mechanism.  It is implementation
	 * defined whether kill() does so.  We attempt to do so, on
	 * the principle of least surprise, but since kill is not
	 * allowed to fail with EAGAIN when low on memory we just
	 * make sure at least one signal gets delivered and don't
	 * pass on the info struct.
	 */
	if (sig < SIGRTMIN)
		override_rlimit = (is_si_special(info) || info->si_code >= 0);
	else
		override_rlimit = 0;

	q = __sigqueue_alloc(sig, t, GFP_ATOMIC | __GFP_NOTRACK_FALSE_POSITIVE,
		override_rlimit);
	if (q) {
		list_add_tail(&q->list, &pending->list);
		switch ((unsigned long) info) {
		case (unsigned long) SEND_SIG_NOINFO:
			q->info.si_signo = sig;
			q->info.si_errno = 0;
			q->info.si_code = SI_USER;
			q->info.si_pid = task_tgid_nr_ns(current,
							task_active_pid_ns(t));
			q->info.si_uid = from_kuid_munged(current_user_ns(), current_uid());
			break;
		case (unsigned long) SEND_SIG_PRIV:
			q->info.si_signo = sig;
			q->info.si_errno = 0;
			q->info.si_code = SI_KERNEL;
			q->info.si_pid = 0;
			q->info.si_uid = 0;
			break;
		default:
			copy_siginfo(&q->info, info);
			if (from_ancestor_ns)
				q->info.si_pid = 0;
			break;
		}

		userns_fixup_signal_uid(&q->info, t);

	} else if (!is_si_special(info)) {
		if (sig >= SIGRTMIN && info->si_code != SI_USER) {
			/*
			 * Queue overflow, abort.  We may abort if the
			 * signal was rt and sent by user using something
			 * other than kill().
			 */
			result = TRACE_SIGNAL_OVERFLOW_FAIL;
			ret = -EAGAIN;
			goto ret;
		} else {
			/*
			 * This is a silent loss of information.  We still
			 * send the signal, but the *info bits are lost.
			 */
			result = TRACE_SIGNAL_LOSE_INFO;
		}
	}

out_set:
	signalfd_notify(t, sig);
	sigaddset(&pending->signal, sig);
	complete_signal(sig, t, group);
ret:
	trace_signal_generate(sig, info, t, group, result);
	return ret;
}

static int send_signal(int sig, struct siginfo *info, struct task_struct *t,
			int group)
{
	int from_ancestor_ns = 0;

#ifdef CONFIG_PID_NS
	from_ancestor_ns = si_fromuser(info) &&
			   !task_pid_nr_ns(current, task_active_pid_ns(t));
#endif

	return __send_signal(sig, info, t, group, from_ancestor_ns);
}

static void print_fatal_signal(int signr)
{
	struct pt_regs *regs = signal_pt_regs();
	printk(KERN_INFO "potentially unexpected fatal signal %d.\n", signr);

#if defined(__i386__) && !defined(__arch_um__)
	printk(KERN_INFO "code at %08lx: ", regs->ip);
	{
		int i;
		for (i = 0; i < 16; i++) {
			unsigned char insn;

			if (get_user(insn, (unsigned char *)(regs->ip + i)))
				break;
			printk(KERN_CONT "%02x ", insn);
		}
	}
	printk(KERN_CONT "\n");
#endif
	preempt_disable();
	show_regs(regs);
	preempt_enable();
}

static int __init setup_print_fatal_signals(char *str)
{
	get_option (&str, &print_fatal_signals);

	return 1;
}

__setup("print-fatal-signals=", setup_print_fatal_signals);

int
__group_send_sig_info(int sig, struct siginfo *info, struct task_struct *p)
{
	return send_signal(sig, info, p, 1);
}

static int
specific_send_sig_info(int sig, struct siginfo *info, struct task_struct *t)
{
	return send_signal(sig, info, t, 0);
}

int do_send_sig_info(int sig, struct siginfo *info, struct task_struct *p,
			bool group)
{
	unsigned long flags;
	int ret = -ESRCH;

	if (lock_task_sighand(p, &flags)) {
		ret = send_signal(sig, info, p, group);
		unlock_task_sighand(p, &flags);
	}

	return ret;
}

/*
 * Force a signal that the process can't ignore: if necessary
 * we unblock the signal and change any SIG_IGN to SIG_DFL.
 *
 * Note: If we unblock the signal, we always reset it to SIG_DFL,
 * since we do not want to have a signal handler that was blocked
 * be invoked when user space had explicitly blocked it.
 *
 * We don't want to have recursive SIGSEGV's etc, for example,
 * that is why we also clear SIGNAL_UNKILLABLE.
 */
int
force_sig_info(int sig, struct siginfo *info, struct task_struct *t)
{
	unsigned long int flags;
	int ret, blocked, ignored;
	struct k_sigaction *action;

	spin_lock_irqsave(&t->sighand->siglock, flags);
	action = &t->sighand->action[sig-1];
	ignored = action->sa.sa_handler == SIG_IGN;
	blocked = sigismember(&t->blocked, sig);
	if (blocked || ignored) {
		action->sa.sa_handler = SIG_DFL;
		if (blocked) {
			sigdelset(&t->blocked, sig);
			recalc_sigpending_and_wake(t);
		}
	}
	if (action->sa.sa_handler == SIG_DFL)
		t->signal->flags &= ~SIGNAL_UNKILLABLE;
	ret = specific_send_sig_info(sig, info, t);
	spin_unlock_irqrestore(&t->sighand->siglock, flags);

	return ret;
}

/*
 * Nuke all other threads in the group.
 */
int zap_other_threads(struct task_struct *p)
{
	struct task_struct *t = p;
	int count = 0;

	p->signal->group_stop_count = 0;

	while_each_thread(p, t) {
		task_clear_jobctl_pending(t, JOBCTL_PENDING_MASK);
		count++;

		/* Don't bother with already dead threads */
		if (t->exit_state)
			continue;
		sigaddset(&t->pending.signal, SIGKILL);
		signal_wake_up(t, 1);
	}

	return count;
}

struct sighand_struct *__lock_task_sighand(struct task_struct *tsk,
					   unsigned long *flags)
{
	struct sighand_struct *sighand;

	for (;;) {
		local_irq_save(*flags);
		rcu_read_lock();
		sighand = rcu_dereference(tsk->sighand);
		if (unlikely(sighand == NULL)) {
			rcu_read_unlock();
			local_irq_restore(*flags);
			break;
		}

		spin_lock(&sighand->siglock);
		if (likely(sighand == tsk->sighand)) {
			rcu_read_unlock();
			break;
		}
		spin_unlock(&sighand->siglock);
		rcu_read_unlock();
		local_irq_restore(*flags);
	}

	return sighand;
}

/*
 * send signal info to all the members of a group
 */
int group_send_sig_info(int sig, struct siginfo *info, struct task_struct *p)
{
	int ret;

	rcu_read_lock();
	ret = check_kill_permission(sig, info, p);
	rcu_read_unlock();

	if (!ret && sig)
		ret = do_send_sig_info(sig, info, p, true);

	return ret;
}

/*
 * __kill_pgrp_info() sends a signal to a process group: this is what the tty
 * control characters do (^C, ^Z etc)
 * - the caller must hold at least a readlock on tasklist_lock
 */
int __kill_pgrp_info(int sig, struct siginfo *info, struct pid *pgrp)
{
	struct task_struct *p = NULL;
	int retval, success;

	success = 0;
	retval = -ESRCH;
	do_each_pid_task(pgrp, PIDTYPE_PGID, p) {
		int err = group_send_sig_info(sig, info, p);
		success |= !err;
		retval = err;
	} while_each_pid_task(pgrp, PIDTYPE_PGID, p);
	return success ? 0 : retval;
}

int kill_pid_info(int sig, struct siginfo *info, struct pid *pid)
{
	int error = -ESRCH;
	struct task_struct *p;

	rcu_read_lock();
retry:
	p = pid_task(pid, PIDTYPE_PID);
	if (p) {
		error = group_send_sig_info(sig, info, p);
		if (unlikely(error == -ESRCH))
			/*
			 * The task was unhashed in between, try again.
			 * If it is dead, pid_task() will return NULL,
			 * if we race with de_thread() it will find the
			 * new leader.
			 */
			goto retry;
	}
	rcu_read_unlock();

	return error;
}

int kill_proc_info(int sig, struct siginfo *info, pid_t pid)
{
	int error;
	rcu_read_lock();
	error = kill_pid_info(sig, info, find_vpid(pid));
	rcu_read_unlock();
	return error;
}

static int kill_as_cred_perm(const struct cred *cred,
			     struct task_struct *target)
{
	const struct cred *pcred = __task_cred(target);
	if (!uid_eq(cred->euid, pcred->suid) && !uid_eq(cred->euid, pcred->uid) &&
	    !uid_eq(cred->uid,  pcred->suid) && !uid_eq(cred->uid,  pcred->uid))
		return 0;
	return 1;
}

/* like kill_pid_info(), but doesn't use uid/euid of "current" */
int kill_pid_info_as_cred(int sig, struct siginfo *info, struct pid *pid,
			 const struct cred *cred, u32 secid)
{
	int ret = -EINVAL;
	struct task_struct *p;
	unsigned long flags;

	if (!valid_signal(sig))
		return ret;

	rcu_read_lock();
	p = pid_task(pid, PIDTYPE_PID);
	if (!p) {
		ret = -ESRCH;
		goto out_unlock;
	}
	if (si_fromuser(info) && !kill_as_cred_perm(cred, p)) {
		ret = -EPERM;
		goto out_unlock;
	}
	ret = security_task_kill(p, info, sig, secid);
	if (ret)
		goto out_unlock;

	if (sig) {
		if (lock_task_sighand(p, &flags)) {
			ret = __send_signal(sig, info, p, 1, 0);
			unlock_task_sighand(p, &flags);
		} else
			ret = -ESRCH;
	}
out_unlock:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(kill_pid_info_as_cred);

/*
 * kill_something_info() interprets pid in interesting ways just like kill(2).
 *
 * POSIX specifies that kill(-1,sig) is unspecified, but what we have
 * is probably wrong.  Should make it like BSD or SYSV.
 */

static int kill_something_info(int sig, struct siginfo *info, pid_t pid)
{
	int ret;

	if (pid > 0) {
		rcu_read_lock();
		ret = kill_pid_info(sig, info, find_vpid(pid));
		rcu_read_unlock();
		return ret;
	}

	read_lock(&tasklist_lock);
	if (pid != -1) {
		ret = __kill_pgrp_info(sig, info,
				pid ? find_vpid(-pid) : task_pgrp(current));
	} else {
		int retval = 0, count = 0;
		struct task_struct * p;

		for_each_process(p) {
			if (task_pid_vnr(p) > 1 &&
					!same_thread_group(p, current)) {
				int err = group_send_sig_info(sig, info, p);
				++count;
				if (err != -EPERM)
					retval = err;
			}
		}
		ret = count ? retval : -ESRCH;
	}
	read_unlock(&tasklist_lock);

	return ret;
}

/*
 * These are for backward compatibility with the rest of the kernel source.
 */

int send_sig_info(int sig, struct siginfo *info, struct task_struct *p)
{
	/*
	 * Make sure legacy kernel users don't send in bad values
	 * (normal paths check this in check_kill_permission).
	 */
	if (!valid_signal(sig))
		return -EINVAL;

	return do_send_sig_info(sig, info, p, false);
}

#define __si_special(priv) \
	((priv) ? SEND_SIG_PRIV : SEND_SIG_NOINFO)

int
send_sig(int sig, struct task_struct *p, int priv)
{
	return send_sig_info(sig, __si_special(priv), p);
}

void
force_sig(int sig, struct task_struct *p)
{
	force_sig_info(sig, SEND_SIG_PRIV, p);
}

/*
 * When things go south during signal handling, we
 * will force a SIGSEGV. And if the signal that caused
 * the problem was already a SIGSEGV, we'll want to
 * make sure we don't even try to deliver the signal..
 */
int
force_sigsegv(int sig, struct task_struct *p)
{
	if (sig == SIGSEGV) {
		unsigned long flags;
		spin_lock_irqsave(&p->sighand->siglock, flags);
		p->sighand->action[sig - 1].sa.sa_handler = SIG_DFL;
		spin_unlock_irqrestore(&p->sighand->siglock, flags);
	}
	force_sig(SIGSEGV, p);
	return 0;
}

int kill_pgrp(struct pid *pid, int sig, int priv)
{
	int ret;

	read_lock(&tasklist_lock);
	ret = __kill_pgrp_info(sig, __si_special(priv), pid);
	read_unlock(&tasklist_lock);

	return ret;
}
EXPORT_SYMBOL(kill_pgrp);

int kill_pid(struct pid *pid, int sig, int priv)
{
	return kill_pid_info(sig, __si_special(priv), pid);
}
EXPORT_SYMBOL(kill_pid);

/*
 * These functions support sending signals using preallocated sigqueue
 * structures.  This is needed "because realtime applications cannot
 * afford to lose notifications of asynchronous events, like timer
 * expirations or I/O completions".  In the case of POSIX Timers
 * we allocate the sigqueue structure from the timer_create.  If this
 * allocation fails we are able to report the failure to the application
 * with an EAGAIN error.
 */
struct sigqueue *sigqueue_alloc(void)
{
	struct sigqueue *q = __sigqueue_alloc(-1, current, GFP_KERNEL, 0);

	if (q)
		q->flags |= SIGQUEUE_PREALLOC;

	return q;
}

void sigqueue_free(struct sigqueue *q)
{
	unsigned long flags;
	spinlock_t *lock = &current->sighand->siglock;

	BUG_ON(!(q->flags & SIGQUEUE_PREALLOC));
	/*
	 * We must hold ->siglock while testing q->list
	 * to serialize with collect_signal() or with
	 * __exit_signal()->flush_sigqueue().
	 */
	spin_lock_irqsave(lock, flags);
	q->flags &= ~SIGQUEUE_PREALLOC;
	/*
	 * If it is queued it will be freed when dequeued,
	 * like the "regular" sigqueue.
	 */
	if (!list_empty(&q->list))
		q = NULL;
	spin_unlock_irqrestore(lock, flags);

	if (q)
		__sigqueue_free(q);
}

int send_sigqueue(struct sigqueue *q, struct task_struct *t, int group)
{
	int sig = q->info.si_signo;
	struct sigpending *pending;
	unsigned long flags;
	int ret, result;

	BUG_ON(!(q->flags & SIGQUEUE_PREALLOC));

	ret = -1;
	if (!likely(lock_task_sighand(t, &flags)))
		goto ret;

	ret = 1; /* the signal is ignored */
	result = TRACE_SIGNAL_IGNORED;
	if (!prepare_signal(sig, t, false))
		goto out;

	ret = 0;
	if (unlikely(!list_empty(&q->list))) {
		/*
		 * If an SI_TIMER entry is already queue just increment
		 * the overrun count.
		 */
		BUG_ON(q->info.si_code != SI_TIMER);
		q->info.si_overrun++;
		result = TRACE_SIGNAL_ALREADY_PENDING;
		goto out;
	}
	q->info.si_overrun = 0;

	signalfd_notify(t, sig);
	pending = group ? &t->signal->shared_pending : &t->pending;
	list_add_tail(&q->list, &pending->list);
	sigaddset(&pending->signal, sig);
	complete_signal(sig, t, group);
	result = TRACE_SIGNAL_DELIVERED;
out:
	trace_signal_generate(sig, &q->info, t, group, result);
	unlock_task_sighand(t, &flags);
ret:
	return ret;
}

/*
 * Let a parent know about the death of a child.
 * For a stopped/continued status change, use do_notify_parent_cldstop instead.
 *
 * Returns true if our parent ignored us and so we've switched to
 * self-reaping.
 */
bool do_notify_parent(struct task_struct *tsk, int sig)
{
	struct siginfo info;
	unsigned long flags;
	struct sighand_struct *psig;
	bool autoreap = false;
	cputime_t utime, stime;

	BUG_ON(sig == -1);

 	/* do_notify_parent_cldstop should have been called instead.  */
 	BUG_ON(task_is_stopped_or_traced(tsk));

	BUG_ON(!tsk->ptrace &&
	       (tsk->group_leader != tsk || !thread_group_empty(tsk)));

	if (sig != SIGCHLD) {
		/*
		 * This is only possible if parent == real_parent.
		 * Check if it has changed security domain.
		 */
		if (tsk->parent_exec_id != tsk->parent->self_exec_id)
			sig = SIGCHLD;
	}

	info.si_signo = sig;
	info.si_errno = 0;
	/*
	 * We are under tasklist_lock here so our parent is tied to
	 * us and cannot change.
	 *
	 * task_active_pid_ns will always return the same pid namespace
	 * until a task passes through release_task.
	 *
	 * write_lock() currently calls preempt_disable() which is the
	 * same as rcu_read_lock(), but according to Oleg, this is not
	 * correct to rely on this
	 */
	rcu_read_lock();
	info.si_pid = task_pid_nr_ns(tsk, task_active_pid_ns(tsk->parent));
	info.si_uid = from_kuid_munged(task_cred_xxx(tsk->parent, user_ns),
				       task_uid(tsk));
	rcu_read_unlock();

	task_cputime(tsk, &utime, &stime);
	info.si_utime = cputime_to_clock_t(utime + tsk->signal->utime);
	info.si_stime = cputime_to_clock_t(stime + tsk->signal->stime);

	info.si_status = tsk->exit_code & 0x7f;
	if (tsk->exit_code & 0x80)
		info.si_code = CLD_DUMPED;
	else if (tsk->exit_code & 0x7f)
		info.si_code = CLD_KILLED;
	else {
		info.si_code = CLD_EXITED;
		info.si_status = tsk->exit_code >> 8;
	}

	psig = tsk->parent->sighand;
	spin_lock_irqsave(&psig->siglock, flags);
	if (!tsk->ptrace && sig == SIGCHLD &&
	    (psig->action[SIGCHLD-1].sa.sa_handler == SIG_IGN ||
	     (psig->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDWAIT))) {
		/*
		 * We are exiting and our parent doesn't care.  POSIX.1
		 * defines special semantics for setting SIGCHLD to SIG_IGN
		 * or setting the SA_NOCLDWAIT flag: we should be reaped
		 * automatically and not left for our parent's wait4 call.
		 * Rather than having the parent do it as a magic kind of
		 * signal handler, we just set this to tell do_exit that we
		 * can be cleaned up without becoming a zombie.  Note that
		 * we still call __wake_up_parent in this case, because a
		 * blocked sys_wait4 might now return -ECHILD.
		 *
		 * Whether we send SIGCHLD or not for SA_NOCLDWAIT
		 * is implementation-defined: we do (if you don't want
		 * it, just use SIG_IGN instead).
		 */
		autoreap = true;
		if (psig->action[SIGCHLD-1].sa.sa_handler == SIG_IGN)
			sig = 0;
	}
	if (valid_signal(sig) && sig)
		__group_send_sig_info(sig, &info, tsk->parent);
	__wake_up_parent(tsk, tsk->parent);
	spin_unlock_irqrestore(&psig->siglock, flags);

	return autoreap;
}

/**
 * do_notify_parent_cldstop - notify parent of stopped/continued state change
 * @tsk: task reporting the state change
 * @for_ptracer: the notification is for ptracer
 * @why: CLD_{CONTINUED|STOPPED|TRAPPED} to report
 *
 * Notify @tsk's parent that the stopped/continued state has changed.  If
 * @for_ptracer is %false, @tsk's group leader notifies to its real parent.
 * If %true, @tsk reports to @tsk->parent which should be the ptracer.
 *
 * CONTEXT:
 * Must be called with tasklist_lock at least read locked.
 */
static void do_notify_parent_cldstop(struct task_struct *tsk,
				     bool for_ptracer, int why)
{
	struct siginfo info;
	unsigned long flags;
	struct task_struct *parent;
	struct sighand_struct *sighand;
	cputime_t utime, stime;

	if (for_ptracer) {
		parent = tsk->parent;
	} else {
		tsk = tsk->group_leader;
		parent = tsk->real_parent;
	}

	info.si_signo = SIGCHLD;
	info.si_errno = 0;
	/*
	 * see comment in do_notify_parent() about the following 4 lines
	 */
	rcu_read_lock();
	info.si_pid = task_pid_nr_ns(tsk, task_active_pid_ns(parent));
	info.si_uid = from_kuid_munged(task_cred_xxx(parent, user_ns), task_uid(tsk));
	rcu_read_unlock();

	task_cputime(tsk, &utime, &stime);
	info.si_utime = cputime_to_clock_t(utime);
	info.si_stime = cputime_to_clock_t(stime);

 	info.si_code = why;
 	switch (why) {
 	case CLD_CONTINUED:
 		info.si_status = SIGCONT;
 		break;
 	case CLD_STOPPED:
 		info.si_status = tsk->signal->group_exit_code & 0x7f;
 		break;
 	case CLD_TRAPPED:
 		info.si_status = tsk->exit_code & 0x7f;
 		break;
 	default:
 		BUG();
 	}

	sighand = parent->sighand;
	spin_lock_irqsave(&sighand->siglock, flags);
	if (sighand->action[SIGCHLD-1].sa.sa_handler != SIG_IGN &&
	    !(sighand->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDSTOP))
		__group_send_sig_info(SIGCHLD, &info, parent);
	/*
	 * Even if SIGCHLD is not generated, we must wake up wait4 calls.
	 */
	__wake_up_parent(tsk, parent);
	spin_unlock_irqrestore(&sighand->siglock, flags);
}

static inline int may_ptrace_stop(void)
{
	if (!likely(current->ptrace))
		return 0;
	/*
	 * Are we in the middle of do_coredump?
	 * If so and our tracer is also part of the coredump stopping
	 * is a deadlock situation, and pointless because our tracer
	 * is dead so don't allow us to stop.
	 * If SIGKILL was already sent before the caller unlocked
	 * ->siglock we must see ->core_state != NULL. Otherwise it
	 * is safe to enter schedule().
	 *
	 * This is almost outdated, a task with the pending SIGKILL can't
	 * block in TASK_TRACED. But PTRACE_EVENT_EXIT can be reported
	 * after SIGKILL was already dequeued.
	 */
	if (unlikely(current->mm->core_state) &&
	    unlikely(current->mm == current->parent->mm))
		return 0;

	return 1;
}

/*
 * Return non-zero if there is a SIGKILL that should be waking us up.
 * Called with the siglock held.
 */
static int sigkill_pending(struct task_struct *tsk)
{
	return	sigismember(&tsk->pending.signal, SIGKILL) ||
		sigismember(&tsk->signal->shared_pending.signal, SIGKILL);
}

/*
 * This must be called with current->sighand->siglock held.
 *
 * This should be the path for all ptrace stops.
 * We always set current->last_siginfo while stopped here.
 * That makes it a way to test a stopped process for
 * being ptrace-stopped vs being job-control-stopped.
 *
 * If we actually decide not to stop at all because the tracer
 * is gone, we keep current->exit_code unless clear_code.
 */
static void ptrace_stop(int exit_code, int why, int clear_code, siginfo_t *info)
	__releases(&current->sighand->siglock)
	__acquires(&current->sighand->siglock)
{
	bool gstop_done = false;

	if (arch_ptrace_stop_needed(exit_code, info)) {
		/*
		 * The arch code has something special to do before a
		 * ptrace stop.  This is allowed to block, e.g. for faults
		 * on user stack pages.  We can't keep the siglock while
		 * calling arch_ptrace_stop, so we must release it now.
		 * To preserve proper semantics, we must do this before
		 * any signal bookkeeping like checking group_stop_count.
		 * Meanwhile, a SIGKILL could come in before we retake the
		 * siglock.  That must prevent us from sleeping in TASK_TRACED.
		 * So after regaining the lock, we must check for SIGKILL.
		 */
		spin_unlock_irq(&current->sighand->siglock);
		arch_ptrace_stop(exit_code, info);
		spin_lock_irq(&current->sighand->siglock);
		if (sigkill_pending(current))
			return;
	}

	/*
	 * We're committing to trapping.  TRACED should be visible before
	 * TRAPPING is cleared; otherwise, the tracer might fail do_wait().
	 * Also, transition to TRACED and updates to ->jobctl should be
	 * atomic with respect to siglock and should be done after the arch
	 * hook as siglock is released and regrabbed across it.
	 */
	set_current_state(TASK_TRACED);

	current->last_siginfo = info;
	current->exit_code = exit_code;

	/*
	 * If @why is CLD_STOPPED, we're trapping to participate in a group
	 * stop.  Do the bookkeeping.  Note that if SIGCONT was delievered
	 * across siglock relocks since INTERRUPT was scheduled, PENDING
	 * could be clear now.  We act as if SIGCONT is received after
	 * TASK_TRACED is entered - ignore it.
	 */
	if (why == CLD_STOPPED && (current->jobctl & JOBCTL_STOP_PENDING))
		gstop_done = task_participate_group_stop(current);

	/* any trap clears pending STOP trap, STOP trap clears NOTIFY */
	task_clear_jobctl_pending(current, JOBCTL_TRAP_STOP);
	if (info && info->si_code >> 8 == PTRACE_EVENT_STOP)
		task_clear_jobctl_pending(current, JOBCTL_TRAP_NOTIFY);

	/* entering a trap, clear TRAPPING */
	task_clear_jobctl_trapping(current);

	spin_unlock_irq(&current->sighand->siglock);
	read_lock(&tasklist_lock);
	if (may_ptrace_stop()) {
		/*
		 * Notify parents of the stop.
		 *
		 * While ptraced, there are two parents - the ptracer and
		 * the real_parent of the group_leader.  The ptracer should
		 * know about every stop while the real parent is only
		 * interested in the completion of group stop.  The states
		 * for the two don't interact with each other.  Notify
		 * separately unless they're gonna be duplicates.
		 */
		do_notify_parent_cldstop(current, true, why);
		if (gstop_done && ptrace_reparented(current))
			do_notify_parent_cldstop(current, false, why);

		/*
		 * Don't want to allow preemption here, because
		 * sys_ptrace() needs this task to be inactive.
		 *
		 * XXX: implement read_unlock_no_resched().
		 */
		preempt_disable();
		read_unlock(&tasklist_lock);
		preempt_enable_no_resched();
		freezable_schedule();
	} else {
		/*
		 * By the time we got the lock, our tracer went away.
		 * Don't drop the lock yet, another tracer may come.
		 *
		 * If @gstop_done, the ptracer went away between group stop
		 * completion and here.  During detach, it would have set
		 * JOBCTL_STOP_PENDING on us and we'll re-enter
		 * TASK_STOPPED in do_signal_stop() on return, so notifying
		 * the real parent of the group stop completion is enough.
		 */
		if (gstop_done)
			do_notify_parent_cldstop(current, false, why);

		/* tasklist protects us from ptrace_freeze_traced() */
		__set_current_state(TASK_RUNNING);
		if (clear_code)
			current->exit_code = 0;
		read_unlock(&tasklist_lock);
	}

	/*
	 * We are back.  Now reacquire the siglock before touching
	 * last_siginfo, so that we are sure to have synchronized with
	 * any signal-sending on another CPU that wants to examine it.
	 */
	spin_lock_irq(&current->sighand->siglock);
	current->last_siginfo = NULL;

	/* LISTENING can be set only during STOP traps, clear it */
	current->jobctl &= ~JOBCTL_LISTENING;

	/*
	 * Queued signals ignored us while we were stopped for tracing.
	 * So check for any that we should take before resuming user mode.
	 * This sets TIF_SIGPENDING, but never clears it.
	 */
	recalc_sigpending_tsk(current);
}

static void ptrace_do_notify(int signr, int exit_code, int why)
{
	siginfo_t info;

	memset(&info, 0, sizeof info);
	info.si_signo = signr;
	info.si_code = exit_code;
	info.si_pid = task_pid_vnr(current);
	info.si_uid = from_kuid_munged(current_user_ns(), current_uid());

	/* Let the debugger run.  */
	ptrace_stop(exit_code, why, 1, &info);
}

void ptrace_notify(int exit_code)
{
	BUG_ON((exit_code & (0x7f | ~0xffff)) != SIGTRAP);
	if (unlikely(current->task_works))
		task_work_run();

	spin_lock_irq(&current->sighand->siglock);
	ptrace_do_notify(SIGTRAP, exit_code, CLD_TRAPPED);
	spin_unlock_irq(&current->sighand->siglock);
}

/**
 * do_signal_stop - handle group stop for SIGSTOP and other stop signals
 * @signr: signr causing group stop if initiating
 *
 * If %JOBCTL_STOP_PENDING is not set yet, initiate group stop with @signr
 * and participate in it.  If already set, participate in the existing
 * group stop.  If participated in a group stop (and thus slept), %true is
 * returned with siglock released.
 *
 * If ptraced, this function doesn't handle stop itself.  Instead,
 * %JOBCTL_TRAP_STOP is scheduled and %false is returned with siglock
 * untouched.  The caller must ensure that INTERRUPT trap handling takes
 * places afterwards.
 *
 * CONTEXT:
 * Must be called with @current->sighand->siglock held, which is released
 * on %true return.
 *
 * RETURNS:
 * %false if group stop is already cancelled or ptrace trap is scheduled.
 * %true if participated in group stop.
 */
static bool do_signal_stop(int signr)
	__releases(&current->sighand->siglock)
{
	struct signal_struct *sig = current->signal;

	if (!(current->jobctl & JOBCTL_STOP_PENDING)) {
		unsigned int gstop = JOBCTL_STOP_PENDING | JOBCTL_STOP_CONSUME;
		struct task_struct *t;

		/* signr will be recorded in task->jobctl for retries */
		WARN_ON_ONCE(signr & ~JOBCTL_STOP_SIGMASK);

		if (!likely(current->jobctl & JOBCTL_STOP_DEQUEUED) ||
		    unlikely(signal_group_exit(sig)))
			return false;
		/*
		 * There is no group stop already in progress.  We must
		 * initiate one now.
		 *
		 * While ptraced, a task may be resumed while group stop is
		 * still in effect and then receive a stop signal and
		 * initiate another group stop.  This deviates from the
		 * usual behavior as two consecutive stop signals can't
		 * cause two group stops when !ptraced.  That is why we
		 * also check !task_is_stopped(t) below.
		 *
		 * The condition can be distinguished by testing whether
		 * SIGNAL_STOP_STOPPED is already set.  Don't generate
		 * group_exit_code in such case.
		 *
		 * This is not necessary for SIGNAL_STOP_CONTINUED because
		 * an intervening stop signal is required to cause two
		 * continued events regardless of ptrace.
		 */
		if (!(sig->flags & SIGNAL_STOP_STOPPED))
			sig->group_exit_code = signr;

		sig->group_stop_count = 0;

		if (task_set_jobctl_pending(current, signr | gstop))
			sig->group_stop_count++;

		for (t = next_thread(current); t != current;
		     t = next_thread(t)) {
			/*
			 * Setting state to TASK_STOPPED for a group
			 * stop is always done with the siglock held,
			 * so this check has no races.
			 */
			if (!task_is_stopped(t) &&
			    task_set_jobctl_pending(t, signr | gstop)) {
				sig->group_stop_count++;
				if (likely(!(t->ptrace & PT_SEIZED)))
					signal_wake_up(t, 0);
				else
					ptrace_trap_notify(t);
			}
		}
	}

	if (likely(!current->ptrace)) {
		int notify = 0;

		/*
		 * If there are no other threads in the group, or if there
		 * is a group stop in progress and we are the last to stop,
		 * report to the parent.
		 */
		if (task_participate_group_stop(current))
			notify = CLD_STOPPED;

		__set_current_state(TASK_STOPPED);
		spin_unlock_irq(&current->sighand->siglock);

		/*
		 * Notify the parent of the group stop completion.  Because
		 * we're not holding either the siglock or tasklist_lock
		 * here, ptracer may attach inbetween; however, this is for
		 * group stop and should always be delivered to the real
		 * parent of the group leader.  The new ptracer will get
		 * its notification when this task transitions into
		 * TASK_TRACED.
		 */
		if (notify) {
			read_lock(&tasklist_lock);
			do_notify_parent_cldstop(current, false, notify);
			read_unlock(&tasklist_lock);
		}

		/* Now we don't run again until woken by SIGCONT or SIGKILL */
		freezable_schedule();
		return true;
	} else {
		/*
		 * While ptraced, group stop is handled by STOP trap.
		 * Schedule it and let the caller deal with it.
		 */
		task_set_jobctl_pending(current, JOBCTL_TRAP_STOP);
		return false;
	}
}

/**
 * do_jobctl_trap - take care of ptrace jobctl traps
 *
 * When PT_SEIZED, it's used for both group stop and explicit
 * SEIZE/INTERRUPT traps.  Both generate PTRACE_EVENT_STOP trap with
 * accompanying siginfo.  If stopped, lower eight bits of exit_code contain
 * the stop signal; otherwise, %SIGTRAP.
 *
 * When !PT_SEIZED, it's used only for group stop trap with stop signal
 * number as exit_code and no siginfo.
 *
 * CONTEXT:
 * Must be called with @current->sighand->siglock held, which may be
 * released and re-acquired before returning with intervening sleep.
 */
static void do_jobctl_trap(void)
{
	struct signal_struct *signal = current->signal;
	int signr = current->jobctl & JOBCTL_STOP_SIGMASK;

	if (current->ptrace & PT_SEIZED) {
		if (!signal->group_stop_count &&
		    !(signal->flags & SIGNAL_STOP_STOPPED))
			signr = SIGTRAP;
		WARN_ON_ONCE(!signr);
		ptrace_do_notify(signr, signr | (PTRACE_EVENT_STOP << 8),
				 CLD_STOPPED);
	} else {
		WARN_ON_ONCE(!signr);
		ptrace_stop(signr, CLD_STOPPED, 0, NULL);
		current->exit_code = 0;
	}
}

static int ptrace_signal(int signr, siginfo_t *info)
{
	ptrace_signal_deliver();
	/*
	 * We do not check sig_kernel_stop(signr) but set this marker
	 * unconditionally because we do not know whether debugger will
	 * change signr. This flag has no meaning unless we are going
	 * to stop after return from ptrace_stop(). In this case it will
	 * be checked in do_signal_stop(), we should only stop if it was
	 * not cleared by SIGCONT while we were sleeping. See also the
	 * comment in dequeue_signal().
	 */
	current->jobctl |= JOBCTL_STOP_DEQUEUED;
	ptrace_stop(signr, CLD_TRAPPED, 0, info);

	/* We're back.  Did the debugger cancel the sig?  */
	signr = current->exit_code;
	if (signr == 0)
		return signr;

	current->exit_code = 0;

	/*
	 * Update the siginfo structure if the signal has
	 * changed.  If the debugger wanted something
	 * specific in the siginfo structure then it should
	 * have updated *info via PTRACE_SETSIGINFO.
	 */
	if (signr != info->si_signo) {
		info->si_signo = signr;
		info->si_errno = 0;
		info->si_code = SI_USER;
		rcu_read_lock();
		info->si_pid = task_pid_vnr(current->parent);
		info->si_uid = from_kuid_munged(current_user_ns(),
						task_uid(current->parent));
		rcu_read_unlock();
	}

	/* If the (new) signal is now blocked, requeue it.  */
	if (sigismember(&current->blocked, signr)) {
		specific_send_sig_info(signr, info, current);
		signr = 0;
	}

	return signr;
}

int get_signal_to_deliver(siginfo_t *info, struct k_sigaction *return_ka,
			  struct pt_regs *regs, void *cookie)
{
	struct sighand_struct *sighand = current->sighand;
	struct signal_struct *signal = current->signal;
	int signr;

	if (unlikely(current->task_works))
		task_work_run();

	if (unlikely(uprobe_deny_signal()))
		return 0;

	/*
	 * Do this once, we can't return to user-mode if freezing() == T.
	 * do_signal_stop() and ptrace_stop() do freezable_schedule() and
	 * thus do not need another check after return.
	 */
	try_to_freeze();

relock:
	spin_lock_irq(&sighand->siglock);
	/*
	 * Every stopped thread goes here after wakeup. Check to see if
	 * we should notify the parent, prepare_signal(SIGCONT) encodes
	 * the CLD_ si_code into SIGNAL_CLD_MASK bits.
	 */
	if (unlikely(signal->flags & SIGNAL_CLD_MASK)) {
		int why;

		if (signal->flags & SIGNAL_CLD_CONTINUED)
			why = CLD_CONTINUED;
		else
			why = CLD_STOPPED;

		signal->flags &= ~SIGNAL_CLD_MASK;

		spin_unlock_irq(&sighand->siglock);

		/*
		 * Notify the parent that we're continuing.  This event is
		 * always per-process and doesn't make whole lot of sense
		 * for ptracers, who shouldn't consume the state via
		 * wait(2) either, but, for backward compatibility, notify
		 * the ptracer of the group leader too unless it's gonna be
		 * a duplicate.
		 */
		read_lock(&tasklist_lock);
		do_notify_parent_cldstop(current, false, why);

		if (ptrace_reparented(current->group_leader))
			do_notify_parent_cldstop(current->group_leader,
						true, why);
		read_unlock(&tasklist_lock);

		goto relock;
	}

	for (;;) {
		struct k_sigaction *ka;

		if (unlikely(current->jobctl & JOBCTL_STOP_PENDING) &&
		    do_signal_stop(0))
			goto relock;

		if (unlikely(current->jobctl & JOBCTL_TRAP_MASK)) {
			do_jobctl_trap();
			spin_unlock_irq(&sighand->siglock);
			goto relock;
		}

		signr = dequeue_signal(current, &current->blocked, info);

		if (!signr)
			break; /* will return 0 */

		if (unlikely(current->ptrace) && signr != SIGKILL) {
			signr = ptrace_signal(signr, info);
			if (!signr)
				continue;
		}

		ka = &sighand->action[signr-1];

		/* Trace actually delivered signals. */
		trace_signal_deliver(signr, info, ka);

		if (ka->sa.sa_handler == SIG_IGN) /* Do nothing.  */
			continue;
		if (ka->sa.sa_handler != SIG_DFL) {
			/* Run the handler.  */
			*return_ka = *ka;

			if (ka->sa.sa_flags & SA_ONESHOT)
				ka->sa.sa_handler = SIG_DFL;

			break; /* will return non-zero "signr" value */
		}

		/*
		 * Now we are doing the default action for this signal.
		 */
		if (sig_kernel_ignore(signr)) /* Default is nothing. */
			continue;

		/*
		 * Global init gets no signals it doesn't want.
		 * Container-init gets no signals it doesn't want from same
		 * container.
		 *
		 * Note that if global/container-init sees a sig_kernel_only()
		 * signal here, the signal must have been generated internally
		 * or must have come from an ancestor namespace. In either
		 * case, the signal cannot be dropped.
		 */
		if (unlikely(signal->flags & SIGNAL_UNKILLABLE) &&
				!sig_kernel_only(signr))
			continue;

		if (sig_kernel_stop(signr)) {
			/*
			 * The default action is to stop all threads in
			 * the thread group.  The job control signals
			 * do nothing in an orphaned pgrp, but SIGSTOP
			 * always works.  Note that siglock needs to be
			 * dropped during the call to is_orphaned_pgrp()
			 * because of lock ordering with tasklist_lock.
			 * This allows an intervening SIGCONT to be posted.
			 * We need to check for that and bail out if necessary.
			 */
			if (signr != SIGSTOP) {
				spin_unlock_irq(&sighand->siglock);

				/* signals can be posted during this window */

				if (is_current_pgrp_orphaned())
					goto relock;

				spin_lock_irq(&sighand->siglock);
			}

			if (likely(do_signal_stop(info->si_signo))) {
				/* It released the siglock.  */
				goto relock;
			}

			/*
			 * We didn't actually stop, due to a race
			 * with SIGCONT or something like that.
			 */
			continue;
		}

		spin_unlock_irq(&sighand->siglock);

		/*
		 * Anything else is fatal, maybe with a core dump.
		 */
		current->flags |= PF_SIGNALED;

		if (sig_kernel_coredump(signr)) {
			if (print_fatal_signals)
				print_fatal_signal(info->si_signo);
			proc_coredump_connector(current);
			/*
			 * If it was able to dump core, this kills all
			 * other threads in the group and synchronizes with
			 * their demise.  If we lost the race with another
			 * thread getting here, it set group_exit_code
			 * first and our do_group_exit call below will use
			 * that value and ignore the one we pass it.
			 */
			do_coredump(info);
		}

		/*
		 * Death signals, no core dump.
		 */
		do_group_exit(info->si_signo);
		/* NOTREACHED */
	}
	spin_unlock_irq(&sighand->siglock);
	return signr;
}

/**
 * signal_delivered - 
 * @sig:		number of signal being delivered
 * @info:		siginfo_t of signal being delivered
 * @ka:			sigaction setting that chose the handler
 * @regs:		user register state
 * @stepping:		nonzero if debugger single-step or block-step in use
 *
 * This function should be called when a signal has succesfully been
 * delivered. It updates the blocked signals accordingly (@ka->sa.sa_mask
 * is always blocked, and the signal itself is blocked unless %SA_NODEFER
 * is set in @ka->sa.sa_flags.  Tracing is notified.
 */
void signal_delivered(int sig, siginfo_t *info, struct k_sigaction *ka,
			struct pt_regs *regs, int stepping)
{
	sigset_t blocked;

	/* A signal was successfully delivered, and the
	   saved sigmask was stored on the signal frame,
	   and will be restored by sigreturn.  So we can
	   simply clear the restore sigmask flag.  */
	clear_restore_sigmask();

	sigorsets(&blocked, &current->blocked, &ka->sa.sa_mask);
	if (!(ka->sa.sa_flags & SA_NODEFER))
		sigaddset(&blocked, sig);
	set_current_blocked(&blocked);
	tracehook_signal_handler(sig, info, ka, regs, stepping);
}

void signal_setup_done(int failed, struct ksignal *ksig, int stepping)
{
	if (failed)
		force_sigsegv(ksig->sig, current);
	else
		signal_delivered(ksig->sig, &ksig->info, &ksig->ka,
			signal_pt_regs(), stepping);
}

/*
 * It could be that complete_signal() picked us to notify about the
 * group-wide signal. Other threads should be notified now to take
 * the shared signals in @which since we will not.
 */
static void retarget_shared_pending(struct task_struct *tsk, sigset_t *which)
{
	sigset_t retarget;
	struct task_struct *t;

	sigandsets(&retarget, &tsk->signal->shared_pending.signal, which);
	if (sigisemptyset(&retarget))
		return;

	t = tsk;
	while_each_thread(tsk, t) {
		if (t->flags & PF_EXITING)
			continue;

		if (!has_pending_signals(&retarget, &t->blocked))
			continue;
		/* Remove the signals this thread can handle. */
		sigandsets(&retarget, &retarget, &t->blocked);

		if (!signal_pending(t))
			signal_wake_up(t, 0);

		if (sigisemptyset(&retarget))
			break;
	}
}

void exit_signals(struct task_struct *tsk)
{
	int group_stop = 0;
	sigset_t unblocked;

	/*
	 * @tsk is about to have PF_EXITING set - lock out users which
	 * expect stable threadgroup.
	 */
	threadgroup_change_begin(tsk);

	if (thread_group_empty(tsk) || signal_group_exit(tsk->signal)) {
		tsk->flags |= PF_EXITING;
		threadgroup_change_end(tsk);
		return;
	}

	spin_lock_irq(&tsk->sighand->siglock);
	/*
	 * From now this task is not visible for group-wide signals,
	 * see wants_signal(), do_signal_stop().
	 */
	tsk->flags |= PF_EXITING;

	threadgroup_change_end(tsk);

	if (!signal_pending(tsk))
		goto out;

	unblocked = tsk->blocked;
	signotset(&unblocked);
	retarget_shared_pending(tsk, &unblocked);

	if (unlikely(tsk->jobctl & JOBCTL_STOP_PENDING) &&
	    task_participate_group_stop(tsk))
		group_stop = CLD_STOPPED;
out:
	spin_unlock_irq(&tsk->sighand->siglock);

	/*
	 * If group stop has completed, deliver the notification.  This
	 * should always go to the real parent of the group leader.
	 */
	if (unlikely(group_stop)) {
		read_lock(&tasklist_lock);
		do_notify_parent_cldstop(tsk, false, group_stop);
		read_unlock(&tasklist_lock);
	}
}

EXPORT_SYMBOL(recalc_sigpending);
EXPORT_SYMBOL_GPL(dequeue_signal);
EXPORT_SYMBOL(flush_signals);
EXPORT_SYMBOL(force_sig);
EXPORT_SYMBOL(send_sig);
EXPORT_SYMBOL(send_sig_info);
EXPORT_SYMBOL(sigprocmask);
EXPORT_SYMBOL(block_all_signals);
EXPORT_SYMBOL(unblock_all_signals);


/*
 * System call entry points.
 */

/**
 *  sys_restart_syscall - restart a system call
 */
SYSCALL_DEFINE0(restart_syscall)
{
	struct restart_block *restart = &current_thread_info()->restart_block;
	return restart->fn(restart);
}

long do_no_restart_syscall(struct restart_block *param)
{
	return -EINTR;
}

static void __set_task_blocked(struct task_struct *tsk, const sigset_t *newset)
{
	if (signal_pending(tsk) && !thread_group_empty(tsk)) {
		sigset_t newblocked;
		/* A set of now blocked but previously unblocked signals. */
		sigandnsets(&newblocked, newset, &current->blocked);
		retarget_shared_pending(tsk, &newblocked);
	}
	tsk->blocked = *newset;
	recalc_sigpending();
}

/**
 * set_current_blocked - change current->blocked mask
 * @newset: new mask
 *
 * It is wrong to change ->blocked directly, this helper should be used
 * to ensure the process can't miss a shared signal we are going to block.
 */
void set_current_blocked(sigset_t *newset)
{
	sigdelsetmask(newset, sigmask(SIGKILL) | sigmask(SIGSTOP));
	__set_current_blocked(newset);
}

void __set_current_blocked(const sigset_t *newset)
{
	struct task_struct *tsk = current;

	spin_lock_irq(&tsk->sighand->siglock);
	__set_task_blocked(tsk, newset);
	spin_unlock_irq(&tsk->sighand->siglock);
}

/*
 * This is also useful for kernel threads that want to temporarily
 * (or permanently) block certain signals.
 *
 * NOTE! Unlike the user-mode sys_sigprocmask(), the kernel
 * interface happily blocks "unblockable" signals like SIGKILL
 * and friends.
 */
int sigprocmask(int how, sigset_t *set, sigset_t *oldset)
{
	struct task_struct *tsk = current;
	sigset_t newset;

	/* Lockless, only current can change ->blocked, never from irq */
	if (oldset)
		*oldset = tsk->blocked;

	switch (how) {
	case SIG_BLOCK:
		sigorsets(&newset, &tsk->blocked, set);
		break;
	case SIG_UNBLOCK:
		sigandnsets(&newset, &tsk->blocked, set);
		break;
	case SIG_SETMASK:
		newset = *set;
		break;
	default:
		return -EINVAL;
	}

	__set_current_blocked(&newset);
	return 0;
}

/**
 *  sys_rt_sigprocmask - change the list of currently blocked signals
 *  @how: whether to add, remove, or set signals
 *  @nset: stores pending signals
 *  @oset: previous value of signal mask if non-null
 *  @sigsetsize: size of sigset_t type
 */
SYSCALL_DEFINE4(rt_sigprocmask, int, how, sigset_t __user *, nset,
		sigset_t __user *, oset, size_t, sigsetsize)
{
	sigset_t old_set, new_set;
	int error;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	old_set = current->blocked;

	if (nset) {
		if (copy_from_user(&new_set, nset, sizeof(sigset_t)))
			return -EFAULT;
		sigdelsetmask(&new_set, sigmask(SIGKILL)|sigmask(SIGSTOP));

		error = sigprocmask(how, &new_set, NULL);
		if (error)
			return error;
	}

	if (oset) {
		if (copy_to_user(oset, &old_set, sizeof(sigset_t)))
			return -EFAULT;
	}

	return 0;
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE4(rt_sigprocmask, int, how, compat_sigset_t __user *, nset,
		compat_sigset_t __user *, oset, compat_size_t, sigsetsize)
{
#ifdef __BIG_ENDIAN
	sigset_t old_set = current->blocked;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (nset) {
		compat_sigset_t new32;
		sigset_t new_set;
		int error;
		if (copy_from_user(&new32, nset, sizeof(compat_sigset_t)))
			return -EFAULT;

		sigset_from_compat(&new_set, &new32);
		sigdelsetmask(&new_set, sigmask(SIGKILL)|sigmask(SIGSTOP));

		error = sigprocmask(how, &new_set, NULL);
		if (error)
			return error;
	}
	if (oset) {
		compat_sigset_t old32;
		sigset_to_compat(&old32, &old_set);
		if (copy_to_user(oset, &old32, sizeof(compat_sigset_t)))
			return -EFAULT;
	}
	return 0;
#else
	return sys_rt_sigprocmask(how, (sigset_t __user *)nset,
				  (sigset_t __user *)oset, sigsetsize);
#endif
}
#endif

static int do_sigpending(void *set, unsigned long sigsetsize)
{
	if (sigsetsize > sizeof(sigset_t))
		return -EINVAL;

	spin_lock_irq(&current->sighand->siglock);
	sigorsets(set, &current->pending.signal,
		  &current->signal->shared_pending.signal);
	spin_unlock_irq(&current->sighand->siglock);

	/* Outside the lock because only this thread touches it.  */
	sigandsets(set, &current->blocked, set);
	return 0;
}

/**
 *  sys_rt_sigpending - examine a pending signal that has been raised
 *			while blocked
 *  @uset: stores pending signals
 *  @sigsetsize: size of sigset_t type or larger
 */
SYSCALL_DEFINE2(rt_sigpending, sigset_t __user *, uset, size_t, sigsetsize)
{
	sigset_t set;
	int err = do_sigpending(&set, sigsetsize);
	if (!err && copy_to_user(uset, &set, sigsetsize))
		err = -EFAULT;
	return err;
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE2(rt_sigpending, compat_sigset_t __user *, uset,
		compat_size_t, sigsetsize)
{
#ifdef __BIG_ENDIAN
	sigset_t set;
	int err = do_sigpending(&set, sigsetsize);
	if (!err) {
		compat_sigset_t set32;
		sigset_to_compat(&set32, &set);
		/* we can get here only if sigsetsize <= sizeof(set) */
		if (copy_to_user(uset, &set32, sigsetsize))
			err = -EFAULT;
	}
	return err;
#else
	return sys_rt_sigpending((sigset_t __user *)uset, sigsetsize);
#endif
}
#endif

#ifndef HAVE_ARCH_COPY_SIGINFO_TO_USER

int copy_siginfo_to_user(siginfo_t __user *to, siginfo_t *from)
{
	int err;

	if (!access_ok (VERIFY_WRITE, to, sizeof(siginfo_t)))
		return -EFAULT;
	if (from->si_code < 0)
		return __copy_to_user(to, from, sizeof(siginfo_t))
			? -EFAULT : 0;
	/*
	 * If you change siginfo_t structure, please be sure
	 * this code is fixed accordingly.
	 * Please remember to update the signalfd_copyinfo() function
	 * inside fs/signalfd.c too, in case siginfo_t changes.
	 * It should never copy any pad contained in the structure
	 * to avoid security leaks, but must copy the generic
	 * 3 ints plus the relevant union member.
	 */
	err = __put_user(from->si_signo, &to->si_signo);
	err |= __put_user(from->si_errno, &to->si_errno);
	err |= __put_user((short)from->si_code, &to->si_code);
	switch (from->si_code & __SI_MASK) {
	case __SI_KILL:
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		break;
	case __SI_TIMER:
		 err |= __put_user(from->si_tid, &to->si_tid);
		 err |= __put_user(from->si_overrun, &to->si_overrun);
		 err |= __put_user(from->si_ptr, &to->si_ptr);
		break;
	case __SI_POLL:
		err |= __put_user(from->si_band, &to->si_band);
		err |= __put_user(from->si_fd, &to->si_fd);
		break;
	case __SI_FAULT:
		err |= __put_user(from->si_addr, &to->si_addr);
#ifdef __ARCH_SI_TRAPNO
		err |= __put_user(from->si_trapno, &to->si_trapno);
#endif
#ifdef BUS_MCEERR_AO
		/*
		 * Other callers might not initialize the si_lsb field,
		 * so check explicitly for the right codes here.
		 */
		if (from->si_code == BUS_MCEERR_AR || from->si_code == BUS_MCEERR_AO)
			err |= __put_user(from->si_addr_lsb, &to->si_addr_lsb);
#endif
		break;
	case __SI_CHLD:
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		err |= __put_user(from->si_status, &to->si_status);
		err |= __put_user(from->si_utime, &to->si_utime);
		err |= __put_user(from->si_stime, &to->si_stime);
		break;
	case __SI_RT: /* This is not generated by the kernel as of now. */
	case __SI_MESGQ: /* But this is */
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		err |= __put_user(from->si_ptr, &to->si_ptr);
		break;
#ifdef __ARCH_SIGSYS
	case __SI_SYS:
		err |= __put_user(from->si_call_addr, &to->si_call_addr);
		err |= __put_user(from->si_syscall, &to->si_syscall);
		err |= __put_user(from->si_arch, &to->si_arch);
		break;
#endif
	default: /* this is just in case for now ... */
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		break;
	}
	return err;
}

#endif

/**
 *  do_sigtimedwait - wait for queued signals specified in @which
 *  @which: queued signals to wait for
 *  @info: if non-null, the signal's siginfo is returned here
 *  @ts: upper bound on process time suspension
 */
int do_sigtimedwait(const sigset_t *which, siginfo_t *info,
			const struct timespec *ts)
{
	struct task_struct *tsk = current;
	long timeout = MAX_SCHEDULE_TIMEOUT;
	sigset_t mask = *which;
	int sig;

	if (ts) {
		if (!timespec_valid(ts))
			return -EINVAL;
		timeout = timespec_to_jiffies(ts);
		/*
		 * We can be close to the next tick, add another one
		 * to ensure we will wait at least the time asked for.
		 */
		if (ts->tv_sec || ts->tv_nsec)
			timeout++;
	}

	/*
	 * Invert the set of allowed signals to get those we want to block.
	 */
	sigdelsetmask(&mask, sigmask(SIGKILL) | sigmask(SIGSTOP));
	signotset(&mask);

	spin_lock_irq(&tsk->sighand->siglock);
	sig = dequeue_signal(tsk, &mask, info);
	if (!sig && timeout) {
		/*
		 * None ready, temporarily unblock those we're interested
		 * while we are sleeping in so that we'll be awakened when
		 * they arrive. Unblocking is always fine, we can avoid
		 * set_current_blocked().
		 */
		tsk->real_blocked = tsk->blocked;
		sigandsets(&tsk->blocked, &tsk->blocked, &mask);
		recalc_sigpending();
		spin_unlock_irq(&tsk->sighand->siglock);

		timeout = freezable_schedule_timeout_interruptible(timeout);

		spin_lock_irq(&tsk->sighand->siglock);
		__set_task_blocked(tsk, &tsk->real_blocked);
		siginitset(&tsk->real_blocked, 0);
		sig = dequeue_signal(tsk, &mask, info);
	}
	spin_unlock_irq(&tsk->sighand->siglock);

	if (sig)
		return sig;
	return timeout ? -EINTR : -EAGAIN;
}

/**
 *  sys_rt_sigtimedwait - synchronously wait for queued signals specified
 *			in @uthese
 *  @uthese: queued signals to wait for
 *  @uinfo: if non-null, the signal's siginfo is returned here
 *  @uts: upper bound on process time suspension
 *  @sigsetsize: size of sigset_t type
 */
SYSCALL_DEFINE4(rt_sigtimedwait, const sigset_t __user *, uthese,
		siginfo_t __user *, uinfo, const struct timespec __user *, uts,
		size_t, sigsetsize)
{
	sigset_t these;
	struct timespec ts;
	siginfo_t info;
	int ret;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&these, uthese, sizeof(these)))
		return -EFAULT;

	if (uts) {
		if (copy_from_user(&ts, uts, sizeof(ts)))
			return -EFAULT;
	}

	ret = do_sigtimedwait(&these, &info, uts ? &ts : NULL);

	if (ret > 0 && uinfo) {
		if (copy_siginfo_to_user(uinfo, &info))
			ret = -EFAULT;
	}

	return ret;
}

/**
 *  sys_kill - send a signal to a process
 *  @pid: the PID of the process
 *  @sig: signal to be sent
 */
SYSCALL_DEFINE2(kill, pid_t, pid, int, sig)
{
	struct siginfo info;

	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_USER;
	info.si_pid = task_tgid_vnr(current);
	info.si_uid = from_kuid_munged(current_user_ns(), current_uid());

	return kill_something_info(sig, &info, pid);
}

static int
do_send_specific(pid_t tgid, pid_t pid, int sig, struct siginfo *info)
{
	struct task_struct *p;
	int error = -ESRCH;

	rcu_read_lock();
	p = find_task_by_vpid(pid);
	if (p && (tgid <= 0 || task_tgid_vnr(p) == tgid)) {
		error = check_kill_permission(sig, info, p);
		/*
		 * The null signal is a permissions and process existence
		 * probe.  No signal is actually delivered.
		 */
		if (!error && sig) {
			error = do_send_sig_info(sig, info, p, false);
			/*
			 * If lock_task_sighand() failed we pretend the task
			 * dies after receiving the signal. The window is tiny,
			 * and the signal is private anyway.
			 */
			if (unlikely(error == -ESRCH))
				error = 0;
		}
	}
	rcu_read_unlock();

	return error;
}

static int do_tkill(pid_t tgid, pid_t pid, int sig)
{
	struct siginfo info = {};

	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_TKILL;
	info.si_pid = task_tgid_vnr(current);
	info.si_uid = from_kuid_munged(current_user_ns(), current_uid());

	return do_send_specific(tgid, pid, sig, &info);
}

/**
 *  sys_tgkill - send signal to one specific thread
 *  @tgid: the thread group ID of the thread
 *  @pid: the PID of the thread
 *  @sig: signal to be sent
 *
 *  This syscall also checks the @tgid and returns -ESRCH even if the PID
 *  exists but it's not belonging to the target process anymore. This
 *  method solves the problem of threads exiting and PIDs getting reused.
 */
SYSCALL_DEFINE3(tgkill, pid_t, tgid, pid_t, pid, int, sig)
{
	/* This is only valid for single tasks */
	if (pid <= 0 || tgid <= 0)
		return -EINVAL;

	return do_tkill(tgid, pid, sig);
}

/**
 *  sys_tkill - send signal to one specific task
 *  @pid: the PID of the task
 *  @sig: signal to be sent
 *
 *  Send a signal to only one task, even if it's a CLONE_THREAD task.
 */
SYSCALL_DEFINE2(tkill, pid_t, pid, int, sig)
{
	/* This is only valid for single tasks */
	if (pid <= 0)
		return -EINVAL;

	return do_tkill(0, pid, sig);
}

static int do_rt_sigqueueinfo(pid_t pid, int sig, siginfo_t *info)
{
	/* Not even root can pretend to send signals from the kernel.
	 * Nor can they impersonate a kill()/tgkill(), which adds source info.
	 */
	if ((info->si_code >= 0 || info->si_code == SI_TKILL) &&
	    (task_pid_vnr(current) != pid)) {
		/* We used to allow any < 0 si_code */
		WARN_ON_ONCE(info->si_code < 0);
		return -EPERM;
	}
	info->si_signo = sig;

	/* POSIX.1b doesn't mention process groups.  */
	return kill_proc_info(sig, info, pid);
}

/**
 *  sys_rt_sigqueueinfo - send signal information to a signal
 *  @pid: the PID of the thread
 *  @sig: signal to be sent
 *  @uinfo: signal info to be sent
 */
SYSCALL_DEFINE3(rt_sigqueueinfo, pid_t, pid, int, sig,
		siginfo_t __user *, uinfo)
{
	siginfo_t info;
	if (copy_from_user(&info, uinfo, sizeof(siginfo_t)))
		return -EFAULT;
	return do_rt_sigqueueinfo(pid, sig, &info);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE3(rt_sigqueueinfo,
			compat_pid_t, pid,
			int, sig,
			struct compat_siginfo __user *, uinfo)
{
	siginfo_t info;
	int ret = copy_siginfo_from_user32(&info, uinfo);
	if (unlikely(ret))
		return ret;
	return do_rt_sigqueueinfo(pid, sig, &info);
}
#endif

static int do_rt_tgsigqueueinfo(pid_t tgid, pid_t pid, int sig, siginfo_t *info)
{
	/* This is only valid for single tasks */
	if (pid <= 0 || tgid <= 0)
		return -EINVAL;

	/* Not even root can pretend to send signals from the kernel.
	 * Nor can they impersonate a kill()/tgkill(), which adds source info.
	 */
	if (((info->si_code >= 0 || info->si_code == SI_TKILL)) &&
	    (task_pid_vnr(current) != pid)) {
		/* We used to allow any < 0 si_code */
		WARN_ON_ONCE(info->si_code < 0);
		return -EPERM;
	}
	info->si_signo = sig;

	return do_send_specific(tgid, pid, sig, info);
}

SYSCALL_DEFINE4(rt_tgsigqueueinfo, pid_t, tgid, pid_t, pid, int, sig,
		siginfo_t __user *, uinfo)
{
	siginfo_t info;

	if (copy_from_user(&info, uinfo, sizeof(siginfo_t)))
		return -EFAULT;

	return do_rt_tgsigqueueinfo(tgid, pid, sig, &info);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE4(rt_tgsigqueueinfo,
			compat_pid_t, tgid,
			compat_pid_t, pid,
			int, sig,
			struct compat_siginfo __user *, uinfo)
{
	siginfo_t info;

	if (copy_siginfo_from_user32(&info, uinfo))
		return -EFAULT;
	return do_rt_tgsigqueueinfo(tgid, pid, sig, &info);
}
#endif

int do_sigaction(int sig, struct k_sigaction *act, struct k_sigaction *oact)
{
	struct task_struct *t = current;
	struct k_sigaction *k;
	sigset_t mask;

	if (!valid_signal(sig) || sig < 1 || (act && sig_kernel_only(sig)))
		return -EINVAL;

	k = &t->sighand->action[sig-1];

	spin_lock_irq(&current->sighand->siglock);
	if (oact)
		*oact = *k;

	if (act) {
		sigdelsetmask(&act->sa.sa_mask,
			      sigmask(SIGKILL) | sigmask(SIGSTOP));
		*k = *act;
		/*
		 * POSIX 3.3.1.3:
		 *  "Setting a signal action to SIG_IGN for a signal that is
		 *   pending shall cause the pending signal to be discarded,
		 *   whether or not it is blocked."
		 *
		 *  "Setting a signal action to SIG_DFL for a signal that is
		 *   pending and whose default action is to ignore the signal
		 *   (for example, SIGCHLD), shall cause the pending signal to
		 *   be discarded, whether or not it is blocked"
		 */
		if (sig_handler_ignored(sig_handler(t, sig), sig)) {
			sigemptyset(&mask);
			sigaddset(&mask, sig);
			rm_from_queue_full(&mask, &t->signal->shared_pending);
			do {
				rm_from_queue_full(&mask, &t->pending);
				t = next_thread(t);
			} while (t != current);
		}
	}

	spin_unlock_irq(&current->sighand->siglock);
	return 0;
}

static int 
do_sigaltstack (const stack_t __user *uss, stack_t __user *uoss, unsigned long sp)
{
	stack_t oss;
	int error;

	oss.ss_sp = (void __user *) current->sas_ss_sp;
	oss.ss_size = current->sas_ss_size;
	oss.ss_flags = sas_ss_flags(sp);

	if (uss) {
		void __user *ss_sp;
		size_t ss_size;
		int ss_flags;

		error = -EFAULT;
		if (!access_ok(VERIFY_READ, uss, sizeof(*uss)))
			goto out;
		error = __get_user(ss_sp, &uss->ss_sp) |
			__get_user(ss_flags, &uss->ss_flags) |
			__get_user(ss_size, &uss->ss_size);
		if (error)
			goto out;

		error = -EPERM;
		if (on_sig_stack(sp))
			goto out;

		error = -EINVAL;
		/*
		 * Note - this code used to test ss_flags incorrectly:
		 *  	  old code may have been written using ss_flags==0
		 *	  to mean ss_flags==SS_ONSTACK (as this was the only
		 *	  way that worked) - this fix preserves that older
		 *	  mechanism.
		 */
		if (ss_flags != SS_DISABLE && ss_flags != SS_ONSTACK && ss_flags != 0)
			goto out;

		if (ss_flags == SS_DISABLE) {
			ss_size = 0;
			ss_sp = NULL;
		} else {
			error = -ENOMEM;
			if (ss_size < MINSIGSTKSZ)
				goto out;
		}

		current->sas_ss_sp = (unsigned long) ss_sp;
		current->sas_ss_size = ss_size;
	}

	error = 0;
	if (uoss) {
		error = -EFAULT;
		if (!access_ok(VERIFY_WRITE, uoss, sizeof(*uoss)))
			goto out;
		error = __put_user(oss.ss_sp, &uoss->ss_sp) |
			__put_user(oss.ss_size, &uoss->ss_size) |
			__put_user(oss.ss_flags, &uoss->ss_flags);
	}

out:
	return error;
}
SYSCALL_DEFINE2(sigaltstack,const stack_t __user *,uss, stack_t __user *,uoss)
{
	return do_sigaltstack(uss, uoss, current_user_stack_pointer());
}

int restore_altstack(const stack_t __user *uss)
{
	int err = do_sigaltstack(uss, NULL, current_user_stack_pointer());
	/* squash all but EFAULT for now */
	return err == -EFAULT ? err : 0;
}

int __save_altstack(stack_t __user *uss, unsigned long sp)
{
	struct task_struct *t = current;
	return  __put_user((void __user *)t->sas_ss_sp, &uss->ss_sp) |
		__put_user(sas_ss_flags(sp), &uss->ss_flags) |
		__put_user(t->sas_ss_size, &uss->ss_size);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE2(sigaltstack,
			const compat_stack_t __user *, uss_ptr,
			compat_stack_t __user *, uoss_ptr)
{
	stack_t uss, uoss;
	int ret;
	mm_segment_t seg;

	if (uss_ptr) {
		compat_stack_t uss32;

		memset(&uss, 0, sizeof(stack_t));
		if (copy_from_user(&uss32, uss_ptr, sizeof(compat_stack_t)))
			return -EFAULT;
		uss.ss_sp = compat_ptr(uss32.ss_sp);
		uss.ss_flags = uss32.ss_flags;
		uss.ss_size = uss32.ss_size;
	}
	seg = get_fs();
	set_fs(KERNEL_DS);
	ret = do_sigaltstack((stack_t __force __user *) (uss_ptr ? &uss : NULL),
			     (stack_t __force __user *) &uoss,
			     compat_user_stack_pointer());
	set_fs(seg);
	if (ret >= 0 && uoss_ptr)  {
		if (!access_ok(VERIFY_WRITE, uoss_ptr, sizeof(compat_stack_t)) ||
		    __put_user(ptr_to_compat(uoss.ss_sp), &uoss_ptr->ss_sp) ||
		    __put_user(uoss.ss_flags, &uoss_ptr->ss_flags) ||
		    __put_user(uoss.ss_size, &uoss_ptr->ss_size))
			ret = -EFAULT;
	}
	return ret;
}

int compat_restore_altstack(const compat_stack_t __user *uss)
{
	int err = compat_sys_sigaltstack(uss, NULL);
	/* squash all but -EFAULT for now */
	return err == -EFAULT ? err : 0;
}

int __compat_save_altstack(compat_stack_t __user *uss, unsigned long sp)
{
	struct task_struct *t = current;
	return  __put_user(ptr_to_compat((void __user *)t->sas_ss_sp), &uss->ss_sp) |
		__put_user(sas_ss_flags(sp), &uss->ss_flags) |
		__put_user(t->sas_ss_size, &uss->ss_size);
}
#endif

#ifdef __ARCH_WANT_SYS_SIGPENDING

/**
 *  sys_sigpending - examine pending signals
 *  @set: where mask of pending signal is returned
 */
SYSCALL_DEFINE1(sigpending, old_sigset_t __user *, set)
{
	return sys_rt_sigpending((sigset_t __user *)set, sizeof(old_sigset_t)); 
}

#endif

#ifdef __ARCH_WANT_SYS_SIGPROCMASK
/**
 *  sys_sigprocmask - examine and change blocked signals
 *  @how: whether to add, remove, or set signals
 *  @nset: signals to add or remove (if non-null)
 *  @oset: previous value of signal mask if non-null
 *
 * Some platforms have their own version with special arguments;
 * others support only sys_rt_sigprocmask.
 */

SYSCALL_DEFINE3(sigprocmask, int, how, old_sigset_t __user *, nset,
		old_sigset_t __user *, oset)
{
	old_sigset_t old_set, new_set;
	sigset_t new_blocked;

	old_set = current->blocked.sig[0];

	if (nset) {
		if (copy_from_user(&new_set, nset, sizeof(*nset)))
			return -EFAULT;

		new_blocked = current->blocked;

		switch (how) {
		case SIG_BLOCK:
			sigaddsetmask(&new_blocked, new_set);
			break;
		case SIG_UNBLOCK:
			sigdelsetmask(&new_blocked, new_set);
			break;
		case SIG_SETMASK:
			new_blocked.sig[0] = new_set;
			break;
		default:
			return -EINVAL;
		}

		set_current_blocked(&new_blocked);
	}

	if (oset) {
		if (copy_to_user(oset, &old_set, sizeof(*oset)))
			return -EFAULT;
	}

	return 0;
}
#endif /* __ARCH_WANT_SYS_SIGPROCMASK */

#ifndef CONFIG_ODD_RT_SIGACTION
/**
 *  sys_rt_sigaction - alter an action taken by a process
 *  @sig: signal to be sent
 *  @act: new sigaction
 *  @oact: used to save the previous sigaction
 *  @sigsetsize: size of sigset_t type
 */
SYSCALL_DEFINE4(rt_sigaction, int, sig,
		const struct sigaction __user *, act,
		struct sigaction __user *, oact,
		size_t, sigsetsize)
{
	struct k_sigaction new_sa, old_sa;
	int ret = -EINVAL;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		goto out;

	if (act) {
		if (copy_from_user(&new_sa.sa, act, sizeof(new_sa.sa)))
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_sa : NULL, oact ? &old_sa : NULL);

	if (!ret && oact) {
		if (copy_to_user(oact, &old_sa.sa, sizeof(old_sa.sa)))
			return -EFAULT;
	}
out:
	return ret;
}
#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE4(rt_sigaction, int, sig,
		const struct compat_sigaction __user *, act,
		struct compat_sigaction __user *, oact,
		compat_size_t, sigsetsize)
{
	struct k_sigaction new_ka, old_ka;
	compat_sigset_t mask;
#ifdef __ARCH_HAS_SA_RESTORER
	compat_uptr_t restorer;
#endif
	int ret;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(compat_sigset_t))
		return -EINVAL;

	if (act) {
		compat_uptr_t handler;
		ret = get_user(handler, &act->sa_handler);
		new_ka.sa.sa_handler = compat_ptr(handler);
#ifdef __ARCH_HAS_SA_RESTORER
		ret |= get_user(restorer, &act->sa_restorer);
		new_ka.sa.sa_restorer = compat_ptr(restorer);
#endif
		ret |= copy_from_user(&mask, &act->sa_mask, sizeof(mask));
		ret |= get_user(new_ka.sa.sa_flags, &act->sa_flags);
		if (ret)
			return -EFAULT;
		sigset_from_compat(&new_ka.sa.sa_mask, &mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);
	if (!ret && oact) {
		sigset_to_compat(&mask, &old_ka.sa.sa_mask);
		ret = put_user(ptr_to_compat(old_ka.sa.sa_handler), 
			       &oact->sa_handler);
		ret |= copy_to_user(&oact->sa_mask, &mask, sizeof(mask));
		ret |= put_user(old_ka.sa.sa_flags, &oact->sa_flags);
#ifdef __ARCH_HAS_SA_RESTORER
		ret |= put_user(ptr_to_compat(old_ka.sa.sa_restorer),
				&oact->sa_restorer);
#endif
	}
	return ret;
}
#endif
#endif /* !CONFIG_ODD_RT_SIGACTION */

#ifdef CONFIG_OLD_SIGACTION
SYSCALL_DEFINE3(sigaction, int, sig,
		const struct old_sigaction __user *, act,
	        struct old_sigaction __user *, oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user(mask, &act->sa_mask))
			return -EFAULT;
#ifdef __ARCH_HAS_KA_RESTORER
		new_ka.ka_restorer = NULL;
#endif
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags) ||
		    __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask))
			return -EFAULT;
	}

	return ret;
}
#endif
#ifdef CONFIG_COMPAT_OLD_SIGACTION
COMPAT_SYSCALL_DEFINE3(sigaction, int, sig,
		const struct compat_old_sigaction __user *, act,
	        struct compat_old_sigaction __user *, oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	compat_old_sigset_t mask;
	compat_uptr_t handler, restorer;

	if (act) {
		if (!access_ok(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(handler, &act->sa_handler) ||
		    __get_user(restorer, &act->sa_restorer) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user(mask, &act->sa_mask))
			return -EFAULT;

#ifdef __ARCH_HAS_KA_RESTORER
		new_ka.ka_restorer = NULL;
#endif
		new_ka.sa.sa_handler = compat_ptr(handler);
		new_ka.sa.sa_restorer = compat_ptr(restorer);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (!access_ok(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(ptr_to_compat(old_ka.sa.sa_handler),
			       &oact->sa_handler) ||
		    __put_user(ptr_to_compat(old_ka.sa.sa_restorer),
			       &oact->sa_restorer) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags) ||
		    __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask))
			return -EFAULT;
	}
	return ret;
}
#endif

#ifdef __ARCH_WANT_SYS_SGETMASK

/*
 * For backwards compatibility.  Functionality superseded by sigprocmask.
 */
SYSCALL_DEFINE0(sgetmask)
{
	/* SMP safe */
	return current->blocked.sig[0];
}

SYSCALL_DEFINE1(ssetmask, int, newmask)
{
	int old = current->blocked.sig[0];
	sigset_t newset;

	siginitset(&newset, newmask);
	set_current_blocked(&newset);

	return old;
}
#endif /* __ARCH_WANT_SGETMASK */

#ifdef __ARCH_WANT_SYS_SIGNAL
/*
 * For backwards compatibility.  Functionality superseded by sigaction.
 */
SYSCALL_DEFINE2(signal, int, sig, __sighandler_t, handler)
{
	struct k_sigaction new_sa, old_sa;
	int ret;

	new_sa.sa.sa_handler = handler;
	new_sa.sa.sa_flags = SA_ONESHOT | SA_NOMASK;
	sigemptyset(&new_sa.sa.sa_mask);

	ret = do_sigaction(sig, &new_sa, &old_sa);

	return ret ? ret : (unsigned long)old_sa.sa.sa_handler;
}
#endif /* __ARCH_WANT_SYS_SIGNAL */

#ifdef __ARCH_WANT_SYS_PAUSE

SYSCALL_DEFINE0(pause)
{
	while (!signal_pending(current)) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
	}
	return -ERESTARTNOHAND;
}

#endif

int sigsuspend(sigset_t *set)
{
	current->saved_sigmask = current->blocked;
	set_current_blocked(set);

	current->state = TASK_INTERRUPTIBLE;
	schedule();
	set_restore_sigmask();
	return -ERESTARTNOHAND;
}

/**
 *  sys_rt_sigsuspend - replace the signal mask for a value with the
 *	@unewset value until a signal is received
 *  @unewset: new signal mask value
 *  @sigsetsize: size of sigset_t type
 */
SYSCALL_DEFINE2(rt_sigsuspend, sigset_t __user *, unewset, size_t, sigsetsize)
{
	sigset_t newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	return sigsuspend(&newset);
}
 
#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE2(rt_sigsuspend, compat_sigset_t __user *, unewset, compat_size_t, sigsetsize)
{
#ifdef __BIG_ENDIAN
	sigset_t newset;
	compat_sigset_t newset32;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset32, unewset, sizeof(compat_sigset_t)))
		return -EFAULT;
	sigset_from_compat(&newset, &newset32);
	return sigsuspend(&newset);
#else
	/* on little-endian bitmaps don't care about granularity */
	return sys_rt_sigsuspend((sigset_t __user *)unewset, sigsetsize);
#endif
}
#endif

#ifdef CONFIG_OLD_SIGSUSPEND
SYSCALL_DEFINE1(sigsuspend, old_sigset_t, mask)
{
	sigset_t blocked;
	siginitset(&blocked, mask);
	return sigsuspend(&blocked);
}
#endif
#ifdef CONFIG_OLD_SIGSUSPEND3
SYSCALL_DEFINE3(sigsuspend, int, unused1, int, unused2, old_sigset_t, mask)
{
	sigset_t blocked;
	siginitset(&blocked, mask);
	return sigsuspend(&blocked);
}
#endif

__attribute__((weak)) const char *arch_vma_name(struct vm_area_struct *vma)
{
	return NULL;
}

void __init signals_init(void)
{
	sigqueue_cachep = KMEM_CACHE(sigqueue, SLAB_PANIC);
}

#ifdef CONFIG_KGDB_KDB
#include <linux/kdb.h>
/*
 * kdb_send_sig_info - Allows kdb to send signals without exposing
 * signal internals.  This function checks if the required locks are
 * available before calling the main signal code, to avoid kdb
 * deadlocks.
 */
void
kdb_send_sig_info(struct task_struct *t, struct siginfo *info)
{
	static struct task_struct *kdb_prev_t;
	int sig, new_t;
	if (!spin_trylock(&t->sighand->siglock)) {
		kdb_printf("Can't do kill command now.\n"
			   "The sigmask lock is held somewhere else in "
			   "kernel, try again later\n");
		return;
	}
	spin_unlock(&t->sighand->siglock);
	new_t = kdb_prev_t != t;
	kdb_prev_t = t;
	if (t->state != TASK_RUNNING && new_t) {
		kdb_printf("Process is not RUNNING, sending a signal from "
			   "kdb risks deadlock\n"
			   "on the run queue locks. "
			   "The signal has _not_ been sent.\n"
			   "Reissue the kill command if you want to risk "
			   "the deadlock.\n");
		return;
	}
	sig = info->si_signo;
	if (send_sig_info(sig, info, t))
		kdb_printf("Fail to deliver Signal %d to process %d.\n",
			   sig, t->pid);
	else
		kdb_printf("Signal %d is sent to process %d.\n", sig, t->pid);
}
#endif	/* CONFIG_KGDB_KDB */
