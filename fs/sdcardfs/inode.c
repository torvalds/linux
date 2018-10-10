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

const struct cred *override_fsids(struct sdcardfs_sb_info *sbi,
		struct sdcardfs_inode_data *data)
{
	struct cred *cred;
	const struct cred *old_cred;
	uid_t uid;

	cred = prepare_creds();
	if (!cred)
		return NULL;

	if (sbi->options.gid_derivation) {
		if (data->under_obb)
			uid = AID_MEDIA_OBB;
		else
			uid = multiuser_get_uid(data->userid, sbi->options.fs_low_uid);
	} else {
		uid = sbi->options.fs_low_uid;
	}
	cred->fsuid = make_kuid(&init_user_ns, uid);
	cred->fsgid = make_kgid(&init_user_ns, sbi->options.fs_low_gid);

	old_cred = override_creds(cred);

	return old_cred;
}

void revert_fsids(const struct cred *old_cred)
{
	const struct cred *cur_cred;

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

	if (!check_caller_access_to_name(dir, &dentry->d_name)) {
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	saved_cred = override_fsids(SDCARDFS_SB(dir->i_sb),
					SDCARDFS_I(dir)->data);
	if (!saved_cred)
		return -ENOMEM;

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
	copied_fs->umask = 0;
	task_lock(current);
	current->fs = copied_fs;
	task_unlock(current);

	err = vfs_create2(lower_dentry_mnt, d_inode(lower_parent_dentry), lower_dentry, mode, want_excl);
	if (err)
		goto out;

	err = sdcardfs_interpose(dentry, dir->i_sb, &lower_path,
			SDCARDFS_I(dir)->data->userid);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, sdcardfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));
	fixup_lower_ownership(dentry, dentry->d_name.name);

out:
	task_lock(current);
	current->fs = saved_fs;
	task_unlock(current);
	free_fs_struct(copied_fs);
out_unlock:
	unlock_dir(lower_parent_dentry);
	sdcardfs_put_lower_path(dentry, &lower_path);
	revert_fsids(saved_cred);
out_eacces:
	return err;
}

static int sdcardfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int err;
	struct dentry *lower_dentry;
	struct vfsmount *lower_mnt;
	struct inode *lower_dir_inode = sdcardfs_lower_inode(dir);
	struct dentry *lower_dir_dentry;
	struct path lower_path;
	const struct cred *saved_cred = NULL;

	if (!check_caller_access_to_name(dir, &dentry->d_name)) {
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	saved_cred = override_fsids(SDCARDFS_SB(dir->i_sb),
						SDCARDFS_I(dir)->data);
	if (!saved_cred)
		return -ENOMEM;

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
	revert_fsids(saved_cred);
out_eacces:
	return err;
}

static int touch(char *abs_path, mode_t mode)
{
	struct file *filp = filp_open(abs_path, O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW, mode);

	if (IS_ERR(filp)) {
		if (PTR_ERR(filp) == -EEXIST) {
			return 0;
		} else {
			pr_err("sdcardfs: failed to open(%s): %ld\n",
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
	struct dentry *parent_dentry = NULL;
	struct path lower_path;
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dentry->d_sb);
	const struct cred *saved_cred = NULL;
	struct sdcardfs_inode_data *pd = SDCARDFS_I(dir)->data;
	int touch_err = 0;
	struct fs_struct *saved_fs;
	struct fs_struct *copied_fs;
	struct qstr q_obb = QSTR_LITERAL("obb");
	struct qstr q_data = QSTR_LITERAL("data");

	if (!check_caller_access_to_name(dir, &dentry->d_name)) {
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	saved_cred = override_fsids(SDCARDFS_SB(dir->i_sb),
						SDCARDFS_I(dir)->data);
	if (!saved_cred)
		return -ENOMEM;

	/* check disk space */
	parent_dentry = dget_parent(dentry);
	if (!check_min_free_space(parent_dentry, 0, 1)) {
		pr_err("sdcardfs: No minimum free space.\n");
		err = -ENOSPC;
		dput(parent_dentry);
		goto out_revert;
	}
	dput(parent_dentry);

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
	copied_fs->umask = 0;
	task_lock(current);
	current->fs = copied_fs;
	task_unlock(current);

	err = vfs_mkdir2(lower_mnt, d_inode(lower_parent_dentry), lower_dentry, mode);

	if (err) {
		unlock_dir(lower_parent_dentry);
		goto out;
	}

	/* if it is a local obb dentry, setup it with the base obbpath */
	if (need_graft_path(dentry)) {

		err = setup_obb_dentry(dentry, &lower_path);
		if (err) {
			/* if the sbi->obbpath is not available, the lower_path won't be
			 * changed by setup_obb_dentry() but the lower path is saved to
			 * its orig_path. this dentry will be revalidated later.
			 * but now, the lower_path should be NULL
			 */
			sdcardfs_put_reset_lower_path(dentry);

			/* the newly created lower path which saved to its orig_path or
			 * the lower_path is the base obbpath.
			 * therefore, an additional path_get is required
			 */
			path_get(&lower_path);
		} else
			make_nomedia_in_obb = 1;
	}

	err = sdcardfs_interpose(dentry, dir->i_sb, &lower_path, pd->userid);
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
		&& (pd->perm == PERM_ANDROID) && (pd->userid == 0))
		make_nomedia_in_obb = 1;

	/* When creating /Android/data and /Android/obb, mark them as .nomedia */
	if (make_nomedia_in_obb ||
		((pd->perm == PERM_ANDROID)
				&& (qstr_case_eq(&dentry->d_name, &q_data)))) {
		revert_fsids(saved_cred);
		saved_cred = override_fsids(sbi,
					SDCARDFS_I(d_inode(dentry))->data);
		if (!saved_cred) {
			pr_err("sdcardfs: failed to set up .nomedia in %s: %d\n",
						lower_path.dentry->d_name.name,
						-ENOMEM);
			goto out;
		}
		set_fs_pwd(current->fs, &lower_path);
		touch_err = touch(".nomedia", 0664);
		if (touch_err) {
			pr_err("sdcardfs: failed to create .nomedia in %s: %d\n",
						lower_path.dentry->d_name.name,
						touch_err);
			goto out;
		}
	}
out:
	task_lock(current);
	current->fs = saved_fs;
	task_unlock(current);

	free_fs_struct(copied_fs);
out_unlock:
	sdcardfs_put_lower_path(dentry, &lower_path);
out_revert:
	revert_fsids(saved_cred);
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

	if (!check_caller_access_to_name(dir, &dentry->d_name)) {
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	saved_cred = override_fsids(SDCARDFS_SB(dir->i_sb),
						SDCARDFS_I(dir)->data);
	if (!saved_cred)
		return -ENOMEM;

	/* sdcardfs_get_real_lower(): in case of remove an user's obb dentry
	 * the dentry on the original path should be deleted.
	 */
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
	revert_fsids(saved_cred);
out_eacces:
	return err;
}

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

	if (!check_caller_access_to_name(old_dir, &old_dentry->d_name) ||
		!check_caller_access_to_name(new_dir, &new_dentry->d_name)) {
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	saved_cred = override_fsids(SDCARDFS_SB(old_dir->i_sb),
						SDCARDFS_I(new_dir)->data);
	if (!saved_cred)
		return -ENOMEM;

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
	revert_fsids(saved_cred);
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
	struct sdcardfs_inode_data *top = top_data_get(SDCARDFS_I(inode));

	if (IS_ERR(mnt))
		return PTR_ERR(mnt);
	if (!top)
		return -EINVAL;

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
	tmp.i_uid = make_kuid(&init_user_ns, top->d_uid);
	tmp.i_gid = make_kgid(&init_user_ns, get_gid(mnt, inode->i_sb, top));
	tmp.i_mode = (inode->i_mode & S_IFMT)
			| get_mode(mnt, SDCARDFS_I(inode), top);
	data_put(top);
	tmp.i_sb = inode->i_sb;
	if (IS_POSIXACL(inode))
		pr_warn("%s: This may be undefined behavior...\n", __func__);
	err = generic_permission(&tmp, mask);
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
	struct sdcardfs_inode_data *top;
	const struct cred *saved_cred = NULL;

	inode = d_inode(dentry);
	top = top_data_get(SDCARDFS_I(inode));

	if (!top)
		return -EINVAL;

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
	tmp.i_uid = make_kuid(&init_user_ns, top->d_uid);
	tmp.i_gid = make_kgid(&init_user_ns, get_gid(mnt, dentry->d_sb, top));
	tmp.i_mode = (inode->i_mode & S_IFMT)
			| get_mode(mnt, SDCARDFS_I(inode), top);
	tmp.i_size = i_size_read(inode);
	data_put(top);
	tmp.i_sb = inode->i_sb;

	/*
	 * Check if user has permission to change inode.  We don't check if
	 * this user can change the lower inode: that should happen when
	 * calling notify_change on the lower inode.
	 */
	/* prepare our own lower struct iattr (with the lower file) */
	memcpy(&lower_ia, ia, sizeof(lower_ia));
	/* Allow touch updating timestamps. A previous permission check ensures
	 * we have write access. Changes to mode, owner, and group are ignored
	 */
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
	saved_cred = override_fsids(SDCARDFS_SB(dentry->d_sb),
						SDCARDFS_I(inode)->data);
	if (!saved_cred)
		return -ENOMEM;

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
	if (ia->ia_valid & ATTR_SIZE) {
		err = inode_newsize_ok(&tmp, ia->ia_size);
		if (err) {
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
	revert_fsids(saved_cred);
out_err:
	return err;
}

static int sdcardfs_fillattr(struct vfsmount *mnt, struct inode *inode,
				struct kstat *lower_stat, struct kstat *stat)
{
	struct sdcardfs_inode_info *info = SDCARDFS_I(inode);
	struct sdcardfs_inode_data *top = top_data_get(info);
	struct super_block *sb = inode->i_sb;

	if (!top)
		return -EINVAL;

	stat->dev = inode->i_sb->s_dev;
	stat->ino = inode->i_ino;
	stat->mode = (inode->i_mode  & S_IFMT) | get_mode(mnt, info, top);
	stat->nlink = inode->i_nlink;
	stat->uid = make_kuid(&init_user_ns, top->d_uid);
	stat->gid = make_kgid(&init_user_ns, get_gid(mnt, sb, top));
	stat->rdev = inode->i_rdev;
	stat->size = lower_stat->size;
	stat->atime = lower_stat->atime;
	stat->mtime = lower_stat->mtime;
	stat->ctime = lower_stat->ctime;
	stat->blksize = lower_stat->blksize;
	stat->blocks = lower_stat->blocks;
	data_put(top);
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
	if (!check_caller_access_to_name(d_inode(parent), &dentry->d_name)) {
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
	err = sdcardfs_fillattr(mnt, d_inode(dentry), &lower_stat, stat);
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
};

const struct inode_operations sdcardfs_main_iops = {
	.permission	= sdcardfs_permission_wrn,
	.permission2	= sdcardfs_permission,
	.setattr	= sdcardfs_setattr_wrn,
	.setattr2	= sdcardfs_setattr,
	.getattr	= sdcardfs_getattr,
};
