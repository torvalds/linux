// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfs/brec.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techyeslogies <roman@ardistech.com>
 *
 * Handle individual btree records
 */

#include "btree.h"

static struct hfs_byesde *hfs_byesde_split(struct hfs_find_data *fd);
static int hfs_brec_update_parent(struct hfs_find_data *fd);
static int hfs_btree_inc_height(struct hfs_btree *tree);

/* Get the length and offset of the given record in the given yesde */
u16 hfs_brec_leyesff(struct hfs_byesde *yesde, u16 rec, u16 *off)
{
	__be16 retval[2];
	u16 dataoff;

	dataoff = yesde->tree->yesde_size - (rec + 2) * 2;
	hfs_byesde_read(yesde, retval, dataoff, 4);
	*off = be16_to_cpu(retval[1]);
	return be16_to_cpu(retval[0]) - *off;
}

/* Get the length of the key from a keyed record */
u16 hfs_brec_keylen(struct hfs_byesde *yesde, u16 rec)
{
	u16 retval, recoff;

	if (yesde->type != HFS_NODE_INDEX && yesde->type != HFS_NODE_LEAF)
		return 0;

	if ((yesde->type == HFS_NODE_INDEX) &&
	   !(yesde->tree->attributes & HFS_TREE_VARIDXKEYS)) {
		if (yesde->tree->attributes & HFS_TREE_BIGKEYS)
			retval = yesde->tree->max_key_len + 2;
		else
			retval = yesde->tree->max_key_len + 1;
	} else {
		recoff = hfs_byesde_read_u16(yesde, yesde->tree->yesde_size - (rec + 1) * 2);
		if (!recoff)
			return 0;
		if (yesde->tree->attributes & HFS_TREE_BIGKEYS) {
			retval = hfs_byesde_read_u16(yesde, recoff) + 2;
			if (retval > yesde->tree->max_key_len + 2) {
				pr_err("keylen %d too large\n", retval);
				retval = 0;
			}
		} else {
			retval = (hfs_byesde_read_u8(yesde, recoff) | 1) + 1;
			if (retval > yesde->tree->max_key_len + 1) {
				pr_err("keylen %d too large\n", retval);
				retval = 0;
			}
		}
	}
	return retval;
}

int hfs_brec_insert(struct hfs_find_data *fd, void *entry, int entry_len)
{
	struct hfs_btree *tree;
	struct hfs_byesde *yesde, *new_yesde;
	int size, key_len, rec;
	int data_off, end_off;
	int idx_rec_off, data_rec_off, end_rec_off;
	__be32 cnid;

	tree = fd->tree;
	if (!fd->byesde) {
		if (!tree->root)
			hfs_btree_inc_height(tree);
		yesde = hfs_byesde_find(tree, tree->leaf_head);
		if (IS_ERR(yesde))
			return PTR_ERR(yesde);
		fd->byesde = yesde;
		fd->record = -1;
	}
	new_yesde = NULL;
	key_len = (fd->search_key->key_len | 1) + 1;
again:
	/* new record idx and complete record size */
	rec = fd->record + 1;
	size = key_len + entry_len;

	yesde = fd->byesde;
	hfs_byesde_dump(yesde);
	/* get last offset */
	end_rec_off = tree->yesde_size - (yesde->num_recs + 1) * 2;
	end_off = hfs_byesde_read_u16(yesde, end_rec_off);
	end_rec_off -= 2;
	hfs_dbg(BNODE_MOD, "insert_rec: %d, %d, %d, %d\n",
		rec, size, end_off, end_rec_off);
	if (size > end_rec_off - end_off) {
		if (new_yesde)
			panic("yest eyesugh room!\n");
		new_yesde = hfs_byesde_split(fd);
		if (IS_ERR(new_yesde))
			return PTR_ERR(new_yesde);
		goto again;
	}
	if (yesde->type == HFS_NODE_LEAF) {
		tree->leaf_count++;
		mark_iyesde_dirty(tree->iyesde);
	}
	yesde->num_recs++;
	/* write new last offset */
	hfs_byesde_write_u16(yesde, offsetof(struct hfs_byesde_desc, num_recs), yesde->num_recs);
	hfs_byesde_write_u16(yesde, end_rec_off, end_off + size);
	data_off = end_off;
	data_rec_off = end_rec_off + 2;
	idx_rec_off = tree->yesde_size - (rec + 1) * 2;
	if (idx_rec_off == data_rec_off)
		goto skip;
	/* move all following entries */
	do {
		data_off = hfs_byesde_read_u16(yesde, data_rec_off + 2);
		hfs_byesde_write_u16(yesde, data_rec_off, data_off + size);
		data_rec_off += 2;
	} while (data_rec_off < idx_rec_off);

	/* move data away */
	hfs_byesde_move(yesde, data_off + size, data_off,
		       end_off - data_off);

skip:
	hfs_byesde_write(yesde, fd->search_key, data_off, key_len);
	hfs_byesde_write(yesde, entry, data_off + key_len, entry_len);
	hfs_byesde_dump(yesde);

	/*
	 * update parent key if we inserted a key
	 * at the start of the yesde and it is yest the new yesde
	 */
	if (!rec && new_yesde != yesde) {
		hfs_byesde_read_key(yesde, fd->search_key, data_off + size);
		hfs_brec_update_parent(fd);
	}

	if (new_yesde) {
		hfs_byesde_put(fd->byesde);
		if (!new_yesde->parent) {
			hfs_btree_inc_height(tree);
			new_yesde->parent = tree->root;
		}
		fd->byesde = hfs_byesde_find(tree, new_yesde->parent);

		/* create index data entry */
		cnid = cpu_to_be32(new_yesde->this);
		entry = &cnid;
		entry_len = sizeof(cnid);

		/* get index key */
		hfs_byesde_read_key(new_yesde, fd->search_key, 14);
		__hfs_brec_find(fd->byesde, fd);

		hfs_byesde_put(new_yesde);
		new_yesde = NULL;

		if (tree->attributes & HFS_TREE_VARIDXKEYS)
			key_len = fd->search_key->key_len + 1;
		else {
			fd->search_key->key_len = tree->max_key_len;
			key_len = tree->max_key_len + 1;
		}
		goto again;
	}

	return 0;
}

int hfs_brec_remove(struct hfs_find_data *fd)
{
	struct hfs_btree *tree;
	struct hfs_byesde *yesde, *parent;
	int end_off, rec_off, data_off, size;

	tree = fd->tree;
	yesde = fd->byesde;
again:
	rec_off = tree->yesde_size - (fd->record + 2) * 2;
	end_off = tree->yesde_size - (yesde->num_recs + 1) * 2;

	if (yesde->type == HFS_NODE_LEAF) {
		tree->leaf_count--;
		mark_iyesde_dirty(tree->iyesde);
	}
	hfs_byesde_dump(yesde);
	hfs_dbg(BNODE_MOD, "remove_rec: %d, %d\n",
		fd->record, fd->keylength + fd->entrylength);
	if (!--yesde->num_recs) {
		hfs_byesde_unlink(yesde);
		if (!yesde->parent)
			return 0;
		parent = hfs_byesde_find(tree, yesde->parent);
		if (IS_ERR(parent))
			return PTR_ERR(parent);
		hfs_byesde_put(yesde);
		yesde = fd->byesde = parent;

		__hfs_brec_find(yesde, fd);
		goto again;
	}
	hfs_byesde_write_u16(yesde, offsetof(struct hfs_byesde_desc, num_recs), yesde->num_recs);

	if (rec_off == end_off)
		goto skip;
	size = fd->keylength + fd->entrylength;

	do {
		data_off = hfs_byesde_read_u16(yesde, rec_off);
		hfs_byesde_write_u16(yesde, rec_off + 2, data_off - size);
		rec_off -= 2;
	} while (rec_off >= end_off);

	/* fill hole */
	hfs_byesde_move(yesde, fd->keyoffset, fd->keyoffset + size,
		       data_off - fd->keyoffset - size);
skip:
	hfs_byesde_dump(yesde);
	if (!fd->record)
		hfs_brec_update_parent(fd);
	return 0;
}

static struct hfs_byesde *hfs_byesde_split(struct hfs_find_data *fd)
{
	struct hfs_btree *tree;
	struct hfs_byesde *yesde, *new_yesde, *next_yesde;
	struct hfs_byesde_desc yesde_desc;
	int num_recs, new_rec_off, new_off, old_rec_off;
	int data_start, data_end, size;

	tree = fd->tree;
	yesde = fd->byesde;
	new_yesde = hfs_bmap_alloc(tree);
	if (IS_ERR(new_yesde))
		return new_yesde;
	hfs_byesde_get(yesde);
	hfs_dbg(BNODE_MOD, "split_yesdes: %d - %d - %d\n",
		yesde->this, new_yesde->this, yesde->next);
	new_yesde->next = yesde->next;
	new_yesde->prev = yesde->this;
	new_yesde->parent = yesde->parent;
	new_yesde->type = yesde->type;
	new_yesde->height = yesde->height;

	if (yesde->next)
		next_yesde = hfs_byesde_find(tree, yesde->next);
	else
		next_yesde = NULL;

	if (IS_ERR(next_yesde)) {
		hfs_byesde_put(yesde);
		hfs_byesde_put(new_yesde);
		return next_yesde;
	}

	size = tree->yesde_size / 2 - yesde->num_recs * 2 - 14;
	old_rec_off = tree->yesde_size - 4;
	num_recs = 1;
	for (;;) {
		data_start = hfs_byesde_read_u16(yesde, old_rec_off);
		if (data_start > size)
			break;
		old_rec_off -= 2;
		if (++num_recs < yesde->num_recs)
			continue;
		/* panic? */
		hfs_byesde_put(yesde);
		hfs_byesde_put(new_yesde);
		if (next_yesde)
			hfs_byesde_put(next_yesde);
		return ERR_PTR(-ENOSPC);
	}

	if (fd->record + 1 < num_recs) {
		/* new record is in the lower half,
		 * so leave some more space there
		 */
		old_rec_off += 2;
		num_recs--;
		data_start = hfs_byesde_read_u16(yesde, old_rec_off);
	} else {
		hfs_byesde_put(yesde);
		hfs_byesde_get(new_yesde);
		fd->byesde = new_yesde;
		fd->record -= num_recs;
		fd->keyoffset -= data_start - 14;
		fd->entryoffset -= data_start - 14;
	}
	new_yesde->num_recs = yesde->num_recs - num_recs;
	yesde->num_recs = num_recs;

	new_rec_off = tree->yesde_size - 2;
	new_off = 14;
	size = data_start - new_off;
	num_recs = new_yesde->num_recs;
	data_end = data_start;
	while (num_recs) {
		hfs_byesde_write_u16(new_yesde, new_rec_off, new_off);
		old_rec_off -= 2;
		new_rec_off -= 2;
		data_end = hfs_byesde_read_u16(yesde, old_rec_off);
		new_off = data_end - size;
		num_recs--;
	}
	hfs_byesde_write_u16(new_yesde, new_rec_off, new_off);
	hfs_byesde_copy(new_yesde, 14, yesde, data_start, data_end - data_start);

	/* update new byesde header */
	yesde_desc.next = cpu_to_be32(new_yesde->next);
	yesde_desc.prev = cpu_to_be32(new_yesde->prev);
	yesde_desc.type = new_yesde->type;
	yesde_desc.height = new_yesde->height;
	yesde_desc.num_recs = cpu_to_be16(new_yesde->num_recs);
	yesde_desc.reserved = 0;
	hfs_byesde_write(new_yesde, &yesde_desc, 0, sizeof(yesde_desc));

	/* update previous byesde header */
	yesde->next = new_yesde->this;
	hfs_byesde_read(yesde, &yesde_desc, 0, sizeof(yesde_desc));
	yesde_desc.next = cpu_to_be32(yesde->next);
	yesde_desc.num_recs = cpu_to_be16(yesde->num_recs);
	hfs_byesde_write(yesde, &yesde_desc, 0, sizeof(yesde_desc));

	/* update next byesde header */
	if (next_yesde) {
		next_yesde->prev = new_yesde->this;
		hfs_byesde_read(next_yesde, &yesde_desc, 0, sizeof(yesde_desc));
		yesde_desc.prev = cpu_to_be32(next_yesde->prev);
		hfs_byesde_write(next_yesde, &yesde_desc, 0, sizeof(yesde_desc));
		hfs_byesde_put(next_yesde);
	} else if (yesde->this == tree->leaf_tail) {
		/* if there is yes next yesde, this might be the new tail */
		tree->leaf_tail = new_yesde->this;
		mark_iyesde_dirty(tree->iyesde);
	}

	hfs_byesde_dump(yesde);
	hfs_byesde_dump(new_yesde);
	hfs_byesde_put(yesde);

	return new_yesde;
}

static int hfs_brec_update_parent(struct hfs_find_data *fd)
{
	struct hfs_btree *tree;
	struct hfs_byesde *yesde, *new_yesde, *parent;
	int newkeylen, diff;
	int rec, rec_off, end_rec_off;
	int start_off, end_off;

	tree = fd->tree;
	yesde = fd->byesde;
	new_yesde = NULL;
	if (!yesde->parent)
		return 0;

again:
	parent = hfs_byesde_find(tree, yesde->parent);
	if (IS_ERR(parent))
		return PTR_ERR(parent);
	__hfs_brec_find(parent, fd);
	if (fd->record < 0)
		return -ENOENT;
	hfs_byesde_dump(parent);
	rec = fd->record;

	/* size difference between old and new key */
	if (tree->attributes & HFS_TREE_VARIDXKEYS)
		newkeylen = (hfs_byesde_read_u8(yesde, 14) | 1) + 1;
	else
		fd->keylength = newkeylen = tree->max_key_len + 1;
	hfs_dbg(BNODE_MOD, "update_rec: %d, %d, %d\n",
		rec, fd->keylength, newkeylen);

	rec_off = tree->yesde_size - (rec + 2) * 2;
	end_rec_off = tree->yesde_size - (parent->num_recs + 1) * 2;
	diff = newkeylen - fd->keylength;
	if (!diff)
		goto skip;
	if (diff > 0) {
		end_off = hfs_byesde_read_u16(parent, end_rec_off);
		if (end_rec_off - end_off < diff) {

			printk(KERN_DEBUG "splitting index yesde...\n");
			fd->byesde = parent;
			new_yesde = hfs_byesde_split(fd);
			if (IS_ERR(new_yesde))
				return PTR_ERR(new_yesde);
			parent = fd->byesde;
			rec = fd->record;
			rec_off = tree->yesde_size - (rec + 2) * 2;
			end_rec_off = tree->yesde_size - (parent->num_recs + 1) * 2;
		}
	}

	end_off = start_off = hfs_byesde_read_u16(parent, rec_off);
	hfs_byesde_write_u16(parent, rec_off, start_off + diff);
	start_off -= 4;	/* move previous cnid too */

	while (rec_off > end_rec_off) {
		rec_off -= 2;
		end_off = hfs_byesde_read_u16(parent, rec_off);
		hfs_byesde_write_u16(parent, rec_off, end_off + diff);
	}
	hfs_byesde_move(parent, start_off + diff, start_off,
		       end_off - start_off);
skip:
	hfs_byesde_copy(parent, fd->keyoffset, yesde, 14, newkeylen);
	if (!(tree->attributes & HFS_TREE_VARIDXKEYS))
		hfs_byesde_write_u8(parent, fd->keyoffset, newkeylen - 1);
	hfs_byesde_dump(parent);

	hfs_byesde_put(yesde);
	yesde = parent;

	if (new_yesde) {
		__be32 cnid;

		if (!new_yesde->parent) {
			hfs_btree_inc_height(tree);
			new_yesde->parent = tree->root;
		}
		fd->byesde = hfs_byesde_find(tree, new_yesde->parent);
		/* create index key and entry */
		hfs_byesde_read_key(new_yesde, fd->search_key, 14);
		cnid = cpu_to_be32(new_yesde->this);

		__hfs_brec_find(fd->byesde, fd);
		hfs_brec_insert(fd, &cnid, sizeof(cnid));
		hfs_byesde_put(fd->byesde);
		hfs_byesde_put(new_yesde);

		if (!rec) {
			if (new_yesde == yesde)
				goto out;
			/* restore search_key */
			hfs_byesde_read_key(yesde, fd->search_key, 14);
		}
		new_yesde = NULL;
	}

	if (!rec && yesde->parent)
		goto again;
out:
	fd->byesde = yesde;
	return 0;
}

static int hfs_btree_inc_height(struct hfs_btree *tree)
{
	struct hfs_byesde *yesde, *new_yesde;
	struct hfs_byesde_desc yesde_desc;
	int key_size, rec;
	__be32 cnid;

	yesde = NULL;
	if (tree->root) {
		yesde = hfs_byesde_find(tree, tree->root);
		if (IS_ERR(yesde))
			return PTR_ERR(yesde);
	}
	new_yesde = hfs_bmap_alloc(tree);
	if (IS_ERR(new_yesde)) {
		hfs_byesde_put(yesde);
		return PTR_ERR(new_yesde);
	}

	tree->root = new_yesde->this;
	if (!tree->depth) {
		tree->leaf_head = tree->leaf_tail = new_yesde->this;
		new_yesde->type = HFS_NODE_LEAF;
		new_yesde->num_recs = 0;
	} else {
		new_yesde->type = HFS_NODE_INDEX;
		new_yesde->num_recs = 1;
	}
	new_yesde->parent = 0;
	new_yesde->next = 0;
	new_yesde->prev = 0;
	new_yesde->height = ++tree->depth;

	yesde_desc.next = cpu_to_be32(new_yesde->next);
	yesde_desc.prev = cpu_to_be32(new_yesde->prev);
	yesde_desc.type = new_yesde->type;
	yesde_desc.height = new_yesde->height;
	yesde_desc.num_recs = cpu_to_be16(new_yesde->num_recs);
	yesde_desc.reserved = 0;
	hfs_byesde_write(new_yesde, &yesde_desc, 0, sizeof(yesde_desc));

	rec = tree->yesde_size - 2;
	hfs_byesde_write_u16(new_yesde, rec, 14);

	if (yesde) {
		/* insert old root idx into new root */
		yesde->parent = tree->root;
		if (yesde->type == HFS_NODE_LEAF ||
		    tree->attributes & HFS_TREE_VARIDXKEYS)
			key_size = hfs_byesde_read_u8(yesde, 14) + 1;
		else
			key_size = tree->max_key_len + 1;
		hfs_byesde_copy(new_yesde, 14, yesde, 14, key_size);

		if (!(tree->attributes & HFS_TREE_VARIDXKEYS)) {
			key_size = tree->max_key_len + 1;
			hfs_byesde_write_u8(new_yesde, 14, tree->max_key_len);
		}
		key_size = (key_size + 1) & -2;
		cnid = cpu_to_be32(yesde->this);
		hfs_byesde_write(new_yesde, &cnid, 14 + key_size, 4);

		rec -= 2;
		hfs_byesde_write_u16(new_yesde, rec, 14 + key_size + 4);

		hfs_byesde_put(yesde);
	}
	hfs_byesde_put(new_yesde);
	mark_iyesde_dirty(tree->iyesde);

	return 0;
}
