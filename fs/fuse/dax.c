// SPDX-License-Identifier: GPL-2.0
/*
 * dax: direct host memory access
 * Copyright (C) 2020 Red Hat, Inc.
 */

#include "fuse_i.h"

#include <linux/dax.h>

struct fuse_conn_dax {
	/* DAX device */
	struct dax_device *dev;
};

void fuse_dax_conn_free(struct fuse_conn *fc)
{
	kfree(fc->dax);
}

int fuse_dax_conn_alloc(struct fuse_conn *fc, struct dax_device *dax_dev)
{
	struct fuse_conn_dax *fcd;

	if (!dax_dev)
		return 0;

	fcd = kzalloc(sizeof(*fcd), GFP_KERNEL);
	if (!fcd)
		return -ENOMEM;

	fcd->dev = dax_dev;

	fc->dax = fcd;
	return 0;
}
