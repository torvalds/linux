#ifndef _LINUX_SHRINKER_H
#define _LINUX_SHRINKER_H

/*
 * This struct is used to pass information from page reclaim to the shrinkers.
 * We consolidate the values for easier extention later.
 */
struct shrink_control {
	gfp_t gfp_mask;

	/* How many slab objects shrinker() should scan and try to reclaim */
	unsigned long nr_to_scan;
};

/*
 * A callback you can register to apply pressure to ageable caches.
 *
 * 'sc' is passed shrink_control which includes a count 'nr_to_scan'
 * and a 'gfpmask'.  It should look through the least-recently-used
 * 'nr_to_scan' entries and attempt to free them up.  It should return
 * the number of objects which remain in the cache.  If it returns -1, it means
 * it cannot do any scanning at this time (eg. there is a risk of deadlock).
 *
 * The 'gfpmask' refers to the allocation we are currently trying to
 * fulfil.
 *
 * Note that 'shrink' will be passed nr_to_scan == 0 when the VM is
 * querying the cache size, so a fastpath for that case is appropriate.
 */
struct shrinker {
	int (*shrink)(struct shrinker *, struct shrink_control *sc);
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
