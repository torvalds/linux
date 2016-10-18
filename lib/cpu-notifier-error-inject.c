#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cpu.h>

#include "notifier-error-inject.h"

static int priority;
module_param(priority, int, 0);
MODULE_PARM_DESC(priority, "specify cpu notifier priority");

#define UP_PREPARE 0
#define UP_PREPARE_FROZEN 0
#define DOWN_PREPARE 0
#define DOWN_PREPARE_FROZEN 0

static struct notifier_err_inject cpu_notifier_err_inject = {
	.actions = {
		{ NOTIFIER_ERR_INJECT_ACTION(UP_PREPARE) },
		{ NOTIFIER_ERR_INJECT_ACTION(UP_PREPARE_FROZEN) },
		{ NOTIFIER_ERR_INJECT_ACTION(DOWN_PREPARE) },
		{ NOTIFIER_ERR_INJECT_ACTION(DOWN_PREPARE_FROZEN) },
		{}
	}
};

static int notf_err_handle(struct notifier_err_inject_action *action)
{
	int ret;

	ret = action->error;
	if (ret)
		pr_info("Injecting error (%d) to %s\n", ret, action->name);
	return ret;
}

static int notf_err_inj_up_prepare(unsigned int cpu)
{
	if (!cpuhp_tasks_frozen)
		return notf_err_handle(&cpu_notifier_err_inject.actions[0]);
	else
		return notf_err_handle(&cpu_notifier_err_inject.actions[1]);
}

static int notf_err_inj_dead(unsigned int cpu)
{
	if (!cpuhp_tasks_frozen)
		return notf_err_handle(&cpu_notifier_err_inject.actions[2]);
	else
		return notf_err_handle(&cpu_notifier_err_inject.actions[3]);
}

static struct dentry *dir;

static int err_inject_init(void)
{
	int err;

	dir = notifier_err_inject_init("cpu", notifier_err_inject_dir,
					&cpu_notifier_err_inject, priority);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	err = cpuhp_setup_state_nocalls(CPUHP_NOTF_ERR_INJ_PREPARE,
					"cpu-err-notif:prepare",
					notf_err_inj_up_prepare,
					notf_err_inj_dead);
	if (err)
		debugfs_remove_recursive(dir);

	return err;
}

static void err_inject_exit(void)
{
	cpuhp_remove_state_nocalls(CPUHP_NOTF_ERR_INJ_PREPARE);
	debugfs_remove_recursive(dir);
}

module_init(err_inject_init);
module_exit(err_inject_exit);

MODULE_DESCRIPTION("CPU notifier error injection module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
