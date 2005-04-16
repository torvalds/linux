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
	int it_requeue_pending;         /* waiting to requeue this timer */
#define REQUEUE_PENDING 1
	int it_sigev_notify;		/* notify word of sigevent struct */
	int it_sigev_signo;		/* signo word of sigevent struct */
	sigval_t it_sigev_value;	/* value word of sigevent struct */
	struct task_struct *it_process;	/* process to send signal to */
	struct sigqueue *sigq;		/* signal queue entry. */
	union {
		struct {
			struct timer_list timer;
			struct list_head abs_timer_entry; /* clock abs_timer_list */
			struct timespec wall_to_prev;   /* wall_to_monotonic used when set */
			unsigned long incr; /* interval in jiffies */
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

struct k_clock_abs {
	struct list_head list;
	spinlock_t lock;
};
struct k_clock {
	int res;		/* in nano seconds */
	int (*clock_getres) (clockid_t which_clock, struct timespec *tp);
	struct k_clock_abs *abs_struct;
	int (*clock_set) (clockid_t which_clock, struct timespec * tp);
	int (*clock_get) (clockid_t which_clock, struct timespec * tp);
	int (*timer_create) (struct k_itimer *timer);
	int (*nsleep) (clockid_t which_clock, int flags, struct timespec *);
	int (*timer_set) (struct k_itimer * timr, int flags,
			  struct itimerspec * new_setting,
			  struct itimerspec * old_setting);
	int (*timer_del) (struct k_itimer * timr);
#define TIMER_RETRY 1
	void (*timer_get) (struct k_itimer * timr,
			   struct itimerspec * cur_setting);
};

void register_posix_clock(clockid_t clock_id, struct k_clock *new_clock);

/* Error handlers for timer_create, nanosleep and settime */
int do_posix_clock_notimer_create(struct k_itimer *timer);
int do_posix_clock_nonanosleep(clockid_t, int flags, struct timespec *);
int do_posix_clock_nosettime(clockid_t, struct timespec *tp);

/* function to call to trigger timer event */
int posix_timer_event(struct k_itimer *timr, int si_private);

struct now_struct {
	unsigned long jiffies;
};

#define posix_get_now(now) (now)->jiffies = jiffies;
#define posix_time_before(timer, now) \
                      time_before((timer)->expires, (now)->jiffies)

#define posix_bump_timer(timr, now)					\
         do {								\
              long delta, orun;						\
	      delta = now.jiffies - (timr)->it.real.timer.expires;	\
              if (delta >= 0) {						\
	           orun = 1 + (delta / (timr)->it.real.incr);		\
	          (timr)->it.real.timer.expires +=			\
			 orun * (timr)->it.real.incr;			\
                  (timr)->it_overrun += orun;				\
              }								\
            }while (0)

int posix_cpu_clock_getres(clockid_t which_clock, struct timespec *);
int posix_cpu_clock_get(clockid_t which_clock, struct timespec *);
int posix_cpu_clock_set(clockid_t which_clock, const struct timespec *tp);
int posix_cpu_timer_create(struct k_itimer *);
int posix_cpu_nsleep(clockid_t, int, struct timespec *);
int posix_cpu_timer_set(struct k_itimer *, int,
			struct itimerspec *, struct itimerspec *);
int posix_cpu_timer_del(struct k_itimer *);
void posix_cpu_timer_get(struct k_itimer *, struct itimerspec *);

void posix_cpu_timer_schedule(struct k_itimer *);

void run_posix_cpu_timers(struct task_struct *);
void posix_cpu_timers_exit(struct task_struct *);
void posix_cpu_timers_exit_group(struct task_struct *);

void set_process_cpu_timer(struct task_struct *, unsigned int,
			   cputime_t *, cputime_t *);

#endif
