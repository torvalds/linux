/*
 *  linux/fs/ext4/file.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext4 fs regular file handling primitives
 *
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 *	(jj@sunsite.ms.mff.cuni.cz)
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/path.h>
#include <linux/dax.h>
#include <linux/quotaops.h>
#include <linux/pagevec.h>
#include <linux/uio.h>
#include "ext4.h"
#include "ext4_jbd2.h"
#include "xattr.h"
#include "acl.h"

#ifdef CONFIG_FS_DAX
static ssize_t ext4_dax_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	ssize_t ret;

	inode_lock_shared(inode);
	/*
	 * Recheck under inode lock - at this point we are sure it cannot
	 * change anymore
	 */
	if (!IS_DAX(inode)) {
		inode_unlock_shared(inode);
		/* Fallback to buffered IO in case we cannot support DAX */
		return generic_file_read_iter(iocb, to);
	}
	ret = dax_iomap_rw(iocb, to, &ext4_iomap_ops);
	inode_unlock_shared(inode);

	file_accessed(iocb->ki_filp);
	return ret;
}
#endif

static ssize_t ext4_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	if (unlikely(ext4_forced_shutdown(EXT4_SB(file_inode(iocb->ki_filp)->i_sb))))
		return -EIO;

	if (!iov_iter_count(to))
		return 0; /* skip atime */

#ifdef CONFIG_FS_DAX
	if (IS_DAX(file_inode(iocb->ki_filp)))
		return ext4_dax_read_iter(iocb, to);
#endif
	return generic_file_read_iter(iocb, to);
}

/*
 * Called when an inode is released. Note that this is different
 * from ext4_file_open: open gets called at every open, but release
 * gets called only when /all/ the files are closed.
 */
static int ext4_release_file(struct inode *inode, struct file *filp)
{
	if (ext4_test_inode_state(inode, EXT4_STATE_DA_ALLOC_CLOSE)) {
		ext4_alloc_da_blocks(inode);
		ext4_clear_inode_state(inode, EXT4_STATE_DA_ALLOC_CLOSE);
	}
	/* if we are the last writer on the inode, drop the block reservation */
	if ((filp->f_mode & FMODE_WRITE) &&
			(atomic_read(&inode->i_writecount) == 1) &&
		        !EXT4_I(inode)->i_reserved_data_blocks)
	{
		down_write(&EXT4_I(inode)->i_data_sem);
		ext4_discard_preallocations(inode);
		up_write(&EXT4_I(inode)->i_data_sem);
	}
	if (is_dx(inode) && filp->private_data)
		ext4_htree_free_dir_info(filp->private_data);

	return 0;
}

static void ext4_unwritten_wait(struct inode *inode)
{
	wait_queue_head_t *wq = ext4_ioend_wq(inode);

	wait_event(*wq, (atomic_read(&EXT4_I(inode)->i_unwritten) == 0));
}

/*
 * This tests whether the IO in question is block-aligned or not.
 * Ext4 utilizes unwritten extents when hole-filling during direct IO, and they
 * are converted to written only after the IO is complete.  Until they are
 * mapped, these blocks appear as holes, so dio_zero_block() will assume that
 * it needs to zero out portions of the start and/or end block.  If 2 AIO
 * threads are at work on the same unwritten block, they must be synchronized
 * or one thread will zero the other's data, causing corruption.
 */
static int
ext4_unaligned_aio(struct inode *inode, struct iov_iter *from, loff_t pos)
{
	struct super_block *sb = inode->i_sb;
	int blockmask = sb->s_blocksize - 1;

	if (pos >= i_size_read(inode))
		return 0;

	if ((pos | iov_iter_alignment(from)) & blockmask)
		return 1;

	return 0;
}

/* Is IO overwriting allocated and initialized blocks? */
static bool ext4_overwrite_io(struct inode *inode, loff_t pos, loff_t len)
{
	struct ext4_map_blocks map;
	unsigned int blkbits = inode->i_blkbits;
	int err, blklen;

	if (pos + len > i_size_read(inode))
		return false;

	map.m_lblk = pos >> blkbits;
	map.m_len = EXT4_MAX_BLOCKS(len, pos, blkbits);
	blklen = map.m_len;

	err = ext4_map_blocks(NULL, inode, &map, 0);
	/*
	 * 'err==len' means that all of the blocks have been preallocated,
	 * regardless of whether they have been initialized or not. To exclude
	 * unwritten extents, we need to check m_flags.
	 */
	return err == blklen && (map.m_flags & EXT4_MAP_MAPPED);
}

static ssize_t ext4_write_checks(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	ssize_t ret;

	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		return ret;
	/*
	 * If we have encountered a bitmap-format file, the size limit
	 * is smaller than s_maxbytes, which is for extent-mapped files.
	 */
	if (!(ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS))) {
		struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);

		if (iocb->ki_pos >= sbi->s_bitmap_maxbytes)
			return -EFBIG;
		iov_iter_truncate(from, sbi->s_bitmap_maxbytes - iocb->ki_pos);
	}
	return iov_iter_count(from);
}

#ifdef CONFIG_FS_DAX
static ssize_t
ext4_dax_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	ssize_t ret;

	inode_lock(inode);
	ret = ext4_write_checks(iocb, from);
	if (ret <= 0)
		goto out;
	ret = file_remove_privs(iocb->ki_filp);
	if (ret)
		goto out;
	ret = file_update_time(iocb->ki_filp);
	if (ret)
		goto out;

	ret = dax_iomap_rw(iocb, from, &ext4_iomap_ops);
out:
	inode_unlock(inode);
	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
	return ret;
}
#endif

static ssize_t
ext4_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	int o_direct = iocb->ki_flags & IOCB_DIRECT;
	int unaligned_aio = 0;
	int overwrite = 0;
	ssize_t ret;

	if (unlikely(ext4_forced_shutdown(EXT4_SB(inode->i_sb))))
		return -EIO;

#ifdef CONFIG_FS_DAX
	if (IS_DAX(inode))
		return ext4_dax_write_iter(iocb, from);
#endif

	inode_lock(inode);
	ret = ext4_write_checks(iocb, from);
	if (ret <= 0)
		goto out;

	/*
	 * Unaligned direct AIO must be serialized among each other as zeroing
	 * of partial blocks of two competing unaligned AIOs can result in data
	 * corruption.
	 */
	if (o_direct && ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS) &&
	    !is_sync_kiocb(iocb) &&
	    ext4_unaligned_aio(inode, from, iocb->ki_pos)) {
		unaligned_aio = 1;
		ext4_unwritten_wait(inode);
	}

	iocb->private = &overwrite;
	/* Check whether we do a DIO overwrite or not */
	if (o_direct && ext4_should_dioread_nolock(inode) && !unaligned_aio &&
	    ext4_overwrite_io(inode, iocb->ki_pos, iov_iter_count(from)))
		overwrite = 1;

	ret = __generic_file_write_iter(iocb, from);
	inode_unlock(inode);

	if (ret > 0)
		ret = generic_write_sync(iocb, ret);

	return ret;

out:
	inode_unlock(inode);
	return ret;
}

#ifdef CONFIG_FS_DAX
static int ext4_dax_huge_fault(struct vm_fault *vmf,
		enum page_entry_size pe_size)
{
	int result;
	struct inode *inode = file_inode(vmf->vma->vm_file);
	struct super_block *sb = inode->i_sb;
	bool write = vmf->flags & FAULT_FLAG_WRITE;

	if (write) {
		sb_start_pagefault(sb);
		file_update_time(vmf->vma->vm_file);
	}
	down_read(&EXT4_I(inode)->i_mmap_sem);
	result = dax_iomap_fault(vmf, pe_size, &ext4_iomap_ops);
	up_read(&EXT4_I(inode)->i_mmap_sem);
	if (write)
		sb_end_pagefault(sb);

	return result;
}

static int ext4_dax_fault(struct vm_fault *vmf)
{
	return ext4_dax_huge_fault(vmf, PE_SIZE_PTE);
}

/*
 * Handle write fault for VM_MIXEDMAP mappings. Similarly to ext4_dax_fault()
 * handler we check for races agaist truncate. Note that since we cycle through
 * i_mmap_sem, we are sure that also any hole punching that began before we
 * were called is finished by now and so if it included part of the file we
 * are working on, our pte will get unmapped and the check for pte_same() in
 * wp_pfn_shared() fails. Thus fault gets retried and things work out as
 * desired.
 */
static int ext4_dax_pfn_mkwrite(struct vm_fault *vmf)
{
	struct inode *inode = file_inode(vmf->vma->vm_file);
	struct super_block *sb = inode->i_sb;
	loff_t size;
	int ret;

	sb_start_pagefault(sb);
	file_update_time(vmf->vma->vm_file);
	down_read(&EXT4_I(inode)->i_mmap_sem);
	size = (i_size_read(inode) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (vmf->pgoff >= size)
		ret = VM_FAULT_SIGBUS;
	else
		ret = dax_pfn_mkwrite(vmf);
	up_read(&EXT4_I(inode)->i_mmap_sem);
	sb_end_pagefault(sb);

	return ret;
}

static const struct vm_operations_struct ext4_dax_vm_ops = {
	.fault		= ext4_dax_fault,
	.huge_fault	= ext4_dax_huge_fault,
	.page_mkwrite	= ext4_dax_fault,
	.pfn_mkwrite	= ext4_dax_pfn_mkwrite,
};
#else
#define ext4_dax_vm_ops	ext4_file_vm_ops
#endif

static const struct vm_operations_struct ext4_file_vm_ops = {
	.fault		= ext4_filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite   = ext4_page_mkwrite,
};

static int ext4_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file->f_mapping->host;

	if (unlikely(ext4_forced_shutdown(EXT4_SB(inode->i_sb))))
		return -EIO;

	if (ext4_encrypted_inode(inode)) {
		int err = fscrypt_get_encryption_info(inode);
		if (err)
			return 0;
		if (!fscrypt_has_encryption_key(inode))
			return -ENOKEY;
	}
	file_accessed(file);
	if (IS_DAX(file_inode(file))) {
		vma->vm_ops = &ext4_dax_vm_ops;
		vma->vm_flags |= VM_MIXEDMAP | VM_HUGEPAGE;
	} else {
		vma->vm_ops = &ext4_file_vm_ops;
	}
	return 0;
}

static int ext4_file_open(struct inode * inode, struct file * filp)
{
	struct super_block *sb = inode->i_sb;
	struct ext4_sb_info *sbi = EXT4_SB(inode->i_sb);
	struct vfsmount *mnt = filp->f_path.mnt;
	struct dentry *dir;
	struct path path;
	char buf[64], *cp;
	int ret;

	if (unlikely(ext4_forced_shutdown(EXT4_SB(inode->i_sb))))
		return -EIO;

	if (unlikely(!(sbi->s_mount_flags & EXT4_MF_MNTDIR_SAMPLED) &&
		     !(sb->s_flags & MS_RDONLY))) {
		sbi->s_mount_flags |= EXT4_MF_MNTDIR_SAMPLED;
		/*
		 * Sample where the filesystem has been mounted and
		 * store it in the superblock for sysadmin convenience
		 * when trying to sort through large numbers of block
		 * devices or filesystem images.
		 */
		memset(buf, 0, sizeof(buf));
		path.mnt = mnt;
		path.dentry = mnt->mnt_root;
		cp = d_path(&path, buf, sizeof(buf));
		if (!IS_ERR(cp)) {
			handle_t *handle;
			int err;

			handle = ext4_journal_start_sb(sb, EXT4_HT_MISC, 1);
			if (IS_ERR(handle))
				return PTR_ERR(handle);
			BUFFER_TRACE(sbi->s_sbh, "get_write_access");
			err = ext4_journal_get_write_access(handle, sbi->s_sbh);
			if (err) {
				ext4_journal_stop(handle);
				return err;
			}
			strlcpy(sbi->s_es->s_last_mounted, cp,
				sizeof(sbi->s_es->s_last_mounted));
			ext4_handle_dirty_super(handle, sb);
			ext4_journal_stop(handle);
		}
	}
	if (ext4_encrypted_inode(inode)) {
		ret = fscrypt_get_encryption_info(inode);
		if (ret)
			return -EACCES;
		if (!fscrypt_has_encryption_key(inode))
			return -ENOKEY;
	}

	dir = dget_parent(file_dentry(filp));
	if (ext4_encrypted_inode(d_inode(dir)) &&
			!fscrypt_has_permitted_context(d_inode(dir), inode)) {
		ext4_warning(inode->i_sb,
			     "Inconsistent encryption contexts: %lu/%lu",
			     (unsigned long) d_inode(dir)->i_ino,
			     (unsigned long) inode->i_ino);
		dput(dir);
		return -EPERM;
	}
	dput(dir);
	/*
	 * Set up the jbd2_inode if we are opening the inode for
	 * writing and the journal is present
	 */
	if (filp->f_mode & FMODE_WRITE) {
		ret = ext4_inode_attach_jinode(inode);
		if (ret < 0)
			return ret;
	}
	return dquot_file_open(inode, filp);
}

/*
 * Here we use ext4_map_blocks() to get a block mapping for a extent-based
 * file rather than ext4_ext_walk_space() because we can introduce
 * SEEK_DATA/SEEK_HOLE for block-mapped and extent-mapped file at the same
 * function.  When extent status tree has been fully implemented, it will
 * track all extent status for a file and we can directly use it to
 * retrieve the offset for SEEK_DATA/SEEK_HOLE.
 */

/*
 * When we retrieve the offset for SEEK_DATA/SEEK_HOLE, we would need to
 * lookup page cache to check whether or not there has some data between
 * [startoff, endoff] because, if this range contains an unwritten extent,
 * we determine this extent as a data or a hole according to whether the
 * page cache has data or not.
 */
static int ext4_find_unwritten_pgoff(struct inode *inode,
				     int whence,
				     ext4_lblk_t end_blk,
				     loff_t *offset)
{
	struct pagevec pvec;
	unsigned int blkbits;
	pgoff_t index;
	pgoff_t end;
	loff_t endoff;
	loff_t startoff;
	loff_t lastoff;
	int found = 0;

	blkbits = inode->i_sb->s_blocksize_bits;
	startoff = *offset;
	lastoff = startoff;
	endoff = (loff_t)end_blk << blkbits;

	index = startoff >> PAGE_SHIFT;
	end = endoff >> PAGE_SHIFT;

	pagevec_init(&pvec, 0);
	do {
		int i, num;
		unsigned long nr_pages;

		num = min_t(pgoff_t, end - index, PAGEVEC_SIZE);
		nr_pages = pagevec_lookup(&pvec, inode->i_mapping, index,
					  (pgoff_t)num);
		if (nr_pages == 0) {
			if (whence == SEEK_DATA)
				break;

			BUG_ON(whence != SEEK_HOLE);
			/*
			 * If this is the first time to go into the loop and
			 * offset is not beyond the end offset, it will be a
			 * hole at this offset
			 */
			if (lastoff == startoff || lastoff < endoff)
				found = 1;
			break;
		}

		/*
		 * If this is the first time to go into the loop and
		 * offset is smaller than the first page offset, it will be a
		 * hole at this offset.
		 */
		if (lastoff == startoff && whence == SEEK_HOLE &&
		    lastoff < page_offset(pvec.pages[0])) {
			found = 1;
			break;
		}

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];
			struct buffer_head *bh, *head;

			/*
			 * If the current offset is not beyond the end of given
			 * range, it will be a hole.
			 */
			if (lastoff < endoff && whence == SEEK_HOLE &&
			    page->index > end) {
				found = 1;
				*offset = lastoff;
				goto out;
			}

			lock_page(page);

			if (unlikely(page->mapping != inode->i_mapping)) {
				unlock_page(page);
				continue;
			}

			if (!page_has_buffers(page)) {
				unlock_page(page);
				continue;
			}

			if (page_has_buffers(page)) {
				lastoff = page_offset(page);
				bh = head = page_buffers(page);
				do {
					if (buffer_uptodate(bh) ||
					    buffer_unwritten(bh)) {
						if (whence == SEEK_DATA)
							found = 1;
					} else {
						if (whence == SEEK_HOLE)
							found = 1;
					}
					if (found) {
						*offset = max_t(loff_t,
							startoff, lastoff);
						unlock_page(page);
						goto out;
					}
					lastoff += bh->b_size;
					bh = bh->b_this_page;
				} while (bh != head);
			}

			lastoff = page_offset(page) + PAGE_SIZE;
			unlock_page(page);
		}

		/*
		 * The no. of pages is less than our desired, that would be a
		 * hole in there.
		 */
		if (nr_pages < num && whence == SEEK_HOLE) {
			found = 1;
			*offset = lastoff;
			break;
		}

		index = pvec.pages[i - 1]->index + 1;
		pagevec_release(&pvec);
	} while (index <= end);

out:
	pagevec_release(&pvec);
	return found;
}

/*
 * ext4_seek_data() retrieves the offset for SEEK_DATA.
 */
static loff_t ext4_seek_data(struct file *file, loff_t offset, loff_t maxsize)
{
	struct inode *inode = file->f_mapping->host;
	struct extent_status es;
	ext4_lblk_t start, last, end;
	loff_t dataoff, isize;
	int blkbits;
	int ret;

	inode_lock(inode);

	isize = i_size_read(inode);
	if (offset >= isize) {
		inode_unlock(inode);
		return -ENXIO;
	}

	blkbits = inode->i_sb->s_blocksize_bits;
	start = offset >> blkbits;
	last = start;
	end = isize >> blkbits;
	dataoff = offset;

	do {
		ret = ext4_get_next_extent(inode, last, end - last + 1, &es);
		if (ret <= 0) {
			/* No extent found -> no data */
			if (ret == 0)
				ret = -ENXIO;
			inode_unlock(inode);
			return ret;
		}

		last = es.es_lblk;
		if (last != start)
			dataoff = (loff_t)last << blkbits;
		if (!ext4_es_is_unwritten(&es))
			break;

		/*
		 * If there is a unwritten extent at this offset,
		 * it will be as a data or a hole according to page
		 * cache that has data or not.
		 */
		if (ext4_find_unwritten_pgoff(inode, SEEK_DATA,
					      es.es_lblk + es.es_len, &dataoff))
			break;
		last += es.es_len;
		dataoff = (loff_t)last << blkbits;
		cond_resched();
	} while (last <= end);

	inode_unlock(inode);

	if (dataoff > isize)
		return -ENXIO;

	return vfs_setpos(file, dataoff, maxsize);
}

/*
 * ext4_seek_hole() retrieves the offset for SEEK_HOLE.
 */
static loff_t ext4_seek_hole(struct file *file, loff_t offset, loff_t maxsize)
{
	struct inode *inode = file->f_mapping->host;
	struct extent_status es;
	ext4_lblk_t start, last, end;
	loff_t holeoff, isize;
	int blkbits;
	int ret;

	inode_lock(inode);

	isize = i_size_read(inode);
	if (offset >= isize) {
		inode_unlock(inode);
		return -ENXIO;
	}

	blkbits = inode->i_sb->s_blocksize_bits;
	start = offset >> blkbits;
	last = start;
	end = isize >> blkbits;
	holeoff = offset;

	do {
		ret = ext4_get_next_extent(inode, last, end - last + 1, &es);
		if (ret < 0) {
			inode_unlock(inode);
			return ret;
		}
		/* Found a hole? */
		if (ret == 0 || es.es_lblk > last) {
			if (last != start)
				holeoff = (loff_t)last << blkbits;
			break;
		}
		/*
		 * If there is a unwritten extent at this offset,
		 * it will be as a data or a hole according to page
		 * cache that has data or not.
		 */
		if (ext4_es_is_unwritten(&es) &&
		    ext4_find_unwritten_pgoff(inode, SEEK_HOLE,
					      last + es.es_len, &holeoff))
			break;

		last += es.es_len;
		holeoff = (loff_t)last << blkbits;
		cond_resched();
	} while (last <= end);

	inode_unlock(inode);

	if (holeoff > isize)
		holeoff = isize;

	return vfs_setpos(file, holeoff, maxsize);
}

/*
 * ext4_llseek() handles both block-mapped and extent-mapped maxbytes values
 * by calling generic_file_llseek_size() with the appropriate maxbytes
 * value for each.
 */
loff_t ext4_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;
	loff_t maxbytes;

	if (!(ext4_test_inode_flag(inode, EXT4_INODE_EXTENTS)))
		maxbytes = EXT4_SB(inode->i_sb)->s_bitmap_maxbytes;
	else
		maxbytes = inode->i_sb->s_maxbytes;

	switch (whence) {
	case SEEK_SET:
	case SEEK_CUR:
	case SEEK_END:
		return generic_file_llseek_size(file, offset, whence,
						maxbytes, i_size_read(inode));
	case SEEK_DATA:
		return ext4_seek_data(file, offset, maxbytes);
	case SEEK_HOLE:
		return ext4_seek_hole(file, offset, maxbytes);
	}

	return -EINVAL;
}

const struct file_operations ext4_file_operations = {
	.llseek		= ext4_llseek,
	.read_iter	= ext4_file_read_iter,
	.write_iter	= ext4_file_write_iter,
	.unlocked_ioctl = ext4_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ext4_compat_ioctl,
#endif
	.mmap		= ext4_file_mmap,
	.open		= ext4_file_open,
	.release	= ext4_release_file,
	.fsync		= ext4_sync_file,
	.get_unmapped_area = thp_get_unmapped_area,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= ext4_fallocate,
};

const struct inode_operations ext4_file_inode_operations = {
	.setattr	= ext4_setattr,
	.getattr	= ext4_getattr,
	.listxattr	= ext4_listxattr,
	.get_acl	= ext4_get_acl,
	.set_acl	= ext4_set_acl,
	.fiemap		= ext4_fiemap,
};

