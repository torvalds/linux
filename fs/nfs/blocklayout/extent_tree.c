/*
 * Copyright (c) 2014-2016 Christoph Hellwig.
 */

#include <linux/vmalloc.h>

#include "blocklayout.h"

#define NFSDBG_FACILITY		NFSDBG_PNFS_LD

static inline struct pnfs_block_extent *
ext_node(struct rb_node *node)
{
	return rb_entry(node, struct pnfs_block_extent, be_node);
}

static struct pnfs_block_extent *
ext_tree_first(struct rb_root *root)
{
	struct rb_node *node = rb_first(root);
	return node ? ext_node(node) : NULL;
}

static struct pnfs_block_extent *
ext_tree_prev(struct pnfs_block_extent *be)
{
	struct rb_node *node = rb_prev(&be->be_node);
	return node ? ext_node(node) : NULL;
}

static struct pnfs_block_extent *
ext_tree_next(struct pnfs_block_extent *be)
{
	struct rb_node *node = rb_next(&be->be_node);
	return node ? ext_node(node) : NULL;
}

static inline sector_t
ext_f_end(struct pnfs_block_extent *be)
{
	return be->be_f_offset + be->be_length;
}

static struct pnfs_block_extent *
__ext_tree_search(struct rb_root *root, sector_t start)
{
	struct rb_node *node = root->rb_node;
	struct pnfs_block_extent *be = NULL;

	while (node) {
		be = ext_node(node);
		if (start < be->be_f_offset)
			node = node->rb_left;
		else if (start >= ext_f_end(be))
			node = node->rb_right;
		else
			return be;
	}

	if (be) {
		if (start < be->be_f_offset)
			return be;

		if (start >= ext_f_end(be))
			return ext_tree_next(be);
	}

	return NULL;
}

static bool
ext_can_merge(struct pnfs_block_extent *be1, struct pnfs_block_extent *be2)
{
	if (be1->be_state != be2->be_state)
		return false;
	if (be1->be_device != be2->be_device)
		return false;

	if (be1->be_f_offset + be1->be_length != be2->be_f_offset)
		return false;

	if (be1->be_state != PNFS_BLOCK_NONE_DATA &&
	    (be1->be_v_offset + be1->be_length != be2->be_v_offset))
		return false;

	if (be1->be_state == PNFS_BLOCK_INVALID_DATA &&
	    be1->be_tag != be2->be_tag)
		return false;

	return true;
}

static struct pnfs_block_extent *
ext_try_to_merge_left(struct rb_root *root, struct pnfs_block_extent *be)
{
	struct pnfs_block_extent *left = ext_tree_prev(be);

	if (left && ext_can_merge(left, be)) {
		left->be_length += be->be_length;
		rb_erase(&be->be_node, root);
		nfs4_put_deviceid_node(be->be_device);
		kfree(be);
		return left;
	}

	return be;
}

static struct pnfs_block_extent *
ext_try_to_merge_right(struct rb_root *root, struct pnfs_block_extent *be)
{
	struct pnfs_block_extent *right = ext_tree_next(be);

	if (right && ext_can_merge(be, right)) {
		be->be_length += right->be_length;
		rb_erase(&right->be_node, root);
		nfs4_put_deviceid_node(right->be_device);
		kfree(right);
	}

	return be;
}

static void __ext_put_deviceids(struct list_head *head)
{
	struct pnfs_block_extent *be, *tmp;

	list_for_each_entry_safe(be, tmp, head, be_list) {
		nfs4_put_deviceid_node(be->be_device);
		kfree(be);
	}
}

static void
__ext_tree_insert(struct rb_root *root,
		struct pnfs_block_extent *new, bool merge_ok)
{
	struct rb_node **p = &root->rb_node, *parent = NULL;
	struct pnfs_block_extent *be;

	while (*p) {
		parent = *p;
		be = ext_node(parent);

		if (new->be_f_offset < be->be_f_offset) {
			if (merge_ok && ext_can_merge(new, be)) {
				be->be_f_offset = new->be_f_offset;
				if (be->be_state != PNFS_BLOCK_NONE_DATA)
					be->be_v_offset = new->be_v_offset;
				be->be_length += new->be_length;
				be = ext_try_to_merge_left(root, be);
				goto free_new;
			}
			p = &(*p)->rb_left;
		} else if (new->be_f_offset >= ext_f_end(be)) {
			if (merge_ok && ext_can_merge(be, new)) {
				be->be_length += new->be_length;
				be = ext_try_to_merge_right(root, be);
				goto free_new;
			}
			p = &(*p)->rb_right;
		} else {
			BUG();
		}
	}

	rb_link_node(&new->be_node, parent, p);
	rb_insert_color(&new->be_node, root);
	return;
free_new:
	nfs4_put_deviceid_node(new->be_device);
	kfree(new);
}

static int
__ext_tree_remove(struct rb_root *root,
		sector_t start, sector_t end, struct list_head *tmp)
{
	struct pnfs_block_extent *be;
	sector_t len1 = 0, len2 = 0;
	sector_t orig_v_offset;
	sector_t orig_len;

	be = __ext_tree_search(root, start);
	if (!be)
		return 0;
	if (be->be_f_offset >= end)
		return 0;

	orig_v_offset = be->be_v_offset;
	orig_len = be->be_length;

	if (start > be->be_f_offset)
		len1 = start - be->be_f_offset;
	if (ext_f_end(be) > end)
		len2 = ext_f_end(be) - end;

	if (len2 > 0) {
		if (len1 > 0) {
			struct pnfs_block_extent *new;

			new = kzalloc(sizeof(*new), GFP_ATOMIC);
			if (!new)
				return -ENOMEM;

			be->be_length = len1;

			new->be_f_offset = end;
			if (be->be_state != PNFS_BLOCK_NONE_DATA) {
				new->be_v_offset =
					orig_v_offset + orig_len - len2;
			}
			new->be_length = len2;
			new->be_state = be->be_state;
			new->be_tag = be->be_tag;
			new->be_device = nfs4_get_deviceid(be->be_device);

			__ext_tree_insert(root, new, true);
		} else {
			be->be_f_offset = end;
			if (be->be_state != PNFS_BLOCK_NONE_DATA) {
				be->be_v_offset =
					orig_v_offset + orig_len - len2;
			}
			be->be_length = len2;
		}
	} else {
		if (len1 > 0) {
			be->be_length = len1;
			be = ext_tree_next(be);
		}

		while (be && ext_f_end(be) <= end) {
			struct pnfs_block_extent *next = ext_tree_next(be);

			rb_erase(&be->be_node, root);
			list_add_tail(&be->be_list, tmp);
			be = next;
		}

		if (be && be->be_f_offset < end) {
			len1 = ext_f_end(be) - end;
			be->be_f_offset = end;
			if (be->be_state != PNFS_BLOCK_NONE_DATA)
				be->be_v_offset += be->be_length - len1;
			be->be_length = len1;
		}
	}

	return 0;
}

int
ext_tree_insert(struct pnfs_block_layout *bl, struct pnfs_block_extent *new)
{
	struct pnfs_block_extent *be;
	struct rb_root *root;
	int err = 0;

	switch (new->be_state) {
	case PNFS_BLOCK_READWRITE_DATA:
	case PNFS_BLOCK_INVALID_DATA:
		root = &bl->bl_ext_rw;
		break;
	case PNFS_BLOCK_READ_DATA:
	case PNFS_BLOCK_NONE_DATA:
		root = &bl->bl_ext_ro;
		break;
	default:
		dprintk("invalid extent type\n");
		return -EINVAL;
	}

	spin_lock(&bl->bl_ext_lock);
retry:
	be = __ext_tree_search(root, new->be_f_offset);
	if (!be || be->be_f_offset >= ext_f_end(new)) {
		__ext_tree_insert(root, new, true);
	} else if (new->be_f_offset >= be->be_f_offset) {
		if (ext_f_end(new) <= ext_f_end(be)) {
			nfs4_put_deviceid_node(new->be_device);
			kfree(new);
		} else {
			sector_t new_len = ext_f_end(new) - ext_f_end(be);
			sector_t diff = new->be_length - new_len;

			new->be_f_offset += diff;
			new->be_v_offset += diff;
			new->be_length = new_len;
			goto retry;
		}
	} else if (ext_f_end(new) <= ext_f_end(be)) {
		new->be_length = be->be_f_offset - new->be_f_offset;
		__ext_tree_insert(root, new, true);
	} else {
		struct pnfs_block_extent *split;
		sector_t new_len = ext_f_end(new) - ext_f_end(be);
		sector_t diff = new->be_length - new_len;

		split = kmemdup(new, sizeof(*new), GFP_ATOMIC);
		if (!split) {
			err = -EINVAL;
			goto out;
		}

		split->be_length = be->be_f_offset - split->be_f_offset;
		split->be_device = nfs4_get_deviceid(new->be_device);
		__ext_tree_insert(root, split, true);

		new->be_f_offset += diff;
		new->be_v_offset += diff;
		new->be_length = new_len;
		goto retry;
	}
out:
	spin_unlock(&bl->bl_ext_lock);
	return err;
}

static bool
__ext_tree_lookup(struct rb_root *root, sector_t isect,
		struct pnfs_block_extent *ret)
{
	struct rb_node *node;
	struct pnfs_block_extent *be;

	node = root->rb_node;
	while (node) {
		be = ext_node(node);
		if (isect < be->be_f_offset)
			node = node->rb_left;
		else if (isect >= ext_f_end(be))
			node = node->rb_right;
		else {
			*ret = *be;
			return true;
		}
	}

	return false;
}

bool
ext_tree_lookup(struct pnfs_block_layout *bl, sector_t isect,
	    struct pnfs_block_extent *ret, bool rw)
{
	bool found = false;

	spin_lock(&bl->bl_ext_lock);
	if (!rw)
		found = __ext_tree_lookup(&bl->bl_ext_ro, isect, ret);
	if (!found)
		found = __ext_tree_lookup(&bl->bl_ext_rw, isect, ret);
	spin_unlock(&bl->bl_ext_lock);

	return found;
}

int ext_tree_remove(struct pnfs_block_layout *bl, bool rw,
		sector_t start, sector_t end)
{
	int err, err2;
	LIST_HEAD(tmp);

	spin_lock(&bl->bl_ext_lock);
	err = __ext_tree_remove(&bl->bl_ext_ro, start, end, &tmp);
	if (rw) {
		err2 = __ext_tree_remove(&bl->bl_ext_rw, start, end, &tmp);
		if (!err)
			err = err2;
	}
	spin_unlock(&bl->bl_ext_lock);

	__ext_put_deviceids(&tmp);
	return err;
}

static int
ext_tree_split(struct rb_root *root, struct pnfs_block_extent *be,
		sector_t split)
{
	struct pnfs_block_extent *new;
	sector_t orig_len = be->be_length;

	new = kzalloc(sizeof(*new), GFP_ATOMIC);
	if (!new)
		return -ENOMEM;

	be->be_length = split - be->be_f_offset;

	new->be_f_offset = split;
	if (be->be_state != PNFS_BLOCK_NONE_DATA)
		new->be_v_offset = be->be_v_offset + be->be_length;
	new->be_length = orig_len - be->be_length;
	new->be_state = be->be_state;
	new->be_tag = be->be_tag;
	new->be_device = nfs4_get_deviceid(be->be_device);

	__ext_tree_insert(root, new, false);
	return 0;
}

int
ext_tree_mark_written(struct pnfs_block_layout *bl, sector_t start,
		sector_t len, u64 lwb)
{
	struct rb_root *root = &bl->bl_ext_rw;
	sector_t end = start + len;
	struct pnfs_block_extent *be;
	int err = 0;
	LIST_HEAD(tmp);

	spin_lock(&bl->bl_ext_lock);
	/*
	 * First remove all COW extents or holes from written to range.
	 */
	err = __ext_tree_remove(&bl->bl_ext_ro, start, end, &tmp);
	if (err)
		goto out;

	/*
	 * Then mark all invalid extents in the range as written to.
	 */
	for (be = __ext_tree_search(root, start); be; be = ext_tree_next(be)) {
		if (be->be_f_offset >= end)
			break;

		if (be->be_state != PNFS_BLOCK_INVALID_DATA || be->be_tag)
			continue;

		if (be->be_f_offset < start) {
			struct pnfs_block_extent *left = ext_tree_prev(be);

			if (left && ext_can_merge(left, be)) {
				sector_t diff = start - be->be_f_offset;

				left->be_length += diff;

				be->be_f_offset += diff;
				be->be_v_offset += diff;
				be->be_length -= diff;
			} else {
				err = ext_tree_split(root, be, start);
				if (err)
					goto out;
			}
		}

		if (ext_f_end(be) > end) {
			struct pnfs_block_extent *right = ext_tree_next(be);

			if (right && ext_can_merge(be, right)) {
				sector_t diff = end - be->be_f_offset;

				be->be_length -= diff;

				right->be_f_offset -= diff;
				right->be_v_offset -= diff;
				right->be_length += diff;
			} else {
				err = ext_tree_split(root, be, end);
				if (err)
					goto out;
			}
		}

		if (be->be_f_offset >= start && ext_f_end(be) <= end) {
			be->be_tag = EXTENT_WRITTEN;
			be = ext_try_to_merge_left(root, be);
			be = ext_try_to_merge_right(root, be);
		}
	}
out:
	if (bl->bl_lwb < lwb)
		bl->bl_lwb = lwb;
	spin_unlock(&bl->bl_ext_lock);

	__ext_put_deviceids(&tmp);
	return err;
}

static size_t ext_tree_layoutupdate_size(struct pnfs_block_layout *bl, size_t count)
{
	if (bl->bl_scsi_layout)
		return sizeof(__be32) + PNFS_SCSI_RANGE_SIZE * count;
	else
		return sizeof(__be32) + PNFS_BLOCK_EXTENT_SIZE * count;
}

static void ext_tree_free_commitdata(struct nfs4_layoutcommit_args *arg,
		size_t buffer_size)
{
	if (arg->layoutupdate_pages != &arg->layoutupdate_page) {
		int nr_pages = DIV_ROUND_UP(buffer_size, PAGE_SIZE), i;

		for (i = 0; i < nr_pages; i++)
			put_page(arg->layoutupdate_pages[i]);
		vfree(arg->start_p);
		kfree(arg->layoutupdate_pages);
	} else {
		put_page(arg->layoutupdate_page);
	}
}

static __be32 *encode_block_extent(struct pnfs_block_extent *be, __be32 *p)
{
	p = xdr_encode_opaque_fixed(p, be->be_device->deviceid.data,
			NFS4_DEVICEID4_SIZE);
	p = xdr_encode_hyper(p, be->be_f_offset << SECTOR_SHIFT);
	p = xdr_encode_hyper(p, be->be_length << SECTOR_SHIFT);
	p = xdr_encode_hyper(p, 0LL);
	*p++ = cpu_to_be32(PNFS_BLOCK_READWRITE_DATA);
	return p;
}

static __be32 *encode_scsi_range(struct pnfs_block_extent *be, __be32 *p)
{
	p = xdr_encode_hyper(p, be->be_f_offset << SECTOR_SHIFT);
	return xdr_encode_hyper(p, be->be_length << SECTOR_SHIFT);
}

static int ext_tree_encode_commit(struct pnfs_block_layout *bl, __be32 *p,
		size_t buffer_size, size_t *count, __u64 *lastbyte)
{
	struct pnfs_block_extent *be;
	int ret = 0;

	spin_lock(&bl->bl_ext_lock);
	for (be = ext_tree_first(&bl->bl_ext_rw); be; be = ext_tree_next(be)) {
		if (be->be_state != PNFS_BLOCK_INVALID_DATA ||
		    be->be_tag != EXTENT_WRITTEN)
			continue;

		(*count)++;
		if (ext_tree_layoutupdate_size(bl, *count) > buffer_size) {
			/* keep counting.. */
			ret = -ENOSPC;
			continue;
		}

		if (bl->bl_scsi_layout)
			p = encode_scsi_range(be, p);
		else
			p = encode_block_extent(be, p);
		be->be_tag = EXTENT_COMMITTING;
	}
	*lastbyte = bl->bl_lwb - 1;
	bl->bl_lwb = 0;
	spin_unlock(&bl->bl_ext_lock);

	return ret;
}

int
ext_tree_prepare_commit(struct nfs4_layoutcommit_args *arg)
{
	struct pnfs_block_layout *bl = BLK_LO2EXT(NFS_I(arg->inode)->layout);
	size_t count = 0, buffer_size = PAGE_SIZE;
	__be32 *start_p;
	int ret;

	dprintk("%s enter\n", __func__);

	arg->layoutupdate_page = alloc_page(GFP_NOFS);
	if (!arg->layoutupdate_page)
		return -ENOMEM;
	start_p = page_address(arg->layoutupdate_page);
	arg->layoutupdate_pages = &arg->layoutupdate_page;

retry:
	ret = ext_tree_encode_commit(bl, start_p + 1, buffer_size, &count, &arg->lastbytewritten);
	if (unlikely(ret)) {
		ext_tree_free_commitdata(arg, buffer_size);

		buffer_size = ext_tree_layoutupdate_size(bl, count);
		count = 0;

		arg->layoutupdate_pages =
			kcalloc(DIV_ROUND_UP(buffer_size, PAGE_SIZE),
				sizeof(struct page *), GFP_NOFS);
		if (!arg->layoutupdate_pages)
			return -ENOMEM;

		start_p = __vmalloc(buffer_size, GFP_NOFS, PAGE_KERNEL);
		if (!start_p) {
			kfree(arg->layoutupdate_pages);
			return -ENOMEM;
		}

		goto retry;
	}

	*start_p = cpu_to_be32(count);
	arg->layoutupdate_len = ext_tree_layoutupdate_size(bl, count);

	if (unlikely(arg->layoutupdate_pages != &arg->layoutupdate_page)) {
		void *p = start_p, *end = p + arg->layoutupdate_len;
		struct page *page = NULL;
		int i = 0;

		arg->start_p = start_p;
		for ( ; p < end; p += PAGE_SIZE) {
			page = vmalloc_to_page(p);
			arg->layoutupdate_pages[i++] = page;
			get_page(page);
		}
	}

	dprintk("%s found %zu ranges\n", __func__, count);
	return 0;
}

void
ext_tree_mark_committed(struct nfs4_layoutcommit_args *arg, int status)
{
	struct pnfs_block_layout *bl = BLK_LO2EXT(NFS_I(arg->inode)->layout);
	struct rb_root *root = &bl->bl_ext_rw;
	struct pnfs_block_extent *be;

	dprintk("%s status %d\n", __func__, status);

	ext_tree_free_commitdata(arg, arg->layoutupdate_len);

	spin_lock(&bl->bl_ext_lock);
	for (be = ext_tree_first(root); be; be = ext_tree_next(be)) {
		if (be->be_state != PNFS_BLOCK_INVALID_DATA ||
		    be->be_tag != EXTENT_COMMITTING)
			continue;

		if (status) {
			/*
			 * Mark as written and try again.
			 *
			 * XXX: some real error handling here wouldn't hurt..
			 */
			be->be_tag = EXTENT_WRITTEN;
		} else {
			be->be_state = PNFS_BLOCK_READWRITE_DATA;
			be->be_tag = 0;
		}

		be = ext_try_to_merge_left(root, be);
		be = ext_try_to_merge_right(root, be);
	}
	spin_unlock(&bl->bl_ext_lock);
}
