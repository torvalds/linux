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

/* Do not directly use this function. Use OVERRIDE_CRED() instead. */
const struct cred * override_fsids(struct sdcardfs_sb_info* sbi)
{
	struct cred * cred;
	const struct cred * old_cred;

	cred = prepare_creds();
	if (!cred)
		return NULL;

	cred->fsuid = make_kuid(&init_user_ns, sbi->options.fs_low_uid);
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
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;
	const struct cred *saved_cred = NULL;

	if(!check_caller_access_to_name(dir, dentry->d_name.name)) {
		printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
						 "  dentry: %s, task:%s\n",
						 __func__, dentry->d_name.name, current->comm);
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb), saved_cred);

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = lock_parent(lower_dentry);

	/* set last 16bytes of mode field to 0664 */
	mode = (mode & S_IFMT) | 00664;
	err = vfs_create(d_inode(lower_parent_dentry), lower_dentry, mode, want_excl);
	if (err)
		goto out;

	err = sdcardfs_interpose(dentry, dir->i_sb, &lower_path, SDCARDFS_I(dir)->userid);
	if (err)
		goto out;
	fsstack_copy_attr_times(dir, sdcardfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));

out:
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
	struct inode *lower_dir_inode = sdcardfs_lower_inode(dir);
	struct dentry *lower_dir_dentry;
	struct path lower_path;
	const struct cred *saved_cred = NULL;

	if(!check_caller_access_to_name(dir, dentry->d_name.name)) {
		printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
						 "  dentry: %s, task:%s\n",
						 __func__, dentry->d_name.name, current->comm);
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb), saved_cred);

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	dget(lower_dentry);
	lower_dir_dentry = lock_parent(lower_dentry);

	err = vfs_unlink(lower_dir_inode, lower_dentry, NULL);

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
	struct dentry *lower_parent_dentry = NULL;
	struct path lower_path;
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dentry->d_sb);
	const struct cred *saved_cred = NULL;
	struct sdcardfs_inode_info *pi = SDCARDFS_I(dir);
	char *page_buf;
	char *nomedia_dir_name;
	char *nomedia_fullpath;
	int fullpath_namelen;
	int touch_err = 0;

	if(!check_caller_access_to_name(dir, dentry->d_name.name)) {
		printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
						 "  dentry: %s, task:%s\n",
						 __func__, dentry->d_name.name, current->comm);
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb), saved_cred);

	/* check disk space */
	if (!check_min_free_space(dentry, 0, 1)) {
		printk(KERN_INFO "sdcardfs: No minimum free space.\n");
		err = -ENOSPC;
		goto out_revert;
	}

	/* the lower_dentry is negative here */
	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_parent_dentry = lock_parent(lower_dentry);

	/* set last 16bytes of mode field to 0775 */
	mode = (mode & S_IFMT) | 00775;
	err = vfs_mkdir(d_inode(lower_parent_dentry), lower_dentry, mode);

	if (err)
		goto out;

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
	if (err)
		goto out;

	fsstack_copy_attr_times(dir, sdcardfs_lower_inode(dir));
	fsstack_copy_inode_size(dir, d_inode(lower_parent_dentry));
	/* update number of links on parent directory */
	set_nlink(dir, sdcardfs_lower_inode(dir)->i_nlink);

	if ((!sbi->options.multiuser) && (!strcasecmp(dentry->d_name.name, "obb"))
		&& (pi->perm == PERM_ANDROID) && (pi->userid == 0))
		make_nomedia_in_obb = 1;

	/* When creating /Android/data and /Android/obb, mark them as .nomedia */
	if (make_nomedia_in_obb ||
		((pi->perm == PERM_ANDROID) && (!strcasecmp(dentry->d_name.name, "data")))) {

		page_buf = (char *)__get_free_page(GFP_KERNEL);
		if (!page_buf) {
			printk(KERN_ERR "sdcardfs: failed to allocate page buf\n");
			goto out;
		}

		nomedia_dir_name = d_absolute_path(&lower_path, page_buf, PAGE_SIZE);
		if (IS_ERR(nomedia_dir_name)) {
			free_page((unsigned long)page_buf);
			printk(KERN_ERR "sdcardfs: failed to get .nomedia dir name\n");
			goto out;
		}

		fullpath_namelen = page_buf + PAGE_SIZE - nomedia_dir_name - 1;
		fullpath_namelen += strlen("/.nomedia");
		nomedia_fullpath = kzalloc(fullpath_namelen + 1, GFP_KERNEL);
		if (!nomedia_fullpath) {
			free_page((unsigned long)page_buf);
			printk(KERN_ERR "sdcardfs: failed to allocate .nomedia fullpath buf\n");
			goto out;
		}

		strcpy(nomedia_fullpath, nomedia_dir_name);
		free_page((unsigned long)page_buf);
		strcat(nomedia_fullpath, "/.nomedia");
		touch_err = touch(nomedia_fullpath, 0664);
		if (touch_err) {
			printk(KERN_ERR "sdcardfs: failed to touch(%s): %d\n",
							nomedia_fullpath, touch_err);
			kfree(nomedia_fullpath);
			goto out;
		}
		kfree(nomedia_fullpath);
	}
out:
	unlock_dir(lower_parent_dentry);
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
	int err;
	struct path lower_path;
	const struct cred *saved_cred = NULL;

	if(!check_caller_access_to_name(dir, dentry->d_name.name)) {
		printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
						 "  dentry: %s, task:%s\n",
						 __func__, dentry->d_name.name, current->comm);
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb), saved_cred);

	/* sdcardfs_get_real_lower(): in case of remove an user's obb dentry
	 * the dentry on the original path should be deleted. */
	sdcardfs_get_real_lower(dentry, &lower_path);

	lower_dentry = lower_path.dentry;
	lower_dir_dentry = lock_parent(lower_dentry);

	err = vfs_rmdir(d_inode(lower_dir_dentry), lower_dentry);
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
	struct dentry *trap = NULL;
	struct dentry *new_parent = NULL;
	struct path lower_old_path, lower_new_path;
	const struct cred *saved_cred = NULL;

	if(!check_caller_access_to_name(old_dir, old_dentry->d_name.name) ||
		!check_caller_access_to_name(new_dir, new_dentry->d_name.name)) {
		printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
						 "  new_dentry: %s, task:%s\n",
						 __func__, new_dentry->d_name.name, current->comm);
		err = -EACCES;
		goto out_eacces;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(old_dir->i_sb), saved_cred);

	sdcardfs_get_real_lower(old_dentry, &lower_old_path);
	sdcardfs_get_lower_path(new_dentry, &lower_new_path);
	lower_old_dentry = lower_old_path.dentry;
	lower_new_dentry = lower_new_path.dentry;
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

	err = vfs_rename(d_inode(lower_old_dir_dentry), lower_old_dentry,
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

		/* update the derived permission of the old_dentry
		 * with its new parent
		 */
		new_parent = dget_parent(new_dentry);
		if(new_parent) {
			if(d_inode(old_dentry)) {
				update_derived_permission_lock(old_dentry);
			}
			dput(new_parent);
		}
	}
	/* At this point, not all dentry information has been moved, so
	 * we pass along new_dentry for the name.*/
	mutex_lock(&d_inode(old_dentry)->i_mutex);
	get_derived_permission_new(new_dentry->d_parent, old_dentry, new_dentry);
	fix_derived_permission(d_inode(old_dentry));
	get_derive_permissions_recursive(old_dentry);
	mutex_unlock(&d_inode(old_dentry)->i_mutex);
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

static int sdcardfs_permission(struct inode *inode, int mask)
{
	int err;

	/*
	 * Permission check on sdcardfs inode.
	 * Calling process should have AID_SDCARD_RW permission
	 */
	err = generic_permission(inode, mask);

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

static int sdcardfs_setattr(struct dentry *dentry, struct iattr *ia)
{
	int err;
	struct dentry *lower_dentry;
	struct inode *inode;
	struct inode *lower_inode;
	struct path lower_path;
	struct iattr lower_ia;
	struct dentry *parent;

	inode = d_inode(dentry);

	/*
	 * Check if user has permission to change inode.  We don't check if
	 * this user can change the lower inode: that should happen when
	 * calling notify_change on the lower inode.
	 */
	err = inode_change_ok(inode, ia);

	/* no vfs_XXX operations required, cred overriding will be skipped. wj*/
	if (!err) {
		/* check the Android group ID */
		parent = dget_parent(dentry);
		if(!check_caller_access_to_name(d_inode(parent), dentry->d_name.name)) {
			printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
							 "  dentry: %s, task:%s\n",
							 __func__, dentry->d_name.name, current->comm);
			err = -EACCES;
		}
		dput(parent);
	}

	if (err)
		goto out_err;

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_inode = sdcardfs_lower_inode(inode);

	/* prepare our own lower struct iattr (with the lower file) */
	memcpy(&lower_ia, ia, sizeof(lower_ia));
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
		err = inode_newsize_ok(inode, ia->ia_size);
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
	err = notify_change(lower_dentry, &lower_ia, /* note: lower_ia */
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
out_err:
	return err;
}

static int sdcardfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		 struct kstat *stat)
{
	struct dentry *lower_dentry;
	struct inode *inode;
	struct inode *lower_inode;
	struct path lower_path;
	struct dentry *parent;

	parent = dget_parent(dentry);
	if(!check_caller_access_to_name(d_inode(parent), dentry->d_name.name)) {
		printk(KERN_INFO "%s: need to check the caller's gid in packages.list\n"
						 "  dentry: %s, task:%s\n",
						 __func__, dentry->d_name.name, current->comm);
		dput(parent);
		return -EACCES;
	}
	dput(parent);

	inode = d_inode(dentry);

	sdcardfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	lower_inode = sdcardfs_lower_inode(inode);


	sdcardfs_copy_and_fix_attrs(inode, lower_inode);
	fsstack_copy_inode_size(inode, lower_inode);


	generic_fillattr(inode, stat);
	sdcardfs_put_lower_path(dentry, &lower_path);
	return 0;
}

const struct inode_operations sdcardfs_symlink_iops = {
	.permission	= sdcardfs_permission,
	.setattr	= sdcardfs_setattr,
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
#if 0
	.permission	= sdcardfs_permission,
#endif
	.unlink		= sdcardfs_unlink,
	.mkdir		= sdcardfs_mkdir,
	.rmdir		= sdcardfs_rmdir,
	.rename		= sdcardfs_rename,
	.setattr	= sdcardfs_setattr,
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
	.permission	= sdcardfs_permission,
	.setattr	= sdcardfs_setattr,
	.getattr	= sdcardfs_getattr,
};
