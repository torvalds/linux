#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>

#include "notifier-error-inject.h"

static int priority;
module_param(priority, int, 0);
MODULE_PARM_DESC(priority, "specify netdevice notifier priority");

static struct notifier_err_inject netdev_notifier_err_inject = {
	.actions = {
		{ NOTIFIER_ERR_INJECT_ACTION(NETDEV_REGISTER) },
		{ NOTIFIER_ERR_INJECT_ACTION(NETDEV_CHANGEMTU) },
		{ NOTIFIER_ERR_INJECT_ACTION(NETDEV_CHANGENAME) },
		{ NOTIFIER_ERR_INJECT_ACTION(NETDEV_PRE_UP) },
		{ NOTIFIER_ERR_INJECT_ACTION(NETDEV_PRE_TYPE_CHANGE) },
		{ NOTIFIER_ERR_INJECT_ACTION(NETDEV_POST_INIT) },
		{ NOTIFIER_ERR_INJECT_ACTION(NETDEV_PRECHANGEMTU) },
		{ NOTIFIER_ERR_INJECT_ACTION(NETDEV_PRECHANGEUPPER) },
		{ NOTIFIER_ERR_INJECT_ACTION(NETDEV_CHANGEUPPER) },
		{}
	}
};

static struct dentry *dir;

static int netdev_err_inject_init(void)
{
	int err;

	dir = notifier_err_inject_init("netdev", notifier_err_inject_dir,
				       &netdev_notifier_err_inject, priority);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	err = register_netdevice_notifier(&netdev_notifier_err_inject.nb);
	if (err)
		debugfs_remove_recursive(dir);

	return err;
}

static void netdev_err_inject_exit(void)
{
	unregister_netdevice_notifier(&netdev_notifier_err_inject.nb);
	debugfs_remove_recursive(dir);
}

module_init(netdev_err_inject_init);
module_exit(netdev_err_inject_exit);

MODULE_DESCRIPTION("Netdevice notifier error injection module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nikolay Aleksandrov <razor@blackwall.org>");
