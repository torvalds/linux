// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/bfind.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 * Search routines for btrees
 */

#include <linux/slab.h>
#include "hfsplus_fs.h"

int hfs_find_init(struct hfs_btree *tree, struct hfs_find_data *fd)
{
	void *ptr;

	fd->tree = tree;
	fd->bnode = NULL;
	ptr = kmalloc(tree->max_key_len * 2 + 4, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;
	fd->search_key = ptr;
	fd->key = ptr + tree->max_key_len + 2;
	hfs_dbg(BNODE_REFS, "find_init: %d (%p)\n",
		tree->cnid, __builtin_return_address(0));
	mutex_lock_nested(&tree->tree_lock,
			hfsplus_btree_lock_class(tree));
	return 0;
}

void hfs_find_exit(struct hfs_find_data *fd)
{
	hfs_bnode_put(fd->bnode);
	kfree(fd->search_key);
	hfs_dbg(BNODE_REFS, "find_exit: %d (%p)\n",
		fd->tree->cnid, __builtin_return_address(0));
	mutex_unlock(&fd->tree->tree_lock);
	fd->tree = NULL;
}

int hfs_find_1st_rec_by_cnid(struct hfs_bnode *bnode,
				struct hfs_find_data *fd,
				int *begin,
				int *end,
				int *cur_rec)
{
	__be32 cur_cnid;
	__be32 search_cnid;

	if (bnode->tree->cnid == HFSPLUS_EXT_CNID) {
		cur_cnid = fd->key->ext.cnid;
		search_cnid = fd->search_key->ext.cnid;
	} else if (bnode->tree->cnid == HFSPLUS_CAT_CNID) {
		cur_cnid = fd->key->cat.parent;
		search_cnid = fd->search_key->cat.parent;
	} else if (bnode->tree->cnid == HFSPLUS_ATTR_CNID) {
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

int hfs_find_rec_by_key(struct hfs_bnode *bnode,
				struct hfs_find_data *fd,
				int *begin,
				int *end,
				int *cur_rec)
{
	int cmpval;

	cmpval = bnode->tree->keycmp(fd->key, fd->search_key);
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

/* Find the record in bnode that best matches key (not greater than...)*/
int __hfs_brec_find(struct hfs_bnode *bnode, struct hfs_find_data *fd,
					search_strategy_t rec_found)
{
	u16 off, len, keylen;
	int rec;
	int b, e;
	int res;

	BUG_ON(!rec_found);
	b = 0;
	e = bnode->num_recs - 1;
	res = -ENOENT;
	do {
		rec = (e + b) / 2;
		len = hfs_brec_lenoff(bnode, rec, &off);
		keylen = hfs_brec_keylen(bnode, rec);
		if (keylen == 0) {
			res = -EINVAL;
			goto fail;
		}
		hfs_bnode_read(bnode, fd->key, off, keylen);
		if (rec_found(bnode, fd, &b, &e, &rec)) {
			res = 0;
			goto done;
		}
	} while (b <= e);

	if (rec != e && e >= 0) {
		len = hfs_brec_lenoff(bnode, e, &off);
		keylen = hfs_brec_keylen(bnode, e);
		if (keylen == 0) {
			res = -EINVAL;
			goto fail;
		}
		hfs_bnode_read(bnode, fd->key, off, keylen);
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
/* Return allocated copy of node found, set recnum to best record */
int hfs_brec_find(struct hfs_find_data *fd, search_strategy_t do_key_compare)
{
	struct hfs_btree *tree;
	struct hfs_bnode *bnode;
	u32 nidx, parent;
	__be32 data;
	int height, res;

	tree = fd->tree;
	if (fd->bnode)
		hfs_bnode_put(fd->bnode);
	fd->bnode = NULL;
	nidx = tree->root;
	if (!nidx)
		return -ENOENT;
	height = tree->depth;
	res = 0;
	parent = 0;
	for (;;) {
		bnode = hfs_bnode_find(tree, nidx);
		if (IS_ERR(bnode)) {
			res = PTR_ERR(bnode);
			bnode = NULL;
			break;
		}
		if (bnode->height != height)
			goto invalid;
		if (bnode->type != (--height ? HFS_NODE_INDEX : HFS_NODE_LEAF))
			goto invalid;
		bnode->parent = parent;

		res = __hfs_brec_find(bnode, fd, do_key_compare);
		if (!height)
			break;
		if (fd->record < 0)
			goto release;

		parent = nidx;
		hfs_bnode_read(bnode, &data, fd->entryoffset, 4);
		nidx = be32_to_cpu(data);
		hfs_bnode_put(bnode);
	}
	fd->bnode = bnode;
	return res;

invalid:
	pr_err("inconsistency in B*Tree (%d,%d,%d,%u,%u)\n",
		height, bnode->height, bnode->type, nidx, parent);
	res = -EIO;
release:
	hfs_bnode_put(bnode);
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
	hfs_bnode_read(fd->bnode, rec, fd->entryoffset, fd->entrylength);
	return 0;
}

int hfs_brec_goto(struct hfs_find_data *fd, int cnt)
{
	struct hfs_btree *tree;
	struct hfs_bnode *bnode;
	int idx, res = 0;
	u16 off, len, keylen;

	bnode = fd->bnode;
	tree = bnode->tree;

	if (cnt < 0) {
		cnt = -cnt;
		while (cnt > fd->record) {
			cnt -= fd->record + 1;
			fd->record = bnode->num_recs - 1;
			idx = bnode->prev;
			if (!idx) {
				res = -ENOENT;
				goto out;
			}
			hfs_bnode_put(bnode);
			bnode = hfs_bnode_find(tree, idx);
			if (IS_ERR(bnode)) {
				res = PTR_ERR(bnode);
				bnode = NULL;
				goto out;
			}
		}
		fd->record -= cnt;
	} else {
		while (cnt >= bnode->num_recs - fd->record) {
			cnt -= bnode->num_recs - fd->record;
			fd->record = 0;
			idx = bnode->next;
			if (!idx) {
				res = -ENOENT;
				goto out;
			}
			hfs_bnode_put(bnode);
			bnode = hfs_bnode_find(tree, idx);
			if (IS_ERR(bnode)) {
				res = PTR_ERR(bnode);
				bnode = NULL;
				goto out;
			}
		}
		fd->record += cnt;
	}

	len = hfs_brec_lenoff(bnode, fd->record, &off);
	keylen = hfs_brec_keylen(bnode, fd->record);
	if (keylen == 0) {
		res = -EINVAL;
		goto out;
	}
	fd->keyoffset = off;
	fd->keylength = keylen;
	fd->entryoffset = off + keylen;
	fd->entrylength = len - keylen;
	hfs_bnode_read(bnode, fd->key, off, keylen);
out:
	fd->bnode = bnode;
	return res;
}
