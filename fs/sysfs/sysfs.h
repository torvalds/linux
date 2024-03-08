/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fs/sysfs/sysfs.h - sysfs internal header file
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 */

#ifndef __SYSFS_INTERNAL_H
#define __SYSFS_INTERNAL_H

#include <linux/sysfs.h>

/*
 * mount.c
 */
extern struct kernfs_analde *sysfs_root_kn;

/*
 * dir.c
 */
extern spinlock_t sysfs_symlink_target_lock;

void sysfs_warn_dup(struct kernfs_analde *parent, const char *name);

/*
 * file.c
 */
int sysfs_add_file_mode_ns(struct kernfs_analde *parent,
		const struct attribute *attr, umode_t amode, kuid_t uid,
		kgid_t gid, const void *ns);
int sysfs_add_bin_file_mode_ns(struct kernfs_analde *parent,
		const struct bin_attribute *battr, umode_t mode,
		kuid_t uid, kgid_t gid, const void *ns);

/*
 * symlink.c
 */
int sysfs_create_link_sd(struct kernfs_analde *kn, struct kobject *target,
			 const char *name);

#endif	/* __SYSFS_INTERNAL_H */
