/*
 * kernel/configs.c
 * Echo the kernel .config file used to build the kernel
 *
 * Copyright (C) 2002 Khalid Aziz <khalid_aziz@hp.com>
 * Copyright (C) 2002 Randy Dunlap <rdunlap@xenotime.net>
 * Copyright (C) 2002 Al Stone <ahs3@fc.hp.com>
 * Copyright (C) 2002 Hewlett-Packard Company
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <asm/uaccess.h>

/**************************************************/
/* the actual current config file                 */

/*
 * Define kernel_config_data and kernel_config_data_size, which contains the
 * wrapped and compressed configuration file.  The file is first compressed
 * with gzip and then bounded by two eight byte magic numbers to allow
 * extraction from a binary kernel image:
 *
 *   IKCFG_ST
 *   <image>
 *   IKCFG_ED
 */
#define MAGIC_START	"IKCFG_ST"
#define MAGIC_END	"IKCFG_ED"
#include "config_data.h"


#define MAGIC_SIZE (sizeof(MAGIC_START) - 1)
#define kernel_config_data_size \
	(sizeof(kernel_config_data) - 1 - MAGIC_SIZE * 2)

#ifdef CONFIG_IKCONFIG_PROC

/**************************************************/
/* globals and useful constants                   */

static ssize_t
ikconfig_read_current(struct file *file, char __user *buf,
		      size_t len, loff_t * offset)
{
	return simple_read_from_buffer(buf, len, offset,
				       kernel_config_data + MAGIC_SIZE,
				       kernel_config_data_size);
}

static const struct file_operations ikconfig_file_ops = {
	.owner = THIS_MODULE,
	.read = ikconfig_read_current,
};

/***************************************************/
/* ikconfig_init: start up everything we need to */

static int __init ikconfig_init(void)
{
	struct proc_dir_entry *entry;

	/* create the current config file */
	entry = create_proc_entry("config.gz", S_IFREG | S_IRUGO, NULL);
	if (!entry)
		return -ENOMEM;

	entry->proc_fops = &ikconfig_file_ops;
	entry->size = kernel_config_data_size;

	return 0;
}

/***************************************************/
/* ikconfig_cleanup: clean up our mess           */

static void __exit ikconfig_cleanup(void)
{
	remove_proc_entry("config.gz", NULL);
}

module_init(ikconfig_init);
module_exit(ikconfig_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Randy Dunlap");
MODULE_DESCRIPTION("Echo the kernel .config file used to build the kernel");

#endif /* CONFIG_IKCONFIG_PROC */
