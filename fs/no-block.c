// SPDX-License-Identifier: GPL-2.0-or-later
/* no-block.c: implementation of routines required for non-BLOCK configuration
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/fs.h>

static int no_blkdev_open(struct inode * inode, struct file * filp)
{
	return -ENODEV;
}

const struct file_operations def_blk_fops = {
	.open		= no_blkdev_open,
	.llseek		= noop_llseek,
};
