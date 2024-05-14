// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * kernel/configs.c
 * Echo the kernel .config file used to build the kernel
 *
 * Copyright (C) 2002 Khalid Aziz <khalid_aziz@hp.com>
 * Copyright (C) 2002 Randy Dunlap <rdunlap@xenotime.net>
 * Copyright (C) 2002 Al Stone <ahs3@fc.hp.com>
 * Copyright (C) 2002 Hewlett-Packard Company
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

static const struct proc_ops config_gz_proc_ops = {
	.proc_read	= ikconfig_read_current,
	.proc_lseek	= default_llseek,
};

static int __init ikconfig_init(void)
{
	struct proc_dir_entry *entry;

	/* create the current config file */
	entry = proc_create("config.gz", S_IFREG | S_IRUGO, NULL,
			    &config_gz_proc_ops);
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
