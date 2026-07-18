/* SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause */

#pragma once

#define RB_MAXLVL_PRINT (16)

struct rbnode;

struct rbnode {
	struct rbnode __arena *parent;
	union {
		struct {
			struct rbnode __arena *left;
			struct rbnode __arena *right;
		};

		struct rbnode __arena *child[2];
	};
	uint64_t key;
	/* Used as a linked list or to store KV pairs. */
	union {
		struct rbnode __arena *next;
		uint64_t value;
	};
	bool is_red;
};

/*
 * Does the rbtree allocate its own nodes, or do they get
 * allocated by the caller?
 */
enum rbtree_alloc {
	RB_ALLOC,
	RB_NOALLOC,
};

/*
 * Specify the behavior of rbtree insertions when the key is
 * already present in the tree.
 *
 * RB_DEFAULT: Default behavior, reject the new insert.
 *
 * RB_UPDATE: Update the existing value in the rbtree.
 * This updates the node itself, not just the value in
 * the existing node.
 *
 * RB_DUPLICATE: Allow nodes with identical keys in the rbtree.
 * Finding/popping/removing a key acts on any of the nodes
 * with the appropriate key - there is no ordering by time
 * of insertion.
 */
enum rbtree_insert_mode {
	RB_DEFAULT,
	RB_UPDATE,
	RB_DUPLICATE,
};

struct rbtree {
	struct rbnode __arena *root;
	enum rbtree_alloc alloc;
	enum rbtree_insert_mode insert;
};

#ifdef __BPF__
struct rbtree __arena *rb_create(enum rbtree_alloc alloc, enum rbtree_insert_mode insert);

int rb_destroy(struct rbtree __arena *rbtree);
int rb_insert(struct rbtree __arena *rbtree, u64 key, u64 value);
int rb_remove(struct rbtree __arena *rbtree, u64 key);
int rb_find(struct rbtree __arena *rbtree, u64 key, u64 *value);
int rb_print(struct rbtree __arena *rbtree);
int rb_least(struct rbtree __arena *rbtree, u64 *key, u64 *value);
int rb_pop(struct rbtree __arena *rbtree, u64 *key, u64 *value);

int rb_insert_node(struct rbtree __arena *rbtree, struct rbnode __arena *node);
int rb_remove_node(struct rbtree __arena *rbtree, struct rbnode __arena *node);

struct rbnode __arena *rb_node_alloc(u64 key, u64 value);
void rb_node_free(struct rbnode __arena *rbnode);

int rb_integrity_check(struct rbtree __arena *rbtree);

#endif /* __BPF__ */
