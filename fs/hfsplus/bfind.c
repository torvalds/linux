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
	dprint(DBG_BNODE_REFS, "find_init: %d (%p)\n", tree->cnid, __builtin_return_address(0));
	down(&tree->tree_lock);
	return 0;
}

void hfs_find_exit(struct hfs_find_data *fd)
{
	hfs_bnode_put(fd->bnode);
	kfree(fd->search_key);
	dprint(DBG_BNODE_REFS, "find_exit: %d (%p)\n", fd->tree->cnid, __builtin_return_address(0));
	up(&fd->tree->tree_lock);
	fd->tree = NULL;
}

/* Find the record in bnode that best matches key (not greater than...)*/
int __hfs_brec_find(struct hfs_bnode *bnode, struct hfs_find_data *fd)
{
	int cmpval;
	u16 off, len, keylen;
	int rec;
	int b, e;
	int res;

	b = 0;
	e = bnode->num_recs - 1;
	res = -ENOENT;
	do {
		rec = (e + b) / 2;
		len = hfs_brec_lenoff(bnode, rec, &off);
		keylen = hfs_brec_keylen(bnode, rec);
		hfs_bnode_read(bnode, fd->key, off, keylen);
		cmpval = bnode->tree->keycmp(fd->key, fd->search_key);
		if (!cmpval) {
			e = rec;
			res = 0;
			goto done;
		}
		if (cmpval < 0)
			b = rec + 1;
		else
			e = rec - 1;
	} while (b <= e);
	//printk("%d: %d,%d,%d\n", bnode->this, b, e, rec);
	if (rec != e && e >= 0) {
		len = hfs_brec_lenoff(bnode, e, &off);
		keylen = hfs_brec_keylen(bnode, e);
		hfs_bnode_read(bnode, fd->key, off, keylen);
	}
done:
	fd->record = e;
	fd->keyoffset = off;
	fd->keylength = keylen;
	fd->entryoffset = off + keylen;
	fd->entrylength = len - keylen;
	return res;
}

/* Traverse a B*Tree from the root to a leaf finding best fit to key */
/* Return allocated copy of node found, set recnum to best record */
int hfs_brec_find(struct hfs_find_data *fd)
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

		res = __hfs_brec_find(bnode, fd);
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
	printk("HFS+-fs: inconsistency in B*Tree (%d,%d,%d,%u,%u)\n",
		height, bnode->height, bnode->type, nidx, parent);
	res = -EIO;
release:
	hfs_bnode_put(bnode);
	return res;
}

int hfs_brec_read(struct hfs_find_data *fd, void *rec, int rec_len)
{
	int res;

	res = hfs_brec_find(fd);
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
	fd->keyoffset = off;
	fd->keylength = keylen;
	fd->entryoffset = off + keylen;
	fd->entrylength = len - keylen;
	hfs_bnode_read(bnode, fd->key, off, keylen);
out:
	fd->bnode = bnode;
	return res;
}
