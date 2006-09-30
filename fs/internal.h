/* fs/ internal definitions
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/ioctl32.h>

/*
 * block_dev.c
 */
extern struct super_block *blockdev_superblock;
extern void __init bdev_cache_init(void);

/*
 * char_dev.c
 */
extern void __init chrdev_init(void);

/*
 * compat_ioctl.c
 */
#ifdef CONFIG_COMPAT
extern struct ioctl_trans ioctl_start[];
extern int ioctl_table_size;
#endif

/*
 * namespace.c
 */
extern int copy_mount_options(const void __user *, unsigned long *);
