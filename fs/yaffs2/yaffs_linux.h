/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */

#ifndef __YAFFS_LINUX_H__
#define __YAFFS_LINUX_H__

#include "yportenv.h"

struct yaffs_linux_context {
	struct list_head context_list;	/* List of these we have mounted */
	struct yaffs_dev *dev;
	struct super_block *super;
	struct task_struct *bg_thread;	/* Background thread for this device */
	int bg_running;
	struct mutex gross_lock;	/* Gross locking mutex*/
	u8 *spare_buffer;	/* For mtdif2 use. Don't know the size of the buffer
				 * at compile time so we have to allocate it.
				 */
	struct list_head search_contexts;
	void (*put_super_fn) (struct super_block * sb);

	struct task_struct *readdir_process;
	unsigned mount_id;
};

#define yaffs_dev_to_lc(dev) ((struct yaffs_linux_context *)((dev)->os_context))
#define yaffs_dev_to_mtd(dev) ((struct mtd_info *)((dev)->driver_context))

#endif
