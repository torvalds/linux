#ifndef _LINUX_COMPACTION_H
#define _LINUX_COMPACTION_H

/* Return values for compact_zone() and try_to_compact_pages() */
/* When adding new states, please adjust include/trace/events/compaction.h */
enum compact_result {
	/* compaction didn't start as it was deferred due to past failures */
	COMPACT_DEFERRED,
	/*
	 * compaction didn't start as it was not possible or direct reclaim
	 * was more suitable
	 */
	COMPACT_SKIPPED,
	/* compaction should continue to another pageblock */
	COMPACT_CONTINUE,
	/*
	 * direct compaction partially compacted a zone and there are suitable
	 * pages
	 */
	COMPACT_PARTIAL,
	/* The full zone was compacted */
	COMPACT_COMPLETE,
	/* For more detailed tracepoint output */
	COMPACT_NO_SUITABLE_PAGE,
	COMPACT_NOT_SUITABLE_ZONE,
	COMPACT_CONTENDED,
};

/* Used to signal whether compaction detected need_sched() or lock contention */
/* No contention detected */
#define COMPACT_CONTENDED_NONE	0
/* Either need_sched() was true or fatal signal pending */
#define COMPACT_CONTENDED_SCHED	1
/* Zone lock or lru_lock was contended in async compaction */
#define COMPACT_CONTENDED_LOCK	2

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
			unsigned int order,
		unsigned int alloc_flags, const struct alloc_context *ac,
		enum migrate_mode mode, int *contended);
extern void compact_pgdat(pg_data_t *pgdat, int order);
extern void reset_isolation_suitable(pg_data_t *pgdat);
extern enum compact_result compaction_suitable(struct zone *zone, int order,
		unsigned int alloc_flags, int classzone_idx);

extern void defer_compaction(struct zone *zone, int order);
extern bool compaction_deferred(struct zone *zone, int order);
extern void compaction_defer_reset(struct zone *zone, int order,
				bool alloc_success);
extern bool compaction_restarting(struct zone *zone, int order);

extern int kcompactd_run(int nid);
extern void kcompactd_stop(int nid);
extern void wakeup_kcompactd(pg_data_t *pgdat, int order, int classzone_idx);

#else
static inline enum compact_result try_to_compact_pages(gfp_t gfp_mask,
			unsigned int order, int alloc_flags,
			const struct alloc_context *ac,
			enum migrate_mode mode, int *contended)
{
	return COMPACT_CONTINUE;
}

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
