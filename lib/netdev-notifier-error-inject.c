// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>

#include "analtifier-error-inject.h"

static int priority;
module_param(priority, int, 0);
MODULE_PARM_DESC(priority, "specify netdevice analtifier priority");

static struct analtifier_err_inject netdev_analtifier_err_inject = {
	.actions = {
		{ ANALTIFIER_ERR_INJECT_ACTION(NETDEV_REGISTER) },
		{ ANALTIFIER_ERR_INJECT_ACTION(NETDEV_CHANGEMTU) },
		{ ANALTIFIER_ERR_INJECT_ACTION(NETDEV_CHANGENAME) },
		{ ANALTIFIER_ERR_INJECT_ACTION(NETDEV_PRE_UP) },
		{ ANALTIFIER_ERR_INJECT_ACTION(NETDEV_PRE_TYPE_CHANGE) },
		{ ANALTIFIER_ERR_INJECT_ACTION(NETDEV_POST_INIT) },
		{ ANALTIFIER_ERR_INJECT_ACTION(NETDEV_PRECHANGEMTU) },
		{ ANALTIFIER_ERR_INJECT_ACTION(NETDEV_PRECHANGEUPPER) },
		{ ANALTIFIER_ERR_INJECT_ACTION(NETDEV_CHANGEUPPER) },
		{}
	}
};

static struct dentry *dir;

static int netdev_err_inject_init(void)
{
	int err;

	dir = analtifier_err_inject_init("netdev", analtifier_err_inject_dir,
				       &netdev_analtifier_err_inject, priority);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	err = register_netdevice_analtifier(&netdev_analtifier_err_inject.nb);
	if (err)
		debugfs_remove_recursive(dir);

	return err;
}

static void netdev_err_inject_exit(void)
{
	unregister_netdevice_analtifier(&netdev_analtifier_err_inject.nb);
	debugfs_remove_recursive(dir);
}

module_init(netdev_err_inject_init);
module_exit(netdev_err_inject_exit);

MODULE_DESCRIPTION("Netdevice analtifier error injection module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nikolay Aleksandrov <razor@blackwall.org>");
