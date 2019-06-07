/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * Authors: Waiman Long <longman@redhat.com>
 */

#include "lock_events.h"

#ifdef CONFIG_LOCK_EVENT_COUNTS
#ifdef CONFIG_PARAVIRT_SPINLOCKS
/*
 * Collect pvqspinlock locking event counts
 */
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/fs.h>

#define EVENT_COUNT(ev)	lockevents[LOCKEVENT_ ## ev]

/*
 * PV specific per-cpu counter
 */
static DEFINE_PER_CPU(u64, pv_kick_time);

/*
 * Function to read and return the PV qspinlock counts.
 *
 * The following counters are handled specially:
 * 1. pv_latency_kick
 *    Average kick latency (ns) = pv_latency_kick/pv_kick_unlock
 * 2. pv_latency_wake
 *    Average wake latency (ns) = pv_latency_wake/pv_kick_wake
 * 3. pv_hash_hops
 *    Average hops/hash = pv_hash_hops/pv_kick_unlock
 */
ssize_t lockevent_read(struct file *file, char __user *user_buf,
		       size_t count, loff_t *ppos)
{
	char buf[64];
	int cpu, id, len;
	u64 sum = 0, kicks = 0;

	/*
	 * Get the counter ID stored in file->f_inode->i_private
	 */
	id = (long)file_inode(file)->i_private;

	if (id >= lockevent_num)
		return -EBADF;

	for_each_possible_cpu(cpu) {
		sum += per_cpu(lockevents[id], cpu);
		/*
		 * Need to sum additional counters for some of them
		 */
		switch (id) {

		case LOCKEVENT_pv_latency_kick:
		case LOCKEVENT_pv_hash_hops:
			kicks += per_cpu(EVENT_COUNT(pv_kick_unlock), cpu);
			break;

		case LOCKEVENT_pv_latency_wake:
			kicks += per_cpu(EVENT_COUNT(pv_kick_wake), cpu);
			break;
		}
	}

	if (id == LOCKEVENT_pv_hash_hops) {
		u64 frac = 0;

		if (kicks) {
			frac = 100ULL * do_div(sum, kicks);
			frac = DIV_ROUND_CLOSEST_ULL(frac, kicks);
		}

		/*
		 * Return a X.XX decimal number
		 */
		len = snprintf(buf, sizeof(buf) - 1, "%llu.%02llu\n",
			       sum, frac);
	} else {
		/*
		 * Round to the nearest ns
		 */
		if ((id == LOCKEVENT_pv_latency_kick) ||
		    (id == LOCKEVENT_pv_latency_wake)) {
			if (kicks)
				sum = DIV_ROUND_CLOSEST_ULL(sum, kicks);
		}
		len = snprintf(buf, sizeof(buf) - 1, "%llu\n", sum);
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

/*
 * PV hash hop count
 */
static inline void lockevent_pv_hop(int hopcnt)
{
	this_cpu_add(EVENT_COUNT(pv_hash_hops), hopcnt);
}

/*
 * Replacement function for pv_kick()
 */
static inline void __pv_kick(int cpu)
{
	u64 start = sched_clock();

	per_cpu(pv_kick_time, cpu) = start;
	pv_kick(cpu);
	this_cpu_add(EVENT_COUNT(pv_latency_kick), sched_clock() - start);
}

/*
 * Replacement function for pv_wait()
 */
static inline void __pv_wait(u8 *ptr, u8 val)
{
	u64 *pkick_time = this_cpu_ptr(&pv_kick_time);

	*pkick_time = 0;
	pv_wait(ptr, val);
	if (*pkick_time) {
		this_cpu_add(EVENT_COUNT(pv_latency_wake),
			     sched_clock() - *pkick_time);
		lockevent_inc(pv_kick_wake);
	}
}

#define pv_kick(c)	__pv_kick(c)
#define pv_wait(p, v)	__pv_wait(p, v)

#endif /* CONFIG_PARAVIRT_SPINLOCKS */

#else /* CONFIG_LOCK_EVENT_COUNTS */

static inline void lockevent_pv_hop(int hopcnt)	{ }

#endif /* CONFIG_LOCK_EVENT_COUNTS */
