#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <net/protocol.h>

#include "nf_internals.h"

/* 
 * A queue handler may be registered for each protocol.  Each is protected by
 * long term mutex.  The handler must provide an an outfn() to accept packets
 * for queueing and must reinject all packets it receives, no matter what.
 */
static struct nf_queue_handler_t {
	nf_queue_outfn_t outfn;
	void *data;
} queue_handler[NPROTO];

static struct nf_queue_rerouter *queue_rerouter;

static DEFINE_RWLOCK(queue_handler_lock);


int nf_register_queue_handler(int pf, nf_queue_outfn_t outfn, void *data)
{      
	int ret;

	if (pf >= NPROTO)
		return -EINVAL;

	write_lock_bh(&queue_handler_lock);
	if (queue_handler[pf].outfn)
		ret = -EBUSY;
	else {
		queue_handler[pf].outfn = outfn;
		queue_handler[pf].data = data;
		ret = 0;
	}
	write_unlock_bh(&queue_handler_lock);

	return ret;
}
EXPORT_SYMBOL(nf_register_queue_handler);

/* The caller must flush their queue before this */
int nf_unregister_queue_handler(int pf)
{
	if (pf >= NPROTO)
		return -EINVAL;

	write_lock_bh(&queue_handler_lock);
	queue_handler[pf].outfn = NULL;
	queue_handler[pf].data = NULL;
	write_unlock_bh(&queue_handler_lock);
	
	return 0;
}
EXPORT_SYMBOL(nf_unregister_queue_handler);

int nf_register_queue_rerouter(int pf, struct nf_queue_rerouter *rer)
{
	if (pf >= NPROTO)
		return -EINVAL;

	write_lock_bh(&queue_handler_lock);
	memcpy(&queue_rerouter[pf], rer, sizeof(queue_rerouter[pf]));
	write_unlock_bh(&queue_handler_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(nf_register_queue_rerouter);

int nf_unregister_queue_rerouter(int pf)
{
	if (pf >= NPROTO)
		return -EINVAL;

	write_lock_bh(&queue_handler_lock);
	memset(&queue_rerouter[pf], 0, sizeof(queue_rerouter[pf]));
	write_unlock_bh(&queue_handler_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(nf_unregister_queue_rerouter);

void nf_unregister_queue_handlers(nf_queue_outfn_t outfn)
{
	int pf;

	write_lock_bh(&queue_handler_lock);
	for (pf = 0; pf < NPROTO; pf++)  {
		if (queue_handler[pf].outfn == outfn) {
			queue_handler[pf].outfn = NULL;
			queue_handler[pf].data = NULL;
		}
	}
	write_unlock_bh(&queue_handler_lock);
}
EXPORT_SYMBOL_GPL(nf_unregister_queue_handlers);

/* 
 * Any packet that leaves via this function must come back 
 * through nf_reinject().
 */
int nf_queue(struct sk_buff **skb, 
	     struct list_head *elem, 
	     int pf, unsigned int hook,
	     struct net_device *indev,
	     struct net_device *outdev,
	     int (*okfn)(struct sk_buff *),
	     unsigned int queuenum)
{
	int status;
	struct nf_info *info;
#ifdef CONFIG_BRIDGE_NETFILTER
	struct net_device *physindev = NULL;
	struct net_device *physoutdev = NULL;
#endif

	/* QUEUE == DROP if noone is waiting, to be safe. */
	read_lock(&queue_handler_lock);
	if (!queue_handler[pf].outfn) {
		read_unlock(&queue_handler_lock);
		kfree_skb(*skb);
		return 1;
	}

	info = kmalloc(sizeof(*info)+queue_rerouter[pf].rer_size, GFP_ATOMIC);
	if (!info) {
		if (net_ratelimit())
			printk(KERN_ERR "OOM queueing packet %p\n",
			       *skb);
		read_unlock(&queue_handler_lock);
		kfree_skb(*skb);
		return 1;
	}

	*info = (struct nf_info) { 
		(struct nf_hook_ops *)elem, pf, hook, indev, outdev, okfn };

	/* If it's going away, ignore hook. */
	if (!try_module_get(info->elem->owner)) {
		read_unlock(&queue_handler_lock);
		kfree(info);
		return 0;
	}

	/* Bump dev refs so they don't vanish while packet is out */
	if (indev) dev_hold(indev);
	if (outdev) dev_hold(outdev);

#ifdef CONFIG_BRIDGE_NETFILTER
	if ((*skb)->nf_bridge) {
		physindev = (*skb)->nf_bridge->physindev;
		if (physindev) dev_hold(physindev);
		physoutdev = (*skb)->nf_bridge->physoutdev;
		if (physoutdev) dev_hold(physoutdev);
	}
#endif
	if (queue_rerouter[pf].save)
		queue_rerouter[pf].save(*skb, info);

	status = queue_handler[pf].outfn(*skb, info, queuenum,
					 queue_handler[pf].data);

	if (status >= 0 && queue_rerouter[pf].reroute)
		status = queue_rerouter[pf].reroute(skb, info);

	read_unlock(&queue_handler_lock);

	if (status < 0) {
		/* James M doesn't say fuck enough. */
		if (indev) dev_put(indev);
		if (outdev) dev_put(outdev);
#ifdef CONFIG_BRIDGE_NETFILTER
		if (physindev) dev_put(physindev);
		if (physoutdev) dev_put(physoutdev);
#endif
		module_put(info->elem->owner);
		kfree(info);
		kfree_skb(*skb);

		return 1;
	}

	return 1;
}

void nf_reinject(struct sk_buff *skb, struct nf_info *info,
		 unsigned int verdict)
{
	struct list_head *elem = &info->elem->list;
	struct list_head *i;

	rcu_read_lock();

	/* Release those devices we held, or Alexey will kill me. */
	if (info->indev) dev_put(info->indev);
	if (info->outdev) dev_put(info->outdev);
#ifdef CONFIG_BRIDGE_NETFILTER
	if (skb->nf_bridge) {
		if (skb->nf_bridge->physindev)
			dev_put(skb->nf_bridge->physindev);
		if (skb->nf_bridge->physoutdev)
			dev_put(skb->nf_bridge->physoutdev);
	}
#endif

	/* Drop reference to owner of hook which queued us. */
	module_put(info->elem->owner);

	list_for_each_rcu(i, &nf_hooks[info->pf][info->hook]) {
		if (i == elem) 
  			break;
  	}
  
	if (elem == &nf_hooks[info->pf][info->hook]) {
		/* The module which sent it to userspace is gone. */
		NFDEBUG("%s: module disappeared, dropping packet.\n",
			__FUNCTION__);
		verdict = NF_DROP;
	}

	/* Continue traversal iff userspace said ok... */
	if (verdict == NF_REPEAT) {
		elem = elem->prev;
		verdict = NF_ACCEPT;
	}

	if (verdict == NF_ACCEPT) {
	next_hook:
		verdict = nf_iterate(&nf_hooks[info->pf][info->hook],
				     &skb, info->hook, 
				     info->indev, info->outdev, &elem,
				     info->okfn, INT_MIN);
	}

	switch (verdict & NF_VERDICT_MASK) {
	case NF_ACCEPT:
		info->okfn(skb);
		break;

	case NF_QUEUE:
		if (!nf_queue(&skb, elem, info->pf, info->hook, 
			      info->indev, info->outdev, info->okfn,
			      verdict >> NF_VERDICT_BITS))
			goto next_hook;
		break;
	}
	rcu_read_unlock();

	if (verdict == NF_DROP)
		kfree_skb(skb);

	kfree(info);
	return;
}
EXPORT_SYMBOL(nf_reinject);

int __init netfilter_queue_init(void)
{
	queue_rerouter = kmalloc(NPROTO * sizeof(struct nf_queue_rerouter),
				 GFP_KERNEL);
	if (!queue_rerouter)
		return -ENOMEM;

	memset(queue_rerouter, 0, NPROTO * sizeof(struct nf_queue_rerouter));

	return 0;
}

