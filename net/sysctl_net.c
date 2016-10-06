/* -*- linux-c -*-
 * sysctl_net.c: sysctl interface to net subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net directories for each protocol family. [MS]
 *
 * Revision 1.2  1996/05/08  20:24:40  shaver
 * Added bits for NET_BRIDGE and the NET_IPV4_ARP stuff and
 * NET_IPV4_IP_FORWARD.
 *
 *
 */

#include <linux/mm.h>
#include <linux/export.h>
#include <linux/sysctl.h>
#include <linux/nsproxy.h>

#include <net/sock.h>

#ifdef CONFIG_INET
#include <net/ip.h>
#endif

#ifdef CONFIG_NET
#include <linux/if_ether.h>
#endif

static struct ctl_table_set *
net_ctl_header_lookup(struct ctl_table_root *root)
{
	return &current->nsproxy->net_ns->sysctls;
}

static int is_seen(struct ctl_table_set *set)
{
	return &current->nsproxy->net_ns->sysctls == set;
}

/* Return standard mode bits for table entry. */
static int net_ctl_permissions(struct ctl_table_header *head,
			       struct ctl_table *table)
{
	struct net *net = container_of(head->set, struct net, sysctls);

	/* Allow network administrator to have same access as root. */
	if (ns_capable_noaudit(net->user_ns, CAP_NET_ADMIN)) {
		int mode = (table->mode >> 6) & 7;
		return (mode << 6) | (mode << 3) | mode;
	}

	return table->mode;
}

static void net_ctl_set_ownership(struct ctl_table_header *head,
				  struct ctl_table *table,
				  kuid_t *uid, kgid_t *gid)
{
	struct net *net = container_of(head->set, struct net, sysctls);
	kuid_t ns_root_uid;
	kgid_t ns_root_gid;

	ns_root_uid = make_kuid(net->user_ns, 0);
	if (uid_valid(ns_root_uid))
		*uid = ns_root_uid;

	ns_root_gid = make_kgid(net->user_ns, 0);
	if (gid_valid(ns_root_gid))
		*gid = ns_root_gid;
}

static struct ctl_table_root net_sysctl_root = {
	.lookup = net_ctl_header_lookup,
	.permissions = net_ctl_permissions,
	.set_ownership = net_ctl_set_ownership,
};

static int __net_init sysctl_net_init(struct net *net)
{
	setup_sysctl_set(&net->sysctls, &net_sysctl_root, is_seen);
	return 0;
}

static void __net_exit sysctl_net_exit(struct net *net)
{
	retire_sysctl_set(&net->sysctls);
}

static struct pernet_operations sysctl_pernet_ops = {
	.init = sysctl_net_init,
	.exit = sysctl_net_exit,
};

static struct ctl_table_header *net_header;
__init int net_sysctl_init(void)
{
	static struct ctl_table empty[1];
	int ret = -ENOMEM;
	/* Avoid limitations in the sysctl implementation by
	 * registering "/proc/sys/net" as an empty directory not in a
	 * network namespace.
	 */
	net_header = register_sysctl("net", empty);
	if (!net_header)
		goto out;
	ret = register_pernet_subsys(&sysctl_pernet_ops);
	if (ret)
		goto out1;
	register_sysctl_root(&net_sysctl_root);
out:
	return ret;
out1:
	unregister_sysctl_table(net_header);
	net_header = NULL;
	goto out;
}

struct ctl_table_header *register_net_sysctl(struct net *net,
	const char *path, struct ctl_table *table)
{
	return __register_sysctl_table(&net->sysctls, path, table);
}
EXPORT_SYMBOL_GPL(register_net_sysctl);

void unregister_net_sysctl_table(struct ctl_table_header *header)
{
	unregister_sysctl_table(header);
}
EXPORT_SYMBOL_GPL(unregister_net_sysctl_table);
