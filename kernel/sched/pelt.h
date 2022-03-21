#ifdef CONFIG_SMP
#include "sched-pelt.h"

int __update_load_avg_blocked_se(u64 now, struct sched_entity *se);
int __update_load_avg_se(u64 now, struct cfs_rq *cfs_rq, struct sched_entity *se);
int __update_load_avg_cfs_rq(u64 now, struct cfs_rq *cfs_rq);
int update_rt_rq_load_avg(u64 now, struct rq *rq, int running);
int update_dl_rq_load_avg(u64 now, struct rq *rq, int running);

#ifdef CONFIG_SCHED_THERMAL_PRESSURE
int update_thermal_load_avg(u64 now, struct rq *rq, u64 capacity);

static inline u64 thermal_load_avg(struct rq *rq)
{
	return READ_ONCE(rq->avg_thermal.load_avg);
}
#else
static inline int
update_thermal_load_avg(u64 now, struct rq *rq, u64 capacity)
{
	return 0;
}

static inline u64 thermal_load_avg(struct rq *rq)
{
	return 0;
}
#endif

#ifdef CONFIG_HAVE_SCHED_AVG_IRQ
int update_irq_load_avg(struct rq *rq, u64 running);
#else
static inline int
update_irq_load_avg(struct rq *rq, u64 running)
{
	return 0;
}
#endif

#define PELT_MIN_DIVIDER	(LOAD_AVG_MAX - 1024)

static inline u32 get_pelt_divider(struct sched_avg *avg)
{
	return PELT_MIN_DIVIDER + avg->period_contrib;
}

static inline void cfs_se_util_change(struct sched_avg *avg)
{
	unsigned int enqueued;

	if (!sched_feat(UTIL_EST))
		return;

	/* Avoid store if the flag has been already reset */
	enqueued = avg->util_est.enqueued;
	if (!(enqueued & UTIL_AVG_UNCHANGED))
		return;

	/* Reset flag to report util_avg has been updated */
	enqueued &= ~UTIL_AVG_UNCHANGED;
	WRITE_ONCE(avg->util_est.enqueued, enqueued);
}

/*
 * The clock_pelt scales the time to reflect the effective amount of
 * computation done during the running delta time but then sync back to
 * clock_task when rq is idle.
 *
 *
 * absolute time   | 1| 2| 3| 4| 5| 6| 7| 8| 9|10|11|12|13|14|15|16
 * @ max capacity  ------******---------------******---------------
 * @ half capacity ------************---------************---------
 * clock pelt      | 1| 2|    3|    4| 7| 8| 9|   10|   11|14|15|16
 *
 */
static inline void update_rq_clock_pelt(struct rq *rq, s64 delta)
{
	if (unlikely(is_idle_task(rq->curr))) {
		/* The rq is idle, we can sync to clock_task */
		rq->clock_pelt  = rq_clock_task(rq);
		return;
	}

	/*
	 * When a rq runs at a lower compute capacity, it will need
	 * more time to do the same amount of work than at max
	 * capacity. In order to be invariant, we scale the delta to
	 * reflect how much work has been really done.
	 * Running longer results in stealing idle time that will
	 * disturb the load signal compared to max capacity. This
	 * stolen idle time will be automatically reflected when the
	 * rq will be idle and the clock will be synced with
	 * rq_clock_task.
	 */

	/*
	 * Scale the elapsed time to reflect the real amount of
	 * computation
	 */
	delta = cap_scale(delta, arch_scale_cpu_capacity(cpu_of(rq)));
	delta = cap_scale(delta, arch_scale_freq_capacity(cpu_of(rq)));

	rq->clock_pelt += delta;
}

/*
 * When rq becomes idle, we have to check if it has lost idle time
 * because it was fully busy. A rq is fully used when the /Sum util_sum
 * is greater or equal to:
 * (LOAD_AVG_MAX - 1024 + rq->cfs.avg.period_contrib) << SCHED_CAPACITY_SHIFT;
 * For optimization and computing rounding purpose, we don't take into account
 * the position in the current window (period_contrib) and we use the higher
 * bound of util_sum to decide.
 */
static inline void update_idle_rq_clock_pelt(struct rq *rq)
{
	u32 divider = ((LOAD_AVG_MAX - 1024) << SCHED_CAPACITY_SHIFT) - LOAD_AVG_MAX;
	u32 util_sum = rq->cfs.avg.util_sum;
	util_sum += rq->avg_rt.util_sum;
	util_sum += rq->avg_dl.util_sum;

	/*
	 * Reflecting stolen time makes sense only if the idle
	 * phase would be present at max capacity. As soon as the
	 * utilization of a rq has reached the maximum value, it is
	 * considered as an always runnig rq without idle time to
	 * steal. This potential idle time is considered as lost in
	 * this case. We keep track of this lost idle time compare to
	 * rq's clock_task.
	 */
	if (util_sum >= divider)
		rq->lost_idle_time += rq_clock_task(rq) - rq->clock_pelt;
}

static inline u64 rq_clock_pelt(struct rq *rq)
{
	lockdep_assert_held(&rq->lock);
	assert_clock_updated(rq);

	return rq->clock_pelt - rq->lost_idle_time;
}

#ifdef CONFIG_CFS_BANDWIDTH
/* rq->task_clock normalized against any time this cfs_rq has spent throttled */
static inline u64 cfs_rq_clock_pelt(struct cfs_rq *cfs_rq)
{
	if (unlikely(cfs_rq->throttle_count))
		return cfs_rq->throttled_clock_task - cfs_rq->throttled_clock_task_time;

	return rq_clock_pelt(rq_of(cfs_rq)) - cfs_rq->throttled_clock_task_time;
}
#else
static inline u64 cfs_rq_clock_pelt(struct cfs_rq *cfs_rq)
{
	return rq_clock_pelt(rq_of(cfs_rq));
}
#endif

#else

static inline int
update_cfs_rq_load_avg(u64 now, struct cfs_rq *cfs_rq)
{
	return 0;
}

static inline int
update_rt_rq_load_avg(u64 now, struct rq *rq, int running)
{
	return 0;
}

static inline int
update_dl_rq_load_avg(u64 now, struct rq *rq, int running)
{
	return 0;
}

static inline int
update_thermal_load_avg(u64 now, struct rq *rq, u64 capacity)
{
	return 0;
}

static inline u64 thermal_load_avg(struct rq *rq)
{
	return 0;
}

static inline int
update_irq_load_avg(struct rq *rq, u64 running)
{
	return 0;
}

static inline u64 rq_clock_pelt(struct rq *rq)
{
	return rq_clock_task(rq);
}

static inline void
update_rq_clock_pelt(struct rq *rq, s64 delta) { }

static inline void
update_idle_rq_clock_pelt(struct rq *rq) { }

#endif


