/*
 *  skl-debug.c - Debugfs for skl driver
 *
 *  Copyright (C) 2016-17 Intel Corp
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 */

#include <linux/pci.h>
#include <linux/debugfs.h>
#include "skl.h"

struct skl_debug {
	struct skl *skl;
	struct device *dev;

	struct dentry *fs;
};

struct skl_debug *skl_debugfs_init(struct skl *skl)
{
	struct skl_debug *d;

	d = devm_kzalloc(&skl->pci->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return NULL;

	/* create the debugfs dir with platform component's debugfs as parent */
	d->fs = debugfs_create_dir("dsp",
				   skl->platform->component.debugfs_root);
	if (IS_ERR(d->fs) || !d->fs) {
		dev_err(&skl->pci->dev, "debugfs root creation failed\n");
		return NULL;
	}

	d->skl = skl;
	d->dev = &skl->pci->dev;

	return d;
}

void skl_debugfs_exit(struct skl_debug *d)
{
	debugfs_remove_recursive(d->fs);

	kfree(d);

}
