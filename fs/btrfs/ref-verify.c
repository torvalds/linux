/*
 * Copyright (C) 2014 Facebook.  All rights reserved.
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

#include <linux/sched.h>
#include <linux/stacktrace.h>
#include "ctree.h"
#include "disk-io.h"
#include "locking.h"
#include "delayed-ref.h"
#include "ref-verify.h"

/*
 * Used to keep track the roots and number of refs each root has for a given
 * bytenr.  This just tracks the number of direct references, no shared
 * references.
 */
struct root_entry {
	u64 root_objectid;
	u64 num_refs;
	struct rb_node node;
};

/*
 * These are meant to represent what should exist in the extent tree, these can
 * be used to verify the extent tree is consistent as these should all match
 * what the extent tree says.
 */
struct ref_entry {
	u64 root_objectid;
	u64 parent;
	u64 owner;
	u64 offset;
	u64 num_refs;
	struct rb_node node;
};

#define MAX_TRACE	16

/*
 * Whenever we add/remove a reference we record the action.  The action maps
 * back to the delayed ref action.  We hold the ref we are changing in the
 * action so we can account for the history properly, and we record the root we
 * were called with since it could be different from ref_root.  We also store
 * stack traces because thats how I roll.
 */
struct ref_action {
	int action;
	u64 root;
	struct ref_entry ref;
	struct list_head list;
	unsigned long trace[MAX_TRACE];
	unsigned int trace_len;
};

/*
 * One of these for every block we reference, it holds the roots and references
 * to it as well as all of the ref actions that have occured to it.  We never
 * free it until we unmount the file system in order to make sure re-allocations
 * are happening properly.
 */
struct block_entry {
	u64 bytenr;
	u64 len;
	u64 num_refs;
	int metadata;
	int from_disk;
	struct rb_root roots;
	struct rb_root refs;
	struct rb_node node;
	struct list_head actions;
};

static struct block_entry *insert_block_entry(struct rb_root *root,
					      struct block_entry *be)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent_node = NULL;
	struct block_entry *entry;

	while (*p) {
		parent_node = *p;
		entry = rb_entry(parent_node, struct block_entry, node);
		if (entry->bytenr > be->bytenr)
			p = &(*p)->rb_left;
		else if (entry->bytenr < be->bytenr)
			p = &(*p)->rb_right;
		else
			return entry;
	}

	rb_link_node(&be->node, parent_node, p);
	rb_insert_color(&be->node, root);
	return NULL;
}

static struct block_entry *lookup_block_entry(struct rb_root *root, u64 bytenr)
{
	struct rb_node *n;
	struct block_entry *entry = NULL;

	n = root->rb_node;
	while (n) {
		entry = rb_entry(n, struct block_entry, node);
		if (entry->bytenr < bytenr)
			n = n->rb_right;
		else if (entry->bytenr > bytenr)
			n = n->rb_left;
		else
			return entry;
	}
	return NULL;
}

static struct root_entry *insert_root_entry(struct rb_root *root,
					    struct root_entry *re)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent_node = NULL;
	struct root_entry *entry;

	while (*p) {
		parent_node = *p;
		entry = rb_entry(parent_node, struct root_entry, node);
		if (entry->root_objectid > re->root_objectid)
			p = &(*p)->rb_left;
		else if (entry->root_objectid < re->root_objectid)
			p = &(*p)->rb_right;
		else
			return entry;
	}

	rb_link_node(&re->node, parent_node, p);
	rb_insert_color(&re->node, root);
	return NULL;

}

static int comp_refs(struct ref_entry *ref1, struct ref_entry *ref2)
{
	if (ref1->root_objectid < ref2->root_objectid)
		return -1;
	if (ref1->root_objectid > ref2->root_objectid)
		return 1;
	if (ref1->parent < ref2->parent)
		return -1;
	if (ref1->parent > ref2->parent)
		return 1;
	if (ref1->owner < ref2->owner)
		return -1;
	if (ref1->owner > ref2->owner)
		return 1;
	if (ref1->offset < ref2->offset)
		return -1;
	if (ref1->offset > ref2->offset)
		return 1;
	return 0;
}

static struct ref_entry *insert_ref_entry(struct rb_root *root,
					  struct ref_entry *ref)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent_node = NULL;
	struct ref_entry *entry;
	int cmp;

	while (*p) {
		parent_node = *p;
		entry = rb_entry(parent_node, struct ref_entry, node);
		cmp = comp_refs(entry, ref);
		if (cmp > 0)
			p = &(*p)->rb_left;
		else if (cmp < 0)
			p = &(*p)->rb_right;
		else
			return entry;
	}

	rb_link_node(&ref->node, parent_node, p);
	rb_insert_color(&ref->node, root);
	return NULL;

}

static struct root_entry *lookup_root_entry(struct rb_root *root, u64 objectid)
{
	struct rb_node *n;
	struct root_entry *entry = NULL;

	n = root->rb_node;
	while (n) {
		entry = rb_entry(n, struct root_entry, node);
		if (entry->root_objectid < objectid)
			n = n->rb_right;
		else if (entry->root_objectid > objectid)
			n = n->rb_left;
		else
			return entry;
	}
	return NULL;
}

#ifdef CONFIG_STACKTRACE
static void __save_stack_trace(struct ref_action *ra)
{
	struct stack_trace stack_trace;

	stack_trace.max_entries = MAX_TRACE;
	stack_trace.nr_entries = 0;
	stack_trace.entries = ra->trace;
	stack_trace.skip = 2;
	save_stack_trace(&stack_trace);
	ra->trace_len = stack_trace.nr_entries;
}

static void __print_stack_trace(struct btrfs_fs_info *fs_info,
				struct ref_action *ra)
{
	struct stack_trace trace;

	if (ra->trace_len == 0) {
		btrfs_err(fs_info, "  ref-verify: no stacktrace");
		return;
	}
	trace.nr_entries = ra->trace_len;
	trace.entries = ra->trace;
	print_stack_trace(&trace, 2);
}
#else
static void inline __save_stack_trace(struct ref_action *ra)
{
}

static void inline __print_stack_trace(struct btrfs_fs_info *fs_info,
				       struct ref_action *ra)
{
	btrfs_err(fs_info, "  ref-verify: no stacktrace support");
}
#endif

static void free_block_entry(struct block_entry *be)
{
	struct root_entry *re;
	struct ref_entry *ref;
	struct ref_action *ra;
	struct rb_node *n;

	while ((n = rb_first(&be->roots))) {
		re = rb_entry(n, struct root_entry, node);
		rb_erase(&re->node, &be->roots);
		kfree(re);
	}

	while((n = rb_first(&be->refs))) {
		ref = rb_entry(n, struct ref_entry, node);
		rb_erase(&ref->node, &be->refs);
		kfree(ref);
	}

	while (!list_empty(&be->actions)) {
		ra = list_first_entry(&be->actions, struct ref_action,
				      list);
		list_del(&ra->list);
		kfree(ra);
	}
	kfree(be);
}

static struct block_entry *add_block_entry(struct btrfs_fs_info *fs_info,
					   u64 bytenr, u64 len,
					   u64 root_objectid)
{
	struct block_entry *be = NULL, *exist;
	struct root_entry *re = NULL;

	re = kzalloc(sizeof(struct root_entry), GFP_KERNEL);
	be = kzalloc(sizeof(struct block_entry), GFP_KERNEL);
	if (!be || !re) {
		kfree(re);
		kfree(be);
		return ERR_PTR(-ENOMEM);
	}
	be->bytenr = bytenr;
	be->len = len;

	re->root_objectid = root_objectid;
	re->num_refs = 0;

	spin_lock(&fs_info->ref_verify_lock);
	exist = insert_block_entry(&fs_info->block_tree, be);
	if (exist) {
		if (root_objectid) {
			struct root_entry *exist_re;

			exist_re = insert_root_entry(&exist->roots, re);
			if (exist_re)
				kfree(re);
		}
		kfree(be);
		return exist;
	}

	be->num_refs = 0;
	be->metadata = 0;
	be->from_disk = 0;
	be->roots = RB_ROOT;
	be->refs = RB_ROOT;
	INIT_LIST_HEAD(&be->actions);
	if (root_objectid)
		insert_root_entry(&be->roots, re);
	else
		kfree(re);
	return be;
}

static int add_tree_block(struct btrfs_fs_info *fs_info, u64 ref_root,
			  u64 parent, u64 bytenr, int level)
{
	struct block_entry *be;
	struct root_entry *re;
	struct ref_entry *ref = NULL, *exist;

	ref = kmalloc(sizeof(struct ref_entry), GFP_KERNEL);
	if (!ref)
		return -ENOMEM;

	if (parent)
		ref->root_objectid = 0;
	else
		ref->root_objectid = ref_root;
	ref->parent = parent;
	ref->owner = level;
	ref->offset = 0;
	ref->num_refs = 1;

	be = add_block_entry(fs_info, bytenr, fs_info->nodesize, ref_root);
	if (IS_ERR(be)) {
		kfree(ref);
		return PTR_ERR(be);
	}
	be->num_refs++;
	be->from_disk = 1;
	be->metadata = 1;

	if (!parent) {
		ASSERT(ref_root);
		re = lookup_root_entry(&be->roots, ref_root);
		ASSERT(re);
		re->num_refs++;
	}
	exist = insert_ref_entry(&be->refs, ref);
	if (exist) {
		exist->num_refs++;
		kfree(ref);
	}
	spin_unlock(&fs_info->ref_verify_lock);

	return 0;
}

static int add_shared_data_ref(struct btrfs_fs_info *fs_info,
			       u64 parent, u32 num_refs, u64 bytenr,
			       u64 num_bytes)
{
	struct block_entry *be;
	struct ref_entry *ref;

	ref = kzalloc(sizeof(struct ref_entry), GFP_KERNEL);
	if (!ref)
		return -ENOMEM;
	be = add_block_entry(fs_info, bytenr, num_bytes, 0);
	if (IS_ERR(be)) {
		kfree(ref);
		return PTR_ERR(be);
	}
	be->num_refs += num_refs;

	ref->parent = parent;
	ref->num_refs = num_refs;
	if (insert_ref_entry(&be->refs, ref)) {
		spin_unlock(&fs_info->ref_verify_lock);
		btrfs_err(fs_info, "existing shared ref when reading from disk?");
		kfree(ref);
		return -EINVAL;
	}
	spin_unlock(&fs_info->ref_verify_lock);
	return 0;
}

static int add_extent_data_ref(struct btrfs_fs_info *fs_info,
			       struct extent_buffer *leaf,
			       struct btrfs_extent_data_ref *dref,
			       u64 bytenr, u64 num_bytes)
{
	struct block_entry *be;
	struct ref_entry *ref;
	struct root_entry *re;
	u64 ref_root = btrfs_extent_data_ref_root(leaf, dref);
	u64 owner = btrfs_extent_data_ref_objectid(leaf, dref);
	u64 offset = btrfs_extent_data_ref_offset(leaf, dref);
	u32 num_refs = btrfs_extent_data_ref_count(leaf, dref);

	ref = kzalloc(sizeof(struct ref_entry), GFP_KERNEL);
	if (!ref)
		return -ENOMEM;
	be = add_block_entry(fs_info, bytenr, num_bytes, ref_root);
	if (IS_ERR(be)) {
		kfree(ref);
		return PTR_ERR(be);
	}
	be->num_refs += num_refs;

	ref->parent = 0;
	ref->owner = owner;
	ref->root_objectid = ref_root;
	ref->offset = offset;
	ref->num_refs = num_refs;
	if (insert_ref_entry(&be->refs, ref)) {
		spin_unlock(&fs_info->ref_verify_lock);
		btrfs_err(fs_info, "existing ref when reading from disk?");
		kfree(ref);
		return -EINVAL;
	}

	re = lookup_root_entry(&be->roots, ref_root);
	if (!re) {
		spin_unlock(&fs_info->ref_verify_lock);
		btrfs_err(fs_info, "missing root in new block entry?");
		return -EINVAL;
	}
	re->num_refs += num_refs;
	spin_unlock(&fs_info->ref_verify_lock);
	return 0;
}

static int process_extent_item(struct btrfs_fs_info *fs_info,
			       struct btrfs_path *path, struct btrfs_key *key,
			       int slot, int *tree_block_level)
{
	struct btrfs_extent_item *ei;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_extent_data_ref *dref;
	struct btrfs_shared_data_ref *sref;
	struct extent_buffer *leaf = path->nodes[0];
	u32 item_size = btrfs_item_size_nr(leaf, slot);
	unsigned long end, ptr;
	u64 offset, flags, count;
	int type, ret;

	ei = btrfs_item_ptr(leaf, slot, struct btrfs_extent_item);
	flags = btrfs_extent_flags(leaf, ei);

	if ((key->type == BTRFS_EXTENT_ITEM_KEY) &&
	    flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		struct btrfs_tree_block_info *info;

		info = (struct btrfs_tree_block_info *)(ei + 1);
		*tree_block_level = btrfs_tree_block_level(leaf, info);
		iref = (struct btrfs_extent_inline_ref *)(info + 1);
	} else {
		if (key->type == BTRFS_METADATA_ITEM_KEY)
			*tree_block_level = key->offset;
		iref = (struct btrfs_extent_inline_ref *)(ei + 1);
	}

	ptr = (unsigned long)iref;
	end = (unsigned long)ei + item_size;
	while (ptr < end) {
		iref = (struct btrfs_extent_inline_ref *)ptr;
		type = btrfs_extent_inline_ref_type(leaf, iref);
		offset = btrfs_extent_inline_ref_offset(leaf, iref);
		switch (type) {
		case BTRFS_TREE_BLOCK_REF_KEY:
			ret = add_tree_block(fs_info, offset, 0, key->objectid,
					     *tree_block_level);
			break;
		case BTRFS_SHARED_BLOCK_REF_KEY:
			ret = add_tree_block(fs_info, 0, offset, key->objectid,
					     *tree_block_level);
			break;
		case BTRFS_EXTENT_DATA_REF_KEY:
			dref = (struct btrfs_extent_data_ref *)(&iref->offset);
			ret = add_extent_data_ref(fs_info, leaf, dref,
						  key->objectid, key->offset);
			break;
		case BTRFS_SHARED_DATA_REF_KEY:
			sref = (struct btrfs_shared_data_ref *)(iref + 1);
			count = btrfs_shared_data_ref_count(leaf, sref);
			ret = add_shared_data_ref(fs_info, offset, count,
						  key->objectid, key->offset);
			break;
		default:
			btrfs_err(fs_info, "invalid key type in iref");
			ret = -EINVAL;
			break;
		}
		if (ret)
			break;
		ptr += btrfs_extent_inline_ref_size(type);
	}
	return ret;
}

static int process_leaf(struct btrfs_root *root,
			struct btrfs_path *path, u64 *bytenr, u64 *num_bytes)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *leaf = path->nodes[0];
	struct btrfs_extent_data_ref *dref;
	struct btrfs_shared_data_ref *sref;
	u32 count;
	int i = 0, tree_block_level = 0, ret;
	struct btrfs_key key;
	int nritems = btrfs_header_nritems(leaf);

	for (i = 0; i < nritems; i++) {
		btrfs_item_key_to_cpu(leaf, &key, i);
		switch (key.type) {
		case BTRFS_EXTENT_ITEM_KEY:
			*num_bytes = key.offset;
		case BTRFS_METADATA_ITEM_KEY:
			*bytenr = key.objectid;
			ret = process_extent_item(fs_info, path, &key, i,
						  &tree_block_level);
			break;
		case BTRFS_TREE_BLOCK_REF_KEY:
			ret = add_tree_block(fs_info, key.offset, 0,
					     key.objectid, tree_block_level);
			break;
		case BTRFS_SHARED_BLOCK_REF_KEY:
			ret = add_tree_block(fs_info, 0, key.offset,
					     key.objectid, tree_block_level);
			break;
		case BTRFS_EXTENT_DATA_REF_KEY:
			dref = btrfs_item_ptr(leaf, i,
					      struct btrfs_extent_data_ref);
			ret = add_extent_data_ref(fs_info, leaf, dref, *bytenr,
						  *num_bytes);
			break;
		case BTRFS_SHARED_DATA_REF_KEY:
			sref = btrfs_item_ptr(leaf, i,
					      struct btrfs_shared_data_ref);
			count = btrfs_shared_data_ref_count(leaf, sref);
			ret = add_shared_data_ref(fs_info, key.offset, count,
						  *bytenr, *num_bytes);
			break;
		default:
			break;
		}
		if (ret)
			break;
	}
	return ret;
}

/* Walk down to the leaf from the given level */
static int walk_down_tree(struct btrfs_root *root, struct btrfs_path *path,
			  int level, u64 *bytenr, u64 *num_bytes)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *eb;
	u64 block_bytenr, gen;
	int ret = 0;

	while (level >= 0) {
		if (level) {
			block_bytenr = btrfs_node_blockptr(path->nodes[level],
							   path->slots[level]);
			gen = btrfs_node_ptr_generation(path->nodes[level],
							path->slots[level]);
			eb = read_tree_block(fs_info, block_bytenr, gen);
			if (IS_ERR(eb))
				return PTR_ERR(eb);
			if (!extent_buffer_uptodate(eb)) {
				free_extent_buffer(eb);
				return -EIO;
			}
			btrfs_tree_read_lock(eb);
			btrfs_set_lock_blocking_rw(eb, BTRFS_READ_LOCK);
			path->nodes[level-1] = eb;
			path->slots[level-1] = 0;
			path->locks[level-1] = BTRFS_READ_LOCK_BLOCKING;
		} else {
			ret = process_leaf(root, path, bytenr, num_bytes);
			if (ret)
				break;
		}
		level--;
	}
	return ret;
}

/* Walk up to the next node that needs to be processed */
static int walk_up_tree(struct btrfs_root *root, struct btrfs_path *path,
			int *level)
{
	int l;

	for (l = 0; l < BTRFS_MAX_LEVEL; l++) {
		if (!path->nodes[l])
			continue;
		if (l) {
			path->slots[l]++;
			if (path->slots[l] <
			    btrfs_header_nritems(path->nodes[l])) {
				*level = l;
				return 0;
			}
		}
		btrfs_tree_unlock_rw(path->nodes[l], path->locks[l]);
		free_extent_buffer(path->nodes[l]);
		path->nodes[l] = NULL;
		path->slots[l] = 0;
		path->locks[l] = 0;
	}

	return 1;
}

static void dump_ref_action(struct btrfs_fs_info *fs_info,
			    struct ref_action *ra)
{
	btrfs_err(fs_info,
"  Ref action %d, root %llu, ref_root %llu, parent %llu, owner %llu, offset %llu, num_refs %llu",
		  ra->action, ra->root, ra->ref.root_objectid, ra->ref.parent,
		  ra->ref.owner, ra->ref.offset, ra->ref.num_refs);
	__print_stack_trace(fs_info, ra);
}

/*
 * Dumps all the information from the block entry to printk, it's going to be
 * awesome.
 */
static void dump_block_entry(struct btrfs_fs_info *fs_info,
			     struct block_entry *be)
{
	struct ref_entry *ref;
	struct root_entry *re;
	struct ref_action *ra;
	struct rb_node *n;

	btrfs_err(fs_info,
"dumping block entry [%llu %llu], num_refs %llu, metadata %d, from disk %d",
		  be->bytenr, be->len, be->num_refs, be->metadata,
		  be->from_disk);

	for (n = rb_first(&be->refs); n; n = rb_next(n)) {
		ref = rb_entry(n, struct ref_entry, node);
		btrfs_err(fs_info,
"  ref root %llu, parent %llu, owner %llu, offset %llu, num_refs %llu",
			  ref->root_objectid, ref->parent, ref->owner,
			  ref->offset, ref->num_refs);
	}

	for (n = rb_first(&be->roots); n; n = rb_next(n)) {
		re = rb_entry(n, struct root_entry, node);
		btrfs_err(fs_info, "  root entry %llu, num_refs %llu",
			  re->root_objectid, re->num_refs);
	}

	list_for_each_entry(ra, &be->actions, list)
		dump_ref_action(fs_info, ra);
}

/*
 * btrfs_ref_tree_mod: called when we modify a ref for a bytenr
 * @root: the root we are making this modification from.
 * @bytenr: the bytenr we are modifying.
 * @num_bytes: number of bytes.
 * @parent: the parent bytenr.
 * @ref_root: the original root owner of the bytenr.
 * @owner: level in the case of metadata, inode in the case of data.
 * @offset: 0 for metadata, file offset for data.
 * @action: the action that we are doing, this is the same as the delayed ref
 *	action.
 *
 * This will add an action item to the given bytenr and do sanity checks to make
 * sure we haven't messed something up.  If we are making a new allocation and
 * this block entry has history we will delete all previous actions as long as
 * our sanity checks pass as they are no longer needed.
 */
int btrfs_ref_tree_mod(struct btrfs_root *root, u64 bytenr, u64 num_bytes,
		       u64 parent, u64 ref_root, u64 owner, u64 offset,
		       int action)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct ref_entry *ref = NULL, *exist;
	struct ref_action *ra = NULL;
	struct block_entry *be = NULL;
	struct root_entry *re = NULL;
	int ret = 0;
	bool metadata = owner < BTRFS_FIRST_FREE_OBJECTID;

	if (!btrfs_test_opt(root->fs_info, REF_VERIFY))
		return 0;

	ref = kzalloc(sizeof(struct ref_entry), GFP_NOFS);
	ra = kmalloc(sizeof(struct ref_action), GFP_NOFS);
	if (!ra || !ref) {
		kfree(ref);
		kfree(ra);
		ret = -ENOMEM;
		goto out;
	}

	if (parent) {
		ref->parent = parent;
	} else {
		ref->root_objectid = ref_root;
		ref->owner = owner;
		ref->offset = offset;
	}
	ref->num_refs = (action == BTRFS_DROP_DELAYED_REF) ? -1 : 1;

	memcpy(&ra->ref, ref, sizeof(struct ref_entry));
	/*
	 * Save the extra info from the delayed ref in the ref action to make it
	 * easier to figure out what is happening.  The real ref's we add to the
	 * ref tree need to reflect what we save on disk so it matches any
	 * on-disk refs we pre-loaded.
	 */
	ra->ref.owner = owner;
	ra->ref.offset = offset;
	ra->ref.root_objectid = ref_root;
	__save_stack_trace(ra);

	INIT_LIST_HEAD(&ra->list);
	ra->action = action;
	ra->root = root->objectid;

	/*
	 * This is an allocation, preallocate the block_entry in case we haven't
	 * used it before.
	 */
	ret = -EINVAL;
	if (action == BTRFS_ADD_DELAYED_EXTENT) {
		/*
		 * For subvol_create we'll just pass in whatever the parent root
		 * is and the new root objectid, so let's not treat the passed
		 * in root as if it really has a ref for this bytenr.
		 */
		be = add_block_entry(root->fs_info, bytenr, num_bytes, ref_root);
		if (IS_ERR(be)) {
			kfree(ra);
			ret = PTR_ERR(be);
			goto out;
		}
		be->num_refs++;
		if (metadata)
			be->metadata = 1;

		if (be->num_refs != 1) {
			btrfs_err(fs_info,
			"re-allocated a block that still has references to it!");
			dump_block_entry(fs_info, be);
			dump_ref_action(fs_info, ra);
			goto out_unlock;
		}

		while (!list_empty(&be->actions)) {
			struct ref_action *tmp;

			tmp = list_first_entry(&be->actions, struct ref_action,
					       list);
			list_del(&tmp->list);
			kfree(tmp);
		}
	} else {
		struct root_entry *tmp;

		if (!parent) {
			re = kmalloc(sizeof(struct root_entry), GFP_NOFS);
			if (!re) {
				kfree(ref);
				kfree(ra);
				ret = -ENOMEM;
				goto out;
			}
			/*
			 * This is the root that is modifying us, so it's the
			 * one we want to lookup below when we modify the
			 * re->num_refs.
			 */
			ref_root = root->objectid;
			re->root_objectid = root->objectid;
			re->num_refs = 0;
		}

		spin_lock(&root->fs_info->ref_verify_lock);
		be = lookup_block_entry(&root->fs_info->block_tree, bytenr);
		if (!be) {
			btrfs_err(fs_info,
"trying to do action %d to bytenr %llu num_bytes %llu but there is no existing entry!",
				  action, (unsigned long long)bytenr,
				  (unsigned long long)num_bytes);
			dump_ref_action(fs_info, ra);
			kfree(ref);
			kfree(ra);
			goto out_unlock;
		}

		if (!parent) {
			tmp = insert_root_entry(&be->roots, re);
			if (tmp) {
				kfree(re);
				re = tmp;
			}
		}
	}

	exist = insert_ref_entry(&be->refs, ref);
	if (exist) {
		if (action == BTRFS_DROP_DELAYED_REF) {
			if (exist->num_refs == 0) {
				btrfs_err(fs_info,
"dropping a ref for a existing root that doesn't have a ref on the block");
				dump_block_entry(fs_info, be);
				dump_ref_action(fs_info, ra);
				kfree(ra);
				goto out_unlock;
			}
			exist->num_refs--;
			if (exist->num_refs == 0) {
				rb_erase(&exist->node, &be->refs);
				kfree(exist);
			}
		} else if (!be->metadata) {
			exist->num_refs++;
		} else {
			btrfs_err(fs_info,
"attempting to add another ref for an existing ref on a tree block");
			dump_block_entry(fs_info, be);
			dump_ref_action(fs_info, ra);
			kfree(ra);
			goto out_unlock;
		}
		kfree(ref);
	} else {
		if (action == BTRFS_DROP_DELAYED_REF) {
			btrfs_err(fs_info,
"dropping a ref for a root that doesn't have a ref on the block");
			dump_block_entry(fs_info, be);
			dump_ref_action(fs_info, ra);
			kfree(ra);
			goto out_unlock;
		}
	}

	if (!parent && !re) {
		re = lookup_root_entry(&be->roots, ref_root);
		if (!re) {
			/*
			 * This shouldn't happen because we will add our re
			 * above when we lookup the be with !parent, but just in
			 * case catch this case so we don't panic because I
			 * didn't thik of some other corner case.
			 */
			btrfs_err(fs_info, "failed to find root %llu for %llu",
				  root->objectid, be->bytenr);
			dump_block_entry(fs_info, be);
			dump_ref_action(fs_info, ra);
			kfree(ra);
			goto out_unlock;
		}
	}
	if (action == BTRFS_DROP_DELAYED_REF) {
		if (re)
			re->num_refs--;
		be->num_refs--;
	} else if (action == BTRFS_ADD_DELAYED_REF) {
		be->num_refs++;
		if (re)
			re->num_refs++;
	}
	list_add_tail(&ra->list, &be->actions);
	ret = 0;
out_unlock:
	spin_unlock(&root->fs_info->ref_verify_lock);
out:
	if (ret)
		btrfs_clear_opt(fs_info->mount_opt, REF_VERIFY);
	return ret;
}

/* Free up the ref cache */
void btrfs_free_ref_cache(struct btrfs_fs_info *fs_info)
{
	struct block_entry *be;
	struct rb_node *n;

	if (!btrfs_test_opt(fs_info, REF_VERIFY))
		return;

	spin_lock(&fs_info->ref_verify_lock);
	while ((n = rb_first(&fs_info->block_tree))) {
		be = rb_entry(n, struct block_entry, node);
		rb_erase(&be->node, &fs_info->block_tree);
		free_block_entry(be);
		cond_resched_lock(&fs_info->ref_verify_lock);
	}
	spin_unlock(&fs_info->ref_verify_lock);
}

void btrfs_free_ref_tree_range(struct btrfs_fs_info *fs_info, u64 start,
			       u64 len)
{
	struct block_entry *be = NULL, *entry;
	struct rb_node *n;

	if (!btrfs_test_opt(fs_info, REF_VERIFY))
		return;

	spin_lock(&fs_info->ref_verify_lock);
	n = fs_info->block_tree.rb_node;
	while (n) {
		entry = rb_entry(n, struct block_entry, node);
		if (entry->bytenr < start) {
			n = n->rb_right;
		} else if (entry->bytenr > start) {
			n = n->rb_left;
		} else {
			be = entry;
			break;
		}
		/* We want to get as close to start as possible */
		if (be == NULL ||
		    (entry->bytenr < start && be->bytenr > start) ||
		    (entry->bytenr < start && entry->bytenr > be->bytenr))
			be = entry;
	}

	/*
	 * Could have an empty block group, maybe have something to check for
	 * this case to verify we were actually empty?
	 */
	if (!be) {
		spin_unlock(&fs_info->ref_verify_lock);
		return;
	}

	n = &be->node;
	while (n) {
		be = rb_entry(n, struct block_entry, node);
		n = rb_next(n);
		if (be->bytenr < start && be->bytenr + be->len > start) {
			btrfs_err(fs_info,
				"block entry overlaps a block group [%llu,%llu]!",
				start, len);
			dump_block_entry(fs_info, be);
			continue;
		}
		if (be->bytenr < start)
			continue;
		if (be->bytenr >= start + len)
			break;
		if (be->bytenr + be->len > start + len) {
			btrfs_err(fs_info,
				"block entry overlaps a block group [%llu,%llu]!",
				start, len);
			dump_block_entry(fs_info, be);
		}
		rb_erase(&be->node, &fs_info->block_tree);
		free_block_entry(be);
	}
	spin_unlock(&fs_info->ref_verify_lock);
}

/* Walk down all roots and build the ref tree, meant to be called at mount */
int btrfs_build_ref_tree(struct btrfs_fs_info *fs_info)
{
	struct btrfs_path *path;
	struct btrfs_root *root;
	struct extent_buffer *eb;
	u64 bytenr = 0, num_bytes = 0;
	int ret, level;

	if (!btrfs_test_opt(fs_info, REF_VERIFY))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	eb = btrfs_read_lock_root_node(fs_info->extent_root);
	btrfs_set_lock_blocking_rw(eb, BTRFS_READ_LOCK);
	level = btrfs_header_level(eb);
	path->nodes[level] = eb;
	path->slots[level] = 0;
	path->locks[level] = BTRFS_READ_LOCK_BLOCKING;

	while (1) {
		/*
		 * We have to keep track of the bytenr/num_bytes we last hit
		 * because we could have run out of space for an inline ref, and
		 * would have had to added a ref key item which may appear on a
		 * different leaf from the original extent item.
		 */
		ret = walk_down_tree(fs_info->extent_root, path, level,
				     &bytenr, &num_bytes);
		if (ret)
			break;
		ret = walk_up_tree(root, path, &level);
		if (ret < 0)
			break;
		if (ret > 0) {
			ret = 0;
			break;
		}
	}
	if (ret) {
		btrfs_clear_opt(fs_info->mount_opt, REF_VERIFY);
		btrfs_free_ref_cache(fs_info);
	}
	btrfs_free_path(path);
	return ret;
}
