/* no-block.c: implementation of routines required for non-BLOCK configuration
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/fs.h>

static int no_blkdev_open(struct inode * inode, struct file * filp)
{
	return -ENODEV;
}

const struct file_operations def_blk_fops = {
	.open		= no_blkdev_open,
};
