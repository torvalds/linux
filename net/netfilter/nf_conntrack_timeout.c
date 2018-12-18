/*
 * (C) 2012 by Pablo Neira Ayuso <pablo@netfilter.org>
 * (C) 2012 by Vyatta Inc. <http://www.vyatta.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation (or any later at your option).
 */

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/stddef.h>
#include <linux/err.h>
#include <linux/percpu.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/export.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_timeout.h>

struct nf_ct_timeout *
(*nf_ct_timeout_find_get_hook)(struct net *net, const char *name) __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_timeout_find_get_hook);

void (*nf_ct_timeout_put_hook)(struct nf_ct_timeout *timeout) __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_timeout_put_hook);

static int untimeout(struct nf_conn *ct, void *timeout)
{
	struct nf_conn_timeout *timeout_ext = nf_ct_timeout_find(ct);

	if (timeout_ext && (!timeout || timeout_ext->timeout == timeout))
		RCU_INIT_POINTER(timeout_ext->timeout, NULL);

	/* We are not intended to delete this conntrack. */
	return 0;
}

void nf_ct_untimeout(struct net *net, struct nf_ct_timeout *timeout)
{
	nf_ct_iterate_cleanup_net(net, untimeout, timeout, 0, 0);
}
EXPORT_SYMBOL_GPL(nf_ct_untimeout);

static const struct nf_ct_ext_type timeout_extend = {
	.len	= sizeof(struct nf_conn_timeout),
	.align	= __alignof__(struct nf_conn_timeout),
	.id	= NF_CT_EXT_TIMEOUT,
};

int nf_conntrack_timeout_init(void)
{
	int ret = nf_ct_extend_register(&timeout_extend);
	if (ret < 0)
		pr_err("nf_ct_timeout: Unable to register timeout extension.\n");
	return ret;
}

void nf_conntrack_timeout_fini(void)
{
	nf_ct_extend_unregister(&timeout_extend);
}
