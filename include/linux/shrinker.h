#ifndef _LINUX_SHRINKER_H
#define _LINUX_SHRINKER_H

/*
 * This struct is used to pass information from page reclaim to the shrinkers.
 * We consolidate the values for easier extention later.
 *
 * The 'gfpmask' refers to the allocation we are currently trying to
 * fulfil.
 *
 * Note that 'shrink' will be passed nr_to_scan == 0 when the VM is
 * querying the cache size, so a fastpath for that case is appropriate.
 */
struct shrink_control {
	gfp_t gfp_mask;

	/* How many slab objects shrinker() should scan and try to reclaim */
	unsigned long nr_to_scan;

	/* shrink from these nodes */
	nodemask_t nodes_to_scan;
};

#define SHRINK_STOP (~0UL)
/*
 * A callback you can register to apply pressure to ageable caches.
 *
 * @shrink() should look through the least-recently-used 'nr_to_scan' entries
 * and attempt to free them up.  It should return the number of objects which
 * remain in the cache.  If it returns -1, it means it cannot do any scanning at
 * this time (eg. there is a risk of deadlock).
 *
 * @count_objects should return the number of freeable items in the cache. If
 * there are no objects to free or the number of freeable items cannot be
 * determined, it should return 0. No deadlock checks should be done during the
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
 */
struct shrinker {
	int (*shrink)(struct shrinker *, struct shrink_control *sc);
	unsigned long (*count_objects)(struct shrinker *,
				       struct shrink_control *sc);
	unsigned long (*scan_objects)(struct shrinker *,
				      struct shrink_control *sc);

	int seeks;	/* seeks to recreate an obj */
	long batch;	/* reclaim batch size, 0 = default */

	/* These are for internal use */
	struct list_head list;
	atomic_long_t nr_in_batch; /* objs pending delete */
};
#define DEFAULT_SEEKS 2 /* A good number if you don't know better. */
extern void register_shrinker(struct shrinker *);
extern void unregister_shrinker(struct shrinker *);
#endif
