/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A security identifier table (sidtab) is a lookup table
 * of security context structures indexed by SID value.
 *
 * Original author: Stephen Smalley, <sds@tycho.nsa.gov>
 * Author: Ondrej Mosnacek, <omosnacek@gmail.com>
 *
 * Copyright (C) 2018 Red Hat, Inc.
 */
#ifndef _SS_SIDTAB_H_
#define _SS_SIDTAB_H_

#include <linux/spinlock_types.h>
#include <linux/log2.h>
#include <linux/hashtable.h>

#include "context.h"

struct sidtab_entry {
	u32 sid;
	struct context context;
#if CONFIG_SECURITY_SELINUX_SID2STR_CACHE_SIZE > 0
	struct sidtab_str_cache __rcu *cache;
#endif
	struct hlist_node list;
};

union sidtab_entry_inner {
	struct sidtab_node_inner *ptr_inner;
	struct sidtab_node_leaf  *ptr_leaf;
};

/* align node size to page boundary */
#define SIDTAB_NODE_ALLOC_SHIFT PAGE_SHIFT
#define SIDTAB_NODE_ALLOC_SIZE  PAGE_SIZE

#define size_to_shift(size) ((size) == 1 ? 1 : (const_ilog2((size) - 1) + 1))

#define SIDTAB_INNER_SHIFT \
	(SIDTAB_NODE_ALLOC_SHIFT - size_to_shift(sizeof(union sidtab_entry_inner)))
#define SIDTAB_INNER_ENTRIES ((size_t)1 << SIDTAB_INNER_SHIFT)
#define SIDTAB_LEAF_ENTRIES \
	(SIDTAB_NODE_ALLOC_SIZE / sizeof(struct sidtab_entry))

#define SIDTAB_MAX_BITS 32
#define SIDTAB_MAX U32_MAX
/* ensure enough tree levels for SIDTAB_MAX entries */
#define SIDTAB_MAX_LEVEL \
	DIV_ROUND_UP(SIDTAB_MAX_BITS - size_to_shift(SIDTAB_LEAF_ENTRIES), \
		     SIDTAB_INNER_SHIFT)

struct sidtab_node_leaf {
	struct sidtab_entry entries[SIDTAB_LEAF_ENTRIES];
};

struct sidtab_node_inner {
	union sidtab_entry_inner entries[SIDTAB_INNER_ENTRIES];
};

struct sidtab_isid_entry {
	int set;
	struct sidtab_entry entry;
};

struct sidtab_convert_params {
	int (*func)(struct context *oldc, struct context *newc, void *args);
	void *args;
	struct sidtab *target;
};

#define SIDTAB_HASH_BITS CONFIG_SECURITY_SELINUX_SIDTAB_HASH_BITS
#define SIDTAB_HASH_BUCKETS (1 << SIDTAB_HASH_BITS)

struct sidtab {
	/*
	 * lock-free read access only for as many items as a prior read of
	 * 'count'
	 */
	union sidtab_entry_inner roots[SIDTAB_MAX_LEVEL + 1];
	/*
	 * access atomically via {READ|WRITE}_ONCE(); only increment under
	 * spinlock
	 */
	u32 count;
	/* access only under spinlock */
	struct sidtab_convert_params *convert;
	spinlock_t lock;

#if CONFIG_SECURITY_SELINUX_SID2STR_CACHE_SIZE > 0
	/* SID -> context string cache */
	u32 cache_free_slots;
	struct list_head cache_lru_list;
	spinlock_t cache_lock;
#endif

	/* index == SID - 1 (no entry for SECSID_NULL) */
	struct sidtab_isid_entry isids[SECINITSID_NUM];

	/* Hash table for fast reverse context-to-sid lookups. */
	DECLARE_HASHTABLE(context_to_sid, SIDTAB_HASH_BITS);
};

int sidtab_init(struct sidtab *s);
int sidtab_set_initial(struct sidtab *s, u32 sid, struct context *context);
struct sidtab_entry *sidtab_search_entry(struct sidtab *s, u32 sid);
struct sidtab_entry *sidtab_search_entry_force(struct sidtab *s, u32 sid);

static inline struct context *sidtab_search(struct sidtab *s, u32 sid)
{
	struct sidtab_entry *entry = sidtab_search_entry(s, sid);

	return entry ? &entry->context : NULL;
}

static inline struct context *sidtab_search_force(struct sidtab *s, u32 sid)
{
	struct sidtab_entry *entry = sidtab_search_entry_force(s, sid);

	return entry ? &entry->context : NULL;
}

int sidtab_convert(struct sidtab *s, struct sidtab_convert_params *params);

int sidtab_context_to_sid(struct sidtab *s, struct context *context, u32 *sid);

void sidtab_destroy(struct sidtab *s);

int sidtab_hash_stats(struct sidtab *sidtab, char *page);

#if CONFIG_SECURITY_SELINUX_SID2STR_CACHE_SIZE > 0
void sidtab_sid2str_put(struct sidtab *s, struct sidtab_entry *entry,
			const char *str, u32 str_len);
int sidtab_sid2str_get(struct sidtab *s, struct sidtab_entry *entry,
		       char **out, u32 *out_len);
#else
static inline void sidtab_sid2str_put(struct sidtab *s,
				      struct sidtab_entry *entry,
				      const char *str, u32 str_len)
{
}
static inline int sidtab_sid2str_get(struct sidtab *s,
				     struct sidtab_entry *entry,
				     char **out, u32 *out_len)
{
	return -ENOENT;
}
#endif /* CONFIG_SECURITY_SELINUX_SID2STR_CACHE_SIZE > 0 */

#endif	/* _SS_SIDTAB_H_ */


