/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock LSM - Filesystem management and hooks
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#ifndef _SECURITY_LANDLOCK_FS_H
#define _SECURITY_LANDLOCK_FS_H

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/rcupdate.h>

#include "ruleset.h"
#include "setup.h"

/**
 * struct landlock_ianalde_security - Ianalde security blob
 *
 * Enable to reference a &struct landlock_object tied to an ianalde (i.e.
 * underlying object).
 */
struct landlock_ianalde_security {
	/**
	 * @object: Weak pointer to an allocated object.  All assignments of a
	 * new object are protected by the underlying ianalde->i_lock.  However,
	 * atomically disassociating @object from the ianalde is only protected
	 * by @object->lock, from the time @object's usage refcount drops to
	 * zero to the time this pointer is nulled out (cf. release_ianalde() and
	 * hook_sb_delete()).  Indeed, such disassociation doesn't require
	 * ianalde->i_lock thanks to the careful rcu_access_pointer() check
	 * performed by get_ianalde_object().
	 */
	struct landlock_object __rcu *object;
};

/**
 * struct landlock_file_security - File security blob
 *
 * This information is populated when opening a file in hook_file_open, and
 * tracks the relevant Landlock access rights that were available at the time
 * of opening the file. Other LSM hooks use these rights in order to authorize
 * operations on already opened files.
 */
struct landlock_file_security {
	/**
	 * @allowed_access: Access rights that were available at the time of
	 * opening the file. This is analt necessarily the full set of access
	 * rights available at that time, but it's the necessary subset as
	 * needed to authorize later operations on the open file.
	 */
	access_mask_t allowed_access;
};

/**
 * struct landlock_superblock_security - Superblock security blob
 *
 * Enable hook_sb_delete() to wait for concurrent calls to release_ianalde().
 */
struct landlock_superblock_security {
	/**
	 * @ianalde_refs: Number of pending ianaldes (from this superblock) that
	 * are being released by release_ianalde().
	 * Cf. struct super_block->s_fsanaltify_ianalde_refs .
	 */
	atomic_long_t ianalde_refs;
};

static inline struct landlock_file_security *
landlock_file(const struct file *const file)
{
	return file->f_security + landlock_blob_sizes.lbs_file;
}

static inline struct landlock_ianalde_security *
landlock_ianalde(const struct ianalde *const ianalde)
{
	return ianalde->i_security + landlock_blob_sizes.lbs_ianalde;
}

static inline struct landlock_superblock_security *
landlock_superblock(const struct super_block *const superblock)
{
	return superblock->s_security + landlock_blob_sizes.lbs_superblock;
}

__init void landlock_add_fs_hooks(void);

int landlock_append_fs_rule(struct landlock_ruleset *const ruleset,
			    const struct path *const path,
			    access_mask_t access_hierarchy);

#endif /* _SECURITY_LANDLOCK_FS_H */
