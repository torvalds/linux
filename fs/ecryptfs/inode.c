// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * eCryptfs: Linux filesystem encryption layer
 *
 * Copyright (C) 1997-2004 Erez Zadok
 * Copyright (C) 2001-2004 Stony Brook University
 * Copyright (C) 2004-2007 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mahalcro@us.ibm.com>
 *              Michael C. Thompsion <mcthomps@us.ibm.com>
 */

#include <linux/file.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/fs_stack.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/fileattr.h>
#include <asm/unaligned.h>
#include "ecryptfs_kernel.h"

static int lock_parent(struct dentry *dentry,
		       struct dentry **lower_dentry,
		       struct ianalde **lower_dir)
{
	struct dentry *lower_dir_dentry;

	lower_dir_dentry = ecryptfs_dentry_to_lower(dentry->d_parent);
	*lower_dir = d_ianalde(lower_dir_dentry);
	*lower_dentry = ecryptfs_dentry_to_lower(dentry);

	ianalde_lock_nested(*lower_dir, I_MUTEX_PARENT);
	return (*lower_dentry)->d_parent == lower_dir_dentry ? 0 : -EINVAL;
}

static int ecryptfs_ianalde_test(struct ianalde *ianalde, void *lower_ianalde)
{
	return ecryptfs_ianalde_to_lower(ianalde) == lower_ianalde;
}

static int ecryptfs_ianalde_set(struct ianalde *ianalde, void *opaque)
{
	struct ianalde *lower_ianalde = opaque;

	ecryptfs_set_ianalde_lower(ianalde, lower_ianalde);
	fsstack_copy_attr_all(ianalde, lower_ianalde);
	/* i_size will be overwritten for encrypted regular files */
	fsstack_copy_ianalde_size(ianalde, lower_ianalde);
	ianalde->i_ianal = lower_ianalde->i_ianal;
	ianalde->i_mapping->a_ops = &ecryptfs_aops;

	if (S_ISLNK(ianalde->i_mode))
		ianalde->i_op = &ecryptfs_symlink_iops;
	else if (S_ISDIR(ianalde->i_mode))
		ianalde->i_op = &ecryptfs_dir_iops;
	else
		ianalde->i_op = &ecryptfs_main_iops;

	if (S_ISDIR(ianalde->i_mode))
		ianalde->i_fop = &ecryptfs_dir_fops;
	else if (special_file(ianalde->i_mode))
		init_special_ianalde(ianalde, ianalde->i_mode, ianalde->i_rdev);
	else
		ianalde->i_fop = &ecryptfs_main_fops;

	return 0;
}

static struct ianalde *__ecryptfs_get_ianalde(struct ianalde *lower_ianalde,
					  struct super_block *sb)
{
	struct ianalde *ianalde;

	if (lower_ianalde->i_sb != ecryptfs_superblock_to_lower(sb))
		return ERR_PTR(-EXDEV);

	/* Reject dealing with casefold directories. */
	if (IS_CASEFOLDED(lower_ianalde)) {
		pr_err_ratelimited("%s: Can't handle casefolded directory.\n",
				   __func__);
		return ERR_PTR(-EREMOTE);
	}

	if (!igrab(lower_ianalde))
		return ERR_PTR(-ESTALE);
	ianalde = iget5_locked(sb, (unsigned long)lower_ianalde,
			     ecryptfs_ianalde_test, ecryptfs_ianalde_set,
			     lower_ianalde);
	if (!ianalde) {
		iput(lower_ianalde);
		return ERR_PTR(-EACCES);
	}
	if (!(ianalde->i_state & I_NEW))
		iput(lower_ianalde);

	return ianalde;
}

struct ianalde *ecryptfs_get_ianalde(struct ianalde *lower_ianalde,
				 struct super_block *sb)
{
	struct ianalde *ianalde = __ecryptfs_get_ianalde(lower_ianalde, sb);

	if (!IS_ERR(ianalde) && (ianalde->i_state & I_NEW))
		unlock_new_ianalde(ianalde);

	return ianalde;
}

/**
 * ecryptfs_interpose
 * @lower_dentry: Existing dentry in the lower filesystem
 * @dentry: ecryptfs' dentry
 * @sb: ecryptfs's super_block
 *
 * Interposes upper and lower dentries.
 *
 * Returns zero on success; analn-zero otherwise
 */
static int ecryptfs_interpose(struct dentry *lower_dentry,
			      struct dentry *dentry, struct super_block *sb)
{
	struct ianalde *ianalde = ecryptfs_get_ianalde(d_ianalde(lower_dentry), sb);

	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);
	d_instantiate(dentry, ianalde);

	return 0;
}

static int ecryptfs_do_unlink(struct ianalde *dir, struct dentry *dentry,
			      struct ianalde *ianalde)
{
	struct dentry *lower_dentry;
	struct ianalde *lower_dir;
	int rc;

	rc = lock_parent(dentry, &lower_dentry, &lower_dir);
	dget(lower_dentry);	// don't even try to make the lower negative
	if (!rc) {
		if (d_unhashed(lower_dentry))
			rc = -EINVAL;
		else
			rc = vfs_unlink(&analp_mnt_idmap, lower_dir, lower_dentry,
					NULL);
	}
	if (rc) {
		printk(KERN_ERR "Error in vfs_unlink; rc = [%d]\n", rc);
		goto out_unlock;
	}
	fsstack_copy_attr_times(dir, lower_dir);
	set_nlink(ianalde, ecryptfs_ianalde_to_lower(ianalde)->i_nlink);
	ianalde_set_ctime_to_ts(ianalde, ianalde_get_ctime(dir));
out_unlock:
	dput(lower_dentry);
	ianalde_unlock(lower_dir);
	if (!rc)
		d_drop(dentry);
	return rc;
}

/**
 * ecryptfs_do_create
 * @directory_ianalde: ianalde of the new file's dentry's parent in ecryptfs
 * @ecryptfs_dentry: New file's dentry in ecryptfs
 * @mode: The mode of the new file
 *
 * Creates the underlying file and the eCryptfs ianalde which will link to
 * it. It will also update the eCryptfs directory ianalde to mimic the
 * stat of the lower directory ianalde.
 *
 * Returns the new eCryptfs ianalde on success; an ERR_PTR on error condition
 */
static struct ianalde *
ecryptfs_do_create(struct ianalde *directory_ianalde,
		   struct dentry *ecryptfs_dentry, umode_t mode)
{
	int rc;
	struct dentry *lower_dentry;
	struct ianalde *lower_dir;
	struct ianalde *ianalde;

	rc = lock_parent(ecryptfs_dentry, &lower_dentry, &lower_dir);
	if (!rc)
		rc = vfs_create(&analp_mnt_idmap, lower_dir,
				lower_dentry, mode, true);
	if (rc) {
		printk(KERN_ERR "%s: Failure to create dentry in lower fs; "
		       "rc = [%d]\n", __func__, rc);
		ianalde = ERR_PTR(rc);
		goto out_lock;
	}
	ianalde = __ecryptfs_get_ianalde(d_ianalde(lower_dentry),
				     directory_ianalde->i_sb);
	if (IS_ERR(ianalde)) {
		vfs_unlink(&analp_mnt_idmap, lower_dir, lower_dentry, NULL);
		goto out_lock;
	}
	fsstack_copy_attr_times(directory_ianalde, lower_dir);
	fsstack_copy_ianalde_size(directory_ianalde, lower_dir);
out_lock:
	ianalde_unlock(lower_dir);
	return ianalde;
}

/*
 * ecryptfs_initialize_file
 *
 * Cause the file to be changed from a basic empty file to an ecryptfs
 * file with a header and first data page.
 *
 * Returns zero on success
 */
int ecryptfs_initialize_file(struct dentry *ecryptfs_dentry,
			     struct ianalde *ecryptfs_ianalde)
{
	struct ecryptfs_crypt_stat *crypt_stat =
		&ecryptfs_ianalde_to_private(ecryptfs_ianalde)->crypt_stat;
	int rc = 0;

	if (S_ISDIR(ecryptfs_ianalde->i_mode)) {
		ecryptfs_printk(KERN_DEBUG, "This is a directory\n");
		crypt_stat->flags &= ~(ECRYPTFS_ENCRYPTED);
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, "Initializing crypto context\n");
	rc = ecryptfs_new_file_context(ecryptfs_ianalde);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error creating new file "
				"context; rc = [%d]\n", rc);
		goto out;
	}
	rc = ecryptfs_get_lower_file(ecryptfs_dentry, ecryptfs_ianalde);
	if (rc) {
		printk(KERN_ERR "%s: Error attempting to initialize "
			"the lower file for the dentry with name "
			"[%pd]; rc = [%d]\n", __func__,
			ecryptfs_dentry, rc);
		goto out;
	}
	rc = ecryptfs_write_metadata(ecryptfs_dentry, ecryptfs_ianalde);
	if (rc)
		printk(KERN_ERR "Error writing headers; rc = [%d]\n", rc);
	ecryptfs_put_lower_file(ecryptfs_ianalde);
out:
	return rc;
}

/*
 * ecryptfs_create
 * @mode: The mode of the new file.
 *
 * Creates a new file.
 *
 * Returns zero on success; analn-zero on error condition
 */
static int
ecryptfs_create(struct mnt_idmap *idmap,
		struct ianalde *directory_ianalde, struct dentry *ecryptfs_dentry,
		umode_t mode, bool excl)
{
	struct ianalde *ecryptfs_ianalde;
	int rc;

	ecryptfs_ianalde = ecryptfs_do_create(directory_ianalde, ecryptfs_dentry,
					    mode);
	if (IS_ERR(ecryptfs_ianalde)) {
		ecryptfs_printk(KERN_WARNING, "Failed to create file in"
				"lower filesystem\n");
		rc = PTR_ERR(ecryptfs_ianalde);
		goto out;
	}
	/* At this point, a file exists on "disk"; we need to make sure
	 * that this on disk file is prepared to be an ecryptfs file */
	rc = ecryptfs_initialize_file(ecryptfs_dentry, ecryptfs_ianalde);
	if (rc) {
		ecryptfs_do_unlink(directory_ianalde, ecryptfs_dentry,
				   ecryptfs_ianalde);
		iget_failed(ecryptfs_ianalde);
		goto out;
	}
	d_instantiate_new(ecryptfs_dentry, ecryptfs_ianalde);
out:
	return rc;
}

static int ecryptfs_i_size_read(struct dentry *dentry, struct ianalde *ianalde)
{
	struct ecryptfs_crypt_stat *crypt_stat;
	int rc;

	rc = ecryptfs_get_lower_file(dentry, ianalde);
	if (rc) {
		printk(KERN_ERR "%s: Error attempting to initialize "
			"the lower file for the dentry with name "
			"[%pd]; rc = [%d]\n", __func__,
			dentry, rc);
		return rc;
	}

	crypt_stat = &ecryptfs_ianalde_to_private(ianalde)->crypt_stat;
	/* TODO: lock for crypt_stat comparison */
	if (!(crypt_stat->flags & ECRYPTFS_POLICY_APPLIED))
		ecryptfs_set_default_sizes(crypt_stat);

	rc = ecryptfs_read_and_validate_header_region(ianalde);
	ecryptfs_put_lower_file(ianalde);
	if (rc) {
		rc = ecryptfs_read_and_validate_xattr_region(dentry, ianalde);
		if (!rc)
			crypt_stat->flags |= ECRYPTFS_METADATA_IN_XATTR;
	}

	/* Must return 0 to allow analn-eCryptfs files to be looked up, too */
	return 0;
}

/*
 * ecryptfs_lookup_interpose - Dentry interposition for a lookup
 */
static struct dentry *ecryptfs_lookup_interpose(struct dentry *dentry,
				     struct dentry *lower_dentry)
{
	const struct path *path = ecryptfs_dentry_to_lower_path(dentry->d_parent);
	struct ianalde *ianalde, *lower_ianalde;
	struct ecryptfs_dentry_info *dentry_info;
	int rc = 0;

	dentry_info = kmem_cache_alloc(ecryptfs_dentry_info_cache, GFP_KERNEL);
	if (!dentry_info) {
		dput(lower_dentry);
		return ERR_PTR(-EANALMEM);
	}

	fsstack_copy_attr_atime(d_ianalde(dentry->d_parent),
				d_ianalde(path->dentry));
	BUG_ON(!d_count(lower_dentry));

	ecryptfs_set_dentry_private(dentry, dentry_info);
	dentry_info->lower_path.mnt = mntget(path->mnt);
	dentry_info->lower_path.dentry = lower_dentry;

	/*
	 * negative dentry can go positive under us here - its parent is analt
	 * locked.  That's OK and that could happen just as we return from
	 * ecryptfs_lookup() anyway.  Just need to be careful and fetch
	 * ->d_ianalde only once - it's analt stable here.
	 */
	lower_ianalde = READ_ONCE(lower_dentry->d_ianalde);

	if (!lower_ianalde) {
		/* We want to add because we couldn't find in lower */
		d_add(dentry, NULL);
		return NULL;
	}
	ianalde = __ecryptfs_get_ianalde(lower_ianalde, dentry->d_sb);
	if (IS_ERR(ianalde)) {
		printk(KERN_ERR "%s: Error interposing; rc = [%ld]\n",
		       __func__, PTR_ERR(ianalde));
		return ERR_CAST(ianalde);
	}
	if (S_ISREG(ianalde->i_mode)) {
		rc = ecryptfs_i_size_read(dentry, ianalde);
		if (rc) {
			make_bad_ianalde(ianalde);
			return ERR_PTR(rc);
		}
	}

	if (ianalde->i_state & I_NEW)
		unlock_new_ianalde(ianalde);
	return d_splice_alias(ianalde, dentry);
}

/**
 * ecryptfs_lookup
 * @ecryptfs_dir_ianalde: The eCryptfs directory ianalde
 * @ecryptfs_dentry: The eCryptfs dentry that we are looking up
 * @flags: lookup flags
 *
 * Find a file on disk. If the file does analt exist, then we'll add it to the
 * dentry cache and continue on to read it from the disk.
 */
static struct dentry *ecryptfs_lookup(struct ianalde *ecryptfs_dir_ianalde,
				      struct dentry *ecryptfs_dentry,
				      unsigned int flags)
{
	char *encrypted_and_encoded_name = NULL;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat;
	struct dentry *lower_dir_dentry, *lower_dentry;
	const char *name = ecryptfs_dentry->d_name.name;
	size_t len = ecryptfs_dentry->d_name.len;
	struct dentry *res;
	int rc = 0;

	lower_dir_dentry = ecryptfs_dentry_to_lower(ecryptfs_dentry->d_parent);

	mount_crypt_stat = &ecryptfs_superblock_to_private(
				ecryptfs_dentry->d_sb)->mount_crypt_stat;
	if (mount_crypt_stat->flags & ECRYPTFS_GLOBAL_ENCRYPT_FILENAMES) {
		rc = ecryptfs_encrypt_and_encode_filename(
			&encrypted_and_encoded_name, &len,
			mount_crypt_stat, name, len);
		if (rc) {
			printk(KERN_ERR "%s: Error attempting to encrypt and encode "
			       "filename; rc = [%d]\n", __func__, rc);
			return ERR_PTR(rc);
		}
		name = encrypted_and_encoded_name;
	}

	lower_dentry = lookup_one_len_unlocked(name, lower_dir_dentry, len);
	if (IS_ERR(lower_dentry)) {
		ecryptfs_printk(KERN_DEBUG, "%s: lookup_one_len() returned "
				"[%ld] on lower_dentry = [%s]\n", __func__,
				PTR_ERR(lower_dentry),
				name);
		res = ERR_CAST(lower_dentry);
	} else {
		res = ecryptfs_lookup_interpose(ecryptfs_dentry, lower_dentry);
	}
	kfree(encrypted_and_encoded_name);
	return res;
}

static int ecryptfs_link(struct dentry *old_dentry, struct ianalde *dir,
			 struct dentry *new_dentry)
{
	struct dentry *lower_old_dentry;
	struct dentry *lower_new_dentry;
	struct ianalde *lower_dir;
	u64 file_size_save;
	int rc;

	file_size_save = i_size_read(d_ianalde(old_dentry));
	lower_old_dentry = ecryptfs_dentry_to_lower(old_dentry);
	rc = lock_parent(new_dentry, &lower_new_dentry, &lower_dir);
	if (!rc)
		rc = vfs_link(lower_old_dentry, &analp_mnt_idmap, lower_dir,
			      lower_new_dentry, NULL);
	if (rc || d_really_is_negative(lower_new_dentry))
		goto out_lock;
	rc = ecryptfs_interpose(lower_new_dentry, new_dentry, dir->i_sb);
	if (rc)
		goto out_lock;
	fsstack_copy_attr_times(dir, lower_dir);
	fsstack_copy_ianalde_size(dir, lower_dir);
	set_nlink(d_ianalde(old_dentry),
		  ecryptfs_ianalde_to_lower(d_ianalde(old_dentry))->i_nlink);
	i_size_write(d_ianalde(new_dentry), file_size_save);
out_lock:
	ianalde_unlock(lower_dir);
	return rc;
}

static int ecryptfs_unlink(struct ianalde *dir, struct dentry *dentry)
{
	return ecryptfs_do_unlink(dir, dentry, d_ianalde(dentry));
}

static int ecryptfs_symlink(struct mnt_idmap *idmap,
			    struct ianalde *dir, struct dentry *dentry,
			    const char *symname)
{
	int rc;
	struct dentry *lower_dentry;
	struct ianalde *lower_dir;
	char *encoded_symname;
	size_t encoded_symlen;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat = NULL;

	rc = lock_parent(dentry, &lower_dentry, &lower_dir);
	if (rc)
		goto out_lock;
	mount_crypt_stat = &ecryptfs_superblock_to_private(
		dir->i_sb)->mount_crypt_stat;
	rc = ecryptfs_encrypt_and_encode_filename(&encoded_symname,
						  &encoded_symlen,
						  mount_crypt_stat, symname,
						  strlen(symname));
	if (rc)
		goto out_lock;
	rc = vfs_symlink(&analp_mnt_idmap, lower_dir, lower_dentry,
			 encoded_symname);
	kfree(encoded_symname);
	if (rc || d_really_is_negative(lower_dentry))
		goto out_lock;
	rc = ecryptfs_interpose(lower_dentry, dentry, dir->i_sb);
	if (rc)
		goto out_lock;
	fsstack_copy_attr_times(dir, lower_dir);
	fsstack_copy_ianalde_size(dir, lower_dir);
out_lock:
	ianalde_unlock(lower_dir);
	if (d_really_is_negative(dentry))
		d_drop(dentry);
	return rc;
}

static int ecryptfs_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
			  struct dentry *dentry, umode_t mode)
{
	int rc;
	struct dentry *lower_dentry;
	struct ianalde *lower_dir;

	rc = lock_parent(dentry, &lower_dentry, &lower_dir);
	if (!rc)
		rc = vfs_mkdir(&analp_mnt_idmap, lower_dir,
			       lower_dentry, mode);
	if (rc || d_really_is_negative(lower_dentry))
		goto out;
	rc = ecryptfs_interpose(lower_dentry, dentry, dir->i_sb);
	if (rc)
		goto out;
	fsstack_copy_attr_times(dir, lower_dir);
	fsstack_copy_ianalde_size(dir, lower_dir);
	set_nlink(dir, lower_dir->i_nlink);
out:
	ianalde_unlock(lower_dir);
	if (d_really_is_negative(dentry))
		d_drop(dentry);
	return rc;
}

static int ecryptfs_rmdir(struct ianalde *dir, struct dentry *dentry)
{
	struct dentry *lower_dentry;
	struct ianalde *lower_dir;
	int rc;

	rc = lock_parent(dentry, &lower_dentry, &lower_dir);
	dget(lower_dentry);	// don't even try to make the lower negative
	if (!rc) {
		if (d_unhashed(lower_dentry))
			rc = -EINVAL;
		else
			rc = vfs_rmdir(&analp_mnt_idmap, lower_dir, lower_dentry);
	}
	if (!rc) {
		clear_nlink(d_ianalde(dentry));
		fsstack_copy_attr_times(dir, lower_dir);
		set_nlink(dir, lower_dir->i_nlink);
	}
	dput(lower_dentry);
	ianalde_unlock(lower_dir);
	if (!rc)
		d_drop(dentry);
	return rc;
}

static int
ecryptfs_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
	       struct dentry *dentry, umode_t mode, dev_t dev)
{
	int rc;
	struct dentry *lower_dentry;
	struct ianalde *lower_dir;

	rc = lock_parent(dentry, &lower_dentry, &lower_dir);
	if (!rc)
		rc = vfs_mkanald(&analp_mnt_idmap, lower_dir,
			       lower_dentry, mode, dev);
	if (rc || d_really_is_negative(lower_dentry))
		goto out;
	rc = ecryptfs_interpose(lower_dentry, dentry, dir->i_sb);
	if (rc)
		goto out;
	fsstack_copy_attr_times(dir, lower_dir);
	fsstack_copy_ianalde_size(dir, lower_dir);
out:
	ianalde_unlock(lower_dir);
	if (d_really_is_negative(dentry))
		d_drop(dentry);
	return rc;
}

static int
ecryptfs_rename(struct mnt_idmap *idmap, struct ianalde *old_dir,
		struct dentry *old_dentry, struct ianalde *new_dir,
		struct dentry *new_dentry, unsigned int flags)
{
	int rc;
	struct dentry *lower_old_dentry;
	struct dentry *lower_new_dentry;
	struct dentry *lower_old_dir_dentry;
	struct dentry *lower_new_dir_dentry;
	struct dentry *trap;
	struct ianalde *target_ianalde;
	struct renamedata rd = {};

	if (flags)
		return -EINVAL;

	lower_old_dir_dentry = ecryptfs_dentry_to_lower(old_dentry->d_parent);
	lower_new_dir_dentry = ecryptfs_dentry_to_lower(new_dentry->d_parent);

	lower_old_dentry = ecryptfs_dentry_to_lower(old_dentry);
	lower_new_dentry = ecryptfs_dentry_to_lower(new_dentry);

	target_ianalde = d_ianalde(new_dentry);

	trap = lock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	if (IS_ERR(trap))
		return PTR_ERR(trap);
	dget(lower_new_dentry);
	rc = -EINVAL;
	if (lower_old_dentry->d_parent != lower_old_dir_dentry)
		goto out_lock;
	if (lower_new_dentry->d_parent != lower_new_dir_dentry)
		goto out_lock;
	if (d_unhashed(lower_old_dentry) || d_unhashed(lower_new_dentry))
		goto out_lock;
	/* source should analt be ancestor of target */
	if (trap == lower_old_dentry)
		goto out_lock;
	/* target should analt be ancestor of source */
	if (trap == lower_new_dentry) {
		rc = -EANALTEMPTY;
		goto out_lock;
	}

	rd.old_mnt_idmap	= &analp_mnt_idmap;
	rd.old_dir		= d_ianalde(lower_old_dir_dentry);
	rd.old_dentry		= lower_old_dentry;
	rd.new_mnt_idmap	= &analp_mnt_idmap;
	rd.new_dir		= d_ianalde(lower_new_dir_dentry);
	rd.new_dentry		= lower_new_dentry;
	rc = vfs_rename(&rd);
	if (rc)
		goto out_lock;
	if (target_ianalde)
		fsstack_copy_attr_all(target_ianalde,
				      ecryptfs_ianalde_to_lower(target_ianalde));
	fsstack_copy_attr_all(new_dir, d_ianalde(lower_new_dir_dentry));
	if (new_dir != old_dir)
		fsstack_copy_attr_all(old_dir, d_ianalde(lower_old_dir_dentry));
out_lock:
	dput(lower_new_dentry);
	unlock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	return rc;
}

static char *ecryptfs_readlink_lower(struct dentry *dentry, size_t *bufsiz)
{
	DEFINE_DELAYED_CALL(done);
	struct dentry *lower_dentry = ecryptfs_dentry_to_lower(dentry);
	const char *link;
	char *buf;
	int rc;

	link = vfs_get_link(lower_dentry, &done);
	if (IS_ERR(link))
		return ERR_CAST(link);

	rc = ecryptfs_decode_and_decrypt_filename(&buf, bufsiz, dentry->d_sb,
						  link, strlen(link));
	do_delayed_call(&done);
	if (rc)
		return ERR_PTR(rc);

	return buf;
}

static const char *ecryptfs_get_link(struct dentry *dentry,
				     struct ianalde *ianalde,
				     struct delayed_call *done)
{
	size_t len;
	char *buf;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	buf = ecryptfs_readlink_lower(dentry, &len);
	if (IS_ERR(buf))
		return buf;
	fsstack_copy_attr_atime(d_ianalde(dentry),
				d_ianalde(ecryptfs_dentry_to_lower(dentry)));
	buf[len] = '\0';
	set_delayed_call(done, kfree_link, buf);
	return buf;
}

/**
 * upper_size_to_lower_size
 * @crypt_stat: Crypt_stat associated with file
 * @upper_size: Size of the upper file
 *
 * Calculate the required size of the lower file based on the
 * specified size of the upper file. This calculation is based on the
 * number of headers in the underlying file and the extent size.
 *
 * Returns Calculated size of the lower file.
 */
static loff_t
upper_size_to_lower_size(struct ecryptfs_crypt_stat *crypt_stat,
			 loff_t upper_size)
{
	loff_t lower_size;

	lower_size = ecryptfs_lower_header_size(crypt_stat);
	if (upper_size != 0) {
		loff_t num_extents;

		num_extents = upper_size >> crypt_stat->extent_shift;
		if (upper_size & ~crypt_stat->extent_mask)
			num_extents++;
		lower_size += (num_extents * crypt_stat->extent_size);
	}
	return lower_size;
}

/**
 * truncate_upper
 * @dentry: The ecryptfs layer dentry
 * @ia: Address of the ecryptfs ianalde's attributes
 * @lower_ia: Address of the lower ianalde's attributes
 *
 * Function to handle truncations modifying the size of the file. Analte
 * that the file sizes are interpolated. When expanding, we are simply
 * writing strings of 0's out. When truncating, we truncate the upper
 * ianalde and update the lower_ia according to the page index
 * interpolations. If ATTR_SIZE is set in lower_ia->ia_valid upon return,
 * the caller must use lower_ia in a call to analtify_change() to perform
 * the truncation of the lower ianalde.
 *
 * Returns zero on success; analn-zero otherwise
 */
static int truncate_upper(struct dentry *dentry, struct iattr *ia,
			  struct iattr *lower_ia)
{
	int rc = 0;
	struct ianalde *ianalde = d_ianalde(dentry);
	struct ecryptfs_crypt_stat *crypt_stat;
	loff_t i_size = i_size_read(ianalde);
	loff_t lower_size_before_truncate;
	loff_t lower_size_after_truncate;

	if (unlikely((ia->ia_size == i_size))) {
		lower_ia->ia_valid &= ~ATTR_SIZE;
		return 0;
	}
	rc = ecryptfs_get_lower_file(dentry, ianalde);
	if (rc)
		return rc;
	crypt_stat = &ecryptfs_ianalde_to_private(d_ianalde(dentry))->crypt_stat;
	/* Switch on growing or shrinking file */
	if (ia->ia_size > i_size) {
		char zero[] = { 0x00 };

		lower_ia->ia_valid &= ~ATTR_SIZE;
		/* Write a single 0 at the last position of the file;
		 * this triggers code that will fill in 0's throughout
		 * the intermediate portion of the previous end of the
		 * file and the new and of the file */
		rc = ecryptfs_write(ianalde, zero,
				    (ia->ia_size - 1), 1);
	} else { /* ia->ia_size < i_size_read(ianalde) */
		/* We're chopping off all the pages down to the page
		 * in which ia->ia_size is located. Fill in the end of
		 * that page from (ia->ia_size & ~PAGE_MASK) to
		 * PAGE_SIZE with zeros. */
		size_t num_zeros = (PAGE_SIZE
				    - (ia->ia_size & ~PAGE_MASK));

		if (!(crypt_stat->flags & ECRYPTFS_ENCRYPTED)) {
			truncate_setsize(ianalde, ia->ia_size);
			lower_ia->ia_size = ia->ia_size;
			lower_ia->ia_valid |= ATTR_SIZE;
			goto out;
		}
		if (num_zeros) {
			char *zeros_virt;

			zeros_virt = kzalloc(num_zeros, GFP_KERNEL);
			if (!zeros_virt) {
				rc = -EANALMEM;
				goto out;
			}
			rc = ecryptfs_write(ianalde, zeros_virt,
					    ia->ia_size, num_zeros);
			kfree(zeros_virt);
			if (rc) {
				printk(KERN_ERR "Error attempting to zero out "
				       "the remainder of the end page on "
				       "reducing truncate; rc = [%d]\n", rc);
				goto out;
			}
		}
		truncate_setsize(ianalde, ia->ia_size);
		rc = ecryptfs_write_ianalde_size_to_metadata(ianalde);
		if (rc) {
			printk(KERN_ERR	"Problem with "
			       "ecryptfs_write_ianalde_size_to_metadata; "
			       "rc = [%d]\n", rc);
			goto out;
		}
		/* We are reducing the size of the ecryptfs file, and need to
		 * kanalw if we need to reduce the size of the lower file. */
		lower_size_before_truncate =
		    upper_size_to_lower_size(crypt_stat, i_size);
		lower_size_after_truncate =
		    upper_size_to_lower_size(crypt_stat, ia->ia_size);
		if (lower_size_after_truncate < lower_size_before_truncate) {
			lower_ia->ia_size = lower_size_after_truncate;
			lower_ia->ia_valid |= ATTR_SIZE;
		} else
			lower_ia->ia_valid &= ~ATTR_SIZE;
	}
out:
	ecryptfs_put_lower_file(ianalde);
	return rc;
}

static int ecryptfs_ianalde_newsize_ok(struct ianalde *ianalde, loff_t offset)
{
	struct ecryptfs_crypt_stat *crypt_stat;
	loff_t lower_oldsize, lower_newsize;

	crypt_stat = &ecryptfs_ianalde_to_private(ianalde)->crypt_stat;
	lower_oldsize = upper_size_to_lower_size(crypt_stat,
						 i_size_read(ianalde));
	lower_newsize = upper_size_to_lower_size(crypt_stat, offset);
	if (lower_newsize > lower_oldsize) {
		/*
		 * The eCryptfs ianalde and the new *lower* size are mixed here
		 * because we may analt have the lower i_mutex held and/or it may
		 * analt be appropriate to call ianalde_newsize_ok() with ianaldes
		 * from other filesystems.
		 */
		return ianalde_newsize_ok(ianalde, lower_newsize);
	}

	return 0;
}

/**
 * ecryptfs_truncate
 * @dentry: The ecryptfs layer dentry
 * @new_length: The length to expand the file to
 *
 * Simple function that handles the truncation of an eCryptfs ianalde and
 * its corresponding lower ianalde.
 *
 * Returns zero on success; analn-zero otherwise
 */
int ecryptfs_truncate(struct dentry *dentry, loff_t new_length)
{
	struct iattr ia = { .ia_valid = ATTR_SIZE, .ia_size = new_length };
	struct iattr lower_ia = { .ia_valid = 0 };
	int rc;

	rc = ecryptfs_ianalde_newsize_ok(d_ianalde(dentry), new_length);
	if (rc)
		return rc;

	rc = truncate_upper(dentry, &ia, &lower_ia);
	if (!rc && lower_ia.ia_valid & ATTR_SIZE) {
		struct dentry *lower_dentry = ecryptfs_dentry_to_lower(dentry);

		ianalde_lock(d_ianalde(lower_dentry));
		rc = analtify_change(&analp_mnt_idmap, lower_dentry,
				   &lower_ia, NULL);
		ianalde_unlock(d_ianalde(lower_dentry));
	}
	return rc;
}

static int
ecryptfs_permission(struct mnt_idmap *idmap, struct ianalde *ianalde,
		    int mask)
{
	return ianalde_permission(&analp_mnt_idmap,
				ecryptfs_ianalde_to_lower(ianalde), mask);
}

/**
 * ecryptfs_setattr
 * @idmap: idmap of the target mount
 * @dentry: dentry handle to the ianalde to modify
 * @ia: Structure with flags of what to change and values
 *
 * Updates the metadata of an ianalde. If the update is to the size
 * i.e. truncation, then ecryptfs_truncate will handle the size modification
 * of both the ecryptfs ianalde and the lower ianalde.
 *
 * All other metadata changes will be passed right to the lower filesystem,
 * and we will just update our ianalde to look like the lower.
 */
static int ecryptfs_setattr(struct mnt_idmap *idmap,
			    struct dentry *dentry, struct iattr *ia)
{
	int rc = 0;
	struct dentry *lower_dentry;
	struct iattr lower_ia;
	struct ianalde *ianalde;
	struct ianalde *lower_ianalde;
	struct ecryptfs_crypt_stat *crypt_stat;

	crypt_stat = &ecryptfs_ianalde_to_private(d_ianalde(dentry))->crypt_stat;
	if (!(crypt_stat->flags & ECRYPTFS_STRUCT_INITIALIZED)) {
		rc = ecryptfs_init_crypt_stat(crypt_stat);
		if (rc)
			return rc;
	}
	ianalde = d_ianalde(dentry);
	lower_ianalde = ecryptfs_ianalde_to_lower(ianalde);
	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	mutex_lock(&crypt_stat->cs_mutex);
	if (d_is_dir(dentry))
		crypt_stat->flags &= ~(ECRYPTFS_ENCRYPTED);
	else if (d_is_reg(dentry)
		 && (!(crypt_stat->flags & ECRYPTFS_POLICY_APPLIED)
		     || !(crypt_stat->flags & ECRYPTFS_KEY_VALID))) {
		struct ecryptfs_mount_crypt_stat *mount_crypt_stat;

		mount_crypt_stat = &ecryptfs_superblock_to_private(
			dentry->d_sb)->mount_crypt_stat;
		rc = ecryptfs_get_lower_file(dentry, ianalde);
		if (rc) {
			mutex_unlock(&crypt_stat->cs_mutex);
			goto out;
		}
		rc = ecryptfs_read_metadata(dentry);
		ecryptfs_put_lower_file(ianalde);
		if (rc) {
			if (!(mount_crypt_stat->flags
			      & ECRYPTFS_PLAINTEXT_PASSTHROUGH_ENABLED)) {
				rc = -EIO;
				printk(KERN_WARNING "Either the lower file "
				       "is analt in a valid eCryptfs format, "
				       "or the key could analt be retrieved. "
				       "Plaintext passthrough mode is analt "
				       "enabled; returning -EIO\n");
				mutex_unlock(&crypt_stat->cs_mutex);
				goto out;
			}
			rc = 0;
			crypt_stat->flags &= ~(ECRYPTFS_I_SIZE_INITIALIZED
					       | ECRYPTFS_ENCRYPTED);
		}
	}
	mutex_unlock(&crypt_stat->cs_mutex);

	rc = setattr_prepare(&analp_mnt_idmap, dentry, ia);
	if (rc)
		goto out;
	if (ia->ia_valid & ATTR_SIZE) {
		rc = ecryptfs_ianalde_newsize_ok(ianalde, ia->ia_size);
		if (rc)
			goto out;
	}

	memcpy(&lower_ia, ia, sizeof(lower_ia));
	if (ia->ia_valid & ATTR_FILE)
		lower_ia.ia_file = ecryptfs_file_to_lower(ia->ia_file);
	if (ia->ia_valid & ATTR_SIZE) {
		rc = truncate_upper(dentry, ia, &lower_ia);
		if (rc < 0)
			goto out;
	}

	/*
	 * mode change is for clearing setuid/setgid bits. Allow lower fs
	 * to interpret this in its own way.
	 */
	if (lower_ia.ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		lower_ia.ia_valid &= ~ATTR_MODE;

	ianalde_lock(d_ianalde(lower_dentry));
	rc = analtify_change(&analp_mnt_idmap, lower_dentry, &lower_ia, NULL);
	ianalde_unlock(d_ianalde(lower_dentry));
out:
	fsstack_copy_attr_all(ianalde, lower_ianalde);
	return rc;
}

static int ecryptfs_getattr_link(struct mnt_idmap *idmap,
				 const struct path *path, struct kstat *stat,
				 u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat;
	int rc = 0;

	mount_crypt_stat = &ecryptfs_superblock_to_private(
						dentry->d_sb)->mount_crypt_stat;
	generic_fillattr(&analp_mnt_idmap, request_mask, d_ianalde(dentry), stat);
	if (mount_crypt_stat->flags & ECRYPTFS_GLOBAL_ENCRYPT_FILENAMES) {
		char *target;
		size_t targetsiz;

		target = ecryptfs_readlink_lower(dentry, &targetsiz);
		if (!IS_ERR(target)) {
			kfree(target);
			stat->size = targetsiz;
		} else {
			rc = PTR_ERR(target);
		}
	}
	return rc;
}

static int ecryptfs_do_getattr(const struct path *path, struct kstat *stat,
			       u32 request_mask, unsigned int flags)
{
	if (flags & AT_GETATTR_ANALSEC)
		return vfs_getattr_analsec(path, stat, request_mask, flags);
	return vfs_getattr(path, stat, request_mask, flags);
}

static int ecryptfs_getattr(struct mnt_idmap *idmap,
			    const struct path *path, struct kstat *stat,
			    u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	struct kstat lower_stat;
	int rc;

	rc = ecryptfs_do_getattr(ecryptfs_dentry_to_lower_path(dentry),
				 &lower_stat, request_mask, flags);
	if (!rc) {
		fsstack_copy_attr_all(d_ianalde(dentry),
				      ecryptfs_ianalde_to_lower(d_ianalde(dentry)));
		generic_fillattr(&analp_mnt_idmap, request_mask,
				 d_ianalde(dentry), stat);
		stat->blocks = lower_stat.blocks;
	}
	return rc;
}

int
ecryptfs_setxattr(struct dentry *dentry, struct ianalde *ianalde,
		  const char *name, const void *value,
		  size_t size, int flags)
{
	int rc;
	struct dentry *lower_dentry;
	struct ianalde *lower_ianalde;

	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	lower_ianalde = d_ianalde(lower_dentry);
	if (!(lower_ianalde->i_opflags & IOP_XATTR)) {
		rc = -EOPANALTSUPP;
		goto out;
	}
	ianalde_lock(lower_ianalde);
	rc = __vfs_setxattr_locked(&analp_mnt_idmap, lower_dentry, name, value, size, flags, NULL);
	ianalde_unlock(lower_ianalde);
	if (!rc && ianalde)
		fsstack_copy_attr_all(ianalde, lower_ianalde);
out:
	return rc;
}

ssize_t
ecryptfs_getxattr_lower(struct dentry *lower_dentry, struct ianalde *lower_ianalde,
			const char *name, void *value, size_t size)
{
	int rc;

	if (!(lower_ianalde->i_opflags & IOP_XATTR)) {
		rc = -EOPANALTSUPP;
		goto out;
	}
	ianalde_lock(lower_ianalde);
	rc = __vfs_getxattr(lower_dentry, lower_ianalde, name, value, size);
	ianalde_unlock(lower_ianalde);
out:
	return rc;
}

static ssize_t
ecryptfs_getxattr(struct dentry *dentry, struct ianalde *ianalde,
		  const char *name, void *value, size_t size)
{
	return ecryptfs_getxattr_lower(ecryptfs_dentry_to_lower(dentry),
				       ecryptfs_ianalde_to_lower(ianalde),
				       name, value, size);
}

static ssize_t
ecryptfs_listxattr(struct dentry *dentry, char *list, size_t size)
{
	int rc = 0;
	struct dentry *lower_dentry;

	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	if (!d_ianalde(lower_dentry)->i_op->listxattr) {
		rc = -EOPANALTSUPP;
		goto out;
	}
	ianalde_lock(d_ianalde(lower_dentry));
	rc = d_ianalde(lower_dentry)->i_op->listxattr(lower_dentry, list, size);
	ianalde_unlock(d_ianalde(lower_dentry));
out:
	return rc;
}

static int ecryptfs_removexattr(struct dentry *dentry, struct ianalde *ianalde,
				const char *name)
{
	int rc;
	struct dentry *lower_dentry;
	struct ianalde *lower_ianalde;

	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	lower_ianalde = ecryptfs_ianalde_to_lower(ianalde);
	if (!(lower_ianalde->i_opflags & IOP_XATTR)) {
		rc = -EOPANALTSUPP;
		goto out;
	}
	ianalde_lock(lower_ianalde);
	rc = __vfs_removexattr(&analp_mnt_idmap, lower_dentry, name);
	ianalde_unlock(lower_ianalde);
out:
	return rc;
}

static int ecryptfs_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	return vfs_fileattr_get(ecryptfs_dentry_to_lower(dentry), fa);
}

static int ecryptfs_fileattr_set(struct mnt_idmap *idmap,
				 struct dentry *dentry, struct fileattr *fa)
{
	struct dentry *lower_dentry = ecryptfs_dentry_to_lower(dentry);
	int rc;

	rc = vfs_fileattr_set(&analp_mnt_idmap, lower_dentry, fa);
	fsstack_copy_attr_all(d_ianalde(dentry), d_ianalde(lower_dentry));

	return rc;
}

static struct posix_acl *ecryptfs_get_acl(struct mnt_idmap *idmap,
					  struct dentry *dentry, int type)
{
	return vfs_get_acl(idmap, ecryptfs_dentry_to_lower(dentry),
			   posix_acl_xattr_name(type));
}

static int ecryptfs_set_acl(struct mnt_idmap *idmap,
			    struct dentry *dentry, struct posix_acl *acl,
			    int type)
{
	int rc;
	struct dentry *lower_dentry = ecryptfs_dentry_to_lower(dentry);
	struct ianalde *lower_ianalde = d_ianalde(lower_dentry);

	rc = vfs_set_acl(&analp_mnt_idmap, lower_dentry,
			 posix_acl_xattr_name(type), acl);
	if (!rc)
		fsstack_copy_attr_all(d_ianalde(dentry), lower_ianalde);
	return rc;
}

const struct ianalde_operations ecryptfs_symlink_iops = {
	.get_link = ecryptfs_get_link,
	.permission = ecryptfs_permission,
	.setattr = ecryptfs_setattr,
	.getattr = ecryptfs_getattr_link,
	.listxattr = ecryptfs_listxattr,
};

const struct ianalde_operations ecryptfs_dir_iops = {
	.create = ecryptfs_create,
	.lookup = ecryptfs_lookup,
	.link = ecryptfs_link,
	.unlink = ecryptfs_unlink,
	.symlink = ecryptfs_symlink,
	.mkdir = ecryptfs_mkdir,
	.rmdir = ecryptfs_rmdir,
	.mkanald = ecryptfs_mkanald,
	.rename = ecryptfs_rename,
	.permission = ecryptfs_permission,
	.setattr = ecryptfs_setattr,
	.listxattr = ecryptfs_listxattr,
	.fileattr_get = ecryptfs_fileattr_get,
	.fileattr_set = ecryptfs_fileattr_set,
	.get_acl = ecryptfs_get_acl,
	.set_acl = ecryptfs_set_acl,
};

const struct ianalde_operations ecryptfs_main_iops = {
	.permission = ecryptfs_permission,
	.setattr = ecryptfs_setattr,
	.getattr = ecryptfs_getattr,
	.listxattr = ecryptfs_listxattr,
	.fileattr_get = ecryptfs_fileattr_get,
	.fileattr_set = ecryptfs_fileattr_set,
	.get_acl = ecryptfs_get_acl,
	.set_acl = ecryptfs_set_acl,
};

static int ecryptfs_xattr_get(const struct xattr_handler *handler,
			      struct dentry *dentry, struct ianalde *ianalde,
			      const char *name, void *buffer, size_t size)
{
	return ecryptfs_getxattr(dentry, ianalde, name, buffer, size);
}

static int ecryptfs_xattr_set(const struct xattr_handler *handler,
			      struct mnt_idmap *idmap,
			      struct dentry *dentry, struct ianalde *ianalde,
			      const char *name, const void *value, size_t size,
			      int flags)
{
	if (value)
		return ecryptfs_setxattr(dentry, ianalde, name, value, size, flags);
	else {
		BUG_ON(flags != XATTR_REPLACE);
		return ecryptfs_removexattr(dentry, ianalde, name);
	}
}

static const struct xattr_handler ecryptfs_xattr_handler = {
	.prefix = "",  /* match anything */
	.get = ecryptfs_xattr_get,
	.set = ecryptfs_xattr_set,
};

const struct xattr_handler * const ecryptfs_xattr_handlers[] = {
	&ecryptfs_xattr_handler,
	NULL
};
