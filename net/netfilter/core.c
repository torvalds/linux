/* netfilter.c: look after the filters for various protocols.
 * Heavily influenced by the old firewall.c by David Bonn and Alan Cox.
 *
 * Thanks to Rob `CmdrTaco' Malda for not influencing this code in any
 * way.
 *
 * Rusty Russell (C)2000 -- This code is GPL.
 * Patrick McHardy (c) 2006-2012
 */
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <net/protocol.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/netfilter_ipv6.h>
#include <linux/inetdevice.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#include "nf_internals.h"

static DEFINE_MUTEX(afinfo_mutex);

const struct nf_afinfo __rcu *nf_afinfo[NFPROTO_NUMPROTO] __read_mostly;
EXPORT_SYMBOL(nf_afinfo);
const struct nf_ipv6_ops __rcu *nf_ipv6_ops __read_mostly;
EXPORT_SYMBOL_GPL(nf_ipv6_ops);

DEFINE_PER_CPU(bool, nf_skb_duplicated);
EXPORT_SYMBOL_GPL(nf_skb_duplicated);

int nf_register_afinfo(const struct nf_afinfo *afinfo)
{
	mutex_lock(&afinfo_mutex);
	RCU_INIT_POINTER(nf_afinfo[afinfo->family], afinfo);
	mutex_unlock(&afinfo_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(nf_register_afinfo);

void nf_unregister_afinfo(const struct nf_afinfo *afinfo)
{
	mutex_lock(&afinfo_mutex);
	RCU_INIT_POINTER(nf_afinfo[afinfo->family], NULL);
	mutex_unlock(&afinfo_mutex);
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(nf_unregister_afinfo);

#ifdef HAVE_JUMP_LABEL
struct static_key nf_hooks_needed[NFPROTO_NUMPROTO][NF_MAX_HOOKS];
EXPORT_SYMBOL(nf_hooks_needed);
#endif

static DEFINE_MUTEX(nf_hook_mutex);
#define nf_entry_dereference(e) \
	rcu_dereference_protected(e, lockdep_is_held(&nf_hook_mutex))

static struct nf_hook_entry __rcu **nf_hook_entry_head(struct net *net, const struct nf_hook_ops *reg)
{
	if (reg->pf != NFPROTO_NETDEV)
		return net->nf.hooks[reg->pf]+reg->hooknum;

#ifdef CONFIG_NETFILTER_INGRESS
	if (reg->hooknum == NF_NETDEV_INGRESS) {
		if (reg->dev && dev_net(reg->dev) == net)
			return &reg->dev->nf_hooks_ingress;
	}
#endif
	return NULL;
}

int nf_register_net_hook(struct net *net, const struct nf_hook_ops *reg)
{
	struct nf_hook_entry __rcu **pp;
	struct nf_hook_entry *entry, *p;

	if (reg->pf == NFPROTO_NETDEV) {
#ifndef CONFIG_NETFILTER_INGRESS
		if (reg->hooknum == NF_NETDEV_INGRESS)
			return -EOPNOTSUPP;
#endif
		if (reg->hooknum != NF_NETDEV_INGRESS ||
		    !reg->dev || dev_net(reg->dev) != net)
			return -EINVAL;
	}

	pp = nf_hook_entry_head(net, reg);
	if (!pp)
		return -EINVAL;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	nf_hook_entry_init(entry, reg);

	mutex_lock(&nf_hook_mutex);

	/* Find the spot in the list */
	for (; (p = nf_entry_dereference(*pp)) != NULL; pp = &p->next) {
		if (reg->priority < nf_hook_entry_priority(p))
			break;
	}
	rcu_assign_pointer(entry->next, p);
	rcu_assign_pointer(*pp, entry);

	mutex_unlock(&nf_hook_mutex);
#ifdef CONFIG_NETFILTER_INGRESS
	if (reg->pf == NFPROTO_NETDEV && reg->hooknum == NF_NETDEV_INGRESS)
		net_inc_ingress_queue();
#endif
#ifdef HAVE_JUMP_LABEL
	static_key_slow_inc(&nf_hooks_needed[reg->pf][reg->hooknum]);
#endif
	return 0;
}
EXPORT_SYMBOL(nf_register_net_hook);

static struct nf_hook_entry *
__nf_unregister_net_hook(struct net *net, const struct nf_hook_ops *reg)
{
	struct nf_hook_entry __rcu **pp;
	struct nf_hook_entry *p;

	pp = nf_hook_entry_head(net, reg);
	if (WARN_ON_ONCE(!pp))
		return NULL;

	mutex_lock(&nf_hook_mutex);
	for (; (p = nf_entry_dereference(*pp)) != NULL; pp = &p->next) {
		if (nf_hook_entry_ops(p) == reg) {
			rcu_assign_pointer(*pp, p->next);
			break;
		}
	}
	mutex_unlock(&nf_hook_mutex);
	if (!p) {
		WARN(1, "nf_unregister_net_hook: hook not found!\n");
		return NULL;
	}
#ifdef CONFIG_NETFILTER_INGRESS
	if (reg->pf == NFPROTO_NETDEV && reg->hooknum == NF_NETDEV_INGRESS)
		net_dec_ingress_queue();
#endif
#ifdef HAVE_JUMP_LABEL
	static_key_slow_dec(&nf_hooks_needed[reg->pf][reg->hooknum]);
#endif

	return p;
}

void nf_unregister_net_hook(struct net *net, const struct nf_hook_ops *reg)
{
	struct nf_hook_entry *p = __nf_unregister_net_hook(net, reg);
	unsigned int nfq;

	if (!p)
		return;

	synchronize_net();

	/* other cpu might still process nfqueue verdict that used reg */
	nfq = nf_queue_nf_hook_drop(net);
	if (nfq)
		synchronize_net();
	kfree(p);
}
EXPORT_SYMBOL(nf_unregister_net_hook);

int nf_register_net_hooks(struct net *net, const struct nf_hook_ops *reg,
			  unsigned int n)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < n; i++) {
		err = nf_register_net_hook(net, &reg[i]);
		if (err)
			goto err;
	}
	return err;

err:
	if (i > 0)
		nf_unregister_net_hooks(net, reg, i);
	return err;
}
EXPORT_SYMBOL(nf_register_net_hooks);

void nf_unregister_net_hooks(struct net *net, const struct nf_hook_ops *reg,
			     unsigned int hookcount)
{
	struct nf_hook_entry *to_free[16];
	unsigned int i, n, nfq;

	do {
		n = min_t(unsigned int, hookcount, ARRAY_SIZE(to_free));

		for (i = 0; i < n; i++)
			to_free[i] = __nf_unregister_net_hook(net, &reg[i]);

		synchronize_net();

		/* need 2nd synchronize_net() if nfqueue is used, skb
		 * can get reinjected right before nf_queue_hook_drop()
		 */
		nfq = nf_queue_nf_hook_drop(net);
		if (nfq)
			synchronize_net();

		for (i = 0; i < n; i++)
			kfree(to_free[i]);

		reg += n;
		hookcount -= n;
	} while (hookcount > 0);
}
EXPORT_SYMBOL(nf_unregister_net_hooks);

static LIST_HEAD(nf_hook_list);

static int _nf_register_hook(struct nf_hook_ops *reg)
{
	struct net *net, *last;
	int ret;

	for_each_net(net) {
		ret = nf_register_net_hook(net, reg);
		if (ret && ret != -ENOENT)
			goto rollback;
	}
	list_add_tail(&reg->list, &nf_hook_list);

	return 0;
rollback:
	last = net;
	for_each_net(net) {
		if (net == last)
			break;
		nf_unregister_net_hook(net, reg);
	}
	return ret;
}

int nf_register_hook(struct nf_hook_ops *reg)
{
	int ret;

	rtnl_lock();
	ret = _nf_register_hook(reg);
	rtnl_unlock();

	return ret;
}
EXPORT_SYMBOL(nf_register_hook);

static void _nf_unregister_hook(struct nf_hook_ops *reg)
{
	struct net *net;

	list_del(&reg->list);
	for_each_net(net)
		nf_unregister_net_hook(net, reg);
}

void nf_unregister_hook(struct nf_hook_ops *reg)
{
	rtnl_lock();
	_nf_unregister_hook(reg);
	rtnl_unlock();
}
EXPORT_SYMBOL(nf_unregister_hook);

int nf_register_hooks(struct nf_hook_ops *reg, unsigned int n)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < n; i++) {
		err = nf_register_hook(&reg[i]);
		if (err)
			goto err;
	}
	return err;

err:
	if (i > 0)
		nf_unregister_hooks(reg, i);
	return err;
}
EXPORT_SYMBOL(nf_register_hooks);

/* Caller MUST take rtnl_lock() */
int _nf_register_hooks(struct nf_hook_ops *reg, unsigned int n)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < n; i++) {
		err = _nf_register_hook(&reg[i]);
		if (err)
			goto err;
	}
	return err;

err:
	if (i > 0)
		_nf_unregister_hooks(reg, i);
	return err;
}
EXPORT_SYMBOL(_nf_register_hooks);

void nf_unregister_hooks(struct nf_hook_ops *reg, unsigned int n)
{
	while (n-- > 0)
		nf_unregister_hook(&reg[n]);
}
EXPORT_SYMBOL(nf_unregister_hooks);

/* Caller MUST take rtnl_lock */
void _nf_unregister_hooks(struct nf_hook_ops *reg, unsigned int n)
{
	while (n-- > 0)
		_nf_unregister_hook(&reg[n]);
}
EXPORT_SYMBOL(_nf_unregister_hooks);

/* Returns 1 if okfn() needs to be executed by the caller,
 * -EPERM for NF_DROP, 0 otherwise.  Caller must hold rcu_read_lock. */
int nf_hook_slow(struct sk_buff *skb, struct nf_hook_state *state,
		 struct nf_hook_entry *entry)
{
	unsigned int verdict;
	int ret;

	do {
		verdict = nf_hook_entry_hookfn(entry, skb, state);
		switch (verdict & NF_VERDICT_MASK) {
		case NF_ACCEPT:
			entry = rcu_dereference(entry->next);
			break;
		case NF_DROP:
			kfree_skb(skb);
			ret = NF_DROP_GETERR(verdict);
			if (ret == 0)
				ret = -EPERM;
			return ret;
		case NF_QUEUE:
			ret = nf_queue(skb, state, &entry, verdict);
			if (ret == 1 && entry)
				continue;
			return ret;
		default:
			/* Implicit handling for NF_STOLEN, as well as any other
			 * non conventional verdicts.
			 */
			return 0;
		}
	} while (entry);

	return 1;
}
EXPORT_SYMBOL(nf_hook_slow);


int skb_make_writable(struct sk_buff *skb, unsigned int writable_len)
{
	if (writable_len > skb->len)
		return 0;

	/* Not exclusive use of packet?  Must copy. */
	if (!skb_cloned(skb)) {
		if (writable_len <= skb_headlen(skb))
			return 1;
	} else if (skb_clone_writable(skb, writable_len))
		return 1;

	if (writable_len <= skb_headlen(skb))
		writable_len = 0;
	else
		writable_len -= skb_headlen(skb);

	return !!__pskb_pull_tail(skb, writable_len);
}
EXPORT_SYMBOL(skb_make_writable);

/* This needs to be compiled in any case to avoid dependencies between the
 * nfnetlink_queue code and nf_conntrack.
 */
struct nfnl_ct_hook __rcu *nfnl_ct_hook __read_mostly;
EXPORT_SYMBOL_GPL(nfnl_ct_hook);

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
/* This does not belong here, but locally generated errors need it if connection
   tracking in use: without this, connection may not be in hash table, and hence
   manufactured ICMP or RST packets will not be associated with it. */
void (*ip_ct_attach)(struct sk_buff *, const struct sk_buff *)
		__rcu __read_mostly;
EXPORT_SYMBOL(ip_ct_attach);

void nf_ct_attach(struct sk_buff *new, const struct sk_buff *skb)
{
	void (*attach)(struct sk_buff *, const struct sk_buff *);

	if (skb->_nfct) {
		rcu_read_lock();
		attach = rcu_dereference(ip_ct_attach);
		if (attach)
			attach(new, skb);
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL(nf_ct_attach);

void (*nf_ct_destroy)(struct nf_conntrack *) __rcu __read_mostly;
EXPORT_SYMBOL(nf_ct_destroy);

void nf_conntrack_destroy(struct nf_conntrack *nfct)
{
	void (*destroy)(struct nf_conntrack *);

	rcu_read_lock();
	destroy = rcu_dereference(nf_ct_destroy);
	BUG_ON(destroy == NULL);
	destroy(nfct);
	rcu_read_unlock();
}
EXPORT_SYMBOL(nf_conntrack_destroy);

/* Built-in default zone used e.g. by modules. */
const struct nf_conntrack_zone nf_ct_zone_dflt = {
	.id	= NF_CT_DEFAULT_ZONE_ID,
	.dir	= NF_CT_DEFAULT_ZONE_DIR,
};
EXPORT_SYMBOL_GPL(nf_ct_zone_dflt);
#endif /* CONFIG_NF_CONNTRACK */

#ifdef CONFIG_NF_NAT_NEEDED
void (*nf_nat_decode_session_hook)(struct sk_buff *, struct flowi *);
EXPORT_SYMBOL(nf_nat_decode_session_hook);
#endif

static int nf_register_hook_list(struct net *net)
{
	struct nf_hook_ops *elem;
	int ret;

	rtnl_lock();
	list_for_each_entry(elem, &nf_hook_list, list) {
		ret = nf_register_net_hook(net, elem);
		if (ret && ret != -ENOENT)
			goto out_undo;
	}
	rtnl_unlock();
	return 0;

out_undo:
	list_for_each_entry_continue_reverse(elem, &nf_hook_list, list)
		nf_unregister_net_hook(net, elem);
	rtnl_unlock();
	return ret;
}

static void nf_unregister_hook_list(struct net *net)
{
	struct nf_hook_ops *elem;

	rtnl_lock();
	list_for_each_entry(elem, &nf_hook_list, list)
		nf_unregister_net_hook(net, elem);
	rtnl_unlock();
}

static int __net_init netfilter_net_init(struct net *net)
{
	int i, h, ret;

	for (i = 0; i < ARRAY_SIZE(net->nf.hooks); i++) {
		for (h = 0; h < NF_MAX_HOOKS; h++)
			RCU_INIT_POINTER(net->nf.hooks[i][h], NULL);
	}

#ifdef CONFIG_PROC_FS
	net->nf.proc_netfilter = proc_net_mkdir(net, "netfilter",
						net->proc_net);
	if (!net->nf.proc_netfilter) {
		if (!net_eq(net, &init_net))
			pr_err("cannot create netfilter proc entry");

		return -ENOMEM;
	}
#endif
	ret = nf_register_hook_list(net);
	if (ret)
		remove_proc_entry("netfilter", net->proc_net);

	return ret;
}

static void __net_exit netfilter_net_exit(struct net *net)
{
	nf_unregister_hook_list(net);
	remove_proc_entry("netfilter", net->proc_net);
}

static struct pernet_operations netfilter_net_ops = {
	.init = netfilter_net_init,
	.exit = netfilter_net_exit,
};

int __init netfilter_init(void)
{
	int ret;

	ret = register_pernet_subsys(&netfilter_net_ops);
	if (ret < 0)
		goto err;

	ret = netfilter_log_init();
	if (ret < 0)
		goto err_pernet;

	return 0;
err_pernet:
	unregister_pernet_subsys(&netfilter_net_ops);
err:
	return ret;
}
