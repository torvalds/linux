#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>
#include <net/protocol.h>

#include "nf_internals.h"

/*
 * A queue handler may be registered for each protocol.  Each is protected by
 * long term mutex.  The handler must provide an an outfn() to accept packets
 * for queueing and must reinject all packets it receives, no matter what.
 */
static struct nf_queue_handler *queue_handler[NPROTO];

static DEFINE_RWLOCK(queue_handler_lock);

/* return EBUSY when somebody else is registered, return EEXIST if the
 * same handler is registered, return 0 in case of success. */
int nf_register_queue_handler(int pf, struct nf_queue_handler *qh)
{
	int ret;

	if (pf >= NPROTO)
		return -EINVAL;

	write_lock_bh(&queue_handler_lock);
	if (queue_handler[pf] == qh)
		ret = -EEXIST;
	else if (queue_handler[pf])
		ret = -EBUSY;
	else {
		queue_handler[pf] = qh;
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
	queue_handler[pf] = NULL;
	write_unlock_bh(&queue_handler_lock);

	return 0;
}
EXPORT_SYMBOL(nf_unregister_queue_handler);

void nf_unregister_queue_handlers(struct nf_queue_handler *qh)
{
	int pf;

	write_lock_bh(&queue_handler_lock);
	for (pf = 0; pf < NPROTO; pf++)  {
		if (queue_handler[pf] == qh)
			queue_handler[pf] = NULL;
	}
	write_unlock_bh(&queue_handler_lock);
}
EXPORT_SYMBOL_GPL(nf_unregister_queue_handlers);

/*
 * Any packet that leaves via this function must come back
 * through nf_reinject().
 */
static int __nf_queue(struct sk_buff *skb,
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
	struct nf_afinfo *afinfo;

	/* QUEUE == DROP if noone is waiting, to be safe. */
	read_lock(&queue_handler_lock);
	if (!queue_handler[pf]) {
		read_unlock(&queue_handler_lock);
		kfree_skb(skb);
		return 1;
	}

	afinfo = nf_get_afinfo(pf);
	if (!afinfo) {
		read_unlock(&queue_handler_lock);
		kfree_skb(skb);
		return 1;
	}

	info = kmalloc(sizeof(*info) + afinfo->route_key_size, GFP_ATOMIC);
	if (!info) {
		if (net_ratelimit())
			printk(KERN_ERR "OOM queueing packet %p\n",
			       skb);
		read_unlock(&queue_handler_lock);
		kfree_skb(skb);
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
	if (skb->nf_bridge) {
		physindev = skb->nf_bridge->physindev;
		if (physindev) dev_hold(physindev);
		physoutdev = skb->nf_bridge->physoutdev;
		if (physoutdev) dev_hold(physoutdev);
	}
#endif
	afinfo->saveroute(skb, info);
	status = queue_handler[pf]->outfn(skb, info, queuenum,
					  queue_handler[pf]->data);

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
		kfree_skb(skb);

		return 1;
	}

	return 1;
}

int nf_queue(struct sk_buff *skb,
	     struct list_head *elem,
	     int pf, unsigned int hook,
	     struct net_device *indev,
	     struct net_device *outdev,
	     int (*okfn)(struct sk_buff *),
	     unsigned int queuenum)
{
	struct sk_buff *segs;

	if (!skb_is_gso(skb))
		return __nf_queue(skb, elem, pf, hook, indev, outdev, okfn,
				  queuenum);

	switch (pf) {
	case AF_INET:
		skb->protocol = htons(ETH_P_IP);
		break;
	case AF_INET6:
		skb->protocol = htons(ETH_P_IPV6);
		break;
	}

	segs = skb_gso_segment(skb, 0);
	kfree_skb(skb);
	if (unlikely(IS_ERR(segs)))
		return 1;

	do {
		struct sk_buff *nskb = segs->next;

		segs->next = NULL;
		if (!__nf_queue(segs, elem, pf, hook, indev, outdev, okfn,
				queuenum))
			kfree_skb(segs);
		segs = nskb;
	} while (segs);
	return 1;
}

void nf_reinject(struct sk_buff *skb, struct nf_info *info,
		 unsigned int verdict)
{
	struct list_head *elem = &info->elem->list;
	struct list_head *i;
	struct nf_afinfo *afinfo;

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

	if (i == &nf_hooks[info->pf][info->hook]) {
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
		afinfo = nf_get_afinfo(info->pf);
		if (!afinfo || afinfo->reroute(&skb, info) < 0)
			verdict = NF_DROP;
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
	case NF_STOP:
		info->okfn(skb);
	case NF_STOLEN:
		break;
	case NF_QUEUE:
		if (!__nf_queue(skb, elem, info->pf, info->hook,
				info->indev, info->outdev, info->okfn,
				verdict >> NF_VERDICT_BITS))
			goto next_hook;
		break;
	default:
		kfree_skb(skb);
	}
	rcu_read_unlock();
	kfree(info);
	return;
}
EXPORT_SYMBOL(nf_reinject);

#ifdef CONFIG_PROC_FS
static void *seq_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos >= NPROTO)
		return NULL;

	return pos;
}

static void *seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;

	if (*pos >= NPROTO)
		return NULL;

	return pos;
}

static void seq_stop(struct seq_file *s, void *v)
{

}

static int seq_show(struct seq_file *s, void *v)
{
	int ret;
	loff_t *pos = v;
	struct nf_queue_handler *qh;

	read_lock_bh(&queue_handler_lock);
	qh = queue_handler[*pos];
	if (!qh)
		ret = seq_printf(s, "%2lld NONE\n", *pos);
	else
		ret = seq_printf(s, "%2lld %s\n", *pos, qh->name);
	read_unlock_bh(&queue_handler_lock);

	return ret;
}

static struct seq_operations nfqueue_seq_ops = {
	.start	= seq_start,
	.next	= seq_next,
	.stop	= seq_stop,
	.show	= seq_show,
};

static int nfqueue_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &nfqueue_seq_ops);
}

static const struct file_operations nfqueue_file_ops = {
	.owner	 = THIS_MODULE,
	.open	 = nfqueue_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};
#endif /* PROC_FS */


int __init netfilter_queue_init(void)
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *pde;

	pde = create_proc_entry("nf_queue", S_IRUGO, proc_net_netfilter);
	if (!pde)
		return -1;
	pde->proc_fops = &nfqueue_file_ops;
#endif
	return 0;
}

