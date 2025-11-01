/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _linux_POSIX_TIMERS_H
#define _linux_POSIX_TIMERS_H

#include <linux/alarmtimer.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pid.h>
#include <linux/posix-timers_types.h>
#include <linux/rcuref.h>
#include <linux/spinlock.h>
#include <linux/timerqueue.h>

struct kernel_siginfo;
struct task_struct;
struct sigqueue;
struct k_itimer;

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

static inline bool clockid_aux_valid(clockid_t id)
{
	return IS_ENABLED(CONFIG_POSIX_AUX_CLOCKS) && id >= CLOCK_AUX && id <= CLOCK_AUX_LAST;
}

#ifdef CONFIG_POSIX_TIMERS

#include <linux/signal_types.h>

/**
 * cpu_timer - Posix CPU timer representation for k_itimer
 * @node:	timerqueue node to queue in the task/sig
 * @head:	timerqueue head on which this timer is queued
 * @pid:	Pointer to target task PID
 * @elist:	List head for the expiry list
 * @firing:	Timer is currently firing
 * @nanosleep:	Timer is used for nanosleep and is not a regular posix-timer
 * @handling:	Pointer to the task which handles expiry
 */
struct cpu_timer {
	struct timerqueue_node		node;
	struct timerqueue_head		*head;
	struct pid			*pid;
	struct list_head		elist;
	bool				firing;
	bool				nanosleep;
	struct task_struct __rcu	*handling;
};

static inline bool cpu_timer_enqueue(struct timerqueue_head *head,
				     struct cpu_timer *ctmr)
{
	ctmr->head = head;
	return timerqueue_add(head, &ctmr->node);
}

static inline bool cpu_timer_queued(struct cpu_timer *ctmr)
{
	return !!ctmr->head;
}

static inline bool cpu_timer_dequeue(struct cpu_timer *ctmr)
{
	if (cpu_timer_queued(ctmr)) {
		timerqueue_del(ctmr->head, &ctmr->node);
		ctmr->head = NULL;
		return true;
	}
	return false;
}

static inline u64 cpu_timer_getexpires(struct cpu_timer *ctmr)
{
	return ctmr->node.expires;
}

static inline void cpu_timer_setexpires(struct cpu_timer *ctmr, u64 exp)
{
	ctmr->node.expires = exp;
}

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

void posixtimer_rearm_itimer(struct task_struct *p);
bool posixtimer_init_sigqueue(struct sigqueue *q);
void posixtimer_send_sigqueue(struct k_itimer *tmr);
bool posixtimer_deliver_signal(struct kernel_siginfo *info, struct sigqueue *timer_sigq);
void posixtimer_free_timer(struct k_itimer *timer);
long posixtimer_create_prctl(unsigned long ctrl);

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
struct cpu_timer { };
#define INIT_CPU_TIMERS(s)
static inline void posix_cputimers_init(struct posix_cputimers *pct) { }
static inline void posix_cputimers_group_init(struct posix_cputimers *pct,
					      u64 cpu_limit) { }
static inline void posixtimer_rearm_itimer(struct task_struct *p) { }
static inline bool posixtimer_deliver_signal(struct kernel_siginfo *info,
					     struct sigqueue *timer_sigq) { return false; }
static inline void posixtimer_free_timer(struct k_itimer *timer) { }
static inline long posixtimer_create_prctl(unsigned long ctrl) { return -EINVAL; }
#endif

#ifdef CONFIG_POSIX_CPU_TIMERS_TASK_WORK
void clear_posix_cputimers_work(struct task_struct *p);
void posix_cputimers_init_work(void);
#else
static inline void clear_posix_cputimers_work(struct task_struct *p) { }
static inline void posix_cputimers_init_work(void) { }
#endif

/**
 * struct k_itimer - POSIX.1b interval timer structure.
 * @list:		List node for binding the timer to tsk::signal::posix_timers
 * @ignored_list:	List node for tracking ignored timers in tsk::signal::ignored_posix_timers
 * @t_hash:		Entry in the posix timer hash table
 * @it_lock:		Lock protecting the timer
 * @kclock:		Pointer to the k_clock struct handling this timer
 * @it_clock:		The posix timer clock id
 * @it_id:		The posix timer id for identifying the timer
 * @it_status:		The status of the timer
 * @it_sig_periodic:	The periodic status at signal delivery
 * @it_overrun:		The overrun counter for pending signals
 * @it_overrun_last:	The overrun at the time of the last delivered signal
 * @it_signal_seq:	Sequence count to control signal delivery
 * @it_sigqueue_seq:	The sequence count at the point where the signal was queued
 * @it_sigev_notify:	The notify word of sigevent struct for signal delivery
 * @it_interval:	The interval for periodic timers
 * @it_signal:		Pointer to the creators signal struct
 * @it_pid:		The pid of the process/task targeted by the signal
 * @it_process:		The task to wakeup on clock_nanosleep (CPU timers)
 * @rcuref:		Reference count for life time management
 * @sigq:		Embedded sigqueue
 * @it:			Union representing the various posix timer type
 *			internals.
 * @rcu:		RCU head for freeing the timer.
 */
struct k_itimer {
	/* 1st cacheline contains read-mostly fields */
	struct hlist_node	t_hash;
	struct hlist_node	list;
	timer_t			it_id;
	clockid_t		it_clock;
	int			it_sigev_notify;
	enum pid_type		it_pid_type;
	struct signal_struct	*it_signal;
	const struct k_clock	*kclock;

	/* 2nd cacheline and above contain fields which are modified regularly */
	spinlock_t		it_lock;
	int			it_status;
	bool			it_sig_periodic;
	s64			it_overrun;
	s64			it_overrun_last;
	unsigned int		it_signal_seq;
	unsigned int		it_sigqueue_seq;
	ktime_t			it_interval;
	struct hlist_node	ignored_list;
	union {
		struct pid		*it_pid;
		struct task_struct	*it_process;
	};
	struct sigqueue		sigq;
	rcuref_t		rcuref;
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
} ____cacheline_aligned_in_smp;

void run_posix_cpu_timers(void);
void posix_cpu_timers_exit(struct task_struct *task);
void posix_cpu_timers_exit_group(struct task_struct *task);
void set_process_cpu_timer(struct task_struct *task, unsigned int clock_idx,
			   u64 *newval, u64 *oldval);

int update_rlimit_cpu(struct task_struct *task, unsigned long rlim_new);

#ifdef CONFIG_POSIX_TIMERS
static inline void posixtimer_putref(struct k_itimer *tmr)
{
	if (rcuref_put(&tmr->rcuref))
		posixtimer_free_timer(tmr);
}

static inline void posixtimer_sigqueue_getref(struct sigqueue *q)
{
	struct k_itimer *tmr = container_of(q, struct k_itimer, sigq);

	WARN_ON_ONCE(!rcuref_get(&tmr->rcuref));
}

static inline void posixtimer_sigqueue_putref(struct sigqueue *q)
{
	struct k_itimer *tmr = container_of(q, struct k_itimer, sigq);

	posixtimer_putref(tmr);
}

static inline bool posixtimer_valid(const struct k_itimer *timer)
{
	unsigned long val = (unsigned long)timer->it_signal;

	return !(val & 0x1UL);
}
#else  /* CONFIG_POSIX_TIMERS */
static inline void posixtimer_sigqueue_getref(struct sigqueue *q) { }
static inline void posixtimer_sigqueue_putref(struct sigqueue *q) { }
#endif /* !CONFIG_POSIX_TIMERS */

#endif
