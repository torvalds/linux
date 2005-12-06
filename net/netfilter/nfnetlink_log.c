/*
 * This is a module which is used for logging packets to userspace via
 * nfetlink.
 *
 * (C) 2005 by Harald Welte <laforge@netfilter.org>
 *
 * Based on the old ipv4-only ipt_ULOG.c:
 * (C) 2000-2004 by Harald Welte <laforge@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/netlink.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_log.h>
#include <linux/spinlock.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/security.h>
#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/random.h>
#include <net/sock.h>

#include <asm/atomic.h>

#ifdef CONFIG_BRIDGE_NETFILTER
#include "../bridge/br_private.h"
#endif

#define NFULNL_NLBUFSIZ_DEFAULT	4096
#define NFULNL_TIMEOUT_DEFAULT 	100	/* every second */
#define NFULNL_QTHRESH_DEFAULT 	100	/* 100 packets */

#define PRINTR(x, args...)	do { if (net_ratelimit()) \
				     printk(x, ## args); } while (0);

#if 0
#define UDEBUG(x, args ...)	printk(KERN_DEBUG "%s(%d):%s():	" x, 	   \
					__FILE__, __LINE__, __FUNCTION__,  \
					## args)
#else
#define UDEBUG(x, ...)
#endif

struct nfulnl_instance {
	struct hlist_node hlist;	/* global list of instances */
	spinlock_t lock;
	atomic_t use;			/* use count */

	unsigned int qlen;		/* number of nlmsgs in skb */
	struct sk_buff *skb;		/* pre-allocatd skb */
	struct nlmsghdr *lastnlh;	/* netlink header of last msg in skb */
	struct timer_list timer;
	int peer_pid;			/* PID of the peer process */

	/* configurable parameters */
	unsigned int flushtimeout;	/* timeout until queue flush */
	unsigned int nlbufsiz;		/* netlink buffer allocation size */
	unsigned int qthreshold;	/* threshold of the queue */
	u_int32_t copy_range;
	u_int16_t group_num;		/* number of this queue */
	u_int8_t copy_mode;	
};

static DEFINE_RWLOCK(instances_lock);

#define INSTANCE_BUCKETS	16
static struct hlist_head instance_table[INSTANCE_BUCKETS];
static unsigned int hash_init;

static inline u_int8_t instance_hashfn(u_int16_t group_num)
{
	return ((group_num & 0xff) % INSTANCE_BUCKETS);
}

static struct nfulnl_instance *
__instance_lookup(u_int16_t group_num)
{
	struct hlist_head *head;
	struct hlist_node *pos;
	struct nfulnl_instance *inst;

	UDEBUG("entering (group_num=%u)\n", group_num);

	head = &instance_table[instance_hashfn(group_num)];
	hlist_for_each_entry(inst, pos, head, hlist) {
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
instance_lookup_get(u_int16_t group_num)
{
	struct nfulnl_instance *inst;

	read_lock_bh(&instances_lock);
	inst = __instance_lookup(group_num);
	if (inst)
		instance_get(inst);
	read_unlock_bh(&instances_lock);

	return inst;
}

static void
instance_put(struct nfulnl_instance *inst)
{
	if (inst && atomic_dec_and_test(&inst->use)) {
		UDEBUG("kfree(inst=%p)\n", inst);
		kfree(inst);
	}
}

static void nfulnl_timer(unsigned long data);

static struct nfulnl_instance *
instance_create(u_int16_t group_num, int pid)
{
	struct nfulnl_instance *inst;

	UDEBUG("entering (group_num=%u, pid=%d)\n", group_num,
		pid);

	write_lock_bh(&instances_lock);	
	if (__instance_lookup(group_num)) {
		inst = NULL;
		UDEBUG("aborting, instance already exists\n");
		goto out_unlock;
	}

	inst = kzalloc(sizeof(*inst), GFP_ATOMIC);
	if (!inst)
		goto out_unlock;

	INIT_HLIST_NODE(&inst->hlist);
	inst->lock = SPIN_LOCK_UNLOCKED;
	/* needs to be two, since we _put() after creation */
	atomic_set(&inst->use, 2);

	init_timer(&inst->timer);
	inst->timer.function = nfulnl_timer;
	inst->timer.data = (unsigned long)inst;
	/* don't start timer yet. (re)start it  with every packet */

	inst->peer_pid = pid;
	inst->group_num = group_num;

	inst->qthreshold 	= NFULNL_QTHRESH_DEFAULT;
	inst->flushtimeout 	= NFULNL_TIMEOUT_DEFAULT;
	inst->nlbufsiz 		= NFULNL_NLBUFSIZ_DEFAULT;
	inst->copy_mode 	= NFULNL_COPY_PACKET;
	inst->copy_range 	= 0xffff;

	if (!try_module_get(THIS_MODULE))
		goto out_free;

	hlist_add_head(&inst->hlist, 
		       &instance_table[instance_hashfn(group_num)]);

	UDEBUG("newly added node: %p, next=%p\n", &inst->hlist, 
		inst->hlist.next);

	write_unlock_bh(&instances_lock);

	return inst;

out_free:
	instance_put(inst);
out_unlock:
	write_unlock_bh(&instances_lock);
	return NULL;
}

static int __nfulnl_send(struct nfulnl_instance *inst);

static void
_instance_destroy2(struct nfulnl_instance *inst, int lock)
{
	/* first pull it out of the global list */
	if (lock)
		write_lock_bh(&instances_lock);

	UDEBUG("removing instance %p (queuenum=%u) from hash\n",
		inst, inst->group_num);

	hlist_del(&inst->hlist);

	if (lock)
		write_unlock_bh(&instances_lock);

	/* then flush all pending packets from skb */

	spin_lock_bh(&inst->lock);
	if (inst->skb) {
		if (inst->qlen)
			__nfulnl_send(inst);
		if (inst->skb) {
			kfree_skb(inst->skb);
			inst->skb = NULL;
		}
	}
	spin_unlock_bh(&inst->lock);

	/* and finally put the refcount */
	instance_put(inst);

	module_put(THIS_MODULE);
}

static inline void
__instance_destroy(struct nfulnl_instance *inst)
{
	_instance_destroy2(inst, 0);
}

static inline void
instance_destroy(struct nfulnl_instance *inst)
{
	_instance_destroy2(inst, 1);
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
		/* we're using struct nfattr which has 16bit nfa_len */
		if (range > 0xffff)
			inst->copy_range = 0xffff;
		else
			inst->copy_range = range;
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

static struct sk_buff *nfulnl_alloc_skb(unsigned int inst_size, 
					unsigned int pkt_size)
{
	struct sk_buff *skb;

	UDEBUG("entered (%u, %u)\n", inst_size, pkt_size);

	/* alloc skb which should be big enough for a whole multipart
	 * message.  WARNING: has to be <= 128k due to slab restrictions */

	skb = alloc_skb(inst_size, GFP_ATOMIC);
	if (!skb) {
		PRINTR("nfnetlink_log: can't alloc whole buffer (%u bytes)\n",
			inst_size);

		/* try to allocate only as much as we need for current
		 * packet */

		skb = alloc_skb(pkt_size, GFP_ATOMIC);
		if (!skb)
			PRINTR("nfnetlink_log: can't even alloc %u bytes\n",
				pkt_size);
	}

	return skb;
}

static int
__nfulnl_send(struct nfulnl_instance *inst)
{
	int status;

	if (timer_pending(&inst->timer))
		del_timer(&inst->timer);

	if (inst->qlen > 1)
		inst->lastnlh->nlmsg_type = NLMSG_DONE;

	status = nfnetlink_unicast(inst->skb, inst->peer_pid, MSG_DONTWAIT);
	if (status < 0) {
		UDEBUG("netlink_unicast() failed\n");
		/* FIXME: statistics */
	}

	inst->qlen = 0;
	inst->skb = NULL;
	inst->lastnlh = NULL;

	return status;
}

static void nfulnl_timer(unsigned long data)
{
	struct nfulnl_instance *inst = (struct nfulnl_instance *)data; 

	UDEBUG("timer function called, flushing buffer\n");

	spin_lock_bh(&inst->lock);
	__nfulnl_send(inst);
	instance_put(inst);
	spin_unlock_bh(&inst->lock);
}

static inline int 
__build_packet_message(struct nfulnl_instance *inst,
			const struct sk_buff *skb, 
			unsigned int data_len,
			unsigned int pf,
			unsigned int hooknum,
			const struct net_device *indev,
			const struct net_device *outdev,
			const struct nf_loginfo *li,
			const char *prefix)
{
	unsigned char *old_tail;
	struct nfulnl_msg_packet_hdr pmsg;
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	u_int32_t tmp_uint;

	UDEBUG("entered\n");
		
	old_tail = inst->skb->tail;
	nlh = NLMSG_PUT(inst->skb, 0, 0, 
			NFNL_SUBSYS_ULOG << 8 | NFULNL_MSG_PACKET,
			sizeof(struct nfgenmsg));
	nfmsg = NLMSG_DATA(nlh);
	nfmsg->nfgen_family = pf;
	nfmsg->version = NFNETLINK_V0;
	nfmsg->res_id = htons(inst->group_num);

	pmsg.hw_protocol	= htons(skb->protocol);
	pmsg.hook		= hooknum;

	NFA_PUT(inst->skb, NFULA_PACKET_HDR, sizeof(pmsg), &pmsg);

	if (prefix) {
		int slen = strlen(prefix);
		if (slen > NFULNL_PREFIXLEN)
			slen = NFULNL_PREFIXLEN;
		NFA_PUT(inst->skb, NFULA_PREFIX, slen, prefix);
	}

	if (indev) {
		tmp_uint = htonl(indev->ifindex);
#ifndef CONFIG_BRIDGE_NETFILTER
		NFA_PUT(inst->skb, NFULA_IFINDEX_INDEV, sizeof(tmp_uint),
			&tmp_uint);
#else
		if (pf == PF_BRIDGE) {
			/* Case 1: outdev is physical input device, we need to
			 * look for bridge group (when called from
			 * netfilter_bridge) */
			NFA_PUT(inst->skb, NFULA_IFINDEX_PHYSINDEV,
				sizeof(tmp_uint), &tmp_uint);
			/* this is the bridge group "brX" */
			tmp_uint = htonl(indev->br_port->br->dev->ifindex);
			NFA_PUT(inst->skb, NFULA_IFINDEX_INDEV,
				sizeof(tmp_uint), &tmp_uint);
		} else {
			/* Case 2: indev is bridge group, we need to look for
			 * physical device (when called from ipv4) */
			NFA_PUT(inst->skb, NFULA_IFINDEX_INDEV,
				sizeof(tmp_uint), &tmp_uint);
			if (skb->nf_bridge && skb->nf_bridge->physindev) {
				tmp_uint = 
				    htonl(skb->nf_bridge->physindev->ifindex);
				NFA_PUT(inst->skb, NFULA_IFINDEX_PHYSINDEV,
					sizeof(tmp_uint), &tmp_uint);
			}
		}
#endif
	}

	if (outdev) {
		tmp_uint = htonl(outdev->ifindex);
#ifndef CONFIG_BRIDGE_NETFILTER
		NFA_PUT(inst->skb, NFULA_IFINDEX_OUTDEV, sizeof(tmp_uint),
			&tmp_uint);
#else
		if (pf == PF_BRIDGE) {
			/* Case 1: outdev is physical output device, we need to
			 * look for bridge group (when called from
			 * netfilter_bridge) */
			NFA_PUT(inst->skb, NFULA_IFINDEX_PHYSOUTDEV,
				sizeof(tmp_uint), &tmp_uint);
			/* this is the bridge group "brX" */
			tmp_uint = htonl(outdev->br_port->br->dev->ifindex);
			NFA_PUT(inst->skb, NFULA_IFINDEX_OUTDEV,
				sizeof(tmp_uint), &tmp_uint);
		} else {
			/* Case 2: indev is a bridge group, we need to look
			 * for physical device (when called from ipv4) */
			NFA_PUT(inst->skb, NFULA_IFINDEX_OUTDEV,
				sizeof(tmp_uint), &tmp_uint);
			if (skb->nf_bridge) {
				tmp_uint = 
				    htonl(skb->nf_bridge->physoutdev->ifindex);
				NFA_PUT(inst->skb, NFULA_IFINDEX_PHYSOUTDEV,
					sizeof(tmp_uint), &tmp_uint);
			}
		}
#endif
	}

	if (skb->nfmark) {
		tmp_uint = htonl(skb->nfmark);
		NFA_PUT(inst->skb, NFULA_MARK, sizeof(tmp_uint), &tmp_uint);
	}

	if (indev && skb->dev && skb->dev->hard_header_parse) {
		struct nfulnl_msg_packet_hw phw;

		phw.hw_addrlen = 
			skb->dev->hard_header_parse((struct sk_buff *)skb, 
						    phw.hw_addr);
		phw.hw_addrlen = htons(phw.hw_addrlen);
		NFA_PUT(inst->skb, NFULA_HWADDR, sizeof(phw), &phw);
	}

	if (skb->tstamp.off_sec) {
		struct nfulnl_msg_packet_timestamp ts;

		ts.sec = cpu_to_be64(skb->tstamp.off_sec);
		ts.usec = cpu_to_be64(skb->tstamp.off_usec);

		NFA_PUT(inst->skb, NFULA_TIMESTAMP, sizeof(ts), &ts);
	}

	/* UID */
	if (skb->sk) {
		read_lock_bh(&skb->sk->sk_callback_lock);
		if (skb->sk->sk_socket && skb->sk->sk_socket->file) {
			u_int32_t uid = htonl(skb->sk->sk_socket->file->f_uid);
			/* need to unlock here since NFA_PUT may goto */
			read_unlock_bh(&skb->sk->sk_callback_lock);
			NFA_PUT(inst->skb, NFULA_UID, sizeof(uid), &uid);
		} else
			read_unlock_bh(&skb->sk->sk_callback_lock);
	}

	if (data_len) {
		struct nfattr *nfa;
		int size = NFA_LENGTH(data_len);

		if (skb_tailroom(inst->skb) < (int)NFA_SPACE(data_len)) {
			printk(KERN_WARNING "nfnetlink_log: no tailroom!\n");
			goto nlmsg_failure;
		}

		nfa = (struct nfattr *)skb_put(inst->skb, NFA_ALIGN(size));
		nfa->nfa_type = NFULA_PAYLOAD;
		nfa->nfa_len = size;

		if (skb_copy_bits(skb, 0, NFA_DATA(nfa), data_len))
			BUG();
	}
		
	nlh->nlmsg_len = inst->skb->tail - old_tail;
	return 0;

nlmsg_failure:
	UDEBUG("nlmsg_failure\n");
nfattr_failure:
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
static void
nfulnl_log_packet(unsigned int pf,
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
	unsigned int nlbufsiz;

	if (li_user && li_user->type == NF_LOG_TYPE_ULOG) 
		li = li_user;
	else
		li = &default_loginfo;

	inst = instance_lookup_get(li->u.ulog.group);
	if (!inst)
		inst = instance_lookup_get(0);
	if (!inst) {
		PRINTR("nfnetlink_log: trying to log packet, "
			"but no instance for group %u\n", li->u.ulog.group);
		return;
	}

	/* all macros expand to constant values at compile time */
	/* FIXME: do we want to make the size calculation conditional based on
	 * what is actually present?  way more branches and checks, but more
	 * memory efficient... */
	size =    NLMSG_SPACE(sizeof(struct nfgenmsg))
		+ NFA_SPACE(sizeof(struct nfulnl_msg_packet_hdr))
		+ NFA_SPACE(sizeof(u_int32_t))	/* ifindex */
		+ NFA_SPACE(sizeof(u_int32_t))	/* ifindex */
#ifdef CONFIG_BRIDGE_NETFILTER
		+ NFA_SPACE(sizeof(u_int32_t))	/* ifindex */
		+ NFA_SPACE(sizeof(u_int32_t))	/* ifindex */
#endif
		+ NFA_SPACE(sizeof(u_int32_t))	/* mark */
		+ NFA_SPACE(sizeof(u_int32_t))	/* uid */
		+ NFA_SPACE(NFULNL_PREFIXLEN)	/* prefix */
		+ NFA_SPACE(sizeof(struct nfulnl_msg_packet_hw))
		+ NFA_SPACE(sizeof(struct nfulnl_msg_packet_timestamp));

	UDEBUG("initial size=%u\n", size);

	spin_lock_bh(&inst->lock);

	qthreshold = inst->qthreshold;
	/* per-rule qthreshold overrides per-instance */
	if (qthreshold > li->u.ulog.qthreshold)
		qthreshold = li->u.ulog.qthreshold;
	
	switch (inst->copy_mode) {
	case NFULNL_COPY_META:
	case NFULNL_COPY_NONE:
		data_len = 0;
		break;
	
	case NFULNL_COPY_PACKET:
		if (inst->copy_range == 0 
		    || inst->copy_range > skb->len)
			data_len = skb->len;
		else
			data_len = inst->copy_range;
		
		size += NFA_SPACE(data_len);
		UDEBUG("copy_packet, therefore size now %u\n", size);
		break;
	
	default:
		spin_unlock_bh(&inst->lock);
		instance_put(inst);
		return;
	}

	if (size > inst->nlbufsiz)
		nlbufsiz = size;
	else
		nlbufsiz = inst->nlbufsiz;

	if (!inst->skb) {
		if (!(inst->skb = nfulnl_alloc_skb(nlbufsiz, size))) {
			UDEBUG("error in nfulnl_alloc_skb(%u, %u)\n",
				inst->nlbufsiz, size);
			goto alloc_failure;
		}
	} else if (inst->qlen >= qthreshold ||
		   size > skb_tailroom(inst->skb)) {
		/* either the queue len is too high or we don't have
		 * enough room in the skb left. flush to userspace. */
		UDEBUG("flushing old skb\n");

		__nfulnl_send(inst);

		if (!(inst->skb = nfulnl_alloc_skb(nlbufsiz, size))) {
			UDEBUG("error in nfulnl_alloc_skb(%u, %u)\n",
				inst->nlbufsiz, size);
			goto alloc_failure;
		}
	}

	UDEBUG("qlen %d, qthreshold %d\n", inst->qlen, qthreshold);
	inst->qlen++;

	__build_packet_message(inst, skb, data_len, pf,
				hooknum, in, out, li, prefix);

	/* timer_pending always called within inst->lock, so there
	 * is no chance of a race here */
	if (!timer_pending(&inst->timer)) {
		instance_get(inst);
		inst->timer.expires = jiffies + (inst->flushtimeout*HZ/100);
		add_timer(&inst->timer);
	}
	spin_unlock_bh(&inst->lock);

	return;

alloc_failure:
	spin_unlock_bh(&inst->lock);
	instance_put(inst);
	UDEBUG("error allocating skb\n");
	/* FIXME: statistics */
}

static int
nfulnl_rcv_nl_event(struct notifier_block *this,
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
			struct nfulnl_instance *inst;
			struct hlist_head *head = &instance_table[i];

			hlist_for_each_entry_safe(inst, tmp, t2, head, hlist) {
				UDEBUG("node = %p\n", inst);
				if (n->pid == inst->peer_pid)
					__instance_destroy(inst);
			}
		}
		write_unlock_bh(&instances_lock);
	}
	return NOTIFY_DONE;
}

static struct notifier_block nfulnl_rtnl_notifier = {
	.notifier_call	= nfulnl_rcv_nl_event,
};

static int
nfulnl_recv_unsupp(struct sock *ctnl, struct sk_buff *skb,
		  struct nlmsghdr *nlh, struct nfattr *nfqa[], int *errp)
{
	return -ENOTSUPP;
}

static struct nf_logger nfulnl_logger = {
	.name	= "nfnetlink_log",
	.logfn	= &nfulnl_log_packet,
	.me	= THIS_MODULE,
};

static const int nfula_min[NFULA_MAX] = {
	[NFULA_PACKET_HDR-1]	= sizeof(struct nfulnl_msg_packet_hdr),
	[NFULA_MARK-1]		= sizeof(u_int32_t),
	[NFULA_TIMESTAMP-1]	= sizeof(struct nfulnl_msg_packet_timestamp),
	[NFULA_IFINDEX_INDEV-1]	= sizeof(u_int32_t),
	[NFULA_IFINDEX_OUTDEV-1]= sizeof(u_int32_t),
	[NFULA_HWADDR-1]	= sizeof(struct nfulnl_msg_packet_hw),
	[NFULA_PAYLOAD-1]	= 0,
	[NFULA_PREFIX-1]	= 0,
	[NFULA_UID-1]		= sizeof(u_int32_t),
};

static const int nfula_cfg_min[NFULA_CFG_MAX] = {
	[NFULA_CFG_CMD-1]	= sizeof(struct nfulnl_msg_config_cmd),
	[NFULA_CFG_MODE-1]	= sizeof(struct nfulnl_msg_config_mode),
	[NFULA_CFG_TIMEOUT-1]	= sizeof(u_int32_t),
	[NFULA_CFG_QTHRESH-1]	= sizeof(u_int32_t),
	[NFULA_CFG_NLBUFSIZ-1]	= sizeof(u_int32_t),
};

static int
nfulnl_recv_config(struct sock *ctnl, struct sk_buff *skb,
		   struct nlmsghdr *nlh, struct nfattr *nfula[], int *errp)
{
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int16_t group_num = ntohs(nfmsg->res_id);
	struct nfulnl_instance *inst;
	int ret = 0;

	UDEBUG("entering for msg %u\n", NFNL_MSG_TYPE(nlh->nlmsg_type));

	if (nfattr_bad_size(nfula, NFULA_CFG_MAX, nfula_cfg_min)) {
		UDEBUG("bad attribute size\n");
		return -EINVAL;
	}

	inst = instance_lookup_get(group_num);
	if (nfula[NFULA_CFG_CMD-1]) {
		u_int8_t pf = nfmsg->nfgen_family;
		struct nfulnl_msg_config_cmd *cmd;
		cmd = NFA_DATA(nfula[NFULA_CFG_CMD-1]);
		UDEBUG("found CFG_CMD for\n");

		switch (cmd->command) {
		case NFULNL_CFG_CMD_BIND:
			if (inst) {
				ret = -EBUSY;
				goto out_put;
			}

			inst = instance_create(group_num,
					       NETLINK_CB(skb).pid);
			if (!inst) {
				ret = -EINVAL;
				goto out_put;
			}
			break;
		case NFULNL_CFG_CMD_UNBIND:
			if (!inst) {
				ret = -ENODEV;
				goto out_put;
			}

			if (inst->peer_pid != NETLINK_CB(skb).pid) {
				ret = -EPERM;
				goto out_put;
			}

			instance_destroy(inst);
			break;
		case NFULNL_CFG_CMD_PF_BIND:
			UDEBUG("registering log handler for pf=%u\n", pf);
			ret = nf_log_register(pf, &nfulnl_logger);
			break;
		case NFULNL_CFG_CMD_PF_UNBIND:
			UDEBUG("unregistering log handler for pf=%u\n", pf);
			/* This is a bug and a feature.  We cannot unregister
			 * other handlers, like nfnetlink_inst can */
			nf_log_unregister_pf(pf);
			break;
		default:
			ret = -EINVAL;
			break;
		}
	} else {
		if (!inst) {
			UDEBUG("no config command, and no instance for "
				"group=%u pid=%u =>ENOENT\n",
				group_num, NETLINK_CB(skb).pid);
			ret = -ENOENT;
			goto out_put;
		}

		if (inst->peer_pid != NETLINK_CB(skb).pid) {
			UDEBUG("no config command, and wrong pid\n");
			ret = -EPERM;
			goto out_put;
		}
	}

	if (nfula[NFULA_CFG_MODE-1]) {
		struct nfulnl_msg_config_mode *params;
		params = NFA_DATA(nfula[NFULA_CFG_MODE-1]);

		nfulnl_set_mode(inst, params->copy_mode,
				ntohs(params->copy_range));
	}

	if (nfula[NFULA_CFG_TIMEOUT-1]) {
		u_int32_t timeout = 
			*(u_int32_t *)NFA_DATA(nfula[NFULA_CFG_TIMEOUT-1]);

		nfulnl_set_timeout(inst, ntohl(timeout));
	}

	if (nfula[NFULA_CFG_NLBUFSIZ-1]) {
		u_int32_t nlbufsiz = 
			*(u_int32_t *)NFA_DATA(nfula[NFULA_CFG_NLBUFSIZ-1]);

		nfulnl_set_nlbufsiz(inst, ntohl(nlbufsiz));
	}

	if (nfula[NFULA_CFG_QTHRESH-1]) {
		u_int32_t qthresh = 
			*(u_int16_t *)NFA_DATA(nfula[NFULA_CFG_QTHRESH-1]);

		nfulnl_set_qthresh(inst, ntohl(qthresh));
	}

out_put:
	instance_put(inst);
	return ret;
}

static struct nfnl_callback nfulnl_cb[NFULNL_MSG_MAX] = {
	[NFULNL_MSG_PACKET]	= { .call = nfulnl_recv_unsupp,
				    .attr_count = NFULA_MAX, },
	[NFULNL_MSG_CONFIG]	= { .call = nfulnl_recv_config,
				    .attr_count = NFULA_CFG_MAX, },
};

static struct nfnetlink_subsystem nfulnl_subsys = {
	.name		= "log",
	.subsys_id	= NFNL_SUBSYS_ULOG,
	.cb_count	= NFULNL_MSG_MAX,
	.cb		= nfulnl_cb,
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
	const struct nfulnl_instance *inst = v;

	return seq_printf(s, "%5d %6d %5d %1d %5d %6d %2d\n", 
			  inst->group_num,
			  inst->peer_pid, inst->qlen, 
			  inst->copy_mode, inst->copy_range,
			  inst->flushtimeout, atomic_read(&inst->use));
}

static struct seq_operations nful_seq_ops = {
	.start	= seq_start,
	.next	= seq_next,
	.stop	= seq_stop,
	.show	= seq_show,
};

static int nful_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	struct iter_state *is;
	int ret;

	is = kzalloc(sizeof(*is), GFP_KERNEL);
	if (!is)
		return -ENOMEM;
	ret = seq_open(file, &nful_seq_ops);
	if (ret < 0)
		goto out_free;
	seq = file->private_data;
	seq->private = is;
	return ret;
out_free:
	kfree(is);
	return ret;
}

static struct file_operations nful_file_ops = {
	.owner	 = THIS_MODULE,
	.open	 = nful_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_private,
};

#endif /* PROC_FS */

static int
init_or_cleanup(int init)
{
	int i, status = -ENOMEM;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc_nful;
#endif
	
	if (!init)
		goto cleanup;

	for (i = 0; i < INSTANCE_BUCKETS; i++)
		INIT_HLIST_HEAD(&instance_table[i]);
	
	/* it's not really all that important to have a random value, so
	 * we can do this from the init function, even if there hasn't
	 * been that much entropy yet */
	get_random_bytes(&hash_init, sizeof(hash_init));

	netlink_register_notifier(&nfulnl_rtnl_notifier);
	status = nfnetlink_subsys_register(&nfulnl_subsys);
	if (status < 0) {
		printk(KERN_ERR "log: failed to create netlink socket\n");
		goto cleanup_netlink_notifier;
	}

#ifdef CONFIG_PROC_FS
	proc_nful = create_proc_entry("nfnetlink_log", 0440,
				      proc_net_netfilter);
	if (!proc_nful)
		goto cleanup_subsys;
	proc_nful->proc_fops = &nful_file_ops;
#endif

	return status;

cleanup:
	nf_log_unregister_logger(&nfulnl_logger);
#ifdef CONFIG_PROC_FS
	remove_proc_entry("nfnetlink_log", proc_net_netfilter);
cleanup_subsys:
#endif
	nfnetlink_subsys_unregister(&nfulnl_subsys);
cleanup_netlink_notifier:
	netlink_unregister_notifier(&nfulnl_rtnl_notifier);
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

MODULE_DESCRIPTION("netfilter userspace logging");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_ULOG);

module_init(init);
module_exit(fini);
