/*
 * NET4:	Sysctl interface to net af_unix subsystem.
 *
 * Authors:	Mike Shaver.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>

#include <net/af_unix.h>

static ctl_table unix_table[] = {
	{
		.procname	= "max_dgram_qlen",
		.data		= &init_net.unx.sysctl_max_dgram_qlen,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{ }
};

static struct ctl_path unix_path[] = {
	{ .procname = "net", },
	{ .procname = "unix", },
	{ },
};

int __net_init unix_sysctl_register(struct net *net)
{
	struct ctl_table *table;

	table = kmemdup(unix_table, sizeof(unix_table), GFP_KERNEL);
	if (table == NULL)
		goto err_alloc;

	table[0].data = &net->unx.sysctl_max_dgram_qlen;
	net->unx.ctl = register_net_sysctl_table(net, unix_path, table);
	if (net->unx.ctl == NULL)
		goto err_reg;

	return 0;

err_reg:
	kfree(table);
err_alloc:
	return -ENOMEM;
}

void unix_sysctl_unregister(struct net *net)
{
	struct ctl_table *table;

	table = net->unx.ctl->ctl_table_arg;
	unregister_sysctl_table(net->unx.ctl);
	kfree(table);
}
