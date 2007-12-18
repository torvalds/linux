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
#include <net/netfilter/nf_log.h>

#include <asm/atomic.h>

#ifdef CONFIG_BRIDGE_NETFILTER
#include "../bridge/br_private.h"
#endif

#define NFULNL_NLBUFSIZ_DEFAULT	NLMSG_GOODSIZE
#define NFULNL_TIMEOUT_DEFAULT 	HZ	/* every second */
#define NFULNL_QTHRESH_DEFAULT 	100	/* 100 packets */
#define NFULNL_COPY_RANGE_MAX	0xFFFF	/* max packet size is limited by 16-bit struct nfattr nfa_len field */

#define PRINTR(x, args...)	do { if (net_ratelimit()) \
				     printk(x, ## args); } while (0);

struct nfulnl_instance {
	struct hlist_node hlist;	/* global list of instances */
	spinlock_t lock;
	atomic_t use;			/* use count */

	unsigned int qlen;		/* number of nlmsgs in skb */
	struct sk_buff *skb;		/* pre-allocatd skb */
	struct timer_list timer;
	int peer_pid;			/* PID of the peer process */

	/* configurable parameters */
	unsigned int flushtimeout;	/* timeout until queue flush */
	unsigned int nlbufsiz;		/* netlink buffer allocation size */
	unsigned int qthreshold;	/* threshold of the queue */
	u_int32_t copy_range;
	u_int32_t seq;			/* instance-local sequential counter */
	u_int16_t group_num;		/* number of this queue */
	u_int16_t flags;
	u_int8_t copy_mode;
};

static DEFINE_RWLOCK(instances_lock);
static atomic_t global_seq;

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
		kfree(inst);
		module_put(THIS_MODULE);
	}
}

static void nfulnl_timer(unsigned long data);

static struct nfulnl_instance *
instance_create(u_int16_t group_num, int pid)
{
	struct nfulnl_instance *inst;
	int err;

	write_lock_bh(&instances_lock);
	if (__instance_lookup(group_num)) {
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

	inst->peer_pid = pid;
	inst->group_num = group_num;

	inst->qthreshold 	= NFULNL_QTHRESH_DEFAULT;
	inst->flushtimeout 	= NFULNL_TIMEOUT_DEFAULT;
	inst->nlbufsiz 		= NFULNL_NLBUFSIZ_DEFAULT;
	inst->copy_mode 	= NFULNL_COPY_PACKET;
	inst->copy_range 	= NFULNL_COPY_RANGE_MAX;

	hlist_add_head(&inst->hlist,
		       &instance_table[instance_hashfn(group_num)]);

	write_unlock_bh(&instances_lock);

	return inst;

out_unlock:
	write_unlock_bh(&instances_lock);
	return ERR_PTR(err);
}

static void __nfulnl_flush(struct nfulnl_instance *inst);

static void
__instance_destroy(struct nfulnl_instance *inst)
{
	/* first pull it out of the global list */
	hlist_del(&inst->hlist);

	/* then flush all pending packets from skb */

	spin_lock_bh(&inst->lock);
	if (inst->skb)
		__nfulnl_flush(inst);
	spin_unlock_bh(&inst->lock);

	/* and finally put the refcount */
	instance_put(inst);
}

static inline void
instance_destroy(struct nfulnl_instance *inst)
{
	write_lock_bh(&instances_lock);
	__instance_destroy(inst);
	write_unlock_bh(&instances_lock);
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
nfulnl_alloc_skb(unsigned int inst_size, unsigned int pkt_size)
{
	struct sk_buff *skb;
	unsigned int n;

	/* alloc skb which should be big enough for a whole multipart
	 * message.  WARNING: has to be <= 128k due to slab restrictions */

	n = max(inst_size, pkt_size);
	skb = alloc_skb(n, GFP_ATOMIC);
	if (!skb) {
		PRINTR("nfnetlink_log: can't alloc whole buffer (%u bytes)\n",
			inst_size);

		if (n > pkt_size) {
			/* try to allocate only as much as we need for current
			 * packet */

			skb = alloc_skb(pkt_size, GFP_ATOMIC);
			if (!skb)
				PRINTR("nfnetlink_log: can't even alloc %u "
				       "bytes\n", pkt_size);
		}
	}

	return skb;
}

static int
__nfulnl_send(struct nfulnl_instance *inst)
{
	int status = -1;

	if (inst->qlen > 1)
		NLMSG_PUT(inst->skb, 0, 0,
			  NLMSG_DONE,
			  sizeof(struct nfgenmsg));

	status = nfnetlink_unicast(inst->skb, inst->peer_pid, MSG_DONTWAIT);

	inst->qlen = 0;
	inst->skb = NULL;

nlmsg_failure:
	return status;
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
__build_packet_message(struct nfulnl_instance *inst,
			const struct sk_buff *skb,
			unsigned int data_len,
			unsigned int pf,
			unsigned int hooknum,
			const struct net_device *indev,
			const struct net_device *outdev,
			const struct nf_loginfo *li,
			const char *prefix, unsigned int plen)
{
	struct nfulnl_msg_packet_hdr pmsg;
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	__be32 tmp_uint;
	sk_buff_data_t old_tail = inst->skb->tail;

	nlh = NLMSG_PUT(inst->skb, 0, 0,
			NFNL_SUBSYS_ULOG << 8 | NFULNL_MSG_PACKET,
			sizeof(struct nfgenmsg));
	nfmsg = NLMSG_DATA(nlh);
	nfmsg->nfgen_family = pf;
	nfmsg->version = NFNETLINK_V0;
	nfmsg->res_id = htons(inst->group_num);

	pmsg.hw_protocol	= skb->protocol;
	pmsg.hook		= hooknum;

	NLA_PUT(inst->skb, NFULA_PACKET_HDR, sizeof(pmsg), &pmsg);

	if (prefix)
		NLA_PUT(inst->skb, NFULA_PREFIX, plen, prefix);

	if (indev) {
#ifndef CONFIG_BRIDGE_NETFILTER
		NLA_PUT_BE32(inst->skb, NFULA_IFINDEX_INDEV,
			     htonl(indev->ifindex));
#else
		if (pf == PF_BRIDGE) {
			/* Case 1: outdev is physical input device, we need to
			 * look for bridge group (when called from
			 * netfilter_bridge) */
			NLA_PUT_BE32(inst->skb, NFULA_IFINDEX_PHYSINDEV,
				     htonl(indev->ifindex));
			/* this is the bridge group "brX" */
			NLA_PUT_BE32(inst->skb, NFULA_IFINDEX_INDEV,
				     htonl(indev->br_port->br->dev->ifindex));
		} else {
			/* Case 2: indev is bridge group, we need to look for
			 * physical device (when called from ipv4) */
			NLA_PUT_BE32(inst->skb, NFULA_IFINDEX_INDEV,
				     htonl(indev->ifindex));
			if (skb->nf_bridge && skb->nf_bridge->physindev)
				NLA_PUT_BE32(inst->skb, NFULA_IFINDEX_PHYSINDEV,
					     htonl(skb->nf_bridge->physindev->ifindex));
		}
#endif
	}

	if (outdev) {
		tmp_uint = htonl(outdev->ifindex);
#ifndef CONFIG_BRIDGE_NETFILTER
		NLA_PUT_BE32(inst->skb, NFULA_IFINDEX_OUTDEV,
			     htonl(outdev->ifindex));
#else
		if (pf == PF_BRIDGE) {
			/* Case 1: outdev is physical output device, we need to
			 * look for bridge group (when called from
			 * netfilter_bridge) */
			NLA_PUT_BE32(inst->skb, NFULA_IFINDEX_PHYSOUTDEV,
				     htonl(outdev->ifindex));
			/* this is the bridge group "brX" */
			NLA_PUT_BE32(inst->skb, NFULA_IFINDEX_OUTDEV,
				     htonl(outdev->br_port->br->dev->ifindex));
		} else {
			/* Case 2: indev is a bridge group, we need to look
			 * for physical device (when called from ipv4) */
			NLA_PUT_BE32(inst->skb, NFULA_IFINDEX_OUTDEV,
				     htonl(outdev->ifindex));
			if (skb->nf_bridge && skb->nf_bridge->physoutdev)
				NLA_PUT_BE32(inst->skb, NFULA_IFINDEX_PHYSOUTDEV,
					     htonl(skb->nf_bridge->physoutdev->ifindex));
		}
#endif
	}

	if (skb->mark)
		NLA_PUT_BE32(inst->skb, NFULA_MARK, htonl(skb->mark));

	if (indev && skb->dev) {
		struct nfulnl_msg_packet_hw phw;
		int len = dev_parse_header(skb, phw.hw_addr);
		if (len > 0) {
			phw.hw_addrlen = htons(len);
			NLA_PUT(inst->skb, NFULA_HWADDR, sizeof(phw), &phw);
		}
	}

	if (skb->tstamp.tv64) {
		struct nfulnl_msg_packet_timestamp ts;
		struct timeval tv = ktime_to_timeval(skb->tstamp);
		ts.sec = cpu_to_be64(tv.tv_sec);
		ts.usec = cpu_to_be64(tv.tv_usec);

		NLA_PUT(inst->skb, NFULA_TIMESTAMP, sizeof(ts), &ts);
	}

	/* UID */
	if (skb->sk) {
		read_lock_bh(&skb->sk->sk_callback_lock);
		if (skb->sk->sk_socket && skb->sk->sk_socket->file) {
			__be32 uid = htonl(skb->sk->sk_socket->file->f_uid);
			__be32 gid = htons(skb->sk->sk_socket->file->f_gid);
			/* need to unlock here since NLA_PUT may goto */
			read_unlock_bh(&skb->sk->sk_callback_lock);
			NLA_PUT_BE32(inst->skb, NFULA_UID, uid);
			NLA_PUT_BE32(inst->skb, NFULA_GID, gid);
		} else
			read_unlock_bh(&skb->sk->sk_callback_lock);
	}

	/* local sequence number */
	if (inst->flags & NFULNL_CFG_F_SEQ)
		NLA_PUT_BE32(inst->skb, NFULA_SEQ, htonl(inst->seq++));

	/* global sequence number */
	if (inst->flags & NFULNL_CFG_F_SEQ_GLOBAL)
		NLA_PUT_BE32(inst->skb, NFULA_SEQ_GLOBAL,
			     htonl(atomic_inc_return(&global_seq)));

	if (data_len) {
		struct nlattr *nla;
		int size = nla_attr_size(data_len);

		if (skb_tailroom(inst->skb) < nla_total_size(data_len)) {
			printk(KERN_WARNING "nfnetlink_log: no tailroom!\n");
			goto nlmsg_failure;
		}

		nla = (struct nlattr *)skb_put(inst->skb, nla_total_size(data_len));
		nla->nla_type = NFULA_PAYLOAD;
		nla->nla_len = size;

		if (skb_copy_bits(skb, 0, nla_data(nla), data_len))
			BUG();
	}

	nlh->nlmsg_len = inst->skb->tail - old_tail;
	return 0;

nlmsg_failure:
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
	unsigned int plen;

	if (li_user && li_user->type == NF_LOG_TYPE_ULOG)
		li = li_user;
	else
		li = &default_loginfo;

	inst = instance_lookup_get(li->u.ulog.group);
	if (!inst)
		return;

	plen = 0;
	if (prefix)
		plen = strlen(prefix) + 1;

	/* FIXME: do we want to make the size calculation conditional based on
	 * what is actually present?  way more branches and checks, but more
	 * memory efficient... */
	size =    NLMSG_ALIGN(sizeof(struct nfgenmsg))
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
		+ nla_total_size(sizeof(struct nfulnl_msg_packet_timestamp));

	spin_lock_bh(&inst->lock);

	if (inst->flags & NFULNL_CFG_F_SEQ)
		size += nla_total_size(sizeof(u_int32_t));
	if (inst->flags & NFULNL_CFG_F_SEQ_GLOBAL)
		size += nla_total_size(sizeof(u_int32_t));

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

		size += nla_total_size(data_len);
		break;

	default:
		goto unlock_and_release;
	}

	if (inst->skb &&
	    size > skb_tailroom(inst->skb) - sizeof(struct nfgenmsg)) {
		/* either the queue len is too high or we don't have
		 * enough room in the skb left. flush to userspace. */
		__nfulnl_flush(inst);
	}

	if (!inst->skb) {
		inst->skb = nfulnl_alloc_skb(inst->nlbufsiz, size);
		if (!inst->skb)
			goto alloc_failure;
	}

	inst->qlen++;

	__build_packet_message(inst, skb, data_len, pf,
				hooknum, in, out, li, prefix, plen);

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
				if ((n->net == &init_net) &&
				    (n->pid == inst->peer_pid))
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
		  struct nlmsghdr *nlh, struct nlattr *nfqa[])
{
	return -ENOTSUPP;
}

static const struct nf_logger nfulnl_logger = {
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
		   struct nlmsghdr *nlh, struct nlattr *nfula[])
{
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int16_t group_num = ntohs(nfmsg->res_id);
	struct nfulnl_instance *inst;
	int ret = 0;

	inst = instance_lookup_get(group_num);
	if (inst && inst->peer_pid != NETLINK_CB(skb).pid) {
		ret = -EPERM;
		goto out_put;
	}

	if (nfula[NFULA_CFG_CMD]) {
		u_int8_t pf = nfmsg->nfgen_family;
		struct nfulnl_msg_config_cmd *cmd;

		cmd = nla_data(nfula[NFULA_CFG_CMD]);

		switch (cmd->command) {
		case NFULNL_CFG_CMD_BIND:
			if (inst) {
				ret = -EBUSY;
				goto out_put;
			}

			inst = instance_create(group_num,
					       NETLINK_CB(skb).pid);
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

			instance_destroy(inst);
			goto out;
		case NFULNL_CFG_CMD_PF_BIND:
			ret = nf_log_register(pf, &nfulnl_logger);
			break;
		case NFULNL_CFG_CMD_PF_UNBIND:
			/* This is a bug and a feature.  We cannot unregister
			 * other handlers, like nfnetlink_inst can */
			nf_log_unregister_pf(pf);
			break;
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
	unsigned int bucket;
};

static struct hlist_node *get_first(struct iter_state *st)
{
	if (!st)
		return NULL;

	for (st->bucket = 0; st->bucket < INSTANCE_BUCKETS; st->bucket++) {
		if (!hlist_empty(&instance_table[st->bucket]))
			return instance_table[st->bucket].first;
	}
	return NULL;
}

static struct hlist_node *get_next(struct iter_state *st, struct hlist_node *h)
{
	h = h->next;
	while (!h) {
		if (++st->bucket >= INSTANCE_BUCKETS)
			return NULL;

		h = instance_table[st->bucket].first;
	}
	return h;
}

static struct hlist_node *get_idx(struct iter_state *st, loff_t pos)
{
	struct hlist_node *head;
	head = get_first(st);

	if (head)
		while (pos && (head = get_next(st, head)))
			pos--;
	return pos ? NULL : head;
}

static void *seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock_bh(&instances_lock);
	return get_idx(seq->private, *pos);
}

static void *seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	return get_next(s->private, v);
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

static const struct seq_operations nful_seq_ops = {
	.start	= seq_start,
	.next	= seq_next,
	.stop	= seq_stop,
	.show	= seq_show,
};

static int nful_open(struct inode *inode, struct file *file)
{
	return seq_open_private(file, &nful_seq_ops,
			sizeof(struct iter_state));
}

static const struct file_operations nful_file_ops = {
	.owner	 = THIS_MODULE,
	.open	 = nful_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_private,
};

#endif /* PROC_FS */

static int __init nfnetlink_log_init(void)
{
	int i, status = -ENOMEM;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc_nful;
#endif

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

#ifdef CONFIG_PROC_FS
cleanup_subsys:
	nfnetlink_subsys_unregister(&nfulnl_subsys);
#endif
cleanup_netlink_notifier:
	netlink_unregister_notifier(&nfulnl_rtnl_notifier);
	return status;
}

static void __exit nfnetlink_log_fini(void)
{
	nf_log_unregister(&nfulnl_logger);
#ifdef CONFIG_PROC_FS
	remove_proc_entry("nfnetlink_log", proc_net_netfilter);
#endif
	nfnetlink_subsys_unregister(&nfulnl_subsys);
	netlink_unregister_notifier(&nfulnl_rtnl_notifier);
}

MODULE_DESCRIPTION("netfilter userspace logging");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_ULOG);

module_init(nfnetlink_log_init);
module_exit(nfnetlink_log_fini);
