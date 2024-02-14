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
#include <linux/dax.h>
#include <linux/overflow.h>
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
	if (pos_in + count == size_in &&
	    (!(remap_flags & REMAP_FILE_DEDUP) || pos_out + count == size_out)) {
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
	int mask = write ? MAY_WRITE : MAY_READ;
	loff_t tmp;
	int ret;

	if (unlikely(pos < 0 || len < 0))
		return -EINVAL;

	if (unlikely(check_add_overflow(pos, len, &tmp)))
		return -EINVAL;

	ret = security_file_permission(file, mask);
	if (ret)
		return ret;

	return fsnotify_file_area_perm(file, mask, &pos, len);
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
static struct folio *vfs_dedupe_get_folio(struct file *file, loff_t pos)
{
	return read_mapping_folio(file->f_mapping, pos >> PAGE_SHIFT, file);
}

/*
 * Lock two folios, ensuring that we lock in offset order if the folios
 * are from the same file.
 */
static void vfs_lock_two_folios(struct folio *folio1, struct folio *folio2)
{
	/* Always lock in order of increasing index. */
	if (folio1->index > folio2->index)
		swap(folio1, folio2);

	folio_lock(folio1);
	if (folio1 != folio2)
		folio_lock(folio2);
}

/* Unlock two folios, being careful not to unlock the same folio twice. */
static void vfs_unlock_two_folios(struct folio *folio1, struct folio *folio2)
{
	folio_unlock(folio1);
	if (folio1 != folio2)
		folio_unlock(folio2);
}

/*
 * Compare extents of two files to see if they are the same.
 * Caller must have locked both inodes to prevent write races.
 */
static int vfs_dedupe_file_range_compare(struct file *src, loff_t srcoff,
					 struct file *dest, loff_t dstoff,
					 loff_t len, bool *is_same)
{
	bool same = true;
	int error = -EINVAL;

	while (len) {
		struct folio *src_folio, *dst_folio;
		void *src_addr, *dst_addr;
		loff_t cmp_len = min(PAGE_SIZE - offset_in_page(srcoff),
				     PAGE_SIZE - offset_in_page(dstoff));

		cmp_len = min(cmp_len, len);
		if (cmp_len <= 0)
			goto out_error;

		src_folio = vfs_dedupe_get_folio(src, srcoff);
		if (IS_ERR(src_folio)) {
			error = PTR_ERR(src_folio);
			goto out_error;
		}
		dst_folio = vfs_dedupe_get_folio(dest, dstoff);
		if (IS_ERR(dst_folio)) {
			error = PTR_ERR(dst_folio);
			folio_put(src_folio);
			goto out_error;
		}

		vfs_lock_two_folios(src_folio, dst_folio);

		/*
		 * Now that we've locked both folios, make sure they're still
		 * mapped to the file data we're interested in.  If not,
		 * someone is invalidating pages on us and we lose.
		 */
		if (!folio_test_uptodate(src_folio) || !folio_test_uptodate(dst_folio) ||
		    src_folio->mapping != src->f_mapping ||
		    dst_folio->mapping != dest->f_mapping) {
			same = false;
			goto unlock;
		}

		src_addr = kmap_local_folio(src_folio,
					offset_in_folio(src_folio, srcoff));
		dst_addr = kmap_local_folio(dst_folio,
					offset_in_folio(dst_folio, dstoff));

		flush_dcache_folio(src_folio);
		flush_dcache_folio(dst_folio);

		if (memcmp(src_addr, dst_addr, cmp_len))
			same = false;

		kunmap_local(dst_addr);
		kunmap_local(src_addr);
unlock:
		vfs_unlock_two_folios(src_folio, dst_folio);
		folio_put(dst_folio);
		folio_put(src_folio);

		if (!same)
			break;

		srcoff += cmp_len;
		dstoff += cmp_len;
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
int
__generic_remap_file_range_prep(struct file *file_in, loff_t pos_in,
				struct file *file_out, loff_t pos_out,
				loff_t *len, unsigned int remap_flags,
				const struct iomap_ops *dax_read_ops)
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
	if (ret || *len == 0)
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

		if (!IS_DAX(inode_in))
			ret = vfs_dedupe_file_range_compare(file_in, pos_in,
					file_out, pos_out, *len, &is_same);
		else if (dax_read_ops)
			ret = dax_dedupe_file_range_compare(inode_in, pos_in,
					inode_out, pos_out, *len, &is_same,
					dax_read_ops);
		else
			return -EINVAL;
		if (ret)
			return ret;
		if (!is_same)
			return -EBADE;
	}

	ret = generic_remap_check_len(inode_in, inode_out, pos_out, len,
			remap_flags);
	if (ret || *len == 0)
		return ret;

	/* If can't alter the file contents, we're done. */
	if (!(remap_flags & REMAP_FILE_DEDUP))
		ret = file_modified(file_out);

	return ret;
}

int generic_remap_file_range_prep(struct file *file_in, loff_t pos_in,
				  struct file *file_out, loff_t pos_out,
				  loff_t *len, unsigned int remap_flags)
{
	return __generic_remap_file_range_prep(file_in, pos_in, file_out,
					       pos_out, len, remap_flags, NULL);
}
EXPORT_SYMBOL(generic_remap_file_range_prep);

loff_t do_clone_file_range(struct file *file_in, loff_t pos_in,
			   struct file *file_out, loff_t pos_out,
			   loff_t len, unsigned int remap_flags)
{
	loff_t ret;

	WARN_ON_ONCE(remap_flags & REMAP_FILE_DEDUP);

	if (file_inode(file_in)->i_sb != file_inode(file_out)->i_sb)
		return -EXDEV;

	ret = generic_file_rw_checks(file_in, file_out);
	if (ret < 0)
		return ret;

	if (!file_in->f_op->remap_file_range)
		return -EOPNOTSUPP;

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

	ret = remap_verify_area(file_in, pos_in, len, false);
	if (ret)
		return ret;

	ret = remap_verify_area(file_out, pos_out, len, true);
	if (ret)
		return ret;

	file_start_write(file_out);
	ret = do_clone_file_range(file_in, pos_in, file_out, pos_out, len,
				  remap_flags);
	file_end_write(file_out);

	return ret;
}
EXPORT_SYMBOL(vfs_clone_file_range);

/* Check whether we are allowed to dedupe the destination file */
static bool may_dedupe_file(struct file *file)
{
	struct mnt_idmap *idmap = file_mnt_idmap(file);
	struct inode *inode = file_inode(file);

	if (capable(CAP_SYS_ADMIN))
		return true;
	if (file->f_mode & FMODE_WRITE)
		return true;
	if (vfsuid_eq_kuid(i_uid_into_vfsuid(idmap, inode), current_fsuid()))
		return true;
	if (!inode_permission(idmap, inode, MAY_WRITE))
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

	/*
	 * This is redundant if called from vfs_dedupe_file_range(), but other
	 * callers need it and it's not performance sesitive...
	 */
	ret = remap_verify_area(src_file, src_pos, len, false);
	if (ret)
		return ret;

	ret = remap_verify_area(dst_file, dst_pos, len, true);
	if (ret)
		return ret;

	/*
	 * This needs to be called after remap_verify_area() because of
	 * sb_start_write() and before may_dedupe_file() because the mount's
	 * MAY_WRITE need to be checked with mnt_get_write_access_file() held.
	 */
	ret = mnt_want_write_file(dst_file);
	if (ret)
		return ret;

	ret = -EPERM;
	if (!may_dedupe_file(dst_file))
		goto out_drop_write;

	ret = -EXDEV;
	if (file_inode(src_file)->i_sb != file_inode(dst_file)->i_sb)
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
