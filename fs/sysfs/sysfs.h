/*
 * fs/sysfs/sysfs.h - sysfs internal header file
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 */

#ifndef __SYSFS_INTERNAL_H
#define __SYSFS_INTERNAL_H

#include "../kernfs/kernfs-internal.h"
#include <linux/sysfs.h>

/*
 * mount.c
 */
extern struct sysfs_dirent *sysfs_root_sd;

/*
 * dir.c
 */
extern spinlock_t sysfs_symlink_target_lock;

void sysfs_warn_dup(struct sysfs_dirent *parent, const char *name);

/*
 * file.c
 */
int sysfs_add_file(struct sysfs_dirent *dir_sd,
		   const struct attribute *attr, bool is_bin);
int sysfs_add_file_mode_ns(struct sysfs_dirent *dir_sd,
			   const struct attribute *attr, bool is_bin,
			   umode_t amode, const void *ns);

/*
 * symlink.c
 */
int sysfs_create_link_sd(struct sysfs_dirent *sd, struct kobject *target,
			 const char *name);

#endif	/* __SYSFS_INTERNAL_H */
