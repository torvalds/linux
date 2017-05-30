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
	u64 expires, incr;
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

#define MAKE_PROCESS_CPUCLOCK(pid, clock) \
	((~(clockid_t) (pid) << 3) | (clockid_t) (clock))
#define MAKE_THREAD_CPUCLOCK(tid, clock) \
	MAKE_PROCESS_CPUCLOCK((tid), (clock) | CPUCLOCK_PERTHREAD_MASK)

#define FD_TO_CLOCKID(fd)	((~(clockid_t) (fd) << 3) | CLOCKFD)
#define CLOCKID_TO_FD(clk)	((unsigned int) ~((clk) >> 3))

#define REQUEUE_PENDING 1

/**
 * struct k_itimer - POSIX.1b interval timer structure.
 * @list:		List head for binding the timer to signals->posix_timers
 * @t_hash:		Entry in the posix timer hash table
 * @it_lock:		Lock protecting the timer
 * @it_clock:		The posix timer clock id
 * @it_id:		The posix timer id for identifying the timer
 * @it_overrun:		The overrun counter for pending signals
 * @it_overrun_last:	The overrun at the time of the last delivered signal
 * @it_requeue_pending:	Indicator that timer waits for being requeued on
 *			signal delivery
 * @it_sigev_notify:	The notify word of sigevent struct for signal delivery
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
	clockid_t		it_clock;
	timer_t			it_id;
	int			it_overrun;
	int			it_overrun_last;
	int			it_requeue_pending;
	int			it_sigev_notify;
	struct signal_struct	*it_signal;
	union {
		struct pid		*it_pid;
		struct task_struct	*it_process;
	};
	struct sigqueue		*sigq;
	union {
		struct {
			struct hrtimer	timer;
			ktime_t		interval;
		} real;
		struct cpu_timer_list	cpu;
		struct {
			struct alarm	alarmtimer;
			ktime_t		interval;
		} alarm;
		struct rcu_head		rcu;
	} it;
};

struct k_clock {
	int (*clock_getres) (const clockid_t which_clock, struct timespec64 *tp);
	int (*clock_set) (const clockid_t which_clock,
			  const struct timespec64 *tp);
	int (*clock_get) (const clockid_t which_clock, struct timespec64 *tp);
	int (*clock_adj) (const clockid_t which_clock, struct timex *tx);
	int (*timer_create) (struct k_itimer *timer);
	int (*nsleep) (const clockid_t which_clock, int flags,
		       struct timespec64 *, struct timespec __user *);
	long (*nsleep_restart) (struct restart_block *restart_block);
	int (*timer_set) (struct k_itimer *timr, int flags,
			  struct itimerspec64 *new_setting,
			  struct itimerspec64 *old_setting);
	int (*timer_del) (struct k_itimer *timr);
#define TIMER_RETRY 1
	void (*timer_get) (struct k_itimer *timr,
			   struct itimerspec64 *cur_setting);
};

extern const struct k_clock clock_posix_cpu;
extern const struct k_clock clock_posix_dynamic;
extern const struct k_clock clock_process;
extern const struct k_clock clock_thread;
extern const struct k_clock alarm_clock;

/* function to call to trigger timer event */
int posix_timer_event(struct k_itimer *timr, int si_private);

void posix_cpu_timer_schedule(struct k_itimer *timer);

void run_posix_cpu_timers(struct task_struct *task);
void posix_cpu_timers_exit(struct task_struct *task);
void posix_cpu_timers_exit_group(struct task_struct *task);
void set_process_cpu_timer(struct task_struct *task, unsigned int clock_idx,
			   u64 *newval, u64 *oldval);

long clock_nanosleep_restart(struct restart_block *restart_block);

void update_rlimit_cpu(struct task_struct *task, unsigned long rlim_new);

void do_schedule_next_timer(struct siginfo *info);

#endif
