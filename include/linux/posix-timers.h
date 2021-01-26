/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _linux_POSIX_TIMERS_H
#define _linux_POSIX_TIMERS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/alarmtimer.h>
#include <linux/timerqueue.h>
#include <linux/task_work.h>

struct kernel_siginfo;
struct task_struct;

/*
 * Bit fields within a clockid:
 *
 * The most significant 29 bits hold either a pid or a file descriptor.
 *
 * Bit 2 indicates whether a cpu clock refers to a thread or a process.
 *
 * Bits 1 and 0 give the type: PROF=0, VIRT=1, SCHED=2, or FD=3.
 *
 * A clockid is invalid if bits 2, 1, and 0 are all set.
 */
#define CPUCLOCK_PID(clock)		((pid_t) ~((clock) >> 3))
#define CPUCLOCK_PERTHREAD(clock) \
	(((clock) & (clockid_t) CPUCLOCK_PERTHREAD_MASK) != 0)

#define CPUCLOCK_PERTHREAD_MASK	4
#define CPUCLOCK_WHICH(clock)	((clock) & (clockid_t) CPUCLOCK_CLOCK_MASK)
#define CPUCLOCK_CLOCK_MASK	3
#define CPUCLOCK_PROF		0
#define CPUCLOCK_VIRT		1
#define CPUCLOCK_SCHED		2
#define CPUCLOCK_MAX		3
#define CLOCKFD			CPUCLOCK_MAX
#define CLOCKFD_MASK		(CPUCLOCK_PERTHREAD_MASK|CPUCLOCK_CLOCK_MASK)

static inline clockid_t make_process_cpuclock(const unsigned int pid,
		const clockid_t clock)
{
	return ((~pid) << 3) | clock;
}
static inline clockid_t make_thread_cpuclock(const unsigned int tid,
		const clockid_t clock)
{
	return make_process_cpuclock(tid, clock | CPUCLOCK_PERTHREAD_MASK);
}

static inline clockid_t fd_to_clockid(const int fd)
{
	return make_process_cpuclock((unsigned int) fd, CLOCKFD);
}

static inline int clockid_to_fd(const clockid_t clk)
{
	return ~(clk >> 3);
}

#ifdef CONFIG_POSIX_TIMERS

/**
 * cpu_timer - Posix CPU timer representation for k_itimer
 * @node:	timerqueue node to queue in the task/sig
 * @head:	timerqueue head on which this timer is queued
 * @task:	Pointer to target task
 * @elist:	List head for the expiry list
 * @firing:	Timer is currently firing
 */
struct cpu_timer {
	struct timerqueue_node	node;
	struct timerqueue_head	*head;
	struct pid		*pid;
	struct list_head	elist;
	int			firing;
};

static inline bool cpu_timer_enqueue(struct timerqueue_head *head,
				     struct cpu_timer *ctmr)
{
	ctmr->head = head;
	return timerqueue_add(head, &ctmr->node);
}

static inline void cpu_timer_dequeue(struct cpu_timer *ctmr)
{
	if (ctmr->head) {
		timerqueue_del(ctmr->head, &ctmr->node);
		ctmr->head = NULL;
	}
}

static inline u64 cpu_timer_getexpires(struct cpu_timer *ctmr)
{
	return ctmr->node.expires;
}

static inline void cpu_timer_setexpires(struct cpu_timer *ctmr, u64 exp)
{
	ctmr->node.expires = exp;
}

/**
 * posix_cputimer_base - Container per posix CPU clock
 * @nextevt:		Earliest-expiration cache
 * @tqhead:		timerqueue head for cpu_timers
 */
struct posix_cputimer_base {
	u64			nextevt;
	struct timerqueue_head	tqhead;
};

/**
 * posix_cputimers - Container for posix CPU timer related data
 * @bases:		Base container for posix CPU clocks
 * @timers_active:	Timers are queued.
 * @expiry_active:	Timer expiry is active. Used for
 *			process wide timers to avoid multiple
 *			task trying to handle expiry concurrently
 *
 * Used in task_struct and signal_struct
 */
struct posix_cputimers {
	struct posix_cputimer_base	bases[CPUCLOCK_MAX];
	unsigned int			timers_active;
	unsigned int			expiry_active;
};

/**
 * posix_cputimers_work - Container for task work based posix CPU timer expiry
 * @work:	The task work to be scheduled
 * @scheduled:  @work has been scheduled already, no further processing
 */
struct posix_cputimers_work {
	struct callback_head	work;
	unsigned int		scheduled;
};

static inline void posix_cputimers_init(struct posix_cputimers *pct)
{
	memset(pct, 0, sizeof(*pct));
	pct->bases[0].nextevt = U64_MAX;
	pct->bases[1].nextevt = U64_MAX;
	pct->bases[2].nextevt = U64_MAX;
}

void posix_cputimers_group_init(struct posix_cputimers *pct, u64 cpu_limit);

static inline void posix_cputimers_rt_watchdog(struct posix_cputimers *pct,
					       u64 runtime)
{
	pct->bases[CPUCLOCK_SCHED].nextevt = runtime;
}

/* Init task static initializer */
#define INIT_CPU_TIMERBASE(b) {						\
	.nextevt	= U64_MAX,					\
}

#define INIT_CPU_TIMERBASES(b) {					\
	INIT_CPU_TIMERBASE(b[0]),					\
	INIT_CPU_TIMERBASE(b[1]),					\
	INIT_CPU_TIMERBASE(b[2]),					\
}

#define INIT_CPU_TIMERS(s)						\
	.posix_cputimers = {						\
		.bases = INIT_CPU_TIMERBASES(s.posix_cputimers.bases),	\
	},
#else
struct posix_cputimers { };
struct cpu_timer { };
#define INIT_CPU_TIMERS(s)
static inline void posix_cputimers_init(struct posix_cputimers *pct) { }
static inline void posix_cputimers_group_init(struct posix_cputimers *pct,
					      u64 cpu_limit) { }
#endif

#ifdef CONFIG_POSIX_CPU_TIMERS_TASK_WORK
void posix_cputimers_init_work(void);
#else
static inline void posix_cputimers_init_work(void) { }
#endif

#define REQUEUE_PENDING 1

/**
 * struct k_itimer - POSIX.1b interval timer structure.
 * @list:		List head for binding the timer to signals->posix_timers
 * @t_hash:		Entry in the posix timer hash table
 * @it_lock:		Lock protecting the timer
 * @kclock:		Pointer to the k_clock struct handling this timer
 * @it_clock:		The posix timer clock id
 * @it_id:		The posix timer id for identifying the timer
 * @it_active:		Marker that timer is active
 * @it_overrun:		The overrun counter for pending signals
 * @it_overrun_last:	The overrun at the time of the last delivered signal
 * @it_requeue_pending:	Indicator that timer waits for being requeued on
 *			signal delivery
 * @it_sigev_notify:	The notify word of sigevent struct for signal delivery
 * @it_interval:	The interval for periodic timers
 * @it_signal:		Pointer to the creators signal struct
 * @it_pid:		The pid of the process/task targeted by the signal
 * @it_process:		The task to wakeup on clock_nanosleep (CPU timers)
 * @sigq:		Pointer to preallocated sigqueue
 * @it:			Union representing the various posix timer type
 *			internals.
 * @rcu:		RCU head for freeing the timer.
 */
struct k_itimer {
	struct list_head	list;
	struct hlist_node	t_hash;
	spinlock_t		it_lock;
	const struct k_clock	*kclock;
	clockid_t		it_clock;
	timer_t			it_id;
	int			it_active;
	s64			it_overrun;
	s64			it_overrun_last;
	int			it_requeue_pending;
	int			it_sigev_notify;
	ktime_t			it_interval;
	struct signal_struct	*it_signal;
	union {
		struct pid		*it_pid;
		struct task_struct	*it_process;
	};
	struct sigqueue		*sigq;
	union {
		struct {
			struct hrtimer	timer;
		} real;
		struct cpu_timer	cpu;
		struct {
			struct alarm	alarmtimer;
		} alarm;
	} it;
	struct rcu_head		rcu;
};

void run_posix_cpu_timers(void);
void posix_cpu_timers_exit(struct task_struct *task);
void posix_cpu_timers_exit_group(struct task_struct *task);
void set_process_cpu_timer(struct task_struct *task, unsigned int clock_idx,
			   u64 *newval, u64 *oldval);

void update_rlimit_cpu(struct task_struct *task, unsigned long rlim_new);

void posixtimer_rearm(struct kernel_siginfo *info);
#endif
