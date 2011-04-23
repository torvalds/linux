/*
 * This is a module which is used for queueing packets and communicating with
 * userspace via nfnetlink.
 *
 * (C) 2005 by Harald Welte <laforge@netfilter.org>
 * (C) 2007 by Patrick McHardy <kaber@trash.net>
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
#include <linux/slab.h>
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
#include <net/netfilter/nf_queue.h>

#include <asm/atomic.h>

#ifdef CONFIG_BRIDGE_NETFILTER
#include "../bridge/br_private.h"
#endif

#define NFQNL_QMAX_DEFAULT 1024

struct nfqnl_instance {
	struct hlist_node hlist;		/* global list of queues */
	struct rcu_head rcu;

	int peer_pid;
	unsigned int queue_maxlen;
	unsigned int copy_range;
	unsigned int queue_dropped;
	unsigned int queue_user_dropped;


	u_int16_t queue_num;			/* number of this queue */
	u_int8_t copy_mode;
/*
 * Following fields are dirtied for each queued packet,
 * keep them in same cache line if possible.
 */
	spinlock_t	lock;
	unsigned int	queue_total;
	atomic_t	id_sequence;		/* 'sequence' of pkt ids */
	struct list_head queue_list;		/* packets in queue */
};

typedef int (*nfqnl_cmpfn)(struct nf_queue_entry *, unsigned long);

static DEFINE_SPINLOCK(instances_lock);

#define INSTANCE_BUCKETS	16
static struct hlist_head instance_table[INSTANCE_BUCKETS] __read_mostly;

static inline u_int8_t instance_hashfn(u_int16_t queue_num)
{
	return ((queue_num >> 8) | queue_num) % INSTANCE_BUCKETS;
}

static struct nfqnl_instance *
instance_lookup(u_int16_t queue_num)
{
	struct hlist_head *head;
	struct hlist_node *pos;
	struct nfqnl_instance *inst;

	head = &instance_table[instance_hashfn(queue_num)];
	hlist_for_each_entry_rcu(inst, pos, head, hlist) {
		if (inst->queue_num == queue_num)
			return inst;
	}
	return NULL;
}

static struct nfqnl_instance *
instance_create(u_int16_t queue_num, int pid)
{
	struct nfqnl_instance *inst;
	unsigned int h;
	int err;

	spin_lock(&instances_lock);
	if (instance_lookup(queue_num)) {
		err = -EEXIST;
		goto out_unlock;
	}

	inst = kzalloc(sizeof(*inst), GFP_ATOMIC);
	if (!inst) {
		err = -ENOMEM;
		goto out_unlock;
	}

	inst->queue_num = queue_num;
	inst->peer_pid = pid;
	inst->queue_maxlen = NFQNL_QMAX_DEFAULT;
	inst->copy_range = 0xfffff;
	inst->copy_mode = NFQNL_COPY_NONE;
	spin_lock_init(&inst->lock);
	INIT_LIST_HEAD(&inst->queue_list);

	if (!try_module_get(THIS_MODULE)) {
		err = -EAGAIN;
		goto out_free;
	}

	h = instance_hashfn(queue_num);
	hlist_add_head_rcu(&inst->hlist, &instance_table[h]);

	spin_unlock(&instances_lock);

	return inst;

out_free:
	kfree(inst);
out_unlock:
	spin_unlock(&instances_lock);
	return ERR_PTR(err);
}

static void nfqnl_flush(struct nfqnl_instance *queue, nfqnl_cmpfn cmpfn,
			unsigned long data);

static void
instance_destroy_rcu(struct rcu_head *head)
{
	struct nfqnl_instance *inst = container_of(head, struct nfqnl_instance,
						   rcu);

	nfqnl_flush(inst, NULL, 0);
	kfree(inst);
	module_put(THIS_MODULE);
}

static void
__instance_destroy(struct nfqnl_instance *inst)
{
	hlist_del_rcu(&inst->hlist);
	call_rcu(&inst->rcu, instance_destroy_rcu);
}

static void
instance_destroy(struct nfqnl_instance *inst)
{
	spin_lock(&instances_lock);
	__instance_destroy(inst);
	spin_unlock(&instances_lock);
}

static inline void
__enqueue_entry(struct nfqnl_instance *queue, struct nf_queue_entry *entry)
{
       list_add_tail(&entry->list, &queue->queue_list);
       queue->queue_total++;
}

static struct nf_queue_entry *
find_dequeue_entry(struct nfqnl_instance *queue, unsigned int id)
{
	struct nf_queue_entry *entry = NULL, *i;

	spin_lock_bh(&queue->lock);

	list_for_each_entry(i, &queue->queue_list, list) {
		if (i->id == id) {
			entry = i;
			break;
		}
	}

	if (entry) {
		list_del(&entry->list);
		queue->queue_total--;
	}

	spin_unlock_bh(&queue->lock);

	return entry;
}

static void
nfqnl_flush(struct nfqnl_instance *queue, nfqnl_cmpfn cmpfn, unsigned long data)
{
	struct nf_queue_entry *entry, *next;

	spin_lock_bh(&queue->lock);
	list_for_each_entry_safe(entry, next, &queue->queue_list, list) {
		if (!cmpfn || cmpfn(entry, data)) {
			list_del(&entry->list);
			queue->queue_total--;
			nf_reinject(entry, NF_DROP);
		}
	}
	spin_unlock_bh(&queue->lock);
}

static struct sk_buff *
nfqnl_build_packet_message(struct nfqnl_instance *queue,
			   struct nf_queue_entry *entry)
{
	sk_buff_data_t old_tail;
	size_t size;
	size_t data_len = 0;
	struct sk_buff *skb;
	struct nfqnl_msg_packet_hdr pmsg;
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct sk_buff *entskb = entry->skb;
	struct net_device *indev;
	struct net_device *outdev;

	size =    NLMSG_SPACE(sizeof(struct nfgenmsg))
		+ nla_total_size(sizeof(struct nfqnl_msg_packet_hdr))
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
#ifdef CONFIG_BRIDGE_NETFILTER
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
#endif
		+ nla_total_size(sizeof(u_int32_t))	/* mark */
		+ nla_total_size(sizeof(struct nfqnl_msg_packet_hw))
		+ nla_total_size(sizeof(struct nfqnl_msg_packet_timestamp));

	outdev = entry->outdev;

	switch ((enum nfqnl_config_mode)ACCESS_ONCE(queue->copy_mode)) {
	case NFQNL_COPY_META:
	case NFQNL_COPY_NONE:
		break;

	case NFQNL_COPY_PACKET:
		if (entskb->ip_summed == CHECKSUM_PARTIAL &&
		    skb_checksum_help(entskb))
			return NULL;

		data_len = ACCESS_ONCE(queue->copy_range);
		if (data_len == 0 || data_len > entskb->len)
			data_len = entskb->len;

		size += nla_total_size(data_len);
		break;
	}


	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb)
		goto nlmsg_failure;

	old_tail = skb->tail;
	nlh = NLMSG_PUT(skb, 0, 0,
			NFNL_SUBSYS_QUEUE << 8 | NFQNL_MSG_PACKET,
			sizeof(struct nfgenmsg));
	nfmsg = NLMSG_DATA(nlh);
	nfmsg->nfgen_family = entry->pf;
	nfmsg->version = NFNETLINK_V0;
	nfmsg->res_id = htons(queue->queue_num);

	entry->id = atomic_inc_return(&queue->id_sequence);
	pmsg.packet_id 		= htonl(entry->id);
	pmsg.hw_protocol	= entskb->protocol;
	pmsg.hook		= entry->hook;

	NLA_PUT(skb, NFQA_PACKET_HDR, sizeof(pmsg), &pmsg);

	indev = entry->indev;
	if (indev) {
#ifndef CONFIG_BRIDGE_NETFILTER
		NLA_PUT_BE32(skb, NFQA_IFINDEX_INDEV, htonl(indev->ifindex));
#else
		if (entry->pf == PF_BRIDGE) {
			/* Case 1: indev is physical input device, we need to
			 * look for bridge group (when called from
			 * netfilter_bridge) */
			NLA_PUT_BE32(skb, NFQA_IFINDEX_PHYSINDEV,
				     htonl(indev->ifindex));
			/* this is the bridge group "brX" */
			/* rcu_read_lock()ed by __nf_queue */
			NLA_PUT_BE32(skb, NFQA_IFINDEX_INDEV,
				     htonl(br_port_get_rcu(indev)->br->dev->ifindex));
		} else {
			/* Case 2: indev is bridge group, we need to look for
			 * physical device (when called from ipv4) */
			NLA_PUT_BE32(skb, NFQA_IFINDEX_INDEV,
				     htonl(indev->ifindex));
			if (entskb->nf_bridge && entskb->nf_bridge->physindev)
				NLA_PUT_BE32(skb, NFQA_IFINDEX_PHYSINDEV,
					     htonl(entskb->nf_bridge->physindev->ifindex));
		}
#endif
	}

	if (outdev) {
#ifndef CONFIG_BRIDGE_NETFILTER
		NLA_PUT_BE32(skb, NFQA_IFINDEX_OUTDEV, htonl(outdev->ifindex));
#else
		if (entry->pf == PF_BRIDGE) {
			/* Case 1: outdev is physical output device, we need to
			 * look for bridge group (when called from
			 * netfilter_bridge) */
			NLA_PUT_BE32(skb, NFQA_IFINDEX_PHYSOUTDEV,
				     htonl(outdev->ifindex));
			/* this is the bridge group "brX" */
			/* rcu_read_lock()ed by __nf_queue */
			NLA_PUT_BE32(skb, NFQA_IFINDEX_OUTDEV,
				     htonl(br_port_get_rcu(outdev)->br->dev->ifindex));
		} else {
			/* Case 2: outdev is bridge group, we need to look for
			 * physical output device (when called from ipv4) */
			NLA_PUT_BE32(skb, NFQA_IFINDEX_OUTDEV,
				     htonl(outdev->ifindex));
			if (entskb->nf_bridge && entskb->nf_bridge->physoutdev)
				NLA_PUT_BE32(skb, NFQA_IFINDEX_PHYSOUTDEV,
					     htonl(entskb->nf_bridge->physoutdev->ifindex));
		}
#endif
	}

	if (entskb->mark)
		NLA_PUT_BE32(skb, NFQA_MARK, htonl(entskb->mark));

	if (indev && entskb->dev) {
		struct nfqnl_msg_packet_hw phw;
		int len = dev_parse_header(entskb, phw.hw_addr);
		if (len) {
			phw.hw_addrlen = htons(len);
			NLA_PUT(skb, NFQA_HWADDR, sizeof(phw), &phw);
		}
	}

	if (entskb->tstamp.tv64) {
		struct nfqnl_msg_packet_timestamp ts;
		struct timeval tv = ktime_to_timeval(entskb->tstamp);
		ts.sec = cpu_to_be64(tv.tv_sec);
		ts.usec = cpu_to_be64(tv.tv_usec);

		NLA_PUT(skb, NFQA_TIMESTAMP, sizeof(ts), &ts);
	}

	if (data_len) {
		struct nlattr *nla;
		int sz = nla_attr_size(data_len);

		if (skb_tailroom(skb) < nla_total_size(data_len)) {
			printk(KERN_WARNING "nf_queue: no tailroom!\n");
			goto nlmsg_failure;
		}

		nla = (struct nlattr *)skb_put(skb, nla_total_size(data_len));
		nla->nla_type = NFQA_PAYLOAD;
		nla->nla_len = sz;

		if (skb_copy_bits(entskb, 0, nla_data(nla), data_len))
			BUG();
	}

	nlh->nlmsg_len = skb->tail - old_tail;
	return skb;

nlmsg_failure:
nla_put_failure:
	if (skb)
		kfree_skb(skb);
	if (net_ratelimit())
		printk(KERN_ERR "nf_queue: error creating packet message\n");
	return NULL;
}

static int
nfqnl_enqueue_packet(struct nf_queue_entry *entry, unsigned int queuenum)
{
	struct sk_buff *nskb;
	struct nfqnl_instance *queue;
	int err = -ENOBUFS;

	/* rcu_read_lock()ed by nf_hook_slow() */
	queue = instance_lookup(queuenum);
	if (!queue) {
		err = -ESRCH;
		goto err_out;
	}

	if (queue->copy_mode == NFQNL_COPY_NONE) {
		err = -EINVAL;
		goto err_out;
	}

	nskb = nfqnl_build_packet_message(queue, entry);
	if (nskb == NULL) {
		err = -ENOMEM;
		goto err_out;
	}
	spin_lock_bh(&queue->lock);

	if (!queue->peer_pid) {
		err = -EINVAL;
		goto err_out_free_nskb;
	}
	if (queue->queue_total >= queue->queue_maxlen) {
		queue->queue_dropped++;
		if (net_ratelimit())
			  printk(KERN_WARNING "nf_queue: full at %d entries, "
				 "dropping packets(s).\n",
				 queue->queue_total);
		goto err_out_free_nskb;
	}

	/* nfnetlink_unicast will either free the nskb or add it to a socket */
	err = nfnetlink_unicast(nskb, &init_net, queue->peer_pid, MSG_DONTWAIT);
	if (err < 0) {
		queue->queue_user_dropped++;
		goto err_out_unlock;
	}

	__enqueue_entry(queue, entry);

	spin_unlock_bh(&queue->lock);
	return 0;

err_out_free_nskb:
	kfree_skb(nskb);
err_out_unlock:
	spin_unlock_bh(&queue->lock);
err_out:
	return err;
}

static int
nfqnl_mangle(void *data, int data_len, struct nf_queue_entry *e)
{
	struct sk_buff *nskb;
	int diff;

	diff = data_len - e->skb->len;
	if (diff < 0) {
		if (pskb_trim(e->skb, data_len))
			return -ENOMEM;
	} else if (diff > 0) {
		if (data_len > 0xFFFF)
			return -EINVAL;
		if (diff > skb_tailroom(e->skb)) {
			nskb = skb_copy_expand(e->skb, skb_headroom(e->skb),
					       diff, GFP_ATOMIC);
			if (!nskb) {
				printk(KERN_WARNING "nf_queue: OOM "
				      "in mangle, dropping packet\n");
				return -ENOMEM;
			}
			kfree_skb(e->skb);
			e->skb = nskb;
		}
		skb_put(e->skb, diff);
	}
	if (!skb_make_writable(e->skb, data_len))
		return -ENOMEM;
	skb_copy_to_linear_data(e->skb, data, data_len);
	e->skb->ip_summed = CHECKSUM_NONE;
	return 0;
}

static int
nfqnl_set_mode(struct nfqnl_instance *queue,
	       unsigned char mode, unsigned int range)
{
	int status = 0;

	spin_lock_bh(&queue->lock);
	switch (mode) {
	case NFQNL_COPY_NONE:
	case NFQNL_COPY_META:
		queue->copy_mode = mode;
		queue->copy_range = 0;
		break;

	case NFQNL_COPY_PACKET:
		queue->copy_mode = mode;
		/* we're using struct nlattr which has 16bit nla_len */
		if (range > 0xffff)
			queue->copy_range = 0xffff;
		else
			queue->copy_range = range;
		break;

	default:
		status = -EINVAL;

	}
	spin_unlock_bh(&queue->lock);

	return status;
}

static int
dev_cmp(struct nf_queue_entry *entry, unsigned long ifindex)
{
	if (entry->indev)
		if (entry->indev->ifindex == ifindex)
			return 1;
	if (entry->outdev)
		if (entry->outdev->ifindex == ifindex)
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

	rcu_read_lock();

	for (i = 0; i < INSTANCE_BUCKETS; i++) {
		struct hlist_node *tmp;
		struct nfqnl_instance *inst;
		struct hlist_head *head = &instance_table[i];

		hlist_for_each_entry_rcu(inst, tmp, head, hlist)
			nfqnl_flush(inst, dev_cmp, ifindex);
	}

	rcu_read_unlock();
}

#define RCV_SKB_FAIL(err) do { netlink_ack(skb, nlh, (err)); return; } while (0)

static int
nfqnl_rcv_dev_event(struct notifier_block *this,
		    unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

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

	if (event == NETLINK_URELEASE && n->protocol == NETLINK_NETFILTER) {
		int i;

		/* destroy all instances for this pid */
		spin_lock(&instances_lock);
		for (i = 0; i < INSTANCE_BUCKETS; i++) {
			struct hlist_node *tmp, *t2;
			struct nfqnl_instance *inst;
			struct hlist_head *head = &instance_table[i];

			hlist_for_each_entry_safe(inst, tmp, t2, head, hlist) {
				if ((n->net == &init_net) &&
				    (n->pid == inst->peer_pid))
					__instance_destroy(inst);
			}
		}
		spin_unlock(&instances_lock);
	}
	return NOTIFY_DONE;
}

static struct notifier_block nfqnl_rtnl_notifier = {
	.notifier_call	= nfqnl_rcv_nl_event,
};

static const struct nla_policy nfqa_verdict_policy[NFQA_MAX+1] = {
	[NFQA_VERDICT_HDR]	= { .len = sizeof(struct nfqnl_msg_verdict_hdr) },
	[NFQA_MARK]		= { .type = NLA_U32 },
	[NFQA_PAYLOAD]		= { .type = NLA_UNSPEC },
};

static int
nfqnl_recv_verdict(struct sock *ctnl, struct sk_buff *skb,
		   const struct nlmsghdr *nlh,
		   const struct nlattr * const nfqa[])
{
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int16_t queue_num = ntohs(nfmsg->res_id);

	struct nfqnl_msg_verdict_hdr *vhdr;
	struct nfqnl_instance *queue;
	unsigned int verdict;
	struct nf_queue_entry *entry;
	int err;

	rcu_read_lock();
	queue = instance_lookup(queue_num);
	if (!queue) {
		err = -ENODEV;
		goto err_out_unlock;
	}

	if (queue->peer_pid != NETLINK_CB(skb).pid) {
		err = -EPERM;
		goto err_out_unlock;
	}

	if (!nfqa[NFQA_VERDICT_HDR]) {
		err = -EINVAL;
		goto err_out_unlock;
	}

	vhdr = nla_data(nfqa[NFQA_VERDICT_HDR]);
	verdict = ntohl(vhdr->verdict);

	if ((verdict & NF_VERDICT_MASK) > NF_MAX_VERDICT) {
		err = -EINVAL;
		goto err_out_unlock;
	}

	entry = find_dequeue_entry(queue, ntohl(vhdr->id));
	if (entry == NULL) {
		err = -ENOENT;
		goto err_out_unlock;
	}
	rcu_read_unlock();

	if (nfqa[NFQA_PAYLOAD]) {
		if (nfqnl_mangle(nla_data(nfqa[NFQA_PAYLOAD]),
				 nla_len(nfqa[NFQA_PAYLOAD]), entry) < 0)
			verdict = NF_DROP;
	}

	if (nfqa[NFQA_MARK])
		entry->skb->mark = ntohl(nla_get_be32(nfqa[NFQA_MARK]));

	nf_reinject(entry, verdict);
	return 0;

err_out_unlock:
	rcu_read_unlock();
	return err;
}

static int
nfqnl_recv_unsupp(struct sock *ctnl, struct sk_buff *skb,
		  const struct nlmsghdr *nlh,
		  const struct nlattr * const nfqa[])
{
	return -ENOTSUPP;
}

static const struct nla_policy nfqa_cfg_policy[NFQA_CFG_MAX+1] = {
	[NFQA_CFG_CMD]		= { .len = sizeof(struct nfqnl_msg_config_cmd) },
	[NFQA_CFG_PARAMS]	= { .len = sizeof(struct nfqnl_msg_config_params) },
};

static const struct nf_queue_handler nfqh = {
	.name 	= "nf_queue",
	.outfn	= &nfqnl_enqueue_packet,
};

static int
nfqnl_recv_config(struct sock *ctnl, struct sk_buff *skb,
		  const struct nlmsghdr *nlh,
		  const struct nlattr * const nfqa[])
{
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int16_t queue_num = ntohs(nfmsg->res_id);
	struct nfqnl_instance *queue;
	struct nfqnl_msg_config_cmd *cmd = NULL;
	int ret = 0;

	if (nfqa[NFQA_CFG_CMD]) {
		cmd = nla_data(nfqa[NFQA_CFG_CMD]);

		/* Commands without queue context - might sleep */
		switch (cmd->command) {
		case NFQNL_CFG_CMD_PF_BIND:
			return nf_register_queue_handler(ntohs(cmd->pf),
							 &nfqh);
		case NFQNL_CFG_CMD_PF_UNBIND:
			return nf_unregister_queue_handler(ntohs(cmd->pf),
							   &nfqh);
		}
	}

	rcu_read_lock();
	queue = instance_lookup(queue_num);
	if (queue && queue->peer_pid != NETLINK_CB(skb).pid) {
		ret = -EPERM;
		goto err_out_unlock;
	}

	if (cmd != NULL) {
		switch (cmd->command) {
		case NFQNL_CFG_CMD_BIND:
			if (queue) {
				ret = -EBUSY;
				goto err_out_unlock;
			}
			queue = instance_create(queue_num, NETLINK_CB(skb).pid);
			if (IS_ERR(queue)) {
				ret = PTR_ERR(queue);
				goto err_out_unlock;
			}
			break;
		case NFQNL_CFG_CMD_UNBIND:
			if (!queue) {
				ret = -ENODEV;
				goto err_out_unlock;
			}
			instance_destroy(queue);
			break;
		case NFQNL_CFG_CMD_PF_BIND:
		case NFQNL_CFG_CMD_PF_UNBIND:
			break;
		default:
			ret = -ENOTSUPP;
			break;
		}
	}

	if (nfqa[NFQA_CFG_PARAMS]) {
		struct nfqnl_msg_config_params *params;

		if (!queue) {
			ret = -ENODEV;
			goto err_out_unlock;
		}
		params = nla_data(nfqa[NFQA_CFG_PARAMS]);
		nfqnl_set_mode(queue, params->copy_mode,
				ntohl(params->copy_range));
	}

	if (nfqa[NFQA_CFG_QUEUE_MAXLEN]) {
		__be32 *queue_maxlen;

		if (!queue) {
			ret = -ENODEV;
			goto err_out_unlock;
		}
		queue_maxlen = nla_data(nfqa[NFQA_CFG_QUEUE_MAXLEN]);
		spin_lock_bh(&queue->lock);
		queue->queue_maxlen = ntohl(*queue_maxlen);
		spin_unlock_bh(&queue->lock);
	}

err_out_unlock:
	rcu_read_unlock();
	return ret;
}

static const struct nfnl_callback nfqnl_cb[NFQNL_MSG_MAX] = {
	[NFQNL_MSG_PACKET]	= { .call = nfqnl_recv_unsupp,
				    .attr_count = NFQA_MAX, },
	[NFQNL_MSG_VERDICT]	= { .call = nfqnl_recv_verdict,
				    .attr_count = NFQA_MAX,
				    .policy = nfqa_verdict_policy },
	[NFQNL_MSG_CONFIG]	= { .call = nfqnl_recv_config,
				    .attr_count = NFQA_CFG_MAX,
				    .policy = nfqa_cfg_policy },
};

static const struct nfnetlink_subsystem nfqnl_subsys = {
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
	__acquires(instances_lock)
{
	spin_lock(&instances_lock);
	return get_idx(seq, *pos);
}

static void *seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	return get_next(s, v);
}

static void seq_stop(struct seq_file *s, void *v)
	__releases(instances_lock)
{
	spin_unlock(&instances_lock);
}

static int seq_show(struct seq_file *s, void *v)
{
	const struct nfqnl_instance *inst = v;

	return seq_printf(s, "%5d %6d %5d %1d %5d %5d %5d %8d %2d\n",
			  inst->queue_num,
			  inst->peer_pid, inst->queue_total,
			  inst->copy_mode, inst->copy_range,
			  inst->queue_dropped, inst->queue_user_dropped,
			  atomic_read(&inst->id_sequence), 1);
}

static const struct seq_operations nfqnl_seq_ops = {
	.start	= seq_start,
	.next	= seq_next,
	.stop	= seq_stop,
	.show	= seq_show,
};

static int nfqnl_open(struct inode *inode, struct file *file)
{
	return seq_open_private(file, &nfqnl_seq_ops,
			sizeof(struct iter_state));
}

static const struct file_operations nfqnl_file_ops = {
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

	for (i = 0; i < INSTANCE_BUCKETS; i++)
		INIT_HLIST_HEAD(&instance_table[i]);

	netlink_register_notifier(&nfqnl_rtnl_notifier);
	status = nfnetlink_subsys_register(&nfqnl_subsys);
	if (status < 0) {
		printk(KERN_ERR "nf_queue: failed to create netlink socket\n");
		goto cleanup_netlink_notifier;
	}

#ifdef CONFIG_PROC_FS
	if (!proc_create("nfnetlink_queue", 0440,
			 proc_net_netfilter, &nfqnl_file_ops))
		goto cleanup_subsys;
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

	rcu_barrier(); /* Wait for completion of call_rcu()'s */
}

MODULE_DESCRIPTION("netfilter packet queue handler");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_QUEUE);

module_init(nfnetlink_queue_init);
module_exit(nfnetlink_queue_fini);
