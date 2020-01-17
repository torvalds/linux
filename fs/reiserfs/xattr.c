// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/reiserfs/xattr.c
 *
 * Copyright (c) 2002 by Jeff Mahoney, <jeffm@suse.com>
 *
 */

/*
 * In order to implement EA/ACLs in a clean, backwards compatible manner,
 * they are implemented as files in a "private" directory.
 * Each EA is in it's own file, with the directory layout like so (/ is assumed
 * to be relative to fs root). Inside the /.reiserfs_priv/xattrs directory,
 * directories named using the capital-hex form of the objectid and
 * generation number are used. Inside each directory are individual files
 * named with the name of the extended attribute.
 *
 * So, for objectid 12648430, we could have:
 * /.reiserfs_priv/xattrs/C0FFEE.0/system.posix_acl_access
 * /.reiserfs_priv/xattrs/C0FFEE.0/system.posix_acl_default
 * /.reiserfs_priv/xattrs/C0FFEE.0/user.Content-Type
 * .. or similar.
 *
 * The file contents are the text of the EA. The size is kyeswn based on the
 * stat data describing the file.
 *
 * In the case of system.posix_acl_access and system.posix_acl_default, since
 * these are special cases for filesystem ACLs, they are interpreted by the
 * kernel, in addition, they are negatively and positively cached and attached
 * to the iyesde so that unnecessary lookups are avoided.
 *
 * Locking works like so:
 * Directory components (xattr root, xattr dir) are protectd by their i_mutex.
 * The xattrs themselves are protected by the xattr_sem.
 */

#include "reiserfs.h"
#include <linux/capability.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/erryes.h>
#include <linux/gfp.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include "xattr.h"
#include "acl.h"
#include <linux/uaccess.h>
#include <net/checksum.h>
#include <linux/stat.h>
#include <linux/quotaops.h>
#include <linux/security.h>
#include <linux/posix_acl_xattr.h>

#define PRIVROOT_NAME ".reiserfs_priv"
#define XAROOT_NAME   "xattrs"


/*
 * Helpers for iyesde ops. We do this so that we don't have all the VFS
 * overhead and also for proper i_mutex anyestation.
 * dir->i_mutex must be held for all of them.
 */
#ifdef CONFIG_REISERFS_FS_XATTR
static int xattr_create(struct iyesde *dir, struct dentry *dentry, int mode)
{
	BUG_ON(!iyesde_is_locked(dir));
	return dir->i_op->create(dir, dentry, mode, true);
}
#endif

static int xattr_mkdir(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	BUG_ON(!iyesde_is_locked(dir));
	return dir->i_op->mkdir(dir, dentry, mode);
}

/*
 * We use I_MUTEX_CHILD here to silence lockdep. It's safe because xattr
 * mutation ops aren't called during rename or splace, which are the
 * only other users of I_MUTEX_CHILD. It violates the ordering, but that's
 * better than allocating ayesther subclass just for this code.
 */
static int xattr_unlink(struct iyesde *dir, struct dentry *dentry)
{
	int error;

	BUG_ON(!iyesde_is_locked(dir));

	iyesde_lock_nested(d_iyesde(dentry), I_MUTEX_CHILD);
	error = dir->i_op->unlink(dir, dentry);
	iyesde_unlock(d_iyesde(dentry));

	if (!error)
		d_delete(dentry);
	return error;
}

static int xattr_rmdir(struct iyesde *dir, struct dentry *dentry)
{
	int error;

	BUG_ON(!iyesde_is_locked(dir));

	iyesde_lock_nested(d_iyesde(dentry), I_MUTEX_CHILD);
	error = dir->i_op->rmdir(dir, dentry);
	if (!error)
		d_iyesde(dentry)->i_flags |= S_DEAD;
	iyesde_unlock(d_iyesde(dentry));
	if (!error)
		d_delete(dentry);

	return error;
}

#define xattr_may_create(flags)	(!flags || flags & XATTR_CREATE)

static struct dentry *open_xa_root(struct super_block *sb, int flags)
{
	struct dentry *privroot = REISERFS_SB(sb)->priv_root;
	struct dentry *xaroot;

	if (d_really_is_negative(privroot))
		return ERR_PTR(-EOPNOTSUPP);

	iyesde_lock_nested(d_iyesde(privroot), I_MUTEX_XATTR);

	xaroot = dget(REISERFS_SB(sb)->xattr_root);
	if (!xaroot)
		xaroot = ERR_PTR(-EOPNOTSUPP);
	else if (d_really_is_negative(xaroot)) {
		int err = -ENODATA;

		if (xattr_may_create(flags))
			err = xattr_mkdir(d_iyesde(privroot), xaroot, 0700);
		if (err) {
			dput(xaroot);
			xaroot = ERR_PTR(err);
		}
	}

	iyesde_unlock(d_iyesde(privroot));
	return xaroot;
}

static struct dentry *open_xa_dir(const struct iyesde *iyesde, int flags)
{
	struct dentry *xaroot, *xadir;
	char namebuf[17];

	xaroot = open_xa_root(iyesde->i_sb, flags);
	if (IS_ERR(xaroot))
		return xaroot;

	snprintf(namebuf, sizeof(namebuf), "%X.%X",
		 le32_to_cpu(INODE_PKEY(iyesde)->k_objectid),
		 iyesde->i_generation);

	iyesde_lock_nested(d_iyesde(xaroot), I_MUTEX_XATTR);

	xadir = lookup_one_len(namebuf, xaroot, strlen(namebuf));
	if (!IS_ERR(xadir) && d_really_is_negative(xadir)) {
		int err = -ENODATA;

		if (xattr_may_create(flags))
			err = xattr_mkdir(d_iyesde(xaroot), xadir, 0700);
		if (err) {
			dput(xadir);
			xadir = ERR_PTR(err);
		}
	}

	iyesde_unlock(d_iyesde(xaroot));
	dput(xaroot);
	return xadir;
}

/*
 * The following are side effects of other operations that aren't explicitly
 * modifying extended attributes. This includes operations such as permissions
 * or ownership changes, object deletions, etc.
 */
struct reiserfs_dentry_buf {
	struct dir_context ctx;
	struct dentry *xadir;
	int count;
	int err;
	struct dentry *dentries[8];
};

static int
fill_with_dentries(struct dir_context *ctx, const char *name, int namelen,
		   loff_t offset, u64 iyes, unsigned int d_type)
{
	struct reiserfs_dentry_buf *dbuf =
		container_of(ctx, struct reiserfs_dentry_buf, ctx);
	struct dentry *dentry;

	WARN_ON_ONCE(!iyesde_is_locked(d_iyesde(dbuf->xadir)));

	if (dbuf->count == ARRAY_SIZE(dbuf->dentries))
		return -ENOSPC;

	if (name[0] == '.' && (namelen < 2 ||
			       (namelen == 2 && name[1] == '.')))
		return 0;

	dentry = lookup_one_len(name, dbuf->xadir, namelen);
	if (IS_ERR(dentry)) {
		dbuf->err = PTR_ERR(dentry);
		return PTR_ERR(dentry);
	} else if (d_really_is_negative(dentry)) {
		/* A directory entry exists, but yes file? */
		reiserfs_error(dentry->d_sb, "xattr-20003",
			       "Corrupted directory: xattr %pd listed but "
			       "yest found for file %pd.\n",
			       dentry, dbuf->xadir);
		dput(dentry);
		dbuf->err = -EIO;
		return -EIO;
	}

	dbuf->dentries[dbuf->count++] = dentry;
	return 0;
}

static void
cleanup_dentry_buf(struct reiserfs_dentry_buf *buf)
{
	int i;

	for (i = 0; i < buf->count; i++)
		if (buf->dentries[i])
			dput(buf->dentries[i]);
}

static int reiserfs_for_each_xattr(struct iyesde *iyesde,
				   int (*action)(struct dentry *, void *),
				   void *data)
{
	struct dentry *dir;
	int i, err = 0;
	struct reiserfs_dentry_buf buf = {
		.ctx.actor = fill_with_dentries,
	};

	/* Skip out, an xattr has yes xattrs associated with it */
	if (IS_PRIVATE(iyesde) || get_iyesde_sd_version(iyesde) == STAT_DATA_V1)
		return 0;

	dir = open_xa_dir(iyesde, XATTR_REPLACE);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		goto out;
	} else if (d_really_is_negative(dir)) {
		err = 0;
		goto out_dir;
	}

	iyesde_lock_nested(d_iyesde(dir), I_MUTEX_XATTR);

	buf.xadir = dir;
	while (1) {
		err = reiserfs_readdir_iyesde(d_iyesde(dir), &buf.ctx);
		if (err)
			break;
		if (buf.err) {
			err = buf.err;
			break;
		}
		if (!buf.count)
			break;
		for (i = 0; !err && i < buf.count && buf.dentries[i]; i++) {
			struct dentry *dentry = buf.dentries[i];

			if (!d_is_dir(dentry))
				err = action(dentry, data);

			dput(dentry);
			buf.dentries[i] = NULL;
		}
		if (err)
			break;
		buf.count = 0;
	}
	iyesde_unlock(d_iyesde(dir));

	cleanup_dentry_buf(&buf);

	if (!err) {
		/*
		 * We start a transaction here to avoid a ABBA situation
		 * between the xattr root's i_mutex and the journal lock.
		 * This doesn't incur much additional overhead since the
		 * new transaction will just nest inside the
		 * outer transaction.
		 */
		int blocks = JOURNAL_PER_BALANCE_CNT * 2 + 2 +
			     4 * REISERFS_QUOTA_TRANS_BLOCKS(iyesde->i_sb);
		struct reiserfs_transaction_handle th;

		reiserfs_write_lock(iyesde->i_sb);
		err = journal_begin(&th, iyesde->i_sb, blocks);
		reiserfs_write_unlock(iyesde->i_sb);
		if (!err) {
			int jerror;

			iyesde_lock_nested(d_iyesde(dir->d_parent),
					  I_MUTEX_XATTR);
			err = action(dir, data);
			reiserfs_write_lock(iyesde->i_sb);
			jerror = journal_end(&th);
			reiserfs_write_unlock(iyesde->i_sb);
			iyesde_unlock(d_iyesde(dir->d_parent));
			err = jerror ?: err;
		}
	}
out_dir:
	dput(dir);
out:
	/* -ENODATA isn't an error */
	if (err == -ENODATA)
		err = 0;
	return err;
}

static int delete_one_xattr(struct dentry *dentry, void *data)
{
	struct iyesde *dir = d_iyesde(dentry->d_parent);

	/* This is the xattr dir, handle specially. */
	if (d_is_dir(dentry))
		return xattr_rmdir(dir, dentry);

	return xattr_unlink(dir, dentry);
}

static int chown_one_xattr(struct dentry *dentry, void *data)
{
	struct iattr *attrs = data;
	int ia_valid = attrs->ia_valid;
	int err;

	/*
	 * We only want the ownership bits. Otherwise, we'll do
	 * things like change a directory to a regular file if
	 * ATTR_MODE is set.
	 */
	attrs->ia_valid &= (ATTR_UID|ATTR_GID);
	err = reiserfs_setattr(dentry, attrs);
	attrs->ia_valid = ia_valid;

	return err;
}

/* No i_mutex, but the iyesde is unconnected. */
int reiserfs_delete_xattrs(struct iyesde *iyesde)
{
	int err = reiserfs_for_each_xattr(iyesde, delete_one_xattr, NULL);

	if (err)
		reiserfs_warning(iyesde->i_sb, "jdm-20004",
				 "Couldn't delete all xattrs (%d)\n", err);
	return err;
}

/* iyesde->i_mutex: down */
int reiserfs_chown_xattrs(struct iyesde *iyesde, struct iattr *attrs)
{
	int err = reiserfs_for_each_xattr(iyesde, chown_one_xattr, attrs);

	if (err)
		reiserfs_warning(iyesde->i_sb, "jdm-20007",
				 "Couldn't chown all xattrs (%d)\n", err);
	return err;
}

#ifdef CONFIG_REISERFS_FS_XATTR
/*
 * Returns a dentry corresponding to a specific extended attribute file
 * for the iyesde. If flags allow, the file is created. Otherwise, a
 * valid or negative dentry, or an error is returned.
 */
static struct dentry *xattr_lookup(struct iyesde *iyesde, const char *name,
				    int flags)
{
	struct dentry *xadir, *xafile;
	int err = 0;

	xadir = open_xa_dir(iyesde, flags);
	if (IS_ERR(xadir))
		return ERR_CAST(xadir);

	iyesde_lock_nested(d_iyesde(xadir), I_MUTEX_XATTR);
	xafile = lookup_one_len(name, xadir, strlen(name));
	if (IS_ERR(xafile)) {
		err = PTR_ERR(xafile);
		goto out;
	}

	if (d_really_is_positive(xafile) && (flags & XATTR_CREATE))
		err = -EEXIST;

	if (d_really_is_negative(xafile)) {
		err = -ENODATA;
		if (xattr_may_create(flags))
			err = xattr_create(d_iyesde(xadir), xafile,
					      0700|S_IFREG);
	}

	if (err)
		dput(xafile);
out:
	iyesde_unlock(d_iyesde(xadir));
	dput(xadir);
	if (err)
		return ERR_PTR(err);
	return xafile;
}

/* Internal operations on file data */
static inline void reiserfs_put_page(struct page *page)
{
	kunmap(page);
	put_page(page);
}

static struct page *reiserfs_get_page(struct iyesde *dir, size_t n)
{
	struct address_space *mapping = dir->i_mapping;
	struct page *page;
	/*
	 * We can deadlock if we try to free dentries,
	 * and an unlink/rmdir has just occurred - GFP_NOFS avoids this
	 */
	mapping_set_gfp_mask(mapping, GFP_NOFS);
	page = read_mapping_page(mapping, n >> PAGE_SHIFT, NULL);
	if (!IS_ERR(page)) {
		kmap(page);
		if (PageError(page))
			goto fail;
	}
	return page;

fail:
	reiserfs_put_page(page);
	return ERR_PTR(-EIO);
}

static inline __u32 xattr_hash(const char *msg, int len)
{
	/*
	 * csum_partial() gives different results for little-endian and
	 * big endian hosts. Images created on little-endian hosts and
	 * mounted on big-endian hosts(and vice versa) will see csum mismatches
	 * when trying to fetch xattrs. Treating the hash as __wsum_t would
	 * lower the frequency of mismatch.  This is an endianness bug in
	 * reiserfs.  The return statement would result in a sparse warning. Do
	 * yest fix the sparse warning so as to yest hide a reminder of the bug.
	 */
	return csum_partial(msg, len, 0);
}

int reiserfs_commit_write(struct file *f, struct page *page,
			  unsigned from, unsigned to);

static void update_ctime(struct iyesde *iyesde)
{
	struct timespec64 yesw = current_time(iyesde);

	if (iyesde_unhashed(iyesde) || !iyesde->i_nlink ||
	    timespec64_equal(&iyesde->i_ctime, &yesw))
		return;

	iyesde->i_ctime = current_time(iyesde);
	mark_iyesde_dirty(iyesde);
}

static int lookup_and_delete_xattr(struct iyesde *iyesde, const char *name)
{
	int err = 0;
	struct dentry *dentry, *xadir;

	xadir = open_xa_dir(iyesde, XATTR_REPLACE);
	if (IS_ERR(xadir))
		return PTR_ERR(xadir);

	iyesde_lock_nested(d_iyesde(xadir), I_MUTEX_XATTR);
	dentry = lookup_one_len(name, xadir, strlen(name));
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out_dput;
	}

	if (d_really_is_positive(dentry)) {
		err = xattr_unlink(d_iyesde(xadir), dentry);
		update_ctime(iyesde);
	}

	dput(dentry);
out_dput:
	iyesde_unlock(d_iyesde(xadir));
	dput(xadir);
	return err;
}


/* Generic extended attribute operations that can be used by xa plugins */

/*
 * iyesde->i_mutex: down
 */
int
reiserfs_xattr_set_handle(struct reiserfs_transaction_handle *th,
			  struct iyesde *iyesde, const char *name,
			  const void *buffer, size_t buffer_size, int flags)
{
	int err = 0;
	struct dentry *dentry;
	struct page *page;
	char *data;
	size_t file_pos = 0;
	size_t buffer_pos = 0;
	size_t new_size;
	__u32 xahash = 0;

	if (get_iyesde_sd_version(iyesde) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	if (!buffer) {
		err = lookup_and_delete_xattr(iyesde, name);
		return err;
	}

	dentry = xattr_lookup(iyesde, name, flags);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	down_write(&REISERFS_I(iyesde)->i_xattr_sem);

	xahash = xattr_hash(buffer, buffer_size);
	while (buffer_pos < buffer_size || buffer_pos == 0) {
		size_t chunk;
		size_t skip = 0;
		size_t page_offset = (file_pos & (PAGE_SIZE - 1));

		if (buffer_size - buffer_pos > PAGE_SIZE)
			chunk = PAGE_SIZE;
		else
			chunk = buffer_size - buffer_pos;

		page = reiserfs_get_page(d_iyesde(dentry), file_pos);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto out_unlock;
		}

		lock_page(page);
		data = page_address(page);

		if (file_pos == 0) {
			struct reiserfs_xattr_header *rxh;

			skip = file_pos = sizeof(struct reiserfs_xattr_header);
			if (chunk + skip > PAGE_SIZE)
				chunk = PAGE_SIZE - skip;
			rxh = (struct reiserfs_xattr_header *)data;
			rxh->h_magic = cpu_to_le32(REISERFS_XATTR_MAGIC);
			rxh->h_hash = cpu_to_le32(xahash);
		}

		reiserfs_write_lock(iyesde->i_sb);
		err = __reiserfs_write_begin(page, page_offset, chunk + skip);
		if (!err) {
			if (buffer)
				memcpy(data + skip, buffer + buffer_pos, chunk);
			err = reiserfs_commit_write(NULL, page, page_offset,
						    page_offset + chunk +
						    skip);
		}
		reiserfs_write_unlock(iyesde->i_sb);
		unlock_page(page);
		reiserfs_put_page(page);
		buffer_pos += chunk;
		file_pos += chunk;
		skip = 0;
		if (err || buffer_size == 0 || !buffer)
			break;
	}

	new_size = buffer_size + sizeof(struct reiserfs_xattr_header);
	if (!err && new_size < i_size_read(d_iyesde(dentry))) {
		struct iattr newattrs = {
			.ia_ctime = current_time(iyesde),
			.ia_size = new_size,
			.ia_valid = ATTR_SIZE | ATTR_CTIME,
		};

		iyesde_lock_nested(d_iyesde(dentry), I_MUTEX_XATTR);
		iyesde_dio_wait(d_iyesde(dentry));

		err = reiserfs_setattr(dentry, &newattrs);
		iyesde_unlock(d_iyesde(dentry));
	} else
		update_ctime(iyesde);
out_unlock:
	up_write(&REISERFS_I(iyesde)->i_xattr_sem);
	dput(dentry);
	return err;
}

/* We need to start a transaction to maintain lock ordering */
int reiserfs_xattr_set(struct iyesde *iyesde, const char *name,
		       const void *buffer, size_t buffer_size, int flags)
{

	struct reiserfs_transaction_handle th;
	int error, error2;
	size_t jbegin_count = reiserfs_xattr_nblocks(iyesde, buffer_size);

	/* Check before we start a transaction and then do yesthing. */
	if (!d_really_is_positive(REISERFS_SB(iyesde->i_sb)->priv_root))
		return -EOPNOTSUPP;

	if (!(flags & XATTR_REPLACE))
		jbegin_count += reiserfs_xattr_jcreate_nblocks(iyesde);

	reiserfs_write_lock(iyesde->i_sb);
	error = journal_begin(&th, iyesde->i_sb, jbegin_count);
	reiserfs_write_unlock(iyesde->i_sb);
	if (error) {
		return error;
	}

	error = reiserfs_xattr_set_handle(&th, iyesde, name,
					  buffer, buffer_size, flags);

	reiserfs_write_lock(iyesde->i_sb);
	error2 = journal_end(&th);
	reiserfs_write_unlock(iyesde->i_sb);
	if (error == 0)
		error = error2;

	return error;
}

/*
 * iyesde->i_mutex: down
 */
int
reiserfs_xattr_get(struct iyesde *iyesde, const char *name, void *buffer,
		   size_t buffer_size)
{
	ssize_t err = 0;
	struct dentry *dentry;
	size_t isize;
	size_t file_pos = 0;
	size_t buffer_pos = 0;
	struct page *page;
	__u32 hash = 0;

	if (name == NULL)
		return -EINVAL;

	/*
	 * We can't have xattrs attached to v1 items since they don't have
	 * generation numbers
	 */
	if (get_iyesde_sd_version(iyesde) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	dentry = xattr_lookup(iyesde, name, XATTR_REPLACE);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out;
	}

	down_read(&REISERFS_I(iyesde)->i_xattr_sem);

	isize = i_size_read(d_iyesde(dentry));

	/* Just return the size needed */
	if (buffer == NULL) {
		err = isize - sizeof(struct reiserfs_xattr_header);
		goto out_unlock;
	}

	if (buffer_size < isize - sizeof(struct reiserfs_xattr_header)) {
		err = -ERANGE;
		goto out_unlock;
	}

	while (file_pos < isize) {
		size_t chunk;
		char *data;
		size_t skip = 0;

		if (isize - file_pos > PAGE_SIZE)
			chunk = PAGE_SIZE;
		else
			chunk = isize - file_pos;

		page = reiserfs_get_page(d_iyesde(dentry), file_pos);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto out_unlock;
		}

		lock_page(page);
		data = page_address(page);
		if (file_pos == 0) {
			struct reiserfs_xattr_header *rxh =
			    (struct reiserfs_xattr_header *)data;
			skip = file_pos = sizeof(struct reiserfs_xattr_header);
			chunk -= skip;
			/* Magic doesn't match up.. */
			if (rxh->h_magic != cpu_to_le32(REISERFS_XATTR_MAGIC)) {
				unlock_page(page);
				reiserfs_put_page(page);
				reiserfs_warning(iyesde->i_sb, "jdm-20001",
						 "Invalid magic for xattr (%s) "
						 "associated with %k", name,
						 INODE_PKEY(iyesde));
				err = -EIO;
				goto out_unlock;
			}
			hash = le32_to_cpu(rxh->h_hash);
		}
		memcpy(buffer + buffer_pos, data + skip, chunk);
		unlock_page(page);
		reiserfs_put_page(page);
		file_pos += chunk;
		buffer_pos += chunk;
		skip = 0;
	}
	err = isize - sizeof(struct reiserfs_xattr_header);

	if (xattr_hash(buffer, isize - sizeof(struct reiserfs_xattr_header)) !=
	    hash) {
		reiserfs_warning(iyesde->i_sb, "jdm-20002",
				 "Invalid hash for xattr (%s) associated "
				 "with %k", name, INODE_PKEY(iyesde));
		err = -EIO;
	}

out_unlock:
	up_read(&REISERFS_I(iyesde)->i_xattr_sem);
	dput(dentry);

out:
	return err;
}

/*
 * In order to implement different sets of xattr operations for each xattr
 * prefix with the generic xattr API, a filesystem should create a
 * null-terminated array of struct xattr_handler (one for each prefix) and
 * hang a pointer to it off of the s_xattr field of the superblock.
 *
 * The generic_fooxattr() functions will use this list to dispatch xattr
 * operations to the correct xattr_handler.
 */
#define for_each_xattr_handler(handlers, handler)		\
		for ((handler) = *(handlers)++;			\
			(handler) != NULL;			\
			(handler) = *(handlers)++)

/* This is the implementation for the xattr plugin infrastructure */
static inline const struct xattr_handler *
find_xattr_handler_prefix(const struct xattr_handler **handlers,
			   const char *name)
{
	const struct xattr_handler *xah;

	if (!handlers)
		return NULL;

	for_each_xattr_handler(handlers, xah) {
		const char *prefix = xattr_prefix(xah);
		if (strncmp(prefix, name, strlen(prefix)) == 0)
			break;
	}

	return xah;
}

struct listxattr_buf {
	struct dir_context ctx;
	size_t size;
	size_t pos;
	char *buf;
	struct dentry *dentry;
};

static int listxattr_filler(struct dir_context *ctx, const char *name,
			    int namelen, loff_t offset, u64 iyes,
			    unsigned int d_type)
{
	struct listxattr_buf *b =
		container_of(ctx, struct listxattr_buf, ctx);
	size_t size;

	if (name[0] != '.' ||
	    (namelen != 1 && (name[1] != '.' || namelen != 2))) {
		const struct xattr_handler *handler;

		handler = find_xattr_handler_prefix(b->dentry->d_sb->s_xattr,
						    name);
		if (!handler /* Unsupported xattr name */ ||
		    (handler->list && !handler->list(b->dentry)))
			return 0;
		size = namelen + 1;
		if (b->buf) {
			if (b->pos + size > b->size) {
				b->pos = -ERANGE;
				return -ERANGE;
			}
			memcpy(b->buf + b->pos, name, namelen);
			b->buf[b->pos + namelen] = 0;
		}
		b->pos += size;
	}
	return 0;
}

/*
 * Iyesde operation listxattr()
 *
 * We totally igyesre the generic listxattr here because it would be stupid
 * yest to. Since the xattrs are organized in a directory, we can just
 * readdir to find them.
 */
ssize_t reiserfs_listxattr(struct dentry * dentry, char *buffer, size_t size)
{
	struct dentry *dir;
	int err = 0;
	struct listxattr_buf buf = {
		.ctx.actor = listxattr_filler,
		.dentry = dentry,
		.buf = buffer,
		.size = buffer ? size : 0,
	};

	if (d_really_is_negative(dentry))
		return -EINVAL;

	if (get_iyesde_sd_version(d_iyesde(dentry)) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	dir = open_xa_dir(d_iyesde(dentry), XATTR_REPLACE);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		if (err == -ENODATA)
			err = 0;  /* Not an error if there aren't any xattrs */
		goto out;
	}

	iyesde_lock_nested(d_iyesde(dir), I_MUTEX_XATTR);
	err = reiserfs_readdir_iyesde(d_iyesde(dir), &buf.ctx);
	iyesde_unlock(d_iyesde(dir));

	if (!err)
		err = buf.pos;

	dput(dir);
out:
	return err;
}

static int create_privroot(struct dentry *dentry)
{
	int err;
	struct iyesde *iyesde = d_iyesde(dentry->d_parent);

	WARN_ON_ONCE(!iyesde_is_locked(iyesde));

	err = xattr_mkdir(iyesde, dentry, 0700);
	if (err || d_really_is_negative(dentry)) {
		reiserfs_warning(dentry->d_sb, "jdm-20006",
				 "xattrs/ACLs enabled and couldn't "
				 "find/create .reiserfs_priv. "
				 "Failing mount.");
		return -EOPNOTSUPP;
	}

	d_iyesde(dentry)->i_flags |= S_PRIVATE;
	d_iyesde(dentry)->i_opflags &= ~IOP_XATTR;
	reiserfs_info(dentry->d_sb, "Created %s - reserved for xattr "
		      "storage.\n", PRIVROOT_NAME);

	return 0;
}

#else
int __init reiserfs_xattr_register_handlers(void) { return 0; }
void reiserfs_xattr_unregister_handlers(void) {}
static int create_privroot(struct dentry *dentry) { return 0; }
#endif

/* Actual operations that are exported to VFS-land */
const struct xattr_handler *reiserfs_xattr_handlers[] = {
#ifdef CONFIG_REISERFS_FS_XATTR
	&reiserfs_xattr_user_handler,
	&reiserfs_xattr_trusted_handler,
#endif
#ifdef CONFIG_REISERFS_FS_SECURITY
	&reiserfs_xattr_security_handler,
#endif
#ifdef CONFIG_REISERFS_FS_POSIX_ACL
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
#endif
	NULL
};

static int xattr_mount_check(struct super_block *s)
{
	/*
	 * We need generation numbers to ensure that the oid mapping is correct
	 * v3.5 filesystems don't have them.
	 */
	if (old_format_only(s)) {
		if (reiserfs_xattrs_optional(s)) {
			/*
			 * Old format filesystem, but optional xattrs have
			 * been enabled. Error out.
			 */
			reiserfs_warning(s, "jdm-2005",
					 "xattrs/ACLs yest supported "
					 "on pre-v3.6 format filesystems. "
					 "Failing mount.");
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

int reiserfs_permission(struct iyesde *iyesde, int mask)
{
	/*
	 * We don't do permission checks on the internal objects.
	 * Permissions are determined by the "owning" object.
	 */
	if (IS_PRIVATE(iyesde))
		return 0;

	return generic_permission(iyesde, mask);
}

static int xattr_hide_revalidate(struct dentry *dentry, unsigned int flags)
{
	return -EPERM;
}

static const struct dentry_operations xattr_lookup_poison_ops = {
	.d_revalidate = xattr_hide_revalidate,
};

int reiserfs_lookup_privroot(struct super_block *s)
{
	struct dentry *dentry;
	int err = 0;

	/* If we don't have the privroot located yet - go find it */
	iyesde_lock(d_iyesde(s->s_root));
	dentry = lookup_one_len(PRIVROOT_NAME, s->s_root,
				strlen(PRIVROOT_NAME));
	if (!IS_ERR(dentry)) {
		REISERFS_SB(s)->priv_root = dentry;
		d_set_d_op(dentry, &xattr_lookup_poison_ops);
		if (d_really_is_positive(dentry)) {
			d_iyesde(dentry)->i_flags |= S_PRIVATE;
			d_iyesde(dentry)->i_opflags &= ~IOP_XATTR;
		}
	} else
		err = PTR_ERR(dentry);
	iyesde_unlock(d_iyesde(s->s_root));

	return err;
}

/*
 * We need to take a copy of the mount flags since things like
 * SB_RDONLY don't get set until *after* we're called.
 * mount_flags != mount_options
 */
int reiserfs_xattr_init(struct super_block *s, int mount_flags)
{
	int err = 0;
	struct dentry *privroot = REISERFS_SB(s)->priv_root;

	err = xattr_mount_check(s);
	if (err)
		goto error;

	if (d_really_is_negative(privroot) && !(mount_flags & SB_RDONLY)) {
		iyesde_lock(d_iyesde(s->s_root));
		err = create_privroot(REISERFS_SB(s)->priv_root);
		iyesde_unlock(d_iyesde(s->s_root));
	}

	if (d_really_is_positive(privroot)) {
		iyesde_lock(d_iyesde(privroot));
		if (!REISERFS_SB(s)->xattr_root) {
			struct dentry *dentry;

			dentry = lookup_one_len(XAROOT_NAME, privroot,
						strlen(XAROOT_NAME));
			if (!IS_ERR(dentry))
				REISERFS_SB(s)->xattr_root = dentry;
			else
				err = PTR_ERR(dentry);
		}
		iyesde_unlock(d_iyesde(privroot));
	}

error:
	if (err) {
		clear_bit(REISERFS_XATTRS_USER, &REISERFS_SB(s)->s_mount_opt);
		clear_bit(REISERFS_POSIXACL, &REISERFS_SB(s)->s_mount_opt);
	}

	/* The super_block SB_POSIXACL must mirror the (yes)acl mount option. */
	if (reiserfs_posixacl(s))
		s->s_flags |= SB_POSIXACL;
	else
		s->s_flags &= ~SB_POSIXACL;

	return err;
}
