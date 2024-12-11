/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#ifndef _RANGE_TREE_H
#define _RANGE_TREE_H 1

struct range_tree {
	/* root of interval tree */
	struct rb_root_cached it_root;
	/* root of rbtree of interval sizes */
	struct rb_root_cached range_size_root;
};

void range_tree_init(struct range_tree *rt);
void range_tree_destroy(struct range_tree *rt);

int range_tree_clear(struct range_tree *rt, u32 start, u32 len);
int range_tree_set(struct range_tree *rt, u32 start, u32 len);
int is_range_tree_set(struct range_tree *rt, u32 start, u32 len);
s64 range_tree_find(struct range_tree *rt, u32 len);

#endif
