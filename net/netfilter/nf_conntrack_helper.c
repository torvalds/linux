/* Helper handling for netfilter. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_core.h>

static __read_mostly LIST_HEAD(helpers);

struct nf_conntrack_helper *
__nf_ct_helper_find(const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_helper *h;

	list_for_each_entry(h, &helpers, list) {
		if (nf_ct_tuple_mask_cmp(tuple, &h->tuple, &h->mask))
			return h;
	}
	return NULL;
}

struct nf_conntrack_helper *
nf_ct_helper_find_get( const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_helper *helper;

	/* need nf_conntrack_lock to assure that helper exists until
	 * try_module_get() is called */
	read_lock_bh(&nf_conntrack_lock);

	helper = __nf_ct_helper_find(tuple);
	if (helper) {
		/* need to increase module usage count to assure helper will
		 * not go away while the caller is e.g. busy putting a
		 * conntrack in the hash that uses the helper */
		if (!try_module_get(helper->me))
			helper = NULL;
	}

	read_unlock_bh(&nf_conntrack_lock);

	return helper;
}

void nf_ct_helper_put(struct nf_conntrack_helper *helper)
{
	module_put(helper->me);
}

struct nf_conntrack_helper *
__nf_conntrack_helper_find_byname(const char *name)
{
	struct nf_conntrack_helper *h;

	list_for_each_entry(h, &helpers, list) {
		if (!strcmp(h->name, name))
			return h;
	}

	return NULL;
}

static inline int unhelp(struct nf_conntrack_tuple_hash *i,
			 const struct nf_conntrack_helper *me)
{
	struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(i);
	struct nf_conn_help *help = nfct_help(ct);

	if (help && help->helper == me) {
		nf_conntrack_event(IPCT_HELPER, ct);
		help->helper = NULL;
	}
	return 0;
}

int nf_conntrack_helper_register(struct nf_conntrack_helper *me)
{
	int size, ret;

	BUG_ON(me->timeout == 0);

	size = ALIGN(sizeof(struct nf_conn), __alignof__(struct nf_conn_help)) +
	       sizeof(struct nf_conn_help);
	ret = nf_conntrack_register_cache(NF_CT_F_HELP, "nf_conntrack:help",
					  size);
	if (ret < 0) {
		printk(KERN_ERR "nf_conntrack_helper_register: Unable to create slab cache for conntracks\n");
		return ret;
	}
	write_lock_bh(&nf_conntrack_lock);
	list_add(&me->list, &helpers);
	write_unlock_bh(&nf_conntrack_lock);

	return 0;
}

void nf_conntrack_helper_unregister(struct nf_conntrack_helper *me)
{
	unsigned int i;
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_expect *exp, *tmp;

	/* Need write lock here, to delete helper. */
	write_lock_bh(&nf_conntrack_lock);
	list_del(&me->list);

	/* Get rid of expectations */
	list_for_each_entry_safe(exp, tmp, &nf_conntrack_expect_list, list) {
		struct nf_conn_help *help = nfct_help(exp->master);
		if (help->helper == me && del_timer(&exp->timeout)) {
			nf_ct_unlink_expect(exp);
			nf_conntrack_expect_put(exp);
		}
	}

	/* Get rid of expecteds, set helpers to NULL. */
	list_for_each_entry(h, &unconfirmed, list)
		unhelp(h, me);
	for (i = 0; i < nf_conntrack_htable_size; i++) {
		list_for_each_entry(h, &nf_conntrack_hash[i], list)
			unhelp(h, me);
	}
	write_unlock_bh(&nf_conntrack_lock);

	/* Someone could be still looking at the helper in a bh. */
	synchronize_net();
}
