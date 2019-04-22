// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>

static int replace;
module_param(replace, int, 0644);
MODULE_PARM_DESC(replace, "replace (default=0)");

#include <linux/seq_file.h>
static int livepatch_meminfo_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s: %s\n", THIS_MODULE->name,
		   "this has been live patched");
	return 0;
}

static struct klp_func funcs[] = {
	{
		.old_name = "meminfo_proc_show",
		.new_func = livepatch_meminfo_proc_show,
	}, {}
};

static struct klp_object objs[] = {
	{
		/* name being NULL means vmlinux */
		.funcs = funcs,
	}, {}
};

static struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
	/* set .replace in the init function below for demo purposes */
};

static int test_klp_atomic_replace_init(void)
{
	patch.replace = replace;
	return klp_enable_patch(&patch);
}

static void test_klp_atomic_replace_exit(void)
{
}

module_init(test_klp_atomic_replace_init);
module_exit(test_klp_atomic_replace_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
MODULE_AUTHOR("Joe Lawrence <joe.lawrence@redhat.com>");
MODULE_DESCRIPTION("Livepatch test: atomic replace");
