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

#include "context.h"

struct sidtab_entry_leaf {
	struct context context;
};

struct sidtab_node_inner;
struct sidtab_node_leaf;

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
	(SIDTAB_NODE_ALLOC_SIZE / sizeof(struct sidtab_entry_leaf))

#define SIDTAB_MAX_BITS 31 /* limited to INT_MAX due to atomic_t range */
#define SIDTAB_MAX (((u32)1 << SIDTAB_MAX_BITS) - 1)
/* ensure enough tree levels for SIDTAB_MAX entries */
#define SIDTAB_MAX_LEVEL \
	DIV_ROUND_UP(SIDTAB_MAX_BITS - size_to_shift(SIDTAB_LEAF_ENTRIES), \
		     SIDTAB_INNER_SHIFT)

struct sidtab_node_leaf {
	struct sidtab_entry_leaf entries[SIDTAB_LEAF_ENTRIES];
};

struct sidtab_node_inner {
	union sidtab_entry_inner entries[SIDTAB_INNER_ENTRIES];
};

struct sidtab_isid_entry {
	int set;
	struct context context;
};

struct sidtab_convert_params {
	int (*func)(struct context *oldc, struct context *newc, void *args);
	void *args;
	struct sidtab *target;
};

#define SIDTAB_RCACHE_SIZE 3

struct sidtab {
	union sidtab_entry_inner roots[SIDTAB_MAX_LEVEL + 1];
	atomic_t count;
	struct sidtab_convert_params *convert;
	spinlock_t lock;

	/* reverse lookup cache */
	atomic_t rcache[SIDTAB_RCACHE_SIZE];

	/* index == SID - 1 (no entry for SECSID_NULL) */
	struct sidtab_isid_entry isids[SECINITSID_NUM];
};

int sidtab_init(struct sidtab *s);
int sidtab_set_initial(struct sidtab *s, u32 sid, struct context *context);
struct context *sidtab_search(struct sidtab *s, u32 sid);
struct context *sidtab_search_force(struct sidtab *s, u32 sid);

int sidtab_convert(struct sidtab *s, struct sidtab_convert_params *params);

int sidtab_context_to_sid(struct sidtab *s, struct context *context, u32 *sid);

void sidtab_destroy(struct sidtab *s);

#endif	/* _SS_SIDTAB_H_ */


