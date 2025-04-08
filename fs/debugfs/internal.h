/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  internal.h - declarations internal to debugfs
 *
 *  Copyright (C) 2016 Nicolai Stange <nicstange@gmail.com>
 */

#ifndef _DEBUGFS_INTERNAL_H_
#define _DEBUGFS_INTERNAL_H_
#include <linux/list.h>

struct file_operations;

struct debugfs_inode_info {
	struct inode vfs_inode;
	union {
		const void *raw;
		const struct file_operations *real_fops;
		const struct debugfs_short_fops *short_fops;
		debugfs_automount_t automount;
	};
	const void *aux;
};

static inline struct debugfs_inode_info *DEBUGFS_I(struct inode *inode)
{
	return container_of(inode, struct debugfs_inode_info, vfs_inode);
}

/* declared over in file.c */
extern const struct file_operations debugfs_noop_file_operations;
extern const struct file_operations debugfs_open_proxy_file_operations;
extern const struct file_operations debugfs_full_proxy_file_operations;
extern const struct file_operations debugfs_full_short_proxy_file_operations;

struct debugfs_fsdata {
	const struct file_operations *real_fops;
	const struct debugfs_short_fops *short_fops;
	struct {
		refcount_t active_users;
		struct completion active_users_drained;

		/* protect cancellations */
		struct mutex cancellations_mtx;
		struct list_head cancellations;
		unsigned int methods;
	};
};

enum {
	HAS_READ = 1,
	HAS_WRITE = 2,
	HAS_LSEEK = 4,
	HAS_POLL = 8,
	HAS_IOCTL = 16
};

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
