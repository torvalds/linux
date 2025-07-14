// SPDX-License-Identifier: GPL-2.0-only
/*
 * Resource Director Technology(RDT)
 * - Monitoring code
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Author:
 *    Vikas Shivappa <vikas.shivappa@intel.com>
 *
 * This replaces the cqm.c based on perf but we reuse a lot of
 * code and datastructures originally from Peter Zijlstra and Matt Fleming.
 *
 * More information about RDT be found in the Intel (R) x86 Architecture
 * Software Developer Manual June 2016, volume 3, section 17.17.
 */

#define pr_fmt(fmt)	"resctrl: " fmt

#include <linux/cpu.h>
#include <linux/resctrl.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#include "internal.h"

#define CREATE_TRACE_POINTS

#include "monitor_trace.h"

/**
 * struct rmid_entry - dirty tracking for all RMID.
 * @closid:	The CLOSID for this entry.
 * @rmid:	The RMID for this entry.
 * @busy:	The number of domains with cached data using this RMID.
 * @list:	Member of the rmid_free_lru list when busy == 0.
 *
 * Depending on the architecture the correct monitor is accessed using
 * both @closid and @rmid, or @rmid only.
 *
 * Take the rdtgroup_mutex when accessing.
 */
struct rmid_entry {
	u32				closid;
	u32				rmid;
	int				busy;
	struct list_head		list;
};

/*
 * @rmid_free_lru - A least recently used list of free RMIDs
 *     These RMIDs are guaranteed to have an occupancy less than the
 *     threshold occupancy
 */
static LIST_HEAD(rmid_free_lru);

/*
 * @closid_num_dirty_rmid    The number of dirty RMID each CLOSID has.
 *     Only allocated when CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID is defined.
 *     Indexed by CLOSID. Protected by rdtgroup_mutex.
 */
static u32 *closid_num_dirty_rmid;

/*
 * @rmid_limbo_count - count of currently unused but (potentially)
 *     dirty RMIDs.
 *     This counts RMIDs that no one is currently using but that
 *     may have a occupancy value > resctrl_rmid_realloc_threshold. User can
 *     change the threshold occupancy value.
 */
static unsigned int rmid_limbo_count;

/*
 * @rmid_entry - The entry in the limbo and free lists.
 */
static struct rmid_entry	*rmid_ptrs;

/*
 * This is the threshold cache occupancy in bytes at which we will consider an
 * RMID available for re-allocation.
 */
unsigned int resctrl_rmid_realloc_threshold;

/*
 * This is the maximum value for the reallocation threshold, in bytes.
 */
unsigned int resctrl_rmid_realloc_limit;

/*
 * x86 and arm64 differ in their handling of monitoring.
 * x86's RMID are independent numbers, there is only one source of traffic
 * with an RMID value of '1'.
 * arm64's PMG extends the PARTID/CLOSID space, there are multiple sources of
 * traffic with a PMG value of '1', one for each CLOSID, meaning the RMID
 * value is no longer unique.
 * To account for this, resctrl uses an index. On x86 this is just the RMID,
 * on arm64 it encodes the CLOSID and RMID. This gives a unique number.
 *
 * The domain's rmid_busy_llc and rmid_ptrs[] are sized by index. The arch code
 * must accept an attempt to read every index.
 */
static inline struct rmid_entry *__rmid_entry(u32 idx)
{
	struct rmid_entry *entry;
	u32 closid, rmid;

	entry = &rmid_ptrs[idx];
	resctrl_arch_rmid_idx_decode(idx, &closid, &rmid);

	WARN_ON_ONCE(entry->closid != closid);
	WARN_ON_ONCE(entry->rmid != rmid);

	return entry;
}

static void limbo_release_entry(struct rmid_entry *entry)
{
	lockdep_assert_held(&rdtgroup_mutex);

	rmid_limbo_count--;
	list_add_tail(&entry->list, &rmid_free_lru);

	if (IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID))
		closid_num_dirty_rmid[entry->closid]--;
}

/*
 * Check the RMIDs that are marked as busy for this domain. If the
 * reported LLC occupancy is below the threshold clear the busy bit and
 * decrement the count. If the busy count gets to zero on an RMID, we
 * free the RMID
 */
void __check_limbo(struct rdt_mon_domain *d, bool force_free)
{
	struct rdt_resource *r = resctrl_arch_get_resource(RDT_RESOURCE_L3);
	u32 idx_limit = resctrl_arch_system_num_rmid_idx();
	struct rmid_entry *entry;
	u32 idx, cur_idx = 1;
	void *arch_mon_ctx;
	bool rmid_dirty;
	u64 val = 0;

	arch_mon_ctx = resctrl_arch_mon_ctx_alloc(r, QOS_L3_OCCUP_EVENT_ID);
	if (IS_ERR(arch_mon_ctx)) {
		pr_warn_ratelimited("Failed to allocate monitor context: %ld",
				    PTR_ERR(arch_mon_ctx));
		return;
	}

	/*
	 * Skip RMID 0 and start from RMID 1 and check all the RMIDs that
	 * are marked as busy for occupancy < threshold. If the occupancy
	 * is less than the threshold decrement the busy counter of the
	 * RMID and move it to the free list when the counter reaches 0.
	 */
	for (;;) {
		idx = find_next_bit(d->rmid_busy_llc, idx_limit, cur_idx);
		if (idx >= idx_limit)
			break;

		entry = __rmid_entry(idx);
		if (resctrl_arch_rmid_read(r, d, entry->closid, entry->rmid,
					   QOS_L3_OCCUP_EVENT_ID, &val,
					   arch_mon_ctx)) {
			rmid_dirty = true;
		} else {
			rmid_dirty = (val >= resctrl_rmid_realloc_threshold);

			/*
			 * x86's CLOSID and RMID are independent numbers, so the entry's
			 * CLOSID is an empty CLOSID (X86_RESCTRL_EMPTY_CLOSID). On Arm the
			 * RMID (PMG) extends the CLOSID (PARTID) space with bits that aren't
			 * used to select the configuration. It is thus necessary to track both
			 * CLOSID and RMID because there may be dependencies between them
			 * on some architectures.
			 */
			trace_mon_llc_occupancy_limbo(entry->closid, entry->rmid, d->hdr.id, val);
		}

		if (force_free || !rmid_dirty) {
			clear_bit(idx, d->rmid_busy_llc);
			if (!--entry->busy)
				limbo_release_entry(entry);
		}
		cur_idx = idx + 1;
	}

	resctrl_arch_mon_ctx_free(r, QOS_L3_OCCUP_EVENT_ID, arch_mon_ctx);
}

bool has_busy_rmid(struct rdt_mon_domain *d)
{
	u32 idx_limit = resctrl_arch_system_num_rmid_idx();

	return find_first_bit(d->rmid_busy_llc, idx_limit) != idx_limit;
}

static struct rmid_entry *resctrl_find_free_rmid(u32 closid)
{
	struct rmid_entry *itr;
	u32 itr_idx, cmp_idx;

	if (list_empty(&rmid_free_lru))
		return rmid_limbo_count ? ERR_PTR(-EBUSY) : ERR_PTR(-ENOSPC);

	list_for_each_entry(itr, &rmid_free_lru, list) {
		/*
		 * Get the index of this free RMID, and the index it would need
		 * to be if it were used with this CLOSID.
		 * If the CLOSID is irrelevant on this architecture, the two
		 * index values are always the same on every entry and thus the
		 * very first entry will be returned.
		 */
		itr_idx = resctrl_arch_rmid_idx_encode(itr->closid, itr->rmid);
		cmp_idx = resctrl_arch_rmid_idx_encode(closid, itr->rmid);

		if (itr_idx == cmp_idx)
			return itr;
	}

	return ERR_PTR(-ENOSPC);
}

/**
 * resctrl_find_cleanest_closid() - Find a CLOSID where all the associated
 *                                  RMID are clean, or the CLOSID that has
 *                                  the most clean RMID.
 *
 * MPAM's equivalent of RMID are per-CLOSID, meaning a freshly allocated CLOSID
 * may not be able to allocate clean RMID. To avoid this the allocator will
 * choose the CLOSID with the most clean RMID.
 *
 * When the CLOSID and RMID are independent numbers, the first free CLOSID will
 * be returned.
 */
int resctrl_find_cleanest_closid(void)
{
	u32 cleanest_closid = ~0;
	int i = 0;

	lockdep_assert_held(&rdtgroup_mutex);

	if (!IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID))
		return -EIO;

	for (i = 0; i < closids_supported(); i++) {
		int num_dirty;

		if (closid_allocated(i))
			continue;

		num_dirty = closid_num_dirty_rmid[i];
		if (num_dirty == 0)
			return i;

		if (cleanest_closid == ~0)
			cleanest_closid = i;

		if (num_dirty < closid_num_dirty_rmid[cleanest_closid])
			cleanest_closid = i;
	}

	if (cleanest_closid == ~0)
		return -ENOSPC;

	return cleanest_closid;
}

/*
 * For MPAM the RMID value is not unique, and has to be considered with
 * the CLOSID. The (CLOSID, RMID) pair is allocated on all domains, which
 * allows all domains to be managed by a single free list.
 * Each domain also has a rmid_busy_llc to reduce the work of the limbo handler.
 */
int alloc_rmid(u32 closid)
{
	struct rmid_entry *entry;

	lockdep_assert_held(&rdtgroup_mutex);

	entry = resctrl_find_free_rmid(closid);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	list_del(&entry->list);
	return entry->rmid;
}

static void add_rmid_to_limbo(struct rmid_entry *entry)
{
	struct rdt_resource *r = resctrl_arch_get_resource(RDT_RESOURCE_L3);
	struct rdt_mon_domain *d;
	u32 idx;

	lockdep_assert_held(&rdtgroup_mutex);

	/* Walking r->domains, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

	idx = resctrl_arch_rmid_idx_encode(entry->closid, entry->rmid);

	entry->busy = 0;
	list_for_each_entry(d, &r->mon_domains, hdr.list) {
		/*
		 * For the first limbo RMID in the domain,
		 * setup up the limbo worker.
		 */
		if (!has_busy_rmid(d))
			cqm_setup_limbo_handler(d, CQM_LIMBOCHECK_INTERVAL,
						RESCTRL_PICK_ANY_CPU);
		set_bit(idx, d->rmid_busy_llc);
		entry->busy++;
	}

	rmid_limbo_count++;
	if (IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID))
		closid_num_dirty_rmid[entry->closid]++;
}

void free_rmid(u32 closid, u32 rmid)
{
	u32 idx = resctrl_arch_rmid_idx_encode(closid, rmid);
	struct rmid_entry *entry;

	lockdep_assert_held(&rdtgroup_mutex);

	/*
	 * Do not allow the default rmid to be free'd. Comparing by index
	 * allows architectures that ignore the closid parameter to avoid an
	 * unnecessary check.
	 */
	if (!resctrl_arch_mon_capable() ||
	    idx == resctrl_arch_rmid_idx_encode(RESCTRL_RESERVED_CLOSID,
						RESCTRL_RESERVED_RMID))
		return;

	entry = __rmid_entry(idx);

	if (resctrl_arch_is_llc_occupancy_enabled())
		add_rmid_to_limbo(entry);
	else
		list_add_tail(&entry->list, &rmid_free_lru);
}

static struct mbm_state *get_mbm_state(struct rdt_mon_domain *d, u32 closid,
				       u32 rmid, enum resctrl_event_id evtid)
{
	u32 idx = resctrl_arch_rmid_idx_encode(closid, rmid);

	switch (evtid) {
	case QOS_L3_MBM_TOTAL_EVENT_ID:
		return &d->mbm_total[idx];
	case QOS_L3_MBM_LOCAL_EVENT_ID:
		return &d->mbm_local[idx];
	default:
		return NULL;
	}
}

static int __mon_event_count(u32 closid, u32 rmid, struct rmid_read *rr)
{
	int cpu = smp_processor_id();
	struct rdt_mon_domain *d;
	struct cacheinfo *ci;
	struct mbm_state *m;
	int err, ret;
	u64 tval = 0;

	if (rr->first) {
		resctrl_arch_reset_rmid(rr->r, rr->d, closid, rmid, rr->evtid);
		m = get_mbm_state(rr->d, closid, rmid, rr->evtid);
		if (m)
			memset(m, 0, sizeof(struct mbm_state));
		return 0;
	}

	if (rr->d) {
		/* Reading a single domain, must be on a CPU in that domain. */
		if (!cpumask_test_cpu(cpu, &rr->d->hdr.cpu_mask))
			return -EINVAL;
		rr->err = resctrl_arch_rmid_read(rr->r, rr->d, closid, rmid,
						 rr->evtid, &tval, rr->arch_mon_ctx);
		if (rr->err)
			return rr->err;

		rr->val += tval;

		return 0;
	}

	/* Summing domains that share a cache, must be on a CPU for that cache. */
	ci = get_cpu_cacheinfo_level(cpu, RESCTRL_L3_CACHE);
	if (!ci || ci->id != rr->ci_id)
		return -EINVAL;

	/*
	 * Legacy files must report the sum of an event across all
	 * domains that share the same L3 cache instance.
	 * Report success if a read from any domain succeeds, -EINVAL
	 * (translated to "Unavailable" for user space) if reading from
	 * all domains fail for any reason.
	 */
	ret = -EINVAL;
	list_for_each_entry(d, &rr->r->mon_domains, hdr.list) {
		if (d->ci_id != rr->ci_id)
			continue;
		err = resctrl_arch_rmid_read(rr->r, d, closid, rmid,
					     rr->evtid, &tval, rr->arch_mon_ctx);
		if (!err) {
			rr->val += tval;
			ret = 0;
		}
	}

	if (ret)
		rr->err = ret;

	return ret;
}

/*
 * mbm_bw_count() - Update bw count from values previously read by
 *		    __mon_event_count().
 * @closid:	The closid used to identify the cached mbm_state.
 * @rmid:	The rmid used to identify the cached mbm_state.
 * @rr:		The struct rmid_read populated by __mon_event_count().
 *
 * Supporting function to calculate the memory bandwidth
 * and delta bandwidth in MBps. The chunks value previously read by
 * __mon_event_count() is compared with the chunks value from the previous
 * invocation. This must be called once per second to maintain values in MBps.
 */
static void mbm_bw_count(u32 closid, u32 rmid, struct rmid_read *rr)
{
	u64 cur_bw, bytes, cur_bytes;
	struct mbm_state *m;

	m = get_mbm_state(rr->d, closid, rmid, rr->evtid);
	if (WARN_ON_ONCE(!m))
		return;

	cur_bytes = rr->val;
	bytes = cur_bytes - m->prev_bw_bytes;
	m->prev_bw_bytes = cur_bytes;

	cur_bw = bytes / SZ_1M;

	m->prev_bw = cur_bw;
}

/*
 * This is scheduled by mon_event_read() to read the CQM/MBM counters
 * on a domain.
 */
void mon_event_count(void *info)
{
	struct rdtgroup *rdtgrp, *entry;
	struct rmid_read *rr = info;
	struct list_head *head;
	int ret;

	rdtgrp = rr->rgrp;

	ret = __mon_event_count(rdtgrp->closid, rdtgrp->mon.rmid, rr);

	/*
	 * For Ctrl groups read data from child monitor groups and
	 * add them together. Count events which are read successfully.
	 * Discard the rmid_read's reporting errors.
	 */
	head = &rdtgrp->mon.crdtgrp_list;

	if (rdtgrp->type == RDTCTRL_GROUP) {
		list_for_each_entry(entry, head, mon.crdtgrp_list) {
			if (__mon_event_count(entry->closid, entry->mon.rmid,
					      rr) == 0)
				ret = 0;
		}
	}

	/*
	 * __mon_event_count() calls for newly created monitor groups may
	 * report -EINVAL/Unavailable if the monitor hasn't seen any traffic.
	 * Discard error if any of the monitor event reads succeeded.
	 */
	if (ret == 0)
		rr->err = 0;
}

static struct rdt_ctrl_domain *get_ctrl_domain_from_cpu(int cpu,
							struct rdt_resource *r)
{
	struct rdt_ctrl_domain *d;

	lockdep_assert_cpus_held();

	list_for_each_entry(d, &r->ctrl_domains, hdr.list) {
		/* Find the domain that contains this CPU */
		if (cpumask_test_cpu(cpu, &d->hdr.cpu_mask))
			return d;
	}

	return NULL;
}

/*
 * Feedback loop for MBA software controller (mba_sc)
 *
 * mba_sc is a feedback loop where we periodically read MBM counters and
 * adjust the bandwidth percentage values via the IA32_MBA_THRTL_MSRs so
 * that:
 *
 *   current bandwidth(cur_bw) < user specified bandwidth(user_bw)
 *
 * This uses the MBM counters to measure the bandwidth and MBA throttle
 * MSRs to control the bandwidth for a particular rdtgrp. It builds on the
 * fact that resctrl rdtgroups have both monitoring and control.
 *
 * The frequency of the checks is 1s and we just tag along the MBM overflow
 * timer. Having 1s interval makes the calculation of bandwidth simpler.
 *
 * Although MBA's goal is to restrict the bandwidth to a maximum, there may
 * be a need to increase the bandwidth to avoid unnecessarily restricting
 * the L2 <-> L3 traffic.
 *
 * Since MBA controls the L2 external bandwidth where as MBM measures the
 * L3 external bandwidth the following sequence could lead to such a
 * situation.
 *
 * Consider an rdtgroup which had high L3 <-> memory traffic in initial
 * phases -> mba_sc kicks in and reduced bandwidth percentage values -> but
 * after some time rdtgroup has mostly L2 <-> L3 traffic.
 *
 * In this case we may restrict the rdtgroup's L2 <-> L3 traffic as its
 * throttle MSRs already have low percentage values.  To avoid
 * unnecessarily restricting such rdtgroups, we also increase the bandwidth.
 */
static void update_mba_bw(struct rdtgroup *rgrp, struct rdt_mon_domain *dom_mbm)
{
	u32 closid, rmid, cur_msr_val, new_msr_val;
	struct mbm_state *pmbm_data, *cmbm_data;
	struct rdt_ctrl_domain *dom_mba;
	enum resctrl_event_id evt_id;
	struct rdt_resource *r_mba;
	struct list_head *head;
	struct rdtgroup *entry;
	u32 cur_bw, user_bw;

	r_mba = resctrl_arch_get_resource(RDT_RESOURCE_MBA);
	evt_id = rgrp->mba_mbps_event;

	closid = rgrp->closid;
	rmid = rgrp->mon.rmid;
	pmbm_data = get_mbm_state(dom_mbm, closid, rmid, evt_id);
	if (WARN_ON_ONCE(!pmbm_data))
		return;

	dom_mba = get_ctrl_domain_from_cpu(smp_processor_id(), r_mba);
	if (!dom_mba) {
		pr_warn_once("Failure to get domain for MBA update\n");
		return;
	}

	cur_bw = pmbm_data->prev_bw;
	user_bw = dom_mba->mbps_val[closid];

	/* MBA resource doesn't support CDP */
	cur_msr_val = resctrl_arch_get_config(r_mba, dom_mba, closid, CDP_NONE);

	/*
	 * For Ctrl groups read data from child monitor groups.
	 */
	head = &rgrp->mon.crdtgrp_list;
	list_for_each_entry(entry, head, mon.crdtgrp_list) {
		cmbm_data = get_mbm_state(dom_mbm, entry->closid, entry->mon.rmid, evt_id);
		if (WARN_ON_ONCE(!cmbm_data))
			return;
		cur_bw += cmbm_data->prev_bw;
	}

	/*
	 * Scale up/down the bandwidth linearly for the ctrl group.  The
	 * bandwidth step is the bandwidth granularity specified by the
	 * hardware.
	 * Always increase throttling if current bandwidth is above the
	 * target set by user.
	 * But avoid thrashing up and down on every poll by checking
	 * whether a decrease in throttling is likely to push the group
	 * back over target. E.g. if currently throttling to 30% of bandwidth
	 * on a system with 10% granularity steps, check whether moving to
	 * 40% would go past the limit by multiplying current bandwidth by
	 * "(30 + 10) / 30".
	 */
	if (cur_msr_val > r_mba->membw.min_bw && user_bw < cur_bw) {
		new_msr_val = cur_msr_val - r_mba->membw.bw_gran;
	} else if (cur_msr_val < MAX_MBA_BW &&
		   (user_bw > (cur_bw * (cur_msr_val + r_mba->membw.min_bw) / cur_msr_val))) {
		new_msr_val = cur_msr_val + r_mba->membw.bw_gran;
	} else {
		return;
	}

	resctrl_arch_update_one(r_mba, dom_mba, closid, CDP_NONE, new_msr_val);
}

static void mbm_update_one_event(struct rdt_resource *r, struct rdt_mon_domain *d,
				 u32 closid, u32 rmid, enum resctrl_event_id evtid)
{
	struct rmid_read rr = {0};

	rr.r = r;
	rr.d = d;
	rr.evtid = evtid;
	rr.arch_mon_ctx = resctrl_arch_mon_ctx_alloc(rr.r, rr.evtid);
	if (IS_ERR(rr.arch_mon_ctx)) {
		pr_warn_ratelimited("Failed to allocate monitor context: %ld",
				    PTR_ERR(rr.arch_mon_ctx));
		return;
	}

	__mon_event_count(closid, rmid, &rr);

	/*
	 * If the software controller is enabled, compute the
	 * bandwidth for this event id.
	 */
	if (is_mba_sc(NULL))
		mbm_bw_count(closid, rmid, &rr);

	resctrl_arch_mon_ctx_free(rr.r, rr.evtid, rr.arch_mon_ctx);
}

static void mbm_update(struct rdt_resource *r, struct rdt_mon_domain *d,
		       u32 closid, u32 rmid)
{
	/*
	 * This is protected from concurrent reads from user as both
	 * the user and overflow handler hold the global mutex.
	 */
	if (resctrl_arch_is_mbm_total_enabled())
		mbm_update_one_event(r, d, closid, rmid, QOS_L3_MBM_TOTAL_EVENT_ID);

	if (resctrl_arch_is_mbm_local_enabled())
		mbm_update_one_event(r, d, closid, rmid, QOS_L3_MBM_LOCAL_EVENT_ID);
}

/*
 * Handler to scan the limbo list and move the RMIDs
 * to free list whose occupancy < threshold_occupancy.
 */
void cqm_handle_limbo(struct work_struct *work)
{
	unsigned long delay = msecs_to_jiffies(CQM_LIMBOCHECK_INTERVAL);
	struct rdt_mon_domain *d;

	cpus_read_lock();
	mutex_lock(&rdtgroup_mutex);

	d = container_of(work, struct rdt_mon_domain, cqm_limbo.work);

	__check_limbo(d, false);

	if (has_busy_rmid(d)) {
		d->cqm_work_cpu = cpumask_any_housekeeping(&d->hdr.cpu_mask,
							   RESCTRL_PICK_ANY_CPU);
		schedule_delayed_work_on(d->cqm_work_cpu, &d->cqm_limbo,
					 delay);
	}

	mutex_unlock(&rdtgroup_mutex);
	cpus_read_unlock();
}

/**
 * cqm_setup_limbo_handler() - Schedule the limbo handler to run for this
 *                             domain.
 * @dom:           The domain the limbo handler should run for.
 * @delay_ms:      How far in the future the handler should run.
 * @exclude_cpu:   Which CPU the handler should not run on,
 *		   RESCTRL_PICK_ANY_CPU to pick any CPU.
 */
void cqm_setup_limbo_handler(struct rdt_mon_domain *dom, unsigned long delay_ms,
			     int exclude_cpu)
{
	unsigned long delay = msecs_to_jiffies(delay_ms);
	int cpu;

	cpu = cpumask_any_housekeeping(&dom->hdr.cpu_mask, exclude_cpu);
	dom->cqm_work_cpu = cpu;

	if (cpu < nr_cpu_ids)
		schedule_delayed_work_on(cpu, &dom->cqm_limbo, delay);
}

void mbm_handle_overflow(struct work_struct *work)
{
	unsigned long delay = msecs_to_jiffies(MBM_OVERFLOW_INTERVAL);
	struct rdtgroup *prgrp, *crgrp;
	struct rdt_mon_domain *d;
	struct list_head *head;
	struct rdt_resource *r;

	cpus_read_lock();
	mutex_lock(&rdtgroup_mutex);

	/*
	 * If the filesystem has been unmounted this work no longer needs to
	 * run.
	 */
	if (!resctrl_mounted || !resctrl_arch_mon_capable())
		goto out_unlock;

	r = resctrl_arch_get_resource(RDT_RESOURCE_L3);
	d = container_of(work, struct rdt_mon_domain, mbm_over.work);

	list_for_each_entry(prgrp, &rdt_all_groups, rdtgroup_list) {
		mbm_update(r, d, prgrp->closid, prgrp->mon.rmid);

		head = &prgrp->mon.crdtgrp_list;
		list_for_each_entry(crgrp, head, mon.crdtgrp_list)
			mbm_update(r, d, crgrp->closid, crgrp->mon.rmid);

		if (is_mba_sc(NULL))
			update_mba_bw(prgrp, d);
	}

	/*
	 * Re-check for housekeeping CPUs. This allows the overflow handler to
	 * move off a nohz_full CPU quickly.
	 */
	d->mbm_work_cpu = cpumask_any_housekeeping(&d->hdr.cpu_mask,
						   RESCTRL_PICK_ANY_CPU);
	schedule_delayed_work_on(d->mbm_work_cpu, &d->mbm_over, delay);

out_unlock:
	mutex_unlock(&rdtgroup_mutex);
	cpus_read_unlock();
}

/**
 * mbm_setup_overflow_handler() - Schedule the overflow handler to run for this
 *                                domain.
 * @dom:           The domain the overflow handler should run for.
 * @delay_ms:      How far in the future the handler should run.
 * @exclude_cpu:   Which CPU the handler should not run on,
 *		   RESCTRL_PICK_ANY_CPU to pick any CPU.
 */
void mbm_setup_overflow_handler(struct rdt_mon_domain *dom, unsigned long delay_ms,
				int exclude_cpu)
{
	unsigned long delay = msecs_to_jiffies(delay_ms);
	int cpu;

	/*
	 * When a domain comes online there is no guarantee the filesystem is
	 * mounted. If not, there is no need to catch counter overflow.
	 */
	if (!resctrl_mounted || !resctrl_arch_mon_capable())
		return;
	cpu = cpumask_any_housekeeping(&dom->hdr.cpu_mask, exclude_cpu);
	dom->mbm_work_cpu = cpu;

	if (cpu < nr_cpu_ids)
		schedule_delayed_work_on(cpu, &dom->mbm_over, delay);
}

static int dom_data_init(struct rdt_resource *r)
{
	u32 idx_limit = resctrl_arch_system_num_rmid_idx();
	u32 num_closid = resctrl_arch_get_num_closid(r);
	struct rmid_entry *entry = NULL;
	int err = 0, i;
	u32 idx;

	mutex_lock(&rdtgroup_mutex);
	if (IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID)) {
		u32 *tmp;

		/*
		 * If the architecture hasn't provided a sanitised value here,
		 * this may result in larger arrays than necessary. Resctrl will
		 * use a smaller system wide value based on the resources in
		 * use.
		 */
		tmp = kcalloc(num_closid, sizeof(*tmp), GFP_KERNEL);
		if (!tmp) {
			err = -ENOMEM;
			goto out_unlock;
		}

		closid_num_dirty_rmid = tmp;
	}

	rmid_ptrs = kcalloc(idx_limit, sizeof(struct rmid_entry), GFP_KERNEL);
	if (!rmid_ptrs) {
		if (IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID)) {
			kfree(closid_num_dirty_rmid);
			closid_num_dirty_rmid = NULL;
		}
		err = -ENOMEM;
		goto out_unlock;
	}

	for (i = 0; i < idx_limit; i++) {
		entry = &rmid_ptrs[i];
		INIT_LIST_HEAD(&entry->list);

		resctrl_arch_rmid_idx_decode(i, &entry->closid, &entry->rmid);
		list_add_tail(&entry->list, &rmid_free_lru);
	}

	/*
	 * RESCTRL_RESERVED_CLOSID and RESCTRL_RESERVED_RMID are special and
	 * are always allocated. These are used for the rdtgroup_default
	 * control group, which will be setup later in resctrl_init().
	 */
	idx = resctrl_arch_rmid_idx_encode(RESCTRL_RESERVED_CLOSID,
					   RESCTRL_RESERVED_RMID);
	entry = __rmid_entry(idx);
	list_del(&entry->list);

out_unlock:
	mutex_unlock(&rdtgroup_mutex);

	return err;
}

static void dom_data_exit(struct rdt_resource *r)
{
	mutex_lock(&rdtgroup_mutex);

	if (!r->mon_capable)
		goto out_unlock;

	if (IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID)) {
		kfree(closid_num_dirty_rmid);
		closid_num_dirty_rmid = NULL;
	}

	kfree(rmid_ptrs);
	rmid_ptrs = NULL;

out_unlock:
	mutex_unlock(&rdtgroup_mutex);
}

static struct mon_evt llc_occupancy_event = {
	.name		= "llc_occupancy",
	.evtid		= QOS_L3_OCCUP_EVENT_ID,
};

static struct mon_evt mbm_total_event = {
	.name		= "mbm_total_bytes",
	.evtid		= QOS_L3_MBM_TOTAL_EVENT_ID,
};

static struct mon_evt mbm_local_event = {
	.name		= "mbm_local_bytes",
	.evtid		= QOS_L3_MBM_LOCAL_EVENT_ID,
};

/*
 * Initialize the event list for the resource.
 *
 * Note that MBM events are also part of RDT_RESOURCE_L3 resource
 * because as per the SDM the total and local memory bandwidth
 * are enumerated as part of L3 monitoring.
 */
static void l3_mon_evt_init(struct rdt_resource *r)
{
	INIT_LIST_HEAD(&r->evt_list);

	if (resctrl_arch_is_llc_occupancy_enabled())
		list_add_tail(&llc_occupancy_event.list, &r->evt_list);
	if (resctrl_arch_is_mbm_total_enabled())
		list_add_tail(&mbm_total_event.list, &r->evt_list);
	if (resctrl_arch_is_mbm_local_enabled())
		list_add_tail(&mbm_local_event.list, &r->evt_list);
}

/**
 * resctrl_mon_resource_init() - Initialise global monitoring structures.
 *
 * Allocate and initialise global monitor resources that do not belong to a
 * specific domain. i.e. the rmid_ptrs[] used for the limbo and free lists.
 * Called once during boot after the struct rdt_resource's have been configured
 * but before the filesystem is mounted.
 * Resctrl's cpuhp callbacks may be called before this point to bring a domain
 * online.
 *
 * Returns 0 for success, or -ENOMEM.
 */
int resctrl_mon_resource_init(void)
{
	struct rdt_resource *r = resctrl_arch_get_resource(RDT_RESOURCE_L3);
	int ret;

	if (!r->mon_capable)
		return 0;

	ret = dom_data_init(r);
	if (ret)
		return ret;

	l3_mon_evt_init(r);

	if (resctrl_arch_is_evt_configurable(QOS_L3_MBM_TOTAL_EVENT_ID)) {
		mbm_total_event.configurable = true;
		resctrl_file_fflags_init("mbm_total_bytes_config",
					 RFTYPE_MON_INFO | RFTYPE_RES_CACHE);
	}
	if (resctrl_arch_is_evt_configurable(QOS_L3_MBM_LOCAL_EVENT_ID)) {
		mbm_local_event.configurable = true;
		resctrl_file_fflags_init("mbm_local_bytes_config",
					 RFTYPE_MON_INFO | RFTYPE_RES_CACHE);
	}

	if (resctrl_arch_is_mbm_local_enabled())
		mba_mbps_default_event = QOS_L3_MBM_LOCAL_EVENT_ID;
	else if (resctrl_arch_is_mbm_total_enabled())
		mba_mbps_default_event = QOS_L3_MBM_TOTAL_EVENT_ID;

	return 0;
}

void resctrl_mon_resource_exit(void)
{
	struct rdt_resource *r = resctrl_arch_get_resource(RDT_RESOURCE_L3);

	dom_data_exit(r);
}
