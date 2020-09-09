// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2019, Tessares SA.
 */

#include <linux/sysctl.h>

#include <net/net_namespace.h>
#include <net/netns/generic.h>

#include "protocol.h"

#define MPTCP_SYSCTL_PATH "net/mptcp"

static int mptcp_pernet_id;
struct mptcp_pernet {
	struct ctl_table_header *ctl_table_hdr;

	int mptcp_enabled;
};

static struct mptcp_pernet *mptcp_get_pernet(struct net *net)
{
	return net_generic(net, mptcp_pernet_id);
}

int mptcp_is_enabled(struct net *net)
{
	return mptcp_get_pernet(net)->mptcp_enabled;
}

static struct ctl_table mptcp_sysctl_table[] = {
	{
		.procname = "enabled",
		.maxlen = sizeof(int),
		.mode = 0644,
		/* users with CAP_NET_ADMIN or root (not and) can change this
		 * value, same as other sysctl or the 'net' tree.
		 */
		.proc_handler = proc_dointvec,
	},
	{}
};

static void mptcp_pernet_set_defaults(struct mptcp_pernet *pernet)
{
	pernet->mptcp_enabled = 1;
}

static int mptcp_pernet_new_table(struct net *net, struct mptcp_pernet *pernet)
{
	struct ctl_table_header *hdr;
	struct ctl_table *table;

	table = mptcp_sysctl_table;
	if (!net_eq(net, &init_net)) {
		table = kmemdup(table, sizeof(mptcp_sysctl_table), GFP_KERNEL);
		if (!table)
			goto err_alloc;
	}

	table[0].data = &pernet->mptcp_enabled;

	hdr = register_net_sysctl(net, MPTCP_SYSCTL_PATH, table);
	if (!hdr)
		goto err_reg;

	pernet->ctl_table_hdr = hdr;

	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static void mptcp_pernet_del_table(struct mptcp_pernet *pernet)
{
	struct ctl_table *table = pernet->ctl_table_hdr->ctl_table_arg;

	unregister_net_sysctl_table(pernet->ctl_table_hdr);

	kfree(table);
}

static int __net_init mptcp_net_init(struct net *net)
{
	struct mptcp_pernet *pernet = mptcp_get_pernet(net);

	mptcp_pernet_set_defaults(pernet);

	return mptcp_pernet_new_table(net, pernet);
}

/* Note: the callback will only be called per extra netns */
static void __net_exit mptcp_net_exit(struct net *net)
{
	struct mptcp_pernet *pernet = mptcp_get_pernet(net);

	mptcp_pernet_del_table(pernet);
}

static struct pernet_operations mptcp_pernet_ops = {
	.init = mptcp_net_init,
	.exit = mptcp_net_exit,
	.id = &mptcp_pernet_id,
	.size = sizeof(struct mptcp_pernet),
};

void __init mptcp_init(void)
{
	mptcp_join_cookie_init();
	mptcp_proto_init();

	if (register_pernet_subsys(&mptcp_pernet_ops) < 0)
		panic("Failed to register MPTCP pernet subsystem.\n");
}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
int __init mptcpv6_init(void)
{
	int err;

	err = mptcp_proto_v6_init();

	return err;
}
#endif
