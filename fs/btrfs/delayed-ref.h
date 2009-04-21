/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */
#ifndef __DELAYED_REF__
#define __DELAYED_REF__

/* these are the possible values of struct btrfs_delayed_ref->action */
#define BTRFS_ADD_DELAYED_REF    1 /* add one backref to the tree */
#define BTRFS_DROP_DELAYED_REF   2 /* delete one backref from the tree */
#define BTRFS_ADD_DELAYED_EXTENT 3 /* record a full extent allocation */
#define BTRFS_UPDATE_DELAYED_HEAD 4 /* not changing ref count on head ref */

struct btrfs_delayed_ref_node {
	struct rb_node rb_node;

	/* the starting bytenr of the extent */
	u64 bytenr;

	/* the parent our backref will point to */
	u64 parent;

	/* the size of the extent */
	u64 num_bytes;

	/* ref count on this data structure */
	atomic_t refs;

	/*
	 * how many refs is this entry adding or deleting.  For
	 * head refs, this may be a negative number because it is keeping
	 * track of the total mods done to the reference count.
	 * For individual refs, this will always be a positive number
	 *
	 * It may be more than one, since it is possible for a single
	 * parent to have more than one ref on an extent
	 */
	int ref_mod;

	/* is this node still in the rbtree? */
	unsigned int in_tree:1;
};

/*
 * the head refs are used to hold a lock on a given extent, which allows us
 * to make sure that only one process is running the delayed refs
 * at a time for a single extent.  They also store the sum of all the
 * reference count modifications we've queued up.
 */
struct btrfs_delayed_ref_head {
	struct btrfs_delayed_ref_node node;

	/*
	 * the mutex is held while running the refs, and it is also
	 * held when checking the sum of reference modifications.
	 */
	struct mutex mutex;

	struct list_head cluster;

	/*
	 * when a new extent is allocated, it is just reserved in memory
	 * The actual extent isn't inserted into the extent allocation tree
	 * until the delayed ref is processed.  must_insert_reserved is
	 * used to flag a delayed ref so the accounting can be updated
	 * when a full insert is done.
	 *
	 * It is possible the extent will be freed before it is ever
	 * inserted into the extent allocation tree.  In this case
	 * we need to update the in ram accounting to properly reflect
	 * the free has happened.
	 */
	unsigned int must_insert_reserved:1;
};

struct btrfs_delayed_ref {
	struct btrfs_delayed_ref_node node;

	/* the root objectid our ref will point to */
	u64 root;

	/* the generation for the backref */
	u64 generation;

	/* owner_objectid of the backref  */
	u64 owner_objectid;

	/* operation done by this entry in the rbtree */
	u8 action;

	/* if pin == 1, when the extent is freed it will be pinned until
	 * transaction commit
	 */
	unsigned int pin:1;
};

struct btrfs_delayed_ref_root {
	struct rb_root root;

	/* this spin lock protects the rbtree and the entries inside */
	spinlock_t lock;

	/* how many delayed ref updates we've queued, used by the
	 * throttling code
	 */
	unsigned long num_entries;

	/* total number of head nodes in tree */
	unsigned long num_heads;

	/* total number of head nodes ready for processing */
	unsigned long num_heads_ready;

	/*
	 * set when the tree is flushing before a transaction commit,
	 * used by the throttling code to decide if new updates need
	 * to be run right away
	 */
	int flushing;

	u64 run_delayed_start;
};

static inline void btrfs_put_delayed_ref(struct btrfs_delayed_ref_node *ref)
{
	WARN_ON(atomic_read(&ref->refs) == 0);
	if (atomic_dec_and_test(&ref->refs)) {
		WARN_ON(ref->in_tree);
		kfree(ref);
	}
}

int btrfs_add_delayed_ref(struct btrfs_trans_handle *trans,
			  u64 bytenr, u64 num_bytes, u64 parent, u64 ref_root,
			  u64 ref_generation, u64 owner_objectid, int action,
			  int pin);

struct btrfs_delayed_ref_head *
btrfs_find_delayed_ref_head(struct btrfs_trans_handle *trans, u64 bytenr);
int btrfs_delayed_ref_pending(struct btrfs_trans_handle *trans, u64 bytenr);
int btrfs_lookup_extent_ref(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root, u64 bytenr,
			    u64 num_bytes, u32 *refs);
int btrfs_update_delayed_ref(struct btrfs_trans_handle *trans,
			  u64 bytenr, u64 num_bytes, u64 orig_parent,
			  u64 parent, u64 orig_ref_root, u64 ref_root,
			  u64 orig_ref_generation, u64 ref_generation,
			  u64 owner_objectid, int pin);
int btrfs_delayed_ref_lock(struct btrfs_trans_handle *trans,
			   struct btrfs_delayed_ref_head *head);
int btrfs_find_ref_cluster(struct btrfs_trans_handle *trans,
			   struct list_head *cluster, u64 search_start);
/*
 * a node might live in a head or a regular ref, this lets you
 * test for the proper type to use.
 */
static int btrfs_delayed_ref_is_head(struct btrfs_delayed_ref_node *node)
{
	return node->parent == (u64)-1;
}

/*
 * helper functions to cast a node into its container
 */
static inline struct btrfs_delayed_ref *
btrfs_delayed_node_to_ref(struct btrfs_delayed_ref_node *node)
{
	WARN_ON(btrfs_delayed_ref_is_head(node));
	return container_of(node, struct btrfs_delayed_ref, node);

}

static inline struct btrfs_delayed_ref_head *
btrfs_delayed_node_to_head(struct btrfs_delayed_ref_node *node)
{
	WARN_ON(!btrfs_delayed_ref_is_head(node));
	return container_of(node, struct btrfs_delayed_ref_head, node);

}
#endif
