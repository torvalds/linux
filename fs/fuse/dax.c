// SPDX-License-Identifier: GPL-2.0
/*
 * dax: direct host memory access
 * Copyright (C) 2020 Red Hat, Inc.
 */

#include "fuse_i.h"

#include <linux/dax.h>
#include <linux/uio.h>
#include <linux/pfn_t.h>
#include <linux/iomap.h>
#include <linux/interval_tree.h>

/*
 * Default memory range size.  A power of 2 so it agrees with common FUSE_INIT
 * map_alignment values 4KB and 64KB.
 */
#define FUSE_DAX_SHIFT	21
#define FUSE_DAX_SZ	(1 << FUSE_DAX_SHIFT)
#define FUSE_DAX_PAGES	(FUSE_DAX_SZ / PAGE_SIZE)

/** Translation information for file offsets to DAX window offsets */
struct fuse_dax_mapping {
	/* Will connect in fcd->free_ranges to keep track of free memory */
	struct list_head list;

	/* For interval tree in file/inode */
	struct interval_tree_node itn;

	/** Position in DAX window */
	u64 window_offset;

	/** Length of mapping, in bytes */
	loff_t length;

	/* Is this mapping read-only or read-write */
	bool writable;
};

/* Per-inode dax map */
struct fuse_inode_dax {
	/* Semaphore to protect modifications to the dmap tree */
	struct rw_semaphore sem;

	/* Sorted rb tree of struct fuse_dax_mapping elements */
	struct rb_root_cached tree;
	unsigned long nr;
};

struct fuse_conn_dax {
	/* DAX device */
	struct dax_device *dev;

	/* Lock protecting accessess to  members of this structure */
	spinlock_t lock;

	/* DAX Window Free Ranges */
	long nr_free_ranges;
	struct list_head free_ranges;
};

static inline struct fuse_dax_mapping *
node_to_dmap(struct interval_tree_node *node)
{
	if (!node)
		return NULL;

	return container_of(node, struct fuse_dax_mapping, itn);
}

static struct fuse_dax_mapping *alloc_dax_mapping(struct fuse_conn_dax *fcd)
{
	struct fuse_dax_mapping *dmap;

	spin_lock(&fcd->lock);
	dmap = list_first_entry_or_null(&fcd->free_ranges,
					struct fuse_dax_mapping, list);
	if (dmap) {
		list_del_init(&dmap->list);
		WARN_ON(fcd->nr_free_ranges <= 0);
		fcd->nr_free_ranges--;
	}
	spin_unlock(&fcd->lock);
	return dmap;
}

/* This assumes fcd->lock is held */
static void __dmap_add_to_free_pool(struct fuse_conn_dax *fcd,
				struct fuse_dax_mapping *dmap)
{
	list_add_tail(&dmap->list, &fcd->free_ranges);
	fcd->nr_free_ranges++;
}

static void dmap_add_to_free_pool(struct fuse_conn_dax *fcd,
				struct fuse_dax_mapping *dmap)
{
	/* Return fuse_dax_mapping to free list */
	spin_lock(&fcd->lock);
	__dmap_add_to_free_pool(fcd, dmap);
	spin_unlock(&fcd->lock);
}

static int fuse_setup_one_mapping(struct inode *inode, unsigned long start_idx,
				  struct fuse_dax_mapping *dmap, bool writable,
				  bool upgrade)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_conn_dax *fcd = fc->dax;
	struct fuse_inode *fi = get_fuse_inode(inode);
	struct fuse_setupmapping_in inarg;
	loff_t offset = start_idx << FUSE_DAX_SHIFT;
	FUSE_ARGS(args);
	ssize_t err;

	WARN_ON(fcd->nr_free_ranges < 0);

	/* Ask fuse daemon to setup mapping */
	memset(&inarg, 0, sizeof(inarg));
	inarg.foffset = offset;
	inarg.fh = -1;
	inarg.moffset = dmap->window_offset;
	inarg.len = FUSE_DAX_SZ;
	inarg.flags |= FUSE_SETUPMAPPING_FLAG_READ;
	if (writable)
		inarg.flags |= FUSE_SETUPMAPPING_FLAG_WRITE;
	args.opcode = FUSE_SETUPMAPPING;
	args.nodeid = fi->nodeid;
	args.in_numargs = 1;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	err = fuse_simple_request(fc, &args);
	if (err < 0)
		return err;
	dmap->writable = writable;
	if (!upgrade) {
		dmap->itn.start = dmap->itn.last = start_idx;
		/* Protected by fi->dax->sem */
		interval_tree_insert(&dmap->itn, &fi->dax->tree);
		fi->dax->nr++;
	}
	return 0;
}

static int fuse_send_removemapping(struct inode *inode,
				   struct fuse_removemapping_in *inargp,
				   struct fuse_removemapping_one *remove_one)
{
	struct fuse_inode *fi = get_fuse_inode(inode);
	struct fuse_conn *fc = get_fuse_conn(inode);
	FUSE_ARGS(args);

	args.opcode = FUSE_REMOVEMAPPING;
	args.nodeid = fi->nodeid;
	args.in_numargs = 2;
	args.in_args[0].size = sizeof(*inargp);
	args.in_args[0].value = inargp;
	args.in_args[1].size = inargp->count * sizeof(*remove_one);
	args.in_args[1].value = remove_one;
	return fuse_simple_request(fc, &args);
}

static int dmap_removemapping_list(struct inode *inode, unsigned int num,
				   struct list_head *to_remove)
{
	struct fuse_removemapping_one *remove_one, *ptr;
	struct fuse_removemapping_in inarg;
	struct fuse_dax_mapping *dmap;
	int ret, i = 0, nr_alloc;

	nr_alloc = min_t(unsigned int, num, FUSE_REMOVEMAPPING_MAX_ENTRY);
	remove_one = kmalloc_array(nr_alloc, sizeof(*remove_one), GFP_NOFS);
	if (!remove_one)
		return -ENOMEM;

	ptr = remove_one;
	list_for_each_entry(dmap, to_remove, list) {
		ptr->moffset = dmap->window_offset;
		ptr->len = dmap->length;
		ptr++;
		i++;
		num--;
		if (i >= nr_alloc || num == 0) {
			memset(&inarg, 0, sizeof(inarg));
			inarg.count = i;
			ret = fuse_send_removemapping(inode, &inarg,
						      remove_one);
			if (ret)
				goto out;
			ptr = remove_one;
			i = 0;
		}
	}
out:
	kfree(remove_one);
	return ret;
}

/*
 * Cleanup dmap entry and add back to free list. This should be called with
 * fcd->lock held.
 */
static void dmap_reinit_add_to_free_pool(struct fuse_conn_dax *fcd,
					    struct fuse_dax_mapping *dmap)
{
	pr_debug("fuse: freeing memory range start_idx=0x%lx end_idx=0x%lx window_offset=0x%llx length=0x%llx\n",
		 dmap->itn.start, dmap->itn.last, dmap->window_offset,
		 dmap->length);
	dmap->itn.start = dmap->itn.last = 0;
	__dmap_add_to_free_pool(fcd, dmap);
}

/*
 * Free inode dmap entries whose range falls inside [start, end].
 * Does not take any locks. At this point of time it should only be
 * called from evict_inode() path where we know all dmap entries can be
 * reclaimed.
 */
static void inode_reclaim_dmap_range(struct fuse_conn_dax *fcd,
				     struct inode *inode,
				     loff_t start, loff_t end)
{
	struct fuse_inode *fi = get_fuse_inode(inode);
	struct fuse_dax_mapping *dmap, *n;
	int err, num = 0;
	LIST_HEAD(to_remove);
	unsigned long start_idx = start >> FUSE_DAX_SHIFT;
	unsigned long end_idx = end >> FUSE_DAX_SHIFT;
	struct interval_tree_node *node;

	while (1) {
		node = interval_tree_iter_first(&fi->dax->tree, start_idx,
						end_idx);
		if (!node)
			break;
		dmap = node_to_dmap(node);
		interval_tree_remove(&dmap->itn, &fi->dax->tree);
		num++;
		list_add(&dmap->list, &to_remove);
	}

	/* Nothing to remove */
	if (list_empty(&to_remove))
		return;

	WARN_ON(fi->dax->nr < num);
	fi->dax->nr -= num;
	err = dmap_removemapping_list(inode, num, &to_remove);
	if (err && err != -ENOTCONN) {
		pr_warn("Failed to removemappings. start=0x%llx end=0x%llx\n",
			start, end);
	}
	spin_lock(&fcd->lock);
	list_for_each_entry_safe(dmap, n, &to_remove, list) {
		list_del_init(&dmap->list);
		dmap_reinit_add_to_free_pool(fcd, dmap);
	}
	spin_unlock(&fcd->lock);
}

/*
 * It is called from evict_inode() and by that time inode is going away. So
 * this function does not take any locks like fi->dax->sem for traversing
 * that fuse inode interval tree. If that lock is taken then lock validator
 * complains of deadlock situation w.r.t fs_reclaim lock.
 */
void fuse_dax_inode_cleanup(struct inode *inode)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_inode *fi = get_fuse_inode(inode);

	/*
	 * fuse_evict_inode() has already called truncate_inode_pages_final()
	 * before we arrive here. So we should not have to worry about any
	 * pages/exception entries still associated with inode.
	 */
	inode_reclaim_dmap_range(fc->dax, inode, 0, -1);
	WARN_ON(fi->dax->nr);
}

static void fuse_fill_iomap_hole(struct iomap *iomap, loff_t length)
{
	iomap->addr = IOMAP_NULL_ADDR;
	iomap->length = length;
	iomap->type = IOMAP_HOLE;
}

static void fuse_fill_iomap(struct inode *inode, loff_t pos, loff_t length,
			    struct iomap *iomap, struct fuse_dax_mapping *dmap,
			    unsigned int flags)
{
	loff_t offset, len;
	loff_t i_size = i_size_read(inode);

	offset = pos - (dmap->itn.start << FUSE_DAX_SHIFT);
	len = min(length, dmap->length - offset);

	/* If length is beyond end of file, truncate further */
	if (pos + len > i_size)
		len = i_size - pos;

	if (len > 0) {
		iomap->addr = dmap->window_offset + offset;
		iomap->length = len;
		if (flags & IOMAP_FAULT)
			iomap->length = ALIGN(len, PAGE_SIZE);
		iomap->type = IOMAP_MAPPED;
	} else {
		/* Mapping beyond end of file is hole */
		fuse_fill_iomap_hole(iomap, length);
	}
}

static int fuse_setup_new_dax_mapping(struct inode *inode, loff_t pos,
				      loff_t length, unsigned int flags,
				      struct iomap *iomap)
{
	struct fuse_inode *fi = get_fuse_inode(inode);
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_conn_dax *fcd = fc->dax;
	struct fuse_dax_mapping *dmap, *alloc_dmap = NULL;
	int ret;
	bool writable = flags & IOMAP_WRITE;
	unsigned long start_idx = pos >> FUSE_DAX_SHIFT;
	struct interval_tree_node *node;

	alloc_dmap = alloc_dax_mapping(fcd);
	if (!alloc_dmap)
		return -EIO;

	/*
	 * Take write lock so that only one caller can try to setup mapping
	 * and other waits.
	 */
	down_write(&fi->dax->sem);
	/*
	 * We dropped lock. Check again if somebody else setup
	 * mapping already.
	 */
	node = interval_tree_iter_first(&fi->dax->tree, start_idx, start_idx);
	if (node) {
		dmap = node_to_dmap(node);
		fuse_fill_iomap(inode, pos, length, iomap, dmap, flags);
		dmap_add_to_free_pool(fcd, alloc_dmap);
		up_write(&fi->dax->sem);
		return 0;
	}

	/* Setup one mapping */
	ret = fuse_setup_one_mapping(inode, pos >> FUSE_DAX_SHIFT, alloc_dmap,
				     writable, false);
	if (ret < 0) {
		dmap_add_to_free_pool(fcd, alloc_dmap);
		up_write(&fi->dax->sem);
		return ret;
	}
	fuse_fill_iomap(inode, pos, length, iomap, alloc_dmap, flags);
	up_write(&fi->dax->sem);
	return 0;
}

static int fuse_upgrade_dax_mapping(struct inode *inode, loff_t pos,
				    loff_t length, unsigned int flags,
				    struct iomap *iomap)
{
	struct fuse_inode *fi = get_fuse_inode(inode);
	struct fuse_dax_mapping *dmap;
	int ret;
	unsigned long idx = pos >> FUSE_DAX_SHIFT;
	struct interval_tree_node *node;

	/*
	 * Take exclusive lock so that only one caller can try to setup
	 * mapping and others wait.
	 */
	down_write(&fi->dax->sem);
	node = interval_tree_iter_first(&fi->dax->tree, idx, idx);

	/* We are holding either inode lock or i_mmap_sem, and that should
	 * ensure that dmap can't reclaimed or truncated and it should still
	 * be there in tree despite the fact we dropped and re-acquired the
	 * lock.
	 */
	ret = -EIO;
	if (WARN_ON(!node))
		goto out_err;

	dmap = node_to_dmap(node);

	/* Maybe another thread already upgraded mapping while we were not
	 * holding lock.
	 */
	if (dmap->writable) {
		ret = 0;
		goto out_fill_iomap;
	}

	ret = fuse_setup_one_mapping(inode, pos >> FUSE_DAX_SHIFT, dmap, true,
				     true);
	if (ret < 0)
		goto out_err;
out_fill_iomap:
	fuse_fill_iomap(inode, pos, length, iomap, dmap, flags);
out_err:
	up_write(&fi->dax->sem);
	return ret;
}

/* This is just for DAX and the mapping is ephemeral, do not use it for other
 * purposes since there is no block device with a permanent mapping.
 */
static int fuse_iomap_begin(struct inode *inode, loff_t pos, loff_t length,
			    unsigned int flags, struct iomap *iomap,
			    struct iomap *srcmap)
{
	struct fuse_inode *fi = get_fuse_inode(inode);
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_dax_mapping *dmap;
	bool writable = flags & IOMAP_WRITE;
	unsigned long start_idx = pos >> FUSE_DAX_SHIFT;
	struct interval_tree_node *node;

	/* We don't support FIEMAP */
	if (WARN_ON(flags & IOMAP_REPORT))
		return -EIO;

	iomap->offset = pos;
	iomap->flags = 0;
	iomap->bdev = NULL;
	iomap->dax_dev = fc->dax->dev;

	/*
	 * Both read/write and mmap path can race here. So we need something
	 * to make sure if we are setting up mapping, then other path waits
	 *
	 * For now, use a semaphore for this. It probably needs to be
	 * optimized later.
	 */
	down_read(&fi->dax->sem);
	node = interval_tree_iter_first(&fi->dax->tree, start_idx, start_idx);
	if (node) {
		dmap = node_to_dmap(node);
		if (writable && !dmap->writable) {
			/* Upgrade read-only mapping to read-write. This will
			 * require exclusive fi->dax->sem lock as we don't want
			 * two threads to be trying to this simultaneously
			 * for same dmap. So drop shared lock and acquire
			 * exclusive lock.
			 */
			up_read(&fi->dax->sem);
			pr_debug("%s: Upgrading mapping at offset 0x%llx length 0x%llx\n",
				 __func__, pos, length);
			return fuse_upgrade_dax_mapping(inode, pos, length,
							flags, iomap);
		} else {
			fuse_fill_iomap(inode, pos, length, iomap, dmap, flags);
			up_read(&fi->dax->sem);
			return 0;
		}
	} else {
		up_read(&fi->dax->sem);
		pr_debug("%s: no mapping at offset 0x%llx length 0x%llx\n",
				__func__, pos, length);
		if (pos >= i_size_read(inode))
			goto iomap_hole;

		return fuse_setup_new_dax_mapping(inode, pos, length, flags,
						  iomap);
	}

	/*
	 * If read beyond end of file happnes, fs code seems to return
	 * it as hole
	 */
iomap_hole:
	fuse_fill_iomap_hole(iomap, length);
	pr_debug("%s returning hole mapping. pos=0x%llx length_asked=0x%llx length_returned=0x%llx\n",
		 __func__, pos, length, iomap->length);
	return 0;
}

static int fuse_iomap_end(struct inode *inode, loff_t pos, loff_t length,
			  ssize_t written, unsigned int flags,
			  struct iomap *iomap)
{
	/* DAX writes beyond end-of-file aren't handled using iomap, so the
	 * file size is unchanged and there is nothing to do here.
	 */
	return 0;
}

static const struct iomap_ops fuse_iomap_ops = {
	.iomap_begin = fuse_iomap_begin,
	.iomap_end = fuse_iomap_end,
};

ssize_t fuse_dax_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	ssize_t ret;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!inode_trylock_shared(inode))
			return -EAGAIN;
	} else {
		inode_lock_shared(inode);
	}

	ret = dax_iomap_rw(iocb, to, &fuse_iomap_ops);
	inode_unlock_shared(inode);

	/* TODO file_accessed(iocb->f_filp) */
	return ret;
}

static bool file_extending_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);

	return (iov_iter_rw(from) == WRITE &&
		((iocb->ki_pos) >= i_size_read(inode) ||
		  (iocb->ki_pos + iov_iter_count(from) > i_size_read(inode))));
}

static ssize_t fuse_dax_direct_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct fuse_io_priv io = FUSE_IO_PRIV_SYNC(iocb);
	ssize_t ret;

	ret = fuse_direct_io(&io, from, &iocb->ki_pos, FUSE_DIO_WRITE);
	if (ret < 0)
		return ret;

	fuse_invalidate_attr(inode);
	fuse_write_update_size(inode, iocb->ki_pos);
	return ret;
}

ssize_t fuse_dax_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	ssize_t ret;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!inode_trylock(inode))
			return -EAGAIN;
	} else {
		inode_lock(inode);
	}

	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		goto out;

	ret = file_remove_privs(iocb->ki_filp);
	if (ret)
		goto out;
	/* TODO file_update_time() but we don't want metadata I/O */

	/* Do not use dax for file extending writes as write and on
	 * disk i_size increase are not atomic otherwise.
	 */
	if (file_extending_write(iocb, from))
		ret = fuse_dax_direct_write(iocb, from);
	else
		ret = dax_iomap_rw(iocb, from, &fuse_iomap_ops);

out:
	inode_unlock(inode);

	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
	return ret;
}

static void fuse_free_dax_mem_ranges(struct list_head *mem_list)
{
	struct fuse_dax_mapping *range, *temp;

	/* Free All allocated elements */
	list_for_each_entry_safe(range, temp, mem_list, list) {
		list_del(&range->list);
		kfree(range);
	}
}

void fuse_dax_conn_free(struct fuse_conn *fc)
{
	if (fc->dax) {
		fuse_free_dax_mem_ranges(&fc->dax->free_ranges);
		kfree(fc->dax);
	}
}

static int fuse_dax_mem_range_init(struct fuse_conn_dax *fcd)
{
	long nr_pages, nr_ranges;
	void *kaddr;
	pfn_t pfn;
	struct fuse_dax_mapping *range;
	int ret, id;
	size_t dax_size = -1;
	unsigned long i;

	INIT_LIST_HEAD(&fcd->free_ranges);
	id = dax_read_lock();
	nr_pages = dax_direct_access(fcd->dev, 0, PHYS_PFN(dax_size), &kaddr,
				     &pfn);
	dax_read_unlock(id);
	if (nr_pages < 0) {
		pr_debug("dax_direct_access() returned %ld\n", nr_pages);
		return nr_pages;
	}

	nr_ranges = nr_pages/FUSE_DAX_PAGES;
	pr_debug("%s: dax mapped %ld pages. nr_ranges=%ld\n",
		__func__, nr_pages, nr_ranges);

	for (i = 0; i < nr_ranges; i++) {
		range = kzalloc(sizeof(struct fuse_dax_mapping), GFP_KERNEL);
		ret = -ENOMEM;
		if (!range)
			goto out_err;

		/* TODO: This offset only works if virtio-fs driver is not
		 * having some memory hidden at the beginning. This needs
		 * better handling
		 */
		range->window_offset = i * FUSE_DAX_SZ;
		range->length = FUSE_DAX_SZ;
		list_add_tail(&range->list, &fcd->free_ranges);
	}

	fcd->nr_free_ranges = nr_ranges;
	return 0;
out_err:
	/* Free All allocated elements */
	fuse_free_dax_mem_ranges(&fcd->free_ranges);
	return ret;
}

int fuse_dax_conn_alloc(struct fuse_conn *fc, struct dax_device *dax_dev)
{
	struct fuse_conn_dax *fcd;
	int err;

	if (!dax_dev)
		return 0;

	fcd = kzalloc(sizeof(*fcd), GFP_KERNEL);
	if (!fcd)
		return -ENOMEM;

	spin_lock_init(&fcd->lock);
	fcd->dev = dax_dev;
	err = fuse_dax_mem_range_init(fcd);
	if (err) {
		kfree(fcd);
		return err;
	}

	fc->dax = fcd;
	return 0;
}

bool fuse_dax_inode_alloc(struct super_block *sb, struct fuse_inode *fi)
{
	struct fuse_conn *fc = get_fuse_conn_super(sb);

	fi->dax = NULL;
	if (fc->dax) {
		fi->dax = kzalloc(sizeof(*fi->dax), GFP_KERNEL_ACCOUNT);
		if (!fi->dax)
			return false;

		init_rwsem(&fi->dax->sem);
		fi->dax->tree = RB_ROOT_CACHED;
	}

	return true;
}

void fuse_dax_inode_init(struct inode *inode)
{
	struct fuse_conn *fc = get_fuse_conn(inode);

	if (!fc->dax)
		return;

	inode->i_flags |= S_DAX;
}

bool fuse_dax_check_alignment(struct fuse_conn *fc, unsigned int map_alignment)
{
	if (fc->dax && (map_alignment > FUSE_DAX_SHIFT)) {
		pr_warn("FUSE: map_alignment %u incompatible with dax mem range size %u\n",
			map_alignment, FUSE_DAX_SZ);
		return false;
	}
	return true;
}
