/* -*- linux-c -*-
 * sysctl_net.c: sysctl interface to net subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net directories for each protocol family. [MS]
 *
 * $Log: sysctl_net.c,v $
 * Revision 1.2  1996/05/08  20:24:40  shaver
 * Added bits for NET_BRIDGE and the NET_IPV4_ARP stuff and
 * NET_IPV4_IP_FORWARD.
 *
 *
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/nsproxy.h>

#include <net/sock.h>

#ifdef CONFIG_INET
#include <net/ip.h>
#endif

#ifdef CONFIG_NET
#include <linux/if_ether.h>
#endif

#ifdef CONFIG_TR
#include <linux/if_tr.h>
#endif

static struct list_head *
net_ctl_header_lookup(struct ctl_table_root *root, struct nsproxy *namespaces)
{
	return &namespaces->net_ns->sysctl_table_headers;
}

static struct ctl_table_root net_sysctl_root = {
	.lookup = net_ctl_header_lookup,
};

static LIST_HEAD(net_sysctl_ro_tables);
static struct list_head *net_ctl_ro_header_lookup(struct ctl_table_root *root,
		struct nsproxy *namespaces)
{
	return &net_sysctl_ro_tables;
}

static int net_ctl_ro_header_perms(struct ctl_table_root *root,
		struct nsproxy *namespaces, struct ctl_table *table)
{
	if (namespaces->net_ns == &init_net)
		return table->mode;
	else
		return table->mode & ~0222;
}

static struct ctl_table_root net_sysctl_ro_root = {
	.lookup = net_ctl_ro_header_lookup,
	.permissions = net_ctl_ro_header_perms,
};

static int sysctl_net_init(struct net *net)
{
	INIT_LIST_HEAD(&net->sysctl_table_headers);
	return 0;
}

static void sysctl_net_exit(struct net *net)
{
	WARN_ON(!list_empty(&net->sysctl_table_headers));
	return;
}

static struct pernet_operations sysctl_pernet_ops = {
	.init = sysctl_net_init,
	.exit = sysctl_net_exit,
};

static __init int sysctl_init(void)
{
	int ret;
	ret = register_pernet_subsys(&sysctl_pernet_ops);
	if (ret)
		goto out;
	register_sysctl_root(&net_sysctl_root);
	register_sysctl_root(&net_sysctl_ro_root);
out:
	return ret;
}
subsys_initcall(sysctl_init);

struct ctl_table_header *register_net_sysctl_table(struct net *net,
	const struct ctl_path *path, struct ctl_table *table)
{
	struct nsproxy namespaces;
	namespaces = *current->nsproxy;
	namespaces.net_ns = net;
	return __register_sysctl_paths(&net_sysctl_root,
					&namespaces, path, table);
}
EXPORT_SYMBOL_GPL(register_net_sysctl_table);

struct ctl_table_header *register_net_sysctl_rotable(const
		struct ctl_path *path, struct ctl_table *table)
{
	return __register_sysctl_paths(&net_sysctl_ro_root,
			&init_nsproxy, path, table);
}
EXPORT_SYMBOL_GPL(register_net_sysctl_rotable);

void unregister_net_sysctl_table(struct ctl_table_header *header)
{
	unregister_sysctl_table(header);
}
EXPORT_SYMBOL_GPL(unregister_net_sysctl_table);
