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
#include <linux/random.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rculist.h>
#include <linux/rtnetlink.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_extend.h>

static DEFINE_MUTEX(nf_ct_helper_mutex);
struct hlist_head *nf_ct_helper_hash __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_helper_hash);
unsigned int nf_ct_helper_hsize __read_mostly;
EXPORT_SYMBOL_GPL(nf_ct_helper_hsize);
static unsigned int nf_ct_helper_count __read_mostly;

static bool nf_ct_auto_assign_helper __read_mostly = true;
module_param_named(nf_conntrack_helper, nf_ct_auto_assign_helper, bool, 0644);
MODULE_PARM_DESC(nf_conntrack_helper,
		 "Enable automatic conntrack helper assignment (default 1)");

#ifdef CONFIG_SYSCTL
static struct ctl_table helper_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_helper",
		.data		= &init_net.ct.sysctl_auto_assign_helper,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{}
};

static int nf_conntrack_helper_init_sysctl(struct net *net)
{
	struct ctl_table *table;

	table = kmemdup(helper_sysctl_table, sizeof(helper_sysctl_table),
			GFP_KERNEL);
	if (!table)
		goto out;

	table[0].data = &net->ct.sysctl_auto_assign_helper;

	net->ct.helper_sysctl_header =
		register_net_sysctl(net, "net/netfilter", table);

	if (!net->ct.helper_sysctl_header) {
		pr_err("nf_conntrack_helper: can't register to sysctl.\n");
		goto out_register;
	}
	return 0;

out_register:
	kfree(table);
out:
	return -ENOMEM;
}

static void nf_conntrack_helper_fini_sysctl(struct net *net)
{
	struct ctl_table *table;

	table = net->ct.helper_sysctl_header->ctl_table_arg;
	unregister_net_sysctl_table(net->ct.helper_sysctl_header);
	kfree(table);
}
#else
static int nf_conntrack_helper_init_sysctl(struct net *net)
{
	return 0;
}

static void nf_conntrack_helper_fini_sysctl(struct net *net)
{
}
#endif /* CONFIG_SYSCTL */

/* Stupid hash, but collision free for the default registrations of the
 * helpers currently in the kernel. */
static unsigned int helper_hash(const struct nf_conntrack_tuple *tuple)
{
	return (((tuple->src.l3num << 8) | tuple->dst.protonum) ^
		(__force __u16)tuple->src.u.all) % nf_ct_helper_hsize;
}

static struct nf_conntrack_helper *
__nf_ct_helper_find(const struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_helper *helper;
	struct nf_conntrack_tuple_mask mask = { .src.u.all = htons(0xFFFF) };
	struct hlist_node *n;
	unsigned int h;

	if (!nf_ct_helper_count)
		return NULL;

	h = helper_hash(tuple);
	hlist_for_each_entry_rcu(helper, n, &nf_ct_helper_hash[h], hnode) {
		if (nf_ct_tuple_src_mask_cmp(tuple, &helper->tuple, &mask))
			return helper;
	}
	return NULL;
}

struct nf_conntrack_helper *
__nf_conntrack_helper_find(const char *name, u16 l3num, u8 protonum)
{
	struct nf_conntrack_helper *h;
	struct hlist_node *n;
	unsigned int i;

	for (i = 0; i < nf_ct_helper_hsize; i++) {
		hlist_for_each_entry_rcu(h, n, &nf_ct_helper_hash[i], hnode) {
			if (!strcmp(h->name, name) &&
			    h->tuple.src.l3num == l3num &&
			    h->tuple.dst.protonum == protonum)
				return h;
		}
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(__nf_conntrack_helper_find);

struct nf_conntrack_helper *
nf_conntrack_helper_try_module_get(const char *name, u16 l3num, u8 protonum)
{
	struct nf_conntrack_helper *h;

	h = __nf_conntrack_helper_find(name, l3num, protonum);
#ifdef CONFIG_MODULES
	if (h == NULL) {
		if (request_module("nfct-helper-%s", name) == 0)
			h = __nf_conntrack_helper_find(name, l3num, protonum);
	}
#endif
	if (h != NULL && !try_module_get(h->me))
		h = NULL;

	return h;
}
EXPORT_SYMBOL_GPL(nf_conntrack_helper_try_module_get);

struct nf_conn_help *
nf_ct_helper_ext_add(struct nf_conn *ct,
		     struct nf_conntrack_helper *helper, gfp_t gfp)
{
	struct nf_conn_help *help;

	help = nf_ct_ext_add_length(ct, NF_CT_EXT_HELPER,
				    helper->data_len, gfp);
	if (help)
		INIT_HLIST_HEAD(&help->expectations);
	else
		pr_debug("failed to add helper extension area");
	return help;
}
EXPORT_SYMBOL_GPL(nf_ct_helper_ext_add);

int __nf_ct_try_assign_helper(struct nf_conn *ct, struct nf_conn *tmpl,
			      gfp_t flags)
{
	struct nf_conntrack_helper *helper = NULL;
	struct nf_conn_help *help;
	struct net *net = nf_ct_net(ct);
	int ret = 0;

	/* We already got a helper explicitly attached. The function
	 * nf_conntrack_alter_reply - in case NAT is in use - asks for looking
	 * the helper up again. Since now the user is in full control of
	 * making consistent helper configurations, skip this automatic
	 * re-lookup, otherwise we'll lose the helper.
	 */
	if (test_bit(IPS_HELPER_BIT, &ct->status))
		return 0;

	if (tmpl != NULL) {
		help = nfct_help(tmpl);
		if (help != NULL) {
			helper = help->helper;
			set_bit(IPS_HELPER_BIT, &ct->status);
		}
	}

	help = nfct_help(ct);
	if (net->ct.sysctl_auto_assign_helper && helper == NULL) {
		helper = __nf_ct_helper_find(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);
		if (unlikely(!net->ct.auto_assign_helper_warned && helper)) {
			pr_info("nf_conntrack: automatic helper "
				"assignment is deprecated and it will "
				"be removed soon. Use the iptables CT target "
				"to attach helpers instead.\n");
			net->ct.auto_assign_helper_warned = true;
		}
	}

	if (helper == NULL) {
		if (help)
			RCU_INIT_POINTER(help->helper, NULL);
		goto out;
	}

	if (help == NULL) {
		help = nf_ct_helper_ext_add(ct, helper, flags);
		if (help == NULL) {
			ret = -ENOMEM;
			goto out;
		}
	} else {
		memset(help->data, 0, helper->data_len);
	}

	rcu_assign_pointer(help->helper, helper);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(__nf_ct_try_assign_helper);

static inline int unhelp(struct nf_conntrack_tuple_hash *i,
			 const struct nf_conntrack_helper *me)
{
	struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(i);
	struct nf_conn_help *help = nfct_help(ct);

	if (help && rcu_dereference_protected(
			help->helper,
			lockdep_is_held(&nf_conntrack_lock)
			) == me) {
		nf_conntrack_event(IPCT_HELPER, ct);
		RCU_INIT_POINTER(help->helper, NULL);
	}
	return 0;
}

void nf_ct_helper_destroy(struct nf_conn *ct)
{
	struct nf_conn_help *help = nfct_help(ct);
	struct nf_conntrack_helper *helper;

	if (help) {
		rcu_read_lock();
		helper = rcu_dereference(help->helper);
		if (helper && helper->destroy)
			helper->destroy(ct);
		rcu_read_unlock();
	}
}

static LIST_HEAD(nf_ct_helper_expectfn_list);

void nf_ct_helper_expectfn_register(struct nf_ct_helper_expectfn *n)
{
	spin_lock_bh(&nf_conntrack_lock);
	list_add_rcu(&n->head, &nf_ct_helper_expectfn_list);
	spin_unlock_bh(&nf_conntrack_lock);
}
EXPORT_SYMBOL_GPL(nf_ct_helper_expectfn_register);

void nf_ct_helper_expectfn_unregister(struct nf_ct_helper_expectfn *n)
{
	spin_lock_bh(&nf_conntrack_lock);
	list_del_rcu(&n->head);
	spin_unlock_bh(&nf_conntrack_lock);
}
EXPORT_SYMBOL_GPL(nf_ct_helper_expectfn_unregister);

struct nf_ct_helper_expectfn *
nf_ct_helper_expectfn_find_by_name(const char *name)
{
	struct nf_ct_helper_expectfn *cur;
	bool found = false;

	rcu_read_lock();
	list_for_each_entry_rcu(cur, &nf_ct_helper_expectfn_list, head) {
		if (!strcmp(cur->name, name)) {
			found = true;
			break;
		}
	}
	rcu_read_unlock();
	return found ? cur : NULL;
}
EXPORT_SYMBOL_GPL(nf_ct_helper_expectfn_find_by_name);

struct nf_ct_helper_expectfn *
nf_ct_helper_expectfn_find_by_symbol(const void *symbol)
{
	struct nf_ct_helper_expectfn *cur;
	bool found = false;

	rcu_read_lock();
	list_for_each_entry_rcu(cur, &nf_ct_helper_expectfn_list, head) {
		if (cur->expectfn == symbol) {
			found = true;
			break;
		}
	}
	rcu_read_unlock();
	return found ? cur : NULL;
}
EXPORT_SYMBOL_GPL(nf_ct_helper_expectfn_find_by_symbol);

int nf_conntrack_helper_register(struct nf_conntrack_helper *me)
{
	int ret = 0;
	struct nf_conntrack_helper *cur;
	struct hlist_node *n;
	unsigned int h = helper_hash(&me->tuple);

	BUG_ON(me->expect_policy == NULL);
	BUG_ON(me->expect_class_max >= NF_CT_MAX_EXPECT_CLASSES);
	BUG_ON(strlen(me->name) > NF_CT_HELPER_NAME_LEN - 1);

	mutex_lock(&nf_ct_helper_mutex);
	hlist_for_each_entry(cur, n, &nf_ct_helper_hash[h], hnode) {
		if (strncmp(cur->name, me->name, NF_CT_HELPER_NAME_LEN) == 0 &&
		    cur->tuple.src.l3num == me->tuple.src.l3num &&
		    cur->tuple.dst.protonum == me->tuple.dst.protonum) {
			ret = -EEXIST;
			goto out;
		}
	}
	hlist_add_head_rcu(&me->hnode, &nf_ct_helper_hash[h]);
	nf_ct_helper_count++;
out:
	mutex_unlock(&nf_ct_helper_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_helper_register);

static void __nf_conntrack_helper_unregister(struct nf_conntrack_helper *me,
					     struct net *net)
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_expect *exp;
	const struct hlist_node *n, *next;
	const struct hlist_nulls_node *nn;
	unsigned int i;

	/* Get rid of expectations */
	for (i = 0; i < nf_ct_expect_hsize; i++) {
		hlist_for_each_entry_safe(exp, n, next,
					  &net->ct.expect_hash[i], hnode) {
			struct nf_conn_help *help = nfct_help(exp->master);
			if ((rcu_dereference_protected(
					help->helper,
					lockdep_is_held(&nf_conntrack_lock)
					) == me || exp->helper == me) &&
			    del_timer(&exp->timeout)) {
				nf_ct_unlink_expect(exp);
				nf_ct_expect_put(exp);
			}
		}
	}

	/* Get rid of expecteds, set helpers to NULL. */
	hlist_nulls_for_each_entry(h, nn, &net->ct.unconfirmed, hnnode)
		unhelp(h, me);
	for (i = 0; i < net->ct.htable_size; i++) {
		hlist_nulls_for_each_entry(h, nn, &net->ct.hash[i], hnnode)
			unhelp(h, me);
	}
}

void nf_conntrack_helper_unregister(struct nf_conntrack_helper *me)
{
	struct net *net;

	mutex_lock(&nf_ct_helper_mutex);
	hlist_del_rcu(&me->hnode);
	nf_ct_helper_count--;
	mutex_unlock(&nf_ct_helper_mutex);

	/* Make sure every nothing is still using the helper unless its a
	 * connection in the hash.
	 */
	synchronize_rcu();

	rtnl_lock();
	spin_lock_bh(&nf_conntrack_lock);
	for_each_net(net)
		__nf_conntrack_helper_unregister(me, net);
	spin_unlock_bh(&nf_conntrack_lock);
	rtnl_unlock();
}
EXPORT_SYMBOL_GPL(nf_conntrack_helper_unregister);

static struct nf_ct_ext_type helper_extend __read_mostly = {
	.len	= sizeof(struct nf_conn_help),
	.align	= __alignof__(struct nf_conn_help),
	.id	= NF_CT_EXT_HELPER,
};

int nf_conntrack_helper_init(struct net *net)
{
	int err;

	net->ct.auto_assign_helper_warned = false;
	net->ct.sysctl_auto_assign_helper = nf_ct_auto_assign_helper;

	if (net_eq(net, &init_net)) {
		nf_ct_helper_hsize = 1; /* gets rounded up to use one page */
		nf_ct_helper_hash =
			nf_ct_alloc_hashtable(&nf_ct_helper_hsize, 0);
		if (!nf_ct_helper_hash)
			return -ENOMEM;

		err = nf_ct_extend_register(&helper_extend);
		if (err < 0)
			goto err1;
	}

	err = nf_conntrack_helper_init_sysctl(net);
	if (err < 0)
		goto out_sysctl;

	return 0;

out_sysctl:
	if (net_eq(net, &init_net))
		nf_ct_extend_unregister(&helper_extend);
err1:
	nf_ct_free_hashtable(nf_ct_helper_hash, nf_ct_helper_hsize);
	return err;
}

void nf_conntrack_helper_fini(struct net *net)
{
	nf_conntrack_helper_fini_sysctl(net);
	if (net_eq(net, &init_net)) {
		nf_ct_extend_unregister(&helper_extend);
		nf_ct_free_hashtable(nf_ct_helper_hash, nf_ct_helper_hsize);
	}
}
