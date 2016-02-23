#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cpu.h>

#include "notifier-error-inject.h"

static int priority;
module_param(priority, int, 0);
MODULE_PARM_DESC(priority, "specify cpu notifier priority");

static struct notifier_err_inject cpu_notifier_err_inject = {
	.actions = {
		{ NOTIFIER_ERR_INJECT_ACTION(CPU_UP_PREPARE) },
		{ NOTIFIER_ERR_INJECT_ACTION(CPU_UP_PREPARE_FROZEN) },
		{ NOTIFIER_ERR_INJECT_ACTION(CPU_DOWN_PREPARE) },
		{ NOTIFIER_ERR_INJECT_ACTION(CPU_DOWN_PREPARE_FROZEN) },
		{}
	}
};

static struct dentry *dir;

static int err_inject_init(void)
{
	int err;

	dir = notifier_err_inject_init("cpu", notifier_err_inject_dir,
					&cpu_notifier_err_inject, priority);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	err = register_hotcpu_notifier(&cpu_notifier_err_inject.nb);
	if (err)
		debugfs_remove_recursive(dir);

	return err;
}

static void err_inject_exit(void)
{
	unregister_hotcpu_notifier(&cpu_notifier_err_inject.nb);
	debugfs_remove_recursive(dir);
}

module_init(err_inject_init);
module_exit(err_inject_exit);

MODULE_DESCRIPTION("CPU notifier error injection module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
