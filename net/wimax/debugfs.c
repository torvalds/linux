// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux WiMAX
 * Debugfs support
 *
 * Copyright (C) 2005-2006 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 */
#include <linux/debugfs.h>
#include <linux/wimax.h>
#include "wimax-internal.h"

#define D_SUBMODULE debugfs
#include "debug-levels.h"

void wimax_debugfs_add(struct wimax_dev *wimax_dev)
{
	struct net_device *net_dev = wimax_dev->net_dev;
	struct dentry *dentry;
	char buf[128];

	snprintf(buf, sizeof(buf), "wimax:%s", net_dev->name);
	dentry = debugfs_create_dir(buf, NULL);
	wimax_dev->debugfs_dentry = dentry;

	d_level_register_debugfs("wimax_dl_", debugfs, dentry);
	d_level_register_debugfs("wimax_dl_", id_table, dentry);
	d_level_register_debugfs("wimax_dl_", op_msg, dentry);
	d_level_register_debugfs("wimax_dl_", op_reset, dentry);
	d_level_register_debugfs("wimax_dl_", op_rfkill, dentry);
	d_level_register_debugfs("wimax_dl_", op_state_get, dentry);
	d_level_register_debugfs("wimax_dl_", stack, dentry);
}

void wimax_debugfs_rm(struct wimax_dev *wimax_dev)
{
	debugfs_remove_recursive(wimax_dev->debugfs_dentry);
}
