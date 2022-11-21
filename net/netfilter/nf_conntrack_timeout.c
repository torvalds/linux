// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) 2012 by Pablo Neira Ayuso <pablo@netfilter.org>
 * (C) 2012 by Vyatta Inc. <http://www.vyatta.com>
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
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_timeout.h>

const struct nf_ct_timeout_hooks *nf_ct_timeout_hook __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_timeout_hook);

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

static void __nf_ct_timeout_put(struct nf_ct_timeout *timeout)
{
	const struct nf_ct_timeout_hooks *h = rcu_dereference(nf_ct_timeout_hook);

	if (h)
		h->timeout_put(timeout);
}

int nf_ct_set_timeout(struct net *net, struct nf_conn *ct,
		      u8 l3num, u8 l4num, const char *timeout_name)
{
	const struct nf_ct_timeout_hooks *h;
	struct nf_ct_timeout *timeout;
	struct nf_conn_timeout *timeout_ext;
	const char *errmsg = NULL;
	int ret = 0;

	rcu_read_lock();
	h = rcu_dereference(nf_ct_timeout_hook);
	if (!h) {
		ret = -ENOENT;
		errmsg = "Timeout policy base is empty";
		goto out;
	}

	timeout = h->timeout_find_get(net, timeout_name);
	if (!timeout) {
		ret = -ENOENT;
		pr_info_ratelimited("No such timeout policy \"%s\"\n",
				    timeout_name);
		goto out;
	}

	if (timeout->l3num != l3num) {
		ret = -EINVAL;
		pr_info_ratelimited("Timeout policy `%s' can only be used by "
				    "L%d protocol number %d\n",
				    timeout_name, 3, timeout->l3num);
		goto err_put_timeout;
	}
	/* Make sure the timeout policy matches any existing protocol tracker,
	 * otherwise default to generic.
	 */
	if (timeout->l4proto->l4proto != l4num) {
		ret = -EINVAL;
		pr_info_ratelimited("Timeout policy `%s' can only be used by "
				    "L%d protocol number %d\n",
				    timeout_name, 4, timeout->l4proto->l4proto);
		goto err_put_timeout;
	}
	timeout_ext = nf_ct_timeout_ext_add(ct, timeout, GFP_ATOMIC);
	if (!timeout_ext) {
		ret = -ENOMEM;
		goto err_put_timeout;
	}

	rcu_read_unlock();
	return ret;

err_put_timeout:
	__nf_ct_timeout_put(timeout);
out:
	rcu_read_unlock();
	if (errmsg)
		pr_info_ratelimited("%s\n", errmsg);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_set_timeout);

void nf_ct_destroy_timeout(struct nf_conn *ct)
{
	struct nf_conn_timeout *timeout_ext;
	const struct nf_ct_timeout_hooks *h;

	rcu_read_lock();
	h = rcu_dereference(nf_ct_timeout_hook);

	if (h) {
		timeout_ext = nf_ct_timeout_find(ct);
		if (timeout_ext) {
			h->timeout_put(timeout_ext->timeout);
			RCU_INIT_POINTER(timeout_ext->timeout, NULL);
		}
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(nf_ct_destroy_timeout);
