// SPDX-License-Identifier: GPL-2.0-only

#include "fuse_i.h"
#include <linux/pagemap.h>
#include <linux/iomap.h>
#include <linux/dax.h>
#include <linux/sort.h>
#include <linux/log2.h>
#include <linux/math64.h>

struct fuse_iext {
	u64 start;
	u64 end; /* exclusive */
	struct fuse_backing *backing;
	u64 backing_offset;
};

void fuse_ext_map_destroy(struct fuse_backing *fb)
{
	unsigned int i;

	for (i = 0; i < fb->nr_extents; i++)
		fuse_backing_put(fb->extents[i].backing);

	kvfree(fb->extents);
	fb->extents = NULL;
	fb->nr_extents = 0;
}

/*
 * Resolve @offset to an extent.
 *
 * A uniform map (all extents the same length, contiguous from offset zero) is
 * resolved with a single array index.  A cyclic map is uniform by construction;
 * the index wraps and *ncycle is the number of complete cycles before @offset.
 * Only an irregular map needs a search: a binary search of the sorted array.
 */
static struct fuse_iext *fuse_find_extent(struct fuse_backing *fb, u64 offset,
					  u64 *ncycle)
{
	struct fuse_iext *ext = fb->extents;
	unsigned int lo, hi;
	u64 idx;

	*ncycle = 0;

	/* If the extents are uniform... */
	if (fb->ext_len) {
		idx = div64_u64(offset, fb->ext_len);

		if (fb->cycle_length) {
			u32 rem;

			*ncycle = div_u64_rem(idx, fb->nr_extents, &rem);
			idx = rem;
		} else if (idx >= fb->nr_extents) {
			return NULL;
		}
		return &ext[idx];
	}

	/* Extents are not uniform: binary search the extents array */
	lo = 0;
	hi = fb->nr_extents;
	while (lo < hi) {
		unsigned int mid = lo + (hi - lo) / 2;

		if (offset < ext[mid].start)
			hi = mid;
		else if (offset >= ext[mid].end)
			lo = mid + 1;
		else
			return &ext[mid];
	}

	return NULL;
}

static int fuse_add_extent(struct fuse_conn *fc, struct fuse_iext *new_fie,
			   struct fuse_extent *ext)
{
	loff_t end;
	u64 tmp;

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

	new_fie->backing = fuse_backing_lookup(fc, ext->backing_id);
	if (!new_fie->backing)
		return fuse_EIO("backing not found");

	if (new_fie->backing->type != FUSE_BACKING_DAXDEV) {
		fuse_backing_put(new_fie->backing);
		new_fie->backing = NULL;
		return fuse_EIO("backing is not dax device");
	}

	new_fie->start = ext->offset;
	new_fie->end = end;
	new_fie->backing_offset = ext->addr;

	return 0;
}

static int fuse_iext_cmp(const void *a, const void *b)
{
	const struct fuse_iext *x = a, *y = b;

	if (x->start < y->start)
		return -1;
	return x->start > y->start;
}

/*
 * fb has an array of extents;
 * - Sort it to guarantee they are in file offset order
 * - Check whether it is uniform (starts at ofs=0, all same size, no gaps)
 * - Validate if cyclic
 *
 * Array-based extent lists have better cache locality than a tree
 * (packed into cache lines). Uniform simple extent lists and all currently
 * valid cyclic extent lists can be indexed in O(1) at fault time. And
 * when search is needed, arrays can be bisected for balanced O(log2 n).
 */
static int fuse_ext_map_finish(struct fuse_backing *fb, bool cyclic)
{
	struct fuse_iext *ext = fb->extents;
	unsigned int n = fb->nr_extents;
	unsigned int i;
	bool uniform;
	u64 len;

	sort(ext, n, sizeof(*ext), fuse_iext_cmp, NULL);

	len = ext[0].end - ext[0].start;
	uniform = ext[0].start == 0;

	/* Check for uniformity */
	for (i = 1; i < n; i++) {
		if (ext[i].start < ext[i - 1].end)
			return fuse_EIO("overlap");

		if (ext[i].start != ext[i - 1].end ||
		    ext[i].end - ext[i].start != len)
			uniform = false;
	}

	if (cyclic) {
		if (!uniform ||
		    check_mul_overflow(len, (u64)n, &fb->cycle_length))
			return -EINVAL;
	}

	if (uniform)
		fb->ext_len = len;

	return 0;
}

static int fuse_ext_map_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
				    unsigned int flags, struct iomap *iomap, struct iomap *srcmap)
{
	struct fuse_backing *fb = fuse_inode_backing(get_fuse_inode(inode));
	struct fuse_iext *fie;
	u64 ncycle, seq_off, addr, tmp;
	loff_t ext_len;

	if (!fb || fb->type != FUSE_BACKING_EXTMAP)
		return fuse_EIO("missing or wrong type backing");

	fie = fuse_find_extent(fb, offset, &ncycle);
	if (!fie)
		return fuse_EIO("missing mapping");

	if (WARN_ON(fie->backing->type != FUSE_BACKING_DAXDEV))
		return fuse_EIO("wrong type backing for extent");

	if (fie->backing->dax_error) {
		fuse_make_bad(inode);
		return fuse_EIO("dax error");
	}

	ext_len = fie->end - fie->start;
	seq_off = ncycle * fb->cycle_length;
	addr = fie->backing_offset;
	if (check_mul_overflow(ncycle, ext_len, &tmp) || check_add_overflow(addr, tmp, &addr))
		return fuse_EIO("extent address overflow");

	if (check_add_overflow(addr, ext_len, &tmp))
		return fuse_EIO("extent end overflow");

	iomap->offset = fie->start + seq_off;
	iomap->addr = addr;
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
	if (WARN_ON(fb->type != FUSE_BACKING_EXTMAP || !fb->nr_extents))
		return false;

	return fb->extents[0].backing->type == FUSE_BACKING_DAXDEV;
}

int fuse_ext_map_populate(struct fuse_conn *fc, struct fuse_notify_backing_map_out *arg,
			  struct fuse_extent *ext)
{
	struct fuse_backing *fb;
	unsigned int i;
	int err;

	if (!arg->num_extents)
		return fuse_EIO("no extents");

	/* Adding extents to an existing extmap backing is not yet supported */
	if (!(arg->flags & FUSE_BACKING_MAP_CREATE))
		return -EINVAL;

	fb = kzalloc_obj(*fb);
	if (!fb)
		return -ENOMEM;

	refcount_set(&fb->count, 1);
	fb->type = FUSE_BACKING_EXTMAP;
	fb->backing_id = arg->backing_id;

	err = -ENOMEM;
	fb->extents = kvcalloc(arg->num_extents, sizeof(*fb->extents), GFP_KERNEL);
	if (!fb->extents)
		goto err_put;

	for (i = 0; i < arg->num_extents; i++) {
		err = fuse_add_extent(fc, &fb->extents[i], &ext[i]);
		if (err)
			goto err_put;
		fb->nr_extents++;
	}

	err = fuse_ext_map_finish(fb, arg->flags & FUSE_BACKING_MAP_CYCLIC);
	if (err)
		goto err_put;

	err = fuse_backing_add_64(fc, fb);
	if (!err)
		return 0;

err_put:
	fuse_backing_put(fb);
	return err;
}
