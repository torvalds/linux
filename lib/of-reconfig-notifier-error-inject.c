#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>

#include "notifier-error-inject.h"

static int priority;
module_param(priority, int, 0);
MODULE_PARM_DESC(priority, "specify OF reconfig notifier priority");

static struct notifier_err_inject reconfig_err_inject = {
	.actions = {
		{ NOTIFIER_ERR_INJECT_ACTION(OF_RECONFIG_ATTACH_NODE) },
		{ NOTIFIER_ERR_INJECT_ACTION(OF_RECONFIG_DETACH_NODE) },
		{ NOTIFIER_ERR_INJECT_ACTION(OF_RECONFIG_ADD_PROPERTY) },
		{ NOTIFIER_ERR_INJECT_ACTION(OF_RECONFIG_REMOVE_PROPERTY) },
		{ NOTIFIER_ERR_INJECT_ACTION(OF_RECONFIG_UPDATE_PROPERTY) },
		{}
	}
};

static struct dentry *dir;

static int err_inject_init(void)
{
	int err;

	dir = notifier_err_inject_init("OF-reconfig",
		notifier_err_inject_dir, &reconfig_err_inject, priority);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	err = of_reconfig_notifier_register(&reconfig_err_inject.nb);
	if (err)
		debugfs_remove_recursive(dir);

	return err;
}

static void err_inject_exit(void)
{
	of_reconfig_notifier_unregister(&reconfig_err_inject.nb);
	debugfs_remove_recursive(dir);
}

module_init(err_inject_init);
module_exit(err_inject_exit);

MODULE_DESCRIPTION("OF reconfig notifier error injection module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
