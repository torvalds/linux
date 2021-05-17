/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * call-path.h: Manipulate a tree data structure containing function call paths
 * Copyright (c) 2014, Intel Corporation.
 */

#ifndef __PERF_CALL_PATH_H
#define __PERF_CALL_PATH_H

#include <sys/types.h>

#include <linux/types.h>
#include <linux/rbtree.h>

/**
 * struct call_path - node in list of calls leading to a function call.
 * @parent: call path to the parent function call
 * @sym: symbol of function called
 * @ip: only if sym is null, the ip of the function
 * @db_id: id used for db-export
 * @in_kernel: whether function is a in the kernel
 * @rb_node: node in parent's tree of called functions
 * @children: tree of call paths of functions called
 *
 * In combination with the call_return structure, the call_path structure
 * defines a context-sensitive call-graph.
 */
struct call_path {
	struct call_path *parent;
	struct symbol *sym;
	u64 ip;
	u64 db_id;
	bool in_kernel;
	struct rb_node rb_node;
	struct rb_root children;
};

#define CALL_PATH_BLOCK_SHIFT 8
#define CALL_PATH_BLOCK_SIZE (1 << CALL_PATH_BLOCK_SHIFT)
#define CALL_PATH_BLOCK_MASK (CALL_PATH_BLOCK_SIZE - 1)

struct call_path_block {
	struct call_path cp[CALL_PATH_BLOCK_SIZE];
	struct list_head node;
};

/**
 * struct call_path_root - root of all call paths.
 * @call_path: root call path
 * @blocks: list of blocks to store call paths
 * @next: next free space
 * @sz: number of spaces
 */
struct call_path_root {
	struct call_path call_path;
	struct list_head blocks;
	size_t next;
	size_t sz;
};

struct call_path_root *call_path_root__new(void);
void call_path_root__free(struct call_path_root *cpr);

struct call_path *call_path__findnew(struct call_path_root *cpr,
				     struct call_path *parent,
				     struct symbol *sym, u64 ip, u64 ks);

#endif
