/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _linux_POSIX_TIMERS_H
#define _linux_POSIX_TIMERS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/timex.h>
#include <linux/alarmtimer.h>

struct siginfo;

struct cpu_timer_list {
	struct list_head entry;
	u64 expires;
	struct task_struct *task;
	int firing;
};

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
 *			internals. Also used for rcu freeing the timer.
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
		struct cpu_timer_list	cpu;
		struct {
			struct alarm	alarmtimer;
		} alarm;
		struct rcu_head		rcu;
	} it;
};

void run_posix_cpu_timers(struct task_struct *task);
void posix_cpu_timers_exit(struct task_struct *task);
void posix_cpu_timers_exit_group(struct task_struct *task);
void set_process_cpu_timer(struct task_struct *task, unsigned int clock_idx,
			   u64 *newval, u64 *oldval);

void update_rlimit_cpu(struct task_struct *task, unsigned long rlim_new);

void posixtimer_rearm(struct kernel_siginfo *info);
#endif
