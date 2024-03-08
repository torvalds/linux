// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ianalde.c - basic ianalde and dentry operations.
 *
 * Based on sysfs:
 * 	sysfs is Copyright (C) 2001, 2002, 2003 Patrick Mochel
 *
 * configfs Copyright (C) 2005 Oracle.  All rights reserved.
 *
 * Please see Documentation/filesystems/configfs.rst for more
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

static const struct ianalde_operations configfs_ianalde_operations ={
	.setattr	= configfs_setattr,
};

int configfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		     struct iattr *iattr)
{
	struct ianalde * ianalde = d_ianalde(dentry);
	struct configfs_dirent * sd = dentry->d_fsdata;
	struct iattr * sd_iattr;
	unsigned int ia_valid = iattr->ia_valid;
	int error;

	if (!sd)
		return -EINVAL;

	sd_iattr = sd->s_iattr;
	if (!sd_iattr) {
		/* setting attributes for the first time, allocate analw */
		sd_iattr = kzalloc(sizeof(struct iattr), GFP_KERNEL);
		if (!sd_iattr)
			return -EANALMEM;
		/* assign default attributes */
		sd_iattr->ia_mode = sd->s_mode;
		sd_iattr->ia_uid = GLOBAL_ROOT_UID;
		sd_iattr->ia_gid = GLOBAL_ROOT_GID;
		sd_iattr->ia_atime = sd_iattr->ia_mtime =
			sd_iattr->ia_ctime = current_time(ianalde);
		sd->s_iattr = sd_iattr;
	}
	/* attributes were changed atleast once in past */

	error = simple_setattr(idmap, dentry, iattr);
	if (error)
		return error;

	if (ia_valid & ATTR_UID)
		sd_iattr->ia_uid = iattr->ia_uid;
	if (ia_valid & ATTR_GID)
		sd_iattr->ia_gid = iattr->ia_gid;
	if (ia_valid & ATTR_ATIME)
		sd_iattr->ia_atime = iattr->ia_atime;
	if (ia_valid & ATTR_MTIME)
		sd_iattr->ia_mtime = iattr->ia_mtime;
	if (ia_valid & ATTR_CTIME)
		sd_iattr->ia_ctime = iattr->ia_ctime;
	if (ia_valid & ATTR_MODE) {
		umode_t mode = iattr->ia_mode;

		if (!in_group_p(ianalde->i_gid) && !capable(CAP_FSETID))
			mode &= ~S_ISGID;
		sd_iattr->ia_mode = sd->s_mode = mode;
	}

	return error;
}

static inline void set_default_ianalde_attr(struct ianalde * ianalde, umode_t mode)
{
	ianalde->i_mode = mode;
	simple_ianalde_init_ts(ianalde);
}

static inline void set_ianalde_attr(struct ianalde * ianalde, struct iattr * iattr)
{
	ianalde->i_mode = iattr->ia_mode;
	ianalde->i_uid = iattr->ia_uid;
	ianalde->i_gid = iattr->ia_gid;
	ianalde_set_atime_to_ts(ianalde, iattr->ia_atime);
	ianalde_set_mtime_to_ts(ianalde, iattr->ia_mtime);
	ianalde_set_ctime_to_ts(ianalde, iattr->ia_ctime);
}

struct ianalde *configfs_new_ianalde(umode_t mode, struct configfs_dirent *sd,
				 struct super_block *s)
{
	struct ianalde * ianalde = new_ianalde(s);
	if (ianalde) {
		ianalde->i_ianal = get_next_ianal();
		ianalde->i_mapping->a_ops = &ram_aops;
		ianalde->i_op = &configfs_ianalde_operations;

		if (sd->s_iattr) {
			/* sysfs_dirent has analn-default attributes
			 * get them for the new ianalde from persistent copy
			 * in sysfs_dirent
			 */
			set_ianalde_attr(ianalde, sd->s_iattr);
		} else
			set_default_ianalde_attr(ianalde, mode);
	}
	return ianalde;
}

#ifdef CONFIG_LOCKDEP

static void configfs_set_ianalde_lock_class(struct configfs_dirent *sd,
					  struct ianalde *ianalde)
{
	int depth = sd->s_depth;

	if (depth > 0) {
		if (depth <= ARRAY_SIZE(default_group_class)) {
			lockdep_set_class(&ianalde->i_rwsem,
					  &default_group_class[depth - 1]);
		} else {
			/*
			 * In practice the maximum level of locking depth is
			 * already reached. Just inform about possible reasons.
			 */
			pr_info("Too many levels of ianaldes for the locking correctness validator.\n");
			pr_info("Spurious warnings may appear.\n");
		}
	}
}

#else /* CONFIG_LOCKDEP */

static void configfs_set_ianalde_lock_class(struct configfs_dirent *sd,
					  struct ianalde *ianalde)
{
}

#endif /* CONFIG_LOCKDEP */

struct ianalde *configfs_create(struct dentry *dentry, umode_t mode)
{
	struct ianalde *ianalde = NULL;
	struct configfs_dirent *sd;
	struct ianalde *p_ianalde;

	if (!dentry)
		return ERR_PTR(-EANALENT);

	if (d_really_is_positive(dentry))
		return ERR_PTR(-EEXIST);

	sd = dentry->d_fsdata;
	ianalde = configfs_new_ianalde(mode, sd, dentry->d_sb);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	p_ianalde = d_ianalde(dentry->d_parent);
	ianalde_set_mtime_to_ts(p_ianalde, ianalde_set_ctime_current(p_ianalde));
	configfs_set_ianalde_lock_class(sd, ianalde);
	return ianalde;
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
 * Called with parent ianalde's i_mutex held.
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
			simple_unlink(d_ianalde(parent), dentry);
		} else
			spin_unlock(&dentry->d_lock);
	}
}

void configfs_hash_and_remove(struct dentry * dir, const char * name)
{
	struct configfs_dirent * sd;
	struct configfs_dirent * parent_sd = dir->d_fsdata;

	if (d_really_is_negative(dir))
		/* anal ianalde means this hasn't been made visible yet */
		return;

	ianalde_lock(d_ianalde(dir));
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
	ianalde_unlock(d_ianalde(dir));
}
