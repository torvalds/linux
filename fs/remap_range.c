// SPDX-License-Identifier: GPL-2.0-only
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/sched/xacct.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/fsnotify.h>
#include <linux/security.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/splice.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include "internal.h"

#include <linux/uaccess.h>
#include <asm/unistd.h>

/*
 * Performs necessary checks before doing a clone.
 *
 * Can adjust amount of bytes to clone via @req_count argument.
 * Returns appropriate error code that caller should return or
 * zero in case the clone should be allowed.
 */
static int generic_remap_checks(struct file *file_in, loff_t pos_in,
				struct file *file_out, loff_t pos_out,
				loff_t *req_count, unsigned int remap_flags)
{
	struct inode *inode_in = file_in->f_mapping->host;
	struct inode *inode_out = file_out->f_mapping->host;
	uint64_t count = *req_count;
	uint64_t bcount;
	loff_t size_in, size_out;
	loff_t bs = inode_out->i_sb->s_blocksize;
	int ret;

	/* The start of both ranges must be aligned to an fs block. */
	if (!IS_ALIGNED(pos_in, bs) || !IS_ALIGNED(pos_out, bs))
		return -EINVAL;

	/* Ensure offsets don't wrap. */
	if (pos_in + count < pos_in || pos_out + count < pos_out)
		return -EINVAL;

	size_in = i_size_read(inode_in);
	size_out = i_size_read(inode_out);

	/* Dedupe requires both ranges to be within EOF. */
	if ((remap_flags & REMAP_FILE_DEDUP) &&
	    (pos_in >= size_in || pos_in + count > size_in ||
	     pos_out >= size_out || pos_out + count > size_out))
		return -EINVAL;

	/* Ensure the infile range is within the infile. */
	if (pos_in >= size_in)
		return -EINVAL;
	count = min(count, size_in - (uint64_t)pos_in);

	ret = generic_write_check_limits(file_out, pos_out, &count);
	if (ret)
		return ret;

	/*
	 * If the user wanted us to link to the infile's EOF, round up to the
	 * next block boundary for this check.
	 *
	 * Otherwise, make sure the count is also block-aligned, having
	 * already confirmed the starting offsets' block alignment.
	 */
	if (pos_in + count == size_in) {
		bcount = ALIGN(size_in, bs) - pos_in;
	} else {
		if (!IS_ALIGNED(count, bs))
			count = ALIGN_DOWN(count, bs);
		bcount = count;
	}

	/* Don't allow overlapped cloning within the same file. */
	if (inode_in == inode_out &&
	    pos_out + bcount > pos_in &&
	    pos_out < pos_in + bcount)
		return -EINVAL;

	/*
	 * We shortened the request but the caller can't deal with that, so
	 * bounce the request back to userspace.
	 */
	if (*req_count != count && !(remap_flags & REMAP_FILE_CAN_SHORTEN))
		return -EINVAL;

	*req_count = count;
	return 0;
}

static int remap_verify_area(struct file *file, loff_t pos, loff_t len,
			     bool write)
{
	struct inode *inode = file_inode(file);

	if (unlikely(pos < 0 || len < 0))
		return -EINVAL;

	if (unlikely((loff_t) (pos + len) < 0))
		return -EINVAL;

	if (unlikely(inode->i_flctx && mandatory_lock(inode))) {
		loff_t end = len ? pos + len - 1 : OFFSET_MAX;
		int retval;

		retval = locks_mandatory_area(inode, file, pos, end,
				write ? F_WRLCK : F_RDLCK);
		if (retval < 0)
			return retval;
	}

	return security_file_permission(file, write ? MAY_WRITE : MAY_READ);
}

/*
 * Ensure that we don't remap a partial EOF block in the middle of something
 * else.  Assume that the offsets have already been checked for block
 * alignment.
 *
 * For clone we only link a partial EOF block above or at the destination file's
 * EOF.  For deduplication we accept a partial EOF block only if it ends at the
 * destination file's EOF (can not link it into the middle of a file).
 *
 * Shorten the request if possible.
 */
static int generic_remap_check_len(struct inode *inode_in,
				   struct inode *inode_out,
				   loff_t pos_out,
				   loff_t *len,
				   unsigned int remap_flags)
{
	u64 blkmask = i_blocksize(inode_in) - 1;
	loff_t new_len = *len;

	if ((*len & blkmask) == 0)
		return 0;

	if (pos_out + *len < i_size_read(inode_out))
		new_len &= ~blkmask;

	if (new_len == *len)
		return 0;

	if (remap_flags & REMAP_FILE_CAN_SHORTEN) {
		*len = new_len;
		return 0;
	}

	return (remap_flags & REMAP_FILE_DEDUP) ? -EBADE : -EINVAL;
}

/* Read a page's worth of file data into the page cache. */
static struct page *vfs_dedupe_get_page(struct inode *inode, loff_t offset)
{
	struct page *page;

	page = read_mapping_page(inode->i_mapping, offset >> PAGE_SHIFT, NULL);
	if (IS_ERR(page))
		return page;
	if (!PageUptodate(page)) {
		put_page(page);
		return ERR_PTR(-EIO);
	}
	return page;
}

/*
 * Lock two pages, ensuring that we lock in offset order if the pages are from
 * the same file.
 */
static void vfs_lock_two_pages(struct page *page1, struct page *page2)
{
	/* Always lock in order of increasing index. */
	if (page1->index > page2->index)
		swap(page1, page2);

	lock_page(page1);
	if (page1 != page2)
		lock_page(page2);
}

/* Unlock two pages, being careful not to unlock the same page twice. */
static void vfs_unlock_two_pages(struct page *page1, struct page *page2)
{
	unlock_page(page1);
	if (page1 != page2)
		unlock_page(page2);
}

/*
 * Compare extents of two files to see if they are the same.
 * Caller must have locked both inodes to prevent write races.
 */
static int vfs_dedupe_file_range_compare(struct inode *src, loff_t srcoff,
					 struct inode *dest, loff_t destoff,
					 loff_t len, bool *is_same)
{
	loff_t src_poff;
	loff_t dest_poff;
	void *src_addr;
	void *dest_addr;
	struct page *src_page;
	struct page *dest_page;
	loff_t cmp_len;
	bool same;
	int error;

	error = -EINVAL;
	same = true;
	while (len) {
		src_poff = srcoff & (PAGE_SIZE - 1);
		dest_poff = destoff & (PAGE_SIZE - 1);
		cmp_len = min(PAGE_SIZE - src_poff,
			      PAGE_SIZE - dest_poff);
		cmp_len = min(cmp_len, len);
		if (cmp_len <= 0)
			goto out_error;

		src_page = vfs_dedupe_get_page(src, srcoff);
		if (IS_ERR(src_page)) {
			error = PTR_ERR(src_page);
			goto out_error;
		}
		dest_page = vfs_dedupe_get_page(dest, destoff);
		if (IS_ERR(dest_page)) {
			error = PTR_ERR(dest_page);
			put_page(src_page);
			goto out_error;
		}

		vfs_lock_two_pages(src_page, dest_page);

		/*
		 * Now that we've locked both pages, make sure they're still
		 * mapped to the file data we're interested in.  If not,
		 * someone is invalidating pages on us and we lose.
		 */
		if (!PageUptodate(src_page) || !PageUptodate(dest_page) ||
		    src_page->mapping != src->i_mapping ||
		    dest_page->mapping != dest->i_mapping) {
			same = false;
			goto unlock;
		}

		src_addr = kmap_atomic(src_page);
		dest_addr = kmap_atomic(dest_page);

		flush_dcache_page(src_page);
		flush_dcache_page(dest_page);

		if (memcmp(src_addr + src_poff, dest_addr + dest_poff, cmp_len))
			same = false;

		kunmap_atomic(dest_addr);
		kunmap_atomic(src_addr);
unlock:
		vfs_unlock_two_pages(src_page, dest_page);
		put_page(dest_page);
		put_page(src_page);

		if (!same)
			break;

		srcoff += cmp_len;
		destoff += cmp_len;
		len -= cmp_len;
	}

	*is_same = same;
	return 0;

out_error:
	return error;
}

/*
 * Check that the two inodes are eligible for cloning, the ranges make
 * sense, and then flush all dirty data.  Caller must ensure that the
 * inodes have been locked against any other modifications.
 *
 * If there's an error, then the usual negative error code is returned.
 * Otherwise returns 0 with *len set to the request length.
 */
int generic_remap_file_range_prep(struct file *file_in, loff_t pos_in,
				  struct file *file_out, loff_t pos_out,
				  loff_t *len, unsigned int remap_flags)
{
	struct inode *inode_in = file_inode(file_in);
	struct inode *inode_out = file_inode(file_out);
	bool same_inode = (inode_in == inode_out);
	int ret;

	/* Don't touch certain kinds of inodes */
	if (IS_IMMUTABLE(inode_out))
		return -EPERM;

	if (IS_SWAPFILE(inode_in) || IS_SWAPFILE(inode_out))
		return -ETXTBSY;

	/* Don't reflink dirs, pipes, sockets... */
	if (S_ISDIR(inode_in->i_mode) || S_ISDIR(inode_out->i_mode))
		return -EISDIR;
	if (!S_ISREG(inode_in->i_mode) || !S_ISREG(inode_out->i_mode))
		return -EINVAL;

	/* Zero length dedupe exits immediately; reflink goes to EOF. */
	if (*len == 0) {
		loff_t isize = i_size_read(inode_in);

		if ((remap_flags & REMAP_FILE_DEDUP) || pos_in == isize)
			return 0;
		if (pos_in > isize)
			return -EINVAL;
		*len = isize - pos_in;
		if (*len == 0)
			return 0;
	}

	/* Check that we don't violate system file offset limits. */
	ret = generic_remap_checks(file_in, pos_in, file_out, pos_out, len,
			remap_flags);
	if (ret)
		return ret;

	/* Wait for the completion of any pending IOs on both files */
	inode_dio_wait(inode_in);
	if (!same_inode)
		inode_dio_wait(inode_out);

	ret = filemap_write_and_wait_range(inode_in->i_mapping,
			pos_in, pos_in + *len - 1);
	if (ret)
		return ret;

	ret = filemap_write_and_wait_range(inode_out->i_mapping,
			pos_out, pos_out + *len - 1);
	if (ret)
		return ret;

	/*
	 * Check that the extents are the same.
	 */
	if (remap_flags & REMAP_FILE_DEDUP) {
		bool		is_same = false;

		ret = vfs_dedupe_file_range_compare(inode_in, pos_in,
				inode_out, pos_out, *len, &is_same);
		if (ret)
			return ret;
		if (!is_same)
			return -EBADE;
	}

	ret = generic_remap_check_len(inode_in, inode_out, pos_out, len,
			remap_flags);
	if (ret)
		return ret;

	/* If can't alter the file contents, we're done. */
	if (!(remap_flags & REMAP_FILE_DEDUP))
		ret = file_modified(file_out);

	return ret;
}
EXPORT_SYMBOL(generic_remap_file_range_prep);

loff_t do_clone_file_range(struct file *file_in, loff_t pos_in,
			   struct file *file_out, loff_t pos_out,
			   loff_t len, unsigned int remap_flags)
{
	loff_t ret;

	WARN_ON_ONCE(remap_flags & REMAP_FILE_DEDUP);

	/*
	 * FICLONE/FICLONERANGE ioctls enforce that src and dest files are on
	 * the same mount. Practically, they only need to be on the same file
	 * system.
	 */
	if (file_inode(file_in)->i_sb != file_inode(file_out)->i_sb)
		return -EXDEV;

	ret = generic_file_rw_checks(file_in, file_out);
	if (ret < 0)
		return ret;

	if (!file_in->f_op->remap_file_range)
		return -EOPNOTSUPP;

	ret = remap_verify_area(file_in, pos_in, len, false);
	if (ret)
		return ret;

	ret = remap_verify_area(file_out, pos_out, len, true);
	if (ret)
		return ret;

	ret = file_in->f_op->remap_file_range(file_in, pos_in,
			file_out, pos_out, len, remap_flags);
	if (ret < 0)
		return ret;

	fsnotify_access(file_in);
	fsnotify_modify(file_out);
	return ret;
}
EXPORT_SYMBOL(do_clone_file_range);

loff_t vfs_clone_file_range(struct file *file_in, loff_t pos_in,
			    struct file *file_out, loff_t pos_out,
			    loff_t len, unsigned int remap_flags)
{
	loff_t ret;

	file_start_write(file_out);
	ret = do_clone_file_range(file_in, pos_in, file_out, pos_out, len,
				  remap_flags);
	file_end_write(file_out);

	return ret;
}
EXPORT_SYMBOL(vfs_clone_file_range);

/* Check whether we are allowed to dedupe the destination file */
static bool allow_file_dedupe(struct file *file)
{
	if (capable(CAP_SYS_ADMIN))
		return true;
	if (file->f_mode & FMODE_WRITE)
		return true;
	if (uid_eq(current_fsuid(), file_inode(file)->i_uid))
		return true;
	if (!inode_permission(file_inode(file), MAY_WRITE))
		return true;
	return false;
}

loff_t vfs_dedupe_file_range_one(struct file *src_file, loff_t src_pos,
				 struct file *dst_file, loff_t dst_pos,
				 loff_t len, unsigned int remap_flags)
{
	loff_t ret;

	WARN_ON_ONCE(remap_flags & ~(REMAP_FILE_DEDUP |
				     REMAP_FILE_CAN_SHORTEN));

	ret = mnt_want_write_file(dst_file);
	if (ret)
		return ret;

	ret = remap_verify_area(dst_file, dst_pos, len, true);
	if (ret < 0)
		goto out_drop_write;

	ret = -EPERM;
	if (!allow_file_dedupe(dst_file))
		goto out_drop_write;

	ret = -EXDEV;
	if (src_file->f_path.mnt != dst_file->f_path.mnt)
		goto out_drop_write;

	ret = -EISDIR;
	if (S_ISDIR(file_inode(dst_file)->i_mode))
		goto out_drop_write;

	ret = -EINVAL;
	if (!dst_file->f_op->remap_file_range)
		goto out_drop_write;

	if (len == 0) {
		ret = 0;
		goto out_drop_write;
	}

	ret = dst_file->f_op->remap_file_range(src_file, src_pos, dst_file,
			dst_pos, len, remap_flags | REMAP_FILE_DEDUP);
out_drop_write:
	mnt_drop_write_file(dst_file);

	return ret;
}
EXPORT_SYMBOL(vfs_dedupe_file_range_one);

int vfs_dedupe_file_range(struct file *file, struct file_dedupe_range *same)
{
	struct file_dedupe_range_info *info;
	struct inode *src = file_inode(file);
	u64 off;
	u64 len;
	int i;
	int ret;
	u16 count = same->dest_count;
	loff_t deduped;

	if (!(file->f_mode & FMODE_READ))
		return -EINVAL;

	if (same->reserved1 || same->reserved2)
		return -EINVAL;

	off = same->src_offset;
	len = same->src_length;

	if (S_ISDIR(src->i_mode))
		return -EISDIR;

	if (!S_ISREG(src->i_mode))
		return -EINVAL;

	if (!file->f_op->remap_file_range)
		return -EOPNOTSUPP;

	ret = remap_verify_area(file, off, len, false);
	if (ret < 0)
		return ret;
	ret = 0;

	if (off + len > i_size_read(src))
		return -EINVAL;

	/* Arbitrary 1G limit on a single dedupe request, can be raised. */
	len = min_t(u64, len, 1 << 30);

	/* pre-format output fields to sane values */
	for (i = 0; i < count; i++) {
		same->info[i].bytes_deduped = 0ULL;
		same->info[i].status = FILE_DEDUPE_RANGE_SAME;
	}

	for (i = 0, info = same->info; i < count; i++, info++) {
		struct fd dst_fd = fdget(info->dest_fd);
		struct file *dst_file = dst_fd.file;

		if (!dst_file) {
			info->status = -EBADF;
			goto next_loop;
		}

		if (info->reserved) {
			info->status = -EINVAL;
			goto next_fdput;
		}

		deduped = vfs_dedupe_file_range_one(file, off, dst_file,
						    info->dest_offset, len,
						    REMAP_FILE_CAN_SHORTEN);
		if (deduped == -EBADE)
			info->status = FILE_DEDUPE_RANGE_DIFFERS;
		else if (deduped < 0)
			info->status = deduped;
		else
			info->bytes_deduped = len;

next_fdput:
		fdput(dst_fd);
next_loop:
		if (fatal_signal_pending(current))
			break;
	}
	return ret;
}
EXPORT_SYMBOL(vfs_dedupe_file_range);
