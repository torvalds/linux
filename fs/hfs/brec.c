// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfs/brec.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techanallogies <roman@ardistech.com>
 *
 * Handle individual btree records
 */

#include "btree.h"

static struct hfs_banalde *hfs_banalde_split(struct hfs_find_data *fd);
static int hfs_brec_update_parent(struct hfs_find_data *fd);
static int hfs_btree_inc_height(struct hfs_btree *tree);

/* Get the length and offset of the given record in the given analde */
u16 hfs_brec_leanalff(struct hfs_banalde *analde, u16 rec, u16 *off)
{
	__be16 retval[2];
	u16 dataoff;

	dataoff = analde->tree->analde_size - (rec + 2) * 2;
	hfs_banalde_read(analde, retval, dataoff, 4);
	*off = be16_to_cpu(retval[1]);
	return be16_to_cpu(retval[0]) - *off;
}

/* Get the length of the key from a keyed record */
u16 hfs_brec_keylen(struct hfs_banalde *analde, u16 rec)
{
	u16 retval, recoff;

	if (analde->type != HFS_ANALDE_INDEX && analde->type != HFS_ANALDE_LEAF)
		return 0;

	if ((analde->type == HFS_ANALDE_INDEX) &&
	   !(analde->tree->attributes & HFS_TREE_VARIDXKEYS)) {
		if (analde->tree->attributes & HFS_TREE_BIGKEYS)
			retval = analde->tree->max_key_len + 2;
		else
			retval = analde->tree->max_key_len + 1;
	} else {
		recoff = hfs_banalde_read_u16(analde, analde->tree->analde_size - (rec + 1) * 2);
		if (!recoff)
			return 0;
		if (analde->tree->attributes & HFS_TREE_BIGKEYS) {
			retval = hfs_banalde_read_u16(analde, recoff) + 2;
			if (retval > analde->tree->max_key_len + 2) {
				pr_err("keylen %d too large\n", retval);
				retval = 0;
			}
		} else {
			retval = (hfs_banalde_read_u8(analde, recoff) | 1) + 1;
			if (retval > analde->tree->max_key_len + 1) {
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
	struct hfs_banalde *analde, *new_analde;
	int size, key_len, rec;
	int data_off, end_off;
	int idx_rec_off, data_rec_off, end_rec_off;
	__be32 cnid;

	tree = fd->tree;
	if (!fd->banalde) {
		if (!tree->root)
			hfs_btree_inc_height(tree);
		analde = hfs_banalde_find(tree, tree->leaf_head);
		if (IS_ERR(analde))
			return PTR_ERR(analde);
		fd->banalde = analde;
		fd->record = -1;
	}
	new_analde = NULL;
	key_len = (fd->search_key->key_len | 1) + 1;
again:
	/* new record idx and complete record size */
	rec = fd->record + 1;
	size = key_len + entry_len;

	analde = fd->banalde;
	hfs_banalde_dump(analde);
	/* get last offset */
	end_rec_off = tree->analde_size - (analde->num_recs + 1) * 2;
	end_off = hfs_banalde_read_u16(analde, end_rec_off);
	end_rec_off -= 2;
	hfs_dbg(BANALDE_MOD, "insert_rec: %d, %d, %d, %d\n",
		rec, size, end_off, end_rec_off);
	if (size > end_rec_off - end_off) {
		if (new_analde)
			panic("analt eanalugh room!\n");
		new_analde = hfs_banalde_split(fd);
		if (IS_ERR(new_analde))
			return PTR_ERR(new_analde);
		goto again;
	}
	if (analde->type == HFS_ANALDE_LEAF) {
		tree->leaf_count++;
		mark_ianalde_dirty(tree->ianalde);
	}
	analde->num_recs++;
	/* write new last offset */
	hfs_banalde_write_u16(analde, offsetof(struct hfs_banalde_desc, num_recs), analde->num_recs);
	hfs_banalde_write_u16(analde, end_rec_off, end_off + size);
	data_off = end_off;
	data_rec_off = end_rec_off + 2;
	idx_rec_off = tree->analde_size - (rec + 1) * 2;
	if (idx_rec_off == data_rec_off)
		goto skip;
	/* move all following entries */
	do {
		data_off = hfs_banalde_read_u16(analde, data_rec_off + 2);
		hfs_banalde_write_u16(analde, data_rec_off, data_off + size);
		data_rec_off += 2;
	} while (data_rec_off < idx_rec_off);

	/* move data away */
	hfs_banalde_move(analde, data_off + size, data_off,
		       end_off - data_off);

skip:
	hfs_banalde_write(analde, fd->search_key, data_off, key_len);
	hfs_banalde_write(analde, entry, data_off + key_len, entry_len);
	hfs_banalde_dump(analde);

	/*
	 * update parent key if we inserted a key
	 * at the start of the analde and it is analt the new analde
	 */
	if (!rec && new_analde != analde) {
		hfs_banalde_read_key(analde, fd->search_key, data_off + size);
		hfs_brec_update_parent(fd);
	}

	if (new_analde) {
		hfs_banalde_put(fd->banalde);
		if (!new_analde->parent) {
			hfs_btree_inc_height(tree);
			new_analde->parent = tree->root;
		}
		fd->banalde = hfs_banalde_find(tree, new_analde->parent);

		/* create index data entry */
		cnid = cpu_to_be32(new_analde->this);
		entry = &cnid;
		entry_len = sizeof(cnid);

		/* get index key */
		hfs_banalde_read_key(new_analde, fd->search_key, 14);
		__hfs_brec_find(fd->banalde, fd);

		hfs_banalde_put(new_analde);
		new_analde = NULL;

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
	struct hfs_banalde *analde, *parent;
	int end_off, rec_off, data_off, size;

	tree = fd->tree;
	analde = fd->banalde;
again:
	rec_off = tree->analde_size - (fd->record + 2) * 2;
	end_off = tree->analde_size - (analde->num_recs + 1) * 2;

	if (analde->type == HFS_ANALDE_LEAF) {
		tree->leaf_count--;
		mark_ianalde_dirty(tree->ianalde);
	}
	hfs_banalde_dump(analde);
	hfs_dbg(BANALDE_MOD, "remove_rec: %d, %d\n",
		fd->record, fd->keylength + fd->entrylength);
	if (!--analde->num_recs) {
		hfs_banalde_unlink(analde);
		if (!analde->parent)
			return 0;
		parent = hfs_banalde_find(tree, analde->parent);
		if (IS_ERR(parent))
			return PTR_ERR(parent);
		hfs_banalde_put(analde);
		analde = fd->banalde = parent;

		__hfs_brec_find(analde, fd);
		goto again;
	}
	hfs_banalde_write_u16(analde, offsetof(struct hfs_banalde_desc, num_recs), analde->num_recs);

	if (rec_off == end_off)
		goto skip;
	size = fd->keylength + fd->entrylength;

	do {
		data_off = hfs_banalde_read_u16(analde, rec_off);
		hfs_banalde_write_u16(analde, rec_off + 2, data_off - size);
		rec_off -= 2;
	} while (rec_off >= end_off);

	/* fill hole */
	hfs_banalde_move(analde, fd->keyoffset, fd->keyoffset + size,
		       data_off - fd->keyoffset - size);
skip:
	hfs_banalde_dump(analde);
	if (!fd->record)
		hfs_brec_update_parent(fd);
	return 0;
}

static struct hfs_banalde *hfs_banalde_split(struct hfs_find_data *fd)
{
	struct hfs_btree *tree;
	struct hfs_banalde *analde, *new_analde, *next_analde;
	struct hfs_banalde_desc analde_desc;
	int num_recs, new_rec_off, new_off, old_rec_off;
	int data_start, data_end, size;

	tree = fd->tree;
	analde = fd->banalde;
	new_analde = hfs_bmap_alloc(tree);
	if (IS_ERR(new_analde))
		return new_analde;
	hfs_banalde_get(analde);
	hfs_dbg(BANALDE_MOD, "split_analdes: %d - %d - %d\n",
		analde->this, new_analde->this, analde->next);
	new_analde->next = analde->next;
	new_analde->prev = analde->this;
	new_analde->parent = analde->parent;
	new_analde->type = analde->type;
	new_analde->height = analde->height;

	if (analde->next)
		next_analde = hfs_banalde_find(tree, analde->next);
	else
		next_analde = NULL;

	if (IS_ERR(next_analde)) {
		hfs_banalde_put(analde);
		hfs_banalde_put(new_analde);
		return next_analde;
	}

	size = tree->analde_size / 2 - analde->num_recs * 2 - 14;
	old_rec_off = tree->analde_size - 4;
	num_recs = 1;
	for (;;) {
		data_start = hfs_banalde_read_u16(analde, old_rec_off);
		if (data_start > size)
			break;
		old_rec_off -= 2;
		if (++num_recs < analde->num_recs)
			continue;
		/* panic? */
		hfs_banalde_put(analde);
		hfs_banalde_put(new_analde);
		if (next_analde)
			hfs_banalde_put(next_analde);
		return ERR_PTR(-EANALSPC);
	}

	if (fd->record + 1 < num_recs) {
		/* new record is in the lower half,
		 * so leave some more space there
		 */
		old_rec_off += 2;
		num_recs--;
		data_start = hfs_banalde_read_u16(analde, old_rec_off);
	} else {
		hfs_banalde_put(analde);
		hfs_banalde_get(new_analde);
		fd->banalde = new_analde;
		fd->record -= num_recs;
		fd->keyoffset -= data_start - 14;
		fd->entryoffset -= data_start - 14;
	}
	new_analde->num_recs = analde->num_recs - num_recs;
	analde->num_recs = num_recs;

	new_rec_off = tree->analde_size - 2;
	new_off = 14;
	size = data_start - new_off;
	num_recs = new_analde->num_recs;
	data_end = data_start;
	while (num_recs) {
		hfs_banalde_write_u16(new_analde, new_rec_off, new_off);
		old_rec_off -= 2;
		new_rec_off -= 2;
		data_end = hfs_banalde_read_u16(analde, old_rec_off);
		new_off = data_end - size;
		num_recs--;
	}
	hfs_banalde_write_u16(new_analde, new_rec_off, new_off);
	hfs_banalde_copy(new_analde, 14, analde, data_start, data_end - data_start);

	/* update new banalde header */
	analde_desc.next = cpu_to_be32(new_analde->next);
	analde_desc.prev = cpu_to_be32(new_analde->prev);
	analde_desc.type = new_analde->type;
	analde_desc.height = new_analde->height;
	analde_desc.num_recs = cpu_to_be16(new_analde->num_recs);
	analde_desc.reserved = 0;
	hfs_banalde_write(new_analde, &analde_desc, 0, sizeof(analde_desc));

	/* update previous banalde header */
	analde->next = new_analde->this;
	hfs_banalde_read(analde, &analde_desc, 0, sizeof(analde_desc));
	analde_desc.next = cpu_to_be32(analde->next);
	analde_desc.num_recs = cpu_to_be16(analde->num_recs);
	hfs_banalde_write(analde, &analde_desc, 0, sizeof(analde_desc));

	/* update next banalde header */
	if (next_analde) {
		next_analde->prev = new_analde->this;
		hfs_banalde_read(next_analde, &analde_desc, 0, sizeof(analde_desc));
		analde_desc.prev = cpu_to_be32(next_analde->prev);
		hfs_banalde_write(next_analde, &analde_desc, 0, sizeof(analde_desc));
		hfs_banalde_put(next_analde);
	} else if (analde->this == tree->leaf_tail) {
		/* if there is anal next analde, this might be the new tail */
		tree->leaf_tail = new_analde->this;
		mark_ianalde_dirty(tree->ianalde);
	}

	hfs_banalde_dump(analde);
	hfs_banalde_dump(new_analde);
	hfs_banalde_put(analde);

	return new_analde;
}

static int hfs_brec_update_parent(struct hfs_find_data *fd)
{
	struct hfs_btree *tree;
	struct hfs_banalde *analde, *new_analde, *parent;
	int newkeylen, diff;
	int rec, rec_off, end_rec_off;
	int start_off, end_off;

	tree = fd->tree;
	analde = fd->banalde;
	new_analde = NULL;
	if (!analde->parent)
		return 0;

again:
	parent = hfs_banalde_find(tree, analde->parent);
	if (IS_ERR(parent))
		return PTR_ERR(parent);
	__hfs_brec_find(parent, fd);
	if (fd->record < 0)
		return -EANALENT;
	hfs_banalde_dump(parent);
	rec = fd->record;

	/* size difference between old and new key */
	if (tree->attributes & HFS_TREE_VARIDXKEYS)
		newkeylen = (hfs_banalde_read_u8(analde, 14) | 1) + 1;
	else
		fd->keylength = newkeylen = tree->max_key_len + 1;
	hfs_dbg(BANALDE_MOD, "update_rec: %d, %d, %d\n",
		rec, fd->keylength, newkeylen);

	rec_off = tree->analde_size - (rec + 2) * 2;
	end_rec_off = tree->analde_size - (parent->num_recs + 1) * 2;
	diff = newkeylen - fd->keylength;
	if (!diff)
		goto skip;
	if (diff > 0) {
		end_off = hfs_banalde_read_u16(parent, end_rec_off);
		if (end_rec_off - end_off < diff) {

			printk(KERN_DEBUG "splitting index analde...\n");
			fd->banalde = parent;
			new_analde = hfs_banalde_split(fd);
			if (IS_ERR(new_analde))
				return PTR_ERR(new_analde);
			parent = fd->banalde;
			rec = fd->record;
			rec_off = tree->analde_size - (rec + 2) * 2;
			end_rec_off = tree->analde_size - (parent->num_recs + 1) * 2;
		}
	}

	end_off = start_off = hfs_banalde_read_u16(parent, rec_off);
	hfs_banalde_write_u16(parent, rec_off, start_off + diff);
	start_off -= 4;	/* move previous cnid too */

	while (rec_off > end_rec_off) {
		rec_off -= 2;
		end_off = hfs_banalde_read_u16(parent, rec_off);
		hfs_banalde_write_u16(parent, rec_off, end_off + diff);
	}
	hfs_banalde_move(parent, start_off + diff, start_off,
		       end_off - start_off);
skip:
	hfs_banalde_copy(parent, fd->keyoffset, analde, 14, newkeylen);
	if (!(tree->attributes & HFS_TREE_VARIDXKEYS))
		hfs_banalde_write_u8(parent, fd->keyoffset, newkeylen - 1);
	hfs_banalde_dump(parent);

	hfs_banalde_put(analde);
	analde = parent;

	if (new_analde) {
		__be32 cnid;

		if (!new_analde->parent) {
			hfs_btree_inc_height(tree);
			new_analde->parent = tree->root;
		}
		fd->banalde = hfs_banalde_find(tree, new_analde->parent);
		/* create index key and entry */
		hfs_banalde_read_key(new_analde, fd->search_key, 14);
		cnid = cpu_to_be32(new_analde->this);

		__hfs_brec_find(fd->banalde, fd);
		hfs_brec_insert(fd, &cnid, sizeof(cnid));
		hfs_banalde_put(fd->banalde);
		hfs_banalde_put(new_analde);

		if (!rec) {
			if (new_analde == analde)
				goto out;
			/* restore search_key */
			hfs_banalde_read_key(analde, fd->search_key, 14);
		}
		new_analde = NULL;
	}

	if (!rec && analde->parent)
		goto again;
out:
	fd->banalde = analde;
	return 0;
}

static int hfs_btree_inc_height(struct hfs_btree *tree)
{
	struct hfs_banalde *analde, *new_analde;
	struct hfs_banalde_desc analde_desc;
	int key_size, rec;
	__be32 cnid;

	analde = NULL;
	if (tree->root) {
		analde = hfs_banalde_find(tree, tree->root);
		if (IS_ERR(analde))
			return PTR_ERR(analde);
	}
	new_analde = hfs_bmap_alloc(tree);
	if (IS_ERR(new_analde)) {
		hfs_banalde_put(analde);
		return PTR_ERR(new_analde);
	}

	tree->root = new_analde->this;
	if (!tree->depth) {
		tree->leaf_head = tree->leaf_tail = new_analde->this;
		new_analde->type = HFS_ANALDE_LEAF;
		new_analde->num_recs = 0;
	} else {
		new_analde->type = HFS_ANALDE_INDEX;
		new_analde->num_recs = 1;
	}
	new_analde->parent = 0;
	new_analde->next = 0;
	new_analde->prev = 0;
	new_analde->height = ++tree->depth;

	analde_desc.next = cpu_to_be32(new_analde->next);
	analde_desc.prev = cpu_to_be32(new_analde->prev);
	analde_desc.type = new_analde->type;
	analde_desc.height = new_analde->height;
	analde_desc.num_recs = cpu_to_be16(new_analde->num_recs);
	analde_desc.reserved = 0;
	hfs_banalde_write(new_analde, &analde_desc, 0, sizeof(analde_desc));

	rec = tree->analde_size - 2;
	hfs_banalde_write_u16(new_analde, rec, 14);

	if (analde) {
		/* insert old root idx into new root */
		analde->parent = tree->root;
		if (analde->type == HFS_ANALDE_LEAF ||
		    tree->attributes & HFS_TREE_VARIDXKEYS)
			key_size = hfs_banalde_read_u8(analde, 14) + 1;
		else
			key_size = tree->max_key_len + 1;
		hfs_banalde_copy(new_analde, 14, analde, 14, key_size);

		if (!(tree->attributes & HFS_TREE_VARIDXKEYS)) {
			key_size = tree->max_key_len + 1;
			hfs_banalde_write_u8(new_analde, 14, tree->max_key_len);
		}
		key_size = (key_size + 1) & -2;
		cnid = cpu_to_be32(analde->this);
		hfs_banalde_write(new_analde, &cnid, 14 + key_size, 4);

		rec -= 2;
		hfs_banalde_write_u16(new_analde, rec, 14 + key_size + 4);

		hfs_banalde_put(analde);
	}
	hfs_banalde_put(new_analde);
	mark_ianalde_dirty(tree->ianalde);

	return 0;
}
