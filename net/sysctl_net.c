// SPDX-License-Identifier: GPL-2.0-only
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
	net_header = register_sysctl_sz("net", empty, 0);
	if (!net_header)
		goto out;
	ret = register_pernet_subsys(&sysctl_pernet_ops);
	if (ret)
		goto out1;
out:
	return ret;
out1:
	unregister_sysctl_table(net_header);
	net_header = NULL;
	goto out;
}

/* Verify that sysctls for non-init netns are safe by either:
 * 1) being read-only, or
 * 2) having a data pointer which points outside of the global kernel/module
 *    data segment, and rather into the heap where a per-net object was
 *    allocated.
 */
static void ensure_safe_net_sysctl(struct net *net, const char *path,
				   struct ctl_table *table, size_t table_size)
{
	struct ctl_table *ent;

	pr_debug("Registering net sysctl (net %p): %s\n", net, path);
	ent = table;
	for (size_t i = 0; i < table_size && ent->procname; ent++, i++) {
		unsigned long addr;
		const char *where;

		pr_debug("  procname=%s mode=%o proc_handler=%ps data=%p\n",
			 ent->procname, ent->mode, ent->proc_handler, ent->data);

		/* If it's not writable inside the netns, then it can't hurt. */
		if ((ent->mode & 0222) == 0) {
			pr_debug("    Not writable by anyone\n");
			continue;
		}

		/* Where does data point? */
		addr = (unsigned long)ent->data;
		if (is_module_address(addr))
			where = "module";
		else if (is_kernel_core_data(addr))
			where = "kernel";
		else
			continue;

		/* If it is writable and points to kernel/module global
		 * data, then it's probably a netns leak.
		 */
		WARN(1, "sysctl %s/%s: data points to %s global data: %ps\n",
		     path, ent->procname, where, ent->data);

		/* Make it "safe" by dropping writable perms */
		ent->mode &= ~0222;
	}
}

struct ctl_table_header *register_net_sysctl_sz(struct net *net,
						const char *path,
						struct ctl_table *table,
						size_t table_size)
{
	int count;
	struct ctl_table *entry;

	if (!net_eq(net, &init_net))
		ensure_safe_net_sysctl(net, path, table, table_size);

	entry = table;
	for (count = 0 ; count < table_size && entry->procname; entry++, count++)
		;

	return __register_sysctl_table(&net->sysctls, path, table, count);
}
EXPORT_SYMBOL_GPL(register_net_sysctl_sz);

void unregister_net_sysctl_table(struct ctl_table_header *header)
{
	unregister_sysctl_table(header);
}
EXPORT_SYMBOL_GPL(unregister_net_sysctl_table);
