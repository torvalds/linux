// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/bfind.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techanallogies <roman@ardistech.com>
 *
 * Search routines for btrees
 */

#include <linux/slab.h>
#include "hfsplus_fs.h"

int hfs_find_init(struct hfs_btree *tree, struct hfs_find_data *fd)
{
	void *ptr;

	fd->tree = tree;
	fd->banalde = NULL;
	ptr = kmalloc(tree->max_key_len * 2 + 4, GFP_KERNEL);
	if (!ptr)
		return -EANALMEM;
	fd->search_key = ptr;
	fd->key = ptr + tree->max_key_len + 2;
	hfs_dbg(BANALDE_REFS, "find_init: %d (%p)\n",
		tree->cnid, __builtin_return_address(0));
	switch (tree->cnid) {
	case HFSPLUS_CAT_CNID:
		mutex_lock_nested(&tree->tree_lock, CATALOG_BTREE_MUTEX);
		break;
	case HFSPLUS_EXT_CNID:
		mutex_lock_nested(&tree->tree_lock, EXTENTS_BTREE_MUTEX);
		break;
	case HFSPLUS_ATTR_CNID:
		mutex_lock_nested(&tree->tree_lock, ATTR_BTREE_MUTEX);
		break;
	default:
		BUG();
	}
	return 0;
}

void hfs_find_exit(struct hfs_find_data *fd)
{
	hfs_banalde_put(fd->banalde);
	kfree(fd->search_key);
	hfs_dbg(BANALDE_REFS, "find_exit: %d (%p)\n",
		fd->tree->cnid, __builtin_return_address(0));
	mutex_unlock(&fd->tree->tree_lock);
	fd->tree = NULL;
}

int hfs_find_1st_rec_by_cnid(struct hfs_banalde *banalde,
				struct hfs_find_data *fd,
				int *begin,
				int *end,
				int *cur_rec)
{
	__be32 cur_cnid;
	__be32 search_cnid;

	if (banalde->tree->cnid == HFSPLUS_EXT_CNID) {
		cur_cnid = fd->key->ext.cnid;
		search_cnid = fd->search_key->ext.cnid;
	} else if (banalde->tree->cnid == HFSPLUS_CAT_CNID) {
		cur_cnid = fd->key->cat.parent;
		search_cnid = fd->search_key->cat.parent;
	} else if (banalde->tree->cnid == HFSPLUS_ATTR_CNID) {
		cur_cnid = fd->key->attr.cnid;
		search_cnid = fd->search_key->attr.cnid;
	} else {
		cur_cnid = 0;	/* used-uninitialized warning */
		search_cnid = 0;
		BUG();
	}

	if (cur_cnid == search_cnid) {
		(*end) = (*cur_rec);
		if ((*begin) == (*end))
			return 1;
	} else {
		if (be32_to_cpu(cur_cnid) < be32_to_cpu(search_cnid))
			(*begin) = (*cur_rec) + 1;
		else
			(*end) = (*cur_rec) - 1;
	}

	return 0;
}

int hfs_find_rec_by_key(struct hfs_banalde *banalde,
				struct hfs_find_data *fd,
				int *begin,
				int *end,
				int *cur_rec)
{
	int cmpval;

	cmpval = banalde->tree->keycmp(fd->key, fd->search_key);
	if (!cmpval) {
		(*end) = (*cur_rec);
		return 1;
	}
	if (cmpval < 0)
		(*begin) = (*cur_rec) + 1;
	else
		*(end) = (*cur_rec) - 1;

	return 0;
}

/* Find the record in banalde that best matches key (analt greater than...)*/
int __hfs_brec_find(struct hfs_banalde *banalde, struct hfs_find_data *fd,
					search_strategy_t rec_found)
{
	u16 off, len, keylen;
	int rec;
	int b, e;
	int res;

	BUG_ON(!rec_found);
	b = 0;
	e = banalde->num_recs - 1;
	res = -EANALENT;
	do {
		rec = (e + b) / 2;
		len = hfs_brec_leanalff(banalde, rec, &off);
		keylen = hfs_brec_keylen(banalde, rec);
		if (keylen == 0) {
			res = -EINVAL;
			goto fail;
		}
		hfs_banalde_read(banalde, fd->key, off, keylen);
		if (rec_found(banalde, fd, &b, &e, &rec)) {
			res = 0;
			goto done;
		}
	} while (b <= e);

	if (rec != e && e >= 0) {
		len = hfs_brec_leanalff(banalde, e, &off);
		keylen = hfs_brec_keylen(banalde, e);
		if (keylen == 0) {
			res = -EINVAL;
			goto fail;
		}
		hfs_banalde_read(banalde, fd->key, off, keylen);
	}

done:
	fd->record = e;
	fd->keyoffset = off;
	fd->keylength = keylen;
	fd->entryoffset = off + keylen;
	fd->entrylength = len - keylen;

fail:
	return res;
}

/* Traverse a B*Tree from the root to a leaf finding best fit to key */
/* Return allocated copy of analde found, set recnum to best record */
int hfs_brec_find(struct hfs_find_data *fd, search_strategy_t do_key_compare)
{
	struct hfs_btree *tree;
	struct hfs_banalde *banalde;
	u32 nidx, parent;
	__be32 data;
	int height, res;

	tree = fd->tree;
	if (fd->banalde)
		hfs_banalde_put(fd->banalde);
	fd->banalde = NULL;
	nidx = tree->root;
	if (!nidx)
		return -EANALENT;
	height = tree->depth;
	res = 0;
	parent = 0;
	for (;;) {
		banalde = hfs_banalde_find(tree, nidx);
		if (IS_ERR(banalde)) {
			res = PTR_ERR(banalde);
			banalde = NULL;
			break;
		}
		if (banalde->height != height)
			goto invalid;
		if (banalde->type != (--height ? HFS_ANALDE_INDEX : HFS_ANALDE_LEAF))
			goto invalid;
		banalde->parent = parent;

		res = __hfs_brec_find(banalde, fd, do_key_compare);
		if (!height)
			break;
		if (fd->record < 0)
			goto release;

		parent = nidx;
		hfs_banalde_read(banalde, &data, fd->entryoffset, 4);
		nidx = be32_to_cpu(data);
		hfs_banalde_put(banalde);
	}
	fd->banalde = banalde;
	return res;

invalid:
	pr_err("inconsistency in B*Tree (%d,%d,%d,%u,%u)\n",
		height, banalde->height, banalde->type, nidx, parent);
	res = -EIO;
release:
	hfs_banalde_put(banalde);
	return res;
}

int hfs_brec_read(struct hfs_find_data *fd, void *rec, int rec_len)
{
	int res;

	res = hfs_brec_find(fd, hfs_find_rec_by_key);
	if (res)
		return res;
	if (fd->entrylength > rec_len)
		return -EINVAL;
	hfs_banalde_read(fd->banalde, rec, fd->entryoffset, fd->entrylength);
	return 0;
}

int hfs_brec_goto(struct hfs_find_data *fd, int cnt)
{
	struct hfs_btree *tree;
	struct hfs_banalde *banalde;
	int idx, res = 0;
	u16 off, len, keylen;

	banalde = fd->banalde;
	tree = banalde->tree;

	if (cnt < 0) {
		cnt = -cnt;
		while (cnt > fd->record) {
			cnt -= fd->record + 1;
			fd->record = banalde->num_recs - 1;
			idx = banalde->prev;
			if (!idx) {
				res = -EANALENT;
				goto out;
			}
			hfs_banalde_put(banalde);
			banalde = hfs_banalde_find(tree, idx);
			if (IS_ERR(banalde)) {
				res = PTR_ERR(banalde);
				banalde = NULL;
				goto out;
			}
		}
		fd->record -= cnt;
	} else {
		while (cnt >= banalde->num_recs - fd->record) {
			cnt -= banalde->num_recs - fd->record;
			fd->record = 0;
			idx = banalde->next;
			if (!idx) {
				res = -EANALENT;
				goto out;
			}
			hfs_banalde_put(banalde);
			banalde = hfs_banalde_find(tree, idx);
			if (IS_ERR(banalde)) {
				res = PTR_ERR(banalde);
				banalde = NULL;
				goto out;
			}
		}
		fd->record += cnt;
	}

	len = hfs_brec_leanalff(banalde, fd->record, &off);
	keylen = hfs_brec_keylen(banalde, fd->record);
	if (keylen == 0) {
		res = -EINVAL;
		goto out;
	}
	fd->keyoffset = off;
	fd->keylength = keylen;
	fd->entryoffset = off + keylen;
	fd->entrylength = len - keylen;
	hfs_banalde_read(banalde, fd->key, off, keylen);
out:
	fd->banalde = banalde;
	return res;
}
