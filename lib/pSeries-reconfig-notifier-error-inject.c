#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/pSeries_reconfig.h>

#include "notifier-error-inject.h"

static int priority;
module_param(priority, int, 0);
MODULE_PARM_DESC(priority, "specify pSeries reconfig notifier priority");

static struct notifier_err_inject reconfig_err_inject = {
	.actions = {
		{ NOTIFIER_ERR_INJECT_ACTION(PSERIES_RECONFIG_ADD) },
		{ NOTIFIER_ERR_INJECT_ACTION(PSERIES_RECONFIG_REMOVE) },
		{ NOTIFIER_ERR_INJECT_ACTION(PSERIES_DRCONF_MEM_ADD) },
		{ NOTIFIER_ERR_INJECT_ACTION(PSERIES_DRCONF_MEM_REMOVE) },
		{}
	}
};

static struct dentry *dir;

static int err_inject_init(void)
{
	int err;

	dir = notifier_err_inject_init("pSeries-reconfig",
		notifier_err_inject_dir, &reconfig_err_inject, priority);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	err = pSeries_reconfig_notifier_register(&reconfig_err_inject.nb);
	if (err)
		debugfs_remove_recursive(dir);

	return err;
}

static void err_inject_exit(void)
{
	pSeries_reconfig_notifier_unregister(&reconfig_err_inject.nb);
	debugfs_remove_recursive(dir);
}

module_init(err_inject_init);
module_exit(err_inject_exit);

MODULE_DESCRIPTION("pSeries reconfig notifier error injection module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
