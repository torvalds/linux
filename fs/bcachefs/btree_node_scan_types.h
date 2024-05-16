/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_NODE_SCAN_TYPES_H
#define _BCACHEFS_BTREE_NODE_SCAN_TYPES_H

#include "darray.h"

struct found_btree_node {
	bool			range_updated:1;
	bool			overwritten:1;
	u8			btree_id;
	u8			level;
	u32			seq;
	u64			cookie;

	struct bpos		min_key;
	struct bpos		max_key;

	unsigned		nr_ptrs;
	struct bch_extent_ptr	ptrs[BCH_REPLICAS_MAX];
};

typedef DARRAY(struct found_btree_node)	found_btree_nodes;

struct find_btree_nodes {
	int			ret;
	struct mutex		lock;
	found_btree_nodes	nodes;
};

#endif /* _BCACHEFS_BTREE_NODE_SCAN_TYPES_H */
