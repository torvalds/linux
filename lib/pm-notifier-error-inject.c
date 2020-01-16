// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/suspend.h>

#include "yestifier-error-inject.h"

static int priority;
module_param(priority, int, 0);
MODULE_PARM_DESC(priority, "specify PM yestifier priority");

static struct yestifier_err_inject pm_yestifier_err_inject = {
	.actions = {
		{ NOTIFIER_ERR_INJECT_ACTION(PM_HIBERNATION_PREPARE) },
		{ NOTIFIER_ERR_INJECT_ACTION(PM_SUSPEND_PREPARE) },
		{ NOTIFIER_ERR_INJECT_ACTION(PM_RESTORE_PREPARE) },
		{}
	}
};

static struct dentry *dir;

static int err_inject_init(void)
{
	int err;

	dir = yestifier_err_inject_init("pm", yestifier_err_inject_dir,
					&pm_yestifier_err_inject, priority);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	err = register_pm_yestifier(&pm_yestifier_err_inject.nb);
	if (err)
		debugfs_remove_recursive(dir);

	return err;
}

static void err_inject_exit(void)
{
	unregister_pm_yestifier(&pm_yestifier_err_inject.nb);
	debugfs_remove_recursive(dir);
}

module_init(err_inject_init);
module_exit(err_inject_exit);

MODULE_DESCRIPTION("PM yestifier error injection module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Akiyesbu Mita <akiyesbu.mita@gmail.com>");
