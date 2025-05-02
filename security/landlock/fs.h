/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Filesystem management and hooks
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 * Copyright © 2024-2025 Microsoft Corporation
 */

#ifndef _SECURITY_LANDLOCK_FS_H
#define _SECURITY_LANDLOCK_FS_H

#include <linux/build_bug.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/rcupdate.h>

#include "access.h"
#include "cred.h"
#include "ruleset.h"
#include "setup.h"

/**
 * struct landlock_inode_security - Inode security blob
 *
 * Enable to reference a &struct landlock_object tied to an inode (i.e.
 * underlying object).
 */
struct landlock_inode_security {
	/**
	 * @object: Weak pointer to an allocated object.  All assignments of a
	 * new object are protected by the underlying inode->i_lock.  However,
	 * atomically disassociating @object from the inode is only protected
	 * by @object->lock, from the time @object's usage refcount drops to
	 * zero to the time this pointer is nulled out (cf. release_inode() and
	 * hook_sb_delete()).  Indeed, such disassociation doesn't require
	 * inode->i_lock thanks to the careful rcu_access_pointer() check
	 * performed by get_inode_object().
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
	 * opening the file. This is not necessarily the full set of access
	 * rights available at that time, but it's the necessary subset as
	 * needed to authorize later operations on the open file.
	 */
	access_mask_t allowed_access;

#ifdef CONFIG_AUDIT
	/**
	 * @deny_masks: Domain layer levels that deny an optional access (see
	 * _LANDLOCK_ACCESS_FS_OPTIONAL).
	 */
	deny_masks_t deny_masks;
	/**
	 * @fown_layer: Layer level of @fown_subject->domain with
	 * LANDLOCK_SCOPE_SIGNAL.
	 */
	u8 fown_layer;
#endif /* CONFIG_AUDIT */

	/**
	 * @fown_subject: Landlock credential of the task that set the PID that
	 * may receive a signal e.g., SIGURG when writing MSG_OOB to the
	 * related socket.  This pointer is protected by the related
	 * file->f_owner->lock, as for fown_struct's members: pid, uid, and
	 * euid.
	 */
	struct landlock_cred_security fown_subject;
};

#ifdef CONFIG_AUDIT

/* Makes sure all layers can be identified. */
/* clang-format off */
static_assert((typeof_member(struct landlock_file_security, fown_layer))~0 >=
	      LANDLOCK_MAX_NUM_LAYERS);
/* clang-format off */

#endif /* CONFIG_AUDIT */

/**
 * struct landlock_superblock_security - Superblock security blob
 *
 * Enable hook_sb_delete() to wait for concurrent calls to release_inode().
 */
struct landlock_superblock_security {
	/**
	 * @inode_refs: Number of pending inodes (from this superblock) that
	 * are being released by release_inode().
	 * Cf. struct super_block->s_fsnotify_inode_refs .
	 */
	atomic_long_t inode_refs;
};

static inline struct landlock_file_security *
landlock_file(const struct file *const file)
{
	return file->f_security + landlock_blob_sizes.lbs_file;
}

static inline struct landlock_inode_security *
landlock_inode(const struct inode *const inode)
{
	return inode->i_security + landlock_blob_sizes.lbs_inode;
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
