// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS named stream handling.
 *
 * Copyright (c) 2026 LG Electronics Co., Ltd.
 */

#include <linux/file.h>
#include <linux/overflow.h>

#include <uapi/linux/ntfs.h>

#include "attrib.h"
#include "dir.h"
#include "iomap.h"
#include "mft.h"
#include "ntfs.h"
#include "stream.h"
#include "time.h"

#define NTFS_STREAM_MAX_IO	(16 * 1024 * 1024)

/*
 * ntfs_check_stream_name() - Validate a named-stream name
 * @name: UTF-16LE stream name
 * @name_len: Length of @name in UTF-16 code units
 *
 * Reject empty, overlong, separator-containing, or malformed UTF-16 names.
 *
 * Return: 0 if valid, or -EINVAL otherwise.
 */
int ntfs_check_stream_name(const __le16 *name, unsigned int name_len)
{
	unsigned int i;

	if (!name_len || name_len > NTFS_MAX_NAME_LEN)
		return -EINVAL;

	for (i = 0; i < name_len; i++) {
		u16 c = le16_to_cpu(name[i]);

		if (c == 0 || c == '/' || c == '\\' || c == ':')
			return -EINVAL;
		if (c >= 0xd800 && c <= 0xdbff) {
			if (++i >= name_len)
				return -EINVAL;
			c = le16_to_cpu(name[i]);
			if (c < 0xdc00 || c > 0xdfff)
				return -EINVAL;
		} else if (c >= 0xdc00 && c <= 0xdfff) {
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * ntfs_stream_inode_refresh() - Refresh base-derived stream inode metadata
 * @vi: inode representing a named stream
 *
 * Synchronize ownership, mode, flags, timestamps, and generation with the
 * base inode. Stream size and allocation remain specific to the stream.
 */
void ntfs_stream_inode_refresh(struct inode *vi)
{
	struct ntfs_inode *ni = NTFS_I(vi);
	struct inode *base_vi;

	if (!ntfs_inode_is_named_stream(ni) || ni->nr_extents != -1)
		return;

	base_vi = VFS_I(ni->ext.base_ntfs_ino);
	vi->i_uid = base_vi->i_uid;
	vi->i_gid = base_vi->i_gid;
	vi->i_mode = (base_vi->i_mode & ~S_IFMT) | S_IFREG;
	vi->i_flags = base_vi->i_flags;
	inode_set_mtime_to_ts(vi, inode_get_mtime(base_vi));
	inode_set_ctime_to_ts(vi, inode_get_ctime(base_vi));
	inode_set_atime_to_ts(vi, inode_get_atime(base_vi));
	vi->i_generation = base_vi->i_generation;
}

/*
 * ntfs_stream_update_base_time() - Update base timestamps after a stream change
 * @base_ni: base inode containing the stream attribute
 *
 * Update the base inode's mtime and ctime and mark it dirty. The helper takes
 * the base inode lock.
 */
void ntfs_stream_update_base_time(struct ntfs_inode *base_ni)
{
	struct inode *base_vi = VFS_I(base_ni);

	inode_lock_nested(base_vi, I_MUTEX_PARENT);
	inode_set_mtime_to_ts(base_vi, inode_set_ctime_current(base_vi));
	mark_inode_dirty(base_vi);
	inode_unlock(base_vi);
}

/*
 * ntfs_stream_inode_validate() - Validate a named-stream inode
 * @vi: stream inode to validate
 *
 * Verify that @vi still maps to its named DATA attribute and refresh its
 * base-derived metadata. The caller must hold the base inode's MFT-record
 * lock.
 *
 * Return: 0 if valid, or a negative errno.
 */
int ntfs_stream_inode_validate(struct inode *vi)
{
	struct ntfs_inode *ni = NTFS_I(vi);
	struct ntfs_inode *base_ni;
	struct ntfs_attr_search_ctx *ctx;
	int err;

	if (!ntfs_inode_is_named_stream(ni) || ni->nr_extents != -1)
		return -EINVAL;

	base_ni = ni->ext.base_ntfs_ino;
	lockdep_assert_held(&base_ni->mrec_lock);
	if (NVolShutdown(base_ni->vol))
		return -EIO;
	if (!NInoStreamUnlinked(ni) &&
	    (!vi->i_nlink || !VFS_I(base_ni)->i_nlink ||
	     NInoBeingDeleted(base_ni)))
		return -ESTALE;

	ctx = ntfs_attr_get_search_ctx(base_ni, NULL);
	if (!ctx)
		return -ENOMEM;
	err = ntfs_attr_lookup(AT_DATA, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	ntfs_attr_put_search_ctx(ctx);
	if (err)
		return err;

	ntfs_stream_inode_refresh(vi);
	return 0;
}

/*
 * ntfs_stream_unlinked() - Check a cached stream's deferred-unlink state
 * @base_ni: base inode containing the stream
 * @name: UTF-16LE stream name
 * @name_len: Length of @name in UTF-16 code units
 *
 * The caller must hold the base inode's MFT-record lock.
 *
 * Return: true if the cached stream inode is unlinked, or false if it is not
 * cached or is still linked.
 */
bool ntfs_stream_unlinked(struct ntfs_inode *base_ni,
		const __le16 *name, u32 name_len)
{
	struct ntfs_attr na = {
		.mft_no = base_ni->mft_no,
		.type = AT_DATA,
		.name = (__le16 *)name,
		.name_len = name_len,
	};
	struct inode *vi;
	bool unlinked;

	lockdep_assert_held(&base_ni->mrec_lock);
	rcu_read_lock();
	vi = find_inode_rcu(base_ni->vol->sb, na.mft_no,
			ntfs_test_inode_rcu, &na);
	if (!vi) {
		rcu_read_unlock();
		return false;
	}
	unlinked = NInoStreamUnlinked(NTFS_I(vi));
	rcu_read_unlock();
	return unlinked;
}

/*
 * Hold a temporary stream reference so unlink cannot remove its attribute
 * while an operation is using it.
 */
static int ntfs_stream_claim(struct inode *inode)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	struct ntfs_inode *base_ni = ni->ext.base_ntfs_ino;
	int err;

	mutex_lock(&base_ni->mrec_lock);
	if (NInoStreamUnlinked(ni))
		err = -ESTALE;
	else
		err = ntfs_stream_inode_validate(inode);
	if (!err) {
		if (atomic_read(&ni->stream_open_count) < 0)
			err = -ESTALE;
		else
			atomic_inc(&ni->stream_open_count);
	}
	mutex_unlock(&base_ni->mrec_lock);
	return err;
}

/*
 * Remove an unlinked stream's DATA attribute after its final active reference
 * is released. Base-inode deletion handles the attribute when the base is
 * already being removed.
 */
static int ntfs_stream_finish_remove(struct inode *attr_vi)
{
	struct ntfs_inode *attr_ni = NTFS_I(attr_vi);
	struct ntfs_inode *ni = attr_ni->ext.base_ntfs_ino;
	int err;

	mutex_lock(&ni->mrec_lock);
	if (!NInoStreamUnlinked(attr_ni) ||
	    atomic_cmpxchg(&attr_ni->stream_open_count, 0, -1)) {
		err = 0;
		goto out_unlock;
	}
	if (NInoBeingDeleted(ni) || !VFS_I(ni)->i_nlink) {
		remove_inode_hash(attr_vi);
		err = 0;
		goto out_unlock;
	}
	mutex_unlock(&ni->mrec_lock);

	truncate_inode_pages(attr_vi->i_mapping, 0);

	mutex_lock(&ni->mrec_lock);
	if (NVolShutdown(ni->vol)) {
		err = -EIO;
		goto out_unlock;
	}
	if (NInoBeingDeleted(ni) || !VFS_I(ni)->i_nlink) {
		remove_inode_hash(attr_vi);
		err = 0;
		goto out_unlock;
	}
	err = ntfs_attr_rm(attr_ni);
	if (!err) {
		NInoClearDirty(attr_ni);
		remove_inode_hash(attr_vi);
	} else {
		NInoSetBeingDeleted(attr_ni);
		remove_inode_hash(attr_vi);
	}

out_unlock:
	mutex_unlock(&ni->mrec_lock);
	return err;
}

/*
 * ntfs_stream_put() - Release a stream operation reference
 * @vi: stream inode whose reference is being released
 *
 * Finish a deferred unlink when this is the last reference to an unlinked
 * stream.
 *
 * Return: 0 on success, or a negative errno if deferred removal fails.
 */
int ntfs_stream_put(struct inode *vi)
{
	struct ntfs_inode *ni = NTFS_I(vi);

	if (atomic_dec_and_test(&ni->stream_open_count) &&
	    NInoStreamUnlinked(ni))
		return ntfs_stream_finish_remove(vi);
	return 0;
}

/*
 * ntfs_remove_named_stream() - Remove a named DATA stream
 * @ni: base inode containing the stream
 * @uname: UTF-16LE stream name
 * @uname_len: length of @uname in UTF-16 code units
 * @expected_vi: expected cached stream inode, or NULL
 *
 * Refuse removal while the stream is open. If @expected_vi is non-NULL, it
 * must be the inode currently associated with the named attribute.
 *
 * Return: 0 on success, -ENOENT if the stream is absent, or another negative
 * errno.
 */
int ntfs_remove_named_stream(struct ntfs_inode *ni, __le16 *uname,
		u32 uname_len, struct inode *expected_vi)
{
	struct inode *attr_vi;
	struct ntfs_inode *attr_ni;
	int err;

	if (!ni || !uname || uname_len == 0)
		return -EINVAL;
	if (NVolShutdown(ni->vol))
		return -EIO;
	if ((ni->flags & FILE_ATTR_READONLY) ||
	    IS_APPEND(VFS_I(ni)) || IS_IMMUTABLE(VFS_I(ni)))
		return -EPERM;
	if (!(ni->vol->vol_flags & VOLUME_IS_DIRTY)) {
		err = ntfs_set_volume_flags(ni->vol, VOLUME_IS_DIRTY);
		if (err)
			return err;
	}

	mutex_lock(&ni->mrec_lock);
	if (NInoBeingDeleted(ni) || !VFS_I(ni)->i_nlink) {
		err = -ENOENT;
		goto out_unlock;
	}
	attr_vi = ntfs_attr_iget(VFS_I(ni), AT_DATA, uname, uname_len);
	if (IS_ERR(attr_vi)) {
		err = PTR_ERR(attr_vi);
		goto out_unlock;
	}
	if (expected_vi && attr_vi != expected_vi) {
		err = -ESTALE;
		goto out_iput;
	}

	attr_ni = NTFS_I(attr_vi);
	if (NInoStreamUnlinked(attr_ni)) {
		err = -ENOENT;
		goto out_iput;
	}
	if (atomic_read(&attr_ni->stream_open_count) > 0) {
		err = -EBUSY;
		goto out_iput;
	}
	err = ntfs_stream_inode_validate(attr_vi);
	if (err)
		goto out_iput;

	NInoSetStreamUnlinked(attr_ni);
	clear_nlink(attr_vi);
	mutex_unlock(&ni->mrec_lock);

	ntfs_stream_update_base_time(ni);
	err = ntfs_stream_finish_remove(attr_vi);
	iput(attr_vi);
	return err;

out_iput:
	mutex_unlock(&ni->mrec_lock);
	iput(attr_vi);
	return err;
out_unlock:
	mutex_unlock(&ni->mrec_lock);
	return err;
}

enum ntfs_stream_ioctl_op {
	NTFS_STREAM_IOCTL_READ,
	NTFS_STREAM_IOCTL_WRITE,
	NTFS_STREAM_IOCTL_REMOVE,
};

/*
 * Look up or create an ioctl target stream and return its referenced inode.
 */
static int ntfs_stream_ioctl_get_inode(struct inode *base_vi, __le16 *sname,
		int sname_len, bool create, struct inode **stream_vi)
{
	struct ntfs_inode *base_ni = NTFS_I(base_vi);
	struct ntfs_attr_search_ctx *ctx;
	struct inode *attr_vi = NULL;
	bool created = false;
	int err;

	*stream_vi = NULL;

	mutex_lock(&base_ni->mrec_lock);
	if (NVolShutdown(base_ni->vol)) {
		err = -EIO;
		goto out_unlock;
	}
	if (NInoBeingDeleted(base_ni) || !base_vi->i_nlink) {
		err = -ENOENT;
		goto out_unlock;
	}

	ctx = ntfs_attr_get_search_ctx(base_ni, NULL);
	if (!ctx) {
		err = -ENOMEM;
		goto out_unlock;
	}

	err = ntfs_attr_lookup(AT_DATA, sname, sname_len, IGNORE_CASE,
			0, NULL, 0, ctx);
	if (err == -ENOENT && create) {
		err = ntfs_attr_add(base_ni, AT_DATA, sname, sname_len,
				NULL, 0);
		if (!err) {
			created = true;
			mark_mft_record_dirty(base_ni);
		}
	}
	ntfs_attr_put_search_ctx(ctx);
	if (err)
		goto out_unlock;

	attr_vi = ntfs_attr_iget(base_vi, AT_DATA, sname, sname_len);
	if (IS_ERR(attr_vi)) {
		err = PTR_ERR(attr_vi);
		attr_vi = NULL;
		goto out_unlock;
	}
	if (NInoStreamUnlinked(NTFS_I(attr_vi)))
		err = create ? -EBUSY : -ENOENT;
	else
		err = ntfs_stream_inode_validate(attr_vi);
out_unlock:
	mutex_unlock(&base_ni->mrec_lock);
	if (created)
		ntfs_stream_update_base_time(base_ni);
	if (err) {
		iput(attr_vi);
		return err;
	}

	*stream_vi = attr_vi;
	return 0;
}

/* Check file mode, base-inode flags, and the requested transfer range. */
static int ntfs_stream_ioctl_access(struct file *filp,
		enum ntfs_stream_ioctl_op op, loff_t pos, size_t len)
{
	struct inode *inode = file_inode(filp);
	bool is_dir = S_ISDIR(inode->i_mode);
	int err;

	if (op == NTFS_STREAM_IOCTL_READ) {
		if (!(filp->f_mode & FMODE_READ))
			return -EBADF;
		return rw_verify_area(READ, filp, &pos, len);
	}

	if (!(filp->f_mode & FMODE_WRITE) && !is_dir)
		return -EBADF;
	if (is_dir) {
		err = inode_permission(file_mnt_idmap(filp), inode,
				MAY_WRITE | MAY_EXEC);
		if (err)
			return err;
	}
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return -EPERM;
	if (op == NTFS_STREAM_IOCTL_REMOVE)
		return 0;
	return rw_verify_area(WRITE, filp, &pos, len);
}

/*
 * Validate and execute a named-stream read, write, or remove ioctl request.
 */
static long ntfs_ioctl_stream(struct file *filp, unsigned long arg,
		enum ntfs_stream_ioctl_op op)
{
	struct inode *base_vi = file_inode(filp);
	struct ntfs_inode *base_ni = NTFS_I(base_vi);
	struct ntfs_stream hdr;
	struct ntfs_stream *req;
	struct inode *stream_vi = NULL;
	__le16 *uname = NULL, *sname;
	size_t data_size, total_size;
	loff_t pos;
	s64 ret;
	int sname_len, err;
	bool claimed = false, got_write = false, utf16;

	if (NVolShutdown(base_ni->vol))
		return -EIO;
	if (NInoAttr(base_ni) ||
	    (!S_ISREG(base_vi->i_mode) && !S_ISDIR(base_vi->i_mode)))
		return -EOPNOTSUPP;
	if (copy_from_user(&hdr, (void __user *)arg, sizeof(hdr)))
		return -EFAULT;

	if (hdr.io_len > NTFS_STREAM_MAX_IO ||
	    (hdr.flags & ~NTFS_STREAM_FL_UTF16) || hdr.reserved)
		return -EINVAL;
	if (!hdr.name_len)
		return -EINVAL;

	utf16 = hdr.flags & NTFS_STREAM_FL_UTF16;
	if (utf16) {
		if ((hdr.name_len & 1) ||
		    hdr.name_len > NTFS_MAX_NAME_LEN * sizeof(__le16))
			return -EINVAL;
	} else if (hdr.name_len >
			NTFS_MAX_NAME_LEN * NLS_MAX_CHARSET_SIZE) {
		return -EINVAL;
	}

	switch (op) {
	case NTFS_STREAM_IOCTL_READ:
	case NTFS_STREAM_IOCTL_WRITE:
		if (!hdr.io_len)
			return -EINVAL;
		if (hdr.stream_offset > S64_MAX ||
		    hdr.stream_offset > S64_MAX - hdr.io_len)
			return -EOVERFLOW;
		data_size = hdr.io_len;
		break;
	case NTFS_STREAM_IOCTL_REMOVE:
		if (hdr.io_len || hdr.stream_offset)
			return -EINVAL;
		data_size = 0;
		break;
	default:
		return -ENOTTY;
	}

	if (check_add_overflow(sizeof(hdr), (size_t)hdr.name_len,
			&total_size) ||
	    check_add_overflow(total_size, data_size, &total_size))
		return -EOVERFLOW;

	if (op == NTFS_STREAM_IOCTL_READ) {
		req = kvmalloc(total_size, GFP_KERNEL);
		if (!req)
			return -ENOMEM;
		if (copy_from_user(req, (void __user *)arg,
				sizeof(hdr) + hdr.name_len)) {
			kvfree(req);
			return -EFAULT;
		}
	} else {
		req = vmemdup_user((void __user *)arg, total_size);
		if (IS_ERR(req))
			return PTR_ERR(req);
	}

	req->bytes_returned = 0;
	if (req->stream_offset != hdr.stream_offset ||
	    req->io_len != hdr.io_len || req->name_len != hdr.name_len ||
	    req->flags != hdr.flags || req->reserved) {
		err = -EINVAL;
		goto out_free;
	}

	if (utf16) {
		sname = (__le16 *)req->buffer;
		sname_len = req->name_len / sizeof(__le16);
	} else {
		if (memchr(req->buffer, '\0', req->name_len)) {
			err = -EINVAL;
			goto out_free;
		}
		sname_len = ntfs_nlstoucs(base_ni->vol, req->buffer,
				req->name_len, &uname, NTFS_MAX_NAME_LEN);
		if (sname_len < 0) {
			err = sname_len;
			goto out_free;
		}
		sname = uname;
	}
	err = ntfs_check_stream_name(sname, sname_len);
	if (err)
		goto out_free;

	pos = hdr.stream_offset;
	err = ntfs_stream_ioctl_access(filp, op, pos, data_size);
	if (err)
		goto out_free;

	if (op == NTFS_STREAM_IOCTL_REMOVE) {
		err = mnt_want_write_file(filp);
		if (err)
			goto out_free;
		err = ntfs_remove_named_stream(base_ni, sname, sname_len, NULL);
		mnt_drop_write_file(filp);
		goto out_free;
	}

	if (op == NTFS_STREAM_IOCTL_WRITE) {
		err = mnt_want_write_file(filp);
		if (err)
			goto out_free;
		got_write = true;
		inode_lock(base_vi);
		err = file_remove_privs(filp);
		inode_unlock(base_vi);
		if (err)
			goto out_drop_write;
		if (!(base_ni->vol->vol_flags & VOLUME_IS_DIRTY)) {
			err = ntfs_set_volume_flags(base_ni->vol,
					VOLUME_IS_DIRTY);
			if (err)
				goto out_drop_write;
		}
	}

	err = ntfs_stream_ioctl_get_inode(base_vi, sname, sname_len,
			op == NTFS_STREAM_IOCTL_WRITE, &stream_vi);
	if (err)
		goto out_drop_write;

	err = ntfs_stream_claim(stream_vi);
	if (err)
		goto out_drop_write;
	claimed = true;

	if (op == NTFS_STREAM_IOCTL_READ) {
		inode_lock_shared(stream_vi);
		if (pos < i_size_read(stream_vi))
			ret = ntfs_inode_attr_pread(stream_vi, pos, data_size,
					req->buffer + req->name_len);
		else
			ret = 0;
		inode_unlock_shared(stream_vi);
		if (ret < 0)
			err = ret;
		else
			req->bytes_returned = ret;
		if (!err)
			file_accessed(filp);
	} else {
		inode_lock(stream_vi);
		err = inode_newsize_ok(stream_vi, pos + data_size);
		if (!err) {
			ret = ntfs_inode_attr_pwrite(stream_vi, pos, data_size,
					req->buffer + req->name_len, false);
			if (ret < 0)
				err = ret;
			else
				req->bytes_returned = ret;
		}
		inode_unlock(stream_vi);
	}
	if (claimed) {
		int put_err;

		put_err = ntfs_stream_put(stream_vi);
		claimed = false;
		if (!err && put_err)
			err = put_err;
	}
	iput(stream_vi);
	stream_vi = NULL;
	if (err)
		goto out_drop_write;

	if (op == NTFS_STREAM_IOCTL_READ) {
		if (copy_to_user((void __user *)arg, req,
				sizeof(*req) + req->name_len +
				req->bytes_returned))
			err = -EFAULT;
	} else if (copy_to_user((void __user *)arg +
			offsetof(struct ntfs_stream, bytes_returned),
			&req->bytes_returned, sizeof(req->bytes_returned))) {
		err = -EFAULT;
	}
	goto out_drop_write;

out_drop_write:
	if (claimed)
		ntfs_stream_put(stream_vi);
	if (stream_vi)
		iput(stream_vi);
	if (got_write)
		mnt_drop_write_file(filp);
out_free:
	if (uname)
		kmem_cache_free(ntfs_name_cache, uname);
	kvfree(req);
	return err;
}

/*
 * ntfs_ioctl_stream_read() - Handle a named-stream read ioctl
 * @filp: file opened on the base file or directory
 * @arg: userspace pointer to the request and data buffer
 *
 * Return: 0 on success, or a negative errno.
 */
long ntfs_ioctl_stream_read(struct file *filp, unsigned long arg)
{
	return ntfs_ioctl_stream(filp, arg, NTFS_STREAM_IOCTL_READ);
}

/*
 * ntfs_ioctl_stream_write() - Handle a named-stream write ioctl
 * @filp: file opened on the base file or directory
 * @arg: userspace pointer to the request and data buffer
 *
 * Create the stream if needed.
 *
 * Return: 0 on success, or a negative errno.
 */
long ntfs_ioctl_stream_write(struct file *filp, unsigned long arg)
{
	return ntfs_ioctl_stream(filp, arg, NTFS_STREAM_IOCTL_WRITE);
}

/*
 * ntfs_ioctl_stream_remove() - Handle a named-stream remove ioctl
 * @filp: file opened on the base file or directory
 * @arg: userspace pointer to the request
 *
 * Return: 0 on success, or a negative errno.
 */
long ntfs_ioctl_stream_remove(struct file *filp, unsigned long arg)
{
	return ntfs_ioctl_stream(filp, arg, NTFS_STREAM_IOCTL_REMOVE);
}

/*
 * ntfs_ioctl_list_streams() - List named DATA streams
 * @filp: file opened on the base file or directory
 * @arg: userspace pointer to the request and output buffer
 *
 * On -ENOSPC, return the required buffer size in the request header.
 *
 * Return: 0 on success, or a negative errno.
 */
int ntfs_ioctl_list_streams(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct ntfs_inode *ni = NTFS_I(inode);
	struct ntfs_list_streams hdr;
	struct ntfs_stream_entry *entry, *last_entry = NULL;
	size_t required = 0;
	void *kbuf = NULL;
	void __user *ubuf;
	struct attr_record *a;
	struct ntfs_attr_search_ctx *actx;
	int ret = 0, err, count = 0;
	size_t entry_size, offset = 0;
	const size_t name_offset = offsetof(struct ntfs_stream_entry, name);
	int name_len;
	unsigned char *sn = NULL;
	bool utf16;

	if (NVolShutdown(ni->vol))
		return -EIO;
	if (NInoAttr(ni) ||
	    (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode)))
		return -EOPNOTSUPP;
	err = inode_permission(file_mnt_idmap(filp), inode, MAY_READ);
	if (err)
		return err;

	if (copy_from_user(&hdr, (void __user *)arg, sizeof(hdr)))
		return -EFAULT;

	if ((hdr.flags & ~NTFS_STREAM_FL_UTF16) || hdr.reserved)
		return -EINVAL;

	utf16 = hdr.flags & NTFS_STREAM_FL_UTF16;

	ubuf = (void __user *)arg + sizeof(hdr);

	mutex_lock(&ni->mrec_lock);
	actx = ntfs_attr_get_search_ctx(ni, NULL);
	if (!actx) {
		mutex_unlock(&ni->mrec_lock);
		return -ENOMEM;
	}

	while ((err = ntfs_attrs_walk(actx)) == 0) {
		a = actx->attr;
		if (a->type != AT_DATA || !a->name_length)
			continue;
		if (a->non_resident &&
		    a->data.non_resident.lowest_vcn)
			continue;
		if (ntfs_stream_unlinked(ni,
				(__le16 *)((u8 *)a +
				le16_to_cpu(a->name_offset)),
				a->name_length))
			continue;

		if (utf16) {
			name_len = a->name_length * sizeof(__le16);
		} else {
			name_len = ntfs_ucstonls(ni->vol,
					(__le16 *)((u8 *)a +
					le16_to_cpu(a->name_offset)),
					a->name_length, &sn, 0);
			if (name_len < 0) {
				if (name_len == -EILSEQ ||
				    name_len == -ENAMETOOLONG)
					continue;
				ret = name_len;
				goto out;
			}
			kfree(sn);
			sn = NULL;
		}

		entry_size = ALIGN(name_offset + name_len, 8);

		if (required > SIZE_MAX - entry_size) {
			ret = -EOVERFLOW;
			goto out;
		}
		required += entry_size;
		count++;
	}
	if (err != -ENOENT) {
		ret = err;
		goto out;
	}

	hdr.stream_count = count;
	hdr.bytes_returned = required;

	if (!count)
		goto out;

	if (hdr.buffer_size < required) {
		ret = -ENOSPC;
		goto out;
	}

	kbuf = kvzalloc(required, GFP_NOFS);
	if (!kbuf) {
		ret = -ENOMEM;
		goto out;
	}

	ntfs_attr_reinit_search_ctx(actx);
	while ((err = ntfs_attrs_walk(actx)) == 0) {
		a = actx->attr;
		if (a->type != AT_DATA || !a->name_length)
			continue;
		if (a->non_resident &&
		    a->data.non_resident.lowest_vcn)
			continue;
		if (ntfs_stream_unlinked(ni,
				(__le16 *)((u8 *)a +
				le16_to_cpu(a->name_offset)),
				a->name_length))
			continue;
		if (utf16) {
			sn = NULL;
			name_len = a->name_length * sizeof(__le16);
		} else {
			name_len = ntfs_ucstonls(ni->vol,
					(__le16 *)((u8 *)a +
					le16_to_cpu(a->name_offset)),
					a->name_length, &sn, 0);
			if (name_len < 0) {
				if (name_len == -EILSEQ ||
				    name_len == -ENAMETOOLONG)
					continue;
				ret = name_len;
				goto out;
			}
		}

		entry_size = ALIGN(name_offset + name_len, 8);
		if (offset > required - entry_size) {
			ret = -EOVERFLOW;
			kfree(sn);
			sn = NULL;
			goto out;
		}

		entry = (struct ntfs_stream_entry *)(kbuf + offset);

		if (a->non_resident) {
			entry->size = le64_to_cpu(a->data.non_resident.data_size);
			entry->alloc_size =
				le64_to_cpu(a->data.non_resident.allocated_size);
		} else {
			entry->size = le32_to_cpu(a->data.resident.value_length);
			entry->alloc_size = le32_to_cpu(a->length) -
				le16_to_cpu(a->data.resident.value_offset);
		}

		entry->name_len = name_len;
		entry->name_offset = name_offset;
		if (utf16)
			memcpy(entry->name,
			       (u8 *)a + le16_to_cpu(a->name_offset), name_len);
		else
			memcpy(entry->name, sn, name_len);
		kfree(sn);
		sn = NULL;

		entry->next_entry_off = entry_size;
		last_entry = entry;
		offset += entry_size;
	}
	if (err != -ENOENT) {
		ret = err;
	} else {
		/* The chain terminates at the last entry. */
		if (last_entry)
			last_entry->next_entry_off = 0;
		hdr.bytes_returned = offset;
	}

out:
	kfree(sn);
	ntfs_attr_put_search_ctx(actx);
	mutex_unlock(&ni->mrec_lock);

	if (ret && ret != -ENOSPC)
		goto out_free;

	if (!ret && kbuf && copy_to_user(ubuf, kbuf, hdr.bytes_returned)) {
		ret = -EFAULT;
		goto out_free;
	}

	if (copy_to_user((void __user *)arg, &hdr, sizeof(hdr)))
		ret = -EFAULT;

out_free:
	kvfree(kbuf);
	return ret;
}
