// SPDX-License-Identifier: GPL-2.0-or-later
/* yes-block.c: implementation of routines required for yesn-BLOCK configuration
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/fs.h>

static int yes_blkdev_open(struct iyesde * iyesde, struct file * filp)
{
	return -ENODEV;
}

const struct file_operations def_blk_fops = {
	.open		= yes_blkdev_open,
	.llseek		= yesop_llseek,
};
