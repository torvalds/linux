/* netfilter.c: look after the filters for various protocols. 
 * Heavily influenced by the old firewall.c by David Bonn and Alan Cox.
 *
 * Thanks to Rob `CmdrTaco' Malda for not influencing this code in any
 * way.
 *
 * Rusty Russell (C)2000 -- This code is GPL.
 *
 * February 2000: Modified by James Morris to have 1 queue per protocol.
 * 15-Mar-2000:   Added NF_REPEAT --RR.
 * 08-May-2003:	  Internal logging interface added by Jozsef Kadlecsik.
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
#include <linux/inetdevice.h>
#include <linux/proc_fs.h>
#include <net/sock.h>

#include "nf_internals.h"

static DEFINE_SPINLOCK(afinfo_lock);

struct nf_afinfo *nf_afinfo[NPROTO];
EXPORT_SYMBOL(nf_afinfo);

int nf_register_afinfo(struct nf_afinfo *afinfo)
{
	spin_lock(&afinfo_lock);
	rcu_assign_pointer(nf_afinfo[afinfo->family], afinfo);
	spin_unlock(&afinfo_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(nf_register_afinfo);

void nf_unregister_afinfo(struct nf_afinfo *afinfo)
{
	spin_lock(&afinfo_lock);
	rcu_assign_pointer(nf_afinfo[afinfo->family], NULL);
	spin_unlock(&afinfo_lock);
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(nf_unregister_afinfo);

/* In this code, we can be waiting indefinitely for userspace to
 * service a packet if a hook returns NF_QUEUE.  We could keep a count
 * of skbuffs queued for userspace, and not deregister a hook unless
 * this is zero, but that sucks.  Now, we simply check when the
 * packets come back: if the hook is gone, the packet is discarded. */
struct list_head nf_hooks[NPROTO][NF_MAX_HOOKS];
EXPORT_SYMBOL(nf_hooks);
static DEFINE_SPINLOCK(nf_hook_lock);

int nf_register_hook(struct nf_hook_ops *reg)
{
	struct list_head *i;

	spin_lock_bh(&nf_hook_lock);
	list_for_each(i, &nf_hooks[reg->pf][reg->hooknum]) {
		if (reg->priority < ((struct nf_hook_ops *)i)->priority)
			break;
	}
	list_add_rcu(&reg->list, i->prev);
	spin_unlock_bh(&nf_hook_lock);

	synchronize_net();
	return 0;
}
EXPORT_SYMBOL(nf_register_hook);

void nf_unregister_hook(struct nf_hook_ops *reg)
{
	spin_lock_bh(&nf_hook_lock);
	list_del_rcu(&reg->list);
	spin_unlock_bh(&nf_hook_lock);

	synchronize_net();
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

void nf_unregister_hooks(struct nf_hook_ops *reg, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++)
		nf_unregister_hook(&reg[i]);
}
EXPORT_SYMBOL(nf_unregister_hooks);

unsigned int nf_iterate(struct list_head *head,
			struct sk_buff **skb,
			int hook,
			const struct net_device *indev,
			const struct net_device *outdev,
			struct list_head **i,
			int (*okfn)(struct sk_buff *),
			int hook_thresh)
{
	unsigned int verdict;

	/*
	 * The caller must not block between calls to this
	 * function because of risk of continuing from deleted element.
	 */
	list_for_each_continue_rcu(*i, head) {
		struct nf_hook_ops *elem = (struct nf_hook_ops *)*i;

		if (hook_thresh > elem->priority)
			continue;

		/* Optimization: we don't need to hold module
                   reference here, since function can't sleep. --RR */
		verdict = elem->hook(hook, skb, indev, outdev, okfn);
		if (verdict != NF_ACCEPT) {
#ifdef CONFIG_NETFILTER_DEBUG
			if (unlikely((verdict & NF_VERDICT_MASK)
							> NF_MAX_VERDICT)) {
				NFDEBUG("Evil return from %p(%u).\n",
				        elem->hook, hook);
				continue;
			}
#endif
			if (verdict != NF_REPEAT)
				return verdict;
			*i = (*i)->prev;
		}
	}
	return NF_ACCEPT;
}


/* Returns 1 if okfn() needs to be executed by the caller,
 * -EPERM for NF_DROP, 0 otherwise. */
int nf_hook_slow(int pf, unsigned int hook, struct sk_buff **pskb,
		 struct net_device *indev,
		 struct net_device *outdev,
		 int (*okfn)(struct sk_buff *),
		 int hook_thresh)
{
	struct list_head *elem;
	unsigned int verdict;
	int ret = 0;

	/* We may already have this, but read-locks nest anyway */
	rcu_read_lock();

	elem = &nf_hooks[pf][hook];
next_hook:
	verdict = nf_iterate(&nf_hooks[pf][hook], pskb, hook, indev,
			     outdev, &elem, okfn, hook_thresh);
	if (verdict == NF_ACCEPT || verdict == NF_STOP) {
		ret = 1;
		goto unlock;
	} else if (verdict == NF_DROP) {
		kfree_skb(*pskb);
		ret = -EPERM;
	} else if ((verdict & NF_VERDICT_MASK)  == NF_QUEUE) {
		NFDEBUG("nf_hook: Verdict = QUEUE.\n");
		if (!nf_queue(*pskb, elem, pf, hook, indev, outdev, okfn,
			      verdict >> NF_VERDICT_BITS))
			goto next_hook;
	}
unlock:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(nf_hook_slow);


int skb_make_writable(struct sk_buff **pskb, unsigned int writable_len)
{
	struct sk_buff *nskb;

	if (writable_len > (*pskb)->len)
		return 0;

	/* Not exclusive use of packet?  Must copy. */
	if (skb_shared(*pskb) || skb_cloned(*pskb))
		goto copy_skb;

	return pskb_may_pull(*pskb, writable_len);

copy_skb:
	nskb = skb_copy(*pskb, GFP_ATOMIC);
	if (!nskb)
		return 0;
	BUG_ON(skb_is_nonlinear(nskb));

	/* Rest of kernel will get very unhappy if we pass it a
	   suddenly-orphaned skbuff */
	if ((*pskb)->sk)
		skb_set_owner_w(nskb, (*pskb)->sk);
	kfree_skb(*pskb);
	*pskb = nskb;
	return 1;
}
EXPORT_SYMBOL(skb_make_writable);

void nf_proto_csum_replace4(__sum16 *sum, struct sk_buff *skb,
			    __be32 from, __be32 to, int pseudohdr)
{
	__be32 diff[] = { ~from, to };
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		*sum = csum_fold(csum_partial((char *)diff, sizeof(diff),
				~csum_unfold(*sum)));
		if (skb->ip_summed == CHECKSUM_COMPLETE && pseudohdr)
			skb->csum = ~csum_partial((char *)diff, sizeof(diff),
						~skb->csum);
	} else if (pseudohdr)
		*sum = ~csum_fold(csum_partial((char *)diff, sizeof(diff),
				csum_unfold(*sum)));
}
EXPORT_SYMBOL(nf_proto_csum_replace4);

/* This does not belong here, but locally generated errors need it if connection
   tracking in use: without this, connection may not be in hash table, and hence
   manufactured ICMP or RST packets will not be associated with it. */
void (*ip_ct_attach)(struct sk_buff *, struct sk_buff *);
EXPORT_SYMBOL(ip_ct_attach);

void nf_ct_attach(struct sk_buff *new, struct sk_buff *skb)
{
	void (*attach)(struct sk_buff *, struct sk_buff *);

	if (skb->nfct && (attach = ip_ct_attach) != NULL) {
		mb(); /* Just to be sure: must be read before executing this */
		attach(new, skb);
	}
}
EXPORT_SYMBOL(nf_ct_attach);

#ifdef CONFIG_PROC_FS
struct proc_dir_entry *proc_net_netfilter;
EXPORT_SYMBOL(proc_net_netfilter);
#endif

void __init netfilter_init(void)
{
	int i, h;
	for (i = 0; i < NPROTO; i++) {
		for (h = 0; h < NF_MAX_HOOKS; h++)
			INIT_LIST_HEAD(&nf_hooks[i][h]);
	}

#ifdef CONFIG_PROC_FS
	proc_net_netfilter = proc_mkdir("netfilter", proc_net);
	if (!proc_net_netfilter)
		panic("cannot create netfilter proc entry");
#endif

	if (netfilter_queue_init() < 0)
		panic("cannot initialize nf_queue");
	if (netfilter_log_init() < 0)
		panic("cannot initialize nf_log");
}
