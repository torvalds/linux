// SPDX-License-Identifier: GPL-2.0-only

#include "fuse_i.h"
#include <linux/rbtree.h>
#include <linux/pagemap.h>
#include <linux/iomap.h>
#include <linux/dax.h>

struct fuse_iext {
	struct rb_node rb;
	u64 start;
	u64 end; /* exclusive */
	struct fuse_backing *backing;
	u64 backing_offset;
};

void fuse_ext_map_destroy(struct rb_root *extents)
{
	struct fuse_iext *fie, *tmp;

	rbtree_postorder_for_each_entry_safe(fie, tmp, extents, rb) {
		fuse_backing_put(fie->backing);
		kfree(fie);
	}
}

static struct fuse_iext *fuse_find_extent(struct rb_root *extents, u64 offset)
{
	struct rb_node *node = extents->rb_node;

	while (node) {
		struct fuse_iext *fie = rb_entry(node, typeof(*fie), rb);

		if (offset < fie->start)
			node = node->rb_left;
		else if (offset >= fie->end)
			node = node->rb_right;
		else
			return fie;
	}

	return NULL;
}

static int fuse_add_extent(struct fuse_conn *fc, struct rb_root *extents,
			   struct fuse_extent *ext)
{
	struct fuse_iext *new_fie __free(kfree) = kzalloc_obj(*new_fie);
	struct rb_node *parent = NULL, **link = &extents->rb_node;
	loff_t end;
	u64 tmp;

	if (!new_fie)
		return -ENOMEM;

	if (ext->reserved[0] || ext->reserved[1])
		return fuse_EIO("reserved fields set");

	if (!PAGE_ALIGNED(ext->offset) || !PAGE_ALIGNED(ext->length) || !PAGE_ALIGNED(ext->addr))
		return fuse_EIO("not page aligned");

	if (!ext->length)
		return fuse_EIO("zero sized extent");

	if (overflows_type(ext->offset, loff_t) ||
	    check_add_overflow(ext->offset, ext->length, &end) ||
	    check_add_overflow(ext->addr, ext->length, &tmp))
		return fuse_EIO("offset overflow");

	new_fie->start = ext->offset;
	new_fie->end = end;
	new_fie->backing_offset = ext->addr;

	while (*link) {
		struct fuse_iext *fie = rb_entry(*link, typeof(*fie), rb);

		parent = *link;
		if (new_fie->end <= fie->start)
			link = &parent->rb_left;
		else if (new_fie->start >= fie->end)
			link = &parent->rb_right;
		else
			return fuse_EIO("overlap");
	}

	new_fie->backing = fuse_backing_lookup(fc, ext->backing_id);
	if (!new_fie->backing)
		return fuse_EIO("backing not found");

	if (new_fie->backing->type != FUSE_BACKING_DAXDEV) {
		fuse_backing_put(new_fie->backing);
		return fuse_EIO("backing is not dax device");
	}

	rb_link_node(&new_fie->rb, parent, link);
	rb_insert_color(&no_free_ptr(new_fie)->rb, extents);

	return 0;
}

static int fuse_ext_map_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
				    unsigned int flags, struct iomap *iomap, struct iomap *srcmap)
{
	struct fuse_backing *fb = fuse_inode_backing(get_fuse_inode(inode));
	struct fuse_iext *fie;
	loff_t ext_len;

	if (!fb || fb->type != FUSE_BACKING_EXTMAP)
		return fuse_EIO("missing or wrong type backing");

	fie = fuse_find_extent(&fb->extents, offset);
	if (!fie)
		return fuse_EIO("missing mapping");

	if (WARN_ON(fie->backing->type != FUSE_BACKING_DAXDEV))
		return fuse_EIO("wrong type backing for extent");

	if (fie->backing->dax_error) {
		fuse_make_bad(inode);
		return fuse_EIO("dax error");
	}

	ext_len = fie->end - fie->start;

	iomap->offset = fie->start;
	iomap->addr = fie->backing_offset;
	iomap->length = ext_len;
	iomap->dax_dev = fie->backing->dax_dev;
	iomap->type = IOMAP_MAPPED;
	iomap->flags = 0;

	return 0;
}

static const struct iomap_ops fuse_ext_map_iomap_ops = {
	.iomap_begin		= fuse_ext_map_iomap_begin,
};

static vm_fault_t fuse_ext_map_huge_fault(struct vm_fault *vmf, unsigned int order)
{
	struct inode *inode = file_inode(vmf->vma->vm_file);
	bool write_fault = (vmf->flags & FAULT_FLAG_WRITE) && (vmf->vma->vm_flags & VM_SHARED);
	vm_fault_t ret;
	unsigned long pfn;

	if (!IS_ENABLED(CONFIG_FS_DAX))
		return VM_FAULT_SIGBUS;

	if (WARN_ON_ONCE(!IS_DAX(inode)))
		return VM_FAULT_SIGBUS;

	if (write_fault) {
		sb_start_pagefault(inode->i_sb);
		file_update_time(vmf->vma->vm_file);
	}

	filemap_invalidate_lock_shared(inode->i_mapping);

	ret = dax_iomap_fault(vmf, order, &pfn, NULL, &fuse_ext_map_iomap_ops);
	if (ret & VM_FAULT_NEEDDSYNC)
		ret = dax_finish_sync_fault(vmf, order, pfn);

	filemap_invalidate_unlock_shared(inode->i_mapping);

	if (write_fault)
		sb_end_pagefault(inode->i_sb);

	return ret;
}

static vm_fault_t fuse_ext_map_fault(struct vm_fault *vmf)
{
	return fuse_ext_map_huge_fault(vmf, 0);
}

static const struct vm_operations_struct fuse_ext_map_vm_ops = {
	.fault		= fuse_ext_map_fault,
	.huge_fault	= fuse_ext_map_huge_fault,
	.page_mkwrite	= fuse_ext_map_fault,
	.pfn_mkwrite	= fuse_ext_map_fault,
};

static void fuse_rw_clamp(struct kiocb *iocb, struct iov_iter *ubuf)
{
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	loff_t i_size = i_size_read(inode);
	loff_t max_count = iocb->ki_pos >= i_size ? 0 : i_size - iocb->ki_pos;

	if (iov_iter_count(ubuf) > max_count)
		iov_iter_truncate(ubuf, max_count);
}

ssize_t fuse_ext_map_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	ssize_t res;

	guard(rwsem_read)(&inode->i_rwsem);

	fuse_rw_clamp(iocb, to);

	if (!iov_iter_count(to))
		return 0;

	if (!IS_ENABLED(CONFIG_FS_DAX))
		return -EIO;

	res = dax_iomap_rw(iocb, to, &fuse_ext_map_iomap_ops);

	file_accessed(iocb->ki_filp);
	return res;
}

ssize_t fuse_ext_map_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	ssize_t res;

	res = generic_write_checks(iocb, from);
	if (res <= 0)
		return res;

	fuse_rw_clamp(iocb, from);

	if (!iov_iter_count(from))
		return -ENOSPC;

	res = kiocb_modified(iocb);
	if (res)
		return res;

	if (!IS_ENABLED(CONFIG_FS_DAX))
		return -EIO;

	return dax_iomap_rw(iocb, from, &fuse_ext_map_iomap_ops);
}

int fuse_ext_map_mmap(struct file *file, struct vm_area_struct *vma)
{
	file_accessed(file);
	vma->vm_ops = &fuse_ext_map_vm_ops;
	vm_flags_set(vma, VM_HUGEPAGE);
	return 0;
}

bool fuse_ext_map_is_dax(struct fuse_backing *fb)
{
	struct fuse_iext *fie;

	if (WARN_ON(fb->type != FUSE_BACKING_EXTMAP || RB_EMPTY_ROOT(&fb->extents)))
		return false;

	fie = rb_entry(fb->extents.rb_node, typeof(*fie), rb);
	return fie->backing->type == FUSE_BACKING_DAXDEV;
}

int fuse_ext_map_populate(struct fuse_conn *fc, struct fuse_notify_backing_map_out *arg,
			  struct fuse_extent *ext)
{
	struct fuse_backing *fb;
	unsigned int i;
	int err;

	if (!arg->num_extents)
		return fuse_EIO("no extents");

	if (arg->flags & FUSE_BACKING_MAP_CREATE) {
		fb = kzalloc_obj(*fb);
		if (!fb)
			return -ENOMEM;

		refcount_set(&fb->count, 1);
		fb->type = FUSE_BACKING_EXTMAP;
		fb->backing_id = arg->backing_id;
	} else {
		/* Adding extents to an existing extmap backing is not yet supported */
		return -EINVAL;
	}

	for (i = 0; i < arg->num_extents; i++) {
		err = fuse_add_extent(fc, &fb->extents, &ext[i]);
		if (err)
			goto err_put;
	}

	err = 0;
	if (arg->flags & FUSE_BACKING_MAP_CREATE) {
		err = fuse_backing_add_64(fc, fb);
		if (!err)
			return 0;
	}

err_put:
	fuse_backing_put(fb);
	return err;
}
