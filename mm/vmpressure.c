// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux VM pressure
 *
 * Copyright 2012 Linaro Ltd.
 *		  Anton Vorontsov <anton.vorontsov@linaro.org>
 *
 * Based on ideas from Andrew Morton, David Rientjes, KOSAKI Motohiro,
 * Leonid Moiseichuk, Mel Gorman, Minchan Kim and Pekka Enberg.
 *
 * Tree-mode (cgroup v1 userspace eventfd) bookkeeping lives in
 * mm/memcontrol-v1.c; this file holds the shared code and the in-kernel
 * (tree=false) socket-pressure path that runs on cgroup v2.
 */

#include <linux/cgroup.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/printk.h>
#include <linux/vmpressure.h>

/*
 * The window size (vmpressure_win) is the number of scanned pages before
 * we try to analyze scanned/reclaimed ratio. So the window is used as a
 * rate-limit tunable for the "low" level notification, and also for
 * averaging the ratio for medium/critical levels. Using small window
 * sizes can cause lot of false positives, but too big window size will
 * delay the notifications.
 *
 * As the vmscan reclaimer logic works with chunks which are multiple of
 * SWAP_CLUSTER_MAX, it makes sense to use it for the window size as well.
 *
 * TODO: Make the window size depend on machine size, as we do for vmstat
 * thresholds. Currently we set it to 512 pages (2MB for 4KB pages).
 */
const unsigned long vmpressure_win = SWAP_CLUSTER_MAX * 16;

/*
 * These thresholds are used when we account memory pressure through
 * scanned/reclaimed ratio. The current values were chosen empirically. In
 * essence, they are percents: the higher the value, the more number
 * unsuccessful reclaims there were.
 */
static const unsigned int vmpressure_level_med = 60;
static const unsigned int vmpressure_level_critical = 95;

static enum vmpressure_levels vmpressure_level(unsigned long pressure)
{
	if (pressure >= vmpressure_level_critical)
		return VMPRESSURE_CRITICAL;
	else if (pressure >= vmpressure_level_med)
		return VMPRESSURE_MEDIUM;
	return VMPRESSURE_LOW;
}

enum vmpressure_levels vmpressure_calc_level(unsigned long scanned,
					     unsigned long reclaimed)
{
	unsigned long scale = scanned + reclaimed;
	unsigned long pressure = 0;

	/*
	 * reclaimed can be greater than scanned for things such as reclaimed
	 * slab pages. shrink_node() just adds reclaimed pages without a
	 * related increment to scanned pages.
	 */
	if (reclaimed >= scanned)
		goto out;
	/*
	 * We calculate the ratio (in percents) of how many pages were
	 * scanned vs. reclaimed in a given time frame (window). Note that
	 * time is in VM reclaimer's "ticks", i.e. number of pages
	 * scanned. This makes it possible to set desired reaction time
	 * and serves as a ratelimit.
	 */
	pressure = scale - (reclaimed * scale / scanned);
	pressure = pressure * 100 / scale;

out:
	pr_debug("%s: %3lu  (s: %lu  r: %lu)\n", __func__, pressure,
		 scanned, reclaimed);

	return vmpressure_level(pressure);
}

/**
 * vmpressure() - Account memory pressure through scanned/reclaimed ratio
 * @gfp:	reclaimer's gfp mask
 * @order:	allocation order being reclaimed for
 * @memcg:	cgroup memory controller handle
 * @tree:	legacy subtree mode
 * @scanned:	number of pages scanned
 * @reclaimed:	number of pages reclaimed
 *
 * This function should be called from the vmscan reclaim path to account
 * "instantaneous" memory pressure (scanned/reclaimed ratio). The raw
 * pressure index is then further refined and averaged over time.
 *
 * If @tree is set, vmpressure is in traditional userspace reporting
 * mode: @memcg is considered the pressure root and userspace is
 * notified of the entire subtree's reclaim efficiency.
 *
 * If @tree is not set, reclaim efficiency is recorded for @memcg, and
 * only in-kernel users are notified.
 *
 * This function does not return any value.
 */
void vmpressure(gfp_t gfp, int order, struct mem_cgroup *memcg, bool tree,
		unsigned long scanned, unsigned long reclaimed)
{
	struct vmpressure *vmpr;

	if (mem_cgroup_disabled())
		return;

	/*
	 * Only two combinations have a consumer:
	 *   cgroup v2 + tree=false -> in-kernel socket pressure
	 *   cgroup v1 + tree=true  -> userspace eventfds (memory.pressure_level)
	 * Skip the other two: nothing consumes the result.
	 */
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys) == tree)
		return;

	vmpr = memcg_to_vmpressure(memcg);

	/*
	 * Here we only want to account pressure that userland is able to
	 * help us with. For example, suppose that DMA zone is under
	 * pressure; if we notify userland about that kind of pressure,
	 * then it will be mostly a waste as it will trigger unnecessary
	 * freeing of memory by userland (since userland is more likely to
	 * have HIGHMEM/MOVABLE pages instead of the DMA fallback). That
	 * is why we include only movable, highmem and FS/IO pages.
	 * Indirect reclaim (kswapd) sets sc->gfp_mask to GFP_KERNEL, so
	 * we account it too.
	 */
	if (!(gfp & (__GFP_HIGHMEM | __GFP_MOVABLE | __GFP_IO | __GFP_FS)))
		return;

	/*
	 * If we got here with no pages scanned, then that is an indicator
	 * that reclaimer was unable to find any shrinkable LRUs at the
	 * current scanning depth. But it does not mean that we should
	 * report the critical pressure, yet. If the scanning priority
	 * (scanning depth) goes too high (deep), we will be notified
	 * through vmpressure_prio(). But so far, keep calm.
	 */
	if (!scanned)
		return;

	if (tree) {
		vmpressure_v1_account_tree(vmpr, scanned, reclaimed);
	} else {
		enum vmpressure_levels level;

		/* For now, no users for root-level efficiency */
		if (!memcg || mem_cgroup_is_root(memcg))
			return;

		spin_lock(&vmpr->sr_lock);
		scanned = vmpr->scanned += scanned;
		reclaimed = vmpr->reclaimed += reclaimed;
		if (scanned < vmpressure_win) {
			spin_unlock(&vmpr->sr_lock);
			return;
		}
		vmpr->scanned = vmpr->reclaimed = 0;
		spin_unlock(&vmpr->sr_lock);

		level = vmpressure_calc_level(scanned, reclaimed);

		/*
		 * Once we go above COSTLY_ORDER, reclaim relies heavily on
		 * compaction to make progress. Reclaim efficiency was never a
		 * great proxy for pressure to begin with, but it's outright
		 * misleading with these high orders. Don't throttle sockets
		 * because somebody is attempting something crazy like an order-7
		 * and predictably struggling.
		 */
		if (level > VMPRESSURE_LOW && order <= PAGE_ALLOC_COSTLY_ORDER) {
			/*
			 * Let the socket buffer allocator know that
			 * we are having trouble reclaiming LRU pages.
			 *
			 * For hysteresis keep the pressure state
			 * asserted for a second in which subsequent
			 * pressure events can occur.
			 */
			mem_cgroup_set_socket_pressure(memcg);
		}
	}
}

/**
 * vmpressure_init() - Initialize vmpressure control structure
 * @vmpr:	Structure to be initialized
 *
 * This function should be called on every allocated vmpressure structure
 * before any usage.
 */
void vmpressure_init(struct vmpressure *vmpr)
{
	spin_lock_init(&vmpr->sr_lock);
	vmpressure_v1_init(vmpr);
}

/**
 * vmpressure_cleanup() - shuts down vmpressure control structure
 * @vmpr:	Structure to be cleaned up
 *
 * This function should be called before the structure in which it is
 * embedded is cleaned up.
 */
void vmpressure_cleanup(struct vmpressure *vmpr)
{
	vmpressure_v1_cleanup(vmpr);
}
