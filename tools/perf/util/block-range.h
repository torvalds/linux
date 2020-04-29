/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_BLOCK_RANGE_H
#define __PERF_BLOCK_RANGE_H

#include <stdbool.h>
#include <linux/rbtree.h>
#include <linux/types.h>

struct symbol;

/*
 * struct block_range - non-overlapping parts of basic blocks
 * @node:	treenode
 * @start:	inclusive start of range
 * @end:	inclusive end of range
 * @is_target:	@start is a jump target
 * @is_branch:	@end is a branch instruction
 * @coverage:	number of blocks that cover this range
 * @taken:	number of times the branch is taken (requires @is_branch)
 * @pred:	number of times the taken branch was predicted
 */
struct block_range {
	struct rb_node node;

	struct symbol *sym;

	u64 start;
	u64 end;

	int is_target, is_branch;

	u64 coverage;
	u64 entry;
	u64 taken;
	u64 pred;
};

static inline struct block_range *block_range__next(struct block_range *br)
{
	struct rb_node *n = rb_next(&br->node);
	if (!n)
		return NULL;
	return rb_entry(n, struct block_range, node);
}

struct block_range_iter {
	struct block_range *start;
	struct block_range *end;
};

static inline struct block_range *block_range_iter(struct block_range_iter *iter)
{
	return iter->start;
}

static inline bool block_range_iter__next(struct block_range_iter *iter)
{
	if (iter->start == iter->end)
		return false;

	iter->start = block_range__next(iter->start);
	return true;
}

static inline bool block_range_iter__valid(struct block_range_iter *iter)
{
	if (!iter->start || !iter->end)
		return false;
	return true;
}

extern struct block_range *block_range__find(u64 addr);
extern struct block_range_iter block_range__create(u64 start, u64 end);
extern double block_range__coverage(struct block_range *br);

#endif /* __PERF_BLOCK_RANGE_H */
