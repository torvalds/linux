/* SPDX-License-Identifier: GPL-2.0 */
#define TIMER_RETRY 1

struct k_clock {
	int	(*clock_getres)(const clockid_t which_clock,
				struct timespec64 *tp);
	int	(*clock_set)(const clockid_t which_clock,
			     const struct timespec64 *tp);
	/* Returns the clock value in the current time namespace. */
	int	(*clock_get_timespec)(const clockid_t which_clock,
				      struct timespec64 *tp);
	/* Returns the clock value in the root time namespace. */
	ktime_t	(*clock_get_ktime)(const clockid_t which_clock);
	int	(*clock_adj)(const clockid_t which_clock, struct __kernel_timex *tx);
	int	(*timer_create)(struct k_itimer *timer);
	int	(*nsleep)(const clockid_t which_clock, int flags,
			  const struct timespec64 *);
	int	(*timer_set)(struct k_itimer *timr, int flags,
			     struct itimerspec64 *new_setting,
			     struct itimerspec64 *old_setting);
	int	(*timer_del)(struct k_itimer *timr);
	void	(*timer_get)(struct k_itimer *timr,
			     struct itimerspec64 *cur_setting);
	void	(*timer_rearm)(struct k_itimer *timr);
	s64	(*timer_forward)(struct k_itimer *timr, ktime_t now);
	ktime_t	(*timer_remaining)(struct k_itimer *timr, ktime_t now);
	int	(*timer_try_to_cancel)(struct k_itimer *timr);
	void	(*timer_arm)(struct k_itimer *timr, ktime_t expires,
			     bool absolute, bool sigev_none);
	void	(*timer_wait_running)(struct k_itimer *timr);
};

extern const struct k_clock clock_posix_cpu;
extern const struct k_clock clock_posix_dynamic;
extern const struct k_clock clock_process;
extern const struct k_clock clock_thread;
extern const struct k_clock alarm_clock;

int posix_timer_event(struct k_itimer *timr, int si_private);

void common_timer_get(struct k_itimer *timr, struct itimerspec64 *cur_setting);
int common_timer_set(struct k_itimer *timr, int flags,
		     struct itimerspec64 *new_setting,
		     struct itimerspec64 *old_setting);
int common_timer_del(struct k_itimer *timer);
