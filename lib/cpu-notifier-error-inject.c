#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/notifier.h>

static int priority;
static int cpu_up_prepare_error;
static int cpu_down_prepare_error;

module_param(priority, int, 0);
MODULE_PARM_DESC(priority, "specify cpu notifier priority");

module_param(cpu_up_prepare_error, int, 0644);
MODULE_PARM_DESC(cpu_up_prepare_error,
		"specify error code to inject CPU_UP_PREPARE action");

module_param(cpu_down_prepare_error, int, 0644);
MODULE_PARM_DESC(cpu_down_prepare_error,
		"specify error code to inject CPU_DOWN_PREPARE action");

static int err_inject_cpu_callback(struct notifier_block *nfb,
				unsigned long action, void *hcpu)
{
	int err = 0;

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		err = cpu_up_prepare_error;
		break;
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		err = cpu_down_prepare_error;
		break;
	}
	if (err)
		printk(KERN_INFO "Injecting error (%d) at cpu notifier\n", err);

	return notifier_from_errno(err);
}

static struct notifier_block err_inject_cpu_notifier = {
	.notifier_call = err_inject_cpu_callback,
};

static int err_inject_init(void)
{
	err_inject_cpu_notifier.priority = priority;

	return register_hotcpu_notifier(&err_inject_cpu_notifier);
}

static void err_inject_exit(void)
{
	unregister_hotcpu_notifier(&err_inject_cpu_notifier);
}

module_init(err_inject_init);
module_exit(err_inject_exit);

MODULE_DESCRIPTION("CPU notifier error injection module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
