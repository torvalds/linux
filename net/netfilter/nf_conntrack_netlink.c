/* Connection tracking via netlink socket. Allows for user space
 * protocol helpers and general trouble making from userspace.
 *
 * (C) 2001 by Jay Schulist <jschlst@samba.org>
 * (C) 2002-2006 by Harald Welte <laforge@gnumonks.org>
 * (C) 2003 by Patrick Mchardy <kaber@trash.net>
 * (C) 2005-2006 by Pablo Neira Ayuso <pablo@eurodev.net>
 *
 * Initial connection tracking via netlink development funded and
 * generally made possible by Network Robots, Inc. (www.networkrobots.com)
 *
 * Further development of this code funded by Astaro AG (http://www.astaro.com)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/netlink.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>

#include <linux/netfilter.h>
#include <net/netlink.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#ifdef CONFIG_NF_NAT_NEEDED
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_protocol.h>
#endif

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

MODULE_LICENSE("GPL");

static char __initdata version[] = "0.93";

static inline int
ctnetlink_dump_tuples_proto(struct sk_buff *skb,
			    const struct nf_conntrack_tuple *tuple,
			    struct nf_conntrack_l4proto *l4proto)
{
	int ret = 0;
	struct nfattr *nest_parms = NFA_NEST(skb, CTA_TUPLE_PROTO);

	NFA_PUT(skb, CTA_PROTO_NUM, sizeof(u_int8_t), &tuple->dst.protonum);

	if (likely(l4proto->tuple_to_nfattr))
		ret = l4proto->tuple_to_nfattr(skb, tuple);

	NFA_NEST_END(skb, nest_parms);

	return ret;

nfattr_failure:
	return -1;
}

static inline int
ctnetlink_dump_tuples_ip(struct sk_buff *skb,
			 const struct nf_conntrack_tuple *tuple,
			 struct nf_conntrack_l3proto *l3proto)
{
	int ret = 0;
	struct nfattr *nest_parms = NFA_NEST(skb, CTA_TUPLE_IP);

	if (likely(l3proto->tuple_to_nfattr))
		ret = l3proto->tuple_to_nfattr(skb, tuple);

	NFA_NEST_END(skb, nest_parms);

	return ret;

nfattr_failure:
	return -1;
}

static inline int
ctnetlink_dump_tuples(struct sk_buff *skb,
		      const struct nf_conntrack_tuple *tuple)
{
	int ret;
	struct nf_conntrack_l3proto *l3proto;
	struct nf_conntrack_l4proto *l4proto;

	l3proto = nf_ct_l3proto_find_get(tuple->src.l3num);
	ret = ctnetlink_dump_tuples_ip(skb, tuple, l3proto);
	nf_ct_l3proto_put(l3proto);

	if (unlikely(ret < 0))
		return ret;

	l4proto = nf_ct_l4proto_find_get(tuple->src.l3num, tuple->dst.protonum);
	ret = ctnetlink_dump_tuples_proto(skb, tuple, l4proto);
	nf_ct_l4proto_put(l4proto);

	return ret;
}

static inline int
ctnetlink_dump_status(struct sk_buff *skb, const struct nf_conn *ct)
{
	__be32 status = htonl((u_int32_t) ct->status);
	NFA_PUT(skb, CTA_STATUS, sizeof(status), &status);
	return 0;

nfattr_failure:
	return -1;
}

static inline int
ctnetlink_dump_timeout(struct sk_buff *skb, const struct nf_conn *ct)
{
	long timeout_l = ct->timeout.expires - jiffies;
	__be32 timeout;

	if (timeout_l < 0)
		timeout = 0;
	else
		timeout = htonl(timeout_l / HZ);

	NFA_PUT(skb, CTA_TIMEOUT, sizeof(timeout), &timeout);
	return 0;

nfattr_failure:
	return -1;
}

static inline int
ctnetlink_dump_protoinfo(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nf_conntrack_l4proto *l4proto = nf_ct_l4proto_find_get(ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num, ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum);
	struct nfattr *nest_proto;
	int ret;

	if (!l4proto->to_nfattr) {
		nf_ct_l4proto_put(l4proto);
		return 0;
	}

	nest_proto = NFA_NEST(skb, CTA_PROTOINFO);

	ret = l4proto->to_nfattr(skb, nest_proto, ct);

	nf_ct_l4proto_put(l4proto);

	NFA_NEST_END(skb, nest_proto);

	return ret;

nfattr_failure:
	nf_ct_l4proto_put(l4proto);
	return -1;
}

static inline int
ctnetlink_dump_helpinfo(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nfattr *nest_helper;
	const struct nf_conn_help *help = nfct_help(ct);

	if (!help || !help->helper)
		return 0;

	nest_helper = NFA_NEST(skb, CTA_HELP);
	NFA_PUT(skb, CTA_HELP_NAME, strlen(help->helper->name), help->helper->name);

	if (help->helper->to_nfattr)
		help->helper->to_nfattr(skb, ct);

	NFA_NEST_END(skb, nest_helper);

	return 0;

nfattr_failure:
	return -1;
}

#ifdef CONFIG_NF_CT_ACCT
static inline int
ctnetlink_dump_counters(struct sk_buff *skb, const struct nf_conn *ct,
			enum ip_conntrack_dir dir)
{
	enum ctattr_type type = dir ? CTA_COUNTERS_REPLY: CTA_COUNTERS_ORIG;
	struct nfattr *nest_count = NFA_NEST(skb, type);
	__be32 tmp;

	tmp = htonl(ct->counters[dir].packets);
	NFA_PUT(skb, CTA_COUNTERS32_PACKETS, sizeof(u_int32_t), &tmp);

	tmp = htonl(ct->counters[dir].bytes);
	NFA_PUT(skb, CTA_COUNTERS32_BYTES, sizeof(u_int32_t), &tmp);

	NFA_NEST_END(skb, nest_count);

	return 0;

nfattr_failure:
	return -1;
}
#else
#define ctnetlink_dump_counters(a, b, c) (0)
#endif

#ifdef CONFIG_NF_CONNTRACK_MARK
static inline int
ctnetlink_dump_mark(struct sk_buff *skb, const struct nf_conn *ct)
{
	__be32 mark = htonl(ct->mark);

	NFA_PUT(skb, CTA_MARK, sizeof(u_int32_t), &mark);
	return 0;

nfattr_failure:
	return -1;
}
#else
#define ctnetlink_dump_mark(a, b) (0)
#endif

static inline int
ctnetlink_dump_id(struct sk_buff *skb, const struct nf_conn *ct)
{
	__be32 id = htonl(ct->id);
	NFA_PUT(skb, CTA_ID, sizeof(u_int32_t), &id);
	return 0;

nfattr_failure:
	return -1;
}

static inline int
ctnetlink_dump_use(struct sk_buff *skb, const struct nf_conn *ct)
{
	__be32 use = htonl(atomic_read(&ct->ct_general.use));

	NFA_PUT(skb, CTA_USE, sizeof(u_int32_t), &use);
	return 0;

nfattr_failure:
	return -1;
}

#define tuple(ct, dir) (&(ct)->tuplehash[dir].tuple)

static int
ctnetlink_fill_info(struct sk_buff *skb, u32 pid, u32 seq,
		    int event, int nowait,
		    const struct nf_conn *ct)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct nfattr *nest_parms;
	unsigned char *b = skb_tail_pointer(skb);

	event |= NFNL_SUBSYS_CTNETLINK << 8;
	nlh    = NLMSG_PUT(skb, pid, seq, event, sizeof(struct nfgenmsg));
	nfmsg  = NLMSG_DATA(nlh);

	nlh->nlmsg_flags    = (nowait && pid) ? NLM_F_MULTI : 0;
	nfmsg->nfgen_family =
		ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num;
	nfmsg->version      = NFNETLINK_V0;
	nfmsg->res_id	    = 0;

	nest_parms = NFA_NEST(skb, CTA_TUPLE_ORIG);
	if (ctnetlink_dump_tuples(skb, tuple(ct, IP_CT_DIR_ORIGINAL)) < 0)
		goto nfattr_failure;
	NFA_NEST_END(skb, nest_parms);

	nest_parms = NFA_NEST(skb, CTA_TUPLE_REPLY);
	if (ctnetlink_dump_tuples(skb, tuple(ct, IP_CT_DIR_REPLY)) < 0)
		goto nfattr_failure;
	NFA_NEST_END(skb, nest_parms);

	if (ctnetlink_dump_status(skb, ct) < 0 ||
	    ctnetlink_dump_timeout(skb, ct) < 0 ||
	    ctnetlink_dump_counters(skb, ct, IP_CT_DIR_ORIGINAL) < 0 ||
	    ctnetlink_dump_counters(skb, ct, IP_CT_DIR_REPLY) < 0 ||
	    ctnetlink_dump_protoinfo(skb, ct) < 0 ||
	    ctnetlink_dump_helpinfo(skb, ct) < 0 ||
	    ctnetlink_dump_mark(skb, ct) < 0 ||
	    ctnetlink_dump_id(skb, ct) < 0 ||
	    ctnetlink_dump_use(skb, ct) < 0)
		goto nfattr_failure;

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

nlmsg_failure:
nfattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

#ifdef CONFIG_NF_CONNTRACK_EVENTS
static int ctnetlink_conntrack_event(struct notifier_block *this,
				     unsigned long events, void *ptr)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct nfattr *nest_parms;
	struct nf_conn *ct = (struct nf_conn *)ptr;
	struct sk_buff *skb;
	unsigned int type;
	sk_buff_data_t b;
	unsigned int flags = 0, group;

	/* ignore our fake conntrack entry */
	if (ct == &nf_conntrack_untracked)
		return NOTIFY_DONE;

	if (events & IPCT_DESTROY) {
		type = IPCTNL_MSG_CT_DELETE;
		group = NFNLGRP_CONNTRACK_DESTROY;
	} else  if (events & (IPCT_NEW | IPCT_RELATED)) {
		type = IPCTNL_MSG_CT_NEW;
		flags = NLM_F_CREATE|NLM_F_EXCL;
		group = NFNLGRP_CONNTRACK_NEW;
	} else  if (events & (IPCT_STATUS | IPCT_PROTOINFO)) {
		type = IPCTNL_MSG_CT_NEW;
		group = NFNLGRP_CONNTRACK_UPDATE;
	} else
		return NOTIFY_DONE;

	if (!nfnetlink_has_listeners(group))
		return NOTIFY_DONE;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_ATOMIC);
	if (!skb)
		return NOTIFY_DONE;

	b = skb->tail;

	type |= NFNL_SUBSYS_CTNETLINK << 8;
	nlh   = NLMSG_PUT(skb, 0, 0, type, sizeof(struct nfgenmsg));
	nfmsg = NLMSG_DATA(nlh);

	nlh->nlmsg_flags    = flags;
	nfmsg->nfgen_family = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num;
	nfmsg->version	= NFNETLINK_V0;
	nfmsg->res_id	= 0;

	nest_parms = NFA_NEST(skb, CTA_TUPLE_ORIG);
	if (ctnetlink_dump_tuples(skb, tuple(ct, IP_CT_DIR_ORIGINAL)) < 0)
		goto nfattr_failure;
	NFA_NEST_END(skb, nest_parms);

	nest_parms = NFA_NEST(skb, CTA_TUPLE_REPLY);
	if (ctnetlink_dump_tuples(skb, tuple(ct, IP_CT_DIR_REPLY)) < 0)
		goto nfattr_failure;
	NFA_NEST_END(skb, nest_parms);

	if (events & IPCT_DESTROY) {
		if (ctnetlink_dump_counters(skb, ct, IP_CT_DIR_ORIGINAL) < 0 ||
		    ctnetlink_dump_counters(skb, ct, IP_CT_DIR_REPLY) < 0)
			goto nfattr_failure;
	} else {
		if (ctnetlink_dump_status(skb, ct) < 0)
			goto nfattr_failure;

		if (ctnetlink_dump_timeout(skb, ct) < 0)
			goto nfattr_failure;

		if (events & IPCT_PROTOINFO
		    && ctnetlink_dump_protoinfo(skb, ct) < 0)
			goto nfattr_failure;

		if ((events & IPCT_HELPER || nfct_help(ct))
		    && ctnetlink_dump_helpinfo(skb, ct) < 0)
			goto nfattr_failure;

#ifdef CONFIG_NF_CONNTRACK_MARK
		if ((events & IPCT_MARK || ct->mark)
		    && ctnetlink_dump_mark(skb, ct) < 0)
			goto nfattr_failure;
#endif

		if (events & IPCT_COUNTER_FILLING &&
		    (ctnetlink_dump_counters(skb, ct, IP_CT_DIR_ORIGINAL) < 0 ||
		     ctnetlink_dump_counters(skb, ct, IP_CT_DIR_REPLY) < 0))
			goto nfattr_failure;
	}

	nlh->nlmsg_len = skb->tail - b;
	nfnetlink_send(skb, 0, group, 0);
	return NOTIFY_DONE;

nlmsg_failure:
nfattr_failure:
	kfree_skb(skb);
	return NOTIFY_DONE;
}
#endif /* CONFIG_NF_CONNTRACK_EVENTS */

static int ctnetlink_done(struct netlink_callback *cb)
{
	if (cb->args[1])
		nf_ct_put((struct nf_conn *)cb->args[1]);
	return 0;
}

#define L3PROTO(ct) ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num

static int
ctnetlink_dump_table(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nf_conn *ct, *last;
	struct nf_conntrack_tuple_hash *h;
	struct list_head *i;
	struct nfgenmsg *nfmsg = NLMSG_DATA(cb->nlh);
	u_int8_t l3proto = nfmsg->nfgen_family;

	read_lock_bh(&nf_conntrack_lock);
	last = (struct nf_conn *)cb->args[1];
	for (; cb->args[0] < nf_conntrack_htable_size; cb->args[0]++) {
restart:
		list_for_each_prev(i, &nf_conntrack_hash[cb->args[0]]) {
			h = (struct nf_conntrack_tuple_hash *) i;
			if (NF_CT_DIRECTION(h) != IP_CT_DIR_ORIGINAL)
				continue;
			ct = nf_ct_tuplehash_to_ctrack(h);
			/* Dump entries of a given L3 protocol number.
			 * If it is not specified, ie. l3proto == 0,
			 * then dump everything. */
			if (l3proto && L3PROTO(ct) != l3proto)
				continue;
			if (cb->args[1]) {
				if (ct != last)
					continue;
				cb->args[1] = 0;
			}
			if (ctnetlink_fill_info(skb, NETLINK_CB(cb->skb).pid,
						cb->nlh->nlmsg_seq,
						IPCTNL_MSG_CT_NEW,
						1, ct) < 0) {
				nf_conntrack_get(&ct->ct_general);
				cb->args[1] = (unsigned long)ct;
				goto out;
			}
#ifdef CONFIG_NF_CT_ACCT
			if (NFNL_MSG_TYPE(cb->nlh->nlmsg_type) ==
						IPCTNL_MSG_CT_GET_CTRZERO)
				memset(&ct->counters, 0, sizeof(ct->counters));
#endif
		}
		if (cb->args[1]) {
			cb->args[1] = 0;
			goto restart;
		}
	}
out:
	read_unlock_bh(&nf_conntrack_lock);
	if (last)
		nf_ct_put(last);

	return skb->len;
}

static inline int
ctnetlink_parse_tuple_ip(struct nfattr *attr, struct nf_conntrack_tuple *tuple)
{
	struct nfattr *tb[CTA_IP_MAX];
	struct nf_conntrack_l3proto *l3proto;
	int ret = 0;

	nfattr_parse_nested(tb, CTA_IP_MAX, attr);

	l3proto = nf_ct_l3proto_find_get(tuple->src.l3num);

	if (likely(l3proto->nfattr_to_tuple))
		ret = l3proto->nfattr_to_tuple(tb, tuple);

	nf_ct_l3proto_put(l3proto);

	return ret;
}

static const size_t cta_min_proto[CTA_PROTO_MAX] = {
	[CTA_PROTO_NUM-1]	= sizeof(u_int8_t),
};

static inline int
ctnetlink_parse_tuple_proto(struct nfattr *attr,
			    struct nf_conntrack_tuple *tuple)
{
	struct nfattr *tb[CTA_PROTO_MAX];
	struct nf_conntrack_l4proto *l4proto;
	int ret = 0;

	nfattr_parse_nested(tb, CTA_PROTO_MAX, attr);

	if (nfattr_bad_size(tb, CTA_PROTO_MAX, cta_min_proto))
		return -EINVAL;

	if (!tb[CTA_PROTO_NUM-1])
		return -EINVAL;
	tuple->dst.protonum = *(u_int8_t *)NFA_DATA(tb[CTA_PROTO_NUM-1]);

	l4proto = nf_ct_l4proto_find_get(tuple->src.l3num, tuple->dst.protonum);

	if (likely(l4proto->nfattr_to_tuple))
		ret = l4proto->nfattr_to_tuple(tb, tuple);

	nf_ct_l4proto_put(l4proto);

	return ret;
}

static inline int
ctnetlink_parse_tuple(struct nfattr *cda[], struct nf_conntrack_tuple *tuple,
		      enum ctattr_tuple type, u_int8_t l3num)
{
	struct nfattr *tb[CTA_TUPLE_MAX];
	int err;

	memset(tuple, 0, sizeof(*tuple));

	nfattr_parse_nested(tb, CTA_TUPLE_MAX, cda[type-1]);

	if (!tb[CTA_TUPLE_IP-1])
		return -EINVAL;

	tuple->src.l3num = l3num;

	err = ctnetlink_parse_tuple_ip(tb[CTA_TUPLE_IP-1], tuple);
	if (err < 0)
		return err;

	if (!tb[CTA_TUPLE_PROTO-1])
		return -EINVAL;

	err = ctnetlink_parse_tuple_proto(tb[CTA_TUPLE_PROTO-1], tuple);
	if (err < 0)
		return err;

	/* orig and expect tuples get DIR_ORIGINAL */
	if (type == CTA_TUPLE_REPLY)
		tuple->dst.dir = IP_CT_DIR_REPLY;
	else
		tuple->dst.dir = IP_CT_DIR_ORIGINAL;

	return 0;
}

#ifdef CONFIG_NF_NAT_NEEDED
static const size_t cta_min_protonat[CTA_PROTONAT_MAX] = {
	[CTA_PROTONAT_PORT_MIN-1]       = sizeof(u_int16_t),
	[CTA_PROTONAT_PORT_MAX-1]       = sizeof(u_int16_t),
};

static int nfnetlink_parse_nat_proto(struct nfattr *attr,
				     const struct nf_conn *ct,
				     struct nf_nat_range *range)
{
	struct nfattr *tb[CTA_PROTONAT_MAX];
	struct nf_nat_protocol *npt;

	nfattr_parse_nested(tb, CTA_PROTONAT_MAX, attr);

	if (nfattr_bad_size(tb, CTA_PROTONAT_MAX, cta_min_protonat))
		return -EINVAL;

	npt = nf_nat_proto_find_get(ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum);

	if (!npt->nfattr_to_range) {
		nf_nat_proto_put(npt);
		return 0;
	}

	/* nfattr_to_range returns 1 if it parsed, 0 if not, neg. on error */
	if (npt->nfattr_to_range(tb, range) > 0)
		range->flags |= IP_NAT_RANGE_PROTO_SPECIFIED;

	nf_nat_proto_put(npt);

	return 0;
}

static const size_t cta_min_nat[CTA_NAT_MAX] = {
	[CTA_NAT_MINIP-1]       = sizeof(u_int32_t),
	[CTA_NAT_MAXIP-1]       = sizeof(u_int32_t),
};

static inline int
nfnetlink_parse_nat(struct nfattr *nat,
		    const struct nf_conn *ct, struct nf_nat_range *range)
{
	struct nfattr *tb[CTA_NAT_MAX];
	int err;

	memset(range, 0, sizeof(*range));

	nfattr_parse_nested(tb, CTA_NAT_MAX, nat);

	if (nfattr_bad_size(tb, CTA_NAT_MAX, cta_min_nat))
		return -EINVAL;

	if (tb[CTA_NAT_MINIP-1])
		range->min_ip = *(__be32 *)NFA_DATA(tb[CTA_NAT_MINIP-1]);

	if (!tb[CTA_NAT_MAXIP-1])
		range->max_ip = range->min_ip;
	else
		range->max_ip = *(__be32 *)NFA_DATA(tb[CTA_NAT_MAXIP-1]);

	if (range->min_ip)
		range->flags |= IP_NAT_RANGE_MAP_IPS;

	if (!tb[CTA_NAT_PROTO-1])
		return 0;

	err = nfnetlink_parse_nat_proto(tb[CTA_NAT_PROTO-1], ct, range);
	if (err < 0)
		return err;

	return 0;
}
#endif

static inline int
ctnetlink_parse_help(struct nfattr *attr, char **helper_name)
{
	struct nfattr *tb[CTA_HELP_MAX];

	nfattr_parse_nested(tb, CTA_HELP_MAX, attr);

	if (!tb[CTA_HELP_NAME-1])
		return -EINVAL;

	*helper_name = NFA_DATA(tb[CTA_HELP_NAME-1]);

	return 0;
}

static const size_t cta_min[CTA_MAX] = {
	[CTA_STATUS-1] 		= sizeof(u_int32_t),
	[CTA_TIMEOUT-1] 	= sizeof(u_int32_t),
	[CTA_MARK-1]		= sizeof(u_int32_t),
	[CTA_USE-1]		= sizeof(u_int32_t),
	[CTA_ID-1]		= sizeof(u_int32_t)
};

static int
ctnetlink_del_conntrack(struct sock *ctnl, struct sk_buff *skb,
			struct nlmsghdr *nlh, struct nfattr *cda[])
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;
	struct nf_conn *ct;
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	int err = 0;

	if (nfattr_bad_size(cda, CTA_MAX, cta_min))
		return -EINVAL;

	if (cda[CTA_TUPLE_ORIG-1])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_ORIG, u3);
	else if (cda[CTA_TUPLE_REPLY-1])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_REPLY, u3);
	else {
		/* Flush the whole table */
		nf_conntrack_flush();
		return 0;
	}

	if (err < 0)
		return err;

	h = nf_conntrack_find_get(&tuple, NULL);
	if (!h)
		return -ENOENT;

	ct = nf_ct_tuplehash_to_ctrack(h);

	if (cda[CTA_ID-1]) {
		u_int32_t id = ntohl(*(__be32 *)NFA_DATA(cda[CTA_ID-1]));
		if (ct->id != id) {
			nf_ct_put(ct);
			return -ENOENT;
		}
	}
	if (del_timer(&ct->timeout))
		ct->timeout.function((unsigned long)ct);

	nf_ct_put(ct);

	return 0;
}

static int
ctnetlink_get_conntrack(struct sock *ctnl, struct sk_buff *skb,
			struct nlmsghdr *nlh, struct nfattr *cda[])
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;
	struct nf_conn *ct;
	struct sk_buff *skb2 = NULL;
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	int err = 0;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
#ifndef CONFIG_NF_CT_ACCT
		if (NFNL_MSG_TYPE(nlh->nlmsg_type) == IPCTNL_MSG_CT_GET_CTRZERO)
			return -ENOTSUPP;
#endif
		return netlink_dump_start(ctnl, skb, nlh, ctnetlink_dump_table,
					  ctnetlink_done);
	}

	if (nfattr_bad_size(cda, CTA_MAX, cta_min))
		return -EINVAL;

	if (cda[CTA_TUPLE_ORIG-1])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_ORIG, u3);
	else if (cda[CTA_TUPLE_REPLY-1])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_REPLY, u3);
	else
		return -EINVAL;

	if (err < 0)
		return err;

	h = nf_conntrack_find_get(&tuple, NULL);
	if (!h)
		return -ENOENT;

	ct = nf_ct_tuplehash_to_ctrack(h);

	err = -ENOMEM;
	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb2) {
		nf_ct_put(ct);
		return -ENOMEM;
	}

	err = ctnetlink_fill_info(skb2, NETLINK_CB(skb).pid, nlh->nlmsg_seq,
				  IPCTNL_MSG_CT_NEW, 1, ct);
	nf_ct_put(ct);
	if (err <= 0)
		goto free;

	err = netlink_unicast(ctnl, skb2, NETLINK_CB(skb).pid, MSG_DONTWAIT);
	if (err < 0)
		goto out;

	return 0;

free:
	kfree_skb(skb2);
out:
	return err;
}

static inline int
ctnetlink_change_status(struct nf_conn *ct, struct nfattr *cda[])
{
	unsigned long d;
	unsigned int status = ntohl(*(__be32 *)NFA_DATA(cda[CTA_STATUS-1]));
	d = ct->status ^ status;

	if (d & (IPS_EXPECTED|IPS_CONFIRMED|IPS_DYING))
		/* unchangeable */
		return -EINVAL;

	if (d & IPS_SEEN_REPLY && !(status & IPS_SEEN_REPLY))
		/* SEEN_REPLY bit can only be set */
		return -EINVAL;


	if (d & IPS_ASSURED && !(status & IPS_ASSURED))
		/* ASSURED bit can only be set */
		return -EINVAL;

	if (cda[CTA_NAT_SRC-1] || cda[CTA_NAT_DST-1]) {
#ifndef CONFIG_NF_NAT_NEEDED
		return -EINVAL;
#else
		struct nf_nat_range range;

		if (cda[CTA_NAT_DST-1]) {
			if (nfnetlink_parse_nat(cda[CTA_NAT_DST-1], ct,
						&range) < 0)
				return -EINVAL;
			if (nf_nat_initialized(ct,
					       HOOK2MANIP(NF_IP_PRE_ROUTING)))
				return -EEXIST;
			nf_nat_setup_info(ct, &range, NF_IP_PRE_ROUTING);
		}
		if (cda[CTA_NAT_SRC-1]) {
			if (nfnetlink_parse_nat(cda[CTA_NAT_SRC-1], ct,
						&range) < 0)
				return -EINVAL;
			if (nf_nat_initialized(ct,
					       HOOK2MANIP(NF_IP_POST_ROUTING)))
				return -EEXIST;
			nf_nat_setup_info(ct, &range, NF_IP_POST_ROUTING);
		}
#endif
	}

	/* Be careful here, modifying NAT bits can screw up things,
	 * so don't let users modify them directly if they don't pass
	 * nf_nat_range. */
	ct->status |= status & ~(IPS_NAT_DONE_MASK | IPS_NAT_MASK);
	return 0;
}


static inline int
ctnetlink_change_helper(struct nf_conn *ct, struct nfattr *cda[])
{
	struct nf_conntrack_helper *helper;
	struct nf_conn_help *help = nfct_help(ct);
	char *helpname;
	int err;

	/* don't change helper of sibling connections */
	if (ct->master)
		return -EINVAL;

	err = ctnetlink_parse_help(cda[CTA_HELP-1], &helpname);
	if (err < 0)
		return err;

	if (!strcmp(helpname, "")) {
		if (help && help->helper) {
			/* we had a helper before ... */
			nf_ct_remove_expectations(ct);
			help->helper = NULL;
		}

		return 0;
	}

	if (!help) {
		/* FIXME: we need to reallocate and rehash */
		return -EBUSY;
	}

	helper = __nf_conntrack_helper_find_byname(helpname);
	if (helper == NULL)
		return -EINVAL;

	if (help->helper == helper)
		return 0;

	if (help->helper)
		/* we had a helper before ... */
		nf_ct_remove_expectations(ct);

	/* need to zero data of old helper */
	memset(&help->help, 0, sizeof(help->help));
	help->helper = helper;

	return 0;
}

static inline int
ctnetlink_change_timeout(struct nf_conn *ct, struct nfattr *cda[])
{
	u_int32_t timeout = ntohl(*(__be32 *)NFA_DATA(cda[CTA_TIMEOUT-1]));

	if (!del_timer(&ct->timeout))
		return -ETIME;

	ct->timeout.expires = jiffies + timeout * HZ;
	add_timer(&ct->timeout);

	return 0;
}

static inline int
ctnetlink_change_protoinfo(struct nf_conn *ct, struct nfattr *cda[])
{
	struct nfattr *tb[CTA_PROTOINFO_MAX], *attr = cda[CTA_PROTOINFO-1];
	struct nf_conntrack_l4proto *l4proto;
	u_int16_t npt = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum;
	u_int16_t l3num = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num;
	int err = 0;

	nfattr_parse_nested(tb, CTA_PROTOINFO_MAX, attr);

	l4proto = nf_ct_l4proto_find_get(l3num, npt);

	if (l4proto->from_nfattr)
		err = l4proto->from_nfattr(tb, ct);
	nf_ct_l4proto_put(l4proto);

	return err;
}

static int
ctnetlink_change_conntrack(struct nf_conn *ct, struct nfattr *cda[])
{
	int err;

	if (cda[CTA_HELP-1]) {
		err = ctnetlink_change_helper(ct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_TIMEOUT-1]) {
		err = ctnetlink_change_timeout(ct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_STATUS-1]) {
		err = ctnetlink_change_status(ct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_PROTOINFO-1]) {
		err = ctnetlink_change_protoinfo(ct, cda);
		if (err < 0)
			return err;
	}

#if defined(CONFIG_NF_CONNTRACK_MARK)
	if (cda[CTA_MARK-1])
		ct->mark = ntohl(*(__be32 *)NFA_DATA(cda[CTA_MARK-1]));
#endif

	return 0;
}

static int
ctnetlink_create_conntrack(struct nfattr *cda[],
			   struct nf_conntrack_tuple *otuple,
			   struct nf_conntrack_tuple *rtuple)
{
	struct nf_conn *ct;
	int err = -EINVAL;
	struct nf_conn_help *help;

	ct = nf_conntrack_alloc(otuple, rtuple);
	if (ct == NULL || IS_ERR(ct))
		return -ENOMEM;

	if (!cda[CTA_TIMEOUT-1])
		goto err;
	ct->timeout.expires = ntohl(*(__be32 *)NFA_DATA(cda[CTA_TIMEOUT-1]));

	ct->timeout.expires = jiffies + ct->timeout.expires * HZ;
	ct->status |= IPS_CONFIRMED;

	if (cda[CTA_STATUS-1]) {
		err = ctnetlink_change_status(ct, cda);
		if (err < 0)
			goto err;
	}

	if (cda[CTA_PROTOINFO-1]) {
		err = ctnetlink_change_protoinfo(ct, cda);
		if (err < 0)
			goto err;
	}

#if defined(CONFIG_NF_CONNTRACK_MARK)
	if (cda[CTA_MARK-1])
		ct->mark = ntohl(*(__be32 *)NFA_DATA(cda[CTA_MARK-1]));
#endif

	help = nfct_help(ct);
	if (help)
		help->helper = nf_ct_helper_find_get(rtuple);

	add_timer(&ct->timeout);
	nf_conntrack_hash_insert(ct);

	if (help && help->helper)
		nf_ct_helper_put(help->helper);

	return 0;

err:
	nf_conntrack_free(ct);
	return err;
}

static int
ctnetlink_new_conntrack(struct sock *ctnl, struct sk_buff *skb,
			struct nlmsghdr *nlh, struct nfattr *cda[])
{
	struct nf_conntrack_tuple otuple, rtuple;
	struct nf_conntrack_tuple_hash *h = NULL;
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	int err = 0;

	if (nfattr_bad_size(cda, CTA_MAX, cta_min))
		return -EINVAL;

	if (cda[CTA_TUPLE_ORIG-1]) {
		err = ctnetlink_parse_tuple(cda, &otuple, CTA_TUPLE_ORIG, u3);
		if (err < 0)
			return err;
	}

	if (cda[CTA_TUPLE_REPLY-1]) {
		err = ctnetlink_parse_tuple(cda, &rtuple, CTA_TUPLE_REPLY, u3);
		if (err < 0)
			return err;
	}

	write_lock_bh(&nf_conntrack_lock);
	if (cda[CTA_TUPLE_ORIG-1])
		h = __nf_conntrack_find(&otuple, NULL);
	else if (cda[CTA_TUPLE_REPLY-1])
		h = __nf_conntrack_find(&rtuple, NULL);

	if (h == NULL) {
		write_unlock_bh(&nf_conntrack_lock);
		err = -ENOENT;
		if (nlh->nlmsg_flags & NLM_F_CREATE)
			err = ctnetlink_create_conntrack(cda, &otuple, &rtuple);
		return err;
	}
	/* implicit 'else' */

	/* we only allow nat config for new conntracks */
	if (cda[CTA_NAT_SRC-1] || cda[CTA_NAT_DST-1]) {
		err = -EINVAL;
		goto out_unlock;
	}

	/* We manipulate the conntrack inside the global conntrack table lock,
	 * so there's no need to increase the refcount */
	err = -EEXIST;
	if (!(nlh->nlmsg_flags & NLM_F_EXCL))
		err = ctnetlink_change_conntrack(nf_ct_tuplehash_to_ctrack(h), cda);

out_unlock:
	write_unlock_bh(&nf_conntrack_lock);
	return err;
}

/***********************************************************************
 * EXPECT
 ***********************************************************************/

static inline int
ctnetlink_exp_dump_tuple(struct sk_buff *skb,
			 const struct nf_conntrack_tuple *tuple,
			 enum ctattr_expect type)
{
	struct nfattr *nest_parms = NFA_NEST(skb, type);

	if (ctnetlink_dump_tuples(skb, tuple) < 0)
		goto nfattr_failure;

	NFA_NEST_END(skb, nest_parms);

	return 0;

nfattr_failure:
	return -1;
}

static inline int
ctnetlink_exp_dump_mask(struct sk_buff *skb,
			const struct nf_conntrack_tuple *tuple,
			const struct nf_conntrack_tuple *mask)
{
	int ret;
	struct nf_conntrack_l3proto *l3proto;
	struct nf_conntrack_l4proto *l4proto;
	struct nfattr *nest_parms = NFA_NEST(skb, CTA_EXPECT_MASK);

	l3proto = nf_ct_l3proto_find_get(tuple->src.l3num);
	ret = ctnetlink_dump_tuples_ip(skb, mask, l3proto);
	nf_ct_l3proto_put(l3proto);

	if (unlikely(ret < 0))
		goto nfattr_failure;

	l4proto = nf_ct_l4proto_find_get(tuple->src.l3num, tuple->dst.protonum);
	ret = ctnetlink_dump_tuples_proto(skb, mask, l4proto);
	nf_ct_l4proto_put(l4proto);
	if (unlikely(ret < 0))
		goto nfattr_failure;

	NFA_NEST_END(skb, nest_parms);

	return 0;

nfattr_failure:
	return -1;
}

static inline int
ctnetlink_exp_dump_expect(struct sk_buff *skb,
			  const struct nf_conntrack_expect *exp)
{
	struct nf_conn *master = exp->master;
	__be32 timeout = htonl((exp->timeout.expires - jiffies) / HZ);
	__be32 id = htonl(exp->id);

	if (ctnetlink_exp_dump_tuple(skb, &exp->tuple, CTA_EXPECT_TUPLE) < 0)
		goto nfattr_failure;
	if (ctnetlink_exp_dump_mask(skb, &exp->tuple, &exp->mask) < 0)
		goto nfattr_failure;
	if (ctnetlink_exp_dump_tuple(skb,
				 &master->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
				 CTA_EXPECT_MASTER) < 0)
		goto nfattr_failure;

	NFA_PUT(skb, CTA_EXPECT_TIMEOUT, sizeof(timeout), &timeout);
	NFA_PUT(skb, CTA_EXPECT_ID, sizeof(u_int32_t), &id);

	return 0;

nfattr_failure:
	return -1;
}

static int
ctnetlink_exp_fill_info(struct sk_buff *skb, u32 pid, u32 seq,
		    int event,
		    int nowait,
		    const struct nf_conntrack_expect *exp)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	unsigned char *b = skb_tail_pointer(skb);

	event |= NFNL_SUBSYS_CTNETLINK_EXP << 8;
	nlh    = NLMSG_PUT(skb, pid, seq, event, sizeof(struct nfgenmsg));
	nfmsg  = NLMSG_DATA(nlh);

	nlh->nlmsg_flags    = (nowait && pid) ? NLM_F_MULTI : 0;
	nfmsg->nfgen_family = exp->tuple.src.l3num;
	nfmsg->version	    = NFNETLINK_V0;
	nfmsg->res_id	    = 0;

	if (ctnetlink_exp_dump_expect(skb, exp) < 0)
		goto nfattr_failure;

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

nlmsg_failure:
nfattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

#ifdef CONFIG_NF_CONNTRACK_EVENTS
static int ctnetlink_expect_event(struct notifier_block *this,
				  unsigned long events, void *ptr)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct nf_conntrack_expect *exp = (struct nf_conntrack_expect *)ptr;
	struct sk_buff *skb;
	unsigned int type;
	sk_buff_data_t b;
	int flags = 0;

	if (events & IPEXP_NEW) {
		type = IPCTNL_MSG_EXP_NEW;
		flags = NLM_F_CREATE|NLM_F_EXCL;
	} else
		return NOTIFY_DONE;

	if (!nfnetlink_has_listeners(NFNLGRP_CONNTRACK_EXP_NEW))
		return NOTIFY_DONE;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_ATOMIC);
	if (!skb)
		return NOTIFY_DONE;

	b = skb->tail;

	type |= NFNL_SUBSYS_CTNETLINK_EXP << 8;
	nlh   = NLMSG_PUT(skb, 0, 0, type, sizeof(struct nfgenmsg));
	nfmsg = NLMSG_DATA(nlh);

	nlh->nlmsg_flags    = flags;
	nfmsg->nfgen_family = exp->tuple.src.l3num;
	nfmsg->version	    = NFNETLINK_V0;
	nfmsg->res_id	    = 0;

	if (ctnetlink_exp_dump_expect(skb, exp) < 0)
		goto nfattr_failure;

	nlh->nlmsg_len = skb->tail - b;
	nfnetlink_send(skb, 0, NFNLGRP_CONNTRACK_EXP_NEW, 0);
	return NOTIFY_DONE;

nlmsg_failure:
nfattr_failure:
	kfree_skb(skb);
	return NOTIFY_DONE;
}
#endif

static int
ctnetlink_exp_dump_table(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nf_conntrack_expect *exp = NULL;
	struct list_head *i;
	u_int32_t *id = (u_int32_t *) &cb->args[0];
	struct nfgenmsg *nfmsg = NLMSG_DATA(cb->nlh);
	u_int8_t l3proto = nfmsg->nfgen_family;

	read_lock_bh(&nf_conntrack_lock);
	list_for_each_prev(i, &nf_conntrack_expect_list) {
		exp = (struct nf_conntrack_expect *) i;
		if (l3proto && exp->tuple.src.l3num != l3proto)
			continue;
		if (exp->id <= *id)
			continue;
		if (ctnetlink_exp_fill_info(skb, NETLINK_CB(cb->skb).pid,
					    cb->nlh->nlmsg_seq,
					    IPCTNL_MSG_EXP_NEW,
					    1, exp) < 0)
			goto out;
		*id = exp->id;
	}
out:
	read_unlock_bh(&nf_conntrack_lock);

	return skb->len;
}

static const size_t cta_min_exp[CTA_EXPECT_MAX] = {
	[CTA_EXPECT_TIMEOUT-1]          = sizeof(u_int32_t),
	[CTA_EXPECT_ID-1]               = sizeof(u_int32_t)
};

static int
ctnetlink_get_expect(struct sock *ctnl, struct sk_buff *skb,
		     struct nlmsghdr *nlh, struct nfattr *cda[])
{
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_expect *exp;
	struct sk_buff *skb2;
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	int err = 0;

	if (nfattr_bad_size(cda, CTA_EXPECT_MAX, cta_min_exp))
		return -EINVAL;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		return netlink_dump_start(ctnl, skb, nlh,
					  ctnetlink_exp_dump_table,
					  ctnetlink_done);
	}

	if (cda[CTA_EXPECT_MASTER-1])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_MASTER, u3);
	else
		return -EINVAL;

	if (err < 0)
		return err;

	exp = nf_conntrack_expect_find_get(&tuple);
	if (!exp)
		return -ENOENT;

	if (cda[CTA_EXPECT_ID-1]) {
		__be32 id = *(__be32 *)NFA_DATA(cda[CTA_EXPECT_ID-1]);
		if (exp->id != ntohl(id)) {
			nf_conntrack_expect_put(exp);
			return -ENOENT;
		}
	}

	err = -ENOMEM;
	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb2)
		goto out;

	err = ctnetlink_exp_fill_info(skb2, NETLINK_CB(skb).pid,
				      nlh->nlmsg_seq, IPCTNL_MSG_EXP_NEW,
				      1, exp);
	if (err <= 0)
		goto free;

	nf_conntrack_expect_put(exp);

	return netlink_unicast(ctnl, skb2, NETLINK_CB(skb).pid, MSG_DONTWAIT);

free:
	kfree_skb(skb2);
out:
	nf_conntrack_expect_put(exp);
	return err;
}

static int
ctnetlink_del_expect(struct sock *ctnl, struct sk_buff *skb,
		     struct nlmsghdr *nlh, struct nfattr *cda[])
{
	struct nf_conntrack_expect *exp, *tmp;
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_helper *h;
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	int err;

	if (nfattr_bad_size(cda, CTA_EXPECT_MAX, cta_min_exp))
		return -EINVAL;

	if (cda[CTA_EXPECT_TUPLE-1]) {
		/* delete a single expect by tuple */
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_TUPLE, u3);
		if (err < 0)
			return err;

		/* bump usage count to 2 */
		exp = nf_conntrack_expect_find_get(&tuple);
		if (!exp)
			return -ENOENT;

		if (cda[CTA_EXPECT_ID-1]) {
			__be32 id = *(__be32 *)NFA_DATA(cda[CTA_EXPECT_ID-1]);
			if (exp->id != ntohl(id)) {
				nf_conntrack_expect_put(exp);
				return -ENOENT;
			}
		}

		/* after list removal, usage count == 1 */
		nf_conntrack_unexpect_related(exp);
		/* have to put what we 'get' above.
		 * after this line usage count == 0 */
		nf_conntrack_expect_put(exp);
	} else if (cda[CTA_EXPECT_HELP_NAME-1]) {
		char *name = NFA_DATA(cda[CTA_EXPECT_HELP_NAME-1]);

		/* delete all expectations for this helper */
		write_lock_bh(&nf_conntrack_lock);
		h = __nf_conntrack_helper_find_byname(name);
		if (!h) {
			write_unlock_bh(&nf_conntrack_lock);
			return -EINVAL;
		}
		list_for_each_entry_safe(exp, tmp, &nf_conntrack_expect_list,
					 list) {
			struct nf_conn_help *m_help = nfct_help(exp->master);
			if (m_help->helper == h
			    && del_timer(&exp->timeout)) {
				nf_ct_unlink_expect(exp);
				nf_conntrack_expect_put(exp);
			}
		}
		write_unlock_bh(&nf_conntrack_lock);
	} else {
		/* This basically means we have to flush everything*/
		write_lock_bh(&nf_conntrack_lock);
		list_for_each_entry_safe(exp, tmp, &nf_conntrack_expect_list,
					 list) {
			if (del_timer(&exp->timeout)) {
				nf_ct_unlink_expect(exp);
				nf_conntrack_expect_put(exp);
			}
		}
		write_unlock_bh(&nf_conntrack_lock);
	}

	return 0;
}
static int
ctnetlink_change_expect(struct nf_conntrack_expect *x, struct nfattr *cda[])
{
	return -EOPNOTSUPP;
}

static int
ctnetlink_create_expect(struct nfattr *cda[], u_int8_t u3)
{
	struct nf_conntrack_tuple tuple, mask, master_tuple;
	struct nf_conntrack_tuple_hash *h = NULL;
	struct nf_conntrack_expect *exp;
	struct nf_conn *ct;
	struct nf_conn_help *help;
	int err = 0;

	/* caller guarantees that those three CTA_EXPECT_* exist */
	err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_TUPLE, u3);
	if (err < 0)
		return err;
	err = ctnetlink_parse_tuple(cda, &mask, CTA_EXPECT_MASK, u3);
	if (err < 0)
		return err;
	err = ctnetlink_parse_tuple(cda, &master_tuple, CTA_EXPECT_MASTER, u3);
	if (err < 0)
		return err;

	/* Look for master conntrack of this expectation */
	h = nf_conntrack_find_get(&master_tuple, NULL);
	if (!h)
		return -ENOENT;
	ct = nf_ct_tuplehash_to_ctrack(h);
	help = nfct_help(ct);

	if (!help || !help->helper) {
		/* such conntrack hasn't got any helper, abort */
		err = -EINVAL;
		goto out;
	}

	exp = nf_conntrack_expect_alloc(ct);
	if (!exp) {
		err = -ENOMEM;
		goto out;
	}

	exp->expectfn = NULL;
	exp->flags = 0;
	exp->master = ct;
	exp->helper = NULL;
	memcpy(&exp->tuple, &tuple, sizeof(struct nf_conntrack_tuple));
	memcpy(&exp->mask, &mask, sizeof(struct nf_conntrack_tuple));

	err = nf_conntrack_expect_related(exp);
	nf_conntrack_expect_put(exp);

out:
	nf_ct_put(nf_ct_tuplehash_to_ctrack(h));
	return err;
}

static int
ctnetlink_new_expect(struct sock *ctnl, struct sk_buff *skb,
		     struct nlmsghdr *nlh, struct nfattr *cda[])
{
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_expect *exp;
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	int err = 0;

	if (nfattr_bad_size(cda, CTA_EXPECT_MAX, cta_min_exp))
		return -EINVAL;

	if (!cda[CTA_EXPECT_TUPLE-1]
	    || !cda[CTA_EXPECT_MASK-1]
	    || !cda[CTA_EXPECT_MASTER-1])
		return -EINVAL;

	err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_TUPLE, u3);
	if (err < 0)
		return err;

	write_lock_bh(&nf_conntrack_lock);
	exp = __nf_conntrack_expect_find(&tuple);

	if (!exp) {
		write_unlock_bh(&nf_conntrack_lock);
		err = -ENOENT;
		if (nlh->nlmsg_flags & NLM_F_CREATE)
			err = ctnetlink_create_expect(cda, u3);
		return err;
	}

	err = -EEXIST;
	if (!(nlh->nlmsg_flags & NLM_F_EXCL))
		err = ctnetlink_change_expect(exp, cda);
	write_unlock_bh(&nf_conntrack_lock);

	return err;
}

#ifdef CONFIG_NF_CONNTRACK_EVENTS
static struct notifier_block ctnl_notifier = {
	.notifier_call	= ctnetlink_conntrack_event,
};

static struct notifier_block ctnl_notifier_exp = {
	.notifier_call	= ctnetlink_expect_event,
};
#endif

static struct nfnl_callback ctnl_cb[IPCTNL_MSG_MAX] = {
	[IPCTNL_MSG_CT_NEW]		= { .call = ctnetlink_new_conntrack,
					    .attr_count = CTA_MAX, },
	[IPCTNL_MSG_CT_GET] 		= { .call = ctnetlink_get_conntrack,
					    .attr_count = CTA_MAX, },
	[IPCTNL_MSG_CT_DELETE]  	= { .call = ctnetlink_del_conntrack,
					    .attr_count = CTA_MAX, },
	[IPCTNL_MSG_CT_GET_CTRZERO] 	= { .call = ctnetlink_get_conntrack,
					    .attr_count = CTA_MAX, },
};

static struct nfnl_callback ctnl_exp_cb[IPCTNL_MSG_EXP_MAX] = {
	[IPCTNL_MSG_EXP_GET]		= { .call = ctnetlink_get_expect,
					    .attr_count = CTA_EXPECT_MAX, },
	[IPCTNL_MSG_EXP_NEW]		= { .call = ctnetlink_new_expect,
					    .attr_count = CTA_EXPECT_MAX, },
	[IPCTNL_MSG_EXP_DELETE]		= { .call = ctnetlink_del_expect,
					    .attr_count = CTA_EXPECT_MAX, },
};

static struct nfnetlink_subsystem ctnl_subsys = {
	.name				= "conntrack",
	.subsys_id			= NFNL_SUBSYS_CTNETLINK,
	.cb_count			= IPCTNL_MSG_MAX,
	.cb				= ctnl_cb,
};

static struct nfnetlink_subsystem ctnl_exp_subsys = {
	.name				= "conntrack_expect",
	.subsys_id			= NFNL_SUBSYS_CTNETLINK_EXP,
	.cb_count			= IPCTNL_MSG_EXP_MAX,
	.cb				= ctnl_exp_cb,
};

MODULE_ALIAS("ip_conntrack_netlink");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_CTNETLINK);
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_CTNETLINK_EXP);

static int __init ctnetlink_init(void)
{
	int ret;

	printk("ctnetlink v%s: registering with nfnetlink.\n", version);
	ret = nfnetlink_subsys_register(&ctnl_subsys);
	if (ret < 0) {
		printk("ctnetlink_init: cannot register with nfnetlink.\n");
		goto err_out;
	}

	ret = nfnetlink_subsys_register(&ctnl_exp_subsys);
	if (ret < 0) {
		printk("ctnetlink_init: cannot register exp with nfnetlink.\n");
		goto err_unreg_subsys;
	}

#ifdef CONFIG_NF_CONNTRACK_EVENTS
	ret = nf_conntrack_register_notifier(&ctnl_notifier);
	if (ret < 0) {
		printk("ctnetlink_init: cannot register notifier.\n");
		goto err_unreg_exp_subsys;
	}

	ret = nf_conntrack_expect_register_notifier(&ctnl_notifier_exp);
	if (ret < 0) {
		printk("ctnetlink_init: cannot expect register notifier.\n");
		goto err_unreg_notifier;
	}
#endif

	return 0;

#ifdef CONFIG_NF_CONNTRACK_EVENTS
err_unreg_notifier:
	nf_conntrack_unregister_notifier(&ctnl_notifier);
err_unreg_exp_subsys:
	nfnetlink_subsys_unregister(&ctnl_exp_subsys);
#endif
err_unreg_subsys:
	nfnetlink_subsys_unregister(&ctnl_subsys);
err_out:
	return ret;
}

static void __exit ctnetlink_exit(void)
{
	printk("ctnetlink: unregistering from nfnetlink.\n");

#ifdef CONFIG_NF_CONNTRACK_EVENTS
	nf_conntrack_expect_unregister_notifier(&ctnl_notifier_exp);
	nf_conntrack_unregister_notifier(&ctnl_notifier);
#endif

	nfnetlink_subsys_unregister(&ctnl_exp_subsys);
	nfnetlink_subsys_unregister(&ctnl_subsys);
	return;
}

module_init(ctnetlink_init);
module_exit(ctnetlink_exit);
