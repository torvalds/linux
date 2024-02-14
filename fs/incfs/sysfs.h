/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google LLC
 */
#ifndef _INCFS_SYSFS_H
#define _INCFS_SYSFS_H

struct incfs_sysfs_node {
	struct kobject isn_sysfs_node;

	struct completion isn_completion;

	struct mount_info *isn_mi;
};

int incfs_init_sysfs(void);
void incfs_cleanup_sysfs(void);
struct incfs_sysfs_node *incfs_add_sysfs_node(const char *name,
					      struct mount_info *mi);
void incfs_free_sysfs_node(struct incfs_sysfs_node *node);

#endif
