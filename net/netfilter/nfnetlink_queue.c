// SPDX-License-Identifier: GPL-2.0-only
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/netfilter_bridge.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_queue.h>
#include <linux/netfilter/nf_conntrack_common.h>
#include <linux/list.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/netfilter/nf_queue.h>
#include <net/netns/generic.h>

#include <linux/atomic.h>

#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
#include "../bridge/br_private.h"
#endif

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <net/netfilter/nf_conntrack.h>
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

	u32 peer_portid;
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
	spinlock_t	lock	____cacheline_aligned_in_smp;
	unsigned int	queue_total;
	unsigned int	id_sequence;		/* 'sequence' of pkt ids */
	struct list_head queue_list;		/* packets in queue */
};

typedef int (*nfqnl_cmpfn)(struct nf_queue_entry *, unsigned long);

static unsigned int nfnl_queue_net_id __read_mostly;

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
instance_create(struct nfnl_queue_net *q, u_int16_t queue_num, u32 portid)
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

static void nfqnl_reinject(struct nf_queue_entry *entry, unsigned int verdict)
{
	struct nf_ct_hook *ct_hook;
	int err;

	if (verdict == NF_ACCEPT ||
	    verdict == NF_REPEAT ||
	    verdict == NF_STOP) {
		rcu_read_lock();
		ct_hook = rcu_dereference(nf_ct_hook);
		if (ct_hook) {
			err = ct_hook->update(entry->state.net, entry->skb);
			if (err < 0)
				verdict = NF_DROP;
		}
		rcu_read_unlock();
	}
	nf_reinject(entry, verdict);
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
			nfqnl_reinject(entry, NF_DROP);
		}
	}
	spin_unlock_bh(&queue->lock);
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

static int nfqnl_put_sk_uidgid(struct sk_buff *skb, struct sock *sk)
{
	const struct cred *cred;

	if (!sk_fullsock(sk))
		return 0;

	read_lock_bh(&sk->sk_callback_lock);
	if (sk->sk_socket && sk->sk_socket->file) {
		cred = sk->sk_socket->file->f_cred;
		if (nla_put_be32(skb, NFQA_UID,
		    htonl(from_kuid_munged(&init_user_ns, cred->fsuid))))
			goto nla_put_failure;
		if (nla_put_be32(skb, NFQA_GID,
		    htonl(from_kgid_munged(&init_user_ns, cred->fsgid))))
			goto nla_put_failure;
	}
	read_unlock_bh(&sk->sk_callback_lock);
	return 0;

nla_put_failure:
	read_unlock_bh(&sk->sk_callback_lock);
	return -1;
}

static u32 nfqnl_get_sk_secctx(struct sk_buff *skb, char **secdata)
{
	u32 seclen = 0;
#if IS_ENABLED(CONFIG_NETWORK_SECMARK)
	if (!skb || !sk_fullsock(skb->sk))
		return 0;

	read_lock_bh(&skb->sk->sk_callback_lock);

	if (skb->secmark)
		security_secid_to_secctx(skb->secmark, secdata, &seclen);

	read_unlock_bh(&skb->sk->sk_callback_lock);
#endif
	return seclen;
}

static u32 nfqnl_get_bridge_size(struct nf_queue_entry *entry)
{
	struct sk_buff *entskb = entry->skb;
	u32 nlalen = 0;

	if (entry->state.pf != PF_BRIDGE || !skb_mac_header_was_set(entskb))
		return 0;

	if (skb_vlan_tag_present(entskb))
		nlalen += nla_total_size(nla_total_size(sizeof(__be16)) +
					 nla_total_size(sizeof(__be16)));

	if (entskb->network_header > entskb->mac_header)
		nlalen += nla_total_size((entskb->network_header -
					  entskb->mac_header));

	return nlalen;
}

static int nfqnl_put_bridge(struct nf_queue_entry *entry, struct sk_buff *skb)
{
	struct sk_buff *entskb = entry->skb;

	if (entry->state.pf != PF_BRIDGE || !skb_mac_header_was_set(entskb))
		return 0;

	if (skb_vlan_tag_present(entskb)) {
		struct nlattr *nest;

		nest = nla_nest_start(skb, NFQA_VLAN);
		if (!nest)
			goto nla_put_failure;

		if (nla_put_be16(skb, NFQA_VLAN_TCI, htons(entskb->vlan_tci)) ||
		    nla_put_be16(skb, NFQA_VLAN_PROTO, entskb->vlan_proto))
			goto nla_put_failure;

		nla_nest_end(skb, nest);
	}

	if (entskb->mac_header < entskb->network_header) {
		int len = (int)(entskb->network_header - entskb->mac_header);

		if (nla_put(skb, NFQA_L2HDR, len, skb_mac_header(entskb)))
			goto nla_put_failure;
	}

	return 0;

nla_put_failure:
	return -1;
}

static struct sk_buff *
nfqnl_build_packet_message(struct net *net, struct nfqnl_instance *queue,
			   struct nf_queue_entry *entry,
			   __be32 **packet_id_ptr)
{
	size_t size;
	size_t data_len = 0, cap_len = 0;
	unsigned int hlen = 0;
	struct sk_buff *skb;
	struct nlattr *nla;
	struct nfqnl_msg_packet_hdr *pmsg;
	struct nlmsghdr *nlh;
	struct sk_buff *entskb = entry->skb;
	struct net_device *indev;
	struct net_device *outdev;
	struct nf_conn *ct = NULL;
	enum ip_conntrack_info ctinfo;
	struct nfnl_ct_hook *nfnl_ct;
	bool csum_verify;
	char *secdata = NULL;
	u32 seclen = 0;

	size = nlmsg_total_size(sizeof(struct nfgenmsg))
		+ nla_total_size(sizeof(struct nfqnl_msg_packet_hdr))
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
#endif
		+ nla_total_size(sizeof(u_int32_t))	/* mark */
		+ nla_total_size(sizeof(struct nfqnl_msg_packet_hw))
		+ nla_total_size(sizeof(u_int32_t))	/* skbinfo */
		+ nla_total_size(sizeof(u_int32_t));	/* cap_len */

	if (entskb->tstamp)
		size += nla_total_size(sizeof(struct nfqnl_msg_packet_timestamp));

	size += nfqnl_get_bridge_size(entry);

	if (entry->state.hook <= NF_INET_FORWARD ||
	   (entry->state.hook == NF_INET_POST_ROUTING && entskb->sk == NULL))
		csum_verify = !skb_csum_unnecessary(entskb);
	else
		csum_verify = false;

	outdev = entry->state.out;

	switch ((enum nfqnl_config_mode)READ_ONCE(queue->copy_mode)) {
	case NFQNL_COPY_META:
	case NFQNL_COPY_NONE:
		break;

	case NFQNL_COPY_PACKET:
		if (!(queue->flags & NFQA_CFG_F_GSO) &&
		    entskb->ip_summed == CHECKSUM_PARTIAL &&
		    skb_checksum_help(entskb))
			return NULL;

		data_len = READ_ONCE(queue->copy_range);
		if (data_len > entskb->len)
			data_len = entskb->len;

		hlen = skb_zerocopy_headlen(entskb);
		hlen = min_t(unsigned int, hlen, data_len);
		size += sizeof(struct nlattr) + hlen;
		cap_len = entskb->len;
		break;
	}

	nfnl_ct = rcu_dereference(nfnl_ct_hook);

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	if (queue->flags & NFQA_CFG_F_CONNTRACK) {
		if (nfnl_ct != NULL) {
			ct = nf_ct_get(entskb, &ctinfo);
			if (ct != NULL)
				size += nfnl_ct->build_size(ct);
		}
	}
#endif

	if (queue->flags & NFQA_CFG_F_UID_GID) {
		size += (nla_total_size(sizeof(u_int32_t))	/* uid */
			+ nla_total_size(sizeof(u_int32_t)));	/* gid */
	}

	if ((queue->flags & NFQA_CFG_F_SECCTX) && entskb->sk) {
		seclen = nfqnl_get_sk_secctx(entskb, &secdata);
		if (seclen)
			size += nla_total_size(seclen);
	}

	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb) {
		skb_tx_error(entskb);
		goto nlmsg_failure;
	}

	nlh = nfnl_msg_put(skb, 0, 0,
			   nfnl_msg_type(NFNL_SUBSYS_QUEUE, NFQNL_MSG_PACKET),
			   0, entry->state.pf, NFNETLINK_V0,
			   htons(queue->queue_num));
	if (!nlh) {
		skb_tx_error(entskb);
		kfree_skb(skb);
		goto nlmsg_failure;
	}

	nla = __nla_reserve(skb, NFQA_PACKET_HDR, sizeof(*pmsg));
	pmsg = nla_data(nla);
	pmsg->hw_protocol	= entskb->protocol;
	pmsg->hook		= entry->state.hook;
	*packet_id_ptr		= &pmsg->packet_id;

	indev = entry->state.in;
	if (indev) {
#if !IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
		if (nla_put_be32(skb, NFQA_IFINDEX_INDEV, htonl(indev->ifindex)))
			goto nla_put_failure;
#else
		if (entry->state.pf == PF_BRIDGE) {
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
			int physinif;

			/* Case 2: indev is bridge group, we need to look for
			 * physical device (when called from ipv4) */
			if (nla_put_be32(skb, NFQA_IFINDEX_INDEV,
					 htonl(indev->ifindex)))
				goto nla_put_failure;

			physinif = nf_bridge_get_physinif(entskb);
			if (physinif &&
			    nla_put_be32(skb, NFQA_IFINDEX_PHYSINDEV,
					 htonl(physinif)))
				goto nla_put_failure;
		}
#endif
	}

	if (outdev) {
#if !IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
		if (nla_put_be32(skb, NFQA_IFINDEX_OUTDEV, htonl(outdev->ifindex)))
			goto nla_put_failure;
#else
		if (entry->state.pf == PF_BRIDGE) {
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
			int physoutif;

			/* Case 2: outdev is bridge group, we need to look for
			 * physical output device (when called from ipv4) */
			if (nla_put_be32(skb, NFQA_IFINDEX_OUTDEV,
					 htonl(outdev->ifindex)))
				goto nla_put_failure;

			physoutif = nf_bridge_get_physoutif(entskb);
			if (physoutif &&
			    nla_put_be32(skb, NFQA_IFINDEX_PHYSOUTDEV,
					 htonl(physoutif)))
				goto nla_put_failure;
		}
#endif
	}

	if (entskb->mark &&
	    nla_put_be32(skb, NFQA_MARK, htonl(entskb->mark)))
		goto nla_put_failure;

	if (indev && entskb->dev &&
	    skb_mac_header_was_set(entskb) &&
	    skb_mac_header_len(entskb) != 0) {
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

	if (nfqnl_put_bridge(entry, skb) < 0)
		goto nla_put_failure;

	if (entry->state.hook <= NF_INET_FORWARD && entskb->tstamp) {
		struct nfqnl_msg_packet_timestamp ts;
		struct timespec64 kts = ktime_to_timespec64(entskb->tstamp);

		ts.sec = cpu_to_be64(kts.tv_sec);
		ts.usec = cpu_to_be64(kts.tv_nsec / NSEC_PER_USEC);

		if (nla_put(skb, NFQA_TIMESTAMP, sizeof(ts), &ts))
			goto nla_put_failure;
	}

	if ((queue->flags & NFQA_CFG_F_UID_GID) && entskb->sk &&
	    nfqnl_put_sk_uidgid(skb, entskb->sk) < 0)
		goto nla_put_failure;

	if (seclen && nla_put(skb, NFQA_SECCTX, seclen, secdata))
		goto nla_put_failure;

	if (ct && nfnl_ct->build(skb, ct, ctinfo, NFQA_CT, NFQA_CT_INFO) < 0)
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

		nla = skb_put(skb, sizeof(*nla));
		nla->nla_type = NFQA_PAYLOAD;
		nla->nla_len = nla_attr_size(data_len);

		if (skb_zerocopy(skb, entskb, data_len, hlen))
			goto nla_put_failure;
	}

	nlh->nlmsg_len = skb->len;
	if (seclen)
		security_release_secctx(secdata, seclen);
	return skb;

nla_put_failure:
	skb_tx_error(entskb);
	kfree_skb(skb);
	net_err_ratelimited("nf_queue: error creating packet message\n");
nlmsg_failure:
	if (seclen)
		security_release_secctx(secdata, seclen);
	return NULL;
}

static bool nf_ct_drop_unconfirmed(const struct nf_queue_entry *entry)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	static const unsigned long flags = IPS_CONFIRMED | IPS_DYING;
	const struct nf_conn *ct = (void *)skb_nfct(entry->skb);

	if (ct && ((ct->status & flags) == IPS_DYING))
		return true;
#endif
	return false;
}

static int
__nfqnl_enqueue_packet(struct net *net, struct nfqnl_instance *queue,
			struct nf_queue_entry *entry)
{
	struct sk_buff *nskb;
	int err = -ENOBUFS;
	__be32 *packet_id_ptr;
	int failopen = 0;

	nskb = nfqnl_build_packet_message(net, queue, entry, &packet_id_ptr);
	if (nskb == NULL) {
		err = -ENOMEM;
		goto err_out;
	}
	spin_lock_bh(&queue->lock);

	if (nf_ct_drop_unconfirmed(entry))
		goto err_out_free_nskb;

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
	err = nfnetlink_unicast(nskb, net, queue->peer_portid);
	if (err < 0) {
		if (queue->flags & NFQA_CFG_F_FAIL_OPEN) {
			failopen = 1;
			err = 0;
		} else {
			queue->queue_user_dropped++;
		}
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
		nfqnl_reinject(entry, NF_ACCEPT);
err_out:
	return err;
}

static struct nf_queue_entry *
nf_queue_entry_dup(struct nf_queue_entry *e)
{
	struct nf_queue_entry *entry = kmemdup(e, e->size, GFP_ATOMIC);

	if (!entry)
		return NULL;

	if (nf_queue_entry_get_refs(entry))
		return entry;

	kfree(entry);
	return NULL;
}

#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
/* When called from bridge netfilter, skb->data must point to MAC header
 * before calling skb_gso_segment(). Else, original MAC header is lost
 * and segmented skbs will be sent to wrong destination.
 */
static void nf_bridge_adjust_skb_data(struct sk_buff *skb)
{
	if (nf_bridge_info_get(skb))
		__skb_push(skb, skb->network_header - skb->mac_header);
}

static void nf_bridge_adjust_segmented_data(struct sk_buff *skb)
{
	if (nf_bridge_info_get(skb))
		__skb_pull(skb, skb->network_header - skb->mac_header);
}
#else
#define nf_bridge_adjust_skb_data(s) do {} while (0)
#define nf_bridge_adjust_segmented_data(s) do {} while (0)
#endif

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

	skb_mark_not_on_list(skb);

	entry_seg = nf_queue_entry_dup(entry);
	if (entry_seg) {
		entry_seg->skb = skb;
		ret = __nfqnl_enqueue_packet(net, queue, entry_seg);
		if (ret)
			nf_queue_entry_free(entry_seg);
	}
	return ret;
}

static int
nfqnl_enqueue_packet(struct nf_queue_entry *entry, unsigned int queuenum)
{
	unsigned int queued;
	struct nfqnl_instance *queue;
	struct sk_buff *skb, *segs, *nskb;
	int err = -ENOBUFS;
	struct net *net = entry->state.net;
	struct nfnl_queue_net *q = nfnl_queue_pernet(net);

	/* rcu_read_lock()ed by nf_hook_thresh */
	queue = instance_lookup(q, queuenum);
	if (!queue)
		return -ESRCH;

	if (queue->copy_mode == NFQNL_COPY_NONE)
		return -EINVAL;

	skb = entry->skb;

	switch (entry->state.pf) {
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
	 * returned by nf_queue.  For instance, callers rely on -ESRCH to
	 * mean 'ignore this hook'.
	 */
	if (IS_ERR_OR_NULL(segs))
		goto out_err;
	queued = 0;
	err = 0;
	skb_list_walk_safe(segs, segs, nskb) {
		if (err == 0)
			err = __nfqnl_enqueue_packet_gso(net, queue,
							segs, entry);
		if (err == 0)
			queued++;
		else
			kfree_skb(segs);
	}

	if (queued) {
		if (err) /* some segments are already queued */
			nf_queue_entry_free(entry);
		kfree_skb(skb);
		return 0;
	}
 out_err:
	nf_bridge_adjust_segmented_data(skb);
	return err;
}

static int
nfqnl_mangle(void *data, unsigned int data_len, struct nf_queue_entry *e, int diff)
{
	struct sk_buff *nskb;

	if (diff < 0) {
		unsigned int min_len = skb_transport_offset(e->skb);

		if (data_len < min_len)
			return -EINVAL;

		if (pskb_trim(e->skb, data_len))
			return -ENOMEM;
	} else if (diff > 0) {
		if (data_len > 0xFFFF)
			return -EINVAL;
		if (diff > skb_tailroom(e->skb)) {
			nskb = skb_copy_expand(e->skb, skb_headroom(e->skb),
					       diff, GFP_ATOMIC);
			if (!nskb)
				return -ENOMEM;
			kfree_skb(e->skb);
			e->skb = nskb;
		}
		skb_put(e->skb, diff);
	}
	if (skb_ensure_writable(e->skb, data_len))
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
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	int physinif, physoutif;

	physinif = nf_bridge_get_physinif(entry->skb);
	physoutif = nf_bridge_get_physoutif(entry->skb);

	if (physinif == ifindex || physoutif == ifindex)
		return 1;
#endif
	if (entry->state.in)
		if (entry->state.in->ifindex == ifindex)
			return 1;
	if (entry->state.out)
		if (entry->state.out->ifindex == ifindex)
			return 1;

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

static void nfqnl_nf_hook_drop(struct net *net)
{
	struct nfnl_queue_net *q = nfnl_queue_pernet(net);
	int i;

	/* This function is also called on net namespace error unwind,
	 * when pernet_ops->init() failed and ->exit() functions of the
	 * previous pernet_ops gets called.
	 *
	 * This may result in a call to nfqnl_nf_hook_drop() before
	 * struct nfnl_queue_net was allocated.
	 */
	if (!q)
		return;

	for (i = 0; i < INSTANCE_BUCKETS; i++) {
		struct nfqnl_instance *inst;
		struct hlist_head *head = &q->instance_table[i];

		hlist_for_each_entry_rcu(inst, head, hlist)
			nfqnl_flush(inst, NULL, 0);
	}
}

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

static const struct nla_policy nfqa_vlan_policy[NFQA_VLAN_MAX + 1] = {
	[NFQA_VLAN_TCI]		= { .type = NLA_U16},
	[NFQA_VLAN_PROTO]	= { .type = NLA_U16},
};

static const struct nla_policy nfqa_verdict_policy[NFQA_MAX+1] = {
	[NFQA_VERDICT_HDR]	= { .len = sizeof(struct nfqnl_msg_verdict_hdr) },
	[NFQA_MARK]		= { .type = NLA_U32 },
	[NFQA_PAYLOAD]		= { .type = NLA_UNSPEC },
	[NFQA_CT]		= { .type = NLA_UNSPEC },
	[NFQA_EXP]		= { .type = NLA_UNSPEC },
	[NFQA_VLAN]		= { .type = NLA_NESTED },
};

static const struct nla_policy nfqa_verdict_batch_policy[NFQA_MAX+1] = {
	[NFQA_VERDICT_HDR]	= { .len = sizeof(struct nfqnl_msg_verdict_hdr) },
	[NFQA_MARK]		= { .type = NLA_U32 },
};

static struct nfqnl_instance *
verdict_instance_lookup(struct nfnl_queue_net *q, u16 queue_num, u32 nlportid)
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

static int nfqnl_recv_verdict_batch(struct sk_buff *skb,
				    const struct nfnl_info *info,
				    const struct nlattr * const nfqa[])
{
	struct nfnl_queue_net *q = nfnl_queue_pernet(info->net);
	u16 queue_num = ntohs(info->nfmsg->res_id);
	struct nf_queue_entry *entry, *tmp;
	struct nfqnl_msg_verdict_hdr *vhdr;
	struct nfqnl_instance *queue;
	unsigned int verdict, maxid;
	LIST_HEAD(batch_list);

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

		nfqnl_reinject(entry, verdict);
	}
	return 0;
}

static struct nf_conn *nfqnl_ct_parse(struct nfnl_ct_hook *nfnl_ct,
				      const struct nlmsghdr *nlh,
				      const struct nlattr * const nfqa[],
				      struct nf_queue_entry *entry,
				      enum ip_conntrack_info *ctinfo)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	struct nf_conn *ct;

	ct = nf_ct_get(entry->skb, ctinfo);
	if (ct == NULL)
		return NULL;

	if (nfnl_ct->parse(nfqa[NFQA_CT], ct) < 0)
		return NULL;

	if (nfqa[NFQA_EXP])
		nfnl_ct->attach_expect(nfqa[NFQA_EXP], ct,
				      NETLINK_CB(entry->skb).portid,
				      nlmsg_report(nlh));
	return ct;
#else
	return NULL;
#endif
}

static int nfqa_parse_bridge(struct nf_queue_entry *entry,
			     const struct nlattr * const nfqa[])
{
	if (nfqa[NFQA_VLAN]) {
		struct nlattr *tb[NFQA_VLAN_MAX + 1];
		int err;

		err = nla_parse_nested_deprecated(tb, NFQA_VLAN_MAX,
						  nfqa[NFQA_VLAN],
						  nfqa_vlan_policy, NULL);
		if (err < 0)
			return err;

		if (!tb[NFQA_VLAN_TCI] || !tb[NFQA_VLAN_PROTO])
			return -EINVAL;

		__vlan_hwaccel_put_tag(entry->skb,
			nla_get_be16(tb[NFQA_VLAN_PROTO]),
			ntohs(nla_get_be16(tb[NFQA_VLAN_TCI])));
	}

	if (nfqa[NFQA_L2HDR]) {
		int mac_header_len = entry->skb->network_header -
			entry->skb->mac_header;

		if (mac_header_len != nla_len(nfqa[NFQA_L2HDR]))
			return -EINVAL;
		else if (mac_header_len > 0)
			memcpy(skb_mac_header(entry->skb),
			       nla_data(nfqa[NFQA_L2HDR]),
			       mac_header_len);
	}

	return 0;
}

static int nfqnl_recv_verdict(struct sk_buff *skb, const struct nfnl_info *info,
			      const struct nlattr * const nfqa[])
{
	struct nfnl_queue_net *q = nfnl_queue_pernet(info->net);
	u_int16_t queue_num = ntohs(info->nfmsg->res_id);
	struct nfqnl_msg_verdict_hdr *vhdr;
	enum ip_conntrack_info ctinfo;
	struct nfqnl_instance *queue;
	struct nf_queue_entry *entry;
	struct nfnl_ct_hook *nfnl_ct;
	struct nf_conn *ct = NULL;
	unsigned int verdict;
	int err;

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

	/* rcu lock already held from nfnl->call_rcu. */
	nfnl_ct = rcu_dereference(nfnl_ct_hook);

	if (nfqa[NFQA_CT]) {
		if (nfnl_ct != NULL)
			ct = nfqnl_ct_parse(nfnl_ct, info->nlh, nfqa, entry,
					    &ctinfo);
	}

	if (entry->state.pf == PF_BRIDGE) {
		err = nfqa_parse_bridge(entry, nfqa);
		if (err < 0)
			return err;
	}

	if (nfqa[NFQA_PAYLOAD]) {
		u16 payload_len = nla_len(nfqa[NFQA_PAYLOAD]);
		int diff = payload_len - entry->skb->len;

		if (nfqnl_mangle(nla_data(nfqa[NFQA_PAYLOAD]),
				 payload_len, entry, diff) < 0)
			verdict = NF_DROP;

		if (ct && diff)
			nfnl_ct->seq_adjust(entry->skb, ct, ctinfo, diff);
	}

	if (nfqa[NFQA_MARK])
		entry->skb->mark = ntohl(nla_get_be32(nfqa[NFQA_MARK]));

	nfqnl_reinject(entry, verdict);
	return 0;
}

static int nfqnl_recv_unsupp(struct sk_buff *skb, const struct nfnl_info *info,
			     const struct nlattr * const cda[])
{
	return -ENOTSUPP;
}

static const struct nla_policy nfqa_cfg_policy[NFQA_CFG_MAX+1] = {
	[NFQA_CFG_CMD]		= { .len = sizeof(struct nfqnl_msg_config_cmd) },
	[NFQA_CFG_PARAMS]	= { .len = sizeof(struct nfqnl_msg_config_params) },
	[NFQA_CFG_QUEUE_MAXLEN]	= { .type = NLA_U32 },
	[NFQA_CFG_MASK]		= { .type = NLA_U32 },
	[NFQA_CFG_FLAGS]	= { .type = NLA_U32 },
};

static const struct nf_queue_handler nfqh = {
	.outfn		= nfqnl_enqueue_packet,
	.nf_hook_drop	= nfqnl_nf_hook_drop,
};

static int nfqnl_recv_config(struct sk_buff *skb, const struct nfnl_info *info,
			     const struct nlattr * const nfqa[])
{
	struct nfnl_queue_net *q = nfnl_queue_pernet(info->net);
	u_int16_t queue_num = ntohs(info->nfmsg->res_id);
	struct nfqnl_msg_config_cmd *cmd = NULL;
	struct nfqnl_instance *queue;
	__u32 flags = 0, mask = 0;
	int ret = 0;

	if (nfqa[NFQA_CFG_CMD]) {
		cmd = nla_data(nfqa[NFQA_CFG_CMD]);

		/* Obsolete commands without queue context */
		switch (cmd->command) {
		case NFQNL_CFG_CMD_PF_BIND: return 0;
		case NFQNL_CFG_CMD_PF_UNBIND: return 0;
		}
	}

	/* Check if we support these flags in first place, dependencies should
	 * be there too not to break atomicity.
	 */
	if (nfqa[NFQA_CFG_FLAGS]) {
		if (!nfqa[NFQA_CFG_MASK]) {
			/* A mask is needed to specify which flags are being
			 * changed.
			 */
			return -EINVAL;
		}

		flags = ntohl(nla_get_be32(nfqa[NFQA_CFG_FLAGS]));
		mask = ntohl(nla_get_be32(nfqa[NFQA_CFG_MASK]));

		if (flags >= NFQA_CFG_F_MAX)
			return -EOPNOTSUPP;

#if !IS_ENABLED(CONFIG_NETWORK_SECMARK)
		if (flags & mask & NFQA_CFG_F_SECCTX)
			return -EOPNOTSUPP;
#endif
		if ((flags & mask & NFQA_CFG_F_CONNTRACK) &&
		    !rcu_access_pointer(nfnl_ct_hook)) {
#ifdef CONFIG_MODULES
			nfnl_unlock(NFNL_SUBSYS_QUEUE);
			request_module("ip_conntrack_netlink");
			nfnl_lock(NFNL_SUBSYS_QUEUE);
			if (rcu_access_pointer(nfnl_ct_hook))
				return -EAGAIN;
#endif
			return -EOPNOTSUPP;
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
			goto err_out_unlock;
		case NFQNL_CFG_CMD_PF_BIND:
		case NFQNL_CFG_CMD_PF_UNBIND:
			break;
		default:
			ret = -ENOTSUPP;
			goto err_out_unlock;
		}
	}

	if (!queue) {
		ret = -ENODEV;
		goto err_out_unlock;
	}

	if (nfqa[NFQA_CFG_PARAMS]) {
		struct nfqnl_msg_config_params *params =
			nla_data(nfqa[NFQA_CFG_PARAMS]);

		nfqnl_set_mode(queue, params->copy_mode,
				ntohl(params->copy_range));
	}

	if (nfqa[NFQA_CFG_QUEUE_MAXLEN]) {
		__be32 *queue_maxlen = nla_data(nfqa[NFQA_CFG_QUEUE_MAXLEN]);

		spin_lock_bh(&queue->lock);
		queue->queue_maxlen = ntohl(*queue_maxlen);
		spin_unlock_bh(&queue->lock);
	}

	if (nfqa[NFQA_CFG_FLAGS]) {
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
	[NFQNL_MSG_PACKET]	= {
		.call		= nfqnl_recv_unsupp,
		.type		= NFNL_CB_RCU,
		.attr_count	= NFQA_MAX,
	},
	[NFQNL_MSG_VERDICT]	= {
		.call		= nfqnl_recv_verdict,
		.type		= NFNL_CB_RCU,
		.attr_count	= NFQA_MAX,
		.policy		= nfqa_verdict_policy
	},
	[NFQNL_MSG_CONFIG]	= {
		.call		= nfqnl_recv_config,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= NFQA_CFG_MAX,
		.policy		= nfqa_cfg_policy
	},
	[NFQNL_MSG_VERDICT_BATCH] = {
		.call		= nfqnl_recv_verdict_batch,
		.type		= NFNL_CB_RCU,
		.attr_count	= NFQA_MAX,
		.policy		= nfqa_verdict_batch_policy
	},
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

	seq_printf(s, "%5u %6u %5u %1u %5u %5u %5u %8u %2d\n",
		   inst->queue_num,
		   inst->peer_portid, inst->queue_total,
		   inst->copy_mode, inst->copy_range,
		   inst->queue_dropped, inst->queue_user_dropped,
		   inst->id_sequence, 1);
	return 0;
}

static const struct seq_operations nfqnl_seq_ops = {
	.start	= seq_start,
	.next	= seq_next,
	.stop	= seq_stop,
	.show	= seq_show,
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
	if (!proc_create_net("nfnetlink_queue", 0440, net->nf.proc_netfilter,
			&nfqnl_seq_ops, sizeof(struct iter_state)))
		return -ENOMEM;
#endif
	return 0;
}

static void __net_exit nfnl_queue_net_exit(struct net *net)
{
	struct nfnl_queue_net *q = nfnl_queue_pernet(net);
	unsigned int i;

#ifdef CONFIG_PROC_FS
	remove_proc_entry("nfnetlink_queue", net->nf.proc_netfilter);
#endif
	for (i = 0; i < INSTANCE_BUCKETS; i++)
		WARN_ON_ONCE(!hlist_empty(&q->instance_table[i]));
}

static void nfnl_queue_net_exit_batch(struct list_head *net_exit_list)
{
	synchronize_rcu();
}

static struct pernet_operations nfnl_queue_net_ops = {
	.init		= nfnl_queue_net_init,
	.exit		= nfnl_queue_net_exit,
	.exit_batch	= nfnl_queue_net_exit_batch,
	.id		= &nfnl_queue_net_id,
	.size		= sizeof(struct nfnl_queue_net),
};

static int __init nfnetlink_queue_init(void)
{
	int status;

	status = register_pernet_subsys(&nfnl_queue_net_ops);
	if (status < 0) {
		pr_err("failed to register pernet ops\n");
		goto out;
	}

	netlink_register_notifier(&nfqnl_rtnl_notifier);
	status = nfnetlink_subsys_register(&nfqnl_subsys);
	if (status < 0) {
		pr_err("failed to create netlink socket\n");
		goto cleanup_netlink_notifier;
	}

	status = register_netdevice_notifier(&nfqnl_dev_notifier);
	if (status < 0) {
		pr_err("failed to register netdevice notifier\n");
		goto cleanup_netlink_subsys;
	}

	nf_register_queue_handler(&nfqh);

	return status;

cleanup_netlink_subsys:
	nfnetlink_subsys_unregister(&nfqnl_subsys);
cleanup_netlink_notifier:
	netlink_unregister_notifier(&nfqnl_rtnl_notifier);
	unregister_pernet_subsys(&nfnl_queue_net_ops);
out:
	return status;
}

static void __exit nfnetlink_queue_fini(void)
{
	nf_unregister_queue_handler();
	unregister_netdevice_notifier(&nfqnl_dev_notifier);
	nfnetlink_subsys_unregister(&nfqnl_subsys);
	netlink_unregister_notifier(&nfqnl_rtnl_notifier);
	unregister_pernet_subsys(&nfnl_queue_net_ops);

	rcu_barrier(); /* Wait for completion of call_rcu()'s */
}

MODULE_DESCRIPTION("netfilter packet queue handler");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_QUEUE);

module_init(nfnetlink_queue_init);
module_exit(nfnetlink_queue_fini);
