// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/brec.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 * Handle individual btree records
 */

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"

static struct hfs_bnode *hfs_bnode_split(struct hfs_find_data *fd);
static int hfs_brec_update_parent(struct hfs_find_data *fd);
static int hfs_btree_inc_height(struct hfs_btree *);

/* Get the length and offset of the given record in the given node */
u16 hfs_brec_lenoff(struct hfs_bnode *node, u16 rec, u16 *off)
{
	__be16 retval[2];
	u16 dataoff;

	dataoff = node->tree->node_size - (rec + 2) * 2;
	hfs_bnode_read(node, retval, dataoff, 4);
	*off = be16_to_cpu(retval[1]);
	return be16_to_cpu(retval[0]) - *off;
}

/* Get the length of the key from a keyed record */
u16 hfs_brec_keylen(struct hfs_bnode *node, u16 rec)
{
	u16 retval, recoff;

	if (node->type != HFS_NODE_INDEX && node->type != HFS_NODE_LEAF)
		return 0;

	if ((node->type == HFS_NODE_INDEX) &&
	   !(node->tree->attributes & HFS_TREE_VARIDXKEYS) &&
	   (node->tree->cnid != HFSPLUS_ATTR_CNID)) {
		retval = node->tree->max_key_len + 2;
	} else {
		recoff = hfs_bnode_read_u16(node,
			node->tree->node_size - (rec + 1) * 2);
		if (!recoff)
			return 0;
		if (recoff > node->tree->node_size - 2) {
			pr_err("recoff %d too large\n", recoff);
			return 0;
		}

		retval = hfs_bnode_read_u16(node, recoff) + 2;
		if (retval > node->tree->max_key_len + 2) {
			pr_err("keylen %d too large\n",
				retval);
			retval = 0;
		}
	}
	return retval;
}

int hfs_brec_insert(struct hfs_find_data *fd, void *entry, int entry_len)
{
	struct hfs_btree *tree;
	struct hfs_bnode *node, *new_node;
	int size, key_len, rec;
	int data_off, end_off;
	int idx_rec_off, data_rec_off, end_rec_off;
	__be32 cnid;

	tree = fd->tree;
	if (!fd->bnode) {
		if (!tree->root)
			hfs_btree_inc_height(tree);
		node = hfs_bnode_find(tree, tree->leaf_head);
		if (IS_ERR(node))
			return PTR_ERR(node);
		fd->bnode = node;
		fd->record = -1;
	}
	new_node = NULL;
	key_len = be16_to_cpu(fd->search_key->key_len) + 2;
again:
	/* new record idx and complete record size */
	rec = fd->record + 1;
	size = key_len + entry_len;

	node = fd->bnode;
	hfs_bnode_dump(node);
	/* get last offset */
	end_rec_off = tree->node_size - (node->num_recs + 1) * 2;
	end_off = hfs_bnode_read_u16(node, end_rec_off);
	end_rec_off -= 2;
	hfs_dbg("rec %d, size %d, end_off %d, end_rec_off %d\n",
		rec, size, end_off, end_rec_off);
	if (size > end_rec_off - end_off) {
		if (new_node)
			panic("not enough room!\n");
		new_node = hfs_bnode_split(fd);
		if (IS_ERR(new_node))
			return PTR_ERR(new_node);
		goto again;
	}
	if (node->type == HFS_NODE_LEAF) {
		tree->leaf_count++;
		mark_inode_dirty(tree->inode);
	}
	node->num_recs++;
	/* write new last offset */
	hfs_bnode_write_u16(node,
		offsetof(struct hfs_bnode_desc, num_recs),
		node->num_recs);
	hfs_bnode_write_u16(node, end_rec_off, end_off + size);
	data_off = end_off;
	data_rec_off = end_rec_off + 2;
	idx_rec_off = tree->node_size - (rec + 1) * 2;
	if (idx_rec_off == data_rec_off)
		goto skip;
	/* move all following entries */
	do {
		data_off = hfs_bnode_read_u16(node, data_rec_off + 2);
		hfs_bnode_write_u16(node, data_rec_off, data_off + size);
		data_rec_off += 2;
	} while (data_rec_off < idx_rec_off);

	/* move data away */
	hfs_bnode_move(node, data_off + size, data_off,
		       end_off - data_off);

skip:
	hfs_bnode_write(node, fd->search_key, data_off, key_len);
	hfs_bnode_write(node, entry, data_off + key_len, entry_len);
	hfs_bnode_dump(node);

	/*
	 * update parent key if we inserted a key
	 * at the start of the node and it is not the new node
	 */
	if (!rec && new_node != node) {
		hfs_bnode_read_key(node, fd->search_key, data_off + size);
		hfs_brec_update_parent(fd);
	}

	if (new_node) {
		hfs_bnode_put(fd->bnode);
		if (!new_node->parent) {
			hfs_btree_inc_height(tree);
			new_node->parent = tree->root;
		}
		fd->bnode = hfs_bnode_find(tree, new_node->parent);

		/* create index data entry */
		cnid = cpu_to_be32(new_node->this);
		entry = &cnid;
		entry_len = sizeof(cnid);

		/* get index key */
		hfs_bnode_read_key(new_node, fd->search_key, 14);
		__hfs_brec_find(fd->bnode, fd, hfs_find_rec_by_key);

		hfs_bnode_put(new_node);
		new_node = NULL;

		if ((tree->attributes & HFS_TREE_VARIDXKEYS) ||
				(tree->cnid == HFSPLUS_ATTR_CNID))
			key_len = be16_to_cpu(fd->search_key->key_len) + 2;
		else {
			fd->search_key->key_len =
				cpu_to_be16(tree->max_key_len);
			key_len = tree->max_key_len + 2;
		}
		goto again;
	}

	return 0;
}

int hfs_brec_remove(struct hfs_find_data *fd)
{
	struct hfs_btree *tree;
	struct hfs_bnode *node, *parent;
	int end_off, rec_off, data_off, size;

	tree = fd->tree;
	node = fd->bnode;
again:
	rec_off = tree->node_size - (fd->record + 2) * 2;
	end_off = tree->node_size - (node->num_recs + 1) * 2;

	if (node->type == HFS_NODE_LEAF) {
		tree->leaf_count--;
		mark_inode_dirty(tree->inode);
	}
	hfs_bnode_dump(node);
	hfs_dbg("rec %d, len %d\n",
		fd->record, fd->keylength + fd->entrylength);
	if (!--node->num_recs) {
		hfs_bnode_unlink(node);
		if (!node->parent)
			return 0;
		parent = hfs_bnode_find(tree, node->parent);
		if (IS_ERR(parent))
			return PTR_ERR(parent);
		hfs_bnode_put(node);
		node = fd->bnode = parent;

		__hfs_brec_find(node, fd, hfs_find_rec_by_key);
		goto again;
	}
	hfs_bnode_write_u16(node,
		offsetof(struct hfs_bnode_desc, num_recs),
		node->num_recs);

	if (rec_off == end_off)
		goto skip;
	size = fd->keylength + fd->entrylength;

	do {
		data_off = hfs_bnode_read_u16(node, rec_off);
		hfs_bnode_write_u16(node, rec_off + 2, data_off - size);
		rec_off -= 2;
	} while (rec_off >= end_off);

	/* fill hole */
	hfs_bnode_move(node, fd->keyoffset, fd->keyoffset + size,
		       data_off - fd->keyoffset - size);
skip:
	hfs_bnode_dump(node);
	if (!fd->record)
		hfs_brec_update_parent(fd);
	return 0;
}

static struct hfs_bnode *hfs_bnode_split(struct hfs_find_data *fd)
{
	struct hfs_btree *tree;
	struct hfs_bnode *node, *new_node, *next_node;
	struct hfs_bnode_desc node_desc;
	int num_recs, new_rec_off, new_off, old_rec_off;
	int data_start, data_end, size;

	tree = fd->tree;
	node = fd->bnode;
	new_node = hfs_bmap_alloc(tree);
	if (IS_ERR(new_node))
		return new_node;
	hfs_bnode_get(node);
	hfs_dbg("this %d - new %d - next %d\n",
		node->this, new_node->this, node->next);
	new_node->next = node->next;
	new_node->prev = node->this;
	new_node->parent = node->parent;
	new_node->type = node->type;
	new_node->height = node->height;

	if (node->next)
		next_node = hfs_bnode_find(tree, node->next);
	else
		next_node = NULL;

	if (IS_ERR(next_node)) {
		hfs_bnode_put(node);
		hfs_bnode_put(new_node);
		return next_node;
	}

	size = tree->node_size / 2 - node->num_recs * 2 - 14;
	old_rec_off = tree->node_size - 4;
	num_recs = 1;
	for (;;) {
		data_start = hfs_bnode_read_u16(node, old_rec_off);
		if (data_start > size)
			break;
		old_rec_off -= 2;
		if (++num_recs < node->num_recs)
			continue;
		/* panic? */
		hfs_bnode_put(node);
		hfs_bnode_put(new_node);
		if (next_node)
			hfs_bnode_put(next_node);
		return ERR_PTR(-ENOSPC);
	}

	if (fd->record + 1 < num_recs) {
		/* new record is in the lower half,
		 * so leave some more space there
		 */
		old_rec_off += 2;
		num_recs--;
		data_start = hfs_bnode_read_u16(node, old_rec_off);
	} else {
		hfs_bnode_put(node);
		hfs_bnode_get(new_node);
		fd->bnode = new_node;
		fd->record -= num_recs;
		fd->keyoffset -= data_start - 14;
		fd->entryoffset -= data_start - 14;
	}
	new_node->num_recs = node->num_recs - num_recs;
	node->num_recs = num_recs;

	new_rec_off = tree->node_size - 2;
	new_off = 14;
	size = data_start - new_off;
	num_recs = new_node->num_recs;
	data_end = data_start;
	while (num_recs) {
		hfs_bnode_write_u16(new_node, new_rec_off, new_off);
		old_rec_off -= 2;
		new_rec_off -= 2;
		data_end = hfs_bnode_read_u16(node, old_rec_off);
		new_off = data_end - size;
		num_recs--;
	}
	hfs_bnode_write_u16(new_node, new_rec_off, new_off);
	hfs_bnode_copy(new_node, 14, node, data_start, data_end - data_start);

	/* update new bnode header */
	node_desc.next = cpu_to_be32(new_node->next);
	node_desc.prev = cpu_to_be32(new_node->prev);
	node_desc.type = new_node->type;
	node_desc.height = new_node->height;
	node_desc.num_recs = cpu_to_be16(new_node->num_recs);
	node_desc.reserved = 0;
	hfs_bnode_write(new_node, &node_desc, 0, sizeof(node_desc));

	/* update previous bnode header */
	node->next = new_node->this;
	hfs_bnode_read(node, &node_desc, 0, sizeof(node_desc));
	node_desc.next = cpu_to_be32(node->next);
	node_desc.num_recs = cpu_to_be16(node->num_recs);
	hfs_bnode_write(node, &node_desc, 0, sizeof(node_desc));

	/* update next bnode header */
	if (next_node) {
		next_node->prev = new_node->this;
		hfs_bnode_read(next_node, &node_desc, 0, sizeof(node_desc));
		node_desc.prev = cpu_to_be32(next_node->prev);
		hfs_bnode_write(next_node, &node_desc, 0, sizeof(node_desc));
		hfs_bnode_put(next_node);
	} else if (node->this == tree->leaf_tail) {
		/* if there is no next node, this might be the new tail */
		tree->leaf_tail = new_node->this;
		mark_inode_dirty(tree->inode);
	}

	hfs_bnode_dump(node);
	hfs_bnode_dump(new_node);
	hfs_bnode_put(node);

	return new_node;
}

static int hfs_brec_update_parent(struct hfs_find_data *fd)
{
	struct hfs_btree *tree;
	struct hfs_bnode *node, *new_node, *parent;
	int newkeylen, diff;
	int rec, rec_off, end_rec_off;
	int start_off, end_off;

	tree = fd->tree;
	node = fd->bnode;
	new_node = NULL;
	if (!node->parent)
		return 0;

again:
	parent = hfs_bnode_find(tree, node->parent);
	if (IS_ERR(parent))
		return PTR_ERR(parent);
	__hfs_brec_find(parent, fd, hfs_find_rec_by_key);
	if (fd->record < 0)
		return -ENOENT;
	hfs_bnode_dump(parent);
	rec = fd->record;

	/* size difference between old and new key */
	if ((tree->attributes & HFS_TREE_VARIDXKEYS) ||
				(tree->cnid == HFSPLUS_ATTR_CNID))
		newkeylen = hfs_bnode_read_u16(node, 14) + 2;
	else
		fd->keylength = newkeylen = tree->max_key_len + 2;
	hfs_dbg("rec %d, keylength %d, newkeylen %d\n",
		rec, fd->keylength, newkeylen);

	rec_off = tree->node_size - (rec + 2) * 2;
	end_rec_off = tree->node_size - (parent->num_recs + 1) * 2;
	diff = newkeylen - fd->keylength;
	if (!diff)
		goto skip;
	if (diff > 0) {
		end_off = hfs_bnode_read_u16(parent, end_rec_off);
		if (end_rec_off - end_off < diff) {

			hfs_dbg("splitting index node\n");
			fd->bnode = parent;
			new_node = hfs_bnode_split(fd);
			if (IS_ERR(new_node))
				return PTR_ERR(new_node);
			parent = fd->bnode;
			rec = fd->record;
			rec_off = tree->node_size - (rec + 2) * 2;
			end_rec_off = tree->node_size -
				(parent->num_recs + 1) * 2;
		}
	}

	end_off = start_off = hfs_bnode_read_u16(parent, rec_off);
	hfs_bnode_write_u16(parent, rec_off, start_off + diff);
	start_off -= 4;	/* move previous cnid too */

	while (rec_off > end_rec_off) {
		rec_off -= 2;
		end_off = hfs_bnode_read_u16(parent, rec_off);
		hfs_bnode_write_u16(parent, rec_off, end_off + diff);
	}
	hfs_bnode_move(parent, start_off + diff, start_off,
		       end_off - start_off);
skip:
	hfs_bnode_copy(parent, fd->keyoffset, node, 14, newkeylen);
	hfs_bnode_dump(parent);

	hfs_bnode_put(node);
	node = parent;

	if (new_node) {
		__be32 cnid;

		if (!new_node->parent) {
			hfs_btree_inc_height(tree);
			new_node->parent = tree->root;
		}
		fd->bnode = hfs_bnode_find(tree, new_node->parent);
		/* create index key and entry */
		hfs_bnode_read_key(new_node, fd->search_key, 14);
		cnid = cpu_to_be32(new_node->this);

		__hfs_brec_find(fd->bnode, fd, hfs_find_rec_by_key);
		hfs_brec_insert(fd, &cnid, sizeof(cnid));
		hfs_bnode_put(fd->bnode);
		hfs_bnode_put(new_node);

		if (!rec) {
			if (new_node == node)
				goto out;
			/* restore search_key */
			hfs_bnode_read_key(node, fd->search_key, 14);
		}
		new_node = NULL;
	}

	if (!rec && node->parent)
		goto again;
out:
	fd->bnode = node;
	return 0;
}

static int hfs_btree_inc_height(struct hfs_btree *tree)
{
	struct hfs_bnode *node, *new_node;
	struct hfs_bnode_desc node_desc;
	int key_size, rec;
	__be32 cnid;

	node = NULL;
	if (tree->root) {
		node = hfs_bnode_find(tree, tree->root);
		if (IS_ERR(node))
			return PTR_ERR(node);
	}
	new_node = hfs_bmap_alloc(tree);
	if (IS_ERR(new_node)) {
		hfs_bnode_put(node);
		return PTR_ERR(new_node);
	}

	tree->root = new_node->this;
	if (!tree->depth) {
		tree->leaf_head = tree->leaf_tail = new_node->this;
		new_node->type = HFS_NODE_LEAF;
		new_node->num_recs = 0;
	} else {
		new_node->type = HFS_NODE_INDEX;
		new_node->num_recs = 1;
	}
	new_node->parent = 0;
	new_node->next = 0;
	new_node->prev = 0;
	new_node->height = ++tree->depth;

	node_desc.next = cpu_to_be32(new_node->next);
	node_desc.prev = cpu_to_be32(new_node->prev);
	node_desc.type = new_node->type;
	node_desc.height = new_node->height;
	node_desc.num_recs = cpu_to_be16(new_node->num_recs);
	node_desc.reserved = 0;
	hfs_bnode_write(new_node, &node_desc, 0, sizeof(node_desc));

	rec = tree->node_size - 2;
	hfs_bnode_write_u16(new_node, rec, 14);

	if (node) {
		/* insert old root idx into new root */
		node->parent = tree->root;
		if (node->type == HFS_NODE_LEAF ||
				tree->attributes & HFS_TREE_VARIDXKEYS ||
				tree->cnid == HFSPLUS_ATTR_CNID)
			key_size = hfs_bnode_read_u16(node, 14) + 2;
		else
			key_size = tree->max_key_len + 2;
		hfs_bnode_copy(new_node, 14, node, 14, key_size);

		if (!(tree->attributes & HFS_TREE_VARIDXKEYS) &&
				(tree->cnid != HFSPLUS_ATTR_CNID)) {
			key_size = tree->max_key_len + 2;
			hfs_bnode_write_u16(new_node, 14, tree->max_key_len);
		}
		cnid = cpu_to_be32(node->this);
		hfs_bnode_write(new_node, &cnid, 14 + key_size, 4);

		rec -= 2;
		hfs_bnode_write_u16(new_node, rec, 14 + key_size + 4);

		hfs_bnode_put(node);
	}
	hfs_bnode_put(new_node);
	mark_inode_dirty(tree->inode);

	return 0;
}
