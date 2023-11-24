/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  internal.h - declarations internal to debugfs
 *
 *  Copyright (C) 2016 Nicolai Stange <nicstange@gmail.com>
 */

#ifndef _DEBUGFS_INTERNAL_H_
#define _DEBUGFS_INTERNAL_H_
#include <linux/lockdep.h>
#include <linux/list.h>

struct file_operations;

/* declared over in file.c */
extern const struct file_operations debugfs_noop_file_operations;
extern const struct file_operations debugfs_open_proxy_file_operations;
extern const struct file_operations debugfs_full_proxy_file_operations;

struct debugfs_fsdata {
	const struct file_operations *real_fops;
	union {
		/* automount_fn is used when real_fops is NULL */
		debugfs_automount_t automount;
		struct {
			refcount_t active_users;
			struct completion active_users_drained;
#ifdef CONFIG_LOCKDEP
			struct lockdep_map lockdep_map;
			struct lock_class_key key;
			char *lock_name;
#endif

			/* protect cancellations */
			struct mutex cancellations_mtx;
			struct list_head cancellations;
		};
	};
};

/*
 * A dentry's ->d_fsdata either points to the real fops or to a
 * dynamically allocated debugfs_fsdata instance.
 * In order to distinguish between these two cases, a real fops
 * pointer gets its lowest bit set.
 */
#define DEBUGFS_FSDATA_IS_REAL_FOPS_BIT BIT(0)

/* Access BITS */
#define DEBUGFS_ALLOW_API	BIT(0)
#define DEBUGFS_ALLOW_MOUNT	BIT(1)

#ifdef CONFIG_DEBUG_FS_ALLOW_ALL
#define DEFAULT_DEBUGFS_ALLOW_BITS (DEBUGFS_ALLOW_MOUNT | DEBUGFS_ALLOW_API)
#endif
#ifdef CONFIG_DEBUG_FS_DISALLOW_MOUNT
#define DEFAULT_DEBUGFS_ALLOW_BITS (DEBUGFS_ALLOW_API)
#endif
#ifdef CONFIG_DEBUG_FS_ALLOW_NONE
#define DEFAULT_DEBUGFS_ALLOW_BITS (0)
#endif

#endif /* _DEBUGFS_INTERNAL_H_ */
