#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/memory.h>

#include "notifier-error-inject.h"

static int priority;
module_param(priority, int, 0);
MODULE_PARM_DESC(priority, "specify memory notifier priority");

static struct notifier_err_inject memory_notifier_err_inject = {
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

	dir = notifier_err_inject_init("memory", notifier_err_inject_dir,
					&memory_notifier_err_inject, priority);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	err = register_memory_notifier(&memory_notifier_err_inject.nb);
	if (err)
		debugfs_remove_recursive(dir);

	return err;
}

static void err_inject_exit(void)
{
	unregister_memory_notifier(&memory_notifier_err_inject.nb);
	debugfs_remove_recursive(dir);
}

module_init(err_inject_init);
module_exit(err_inject_exit);

MODULE_DESCRIPTION("memory notifier error injection module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
