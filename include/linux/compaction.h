#ifndef _LINUX_COMPACTION_H
#define _LINUX_COMPACTION_H

/* Return values for compact_zone() and try_to_compact_pages() */
/* compaction didn't start as it was deferred due to past failures */
#define COMPACT_DEFERRED	0
/* compaction didn't start as it was not possible or direct reclaim was more suitable */
#define COMPACT_SKIPPED		1
/* compaction should continue to another pageblock */
#define COMPACT_CONTINUE	2
/* direct compaction partially compacted a zone and there are suitable pages */
#define COMPACT_PARTIAL		3
/* The full zone was compacted */
#define COMPACT_COMPLETE	4
/* For more detailed tracepoint output */
#define COMPACT_NO_SUITABLE_PAGE	5
#define COMPACT_NOT_SUITABLE_ZONE	6
/* When adding new state, please change compaction_status_string, too */

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
extern unsigned long try_to_compact_pages(gfp_t gfp_mask, unsigned int order,
			int alloc_flags, const struct alloc_context *ac,
			enum migrate_mode mode, int *contended);
extern void compact_pgdat(pg_data_t *pgdat, int order);
extern void reset_isolation_suitable(pg_data_t *pgdat);
extern unsigned long compaction_suitable(struct zone *zone, int order,
					int alloc_flags, int classzone_idx);

extern void defer_compaction(struct zone *zone, int order);
extern bool compaction_deferred(struct zone *zone, int order);
extern void compaction_defer_reset(struct zone *zone, int order,
				bool alloc_success);
extern bool compaction_restarting(struct zone *zone, int order);

#else
static inline unsigned long try_to_compact_pages(gfp_t gfp_mask,
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

static inline unsigned long compaction_suitable(struct zone *zone, int order,
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
