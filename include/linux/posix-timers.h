#ifndef _linux_POSIX_TIMERS_H
#define _linux_POSIX_TIMERS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/sched.h>

union cpu_time_count {
	cputime_t cpu;
	unsigned long long sched;
};

struct cpu_timer_list {
	struct list_head entry;
	union cpu_time_count expires, incr;
	struct task_struct *task;
	int firing;
};

#define CPUCLOCK_PID(clock)		((pid_t) ~((clock) >> 3))
#define CPUCLOCK_PERTHREAD(clock) \
	(((clock) & (clockid_t) CPUCLOCK_PERTHREAD_MASK) != 0)
#define CPUCLOCK_PID_MASK	7
#define CPUCLOCK_PERTHREAD_MASK	4
#define CPUCLOCK_WHICH(clock)	((clock) & (clockid_t) CPUCLOCK_CLOCK_MASK)
#define CPUCLOCK_CLOCK_MASK	3
#define CPUCLOCK_PROF		0
#define CPUCLOCK_VIRT		1
#define CPUCLOCK_SCHED		2
#define CPUCLOCK_MAX		3

#define MAKE_PROCESS_CPUCLOCK(pid, clock) \
	((~(clockid_t) (pid) << 3) | (clockid_t) (clock))
#define MAKE_THREAD_CPUCLOCK(tid, clock) \
	MAKE_PROCESS_CPUCLOCK((tid), (clock) | CPUCLOCK_PERTHREAD_MASK)

/* POSIX.1b interval timer structure. */
struct k_itimer {
	struct list_head list;		/* free/ allocate list */
	spinlock_t it_lock;
	clockid_t it_clock;		/* which timer type */
	timer_t it_id;			/* timer id */
	int it_overrun;			/* overrun on pending signal  */
	int it_overrun_last;		/* overrun on last delivered signal */
	int it_requeue_pending;		/* waiting to requeue this timer */
#define REQUEUE_PENDING 1
	int it_sigev_notify;		/* notify word of sigevent struct */
	struct signal_struct *it_signal;
	union {
		struct pid *it_pid;	/* pid of process to send signal to */
		struct task_struct *it_process;	/* for clock_nanosleep */
	};
	struct sigqueue *sigq;		/* signal queue entry. */
	union {
		struct {
			struct hrtimer timer;
			ktime_t interval;
		} real;
		struct cpu_timer_list cpu;
		struct {
			unsigned int clock;
			unsigned int node;
			unsigned long incr;
			unsigned long expires;
		} mmtimer;
	} it;
};

struct k_clock {
	int res;		/* in nanoseconds */
	int (*clock_getres) (const clockid_t which_clock, struct timespec *tp);
	int (*clock_set) (const clockid_t which_clock, struct timespec * tp);
	int (*clock_get) (const clockid_t which_clock, struct timespec * tp);
	int (*timer_create) (struct k_itimer *timer);
	int (*nsleep) (const clockid_t which_clock, int flags,
		       struct timespec *, struct timespec __user *);
	long (*nsleep_restart) (struct restart_block *restart_block);
	int (*timer_set) (struct k_itimer * timr, int flags,
			  struct itimerspec * new_setting,
			  struct itimerspec * old_setting);
	int (*timer_del) (struct k_itimer * timr);
#define TIMER_RETRY 1
	void (*timer_get) (struct k_itimer * timr,
			   struct itimerspec * cur_setting);
};

void register_posix_clock(const clockid_t clock_id, struct k_clock *new_clock);

/* error handlers for timer_create, nanosleep and settime */
int do_posix_clock_nonanosleep(const clockid_t, int flags, struct timespec *,
			       struct timespec __user *);
int do_posix_clock_nosettime(const clockid_t, struct timespec *tp);

/* function to call to trigger timer event */
int posix_timer_event(struct k_itimer *timr, int si_private);

int posix_cpu_clock_getres(const clockid_t which_clock, struct timespec *ts);
int posix_cpu_clock_get(const clockid_t which_clock, struct timespec *ts);
int posix_cpu_clock_set(const clockid_t which_clock, const struct timespec *ts);
int posix_cpu_timer_create(struct k_itimer *timer);
int posix_cpu_nsleep(const clockid_t which_clock, int flags,
		     struct timespec *rqtp, struct timespec __user *rmtp);
long posix_cpu_nsleep_restart(struct restart_block *restart_block);
int posix_cpu_timer_set(struct k_itimer *timer, int flags,
			struct itimerspec *new, struct itimerspec *old);
int posix_cpu_timer_del(struct k_itimer *timer);
void posix_cpu_timer_get(struct k_itimer *timer, struct itimerspec *itp);

void posix_cpu_timer_schedule(struct k_itimer *timer);

void run_posix_cpu_timers(struct task_struct *task);
void posix_cpu_timers_exit(struct task_struct *task);
void posix_cpu_timers_exit_group(struct task_struct *task);

void set_process_cpu_timer(struct task_struct *task, unsigned int clock_idx,
			   cputime_t *newval, cputime_t *oldval);

long clock_nanosleep_restart(struct restart_block *restart_block);

void update_rlimit_cpu(unsigned long rlim_new);

#endif
