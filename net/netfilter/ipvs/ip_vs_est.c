// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ip_vs_est.c: simple rate estimator for IPVS
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *
 * Changes:     Hans Schillstrom <hans.schillstrom@ericsson.com>
 *              Network name space (netns) aware.
 *              Global data moved to netns i.e struct netns_ipvs
 *              Affected data: est_list and est_lock.
 *              estimation_timer() runs with timer per netns.
 *              get_stats()) do the per cpu summing.
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/sysctl.h>
#include <linux/list.h>
#include <linux/rcupdate_wait.h>

#include <net/ip_vs.h>

/*
  This code is to estimate rate in a shorter interval (such as 8
  seconds) for virtual services and real servers. For measure rate in a
  long interval, it is easy to implement a user level daemon which
  periodically reads those statistical counters and measure rate.

  We measure rate during the last 8 seconds every 2 seconds:

    avgrate = avgrate*(1-W) + rate*W

    where W = 2^(-2)

  NOTES.

  * Average bps is scaled by 2^5, while average pps and cps are scaled by 2^10.

  * Netlink users can see 64-bit values but sockopt users are restricted
    to 32-bit values for conns, packets, bps, cps and pps.

  * A lot of code is taken from net/core/gen_estimator.c

  KEY POINTS:
  - cpustats counters are updated per-cpu in SoftIRQ context with BH disabled
  - kthreads read the cpustats to update the estimators (svcs, dests, total)
  - the states of estimators can be read (get stats) or modified (zero stats)
    from processes

  KTHREADS:
  - estimators are added initially to est_temp_list and later kthread 0
    distributes them to one or many kthreads for estimation
  - kthread contexts are created and attached to array
  - the kthread tasks are started when first service is added, before that
    the total stats are not estimated
  - when configuration (cpulist/nice) is changed, the tasks are restarted
    by work (est_reload_work)
  - kthread tasks are stopped while the cpulist is empty
  - the kthread context holds lists with estimators (chains) which are
    processed every 2 seconds
  - as estimators can be added dynamically and in bursts, we try to spread
    them to multiple chains which are estimated at different time
  - on start, kthread 0 enters calculation phase to determine the chain limits
    and the limit of estimators per kthread
  - est_add_ktid: ktid where to add new ests, can point to empty slot where
    we should add kt data
 */

static struct lock_class_key __ipvs_est_key;

static void ip_vs_est_calc_phase(struct netns_ipvs *ipvs);
static void ip_vs_est_drain_temp_list(struct netns_ipvs *ipvs);

static void ip_vs_chain_estimation(struct hlist_head *chain)
{
	struct ip_vs_estimator *e;
	struct ip_vs_cpu_stats *c;
	struct ip_vs_stats *s;
	u64 rate;

	hlist_for_each_entry_rcu(e, chain, list) {
		u64 conns, inpkts, outpkts, inbytes, outbytes;
		u64 kconns = 0, kinpkts = 0, koutpkts = 0;
		u64 kinbytes = 0, koutbytes = 0;
		unsigned int start;
		int i;

		if (kthread_should_stop())
			break;

		s = container_of(e, struct ip_vs_stats, est);
		for_each_possible_cpu(i) {
			c = per_cpu_ptr(s->cpustats, i);
			do {
				start = u64_stats_fetch_begin(&c->syncp);
				conns = u64_stats_read(&c->cnt.conns);
				inpkts = u64_stats_read(&c->cnt.inpkts);
				outpkts = u64_stats_read(&c->cnt.outpkts);
				inbytes = u64_stats_read(&c->cnt.inbytes);
				outbytes = u64_stats_read(&c->cnt.outbytes);
			} while (u64_stats_fetch_retry(&c->syncp, start));
			kconns += conns;
			kinpkts += inpkts;
			koutpkts += outpkts;
			kinbytes += inbytes;
			koutbytes += outbytes;
		}

		spin_lock(&s->lock);

		s->kstats.conns = kconns;
		s->kstats.inpkts = kinpkts;
		s->kstats.outpkts = koutpkts;
		s->kstats.inbytes = kinbytes;
		s->kstats.outbytes = koutbytes;

		/* scaled by 2^10, but divided 2 seconds */
		rate = (s->kstats.conns - e->last_conns) << 9;
		e->last_conns = s->kstats.conns;
		e->cps += ((s64)rate - (s64)e->cps) >> 2;

		rate = (s->kstats.inpkts - e->last_inpkts) << 9;
		e->last_inpkts = s->kstats.inpkts;
		e->inpps += ((s64)rate - (s64)e->inpps) >> 2;

		rate = (s->kstats.outpkts - e->last_outpkts) << 9;
		e->last_outpkts = s->kstats.outpkts;
		e->outpps += ((s64)rate - (s64)e->outpps) >> 2;

		/* scaled by 2^5, but divided 2 seconds */
		rate = (s->kstats.inbytes - e->last_inbytes) << 4;
		e->last_inbytes = s->kstats.inbytes;
		e->inbps += ((s64)rate - (s64)e->inbps) >> 2;

		rate = (s->kstats.outbytes - e->last_outbytes) << 4;
		e->last_outbytes = s->kstats.outbytes;
		e->outbps += ((s64)rate - (s64)e->outbps) >> 2;
		spin_unlock(&s->lock);
	}
}

static void ip_vs_tick_estimation(struct ip_vs_est_kt_data *kd, int row)
{
	struct ip_vs_est_tick_data *td;
	int cid;

	rcu_read_lock();
	td = rcu_dereference(kd->ticks[row]);
	if (!td)
		goto out;
	for_each_set_bit(cid, td->present, IPVS_EST_TICK_CHAINS) {
		if (kthread_should_stop())
			break;
		ip_vs_chain_estimation(&td->chains[cid]);
		cond_resched_rcu();
		td = rcu_dereference(kd->ticks[row]);
		if (!td)
			break;
	}

out:
	rcu_read_unlock();
}

static int ip_vs_estimation_kthread(void *data)
{
	struct ip_vs_est_kt_data *kd = data;
	struct netns_ipvs *ipvs = kd->ipvs;
	int row = kd->est_row;
	unsigned long now;
	int id = kd->id;
	long gap;

	if (id > 0) {
		if (!ipvs->est_chain_max)
			return 0;
	} else {
		if (!ipvs->est_chain_max) {
			ipvs->est_calc_phase = 1;
			/* commit est_calc_phase before reading est_genid */
			smp_mb();
		}

		/* kthread 0 will handle the calc phase */
		if (ipvs->est_calc_phase)
			ip_vs_est_calc_phase(ipvs);
	}

	while (1) {
		if (!id && !hlist_empty(&ipvs->est_temp_list))
			ip_vs_est_drain_temp_list(ipvs);
		set_current_state(TASK_IDLE);
		if (kthread_should_stop())
			break;

		/* before estimation, check if we should sleep */
		now = jiffies;
		gap = kd->est_timer - now;
		if (gap > 0) {
			if (gap > IPVS_EST_TICK) {
				kd->est_timer = now - IPVS_EST_TICK;
				gap = IPVS_EST_TICK;
			}
			schedule_timeout(gap);
		} else {
			__set_current_state(TASK_RUNNING);
			if (gap < -8 * IPVS_EST_TICK)
				kd->est_timer = now;
		}

		if (kd->tick_len[row])
			ip_vs_tick_estimation(kd, row);

		row++;
		if (row >= IPVS_EST_NTICKS)
			row = 0;
		WRITE_ONCE(kd->est_row, row);
		kd->est_timer += IPVS_EST_TICK;
	}
	__set_current_state(TASK_RUNNING);

	return 0;
}

/* Schedule stop/start for kthread tasks */
void ip_vs_est_reload_start(struct netns_ipvs *ipvs)
{
	/* Ignore reloads before first service is added */
	if (!READ_ONCE(ipvs->enable))
		return;
	ip_vs_est_stopped_recalc(ipvs);
	/* Bump the kthread configuration genid */
	atomic_inc(&ipvs->est_genid);
	queue_delayed_work(system_long_wq, &ipvs->est_reload_work, 0);
}

/* Start kthread task with current configuration */
int ip_vs_est_kthread_start(struct netns_ipvs *ipvs,
			    struct ip_vs_est_kt_data *kd)
{
	unsigned long now;
	int ret = 0;
	long gap;

	lockdep_assert_held(&ipvs->est_mutex);

	if (kd->task)
		goto out;
	now = jiffies;
	gap = kd->est_timer - now;
	/* Sync est_timer if task is starting later */
	if (abs(gap) > 4 * IPVS_EST_TICK)
		kd->est_timer = now;
	kd->task = kthread_create(ip_vs_estimation_kthread, kd, "ipvs-e:%d:%d",
				  ipvs->gen, kd->id);
	if (IS_ERR(kd->task)) {
		ret = PTR_ERR(kd->task);
		kd->task = NULL;
		goto out;
	}

	set_user_nice(kd->task, sysctl_est_nice(ipvs));
	if (sysctl_est_preferred_cpulist(ipvs))
		kthread_affine_preferred(kd->task, sysctl_est_preferred_cpulist(ipvs));

	pr_info("starting estimator thread %d...\n", kd->id);
	wake_up_process(kd->task);

out:
	return ret;
}

void ip_vs_est_kthread_stop(struct ip_vs_est_kt_data *kd)
{
	if (kd->task) {
		pr_info("stopping estimator thread %d...\n", kd->id);
		kthread_stop(kd->task);
		kd->task = NULL;
	}
}

/* Apply parameters to kthread */
static void ip_vs_est_set_params(struct netns_ipvs *ipvs,
				 struct ip_vs_est_kt_data *kd)
{
	kd->chain_max = ipvs->est_chain_max;
	/* We are using single chain on RCU preemption */
	if (IPVS_EST_TICK_CHAINS == 1)
		kd->chain_max *= IPVS_EST_CHAIN_FACTOR;
	kd->tick_max = IPVS_EST_TICK_CHAINS * kd->chain_max;
	kd->est_max_count = IPVS_EST_NTICKS * kd->tick_max;
}

/* Create and start estimation kthread in a free or new array slot */
static int ip_vs_est_add_kthread(struct netns_ipvs *ipvs)
{
	struct ip_vs_est_kt_data *kd = NULL;
	int id = ipvs->est_kt_count;
	int ret = -ENOMEM;
	void *arr = NULL;
	int i;

	if ((unsigned long)ipvs->est_kt_count >= ipvs->est_max_threads &&
	    READ_ONCE(ipvs->enable) && ipvs->est_max_threads)
		return -EINVAL;

	mutex_lock(&ipvs->est_mutex);

	for (i = 0; i < id; i++) {
		if (!ipvs->est_kt_arr[i])
			break;
	}
	if (i >= id) {
		arr = krealloc_array(ipvs->est_kt_arr, id + 1,
				     sizeof(struct ip_vs_est_kt_data *),
				     GFP_KERNEL);
		if (!arr)
			goto out;
		ipvs->est_kt_arr = arr;
	} else {
		id = i;
	}

	kd = kzalloc(sizeof(*kd), GFP_KERNEL);
	if (!kd)
		goto out;
	kd->ipvs = ipvs;
	bitmap_fill(kd->avail, IPVS_EST_NTICKS);
	kd->est_timer = jiffies;
	kd->id = id;
	ip_vs_est_set_params(ipvs, kd);

	/* Pre-allocate stats used in calc phase */
	if (!id && !kd->calc_stats) {
		kd->calc_stats = ip_vs_stats_alloc();
		if (!kd->calc_stats)
			goto out;
	}

	/* Start kthread tasks only when services are present */
	if (READ_ONCE(ipvs->enable) && !ip_vs_est_stopped(ipvs)) {
		ret = ip_vs_est_kthread_start(ipvs, kd);
		if (ret < 0)
			goto out;
	}

	if (arr)
		ipvs->est_kt_count++;
	ipvs->est_kt_arr[id] = kd;
	kd = NULL;
	/* Use most recent kthread for new ests */
	ipvs->est_add_ktid = id;
	ret = 0;

out:
	mutex_unlock(&ipvs->est_mutex);
	if (kd) {
		ip_vs_stats_free(kd->calc_stats);
		kfree(kd);
	}

	return ret;
}

/* Select ktid where to add new ests: available, unused or new slot */
static void ip_vs_est_update_ktid(struct netns_ipvs *ipvs)
{
	int ktid, best = ipvs->est_kt_count;
	struct ip_vs_est_kt_data *kd;

	for (ktid = 0; ktid < ipvs->est_kt_count; ktid++) {
		kd = ipvs->est_kt_arr[ktid];
		if (kd) {
			if (kd->est_count < kd->est_max_count) {
				best = ktid;
				break;
			}
		} else if (ktid < best) {
			best = ktid;
		}
	}
	ipvs->est_add_ktid = best;
}

/* Add estimator to current kthread (est_add_ktid) */
static int ip_vs_enqueue_estimator(struct netns_ipvs *ipvs,
				   struct ip_vs_estimator *est)
{
	struct ip_vs_est_kt_data *kd = NULL;
	struct ip_vs_est_tick_data *td;
	int ktid, row, crow, cid, ret;
	int delay = est->ktrow;

	BUILD_BUG_ON_MSG(IPVS_EST_TICK_CHAINS > 127,
			 "Too many chains for ktcid");

	if (ipvs->est_add_ktid < ipvs->est_kt_count) {
		kd = ipvs->est_kt_arr[ipvs->est_add_ktid];
		if (kd)
			goto add_est;
	}

	ret = ip_vs_est_add_kthread(ipvs);
	if (ret < 0)
		goto out;
	kd = ipvs->est_kt_arr[ipvs->est_add_ktid];

add_est:
	ktid = kd->id;
	/* For small number of estimators prefer to use few ticks,
	 * otherwise try to add into the last estimated row.
	 * est_row and add_row point after the row we should use
	 */
	if (kd->est_count >= 2 * kd->tick_max || delay < IPVS_EST_NTICKS - 1)
		crow = READ_ONCE(kd->est_row);
	else
		crow = kd->add_row;
	crow += delay;
	if (crow >= IPVS_EST_NTICKS)
		crow -= IPVS_EST_NTICKS;
	/* Assume initial delay ? */
	if (delay >= IPVS_EST_NTICKS - 1) {
		/* Preserve initial delay or decrease it if no space in tick */
		row = crow;
		if (crow < IPVS_EST_NTICKS - 1) {
			crow++;
			row = find_last_bit(kd->avail, crow);
		}
		if (row >= crow)
			row = find_last_bit(kd->avail, IPVS_EST_NTICKS);
	} else {
		/* Preserve delay or increase it if no space in tick */
		row = IPVS_EST_NTICKS;
		if (crow > 0)
			row = find_next_bit(kd->avail, IPVS_EST_NTICKS, crow);
		if (row >= IPVS_EST_NTICKS)
			row = find_first_bit(kd->avail, IPVS_EST_NTICKS);
	}

	td = rcu_dereference_protected(kd->ticks[row], 1);
	if (!td) {
		td = kzalloc(sizeof(*td), GFP_KERNEL);
		if (!td) {
			ret = -ENOMEM;
			goto out;
		}
		rcu_assign_pointer(kd->ticks[row], td);
	}

	cid = find_first_zero_bit(td->full, IPVS_EST_TICK_CHAINS);

	kd->est_count++;
	kd->tick_len[row]++;
	if (!td->chain_len[cid])
		__set_bit(cid, td->present);
	td->chain_len[cid]++;
	est->ktid = ktid;
	est->ktrow = row;
	est->ktcid = cid;
	hlist_add_head_rcu(&est->list, &td->chains[cid]);

	if (td->chain_len[cid] >= kd->chain_max) {
		__set_bit(cid, td->full);
		if (kd->tick_len[row] >= kd->tick_max)
			__clear_bit(row, kd->avail);
	}

	/* Update est_add_ktid to point to first available/empty kt slot */
	if (kd->est_count == kd->est_max_count)
		ip_vs_est_update_ktid(ipvs);

	ret = 0;

out:
	return ret;
}

/* Start estimation for stats */
int ip_vs_start_estimator(struct netns_ipvs *ipvs, struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *est = &stats->est;
	int ret;

	if (!ipvs->est_max_threads && READ_ONCE(ipvs->enable))
		ipvs->est_max_threads = ip_vs_est_max_threads(ipvs);

	est->ktid = -1;
	est->ktrow = IPVS_EST_NTICKS - 1;	/* Initial delay */

	/* We prefer this code to be short, kthread 0 will requeue the
	 * estimator to available chain. If tasks are disabled, we
	 * will not allocate much memory, just for kt 0.
	 */
	ret = 0;
	if (!ipvs->est_kt_count || !ipvs->est_kt_arr[0])
		ret = ip_vs_est_add_kthread(ipvs);
	if (ret >= 0)
		hlist_add_head(&est->list, &ipvs->est_temp_list);
	else
		INIT_HLIST_NODE(&est->list);
	return ret;
}

static void ip_vs_est_kthread_destroy(struct ip_vs_est_kt_data *kd)
{
	if (kd) {
		if (kd->task) {
			pr_info("stop unused estimator thread %d...\n", kd->id);
			kthread_stop(kd->task);
		}
		ip_vs_stats_free(kd->calc_stats);
		kfree(kd);
	}
}

/* Unlink estimator from chain */
void ip_vs_stop_estimator(struct netns_ipvs *ipvs, struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *est = &stats->est;
	struct ip_vs_est_tick_data *td;
	struct ip_vs_est_kt_data *kd;
	int ktid = est->ktid;
	int row = est->ktrow;
	int cid = est->ktcid;

	/* Failed to add to chain ? */
	if (hlist_unhashed(&est->list))
		return;

	/* On return, estimator can be freed, dequeue it now */

	/* In est_temp_list ? */
	if (ktid < 0) {
		hlist_del(&est->list);
		goto end_kt0;
	}

	hlist_del_rcu(&est->list);
	kd = ipvs->est_kt_arr[ktid];
	td = rcu_dereference_protected(kd->ticks[row], 1);
	__clear_bit(cid, td->full);
	td->chain_len[cid]--;
	if (!td->chain_len[cid])
		__clear_bit(cid, td->present);
	kd->tick_len[row]--;
	__set_bit(row, kd->avail);
	if (!kd->tick_len[row]) {
		RCU_INIT_POINTER(kd->ticks[row], NULL);
		kfree_rcu(td, rcu_head);
	}
	kd->est_count--;
	if (kd->est_count) {
		/* This kt slot can become available just now, prefer it */
		if (ktid < ipvs->est_add_ktid)
			ipvs->est_add_ktid = ktid;
		return;
	}

	if (ktid > 0) {
		mutex_lock(&ipvs->est_mutex);
		ip_vs_est_kthread_destroy(kd);
		ipvs->est_kt_arr[ktid] = NULL;
		if (ktid == ipvs->est_kt_count - 1) {
			ipvs->est_kt_count--;
			while (ipvs->est_kt_count > 1 &&
			       !ipvs->est_kt_arr[ipvs->est_kt_count - 1])
				ipvs->est_kt_count--;
		}
		mutex_unlock(&ipvs->est_mutex);

		/* This slot is now empty, prefer another available kt slot */
		if (ktid == ipvs->est_add_ktid)
			ip_vs_est_update_ktid(ipvs);
	}

end_kt0:
	/* kt 0 is freed after all other kthreads and chains are empty */
	if (ipvs->est_kt_count == 1 && hlist_empty(&ipvs->est_temp_list)) {
		kd = ipvs->est_kt_arr[0];
		if (!kd || !kd->est_count) {
			mutex_lock(&ipvs->est_mutex);
			if (kd) {
				ip_vs_est_kthread_destroy(kd);
				ipvs->est_kt_arr[0] = NULL;
			}
			ipvs->est_kt_count--;
			mutex_unlock(&ipvs->est_mutex);
			ipvs->est_add_ktid = 0;
		}
	}
}

/* Register all ests from est_temp_list to kthreads */
static void ip_vs_est_drain_temp_list(struct netns_ipvs *ipvs)
{
	struct ip_vs_estimator *est;

	while (1) {
		int max = 16;

		mutex_lock(&__ip_vs_mutex);

		while (max-- > 0) {
			est = hlist_entry_safe(ipvs->est_temp_list.first,
					       struct ip_vs_estimator, list);
			if (est) {
				if (kthread_should_stop())
					goto unlock;
				hlist_del_init(&est->list);
				if (ip_vs_enqueue_estimator(ipvs, est) >= 0)
					continue;
				est->ktid = -1;
				hlist_add_head(&est->list,
					       &ipvs->est_temp_list);
				/* Abort, some entries will not be estimated
				 * until next attempt
				 */
			}
			goto unlock;
		}
		mutex_unlock(&__ip_vs_mutex);
		cond_resched();
	}

unlock:
	mutex_unlock(&__ip_vs_mutex);
}

/* Calculate limits for all kthreads */
static int ip_vs_est_calc_limits(struct netns_ipvs *ipvs, int *chain_max)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);
	struct ip_vs_est_kt_data *kd;
	struct hlist_head chain;
	struct ip_vs_stats *s;
	int cache_factor = 4;
	int i, loops, ntest;
	s32 min_est = 0;
	ktime_t t1, t2;
	int max = 8;
	int ret = 1;
	s64 diff;
	u64 val;

	INIT_HLIST_HEAD(&chain);
	mutex_lock(&__ip_vs_mutex);
	kd = ipvs->est_kt_arr[0];
	mutex_unlock(&__ip_vs_mutex);
	s = kd ? kd->calc_stats : NULL;
	if (!s)
		goto out;
	hlist_add_head(&s->est.list, &chain);

	loops = 1;
	/* Get best result from many tests */
	for (ntest = 0; ntest < 12; ntest++) {
		if (!(ntest & 3)) {
			/* Wait for cpufreq frequency transition */
			wait_event_idle_timeout(wq, kthread_should_stop(),
						HZ / 50);
			if (!READ_ONCE(ipvs->enable) || kthread_should_stop())
				goto stop;
		}

		local_bh_disable();
		rcu_read_lock();

		/* Put stats in cache */
		ip_vs_chain_estimation(&chain);

		t1 = ktime_get();
		for (i = loops * cache_factor; i > 0; i--)
			ip_vs_chain_estimation(&chain);
		t2 = ktime_get();

		rcu_read_unlock();
		local_bh_enable();

		if (!READ_ONCE(ipvs->enable) || kthread_should_stop())
			goto stop;
		cond_resched();

		diff = ktime_to_ns(ktime_sub(t2, t1));
		if (diff <= 1 * NSEC_PER_USEC) {
			/* Do more loops on low time resolution */
			loops *= 2;
			continue;
		}
		if (diff >= NSEC_PER_SEC)
			continue;
		val = diff;
		do_div(val, loops);
		if (!min_est || val < min_est) {
			min_est = val;
			/* goal: 95usec per chain */
			val = 95 * NSEC_PER_USEC;
			if (val >= min_est) {
				do_div(val, min_est);
				max = (int)val;
			} else {
				max = 1;
			}
		}
	}

out:
	if (s)
		hlist_del_init(&s->est.list);
	*chain_max = max;
	return ret;

stop:
	ret = 0;
	goto out;
}

/* Calculate the parameters and apply them in context of kt #0
 * ECP: est_calc_phase
 * ECM: est_chain_max
 * ECP	ECM	Insert Chain	enable	Description
 * ---------------------------------------------------------------------------
 * 0	0	est_temp_list	0	create kt #0 context
 * 0	0	est_temp_list	0->1	service added, start kthread #0 task
 * 0->1	0	est_temp_list	1	kt task #0 started, enters calc phase
 * 1	0	est_temp_list	1	kt #0: determine est_chain_max,
 *					stop tasks, move ests to est_temp_list
 *					and free kd for kthreads 1..last
 * 1->0	0->N	kt chains	1	ests can go to kthreads
 * 0	N	kt chains	1	drain est_temp_list, create new kthread
 *					contexts, start tasks, estimate
 */
static void ip_vs_est_calc_phase(struct netns_ipvs *ipvs)
{
	int genid = atomic_read(&ipvs->est_genid);
	struct ip_vs_est_tick_data *td;
	struct ip_vs_est_kt_data *kd;
	struct ip_vs_estimator *est;
	struct ip_vs_stats *stats;
	int id, row, cid, delay;
	bool last, last_td;
	int chain_max;
	int step;

	if (!ip_vs_est_calc_limits(ipvs, &chain_max))
		return;

	mutex_lock(&__ip_vs_mutex);

	/* Stop all other tasks, so that we can immediately move the
	 * estimators to est_temp_list without RCU grace period
	 */
	mutex_lock(&ipvs->est_mutex);
	for (id = 1; id < ipvs->est_kt_count; id++) {
		/* netns clean up started, abort */
		if (!READ_ONCE(ipvs->enable))
			goto unlock2;
		kd = ipvs->est_kt_arr[id];
		if (!kd)
			continue;
		ip_vs_est_kthread_stop(kd);
	}
	mutex_unlock(&ipvs->est_mutex);

	/* Move all estimators to est_temp_list but carefully,
	 * all estimators and kthread data can be released while
	 * we reschedule. Even for kthread 0.
	 */
	step = 0;

	/* Order entries in est_temp_list in ascending delay, so now
	 * walk delay(desc), id(desc), cid(asc)
	 */
	delay = IPVS_EST_NTICKS;

next_delay:
	delay--;
	if (delay < 0)
		goto end_dequeue;

last_kt:
	/* Destroy contexts backwards */
	id = ipvs->est_kt_count;

next_kt:
	if (!READ_ONCE(ipvs->enable) || kthread_should_stop())
		goto unlock;
	id--;
	if (id < 0)
		goto next_delay;
	kd = ipvs->est_kt_arr[id];
	if (!kd)
		goto next_kt;
	/* kt 0 can exist with empty chains */
	if (!id && kd->est_count <= 1)
		goto next_delay;

	row = kd->est_row + delay;
	if (row >= IPVS_EST_NTICKS)
		row -= IPVS_EST_NTICKS;
	td = rcu_dereference_protected(kd->ticks[row], 1);
	if (!td)
		goto next_kt;

	cid = 0;

walk_chain:
	if (kthread_should_stop())
		goto unlock;
	step++;
	if (!(step & 63)) {
		/* Give chance estimators to be added (to est_temp_list)
		 * and deleted (releasing kthread contexts)
		 */
		mutex_unlock(&__ip_vs_mutex);
		cond_resched();
		mutex_lock(&__ip_vs_mutex);

		/* Current kt released ? */
		if (id >= ipvs->est_kt_count)
			goto last_kt;
		if (kd != ipvs->est_kt_arr[id])
			goto next_kt;
		/* Current td released ? */
		if (td != rcu_dereference_protected(kd->ticks[row], 1))
			goto next_kt;
		/* No fatal changes on the current kd and td */
	}
	est = hlist_entry_safe(td->chains[cid].first, struct ip_vs_estimator,
			       list);
	if (!est) {
		cid++;
		if (cid >= IPVS_EST_TICK_CHAINS)
			goto next_kt;
		goto walk_chain;
	}
	/* We can cheat and increase est_count to protect kt 0 context
	 * from release but we prefer to keep the last estimator
	 */
	last = kd->est_count <= 1;
	/* Do not free kt #0 data */
	if (!id && last)
		goto next_delay;
	last_td = kd->tick_len[row] <= 1;
	stats = container_of(est, struct ip_vs_stats, est);
	ip_vs_stop_estimator(ipvs, stats);
	/* Tasks are stopped, move without RCU grace period */
	est->ktid = -1;
	est->ktrow = row - kd->est_row;
	if (est->ktrow < 0)
		est->ktrow += IPVS_EST_NTICKS;
	hlist_add_head(&est->list, &ipvs->est_temp_list);
	/* kd freed ? */
	if (last)
		goto next_kt;
	/* td freed ? */
	if (last_td)
		goto next_kt;
	goto walk_chain;

end_dequeue:
	/* All estimators removed while calculating ? */
	if (!ipvs->est_kt_count)
		goto unlock;
	kd = ipvs->est_kt_arr[0];
	if (!kd)
		goto unlock;
	kd->add_row = kd->est_row;
	ipvs->est_chain_max = chain_max;
	ip_vs_est_set_params(ipvs, kd);

	pr_info("using max %d ests per chain, %d per kthread\n",
		kd->chain_max, kd->est_max_count);

	/* Try to keep tot_stats in kt0, enqueue it early */
	if (ipvs->tot_stats && !hlist_unhashed(&ipvs->tot_stats->s.est.list) &&
	    ipvs->tot_stats->s.est.ktid == -1) {
		hlist_del(&ipvs->tot_stats->s.est.list);
		hlist_add_head(&ipvs->tot_stats->s.est.list,
			       &ipvs->est_temp_list);
	}

	mutex_lock(&ipvs->est_mutex);

	/* We completed the calc phase, new calc phase not requested */
	if (genid == atomic_read(&ipvs->est_genid))
		ipvs->est_calc_phase = 0;

unlock2:
	mutex_unlock(&ipvs->est_mutex);

unlock:
	mutex_unlock(&__ip_vs_mutex);
}

void ip_vs_zero_estimator(struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *est = &stats->est;
	struct ip_vs_kstats *k = &stats->kstats;

	/* reset counters, caller must hold the stats->lock lock */
	est->last_inbytes = k->inbytes;
	est->last_outbytes = k->outbytes;
	est->last_conns = k->conns;
	est->last_inpkts = k->inpkts;
	est->last_outpkts = k->outpkts;
	est->cps = 0;
	est->inpps = 0;
	est->outpps = 0;
	est->inbps = 0;
	est->outbps = 0;
}

/* Get decoded rates */
void ip_vs_read_estimator(struct ip_vs_kstats *dst, struct ip_vs_stats *stats)
{
	struct ip_vs_estimator *e = &stats->est;

	dst->cps = (e->cps + 0x1FF) >> 10;
	dst->inpps = (e->inpps + 0x1FF) >> 10;
	dst->outpps = (e->outpps + 0x1FF) >> 10;
	dst->inbps = (e->inbps + 0xF) >> 5;
	dst->outbps = (e->outbps + 0xF) >> 5;
}

int __net_init ip_vs_estimator_net_init(struct netns_ipvs *ipvs)
{
	INIT_HLIST_HEAD(&ipvs->est_temp_list);
	ipvs->est_kt_arr = NULL;
	ipvs->est_max_threads = 0;
	ipvs->est_calc_phase = 0;
	ipvs->est_chain_max = 0;
	ipvs->est_kt_count = 0;
	ipvs->est_add_ktid = 0;
	atomic_set(&ipvs->est_genid, 0);
	atomic_set(&ipvs->est_genid_done, 0);
	__mutex_init(&ipvs->est_mutex, "ipvs->est_mutex", &__ipvs_est_key);
	return 0;
}

void __net_exit ip_vs_estimator_net_cleanup(struct netns_ipvs *ipvs)
{
	int i;

	for (i = 0; i < ipvs->est_kt_count; i++)
		ip_vs_est_kthread_destroy(ipvs->est_kt_arr[i]);
	kfree(ipvs->est_kt_arr);
	mutex_destroy(&ipvs->est_mutex);
}
