/*
 * This is a module which is used for queueing packets and communicating with
 * userspace via nfetlink.
 *
 * (C) 2005 by Harald Welte <laforge@netfilter.org>
 *
 * Based on the old ipv4-only ip_queue.c:
 * (C) 2000-2002 James Morris <jmorris@intercode.com.au>
 * (C) 2003-2005 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/proc_fs.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_queue.h>
#include <linux/list.h>
#include <net/sock.h>

#include <asm/atomic.h>

#ifdef CONFIG_BRIDGE_NETFILTER
#include "../bridge/br_private.h"
#endif

#define NFQNL_QMAX_DEFAULT 1024

#if 0
#define QDEBUG(x, args ...)	printk(KERN_DEBUG "%s(%d):%s():	" x, 	   \
					__FILE__, __LINE__, __FUNCTION__,  \
					## args)
#else
#define QDEBUG(x, ...)
#endif

struct nfqnl_queue_entry {
	struct list_head list;
	struct nf_info *info;
	struct sk_buff *skb;
	unsigned int id;
};

struct nfqnl_instance {
	struct hlist_node hlist;		/* global list of queues */
	atomic_t use;

	int peer_pid;
	unsigned int queue_maxlen;
	unsigned int copy_range;
	unsigned int queue_total;
	unsigned int queue_dropped;
	unsigned int queue_user_dropped;

	atomic_t id_sequence;			/* 'sequence' of pkt ids */

	u_int16_t queue_num;			/* number of this queue */
	u_int8_t copy_mode;

	spinlock_t lock;

	struct list_head queue_list;		/* packets in queue */
};

typedef int (*nfqnl_cmpfn)(struct nfqnl_queue_entry *, unsigned long);

static DEFINE_RWLOCK(instances_lock);

#define INSTANCE_BUCKETS	16
static struct hlist_head instance_table[INSTANCE_BUCKETS];

static inline u_int8_t instance_hashfn(u_int16_t queue_num)
{
	return ((queue_num >> 8) | queue_num) % INSTANCE_BUCKETS;
}

static struct nfqnl_instance *
__instance_lookup(u_int16_t queue_num)
{
	struct hlist_head *head;
	struct hlist_node *pos;
	struct nfqnl_instance *inst;

	head = &instance_table[instance_hashfn(queue_num)];
	hlist_for_each_entry(inst, pos, head, hlist) {
		if (inst->queue_num == queue_num)
			return inst;
	}
	return NULL;
}

static struct nfqnl_instance *
instance_lookup_get(u_int16_t queue_num)
{
	struct nfqnl_instance *inst;

	read_lock_bh(&instances_lock);
	inst = __instance_lookup(queue_num);
	if (inst)
		atomic_inc(&inst->use);
	read_unlock_bh(&instances_lock);

	return inst;
}

static void
instance_put(struct nfqnl_instance *inst)
{
	if (inst && atomic_dec_and_test(&inst->use)) {
		QDEBUG("kfree(inst=%p)\n", inst);
		kfree(inst);
	}
}

static struct nfqnl_instance *
instance_create(u_int16_t queue_num, int pid)
{
	struct nfqnl_instance *inst;

	QDEBUG("entering for queue_num=%u, pid=%d\n", queue_num, pid);

	write_lock_bh(&instances_lock);	
	if (__instance_lookup(queue_num)) {
		inst = NULL;
		QDEBUG("aborting, instance already exists\n");
		goto out_unlock;
	}

	inst = kzalloc(sizeof(*inst), GFP_ATOMIC);
	if (!inst)
		goto out_unlock;

	inst->queue_num = queue_num;
	inst->peer_pid = pid;
	inst->queue_maxlen = NFQNL_QMAX_DEFAULT;
	inst->copy_range = 0xfffff;
	inst->copy_mode = NFQNL_COPY_NONE;
	atomic_set(&inst->id_sequence, 0);
	/* needs to be two, since we _put() after creation */
	atomic_set(&inst->use, 2);
	spin_lock_init(&inst->lock);
	INIT_LIST_HEAD(&inst->queue_list);

	if (!try_module_get(THIS_MODULE))
		goto out_free;

	hlist_add_head(&inst->hlist, 
		       &instance_table[instance_hashfn(queue_num)]);

	write_unlock_bh(&instances_lock);

	QDEBUG("successfully created new instance\n");

	return inst;

out_free:
	kfree(inst);
out_unlock:
	write_unlock_bh(&instances_lock);
	return NULL;
}

static void nfqnl_flush(struct nfqnl_instance *queue, int verdict);

static void
_instance_destroy2(struct nfqnl_instance *inst, int lock)
{
	/* first pull it out of the global list */
	if (lock)
		write_lock_bh(&instances_lock);

	QDEBUG("removing instance %p (queuenum=%u) from hash\n",
		inst, inst->queue_num);
	hlist_del(&inst->hlist);

	if (lock)
		write_unlock_bh(&instances_lock);

	/* then flush all pending skbs from the queue */
	nfqnl_flush(inst, NF_DROP);

	/* and finally put the refcount */
	instance_put(inst);

	module_put(THIS_MODULE);
}

static inline void
__instance_destroy(struct nfqnl_instance *inst)
{
	_instance_destroy2(inst, 0);
}

static inline void
instance_destroy(struct nfqnl_instance *inst)
{
	_instance_destroy2(inst, 1);
}



static void
issue_verdict(struct nfqnl_queue_entry *entry, int verdict)
{
	QDEBUG("entering for entry %p, verdict %u\n", entry, verdict);

	/* TCP input path (and probably other bits) assume to be called
	 * from softirq context, not from syscall, like issue_verdict is
	 * called.  TCP input path deadlocks with locks taken from timer
	 * softirq, e.g.  We therefore emulate this by local_bh_disable() */

	local_bh_disable();
	nf_reinject(entry->skb, entry->info, verdict);
	local_bh_enable();

	kfree(entry);
}

static inline void
__enqueue_entry(struct nfqnl_instance *queue,
		      struct nfqnl_queue_entry *entry)
{
       list_add(&entry->list, &queue->queue_list);
       queue->queue_total++;
}

/*
 * Find and return a queued entry matched by cmpfn, or return the last
 * entry if cmpfn is NULL.
 */
static inline struct nfqnl_queue_entry *
__find_entry(struct nfqnl_instance *queue, nfqnl_cmpfn cmpfn, 
		   unsigned long data)
{
	struct list_head *p;

	list_for_each_prev(p, &queue->queue_list) {
		struct nfqnl_queue_entry *entry = (struct nfqnl_queue_entry *)p;
		
		if (!cmpfn || cmpfn(entry, data))
			return entry;
	}
	return NULL;
}

static inline void
__dequeue_entry(struct nfqnl_instance *q, struct nfqnl_queue_entry *entry)
{
	list_del(&entry->list);
	q->queue_total--;
}

static inline struct nfqnl_queue_entry *
__find_dequeue_entry(struct nfqnl_instance *queue,
		     nfqnl_cmpfn cmpfn, unsigned long data)
{
	struct nfqnl_queue_entry *entry;

	entry = __find_entry(queue, cmpfn, data);
	if (entry == NULL)
		return NULL;

	__dequeue_entry(queue, entry);
	return entry;
}


static inline void
__nfqnl_flush(struct nfqnl_instance *queue, int verdict)
{
	struct nfqnl_queue_entry *entry;
	
	while ((entry = __find_dequeue_entry(queue, NULL, 0)))
		issue_verdict(entry, verdict);
}

static inline int
__nfqnl_set_mode(struct nfqnl_instance *queue,
		 unsigned char mode, unsigned int range)
{
	int status = 0;
	
	switch (mode) {
	case NFQNL_COPY_NONE:
	case NFQNL_COPY_META:
		queue->copy_mode = mode;
		queue->copy_range = 0;
		break;
		
	case NFQNL_COPY_PACKET:
		queue->copy_mode = mode;
		/* we're using struct nfattr which has 16bit nfa_len */
		if (range > 0xffff)
			queue->copy_range = 0xffff;
		else
			queue->copy_range = range;
		break;
		
	default:
		status = -EINVAL;

	}
	return status;
}

static struct nfqnl_queue_entry *
find_dequeue_entry(struct nfqnl_instance *queue,
			 nfqnl_cmpfn cmpfn, unsigned long data)
{
	struct nfqnl_queue_entry *entry;
	
	spin_lock_bh(&queue->lock);
	entry = __find_dequeue_entry(queue, cmpfn, data);
	spin_unlock_bh(&queue->lock);

	return entry;
}

static void
nfqnl_flush(struct nfqnl_instance *queue, int verdict)
{
	spin_lock_bh(&queue->lock);
	__nfqnl_flush(queue, verdict);
	spin_unlock_bh(&queue->lock);
}

static struct sk_buff *
nfqnl_build_packet_message(struct nfqnl_instance *queue,
			   struct nfqnl_queue_entry *entry, int *errp)
{
	unsigned char *old_tail;
	size_t size;
	size_t data_len = 0;
	struct sk_buff *skb;
	struct nfqnl_msg_packet_hdr pmsg;
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct nf_info *entinf = entry->info;
	struct sk_buff *entskb = entry->skb;
	struct net_device *indev;
	struct net_device *outdev;
	__be32 tmp_uint;

	QDEBUG("entered\n");

	/* all macros expand to constant values at compile time */
	size =    NLMSG_SPACE(sizeof(struct nfgenmsg)) +
		+ NFA_SPACE(sizeof(struct nfqnl_msg_packet_hdr))
		+ NFA_SPACE(sizeof(u_int32_t))	/* ifindex */
		+ NFA_SPACE(sizeof(u_int32_t))	/* ifindex */
#ifdef CONFIG_BRIDGE_NETFILTER
		+ NFA_SPACE(sizeof(u_int32_t))	/* ifindex */
		+ NFA_SPACE(sizeof(u_int32_t))	/* ifindex */
#endif
		+ NFA_SPACE(sizeof(u_int32_t))	/* mark */
		+ NFA_SPACE(sizeof(struct nfqnl_msg_packet_hw))
		+ NFA_SPACE(sizeof(struct nfqnl_msg_packet_timestamp));

	outdev = entinf->outdev;

	spin_lock_bh(&queue->lock);
	
	switch (queue->copy_mode) {
	case NFQNL_COPY_META:
	case NFQNL_COPY_NONE:
		data_len = 0;
		break;
	
	case NFQNL_COPY_PACKET:
		if ((entskb->ip_summed == CHECKSUM_PARTIAL ||
		     entskb->ip_summed == CHECKSUM_COMPLETE) &&
		    (*errp = skb_checksum_help(entskb))) {
			spin_unlock_bh(&queue->lock);
			return NULL;
		}
		if (queue->copy_range == 0 
		    || queue->copy_range > entskb->len)
			data_len = entskb->len;
		else
			data_len = queue->copy_range;
		
		size += NFA_SPACE(data_len);
		break;
	
	default:
		*errp = -EINVAL;
		spin_unlock_bh(&queue->lock);
		return NULL;
	}

	spin_unlock_bh(&queue->lock);

	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb)
		goto nlmsg_failure;
		
	old_tail= skb->tail;
	nlh = NLMSG_PUT(skb, 0, 0, 
			NFNL_SUBSYS_QUEUE << 8 | NFQNL_MSG_PACKET,
			sizeof(struct nfgenmsg));
	nfmsg = NLMSG_DATA(nlh);
	nfmsg->nfgen_family = entinf->pf;
	nfmsg->version = NFNETLINK_V0;
	nfmsg->res_id = htons(queue->queue_num);

	pmsg.packet_id 		= htonl(entry->id);
	pmsg.hw_protocol	= entskb->protocol;
	pmsg.hook		= entinf->hook;

	NFA_PUT(skb, NFQA_PACKET_HDR, sizeof(pmsg), &pmsg);

	indev = entinf->indev;
	if (indev) {
		tmp_uint = htonl(indev->ifindex);
#ifndef CONFIG_BRIDGE_NETFILTER
		NFA_PUT(skb, NFQA_IFINDEX_INDEV, sizeof(tmp_uint), &tmp_uint);
#else
		if (entinf->pf == PF_BRIDGE) {
			/* Case 1: indev is physical input device, we need to
			 * look for bridge group (when called from 
			 * netfilter_bridge) */
			NFA_PUT(skb, NFQA_IFINDEX_PHYSINDEV, sizeof(tmp_uint), 
				&tmp_uint);
			/* this is the bridge group "brX" */
			tmp_uint = htonl(indev->br_port->br->dev->ifindex);
			NFA_PUT(skb, NFQA_IFINDEX_INDEV, sizeof(tmp_uint),
				&tmp_uint);
		} else {
			/* Case 2: indev is bridge group, we need to look for
			 * physical device (when called from ipv4) */
			NFA_PUT(skb, NFQA_IFINDEX_INDEV, sizeof(tmp_uint),
				&tmp_uint);
			if (entskb->nf_bridge
			    && entskb->nf_bridge->physindev) {
				tmp_uint = htonl(entskb->nf_bridge->physindev->ifindex);
				NFA_PUT(skb, NFQA_IFINDEX_PHYSINDEV,
					sizeof(tmp_uint), &tmp_uint);
			}
		}
#endif
	}

	if (outdev) {
		tmp_uint = htonl(outdev->ifindex);
#ifndef CONFIG_BRIDGE_NETFILTER
		NFA_PUT(skb, NFQA_IFINDEX_OUTDEV, sizeof(tmp_uint), &tmp_uint);
#else
		if (entinf->pf == PF_BRIDGE) {
			/* Case 1: outdev is physical output device, we need to
			 * look for bridge group (when called from 
			 * netfilter_bridge) */
			NFA_PUT(skb, NFQA_IFINDEX_PHYSOUTDEV, sizeof(tmp_uint),
				&tmp_uint);
			/* this is the bridge group "brX" */
			tmp_uint = htonl(outdev->br_port->br->dev->ifindex);
			NFA_PUT(skb, NFQA_IFINDEX_OUTDEV, sizeof(tmp_uint),
				&tmp_uint);
		} else {
			/* Case 2: outdev is bridge group, we need to look for
			 * physical output device (when called from ipv4) */
			NFA_PUT(skb, NFQA_IFINDEX_OUTDEV, sizeof(tmp_uint),
				&tmp_uint);
			if (entskb->nf_bridge
			    && entskb->nf_bridge->physoutdev) {
				tmp_uint = htonl(entskb->nf_bridge->physoutdev->ifindex);
				NFA_PUT(skb, NFQA_IFINDEX_PHYSOUTDEV,
					sizeof(tmp_uint), &tmp_uint);
			}
		}
#endif
	}

	if (entskb->mark) {
		tmp_uint = htonl(entskb->mark);
		NFA_PUT(skb, NFQA_MARK, sizeof(u_int32_t), &tmp_uint);
	}

	if (indev && entskb->dev
	    && entskb->dev->hard_header_parse) {
		struct nfqnl_msg_packet_hw phw;

		int len = entskb->dev->hard_header_parse(entskb,
			                                   phw.hw_addr);
		phw.hw_addrlen = htons(len);
		NFA_PUT(skb, NFQA_HWADDR, sizeof(phw), &phw);
	}

	if (entskb->tstamp.off_sec) {
		struct nfqnl_msg_packet_timestamp ts;

		ts.sec = cpu_to_be64(entskb->tstamp.off_sec);
		ts.usec = cpu_to_be64(entskb->tstamp.off_usec);

		NFA_PUT(skb, NFQA_TIMESTAMP, sizeof(ts), &ts);
	}

	if (data_len) {
		struct nfattr *nfa;
		int size = NFA_LENGTH(data_len);

		if (skb_tailroom(skb) < (int)NFA_SPACE(data_len)) {
			printk(KERN_WARNING "nf_queue: no tailroom!\n");
			goto nlmsg_failure;
		}

		nfa = (struct nfattr *)skb_put(skb, NFA_ALIGN(size));
		nfa->nfa_type = NFQA_PAYLOAD;
		nfa->nfa_len = size;

		if (skb_copy_bits(entskb, 0, NFA_DATA(nfa), data_len))
			BUG();
	}
		
	nlh->nlmsg_len = skb->tail - old_tail;
	return skb;

nlmsg_failure:
nfattr_failure:
	if (skb)
		kfree_skb(skb);
	*errp = -EINVAL;
	if (net_ratelimit())
		printk(KERN_ERR "nf_queue: error creating packet message\n");
	return NULL;
}

static int
nfqnl_enqueue_packet(struct sk_buff *skb, struct nf_info *info, 
		     unsigned int queuenum, void *data)
{
	int status = -EINVAL;
	struct sk_buff *nskb;
	struct nfqnl_instance *queue;
	struct nfqnl_queue_entry *entry;

	QDEBUG("entered\n");

	queue = instance_lookup_get(queuenum);
	if (!queue) {
		QDEBUG("no queue instance matching\n");
		return -EINVAL;
	}

	if (queue->copy_mode == NFQNL_COPY_NONE) {
		QDEBUG("mode COPY_NONE, aborting\n");
		status = -EAGAIN;
		goto err_out_put;
	}

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (entry == NULL) {
		if (net_ratelimit())
			printk(KERN_ERR 
				"nf_queue: OOM in nfqnl_enqueue_packet()\n");
		status = -ENOMEM;
		goto err_out_put;
	}

	entry->info = info;
	entry->skb = skb;
	entry->id = atomic_inc_return(&queue->id_sequence);

	nskb = nfqnl_build_packet_message(queue, entry, &status);
	if (nskb == NULL)
		goto err_out_free;
		
	spin_lock_bh(&queue->lock);
	
	if (!queue->peer_pid)
		goto err_out_free_nskb; 

	if (queue->queue_total >= queue->queue_maxlen) {
                queue->queue_dropped++;
		status = -ENOSPC;
		if (net_ratelimit())
		          printk(KERN_WARNING "nf_queue: full at %d entries, "
				 "dropping packets(s). Dropped: %d\n", 
				 queue->queue_total, queue->queue_dropped);
		goto err_out_free_nskb;
	}

	/* nfnetlink_unicast will either free the nskb or add it to a socket */
	status = nfnetlink_unicast(nskb, queue->peer_pid, MSG_DONTWAIT);
	if (status < 0) {
	        queue->queue_user_dropped++;
		goto err_out_unlock;
	}

	__enqueue_entry(queue, entry);

	spin_unlock_bh(&queue->lock);
	instance_put(queue);
	return status;

err_out_free_nskb:
	kfree_skb(nskb); 
	
err_out_unlock:
	spin_unlock_bh(&queue->lock);

err_out_free:
	kfree(entry);
err_out_put:
	instance_put(queue);
	return status;
}

static int
nfqnl_mangle(void *data, int data_len, struct nfqnl_queue_entry *e)
{
	int diff;

	diff = data_len - e->skb->len;
	if (diff < 0) {
		if (pskb_trim(e->skb, data_len))
			return -ENOMEM;
	} else if (diff > 0) {
		if (data_len > 0xFFFF)
			return -EINVAL;
		if (diff > skb_tailroom(e->skb)) {
			struct sk_buff *newskb;
			
			newskb = skb_copy_expand(e->skb,
			                         skb_headroom(e->skb),
			                         diff,
			                         GFP_ATOMIC);
			if (newskb == NULL) {
				printk(KERN_WARNING "nf_queue: OOM "
				      "in mangle, dropping packet\n");
				return -ENOMEM;
			}
			if (e->skb->sk)
				skb_set_owner_w(newskb, e->skb->sk);
			kfree_skb(e->skb);
			e->skb = newskb;
		}
		skb_put(e->skb, diff);
	}
	if (!skb_make_writable(&e->skb, data_len))
		return -ENOMEM;
	memcpy(e->skb->data, data, data_len);
	e->skb->ip_summed = CHECKSUM_NONE;
	return 0;
}

static inline int
id_cmp(struct nfqnl_queue_entry *e, unsigned long id)
{
	return (id == e->id);
}

static int
nfqnl_set_mode(struct nfqnl_instance *queue,
	       unsigned char mode, unsigned int range)
{
	int status;

	spin_lock_bh(&queue->lock);
	status = __nfqnl_set_mode(queue, mode, range);
	spin_unlock_bh(&queue->lock);

	return status;
}

static int
dev_cmp(struct nfqnl_queue_entry *entry, unsigned long ifindex)
{
	struct nf_info *entinf = entry->info;
	
	if (entinf->indev)
		if (entinf->indev->ifindex == ifindex)
			return 1;
	if (entinf->outdev)
		if (entinf->outdev->ifindex == ifindex)
			return 1;
#ifdef CONFIG_BRIDGE_NETFILTER
	if (entry->skb->nf_bridge) {
		if (entry->skb->nf_bridge->physindev &&
		    entry->skb->nf_bridge->physindev->ifindex == ifindex)
			return 1;
		if (entry->skb->nf_bridge->physoutdev &&
		    entry->skb->nf_bridge->physoutdev->ifindex == ifindex)
			return 1;
	}
#endif
	return 0;
}

/* drop all packets with either indev or outdev == ifindex from all queue
 * instances */
static void
nfqnl_dev_drop(int ifindex)
{
	int i;
	
	QDEBUG("entering for ifindex %u\n", ifindex);

	/* this only looks like we have to hold the readlock for a way too long
	 * time, issue_verdict(),  nf_reinject(), ... - but we always only
	 * issue NF_DROP, which is processed directly in nf_reinject() */
	read_lock_bh(&instances_lock);

	for  (i = 0; i < INSTANCE_BUCKETS; i++) {
		struct hlist_node *tmp;
		struct nfqnl_instance *inst;
		struct hlist_head *head = &instance_table[i];

		hlist_for_each_entry(inst, tmp, head, hlist) {
			struct nfqnl_queue_entry *entry;
			while ((entry = find_dequeue_entry(inst, dev_cmp, 
							   ifindex)) != NULL)
				issue_verdict(entry, NF_DROP);
		}
	}

	read_unlock_bh(&instances_lock);
}

#define RCV_SKB_FAIL(err) do { netlink_ack(skb, nlh, (err)); return; } while (0)

static int
nfqnl_rcv_dev_event(struct notifier_block *this,
		    unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;

	/* Drop any packets associated with the downed device */
	if (event == NETDEV_DOWN)
		nfqnl_dev_drop(dev->ifindex);
	return NOTIFY_DONE;
}

static struct notifier_block nfqnl_dev_notifier = {
	.notifier_call	= nfqnl_rcv_dev_event,
};

static int
nfqnl_rcv_nl_event(struct notifier_block *this,
		   unsigned long event, void *ptr)
{
	struct netlink_notify *n = ptr;

	if (event == NETLINK_URELEASE &&
	    n->protocol == NETLINK_NETFILTER && n->pid) {
		int i;

		/* destroy all instances for this pid */
		write_lock_bh(&instances_lock);
		for  (i = 0; i < INSTANCE_BUCKETS; i++) {
			struct hlist_node *tmp, *t2;
			struct nfqnl_instance *inst;
			struct hlist_head *head = &instance_table[i];

			hlist_for_each_entry_safe(inst, tmp, t2, head, hlist) {
				if (n->pid == inst->peer_pid)
					__instance_destroy(inst);
			}
		}
		write_unlock_bh(&instances_lock);
	}
	return NOTIFY_DONE;
}

static struct notifier_block nfqnl_rtnl_notifier = {
	.notifier_call	= nfqnl_rcv_nl_event,
};

static const int nfqa_verdict_min[NFQA_MAX] = {
	[NFQA_VERDICT_HDR-1]	= sizeof(struct nfqnl_msg_verdict_hdr),
	[NFQA_MARK-1]		= sizeof(u_int32_t),
	[NFQA_PAYLOAD-1]	= 0,
};

static int
nfqnl_recv_verdict(struct sock *ctnl, struct sk_buff *skb,
		   struct nlmsghdr *nlh, struct nfattr *nfqa[], int *errp)
{
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int16_t queue_num = ntohs(nfmsg->res_id);

	struct nfqnl_msg_verdict_hdr *vhdr;
	struct nfqnl_instance *queue;
	unsigned int verdict;
	struct nfqnl_queue_entry *entry;
	int err;

	if (nfattr_bad_size(nfqa, NFQA_MAX, nfqa_verdict_min)) {
		QDEBUG("bad attribute size\n");
		return -EINVAL;
	}

	queue = instance_lookup_get(queue_num);
	if (!queue)
		return -ENODEV;

	if (queue->peer_pid != NETLINK_CB(skb).pid) {
		err = -EPERM;
		goto err_out_put;
	}

	if (!nfqa[NFQA_VERDICT_HDR-1]) {
		err = -EINVAL;
		goto err_out_put;
	}

	vhdr = NFA_DATA(nfqa[NFQA_VERDICT_HDR-1]);
	verdict = ntohl(vhdr->verdict);

	if ((verdict & NF_VERDICT_MASK) > NF_MAX_VERDICT) {
		err = -EINVAL;
		goto err_out_put;
	}

	entry = find_dequeue_entry(queue, id_cmp, ntohl(vhdr->id));
	if (entry == NULL) {
		err = -ENOENT;
		goto err_out_put;
	}

	if (nfqa[NFQA_PAYLOAD-1]) {
		if (nfqnl_mangle(NFA_DATA(nfqa[NFQA_PAYLOAD-1]),
				 NFA_PAYLOAD(nfqa[NFQA_PAYLOAD-1]), entry) < 0)
			verdict = NF_DROP;
	}

	if (nfqa[NFQA_MARK-1])
		entry->skb->mark = ntohl(*(__be32 *)
		                         NFA_DATA(nfqa[NFQA_MARK-1]));
		
	issue_verdict(entry, verdict);
	instance_put(queue);
	return 0;

err_out_put:
	instance_put(queue);
	return err;
}

static int
nfqnl_recv_unsupp(struct sock *ctnl, struct sk_buff *skb,
		  struct nlmsghdr *nlh, struct nfattr *nfqa[], int *errp)
{
	return -ENOTSUPP;
}

static const int nfqa_cfg_min[NFQA_CFG_MAX] = {
	[NFQA_CFG_CMD-1]	= sizeof(struct nfqnl_msg_config_cmd),
	[NFQA_CFG_PARAMS-1]	= sizeof(struct nfqnl_msg_config_params),
};

static struct nf_queue_handler nfqh = {
	.name 	= "nf_queue",
	.outfn	= &nfqnl_enqueue_packet,
};

static int
nfqnl_recv_config(struct sock *ctnl, struct sk_buff *skb,
		  struct nlmsghdr *nlh, struct nfattr *nfqa[], int *errp)
{
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int16_t queue_num = ntohs(nfmsg->res_id);
	struct nfqnl_instance *queue;
	int ret = 0;

	QDEBUG("entering for msg %u\n", NFNL_MSG_TYPE(nlh->nlmsg_type));

	if (nfattr_bad_size(nfqa, NFQA_CFG_MAX, nfqa_cfg_min)) {
		QDEBUG("bad attribute size\n");
		return -EINVAL;
	}

	queue = instance_lookup_get(queue_num);
	if (nfqa[NFQA_CFG_CMD-1]) {
		struct nfqnl_msg_config_cmd *cmd;
		cmd = NFA_DATA(nfqa[NFQA_CFG_CMD-1]);
		QDEBUG("found CFG_CMD\n");

		switch (cmd->command) {
		case NFQNL_CFG_CMD_BIND:
			if (queue)
				return -EBUSY;

			queue = instance_create(queue_num, NETLINK_CB(skb).pid);
			if (!queue)
				return -EINVAL;
			break;
		case NFQNL_CFG_CMD_UNBIND:
			if (!queue)
				return -ENODEV;

			if (queue->peer_pid != NETLINK_CB(skb).pid) {
				ret = -EPERM;
				goto out_put;
			}

			instance_destroy(queue);
			break;
		case NFQNL_CFG_CMD_PF_BIND:
			QDEBUG("registering queue handler for pf=%u\n",
				ntohs(cmd->pf));
			ret = nf_register_queue_handler(ntohs(cmd->pf), &nfqh);
			break;
		case NFQNL_CFG_CMD_PF_UNBIND:
			QDEBUG("unregistering queue handler for pf=%u\n",
				ntohs(cmd->pf));
			/* This is a bug and a feature.  We can unregister
			 * other handlers(!) */
			ret = nf_unregister_queue_handler(ntohs(cmd->pf));
			break;
		default:
			ret = -EINVAL;
			break;
		}
	} else {
		if (!queue) {
			QDEBUG("no config command, and no instance ENOENT\n");
			ret = -ENOENT;
			goto out_put;
		}

		if (queue->peer_pid != NETLINK_CB(skb).pid) {
			QDEBUG("no config command, and wrong pid\n");
			ret = -EPERM;
			goto out_put;
		}
	}

	if (nfqa[NFQA_CFG_PARAMS-1]) {
		struct nfqnl_msg_config_params *params;

		if (!queue) {
			ret = -ENOENT;
			goto out_put;
		}
		params = NFA_DATA(nfqa[NFQA_CFG_PARAMS-1]);
		nfqnl_set_mode(queue, params->copy_mode,
				ntohl(params->copy_range));
	}

out_put:
	instance_put(queue);
	return ret;
}

static struct nfnl_callback nfqnl_cb[NFQNL_MSG_MAX] = {
	[NFQNL_MSG_PACKET]	= { .call = nfqnl_recv_unsupp,
				    .attr_count = NFQA_MAX, },
	[NFQNL_MSG_VERDICT]	= { .call = nfqnl_recv_verdict,
				    .attr_count = NFQA_MAX, },
	[NFQNL_MSG_CONFIG]	= { .call = nfqnl_recv_config,
				    .attr_count = NFQA_CFG_MAX, },
};

static struct nfnetlink_subsystem nfqnl_subsys = {
	.name		= "nf_queue",
	.subsys_id	= NFNL_SUBSYS_QUEUE,
	.cb_count	= NFQNL_MSG_MAX,
	.cb		= nfqnl_cb,
};

#ifdef CONFIG_PROC_FS
struct iter_state {
	unsigned int bucket;
};

static struct hlist_node *get_first(struct seq_file *seq)
{
	struct iter_state *st = seq->private;

	if (!st)
		return NULL;

	for (st->bucket = 0; st->bucket < INSTANCE_BUCKETS; st->bucket++) {
		if (!hlist_empty(&instance_table[st->bucket]))
			return instance_table[st->bucket].first;
	}
	return NULL;
}

static struct hlist_node *get_next(struct seq_file *seq, struct hlist_node *h)
{
	struct iter_state *st = seq->private;

	h = h->next;
	while (!h) {
		if (++st->bucket >= INSTANCE_BUCKETS)
			return NULL;

		h = instance_table[st->bucket].first;
	}
	return h;
}

static struct hlist_node *get_idx(struct seq_file *seq, loff_t pos)
{
	struct hlist_node *head;
	head = get_first(seq);

	if (head)
		while (pos && (head = get_next(seq, head)))
			pos--;
	return pos ? NULL : head;
}

static void *seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock_bh(&instances_lock);
	return get_idx(seq, *pos);
}

static void *seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	return get_next(s, v);
}

static void seq_stop(struct seq_file *s, void *v)
{
	read_unlock_bh(&instances_lock);
}

static int seq_show(struct seq_file *s, void *v)
{
	const struct nfqnl_instance *inst = v;

	return seq_printf(s, "%5d %6d %5d %1d %5d %5d %5d %8d %2d\n",
			  inst->queue_num,
			  inst->peer_pid, inst->queue_total,
			  inst->copy_mode, inst->copy_range,
			  inst->queue_dropped, inst->queue_user_dropped,
			  atomic_read(&inst->id_sequence),
			  atomic_read(&inst->use));
}

static struct seq_operations nfqnl_seq_ops = {
	.start	= seq_start,
	.next	= seq_next,
	.stop	= seq_stop,
	.show	= seq_show,
};

static int nfqnl_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	struct iter_state *is;
	int ret;

	is = kzalloc(sizeof(*is), GFP_KERNEL);
	if (!is)
		return -ENOMEM;
	ret = seq_open(file, &nfqnl_seq_ops);
	if (ret < 0)
		goto out_free;
	seq = file->private_data;
	seq->private = is;
	return ret;
out_free:
	kfree(is);
	return ret;
}

static struct file_operations nfqnl_file_ops = {
	.owner	 = THIS_MODULE,
	.open	 = nfqnl_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_private,
};

#endif /* PROC_FS */

static int __init nfnetlink_queue_init(void)
{
	int i, status = -ENOMEM;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc_nfqueue;
#endif
	
	for (i = 0; i < INSTANCE_BUCKETS; i++)
		INIT_HLIST_HEAD(&instance_table[i]);

	netlink_register_notifier(&nfqnl_rtnl_notifier);
	status = nfnetlink_subsys_register(&nfqnl_subsys);
	if (status < 0) {
		printk(KERN_ERR "nf_queue: failed to create netlink socket\n");
		goto cleanup_netlink_notifier;
	}

#ifdef CONFIG_PROC_FS
	proc_nfqueue = create_proc_entry("nfnetlink_queue", 0440,
					 proc_net_netfilter);
	if (!proc_nfqueue)
		goto cleanup_subsys;
	proc_nfqueue->proc_fops = &nfqnl_file_ops;
#endif

	register_netdevice_notifier(&nfqnl_dev_notifier);
	return status;

#ifdef CONFIG_PROC_FS
cleanup_subsys:
	nfnetlink_subsys_unregister(&nfqnl_subsys);
#endif
cleanup_netlink_notifier:
	netlink_unregister_notifier(&nfqnl_rtnl_notifier);
	return status;
}

static void __exit nfnetlink_queue_fini(void)
{
	nf_unregister_queue_handlers(&nfqh);
	unregister_netdevice_notifier(&nfqnl_dev_notifier);
#ifdef CONFIG_PROC_FS
	remove_proc_entry("nfnetlink_queue", proc_net_netfilter);
#endif
	nfnetlink_subsys_unregister(&nfqnl_subsys);
	netlink_unregister_notifier(&nfqnl_rtnl_notifier);
}

MODULE_DESCRIPTION("netfilter packet queue handler");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_QUEUE);

module_init(nfnetlink_queue_init);
module_exit(nfnetlink_queue_fini);
