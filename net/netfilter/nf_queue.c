/*
 * Rusty Russell (C)2000 -- This code is GPL.
 * Patrick McHardy (c) 2006-2012
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_bridge.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>
#include <net/protocol.h>
#include <net/netfilter/nf_queue.h>
#include <net/dst.h>

#include "nf_internals.h"

/*
 * Hook for nfnetlink_queue to register its queue handler.
 * We do this so that most of the NFQUEUE code can be modular.
 *
 * Once the queue is registered it must reinject all packets it
 * receives, no matter what.
 */

/* return EBUSY when somebody else is registered, return EEXIST if the
 * same handler is registered, return 0 in case of success. */
void nf_register_queue_handler(struct net *net, const struct nf_queue_handler *qh)
{
	/* should never happen, we only have one queueing backend in kernel */
	WARN_ON(rcu_access_pointer(net->nf.queue_handler));
	rcu_assign_pointer(net->nf.queue_handler, qh);
}
EXPORT_SYMBOL(nf_register_queue_handler);

/* The caller must flush their queue before this */
void nf_unregister_queue_handler(struct net *net)
{
	RCU_INIT_POINTER(net->nf.queue_handler, NULL);
}
EXPORT_SYMBOL(nf_unregister_queue_handler);

static void nf_queue_entry_release_br_nf_refs(struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);

	if (nf_bridge) {
		struct net_device *physdev;

		physdev = nf_bridge_get_physindev(skb);
		if (physdev)
			dev_put(physdev);
		physdev = nf_bridge_get_physoutdev(skb);
		if (physdev)
			dev_put(physdev);
	}
#endif
}

void nf_queue_entry_release_refs(struct nf_queue_entry *entry)
{
	struct nf_hook_state *state = &entry->state;

	/* Release those devices we held, or Alexey will kill me. */
	if (state->in)
		dev_put(state->in);
	if (state->out)
		dev_put(state->out);
	if (state->sk)
		sock_put(state->sk);

	nf_queue_entry_release_br_nf_refs(entry->skb);
}
EXPORT_SYMBOL_GPL(nf_queue_entry_release_refs);

static void nf_queue_entry_get_br_nf_refs(struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	struct nf_bridge_info *nf_bridge = nf_bridge_info_get(skb);

	if (nf_bridge) {
		struct net_device *physdev;

		physdev = nf_bridge_get_physindev(skb);
		if (physdev)
			dev_hold(physdev);
		physdev = nf_bridge_get_physoutdev(skb);
		if (physdev)
			dev_hold(physdev);
	}
#endif
}

/* Bump dev refs so they don't vanish while packet is out */
void nf_queue_entry_get_refs(struct nf_queue_entry *entry)
{
	struct nf_hook_state *state = &entry->state;

	if (state->in)
		dev_hold(state->in);
	if (state->out)
		dev_hold(state->out);
	if (state->sk)
		sock_hold(state->sk);

	nf_queue_entry_get_br_nf_refs(entry->skb);
}
EXPORT_SYMBOL_GPL(nf_queue_entry_get_refs);

void nf_queue_nf_hook_drop(struct net *net)
{
	const struct nf_queue_handler *qh;

	rcu_read_lock();
	qh = rcu_dereference(net->nf.queue_handler);
	if (qh)
		qh->nf_hook_drop(net);
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(nf_queue_nf_hook_drop);

static void nf_ip_saveroute(const struct sk_buff *skb,
			    struct nf_queue_entry *entry)
{
	struct ip_rt_info *rt_info = nf_queue_entry_reroute(entry);

	if (entry->state.hook == NF_INET_LOCAL_OUT) {
		const struct iphdr *iph = ip_hdr(skb);

		rt_info->tos = iph->tos;
		rt_info->daddr = iph->daddr;
		rt_info->saddr = iph->saddr;
		rt_info->mark = skb->mark;
	}
}

static void nf_ip6_saveroute(const struct sk_buff *skb,
			     struct nf_queue_entry *entry)
{
	struct ip6_rt_info *rt_info = nf_queue_entry_reroute(entry);

	if (entry->state.hook == NF_INET_LOCAL_OUT) {
		const struct ipv6hdr *iph = ipv6_hdr(skb);

		rt_info->daddr = iph->daddr;
		rt_info->saddr = iph->saddr;
		rt_info->mark = skb->mark;
	}
}

static int __nf_queue(struct sk_buff *skb, const struct nf_hook_state *state,
		      unsigned int index, unsigned int queuenum)
{
	int status = -ENOENT;
	struct nf_queue_entry *entry = NULL;
	const struct nf_queue_handler *qh;
	struct net *net = state->net;
	unsigned int route_key_size;

	/* QUEUE == DROP if no one is waiting, to be safe. */
	qh = rcu_dereference(net->nf.queue_handler);
	if (!qh) {
		status = -ESRCH;
		goto err;
	}

	switch (state->pf) {
	case AF_INET:
		route_key_size = sizeof(struct ip_rt_info);
		break;
	case AF_INET6:
		route_key_size = sizeof(struct ip6_rt_info);
		break;
	default:
		route_key_size = 0;
		break;
	}

	entry = kmalloc(sizeof(*entry) + route_key_size, GFP_ATOMIC);
	if (!entry) {
		status = -ENOMEM;
		goto err;
	}

	if (skb_dst(skb) && !skb_dst_force(skb)) {
		status = -ENETDOWN;
		goto err;
	}

	*entry = (struct nf_queue_entry) {
		.skb	= skb,
		.state	= *state,
		.hook_index = index,
		.size	= sizeof(*entry) + route_key_size,
	};

	nf_queue_entry_get_refs(entry);

	switch (entry->state.pf) {
	case AF_INET:
		nf_ip_saveroute(skb, entry);
		break;
	case AF_INET6:
		nf_ip6_saveroute(skb, entry);
		break;
	}

	status = qh->outfn(entry, queuenum);

	if (status < 0) {
		nf_queue_entry_release_refs(entry);
		goto err;
	}

	return 0;

err:
	kfree(entry);
	return status;
}

/* Packets leaving via this function must come back through nf_reinject(). */
int nf_queue(struct sk_buff *skb, struct nf_hook_state *state,
	     unsigned int index, unsigned int verdict)
{
	int ret;

	ret = __nf_queue(skb, state, index, verdict >> NF_VERDICT_QBITS);
	if (ret < 0) {
		if (ret == -ESRCH &&
		    (verdict & NF_VERDICT_FLAG_QUEUE_BYPASS))
			return 1;
		kfree_skb(skb);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nf_queue);

static unsigned int nf_iterate(struct sk_buff *skb,
			       struct nf_hook_state *state,
			       const struct nf_hook_entries *hooks,
			       unsigned int *index)
{
	const struct nf_hook_entry *hook;
	unsigned int verdict, i = *index;

	while (i < hooks->num_hook_entries) {
		hook = &hooks->hooks[i];
repeat:
		verdict = nf_hook_entry_hookfn(hook, skb, state);
		if (verdict != NF_ACCEPT) {
			*index = i;
			if (verdict != NF_REPEAT)
				return verdict;
			goto repeat;
		}
		i++;
	}

	*index = i;
	return NF_ACCEPT;
}

static struct nf_hook_entries *nf_hook_entries_head(const struct net *net, u8 pf, u8 hooknum)
{
	switch (pf) {
#ifdef CONFIG_NETFILTER_FAMILY_BRIDGE
	case NFPROTO_BRIDGE:
		return rcu_dereference(net->nf.hooks_bridge[hooknum]);
#endif
	case NFPROTO_IPV4:
		return rcu_dereference(net->nf.hooks_ipv4[hooknum]);
	case NFPROTO_IPV6:
		return rcu_dereference(net->nf.hooks_ipv6[hooknum]);
	default:
		WARN_ON_ONCE(1);
		return NULL;
	}

	return NULL;
}

/* Caller must hold rcu read-side lock */
void nf_reinject(struct nf_queue_entry *entry, unsigned int verdict)
{
	const struct nf_hook_entry *hook_entry;
	const struct nf_hook_entries *hooks;
	struct sk_buff *skb = entry->skb;
	const struct net *net;
	unsigned int i;
	int err;
	u8 pf;

	net = entry->state.net;
	pf = entry->state.pf;

	hooks = nf_hook_entries_head(net, pf, entry->state.hook);

	nf_queue_entry_release_refs(entry);

	i = entry->hook_index;
	if (WARN_ON_ONCE(!hooks || i >= hooks->num_hook_entries)) {
		kfree_skb(skb);
		kfree(entry);
		return;
	}

	hook_entry = &hooks->hooks[i];

	/* Continue traversal iff userspace said ok... */
	if (verdict == NF_REPEAT)
		verdict = nf_hook_entry_hookfn(hook_entry, skb, &entry->state);

	if (verdict == NF_ACCEPT) {
		if (nf_reroute(skb, entry) < 0)
			verdict = NF_DROP;
	}

	if (verdict == NF_ACCEPT) {
next_hook:
		++i;
		verdict = nf_iterate(skb, &entry->state, hooks, &i);
	}

	switch (verdict & NF_VERDICT_MASK) {
	case NF_ACCEPT:
	case NF_STOP:
		local_bh_disable();
		entry->state.okfn(entry->state.net, entry->state.sk, skb);
		local_bh_enable();
		break;
	case NF_QUEUE:
		err = nf_queue(skb, &entry->state, i, verdict);
		if (err == 1)
			goto next_hook;
		break;
	case NF_STOLEN:
		break;
	default:
		kfree_skb(skb);
	}

	kfree(entry);
}
EXPORT_SYMBOL(nf_reinject);
