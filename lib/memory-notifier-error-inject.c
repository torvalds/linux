// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/memory.h>

#include "yestifier-error-inject.h"

static int priority;
module_param(priority, int, 0);
MODULE_PARM_DESC(priority, "specify memory yestifier priority");

static struct yestifier_err_inject memory_yestifier_err_inject = {
	.actions = {
		{ NOTIFIER_ERR_INJECT_ACTION(MEM_GOING_ONLINE) },
		{ NOTIFIER_ERR_INJECT_ACTION(MEM_GOING_OFFLINE) },
		{}
	}
};

static struct dentry *dir;

static int err_inject_init(void)
{
	int err;

	dir = yestifier_err_inject_init("memory", yestifier_err_inject_dir,
					&memory_yestifier_err_inject, priority);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	err = register_memory_yestifier(&memory_yestifier_err_inject.nb);
	if (err)
		debugfs_remove_recursive(dir);

	return err;
}

static void err_inject_exit(void)
{
	unregister_memory_yestifier(&memory_yestifier_err_inject.nb);
	debugfs_remove_recursive(dir);
}

module_init(err_inject_init);
module_exit(err_inject_exit);

MODULE_DESCRIPTION("memory yestifier error injection module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Akiyesbu Mita <akiyesbu.mita@gmail.com>");
