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
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_queue.h>
#include <linux/list.h>
#include <net/sock.h>

#include <asm/atomic.h>

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

u_int64_t htonll(u_int64_t in)
{
	u_int64_t out;
	int i;

	for (i = 0; i < sizeof(u_int64_t); i++)
		((u_int8_t *)&out)[sizeof(u_int64_t)-1] = ((u_int8_t *)&in)[i];

	return out;
}

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
instance_lookup(u_int16_t queue_num)
{
	struct nfqnl_instance *inst;

	read_lock_bh(&instances_lock);
	inst = __instance_lookup(queue_num);
	read_unlock_bh(&instances_lock);

	return inst;
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

	inst = kmalloc(sizeof(*inst), GFP_ATOMIC);
	if (!inst)
		goto out_unlock;

	memset(inst, 0, sizeof(*inst));
	inst->queue_num = queue_num;
	inst->peer_pid = pid;
	inst->queue_maxlen = NFQNL_QMAX_DEFAULT;
	inst->copy_range = 0xfffff;
	inst->copy_mode = NFQNL_COPY_NONE;
	atomic_set(&inst->id_sequence, 0);
	inst->lock = SPIN_LOCK_UNLOCKED;
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

	/* and finally free the data structure */
	kfree(inst);

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
	unsigned int tmp_uint;

	QDEBUG("entered\n");

	/* all macros expand to constant values at compile time */
	size =    NLMSG_SPACE(sizeof(struct nfqnl_msg_packet_hdr))
		+ NLMSG_SPACE(sizeof(u_int32_t))	/* ifindex */
		+ NLMSG_SPACE(sizeof(u_int32_t))	/* ifindex */
		+ NLMSG_SPACE(sizeof(u_int32_t))	/* mark */
		+ NLMSG_SPACE(sizeof(struct nfqnl_msg_packet_hw))
		+ NLMSG_SPACE(sizeof(struct nfqnl_msg_packet_timestamp));

	spin_lock_bh(&queue->lock);
	
	switch (queue->copy_mode) {
	case NFQNL_COPY_META:
	case NFQNL_COPY_NONE:
		data_len = 0;
		break;
	
	case NFQNL_COPY_PACKET:
		if (queue->copy_range == 0 
		    || queue->copy_range > entry->skb->len)
			data_len = entry->skb->len;
		else
			data_len = queue->copy_range;
		
		size += NLMSG_SPACE(data_len);
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
	nfmsg->nfgen_family = entry->info->pf;
	nfmsg->version = NFNETLINK_V0;
	nfmsg->res_id = htons(queue->queue_num);

	pmsg.packet_id 		= htonl(entry->id);
	pmsg.hw_protocol	= htons(entry->skb->protocol);
	pmsg.hook		= entry->info->hook;

	NFA_PUT(skb, NFQA_PACKET_HDR, sizeof(pmsg), &pmsg);

	if (entry->info->indev) {
		tmp_uint = htonl(entry->info->indev->ifindex);
		NFA_PUT(skb, NFQA_IFINDEX_INDEV, sizeof(tmp_uint), &tmp_uint);
	}

	if (entry->info->outdev) {
		tmp_uint = htonl(entry->info->outdev->ifindex);
		NFA_PUT(skb, NFQA_IFINDEX_OUTDEV, sizeof(tmp_uint), &tmp_uint);
	}

	if (entry->skb->nfmark) {
		tmp_uint = htonl(entry->skb->nfmark);
		NFA_PUT(skb, NFQA_MARK, sizeof(u_int32_t), &tmp_uint);
	}

	if (entry->info->indev && entry->skb->dev
	    && entry->skb->dev->hard_header_parse) {
		struct nfqnl_msg_packet_hw phw;

		phw.hw_addrlen =
			entry->skb->dev->hard_header_parse(entry->skb,
			                                   phw.hw_addr);
		phw.hw_addrlen = htons(phw.hw_addrlen);
		NFA_PUT(skb, NFQA_HWADDR, sizeof(phw), &phw);
	}

	if (entry->skb->stamp.tv_sec) {
		struct nfqnl_msg_packet_timestamp ts;

		ts.sec = htonll(entry->skb->stamp.tv_sec);
		ts.usec = htonll(entry->skb->stamp.tv_usec);

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

		if (skb_copy_bits(entry->skb, 0, NFA_DATA(nfa), data_len))
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

	queue = instance_lookup(queuenum);
	if (!queue) {
		QDEBUG("no queue instance matching\n");
		return -EINVAL;
	}

	if (queue->copy_mode == NFQNL_COPY_NONE) {
		QDEBUG("mode COPY_NONE, aborting\n");
		return -EAGAIN;
	}

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (entry == NULL) {
		if (net_ratelimit())
			printk(KERN_ERR 
				"nf_queue: OOM in nfqnl_enqueue_packet()\n");
		return -ENOMEM;
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
		          printk(KERN_WARNING "ip_queue: full at %d entries, "
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
	return status;

err_out_free_nskb:
	kfree_skb(nskb); 
	
err_out_unlock:
	spin_unlock_bh(&queue->lock);

err_out_free:
	kfree(entry);
	return status;
}

static int
nfqnl_mangle(void *data, int data_len, struct nfqnl_queue_entry *e)
{
	int diff;

	diff = data_len - e->skb->len;
	if (diff < 0)
		skb_trim(e->skb, data_len);
	else if (diff > 0) {
		if (data_len > 0xFFFF)
			return -EINVAL;
		if (diff > skb_tailroom(e->skb)) {
			struct sk_buff *newskb;
			
			newskb = skb_copy_expand(e->skb,
			                         skb_headroom(e->skb),
			                         diff,
			                         GFP_ATOMIC);
			if (newskb == NULL) {
				printk(KERN_WARNING "ip_queue: OOM "
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
	if (entry->info->indev)
		if (entry->info->indev->ifindex == ifindex)
			return 1;
			
	if (entry->info->outdev)
		if (entry->info->outdev->ifindex == ifindex)
			return 1;

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

	queue = instance_lookup(queue_num);
	if (!queue)
		return -ENODEV;

	if (queue->peer_pid != NETLINK_CB(skb).pid)
		return -EPERM;

	if (!nfqa[NFQA_VERDICT_HDR-1])
		return -EINVAL;

	vhdr = NFA_DATA(nfqa[NFQA_VERDICT_HDR-1]);
	verdict = ntohl(vhdr->verdict);

	if ((verdict & NF_VERDICT_MASK) > NF_MAX_VERDICT)
		return -EINVAL;

	entry = find_dequeue_entry(queue, id_cmp, ntohl(vhdr->id));
	if (entry == NULL)
		return -ENOENT;

	if (nfqa[NFQA_PAYLOAD-1]) {
		if (nfqnl_mangle(NFA_DATA(nfqa[NFQA_PAYLOAD-1]),
				 NFA_PAYLOAD(nfqa[NFQA_PAYLOAD-1]), entry) < 0)
			verdict = NF_DROP;
	}

	if (nfqa[NFQA_MARK-1])
		skb->nfmark = ntohl(*(u_int32_t *)NFA_DATA(nfqa[NFQA_MARK-1]));
		
	issue_verdict(entry, verdict);
	return 0;
}

static int
nfqnl_recv_unsupp(struct sock *ctnl, struct sk_buff *skb,
		  struct nlmsghdr *nlh, struct nfattr *nfqa[], int *errp)
{
	return -ENOTSUPP;
}

static int
nfqnl_recv_config(struct sock *ctnl, struct sk_buff *skb,
		  struct nlmsghdr *nlh, struct nfattr *nfqa[], int *errp)
{
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int16_t queue_num = ntohs(nfmsg->res_id);
	struct nfqnl_instance *queue;

	QDEBUG("entering for msg %u\n", NFNL_MSG_TYPE(nlh->nlmsg_type));

	queue = instance_lookup(queue_num);
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

			if (queue->peer_pid != NETLINK_CB(skb).pid)
				return -EPERM;

			instance_destroy(queue);
			break;
		case NFQNL_CFG_CMD_PF_BIND:
			QDEBUG("registering queue handler for pf=%u\n",
				ntohs(cmd->pf));
			return nf_register_queue_handler(ntohs(cmd->pf),
							 nfqnl_enqueue_packet,
							 NULL);

			break;
		case NFQNL_CFG_CMD_PF_UNBIND:
			QDEBUG("unregistering queue handler for pf=%u\n",
				ntohs(cmd->pf));
			/* This is a bug and a feature.  We can unregister
			 * other handlers(!) */
			return nf_unregister_queue_handler(ntohs(cmd->pf));
			break;
		default:
			return -EINVAL;
		}
	} else {
		if (!queue) {
			QDEBUG("no config command, and no instance ENOENT\n");
			return -ENOENT;
		}

		if (queue->peer_pid != NETLINK_CB(skb).pid) {
			QDEBUG("no config command, and wrong pid\n");
			return -EPERM;
		}
	}

	if (nfqa[NFQA_CFG_PARAMS-1]) {
		struct nfqnl_msg_config_params *params;
		params = NFA_DATA(nfqa[NFQA_CFG_PARAMS-1]);

		nfqnl_set_mode(queue, params->copy_mode,
				ntohl(params->copy_range));
	}

	return 0;
}

static struct nfnl_callback nfqnl_cb[NFQNL_MSG_MAX] = {
	[NFQNL_MSG_PACKET]	= { .call = nfqnl_recv_unsupp,
				    .cap_required = CAP_NET_ADMIN },
	[NFQNL_MSG_VERDICT]	= { .call = nfqnl_recv_verdict,
				    .cap_required = CAP_NET_ADMIN },
	[NFQNL_MSG_CONFIG]	= { .call = nfqnl_recv_config,
				    .cap_required = CAP_NET_ADMIN },
};

static struct nfnetlink_subsystem nfqnl_subsys = {
	.name		= "nf_queue",
	.subsys_id	= NFNL_SUBSYS_QUEUE,
	.cb_count	= NFQNL_MSG_MAX,
	.attr_count	= NFQA_MAX,
	.cb		= nfqnl_cb,
};

static int
init_or_cleanup(int init)
{
	int status = -ENOMEM;
	
	if (!init)
		goto cleanup;

	netlink_register_notifier(&nfqnl_rtnl_notifier);
	status = nfnetlink_subsys_register(&nfqnl_subsys);
	if (status < 0) {
		printk(KERN_ERR "nf_queue: failed to create netlink socket\n");
		goto cleanup_netlink_notifier;
	}

	register_netdevice_notifier(&nfqnl_dev_notifier);
	return status;

cleanup:
	nf_unregister_queue_handlers(nfqnl_enqueue_packet);
	unregister_netdevice_notifier(&nfqnl_dev_notifier);
	nfnetlink_subsys_unregister(&nfqnl_subsys);
	
cleanup_netlink_notifier:
	netlink_unregister_notifier(&nfqnl_rtnl_notifier);
	return status;
}

static int __init init(void)
{
	
	return init_or_cleanup(1);
}

static void __exit fini(void)
{
	init_or_cleanup(0);
}

MODULE_DESCRIPTION("netfilter packet queue handler");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_QUEUE);

module_init(init);
module_exit(fini);
