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
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	if (entry->skb->nf_bridge) {
		struct net_device *physdev;

		physdev = nf_bridge_get_physindev(entry->skb);
		if (physdev)
			dev_put(physdev);
		physdev = nf_bridge_get_physoutdev(entry->skb);
		if (physdev)
			dev_put(physdev);
	}
#endif
}
EXPORT_SYMBOL_GPL(nf_queue_entry_release_refs);

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
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	if (entry->skb->nf_bridge) {
		struct net_device *physdev;

		physdev = nf_bridge_get_physindev(entry->skb);
		if (physdev)
			dev_hold(physdev);
		physdev = nf_bridge_get_physoutdev(entry->skb);
		if (physdev)
			dev_hold(physdev);
	}
#endif
}
EXPORT_SYMBOL_GPL(nf_queue_entry_get_refs);

void nf_queue_nf_hook_drop(struct net *net, const struct nf_hook_entry *entry)
{
	const struct nf_queue_handler *qh;

	rcu_read_lock();
	qh = rcu_dereference(net->nf.queue_handler);
	if (qh)
		qh->nf_hook_drop(net, entry);
	rcu_read_unlock();
}

static int __nf_queue(struct sk_buff *skb, const struct nf_hook_state *state,
		      struct nf_hook_entry *hook_entry, unsigned int queuenum)
{
	int status = -ENOENT;
	struct nf_queue_entry *entry = NULL;
	const struct nf_afinfo *afinfo;
	const struct nf_queue_handler *qh;
	struct net *net = state->net;

	/* QUEUE == DROP if no one is waiting, to be safe. */
	qh = rcu_dereference(net->nf.queue_handler);
	if (!qh) {
		status = -ESRCH;
		goto err;
	}

	afinfo = nf_get_afinfo(state->pf);
	if (!afinfo)
		goto err;

	entry = kmalloc(sizeof(*entry) + afinfo->route_key_size, GFP_ATOMIC);
	if (!entry) {
		status = -ENOMEM;
		goto err;
	}

	*entry = (struct nf_queue_entry) {
		.skb	= skb,
		.state	= *state,
		.hook	= hook_entry,
		.size	= sizeof(*entry) + afinfo->route_key_size,
	};

	nf_queue_entry_get_refs(entry);
	skb_dst_force(skb);
	afinfo->saveroute(skb, entry);
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
	     struct nf_hook_entry **entryp, unsigned int verdict)
{
	struct nf_hook_entry *entry = *entryp;
	int ret;

	ret = __nf_queue(skb, state, entry, verdict >> NF_VERDICT_QBITS);
	if (ret < 0) {
		if (ret == -ESRCH &&
		    (verdict & NF_VERDICT_FLAG_QUEUE_BYPASS)) {
			*entryp = rcu_dereference(entry->next);
			return 1;
		}
		kfree_skb(skb);
	}

	return 0;
}

static unsigned int nf_iterate(struct sk_buff *skb,
			       struct nf_hook_state *state,
			       struct nf_hook_entry **entryp)
{
	unsigned int verdict;

	do {
repeat:
		verdict = (*entryp)->ops.hook((*entryp)->ops.priv, skb, state);
		if (verdict != NF_ACCEPT) {
			if (verdict != NF_REPEAT)
				return verdict;
			goto repeat;
		}
		*entryp = rcu_dereference((*entryp)->next);
	} while (*entryp);

	return NF_ACCEPT;
}

void nf_reinject(struct nf_queue_entry *entry, unsigned int verdict)
{
	struct nf_hook_entry *hook_entry = entry->hook;
	struct nf_hook_ops *elem = &hook_entry->ops;
	struct sk_buff *skb = entry->skb;
	const struct nf_afinfo *afinfo;
	int err;

	nf_queue_entry_release_refs(entry);

	/* Continue traversal iff userspace said ok... */
	if (verdict == NF_REPEAT)
		verdict = elem->hook(elem->priv, skb, &entry->state);

	if (verdict == NF_ACCEPT) {
		afinfo = nf_get_afinfo(entry->state.pf);
		if (!afinfo || afinfo->reroute(entry->state.net, skb, entry) < 0)
			verdict = NF_DROP;
	}

	if (verdict == NF_ACCEPT) {
		hook_entry = rcu_dereference(hook_entry->next);
		if (hook_entry)
next_hook:
			verdict = nf_iterate(skb, &entry->state, &hook_entry);
	}

	switch (verdict & NF_VERDICT_MASK) {
	case NF_ACCEPT:
	case NF_STOP:
okfn:
		local_bh_disable();
		entry->state.okfn(entry->state.net, entry->state.sk, skb);
		local_bh_enable();
		break;
	case NF_QUEUE:
		err = nf_queue(skb, &entry->state, &hook_entry, verdict);
		if (err == 1) {
			if (hook_entry)
				goto next_hook;
			goto okfn;
		}
		break;
	case NF_STOLEN:
		break;
	default:
		kfree_skb(skb);
	}

	kfree(entry);
}
EXPORT_SYMBOL(nf_reinject);
