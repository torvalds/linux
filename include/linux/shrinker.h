/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SHRINKER_H
#define _LINUX_SHRINKER_H

/*
 * This struct is used to pass information from page reclaim to the shrinkers.
 * We consolidate the values for easier extention later.
 *
 * The 'gfpmask' refers to the allocation we are currently trying to
 * fulfil.
 */
struct shrink_control {
	gfp_t gfp_mask;

	/* current node being shrunk (for NUMA aware shrinkers) */
	int nid;

	/*
	 * How many objects scan_objects should scan and try to reclaim.
	 * This is reset before every call, so it is safe for callees
	 * to modify.
	 */
	unsigned long nr_to_scan;

	/*
	 * How many objects did scan_objects process?
	 * This defaults to nr_to_scan before every call, but the callee
	 * should track its actual progress.
	 */
	unsigned long nr_scanned;

	/* current memcg being shrunk (for memcg aware shrinkers) */
	struct mem_cgroup *memcg;
};

#define SHRINK_STOP (~0UL)
#define SHRINK_EMPTY (~0UL - 1)
/*
 * A callback you can register to apply pressure to ageable caches.
 *
 * @count_objects should return the number of freeable items in the cache. If
 * there are no objects to free, it should return SHRINK_EMPTY, while 0 is
 * returned in cases of the number of freeable items cannot be determined
 * or shrinker should skip this cache for this time (e.g., their number
 * is below shrinkable limit). No deadlock checks should be done during the
 * count callback - the shrinker relies on aggregating scan counts that couldn't
 * be executed due to potential deadlocks to be run at a later call when the
 * deadlock condition is no longer pending.
 *
 * @scan_objects will only be called if @count_objects returned a non-zero
 * value for the number of freeable objects. The callout should scan the cache
 * and attempt to free items from the cache. It should then return the number
 * of objects freed during the scan, or SHRINK_STOP if progress cannot be made
 * due to potential deadlocks. If SHRINK_STOP is returned, then no further
 * attempts to call the @scan_objects will be made from the current reclaim
 * context.
 *
 * @flags determine the shrinker abilities, like numa awareness
 */
struct shrinker {
	unsigned long (*count_objects)(struct shrinker *,
				       struct shrink_control *sc);
	unsigned long (*scan_objects)(struct shrinker *,
				      struct shrink_control *sc);

	long batch;	/* reclaim batch size, 0 = default */
	int seeks;	/* seeks to recreate an obj */
	unsigned flags;

	/* These are for internal use */
	struct list_head list;
#ifdef CONFIG_MEMCG
	/* ID in shrinker_idr */
	int id;
#endif
	/* objs pending delete, per node */
	atomic_long_t *nr_deferred;
};
#define DEFAULT_SEEKS 2 /* A good number if you don't know better. */

/* Flags */
#define SHRINKER_NUMA_AWARE	(1 << 0)
#define SHRINKER_MEMCG_AWARE	(1 << 1)
/*
 * It just makes sense when the shrinker is also MEMCG_AWARE for now,
 * non-MEMCG_AWARE shrinker should not have this flag set.
 */
#define SHRINKER_NONSLAB	(1 << 2)

extern int prealloc_shrinker(struct shrinker *shrinker);
extern void register_shrinker_prepared(struct shrinker *shrinker);
extern int register_shrinker(struct shrinker *shrinker);
extern void unregister_shrinker(struct shrinker *shrinker);
extern void free_prealloced_shrinker(struct shrinker *shrinker);
#endif
