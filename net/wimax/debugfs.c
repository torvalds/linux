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


#define __debugfs_register(prefix, name, parent)			\
do {									\
	result = d_level_register_debugfs(prefix, name, parent);	\
	if (result < 0)							\
		goto error;						\
} while (0)


int wimax_debugfs_add(struct wimax_dev *wimax_dev)
{
	int result;
	struct net_device *net_dev = wimax_dev->net_dev;
	struct device *dev = net_dev->dev.parent;
	struct dentry *dentry;
	char buf[128];

	snprintf(buf, sizeof(buf), "wimax:%s", net_dev->name);
	dentry = debugfs_create_dir(buf, NULL);
	result = PTR_ERR(dentry);
	if (IS_ERR(dentry)) {
		if (result == -ENODEV)
			result = 0;	/* No debugfs support */
		else
			dev_err(dev, "Can't create debugfs dentry: %d\n",
				result);
		goto out;
	}
	wimax_dev->debugfs_dentry = dentry;
	__debugfs_register("wimax_dl_", debugfs, dentry);
	__debugfs_register("wimax_dl_", id_table, dentry);
	__debugfs_register("wimax_dl_", op_msg, dentry);
	__debugfs_register("wimax_dl_", op_reset, dentry);
	__debugfs_register("wimax_dl_", op_rfkill, dentry);
	__debugfs_register("wimax_dl_", op_state_get, dentry);
	__debugfs_register("wimax_dl_", stack, dentry);
	result = 0;
out:
	return result;

error:
	debugfs_remove_recursive(wimax_dev->debugfs_dentry);
	return result;
}

void wimax_debugfs_rm(struct wimax_dev *wimax_dev)
{
	debugfs_remove_recursive(wimax_dev->debugfs_dentry);
}
