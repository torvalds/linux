/*
 * fs/sdcardfs/derived_perm.c
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

/* copy derived state from parent inode */
static void inherit_derived_state(struct inode *parent, struct inode *child)
{
	struct sdcardfs_inode_info *pi = SDCARDFS_I(parent);
	struct sdcardfs_inode_info *ci = SDCARDFS_I(child);

	ci->perm = PERM_INHERIT;
	ci->userid = pi->userid;
	ci->d_uid = pi->d_uid;
	ci->under_android = pi->under_android;
	ci->under_cache = pi->under_cache;
	ci->under_obb = pi->under_obb;
	set_top(ci, pi->top);
}

/* helper function for derived state */
void setup_derived_state(struct inode *inode, perm_t perm, userid_t userid,
                        uid_t uid, bool under_android, struct inode *top)
{
	struct sdcardfs_inode_info *info = SDCARDFS_I(inode);

	info->perm = perm;
	info->userid = userid;
	info->d_uid = uid;
	info->under_android = under_android;
	info->under_cache = false;
	info->under_obb = false;
	set_top(info, top);
}

/* While renaming, there is a point where we want the path from dentry, but the name from newdentry */
void get_derived_permission_new(struct dentry *parent, struct dentry *dentry, const struct qstr *name)
{
	struct sdcardfs_inode_info *info = SDCARDFS_I(d_inode(dentry));
	struct sdcardfs_inode_info *parent_info= SDCARDFS_I(d_inode(parent));
	appid_t appid;
	struct qstr q_Android = QSTR_LITERAL("Android");
	struct qstr q_data = QSTR_LITERAL("data");
	struct qstr q_obb = QSTR_LITERAL("obb");
	struct qstr q_media = QSTR_LITERAL("media");
	struct qstr q_cache = QSTR_LITERAL("cache");

	/* By default, each inode inherits from its parent.
	 * the properties are maintained on its private fields
	 * because the inode attributes will be modified with that of
	 * its lower inode.
	 * These values are used by our custom permission call instead
	 * of using the inode permissions.
	 */

	inherit_derived_state(d_inode(parent), d_inode(dentry));

	/* Files don't get special labels */
	if (!S_ISDIR(d_inode(dentry)->i_mode))
		return;
	/* Derive custom permissions based on parent and current node */
	switch (parent_info->perm) {
		case PERM_INHERIT:
		case PERM_ANDROID_PACKAGE_CACHE:
			/* Already inherited above */
			break;
		case PERM_PRE_ROOT:
			/* Legacy internal layout places users at top level */
			info->perm = PERM_ROOT;
			info->userid = simple_strtoul(name->name, NULL, 10);
			set_top(info, &info->vfs_inode);
			break;
		case PERM_ROOT:
			/* Assume masked off by default. */
			if (qstr_case_eq(name, &q_Android)) {
				/* App-specific directories inside; let anyone traverse */
				info->perm = PERM_ANDROID;
				info->under_android = true;
				set_top(info, &info->vfs_inode);
			}
			break;
		case PERM_ANDROID:
			if (qstr_case_eq(name, &q_data)) {
				/* App-specific directories inside; let anyone traverse */
				info->perm = PERM_ANDROID_DATA;
				set_top(info, &info->vfs_inode);
			} else if (qstr_case_eq(name, &q_obb)) {
				/* App-specific directories inside; let anyone traverse */
				info->perm = PERM_ANDROID_OBB;
				info->under_obb = true;
				set_top(info, &info->vfs_inode);
				/* Single OBB directory is always shared */
			} else if (qstr_case_eq(name, &q_media)) {
				/* App-specific directories inside; let anyone traverse */
				info->perm = PERM_ANDROID_MEDIA;
				set_top(info, &info->vfs_inode);
			}
			break;
		case PERM_ANDROID_OBB:
		case PERM_ANDROID_DATA:
		case PERM_ANDROID_MEDIA:
			info->perm = PERM_ANDROID_PACKAGE;
			appid = get_appid(name->name);
			if (appid != 0 && !is_excluded(name->name, parent_info->userid)) {
				info->d_uid = multiuser_get_uid(parent_info->userid, appid);
			}
			set_top(info, &info->vfs_inode);
			break;
		case PERM_ANDROID_PACKAGE:
			if (qstr_case_eq(name, &q_cache)) {
				info->perm = PERM_ANDROID_PACKAGE_CACHE;
				info->under_cache = true;
			}
			break;
	}
}

void get_derived_permission(struct dentry *parent, struct dentry *dentry)
{
	get_derived_permission_new(parent, dentry, &dentry->d_name);
}

static appid_t get_type(const char *name) {
	const char *ext = strrchr(name, '.');
	appid_t id;

	if (ext && ext[0]) {
		ext = &ext[1];
		id = get_ext_gid(ext);
		return id?:AID_MEDIA_RW;
	}
	return AID_MEDIA_RW;
}

void fixup_lower_ownership(struct dentry* dentry, const char *name) {
	struct path path;
	struct inode *inode;
	struct inode *delegated_inode = NULL;
	int error;
	struct sdcardfs_inode_info *info;
	struct sdcardfs_inode_info *info_top;
	perm_t perm;
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dentry->d_sb);
	uid_t uid = sbi->options.fs_low_uid;
	gid_t gid = sbi->options.fs_low_gid;
	struct iattr newattrs;

	info = SDCARDFS_I(d_inode(dentry));
	perm = info->perm;
	if (info->under_obb) {
		perm = PERM_ANDROID_OBB;
	} else if (info->under_cache) {
		perm = PERM_ANDROID_PACKAGE_CACHE;
	} else if (perm == PERM_INHERIT) {
		info_top = SDCARDFS_I(grab_top(info));
		perm = info_top->perm;
		release_top(info);
	}

	switch (perm) {
		case PERM_ROOT:
		case PERM_ANDROID:
		case PERM_ANDROID_DATA:
		case PERM_ANDROID_MEDIA:
		case PERM_ANDROID_PACKAGE:
		case PERM_ANDROID_PACKAGE_CACHE:
			uid = multiuser_get_uid(info->userid, uid);
			break;
		case PERM_ANDROID_OBB:
			uid = AID_MEDIA_OBB;
			break;
		case PERM_PRE_ROOT:
		default:
			break;
	}
	switch (perm) {
		case PERM_ROOT:
		case PERM_ANDROID:
		case PERM_ANDROID_DATA:
		case PERM_ANDROID_MEDIA:
			if (S_ISDIR(d_inode(dentry)->i_mode))
				gid = multiuser_get_uid(info->userid, AID_MEDIA_RW);
			else
				gid = multiuser_get_uid(info->userid, get_type(name));
			break;
		case PERM_ANDROID_OBB:
			gid = AID_MEDIA_OBB;
			break;
		case PERM_ANDROID_PACKAGE:
			if (info->d_uid != 0)
				gid = multiuser_get_ext_gid(info->userid, info->d_uid);
			else
				gid = multiuser_get_uid(info->userid, uid);
			break;
		case PERM_ANDROID_PACKAGE_CACHE:
			if (info->d_uid != 0)
				gid = multiuser_get_cache_gid(info->userid, info->d_uid);
			else
				gid = multiuser_get_uid(info->userid, uid);
			break;
		case PERM_PRE_ROOT:
		default:
			break;
	}

	sdcardfs_get_lower_path(dentry, &path);
	inode = d_inode(path.dentry);
	if (d_inode(path.dentry)->i_gid.val != gid || d_inode(path.dentry)->i_uid.val != uid) {
retry_deleg:
		newattrs.ia_valid = ATTR_GID | ATTR_UID | ATTR_FORCE;
		newattrs.ia_uid = make_kuid(current_user_ns(), uid);
		newattrs.ia_gid = make_kgid(current_user_ns(), gid);
		if (!S_ISDIR(inode->i_mode))
			newattrs.ia_valid |=
				ATTR_KILL_SUID | ATTR_KILL_SGID | ATTR_KILL_PRIV;
		mutex_lock(&inode->i_mutex);
		error = security_path_chown(&path, newattrs.ia_uid, newattrs.ia_gid);
		if (!error)
			error = notify_change2(path.mnt, path.dentry, &newattrs, &delegated_inode);
		mutex_unlock(&inode->i_mutex);
		if (delegated_inode) {
			error = break_deleg_wait(&delegated_inode);
			if (!error)
				goto retry_deleg;
		}
		if (error)
			pr_err("sdcardfs: Failed to touch up lower fs gid/uid.\n");
	}
}

static int descendant_may_need_fixup(struct sdcardfs_inode_info *info, struct limit_search *limit) {
	if (info->perm == PERM_ROOT)
		return (limit->flags & BY_USERID)?info->userid == limit->userid:1;
	if (info->perm == PERM_PRE_ROOT || info->perm == PERM_ANDROID)
		return 1;
	return 0;
}

static int needs_fixup(perm_t perm) {
	if (perm == PERM_ANDROID_DATA || perm == PERM_ANDROID_OBB
			|| perm == PERM_ANDROID_MEDIA)
		return 1;
	return 0;
}

void fixup_perms_recursive(struct dentry *dentry, struct limit_search *limit) {
	struct dentry *child;
	struct sdcardfs_inode_info *info;
	if (!dget(dentry))
		return;
	if (!d_inode(dentry)) {
		dput(dentry);
		return;
	}
	info = SDCARDFS_I(d_inode(dentry));

	if (needs_fixup(info->perm)) {
		spin_lock(&dentry->d_lock);
		list_for_each_entry(child, &dentry->d_subdirs, d_child) {
			dget(child);
			if (!(limit->flags & BY_NAME) || !strncasecmp(child->d_name.name, limit->name, limit->length)) {
				if (d_inode(child)) {
					get_derived_permission(dentry, child);
					fixup_tmp_permissions(d_inode(child));
					dput(child);
					break;
				}
			}
			dput(child);
		}
		spin_unlock(&dentry->d_lock);
	} else 	if (descendant_may_need_fixup(info, limit)) {
		spin_lock(&dentry->d_lock);
		list_for_each_entry(child, &dentry->d_subdirs, d_child) {
				fixup_perms_recursive(child, limit);
		}
		spin_unlock(&dentry->d_lock);
	}
	dput(dentry);
}

void drop_recursive(struct dentry *parent) {
	struct dentry *dentry;
	struct sdcardfs_inode_info *info;
	if (!d_inode(parent))
		return;
	info = SDCARDFS_I(d_inode(parent));
	spin_lock(&parent->d_lock);
	list_for_each_entry(dentry, &parent->d_subdirs, d_child) {
		if (d_inode(dentry)) {
			if (SDCARDFS_I(d_inode(parent))->top != SDCARDFS_I(d_inode(dentry))->top) {
				drop_recursive(dentry);
				d_drop(dentry);
			}
		}
	}
	spin_unlock(&parent->d_lock);
}

void fixup_top_recursive(struct dentry *parent) {
	struct dentry *dentry;
	struct sdcardfs_inode_info *info;

	if (!d_inode(parent))
		return;
	info = SDCARDFS_I(d_inode(parent));
	spin_lock(&parent->d_lock);
	list_for_each_entry(dentry, &parent->d_subdirs, d_child) {
		if (d_inode(dentry)) {
			if (SDCARDFS_I(d_inode(parent))->top != SDCARDFS_I(d_inode(dentry))->top) {
				get_derived_permission(parent, dentry);
				fixup_tmp_permissions(d_inode(dentry));
				fixup_top_recursive(dentry);
			}
		}
	}
	spin_unlock(&parent->d_lock);
}

/* main function for updating derived permission */
inline void update_derived_permission_lock(struct dentry *dentry)
{
	struct dentry *parent;

	if(!dentry || !d_inode(dentry)) {
		printk(KERN_ERR "sdcardfs: %s: invalid dentry\n", __func__);
		return;
	}
	/* FIXME:
	 * 1. need to check whether the dentry is updated or not
	 * 2. remove the root dentry update
	 */
	if(IS_ROOT(dentry)) {
		//setup_default_pre_root_state(d_inode(dentry));
	} else {
		parent = dget_parent(dentry);
		if(parent) {
			get_derived_permission(parent, dentry);
			dput(parent);
		}
	}
	fixup_tmp_permissions(d_inode(dentry));
}

int need_graft_path(struct dentry *dentry)
{
	int ret = 0;
	struct dentry *parent = dget_parent(dentry);
	struct sdcardfs_inode_info *parent_info= SDCARDFS_I(d_inode(parent));
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dentry->d_sb);
	struct qstr obb = QSTR_LITERAL("obb");

	if(parent_info->perm == PERM_ANDROID &&
			qstr_case_eq(&dentry->d_name, &obb)) {

		/* /Android/obb is the base obbpath of DERIVED_UNIFIED */
		if(!(sbi->options.multiuser == false
				&& parent_info->userid == 0)) {
			ret = 1;
		}
	}
	dput(parent);
	return ret;
}

int is_obbpath_invalid(struct dentry *dent)
{
	int ret = 0;
	struct sdcardfs_dentry_info *di = SDCARDFS_D(dent);
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dent->d_sb);
	char *path_buf, *obbpath_s;

	/* check the base obbpath has been changed.
	 * this routine can check an uninitialized obb dentry as well.
	 * regarding the uninitialized obb, refer to the sdcardfs_mkdir() */
	spin_lock(&di->lock);
	if(di->orig_path.dentry) {
 		if(!di->lower_path.dentry) {
			ret = 1;
		} else {
			path_get(&di->lower_path);
			//lower_parent = lock_parent(lower_path->dentry);

			path_buf = kmalloc(PATH_MAX, GFP_ATOMIC);
			if(!path_buf) {
				ret = 1;
				printk(KERN_ERR "sdcardfs: fail to allocate path_buf in %s.\n", __func__);
			} else {
				obbpath_s = d_path(&di->lower_path, path_buf, PATH_MAX);
				if (d_unhashed(di->lower_path.dentry) ||
					!str_case_eq(sbi->obbpath_s, obbpath_s)) {
					ret = 1;
				}
				kfree(path_buf);
			}

			//unlock_dir(lower_parent);
			path_put(&di->lower_path);
		}
	}
	spin_unlock(&di->lock);
	return ret;
}

int is_base_obbpath(struct dentry *dentry)
{
	int ret = 0;
	struct dentry *parent = dget_parent(dentry);
	struct sdcardfs_inode_info *parent_info= SDCARDFS_I(d_inode(parent));
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dentry->d_sb);
	struct qstr q_obb = QSTR_LITERAL("obb");

	spin_lock(&SDCARDFS_D(dentry)->lock);
	if (sbi->options.multiuser) {
		if(parent_info->perm == PERM_PRE_ROOT &&
				qstr_case_eq(&dentry->d_name, &q_obb)) {
			ret = 1;
		}
	} else  if (parent_info->perm == PERM_ANDROID &&
			qstr_case_eq(&dentry->d_name, &q_obb)) {
		ret = 1;
	}
	spin_unlock(&SDCARDFS_D(dentry)->lock);
	return ret;
}

/* The lower_path will be stored to the dentry's orig_path
 * and the base obbpath will be copyed to the lower_path variable.
 * if an error returned, there's no change in the lower_path
 * returns: -ERRNO if error (0: no error) */
int setup_obb_dentry(struct dentry *dentry, struct path *lower_path)
{
	int err = 0;
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dentry->d_sb);
	struct path obbpath;

	/* A local obb dentry must have its own orig_path to support rmdir
	 * and mkdir of itself. Usually, we expect that the sbi->obbpath
	 * is avaiable on this stage. */
	sdcardfs_set_orig_path(dentry, lower_path);

	err = kern_path(sbi->obbpath_s,
			LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &obbpath);

	if(!err) {
		/* the obbpath base has been found */
		printk(KERN_INFO "sdcardfs: the sbi->obbpath is found\n");
		pathcpy(lower_path, &obbpath);
	} else {
		/* if the sbi->obbpath is not available, we can optionally
		 * setup the lower_path with its orig_path.
		 * but, the current implementation just returns an error
		 * because the sdcard daemon also regards this case as
		 * a lookup fail. */
		printk(KERN_INFO "sdcardfs: the sbi->obbpath is not available\n");
	}
	return err;
}


