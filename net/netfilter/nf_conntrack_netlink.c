/* Connection tracking via netlink socket. Allows for user space
 * protocol helpers and general trouble making from userspace.
 *
 * (C) 2001 by Jay Schulist <jschlst@samba.org>
 * (C) 2002-2006 by Harald Welte <laforge@gnumonks.org>
 * (C) 2003 by Patrick Mchardy <kaber@trash.net>
 * (C) 2005-2008 by Pablo Neira Ayuso <pablo@netfilter.org>
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
#include <linux/rculist.h>
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
#include <net/netfilter/nf_conntrack_acct.h>
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
	struct nlattr *nest_parms;

	nest_parms = nla_nest_start(skb, CTA_TUPLE_PROTO | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	NLA_PUT_U8(skb, CTA_PROTO_NUM, tuple->dst.protonum);

	if (likely(l4proto->tuple_to_nlattr))
		ret = l4proto->tuple_to_nlattr(skb, tuple);

	nla_nest_end(skb, nest_parms);

	return ret;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_dump_tuples_ip(struct sk_buff *skb,
			 const struct nf_conntrack_tuple *tuple,
			 struct nf_conntrack_l3proto *l3proto)
{
	int ret = 0;
	struct nlattr *nest_parms;

	nest_parms = nla_nest_start(skb, CTA_TUPLE_IP | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;

	if (likely(l3proto->tuple_to_nlattr))
		ret = l3proto->tuple_to_nlattr(skb, tuple);

	nla_nest_end(skb, nest_parms);

	return ret;

nla_put_failure:
	return -1;
}

static int
ctnetlink_dump_tuples(struct sk_buff *skb,
		      const struct nf_conntrack_tuple *tuple)
{
	int ret;
	struct nf_conntrack_l3proto *l3proto;
	struct nf_conntrack_l4proto *l4proto;

	l3proto = __nf_ct_l3proto_find(tuple->src.l3num);
	ret = ctnetlink_dump_tuples_ip(skb, tuple, l3proto);

	if (unlikely(ret < 0))
		return ret;

	l4proto = __nf_ct_l4proto_find(tuple->src.l3num, tuple->dst.protonum);
	ret = ctnetlink_dump_tuples_proto(skb, tuple, l4proto);

	return ret;
}

static inline int
ctnetlink_dump_status(struct sk_buff *skb, const struct nf_conn *ct)
{
	NLA_PUT_BE32(skb, CTA_STATUS, htonl(ct->status));
	return 0;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_dump_timeout(struct sk_buff *skb, const struct nf_conn *ct)
{
	long timeout = (ct->timeout.expires - jiffies) / HZ;

	if (timeout < 0)
		timeout = 0;

	NLA_PUT_BE32(skb, CTA_TIMEOUT, htonl(timeout));
	return 0;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_dump_protoinfo(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nf_conntrack_l4proto *l4proto;
	struct nlattr *nest_proto;
	int ret;

	l4proto = __nf_ct_l4proto_find(nf_ct_l3num(ct), nf_ct_protonum(ct));
	if (!l4proto->to_nlattr)
		return 0;

	nest_proto = nla_nest_start(skb, CTA_PROTOINFO | NLA_F_NESTED);
	if (!nest_proto)
		goto nla_put_failure;

	ret = l4proto->to_nlattr(skb, nest_proto, ct);

	nla_nest_end(skb, nest_proto);

	return ret;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_dump_helpinfo(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nlattr *nest_helper;
	const struct nf_conn_help *help = nfct_help(ct);
	struct nf_conntrack_helper *helper;

	if (!help)
		return 0;

	helper = rcu_dereference(help->helper);
	if (!helper)
		goto out;

	nest_helper = nla_nest_start(skb, CTA_HELP | NLA_F_NESTED);
	if (!nest_helper)
		goto nla_put_failure;
	NLA_PUT_STRING(skb, CTA_HELP_NAME, helper->name);

	if (helper->to_nlattr)
		helper->to_nlattr(skb, ct);

	nla_nest_end(skb, nest_helper);
out:
	return 0;

nla_put_failure:
	return -1;
}

static int
ctnetlink_dump_counters(struct sk_buff *skb, const struct nf_conn *ct,
			enum ip_conntrack_dir dir)
{
	enum ctattr_type type = dir ? CTA_COUNTERS_REPLY: CTA_COUNTERS_ORIG;
	struct nlattr *nest_count;
	const struct nf_conn_counter *acct;

	acct = nf_conn_acct_find(ct);
	if (!acct)
		return 0;

	nest_count = nla_nest_start(skb, type | NLA_F_NESTED);
	if (!nest_count)
		goto nla_put_failure;

	NLA_PUT_BE64(skb, CTA_COUNTERS_PACKETS,
		     cpu_to_be64(acct[dir].packets));
	NLA_PUT_BE64(skb, CTA_COUNTERS_BYTES,
		     cpu_to_be64(acct[dir].bytes));

	nla_nest_end(skb, nest_count);

	return 0;

nla_put_failure:
	return -1;
}

#ifdef CONFIG_NF_CONNTRACK_MARK
static inline int
ctnetlink_dump_mark(struct sk_buff *skb, const struct nf_conn *ct)
{
	NLA_PUT_BE32(skb, CTA_MARK, htonl(ct->mark));
	return 0;

nla_put_failure:
	return -1;
}
#else
#define ctnetlink_dump_mark(a, b) (0)
#endif

#ifdef CONFIG_NF_CONNTRACK_SECMARK
static inline int
ctnetlink_dump_secmark(struct sk_buff *skb, const struct nf_conn *ct)
{
	NLA_PUT_BE32(skb, CTA_SECMARK, htonl(ct->secmark));
	return 0;

nla_put_failure:
	return -1;
}
#else
#define ctnetlink_dump_secmark(a, b) (0)
#endif

#define master_tuple(ct) &(ct->master->tuplehash[IP_CT_DIR_ORIGINAL].tuple)

static inline int
ctnetlink_dump_master(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nlattr *nest_parms;

	if (!(ct->status & IPS_EXPECTED))
		return 0;

	nest_parms = nla_nest_start(skb, CTA_TUPLE_MASTER | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, master_tuple(ct)) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	return 0;

nla_put_failure:
	return -1;
}

#ifdef CONFIG_NF_NAT_NEEDED
static int
dump_nat_seq_adj(struct sk_buff *skb, const struct nf_nat_seq *natseq, int type)
{
	struct nlattr *nest_parms;

	nest_parms = nla_nest_start(skb, type | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;

	NLA_PUT_BE32(skb, CTA_NAT_SEQ_CORRECTION_POS,
		     htonl(natseq->correction_pos));
	NLA_PUT_BE32(skb, CTA_NAT_SEQ_OFFSET_BEFORE,
		     htonl(natseq->offset_before));
	NLA_PUT_BE32(skb, CTA_NAT_SEQ_OFFSET_AFTER,
		     htonl(natseq->offset_after));

	nla_nest_end(skb, nest_parms);

	return 0;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_dump_nat_seq_adj(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nf_nat_seq *natseq;
	struct nf_conn_nat *nat = nfct_nat(ct);

	if (!(ct->status & IPS_SEQ_ADJUST) || !nat)
		return 0;

	natseq = &nat->seq[IP_CT_DIR_ORIGINAL];
	if (dump_nat_seq_adj(skb, natseq, CTA_NAT_SEQ_ADJ_ORIG) == -1)
		return -1;

	natseq = &nat->seq[IP_CT_DIR_REPLY];
	if (dump_nat_seq_adj(skb, natseq, CTA_NAT_SEQ_ADJ_REPLY) == -1)
		return -1;

	return 0;
}
#else
#define ctnetlink_dump_nat_seq_adj(a, b) (0)
#endif

static inline int
ctnetlink_dump_id(struct sk_buff *skb, const struct nf_conn *ct)
{
	NLA_PUT_BE32(skb, CTA_ID, htonl((unsigned long)ct));
	return 0;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_dump_use(struct sk_buff *skb, const struct nf_conn *ct)
{
	NLA_PUT_BE32(skb, CTA_USE, htonl(atomic_read(&ct->ct_general.use)));
	return 0;

nla_put_failure:
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
	struct nlattr *nest_parms;
	unsigned char *b = skb_tail_pointer(skb);

	event |= NFNL_SUBSYS_CTNETLINK << 8;
	nlh    = NLMSG_PUT(skb, pid, seq, event, sizeof(struct nfgenmsg));
	nfmsg  = NLMSG_DATA(nlh);

	nlh->nlmsg_flags    = (nowait && pid) ? NLM_F_MULTI : 0;
	nfmsg->nfgen_family = nf_ct_l3num(ct);
	nfmsg->version      = NFNETLINK_V0;
	nfmsg->res_id	    = 0;

	nest_parms = nla_nest_start(skb, CTA_TUPLE_ORIG | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, tuple(ct, IP_CT_DIR_ORIGINAL)) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	nest_parms = nla_nest_start(skb, CTA_TUPLE_REPLY | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, tuple(ct, IP_CT_DIR_REPLY)) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	if (ctnetlink_dump_status(skb, ct) < 0 ||
	    ctnetlink_dump_timeout(skb, ct) < 0 ||
	    ctnetlink_dump_counters(skb, ct, IP_CT_DIR_ORIGINAL) < 0 ||
	    ctnetlink_dump_counters(skb, ct, IP_CT_DIR_REPLY) < 0 ||
	    ctnetlink_dump_protoinfo(skb, ct) < 0 ||
	    ctnetlink_dump_helpinfo(skb, ct) < 0 ||
	    ctnetlink_dump_mark(skb, ct) < 0 ||
	    ctnetlink_dump_secmark(skb, ct) < 0 ||
	    ctnetlink_dump_id(skb, ct) < 0 ||
	    ctnetlink_dump_use(skb, ct) < 0 ||
	    ctnetlink_dump_master(skb, ct) < 0 ||
	    ctnetlink_dump_nat_seq_adj(skb, ct) < 0)
		goto nla_put_failure;

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

nlmsg_failure:
nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

#ifdef CONFIG_NF_CONNTRACK_EVENTS
static int ctnetlink_conntrack_event(struct notifier_block *this,
				     unsigned long events, void *ptr)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct nlattr *nest_parms;
	struct nf_ct_event *item = (struct nf_ct_event *)ptr;
	struct nf_conn *ct = item->ct;
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

	if (!item->report && !nfnetlink_has_listeners(group))
		return NOTIFY_DONE;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_ATOMIC);
	if (!skb)
		return NOTIFY_DONE;

	b = skb->tail;

	type |= NFNL_SUBSYS_CTNETLINK << 8;
	nlh   = NLMSG_PUT(skb, item->pid, 0, type, sizeof(struct nfgenmsg));
	nfmsg = NLMSG_DATA(nlh);

	nlh->nlmsg_flags    = flags;
	nfmsg->nfgen_family = nf_ct_l3num(ct);
	nfmsg->version	= NFNETLINK_V0;
	nfmsg->res_id	= 0;

	rcu_read_lock();
	nest_parms = nla_nest_start(skb, CTA_TUPLE_ORIG | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, tuple(ct, IP_CT_DIR_ORIGINAL)) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	nest_parms = nla_nest_start(skb, CTA_TUPLE_REPLY | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, tuple(ct, IP_CT_DIR_REPLY)) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	if (ctnetlink_dump_id(skb, ct) < 0)
		goto nla_put_failure;

	if (ctnetlink_dump_status(skb, ct) < 0)
		goto nla_put_failure;

	if (events & IPCT_DESTROY) {
		if (ctnetlink_dump_counters(skb, ct, IP_CT_DIR_ORIGINAL) < 0 ||
		    ctnetlink_dump_counters(skb, ct, IP_CT_DIR_REPLY) < 0)
			goto nla_put_failure;
	} else {
		if (ctnetlink_dump_timeout(skb, ct) < 0)
			goto nla_put_failure;

		if (events & IPCT_PROTOINFO
		    && ctnetlink_dump_protoinfo(skb, ct) < 0)
			goto nla_put_failure;

		if ((events & IPCT_HELPER || nfct_help(ct))
		    && ctnetlink_dump_helpinfo(skb, ct) < 0)
			goto nla_put_failure;

#ifdef CONFIG_NF_CONNTRACK_SECMARK
		if ((events & IPCT_SECMARK || ct->secmark)
		    && ctnetlink_dump_secmark(skb, ct) < 0)
			goto nla_put_failure;
#endif

		if (events & IPCT_RELATED &&
		    ctnetlink_dump_master(skb, ct) < 0)
			goto nla_put_failure;

		if (events & IPCT_NATSEQADJ &&
		    ctnetlink_dump_nat_seq_adj(skb, ct) < 0)
			goto nla_put_failure;
	}

#ifdef CONFIG_NF_CONNTRACK_MARK
	if ((events & IPCT_MARK || ct->mark)
	    && ctnetlink_dump_mark(skb, ct) < 0)
		goto nla_put_failure;
#endif
	rcu_read_unlock();

	nlh->nlmsg_len = skb->tail - b;
	nfnetlink_send(skb, item->pid, group, item->report);
	return NOTIFY_DONE;

nla_put_failure:
	rcu_read_unlock();
nlmsg_failure:
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

static int
ctnetlink_dump_table(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nf_conn *ct, *last;
	struct nf_conntrack_tuple_hash *h;
	struct hlist_node *n;
	struct nfgenmsg *nfmsg = NLMSG_DATA(cb->nlh);
	u_int8_t l3proto = nfmsg->nfgen_family;

	rcu_read_lock();
	last = (struct nf_conn *)cb->args[1];
	for (; cb->args[0] < nf_conntrack_htable_size; cb->args[0]++) {
restart:
		hlist_for_each_entry_rcu(h, n, &init_net.ct.hash[cb->args[0]],
					 hnode) {
			if (NF_CT_DIRECTION(h) != IP_CT_DIR_ORIGINAL)
				continue;
			ct = nf_ct_tuplehash_to_ctrack(h);
			/* Dump entries of a given L3 protocol number.
			 * If it is not specified, ie. l3proto == 0,
			 * then dump everything. */
			if (l3proto && nf_ct_l3num(ct) != l3proto)
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
				if (!atomic_inc_not_zero(&ct->ct_general.use))
					continue;
				cb->args[1] = (unsigned long)ct;
				goto out;
			}

			if (NFNL_MSG_TYPE(cb->nlh->nlmsg_type) ==
						IPCTNL_MSG_CT_GET_CTRZERO) {
				struct nf_conn_counter *acct;

				acct = nf_conn_acct_find(ct);
				if (acct)
					memset(acct, 0, sizeof(struct nf_conn_counter[IP_CT_DIR_MAX]));
			}
		}
		if (cb->args[1]) {
			cb->args[1] = 0;
			goto restart;
		}
	}
out:
	rcu_read_unlock();
	if (last)
		nf_ct_put(last);

	return skb->len;
}

static inline int
ctnetlink_parse_tuple_ip(struct nlattr *attr, struct nf_conntrack_tuple *tuple)
{
	struct nlattr *tb[CTA_IP_MAX+1];
	struct nf_conntrack_l3proto *l3proto;
	int ret = 0;

	nla_parse_nested(tb, CTA_IP_MAX, attr, NULL);

	l3proto = nf_ct_l3proto_find_get(tuple->src.l3num);

	if (likely(l3proto->nlattr_to_tuple)) {
		ret = nla_validate_nested(attr, CTA_IP_MAX,
					  l3proto->nla_policy);
		if (ret == 0)
			ret = l3proto->nlattr_to_tuple(tb, tuple);
	}

	nf_ct_l3proto_put(l3proto);

	return ret;
}

static const struct nla_policy proto_nla_policy[CTA_PROTO_MAX+1] = {
	[CTA_PROTO_NUM]	= { .type = NLA_U8 },
};

static inline int
ctnetlink_parse_tuple_proto(struct nlattr *attr,
			    struct nf_conntrack_tuple *tuple)
{
	struct nlattr *tb[CTA_PROTO_MAX+1];
	struct nf_conntrack_l4proto *l4proto;
	int ret = 0;

	ret = nla_parse_nested(tb, CTA_PROTO_MAX, attr, proto_nla_policy);
	if (ret < 0)
		return ret;

	if (!tb[CTA_PROTO_NUM])
		return -EINVAL;
	tuple->dst.protonum = nla_get_u8(tb[CTA_PROTO_NUM]);

	l4proto = nf_ct_l4proto_find_get(tuple->src.l3num, tuple->dst.protonum);

	if (likely(l4proto->nlattr_to_tuple)) {
		ret = nla_validate_nested(attr, CTA_PROTO_MAX,
					  l4proto->nla_policy);
		if (ret == 0)
			ret = l4proto->nlattr_to_tuple(tb, tuple);
	}

	nf_ct_l4proto_put(l4proto);

	return ret;
}

static int
ctnetlink_parse_tuple(struct nlattr *cda[], struct nf_conntrack_tuple *tuple,
		      enum ctattr_tuple type, u_int8_t l3num)
{
	struct nlattr *tb[CTA_TUPLE_MAX+1];
	int err;

	memset(tuple, 0, sizeof(*tuple));

	nla_parse_nested(tb, CTA_TUPLE_MAX, cda[type], NULL);

	if (!tb[CTA_TUPLE_IP])
		return -EINVAL;

	tuple->src.l3num = l3num;

	err = ctnetlink_parse_tuple_ip(tb[CTA_TUPLE_IP], tuple);
	if (err < 0)
		return err;

	if (!tb[CTA_TUPLE_PROTO])
		return -EINVAL;

	err = ctnetlink_parse_tuple_proto(tb[CTA_TUPLE_PROTO], tuple);
	if (err < 0)
		return err;

	/* orig and expect tuples get DIR_ORIGINAL */
	if (type == CTA_TUPLE_REPLY)
		tuple->dst.dir = IP_CT_DIR_REPLY;
	else
		tuple->dst.dir = IP_CT_DIR_ORIGINAL;

	return 0;
}

static inline int
ctnetlink_parse_help(struct nlattr *attr, char **helper_name)
{
	struct nlattr *tb[CTA_HELP_MAX+1];

	nla_parse_nested(tb, CTA_HELP_MAX, attr, NULL);

	if (!tb[CTA_HELP_NAME])
		return -EINVAL;

	*helper_name = nla_data(tb[CTA_HELP_NAME]);

	return 0;
}

static const struct nla_policy ct_nla_policy[CTA_MAX+1] = {
	[CTA_STATUS] 		= { .type = NLA_U32 },
	[CTA_TIMEOUT] 		= { .type = NLA_U32 },
	[CTA_MARK]		= { .type = NLA_U32 },
	[CTA_USE]		= { .type = NLA_U32 },
	[CTA_ID]		= { .type = NLA_U32 },
};

static int
ctnetlink_del_conntrack(struct sock *ctnl, struct sk_buff *skb,
			struct nlmsghdr *nlh, struct nlattr *cda[])
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;
	struct nf_conn *ct;
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	int err = 0;

	if (cda[CTA_TUPLE_ORIG])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_ORIG, u3);
	else if (cda[CTA_TUPLE_REPLY])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_REPLY, u3);
	else {
		/* Flush the whole table */
		nf_conntrack_flush(&init_net, 
				   NETLINK_CB(skb).pid, 
				   nlmsg_report(nlh));
		return 0;
	}

	if (err < 0)
		return err;

	h = nf_conntrack_find_get(&init_net, &tuple);
	if (!h)
		return -ENOENT;

	ct = nf_ct_tuplehash_to_ctrack(h);

	if (cda[CTA_ID]) {
		u_int32_t id = ntohl(nla_get_be32(cda[CTA_ID]));
		if (id != (u32)(unsigned long)ct) {
			nf_ct_put(ct);
			return -ENOENT;
		}
	}

	nf_conntrack_event_report(IPCT_DESTROY,
				  ct,
				  NETLINK_CB(skb).pid,
				  nlmsg_report(nlh));

	/* death_by_timeout would report the event again */
	set_bit(IPS_DYING_BIT, &ct->status);

	nf_ct_kill(ct);
	nf_ct_put(ct);

	return 0;
}

static int
ctnetlink_get_conntrack(struct sock *ctnl, struct sk_buff *skb,
			struct nlmsghdr *nlh, struct nlattr *cda[])
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;
	struct nf_conn *ct;
	struct sk_buff *skb2 = NULL;
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	int err = 0;

	if (nlh->nlmsg_flags & NLM_F_DUMP)
		return netlink_dump_start(ctnl, skb, nlh, ctnetlink_dump_table,
					  ctnetlink_done);

	if (cda[CTA_TUPLE_ORIG])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_ORIG, u3);
	else if (cda[CTA_TUPLE_REPLY])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_REPLY, u3);
	else
		return -EINVAL;

	if (err < 0)
		return err;

	h = nf_conntrack_find_get(&init_net, &tuple);
	if (!h)
		return -ENOENT;

	ct = nf_ct_tuplehash_to_ctrack(h);

	err = -ENOMEM;
	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb2) {
		nf_ct_put(ct);
		return -ENOMEM;
	}

	rcu_read_lock();
	err = ctnetlink_fill_info(skb2, NETLINK_CB(skb).pid, nlh->nlmsg_seq,
				  IPCTNL_MSG_CT_NEW, 1, ct);
	rcu_read_unlock();
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

#ifdef CONFIG_NF_NAT_NEEDED
static int
ctnetlink_parse_nat_setup(struct nf_conn *ct,
			  enum nf_nat_manip_type manip,
			  struct nlattr *attr)
{
	typeof(nfnetlink_parse_nat_setup_hook) parse_nat_setup;

	parse_nat_setup = rcu_dereference(nfnetlink_parse_nat_setup_hook);
	if (!parse_nat_setup) {
#ifdef CONFIG_MODULES
		rcu_read_unlock();
		spin_unlock_bh(&nf_conntrack_lock);
		nfnl_unlock();
		if (request_module("nf-nat-ipv4") < 0) {
			nfnl_lock();
			spin_lock_bh(&nf_conntrack_lock);
			rcu_read_lock();
			return -EOPNOTSUPP;
		}
		nfnl_lock();
		spin_lock_bh(&nf_conntrack_lock);
		rcu_read_lock();
		if (nfnetlink_parse_nat_setup_hook)
			return -EAGAIN;
#endif
		return -EOPNOTSUPP;
	}

	return parse_nat_setup(ct, manip, attr);
}
#endif

static int
ctnetlink_change_status(struct nf_conn *ct, struct nlattr *cda[])
{
	unsigned long d;
	unsigned int status = ntohl(nla_get_be32(cda[CTA_STATUS]));
	d = ct->status ^ status;

	if (d & (IPS_EXPECTED|IPS_CONFIRMED|IPS_DYING))
		/* unchangeable */
		return -EBUSY;

	if (d & IPS_SEEN_REPLY && !(status & IPS_SEEN_REPLY))
		/* SEEN_REPLY bit can only be set */
		return -EBUSY;

	if (d & IPS_ASSURED && !(status & IPS_ASSURED))
		/* ASSURED bit can only be set */
		return -EBUSY;

	/* Be careful here, modifying NAT bits can screw up things,
	 * so don't let users modify them directly if they don't pass
	 * nf_nat_range. */
	ct->status |= status & ~(IPS_NAT_DONE_MASK | IPS_NAT_MASK);
	return 0;
}

static int
ctnetlink_change_nat(struct nf_conn *ct, struct nlattr *cda[])
{
#ifdef CONFIG_NF_NAT_NEEDED
	int ret;

	if (cda[CTA_NAT_DST]) {
		ret = ctnetlink_parse_nat_setup(ct,
						IP_NAT_MANIP_DST,
						cda[CTA_NAT_DST]);
		if (ret < 0)
			return ret;
	}
	if (cda[CTA_NAT_SRC]) {
		ret = ctnetlink_parse_nat_setup(ct,
						IP_NAT_MANIP_SRC,
						cda[CTA_NAT_SRC]);
		if (ret < 0)
			return ret;
	}
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static inline int
ctnetlink_change_helper(struct nf_conn *ct, struct nlattr *cda[])
{
	struct nf_conntrack_helper *helper;
	struct nf_conn_help *help = nfct_help(ct);
	char *helpname;
	int err;

	/* don't change helper of sibling connections */
	if (ct->master)
		return -EBUSY;

	err = ctnetlink_parse_help(cda[CTA_HELP], &helpname);
	if (err < 0)
		return err;

	if (!strcmp(helpname, "")) {
		if (help && help->helper) {
			/* we had a helper before ... */
			nf_ct_remove_expectations(ct);
			rcu_assign_pointer(help->helper, NULL);
		}

		return 0;
	}

	helper = __nf_conntrack_helper_find_byname(helpname);
	if (helper == NULL) {
#ifdef CONFIG_MODULES
		spin_unlock_bh(&nf_conntrack_lock);

		if (request_module("nfct-helper-%s", helpname) < 0) {
			spin_lock_bh(&nf_conntrack_lock);
			return -EOPNOTSUPP;
		}

		spin_lock_bh(&nf_conntrack_lock);
		helper = __nf_conntrack_helper_find_byname(helpname);
		if (helper)
			return -EAGAIN;
#endif
		return -EOPNOTSUPP;
	}

	if (help) {
		if (help->helper == helper)
			return 0;
		if (help->helper)
			return -EBUSY;
		/* need to zero data of old helper */
		memset(&help->help, 0, sizeof(help->help));
	} else {
		help = nf_ct_helper_ext_add(ct, GFP_ATOMIC);
		if (help == NULL)
			return -ENOMEM;
	}

	rcu_assign_pointer(help->helper, helper);

	return 0;
}

static inline int
ctnetlink_change_timeout(struct nf_conn *ct, struct nlattr *cda[])
{
	u_int32_t timeout = ntohl(nla_get_be32(cda[CTA_TIMEOUT]));

	if (!del_timer(&ct->timeout))
		return -ETIME;

	ct->timeout.expires = jiffies + timeout * HZ;
	add_timer(&ct->timeout);

	return 0;
}

static inline int
ctnetlink_change_protoinfo(struct nf_conn *ct, struct nlattr *cda[])
{
	struct nlattr *tb[CTA_PROTOINFO_MAX+1], *attr = cda[CTA_PROTOINFO];
	struct nf_conntrack_l4proto *l4proto;
	int err = 0;

	nla_parse_nested(tb, CTA_PROTOINFO_MAX, attr, NULL);

	l4proto = nf_ct_l4proto_find_get(nf_ct_l3num(ct), nf_ct_protonum(ct));
	if (l4proto->from_nlattr)
		err = l4proto->from_nlattr(tb, ct);
	nf_ct_l4proto_put(l4proto);

	return err;
}

#ifdef CONFIG_NF_NAT_NEEDED
static inline int
change_nat_seq_adj(struct nf_nat_seq *natseq, struct nlattr *attr)
{
	struct nlattr *cda[CTA_NAT_SEQ_MAX+1];

	nla_parse_nested(cda, CTA_NAT_SEQ_MAX, attr, NULL);

	if (!cda[CTA_NAT_SEQ_CORRECTION_POS])
		return -EINVAL;

	natseq->correction_pos =
		ntohl(nla_get_be32(cda[CTA_NAT_SEQ_CORRECTION_POS]));

	if (!cda[CTA_NAT_SEQ_OFFSET_BEFORE])
		return -EINVAL;

	natseq->offset_before =
		ntohl(nla_get_be32(cda[CTA_NAT_SEQ_OFFSET_BEFORE]));

	if (!cda[CTA_NAT_SEQ_OFFSET_AFTER])
		return -EINVAL;

	natseq->offset_after =
		ntohl(nla_get_be32(cda[CTA_NAT_SEQ_OFFSET_AFTER]));

	return 0;
}

static int
ctnetlink_change_nat_seq_adj(struct nf_conn *ct, struct nlattr *cda[])
{
	int ret = 0;
	struct nf_conn_nat *nat = nfct_nat(ct);

	if (!nat)
		return 0;

	if (cda[CTA_NAT_SEQ_ADJ_ORIG]) {
		ret = change_nat_seq_adj(&nat->seq[IP_CT_DIR_ORIGINAL],
					 cda[CTA_NAT_SEQ_ADJ_ORIG]);
		if (ret < 0)
			return ret;

		ct->status |= IPS_SEQ_ADJUST;
	}

	if (cda[CTA_NAT_SEQ_ADJ_REPLY]) {
		ret = change_nat_seq_adj(&nat->seq[IP_CT_DIR_REPLY],
					 cda[CTA_NAT_SEQ_ADJ_REPLY]);
		if (ret < 0)
			return ret;

		ct->status |= IPS_SEQ_ADJUST;
	}

	return 0;
}
#endif

static int
ctnetlink_change_conntrack(struct nf_conn *ct, struct nlattr *cda[])
{
	int err;

	if (cda[CTA_HELP]) {
		err = ctnetlink_change_helper(ct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_TIMEOUT]) {
		err = ctnetlink_change_timeout(ct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_STATUS]) {
		err = ctnetlink_change_status(ct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_PROTOINFO]) {
		err = ctnetlink_change_protoinfo(ct, cda);
		if (err < 0)
			return err;
	}

#if defined(CONFIG_NF_CONNTRACK_MARK)
	if (cda[CTA_MARK])
		ct->mark = ntohl(nla_get_be32(cda[CTA_MARK]));
#endif

#ifdef CONFIG_NF_NAT_NEEDED
	if (cda[CTA_NAT_SEQ_ADJ_ORIG] || cda[CTA_NAT_SEQ_ADJ_REPLY]) {
		err = ctnetlink_change_nat_seq_adj(ct, cda);
		if (err < 0)
			return err;
	}
#endif

	return 0;
}

static inline void
ctnetlink_event_report(struct nf_conn *ct, u32 pid, int report)
{
	unsigned int events = 0;

	if (test_bit(IPS_EXPECTED_BIT, &ct->status))
		events |= IPCT_RELATED;
	else
		events |= IPCT_NEW;

	nf_conntrack_event_report(IPCT_STATUS |
				  IPCT_HELPER |
				  IPCT_REFRESH |
				  IPCT_PROTOINFO |
				  IPCT_NATSEQADJ |
				  IPCT_MARK |
				  events,
				  ct,
				  pid,
				  report);
}

static int
ctnetlink_create_conntrack(struct nlattr *cda[],
			   struct nf_conntrack_tuple *otuple,
			   struct nf_conntrack_tuple *rtuple,
			   struct nf_conn *master_ct,
			   u32 pid,
			   int report)
{
	struct nf_conn *ct;
	int err = -EINVAL;
	struct nf_conntrack_helper *helper;

	ct = nf_conntrack_alloc(&init_net, otuple, rtuple, GFP_ATOMIC);
	if (IS_ERR(ct))
		return -ENOMEM;

	if (!cda[CTA_TIMEOUT])
		goto err;
	ct->timeout.expires = ntohl(nla_get_be32(cda[CTA_TIMEOUT]));

	ct->timeout.expires = jiffies + ct->timeout.expires * HZ;
	ct->status |= IPS_CONFIRMED;

	rcu_read_lock();
 	if (cda[CTA_HELP]) {
 		char *helpname;
 
 		err = ctnetlink_parse_help(cda[CTA_HELP], &helpname);
 		if (err < 0) {
			rcu_read_unlock();
			goto err;
		}

		helper = __nf_conntrack_helper_find_byname(helpname);
		if (helper == NULL) {
			rcu_read_unlock();
#ifdef CONFIG_MODULES
			if (request_module("nfct-helper-%s", helpname) < 0) {
				err = -EOPNOTSUPP;
				goto err;
			}

			rcu_read_lock();
			helper = __nf_conntrack_helper_find_byname(helpname);
			if (helper) {
				rcu_read_unlock();
				err = -EAGAIN;
				goto err;
			}
			rcu_read_unlock();
#endif
			err = -EOPNOTSUPP;
			goto err;
		} else {
			struct nf_conn_help *help;

			help = nf_ct_helper_ext_add(ct, GFP_ATOMIC);
			if (help == NULL) {
				rcu_read_unlock();
				err = -ENOMEM;
				goto err;
			}

			/* not in hash table yet so not strictly necessary */
			rcu_assign_pointer(help->helper, helper);
		}
	} else {
		/* try an implicit helper assignation */
		err = __nf_ct_try_assign_helper(ct, GFP_ATOMIC);
		if (err < 0) {
			rcu_read_unlock();
			goto err;
		}
	}

	if (cda[CTA_STATUS]) {
		err = ctnetlink_change_status(ct, cda);
		if (err < 0) {
			rcu_read_unlock();
			goto err;
		}
	}

	if (cda[CTA_NAT_SRC] || cda[CTA_NAT_DST]) {
		err = ctnetlink_change_nat(ct, cda);
		if (err < 0) {
			rcu_read_unlock();
			goto err;
		}
	}

#ifdef CONFIG_NF_NAT_NEEDED
	if (cda[CTA_NAT_SEQ_ADJ_ORIG] || cda[CTA_NAT_SEQ_ADJ_REPLY]) {
		err = ctnetlink_change_nat_seq_adj(ct, cda);
		if (err < 0) {
			rcu_read_unlock();
			goto err;
		}
	}
#endif

	if (cda[CTA_PROTOINFO]) {
		err = ctnetlink_change_protoinfo(ct, cda);
		if (err < 0) {
			rcu_read_unlock();
			goto err;
		}
	}

	nf_ct_acct_ext_add(ct, GFP_ATOMIC);

#if defined(CONFIG_NF_CONNTRACK_MARK)
	if (cda[CTA_MARK])
		ct->mark = ntohl(nla_get_be32(cda[CTA_MARK]));
#endif

	/* setup master conntrack: this is a confirmed expectation */
	if (master_ct) {
		__set_bit(IPS_EXPECTED_BIT, &ct->status);
		ct->master = master_ct;
	}

	nf_conntrack_get(&ct->ct_general);
	add_timer(&ct->timeout);
	nf_conntrack_hash_insert(ct);
	rcu_read_unlock();
	ctnetlink_event_report(ct, pid, report);
	nf_ct_put(ct);

	return 0;

err:
	nf_conntrack_free(ct);
	return err;
}

static int
ctnetlink_new_conntrack(struct sock *ctnl, struct sk_buff *skb,
			struct nlmsghdr *nlh, struct nlattr *cda[])
{
	struct nf_conntrack_tuple otuple, rtuple;
	struct nf_conntrack_tuple_hash *h = NULL;
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	int err = 0;

	if (cda[CTA_TUPLE_ORIG]) {
		err = ctnetlink_parse_tuple(cda, &otuple, CTA_TUPLE_ORIG, u3);
		if (err < 0)
			return err;
	}

	if (cda[CTA_TUPLE_REPLY]) {
		err = ctnetlink_parse_tuple(cda, &rtuple, CTA_TUPLE_REPLY, u3);
		if (err < 0)
			return err;
	}

	spin_lock_bh(&nf_conntrack_lock);
	if (cda[CTA_TUPLE_ORIG])
		h = __nf_conntrack_find(&init_net, &otuple);
	else if (cda[CTA_TUPLE_REPLY])
		h = __nf_conntrack_find(&init_net, &rtuple);

	if (h == NULL) {
		struct nf_conntrack_tuple master;
		struct nf_conntrack_tuple_hash *master_h = NULL;
		struct nf_conn *master_ct = NULL;

		if (cda[CTA_TUPLE_MASTER]) {
			err = ctnetlink_parse_tuple(cda,
						    &master,
						    CTA_TUPLE_MASTER,
						    u3);
			if (err < 0)
				goto out_unlock;

			master_h = __nf_conntrack_find(&init_net, &master);
			if (master_h == NULL) {
				err = -ENOENT;
				goto out_unlock;
			}
			master_ct = nf_ct_tuplehash_to_ctrack(master_h);
			nf_conntrack_get(&master_ct->ct_general);
		}

		err = -ENOENT;
		if (nlh->nlmsg_flags & NLM_F_CREATE)
			err = ctnetlink_create_conntrack(cda,
							 &otuple,
							 &rtuple,
							 master_ct,
							 NETLINK_CB(skb).pid,
							 nlmsg_report(nlh));
		spin_unlock_bh(&nf_conntrack_lock);
		if (err < 0 && master_ct)
			nf_ct_put(master_ct);

		return err;
	}
	/* implicit 'else' */

	/* We manipulate the conntrack inside the global conntrack table lock,
	 * so there's no need to increase the refcount */
	err = -EEXIST;
	if (!(nlh->nlmsg_flags & NLM_F_EXCL)) {
		struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);

		/* we only allow nat config for new conntracks */
		if (cda[CTA_NAT_SRC] || cda[CTA_NAT_DST]) {
			err = -EOPNOTSUPP;
			goto out_unlock;
		}
		/* can't link an existing conntrack to a master */
		if (cda[CTA_TUPLE_MASTER]) {
			err = -EOPNOTSUPP;
			goto out_unlock;
		}

		err = ctnetlink_change_conntrack(ct, cda);
		if (err == 0) {
			nf_conntrack_get(&ct->ct_general);
			spin_unlock_bh(&nf_conntrack_lock);
			ctnetlink_event_report(ct,
					       NETLINK_CB(skb).pid,
					       nlmsg_report(nlh));
			nf_ct_put(ct);
		} else
			spin_unlock_bh(&nf_conntrack_lock);

		return err;
	}

out_unlock:
	spin_unlock_bh(&nf_conntrack_lock);
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
	struct nlattr *nest_parms;

	nest_parms = nla_nest_start(skb, type | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, tuple) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	return 0;

nla_put_failure:
	return -1;
}

static inline int
ctnetlink_exp_dump_mask(struct sk_buff *skb,
			const struct nf_conntrack_tuple *tuple,
			const struct nf_conntrack_tuple_mask *mask)
{
	int ret;
	struct nf_conntrack_l3proto *l3proto;
	struct nf_conntrack_l4proto *l4proto;
	struct nf_conntrack_tuple m;
	struct nlattr *nest_parms;

	memset(&m, 0xFF, sizeof(m));
	m.src.u.all = mask->src.u.all;
	memcpy(&m.src.u3, &mask->src.u3, sizeof(m.src.u3));

	nest_parms = nla_nest_start(skb, CTA_EXPECT_MASK | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;

	l3proto = __nf_ct_l3proto_find(tuple->src.l3num);
	ret = ctnetlink_dump_tuples_ip(skb, &m, l3proto);

	if (unlikely(ret < 0))
		goto nla_put_failure;

	l4proto = __nf_ct_l4proto_find(tuple->src.l3num, tuple->dst.protonum);
	ret = ctnetlink_dump_tuples_proto(skb, &m, l4proto);
	if (unlikely(ret < 0))
		goto nla_put_failure;

	nla_nest_end(skb, nest_parms);

	return 0;

nla_put_failure:
	return -1;
}

static int
ctnetlink_exp_dump_expect(struct sk_buff *skb,
			  const struct nf_conntrack_expect *exp)
{
	struct nf_conn *master = exp->master;
	long timeout = (exp->timeout.expires - jiffies) / HZ;

	if (timeout < 0)
		timeout = 0;

	if (ctnetlink_exp_dump_tuple(skb, &exp->tuple, CTA_EXPECT_TUPLE) < 0)
		goto nla_put_failure;
	if (ctnetlink_exp_dump_mask(skb, &exp->tuple, &exp->mask) < 0)
		goto nla_put_failure;
	if (ctnetlink_exp_dump_tuple(skb,
				 &master->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
				 CTA_EXPECT_MASTER) < 0)
		goto nla_put_failure;

	NLA_PUT_BE32(skb, CTA_EXPECT_TIMEOUT, htonl(timeout));
	NLA_PUT_BE32(skb, CTA_EXPECT_ID, htonl((unsigned long)exp));

	return 0;

nla_put_failure:
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
		goto nla_put_failure;

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

nlmsg_failure:
nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

#ifdef CONFIG_NF_CONNTRACK_EVENTS
static int ctnetlink_expect_event(struct notifier_block *this,
				  unsigned long events, void *ptr)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct nf_exp_event *item = (struct nf_exp_event *)ptr;
	struct nf_conntrack_expect *exp = item->exp;
	struct sk_buff *skb;
	unsigned int type;
	sk_buff_data_t b;
	int flags = 0;

	if (events & IPEXP_NEW) {
		type = IPCTNL_MSG_EXP_NEW;
		flags = NLM_F_CREATE|NLM_F_EXCL;
	} else
		return NOTIFY_DONE;

	if (!item->report &&
	    !nfnetlink_has_listeners(NFNLGRP_CONNTRACK_EXP_NEW))
		return NOTIFY_DONE;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_ATOMIC);
	if (!skb)
		return NOTIFY_DONE;

	b = skb->tail;

	type |= NFNL_SUBSYS_CTNETLINK_EXP << 8;
	nlh   = NLMSG_PUT(skb, item->pid, 0, type, sizeof(struct nfgenmsg));
	nfmsg = NLMSG_DATA(nlh);

	nlh->nlmsg_flags    = flags;
	nfmsg->nfgen_family = exp->tuple.src.l3num;
	nfmsg->version	    = NFNETLINK_V0;
	nfmsg->res_id	    = 0;

	rcu_read_lock();
	if (ctnetlink_exp_dump_expect(skb, exp) < 0)
		goto nla_put_failure;
	rcu_read_unlock();

	nlh->nlmsg_len = skb->tail - b;
	nfnetlink_send(skb, item->pid, NFNLGRP_CONNTRACK_EXP_NEW, item->report);
	return NOTIFY_DONE;

nla_put_failure:
	rcu_read_unlock();
nlmsg_failure:
	kfree_skb(skb);
	return NOTIFY_DONE;
}
#endif
static int ctnetlink_exp_done(struct netlink_callback *cb)
{
	if (cb->args[1])
		nf_ct_expect_put((struct nf_conntrack_expect *)cb->args[1]);
	return 0;
}

static int
ctnetlink_exp_dump_table(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = &init_net;
	struct nf_conntrack_expect *exp, *last;
	struct nfgenmsg *nfmsg = NLMSG_DATA(cb->nlh);
	struct hlist_node *n;
	u_int8_t l3proto = nfmsg->nfgen_family;

	rcu_read_lock();
	last = (struct nf_conntrack_expect *)cb->args[1];
	for (; cb->args[0] < nf_ct_expect_hsize; cb->args[0]++) {
restart:
		hlist_for_each_entry(exp, n, &net->ct.expect_hash[cb->args[0]],
				     hnode) {
			if (l3proto && exp->tuple.src.l3num != l3proto)
				continue;
			if (cb->args[1]) {
				if (exp != last)
					continue;
				cb->args[1] = 0;
			}
			if (ctnetlink_exp_fill_info(skb, NETLINK_CB(cb->skb).pid,
						    cb->nlh->nlmsg_seq,
						    IPCTNL_MSG_EXP_NEW,
						    1, exp) < 0) {
				if (!atomic_inc_not_zero(&exp->use))
					continue;
				cb->args[1] = (unsigned long)exp;
				goto out;
			}
		}
		if (cb->args[1]) {
			cb->args[1] = 0;
			goto restart;
		}
	}
out:
	rcu_read_unlock();
	if (last)
		nf_ct_expect_put(last);

	return skb->len;
}

static const struct nla_policy exp_nla_policy[CTA_EXPECT_MAX+1] = {
	[CTA_EXPECT_TIMEOUT]	= { .type = NLA_U32 },
	[CTA_EXPECT_ID]		= { .type = NLA_U32 },
};

static int
ctnetlink_get_expect(struct sock *ctnl, struct sk_buff *skb,
		     struct nlmsghdr *nlh, struct nlattr *cda[])
{
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_expect *exp;
	struct sk_buff *skb2;
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	int err = 0;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		return netlink_dump_start(ctnl, skb, nlh,
					  ctnetlink_exp_dump_table,
					  ctnetlink_exp_done);
	}

	if (cda[CTA_EXPECT_MASTER])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_MASTER, u3);
	else
		return -EINVAL;

	if (err < 0)
		return err;

	exp = nf_ct_expect_find_get(&init_net, &tuple);
	if (!exp)
		return -ENOENT;

	if (cda[CTA_EXPECT_ID]) {
		__be32 id = nla_get_be32(cda[CTA_EXPECT_ID]);
		if (ntohl(id) != (u32)(unsigned long)exp) {
			nf_ct_expect_put(exp);
			return -ENOENT;
		}
	}

	err = -ENOMEM;
	skb2 = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb2)
		goto out;

	rcu_read_lock();
	err = ctnetlink_exp_fill_info(skb2, NETLINK_CB(skb).pid,
				      nlh->nlmsg_seq, IPCTNL_MSG_EXP_NEW,
				      1, exp);
	rcu_read_unlock();
	if (err <= 0)
		goto free;

	nf_ct_expect_put(exp);

	return netlink_unicast(ctnl, skb2, NETLINK_CB(skb).pid, MSG_DONTWAIT);

free:
	kfree_skb(skb2);
out:
	nf_ct_expect_put(exp);
	return err;
}

static int
ctnetlink_del_expect(struct sock *ctnl, struct sk_buff *skb,
		     struct nlmsghdr *nlh, struct nlattr *cda[])
{
	struct nf_conntrack_expect *exp;
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_helper *h;
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	struct hlist_node *n, *next;
	u_int8_t u3 = nfmsg->nfgen_family;
	unsigned int i;
	int err;

	if (cda[CTA_EXPECT_TUPLE]) {
		/* delete a single expect by tuple */
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_TUPLE, u3);
		if (err < 0)
			return err;

		/* bump usage count to 2 */
		exp = nf_ct_expect_find_get(&init_net, &tuple);
		if (!exp)
			return -ENOENT;

		if (cda[CTA_EXPECT_ID]) {
			__be32 id = nla_get_be32(cda[CTA_EXPECT_ID]);
			if (ntohl(id) != (u32)(unsigned long)exp) {
				nf_ct_expect_put(exp);
				return -ENOENT;
			}
		}

		/* after list removal, usage count == 1 */
		nf_ct_unexpect_related(exp);
		/* have to put what we 'get' above.
		 * after this line usage count == 0 */
		nf_ct_expect_put(exp);
	} else if (cda[CTA_EXPECT_HELP_NAME]) {
		char *name = nla_data(cda[CTA_EXPECT_HELP_NAME]);
		struct nf_conn_help *m_help;

		/* delete all expectations for this helper */
		spin_lock_bh(&nf_conntrack_lock);
		h = __nf_conntrack_helper_find_byname(name);
		if (!h) {
			spin_unlock_bh(&nf_conntrack_lock);
			return -EOPNOTSUPP;
		}
		for (i = 0; i < nf_ct_expect_hsize; i++) {
			hlist_for_each_entry_safe(exp, n, next,
						  &init_net.ct.expect_hash[i],
						  hnode) {
				m_help = nfct_help(exp->master);
				if (m_help->helper == h
				    && del_timer(&exp->timeout)) {
					nf_ct_unlink_expect(exp);
					nf_ct_expect_put(exp);
				}
			}
		}
		spin_unlock_bh(&nf_conntrack_lock);
	} else {
		/* This basically means we have to flush everything*/
		spin_lock_bh(&nf_conntrack_lock);
		for (i = 0; i < nf_ct_expect_hsize; i++) {
			hlist_for_each_entry_safe(exp, n, next,
						  &init_net.ct.expect_hash[i],
						  hnode) {
				if (del_timer(&exp->timeout)) {
					nf_ct_unlink_expect(exp);
					nf_ct_expect_put(exp);
				}
			}
		}
		spin_unlock_bh(&nf_conntrack_lock);
	}

	return 0;
}
static int
ctnetlink_change_expect(struct nf_conntrack_expect *x, struct nlattr *cda[])
{
	return -EOPNOTSUPP;
}

static int
ctnetlink_create_expect(struct nlattr *cda[], u_int8_t u3, u32 pid, int report)
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
	h = nf_conntrack_find_get(&init_net, &master_tuple);
	if (!h)
		return -ENOENT;
	ct = nf_ct_tuplehash_to_ctrack(h);
	help = nfct_help(ct);

	if (!help || !help->helper) {
		/* such conntrack hasn't got any helper, abort */
		err = -EOPNOTSUPP;
		goto out;
	}

	exp = nf_ct_expect_alloc(ct);
	if (!exp) {
		err = -ENOMEM;
		goto out;
	}

	exp->class = 0;
	exp->expectfn = NULL;
	exp->flags = 0;
	exp->master = ct;
	exp->helper = NULL;
	memcpy(&exp->tuple, &tuple, sizeof(struct nf_conntrack_tuple));
	memcpy(&exp->mask.src.u3, &mask.src.u3, sizeof(exp->mask.src.u3));
	exp->mask.src.u.all = mask.src.u.all;

	err = nf_ct_expect_related_report(exp, pid, report);
	nf_ct_expect_put(exp);

out:
	nf_ct_put(nf_ct_tuplehash_to_ctrack(h));
	return err;
}

static int
ctnetlink_new_expect(struct sock *ctnl, struct sk_buff *skb,
		     struct nlmsghdr *nlh, struct nlattr *cda[])
{
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_expect *exp;
	struct nfgenmsg *nfmsg = NLMSG_DATA(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	int err = 0;

	if (!cda[CTA_EXPECT_TUPLE]
	    || !cda[CTA_EXPECT_MASK]
	    || !cda[CTA_EXPECT_MASTER])
		return -EINVAL;

	err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_TUPLE, u3);
	if (err < 0)
		return err;

	spin_lock_bh(&nf_conntrack_lock);
	exp = __nf_ct_expect_find(&init_net, &tuple);

	if (!exp) {
		spin_unlock_bh(&nf_conntrack_lock);
		err = -ENOENT;
		if (nlh->nlmsg_flags & NLM_F_CREATE) {
			err = ctnetlink_create_expect(cda,
						      u3,
						      NETLINK_CB(skb).pid,
						      nlmsg_report(nlh));
		}
		return err;
	}

	err = -EEXIST;
	if (!(nlh->nlmsg_flags & NLM_F_EXCL))
		err = ctnetlink_change_expect(exp, cda);
	spin_unlock_bh(&nf_conntrack_lock);

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

static const struct nfnl_callback ctnl_cb[IPCTNL_MSG_MAX] = {
	[IPCTNL_MSG_CT_NEW]		= { .call = ctnetlink_new_conntrack,
					    .attr_count = CTA_MAX,
					    .policy = ct_nla_policy },
	[IPCTNL_MSG_CT_GET] 		= { .call = ctnetlink_get_conntrack,
					    .attr_count = CTA_MAX,
					    .policy = ct_nla_policy },
	[IPCTNL_MSG_CT_DELETE]  	= { .call = ctnetlink_del_conntrack,
					    .attr_count = CTA_MAX,
					    .policy = ct_nla_policy },
	[IPCTNL_MSG_CT_GET_CTRZERO] 	= { .call = ctnetlink_get_conntrack,
					    .attr_count = CTA_MAX,
					    .policy = ct_nla_policy },
};

static const struct nfnl_callback ctnl_exp_cb[IPCTNL_MSG_EXP_MAX] = {
	[IPCTNL_MSG_EXP_GET]		= { .call = ctnetlink_get_expect,
					    .attr_count = CTA_EXPECT_MAX,
					    .policy = exp_nla_policy },
	[IPCTNL_MSG_EXP_NEW]		= { .call = ctnetlink_new_expect,
					    .attr_count = CTA_EXPECT_MAX,
					    .policy = exp_nla_policy },
	[IPCTNL_MSG_EXP_DELETE]		= { .call = ctnetlink_del_expect,
					    .attr_count = CTA_EXPECT_MAX,
					    .policy = exp_nla_policy },
};

static const struct nfnetlink_subsystem ctnl_subsys = {
	.name				= "conntrack",
	.subsys_id			= NFNL_SUBSYS_CTNETLINK,
	.cb_count			= IPCTNL_MSG_MAX,
	.cb				= ctnl_cb,
};

static const struct nfnetlink_subsystem ctnl_exp_subsys = {
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

	ret = nf_ct_expect_register_notifier(&ctnl_notifier_exp);
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
	nf_ct_expect_unregister_notifier(&ctnl_notifier_exp);
	nf_conntrack_unregister_notifier(&ctnl_notifier);
#endif

	nfnetlink_subsys_unregister(&ctnl_exp_subsys);
	nfnetlink_subsys_unregister(&ctnl_subsys);
	return;
}

module_init(ctnetlink_init);
module_exit(ctnetlink_exit);
