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
#include <linux/uaccess.h>

/*
 * "IKCFG_ST" and "IKCFG_ED" are used to extract the config data from
 * a binary kernel image or a module. See scripts/extract-ikconfig.
 */
asm (
"	.pushsection .rodata, \"a\"		\n"
"	.ascii \"IKCFG_ST\"			\n"
"	.global kernel_config_data		\n"
"kernel_config_data:				\n"
"	.incbin \"kernel/config_data.gz\"	\n"
"	.global kernel_config_data_end		\n"
"kernel_config_data_end:			\n"
"	.ascii \"IKCFG_ED\"			\n"
"	.popsection				\n"
);

#ifdef CONFIG_IKCONFIG_PROC

extern char kernel_config_data;
extern char kernel_config_data_end;

static ssize_t
ikconfig_read_current(struct file *file, char __user *buf,
		      size_t len, loff_t * offset)
{
	return simple_read_from_buffer(buf, len, offset,
				       &kernel_config_data,
				       &kernel_config_data_end -
				       &kernel_config_data);
}

static const struct file_operations ikconfig_file_ops = {
	.owner = THIS_MODULE,
	.read = ikconfig_read_current,
	.llseek = default_llseek,
};

static int __init ikconfig_init(void)
{
	struct proc_dir_entry *entry;

	/* create the current config file */
	entry = proc_create("config.gz", S_IFREG | S_IRUGO, NULL,
			    &ikconfig_file_ops);
	if (!entry)
		return -ENOMEM;

	proc_set_size(entry, &kernel_config_data_end - &kernel_config_data);

	return 0;
}

static void __exit ikconfig_cleanup(void)
{
	remove_proc_entry("config.gz", NULL);
}

module_init(ikconfig_init);
module_exit(ikconfig_cleanup);

#endif /* CONFIG_IKCONFIG_PROC */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Randy Dunlap");
MODULE_DESCRIPTION("Echo the kernel .config file used to build the kernel");
