// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 *  Regular file handling primitives for NTFS-based filesystems.
 *
 */

#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/compat.h>
#include <linux/falloc.h>
#include <linux/fiemap.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

static int ntfs_ioctl_fitrim(struct ntfs_sb_info *sbi, unsigned long arg)
{
	struct fstrim_range __user *user_range;
	struct fstrim_range range;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!bdev_max_discard_sectors(sbi->sb->s_bdev))
		return -EOPNOTSUPP;

	user_range = (struct fstrim_range __user *)arg;
	if (copy_from_user(&range, user_range, sizeof(range)))
		return -EFAULT;

	range.minlen = max_t(u32, range.minlen,
			     bdev_discard_granularity(sbi->sb->s_bdev));

	err = ntfs_trim_fs(sbi, &range);
	if (err < 0)
		return err;

	if (copy_to_user(user_range, &range, sizeof(range)))
		return -EFAULT;

	return 0;
}

static long ntfs_ioctl(struct file *filp, u32 cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct ntfs_sb_info *sbi = inode->i_sb->s_fs_info;

	switch (cmd) {
	case FITRIM:
		return ntfs_ioctl_fitrim(sbi, arg);
	}
	return -ENOTTY; /* Inappropriate ioctl for device. */
}

#ifdef CONFIG_COMPAT
static long ntfs_compat_ioctl(struct file *filp, u32 cmd, unsigned long arg)

{
	return ntfs_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif

/*
 * ntfs_getattr - inode_operations::getattr
 */
int ntfs_getattr(struct user_namespace *mnt_userns, const struct path *path,
		 struct kstat *stat, u32 request_mask, u32 flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct ntfs_inode *ni = ntfs_i(inode);

	if (is_compressed(ni))
		stat->attributes |= STATX_ATTR_COMPRESSED;

	if (is_encrypted(ni))
		stat->attributes |= STATX_ATTR_ENCRYPTED;

	stat->attributes_mask |= STATX_ATTR_COMPRESSED | STATX_ATTR_ENCRYPTED;

	generic_fillattr(mnt_userns, inode, stat);

	stat->result_mask |= STATX_BTIME;
	stat->btime = ni->i_crtime;
	stat->blksize = ni->mi.sbi->cluster_size; /* 512, 1K, ..., 2M */

	return 0;
}

static int ntfs_extend_initialized_size(struct file *file,
					struct ntfs_inode *ni,
					const loff_t valid,
					const loff_t new_valid)
{
	struct inode *inode = &ni->vfs_inode;
	struct address_space *mapping = inode->i_mapping;
	struct ntfs_sb_info *sbi = inode->i_sb->s_fs_info;
	loff_t pos = valid;
	int err;

	if (is_resident(ni)) {
		ni->i_valid = new_valid;
		return 0;
	}

	WARN_ON(is_compressed(ni));
	WARN_ON(valid >= new_valid);

	for (;;) {
		u32 zerofrom, len;
		struct page *page;
		u8 bits;
		CLST vcn, lcn, clen;

		if (is_sparsed(ni)) {
			bits = sbi->cluster_bits;
			vcn = pos >> bits;

			err = attr_data_get_block(ni, vcn, 1, &lcn, &clen, NULL,
						  false);
			if (err)
				goto out;

			if (lcn == SPARSE_LCN) {
				pos = ((loff_t)clen + vcn) << bits;
				ni->i_valid = pos;
				goto next;
			}
		}

		zerofrom = pos & (PAGE_SIZE - 1);
		len = PAGE_SIZE - zerofrom;

		if (pos + len > new_valid)
			len = new_valid - pos;

		err = ntfs_write_begin(file, mapping, pos, len, &page, NULL);
		if (err)
			goto out;

		zero_user_segment(page, zerofrom, PAGE_SIZE);

		/* This function in any case puts page. */
		err = ntfs_write_end(file, mapping, pos, len, len, page, NULL);
		if (err < 0)
			goto out;
		pos += len;

next:
		if (pos >= new_valid)
			break;

		balance_dirty_pages_ratelimited(mapping);
		cond_resched();
	}

	return 0;

out:
	ni->i_valid = valid;
	ntfs_inode_warn(inode, "failed to extend initialized size to %llx.",
			new_valid);
	return err;
}

/*
 * ntfs_zero_range - Helper function for punch_hole.
 *
 * It zeroes a range [vbo, vbo_to).
 */
static int ntfs_zero_range(struct inode *inode, u64 vbo, u64 vbo_to)
{
	int err = 0;
	struct address_space *mapping = inode->i_mapping;
	u32 blocksize = 1 << inode->i_blkbits;
	pgoff_t idx = vbo >> PAGE_SHIFT;
	u32 from = vbo & (PAGE_SIZE - 1);
	pgoff_t idx_end = (vbo_to + PAGE_SIZE - 1) >> PAGE_SHIFT;
	loff_t page_off;
	struct buffer_head *head, *bh;
	u32 bh_next, bh_off, to;
	sector_t iblock;
	struct page *page;

	for (; idx < idx_end; idx += 1, from = 0) {
		page_off = (loff_t)idx << PAGE_SHIFT;
		to = (page_off + PAGE_SIZE) > vbo_to ? (vbo_to - page_off)
						     : PAGE_SIZE;
		iblock = page_off >> inode->i_blkbits;

		page = find_or_create_page(mapping, idx,
					   mapping_gfp_constraint(mapping,
								  ~__GFP_FS));
		if (!page)
			return -ENOMEM;

		if (!page_has_buffers(page))
			create_empty_buffers(page, blocksize, 0);

		bh = head = page_buffers(page);
		bh_off = 0;
		do {
			bh_next = bh_off + blocksize;

			if (bh_next <= from || bh_off >= to)
				continue;

			if (!buffer_mapped(bh)) {
				ntfs_get_block(inode, iblock, bh, 0);
				/* Unmapped? It's a hole - nothing to do. */
				if (!buffer_mapped(bh))
					continue;
			}

			/* Ok, it's mapped. Make sure it's up-to-date. */
			if (PageUptodate(page))
				set_buffer_uptodate(bh);

			if (!buffer_uptodate(bh)) {
				lock_buffer(bh);
				bh->b_end_io = end_buffer_read_sync;
				get_bh(bh);
				submit_bh(REQ_OP_READ, bh);

				wait_on_buffer(bh);
				if (!buffer_uptodate(bh)) {
					unlock_page(page);
					put_page(page);
					err = -EIO;
					goto out;
				}
			}

			mark_buffer_dirty(bh);

		} while (bh_off = bh_next, iblock += 1,
			 head != (bh = bh->b_this_page));

		zero_user_segment(page, from, to);

		unlock_page(page);
		put_page(page);
		cond_resched();
	}
out:
	mark_inode_dirty(inode);
	return err;
}

/*
 * ntfs_file_mmap - file_operations::mmap
 */
static int ntfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);
	u64 from = ((u64)vma->vm_pgoff << PAGE_SHIFT);
	bool rw = vma->vm_flags & VM_WRITE;
	int err;

	if (is_encrypted(ni)) {
		ntfs_inode_warn(inode, "mmap encrypted not supported");
		return -EOPNOTSUPP;
	}

	if (is_dedup(ni)) {
		ntfs_inode_warn(inode, "mmap deduplicated not supported");
		return -EOPNOTSUPP;
	}

	if (is_compressed(ni) && rw) {
		ntfs_inode_warn(inode, "mmap(write) compressed not supported");
		return -EOPNOTSUPP;
	}

	if (rw) {
		u64 to = min_t(loff_t, i_size_read(inode),
			       from + vma->vm_end - vma->vm_start);

		if (is_sparsed(ni)) {
			/* Allocate clusters for rw map. */
			struct ntfs_sb_info *sbi = inode->i_sb->s_fs_info;
			CLST lcn, len;
			CLST vcn = from >> sbi->cluster_bits;
			CLST end = bytes_to_cluster(sbi, to);
			bool new;

			for (; vcn < end; vcn += len) {
				err = attr_data_get_block(ni, vcn, 1, &lcn,
							  &len, &new, true);
				if (err)
					goto out;
			}
		}

		if (ni->i_valid < to) {
			if (!inode_trylock(inode)) {
				err = -EAGAIN;
				goto out;
			}
			err = ntfs_extend_initialized_size(file, ni,
							   ni->i_valid, to);
			inode_unlock(inode);
			if (err)
				goto out;
		}
	}

	err = generic_file_mmap(file, vma);
out:
	return err;
}

static int ntfs_extend(struct inode *inode, loff_t pos, size_t count,
		       struct file *file)
{
	struct ntfs_inode *ni = ntfs_i(inode);
	struct address_space *mapping = inode->i_mapping;
	loff_t end = pos + count;
	bool extend_init = file && pos > ni->i_valid;
	int err;

	if (end <= inode->i_size && !extend_init)
		return 0;

	/* Mark rw ntfs as dirty. It will be cleared at umount. */
	ntfs_set_state(ni->mi.sbi, NTFS_DIRTY_DIRTY);

	if (end > inode->i_size) {
		err = ntfs_set_size(inode, end);
		if (err)
			goto out;
	}

	if (extend_init && !is_compressed(ni)) {
		err = ntfs_extend_initialized_size(file, ni, ni->i_valid, pos);
		if (err)
			goto out;
	} else {
		err = 0;
	}

	inode->i_ctime = inode->i_mtime = current_time(inode);
	mark_inode_dirty(inode);

	if (IS_SYNC(inode)) {
		int err2;

		err = filemap_fdatawrite_range(mapping, pos, end - 1);
		err2 = sync_mapping_buffers(mapping);
		if (!err)
			err = err2;
		err2 = write_inode_now(inode, 1);
		if (!err)
			err = err2;
		if (!err)
			err = filemap_fdatawait_range(mapping, pos, end - 1);
	}

out:
	return err;
}

static int ntfs_truncate(struct inode *inode, loff_t new_size)
{
	struct super_block *sb = inode->i_sb;
	struct ntfs_inode *ni = ntfs_i(inode);
	int err, dirty = 0;
	u64 new_valid;

	if (!S_ISREG(inode->i_mode))
		return 0;

	if (is_compressed(ni)) {
		if (ni->i_valid > new_size)
			ni->i_valid = new_size;
	} else {
		err = block_truncate_page(inode->i_mapping, new_size,
					  ntfs_get_block);
		if (err)
			return err;
	}

	new_valid = ntfs_up_block(sb, min_t(u64, ni->i_valid, new_size));

	truncate_setsize(inode, new_size);

	ni_lock(ni);

	down_write(&ni->file.run_lock);
	err = attr_set_size(ni, ATTR_DATA, NULL, 0, &ni->file.run, new_size,
			    &new_valid, ni->mi.sbi->options->prealloc, NULL);
	up_write(&ni->file.run_lock);

	if (new_valid < ni->i_valid)
		ni->i_valid = new_valid;

	ni_unlock(ni);

	ni->std_fa |= FILE_ATTRIBUTE_ARCHIVE;
	inode->i_ctime = inode->i_mtime = current_time(inode);
	if (!IS_DIRSYNC(inode)) {
		dirty = 1;
	} else {
		err = ntfs_sync_inode(inode);
		if (err)
			return err;
	}

	if (dirty)
		mark_inode_dirty(inode);

	/*ntfs_flush_inodes(inode->i_sb, inode, NULL);*/

	return 0;
}

/*
 * ntfs_fallocate
 *
 * Preallocate space for a file. This implements ntfs's fallocate file
 * operation, which gets called from sys_fallocate system call. User
 * space requests 'len' bytes at 'vbo'. If FALLOC_FL_KEEP_SIZE is set
 * we just allocate clusters without zeroing them out. Otherwise we
 * allocate and zero out clusters via an expanding truncate.
 */
static long ntfs_fallocate(struct file *file, int mode, loff_t vbo, loff_t len)
{
	struct inode *inode = file->f_mapping->host;
	struct address_space *mapping = inode->i_mapping;
	struct super_block *sb = inode->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct ntfs_inode *ni = ntfs_i(inode);
	loff_t end = vbo + len;
	loff_t vbo_down = round_down(vbo, max_t(unsigned long,
						sbi->cluster_size, PAGE_SIZE));
	bool is_supported_holes = is_sparsed(ni) || is_compressed(ni);
	loff_t i_size, new_size;
	bool map_locked;
	int err;

	/* No support for dir. */
	if (!S_ISREG(inode->i_mode))
		return -EOPNOTSUPP;

	/*
	 * vfs_fallocate checks all possible combinations of mode.
	 * Do additional checks here before ntfs_set_state(dirty).
	 */
	if (mode & FALLOC_FL_PUNCH_HOLE) {
		if (!is_supported_holes)
			return -EOPNOTSUPP;
	} else if (mode & FALLOC_FL_COLLAPSE_RANGE) {
	} else if (mode & FALLOC_FL_INSERT_RANGE) {
		if (!is_supported_holes)
			return -EOPNOTSUPP;
	} else if (mode &
		   ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE |
		     FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_INSERT_RANGE)) {
		ntfs_inode_warn(inode, "fallocate(0x%x) is not supported",
				mode);
		return -EOPNOTSUPP;
	}

	ntfs_set_state(sbi, NTFS_DIRTY_DIRTY);

	inode_lock(inode);
	i_size = inode->i_size;
	new_size = max(end, i_size);
	map_locked = false;

	if (WARN_ON(ni->ni_flags & NI_FLAG_COMPRESSED_MASK)) {
		/* Should never be here, see ntfs_file_open. */
		err = -EOPNOTSUPP;
		goto out;
	}

	if (mode & (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_COLLAPSE_RANGE |
		    FALLOC_FL_INSERT_RANGE)) {
		inode_dio_wait(inode);
		filemap_invalidate_lock(mapping);
		map_locked = true;
	}

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		u32 frame_size;
		loff_t mask, vbo_a, end_a, tmp;

		err = filemap_write_and_wait_range(mapping, vbo_down,
						   LLONG_MAX);
		if (err)
			goto out;

		truncate_pagecache(inode, vbo_down);

		ni_lock(ni);
		err = attr_punch_hole(ni, vbo, len, &frame_size);
		ni_unlock(ni);
		if (err != E_NTFS_NOTALIGNED)
			goto out;

		/* Process not aligned punch. */
		mask = frame_size - 1;
		vbo_a = (vbo + mask) & ~mask;
		end_a = end & ~mask;

		tmp = min(vbo_a, end);
		if (tmp > vbo) {
			err = ntfs_zero_range(inode, vbo, tmp);
			if (err)
				goto out;
		}

		if (vbo < end_a && end_a < end) {
			err = ntfs_zero_range(inode, end_a, end);
			if (err)
				goto out;
		}

		/* Aligned punch_hole */
		if (end_a > vbo_a) {
			ni_lock(ni);
			err = attr_punch_hole(ni, vbo_a, end_a - vbo_a, NULL);
			ni_unlock(ni);
		}
	} else if (mode & FALLOC_FL_COLLAPSE_RANGE) {
		/*
		 * Write tail of the last page before removed range since
		 * it will get removed from the page cache below.
		 */
		err = filemap_write_and_wait_range(mapping, vbo_down, vbo);
		if (err)
			goto out;

		/*
		 * Write data that will be shifted to preserve them
		 * when discarding page cache below.
		 */
		err = filemap_write_and_wait_range(mapping, end, LLONG_MAX);
		if (err)
			goto out;

		truncate_pagecache(inode, vbo_down);

		ni_lock(ni);
		err = attr_collapse_range(ni, vbo, len);
		ni_unlock(ni);
	} else if (mode & FALLOC_FL_INSERT_RANGE) {
		/* Check new size. */
		err = inode_newsize_ok(inode, new_size);
		if (err)
			goto out;

		/* Write out all dirty pages. */
		err = filemap_write_and_wait_range(mapping, vbo_down,
						   LLONG_MAX);
		if (err)
			goto out;
		truncate_pagecache(inode, vbo_down);

		ni_lock(ni);
		err = attr_insert_range(ni, vbo, len);
		ni_unlock(ni);
	} else {
		/* Check new size. */

		/* generic/213: expected -ENOSPC instead of -EFBIG. */
		if (!is_supported_holes) {
			loff_t to_alloc = new_size - inode_get_bytes(inode);

			if (to_alloc > 0 &&
			    (to_alloc >> sbi->cluster_bits) >
				    wnd_zeroes(&sbi->used.bitmap)) {
				err = -ENOSPC;
				goto out;
			}
		}

		err = inode_newsize_ok(inode, new_size);
		if (err)
			goto out;

		if (new_size > i_size) {
			/*
			 * Allocate clusters, do not change 'valid' size.
			 */
			err = ntfs_set_size(inode, new_size);
			if (err)
				goto out;
		}

		if (is_supported_holes) {
			CLST vcn = vbo >> sbi->cluster_bits;
			CLST cend = bytes_to_cluster(sbi, end);
			CLST cend_v = bytes_to_cluster(sbi, ni->i_valid);
			CLST lcn, clen;
			bool new;

			if (cend_v > cend)
				cend_v = cend;

			/*
			 * Allocate and zero new clusters.
			 * Zeroing these clusters may be too long.
			 */
			for (; vcn < cend_v; vcn += clen) {
				err = attr_data_get_block(ni, vcn, cend_v - vcn,
							  &lcn, &clen, &new,
							  true);
				if (err)
					goto out;
			}
			/*
			 * Allocate but not zero new clusters.
			 */
			for (; vcn < cend; vcn += clen) {
				err = attr_data_get_block(ni, vcn, cend - vcn,
							  &lcn, &clen, &new,
							  false);
				if (err)
					goto out;
			}
		}

		if (mode & FALLOC_FL_KEEP_SIZE) {
			ni_lock(ni);
			/* True - Keep preallocated. */
			err = attr_set_size(ni, ATTR_DATA, NULL, 0,
					    &ni->file.run, i_size, &ni->i_valid,
					    true, NULL);
			ni_unlock(ni);
		} else if (new_size > i_size) {
			inode->i_size = new_size;
		}
	}

out:
	if (map_locked)
		filemap_invalidate_unlock(mapping);

	if (!err) {
		inode->i_ctime = inode->i_mtime = current_time(inode);
		mark_inode_dirty(inode);
	}

	inode_unlock(inode);
	return err;
}

/*
 * ntfs3_setattr - inode_operations::setattr
 */
int ntfs3_setattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		  struct iattr *attr)
{
	struct super_block *sb = dentry->d_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct inode *inode = d_inode(dentry);
	struct ntfs_inode *ni = ntfs_i(inode);
	u32 ia_valid = attr->ia_valid;
	umode_t mode = inode->i_mode;
	int err;

	if (sbi->options->noacsrules) {
		/* "No access rules" - Force any changes of time etc. */
		attr->ia_valid |= ATTR_FORCE;
		/* and disable for editing some attributes. */
		attr->ia_valid &= ~(ATTR_UID | ATTR_GID | ATTR_MODE);
		ia_valid = attr->ia_valid;
	}

	err = setattr_prepare(mnt_userns, dentry, attr);
	if (err)
		goto out;

	if (ia_valid & ATTR_SIZE) {
		loff_t newsize, oldsize;

		if (WARN_ON(ni->ni_flags & NI_FLAG_COMPRESSED_MASK)) {
			/* Should never be here, see ntfs_file_open(). */
			err = -EOPNOTSUPP;
			goto out;
		}
		inode_dio_wait(inode);
		oldsize = inode->i_size;
		newsize = attr->ia_size;

		if (newsize <= oldsize)
			err = ntfs_truncate(inode, newsize);
		else
			err = ntfs_extend(inode, newsize, 0, NULL);

		if (err)
			goto out;

		ni->ni_flags |= NI_FLAG_UPDATE_PARENT;
		inode->i_size = newsize;
	}

	setattr_copy(mnt_userns, inode, attr);

	if (mode != inode->i_mode) {
		err = ntfs_acl_chmod(mnt_userns, dentry);
		if (err)
			goto out;

		/* Linux 'w' -> Windows 'ro'. */
		if (0222 & inode->i_mode)
			ni->std_fa &= ~FILE_ATTRIBUTE_READONLY;
		else
			ni->std_fa |= FILE_ATTRIBUTE_READONLY;
	}

	if (ia_valid & (ATTR_UID | ATTR_GID | ATTR_MODE))
		ntfs_save_wsl_perm(inode);
	mark_inode_dirty(inode);
out:
	return err;
}

static ssize_t ntfs_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);

	if (is_encrypted(ni)) {
		ntfs_inode_warn(inode, "encrypted i/o not supported");
		return -EOPNOTSUPP;
	}

	if (is_compressed(ni) && (iocb->ki_flags & IOCB_DIRECT)) {
		ntfs_inode_warn(inode, "direct i/o + compressed not supported");
		return -EOPNOTSUPP;
	}

#ifndef CONFIG_NTFS3_LZX_XPRESS
	if (ni->ni_flags & NI_FLAG_COMPRESSED_MASK) {
		ntfs_inode_warn(
			inode,
			"activate CONFIG_NTFS3_LZX_XPRESS to read external compressed files");
		return -EOPNOTSUPP;
	}
#endif

	if (is_dedup(ni)) {
		ntfs_inode_warn(inode, "read deduplicated not supported");
		return -EOPNOTSUPP;
	}

	return generic_file_read_iter(iocb, iter);
}

/*
 * ntfs_get_frame_pages
 *
 * Return: Array of locked pages.
 */
static int ntfs_get_frame_pages(struct address_space *mapping, pgoff_t index,
				struct page **pages, u32 pages_per_frame,
				bool *frame_uptodate)
{
	gfp_t gfp_mask = mapping_gfp_mask(mapping);
	u32 npages;

	*frame_uptodate = true;

	for (npages = 0; npages < pages_per_frame; npages++, index++) {
		struct page *page;

		page = find_or_create_page(mapping, index, gfp_mask);
		if (!page) {
			while (npages--) {
				page = pages[npages];
				unlock_page(page);
				put_page(page);
			}

			return -ENOMEM;
		}

		if (!PageUptodate(page))
			*frame_uptodate = false;

		pages[npages] = page;
	}

	return 0;
}

/*
 * ntfs_compress_write - Helper for ntfs_file_write_iter() (compressed files).
 */
static ssize_t ntfs_compress_write(struct kiocb *iocb, struct iov_iter *from)
{
	int err;
	struct file *file = iocb->ki_filp;
	size_t count = iov_iter_count(from);
	loff_t pos = iocb->ki_pos;
	struct inode *inode = file_inode(file);
	loff_t i_size = inode->i_size;
	struct address_space *mapping = inode->i_mapping;
	struct ntfs_inode *ni = ntfs_i(inode);
	u64 valid = ni->i_valid;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct page *page, **pages = NULL;
	size_t written = 0;
	u8 frame_bits = NTFS_LZNT_CUNIT + sbi->cluster_bits;
	u32 frame_size = 1u << frame_bits;
	u32 pages_per_frame = frame_size >> PAGE_SHIFT;
	u32 ip, off;
	CLST frame;
	u64 frame_vbo;
	pgoff_t index;
	bool frame_uptodate;

	if (frame_size < PAGE_SIZE) {
		/*
		 * frame_size == 8K if cluster 512
		 * frame_size == 64K if cluster 4096
		 */
		ntfs_inode_warn(inode, "page size is bigger than frame size");
		return -EOPNOTSUPP;
	}

	pages = kmalloc_array(pages_per_frame, sizeof(struct page *), GFP_NOFS);
	if (!pages)
		return -ENOMEM;

	current->backing_dev_info = inode_to_bdi(inode);
	err = file_remove_privs(file);
	if (err)
		goto out;

	err = file_update_time(file);
	if (err)
		goto out;

	/* Zero range [valid : pos). */
	while (valid < pos) {
		CLST lcn, clen;

		frame = valid >> frame_bits;
		frame_vbo = valid & ~(frame_size - 1);
		off = valid & (frame_size - 1);

		err = attr_data_get_block(ni, frame << NTFS_LZNT_CUNIT, 1, &lcn,
					  &clen, NULL, false);
		if (err)
			goto out;

		if (lcn == SPARSE_LCN) {
			ni->i_valid = valid =
				frame_vbo + ((u64)clen << sbi->cluster_bits);
			continue;
		}

		/* Load full frame. */
		err = ntfs_get_frame_pages(mapping, frame_vbo >> PAGE_SHIFT,
					   pages, pages_per_frame,
					   &frame_uptodate);
		if (err)
			goto out;

		if (!frame_uptodate && off) {
			err = ni_read_frame(ni, frame_vbo, pages,
					    pages_per_frame);
			if (err) {
				for (ip = 0; ip < pages_per_frame; ip++) {
					page = pages[ip];
					unlock_page(page);
					put_page(page);
				}
				goto out;
			}
		}

		ip = off >> PAGE_SHIFT;
		off = offset_in_page(valid);
		for (; ip < pages_per_frame; ip++, off = 0) {
			page = pages[ip];
			zero_user_segment(page, off, PAGE_SIZE);
			flush_dcache_page(page);
			SetPageUptodate(page);
		}

		ni_lock(ni);
		err = ni_write_frame(ni, pages, pages_per_frame);
		ni_unlock(ni);

		for (ip = 0; ip < pages_per_frame; ip++) {
			page = pages[ip];
			SetPageUptodate(page);
			unlock_page(page);
			put_page(page);
		}

		if (err)
			goto out;

		ni->i_valid = valid = frame_vbo + frame_size;
	}

	/* Copy user data [pos : pos + count). */
	while (count) {
		size_t copied, bytes;

		off = pos & (frame_size - 1);
		bytes = frame_size - off;
		if (bytes > count)
			bytes = count;

		frame_vbo = pos & ~(frame_size - 1);
		index = frame_vbo >> PAGE_SHIFT;

		if (unlikely(fault_in_iov_iter_readable(from, bytes))) {
			err = -EFAULT;
			goto out;
		}

		/* Load full frame. */
		err = ntfs_get_frame_pages(mapping, index, pages,
					   pages_per_frame, &frame_uptodate);
		if (err)
			goto out;

		if (!frame_uptodate) {
			loff_t to = pos + bytes;

			if (off || (to < i_size && (to & (frame_size - 1)))) {
				err = ni_read_frame(ni, frame_vbo, pages,
						    pages_per_frame);
				if (err) {
					for (ip = 0; ip < pages_per_frame;
					     ip++) {
						page = pages[ip];
						unlock_page(page);
						put_page(page);
					}
					goto out;
				}
			}
		}

		WARN_ON(!bytes);
		copied = 0;
		ip = off >> PAGE_SHIFT;
		off = offset_in_page(pos);

		/* Copy user data to pages. */
		for (;;) {
			size_t cp, tail = PAGE_SIZE - off;

			page = pages[ip];
			cp = copy_page_from_iter_atomic(page, off,
							min(tail, bytes), from);
			flush_dcache_page(page);

			copied += cp;
			bytes -= cp;
			if (!bytes || !cp)
				break;

			if (cp < tail) {
				off += cp;
			} else {
				ip++;
				off = 0;
			}
		}

		ni_lock(ni);
		err = ni_write_frame(ni, pages, pages_per_frame);
		ni_unlock(ni);

		for (ip = 0; ip < pages_per_frame; ip++) {
			page = pages[ip];
			ClearPageDirty(page);
			SetPageUptodate(page);
			unlock_page(page);
			put_page(page);
		}

		if (err)
			goto out;

		/*
		 * We can loop for a long time in here. Be nice and allow
		 * us to schedule out to avoid softlocking if preempt
		 * is disabled.
		 */
		cond_resched();

		pos += copied;
		written += copied;

		count = iov_iter_count(from);
	}

out:
	kfree(pages);

	current->backing_dev_info = NULL;

	if (err < 0)
		return err;

	iocb->ki_pos += written;
	if (iocb->ki_pos > ni->i_valid)
		ni->i_valid = iocb->ki_pos;

	return written;
}

/*
 * ntfs_file_write_iter - file_operations::write_iter
 */
static ssize_t ntfs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	ssize_t ret;
	struct ntfs_inode *ni = ntfs_i(inode);

	if (is_encrypted(ni)) {
		ntfs_inode_warn(inode, "encrypted i/o not supported");
		return -EOPNOTSUPP;
	}

	if (is_compressed(ni) && (iocb->ki_flags & IOCB_DIRECT)) {
		ntfs_inode_warn(inode, "direct i/o + compressed not supported");
		return -EOPNOTSUPP;
	}

	if (is_dedup(ni)) {
		ntfs_inode_warn(inode, "write into deduplicated not supported");
		return -EOPNOTSUPP;
	}

	if (!inode_trylock(inode)) {
		if (iocb->ki_flags & IOCB_NOWAIT)
			return -EAGAIN;
		inode_lock(inode);
	}

	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		goto out;

	if (WARN_ON(ni->ni_flags & NI_FLAG_COMPRESSED_MASK)) {
		/* Should never be here, see ntfs_file_open(). */
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = ntfs_extend(inode, iocb->ki_pos, ret, file);
	if (ret)
		goto out;

	ret = is_compressed(ni) ? ntfs_compress_write(iocb, from)
				: __generic_file_write_iter(iocb, from);

out:
	inode_unlock(inode);

	if (ret > 0)
		ret = generic_write_sync(iocb, ret);

	return ret;
}

/*
 * ntfs_file_open - file_operations::open
 */
int ntfs_file_open(struct inode *inode, struct file *file)
{
	struct ntfs_inode *ni = ntfs_i(inode);

	if (unlikely((is_compressed(ni) || is_encrypted(ni)) &&
		     (file->f_flags & O_DIRECT))) {
		return -EOPNOTSUPP;
	}

	/* Decompress "external compressed" file if opened for rw. */
	if ((ni->ni_flags & NI_FLAG_COMPRESSED_MASK) &&
	    (file->f_flags & (O_WRONLY | O_RDWR | O_TRUNC))) {
#ifdef CONFIG_NTFS3_LZX_XPRESS
		int err = ni_decompress_file(ni);

		if (err)
			return err;
#else
		ntfs_inode_warn(
			inode,
			"activate CONFIG_NTFS3_LZX_XPRESS to write external compressed files");
		return -EOPNOTSUPP;
#endif
	}

	return generic_file_open(inode, file);
}

/*
 * ntfs_file_release - file_operations::release
 */
static int ntfs_file_release(struct inode *inode, struct file *file)
{
	struct ntfs_inode *ni = ntfs_i(inode);
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	int err = 0;

	/* If we are last writer on the inode, drop the block reservation. */
	if (sbi->options->prealloc && ((file->f_mode & FMODE_WRITE) &&
				      atomic_read(&inode->i_writecount) == 1)) {
		ni_lock(ni);
		down_write(&ni->file.run_lock);

		err = attr_set_size(ni, ATTR_DATA, NULL, 0, &ni->file.run,
				    inode->i_size, &ni->i_valid, false, NULL);

		up_write(&ni->file.run_lock);
		ni_unlock(ni);
	}
	return err;
}

/*
 * ntfs_fiemap - file_operations::fiemap
 */
int ntfs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		__u64 start, __u64 len)
{
	int err;
	struct ntfs_inode *ni = ntfs_i(inode);

	err = fiemap_prep(inode, fieinfo, start, &len, ~FIEMAP_FLAG_XATTR);
	if (err)
		return err;

	ni_lock(ni);

	err = ni_fiemap(ni, fieinfo, start, len);

	ni_unlock(ni);

	return err;
}

// clang-format off
const struct inode_operations ntfs_file_inode_operations = {
	.getattr	= ntfs_getattr,
	.setattr	= ntfs3_setattr,
	.listxattr	= ntfs_listxattr,
	.permission	= ntfs_permission,
	.get_inode_acl	= ntfs_get_acl,
	.set_acl	= ntfs_set_acl,
	.fiemap		= ntfs_fiemap,
};

const struct file_operations ntfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= ntfs_file_read_iter,
	.write_iter	= ntfs_file_write_iter,
	.unlocked_ioctl = ntfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ntfs_compat_ioctl,
#endif
	.splice_read	= generic_file_splice_read,
	.mmap		= ntfs_file_mmap,
	.open		= ntfs_file_open,
	.fsync		= generic_file_fsync,
	.splice_write	= iter_file_splice_write,
	.fallocate	= ntfs_fallocate,
	.release	= ntfs_file_release,
};
// clang-format on
