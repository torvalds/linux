// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS named stream handling.
 *
 * Copyright (c) 2026 LG Electronics Co., Ltd.
 */

#include <linux/file.h>
#include <linux/namei.h>
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
 * ntfs_stream_path_parse() - Split a pathname stream component
 * @vol: NTFS volume
 * @qname: final pathname component
 * @path: receives slices of @qname on success
 *
 * Parse the ``file:stream`` form when the Windows pathname interface is
 * enabled. This does not convert or validate the component names.
 *
 * Return: 1 if stream syntax was parsed, 0 if streams are disabled or no
 * stream separator is present, or -EINVAL for malformed syntax.
 */
int ntfs_stream_path_parse(struct ntfs_volume *vol,
		const struct qstr *qname, struct ntfs_stream_path *path)
{
	const unsigned char *colon;

	if (!NVolStreamsWindows(vol))
		return 0;

	colon = memchr(qname->name, ':', qname->len);
	if (!colon)
		return 0;
	if (colon == qname->name || colon + 1 == qname->name + qname->len)
		return -EINVAL;
	if (memchr(colon + 1, ':', qname->name + qname->len - colon - 1))
		return -EINVAL;

	path->base_name = (const char *)qname->name;
	path->base_len = colon - qname->name;
	path->stream_name = (const char *)colon + 1;
	path->stream_len = qname->name + qname->len - colon - 1;
	return 1;
}

/*
 * ntfs_stream_path_has_colon() - Check for a pathname stream separator
 * @vol: NTFS volume
 * @qname: final pathname component
 *
 * Return: true if the Windows pathname interface is enabled and @qname
 * contains a colon, or false otherwise.
 */
bool ntfs_stream_path_has_colon(struct ntfs_volume *vol,
		const struct qstr *qname)
{
	return NVolStreamsWindows(vol) &&
		memchr(qname->name, ':', qname->len);
}

/*
 * ntfs_stream_path_check() - Reject stream syntax for unsupported operations
 * @vol: NTFS volume
 * @qname: final pathname component
 *
 * Return: 0 for an ordinary name or when streams are disabled,
 * -EOPNOTSUPP for parsed stream syntax, or -EINVAL for malformed syntax.
 */
int ntfs_stream_path_check(struct ntfs_volume *vol,
		const struct qstr *qname)
{
	struct ntfs_stream_path path;
	int ret;

	ret = ntfs_stream_path_parse(vol, qname, &path);
	if (ret < 0)
		return ret;
	return ret ? -EOPNOTSUPP : 0;
}

/*
 * ntfs_stream_lookup_inode_by_name() - Look up a base inode by name
 * @dir_ino: directory containing the base file or directory
 * @uname: UTF-16LE base name
 * @uname_len: Length of @uname in UTF-16 code units
 *
 * Resolve the directory entry and verify its MFT reference before returning
 * the inode.
 *
 * Return: Referenced inode on success, or ERR_PTR() on failure.
 */
struct inode *ntfs_stream_lookup_inode_by_name(struct inode *dir_ino,
		__le16 *uname, int uname_len)
{
	struct ntfs_volume *vol = NTFS_SB(dir_ino->i_sb);
	struct ntfs_name *name = NULL;
	struct inode *inode;
	u64 mref;

	mutex_lock(&NTFS_I(dir_ino)->mrec_lock);
	mref = ntfs_lookup_inode_by_name(NTFS_I(dir_ino), uname, uname_len,
			&name);
	mutex_unlock(&NTFS_I(dir_ino)->mrec_lock);
	kfree(name);

	if (IS_ERR_MREF(mref))
		return ERR_PTR(MREF_ERR(mref));

	inode = ntfs_iget(vol->sb, MREF(mref));
	if (IS_ERR(inode))
		return inode;
	if (MSEQNO(mref) != NTFS_I(inode)->seq_no &&
	    MREF(mref) != FILE_MFT) {
		iput(inode);
		return ERR_PTR(-EIO);
	}
	return inode;
}

/*
 * Recheck that a stream dentry still names the same base inode and DATA
 * attribute.
 */
static int ntfs_stream_d_revalidate(struct inode *dir,
		const struct qstr *qname, struct dentry *dentry,
		unsigned int flags)
{
	struct inode *inode = d_inode(dentry);
	struct ntfs_inode *ni;
	struct ntfs_stream_path path;
	struct inode *base_vi;
	__le16 *base_name = NULL, *stream_name = NULL;
	int base_len, stream_len, err, ret = 0;

	if (flags & LOOKUP_RCU)
		return -ECHILD;
	if (!inode)
		return 0;
	if (!inode->i_nlink)
		return 0;
	if (NVolShutdown(NTFS_SB(dir->i_sb)))
		return 0;

	ni = NTFS_I(inode);
	if (!ntfs_inode_is_named_stream(ni))
		return 0;

	err = ntfs_stream_path_parse(NTFS_SB(dir->i_sb), qname, &path);
	if (err <= 0)
		return 0;
	base_len = ntfs_nlstoucs(NTFS_SB(dir->i_sb), path.base_name,
			path.base_len, &base_name, NTFS_MAX_NAME_LEN);
	if (base_len < 0)
		goto out;
	stream_len = ntfs_nlstoucs(NTFS_SB(dir->i_sb), path.stream_name,
			path.stream_len, &stream_name, NTFS_MAX_NAME_LEN);
	if (stream_len < 0)
		goto out;
	if (ntfs_check_stream_name(stream_name, stream_len))
		goto out;
	if (stream_len != ni->name_len ||
	    !ntfs_names_are_equal(stream_name, stream_len, ni->name,
			ni->name_len, IGNORE_CASE, ni->vol->upcase,
			ni->vol->upcase_len))
		goto out;

	base_vi = ntfs_stream_lookup_inode_by_name(dir, base_name, base_len);
	if (IS_ERR(base_vi))
		goto out;
	if (NTFS_I(base_vi) != ni->ext.base_ntfs_ino) {
		iput(base_vi);
		goto out;
	}

	mutex_lock(&NTFS_I(base_vi)->mrec_lock);
	err = ntfs_stream_inode_validate(inode);
	mutex_unlock(&NTFS_I(base_vi)->mrec_lock);
	iput(base_vi);
	if (!err)
		ret = 1;
out:
	if (stream_name)
		kmem_cache_free(ntfs_name_cache, stream_name);
	if (base_name)
		kmem_cache_free(ntfs_name_cache, base_name);
	return ret;
}

/* Revalidate stream paths while leaving ordinary NTFS dentries cacheable. */
static int ntfs_dentry_revalidate(struct inode *dir,
		const struct qstr *qname, struct dentry *dentry,
		unsigned int flags)
{
	struct inode *inode = d_inode(dentry);
	struct ntfs_volume *vol = NTFS_SB(dir->i_sb);

	if (ntfs_stream_path_has_colon(vol, qname))
		return ntfs_stream_d_revalidate(dir, qname, dentry, flags);
	if (inode && ntfs_inode_is_named_stream(NTFS_I(inode)))
		return 0;
	return 1;
}

/* Drop stream-path aliases so future lookups recheck their backing attribute. */
static int ntfs_dentry_delete(const struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	struct ntfs_volume *vol = NTFS_SB(dentry->d_sb);

	if (ntfs_stream_path_has_colon(vol, &dentry->d_name) ||
	    (inode && ntfs_inode_is_named_stream(NTFS_I(inode))))
		return always_delete_dentry(dentry);
	return 0;
}

static const struct dentry_operations ntfs_dentry_ops = {
	.d_revalidate	= ntfs_dentry_revalidate,
	.d_delete		= ntfs_dentry_delete,
};

/*
 * ntfs_set_default_dentry_ops() - Install stream-aware dentry operations
 * @sb: superblock on which to install the operations
 *
 * Ensure pathname stream dentries are revalidated and not kept as stale
 * aliases.
 */
void ntfs_set_default_dentry_ops(struct super_block *sb)
{
	set_default_d_op(sb, &ntfs_dentry_ops);
}

/*
 * ntfs_lookup_stream() - Look up a named stream by pathname
 * @dir_ino: directory containing the base file or directory
 * @dent: dentry for the pathname component
 * @path: parsed base and stream name slices
 *
 * Find the existing base inode and its named DATA attribute, then splice the
 * stream inode into @dent.
 *
 * Return: NULL if @dent was instantiated, an alternate dentry if a
 * disconnected alias was spliced, or ERR_PTR() on failure.
 */
struct dentry *ntfs_lookup_stream(struct inode *dir_ino,
		struct dentry *dent, const struct ntfs_stream_path *path)
{
	struct ntfs_volume *vol = NTFS_SB(dir_ino->i_sb);
	struct inode *base_vi, *stream_vi;
	__le16 *base_uname = NULL, *stream_uname = NULL;
	int base_len, stream_len, err;

	if (NVolShutdown(vol))
		return ERR_PTR(-EIO);

	base_len = ntfs_nlstoucs(vol, path->base_name, path->base_len,
			&base_uname, NTFS_MAX_NAME_LEN);
	if (base_len < 0)
		return ERR_PTR(base_len);

	stream_len = ntfs_nlstoucs(vol, path->stream_name, path->stream_len,
			&stream_uname, NTFS_MAX_NAME_LEN);
	if (stream_len < 0) {
		kmem_cache_free(ntfs_name_cache, base_uname);
		return ERR_PTR(stream_len);
	}
	err = ntfs_check_stream_name(stream_uname, stream_len);
	if (err) {
		kmem_cache_free(ntfs_name_cache, base_uname);
		kmem_cache_free(ntfs_name_cache, stream_uname);
		return ERR_PTR(err);
	}

	base_vi = ntfs_stream_lookup_inode_by_name(dir_ino, base_uname,
			base_len);
	kmem_cache_free(ntfs_name_cache, base_uname);
	if (IS_ERR(base_vi)) {
		err = PTR_ERR(base_vi);
		kmem_cache_free(ntfs_name_cache, stream_uname);
		if (err == -ENOENT)
			return d_splice_alias(NULL, dent);
		return ERR_PTR(err);
	}

	if (!S_ISREG(base_vi->i_mode) && !S_ISDIR(base_vi->i_mode)) {
		iput(base_vi);
		kmem_cache_free(ntfs_name_cache, stream_uname);
		return ERR_PTR(-ENOTDIR);
	}

	mutex_lock(&NTFS_I(base_vi)->mrec_lock);
	stream_vi = ntfs_attr_iget(base_vi, AT_DATA, stream_uname, stream_len);
	if (!IS_ERR(stream_vi)) {
		if (NInoStreamUnlinked(NTFS_I(stream_vi)))
			err = -ENOENT;
		else
			err = ntfs_stream_inode_validate(stream_vi);
	}
	mutex_unlock(&NTFS_I(base_vi)->mrec_lock);
	if (!IS_ERR(stream_vi) && err) {
		iput(stream_vi);
		stream_vi = ERR_PTR(err);
	}
	iput(base_vi);
	kmem_cache_free(ntfs_name_cache, stream_uname);

	if (IS_ERR(stream_vi)) {
		err = PTR_ERR(stream_vi);
		if (err == -ENOENT || err == -ESTALE)
			return d_splice_alias(NULL, dent);
		return ERR_PTR(err);
	}

	return d_splice_alias(stream_vi, dent);
}

/*
 * ntfs_stream_path_names() - Convert and validate pathname stream names
 * @vol: NTFS volume
 * @qname: final pathname component
 * @base_name: receives the allocated UTF-16LE base name
 * @base_len: receives the base name length in UTF-16 code units
 * @stream_name: receives the allocated UTF-16LE stream name
 * @stream_len: receives the stream name length in UTF-16 code units
 *
 * Return: 1 if stream names were produced, 0 if @qname has no stream syntax,
 * or a negative errno. The caller owns both name buffers on success.
 */
int ntfs_stream_path_names(struct ntfs_volume *vol,
		const struct qstr *qname, __le16 **base_name, int *base_len,
		__le16 **stream_name, int *stream_len)
{
	struct ntfs_stream_path path;
	int err;

	*base_name = NULL;
	*stream_name = NULL;
	err = ntfs_stream_path_parse(vol, qname, &path);
	if (err <= 0)
		return err;

	*base_len = ntfs_nlstoucs(vol, path.base_name, path.base_len,
			base_name, NTFS_MAX_NAME_LEN);
	if (*base_len < 0)
		return *base_len;

	*stream_len = ntfs_nlstoucs(vol, path.stream_name, path.stream_len,
			stream_name, NTFS_MAX_NAME_LEN);
	if (*stream_len < 0) {
		kmem_cache_free(ntfs_name_cache, *base_name);
		*base_name = NULL;
		return *stream_len;
	}

	err = ntfs_check_bad_windows_name(vol, *base_name, *base_len);
	if (err)
		goto out_free;
	err = ntfs_check_stream_name(*stream_name, *stream_len);
	if (err)
		goto out_free;
	err = ntfs_check_bad_windows_name(vol, *stream_name, *stream_len);
	if (err)
		goto out_free;
	return 1;

out_free:
	kmem_cache_free(ntfs_name_cache, *stream_name);
	kmem_cache_free(ntfs_name_cache, *base_name);
	*stream_name = NULL;
	*base_name = NULL;
	return err;
}

/*
 * ntfs_create_named_stream() - Create a named DATA stream
 * @idmap: mount idmap used for permission checks
 * @dir: directory containing the base object
 * @base_name: UTF-16LE base name
 * @base_len: length of @base_name in UTF-16 code units
 * @stream_name: UTF-16LE stream name
 * @stream_len: length of @stream_name in UTF-16 code units
 *
 * Create the stream on an existing regular file or directory.
 *
 * Return: Referenced stream inode on success, or ERR_PTR() on failure.
 */
struct inode *ntfs_create_named_stream(const struct mnt_idmap *idmap,
		struct inode *dir, __le16 *base_name, int base_len,
		__le16 *stream_name, int stream_len)
{
	struct ntfs_inode *base_ni;
	struct inode *base_vi, *stream_vi;
	struct inode *rollback_vi = NULL;
	struct ntfs_attr_search_ctx *ctx;
	bool stream_created = false;
	int err, rollback_err;

	base_vi = ntfs_stream_lookup_inode_by_name(dir, base_name, base_len);
	if (IS_ERR(base_vi))
		return base_vi;
	if (!S_ISREG(base_vi->i_mode) && !S_ISDIR(base_vi->i_mode)) {
		iput(base_vi);
		return ERR_PTR(-ENOTDIR);
	}
	err = inode_permission(idmap, base_vi, MAY_OPEN | MAY_WRITE);
	if (err)
		goto out_iput;
	if (IS_APPEND(base_vi) || IS_IMMUTABLE(base_vi)) {
		err = -EPERM;
		goto out_iput;
	}
	if (!(NTFS_SB(base_vi->i_sb)->vol_flags & VOLUME_IS_DIRTY)) {
		err = ntfs_set_volume_flags(NTFS_SB(base_vi->i_sb),
				VOLUME_IS_DIRTY);
		if (err)
			goto out_iput;
	}

	base_ni = NTFS_I(base_vi);
	mutex_lock(&base_ni->mrec_lock);
	if (NVolShutdown(base_ni->vol)) {
		err = -EIO;
		goto out_unlock;
	}
	if (NInoBeingDeleted(base_ni) || !base_vi->i_nlink) {
		err = -ENOENT;
		goto out_unlock;
	}
	if (IS_APPEND(base_vi) || IS_IMMUTABLE(base_vi)) {
		err = -EPERM;
		goto out_unlock;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, NULL);
	if (!ctx) {
		err = -ENOMEM;
		goto out_unlock;
	}

	err = ntfs_attr_lookup(AT_DATA, stream_name, stream_len,
			IGNORE_CASE, 0, NULL, 0, ctx);
	if (!err) {
		if (ntfs_stream_unlinked(base_ni, stream_name, stream_len))
			err = -EBUSY;
		else
			err = -EEXIST;
	} else if (err == -ENOENT) {
		if (NVolShutdown(base_ni->vol)) {
			err = -EIO;
			goto out_put_ctx;
		}
		err = ntfs_attr_add(base_ni, AT_DATA, stream_name, stream_len,
				NULL, 0);
		if (!err) {
			stream_created = true;
			mark_mft_record_dirty(base_ni);
		}
	}
	ntfs_attr_put_search_ctx(ctx);
	if (err)
		goto out_unlock;

	stream_vi = ntfs_attr_iget(base_vi, AT_DATA, stream_name, stream_len);
	if (!IS_ERR(stream_vi))
		err = ntfs_stream_inode_validate(stream_vi);
	else
		err = PTR_ERR(stream_vi);
	if (err) {
		if (stream_created) {
			rollback_err = ntfs_attr_remove(base_ni, AT_DATA,
					stream_name, stream_len, &rollback_vi);
			if (rollback_err) {
				ntfs_error(base_ni->vol->sb,
					"Failed to roll back named stream creation.\n");
				if (rollback_err == -ENOMEM ||
				    rollback_err == -EINTR ||
				    rollback_err == -ERESTARTSYS) {
					err = rollback_err;
				} else {
					NVolSetErrors(base_ni->vol);
					NVolSetShutdown(base_ni->vol);
					err = -EIO;
				}
			}
		}
		mutex_unlock(&base_ni->mrec_lock);
		if (stream_created)
			ntfs_stream_update_base_time(base_ni);
		if (!IS_ERR(stream_vi))
			iput(stream_vi);
		if (rollback_vi)
			iput(rollback_vi);
		iput(base_vi);
		return ERR_PTR(err);
	}
	mutex_unlock(&base_ni->mrec_lock);
	if (stream_created)
		ntfs_stream_update_base_time(base_ni);
	iput(base_vi);
	return stream_vi;

out_put_ctx:
	ntfs_attr_put_search_ctx(ctx);
out_unlock:
	mutex_unlock(&base_ni->mrec_lock);
out_iput:
	iput(base_vi);
	return ERR_PTR(err);
}

/*
 * ntfs_unlink_named_stream() - Remove a pathname named stream
 * @dir: directory containing the base object
 * @dentry: dentry for the named stream
 *
 * Return: 0 on success, or a negative errno.
 */
int ntfs_unlink_named_stream(struct inode *dir, struct dentry *dentry)
{
	struct ntfs_volume *vol = NTFS_SB(dir->i_sb);
	struct inode *base_vi;
	__le16 *base_name = NULL, *stream_name = NULL;
	int base_len, stream_len, path_result, err;

	path_result = ntfs_stream_path_names(vol, &dentry->d_name, &base_name,
			&base_len, &stream_name, &stream_len);
	if (path_result < 0)
		return path_result;
	if (!path_result)
		return -EINVAL;

	base_vi = ntfs_stream_lookup_inode_by_name(dir, base_name, base_len);
	if (IS_ERR(base_vi)) {
		err = PTR_ERR(base_vi);
		goto out_free;
	}
	if (!S_ISREG(base_vi->i_mode) && !S_ISDIR(base_vi->i_mode)) {
		err = -ENOTDIR;
		goto out_iput;
	}
	if (!ntfs_inode_is_named_stream(NTFS_I(dentry->d_inode))) {
		err = -ESTALE;
		goto out_iput;
	}
	if (NTFS_I(dentry->d_inode)->ext.base_ntfs_ino != NTFS_I(base_vi)) {
		err = -ESTALE;
		goto out_iput;
	}

	/*
	 * VFS authorizes pathname removal via the parent directory, not base
	 * file write access. The shared helper enforces NTFS delete restrictions.
	 */
	err = ntfs_remove_named_stream(NTFS_I(base_vi), stream_name,
			stream_len, dentry->d_inode);

out_iput:
	iput(base_vi);
out_free:
	kmem_cache_free(ntfs_name_cache, stream_name);
	kmem_cache_free(ntfs_name_cache, base_name);
	return err;
}

/* Obtain a dentry for the base inode behind a stream inode. */
static struct dentry *ntfs_stream_base_dentry(struct inode *inode)
{
	struct inode *base_vi =
		VFS_I(NTFS_I(inode)->ext.base_ntfs_ino);
	struct dentry *dentry;

	dentry = d_find_alias(base_vi);
	if (dentry)
		return dentry;
	if (!igrab(base_vi))
		return ERR_PTR(-ESTALE);
	return d_obtain_alias(base_vi);
}

/*
 * ntfs_stream_validate_for_mutation() - Validate a stream before mutation
 * @inode: stream inode to validate
 * @nowait: do not wait for the base MFT-record lock
 *
 * Return: 0 if the backing DATA attribute is valid, -EAGAIN if a nonblocking
 * lock attempt fails, or another negative errno.
 */
int ntfs_stream_validate_for_mutation(struct inode *inode, bool nowait)
{
	struct ntfs_inode *base_ni = NTFS_I(inode)->ext.base_ntfs_ino;
	int err;

	if (nowait) {
		if (!mutex_trylock(&base_ni->mrec_lock))
			return -EAGAIN;
	} else {
		mutex_lock(&base_ni->mrec_lock);
	}
	err = ntfs_stream_inode_validate(inode);
	mutex_unlock(&base_ni->mrec_lock);
	return err;
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

/* Stream permission checks use the base inode's permissions. */
static int ntfs_stream_permission(const struct mnt_idmap *idmap,
		struct inode *inode, int mask)
{
	return inode_permission(idmap,
			VFS_I(NTFS_I(inode)->ext.base_ntfs_ino), mask);
}

/* Report base metadata with the stream's own size and allocation. */
static int ntfs_stream_getattr(const struct mnt_idmap *idmap,
		const struct path *path, struct kstat *stat,
		unsigned int request_mask, unsigned int query_flags)
{
	struct inode *inode = d_backing_inode(path->dentry);
	struct ntfs_inode *ni = NTFS_I(inode);
	struct inode *base_vi = VFS_I(ni->ext.base_ntfs_ino);

	generic_fillattr(idmap, request_mask, base_vi, stat);
	stat->size = i_size_read(inode);
	stat->blocks = (((u64)ni->i_dealloc_clusters <<
			NTFS_SB(inode->i_sb)->cluster_size_bits) >> 9) +
			inode->i_blocks;
	stat->blksize = NTFS_SB(inode->i_sb)->cluster_size;
	stat->result_mask |= STATX_BTIME;
	stat->btime = NTFS_I(base_vi)->i_crtime;

	if (NInoCompressed(ni))
		stat->attributes |= STATX_ATTR_COMPRESSED;
	if (NInoEncrypted(ni))
		stat->attributes |= STATX_ATTR_ENCRYPTED;
	if (base_vi->i_flags & S_IMMUTABLE)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (base_vi->i_flags & S_APPEND)
		stat->attributes |= STATX_ATTR_APPEND;
	stat->attributes_mask |= STATX_ATTR_COMPRESSED | STATX_ATTR_ENCRYPTED |
			STATX_ATTR_IMMUTABLE | STATX_ATTR_APPEND;
	stat->mode = (stat->mode & ~S_IFMT) | S_IFREG;

	if (request_mask & STATX_DIOALIGN) {
		unsigned int align =
			bdev_logical_block_size(inode->i_sb->s_bdev);

		stat->result_mask |= STATX_DIOALIGN;
		if (!NInoCompressed(ni) && !NInoEncrypted(ni)) {
			stat->dio_mem_align = align;
			stat->dio_offset_align = align;
		}
	}

	return 0;
}

/*
 * Apply size changes to the stream and route shared inode metadata changes to
 * its base inode.
 */
static int ntfs_stream_setattr_common(const struct mnt_idmap *idmap,
		struct inode *inode, struct iattr *attr)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	struct inode *base_vi = VFS_I(ni->ext.base_ntfs_ino);
	struct dentry *base_dentry = NULL;
	struct iattr base_attr = *attr;
	int err;

	base_attr.ia_valid &= ~ATTR_FILE;
	base_attr.ia_file = NULL;
	if (base_attr.ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		base_attr.ia_valid &= ~ATTR_MODE;
	else if (base_attr.ia_valid & ATTR_MODE)
		base_attr.ia_mode = (base_attr.ia_mode & ~S_IFMT) |
				(base_vi->i_mode & S_IFMT);

	if (attr->ia_valid & ATTR_SIZE) {
		err = inode_permission(idmap, base_vi, MAY_WRITE);
		if (err)
			goto out;
		if (IS_APPEND(base_vi) || IS_IMMUTABLE(base_vi)) {
			err = -EPERM;
			goto out;
		}
		if (!(ni->vol->vol_flags & VOLUME_IS_DIRTY)) {
			err = ntfs_set_volume_flags(ni->vol, VOLUME_IS_DIRTY);
			if (err)
				goto out;
		}
		base_attr.ia_valid &= ~ATTR_SIZE;
	}

	if ((attr->ia_valid & ATTR_SIZE) || base_attr.ia_valid) {
		base_dentry = ntfs_stream_base_dentry(inode);
		if (IS_ERR(base_dentry)) {
			err = PTR_ERR(base_dentry);
			base_dentry = NULL;
			goto out;
		}
		inode_lock_nested(base_vi, I_MUTEX_PARENT);
		err = ntfs_stream_validate_for_mutation(inode, false);
		if (err) {
			inode_unlock(base_vi);
			goto out;
		}
		if (base_attr.ia_valid)
			err = notify_change(idmap, base_dentry, &base_attr,
					NULL);
		else
			err = 0;
		if (!err)
			ntfs_stream_inode_refresh(inode);
		inode_unlock(base_vi);
		if (err)
			goto out;
	}

	if (attr->ia_valid & ATTR_SIZE)
		err = ntfs_setattr_size(inode, attr);
	else
		err = 0;
out:
	dput(base_dentry);
	return err;
}

/* Hold the stream against unlink while applying VFS setattr operations. */
static int ntfs_stream_setattr(const struct mnt_idmap *idmap,
		struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct ntfs_inode *ni = NTFS_I(inode);
	bool claimed = false;
	int err;

	if (NVolShutdown(ni->vol))
		return -EIO;

	if (!(attr->ia_valid & ATTR_FILE)) {
		err = ntfs_stream_claim(inode);
		if (err)
			return err;
		claimed = true;
	}

	err = ntfs_stream_setattr_common(idmap, inode, attr);
	if (claimed)
		ntfs_stream_put(inode);
	return err;
}

/*
 * Apply stream timestamp updates to the base inode after validating the
 * backing attribute under its MFT-record lock. Nonblocking callers get
 * -EAGAIN.
 */
static int ntfs_stream_update_time_common(struct inode *inode,
		enum fs_update_time type, unsigned int flags)
{
	struct ntfs_inode *base_ni = NTFS_I(inode)->ext.base_ntfs_ino;
	struct inode *base_vi = VFS_I(base_ni);
	int err;

	if (NVolShutdown(base_ni->vol))
		return -EIO;
	if (flags & IOCB_NOWAIT)
		return -EAGAIN;

	mutex_lock(&base_ni->mrec_lock);
	if (NVolShutdown(base_ni->vol)) {
		err = -EIO;
		goto out_mrec;
	}
	err = ntfs_stream_inode_validate(inode);
	if (!err) {
		err = generic_update_time(base_vi, type, flags);
		if (!err)
			ntfs_stream_inode_refresh(inode);
	}
out_mrec:
	mutex_unlock(&base_ni->mrec_lock);
	return err;
}

static int ntfs_stream_update_time(struct inode *inode,
		enum fs_update_time type, unsigned int flags)
{
	return ntfs_stream_update_time_common(inode, type, flags);
}

static ssize_t ntfs_stream_listxattr(struct dentry *dentry, char *buffer,
		size_t size)
{
	return -EOPNOTSUPP;
}

#ifdef CONFIG_NTFS_FS_POSIX_ACL
static struct posix_acl *ntfs_stream_get_acl(const struct mnt_idmap *idmap,
		struct dentry *dentry, int type)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static int ntfs_stream_set_acl(const struct mnt_idmap *idmap,
		struct dentry *dentry, struct posix_acl *acl, int type)
{
	return -EOPNOTSUPP;
}
#endif

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

	err = ntfs_stream_finish_remove(attr_vi);
	if (!err)
		ntfs_stream_update_base_time(ni);
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

/*
 * Validate access against the base inode and hold the stream against unlink
 * until the file is released.
 */
static int ntfs_stream_file_open(struct inode *inode, struct file *file)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	struct ntfs_inode *base_ni = ni->ext.base_ntfs_ino;
	struct inode *base_vi = VFS_I(base_ni);
	int mask = MAY_OPEN;
	int err;

	if (file->f_mode & FMODE_READ)
		mask |= MAY_READ;
	if (file->f_mode & FMODE_WRITE)
		mask |= MAY_WRITE;
	err = inode_permission(file_mnt_idmap(file), base_vi, mask);
	if (err)
		return err;

	mutex_lock(&base_ni->mrec_lock);
	if (NInoStreamUnlinked(ni))
		err = -ENOENT;
	else
		err = ntfs_stream_inode_validate(inode);
	if (err)
		goto out_unlock;
	if ((file->f_mode & FMODE_WRITE) &&
	    (IS_IMMUTABLE(base_vi) ||
	     (IS_APPEND(base_vi) && !(file->f_flags & O_APPEND)))) {
		err = -EPERM;
		goto out_unlock;
	}
	if ((file->f_flags & O_TRUNC) && IS_APPEND(base_vi)) {
		err = -EPERM;
		goto out_unlock;
	}
	if ((file->f_flags & O_NOATIME) &&
	    !inode_owner_or_capable(file_mnt_idmap(file), base_vi)) {
		err = -EPERM;
		goto out_unlock;
	}
	err = ntfs_file_open(inode, file);
	if (err)
		goto out_unlock;
	if (file->f_mode & FMODE_WRITE) {
		err = get_write_access(base_vi);
		if (err)
			goto out_unlock;
		file->private_data = base_vi;
	}

	atomic_inc(&ni->stream_open_count);
out_unlock:
	mutex_unlock(&base_ni->mrec_lock);
	return err;
}

/* Drop open-time references and complete any deferred stream unlink. */
static int ntfs_stream_file_release(struct inode *inode, struct file *file)
{
	int err, remove_err;

	err = ntfs_file_release(inode, file);
	if (file->private_data) {
		put_write_access(file->private_data);
		file->private_data = NULL;
	}
	remove_err = ntfs_stream_put(inode);
	if (!err)
		err = remove_err;
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
		inode_dio_wait(stream_vi);
		/* Exclude faults before taking MFT record and folio locks. */
		filemap_invalidate_lock(stream_vi->i_mapping);
		err = inode_newsize_ok(stream_vi, pos + data_size);
		if (!err) {
			ret = ntfs_inode_attr_pwrite(stream_vi, pos, data_size,
					req->buffer + req->name_len, false);
			if (ret < 0)
				err = ret;
			else
				req->bytes_returned = ret;
		}
		filemap_invalidate_unlock(stream_vi->i_mapping);
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

const struct file_operations ntfs_stream_file_ops = {
	.llseek		= ntfs_file_llseek,
	.read_iter	= ntfs_file_read_iter,
	.write_iter	= ntfs_file_write_iter,
	.fsync		= ntfs_file_fsync,
	.fallocate	= ntfs_fallocate,
	.mmap_prepare	= ntfs_file_mmap_prepare,
	.open		= ntfs_stream_file_open,
	.release	= ntfs_stream_file_release,
	.splice_read	= ntfs_file_splice_read,
	.splice_write	= iter_file_splice_write,
};

const struct inode_operations ntfs_stream_inode_ops = {
	.permission	= ntfs_stream_permission,
	.setattr	= ntfs_stream_setattr,
	.getattr	= ntfs_stream_getattr,
	.listxattr	= ntfs_stream_listxattr,
#ifdef CONFIG_NTFS_FS_POSIX_ACL
	.get_acl	= ntfs_stream_get_acl,
	.set_acl	= ntfs_stream_set_acl,
#endif
	.update_time	= ntfs_stream_update_time,
	.fiemap		= ntfs_fiemap,
};
