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
#include <net/netns/generic.h>
#include <net/netfilter/nfnetlink_queue.h>

#include <linux/atomic.h>

#ifdef CONFIG_BRIDGE_NETFILTER
#include "../bridge/br_private.h"
#endif

#define NFQNL_QMAX_DEFAULT 1024

/* We're using struct nlattr which has 16bit nla_len. Note that nla_len
 * includes the header length. Thus, the maximum packet length that we
 * support is 65531 bytes. We send truncated packets if the specified length
 * is larger than that.  Userspace can check for presence of NFQA_CAP_LEN
 * attribute to detect truncation.
 */
#define NFQNL_MAX_COPY_RANGE (0xffff - NLA_HDRLEN)

struct nfqnl_instance {
	struct hlist_node hlist;		/* global list of queues */
	struct rcu_head rcu;

	int peer_portid;
	unsigned int queue_maxlen;
	unsigned int copy_range;
	unsigned int queue_dropped;
	unsigned int queue_user_dropped;


	u_int16_t queue_num;			/* number of this queue */
	u_int8_t copy_mode;
	u_int32_t flags;			/* Set using NFQA_CFG_FLAGS */
/*
 * Following fields are dirtied for each queued packet,
 * keep them in same cache line if possible.
 */
	spinlock_t	lock;
	unsigned int	queue_total;
	unsigned int	id_sequence;		/* 'sequence' of pkt ids */
	struct list_head queue_list;		/* packets in queue */
};

typedef int (*nfqnl_cmpfn)(struct nf_queue_entry *, unsigned long);

static int nfnl_queue_net_id __read_mostly;

#define INSTANCE_BUCKETS	16
struct nfnl_queue_net {
	spinlock_t instances_lock;
	struct hlist_head instance_table[INSTANCE_BUCKETS];
};

static struct nfnl_queue_net *nfnl_queue_pernet(struct net *net)
{
	return net_generic(net, nfnl_queue_net_id);
}

static inline u_int8_t instance_hashfn(u_int16_t queue_num)
{
	return ((queue_num >> 8) ^ queue_num) % INSTANCE_BUCKETS;
}

static struct nfqnl_instance *
instance_lookup(struct nfnl_queue_net *q, u_int16_t queue_num)
{
	struct hlist_head *head;
	struct nfqnl_instance *inst;

	head = &q->instance_table[instance_hashfn(queue_num)];
	hlist_for_each_entry_rcu(inst, head, hlist) {
		if (inst->queue_num == queue_num)
			return inst;
	}
	return NULL;
}

static struct nfqnl_instance *
instance_create(struct nfnl_queue_net *q, u_int16_t queue_num,
		int portid)
{
	struct nfqnl_instance *inst;
	unsigned int h;
	int err;

	spin_lock(&q->instances_lock);
	if (instance_lookup(q, queue_num)) {
		err = -EEXIST;
		goto out_unlock;
	}

	inst = kzalloc(sizeof(*inst), GFP_ATOMIC);
	if (!inst) {
		err = -ENOMEM;
		goto out_unlock;
	}

	inst->queue_num = queue_num;
	inst->peer_portid = portid;
	inst->queue_maxlen = NFQNL_QMAX_DEFAULT;
	inst->copy_range = NFQNL_MAX_COPY_RANGE;
	inst->copy_mode = NFQNL_COPY_NONE;
	spin_lock_init(&inst->lock);
	INIT_LIST_HEAD(&inst->queue_list);

	if (!try_module_get(THIS_MODULE)) {
		err = -EAGAIN;
		goto out_free;
	}

	h = instance_hashfn(queue_num);
	hlist_add_head_rcu(&inst->hlist, &q->instance_table[h]);

	spin_unlock(&q->instances_lock);

	return inst;

out_free:
	kfree(inst);
out_unlock:
	spin_unlock(&q->instances_lock);
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
instance_destroy(struct nfnl_queue_net *q, struct nfqnl_instance *inst)
{
	spin_lock(&q->instances_lock);
	__instance_destroy(inst);
	spin_unlock(&q->instances_lock);
}

static inline void
__enqueue_entry(struct nfqnl_instance *queue, struct nf_queue_entry *entry)
{
       list_add_tail(&entry->list, &queue->queue_list);
       queue->queue_total++;
}

static void
__dequeue_entry(struct nfqnl_instance *queue, struct nf_queue_entry *entry)
{
	list_del(&entry->list);
	queue->queue_total--;
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

	if (entry)
		__dequeue_entry(queue, entry);

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

static void
nfqnl_zcopy(struct sk_buff *to, const struct sk_buff *from, int len, int hlen)
{
	int i, j = 0;
	int plen = 0; /* length of skb->head fragment */
	struct page *page;
	unsigned int offset;

	/* dont bother with small payloads */
	if (len <= skb_tailroom(to)) {
		skb_copy_bits(from, 0, skb_put(to, len), len);
		return;
	}

	if (hlen) {
		skb_copy_bits(from, 0, skb_put(to, hlen), hlen);
		len -= hlen;
	} else {
		plen = min_t(int, skb_headlen(from), len);
		if (plen) {
			page = virt_to_head_page(from->head);
			offset = from->data - (unsigned char *)page_address(page);
			__skb_fill_page_desc(to, 0, page, offset, plen);
			get_page(page);
			j = 1;
			len -= plen;
		}
	}

	to->truesize += len + plen;
	to->len += len + plen;
	to->data_len += len + plen;

	for (i = 0; i < skb_shinfo(from)->nr_frags; i++) {
		if (!len)
			break;
		skb_shinfo(to)->frags[j] = skb_shinfo(from)->frags[i];
		skb_shinfo(to)->frags[j].size = min_t(int, skb_shinfo(to)->frags[j].size, len);
		len -= skb_shinfo(to)->frags[j].size;
		skb_frag_ref(to, j);
		j++;
	}
	skb_shinfo(to)->nr_frags = j;
}

static int
nfqnl_put_packet_info(struct sk_buff *nlskb, struct sk_buff *packet,
		      bool csum_verify)
{
	__u32 flags = 0;

	if (packet->ip_summed == CHECKSUM_PARTIAL)
		flags = NFQA_SKB_CSUMNOTREADY;
	else if (csum_verify)
		flags = NFQA_SKB_CSUM_NOTVERIFIED;

	if (skb_is_gso(packet))
		flags |= NFQA_SKB_GSO;

	return flags ? nla_put_be32(nlskb, NFQA_SKB_INFO, htonl(flags)) : 0;
}

static struct sk_buff *
nfqnl_build_packet_message(struct nfqnl_instance *queue,
			   struct nf_queue_entry *entry,
			   __be32 **packet_id_ptr)
{
	size_t size;
	size_t data_len = 0, cap_len = 0;
	int hlen = 0;
	struct sk_buff *skb;
	struct nlattr *nla;
	struct nfqnl_msg_packet_hdr *pmsg;
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct sk_buff *entskb = entry->skb;
	struct net_device *indev;
	struct net_device *outdev;
	struct nf_conn *ct = NULL;
	enum ip_conntrack_info uninitialized_var(ctinfo);
	bool csum_verify;

	size =    nlmsg_total_size(sizeof(struct nfgenmsg))
		+ nla_total_size(sizeof(struct nfqnl_msg_packet_hdr))
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
#ifdef CONFIG_BRIDGE_NETFILTER
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
#endif
		+ nla_total_size(sizeof(u_int32_t))	/* mark */
		+ nla_total_size(sizeof(struct nfqnl_msg_packet_hw))
		+ nla_total_size(sizeof(u_int32_t))	/* skbinfo */
		+ nla_total_size(sizeof(u_int32_t));	/* cap_len */

	if (entskb->tstamp.tv64)
		size += nla_total_size(sizeof(struct nfqnl_msg_packet_timestamp));

	if (entry->hook <= NF_INET_FORWARD ||
	   (entry->hook == NF_INET_POST_ROUTING && entskb->sk == NULL))
		csum_verify = !skb_csum_unnecessary(entskb);
	else
		csum_verify = false;

	outdev = entry->outdev;

	switch ((enum nfqnl_config_mode)ACCESS_ONCE(queue->copy_mode)) {
	case NFQNL_COPY_META:
	case NFQNL_COPY_NONE:
		break;

	case NFQNL_COPY_PACKET:
		if (!(queue->flags & NFQA_CFG_F_GSO) &&
		    entskb->ip_summed == CHECKSUM_PARTIAL &&
		    skb_checksum_help(entskb))
			return NULL;

		data_len = ACCESS_ONCE(queue->copy_range);
		if (data_len > entskb->len)
			data_len = entskb->len;

		if (!entskb->head_frag ||
		    skb_headlen(entskb) < L1_CACHE_BYTES ||
		    skb_shinfo(entskb)->nr_frags >= MAX_SKB_FRAGS)
			hlen = skb_headlen(entskb);

		if (skb_has_frag_list(entskb))
			hlen = entskb->len;
		hlen = min_t(int, data_len, hlen);
		size += sizeof(struct nlattr) + hlen;
		cap_len = entskb->len;
		break;
	}

	if (queue->flags & NFQA_CFG_F_CONNTRACK)
		ct = nfqnl_ct_get(entskb, &size, &ctinfo);

	skb = nfnetlink_alloc_skb(&init_net, size, queue->peer_portid,
				  GFP_ATOMIC);
	if (!skb)
		return NULL;

	nlh = nlmsg_put(skb, 0, 0,
			NFNL_SUBSYS_QUEUE << 8 | NFQNL_MSG_PACKET,
			sizeof(struct nfgenmsg), 0);
	if (!nlh) {
		kfree_skb(skb);
		return NULL;
	}
	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = entry->pf;
	nfmsg->version = NFNETLINK_V0;
	nfmsg->res_id = htons(queue->queue_num);

	nla = __nla_reserve(skb, NFQA_PACKET_HDR, sizeof(*pmsg));
	pmsg = nla_data(nla);
	pmsg->hw_protocol	= entskb->protocol;
	pmsg->hook		= entry->hook;
	*packet_id_ptr		= &pmsg->packet_id;

	indev = entry->indev;
	if (indev) {
#ifndef CONFIG_BRIDGE_NETFILTER
		if (nla_put_be32(skb, NFQA_IFINDEX_INDEV, htonl(indev->ifindex)))
			goto nla_put_failure;
#else
		if (entry->pf == PF_BRIDGE) {
			/* Case 1: indev is physical input device, we need to
			 * look for bridge group (when called from
			 * netfilter_bridge) */
			if (nla_put_be32(skb, NFQA_IFINDEX_PHYSINDEV,
					 htonl(indev->ifindex)) ||
			/* this is the bridge group "brX" */
			/* rcu_read_lock()ed by __nf_queue */
			    nla_put_be32(skb, NFQA_IFINDEX_INDEV,
					 htonl(br_port_get_rcu(indev)->br->dev->ifindex)))
				goto nla_put_failure;
		} else {
			/* Case 2: indev is bridge group, we need to look for
			 * physical device (when called from ipv4) */
			if (nla_put_be32(skb, NFQA_IFINDEX_INDEV,
					 htonl(indev->ifindex)))
				goto nla_put_failure;
			if (entskb->nf_bridge && entskb->nf_bridge->physindev &&
			    nla_put_be32(skb, NFQA_IFINDEX_PHYSINDEV,
					 htonl(entskb->nf_bridge->physindev->ifindex)))
				goto nla_put_failure;
		}
#endif
	}

	if (outdev) {
#ifndef CONFIG_BRIDGE_NETFILTER
		if (nla_put_be32(skb, NFQA_IFINDEX_OUTDEV, htonl(outdev->ifindex)))
			goto nla_put_failure;
#else
		if (entry->pf == PF_BRIDGE) {
			/* Case 1: outdev is physical output device, we need to
			 * look for bridge group (when called from
			 * netfilter_bridge) */
			if (nla_put_be32(skb, NFQA_IFINDEX_PHYSOUTDEV,
					 htonl(outdev->ifindex)) ||
			/* this is the bridge group "brX" */
			/* rcu_read_lock()ed by __nf_queue */
			    nla_put_be32(skb, NFQA_IFINDEX_OUTDEV,
					 htonl(br_port_get_rcu(outdev)->br->dev->ifindex)))
				goto nla_put_failure;
		} else {
			/* Case 2: outdev is bridge group, we need to look for
			 * physical output device (when called from ipv4) */
			if (nla_put_be32(skb, NFQA_IFINDEX_OUTDEV,
					 htonl(outdev->ifindex)))
				goto nla_put_failure;
			if (entskb->nf_bridge && entskb->nf_bridge->physoutdev &&
			    nla_put_be32(skb, NFQA_IFINDEX_PHYSOUTDEV,
					 htonl(entskb->nf_bridge->physoutdev->ifindex)))
				goto nla_put_failure;
		}
#endif
	}

	if (entskb->mark &&
	    nla_put_be32(skb, NFQA_MARK, htonl(entskb->mark)))
		goto nla_put_failure;

	if (indev && entskb->dev &&
	    entskb->mac_header != entskb->network_header) {
		struct nfqnl_msg_packet_hw phw;
		int len;

		memset(&phw, 0, sizeof(phw));
		len = dev_parse_header(entskb, phw.hw_addr);
		if (len) {
			phw.hw_addrlen = htons(len);
			if (nla_put(skb, NFQA_HWADDR, sizeof(phw), &phw))
				goto nla_put_failure;
		}
	}

	if (entskb->tstamp.tv64) {
		struct nfqnl_msg_packet_timestamp ts;
		struct timeval tv = ktime_to_timeval(entskb->tstamp);
		ts.sec = cpu_to_be64(tv.tv_sec);
		ts.usec = cpu_to_be64(tv.tv_usec);

		if (nla_put(skb, NFQA_TIMESTAMP, sizeof(ts), &ts))
			goto nla_put_failure;
	}

	if (ct && nfqnl_ct_put(skb, ct, ctinfo) < 0)
		goto nla_put_failure;

	if (cap_len > data_len &&
	    nla_put_be32(skb, NFQA_CAP_LEN, htonl(cap_len)))
		goto nla_put_failure;

	if (nfqnl_put_packet_info(skb, entskb, csum_verify))
		goto nla_put_failure;

	if (data_len) {
		struct nlattr *nla;

		if (skb_tailroom(skb) < sizeof(*nla) + hlen)
			goto nla_put_failure;

		nla = (struct nlattr *)skb_put(skb, sizeof(*nla));
		nla->nla_type = NFQA_PAYLOAD;
		nla->nla_len = nla_attr_size(data_len);

		nfqnl_zcopy(skb, entskb, data_len, hlen);
	}

	nlh->nlmsg_len = skb->len;
	return skb;

nla_put_failure:
	kfree_skb(skb);
	net_err_ratelimited("nf_queue: error creating packet message\n");
	return NULL;
}

static int
__nfqnl_enqueue_packet(struct net *net, struct nfqnl_instance *queue,
			struct nf_queue_entry *entry)
{
	struct sk_buff *nskb;
	int err = -ENOBUFS;
	__be32 *packet_id_ptr;
	int failopen = 0;

	nskb = nfqnl_build_packet_message(queue, entry, &packet_id_ptr);
	if (nskb == NULL) {
		err = -ENOMEM;
		goto err_out;
	}
	spin_lock_bh(&queue->lock);

	if (queue->queue_total >= queue->queue_maxlen) {
		if (queue->flags & NFQA_CFG_F_FAIL_OPEN) {
			failopen = 1;
			err = 0;
		} else {
			queue->queue_dropped++;
			net_warn_ratelimited("nf_queue: full at %d entries, dropping packets(s)\n",
					     queue->queue_total);
		}
		goto err_out_free_nskb;
	}
	entry->id = ++queue->id_sequence;
	*packet_id_ptr = htonl(entry->id);

	/* nfnetlink_unicast will either free the nskb or add it to a socket */
	err = nfnetlink_unicast(nskb, net, queue->peer_portid, MSG_DONTWAIT);
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
	if (failopen)
		nf_reinject(entry, NF_ACCEPT);
err_out:
	return err;
}

static struct nf_queue_entry *
nf_queue_entry_dup(struct nf_queue_entry *e)
{
	struct nf_queue_entry *entry = kmemdup(e, e->size, GFP_ATOMIC);
	if (entry) {
		if (nf_queue_entry_get_refs(entry))
			return entry;
		kfree(entry);
	}
	return NULL;
}

#ifdef CONFIG_BRIDGE_NETFILTER
/* When called from bridge netfilter, skb->data must point to MAC header
 * before calling skb_gso_segment(). Else, original MAC header is lost
 * and segmented skbs will be sent to wrong destination.
 */
static void nf_bridge_adjust_skb_data(struct sk_buff *skb)
{
	if (skb->nf_bridge)
		__skb_push(skb, skb->network_header - skb->mac_header);
}

static void nf_bridge_adjust_segmented_data(struct sk_buff *skb)
{
	if (skb->nf_bridge)
		__skb_pull(skb, skb->network_header - skb->mac_header);
}
#else
#define nf_bridge_adjust_skb_data(s) do {} while (0)
#define nf_bridge_adjust_segmented_data(s) do {} while (0)
#endif

static void free_entry(struct nf_queue_entry *entry)
{
	nf_queue_entry_release_refs(entry);
	kfree(entry);
}

static int
__nfqnl_enqueue_packet_gso(struct net *net, struct nfqnl_instance *queue,
			   struct sk_buff *skb, struct nf_queue_entry *entry)
{
	int ret = -ENOMEM;
	struct nf_queue_entry *entry_seg;

	nf_bridge_adjust_segmented_data(skb);

	if (skb->next == NULL) { /* last packet, no need to copy entry */
		struct sk_buff *gso_skb = entry->skb;
		entry->skb = skb;
		ret = __nfqnl_enqueue_packet(net, queue, entry);
		if (ret)
			entry->skb = gso_skb;
		return ret;
	}

	skb->next = NULL;

	entry_seg = nf_queue_entry_dup(entry);
	if (entry_seg) {
		entry_seg->skb = skb;
		ret = __nfqnl_enqueue_packet(net, queue, entry_seg);
		if (ret)
			free_entry(entry_seg);
	}
	return ret;
}

static int
nfqnl_enqueue_packet(struct nf_queue_entry *entry, unsigned int queuenum)
{
	unsigned int queued;
	struct nfqnl_instance *queue;
	struct sk_buff *skb, *segs;
	int err = -ENOBUFS;
	struct net *net = dev_net(entry->indev ?
				  entry->indev : entry->outdev);
	struct nfnl_queue_net *q = nfnl_queue_pernet(net);

	/* rcu_read_lock()ed by nf_hook_slow() */
	queue = instance_lookup(q, queuenum);
	if (!queue)
		return -ESRCH;

	if (queue->copy_mode == NFQNL_COPY_NONE)
		return -EINVAL;

	skb = entry->skb;

	switch (entry->pf) {
	case NFPROTO_IPV4:
		skb->protocol = htons(ETH_P_IP);
		break;
	case NFPROTO_IPV6:
		skb->protocol = htons(ETH_P_IPV6);
		break;
	}

	if ((queue->flags & NFQA_CFG_F_GSO) || !skb_is_gso(skb))
		return __nfqnl_enqueue_packet(net, queue, entry);

	nf_bridge_adjust_skb_data(skb);
	segs = skb_gso_segment(skb, 0);
	/* Does not use PTR_ERR to limit the number of error codes that can be
	 * returned by nf_queue.  For instance, callers rely on -ECANCELED to
	 * mean 'ignore this hook'.
	 */
	if (IS_ERR(segs))
		goto out_err;
	queued = 0;
	err = 0;
	do {
		struct sk_buff *nskb = segs->next;
		if (err == 0)
			err = __nfqnl_enqueue_packet_gso(net, queue,
							segs, entry);
		if (err == 0)
			queued++;
		else
			kfree_skb(segs);
		segs = nskb;
	} while (segs);

	if (queued) {
		if (err) /* some segments are already queued */
			free_entry(entry);
		kfree_skb(skb);
		return 0;
	}
 out_err:
	nf_bridge_adjust_segmented_data(skb);
	return err;
}

static int
nfqnl_mangle(void *data, int data_len, struct nf_queue_entry *e, int diff)
{
	struct sk_buff *nskb;

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
		if (range == 0 || range > NFQNL_MAX_COPY_RANGE)
			queue->copy_range = NFQNL_MAX_COPY_RANGE;
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
nfqnl_dev_drop(struct net *net, int ifindex)
{
	int i;
	struct nfnl_queue_net *q = nfnl_queue_pernet(net);

	rcu_read_lock();

	for (i = 0; i < INSTANCE_BUCKETS; i++) {
		struct nfqnl_instance *inst;
		struct hlist_head *head = &q->instance_table[i];

		hlist_for_each_entry_rcu(inst, head, hlist)
			nfqnl_flush(inst, dev_cmp, ifindex);
	}

	rcu_read_unlock();
}

#define RCV_SKB_FAIL(err) do { netlink_ack(skb, nlh, (err)); return; } while (0)

static int
nfqnl_rcv_dev_event(struct notifier_block *this,
		    unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	/* Drop any packets associated with the downed device */
	if (event == NETDEV_DOWN)
		nfqnl_dev_drop(dev_net(dev), dev->ifindex);
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
	struct nfnl_queue_net *q = nfnl_queue_pernet(n->net);

	if (event == NETLINK_URELEASE && n->protocol == NETLINK_NETFILTER) {
		int i;

		/* destroy all instances for this portid */
		spin_lock(&q->instances_lock);
		for (i = 0; i < INSTANCE_BUCKETS; i++) {
			struct hlist_node *t2;
			struct nfqnl_instance *inst;
			struct hlist_head *head = &q->instance_table[i];

			hlist_for_each_entry_safe(inst, t2, head, hlist) {
				if (n->portid == inst->peer_portid)
					__instance_destroy(inst);
			}
		}
		spin_unlock(&q->instances_lock);
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
	[NFQA_CT]		= { .type = NLA_UNSPEC },
	[NFQA_EXP]		= { .type = NLA_UNSPEC },
};

static const struct nla_policy nfqa_verdict_batch_policy[NFQA_MAX+1] = {
	[NFQA_VERDICT_HDR]	= { .len = sizeof(struct nfqnl_msg_verdict_hdr) },
	[NFQA_MARK]		= { .type = NLA_U32 },
};

static struct nfqnl_instance *
verdict_instance_lookup(struct nfnl_queue_net *q, u16 queue_num, int nlportid)
{
	struct nfqnl_instance *queue;

	queue = instance_lookup(q, queue_num);
	if (!queue)
		return ERR_PTR(-ENODEV);

	if (queue->peer_portid != nlportid)
		return ERR_PTR(-EPERM);

	return queue;
}

static struct nfqnl_msg_verdict_hdr*
verdicthdr_get(const struct nlattr * const nfqa[])
{
	struct nfqnl_msg_verdict_hdr *vhdr;
	unsigned int verdict;

	if (!nfqa[NFQA_VERDICT_HDR])
		return NULL;

	vhdr = nla_data(nfqa[NFQA_VERDICT_HDR]);
	verdict = ntohl(vhdr->verdict) & NF_VERDICT_MASK;
	if (verdict > NF_MAX_VERDICT || verdict == NF_STOLEN)
		return NULL;
	return vhdr;
}

static int nfq_id_after(unsigned int id, unsigned int max)
{
	return (int)(id - max) > 0;
}

static int
nfqnl_recv_verdict_batch(struct sock *ctnl, struct sk_buff *skb,
		   const struct nlmsghdr *nlh,
		   const struct nlattr * const nfqa[])
{
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	struct nf_queue_entry *entry, *tmp;
	unsigned int verdict, maxid;
	struct nfqnl_msg_verdict_hdr *vhdr;
	struct nfqnl_instance *queue;
	LIST_HEAD(batch_list);
	u16 queue_num = ntohs(nfmsg->res_id);

	struct net *net = sock_net(ctnl);
	struct nfnl_queue_net *q = nfnl_queue_pernet(net);

	queue = verdict_instance_lookup(q, queue_num,
					NETLINK_CB(skb).portid);
	if (IS_ERR(queue))
		return PTR_ERR(queue);

	vhdr = verdicthdr_get(nfqa);
	if (!vhdr)
		return -EINVAL;

	verdict = ntohl(vhdr->verdict);
	maxid = ntohl(vhdr->id);

	spin_lock_bh(&queue->lock);

	list_for_each_entry_safe(entry, tmp, &queue->queue_list, list) {
		if (nfq_id_after(entry->id, maxid))
			break;
		__dequeue_entry(queue, entry);
		list_add_tail(&entry->list, &batch_list);
	}

	spin_unlock_bh(&queue->lock);

	if (list_empty(&batch_list))
		return -ENOENT;

	list_for_each_entry_safe(entry, tmp, &batch_list, list) {
		if (nfqa[NFQA_MARK])
			entry->skb->mark = ntohl(nla_get_be32(nfqa[NFQA_MARK]));
		nf_reinject(entry, verdict);
	}
	return 0;
}

static int
nfqnl_recv_verdict(struct sock *ctnl, struct sk_buff *skb,
		   const struct nlmsghdr *nlh,
		   const struct nlattr * const nfqa[])
{
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int16_t queue_num = ntohs(nfmsg->res_id);

	struct nfqnl_msg_verdict_hdr *vhdr;
	struct nfqnl_instance *queue;
	unsigned int verdict;
	struct nf_queue_entry *entry;
	enum ip_conntrack_info uninitialized_var(ctinfo);
	struct nf_conn *ct = NULL;

	struct net *net = sock_net(ctnl);
	struct nfnl_queue_net *q = nfnl_queue_pernet(net);

	queue = instance_lookup(q, queue_num);
	if (!queue)
		queue = verdict_instance_lookup(q, queue_num,
						NETLINK_CB(skb).portid);
	if (IS_ERR(queue))
		return PTR_ERR(queue);

	vhdr = verdicthdr_get(nfqa);
	if (!vhdr)
		return -EINVAL;

	verdict = ntohl(vhdr->verdict);

	entry = find_dequeue_entry(queue, ntohl(vhdr->id));
	if (entry == NULL)
		return -ENOENT;

	if (nfqa[NFQA_CT]) {
		ct = nfqnl_ct_parse(entry->skb, nfqa[NFQA_CT], &ctinfo);
		if (ct && nfqa[NFQA_EXP]) {
			nfqnl_attach_expect(ct, nfqa[NFQA_EXP],
					    NETLINK_CB(skb).portid,
					    nlmsg_report(nlh));
		}
	}

	if (nfqa[NFQA_PAYLOAD]) {
		u16 payload_len = nla_len(nfqa[NFQA_PAYLOAD]);
		int diff = payload_len - entry->skb->len;

		if (nfqnl_mangle(nla_data(nfqa[NFQA_PAYLOAD]),
				 payload_len, entry, diff) < 0)
			verdict = NF_DROP;

		if (ct)
			nfqnl_ct_seq_adjust(skb, ct, ctinfo, diff);
	}

	if (nfqa[NFQA_MARK])
		entry->skb->mark = ntohl(nla_get_be32(nfqa[NFQA_MARK]));

	nf_reinject(entry, verdict);
	return 0;
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
	.outfn	= &nfqnl_enqueue_packet,
};

static int
nfqnl_recv_config(struct sock *ctnl, struct sk_buff *skb,
		  const struct nlmsghdr *nlh,
		  const struct nlattr * const nfqa[])
{
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int16_t queue_num = ntohs(nfmsg->res_id);
	struct nfqnl_instance *queue;
	struct nfqnl_msg_config_cmd *cmd = NULL;
	struct net *net = sock_net(ctnl);
	struct nfnl_queue_net *q = nfnl_queue_pernet(net);
	int ret = 0;

	if (nfqa[NFQA_CFG_CMD]) {
		cmd = nla_data(nfqa[NFQA_CFG_CMD]);

		/* Obsolete commands without queue context */
		switch (cmd->command) {
		case NFQNL_CFG_CMD_PF_BIND: return 0;
		case NFQNL_CFG_CMD_PF_UNBIND: return 0;
		}
	}

	rcu_read_lock();
	queue = instance_lookup(q, queue_num);
	if (queue && queue->peer_portid != NETLINK_CB(skb).portid) {
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
			queue = instance_create(q, queue_num,
						NETLINK_CB(skb).portid);
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
			instance_destroy(q, queue);
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

	if (nfqa[NFQA_CFG_FLAGS]) {
		__u32 flags, mask;

		if (!queue) {
			ret = -ENODEV;
			goto err_out_unlock;
		}

		if (!nfqa[NFQA_CFG_MASK]) {
			/* A mask is needed to specify which flags are being
			 * changed.
			 */
			ret = -EINVAL;
			goto err_out_unlock;
		}

		flags = ntohl(nla_get_be32(nfqa[NFQA_CFG_FLAGS]));
		mask = ntohl(nla_get_be32(nfqa[NFQA_CFG_MASK]));

		if (flags >= NFQA_CFG_F_MAX) {
			ret = -EOPNOTSUPP;
			goto err_out_unlock;
		}

		spin_lock_bh(&queue->lock);
		queue->flags &= ~mask;
		queue->flags |= flags & mask;
		spin_unlock_bh(&queue->lock);
	}

err_out_unlock:
	rcu_read_unlock();
	return ret;
}

static const struct nfnl_callback nfqnl_cb[NFQNL_MSG_MAX] = {
	[NFQNL_MSG_PACKET]	= { .call_rcu = nfqnl_recv_unsupp,
				    .attr_count = NFQA_MAX, },
	[NFQNL_MSG_VERDICT]	= { .call_rcu = nfqnl_recv_verdict,
				    .attr_count = NFQA_MAX,
				    .policy = nfqa_verdict_policy },
	[NFQNL_MSG_CONFIG]	= { .call = nfqnl_recv_config,
				    .attr_count = NFQA_CFG_MAX,
				    .policy = nfqa_cfg_policy },
	[NFQNL_MSG_VERDICT_BATCH]={ .call_rcu = nfqnl_recv_verdict_batch,
				    .attr_count = NFQA_MAX,
				    .policy = nfqa_verdict_batch_policy },
};

static const struct nfnetlink_subsystem nfqnl_subsys = {
	.name		= "nf_queue",
	.subsys_id	= NFNL_SUBSYS_QUEUE,
	.cb_count	= NFQNL_MSG_MAX,
	.cb		= nfqnl_cb,
};

#ifdef CONFIG_PROC_FS
struct iter_state {
	struct seq_net_private p;
	unsigned int bucket;
};

static struct hlist_node *get_first(struct seq_file *seq)
{
	struct iter_state *st = seq->private;
	struct net *net;
	struct nfnl_queue_net *q;

	if (!st)
		return NULL;

	net = seq_file_net(seq);
	q = nfnl_queue_pernet(net);
	for (st->bucket = 0; st->bucket < INSTANCE_BUCKETS; st->bucket++) {
		if (!hlist_empty(&q->instance_table[st->bucket]))
			return q->instance_table[st->bucket].first;
	}
	return NULL;
}

static struct hlist_node *get_next(struct seq_file *seq, struct hlist_node *h)
{
	struct iter_state *st = seq->private;
	struct net *net = seq_file_net(seq);

	h = h->next;
	while (!h) {
		struct nfnl_queue_net *q;

		if (++st->bucket >= INSTANCE_BUCKETS)
			return NULL;

		q = nfnl_queue_pernet(net);
		h = q->instance_table[st->bucket].first;
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

static void *seq_start(struct seq_file *s, loff_t *pos)
	__acquires(nfnl_queue_pernet(seq_file_net(s))->instances_lock)
{
	spin_lock(&nfnl_queue_pernet(seq_file_net(s))->instances_lock);
	return get_idx(s, *pos);
}

static void *seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	return get_next(s, v);
}

static void seq_stop(struct seq_file *s, void *v)
	__releases(nfnl_queue_pernet(seq_file_net(s))->instances_lock)
{
	spin_unlock(&nfnl_queue_pernet(seq_file_net(s))->instances_lock);
}

static int seq_show(struct seq_file *s, void *v)
{
	const struct nfqnl_instance *inst = v;

	return seq_printf(s, "%5d %6d %5d %1d %5d %5d %5d %8d %2d\n",
			  inst->queue_num,
			  inst->peer_portid, inst->queue_total,
			  inst->copy_mode, inst->copy_range,
			  inst->queue_dropped, inst->queue_user_dropped,
			  inst->id_sequence, 1);
}

static const struct seq_operations nfqnl_seq_ops = {
	.start	= seq_start,
	.next	= seq_next,
	.stop	= seq_stop,
	.show	= seq_show,
};

static int nfqnl_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &nfqnl_seq_ops,
			sizeof(struct iter_state));
}

static const struct file_operations nfqnl_file_ops = {
	.owner	 = THIS_MODULE,
	.open	 = nfqnl_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_net,
};

#endif /* PROC_FS */

static int __net_init nfnl_queue_net_init(struct net *net)
{
	unsigned int i;
	struct nfnl_queue_net *q = nfnl_queue_pernet(net);

	for (i = 0; i < INSTANCE_BUCKETS; i++)
		INIT_HLIST_HEAD(&q->instance_table[i]);

	spin_lock_init(&q->instances_lock);

#ifdef CONFIG_PROC_FS
	if (!proc_create("nfnetlink_queue", 0440,
			 net->nf.proc_netfilter, &nfqnl_file_ops))
		return -ENOMEM;
#endif
	return 0;
}

static void __net_exit nfnl_queue_net_exit(struct net *net)
{
#ifdef CONFIG_PROC_FS
	remove_proc_entry("nfnetlink_queue", net->nf.proc_netfilter);
#endif
}

static struct pernet_operations nfnl_queue_net_ops = {
	.init	= nfnl_queue_net_init,
	.exit	= nfnl_queue_net_exit,
	.id	= &nfnl_queue_net_id,
	.size	= sizeof(struct nfnl_queue_net),
};

static int __init nfnetlink_queue_init(void)
{
	int status = -ENOMEM;

	netlink_register_notifier(&nfqnl_rtnl_notifier);
	status = nfnetlink_subsys_register(&nfqnl_subsys);
	if (status < 0) {
		pr_err("nf_queue: failed to create netlink socket\n");
		goto cleanup_netlink_notifier;
	}

	status = register_pernet_subsys(&nfnl_queue_net_ops);
	if (status < 0) {
		pr_err("nf_queue: failed to register pernet ops\n");
		goto cleanup_subsys;
	}
	register_netdevice_notifier(&nfqnl_dev_notifier);
	nf_register_queue_handler(&nfqh);
	return status;

cleanup_subsys:
	nfnetlink_subsys_unregister(&nfqnl_subsys);
cleanup_netlink_notifier:
	netlink_unregister_notifier(&nfqnl_rtnl_notifier);
	return status;
}

static void __exit nfnetlink_queue_fini(void)
{
	nf_unregister_queue_handler();
	unregister_netdevice_notifier(&nfqnl_dev_notifier);
	unregister_pernet_subsys(&nfnl_queue_net_ops);
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
