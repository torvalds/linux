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

	ci->data->perm = PERM_INHERIT;
	ci->data->userid = pi->data->userid;
	ci->data->d_uid = pi->data->d_uid;
	ci->data->under_android = pi->data->under_android;
	ci->data->under_cache = pi->data->under_cache;
	ci->data->under_obb = pi->data->under_obb;
	set_top(ci, pi->top_data);
}

/* helper function for derived state */
void setup_derived_state(struct inode *inode, perm_t perm, userid_t userid,
					uid_t uid, bool under_android,
					struct sdcardfs_inode_data *top)
{
	struct sdcardfs_inode_info *info = SDCARDFS_I(inode);

	info->data->perm = perm;
	info->data->userid = userid;
	info->data->d_uid = uid;
	info->data->under_android = under_android;
	info->data->under_cache = false;
	info->data->under_obb = false;
	set_top(info, top);
}

/* While renaming, there is a point where we want the path from dentry,
 * but the name from newdentry
 */
void get_derived_permission_new(struct dentry *parent, struct dentry *dentry,
				const struct qstr *name)
{
	struct sdcardfs_inode_info *info = SDCARDFS_I(d_inode(dentry));
	struct sdcardfs_inode_data *parent_data =
			SDCARDFS_I(d_inode(parent))->data;
	appid_t appid;
	unsigned long user_num;
	int err;
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
	switch (parent_data->perm) {
	case PERM_INHERIT:
	case PERM_ANDROID_PACKAGE_CACHE:
		/* Already inherited above */
		break;
	case PERM_PRE_ROOT:
		/* Legacy internal layout places users at top level */
		info->data->perm = PERM_ROOT;
		err = kstrtoul(name->name, 10, &user_num);
		if (err)
			info->data->userid = 0;
		else
			info->data->userid = user_num;
		set_top(info, info->data);
		break;
	case PERM_ROOT:
		/* Assume masked off by default. */
		if (qstr_case_eq(name, &q_Android)) {
			/* App-specific directories inside; let anyone traverse */
			info->data->perm = PERM_ANDROID;
			info->data->under_android = true;
			set_top(info, info->data);
		}
		break;
	case PERM_ANDROID:
		if (qstr_case_eq(name, &q_data)) {
			/* App-specific directories inside; let anyone traverse */
			info->data->perm = PERM_ANDROID_DATA;
			set_top(info, info->data);
		} else if (qstr_case_eq(name, &q_obb)) {
			/* App-specific directories inside; let anyone traverse */
			info->data->perm = PERM_ANDROID_OBB;
			info->data->under_obb = true;
			set_top(info, info->data);
			/* Single OBB directory is always shared */
		} else if (qstr_case_eq(name, &q_media)) {
			/* App-specific directories inside; let anyone traverse */
			info->data->perm = PERM_ANDROID_MEDIA;
			set_top(info, info->data);
		}
		break;
	case PERM_ANDROID_OBB:
	case PERM_ANDROID_DATA:
	case PERM_ANDROID_MEDIA:
		info->data->perm = PERM_ANDROID_PACKAGE;
		appid = get_appid(name->name);
		if (appid != 0 && !is_excluded(name->name, parent_data->userid))
			info->data->d_uid =
				multiuser_get_uid(parent_data->userid, appid);
		set_top(info, info->data);
		break;
	case PERM_ANDROID_PACKAGE:
		if (qstr_case_eq(name, &q_cache)) {
			info->data->perm = PERM_ANDROID_PACKAGE_CACHE;
			info->data->under_cache = true;
		}
		break;
	}
}

void get_derived_permission(struct dentry *parent, struct dentry *dentry)
{
	get_derived_permission_new(parent, dentry, &dentry->d_name);
}

static appid_t get_type(const char *name)
{
	const char *ext = strrchr(name, '.');
	appid_t id;

	if (ext && ext[0]) {
		ext = &ext[1];
		id = get_ext_gid(ext);
		return id?:AID_MEDIA_RW;
	}
	return AID_MEDIA_RW;
}

void fixup_lower_ownership(struct dentry *dentry, const char *name)
{
	struct path path;
	struct inode *inode;
	struct inode *delegated_inode = NULL;
	int error;
	struct sdcardfs_inode_info *info;
	struct sdcardfs_inode_data *info_d;
	struct sdcardfs_inode_data *info_top;
	perm_t perm;
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dentry->d_sb);
	uid_t uid = sbi->options.fs_low_uid;
	gid_t gid = sbi->options.fs_low_gid;
	struct iattr newattrs;

	if (!sbi->options.gid_derivation)
		return;

	info = SDCARDFS_I(d_inode(dentry));
	info_d = info->data;
	perm = info_d->perm;
	if (info_d->under_obb) {
		perm = PERM_ANDROID_OBB;
	} else if (info_d->under_cache) {
		perm = PERM_ANDROID_PACKAGE_CACHE;
	} else if (perm == PERM_INHERIT) {
		info_top = top_data_get(info);
		perm = info_top->perm;
		data_put(info_top);
	}

	switch (perm) {
	case PERM_ROOT:
	case PERM_ANDROID:
	case PERM_ANDROID_DATA:
	case PERM_ANDROID_MEDIA:
	case PERM_ANDROID_PACKAGE:
	case PERM_ANDROID_PACKAGE_CACHE:
		uid = multiuser_get_uid(info_d->userid, uid);
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
			gid = multiuser_get_uid(info_d->userid, AID_MEDIA_RW);
		else
			gid = multiuser_get_uid(info_d->userid, get_type(name));
		break;
	case PERM_ANDROID_OBB:
		gid = AID_MEDIA_OBB;
		break;
	case PERM_ANDROID_PACKAGE:
		if (uid_is_app(info_d->d_uid))
			gid = multiuser_get_ext_gid(info_d->d_uid);
		else
			gid = multiuser_get_uid(info_d->userid, AID_MEDIA_RW);
		break;
	case PERM_ANDROID_PACKAGE_CACHE:
		if (uid_is_app(info_d->d_uid))
			gid = multiuser_get_ext_cache_gid(info_d->d_uid);
		else
			gid = multiuser_get_uid(info_d->userid, AID_MEDIA_RW);
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
			pr_debug("sdcardfs: Failed to touch up lower fs gid/uid for %s\n", name);
	}
	sdcardfs_put_lower_path(dentry, &path);
}

static int descendant_may_need_fixup(struct sdcardfs_inode_data *data,
		struct limit_search *limit)
{
	if (data->perm == PERM_ROOT)
		return (limit->flags & BY_USERID) ?
				data->userid == limit->userid : 1;
	if (data->perm == PERM_PRE_ROOT || data->perm == PERM_ANDROID)
		return 1;
	return 0;
}

static int needs_fixup(perm_t perm)
{
	if (perm == PERM_ANDROID_DATA || perm == PERM_ANDROID_OBB
			|| perm == PERM_ANDROID_MEDIA)
		return 1;
	return 0;
}

static void __fixup_perms_recursive(struct dentry *dentry, struct limit_search *limit, int depth)
{
	struct dentry *child;
	struct sdcardfs_inode_info *info;

	/*
	 * All paths will terminate their recursion on hitting PERM_ANDROID_OBB,
	 * PERM_ANDROID_MEDIA, or PERM_ANDROID_DATA. This happens at a depth of
	 * at most 3.
	 */
	WARN(depth > 3, "%s: Max expected depth exceeded!\n", __func__);
	spin_lock_nested(&dentry->d_lock, depth);
	if (!d_inode(dentry)) {
		spin_unlock(&dentry->d_lock);
		return;
	}
	info = SDCARDFS_I(d_inode(dentry));

	if (needs_fixup(info->data->perm)) {
		list_for_each_entry(child, &dentry->d_subdirs, d_child) {
			spin_lock_nested(&child->d_lock, depth + 1);
			if (!(limit->flags & BY_NAME) || qstr_case_eq(&child->d_name, &limit->name)) {
				if (d_inode(child)) {
					get_derived_permission(dentry, child);
					fixup_tmp_permissions(d_inode(child));
					spin_unlock(&child->d_lock);
					break;
				}
			}
			spin_unlock(&child->d_lock);
		}
	} else if (descendant_may_need_fixup(info->data, limit)) {
		list_for_each_entry(child, &dentry->d_subdirs, d_child) {
			__fixup_perms_recursive(child, limit, depth + 1);
		}
	}
	spin_unlock(&dentry->d_lock);
}

void fixup_perms_recursive(struct dentry *dentry, struct limit_search *limit)
{
	__fixup_perms_recursive(dentry, limit, 0);
}

/* main function for updating derived permission */
inline void update_derived_permission_lock(struct dentry *dentry)
{
	struct dentry *parent;

	if (!dentry || !d_inode(dentry)) {
		pr_err("sdcardfs: %s: invalid dentry\n", __func__);
		return;
	}
	/* FIXME:
	 * 1. need to check whether the dentry is updated or not
	 * 2. remove the root dentry update
	 */
	if (!IS_ROOT(dentry)) {
		parent = dget_parent(dentry);
		if (parent) {
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
	struct sdcardfs_inode_info *parent_info = SDCARDFS_I(d_inode(parent));
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dentry->d_sb);
	struct qstr obb = QSTR_LITERAL("obb");

	if (parent_info->data->perm == PERM_ANDROID &&
			qstr_case_eq(&dentry->d_name, &obb)) {

		/* /Android/obb is the base obbpath of DERIVED_UNIFIED */
		if (!(sbi->options.multiuser == false
				&& parent_info->data->userid == 0)) {
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
	int need_put = 0;
	struct path lower_path;

	/* check the base obbpath has been changed.
	 * this routine can check an uninitialized obb dentry as well.
	 * regarding the uninitialized obb, refer to the sdcardfs_mkdir()
	 */
	spin_lock(&di->lock);
	if (di->orig_path.dentry) {
		if (!di->lower_path.dentry) {
			ret = 1;
		} else {
			path_get(&di->lower_path);

			path_buf = kmalloc(PATH_MAX, GFP_ATOMIC);
			if (!path_buf) {
				ret = 1;
				pr_err("sdcardfs: fail to allocate path_buf in %s.\n", __func__);
			} else {
				obbpath_s = d_path(&di->lower_path, path_buf, PATH_MAX);
				if (d_unhashed(di->lower_path.dentry) ||
					!str_case_eq(sbi->obbpath_s, obbpath_s)) {
					ret = 1;
				}
				kfree(path_buf);
			}

			pathcpy(&lower_path, &di->lower_path);
			need_put = 1;
		}
	}
	spin_unlock(&di->lock);
	if (need_put)
		path_put(&lower_path);
	return ret;
}

int is_base_obbpath(struct dentry *dentry)
{
	int ret = 0;
	struct dentry *parent = dget_parent(dentry);
	struct sdcardfs_inode_info *parent_info = SDCARDFS_I(d_inode(parent));
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dentry->d_sb);
	struct qstr q_obb = QSTR_LITERAL("obb");

	spin_lock(&SDCARDFS_D(dentry)->lock);
	if (sbi->options.multiuser) {
		if (parent_info->data->perm == PERM_PRE_ROOT &&
				qstr_case_eq(&dentry->d_name, &q_obb)) {
			ret = 1;
		}
	} else  if (parent_info->data->perm == PERM_ANDROID &&
			qstr_case_eq(&dentry->d_name, &q_obb)) {
		ret = 1;
	}
	spin_unlock(&SDCARDFS_D(dentry)->lock);
	return ret;
}

/* The lower_path will be stored to the dentry's orig_path
 * and the base obbpath will be copyed to the lower_path variable.
 * if an error returned, there's no change in the lower_path
 * returns: -ERRNO if error (0: no error)
 */
int setup_obb_dentry(struct dentry *dentry, struct path *lower_path)
{
	int err = 0;
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dentry->d_sb);
	struct path obbpath;

	/* A local obb dentry must have its own orig_path to support rmdir
	 * and mkdir of itself. Usually, we expect that the sbi->obbpath
	 * is avaiable on this stage.
	 */
	sdcardfs_set_orig_path(dentry, lower_path);

	err = kern_path(sbi->obbpath_s,
			LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &obbpath);

	if (!err) {
		/* the obbpath base has been found */
		pathcpy(lower_path, &obbpath);
	} else {
		/* if the sbi->obbpath is not available, we can optionally
		 * setup the lower_path with its orig_path.
		 * but, the current implementation just returns an error
		 * because the sdcard daemon also regards this case as
		 * a lookup fail.
		 */
		pr_info("sdcardfs: the sbi->obbpath is not available\n");
	}
	return err;
}


