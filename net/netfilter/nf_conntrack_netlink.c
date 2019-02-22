/* Connection tracking via netlink socket. Allows for user space
 * protocol helpers and general trouble making from userspace.
 *
 * (C) 2001 by Jay Schulist <jschlst@samba.org>
 * (C) 2002-2006 by Harald Welte <laforge@gnumonks.org>
 * (C) 2003 by Patrick Mchardy <kaber@trash.net>
 * (C) 2005-2012 by Pablo Neira Ayuso <pablo@netfilter.org>
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
#include <linux/rculist_nulls.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/security.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/netlink.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include <linux/netfilter.h>
#include <net/netlink.h>
#include <net/sock.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_acct.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_timestamp.h>
#include <net/netfilter/nf_conntrack_labels.h>
#include <net/netfilter/nf_conntrack_synproxy.h>
#ifdef CONFIG_NF_NAT_NEEDED
#include <net/netfilter/nf_nat_core.h>
#include <net/netfilter/nf_nat_helper.h>
#endif

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

MODULE_LICENSE("GPL");

static int ctnetlink_dump_tuples_proto(struct sk_buff *skb,
				const struct nf_conntrack_tuple *tuple,
				const struct nf_conntrack_l4proto *l4proto)
{
	int ret = 0;
	struct nlattr *nest_parms;

	nest_parms = nla_nest_start(skb, CTA_TUPLE_PROTO | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (nla_put_u8(skb, CTA_PROTO_NUM, tuple->dst.protonum))
		goto nla_put_failure;

	if (likely(l4proto->tuple_to_nlattr))
		ret = l4proto->tuple_to_nlattr(skb, tuple);

	nla_nest_end(skb, nest_parms);

	return ret;

nla_put_failure:
	return -1;
}

static int ipv4_tuple_to_nlattr(struct sk_buff *skb,
				const struct nf_conntrack_tuple *tuple)
{
	if (nla_put_in_addr(skb, CTA_IP_V4_SRC, tuple->src.u3.ip) ||
	    nla_put_in_addr(skb, CTA_IP_V4_DST, tuple->dst.u3.ip))
		return -EMSGSIZE;
	return 0;
}

static int ipv6_tuple_to_nlattr(struct sk_buff *skb,
				const struct nf_conntrack_tuple *tuple)
{
	if (nla_put_in6_addr(skb, CTA_IP_V6_SRC, &tuple->src.u3.in6) ||
	    nla_put_in6_addr(skb, CTA_IP_V6_DST, &tuple->dst.u3.in6))
		return -EMSGSIZE;
	return 0;
}

static int ctnetlink_dump_tuples_ip(struct sk_buff *skb,
				    const struct nf_conntrack_tuple *tuple)
{
	int ret = 0;
	struct nlattr *nest_parms;

	nest_parms = nla_nest_start(skb, CTA_TUPLE_IP | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;

	switch (tuple->src.l3num) {
	case NFPROTO_IPV4:
		ret = ipv4_tuple_to_nlattr(skb, tuple);
		break;
	case NFPROTO_IPV6:
		ret = ipv6_tuple_to_nlattr(skb, tuple);
		break;
	}

	nla_nest_end(skb, nest_parms);

	return ret;

nla_put_failure:
	return -1;
}

static int ctnetlink_dump_tuples(struct sk_buff *skb,
				 const struct nf_conntrack_tuple *tuple)
{
	const struct nf_conntrack_l4proto *l4proto;
	int ret;

	rcu_read_lock();
	ret = ctnetlink_dump_tuples_ip(skb, tuple);

	if (ret >= 0) {
		l4proto = nf_ct_l4proto_find(tuple->dst.protonum);
		ret = ctnetlink_dump_tuples_proto(skb, tuple, l4proto);
	}
	rcu_read_unlock();
	return ret;
}

static int ctnetlink_dump_zone_id(struct sk_buff *skb, int attrtype,
				  const struct nf_conntrack_zone *zone, int dir)
{
	if (zone->id == NF_CT_DEFAULT_ZONE_ID || zone->dir != dir)
		return 0;
	if (nla_put_be16(skb, attrtype, htons(zone->id)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static int ctnetlink_dump_status(struct sk_buff *skb, const struct nf_conn *ct)
{
	if (nla_put_be32(skb, CTA_STATUS, htonl(ct->status)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static int ctnetlink_dump_timeout(struct sk_buff *skb, const struct nf_conn *ct)
{
	long timeout = nf_ct_expires(ct) / HZ;

	if (nla_put_be32(skb, CTA_TIMEOUT, htonl(timeout)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static int ctnetlink_dump_protoinfo(struct sk_buff *skb, struct nf_conn *ct)
{
	const struct nf_conntrack_l4proto *l4proto;
	struct nlattr *nest_proto;
	int ret;

	l4proto = nf_ct_l4proto_find(nf_ct_protonum(ct));
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

static int ctnetlink_dump_helpinfo(struct sk_buff *skb,
				   const struct nf_conn *ct)
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
	if (nla_put_string(skb, CTA_HELP_NAME, helper->name))
		goto nla_put_failure;

	if (helper->to_nlattr)
		helper->to_nlattr(skb, ct);

	nla_nest_end(skb, nest_helper);
out:
	return 0;

nla_put_failure:
	return -1;
}

static int
dump_counters(struct sk_buff *skb, struct nf_conn_acct *acct,
	      enum ip_conntrack_dir dir, int type)
{
	enum ctattr_type attr = dir ? CTA_COUNTERS_REPLY: CTA_COUNTERS_ORIG;
	struct nf_conn_counter *counter = acct->counter;
	struct nlattr *nest_count;
	u64 pkts, bytes;

	if (type == IPCTNL_MSG_CT_GET_CTRZERO) {
		pkts = atomic64_xchg(&counter[dir].packets, 0);
		bytes = atomic64_xchg(&counter[dir].bytes, 0);
	} else {
		pkts = atomic64_read(&counter[dir].packets);
		bytes = atomic64_read(&counter[dir].bytes);
	}

	nest_count = nla_nest_start(skb, attr | NLA_F_NESTED);
	if (!nest_count)
		goto nla_put_failure;

	if (nla_put_be64(skb, CTA_COUNTERS_PACKETS, cpu_to_be64(pkts),
			 CTA_COUNTERS_PAD) ||
	    nla_put_be64(skb, CTA_COUNTERS_BYTES, cpu_to_be64(bytes),
			 CTA_COUNTERS_PAD))
		goto nla_put_failure;

	nla_nest_end(skb, nest_count);

	return 0;

nla_put_failure:
	return -1;
}

static int
ctnetlink_dump_acct(struct sk_buff *skb, const struct nf_conn *ct, int type)
{
	struct nf_conn_acct *acct = nf_conn_acct_find(ct);

	if (!acct)
		return 0;

	if (dump_counters(skb, acct, IP_CT_DIR_ORIGINAL, type) < 0)
		return -1;
	if (dump_counters(skb, acct, IP_CT_DIR_REPLY, type) < 0)
		return -1;

	return 0;
}

static int
ctnetlink_dump_timestamp(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nlattr *nest_count;
	const struct nf_conn_tstamp *tstamp;

	tstamp = nf_conn_tstamp_find(ct);
	if (!tstamp)
		return 0;

	nest_count = nla_nest_start(skb, CTA_TIMESTAMP | NLA_F_NESTED);
	if (!nest_count)
		goto nla_put_failure;

	if (nla_put_be64(skb, CTA_TIMESTAMP_START, cpu_to_be64(tstamp->start),
			 CTA_TIMESTAMP_PAD) ||
	    (tstamp->stop != 0 && nla_put_be64(skb, CTA_TIMESTAMP_STOP,
					       cpu_to_be64(tstamp->stop),
					       CTA_TIMESTAMP_PAD)))
		goto nla_put_failure;
	nla_nest_end(skb, nest_count);

	return 0;

nla_put_failure:
	return -1;
}

#ifdef CONFIG_NF_CONNTRACK_MARK
static int ctnetlink_dump_mark(struct sk_buff *skb, const struct nf_conn *ct)
{
	if (nla_put_be32(skb, CTA_MARK, htonl(ct->mark)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}
#else
#define ctnetlink_dump_mark(a, b) (0)
#endif

#ifdef CONFIG_NF_CONNTRACK_SECMARK
static int ctnetlink_dump_secctx(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nlattr *nest_secctx;
	int len, ret;
	char *secctx;

	ret = security_secid_to_secctx(ct->secmark, &secctx, &len);
	if (ret)
		return 0;

	ret = -1;
	nest_secctx = nla_nest_start(skb, CTA_SECCTX | NLA_F_NESTED);
	if (!nest_secctx)
		goto nla_put_failure;

	if (nla_put_string(skb, CTA_SECCTX_NAME, secctx))
		goto nla_put_failure;
	nla_nest_end(skb, nest_secctx);

	ret = 0;
nla_put_failure:
	security_release_secctx(secctx, len);
	return ret;
}
#else
#define ctnetlink_dump_secctx(a, b) (0)
#endif

#ifdef CONFIG_NF_CONNTRACK_LABELS
static inline int ctnetlink_label_size(const struct nf_conn *ct)
{
	struct nf_conn_labels *labels = nf_ct_labels_find(ct);

	if (!labels)
		return 0;
	return nla_total_size(sizeof(labels->bits));
}

static int
ctnetlink_dump_labels(struct sk_buff *skb, const struct nf_conn *ct)
{
	struct nf_conn_labels *labels = nf_ct_labels_find(ct);
	unsigned int i;

	if (!labels)
		return 0;

	i = 0;
	do {
		if (labels->bits[i] != 0)
			return nla_put(skb, CTA_LABELS, sizeof(labels->bits),
				       labels->bits);
		i++;
	} while (i < ARRAY_SIZE(labels->bits));

	return 0;
}
#else
#define ctnetlink_dump_labels(a, b) (0)
#define ctnetlink_label_size(a)	(0)
#endif

#define master_tuple(ct) &(ct->master->tuplehash[IP_CT_DIR_ORIGINAL].tuple)

static int ctnetlink_dump_master(struct sk_buff *skb, const struct nf_conn *ct)
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

static int
dump_ct_seq_adj(struct sk_buff *skb, const struct nf_ct_seqadj *seq, int type)
{
	struct nlattr *nest_parms;

	nest_parms = nla_nest_start(skb, type | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;

	if (nla_put_be32(skb, CTA_SEQADJ_CORRECTION_POS,
			 htonl(seq->correction_pos)) ||
	    nla_put_be32(skb, CTA_SEQADJ_OFFSET_BEFORE,
			 htonl(seq->offset_before)) ||
	    nla_put_be32(skb, CTA_SEQADJ_OFFSET_AFTER,
			 htonl(seq->offset_after)))
		goto nla_put_failure;

	nla_nest_end(skb, nest_parms);

	return 0;

nla_put_failure:
	return -1;
}

static int ctnetlink_dump_ct_seq_adj(struct sk_buff *skb, struct nf_conn *ct)
{
	struct nf_conn_seqadj *seqadj = nfct_seqadj(ct);
	struct nf_ct_seqadj *seq;

	if (!(ct->status & IPS_SEQ_ADJUST) || !seqadj)
		return 0;

	spin_lock_bh(&ct->lock);
	seq = &seqadj->seq[IP_CT_DIR_ORIGINAL];
	if (dump_ct_seq_adj(skb, seq, CTA_SEQ_ADJ_ORIG) == -1)
		goto err;

	seq = &seqadj->seq[IP_CT_DIR_REPLY];
	if (dump_ct_seq_adj(skb, seq, CTA_SEQ_ADJ_REPLY) == -1)
		goto err;

	spin_unlock_bh(&ct->lock);
	return 0;
err:
	spin_unlock_bh(&ct->lock);
	return -1;
}

static int ctnetlink_dump_ct_synproxy(struct sk_buff *skb, struct nf_conn *ct)
{
	struct nf_conn_synproxy *synproxy = nfct_synproxy(ct);
	struct nlattr *nest_parms;

	if (!synproxy)
		return 0;

	nest_parms = nla_nest_start(skb, CTA_SYNPROXY | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;

	if (nla_put_be32(skb, CTA_SYNPROXY_ISN, htonl(synproxy->isn)) ||
	    nla_put_be32(skb, CTA_SYNPROXY_ITS, htonl(synproxy->its)) ||
	    nla_put_be32(skb, CTA_SYNPROXY_TSOFF, htonl(synproxy->tsoff)))
		goto nla_put_failure;

	nla_nest_end(skb, nest_parms);

	return 0;

nla_put_failure:
	return -1;
}

static int ctnetlink_dump_id(struct sk_buff *skb, const struct nf_conn *ct)
{
	if (nla_put_be32(skb, CTA_ID, htonl((unsigned long)ct)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static int ctnetlink_dump_use(struct sk_buff *skb, const struct nf_conn *ct)
{
	if (nla_put_be32(skb, CTA_USE, htonl(atomic_read(&ct->ct_general.use))))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static int
ctnetlink_fill_info(struct sk_buff *skb, u32 portid, u32 seq, u32 type,
		    struct nf_conn *ct)
{
	const struct nf_conntrack_zone *zone;
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct nlattr *nest_parms;
	unsigned int flags = portid ? NLM_F_MULTI : 0, event;

	event = nfnl_msg_type(NFNL_SUBSYS_CTNETLINK, IPCTNL_MSG_CT_NEW);
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = nf_ct_l3num(ct);
	nfmsg->version      = NFNETLINK_V0;
	nfmsg->res_id	    = 0;

	zone = nf_ct_zone(ct);

	nest_parms = nla_nest_start(skb, CTA_TUPLE_ORIG | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, nf_ct_tuple(ct, IP_CT_DIR_ORIGINAL)) < 0)
		goto nla_put_failure;
	if (ctnetlink_dump_zone_id(skb, CTA_TUPLE_ZONE, zone,
				   NF_CT_ZONE_DIR_ORIG) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	nest_parms = nla_nest_start(skb, CTA_TUPLE_REPLY | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, nf_ct_tuple(ct, IP_CT_DIR_REPLY)) < 0)
		goto nla_put_failure;
	if (ctnetlink_dump_zone_id(skb, CTA_TUPLE_ZONE, zone,
				   NF_CT_ZONE_DIR_REPL) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	if (ctnetlink_dump_zone_id(skb, CTA_ZONE, zone,
				   NF_CT_DEFAULT_ZONE_DIR) < 0)
		goto nla_put_failure;

	if (ctnetlink_dump_status(skb, ct) < 0 ||
	    ctnetlink_dump_timeout(skb, ct) < 0 ||
	    ctnetlink_dump_acct(skb, ct, type) < 0 ||
	    ctnetlink_dump_timestamp(skb, ct) < 0 ||
	    ctnetlink_dump_protoinfo(skb, ct) < 0 ||
	    ctnetlink_dump_helpinfo(skb, ct) < 0 ||
	    ctnetlink_dump_mark(skb, ct) < 0 ||
	    ctnetlink_dump_secctx(skb, ct) < 0 ||
	    ctnetlink_dump_labels(skb, ct) < 0 ||
	    ctnetlink_dump_id(skb, ct) < 0 ||
	    ctnetlink_dump_use(skb, ct) < 0 ||
	    ctnetlink_dump_master(skb, ct) < 0 ||
	    ctnetlink_dump_ct_seq_adj(skb, ct) < 0 ||
	    ctnetlink_dump_ct_synproxy(skb, ct) < 0)
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return skb->len;

nlmsg_failure:
nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -1;
}

static const struct nla_policy cta_ip_nla_policy[CTA_IP_MAX + 1] = {
	[CTA_IP_V4_SRC]	= { .type = NLA_U32 },
	[CTA_IP_V4_DST]	= { .type = NLA_U32 },
	[CTA_IP_V6_SRC]	= { .len = sizeof(__be32) * 4 },
	[CTA_IP_V6_DST]	= { .len = sizeof(__be32) * 4 },
};

#if defined(CONFIG_NETFILTER_NETLINK_GLUE_CT) || defined(CONFIG_NF_CONNTRACK_EVENTS)
static size_t ctnetlink_proto_size(const struct nf_conn *ct)
{
	const struct nf_conntrack_l4proto *l4proto;
	size_t len, len4 = 0;

	len = nla_policy_len(cta_ip_nla_policy, CTA_IP_MAX + 1);
	len *= 3u; /* ORIG, REPLY, MASTER */

	l4proto = nf_ct_l4proto_find(nf_ct_protonum(ct));
	len += l4proto->nlattr_size;
	if (l4proto->nlattr_tuple_size) {
		len4 = l4proto->nlattr_tuple_size();
		len4 *= 3u; /* ORIG, REPLY, MASTER */
	}

	return len + len4;
}
#endif

static inline size_t ctnetlink_acct_size(const struct nf_conn *ct)
{
	if (!nf_ct_ext_exist(ct, NF_CT_EXT_ACCT))
		return 0;
	return 2 * nla_total_size(0) /* CTA_COUNTERS_ORIG|REPL */
	       + 2 * nla_total_size_64bit(sizeof(uint64_t)) /* CTA_COUNTERS_PACKETS */
	       + 2 * nla_total_size_64bit(sizeof(uint64_t)) /* CTA_COUNTERS_BYTES */
	       ;
}

static inline int ctnetlink_secctx_size(const struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_SECMARK
	int len, ret;

	ret = security_secid_to_secctx(ct->secmark, NULL, &len);
	if (ret)
		return 0;

	return nla_total_size(0) /* CTA_SECCTX */
	       + nla_total_size(sizeof(char) * len); /* CTA_SECCTX_NAME */
#else
	return 0;
#endif
}

static inline size_t ctnetlink_timestamp_size(const struct nf_conn *ct)
{
#ifdef CONFIG_NF_CONNTRACK_TIMESTAMP
	if (!nf_ct_ext_exist(ct, NF_CT_EXT_TSTAMP))
		return 0;
	return nla_total_size(0) + 2 * nla_total_size_64bit(sizeof(uint64_t));
#else
	return 0;
#endif
}

#ifdef CONFIG_NF_CONNTRACK_EVENTS
static size_t ctnetlink_nlmsg_size(const struct nf_conn *ct)
{
	return NLMSG_ALIGN(sizeof(struct nfgenmsg))
	       + 3 * nla_total_size(0) /* CTA_TUPLE_ORIG|REPL|MASTER */
	       + 3 * nla_total_size(0) /* CTA_TUPLE_IP */
	       + 3 * nla_total_size(0) /* CTA_TUPLE_PROTO */
	       + 3 * nla_total_size(sizeof(u_int8_t)) /* CTA_PROTO_NUM */
	       + nla_total_size(sizeof(u_int32_t)) /* CTA_ID */
	       + nla_total_size(sizeof(u_int32_t)) /* CTA_STATUS */
	       + ctnetlink_acct_size(ct)
	       + ctnetlink_timestamp_size(ct)
	       + nla_total_size(sizeof(u_int32_t)) /* CTA_TIMEOUT */
	       + nla_total_size(0) /* CTA_PROTOINFO */
	       + nla_total_size(0) /* CTA_HELP */
	       + nla_total_size(NF_CT_HELPER_NAME_LEN) /* CTA_HELP_NAME */
	       + ctnetlink_secctx_size(ct)
#ifdef CONFIG_NF_NAT_NEEDED
	       + 2 * nla_total_size(0) /* CTA_NAT_SEQ_ADJ_ORIG|REPL */
	       + 6 * nla_total_size(sizeof(u_int32_t)) /* CTA_NAT_SEQ_OFFSET */
#endif
#ifdef CONFIG_NF_CONNTRACK_MARK
	       + nla_total_size(sizeof(u_int32_t)) /* CTA_MARK */
#endif
#ifdef CONFIG_NF_CONNTRACK_ZONES
	       + nla_total_size(sizeof(u_int16_t)) /* CTA_ZONE|CTA_TUPLE_ZONE */
#endif
	       + ctnetlink_proto_size(ct)
	       + ctnetlink_label_size(ct)
	       ;
}

static int
ctnetlink_conntrack_event(unsigned int events, struct nf_ct_event *item)
{
	const struct nf_conntrack_zone *zone;
	struct net *net;
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct nlattr *nest_parms;
	struct nf_conn *ct = item->ct;
	struct sk_buff *skb;
	unsigned int type;
	unsigned int flags = 0, group;
	int err;

	if (events & (1 << IPCT_DESTROY)) {
		type = IPCTNL_MSG_CT_DELETE;
		group = NFNLGRP_CONNTRACK_DESTROY;
	} else if (events & ((1 << IPCT_NEW) | (1 << IPCT_RELATED))) {
		type = IPCTNL_MSG_CT_NEW;
		flags = NLM_F_CREATE|NLM_F_EXCL;
		group = NFNLGRP_CONNTRACK_NEW;
	} else if (events) {
		type = IPCTNL_MSG_CT_NEW;
		group = NFNLGRP_CONNTRACK_UPDATE;
	} else
		return 0;

	net = nf_ct_net(ct);
	if (!item->report && !nfnetlink_has_listeners(net, group))
		return 0;

	skb = nlmsg_new(ctnetlink_nlmsg_size(ct), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	type = nfnl_msg_type(NFNL_SUBSYS_CTNETLINK, type);
	nlh = nlmsg_put(skb, item->portid, 0, type, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = nf_ct_l3num(ct);
	nfmsg->version	= NFNETLINK_V0;
	nfmsg->res_id	= 0;

	zone = nf_ct_zone(ct);

	nest_parms = nla_nest_start(skb, CTA_TUPLE_ORIG | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, nf_ct_tuple(ct, IP_CT_DIR_ORIGINAL)) < 0)
		goto nla_put_failure;
	if (ctnetlink_dump_zone_id(skb, CTA_TUPLE_ZONE, zone,
				   NF_CT_ZONE_DIR_ORIG) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	nest_parms = nla_nest_start(skb, CTA_TUPLE_REPLY | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, nf_ct_tuple(ct, IP_CT_DIR_REPLY)) < 0)
		goto nla_put_failure;
	if (ctnetlink_dump_zone_id(skb, CTA_TUPLE_ZONE, zone,
				   NF_CT_ZONE_DIR_REPL) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	if (ctnetlink_dump_zone_id(skb, CTA_ZONE, zone,
				   NF_CT_DEFAULT_ZONE_DIR) < 0)
		goto nla_put_failure;

	if (ctnetlink_dump_id(skb, ct) < 0)
		goto nla_put_failure;

	if (ctnetlink_dump_status(skb, ct) < 0)
		goto nla_put_failure;

	if (events & (1 << IPCT_DESTROY)) {
		if (ctnetlink_dump_acct(skb, ct, type) < 0 ||
		    ctnetlink_dump_timestamp(skb, ct) < 0)
			goto nla_put_failure;
	} else {
		if (ctnetlink_dump_timeout(skb, ct) < 0)
			goto nla_put_failure;

		if (events & (1 << IPCT_PROTOINFO)
		    && ctnetlink_dump_protoinfo(skb, ct) < 0)
			goto nla_put_failure;

		if ((events & (1 << IPCT_HELPER) || nfct_help(ct))
		    && ctnetlink_dump_helpinfo(skb, ct) < 0)
			goto nla_put_failure;

#ifdef CONFIG_NF_CONNTRACK_SECMARK
		if ((events & (1 << IPCT_SECMARK) || ct->secmark)
		    && ctnetlink_dump_secctx(skb, ct) < 0)
			goto nla_put_failure;
#endif
		if (events & (1 << IPCT_LABEL) &&
		     ctnetlink_dump_labels(skb, ct) < 0)
			goto nla_put_failure;

		if (events & (1 << IPCT_RELATED) &&
		    ctnetlink_dump_master(skb, ct) < 0)
			goto nla_put_failure;

		if (events & (1 << IPCT_SEQADJ) &&
		    ctnetlink_dump_ct_seq_adj(skb, ct) < 0)
			goto nla_put_failure;

		if (events & (1 << IPCT_SYNPROXY) &&
		    ctnetlink_dump_ct_synproxy(skb, ct) < 0)
			goto nla_put_failure;
	}

#ifdef CONFIG_NF_CONNTRACK_MARK
	if ((events & (1 << IPCT_MARK) || ct->mark)
	    && ctnetlink_dump_mark(skb, ct) < 0)
		goto nla_put_failure;
#endif
	nlmsg_end(skb, nlh);
	err = nfnetlink_send(skb, net, item->portid, group, item->report,
			     GFP_ATOMIC);
	if (err == -ENOBUFS || err == -EAGAIN)
		return -ENOBUFS;

	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
nlmsg_failure:
	kfree_skb(skb);
errout:
	if (nfnetlink_set_err(net, 0, group, -ENOBUFS) > 0)
		return -ENOBUFS;

	return 0;
}
#endif /* CONFIG_NF_CONNTRACK_EVENTS */

static int ctnetlink_done(struct netlink_callback *cb)
{
	if (cb->args[1])
		nf_ct_put((struct nf_conn *)cb->args[1]);
	kfree(cb->data);
	return 0;
}

struct ctnetlink_filter {
	u8 family;
	struct {
		u_int32_t val;
		u_int32_t mask;
	} mark;
};

static struct ctnetlink_filter *
ctnetlink_alloc_filter(const struct nlattr * const cda[], u8 family)
{
	struct ctnetlink_filter *filter;

#ifndef CONFIG_NF_CONNTRACK_MARK
	if (cda[CTA_MARK] && cda[CTA_MARK_MASK])
		return ERR_PTR(-EOPNOTSUPP);
#endif

	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (filter == NULL)
		return ERR_PTR(-ENOMEM);

	filter->family = family;

#ifdef CONFIG_NF_CONNTRACK_MARK
	if (cda[CTA_MARK] && cda[CTA_MARK_MASK]) {
		filter->mark.val = ntohl(nla_get_be32(cda[CTA_MARK]));
		filter->mark.mask = ntohl(nla_get_be32(cda[CTA_MARK_MASK]));
	}
#endif
	return filter;
}

static int ctnetlink_start(struct netlink_callback *cb)
{
	const struct nlattr * const *cda = cb->data;
	struct ctnetlink_filter *filter = NULL;
	struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	u8 family = nfmsg->nfgen_family;

	if (family || (cda[CTA_MARK] && cda[CTA_MARK_MASK])) {
		filter = ctnetlink_alloc_filter(cda, family);
		if (IS_ERR(filter))
			return PTR_ERR(filter);
	}

	cb->data = filter;
	return 0;
}

static int ctnetlink_filter_match(struct nf_conn *ct, void *data)
{
	struct ctnetlink_filter *filter = data;

	if (filter == NULL)
		goto out;

	/* Match entries of a given L3 protocol number.
	 * If it is not specified, ie. l3proto == 0,
	 * then match everything.
	 */
	if (filter->family && nf_ct_l3num(ct) != filter->family)
		goto ignore_entry;

#ifdef CONFIG_NF_CONNTRACK_MARK
	if ((ct->mark & filter->mark.mask) != filter->mark.val)
		goto ignore_entry;
#endif

out:
	return 1;

ignore_entry:
	return 0;
}

static int
ctnetlink_dump_table(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct nf_conn *ct, *last;
	struct nf_conntrack_tuple_hash *h;
	struct hlist_nulls_node *n;
	struct nf_conn *nf_ct_evict[8];
	int res, i;
	spinlock_t *lockp;

	last = (struct nf_conn *)cb->args[1];
	i = 0;

	local_bh_disable();
	for (; cb->args[0] < nf_conntrack_htable_size; cb->args[0]++) {
restart:
		while (i) {
			i--;
			if (nf_ct_should_gc(nf_ct_evict[i]))
				nf_ct_kill(nf_ct_evict[i]);
			nf_ct_put(nf_ct_evict[i]);
		}

		lockp = &nf_conntrack_locks[cb->args[0] % CONNTRACK_LOCKS];
		nf_conntrack_lock(lockp);
		if (cb->args[0] >= nf_conntrack_htable_size) {
			spin_unlock(lockp);
			goto out;
		}
		hlist_nulls_for_each_entry(h, n, &nf_conntrack_hash[cb->args[0]],
					   hnnode) {
			if (NF_CT_DIRECTION(h) != IP_CT_DIR_ORIGINAL)
				continue;
			ct = nf_ct_tuplehash_to_ctrack(h);
			if (nf_ct_is_expired(ct)) {
				if (i < ARRAY_SIZE(nf_ct_evict) &&
				    atomic_inc_not_zero(&ct->ct_general.use))
					nf_ct_evict[i++] = ct;
				continue;
			}

			if (!net_eq(net, nf_ct_net(ct)))
				continue;

			if (cb->args[1]) {
				if (ct != last)
					continue;
				cb->args[1] = 0;
			}
			if (!ctnetlink_filter_match(ct, cb->data))
				continue;

			rcu_read_lock();
			res =
			ctnetlink_fill_info(skb, NETLINK_CB(cb->skb).portid,
					    cb->nlh->nlmsg_seq,
					    NFNL_MSG_TYPE(cb->nlh->nlmsg_type),
					    ct);
			rcu_read_unlock();
			if (res < 0) {
				nf_conntrack_get(&ct->ct_general);
				cb->args[1] = (unsigned long)ct;
				spin_unlock(lockp);
				goto out;
			}
		}
		spin_unlock(lockp);
		if (cb->args[1]) {
			cb->args[1] = 0;
			goto restart;
		}
	}
out:
	local_bh_enable();
	if (last) {
		/* nf ct hash resize happened, now clear the leftover. */
		if ((struct nf_conn *)cb->args[1] == last)
			cb->args[1] = 0;

		nf_ct_put(last);
	}

	while (i) {
		i--;
		if (nf_ct_should_gc(nf_ct_evict[i]))
			nf_ct_kill(nf_ct_evict[i]);
		nf_ct_put(nf_ct_evict[i]);
	}

	return skb->len;
}

static int ipv4_nlattr_to_tuple(struct nlattr *tb[],
				struct nf_conntrack_tuple *t)
{
	if (!tb[CTA_IP_V4_SRC] || !tb[CTA_IP_V4_DST])
		return -EINVAL;

	t->src.u3.ip = nla_get_in_addr(tb[CTA_IP_V4_SRC]);
	t->dst.u3.ip = nla_get_in_addr(tb[CTA_IP_V4_DST]);

	return 0;
}

static int ipv6_nlattr_to_tuple(struct nlattr *tb[],
				struct nf_conntrack_tuple *t)
{
	if (!tb[CTA_IP_V6_SRC] || !tb[CTA_IP_V6_DST])
		return -EINVAL;

	t->src.u3.in6 = nla_get_in6_addr(tb[CTA_IP_V6_SRC]);
	t->dst.u3.in6 = nla_get_in6_addr(tb[CTA_IP_V6_DST]);

	return 0;
}

static int ctnetlink_parse_tuple_ip(struct nlattr *attr,
				    struct nf_conntrack_tuple *tuple)
{
	struct nlattr *tb[CTA_IP_MAX+1];
	int ret = 0;

	ret = nla_parse_nested(tb, CTA_IP_MAX, attr, NULL, NULL);
	if (ret < 0)
		return ret;

	ret = nla_validate_nested(attr, CTA_IP_MAX,
				  cta_ip_nla_policy, NULL);
	if (ret)
		return ret;

	switch (tuple->src.l3num) {
	case NFPROTO_IPV4:
		ret = ipv4_nlattr_to_tuple(tb, tuple);
		break;
	case NFPROTO_IPV6:
		ret = ipv6_nlattr_to_tuple(tb, tuple);
		break;
	}

	return ret;
}

static const struct nla_policy proto_nla_policy[CTA_PROTO_MAX+1] = {
	[CTA_PROTO_NUM]	= { .type = NLA_U8 },
};

static int ctnetlink_parse_tuple_proto(struct nlattr *attr,
				       struct nf_conntrack_tuple *tuple)
{
	const struct nf_conntrack_l4proto *l4proto;
	struct nlattr *tb[CTA_PROTO_MAX+1];
	int ret = 0;

	ret = nla_parse_nested(tb, CTA_PROTO_MAX, attr, proto_nla_policy,
			       NULL);
	if (ret < 0)
		return ret;

	if (!tb[CTA_PROTO_NUM])
		return -EINVAL;
	tuple->dst.protonum = nla_get_u8(tb[CTA_PROTO_NUM]);

	rcu_read_lock();
	l4proto = nf_ct_l4proto_find(tuple->dst.protonum);

	if (likely(l4proto->nlattr_to_tuple)) {
		ret = nla_validate_nested(attr, CTA_PROTO_MAX,
					  l4proto->nla_policy, NULL);
		if (ret == 0)
			ret = l4proto->nlattr_to_tuple(tb, tuple);
	}

	rcu_read_unlock();

	return ret;
}

static int
ctnetlink_parse_zone(const struct nlattr *attr,
		     struct nf_conntrack_zone *zone)
{
	nf_ct_zone_init(zone, NF_CT_DEFAULT_ZONE_ID,
			NF_CT_DEFAULT_ZONE_DIR, 0);
#ifdef CONFIG_NF_CONNTRACK_ZONES
	if (attr)
		zone->id = ntohs(nla_get_be16(attr));
#else
	if (attr)
		return -EOPNOTSUPP;
#endif
	return 0;
}

static int
ctnetlink_parse_tuple_zone(struct nlattr *attr, enum ctattr_type type,
			   struct nf_conntrack_zone *zone)
{
	int ret;

	if (zone->id != NF_CT_DEFAULT_ZONE_ID)
		return -EINVAL;

	ret = ctnetlink_parse_zone(attr, zone);
	if (ret < 0)
		return ret;

	if (type == CTA_TUPLE_REPLY)
		zone->dir = NF_CT_ZONE_DIR_REPL;
	else
		zone->dir = NF_CT_ZONE_DIR_ORIG;

	return 0;
}

static const struct nla_policy tuple_nla_policy[CTA_TUPLE_MAX+1] = {
	[CTA_TUPLE_IP]		= { .type = NLA_NESTED },
	[CTA_TUPLE_PROTO]	= { .type = NLA_NESTED },
	[CTA_TUPLE_ZONE]	= { .type = NLA_U16 },
};

static int
ctnetlink_parse_tuple(const struct nlattr * const cda[],
		      struct nf_conntrack_tuple *tuple, u32 type,
		      u_int8_t l3num, struct nf_conntrack_zone *zone)
{
	struct nlattr *tb[CTA_TUPLE_MAX+1];
	int err;

	memset(tuple, 0, sizeof(*tuple));

	err = nla_parse_nested(tb, CTA_TUPLE_MAX, cda[type], tuple_nla_policy,
			       NULL);
	if (err < 0)
		return err;

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

	if (tb[CTA_TUPLE_ZONE]) {
		if (!zone)
			return -EINVAL;

		err = ctnetlink_parse_tuple_zone(tb[CTA_TUPLE_ZONE],
						 type, zone);
		if (err < 0)
			return err;
	}

	/* orig and expect tuples get DIR_ORIGINAL */
	if (type == CTA_TUPLE_REPLY)
		tuple->dst.dir = IP_CT_DIR_REPLY;
	else
		tuple->dst.dir = IP_CT_DIR_ORIGINAL;

	return 0;
}

static const struct nla_policy help_nla_policy[CTA_HELP_MAX+1] = {
	[CTA_HELP_NAME]		= { .type = NLA_NUL_STRING,
				    .len = NF_CT_HELPER_NAME_LEN - 1 },
};

static int ctnetlink_parse_help(const struct nlattr *attr, char **helper_name,
				struct nlattr **helpinfo)
{
	int err;
	struct nlattr *tb[CTA_HELP_MAX+1];

	err = nla_parse_nested(tb, CTA_HELP_MAX, attr, help_nla_policy, NULL);
	if (err < 0)
		return err;

	if (!tb[CTA_HELP_NAME])
		return -EINVAL;

	*helper_name = nla_data(tb[CTA_HELP_NAME]);

	if (tb[CTA_HELP_INFO])
		*helpinfo = tb[CTA_HELP_INFO];

	return 0;
}

static const struct nla_policy ct_nla_policy[CTA_MAX+1] = {
	[CTA_TUPLE_ORIG]	= { .type = NLA_NESTED },
	[CTA_TUPLE_REPLY]	= { .type = NLA_NESTED },
	[CTA_STATUS] 		= { .type = NLA_U32 },
	[CTA_PROTOINFO]		= { .type = NLA_NESTED },
	[CTA_HELP]		= { .type = NLA_NESTED },
	[CTA_NAT_SRC]		= { .type = NLA_NESTED },
	[CTA_TIMEOUT] 		= { .type = NLA_U32 },
	[CTA_MARK]		= { .type = NLA_U32 },
	[CTA_ID]		= { .type = NLA_U32 },
	[CTA_NAT_DST]		= { .type = NLA_NESTED },
	[CTA_TUPLE_MASTER]	= { .type = NLA_NESTED },
	[CTA_NAT_SEQ_ADJ_ORIG]  = { .type = NLA_NESTED },
	[CTA_NAT_SEQ_ADJ_REPLY] = { .type = NLA_NESTED },
	[CTA_ZONE]		= { .type = NLA_U16 },
	[CTA_MARK_MASK]		= { .type = NLA_U32 },
	[CTA_LABELS]		= { .type = NLA_BINARY,
				    .len = NF_CT_LABELS_MAX_SIZE },
	[CTA_LABELS_MASK]	= { .type = NLA_BINARY,
				    .len = NF_CT_LABELS_MAX_SIZE },
};

static int ctnetlink_flush_iterate(struct nf_conn *ct, void *data)
{
	if (test_bit(IPS_OFFLOAD_BIT, &ct->status))
		return 0;

	return ctnetlink_filter_match(ct, data);
}

static int ctnetlink_flush_conntrack(struct net *net,
				     const struct nlattr * const cda[],
				     u32 portid, int report, u8 family)
{
	struct ctnetlink_filter *filter = NULL;

	if (family || (cda[CTA_MARK] && cda[CTA_MARK_MASK])) {
		filter = ctnetlink_alloc_filter(cda, family);
		if (IS_ERR(filter))
			return PTR_ERR(filter);
	}

	nf_ct_iterate_cleanup_net(net, ctnetlink_flush_iterate, filter,
				  portid, report);
	kfree(filter);

	return 0;
}

static int ctnetlink_del_conntrack(struct net *net, struct sock *ctnl,
				   struct sk_buff *skb,
				   const struct nlmsghdr *nlh,
				   const struct nlattr * const cda[],
				   struct netlink_ext_ack *extack)
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;
	struct nf_conn *ct;
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	struct nf_conntrack_zone zone;
	int err;

	err = ctnetlink_parse_zone(cda[CTA_ZONE], &zone);
	if (err < 0)
		return err;

	if (cda[CTA_TUPLE_ORIG])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_ORIG,
					    u3, &zone);
	else if (cda[CTA_TUPLE_REPLY])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_REPLY,
					    u3, &zone);
	else {
		return ctnetlink_flush_conntrack(net, cda,
						 NETLINK_CB(skb).portid,
						 nlmsg_report(nlh), u3);
	}

	if (err < 0)
		return err;

	h = nf_conntrack_find_get(net, &zone, &tuple);
	if (!h)
		return -ENOENT;

	ct = nf_ct_tuplehash_to_ctrack(h);

	if (test_bit(IPS_OFFLOAD_BIT, &ct->status)) {
		nf_ct_put(ct);
		return -EBUSY;
	}

	if (cda[CTA_ID]) {
		u_int32_t id = ntohl(nla_get_be32(cda[CTA_ID]));
		if (id != (u32)(unsigned long)ct) {
			nf_ct_put(ct);
			return -ENOENT;
		}
	}

	nf_ct_delete(ct, NETLINK_CB(skb).portid, nlmsg_report(nlh));
	nf_ct_put(ct);

	return 0;
}

static int ctnetlink_get_conntrack(struct net *net, struct sock *ctnl,
				   struct sk_buff *skb,
				   const struct nlmsghdr *nlh,
				   const struct nlattr * const cda[],
				   struct netlink_ext_ack *extack)
{
	struct nf_conntrack_tuple_hash *h;
	struct nf_conntrack_tuple tuple;
	struct nf_conn *ct;
	struct sk_buff *skb2 = NULL;
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	struct nf_conntrack_zone zone;
	int err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.start = ctnetlink_start,
			.dump = ctnetlink_dump_table,
			.done = ctnetlink_done,
			.data = (void *)cda,
		};

		return netlink_dump_start(ctnl, skb, nlh, &c);
	}

	err = ctnetlink_parse_zone(cda[CTA_ZONE], &zone);
	if (err < 0)
		return err;

	if (cda[CTA_TUPLE_ORIG])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_ORIG,
					    u3, &zone);
	else if (cda[CTA_TUPLE_REPLY])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_TUPLE_REPLY,
					    u3, &zone);
	else
		return -EINVAL;

	if (err < 0)
		return err;

	h = nf_conntrack_find_get(net, &zone, &tuple);
	if (!h)
		return -ENOENT;

	ct = nf_ct_tuplehash_to_ctrack(h);

	err = -ENOMEM;
	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb2 == NULL) {
		nf_ct_put(ct);
		return -ENOMEM;
	}

	rcu_read_lock();
	err = ctnetlink_fill_info(skb2, NETLINK_CB(skb).portid, nlh->nlmsg_seq,
				  NFNL_MSG_TYPE(nlh->nlmsg_type), ct);
	rcu_read_unlock();
	nf_ct_put(ct);
	if (err <= 0)
		goto free;

	err = netlink_unicast(ctnl, skb2, NETLINK_CB(skb).portid, MSG_DONTWAIT);
	if (err < 0)
		goto out;

	return 0;

free:
	kfree_skb(skb2);
out:
	/* this avoids a loop in nfnetlink. */
	return err == -EAGAIN ? -ENOBUFS : err;
}

static int ctnetlink_done_list(struct netlink_callback *cb)
{
	if (cb->args[1])
		nf_ct_put((struct nf_conn *)cb->args[1]);
	return 0;
}

static int
ctnetlink_dump_list(struct sk_buff *skb, struct netlink_callback *cb, bool dying)
{
	struct nf_conn *ct, *last;
	struct nf_conntrack_tuple_hash *h;
	struct hlist_nulls_node *n;
	struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	u_int8_t l3proto = nfmsg->nfgen_family;
	int res;
	int cpu;
	struct hlist_nulls_head *list;
	struct net *net = sock_net(skb->sk);

	if (cb->args[2])
		return 0;

	last = (struct nf_conn *)cb->args[1];

	for (cpu = cb->args[0]; cpu < nr_cpu_ids; cpu++) {
		struct ct_pcpu *pcpu;

		if (!cpu_possible(cpu))
			continue;

		pcpu = per_cpu_ptr(net->ct.pcpu_lists, cpu);
		spin_lock_bh(&pcpu->lock);
		list = dying ? &pcpu->dying : &pcpu->unconfirmed;
restart:
		hlist_nulls_for_each_entry(h, n, list, hnnode) {
			ct = nf_ct_tuplehash_to_ctrack(h);
			if (l3proto && nf_ct_l3num(ct) != l3proto)
				continue;
			if (cb->args[1]) {
				if (ct != last)
					continue;
				cb->args[1] = 0;
			}
			rcu_read_lock();
			res = ctnetlink_fill_info(skb, NETLINK_CB(cb->skb).portid,
						  cb->nlh->nlmsg_seq,
						  NFNL_MSG_TYPE(cb->nlh->nlmsg_type),
						  ct);
			rcu_read_unlock();
			if (res < 0) {
				if (!atomic_inc_not_zero(&ct->ct_general.use))
					continue;
				cb->args[0] = cpu;
				cb->args[1] = (unsigned long)ct;
				spin_unlock_bh(&pcpu->lock);
				goto out;
			}
		}
		if (cb->args[1]) {
			cb->args[1] = 0;
			goto restart;
		}
		spin_unlock_bh(&pcpu->lock);
	}
	cb->args[2] = 1;
out:
	if (last)
		nf_ct_put(last);

	return skb->len;
}

static int
ctnetlink_dump_dying(struct sk_buff *skb, struct netlink_callback *cb)
{
	return ctnetlink_dump_list(skb, cb, true);
}

static int ctnetlink_get_ct_dying(struct net *net, struct sock *ctnl,
				  struct sk_buff *skb,
				  const struct nlmsghdr *nlh,
				  const struct nlattr * const cda[],
				  struct netlink_ext_ack *extack)
{
	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = ctnetlink_dump_dying,
			.done = ctnetlink_done_list,
		};
		return netlink_dump_start(ctnl, skb, nlh, &c);
	}

	return -EOPNOTSUPP;
}

static int
ctnetlink_dump_unconfirmed(struct sk_buff *skb, struct netlink_callback *cb)
{
	return ctnetlink_dump_list(skb, cb, false);
}

static int ctnetlink_get_ct_unconfirmed(struct net *net, struct sock *ctnl,
					struct sk_buff *skb,
					const struct nlmsghdr *nlh,
					const struct nlattr * const cda[],
					struct netlink_ext_ack *extack)
{
	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = ctnetlink_dump_unconfirmed,
			.done = ctnetlink_done_list,
		};
		return netlink_dump_start(ctnl, skb, nlh, &c);
	}

	return -EOPNOTSUPP;
}

#ifdef CONFIG_NF_NAT_NEEDED
static int
ctnetlink_parse_nat_setup(struct nf_conn *ct,
			  enum nf_nat_manip_type manip,
			  const struct nlattr *attr)
{
	struct nf_nat_hook *nat_hook;
	int err;

	nat_hook = rcu_dereference(nf_nat_hook);
	if (!nat_hook) {
#ifdef CONFIG_MODULES
		rcu_read_unlock();
		nfnl_unlock(NFNL_SUBSYS_CTNETLINK);
		if (request_module("nf-nat") < 0) {
			nfnl_lock(NFNL_SUBSYS_CTNETLINK);
			rcu_read_lock();
			return -EOPNOTSUPP;
		}
		nfnl_lock(NFNL_SUBSYS_CTNETLINK);
		rcu_read_lock();
		nat_hook = rcu_dereference(nf_nat_hook);
		if (nat_hook)
			return -EAGAIN;
#endif
		return -EOPNOTSUPP;
	}

	err = nat_hook->parse_nat_setup(ct, manip, attr);
	if (err == -EAGAIN) {
#ifdef CONFIG_MODULES
		rcu_read_unlock();
		nfnl_unlock(NFNL_SUBSYS_CTNETLINK);
		if (request_module("nf-nat-%u", nf_ct_l3num(ct)) < 0) {
			nfnl_lock(NFNL_SUBSYS_CTNETLINK);
			rcu_read_lock();
			return -EOPNOTSUPP;
		}
		nfnl_lock(NFNL_SUBSYS_CTNETLINK);
		rcu_read_lock();
#else
		err = -EOPNOTSUPP;
#endif
	}
	return err;
}
#endif

static void
__ctnetlink_change_status(struct nf_conn *ct, unsigned long on,
			  unsigned long off)
{
	unsigned int bit;

	/* Ignore these unchangable bits */
	on &= ~IPS_UNCHANGEABLE_MASK;
	off &= ~IPS_UNCHANGEABLE_MASK;

	for (bit = 0; bit < __IPS_MAX_BIT; bit++) {
		if (on & (1 << bit))
			set_bit(bit, &ct->status);
		else if (off & (1 << bit))
			clear_bit(bit, &ct->status);
	}
}

static int
ctnetlink_change_status(struct nf_conn *ct, const struct nlattr * const cda[])
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

	__ctnetlink_change_status(ct, status, 0);
	return 0;
}

static int
ctnetlink_setup_nat(struct nf_conn *ct, const struct nlattr * const cda[])
{
#ifdef CONFIG_NF_NAT_NEEDED
	int ret;

	if (!cda[CTA_NAT_DST] && !cda[CTA_NAT_SRC])
		return 0;

	ret = ctnetlink_parse_nat_setup(ct, NF_NAT_MANIP_DST,
					cda[CTA_NAT_DST]);
	if (ret < 0)
		return ret;

	return ctnetlink_parse_nat_setup(ct, NF_NAT_MANIP_SRC,
					 cda[CTA_NAT_SRC]);
#else
	if (!cda[CTA_NAT_DST] && !cda[CTA_NAT_SRC])
		return 0;
	return -EOPNOTSUPP;
#endif
}

static int ctnetlink_change_helper(struct nf_conn *ct,
				   const struct nlattr * const cda[])
{
	struct nf_conntrack_helper *helper;
	struct nf_conn_help *help = nfct_help(ct);
	char *helpname = NULL;
	struct nlattr *helpinfo = NULL;
	int err;

	err = ctnetlink_parse_help(cda[CTA_HELP], &helpname, &helpinfo);
	if (err < 0)
		return err;

	/* don't change helper of sibling connections */
	if (ct->master) {
		/* If we try to change the helper to the same thing twice,
		 * treat the second attempt as a no-op instead of returning
		 * an error.
		 */
		err = -EBUSY;
		if (help) {
			rcu_read_lock();
			helper = rcu_dereference(help->helper);
			if (helper && !strcmp(helper->name, helpname))
				err = 0;
			rcu_read_unlock();
		}

		return err;
	}

	if (!strcmp(helpname, "")) {
		if (help && help->helper) {
			/* we had a helper before ... */
			nf_ct_remove_expectations(ct);
			RCU_INIT_POINTER(help->helper, NULL);
		}

		return 0;
	}

	rcu_read_lock();
	helper = __nf_conntrack_helper_find(helpname, nf_ct_l3num(ct),
					    nf_ct_protonum(ct));
	if (helper == NULL) {
		rcu_read_unlock();
		return -EOPNOTSUPP;
	}

	if (help) {
		if (help->helper == helper) {
			/* update private helper data if allowed. */
			if (helper->from_nlattr)
				helper->from_nlattr(helpinfo, ct);
			err = 0;
		} else
			err = -EBUSY;
	} else {
		/* we cannot set a helper for an existing conntrack */
		err = -EOPNOTSUPP;
	}

	rcu_read_unlock();
	return err;
}

static int ctnetlink_change_timeout(struct nf_conn *ct,
				    const struct nlattr * const cda[])
{
	u64 timeout = (u64)ntohl(nla_get_be32(cda[CTA_TIMEOUT])) * HZ;

	if (timeout > INT_MAX)
		timeout = INT_MAX;
	ct->timeout = nfct_time_stamp + (u32)timeout;

	if (test_bit(IPS_DYING_BIT, &ct->status))
		return -ETIME;

	return 0;
}

#if defined(CONFIG_NF_CONNTRACK_MARK)
static void ctnetlink_change_mark(struct nf_conn *ct,
				    const struct nlattr * const cda[])
{
	u32 mark, newmark, mask = 0;

	if (cda[CTA_MARK_MASK])
		mask = ~ntohl(nla_get_be32(cda[CTA_MARK_MASK]));

	mark = ntohl(nla_get_be32(cda[CTA_MARK]));
	newmark = (ct->mark & mask) ^ mark;
	if (newmark != ct->mark)
		ct->mark = newmark;
}
#endif

static const struct nla_policy protoinfo_policy[CTA_PROTOINFO_MAX+1] = {
	[CTA_PROTOINFO_TCP]	= { .type = NLA_NESTED },
	[CTA_PROTOINFO_DCCP]	= { .type = NLA_NESTED },
	[CTA_PROTOINFO_SCTP]	= { .type = NLA_NESTED },
};

static int ctnetlink_change_protoinfo(struct nf_conn *ct,
				      const struct nlattr * const cda[])
{
	const struct nlattr *attr = cda[CTA_PROTOINFO];
	const struct nf_conntrack_l4proto *l4proto;
	struct nlattr *tb[CTA_PROTOINFO_MAX+1];
	int err = 0;

	err = nla_parse_nested(tb, CTA_PROTOINFO_MAX, attr, protoinfo_policy,
			       NULL);
	if (err < 0)
		return err;

	l4proto = nf_ct_l4proto_find(nf_ct_protonum(ct));
	if (l4proto->from_nlattr)
		err = l4proto->from_nlattr(tb, ct);

	return err;
}

static const struct nla_policy seqadj_policy[CTA_SEQADJ_MAX+1] = {
	[CTA_SEQADJ_CORRECTION_POS]	= { .type = NLA_U32 },
	[CTA_SEQADJ_OFFSET_BEFORE]	= { .type = NLA_U32 },
	[CTA_SEQADJ_OFFSET_AFTER]	= { .type = NLA_U32 },
};

static int change_seq_adj(struct nf_ct_seqadj *seq,
			  const struct nlattr * const attr)
{
	int err;
	struct nlattr *cda[CTA_SEQADJ_MAX+1];

	err = nla_parse_nested(cda, CTA_SEQADJ_MAX, attr, seqadj_policy, NULL);
	if (err < 0)
		return err;

	if (!cda[CTA_SEQADJ_CORRECTION_POS])
		return -EINVAL;

	seq->correction_pos =
		ntohl(nla_get_be32(cda[CTA_SEQADJ_CORRECTION_POS]));

	if (!cda[CTA_SEQADJ_OFFSET_BEFORE])
		return -EINVAL;

	seq->offset_before =
		ntohl(nla_get_be32(cda[CTA_SEQADJ_OFFSET_BEFORE]));

	if (!cda[CTA_SEQADJ_OFFSET_AFTER])
		return -EINVAL;

	seq->offset_after =
		ntohl(nla_get_be32(cda[CTA_SEQADJ_OFFSET_AFTER]));

	return 0;
}

static int
ctnetlink_change_seq_adj(struct nf_conn *ct,
			 const struct nlattr * const cda[])
{
	struct nf_conn_seqadj *seqadj = nfct_seqadj(ct);
	int ret = 0;

	if (!seqadj)
		return 0;

	spin_lock_bh(&ct->lock);
	if (cda[CTA_SEQ_ADJ_ORIG]) {
		ret = change_seq_adj(&seqadj->seq[IP_CT_DIR_ORIGINAL],
				     cda[CTA_SEQ_ADJ_ORIG]);
		if (ret < 0)
			goto err;

		set_bit(IPS_SEQ_ADJUST_BIT, &ct->status);
	}

	if (cda[CTA_SEQ_ADJ_REPLY]) {
		ret = change_seq_adj(&seqadj->seq[IP_CT_DIR_REPLY],
				     cda[CTA_SEQ_ADJ_REPLY]);
		if (ret < 0)
			goto err;

		set_bit(IPS_SEQ_ADJUST_BIT, &ct->status);
	}

	spin_unlock_bh(&ct->lock);
	return 0;
err:
	spin_unlock_bh(&ct->lock);
	return ret;
}

static const struct nla_policy synproxy_policy[CTA_SYNPROXY_MAX + 1] = {
	[CTA_SYNPROXY_ISN]	= { .type = NLA_U32 },
	[CTA_SYNPROXY_ITS]	= { .type = NLA_U32 },
	[CTA_SYNPROXY_TSOFF]	= { .type = NLA_U32 },
};

static int ctnetlink_change_synproxy(struct nf_conn *ct,
				     const struct nlattr * const cda[])
{
	struct nf_conn_synproxy *synproxy = nfct_synproxy(ct);
	struct nlattr *tb[CTA_SYNPROXY_MAX + 1];
	int err;

	if (!synproxy)
		return 0;

	err = nla_parse_nested(tb, CTA_SYNPROXY_MAX, cda[CTA_SYNPROXY],
			       synproxy_policy, NULL);
	if (err < 0)
		return err;

	if (!tb[CTA_SYNPROXY_ISN] ||
	    !tb[CTA_SYNPROXY_ITS] ||
	    !tb[CTA_SYNPROXY_TSOFF])
		return -EINVAL;

	synproxy->isn = ntohl(nla_get_be32(tb[CTA_SYNPROXY_ISN]));
	synproxy->its = ntohl(nla_get_be32(tb[CTA_SYNPROXY_ITS]));
	synproxy->tsoff = ntohl(nla_get_be32(tb[CTA_SYNPROXY_TSOFF]));

	return 0;
}

static int
ctnetlink_attach_labels(struct nf_conn *ct, const struct nlattr * const cda[])
{
#ifdef CONFIG_NF_CONNTRACK_LABELS
	size_t len = nla_len(cda[CTA_LABELS]);
	const void *mask = cda[CTA_LABELS_MASK];

	if (len & (sizeof(u32)-1)) /* must be multiple of u32 */
		return -EINVAL;

	if (mask) {
		if (nla_len(cda[CTA_LABELS_MASK]) == 0 ||
		    nla_len(cda[CTA_LABELS_MASK]) != len)
			return -EINVAL;
		mask = nla_data(cda[CTA_LABELS_MASK]);
	}

	len /= sizeof(u32);

	return nf_connlabels_replace(ct, nla_data(cda[CTA_LABELS]), mask, len);
#else
	return -EOPNOTSUPP;
#endif
}

static int
ctnetlink_change_conntrack(struct nf_conn *ct,
			   const struct nlattr * const cda[])
{
	int err;

	/* only allow NAT changes and master assignation for new conntracks */
	if (cda[CTA_NAT_SRC] || cda[CTA_NAT_DST] || cda[CTA_TUPLE_MASTER])
		return -EOPNOTSUPP;

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
		ctnetlink_change_mark(ct, cda);
#endif

	if (cda[CTA_SEQ_ADJ_ORIG] || cda[CTA_SEQ_ADJ_REPLY]) {
		err = ctnetlink_change_seq_adj(ct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_SYNPROXY]) {
		err = ctnetlink_change_synproxy(ct, cda);
		if (err < 0)
			return err;
	}

	if (cda[CTA_LABELS]) {
		err = ctnetlink_attach_labels(ct, cda);
		if (err < 0)
			return err;
	}

	return 0;
}

static struct nf_conn *
ctnetlink_create_conntrack(struct net *net,
			   const struct nf_conntrack_zone *zone,
			   const struct nlattr * const cda[],
			   struct nf_conntrack_tuple *otuple,
			   struct nf_conntrack_tuple *rtuple,
			   u8 u3)
{
	struct nf_conn *ct;
	int err = -EINVAL;
	struct nf_conntrack_helper *helper;
	struct nf_conn_tstamp *tstamp;
	u64 timeout;

	ct = nf_conntrack_alloc(net, zone, otuple, rtuple, GFP_ATOMIC);
	if (IS_ERR(ct))
		return ERR_PTR(-ENOMEM);

	if (!cda[CTA_TIMEOUT])
		goto err1;

	timeout = (u64)ntohl(nla_get_be32(cda[CTA_TIMEOUT])) * HZ;
	if (timeout > INT_MAX)
		timeout = INT_MAX;
	ct->timeout = (u32)timeout + nfct_time_stamp;

	rcu_read_lock();
 	if (cda[CTA_HELP]) {
		char *helpname = NULL;
		struct nlattr *helpinfo = NULL;

		err = ctnetlink_parse_help(cda[CTA_HELP], &helpname, &helpinfo);
 		if (err < 0)
			goto err2;

		helper = __nf_conntrack_helper_find(helpname, nf_ct_l3num(ct),
						    nf_ct_protonum(ct));
		if (helper == NULL) {
			rcu_read_unlock();
#ifdef CONFIG_MODULES
			if (request_module("nfct-helper-%s", helpname) < 0) {
				err = -EOPNOTSUPP;
				goto err1;
			}

			rcu_read_lock();
			helper = __nf_conntrack_helper_find(helpname,
							    nf_ct_l3num(ct),
							    nf_ct_protonum(ct));
			if (helper) {
				err = -EAGAIN;
				goto err2;
			}
			rcu_read_unlock();
#endif
			err = -EOPNOTSUPP;
			goto err1;
		} else {
			struct nf_conn_help *help;

			help = nf_ct_helper_ext_add(ct, GFP_ATOMIC);
			if (help == NULL) {
				err = -ENOMEM;
				goto err2;
			}
			/* set private helper data if allowed. */
			if (helper->from_nlattr)
				helper->from_nlattr(helpinfo, ct);

			/* not in hash table yet so not strictly necessary */
			RCU_INIT_POINTER(help->helper, helper);
		}
	} else {
		/* try an implicit helper assignation */
		err = __nf_ct_try_assign_helper(ct, NULL, GFP_ATOMIC);
		if (err < 0)
			goto err2;
	}

	err = ctnetlink_setup_nat(ct, cda);
	if (err < 0)
		goto err2;

	nf_ct_acct_ext_add(ct, GFP_ATOMIC);
	nf_ct_tstamp_ext_add(ct, GFP_ATOMIC);
	nf_ct_ecache_ext_add(ct, 0, 0, GFP_ATOMIC);
	nf_ct_labels_ext_add(ct);
	nfct_seqadj_ext_add(ct);
	nfct_synproxy_ext_add(ct);

	/* we must add conntrack extensions before confirmation. */
	ct->status |= IPS_CONFIRMED;

	if (cda[CTA_STATUS]) {
		err = ctnetlink_change_status(ct, cda);
		if (err < 0)
			goto err2;
	}

	if (cda[CTA_SEQ_ADJ_ORIG] || cda[CTA_SEQ_ADJ_REPLY]) {
		err = ctnetlink_change_seq_adj(ct, cda);
		if (err < 0)
			goto err2;
	}

	memset(&ct->proto, 0, sizeof(ct->proto));
	if (cda[CTA_PROTOINFO]) {
		err = ctnetlink_change_protoinfo(ct, cda);
		if (err < 0)
			goto err2;
	}

	if (cda[CTA_SYNPROXY]) {
		err = ctnetlink_change_synproxy(ct, cda);
		if (err < 0)
			goto err2;
	}

#if defined(CONFIG_NF_CONNTRACK_MARK)
	if (cda[CTA_MARK])
		ctnetlink_change_mark(ct, cda);
#endif

	/* setup master conntrack: this is a confirmed expectation */
	if (cda[CTA_TUPLE_MASTER]) {
		struct nf_conntrack_tuple master;
		struct nf_conntrack_tuple_hash *master_h;
		struct nf_conn *master_ct;

		err = ctnetlink_parse_tuple(cda, &master, CTA_TUPLE_MASTER,
					    u3, NULL);
		if (err < 0)
			goto err2;

		master_h = nf_conntrack_find_get(net, zone, &master);
		if (master_h == NULL) {
			err = -ENOENT;
			goto err2;
		}
		master_ct = nf_ct_tuplehash_to_ctrack(master_h);
		__set_bit(IPS_EXPECTED_BIT, &ct->status);
		ct->master = master_ct;
	}
	tstamp = nf_conn_tstamp_find(ct);
	if (tstamp)
		tstamp->start = ktime_get_real_ns();

	err = nf_conntrack_hash_check_insert(ct);
	if (err < 0)
		goto err2;

	rcu_read_unlock();

	return ct;

err2:
	rcu_read_unlock();
err1:
	nf_conntrack_free(ct);
	return ERR_PTR(err);
}

static int ctnetlink_new_conntrack(struct net *net, struct sock *ctnl,
				   struct sk_buff *skb,
				   const struct nlmsghdr *nlh,
				   const struct nlattr * const cda[],
				   struct netlink_ext_ack *extack)
{
	struct nf_conntrack_tuple otuple, rtuple;
	struct nf_conntrack_tuple_hash *h = NULL;
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	struct nf_conn *ct;
	u_int8_t u3 = nfmsg->nfgen_family;
	struct nf_conntrack_zone zone;
	int err;

	err = ctnetlink_parse_zone(cda[CTA_ZONE], &zone);
	if (err < 0)
		return err;

	if (cda[CTA_TUPLE_ORIG]) {
		err = ctnetlink_parse_tuple(cda, &otuple, CTA_TUPLE_ORIG,
					    u3, &zone);
		if (err < 0)
			return err;
	}

	if (cda[CTA_TUPLE_REPLY]) {
		err = ctnetlink_parse_tuple(cda, &rtuple, CTA_TUPLE_REPLY,
					    u3, &zone);
		if (err < 0)
			return err;
	}

	if (cda[CTA_TUPLE_ORIG])
		h = nf_conntrack_find_get(net, &zone, &otuple);
	else if (cda[CTA_TUPLE_REPLY])
		h = nf_conntrack_find_get(net, &zone, &rtuple);

	if (h == NULL) {
		err = -ENOENT;
		if (nlh->nlmsg_flags & NLM_F_CREATE) {
			enum ip_conntrack_events events;

			if (!cda[CTA_TUPLE_ORIG] || !cda[CTA_TUPLE_REPLY])
				return -EINVAL;
			if (otuple.dst.protonum != rtuple.dst.protonum)
				return -EINVAL;

			ct = ctnetlink_create_conntrack(net, &zone, cda, &otuple,
							&rtuple, u3);
			if (IS_ERR(ct))
				return PTR_ERR(ct);

			err = 0;
			if (test_bit(IPS_EXPECTED_BIT, &ct->status))
				events = 1 << IPCT_RELATED;
			else
				events = 1 << IPCT_NEW;

			if (cda[CTA_LABELS] &&
			    ctnetlink_attach_labels(ct, cda) == 0)
				events |= (1 << IPCT_LABEL);

			nf_conntrack_eventmask_report((1 << IPCT_REPLY) |
						      (1 << IPCT_ASSURED) |
						      (1 << IPCT_HELPER) |
						      (1 << IPCT_PROTOINFO) |
						      (1 << IPCT_SEQADJ) |
						      (1 << IPCT_MARK) |
						      (1 << IPCT_SYNPROXY) |
						      events,
						      ct, NETLINK_CB(skb).portid,
						      nlmsg_report(nlh));
			nf_ct_put(ct);
		}

		return err;
	}
	/* implicit 'else' */

	err = -EEXIST;
	ct = nf_ct_tuplehash_to_ctrack(h);
	if (!(nlh->nlmsg_flags & NLM_F_EXCL)) {
		err = ctnetlink_change_conntrack(ct, cda);
		if (err == 0) {
			nf_conntrack_eventmask_report((1 << IPCT_REPLY) |
						      (1 << IPCT_ASSURED) |
						      (1 << IPCT_HELPER) |
						      (1 << IPCT_LABEL) |
						      (1 << IPCT_PROTOINFO) |
						      (1 << IPCT_SEQADJ) |
						      (1 << IPCT_MARK) |
						      (1 << IPCT_SYNPROXY),
						      ct, NETLINK_CB(skb).portid,
						      nlmsg_report(nlh));
		}
	}

	nf_ct_put(ct);
	return err;
}

static int
ctnetlink_ct_stat_cpu_fill_info(struct sk_buff *skb, u32 portid, u32 seq,
				__u16 cpu, const struct ip_conntrack_stat *st)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	unsigned int flags = portid ? NLM_F_MULTI : 0, event;

	event = nfnl_msg_type(NFNL_SUBSYS_CTNETLINK,
			      IPCTNL_MSG_CT_GET_STATS_CPU);
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = AF_UNSPEC;
	nfmsg->version      = NFNETLINK_V0;
	nfmsg->res_id	    = htons(cpu);

	if (nla_put_be32(skb, CTA_STATS_FOUND, htonl(st->found)) ||
	    nla_put_be32(skb, CTA_STATS_INVALID, htonl(st->invalid)) ||
	    nla_put_be32(skb, CTA_STATS_IGNORE, htonl(st->ignore)) ||
	    nla_put_be32(skb, CTA_STATS_INSERT, htonl(st->insert)) ||
	    nla_put_be32(skb, CTA_STATS_INSERT_FAILED,
				htonl(st->insert_failed)) ||
	    nla_put_be32(skb, CTA_STATS_DROP, htonl(st->drop)) ||
	    nla_put_be32(skb, CTA_STATS_EARLY_DROP, htonl(st->early_drop)) ||
	    nla_put_be32(skb, CTA_STATS_ERROR, htonl(st->error)) ||
	    nla_put_be32(skb, CTA_STATS_SEARCH_RESTART,
				htonl(st->search_restart)))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return skb->len;

nla_put_failure:
nlmsg_failure:
	nlmsg_cancel(skb, nlh);
	return -1;
}

static int
ctnetlink_ct_stat_cpu_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int cpu;
	struct net *net = sock_net(skb->sk);

	if (cb->args[0] == nr_cpu_ids)
		return 0;

	for (cpu = cb->args[0]; cpu < nr_cpu_ids; cpu++) {
		const struct ip_conntrack_stat *st;

		if (!cpu_possible(cpu))
			continue;

		st = per_cpu_ptr(net->ct.stat, cpu);
		if (ctnetlink_ct_stat_cpu_fill_info(skb,
						    NETLINK_CB(cb->skb).portid,
						    cb->nlh->nlmsg_seq,
						    cpu, st) < 0)
				break;
	}
	cb->args[0] = cpu;

	return skb->len;
}

static int ctnetlink_stat_ct_cpu(struct net *net, struct sock *ctnl,
				 struct sk_buff *skb,
				 const struct nlmsghdr *nlh,
				 const struct nlattr * const cda[],
				 struct netlink_ext_ack *extack)
{
	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = ctnetlink_ct_stat_cpu_dump,
		};
		return netlink_dump_start(ctnl, skb, nlh, &c);
	}

	return 0;
}

static int
ctnetlink_stat_ct_fill_info(struct sk_buff *skb, u32 portid, u32 seq, u32 type,
			    struct net *net)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	unsigned int flags = portid ? NLM_F_MULTI : 0, event;
	unsigned int nr_conntracks = atomic_read(&net->ct.count);

	event = nfnl_msg_type(NFNL_SUBSYS_CTNETLINK, IPCTNL_MSG_CT_GET_STATS);
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = AF_UNSPEC;
	nfmsg->version      = NFNETLINK_V0;
	nfmsg->res_id	    = 0;

	if (nla_put_be32(skb, CTA_STATS_GLOBAL_ENTRIES, htonl(nr_conntracks)))
		goto nla_put_failure;

	if (nla_put_be32(skb, CTA_STATS_GLOBAL_MAX_ENTRIES, htonl(nf_conntrack_max)))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return skb->len;

nla_put_failure:
nlmsg_failure:
	nlmsg_cancel(skb, nlh);
	return -1;
}

static int ctnetlink_stat_ct(struct net *net, struct sock *ctnl,
			     struct sk_buff *skb, const struct nlmsghdr *nlh,
			     const struct nlattr * const cda[],
			     struct netlink_ext_ack *extack)
{
	struct sk_buff *skb2;
	int err;

	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb2 == NULL)
		return -ENOMEM;

	err = ctnetlink_stat_ct_fill_info(skb2, NETLINK_CB(skb).portid,
					  nlh->nlmsg_seq,
					  NFNL_MSG_TYPE(nlh->nlmsg_type),
					  sock_net(skb->sk));
	if (err <= 0)
		goto free;

	err = netlink_unicast(ctnl, skb2, NETLINK_CB(skb).portid, MSG_DONTWAIT);
	if (err < 0)
		goto out;

	return 0;

free:
	kfree_skb(skb2);
out:
	/* this avoids a loop in nfnetlink. */
	return err == -EAGAIN ? -ENOBUFS : err;
}

static const struct nla_policy exp_nla_policy[CTA_EXPECT_MAX+1] = {
	[CTA_EXPECT_MASTER]	= { .type = NLA_NESTED },
	[CTA_EXPECT_TUPLE]	= { .type = NLA_NESTED },
	[CTA_EXPECT_MASK]	= { .type = NLA_NESTED },
	[CTA_EXPECT_TIMEOUT]	= { .type = NLA_U32 },
	[CTA_EXPECT_ID]		= { .type = NLA_U32 },
	[CTA_EXPECT_HELP_NAME]	= { .type = NLA_NUL_STRING,
				    .len = NF_CT_HELPER_NAME_LEN - 1 },
	[CTA_EXPECT_ZONE]	= { .type = NLA_U16 },
	[CTA_EXPECT_FLAGS]	= { .type = NLA_U32 },
	[CTA_EXPECT_CLASS]	= { .type = NLA_U32 },
	[CTA_EXPECT_NAT]	= { .type = NLA_NESTED },
	[CTA_EXPECT_FN]		= { .type = NLA_NUL_STRING },
};

static struct nf_conntrack_expect *
ctnetlink_alloc_expect(const struct nlattr *const cda[], struct nf_conn *ct,
		       struct nf_conntrack_helper *helper,
		       struct nf_conntrack_tuple *tuple,
		       struct nf_conntrack_tuple *mask);

#ifdef CONFIG_NETFILTER_NETLINK_GLUE_CT
static size_t
ctnetlink_glue_build_size(const struct nf_conn *ct)
{
	return 3 * nla_total_size(0) /* CTA_TUPLE_ORIG|REPL|MASTER */
	       + 3 * nla_total_size(0) /* CTA_TUPLE_IP */
	       + 3 * nla_total_size(0) /* CTA_TUPLE_PROTO */
	       + 3 * nla_total_size(sizeof(u_int8_t)) /* CTA_PROTO_NUM */
	       + nla_total_size(sizeof(u_int32_t)) /* CTA_ID */
	       + nla_total_size(sizeof(u_int32_t)) /* CTA_STATUS */
	       + nla_total_size(sizeof(u_int32_t)) /* CTA_TIMEOUT */
	       + nla_total_size(0) /* CTA_PROTOINFO */
	       + nla_total_size(0) /* CTA_HELP */
	       + nla_total_size(NF_CT_HELPER_NAME_LEN) /* CTA_HELP_NAME */
	       + ctnetlink_secctx_size(ct)
#ifdef CONFIG_NF_NAT_NEEDED
	       + 2 * nla_total_size(0) /* CTA_NAT_SEQ_ADJ_ORIG|REPL */
	       + 6 * nla_total_size(sizeof(u_int32_t)) /* CTA_NAT_SEQ_OFFSET */
#endif
#ifdef CONFIG_NF_CONNTRACK_MARK
	       + nla_total_size(sizeof(u_int32_t)) /* CTA_MARK */
#endif
#ifdef CONFIG_NF_CONNTRACK_ZONES
	       + nla_total_size(sizeof(u_int16_t)) /* CTA_ZONE|CTA_TUPLE_ZONE */
#endif
	       + ctnetlink_proto_size(ct)
	       ;
}

static struct nf_conn *ctnetlink_glue_get_ct(const struct sk_buff *skb,
					     enum ip_conntrack_info *ctinfo)
{
	return nf_ct_get(skb, ctinfo);
}

static int __ctnetlink_glue_build(struct sk_buff *skb, struct nf_conn *ct)
{
	const struct nf_conntrack_zone *zone;
	struct nlattr *nest_parms;

	zone = nf_ct_zone(ct);

	nest_parms = nla_nest_start(skb, CTA_TUPLE_ORIG | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, nf_ct_tuple(ct, IP_CT_DIR_ORIGINAL)) < 0)
		goto nla_put_failure;
	if (ctnetlink_dump_zone_id(skb, CTA_TUPLE_ZONE, zone,
				   NF_CT_ZONE_DIR_ORIG) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	nest_parms = nla_nest_start(skb, CTA_TUPLE_REPLY | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;
	if (ctnetlink_dump_tuples(skb, nf_ct_tuple(ct, IP_CT_DIR_REPLY)) < 0)
		goto nla_put_failure;
	if (ctnetlink_dump_zone_id(skb, CTA_TUPLE_ZONE, zone,
				   NF_CT_ZONE_DIR_REPL) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest_parms);

	if (ctnetlink_dump_zone_id(skb, CTA_ZONE, zone,
				   NF_CT_DEFAULT_ZONE_DIR) < 0)
		goto nla_put_failure;

	if (ctnetlink_dump_id(skb, ct) < 0)
		goto nla_put_failure;

	if (ctnetlink_dump_status(skb, ct) < 0)
		goto nla_put_failure;

	if (ctnetlink_dump_timeout(skb, ct) < 0)
		goto nla_put_failure;

	if (ctnetlink_dump_protoinfo(skb, ct) < 0)
		goto nla_put_failure;

	if (ctnetlink_dump_helpinfo(skb, ct) < 0)
		goto nla_put_failure;

#ifdef CONFIG_NF_CONNTRACK_SECMARK
	if (ct->secmark && ctnetlink_dump_secctx(skb, ct) < 0)
		goto nla_put_failure;
#endif
	if (ct->master && ctnetlink_dump_master(skb, ct) < 0)
		goto nla_put_failure;

	if ((ct->status & IPS_SEQ_ADJUST) &&
	    ctnetlink_dump_ct_seq_adj(skb, ct) < 0)
		goto nla_put_failure;

	if (ctnetlink_dump_ct_synproxy(skb, ct) < 0)
		goto nla_put_failure;

#ifdef CONFIG_NF_CONNTRACK_MARK
	if (ct->mark && ctnetlink_dump_mark(skb, ct) < 0)
		goto nla_put_failure;
#endif
	if (ctnetlink_dump_labels(skb, ct) < 0)
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -ENOSPC;
}

static int
ctnetlink_glue_build(struct sk_buff *skb, struct nf_conn *ct,
		     enum ip_conntrack_info ctinfo,
		     u_int16_t ct_attr, u_int16_t ct_info_attr)
{
	struct nlattr *nest_parms;

	nest_parms = nla_nest_start(skb, ct_attr | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;

	if (__ctnetlink_glue_build(skb, ct) < 0)
		goto nla_put_failure;

	nla_nest_end(skb, nest_parms);

	if (nla_put_be32(skb, ct_info_attr, htonl(ctinfo)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -ENOSPC;
}

static int
ctnetlink_update_status(struct nf_conn *ct, const struct nlattr * const cda[])
{
	unsigned int status = ntohl(nla_get_be32(cda[CTA_STATUS]));
	unsigned long d = ct->status ^ status;

	if (d & IPS_SEEN_REPLY && !(status & IPS_SEEN_REPLY))
		/* SEEN_REPLY bit can only be set */
		return -EBUSY;

	if (d & IPS_ASSURED && !(status & IPS_ASSURED))
		/* ASSURED bit can only be set */
		return -EBUSY;

	/* This check is less strict than ctnetlink_change_status()
	 * because callers often flip IPS_EXPECTED bits when sending
	 * an NFQA_CT attribute to the kernel.  So ignore the
	 * unchangeable bits but do not error out. Also user programs
	 * are allowed to clear the bits that they are allowed to change.
	 */
	__ctnetlink_change_status(ct, status, ~status);
	return 0;
}

static int
ctnetlink_glue_parse_ct(const struct nlattr *cda[], struct nf_conn *ct)
{
	int err;

	if (cda[CTA_TIMEOUT]) {
		err = ctnetlink_change_timeout(ct, cda);
		if (err < 0)
			return err;
	}
	if (cda[CTA_STATUS]) {
		err = ctnetlink_update_status(ct, cda);
		if (err < 0)
			return err;
	}
	if (cda[CTA_HELP]) {
		err = ctnetlink_change_helper(ct, cda);
		if (err < 0)
			return err;
	}
	if (cda[CTA_LABELS]) {
		err = ctnetlink_attach_labels(ct, cda);
		if (err < 0)
			return err;
	}
#if defined(CONFIG_NF_CONNTRACK_MARK)
	if (cda[CTA_MARK]) {
		ctnetlink_change_mark(ct, cda);
	}
#endif
	return 0;
}

static int
ctnetlink_glue_parse(const struct nlattr *attr, struct nf_conn *ct)
{
	struct nlattr *cda[CTA_MAX+1];
	int ret;

	ret = nla_parse_nested(cda, CTA_MAX, attr, ct_nla_policy, NULL);
	if (ret < 0)
		return ret;

	return ctnetlink_glue_parse_ct((const struct nlattr **)cda, ct);
}

static int ctnetlink_glue_exp_parse(const struct nlattr * const *cda,
				    const struct nf_conn *ct,
				    struct nf_conntrack_tuple *tuple,
				    struct nf_conntrack_tuple *mask)
{
	int err;

	err = ctnetlink_parse_tuple(cda, tuple, CTA_EXPECT_TUPLE,
				    nf_ct_l3num(ct), NULL);
	if (err < 0)
		return err;

	return ctnetlink_parse_tuple(cda, mask, CTA_EXPECT_MASK,
				     nf_ct_l3num(ct), NULL);
}

static int
ctnetlink_glue_attach_expect(const struct nlattr *attr, struct nf_conn *ct,
			     u32 portid, u32 report)
{
	struct nlattr *cda[CTA_EXPECT_MAX+1];
	struct nf_conntrack_tuple tuple, mask;
	struct nf_conntrack_helper *helper = NULL;
	struct nf_conntrack_expect *exp;
	int err;

	err = nla_parse_nested(cda, CTA_EXPECT_MAX, attr, exp_nla_policy,
			       NULL);
	if (err < 0)
		return err;

	err = ctnetlink_glue_exp_parse((const struct nlattr * const *)cda,
				       ct, &tuple, &mask);
	if (err < 0)
		return err;

	if (cda[CTA_EXPECT_HELP_NAME]) {
		const char *helpname = nla_data(cda[CTA_EXPECT_HELP_NAME]);

		helper = __nf_conntrack_helper_find(helpname, nf_ct_l3num(ct),
						    nf_ct_protonum(ct));
		if (helper == NULL)
			return -EOPNOTSUPP;
	}

	exp = ctnetlink_alloc_expect((const struct nlattr * const *)cda, ct,
				     helper, &tuple, &mask);
	if (IS_ERR(exp))
		return PTR_ERR(exp);

	err = nf_ct_expect_related_report(exp, portid, report);
	nf_ct_expect_put(exp);
	return err;
}

static void ctnetlink_glue_seqadj(struct sk_buff *skb, struct nf_conn *ct,
				  enum ip_conntrack_info ctinfo, int diff)
{
	if (!(ct->status & IPS_NAT_MASK))
		return;

	nf_ct_tcp_seqadj_set(skb, ct, ctinfo, diff);
}

static struct nfnl_ct_hook ctnetlink_glue_hook = {
	.get_ct		= ctnetlink_glue_get_ct,
	.build_size	= ctnetlink_glue_build_size,
	.build		= ctnetlink_glue_build,
	.parse		= ctnetlink_glue_parse,
	.attach_expect	= ctnetlink_glue_attach_expect,
	.seq_adjust	= ctnetlink_glue_seqadj,
};
#endif /* CONFIG_NETFILTER_NETLINK_GLUE_CT */

/***********************************************************************
 * EXPECT
 ***********************************************************************/

static int ctnetlink_exp_dump_tuple(struct sk_buff *skb,
				    const struct nf_conntrack_tuple *tuple,
				    u32 type)
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

static int ctnetlink_exp_dump_mask(struct sk_buff *skb,
				   const struct nf_conntrack_tuple *tuple,
				   const struct nf_conntrack_tuple_mask *mask)
{
	const struct nf_conntrack_l4proto *l4proto;
	struct nf_conntrack_tuple m;
	struct nlattr *nest_parms;
	int ret;

	memset(&m, 0xFF, sizeof(m));
	memcpy(&m.src.u3, &mask->src.u3, sizeof(m.src.u3));
	m.src.u.all = mask->src.u.all;
	m.dst.protonum = tuple->dst.protonum;

	nest_parms = nla_nest_start(skb, CTA_EXPECT_MASK | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;

	rcu_read_lock();
	ret = ctnetlink_dump_tuples_ip(skb, &m);
	if (ret >= 0) {
		l4proto = nf_ct_l4proto_find(tuple->dst.protonum);
		ret = ctnetlink_dump_tuples_proto(skb, &m, l4proto);
	}
	rcu_read_unlock();

	if (unlikely(ret < 0))
		goto nla_put_failure;

	nla_nest_end(skb, nest_parms);

	return 0;

nla_put_failure:
	return -1;
}

static const union nf_inet_addr any_addr;

static int
ctnetlink_exp_dump_expect(struct sk_buff *skb,
			  const struct nf_conntrack_expect *exp)
{
	struct nf_conn *master = exp->master;
	long timeout = ((long)exp->timeout.expires - (long)jiffies) / HZ;
	struct nf_conn_help *help;
#ifdef CONFIG_NF_NAT_NEEDED
	struct nlattr *nest_parms;
	struct nf_conntrack_tuple nat_tuple = {};
#endif
	struct nf_ct_helper_expectfn *expfn;

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

#ifdef CONFIG_NF_NAT_NEEDED
	if (!nf_inet_addr_cmp(&exp->saved_addr, &any_addr) ||
	    exp->saved_proto.all) {
		nest_parms = nla_nest_start(skb, CTA_EXPECT_NAT | NLA_F_NESTED);
		if (!nest_parms)
			goto nla_put_failure;

		if (nla_put_be32(skb, CTA_EXPECT_NAT_DIR, htonl(exp->dir)))
			goto nla_put_failure;

		nat_tuple.src.l3num = nf_ct_l3num(master);
		nat_tuple.src.u3 = exp->saved_addr;
		nat_tuple.dst.protonum = nf_ct_protonum(master);
		nat_tuple.src.u = exp->saved_proto;

		if (ctnetlink_exp_dump_tuple(skb, &nat_tuple,
						CTA_EXPECT_NAT_TUPLE) < 0)
	                goto nla_put_failure;
	        nla_nest_end(skb, nest_parms);
	}
#endif
	if (nla_put_be32(skb, CTA_EXPECT_TIMEOUT, htonl(timeout)) ||
	    nla_put_be32(skb, CTA_EXPECT_ID, htonl((unsigned long)exp)) ||
	    nla_put_be32(skb, CTA_EXPECT_FLAGS, htonl(exp->flags)) ||
	    nla_put_be32(skb, CTA_EXPECT_CLASS, htonl(exp->class)))
		goto nla_put_failure;
	help = nfct_help(master);
	if (help) {
		struct nf_conntrack_helper *helper;

		helper = rcu_dereference(help->helper);
		if (helper &&
		    nla_put_string(skb, CTA_EXPECT_HELP_NAME, helper->name))
			goto nla_put_failure;
	}
	expfn = nf_ct_helper_expectfn_find_by_symbol(exp->expectfn);
	if (expfn != NULL &&
	    nla_put_string(skb, CTA_EXPECT_FN, expfn->name))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static int
ctnetlink_exp_fill_info(struct sk_buff *skb, u32 portid, u32 seq,
			int event, const struct nf_conntrack_expect *exp)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	unsigned int flags = portid ? NLM_F_MULTI : 0;

	event = nfnl_msg_type(NFNL_SUBSYS_CTNETLINK_EXP, event);
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = exp->tuple.src.l3num;
	nfmsg->version	    = NFNETLINK_V0;
	nfmsg->res_id	    = 0;

	if (ctnetlink_exp_dump_expect(skb, exp) < 0)
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return skb->len;

nlmsg_failure:
nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -1;
}

#ifdef CONFIG_NF_CONNTRACK_EVENTS
static int
ctnetlink_expect_event(unsigned int events, struct nf_exp_event *item)
{
	struct nf_conntrack_expect *exp = item->exp;
	struct net *net = nf_ct_exp_net(exp);
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	struct sk_buff *skb;
	unsigned int type, group;
	int flags = 0;

	if (events & (1 << IPEXP_DESTROY)) {
		type = IPCTNL_MSG_EXP_DELETE;
		group = NFNLGRP_CONNTRACK_EXP_DESTROY;
	} else if (events & (1 << IPEXP_NEW)) {
		type = IPCTNL_MSG_EXP_NEW;
		flags = NLM_F_CREATE|NLM_F_EXCL;
		group = NFNLGRP_CONNTRACK_EXP_NEW;
	} else
		return 0;

	if (!item->report && !nfnetlink_has_listeners(net, group))
		return 0;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	type = nfnl_msg_type(NFNL_SUBSYS_CTNETLINK_EXP, type);
	nlh = nlmsg_put(skb, item->portid, 0, type, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = exp->tuple.src.l3num;
	nfmsg->version	    = NFNETLINK_V0;
	nfmsg->res_id	    = 0;

	if (ctnetlink_exp_dump_expect(skb, exp) < 0)
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	nfnetlink_send(skb, net, item->portid, group, item->report, GFP_ATOMIC);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
nlmsg_failure:
	kfree_skb(skb);
errout:
	nfnetlink_set_err(net, 0, 0, -ENOBUFS);
	return 0;
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
	struct net *net = sock_net(skb->sk);
	struct nf_conntrack_expect *exp, *last;
	struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	u_int8_t l3proto = nfmsg->nfgen_family;

	rcu_read_lock();
	last = (struct nf_conntrack_expect *)cb->args[1];
	for (; cb->args[0] < nf_ct_expect_hsize; cb->args[0]++) {
restart:
		hlist_for_each_entry_rcu(exp, &nf_ct_expect_hash[cb->args[0]],
					 hnode) {
			if (l3proto && exp->tuple.src.l3num != l3proto)
				continue;

			if (!net_eq(nf_ct_net(exp->master), net))
				continue;

			if (cb->args[1]) {
				if (exp != last)
					continue;
				cb->args[1] = 0;
			}
			if (ctnetlink_exp_fill_info(skb,
						    NETLINK_CB(cb->skb).portid,
						    cb->nlh->nlmsg_seq,
						    IPCTNL_MSG_EXP_NEW,
						    exp) < 0) {
				if (!refcount_inc_not_zero(&exp->use))
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

static int
ctnetlink_exp_ct_dump_table(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nf_conntrack_expect *exp, *last;
	struct nfgenmsg *nfmsg = nlmsg_data(cb->nlh);
	struct nf_conn *ct = cb->data;
	struct nf_conn_help *help = nfct_help(ct);
	u_int8_t l3proto = nfmsg->nfgen_family;

	if (cb->args[0])
		return 0;

	rcu_read_lock();
	last = (struct nf_conntrack_expect *)cb->args[1];
restart:
	hlist_for_each_entry_rcu(exp, &help->expectations, lnode) {
		if (l3proto && exp->tuple.src.l3num != l3proto)
			continue;
		if (cb->args[1]) {
			if (exp != last)
				continue;
			cb->args[1] = 0;
		}
		if (ctnetlink_exp_fill_info(skb, NETLINK_CB(cb->skb).portid,
					    cb->nlh->nlmsg_seq,
					    IPCTNL_MSG_EXP_NEW,
					    exp) < 0) {
			if (!refcount_inc_not_zero(&exp->use))
				continue;
			cb->args[1] = (unsigned long)exp;
			goto out;
		}
	}
	if (cb->args[1]) {
		cb->args[1] = 0;
		goto restart;
	}
	cb->args[0] = 1;
out:
	rcu_read_unlock();
	if (last)
		nf_ct_expect_put(last);

	return skb->len;
}

static int ctnetlink_dump_exp_ct(struct net *net, struct sock *ctnl,
				 struct sk_buff *skb,
				 const struct nlmsghdr *nlh,
				 const struct nlattr * const cda[],
				 struct netlink_ext_ack *extack)
{
	int err;
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct;
	struct nf_conntrack_zone zone;
	struct netlink_dump_control c = {
		.dump = ctnetlink_exp_ct_dump_table,
		.done = ctnetlink_exp_done,
	};

	err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_MASTER,
				    u3, NULL);
	if (err < 0)
		return err;

	err = ctnetlink_parse_zone(cda[CTA_EXPECT_ZONE], &zone);
	if (err < 0)
		return err;

	h = nf_conntrack_find_get(net, &zone, &tuple);
	if (!h)
		return -ENOENT;

	ct = nf_ct_tuplehash_to_ctrack(h);
	/* No expectation linked to this connection tracking. */
	if (!nfct_help(ct)) {
		nf_ct_put(ct);
		return 0;
	}

	c.data = ct;

	err = netlink_dump_start(ctnl, skb, nlh, &c);
	nf_ct_put(ct);

	return err;
}

static int ctnetlink_get_expect(struct net *net, struct sock *ctnl,
				struct sk_buff *skb, const struct nlmsghdr *nlh,
				const struct nlattr * const cda[],
				struct netlink_ext_ack *extack)
{
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_expect *exp;
	struct sk_buff *skb2;
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	struct nf_conntrack_zone zone;
	int err;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		if (cda[CTA_EXPECT_MASTER])
			return ctnetlink_dump_exp_ct(net, ctnl, skb, nlh, cda,
						     extack);
		else {
			struct netlink_dump_control c = {
				.dump = ctnetlink_exp_dump_table,
				.done = ctnetlink_exp_done,
			};
			return netlink_dump_start(ctnl, skb, nlh, &c);
		}
	}

	err = ctnetlink_parse_zone(cda[CTA_EXPECT_ZONE], &zone);
	if (err < 0)
		return err;

	if (cda[CTA_EXPECT_TUPLE])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_TUPLE,
					    u3, NULL);
	else if (cda[CTA_EXPECT_MASTER])
		err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_MASTER,
					    u3, NULL);
	else
		return -EINVAL;

	if (err < 0)
		return err;

	exp = nf_ct_expect_find_get(net, &zone, &tuple);
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
	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb2 == NULL) {
		nf_ct_expect_put(exp);
		goto out;
	}

	rcu_read_lock();
	err = ctnetlink_exp_fill_info(skb2, NETLINK_CB(skb).portid,
				      nlh->nlmsg_seq, IPCTNL_MSG_EXP_NEW, exp);
	rcu_read_unlock();
	nf_ct_expect_put(exp);
	if (err <= 0)
		goto free;

	err = netlink_unicast(ctnl, skb2, NETLINK_CB(skb).portid, MSG_DONTWAIT);
	if (err < 0)
		goto out;

	return 0;

free:
	kfree_skb(skb2);
out:
	/* this avoids a loop in nfnetlink. */
	return err == -EAGAIN ? -ENOBUFS : err;
}

static bool expect_iter_name(struct nf_conntrack_expect *exp, void *data)
{
	const struct nf_conn_help *m_help;
	const char *name = data;

	m_help = nfct_help(exp->master);

	return strcmp(m_help->helper->name, name) == 0;
}

static bool expect_iter_all(struct nf_conntrack_expect *exp, void *data)
{
	return true;
}

static int ctnetlink_del_expect(struct net *net, struct sock *ctnl,
				struct sk_buff *skb, const struct nlmsghdr *nlh,
				const struct nlattr * const cda[],
				struct netlink_ext_ack *extack)
{
	struct nf_conntrack_expect *exp;
	struct nf_conntrack_tuple tuple;
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	struct nf_conntrack_zone zone;
	int err;

	if (cda[CTA_EXPECT_TUPLE]) {
		/* delete a single expect by tuple */
		err = ctnetlink_parse_zone(cda[CTA_EXPECT_ZONE], &zone);
		if (err < 0)
			return err;

		err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_TUPLE,
					    u3, NULL);
		if (err < 0)
			return err;

		/* bump usage count to 2 */
		exp = nf_ct_expect_find_get(net, &zone, &tuple);
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
		spin_lock_bh(&nf_conntrack_expect_lock);
		if (del_timer(&exp->timeout)) {
			nf_ct_unlink_expect_report(exp, NETLINK_CB(skb).portid,
						   nlmsg_report(nlh));
			nf_ct_expect_put(exp);
		}
		spin_unlock_bh(&nf_conntrack_expect_lock);
		/* have to put what we 'get' above.
		 * after this line usage count == 0 */
		nf_ct_expect_put(exp);
	} else if (cda[CTA_EXPECT_HELP_NAME]) {
		char *name = nla_data(cda[CTA_EXPECT_HELP_NAME]);

		nf_ct_expect_iterate_net(net, expect_iter_name, name,
					 NETLINK_CB(skb).portid,
					 nlmsg_report(nlh));
	} else {
		/* This basically means we have to flush everything*/
		nf_ct_expect_iterate_net(net, expect_iter_all, NULL,
					 NETLINK_CB(skb).portid,
					 nlmsg_report(nlh));
	}

	return 0;
}
static int
ctnetlink_change_expect(struct nf_conntrack_expect *x,
			const struct nlattr * const cda[])
{
	if (cda[CTA_EXPECT_TIMEOUT]) {
		if (!del_timer(&x->timeout))
			return -ETIME;

		x->timeout.expires = jiffies +
			ntohl(nla_get_be32(cda[CTA_EXPECT_TIMEOUT])) * HZ;
		add_timer(&x->timeout);
	}
	return 0;
}

static const struct nla_policy exp_nat_nla_policy[CTA_EXPECT_NAT_MAX+1] = {
	[CTA_EXPECT_NAT_DIR]	= { .type = NLA_U32 },
	[CTA_EXPECT_NAT_TUPLE]	= { .type = NLA_NESTED },
};

static int
ctnetlink_parse_expect_nat(const struct nlattr *attr,
			   struct nf_conntrack_expect *exp,
			   u_int8_t u3)
{
#ifdef CONFIG_NF_NAT_NEEDED
	struct nlattr *tb[CTA_EXPECT_NAT_MAX+1];
	struct nf_conntrack_tuple nat_tuple = {};
	int err;

	err = nla_parse_nested(tb, CTA_EXPECT_NAT_MAX, attr,
			       exp_nat_nla_policy, NULL);
	if (err < 0)
		return err;

	if (!tb[CTA_EXPECT_NAT_DIR] || !tb[CTA_EXPECT_NAT_TUPLE])
		return -EINVAL;

	err = ctnetlink_parse_tuple((const struct nlattr * const *)tb,
				    &nat_tuple, CTA_EXPECT_NAT_TUPLE,
				    u3, NULL);
	if (err < 0)
		return err;

	exp->saved_addr = nat_tuple.src.u3;
	exp->saved_proto = nat_tuple.src.u;
	exp->dir = ntohl(nla_get_be32(tb[CTA_EXPECT_NAT_DIR]));

	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static struct nf_conntrack_expect *
ctnetlink_alloc_expect(const struct nlattr * const cda[], struct nf_conn *ct,
		       struct nf_conntrack_helper *helper,
		       struct nf_conntrack_tuple *tuple,
		       struct nf_conntrack_tuple *mask)
{
	u_int32_t class = 0;
	struct nf_conntrack_expect *exp;
	struct nf_conn_help *help;
	int err;

	help = nfct_help(ct);
	if (!help)
		return ERR_PTR(-EOPNOTSUPP);

	if (cda[CTA_EXPECT_CLASS] && helper) {
		class = ntohl(nla_get_be32(cda[CTA_EXPECT_CLASS]));
		if (class > helper->expect_class_max)
			return ERR_PTR(-EINVAL);
	}
	exp = nf_ct_expect_alloc(ct);
	if (!exp)
		return ERR_PTR(-ENOMEM);

	if (cda[CTA_EXPECT_FLAGS]) {
		exp->flags = ntohl(nla_get_be32(cda[CTA_EXPECT_FLAGS]));
		exp->flags &= ~NF_CT_EXPECT_USERSPACE;
	} else {
		exp->flags = 0;
	}
	if (cda[CTA_EXPECT_FN]) {
		const char *name = nla_data(cda[CTA_EXPECT_FN]);
		struct nf_ct_helper_expectfn *expfn;

		expfn = nf_ct_helper_expectfn_find_by_name(name);
		if (expfn == NULL) {
			err = -EINVAL;
			goto err_out;
		}
		exp->expectfn = expfn->expectfn;
	} else
		exp->expectfn = NULL;

	exp->class = class;
	exp->master = ct;
	exp->helper = helper;
	exp->tuple = *tuple;
	exp->mask.src.u3 = mask->src.u3;
	exp->mask.src.u.all = mask->src.u.all;

	if (cda[CTA_EXPECT_NAT]) {
		err = ctnetlink_parse_expect_nat(cda[CTA_EXPECT_NAT],
						 exp, nf_ct_l3num(ct));
		if (err < 0)
			goto err_out;
	}
	return exp;
err_out:
	nf_ct_expect_put(exp);
	return ERR_PTR(err);
}

static int
ctnetlink_create_expect(struct net *net,
			const struct nf_conntrack_zone *zone,
			const struct nlattr * const cda[],
			u_int8_t u3, u32 portid, int report)
{
	struct nf_conntrack_tuple tuple, mask, master_tuple;
	struct nf_conntrack_tuple_hash *h = NULL;
	struct nf_conntrack_helper *helper = NULL;
	struct nf_conntrack_expect *exp;
	struct nf_conn *ct;
	int err;

	/* caller guarantees that those three CTA_EXPECT_* exist */
	err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_TUPLE,
				    u3, NULL);
	if (err < 0)
		return err;
	err = ctnetlink_parse_tuple(cda, &mask, CTA_EXPECT_MASK,
				    u3, NULL);
	if (err < 0)
		return err;
	err = ctnetlink_parse_tuple(cda, &master_tuple, CTA_EXPECT_MASTER,
				    u3, NULL);
	if (err < 0)
		return err;

	/* Look for master conntrack of this expectation */
	h = nf_conntrack_find_get(net, zone, &master_tuple);
	if (!h)
		return -ENOENT;
	ct = nf_ct_tuplehash_to_ctrack(h);

	rcu_read_lock();
	if (cda[CTA_EXPECT_HELP_NAME]) {
		const char *helpname = nla_data(cda[CTA_EXPECT_HELP_NAME]);

		helper = __nf_conntrack_helper_find(helpname, u3,
						    nf_ct_protonum(ct));
		if (helper == NULL) {
			rcu_read_unlock();
#ifdef CONFIG_MODULES
			if (request_module("nfct-helper-%s", helpname) < 0) {
				err = -EOPNOTSUPP;
				goto err_ct;
			}
			rcu_read_lock();
			helper = __nf_conntrack_helper_find(helpname, u3,
							    nf_ct_protonum(ct));
			if (helper) {
				err = -EAGAIN;
				goto err_rcu;
			}
			rcu_read_unlock();
#endif
			err = -EOPNOTSUPP;
			goto err_ct;
		}
	}

	exp = ctnetlink_alloc_expect(cda, ct, helper, &tuple, &mask);
	if (IS_ERR(exp)) {
		err = PTR_ERR(exp);
		goto err_rcu;
	}

	err = nf_ct_expect_related_report(exp, portid, report);
	nf_ct_expect_put(exp);
err_rcu:
	rcu_read_unlock();
err_ct:
	nf_ct_put(ct);
	return err;
}

static int ctnetlink_new_expect(struct net *net, struct sock *ctnl,
				struct sk_buff *skb, const struct nlmsghdr *nlh,
				const struct nlattr * const cda[],
				struct netlink_ext_ack *extack)
{
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_expect *exp;
	struct nfgenmsg *nfmsg = nlmsg_data(nlh);
	u_int8_t u3 = nfmsg->nfgen_family;
	struct nf_conntrack_zone zone;
	int err;

	if (!cda[CTA_EXPECT_TUPLE]
	    || !cda[CTA_EXPECT_MASK]
	    || !cda[CTA_EXPECT_MASTER])
		return -EINVAL;

	err = ctnetlink_parse_zone(cda[CTA_EXPECT_ZONE], &zone);
	if (err < 0)
		return err;

	err = ctnetlink_parse_tuple(cda, &tuple, CTA_EXPECT_TUPLE,
				    u3, NULL);
	if (err < 0)
		return err;

	spin_lock_bh(&nf_conntrack_expect_lock);
	exp = __nf_ct_expect_find(net, &zone, &tuple);
	if (!exp) {
		spin_unlock_bh(&nf_conntrack_expect_lock);
		err = -ENOENT;
		if (nlh->nlmsg_flags & NLM_F_CREATE) {
			err = ctnetlink_create_expect(net, &zone, cda, u3,
						      NETLINK_CB(skb).portid,
						      nlmsg_report(nlh));
		}
		return err;
	}

	err = -EEXIST;
	if (!(nlh->nlmsg_flags & NLM_F_EXCL))
		err = ctnetlink_change_expect(exp, cda);
	spin_unlock_bh(&nf_conntrack_expect_lock);

	return err;
}

static int
ctnetlink_exp_stat_fill_info(struct sk_buff *skb, u32 portid, u32 seq, int cpu,
			     const struct ip_conntrack_stat *st)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	unsigned int flags = portid ? NLM_F_MULTI : 0, event;

	event = nfnl_msg_type(NFNL_SUBSYS_CTNETLINK,
			      IPCTNL_MSG_EXP_GET_STATS_CPU);
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = AF_UNSPEC;
	nfmsg->version      = NFNETLINK_V0;
	nfmsg->res_id	    = htons(cpu);

	if (nla_put_be32(skb, CTA_STATS_EXP_NEW, htonl(st->expect_new)) ||
	    nla_put_be32(skb, CTA_STATS_EXP_CREATE, htonl(st->expect_create)) ||
	    nla_put_be32(skb, CTA_STATS_EXP_DELETE, htonl(st->expect_delete)))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return skb->len;

nla_put_failure:
nlmsg_failure:
	nlmsg_cancel(skb, nlh);
	return -1;
}

static int
ctnetlink_exp_stat_cpu_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int cpu;
	struct net *net = sock_net(skb->sk);

	if (cb->args[0] == nr_cpu_ids)
		return 0;

	for (cpu = cb->args[0]; cpu < nr_cpu_ids; cpu++) {
		const struct ip_conntrack_stat *st;

		if (!cpu_possible(cpu))
			continue;

		st = per_cpu_ptr(net->ct.stat, cpu);
		if (ctnetlink_exp_stat_fill_info(skb, NETLINK_CB(cb->skb).portid,
						 cb->nlh->nlmsg_seq,
						 cpu, st) < 0)
			break;
	}
	cb->args[0] = cpu;

	return skb->len;
}

static int ctnetlink_stat_exp_cpu(struct net *net, struct sock *ctnl,
				  struct sk_buff *skb,
				  const struct nlmsghdr *nlh,
				  const struct nlattr * const cda[],
				  struct netlink_ext_ack *extack)
{
	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = ctnetlink_exp_stat_cpu_dump,
		};
		return netlink_dump_start(ctnl, skb, nlh, &c);
	}

	return 0;
}

#ifdef CONFIG_NF_CONNTRACK_EVENTS
static struct nf_ct_event_notifier ctnl_notifier = {
	.fcn = ctnetlink_conntrack_event,
};

static struct nf_exp_event_notifier ctnl_notifier_exp = {
	.fcn = ctnetlink_expect_event,
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
	[IPCTNL_MSG_CT_GET_STATS_CPU]	= { .call = ctnetlink_stat_ct_cpu },
	[IPCTNL_MSG_CT_GET_STATS]	= { .call = ctnetlink_stat_ct },
	[IPCTNL_MSG_CT_GET_DYING]	= { .call = ctnetlink_get_ct_dying },
	[IPCTNL_MSG_CT_GET_UNCONFIRMED]	= { .call = ctnetlink_get_ct_unconfirmed },
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
	[IPCTNL_MSG_EXP_GET_STATS_CPU]	= { .call = ctnetlink_stat_exp_cpu },
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

static int __net_init ctnetlink_net_init(struct net *net)
{
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	int ret;

	ret = nf_conntrack_register_notifier(net, &ctnl_notifier);
	if (ret < 0) {
		pr_err("ctnetlink_init: cannot register notifier.\n");
		goto err_out;
	}

	ret = nf_ct_expect_register_notifier(net, &ctnl_notifier_exp);
	if (ret < 0) {
		pr_err("ctnetlink_init: cannot expect register notifier.\n");
		goto err_unreg_notifier;
	}
#endif
	return 0;

#ifdef CONFIG_NF_CONNTRACK_EVENTS
err_unreg_notifier:
	nf_conntrack_unregister_notifier(net, &ctnl_notifier);
err_out:
	return ret;
#endif
}

static void ctnetlink_net_exit(struct net *net)
{
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	nf_ct_expect_unregister_notifier(net, &ctnl_notifier_exp);
	nf_conntrack_unregister_notifier(net, &ctnl_notifier);
#endif
}

static void __net_exit ctnetlink_net_exit_batch(struct list_head *net_exit_list)
{
	struct net *net;

	list_for_each_entry(net, net_exit_list, exit_list)
		ctnetlink_net_exit(net);
}

static struct pernet_operations ctnetlink_net_ops = {
	.init		= ctnetlink_net_init,
	.exit_batch	= ctnetlink_net_exit_batch,
};

static int __init ctnetlink_init(void)
{
	int ret;

	ret = nfnetlink_subsys_register(&ctnl_subsys);
	if (ret < 0) {
		pr_err("ctnetlink_init: cannot register with nfnetlink.\n");
		goto err_out;
	}

	ret = nfnetlink_subsys_register(&ctnl_exp_subsys);
	if (ret < 0) {
		pr_err("ctnetlink_init: cannot register exp with nfnetlink.\n");
		goto err_unreg_subsys;
	}

	ret = register_pernet_subsys(&ctnetlink_net_ops);
	if (ret < 0) {
		pr_err("ctnetlink_init: cannot register pernet operations\n");
		goto err_unreg_exp_subsys;
	}
#ifdef CONFIG_NETFILTER_NETLINK_GLUE_CT
	/* setup interaction between nf_queue and nf_conntrack_netlink. */
	RCU_INIT_POINTER(nfnl_ct_hook, &ctnetlink_glue_hook);
#endif
	return 0;

err_unreg_exp_subsys:
	nfnetlink_subsys_unregister(&ctnl_exp_subsys);
err_unreg_subsys:
	nfnetlink_subsys_unregister(&ctnl_subsys);
err_out:
	return ret;
}

static void __exit ctnetlink_exit(void)
{
	unregister_pernet_subsys(&ctnetlink_net_ops);
	nfnetlink_subsys_unregister(&ctnl_exp_subsys);
	nfnetlink_subsys_unregister(&ctnl_subsys);
#ifdef CONFIG_NETFILTER_NETLINK_GLUE_CT
	RCU_INIT_POINTER(nfnl_ct_hook, NULL);
#endif
	synchronize_rcu();
}

module_init(ctnetlink_init);
module_exit(ctnetlink_exit);
