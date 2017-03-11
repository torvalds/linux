/*
 * fs/sdcardfs/inode.c
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd
 *   Authors: Daeho Jeong, Woojoong Lee, Seunghwan Hyun,
 *               Sunghwan Yun, Sungjong Seo
 *
 * This program has been developed as a stackable file system based on
 * the WrapFS which written by
 *
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009     Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This file is dual licensed.  It may be redistributed and/or modified
 * under the terms of the Apache 2.0 License OR version 2 of the GNU
 * General Public License.
 */

#include "sdcardfs.h"
#include <linux/fs_struct.h>
#include <linux/ratelimit.h>

/* Do not directly use this function. Use OVERRIDE_CRED() instead. */
const struct cred * override_fsids(struct sdcardfs_sb_info* sbi, struct sdcardfs_inode_info *info)
{
	struct cred * cred;
	const struct cred * old_cred;
	uid_t uid;

	cred = prepare_creds();
	if (!cred)
		return NULL;

	if (info->under_obb)
		uid = AID_MEDIA_OBB;
	else
		uid = multiuser_get_uid(info->userid, sbi->options.fs_low_uid);
	cred->fsuid = make_kuid(&init_user_ns, uid);
	cred->fsgid = make_kgid(&init_user_ns, sbi->options.fs_low_gid);

	old_cred = override_creds(cred);

	return old_cred;
}

/* Do not directly use this function, use REVERT_CRED() instead. */
void revert_fsids(const struct cred * old_cred)
{
	const struct cred * cur_cred;

	cur_cred = current->cred;
	revert_creds(old_cred);
	put_cred(cur_cred);
}

static int sdcardfs_create(struct inode *dir, struct dentry *dentry,
			 umode_t mode, bool want_excl)
{
	int err;
	struct dentry *lower_dentry;
	struct vfsmount *lower_dentry_mnt;
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;
	const struct cred *saved_cred = NULL;
	struct fs_struct *saved_fs;
	struct fs_struct *copied_fs;

	if(!check_caller_access_to_name(dir, &dentry->d_name)) {
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb), saved_cred, SDCARDFS_I(dir));

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_dentry_mnt = lower_path.mnt;
	lower_parent_dentry = lock_parent(lower_dentry);

	/* set last 16bytes of mode field to 0664 */
	mode = (mode & S_IFMT) | 00664;

	/* temporarily change umask for lower fs write */
	saved_fs = current->fs;
	copied_fs = copy_fs_struct(current->fs);
	if (!copied_fs) {
		err = -ENOMEM;
		goto out_unlock;
	}
	current->fs = copied_fs;
	current->fs->umask = 0;
	err = vfs_create2(lower_dentry_mnt, d_inode(lower_parent_dentry), lower_dentry, mode, want_excl);
	if (err)
		goto out;

	err = sdcardfs_interpose(dentry, dir->i_sb, &lower_path, SDCARDFS_I(dir)->userid);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, sdcardfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));
	fixup_lower_ownership(dentry, dentry->d_name.name);

out:
	current->fs = saved_fs;
	free_fs_struct(copied_fs);
out_unlock:
	unlock_dir(lower_parent_dentry);
	sdcardfs_put_lower_path(dentry, &lower_path);
	REVERT_CRED(saved_cred);
out_eacces:
	return err;
}

#if 0
static int sdcardfs_link(struct dentry *old_dentry, struct inode *dir,
		       struct dentry *new_dentry)
{
	struct dentry *lower_old_dentry;
	struct dentry *lower_new_dentry;
	struct dentry *lower_dir_dentry;
	u64 file_size_save;
	int err;
	struct path lower_old_path, lower_new_path;

	OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb));

	file_size_save = i_size_read(d_inode(old_dentry));
	sdcardfs_get_lower_path(old_dentry, &lower_old_path);
	sdcardfs_get_lower_path(new_dentry, &lower_new_path);
	lower_old_dentry = lower_old_path.dentry;
	lower_new_dentry = lower_new_path.dentry;
	lower_dir_dentry = lock_parent(lower_new_dentry);

	err = vfs_link(lower_old_dentry, d_inode(lower_dir_dentry),
		       lower_new_dentry, NULL);
	if (err || !d_inode(lower_new_dentry))
		goto out;

	err = sdcardfs_interpose(new_dentry, dir->i_sb, &lower_new_path);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, d_inode(lower_new_dentry));
	fsstack_copy_inode_size(dir, d_inode(lower_new_dentry));
	set_nlink(d_inode(old_dentry),
		  sdcardfs_lower_inode(d_inode(old_dentry))->i_nlink);
	i_size_write(d_inode(new_dentry), file_size_save);
out:
	unlock_dir(lower_dir_dentry);
	sdcardfs_put_lower_path(old_dentry, &lower_old_path);
	sdcardfs_put_lower_path(new_dentry, &lower_new_path);
	REVERT_CRED();
	return err;
}
#endif

static int sdcardfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int err;
	struct dentry *lower_dentry;
	struct vfsmount *lower_mnt;
	struct inode *lower_dir_inode = sdcardfs_lower_inode(dir);
	struct dentry *lower_dir_dentry;
	struct path lower_path;
	const struct cred *saved_cred = NULL;

	if(!check_caller_access_to_name(dir, &dentry->d_name)) {
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb), saved_cred, SDCARDFS_I(dir));

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_mnt = lower_path.mnt;
	dget(lower_dentry);
	lower_dir_dentry = lock_parent(lower_dentry);

	err = vfs_unlink2(lower_mnt, lower_dir_inode, lower_dentry, NULL);

	/*
	 * Note: unlinking on top of NFS can cause silly-renamed files.
	 * Trying to delete such files results in EBUSY from NFS
	 * below.  Silly-renamed files will get deleted by NFS later on, so
	 * we just need to detect them here and treat such EBUSY errors as
	 * if the upper file was successfully deleted.
	 */
	if (err == -EBUSY && lower_dentry->d_flags & DCACHE_NFSFS_RENAMED)
		err = 0;
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, lower_dir_inode);
	fsstack_copy_inode_size(dir, lower_dir_inode);
	set_nlink(d_inode(dentry),
		  sdcardfs_lower_inode(d_inode(dentry))->i_nlink);
	d_inode(dentry)->i_ctime = dir->i_ctime;
	d_drop(dentry); /* this is needed, else LTP fails (VFS won't do it) */
out:
	unlock_dir(lower_dir_dentry);
	dput(lower_dentry);
	sdcardfs_put_lower_path(dentry, &lower_path);
	REVERT_CRED(saved_cred);
out_eacces:
	return err;
}

#if 0
static int sdcardfs_symlink(struct inode *dir, struct dentry *dentry,
			  const char *symname)
{
	int err;
	struct dentry *lower_dentry;
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;

	OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb));

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = lock_parent(lower_dentry);

	err = vfs_symlink(d_inode(lower_parent_dentry), lower_dentry, symname);
	if (err)
		goto out;
	err = sdcardfs_interpose(dentry, dir->i_sb, &lower_path);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, sdcardfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));

out:
	unlock_dir(lower_parent_dentry);
	sdcardfs_put_lower_path(dentry, &lower_path);
	REVERT_CRED();
	return err;
}
#endif

static int touch(char *abs_path, mode_t mode) {
	struct file *filp = filp_open(abs_path, O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW, mode);
	if (IS_ERR(filp)) {
		if (PTR_ERR(filp) == -EEXIST) {
			return 0;
		}
		else {
			printk(KERN_ERR "sdcardfs: failed to open(%s): %ld\n",
						abs_path, PTR_ERR(filp));
			return PTR_ERR(filp);
		}
	}
	filp_close(filp, current->files);
	return 0;
}

static int sdcardfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int err;
	int make_nomedia_in_obb = 0;
	struct dentry *lower_dentry;
	struct vfsmount *lower_mnt;
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dentry->d_sb);
	const struct cred *saved_cred = NULL;
	struct sdcardfs_inode_info *pi = SDCARDFS_I(dir);
	int touch_err = 0;
	struct fs_struct *saved_fs;
	struct fs_struct *copied_fs;
	struct qstr q_obb = QSTR_LITERAL("obb");
	struct qstr q_data = QSTR_LITERAL("data");

	if(!check_caller_access_to_name(dir, &dentry->d_name)) {
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb), saved_cred, SDCARDFS_I(dir));

	/* check disk space */
	if (!check_min_free_space(dentry, 0, 1)) {
		printk(KERN_INFO "sdcardfs: No minimum free space.\n");
		err = -ENOSPC;
		goto out_revert;
	}

	/* the lower_dentry is negative here */
	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_mnt = lower_path.mnt;
	lower_parent_dentry = lock_parent(lower_dentry);

	/* set last 16bytes of mode field to 0775 */
	mode = (mode & S_IFMT) | 00775;

	/* temporarily change umask for lower fs write */
	saved_fs = current->fs;
	copied_fs = copy_fs_struct(current->fs);
	if (!copied_fs) {
		err = -ENOMEM;
		unlock_dir(lower_parent_dentry);
		goto out_unlock;
	}
	current->fs = copied_fs;
	current->fs->umask = 0;
	err = vfs_mkdir2(lower_mnt, d_inode(lower_parent_dentry), lower_dentry, mode);

	if (err) {
		unlock_dir(lower_parent_dentry);
		goto out;
	}

	/* if it is a local obb dentry, setup it with the base obbpath */
	if(need_graft_path(dentry)) {

		err = setup_obb_dentry(dentry, &lower_path);
		if(err) {
			/* if the sbi->obbpath is not available, the lower_path won't be
			 * changed by setup_obb_dentry() but the lower path is saved to
			 * its orig_path. this dentry will be revalidated later.
			 * but now, the lower_path should be NULL */
			sdcardfs_put_reset_lower_path(dentry);

			/* the newly created lower path which saved to its orig_path or
			 * the lower_path is the base obbpath.
			 * therefore, an additional path_get is required */
			path_get(&lower_path);
		} else
			make_nomedia_in_obb = 1;
	}

	err = sdcardfs_interpose(dentry, dir->i_sb, &lower_path, pi->userid);
	if (err) {
		unlock_dir(lower_parent_dentry);
		goto out;
	}

	fsstack_copy_attr_times(dir, sdcardfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));
	/* update number of links on parent directory */
	set_nlink(dir, sdcardfs_lower_inode(dir)->i_nlink);
	fixup_lower_ownership(dentry, dentry->d_name.name);
	unlock_dir(lower_parent_dentry);
	if ((!sbi->options.multiuser) && (qstr_case_eq(&dentry->d_name, &q_obb))
		&& (pi->perm == PERM_ANDROID) && (pi->userid == 0))
		make_nomedia_in_obb = 1;

	/* When creating /Android/data and /Android/obb, mark them as .nomedia */
	if (make_nomedia_in_obb ||
		((pi->perm == PERM_ANDROID) && (qstr_case_eq(&dentry->d_name, &q_data)))) {
		REVERT_CRED(saved_cred);
		OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb), saved_cred, SDCARDFS_I(d_inode(dentry)));
		set_fs_pwd(current->fs, &lower_path);
		touch_err = touch(".nomedia", 0664);
		if (touch_err) {
			printk(KERN_ERR "sdcardfs: failed to create .nomedia in %s: %d\n",
							lower_path.dentry->d_name.name, touch_err);
			goto out;
		}
	}
out:
	current->fs = saved_fs;
	free_fs_struct(copied_fs);
out_unlock:
	sdcardfs_put_lower_path(dentry, &lower_path);
out_revert:
	REVERT_CRED(saved_cred);
out_eacces:
	return err;
}

static int sdcardfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;
	struct vfsmount *lower_mnt;
	int err;
	struct path lower_path;
	const struct cred *saved_cred = NULL;

	if(!check_caller_access_to_name(dir, &dentry->d_name)) {
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb), saved_cred, SDCARDFS_I(dir));

	/* sdcardfs_get_real_lower(): in case of remove an user's obb dentry
	 * the dentry on the original path should be deleted. */
	sdcardfs_get_real_lower(dentry, &lower_path);

	lower_dentry = lower_path.dentry;
	lower_mnt = lower_path.mnt;
	lower_dir_dentry = lock_parent(lower_dentry);

	err = vfs_rmdir2(lower_mnt, d_inode(lower_dir_dentry), lower_dentry);
	if (err)
		goto out;

	d_drop(dentry);	/* drop our dentry on success (why not VFS's job?) */
	if (d_inode(dentry))
		clear_nlink(d_inode(dentry));
	fsstack_copy_attr_times(dir, d_inode(lower_dir_dentry));
	fsstack_copy_inode_size(dir, d_inode(lower_dir_dentry));
	set_nlink(dir, d_inode(lower_dir_dentry)->i_nlink);

out:
	unlock_dir(lower_dir_dentry);
	sdcardfs_put_real_lower(dentry, &lower_path);
	REVERT_CRED(saved_cred);
out_eacces:
	return err;
}

#if 0
static int sdcardfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode,
			dev_t dev)
{
	int err;
	struct dentry *lower_dentry;
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;

	OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb));

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = lock_parent(lower_dentry);

	err = vfs_mknod(d_inode(lower_parent_dentry), lower_dentry, mode, dev);
	if (err)
		goto out;

	err = sdcardfs_interpose(dentry, dir->i_sb, &lower_path);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, sdcardfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));

out:
	unlock_dir(lower_parent_dentry);
	sdcardfs_put_lower_path(dentry, &lower_path);
	REVERT_CRED();
	return err;
}
#endif

/*
 * The locking rules in sdcardfs_rename are complex.  We could use a simpler
 * superblock-level name-space lock for renames and copy-ups.
 */
static int sdcardfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			 struct inode *new_dir, struct dentry *new_dentry)
{
	int err = 0;
	struct dentry *lower_old_dentry = NULL;
	struct dentry *lower_new_dentry = NULL;
	struct dentry *lower_old_dir_dentry = NULL;
	struct dentry *lower_new_dir_dentry = NULL;
	struct vfsmount *lower_mnt = NULL;
	struct dentry *trap = NULL;
	struct path lower_old_path, lower_new_path;
	const struct cred *saved_cred = NULL;

	if(!check_caller_access_to_name(old_dir, &old_dentry->d_name) ||
		!check_caller_access_to_name(new_dir, &new_dentry->d_name)) {
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(old_dir->i_sb), saved_cred, SDCARDFS_I(new_dir));

	sdcardfs_get_real_lower(old_dentry, &lower_old_path);
	sdcardfs_get_lower_path(new_dentry, &lower_new_path);
	lower_old_dentry = lower_old_path.dentry;
	lower_new_dentry = lower_new_path.dentry;
	lower_mnt = lower_old_path.mnt;
	lower_old_dir_dentry = dget_parent(lower_old_dentry);
	lower_new_dir_dentry = dget_parent(lower_new_dentry);

	trap = lock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	/* source should not be ancestor of target */
	if (trap == lower_old_dentry) {
		err = -EINVAL;
		goto out;
	}
	/* target should not be ancestor of source */
	if (trap == lower_new_dentry) {
		err = -ENOTEMPTY;
		goto out;
	}

	err = vfs_rename2(lower_mnt,
			 d_inode(lower_old_dir_dentry), lower_old_dentry,
			 d_inode(lower_new_dir_dentry), lower_new_dentry,
			 NULL, 0);
	if (err)
		goto out;

	/* Copy attrs from lower dir, but i_uid/i_gid */
	sdcardfs_copy_and_fix_attrs(new_dir, d_inode(lower_new_dir_dentry));
	fsstack_copy_inode_size(new_dir, d_inode(lower_new_dir_dentry));

	if (new_dir != old_dir) {
		sdcardfs_copy_and_fix_attrs(old_dir, d_inode(lower_old_dir_dentry));
		fsstack_copy_inode_size(old_dir, d_inode(lower_old_dir_dentry));
	}
	get_derived_permission_new(new_dentry->d_parent, old_dentry, &new_dentry->d_name);
	fixup_tmp_permissions(d_inode(old_dentry));
	fixup_lower_ownership(old_dentry, new_dentry->d_name.name);
	d_invalidate(old_dentry); /* Can't fixup ownership recursively :( */
out:
	unlock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	dput(lower_old_dir_dentry);
	dput(lower_new_dir_dentry);
	sdcardfs_put_real_lower(old_dentry, &lower_old_path);
	sdcardfs_put_lower_path(new_dentry, &lower_new_path);
	REVERT_CRED(saved_cred);
out_eacces:
	return err;
}

#if 0
static int sdcardfs_readlink(struct dentry *dentry, char __user *buf, int bufsiz)
{
	int err;
	struct dentry *lower_dentry;
	struct path lower_path;
	/* XXX readlink does not requires overriding credential */

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!d_inode(lower_dentry)->i_op ||
	    !d_inode(lower_dentry)->i_op->readlink) {
		err = -EINVAL;
		goto out;
	}

	err = d_inode(lower_dentry)->i_op->readlink(lower_dentry,
						    buf, bufsiz);
	if (err < 0)
		goto out;
	fsstack_copy_attr_atime(d_inode(dentry), d_inode(lower_dentry));

out:
	sdcardfs_put_lower_path(dentry, &lower_path);
	return err;
}
#endif

#if 0
static const char *sdcardfs_follow_link(struct dentry *dentry, void **cookie)
{
	char *buf;
	int len = PAGE_SIZE, err;
	mm_segment_t old_fs;

	/* This is freed by the put_link method assuming a successful call. */
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		buf = ERR_PTR(-ENOMEM);
		return buf;
	}

	/* read the symlink, and then we will follow it */
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sdcardfs_readlink(dentry, buf, len);
	set_fs(old_fs);
	if (err < 0) {
		kfree(buf);
		buf = ERR_PTR(err);
	} else {
		buf[err] = '\0';
	}
	return *cookie = buf;
}
#endif

static int sdcardfs_permission_wrn(struct inode *inode, int mask)
{
	WARN_RATELIMIT(1, "sdcardfs does not support permission. Use permission2.\n");
	return -EINVAL;
}

void copy_attrs(struct inode *dest, const struct inode *src)
{
	dest->i_mode = src->i_mode;
	dest->i_uid = src->i_uid;
	dest->i_gid = src->i_gid;
	dest->i_rdev = src->i_rdev;
	dest->i_atime = src->i_atime;
	dest->i_mtime = src->i_mtime;
	dest->i_ctime = src->i_ctime;
	dest->i_blkbits = src->i_blkbits;
	dest->i_flags = src->i_flags;
#ifdef CONFIG_FS_POSIX_ACL
	dest->i_acl = src->i_acl;
#endif
#ifdef CONFIG_SECURITY
	dest->i_security = src->i_security;
#endif
}

static int sdcardfs_permission(struct vfsmount *mnt, struct inode *inode, int mask)
{
	int err;
	struct inode tmp;
	struct inode *top = grab_top(SDCARDFS_I(inode));

	if (!top) {
		release_top(SDCARDFS_I(inode));
		WARN(1, "Top value was null!\n");
		return -EINVAL;
	}

	/*
	 * Permission check on sdcardfs inode.
	 * Calling process should have AID_SDCARD_RW permission
	 * Since generic_permission only needs i_mode, i_uid,
	 * i_gid, and i_sb, we can create a fake inode to pass
	 * this information down in.
	 *
	 * The underlying code may attempt to take locks in some
	 * cases for features we're not using, but if that changes,
	 * locks must be dealt with to avoid undefined behavior.
	 */
	copy_attrs(&tmp, inode);
	tmp.i_uid = make_kuid(&init_user_ns, SDCARDFS_I(top)->d_uid);
	tmp.i_gid = make_kgid(&init_user_ns, get_gid(mnt, SDCARDFS_I(top)));
	tmp.i_mode = (inode->i_mode & S_IFMT) | get_mode(mnt, SDCARDFS_I(top));
	release_top(SDCARDFS_I(inode));
	tmp.i_sb = inode->i_sb;
	if (IS_POSIXACL(inode))
		printk(KERN_WARNING "%s: This may be undefined behavior... \n", __func__);
	err = generic_permission(&tmp, mask);
	/* XXX
	 * Original sdcardfs code calls inode_permission(lower_inode,.. )
	 * for checking inode permission. But doing such things here seems
	 * duplicated work, because the functions called after this func,
	 * such as vfs_create, vfs_unlink, vfs_rename, and etc,
	 * does exactly same thing, i.e., they calls inode_permission().
	 * So we just let they do the things.
	 * If there are any security hole, just uncomment following if block.
	 */
#if 0
	if (!err) {
		/*
		 * Permission check on lower_inode(=EXT4).
		 * we check it with AID_MEDIA_RW permission
		 */
		struct inode *lower_inode;
		OVERRIDE_CRED(SDCARDFS_SB(inode->sb));

		lower_inode = sdcardfs_lower_inode(inode);
		err = inode_permission(lower_inode, mask);

		REVERT_CRED();
	}
#endif
	return err;

}

static int sdcardfs_setattr_wrn(struct dentry *dentry, struct iattr *ia)
{
	WARN_RATELIMIT(1, "sdcardfs does not support setattr. User setattr2.\n");
	return -EINVAL;
}

static int sdcardfs_setattr(struct vfsmount *mnt, struct dentry *dentry, struct iattr *ia)
{
	int err;
	struct dentry *lower_dentry;
	struct vfsmount *lower_mnt;
	struct inode *inode;
	struct inode *lower_inode;
	struct path lower_path;
	struct iattr lower_ia;
	struct dentry *parent;
	struct inode tmp;
	struct inode *top;
	const struct cred *saved_cred = NULL;

	inode = d_inode(dentry);
	top = grab_top(SDCARDFS_I(inode));

	if (!top) {
		release_top(SDCARDFS_I(inode));
		return -EINVAL;
	}

	/*
	 * Permission check on sdcardfs inode.
	 * Calling process should have AID_SDCARD_RW permission
	 * Since generic_permission only needs i_mode, i_uid,
	 * i_gid, and i_sb, we can create a fake inode to pass
	 * this information down in.
	 *
	 * The underlying code may attempt to take locks in some
	 * cases for features we're not using, but if that changes,
	 * locks must be dealt with to avoid undefined behavior.
	 *
	 */
	copy_attrs(&tmp, inode);
	tmp.i_uid = make_kuid(&init_user_ns, SDCARDFS_I(top)->d_uid);
	tmp.i_gid = make_kgid(&init_user_ns, get_gid(mnt, SDCARDFS_I(top)));
	tmp.i_mode = (inode->i_mode & S_IFMT) | get_mode(mnt, SDCARDFS_I(top));
	tmp.i_size = i_size_read(inode);
	release_top(SDCARDFS_I(inode));
	tmp.i_sb = inode->i_sb;

	/*
	 * Check if user has permission to change inode.  We don't check if
	 * this user can change the lower inode: that should happen when
	 * calling notify_change on the lower inode.
	 */
	/* prepare our own lower struct iattr (with the lower file) */
	memcpy(&lower_ia, ia, sizeof(lower_ia));
	/* Allow touch updating timestamps. A previous permission check ensures
	 * we have write access. Changes to mode, owner, and group are ignored*/
	ia->ia_valid |= ATTR_FORCE;
	err = inode_change_ok(&tmp, ia);

	if (!err) {
		/* check the Android group ID */
		parent = dget_parent(dentry);
		if (!check_caller_access_to_name(d_inode(parent), &dentry->d_name))
			err = -EACCES;
		dput(parent);
	}

	if (err)
		goto out_err;

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(dentry->d_sb), saved_cred, SDCARDFS_I(inode));

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_mnt = lower_path.mnt;
	lower_inode = sdcardfs_lower_inode(inode);

	if (ia->ia_valid & ATTR_FILE)
		lower_ia.ia_file = sdcardfs_lower_file(ia->ia_file);

	lower_ia.ia_valid &= ~(ATTR_UID | ATTR_GID | ATTR_MODE);

	/*
	 * If shrinking, first truncate upper level to cancel writing dirty
	 * pages beyond the new eof; and also if its' maxbytes is more
	 * limiting (fail with -EFBIG before making any change to the lower
	 * level).  There is no need to vmtruncate the upper level
	 * afterwards in the other cases: we fsstack_copy_inode_size from
	 * the lower level.
	 */
	if (current->mm)
		down_write(&current->mm->mmap_sem);
	if (ia->ia_valid & ATTR_SIZE) {
		err = inode_newsize_ok(&tmp, ia->ia_size);
		if (err) {
			if (current->mm)
				up_write(&current->mm->mmap_sem);
			goto out;
		}
		truncate_setsize(inode, ia->ia_size);
	}

	/*
	 * mode change is for clearing setuid/setgid bits. Allow lower fs
	 * to interpret this in its own way.
	 */
	if (lower_ia.ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		lower_ia.ia_valid &= ~ATTR_MODE;

	/* notify the (possibly copied-up) lower inode */
	/*
	 * Note: we use d_inode(lower_dentry), because lower_inode may be
	 * unlinked (no inode->i_sb and i_ino==0.  This happens if someone
	 * tries to open(), unlink(), then ftruncate() a file.
	 */
	mutex_lock(&d_inode(lower_dentry)->i_mutex);
	err = notify_change2(lower_mnt, lower_dentry, &lower_ia, /* note: lower_ia */
			NULL);
	mutex_unlock(&d_inode(lower_dentry)->i_mutex);
	if (current->mm)
		up_write(&current->mm->mmap_sem);
	if (err)
		goto out;

	/* get attributes from the lower inode and update derived permissions */
	sdcardfs_copy_and_fix_attrs(inode, lower_inode);

	/*
	 * Not running fsstack_copy_inode_size(inode, lower_inode), because
	 * VFS should update our inode size, and notify_change on
	 * lower_inode should update its size.
	 */

out:
	sdcardfs_put_lower_path(dentry, &lower_path);
	REVERT_CRED(saved_cred);
out_err:
	return err;
}

static int sdcardfs_fillattr(struct vfsmount *mnt, struct inode *inode, struct kstat *stat)
{
	struct sdcardfs_inode_info *info = SDCARDFS_I(inode);
	struct inode *top = grab_top(info);
	if (!top)
		return -EINVAL;

	stat->dev = inode->i_sb->s_dev;
	stat->ino = inode->i_ino;
	stat->mode = (inode->i_mode  & S_IFMT) | get_mode(mnt, SDCARDFS_I(top));
	stat->nlink = inode->i_nlink;
	stat->uid = make_kuid(&init_user_ns, SDCARDFS_I(top)->d_uid);
	stat->gid = make_kgid(&init_user_ns, get_gid(mnt, SDCARDFS_I(top)));
	stat->rdev = inode->i_rdev;
	stat->size = i_size_read(inode);
	stat->atime = inode->i_atime;
	stat->mtime = inode->i_mtime;
	stat->ctime = inode->i_ctime;
	stat->blksize = (1 << inode->i_blkbits);
	stat->blocks = inode->i_blocks;
	release_top(info);
	return 0;
}

static int sdcardfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		 struct kstat *stat)
{
	struct kstat lower_stat;
	struct path lower_path;
	struct dentry *parent;
	int err;

	parent = dget_parent(dentry);
	if(!check_caller_access_to_name(d_inode(parent), &dentry->d_name)) {
		dput(parent);
		return -EACCES;
	}
	dput(parent);

	sdcardfs_get_lower_path(dentry, &lower_path);
	err = vfs_getattr(&lower_path, &lower_stat);
	if (err)
		goto out;
	sdcardfs_copy_and_fix_attrs(d_inode(dentry),
			      d_inode(lower_path.dentry));
	err = sdcardfs_fillattr(mnt, d_inode(dentry), stat);
	stat->blocks = lower_stat.blocks;
out:
	sdcardfs_put_lower_path(dentry, &lower_path);
	return err;
}

const struct inode_operations sdcardfs_symlink_iops = {
	.permission2	= sdcardfs_permission,
	.setattr2	= sdcardfs_setattr,
	/* XXX Following operations are implemented,
	 *     but FUSE(sdcard) or FAT does not support them
	 *     These methods are *NOT* perfectly tested.
	.readlink	= sdcardfs_readlink,
	.follow_link	= sdcardfs_follow_link,
	.put_link	= kfree_put_link,
	 */
};

const struct inode_operations sdcardfs_dir_iops = {
	.create		= sdcardfs_create,
	.lookup		= sdcardfs_lookup,
	.permission	= sdcardfs_permission_wrn,
	.permission2	= sdcardfs_permission,
	.unlink		= sdcardfs_unlink,
	.mkdir		= sdcardfs_mkdir,
	.rmdir		= sdcardfs_rmdir,
	.rename		= sdcardfs_rename,
	.setattr	= sdcardfs_setattr_wrn,
	.setattr2	= sdcardfs_setattr,
	.getattr	= sdcardfs_getattr,
	/* XXX Following operations are implemented,
	 *     but FUSE(sdcard) or FAT does not support them
	 *     These methods are *NOT* perfectly tested.
	.symlink	= sdcardfs_symlink,
	.link		= sdcardfs_link,
	.mknod		= sdcardfs_mknod,
	 */
};

const struct inode_operations sdcardfs_main_iops = {
	.permission	= sdcardfs_permission_wrn,
	.permission2	= sdcardfs_permission,
	.setattr	= sdcardfs_setattr_wrn,
	.setattr2	= sdcardfs_setattr,
	.getattr	= sdcardfs_getattr,
};
