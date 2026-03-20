// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2026 Pablo Hugen <phugen@redhat.com>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *pde;

static noinline int test_klp_mod_target_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s: %s\n", THIS_MODULE->name, "original output");
	return 0;
}

static int test_klp_mod_target_init(void)
{
	pr_info("%s\n", __func__);
	pde = proc_create_single("test_klp_mod_target", 0, NULL,
				 test_klp_mod_target_show);
	if (!pde)
		return -ENOMEM;
	return 0;
}

static void test_klp_mod_target_exit(void)
{
	pr_info("%s\n", __func__);
	proc_remove(pde);
}

module_init(test_klp_mod_target_init);
module_exit(test_klp_mod_target_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Hugen <phugen@redhat.com>");
MODULE_DESCRIPTION("Livepatch test: target module with proc entry");
