/*
 * This is a module which is used for logging packets to userspace via
 * nfetlink.
 *
 * (C) 2005 by Harald Welte <laforge@netfilter.org>
 * (C) 2006-2012 Patrick McHardy <kaber@trash.net>
 *
 * Based on the old ipv4-only ipt_ULOG.c:
 * (C) 2000-2004 by Harald Welte <laforge@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <net/netlink.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_log.h>
#include <linux/spinlock.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/security.h>
#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/netfilter/nf_log.h>
#include <net/netns/generic.h>
#include <net/netfilter/nfnetlink_log.h>

#include <linux/atomic.h>

#ifdef CONFIG_BRIDGE_NETFILTER
#include "../bridge/br_private.h"
#endif

#define NFULNL_NLBUFSIZ_DEFAULT	NLMSG_GOODSIZE
#define NFULNL_TIMEOUT_DEFAULT 	100	/* every second */
#define NFULNL_QTHRESH_DEFAULT 	100	/* 100 packets */
/* max packet size is limited by 16-bit struct nfattr nfa_len field */
#define NFULNL_COPY_RANGE_MAX	(0xFFFF - NLA_HDRLEN)

#define PRINTR(x, args...)	do { if (net_ratelimit()) \
				     printk(x, ## args); } while (0);

struct nfulnl_instance {
	struct hlist_node hlist;	/* global list of instances */
	spinlock_t lock;
	atomic_t use;			/* use count */

	unsigned int qlen;		/* number of nlmsgs in skb */
	struct sk_buff *skb;		/* pre-allocatd skb */
	struct timer_list timer;
	struct net *net;
	struct user_namespace *peer_user_ns;	/* User namespace of the peer process */
	int peer_portid;			/* PORTID of the peer process */

	/* configurable parameters */
	unsigned int flushtimeout;	/* timeout until queue flush */
	unsigned int nlbufsiz;		/* netlink buffer allocation size */
	unsigned int qthreshold;	/* threshold of the queue */
	u_int32_t copy_range;
	u_int32_t seq;			/* instance-local sequential counter */
	u_int16_t group_num;		/* number of this queue */
	u_int16_t flags;
	u_int8_t copy_mode;
	struct rcu_head rcu;
};

#define INSTANCE_BUCKETS	16
static unsigned int hash_init;

static int nfnl_log_net_id __read_mostly;

struct nfnl_log_net {
	spinlock_t instances_lock;
	struct hlist_head instance_table[INSTANCE_BUCKETS];
	atomic_t global_seq;
};

static struct nfnl_log_net *nfnl_log_pernet(struct net *net)
{
	return net_generic(net, nfnl_log_net_id);
}

static inline u_int8_t instance_hashfn(u_int16_t group_num)
{
	return ((group_num & 0xff) % INSTANCE_BUCKETS);
}

static struct nfulnl_instance *
__instance_lookup(struct nfnl_log_net *log, u_int16_t group_num)
{
	struct hlist_head *head;
	struct nfulnl_instance *inst;

	head = &log->instance_table[instance_hashfn(group_num)];
	hlist_for_each_entry_rcu(inst, head, hlist) {
		if (inst->group_num == group_num)
			return inst;
	}
	return NULL;
}

static inline void
instance_get(struct nfulnl_instance *inst)
{
	atomic_inc(&inst->use);
}

static struct nfulnl_instance *
instance_lookup_get(struct nfnl_log_net *log, u_int16_t group_num)
{
	struct nfulnl_instance *inst;

	rcu_read_lock_bh();
	inst = __instance_lookup(log, group_num);
	if (inst && !atomic_inc_not_zero(&inst->use))
		inst = NULL;
	rcu_read_unlock_bh();

	return inst;
}

static void nfulnl_instance_free_rcu(struct rcu_head *head)
{
	struct nfulnl_instance *inst =
		container_of(head, struct nfulnl_instance, rcu);

	put_net(inst->net);
	kfree(inst);
	module_put(THIS_MODULE);
}

static void
instance_put(struct nfulnl_instance *inst)
{
	if (inst && atomic_dec_and_test(&inst->use))
		call_rcu_bh(&inst->rcu, nfulnl_instance_free_rcu);
}

static void nfulnl_timer(unsigned long data);

static struct nfulnl_instance *
instance_create(struct net *net, u_int16_t group_num,
		int portid, struct user_namespace *user_ns)
{
	struct nfulnl_instance *inst;
	struct nfnl_log_net *log = nfnl_log_pernet(net);
	int err;

	spin_lock_bh(&log->instances_lock);
	if (__instance_lookup(log, group_num)) {
		err = -EEXIST;
		goto out_unlock;
	}

	inst = kzalloc(sizeof(*inst), GFP_ATOMIC);
	if (!inst) {
		err = -ENOMEM;
		goto out_unlock;
	}

	if (!try_module_get(THIS_MODULE)) {
		kfree(inst);
		err = -EAGAIN;
		goto out_unlock;
	}

	INIT_HLIST_NODE(&inst->hlist);
	spin_lock_init(&inst->lock);
	/* needs to be two, since we _put() after creation */
	atomic_set(&inst->use, 2);

	setup_timer(&inst->timer, nfulnl_timer, (unsigned long)inst);

	inst->net = get_net(net);
	inst->peer_user_ns = user_ns;
	inst->peer_portid = portid;
	inst->group_num = group_num;

	inst->qthreshold 	= NFULNL_QTHRESH_DEFAULT;
	inst->flushtimeout 	= NFULNL_TIMEOUT_DEFAULT;
	inst->nlbufsiz 		= NFULNL_NLBUFSIZ_DEFAULT;
	inst->copy_mode 	= NFULNL_COPY_PACKET;
	inst->copy_range 	= NFULNL_COPY_RANGE_MAX;

	hlist_add_head_rcu(&inst->hlist,
		       &log->instance_table[instance_hashfn(group_num)]);


	spin_unlock_bh(&log->instances_lock);

	return inst;

out_unlock:
	spin_unlock_bh(&log->instances_lock);
	return ERR_PTR(err);
}

static void __nfulnl_flush(struct nfulnl_instance *inst);

/* called with BH disabled */
static void
__instance_destroy(struct nfulnl_instance *inst)
{
	/* first pull it out of the global list */
	hlist_del_rcu(&inst->hlist);

	/* then flush all pending packets from skb */

	spin_lock(&inst->lock);

	/* lockless readers wont be able to use us */
	inst->copy_mode = NFULNL_COPY_DISABLED;

	if (inst->skb)
		__nfulnl_flush(inst);
	spin_unlock(&inst->lock);

	/* and finally put the refcount */
	instance_put(inst);
}

static inline void
instance_destroy(struct nfnl_log_net *log,
		 struct nfulnl_instance *inst)
{
	spin_lock_bh(&log->instances_lock);
	__instance_destroy(inst);
	spin_unlock_bh(&log->instances_lock);
}

static int
nfulnl_set_mode(struct nfulnl_instance *inst, u_int8_t mode,
		  unsigned int range)
{
	int status = 0;

	spin_lock_bh(&inst->lock);

	switch (mode) {
	case NFULNL_COPY_NONE:
	case NFULNL_COPY_META:
		inst->copy_mode = mode;
		inst->copy_range = 0;
		break;

	case NFULNL_COPY_PACKET:
		inst->copy_mode = mode;
		if (range == 0)
			range = NFULNL_COPY_RANGE_MAX;
		inst->copy_range = min_t(unsigned int,
					 range, NFULNL_COPY_RANGE_MAX);
		break;

	default:
		status = -EINVAL;
		break;
	}

	spin_unlock_bh(&inst->lock);

	return status;
}

static int
nfulnl_set_nlbufsiz(struct nfulnl_instance *inst, u_int32_t nlbufsiz)
{
	int status;

	spin_lock_bh(&inst->lock);
	if (nlbufsiz < NFULNL_NLBUFSIZ_DEFAULT)
		status = -ERANGE;
	else if (nlbufsiz > 131072)
		status = -ERANGE;
	else {
		inst->nlbufsiz = nlbufsiz;
		status = 0;
	}
	spin_unlock_bh(&inst->lock);

	return status;
}

static int
nfulnl_set_timeout(struct nfulnl_instance *inst, u_int32_t timeout)
{
	spin_lock_bh(&inst->lock);
	inst->flushtimeout = timeout;
	spin_unlock_bh(&inst->lock);

	return 0;
}

static int
nfulnl_set_qthresh(struct nfulnl_instance *inst, u_int32_t qthresh)
{
	spin_lock_bh(&inst->lock);
	inst->qthreshold = qthresh;
	spin_unlock_bh(&inst->lock);

	return 0;
}

static int
nfulnl_set_flags(struct nfulnl_instance *inst, u_int16_t flags)
{
	spin_lock_bh(&inst->lock);
	inst->flags = flags;
	spin_unlock_bh(&inst->lock);

	return 0;
}

static struct sk_buff *
nfulnl_alloc_skb(u32 peer_portid, unsigned int inst_size, unsigned int pkt_size)
{
	struct sk_buff *skb;
	unsigned int n;

	/* alloc skb which should be big enough for a whole multipart
	 * message.  WARNING: has to be <= 128k due to slab restrictions */

	n = max(inst_size, pkt_size);
	skb = nfnetlink_alloc_skb(&init_net, n, peer_portid, GFP_ATOMIC);
	if (!skb) {
		if (n > pkt_size) {
			/* try to allocate only as much as we need for current
			 * packet */

			skb = nfnetlink_alloc_skb(&init_net, pkt_size,
						  peer_portid, GFP_ATOMIC);
			if (!skb)
				pr_err("nfnetlink_log: can't even alloc %u bytes\n",
				       pkt_size);
		}
	}

	return skb;
}

static void
__nfulnl_send(struct nfulnl_instance *inst)
{
	if (inst->qlen > 1) {
		struct nlmsghdr *nlh = nlmsg_put(inst->skb, 0, 0,
						 NLMSG_DONE,
						 sizeof(struct nfgenmsg),
						 0);
		if (WARN_ONCE(!nlh, "bad nlskb size: %u, tailroom %d\n",
			      inst->skb->len, skb_tailroom(inst->skb))) {
			kfree_skb(inst->skb);
			goto out;
		}
	}
	nfnetlink_unicast(inst->skb, inst->net, inst->peer_portid,
			  MSG_DONTWAIT);
out:
	inst->qlen = 0;
	inst->skb = NULL;
}

static void
__nfulnl_flush(struct nfulnl_instance *inst)
{
	/* timer holds a reference */
	if (del_timer(&inst->timer))
		instance_put(inst);
	if (inst->skb)
		__nfulnl_send(inst);
}

static void
nfulnl_timer(unsigned long data)
{
	struct nfulnl_instance *inst = (struct nfulnl_instance *)data;

	spin_lock_bh(&inst->lock);
	if (inst->skb)
		__nfulnl_send(inst);
	spin_unlock_bh(&inst->lock);
	instance_put(inst);
}

/* This is an inline function, we don't really care about a long
 * list of arguments */
static inline int
__build_packet_message(struct nfnl_log_net *log,
			struct nfulnl_instance *inst,
			const struct sk_buff *skb,
			unsigned int data_len,
			u_int8_t pf,
			unsigned int hooknum,
			const struct net_device *indev,
			const struct net_device *outdev,
			const char *prefix, unsigned int plen)
{
	struct nfulnl_msg_packet_hdr pmsg;
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	sk_buff_data_t old_tail = inst->skb->tail;
	struct sock *sk;
	const unsigned char *hwhdrp;

	nlh = nlmsg_put(inst->skb, 0, 0,
			NFNL_SUBSYS_ULOG << 8 | NFULNL_MSG_PACKET,
			sizeof(struct nfgenmsg), 0);
	if (!nlh)
		return -1;
	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = pf;
	nfmsg->version = NFNETLINK_V0;
	nfmsg->res_id = htons(inst->group_num);

	pmsg.hw_protocol	= skb->protocol;
	pmsg.hook		= hooknum;

	if (nla_put(inst->skb, NFULA_PACKET_HDR, sizeof(pmsg), &pmsg))
		goto nla_put_failure;

	if (prefix &&
	    nla_put(inst->skb, NFULA_PREFIX, plen, prefix))
		goto nla_put_failure;

	if (indev) {
#ifndef CONFIG_BRIDGE_NETFILTER
		if (nla_put_be32(inst->skb, NFULA_IFINDEX_INDEV,
				 htonl(indev->ifindex)))
			goto nla_put_failure;
#else
		if (pf == PF_BRIDGE) {
			/* Case 1: outdev is physical input device, we need to
			 * look for bridge group (when called from
			 * netfilter_bridge) */
			if (nla_put_be32(inst->skb, NFULA_IFINDEX_PHYSINDEV,
					 htonl(indev->ifindex)) ||
			/* this is the bridge group "brX" */
			/* rcu_read_lock()ed by nf_hook_slow or nf_log_packet */
			    nla_put_be32(inst->skb, NFULA_IFINDEX_INDEV,
					 htonl(br_port_get_rcu(indev)->br->dev->ifindex)))
				goto nla_put_failure;
		} else {
			/* Case 2: indev is bridge group, we need to look for
			 * physical device (when called from ipv4) */
			if (nla_put_be32(inst->skb, NFULA_IFINDEX_INDEV,
					 htonl(indev->ifindex)))
				goto nla_put_failure;
			if (skb->nf_bridge && skb->nf_bridge->physindev &&
			    nla_put_be32(inst->skb, NFULA_IFINDEX_PHYSINDEV,
					 htonl(skb->nf_bridge->physindev->ifindex)))
				goto nla_put_failure;
		}
#endif
	}

	if (outdev) {
#ifndef CONFIG_BRIDGE_NETFILTER
		if (nla_put_be32(inst->skb, NFULA_IFINDEX_OUTDEV,
				 htonl(outdev->ifindex)))
			goto nla_put_failure;
#else
		if (pf == PF_BRIDGE) {
			/* Case 1: outdev is physical output device, we need to
			 * look for bridge group (when called from
			 * netfilter_bridge) */
			if (nla_put_be32(inst->skb, NFULA_IFINDEX_PHYSOUTDEV,
					 htonl(outdev->ifindex)) ||
			/* this is the bridge group "brX" */
			/* rcu_read_lock()ed by nf_hook_slow or nf_log_packet */
			    nla_put_be32(inst->skb, NFULA_IFINDEX_OUTDEV,
					 htonl(br_port_get_rcu(outdev)->br->dev->ifindex)))
				goto nla_put_failure;
		} else {
			/* Case 2: indev is a bridge group, we need to look
			 * for physical device (when called from ipv4) */
			if (nla_put_be32(inst->skb, NFULA_IFINDEX_OUTDEV,
					 htonl(outdev->ifindex)))
				goto nla_put_failure;
			if (skb->nf_bridge && skb->nf_bridge->physoutdev &&
			    nla_put_be32(inst->skb, NFULA_IFINDEX_PHYSOUTDEV,
					 htonl(skb->nf_bridge->physoutdev->ifindex)))
				goto nla_put_failure;
		}
#endif
	}

	if (skb->mark &&
	    nla_put_be32(inst->skb, NFULA_MARK, htonl(skb->mark)))
		goto nla_put_failure;

	if (indev && skb->dev &&
	    skb->mac_header != skb->network_header) {
		struct nfulnl_msg_packet_hw phw;
		int len = dev_parse_header(skb, phw.hw_addr);
		if (len > 0) {
			phw.hw_addrlen = htons(len);
			if (nla_put(inst->skb, NFULA_HWADDR, sizeof(phw), &phw))
				goto nla_put_failure;
		}
	}

	if (indev && skb_mac_header_was_set(skb)) {
		if (nla_put_be16(inst->skb, NFULA_HWTYPE, htons(skb->dev->type)) ||
		    nla_put_be16(inst->skb, NFULA_HWLEN,
				 htons(skb->dev->hard_header_len)))
			goto nla_put_failure;

		hwhdrp = skb_mac_header(skb);

		if (skb->dev->type == ARPHRD_SIT)
			hwhdrp -= ETH_HLEN;

		if (hwhdrp >= skb->head &&
		    nla_put(inst->skb, NFULA_HWHEADER,
			    skb->dev->hard_header_len, hwhdrp))
			goto nla_put_failure;
	}

	if (skb->tstamp.tv64) {
		struct nfulnl_msg_packet_timestamp ts;
		struct timeval tv = ktime_to_timeval(skb->tstamp);
		ts.sec = cpu_to_be64(tv.tv_sec);
		ts.usec = cpu_to_be64(tv.tv_usec);

		if (nla_put(inst->skb, NFULA_TIMESTAMP, sizeof(ts), &ts))
			goto nla_put_failure;
	}

	/* UID */
	sk = skb->sk;
	if (sk && sk->sk_state != TCP_TIME_WAIT) {
		read_lock_bh(&sk->sk_callback_lock);
		if (sk->sk_socket && sk->sk_socket->file) {
			struct file *file = sk->sk_socket->file;
			const struct cred *cred = file->f_cred;
			struct user_namespace *user_ns = inst->peer_user_ns;
			__be32 uid = htonl(from_kuid_munged(user_ns, cred->fsuid));
			__be32 gid = htonl(from_kgid_munged(user_ns, cred->fsgid));
			read_unlock_bh(&sk->sk_callback_lock);
			if (nla_put_be32(inst->skb, NFULA_UID, uid) ||
			    nla_put_be32(inst->skb, NFULA_GID, gid))
				goto nla_put_failure;
		} else
			read_unlock_bh(&sk->sk_callback_lock);
	}

	/* local sequence number */
	if ((inst->flags & NFULNL_CFG_F_SEQ) &&
	    nla_put_be32(inst->skb, NFULA_SEQ, htonl(inst->seq++)))
		goto nla_put_failure;

	/* global sequence number */
	if ((inst->flags & NFULNL_CFG_F_SEQ_GLOBAL) &&
	    nla_put_be32(inst->skb, NFULA_SEQ_GLOBAL,
			 htonl(atomic_inc_return(&log->global_seq))))
		goto nla_put_failure;

	if (data_len) {
		struct nlattr *nla;
		int size = nla_attr_size(data_len);

		if (skb_tailroom(inst->skb) < nla_total_size(data_len)) {
			printk(KERN_WARNING "nfnetlink_log: no tailroom!\n");
			return -1;
		}

		nla = (struct nlattr *)skb_put(inst->skb, nla_total_size(data_len));
		nla->nla_type = NFULA_PAYLOAD;
		nla->nla_len = size;

		if (skb_copy_bits(skb, 0, nla_data(nla), data_len))
			BUG();
	}

	nlh->nlmsg_len = inst->skb->tail - old_tail;
	return 0;

nla_put_failure:
	PRINTR(KERN_ERR "nfnetlink_log: error creating log nlmsg\n");
	return -1;
}

#define RCV_SKB_FAIL(err) do { netlink_ack(skb, nlh, (err)); return; } while (0)

static struct nf_loginfo default_loginfo = {
	.type =		NF_LOG_TYPE_ULOG,
	.u = {
		.ulog = {
			.copy_len	= 0xffff,
			.group		= 0,
			.qthreshold	= 1,
		},
	},
};

/* log handler for internal netfilter logging api */
void
nfulnl_log_packet(struct net *net,
		  u_int8_t pf,
		  unsigned int hooknum,
		  const struct sk_buff *skb,
		  const struct net_device *in,
		  const struct net_device *out,
		  const struct nf_loginfo *li_user,
		  const char *prefix)
{
	unsigned int size, data_len;
	struct nfulnl_instance *inst;
	const struct nf_loginfo *li;
	unsigned int qthreshold;
	unsigned int plen;
	struct nfnl_log_net *log = nfnl_log_pernet(net);

	if (li_user && li_user->type == NF_LOG_TYPE_ULOG)
		li = li_user;
	else
		li = &default_loginfo;

	inst = instance_lookup_get(log, li->u.ulog.group);
	if (!inst)
		return;

	plen = 0;
	if (prefix)
		plen = strlen(prefix) + 1;

	/* FIXME: do we want to make the size calculation conditional based on
	 * what is actually present?  way more branches and checks, but more
	 * memory efficient... */
	size =    nlmsg_total_size(sizeof(struct nfgenmsg))
		+ nla_total_size(sizeof(struct nfulnl_msg_packet_hdr))
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
#ifdef CONFIG_BRIDGE_NETFILTER
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
		+ nla_total_size(sizeof(u_int32_t))	/* ifindex */
#endif
		+ nla_total_size(sizeof(u_int32_t))	/* mark */
		+ nla_total_size(sizeof(u_int32_t))	/* uid */
		+ nla_total_size(sizeof(u_int32_t))	/* gid */
		+ nla_total_size(plen)			/* prefix */
		+ nla_total_size(sizeof(struct nfulnl_msg_packet_hw))
		+ nla_total_size(sizeof(struct nfulnl_msg_packet_timestamp))
		+ nla_total_size(sizeof(struct nfgenmsg));	/* NLMSG_DONE */

	if (in && skb_mac_header_was_set(skb)) {
		size +=   nla_total_size(skb->dev->hard_header_len)
			+ nla_total_size(sizeof(u_int16_t))	/* hwtype */
			+ nla_total_size(sizeof(u_int16_t));	/* hwlen */
	}

	spin_lock_bh(&inst->lock);

	if (inst->flags & NFULNL_CFG_F_SEQ)
		size += nla_total_size(sizeof(u_int32_t));
	if (inst->flags & NFULNL_CFG_F_SEQ_GLOBAL)
		size += nla_total_size(sizeof(u_int32_t));

	qthreshold = inst->qthreshold;
	/* per-rule qthreshold overrides per-instance */
	if (li->u.ulog.qthreshold)
		if (qthreshold > li->u.ulog.qthreshold)
			qthreshold = li->u.ulog.qthreshold;


	switch (inst->copy_mode) {
	case NFULNL_COPY_META:
	case NFULNL_COPY_NONE:
		data_len = 0;
		break;

	case NFULNL_COPY_PACKET:
		if (inst->copy_range > skb->len)
			data_len = skb->len;
		else
			data_len = inst->copy_range;

		size += nla_total_size(data_len);
		break;

	case NFULNL_COPY_DISABLED:
	default:
		goto unlock_and_release;
	}

	if (inst->skb && size > skb_tailroom(inst->skb)) {
		/* either the queue len is too high or we don't have
		 * enough room in the skb left. flush to userspace. */
		__nfulnl_flush(inst);
	}

	if (!inst->skb) {
		inst->skb = nfulnl_alloc_skb(inst->peer_portid, inst->nlbufsiz,
					     size);
		if (!inst->skb)
			goto alloc_failure;
	}

	inst->qlen++;

	__build_packet_message(log, inst, skb, data_len, pf,
				hooknum, in, out, prefix, plen);

	if (inst->qlen >= qthreshold)
		__nfulnl_flush(inst);
	/* timer_pending always called within inst->lock, so there
	 * is no chance of a race here */
	else if (!timer_pending(&inst->timer)) {
		instance_get(inst);
		inst->timer.expires = jiffies + (inst->flushtimeout*HZ/100);
		add_timer(&inst->timer);
	}

unlock_and_release:
	spin_unlock_bh(&inst->lock);
	instance_put(inst);
	return;

alloc_failure:
	/* FIXME: statistics */
	goto unlock_and_release;
}
EXPORT_SYMBOL_GPL(nfulnl_log_packet);

static int
nfulnl_rcv_nl_event(struct notifier_block *this,
		   unsigned long event, void *ptr)
{
	struct netlink_notify *n = ptr;
	struct nfnl_log_net *log = nfnl_log_pernet(n->net);

	if (event == NETLINK_URELEASE && n->protocol == NETLINK_NETFILTER) {
		int i;

		/* destroy all instances for this portid */
		spin_lock_bh(&log->instances_lock);
		for  (i = 0; i < INSTANCE_BUCKETS; i++) {
			struct hlist_node *t2;
			struct nfulnl_instance *inst;
			struct hlist_head *head = &log->instance_table[i];

			hlist_for_each_entry_safe(inst, t2, head, hlist) {
				if (n->portid == inst->peer_portid)
					__instance_destroy(inst);
			}
		}
		spin_unlock_bh(&log->instances_lock);
	}
	return NOTIFY_DONE;
}

static struct notifier_block nfulnl_rtnl_notifier = {
	.notifier_call	= nfulnl_rcv_nl_event,
};

static int
nfulnl_recv_unsupp(struct sock *ctnl, struct sk_buff *skb,
		   const struct nlmsghdr *nlh,
		   const struct nlattr * const nfqa[])
{
	return -ENOTSUPP;
}

static struct nf_logger nfulnl_logger __read_mostly = {
	.name	= "nfnetlink_log",
	.logfn	= &nfulnl_log_packet,
	.me	= THIS_MODULE,
};

static const struct nla_policy nfula_cfg_policy[NFULA_CFG_MAX+1] = {
	[NFULA_CFG_CMD]		= { .len = sizeof(struct nfulnl_msg_config_cmd) },
	[NFULA_CFG_MODE]	= { .len = sizeof(struct nfulnl_msg_config_mode) },
	[NFULA_CFG_TIMEOUT]	= { .type = NLA_U32 },
	[NFULA_CFG_QTHRESH]	= { .type = NLA_U32 },
	[NFULA_CFG_NLBUFSIZ]	= { .type = NLA_U32 },
	[NFULA_CFG_FLAGS]	= { .type = NLA_U16 },
};

static int
nfulnl_recv_config(struct sock *ctnl, struct sk_buff *skb,
		   const struct nlmsghdr *nlh,
		   const struct nlattr * const nfula[])
{
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int16_t group_num = ntohs(nfmsg->res_id);
	struct nfulnl_instance *inst;
	struct nfulnl_msg_config_cmd *cmd = NULL;
	struct net *net = sock_net(ctnl);
	struct nfnl_log_net *log = nfnl_log_pernet(net);
	int ret = 0;

	if (nfula[NFULA_CFG_CMD]) {
		u_int8_t pf = nfmsg->nfgen_family;
		cmd = nla_data(nfula[NFULA_CFG_CMD]);

		/* Commands without queue context */
		switch (cmd->command) {
		case NFULNL_CFG_CMD_PF_BIND:
			return nf_log_bind_pf(net, pf, &nfulnl_logger);
		case NFULNL_CFG_CMD_PF_UNBIND:
			nf_log_unbind_pf(net, pf);
			return 0;
		}
	}

	inst = instance_lookup_get(log, group_num);
	if (inst && inst->peer_portid != NETLINK_CB(skb).portid) {
		ret = -EPERM;
		goto out_put;
	}

	if (cmd != NULL) {
		switch (cmd->command) {
		case NFULNL_CFG_CMD_BIND:
			if (inst) {
				ret = -EBUSY;
				goto out_put;
			}

			inst = instance_create(net, group_num,
					       NETLINK_CB(skb).portid,
					       sk_user_ns(NETLINK_CB(skb).sk));
			if (IS_ERR(inst)) {
				ret = PTR_ERR(inst);
				goto out;
			}
			break;
		case NFULNL_CFG_CMD_UNBIND:
			if (!inst) {
				ret = -ENODEV;
				goto out;
			}

			instance_destroy(log, inst);
			goto out_put;
		default:
			ret = -ENOTSUPP;
			break;
		}
	}

	if (nfula[NFULA_CFG_MODE]) {
		struct nfulnl_msg_config_mode *params;
		params = nla_data(nfula[NFULA_CFG_MODE]);

		if (!inst) {
			ret = -ENODEV;
			goto out;
		}
		nfulnl_set_mode(inst, params->copy_mode,
				ntohl(params->copy_range));
	}

	if (nfula[NFULA_CFG_TIMEOUT]) {
		__be32 timeout = nla_get_be32(nfula[NFULA_CFG_TIMEOUT]);

		if (!inst) {
			ret = -ENODEV;
			goto out;
		}
		nfulnl_set_timeout(inst, ntohl(timeout));
	}

	if (nfula[NFULA_CFG_NLBUFSIZ]) {
		__be32 nlbufsiz = nla_get_be32(nfula[NFULA_CFG_NLBUFSIZ]);

		if (!inst) {
			ret = -ENODEV;
			goto out;
		}
		nfulnl_set_nlbufsiz(inst, ntohl(nlbufsiz));
	}

	if (nfula[NFULA_CFG_QTHRESH]) {
		__be32 qthresh = nla_get_be32(nfula[NFULA_CFG_QTHRESH]);

		if (!inst) {
			ret = -ENODEV;
			goto out;
		}
		nfulnl_set_qthresh(inst, ntohl(qthresh));
	}

	if (nfula[NFULA_CFG_FLAGS]) {
		__be16 flags = nla_get_be16(nfula[NFULA_CFG_FLAGS]);

		if (!inst) {
			ret = -ENODEV;
			goto out;
		}
		nfulnl_set_flags(inst, ntohs(flags));
	}

out_put:
	instance_put(inst);
out:
	return ret;
}

static const struct nfnl_callback nfulnl_cb[NFULNL_MSG_MAX] = {
	[NFULNL_MSG_PACKET]	= { .call = nfulnl_recv_unsupp,
				    .attr_count = NFULA_MAX, },
	[NFULNL_MSG_CONFIG]	= { .call = nfulnl_recv_config,
				    .attr_count = NFULA_CFG_MAX,
				    .policy = nfula_cfg_policy },
};

static const struct nfnetlink_subsystem nfulnl_subsys = {
	.name		= "log",
	.subsys_id	= NFNL_SUBSYS_ULOG,
	.cb_count	= NFULNL_MSG_MAX,
	.cb		= nfulnl_cb,
};

#ifdef CONFIG_PROC_FS
struct iter_state {
	struct seq_net_private p;
	unsigned int bucket;
};

static struct hlist_node *get_first(struct net *net, struct iter_state *st)
{
	struct nfnl_log_net *log;
	if (!st)
		return NULL;

	log = nfnl_log_pernet(net);

	for (st->bucket = 0; st->bucket < INSTANCE_BUCKETS; st->bucket++) {
		struct hlist_head *head = &log->instance_table[st->bucket];

		if (!hlist_empty(head))
			return rcu_dereference_bh(hlist_first_rcu(head));
	}
	return NULL;
}

static struct hlist_node *get_next(struct net *net, struct iter_state *st,
				   struct hlist_node *h)
{
	h = rcu_dereference_bh(hlist_next_rcu(h));
	while (!h) {
		struct nfnl_log_net *log;
		struct hlist_head *head;

		if (++st->bucket >= INSTANCE_BUCKETS)
			return NULL;

		log = nfnl_log_pernet(net);
		head = &log->instance_table[st->bucket];
		h = rcu_dereference_bh(hlist_first_rcu(head));
	}
	return h;
}

static struct hlist_node *get_idx(struct net *net, struct iter_state *st,
				  loff_t pos)
{
	struct hlist_node *head;
	head = get_first(net, st);

	if (head)
		while (pos && (head = get_next(net, st, head)))
			pos--;
	return pos ? NULL : head;
}

static void *seq_start(struct seq_file *s, loff_t *pos)
	__acquires(rcu_bh)
{
	rcu_read_lock_bh();
	return get_idx(seq_file_net(s), s->private, *pos);
}

static void *seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	return get_next(seq_file_net(s), s->private, v);
}

static void seq_stop(struct seq_file *s, void *v)
	__releases(rcu_bh)
{
	rcu_read_unlock_bh();
}

static int seq_show(struct seq_file *s, void *v)
{
	const struct nfulnl_instance *inst = v;

	return seq_printf(s, "%5d %6d %5d %1d %5d %6d %2d\n",
			  inst->group_num,
			  inst->peer_portid, inst->qlen,
			  inst->copy_mode, inst->copy_range,
			  inst->flushtimeout, atomic_read(&inst->use));
}

static const struct seq_operations nful_seq_ops = {
	.start	= seq_start,
	.next	= seq_next,
	.stop	= seq_stop,
	.show	= seq_show,
};

static int nful_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &nful_seq_ops,
			    sizeof(struct iter_state));
}

static const struct file_operations nful_file_ops = {
	.owner	 = THIS_MODULE,
	.open	 = nful_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_net,
};

#endif /* PROC_FS */

static int __net_init nfnl_log_net_init(struct net *net)
{
	unsigned int i;
	struct nfnl_log_net *log = nfnl_log_pernet(net);

	for (i = 0; i < INSTANCE_BUCKETS; i++)
		INIT_HLIST_HEAD(&log->instance_table[i]);
	spin_lock_init(&log->instances_lock);

#ifdef CONFIG_PROC_FS
	if (!proc_create("nfnetlink_log", 0440,
			 net->nf.proc_netfilter, &nful_file_ops))
		return -ENOMEM;
#endif
	return 0;
}

static void __net_exit nfnl_log_net_exit(struct net *net)
{
#ifdef CONFIG_PROC_FS
	remove_proc_entry("nfnetlink_log", net->nf.proc_netfilter);
#endif
}

static struct pernet_operations nfnl_log_net_ops = {
	.init	= nfnl_log_net_init,
	.exit	= nfnl_log_net_exit,
	.id	= &nfnl_log_net_id,
	.size	= sizeof(struct nfnl_log_net),
};

static int __init nfnetlink_log_init(void)
{
	int status = -ENOMEM;

	/* it's not really all that important to have a random value, so
	 * we can do this from the init function, even if there hasn't
	 * been that much entropy yet */
	get_random_bytes(&hash_init, sizeof(hash_init));

	netlink_register_notifier(&nfulnl_rtnl_notifier);
	status = nfnetlink_subsys_register(&nfulnl_subsys);
	if (status < 0) {
		pr_err("log: failed to create netlink socket\n");
		goto cleanup_netlink_notifier;
	}

	status = nf_log_register(NFPROTO_UNSPEC, &nfulnl_logger);
	if (status < 0) {
		pr_err("log: failed to register logger\n");
		goto cleanup_subsys;
	}

	status = register_pernet_subsys(&nfnl_log_net_ops);
	if (status < 0) {
		pr_err("log: failed to register pernet ops\n");
		goto cleanup_logger;
	}
	return status;

cleanup_logger:
	nf_log_unregister(&nfulnl_logger);
cleanup_subsys:
	nfnetlink_subsys_unregister(&nfulnl_subsys);
cleanup_netlink_notifier:
	netlink_unregister_notifier(&nfulnl_rtnl_notifier);
	return status;
}

static void __exit nfnetlink_log_fini(void)
{
	unregister_pernet_subsys(&nfnl_log_net_ops);
	nf_log_unregister(&nfulnl_logger);
	nfnetlink_subsys_unregister(&nfulnl_subsys);
	netlink_unregister_notifier(&nfulnl_rtnl_notifier);
}

MODULE_DESCRIPTION("netfilter userspace logging");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_ULOG);

module_init(nfnetlink_log_init);
module_exit(nfnetlink_log_fini);
