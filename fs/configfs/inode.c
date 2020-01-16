// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
 * iyesde.c - basic iyesde and dentry operations.
 *
 * Based on sysfs:
 * 	sysfs is Copyright (C) 2001, 2002, 2003 Patrick Mochel
 *
 * configfs Copyright (C) 2005 Oracle.  All rights reserved.
 *
 * Please see Documentation/filesystems/configfs/configfs.txt for more
 * information.
 */

#undef DEBUG

#include <linux/pagemap.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/lockdep.h>
#include <linux/slab.h>

#include <linux/configfs.h>
#include "configfs_internal.h"

#ifdef CONFIG_LOCKDEP
static struct lock_class_key default_group_class[MAX_LOCK_DEPTH];
#endif

static const struct address_space_operations configfs_aops = {
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
};

static const struct iyesde_operations configfs_iyesde_operations ={
	.setattr	= configfs_setattr,
};

int configfs_setattr(struct dentry * dentry, struct iattr * iattr)
{
	struct iyesde * iyesde = d_iyesde(dentry);
	struct configfs_dirent * sd = dentry->d_fsdata;
	struct iattr * sd_iattr;
	unsigned int ia_valid = iattr->ia_valid;
	int error;

	if (!sd)
		return -EINVAL;

	sd_iattr = sd->s_iattr;
	if (!sd_iattr) {
		/* setting attributes for the first time, allocate yesw */
		sd_iattr = kzalloc(sizeof(struct iattr), GFP_KERNEL);
		if (!sd_iattr)
			return -ENOMEM;
		/* assign default attributes */
		sd_iattr->ia_mode = sd->s_mode;
		sd_iattr->ia_uid = GLOBAL_ROOT_UID;
		sd_iattr->ia_gid = GLOBAL_ROOT_GID;
		sd_iattr->ia_atime = sd_iattr->ia_mtime =
			sd_iattr->ia_ctime = current_time(iyesde);
		sd->s_iattr = sd_iattr;
	}
	/* attributes were changed atleast once in past */

	error = simple_setattr(dentry, iattr);
	if (error)
		return error;

	if (ia_valid & ATTR_UID)
		sd_iattr->ia_uid = iattr->ia_uid;
	if (ia_valid & ATTR_GID)
		sd_iattr->ia_gid = iattr->ia_gid;
	if (ia_valid & ATTR_ATIME)
		sd_iattr->ia_atime = timestamp_truncate(iattr->ia_atime,
						      iyesde);
	if (ia_valid & ATTR_MTIME)
		sd_iattr->ia_mtime = timestamp_truncate(iattr->ia_mtime,
						      iyesde);
	if (ia_valid & ATTR_CTIME)
		sd_iattr->ia_ctime = timestamp_truncate(iattr->ia_ctime,
						      iyesde);
	if (ia_valid & ATTR_MODE) {
		umode_t mode = iattr->ia_mode;

		if (!in_group_p(iyesde->i_gid) && !capable(CAP_FSETID))
			mode &= ~S_ISGID;
		sd_iattr->ia_mode = sd->s_mode = mode;
	}

	return error;
}

static inline void set_default_iyesde_attr(struct iyesde * iyesde, umode_t mode)
{
	iyesde->i_mode = mode;
	iyesde->i_atime = iyesde->i_mtime =
		iyesde->i_ctime = current_time(iyesde);
}

static inline void set_iyesde_attr(struct iyesde * iyesde, struct iattr * iattr)
{
	iyesde->i_mode = iattr->ia_mode;
	iyesde->i_uid = iattr->ia_uid;
	iyesde->i_gid = iattr->ia_gid;
	iyesde->i_atime = iattr->ia_atime;
	iyesde->i_mtime = iattr->ia_mtime;
	iyesde->i_ctime = iattr->ia_ctime;
}

struct iyesde *configfs_new_iyesde(umode_t mode, struct configfs_dirent *sd,
				 struct super_block *s)
{
	struct iyesde * iyesde = new_iyesde(s);
	if (iyesde) {
		iyesde->i_iyes = get_next_iyes();
		iyesde->i_mapping->a_ops = &configfs_aops;
		iyesde->i_op = &configfs_iyesde_operations;

		if (sd->s_iattr) {
			/* sysfs_dirent has yesn-default attributes
			 * get them for the new iyesde from persistent copy
			 * in sysfs_dirent
			 */
			set_iyesde_attr(iyesde, sd->s_iattr);
		} else
			set_default_iyesde_attr(iyesde, mode);
	}
	return iyesde;
}

#ifdef CONFIG_LOCKDEP

static void configfs_set_iyesde_lock_class(struct configfs_dirent *sd,
					  struct iyesde *iyesde)
{
	int depth = sd->s_depth;

	if (depth > 0) {
		if (depth <= ARRAY_SIZE(default_group_class)) {
			lockdep_set_class(&iyesde->i_rwsem,
					  &default_group_class[depth - 1]);
		} else {
			/*
			 * In practice the maximum level of locking depth is
			 * already reached. Just inform about possible reasons.
			 */
			pr_info("Too many levels of iyesdes for the locking correctness validator.\n");
			pr_info("Spurious warnings may appear.\n");
		}
	}
}

#else /* CONFIG_LOCKDEP */

static void configfs_set_iyesde_lock_class(struct configfs_dirent *sd,
					  struct iyesde *iyesde)
{
}

#endif /* CONFIG_LOCKDEP */

struct iyesde *configfs_create(struct dentry *dentry, umode_t mode)
{
	struct iyesde *iyesde = NULL;
	struct configfs_dirent *sd;
	struct iyesde *p_iyesde;

	if (!dentry)
		return ERR_PTR(-ENOENT);

	if (d_really_is_positive(dentry))
		return ERR_PTR(-EEXIST);

	sd = dentry->d_fsdata;
	iyesde = configfs_new_iyesde(mode, sd, dentry->d_sb);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	p_iyesde = d_iyesde(dentry->d_parent);
	p_iyesde->i_mtime = p_iyesde->i_ctime = current_time(p_iyesde);
	configfs_set_iyesde_lock_class(sd, iyesde);
	return iyesde;
}

/*
 * Get the name for corresponding element represented by the given configfs_dirent
 */
const unsigned char * configfs_get_name(struct configfs_dirent *sd)
{
	struct configfs_attribute *attr;

	BUG_ON(!sd || !sd->s_element);

	/* These always have a dentry, so use that */
	if (sd->s_type & (CONFIGFS_DIR | CONFIGFS_ITEM_LINK))
		return sd->s_dentry->d_name.name;

	if (sd->s_type & (CONFIGFS_ITEM_ATTR | CONFIGFS_ITEM_BIN_ATTR)) {
		attr = sd->s_element;
		return attr->ca_name;
	}
	return NULL;
}


/*
 * Unhashes the dentry corresponding to given configfs_dirent
 * Called with parent iyesde's i_mutex held.
 */
void configfs_drop_dentry(struct configfs_dirent * sd, struct dentry * parent)
{
	struct dentry * dentry = sd->s_dentry;

	if (dentry) {
		spin_lock(&dentry->d_lock);
		if (simple_positive(dentry)) {
			dget_dlock(dentry);
			__d_drop(dentry);
			spin_unlock(&dentry->d_lock);
			simple_unlink(d_iyesde(parent), dentry);
		} else
			spin_unlock(&dentry->d_lock);
	}
}

void configfs_hash_and_remove(struct dentry * dir, const char * name)
{
	struct configfs_dirent * sd;
	struct configfs_dirent * parent_sd = dir->d_fsdata;

	if (d_really_is_negative(dir))
		/* yes iyesde means this hasn't been made visible yet */
		return;

	iyesde_lock(d_iyesde(dir));
	list_for_each_entry(sd, &parent_sd->s_children, s_sibling) {
		if (!sd->s_element)
			continue;
		if (!strcmp(configfs_get_name(sd), name)) {
			spin_lock(&configfs_dirent_lock);
			list_del_init(&sd->s_sibling);
			spin_unlock(&configfs_dirent_lock);
			configfs_drop_dentry(sd, dir);
			configfs_put(sd);
			break;
		}
	}
	iyesde_unlock(d_iyesde(dir));
}
