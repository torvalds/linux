// SPDX-License-Identifier: GPL-2.0

#include "fuse_i.h"

#include <linux/fuse.h>

int fuse_passthrough_open(struct fuse_dev *fud,
			  struct fuse_passthrough_out *pto)
{
	return -EINVAL;
}

int fuse_passthrough_setup(struct fuse_conn *fc, struct fuse_file *ff,
			   struct fuse_open_out *openarg)
{
	return -EINVAL;
}

void fuse_passthrough_release(struct fuse_passthrough *passthrough)
{
}
