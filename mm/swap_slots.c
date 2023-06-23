// SPDX-License-Identifier: GPL-2.0
/*
 * Manage cache of swap slots to be used for and returned from
 * swap.
 *
 * Copyright(c) 2016 Intel Corporation.
 *
 * Author: Tim Chen <tim.c.chen@linux.intel.com>
 *
 * We allocate the swap slots from the global pool and put
 * it into local per cpu caches.  This has the advantage
 * of no needing to acquire the swap_info lock every time
 * we need a new slot.
 *
 * There is also opportunity to simply return the slot
 * to local caches without needing to acquire swap_info
 * lock.  We do not reuse the returned slots directly but
 * move them back to the global pool in a batch.  This
 * allows the slots to coalesce and reduce fragmentation.
 *
 * The swap entry allocated is marked with SWAP_HAS_CACHE
 * flag in map_count that prevents it from being allocated
 * again from the global pool.
 *
 * The swap slots cache is protected by a mutex instead of
 * a spin lock as when we search for slots with scan_swap_map,
 * we can possibly sleep.
 */

#include <linux/swap_slots.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/mm.h>

static DEFINE_PER_CPU(struct swap_slots_cache, swp_slots);
static bool	swap_slot_cache_active;
bool	swap_slot_cache_enabled;
static bool	swap_slot_cache_initialized;
static DEFINE_MUTEX(swap_slots_cache_mutex);
/* Serialize swap slots cache enable/disable operations */
static DEFINE_MUTEX(swap_slots_cache_enable_mutex);

static void __drain_swap_slots_cache(unsigned int type);

#define use_swap_slot_cache (swap_slot_cache_active && swap_slot_cache_enabled)
#define SLOTS_CACHE 0x1
#define SLOTS_CACHE_RET 0x2

static void deactivate_swap_slots_cache(void)
{
	mutex_lock(&swap_slots_cache_mutex);
	swap_slot_cache_active = false;
	__drain_swap_slots_cache(SLOTS_CACHE|SLOTS_CACHE_RET);
	mutex_unlock(&swap_slots_cache_mutex);
}

static void reactivate_swap_slots_cache(void)
{
	mutex_lock(&swap_slots_cache_mutex);
	swap_slot_cache_active = true;
	mutex_unlock(&swap_slots_cache_mutex);
}

/* Must not be called with cpu hot plug lock */
void disable_swap_slots_cache_lock(void)
{
	mutex_lock(&swap_slots_cache_enable_mutex);
	swap_slot_cache_enabled = false;
	if (swap_slot_cache_initialized) {
		/* serialize with cpu hotplug operations */
		cpus_read_lock();
		__drain_swap_slots_cache(SLOTS_CACHE|SLOTS_CACHE_RET);
		cpus_read_unlock();
	}
}

static void __reenable_swap_slots_cache(void)
{
	swap_slot_cache_enabled = has_usable_swap();
}

void reenable_swap_slots_cache_unlock(void)
{
	__reenable_swap_slots_cache();
	mutex_unlock(&swap_slots_cache_enable_mutex);
}

static bool check_cache_active(void)
{
	long pages;

	if (!swap_slot_cache_enabled)
		return false;

	pages = get_nr_swap_pages();
	if (!swap_slot_cache_active) {
		if (pages > num_online_cpus() *
		    THRESHOLD_ACTIVATE_SWAP_SLOTS_CACHE)
			reactivate_swap_slots_cache();
		goto out;
	}

	/* if global pool of slot caches too low, deactivate cache */
	if (pages < num_online_cpus() * THRESHOLD_DEACTIVATE_SWAP_SLOTS_CACHE)
		deactivate_swap_slots_cache();
out:
	return swap_slot_cache_active;
}

static int alloc_swap_slot_cache(unsigned int cpu)
{
	struct swap_slots_cache *cache;
	swp_entry_t *slots, *slots_ret;

	/*
	 * Do allocation outside swap_slots_cache_mutex
	 * as kvzalloc could trigger reclaim and folio_alloc_swap,
	 * which can lock swap_slots_cache_mutex.
	 */
	slots = kvcalloc(SWAP_SLOTS_CACHE_SIZE, sizeof(swp_entry_t),
			 GFP_KERNEL);
	if (!slots)
		return -ENOMEM;

	slots_ret = kvcalloc(SWAP_SLOTS_CACHE_SIZE, sizeof(swp_entry_t),
			     GFP_KERNEL);
	if (!slots_ret) {
		kvfree(slots);
		return -ENOMEM;
	}

	mutex_lock(&swap_slots_cache_mutex);
	cache = &per_cpu(swp_slots, cpu);
	if (cache->slots || cache->slots_ret) {
		/* cache already allocated */
		mutex_unlock(&swap_slots_cache_mutex);

		kvfree(slots);
		kvfree(slots_ret);

		return 0;
	}

	if (!cache->lock_initialized) {
		mutex_init(&cache->alloc_lock);
		spin_lock_init(&cache->free_lock);
		cache->lock_initialized = true;
	}
	cache->nr = 0;
	cache->cur = 0;
	cache->n_ret = 0;
	/*
	 * We initialized alloc_lock and free_lock earlier.  We use
	 * !cache->slots or !cache->slots_ret to know if it is safe to acquire
	 * the corresponding lock and use the cache.  Memory barrier below
	 * ensures the assumption.
	 */
	mb();
	cache->slots = slots;
	cache->slots_ret = slots_ret;
	mutex_unlock(&swap_slots_cache_mutex);
	return 0;
}

static void drain_slots_cache_cpu(unsigned int cpu, unsigned int type,
				  bool free_slots)
{
	struct swap_slots_cache *cache;
	swp_entry_t *slots = NULL;

	cache = &per_cpu(swp_slots, cpu);
	if ((type & SLOTS_CACHE) && cache->slots) {
		mutex_lock(&cache->alloc_lock);
		swapcache_free_entries(cache->slots + cache->cur, cache->nr);
		cache->cur = 0;
		cache->nr = 0;
		if (free_slots && cache->slots) {
			kvfree(cache->slots);
			cache->slots = NULL;
		}
		mutex_unlock(&cache->alloc_lock);
	}
	if ((type & SLOTS_CACHE_RET) && cache->slots_ret) {
		spin_lock_irq(&cache->free_lock);
		swapcache_free_entries(cache->slots_ret, cache->n_ret);
		cache->n_ret = 0;
		if (free_slots && cache->slots_ret) {
			slots = cache->slots_ret;
			cache->slots_ret = NULL;
		}
		spin_unlock_irq(&cache->free_lock);
		kvfree(slots);
	}
}

static void __drain_swap_slots_cache(unsigned int type)
{
	unsigned int cpu;

	/*
	 * This function is called during
	 *	1) swapoff, when we have to make sure no
	 *	   left over slots are in cache when we remove
	 *	   a swap device;
	 *      2) disabling of swap slot cache, when we run low
	 *	   on swap slots when allocating memory and need
	 *	   to return swap slots to global pool.
	 *
	 * We cannot acquire cpu hot plug lock here as
	 * this function can be invoked in the cpu
	 * hot plug path:
	 * cpu_up -> lock cpu_hotplug -> cpu hotplug state callback
	 *   -> memory allocation -> direct reclaim -> folio_alloc_swap
	 *   -> drain_swap_slots_cache
	 *
	 * Hence the loop over current online cpu below could miss cpu that
	 * is being brought online but not yet marked as online.
	 * That is okay as we do not schedule and run anything on a
	 * cpu before it has been marked online. Hence, we will not
	 * fill any swap slots in slots cache of such cpu.
	 * There are no slots on such cpu that need to be drained.
	 */
	for_each_online_cpu(cpu)
		drain_slots_cache_cpu(cpu, type, false);
}

static int free_slot_cache(unsigned int cpu)
{
	mutex_lock(&swap_slots_cache_mutex);
	drain_slots_cache_cpu(cpu, SLOTS_CACHE | SLOTS_CACHE_RET, true);
	mutex_unlock(&swap_slots_cache_mutex);
	return 0;
}

void enable_swap_slots_cache(void)
{
	mutex_lock(&swap_slots_cache_enable_mutex);
	if (!swap_slot_cache_initialized) {
		int ret;

		ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "swap_slots_cache",
					alloc_swap_slot_cache, free_slot_cache);
		if (WARN_ONCE(ret < 0, "Cache allocation failed (%s), operating "
				       "without swap slots cache.\n", __func__))
			goto out_unlock;

		swap_slot_cache_initialized = true;
	}

	__reenable_swap_slots_cache();
out_unlock:
	mutex_unlock(&swap_slots_cache_enable_mutex);
}

/* called with swap slot cache's alloc lock held */
static int refill_swap_slots_cache(struct swap_slots_cache *cache)
{
	if (!use_swap_slot_cache)
		return 0;

	cache->cur = 0;
	if (swap_slot_cache_active)
		cache->nr = get_swap_pages(SWAP_SLOTS_CACHE_SIZE,
					   cache->slots, 1);

	return cache->nr;
}

void free_swap_slot(swp_entry_t entry)
{
	struct swap_slots_cache *cache;

	cache = raw_cpu_ptr(&swp_slots);
	if (likely(use_swap_slot_cache && cache->slots_ret)) {
		spin_lock_irq(&cache->free_lock);
		/* Swap slots cache may be deactivated before acquiring lock */
		if (!use_swap_slot_cache || !cache->slots_ret) {
			spin_unlock_irq(&cache->free_lock);
			goto direct_free;
		}
		if (cache->n_ret >= SWAP_SLOTS_CACHE_SIZE) {
			/*
			 * Return slots to global pool.
			 * The current swap_map value is SWAP_HAS_CACHE.
			 * Set it to 0 to indicate it is available for
			 * allocation in global pool
			 */
			swapcache_free_entries(cache->slots_ret, cache->n_ret);
			cache->n_ret = 0;
		}
		cache->slots_ret[cache->n_ret++] = entry;
		spin_unlock_irq(&cache->free_lock);
	} else {
direct_free:
		swapcache_free_entries(&entry, 1);
	}
}

swp_entry_t folio_alloc_swap(struct folio *folio)
{
	swp_entry_t entry;
	struct swap_slots_cache *cache;

	entry.val = 0;

	if (folio_test_large(folio)) {
		if (IS_ENABLED(CONFIG_THP_SWAP) && arch_thp_swp_supported())
			get_swap_pages(1, &entry, folio_nr_pages(folio));
		goto out;
	}

	/*
	 * Preemption is allowed here, because we may sleep
	 * in refill_swap_slots_cache().  But it is safe, because
	 * accesses to the per-CPU data structure are protected by the
	 * mutex cache->alloc_lock.
	 *
	 * The alloc path here does not touch cache->slots_ret
	 * so cache->free_lock is not taken.
	 */
	cache = raw_cpu_ptr(&swp_slots);

	if (likely(check_cache_active() && cache->slots)) {
		mutex_lock(&cache->alloc_lock);
		if (cache->slots) {
repeat:
			if (cache->nr) {
				entry = cache->slots[cache->cur];
				cache->slots[cache->cur++].val = 0;
				cache->nr--;
			} else if (refill_swap_slots_cache(cache)) {
				goto repeat;
			}
		}
		mutex_unlock(&cache->alloc_lock);
		if (entry.val)
			goto out;
	}

	get_swap_pages(1, &entry, 1);
out:
	if (mem_cgroup_try_charge_swap(folio, entry)) {
		put_swap_page(&folio->page, entry);
		entry.val = 0;
	}
	return entry;
}
