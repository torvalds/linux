#ifndef _LINUX_COMPACTION_H
#define _LINUX_COMPACTION_H

/*
 * Determines how hard direct compaction should try to succeed.
 * Lower value means higher priority, analogically to reclaim priority.
 */
enum compact_priority {
	COMPACT_PRIO_SYNC_LIGHT,
	MIN_COMPACT_PRIORITY = COMPACT_PRIO_SYNC_LIGHT,
	DEF_COMPACT_PRIORITY = COMPACT_PRIO_SYNC_LIGHT,
	COMPACT_PRIO_ASYNC,
	INIT_COMPACT_PRIORITY = COMPACT_PRIO_ASYNC
};

/* Return values for compact_zone() and try_to_compact_pages() */
/* When adding new states, please adjust include/trace/events/compaction.h */
enum compact_result {
	/* For more detailed tracepoint output - internal to compaction */
	COMPACT_NOT_SUITABLE_ZONE,
	/*
	 * compaction didn't start as it was not possible or direct reclaim
	 * was more suitable
	 */
	COMPACT_SKIPPED,
	/* compaction didn't start as it was deferred due to past failures */
	COMPACT_DEFERRED,

	/* compaction not active last round */
	COMPACT_INACTIVE = COMPACT_DEFERRED,

	/* For more detailed tracepoint output - internal to compaction */
	COMPACT_NO_SUITABLE_PAGE,
	/* compaction should continue to another pageblock */
	COMPACT_CONTINUE,

	/*
	 * The full zone was compacted scanned but wasn't successfull to compact
	 * suitable pages.
	 */
	COMPACT_COMPLETE,
	/*
	 * direct compaction has scanned part of the zone but wasn't successfull
	 * to compact suitable pages.
	 */
	COMPACT_PARTIAL_SKIPPED,

	/* compaction terminated prematurely due to lock contentions */
	COMPACT_CONTENDED,

	/*
	 * direct compaction partially compacted a zone and there might be
	 * suitable pages
	 */
	COMPACT_PARTIAL,
};

struct alloc_context; /* in mm/internal.h */

#ifdef CONFIG_COMPACTION
extern int sysctl_compact_memory;
extern int sysctl_compaction_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *length, loff_t *ppos);
extern int sysctl_extfrag_threshold;
extern int sysctl_extfrag_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *length, loff_t *ppos);
extern int sysctl_compact_unevictable_allowed;

extern int fragmentation_index(struct zone *zone, unsigned int order);
extern enum compact_result try_to_compact_pages(gfp_t gfp_mask,
		unsigned int order, unsigned int alloc_flags,
		const struct alloc_context *ac, enum compact_priority prio);
extern void compact_pgdat(pg_data_t *pgdat, int order);
extern void reset_isolation_suitable(pg_data_t *pgdat);
extern enum compact_result compaction_suitable(struct zone *zone, int order,
		unsigned int alloc_flags, int classzone_idx);

extern void defer_compaction(struct zone *zone, int order);
extern bool compaction_deferred(struct zone *zone, int order);
extern void compaction_defer_reset(struct zone *zone, int order,
				bool alloc_success);
extern bool compaction_restarting(struct zone *zone, int order);

/* Compaction has made some progress and retrying makes sense */
static inline bool compaction_made_progress(enum compact_result result)
{
	/*
	 * Even though this might sound confusing this in fact tells us
	 * that the compaction successfully isolated and migrated some
	 * pageblocks.
	 */
	if (result == COMPACT_PARTIAL)
		return true;

	return false;
}

/* Compaction has failed and it doesn't make much sense to keep retrying. */
static inline bool compaction_failed(enum compact_result result)
{
	/* All zones were scanned completely and still not result. */
	if (result == COMPACT_COMPLETE)
		return true;

	return false;
}

/*
 * Compaction  has backed off for some reason. It might be throttling or
 * lock contention. Retrying is still worthwhile.
 */
static inline bool compaction_withdrawn(enum compact_result result)
{
	/*
	 * Compaction backed off due to watermark checks for order-0
	 * so the regular reclaim has to try harder and reclaim something.
	 */
	if (result == COMPACT_SKIPPED)
		return true;

	/*
	 * If compaction is deferred for high-order allocations, it is
	 * because sync compaction recently failed. If this is the case
	 * and the caller requested a THP allocation, we do not want
	 * to heavily disrupt the system, so we fail the allocation
	 * instead of entering direct reclaim.
	 */
	if (result == COMPACT_DEFERRED)
		return true;

	/*
	 * If compaction in async mode encounters contention or blocks higher
	 * priority task we back off early rather than cause stalls.
	 */
	if (result == COMPACT_CONTENDED)
		return true;

	/*
	 * Page scanners have met but we haven't scanned full zones so this
	 * is a back off in fact.
	 */
	if (result == COMPACT_PARTIAL_SKIPPED)
		return true;

	return false;
}


bool compaction_zonelist_suitable(struct alloc_context *ac, int order,
					int alloc_flags);

extern int kcompactd_run(int nid);
extern void kcompactd_stop(int nid);
extern void wakeup_kcompactd(pg_data_t *pgdat, int order, int classzone_idx);

#else
static inline void compact_pgdat(pg_data_t *pgdat, int order)
{
}

static inline void reset_isolation_suitable(pg_data_t *pgdat)
{
}

static inline enum compact_result compaction_suitable(struct zone *zone, int order,
					int alloc_flags, int classzone_idx)
{
	return COMPACT_SKIPPED;
}

static inline void defer_compaction(struct zone *zone, int order)
{
}

static inline bool compaction_deferred(struct zone *zone, int order)
{
	return true;
}

static inline bool compaction_made_progress(enum compact_result result)
{
	return false;
}

static inline bool compaction_failed(enum compact_result result)
{
	return false;
}

static inline bool compaction_withdrawn(enum compact_result result)
{
	return true;
}

static inline int kcompactd_run(int nid)
{
	return 0;
}
static inline void kcompactd_stop(int nid)
{
}

static inline void wakeup_kcompactd(pg_data_t *pgdat, int order, int classzone_idx)
{
}

#endif /* CONFIG_COMPACTION */

#if defined(CONFIG_COMPACTION) && defined(CONFIG_SYSFS) && defined(CONFIG_NUMA)
struct node;
extern int compaction_register_node(struct node *node);
extern void compaction_unregister_node(struct node *node);

#else

static inline int compaction_register_node(struct node *node)
{
	return 0;
}

static inline void compaction_unregister_node(struct node *node)
{
}
#endif /* CONFIG_COMPACTION && CONFIG_SYSFS && CONFIG_NUMA */

#endif /* _LINUX_COMPACTION_H */
