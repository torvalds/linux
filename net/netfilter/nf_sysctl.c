/* nf_sysctl.c	netfilter sysctl registration/unregistation
 *
 * Copyright (c) 2006 Patrick McHardy <kaber@trash.net>
 */
#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/string.h>
#include <linux/slab.h>

static void
path_free(struct ctl_table *path, struct ctl_table *table)
{
	struct ctl_table *t, *next;

	for (t = path; t != NULL && t != table; t = next) {
		next = t->child;
		kfree(t);
	}
}

static struct ctl_table *
path_dup(struct ctl_table *path, struct ctl_table *table)
{
	struct ctl_table *t, *last = NULL, *tmp;

	for (t = path; t != NULL; t = t->child) {
		/* twice the size since path elements are terminated by an
		 * empty element */
		tmp = kmemdup(t, 2 * sizeof(*t), GFP_KERNEL);
		if (tmp == NULL) {
			if (last != NULL)
				path_free(path, table);
			return NULL;
		}

		if (last != NULL)
			last->child = tmp;
		else
			path = tmp;
		last = tmp;
	}

	if (last != NULL)
		last->child = table;
	else
		path = table;

	return path;
}

struct ctl_table_header *
nf_register_sysctl_table(struct ctl_table *path, struct ctl_table *table)
{
	struct ctl_table_header *header;

	path = path_dup(path, table);
	if (path == NULL)
		return NULL;
	header = register_sysctl_table(path, 0);
	if (header == NULL)
		path_free(path, table);
	return header;
}
EXPORT_SYMBOL_GPL(nf_register_sysctl_table);

void
nf_unregister_sysctl_table(struct ctl_table_header *header,
			   struct ctl_table *table)
{
	struct ctl_table *path = header->ctl_table;

	unregister_sysctl_table(header);
	path_free(path, table);
}
EXPORT_SYMBOL_GPL(nf_unregister_sysctl_table);

/* net/netfilter */
static struct ctl_table nf_net_netfilter_table[] = {
	{
		.ctl_name	= NET_NETFILTER,
		.procname	= "netfilter",
		.mode		= 0555,
	},
	{
		.ctl_name	= 0
	}
};
struct ctl_table nf_net_netfilter_sysctl_path[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= nf_net_netfilter_table,
	},
	{
		.ctl_name	= 0
	}
};
EXPORT_SYMBOL_GPL(nf_net_netfilter_sysctl_path);

/* net/ipv4/netfilter */
static struct ctl_table nf_net_ipv4_netfilter_table[] = {
	{
		.ctl_name	= NET_IPV4_NETFILTER,
		.procname	= "netfilter",
		.mode		= 0555,
	},
	{
		.ctl_name	= 0
	}
};
static struct ctl_table nf_net_ipv4_table[] = {
	{
		.ctl_name	= NET_IPV4,
		.procname	= "ipv4",
		.mode		= 0555,
		.child		= nf_net_ipv4_netfilter_table,
	},
	{
		.ctl_name	= 0
	}
};
struct ctl_table nf_net_ipv4_netfilter_sysctl_path[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= nf_net_ipv4_table,
	},
	{
		.ctl_name	= 0
	}
};
EXPORT_SYMBOL_GPL(nf_net_ipv4_netfilter_sysctl_path);
