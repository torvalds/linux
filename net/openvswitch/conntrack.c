// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 Nicira, Inc.
 */

#include <linux/module.h>
#include <linux/openvswitch.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/sctp.h>
#include <linux/static_key.h>
#include <net/ip.h>
#include <net/genetlink.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_count.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_labels.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_timeout.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>
#include <net/ipv6_frag.h>

#if IS_ENABLED(CONFIG_NF_NAT)
#include <net/netfilter/nf_nat.h>
#endif

#include "datapath.h"
#include "conntrack.h"
#include "flow.h"
#include "flow_netlink.h"

struct ovs_ct_len_tbl {
	int maxlen;
	int minlen;
};

/* Metadata mark for masked write to conntrack mark */
struct md_mark {
	u32 value;
	u32 mask;
};

/* Metadata label for masked write to conntrack label. */
struct md_labels {
	struct ovs_key_ct_labels value;
	struct ovs_key_ct_labels mask;
};

enum ovs_ct_nat {
	OVS_CT_NAT = 1 << 0,     /* NAT for committed connections only. */
	OVS_CT_SRC_NAT = 1 << 1, /* Source NAT for NEW connections. */
	OVS_CT_DST_NAT = 1 << 2, /* Destination NAT for NEW connections. */
};

/* Conntrack action context for execution. */
struct ovs_conntrack_info {
	struct nf_conntrack_helper *helper;
	struct nf_conntrack_zone zone;
	struct nf_conn *ct;
	u8 commit : 1;
	u8 nat : 3;                 /* enum ovs_ct_nat */
	u8 force : 1;
	u8 have_eventmask : 1;
	u16 family;
	u32 eventmask;              /* Mask of 1 << IPCT_*. */
	struct md_mark mark;
	struct md_labels labels;
	char timeout[CTNL_TIMEOUT_NAME_MAX];
	struct nf_ct_timeout *nf_ct_timeout;
#if IS_ENABLED(CONFIG_NF_NAT)
	struct nf_nat_range2 range;  /* Only present for SRC NAT and DST NAT. */
#endif
};

#if	IS_ENABLED(CONFIG_NETFILTER_CONNCOUNT)
#define OVS_CT_LIMIT_UNLIMITED	0
#define OVS_CT_LIMIT_DEFAULT OVS_CT_LIMIT_UNLIMITED
#define CT_LIMIT_HASH_BUCKETS 512
static DEFINE_STATIC_KEY_FALSE(ovs_ct_limit_enabled);

struct ovs_ct_limit {
	/* Elements in ovs_ct_limit_info->limits hash table */
	struct hlist_node hlist_node;
	struct rcu_head rcu;
	u16 zone;
	u32 limit;
};

struct ovs_ct_limit_info {
	u32 default_limit;
	struct hlist_head *limits;
	struct nf_conncount_data *data;
};

static const struct nla_policy ct_limit_policy[OVS_CT_LIMIT_ATTR_MAX + 1] = {
	[OVS_CT_LIMIT_ATTR_ZONE_LIMIT] = { .type = NLA_NESTED, },
};
#endif

static bool labels_nonzero(const struct ovs_key_ct_labels *labels);

static void __ovs_ct_free_action(struct ovs_conntrack_info *ct_info);

static u16 key_to_nfproto(const struct sw_flow_key *key)
{
	switch (ntohs(key->eth.type)) {
	case ETH_P_IP:
		return NFPROTO_IPV4;
	case ETH_P_IPV6:
		return NFPROTO_IPV6;
	default:
		return NFPROTO_UNSPEC;
	}
}

/* Map SKB connection state into the values used by flow definition. */
static u8 ovs_ct_get_state(enum ip_conntrack_info ctinfo)
{
	u8 ct_state = OVS_CS_F_TRACKED;

	switch (ctinfo) {
	case IP_CT_ESTABLISHED_REPLY:
	case IP_CT_RELATED_REPLY:
		ct_state |= OVS_CS_F_REPLY_DIR;
		break;
	default:
		break;
	}

	switch (ctinfo) {
	case IP_CT_ESTABLISHED:
	case IP_CT_ESTABLISHED_REPLY:
		ct_state |= OVS_CS_F_ESTABLISHED;
		break;
	case IP_CT_RELATED:
	case IP_CT_RELATED_REPLY:
		ct_state |= OVS_CS_F_RELATED;
		break;
	case IP_CT_NEW:
		ct_state |= OVS_CS_F_NEW;
		break;
	default:
		break;
	}

	return ct_state;
}

static u32 ovs_ct_get_mark(const struct nf_conn *ct)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK_MARK)
	return ct ? ct->mark : 0;
#else
	return 0;
#endif
}

/* Guard against conntrack labels max size shrinking below 128 bits. */
#if NF_CT_LABELS_MAX_SIZE < 16
#error NF_CT_LABELS_MAX_SIZE must be at least 16 bytes
#endif

static void ovs_ct_get_labels(const struct nf_conn *ct,
			      struct ovs_key_ct_labels *labels)
{
	struct nf_conn_labels *cl = ct ? nf_ct_labels_find(ct) : NULL;

	if (cl)
		memcpy(labels, cl->bits, OVS_CT_LABELS_LEN);
	else
		memset(labels, 0, OVS_CT_LABELS_LEN);
}

static void __ovs_ct_update_key_orig_tp(struct sw_flow_key *key,
					const struct nf_conntrack_tuple *orig,
					u8 icmp_proto)
{
	key->ct_orig_proto = orig->dst.protonum;
	if (orig->dst.protonum == icmp_proto) {
		key->ct.orig_tp.src = htons(orig->dst.u.icmp.type);
		key->ct.orig_tp.dst = htons(orig->dst.u.icmp.code);
	} else {
		key->ct.orig_tp.src = orig->src.u.all;
		key->ct.orig_tp.dst = orig->dst.u.all;
	}
}

static void __ovs_ct_update_key(struct sw_flow_key *key, u8 state,
				const struct nf_conntrack_zone *zone,
				const struct nf_conn *ct)
{
	key->ct_state = state;
	key->ct_zone = zone->id;
	key->ct.mark = ovs_ct_get_mark(ct);
	ovs_ct_get_labels(ct, &key->ct.labels);

	if (ct) {
		const struct nf_conntrack_tuple *orig;

		/* Use the master if we have one. */
		if (ct->master)
			ct = ct->master;
		orig = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;

		/* IP version must match with the master connection. */
		if (key->eth.type == htons(ETH_P_IP) &&
		    nf_ct_l3num(ct) == NFPROTO_IPV4) {
			key->ipv4.ct_orig.src = orig->src.u3.ip;
			key->ipv4.ct_orig.dst = orig->dst.u3.ip;
			__ovs_ct_update_key_orig_tp(key, orig, IPPROTO_ICMP);
			return;
		} else if (key->eth.type == htons(ETH_P_IPV6) &&
			   !sw_flow_key_is_nd(key) &&
			   nf_ct_l3num(ct) == NFPROTO_IPV6) {
			key->ipv6.ct_orig.src = orig->src.u3.in6;
			key->ipv6.ct_orig.dst = orig->dst.u3.in6;
			__ovs_ct_update_key_orig_tp(key, orig, NEXTHDR_ICMP);
			return;
		}
	}
	/* Clear 'ct_orig_proto' to mark the non-existence of conntrack
	 * original direction key fields.
	 */
	key->ct_orig_proto = 0;
}

/* Update 'key' based on skb->_nfct.  If 'post_ct' is true, then OVS has
 * previously sent the packet to conntrack via the ct action.  If
 * 'keep_nat_flags' is true, the existing NAT flags retained, else they are
 * initialized from the connection status.
 */
static void ovs_ct_update_key(const struct sk_buff *skb,
			      const struct ovs_conntrack_info *info,
			      struct sw_flow_key *key, bool post_ct,
			      bool keep_nat_flags)
{
	const struct nf_conntrack_zone *zone = &nf_ct_zone_dflt;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	u8 state = 0;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct) {
		state = ovs_ct_get_state(ctinfo);
		/* All unconfirmed entries are NEW connections. */
		if (!nf_ct_is_confirmed(ct))
			state |= OVS_CS_F_NEW;
		/* OVS persists the related flag for the duration of the
		 * connection.
		 */
		if (ct->master)
			state |= OVS_CS_F_RELATED;
		if (keep_nat_flags) {
			state |= key->ct_state & OVS_CS_F_NAT_MASK;
		} else {
			if (ct->status & IPS_SRC_NAT)
				state |= OVS_CS_F_SRC_NAT;
			if (ct->status & IPS_DST_NAT)
				state |= OVS_CS_F_DST_NAT;
		}
		zone = nf_ct_zone(ct);
	} else if (post_ct) {
		state = OVS_CS_F_TRACKED | OVS_CS_F_INVALID;
		if (info)
			zone = &info->zone;
	}
	__ovs_ct_update_key(key, state, zone, ct);
}

/* This is called to initialize CT key fields possibly coming in from the local
 * stack.
 */
void ovs_ct_fill_key(const struct sk_buff *skb,
		     struct sw_flow_key *key,
		     bool post_ct)
{
	ovs_ct_update_key(skb, NULL, key, post_ct, false);
}

int ovs_ct_put_key(const struct sw_flow_key *swkey,
		   const struct sw_flow_key *output, struct sk_buff *skb)
{
	if (nla_put_u32(skb, OVS_KEY_ATTR_CT_STATE, output->ct_state))
		return -EMSGSIZE;

	if (IS_ENABLED(CONFIG_NF_CONNTRACK_ZONES) &&
	    nla_put_u16(skb, OVS_KEY_ATTR_CT_ZONE, output->ct_zone))
		return -EMSGSIZE;

	if (IS_ENABLED(CONFIG_NF_CONNTRACK_MARK) &&
	    nla_put_u32(skb, OVS_KEY_ATTR_CT_MARK, output->ct.mark))
		return -EMSGSIZE;

	if (IS_ENABLED(CONFIG_NF_CONNTRACK_LABELS) &&
	    nla_put(skb, OVS_KEY_ATTR_CT_LABELS, sizeof(output->ct.labels),
		    &output->ct.labels))
		return -EMSGSIZE;

	if (swkey->ct_orig_proto) {
		if (swkey->eth.type == htons(ETH_P_IP)) {
			struct ovs_key_ct_tuple_ipv4 orig;

			memset(&orig, 0, sizeof(orig));
			orig.ipv4_src = output->ipv4.ct_orig.src;
			orig.ipv4_dst = output->ipv4.ct_orig.dst;
			orig.src_port = output->ct.orig_tp.src;
			orig.dst_port = output->ct.orig_tp.dst;
			orig.ipv4_proto = output->ct_orig_proto;

			if (nla_put(skb, OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV4,
				    sizeof(orig), &orig))
				return -EMSGSIZE;
		} else if (swkey->eth.type == htons(ETH_P_IPV6)) {
			struct ovs_key_ct_tuple_ipv6 orig;

			memset(&orig, 0, sizeof(orig));
			memcpy(orig.ipv6_src, output->ipv6.ct_orig.src.s6_addr32,
			       sizeof(orig.ipv6_src));
			memcpy(orig.ipv6_dst, output->ipv6.ct_orig.dst.s6_addr32,
			       sizeof(orig.ipv6_dst));
			orig.src_port = output->ct.orig_tp.src;
			orig.dst_port = output->ct.orig_tp.dst;
			orig.ipv6_proto = output->ct_orig_proto;

			if (nla_put(skb, OVS_KEY_ATTR_CT_ORIG_TUPLE_IPV6,
				    sizeof(orig), &orig))
				return -EMSGSIZE;
		}
	}

	return 0;
}

static int ovs_ct_set_mark(struct nf_conn *ct, struct sw_flow_key *key,
			   u32 ct_mark, u32 mask)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK_MARK)
	u32 new_mark;

	new_mark = ct_mark | (ct->mark & ~(mask));
	if (ct->mark != new_mark) {
		ct->mark = new_mark;
		if (nf_ct_is_confirmed(ct))
			nf_conntrack_event_cache(IPCT_MARK, ct);
		key->ct.mark = new_mark;
	}

	return 0;
#else
	return -ENOTSUPP;
#endif
}

static struct nf_conn_labels *ovs_ct_get_conn_labels(struct nf_conn *ct)
{
	struct nf_conn_labels *cl;

	cl = nf_ct_labels_find(ct);
	if (!cl) {
		nf_ct_labels_ext_add(ct);
		cl = nf_ct_labels_find(ct);
	}

	return cl;
}

/* Initialize labels for a new, yet to be committed conntrack entry.  Note that
 * since the new connection is not yet confirmed, and thus no-one else has
 * access to it's labels, we simply write them over.
 */
static int ovs_ct_init_labels(struct nf_conn *ct, struct sw_flow_key *key,
			      const struct ovs_key_ct_labels *labels,
			      const struct ovs_key_ct_labels *mask)
{
	struct nf_conn_labels *cl, *master_cl;
	bool have_mask = labels_nonzero(mask);

	/* Inherit master's labels to the related connection? */
	master_cl = ct->master ? nf_ct_labels_find(ct->master) : NULL;

	if (!master_cl && !have_mask)
		return 0;   /* Nothing to do. */

	cl = ovs_ct_get_conn_labels(ct);
	if (!cl)
		return -ENOSPC;

	/* Inherit the master's labels, if any. */
	if (master_cl)
		*cl = *master_cl;

	if (have_mask) {
		u32 *dst = (u32 *)cl->bits;
		int i;

		for (i = 0; i < OVS_CT_LABELS_LEN_32; i++)
			dst[i] = (dst[i] & ~mask->ct_labels_32[i]) |
				(labels->ct_labels_32[i]
				 & mask->ct_labels_32[i]);
	}

	/* Labels are included in the IPCTNL_MSG_CT_NEW event only if the
	 * IPCT_LABEL bit is set in the event cache.
	 */
	nf_conntrack_event_cache(IPCT_LABEL, ct);

	memcpy(&key->ct.labels, cl->bits, OVS_CT_LABELS_LEN);

	return 0;
}

static int ovs_ct_set_labels(struct nf_conn *ct, struct sw_flow_key *key,
			     const struct ovs_key_ct_labels *labels,
			     const struct ovs_key_ct_labels *mask)
{
	struct nf_conn_labels *cl;
	int err;

	cl = ovs_ct_get_conn_labels(ct);
	if (!cl)
		return -ENOSPC;

	err = nf_connlabels_replace(ct, labels->ct_labels_32,
				    mask->ct_labels_32,
				    OVS_CT_LABELS_LEN_32);
	if (err)
		return err;

	memcpy(&key->ct.labels, cl->bits, OVS_CT_LABELS_LEN);

	return 0;
}

/* 'skb' should already be pulled to nh_ofs. */
static int ovs_ct_helper(struct sk_buff *skb, u16 proto)
{
	const struct nf_conntrack_helper *helper;
	const struct nf_conn_help *help;
	enum ip_conntrack_info ctinfo;
	unsigned int protoff;
	struct nf_conn *ct;
	int err;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct || ctinfo == IP_CT_RELATED_REPLY)
		return NF_ACCEPT;

	help = nfct_help(ct);
	if (!help)
		return NF_ACCEPT;

	helper = rcu_dereference(help->helper);
	if (!helper)
		return NF_ACCEPT;

	switch (proto) {
	case NFPROTO_IPV4:
		protoff = ip_hdrlen(skb);
		break;
	case NFPROTO_IPV6: {
		u8 nexthdr = ipv6_hdr(skb)->nexthdr;
		__be16 frag_off;
		int ofs;

		ofs = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &nexthdr,
				       &frag_off);
		if (ofs < 0 || (frag_off & htons(~0x7)) != 0) {
			pr_debug("proto header not found\n");
			return NF_ACCEPT;
		}
		protoff = ofs;
		break;
	}
	default:
		WARN_ONCE(1, "helper invoked on non-IP family!");
		return NF_DROP;
	}

	err = helper->help(skb, protoff, ct, ctinfo);
	if (err != NF_ACCEPT)
		return err;

	/* Adjust seqs after helper.  This is needed due to some helpers (e.g.,
	 * FTP with NAT) adusting the TCP payload size when mangling IP
	 * addresses and/or port numbers in the text-based control connection.
	 */
	if (test_bit(IPS_SEQ_ADJUST_BIT, &ct->status) &&
	    !nf_ct_seq_adjust(skb, ct, ctinfo, protoff))
		return NF_DROP;
	return NF_ACCEPT;
}

/* Returns 0 on success, -EINPROGRESS if 'skb' is stolen, or other nonzero
 * value if 'skb' is freed.
 */
static int handle_fragments(struct net *net, struct sw_flow_key *key,
			    u16 zone, struct sk_buff *skb)
{
	struct ovs_skb_cb ovs_cb = *OVS_CB(skb);
	int err;

	if (key->eth.type == htons(ETH_P_IP)) {
		enum ip_defrag_users user = IP_DEFRAG_CONNTRACK_IN + zone;

		memset(IPCB(skb), 0, sizeof(struct inet_skb_parm));
		err = ip_defrag(net, skb, user);
		if (err)
			return err;

		ovs_cb.mru = IPCB(skb)->frag_max_size;
#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV6)
	} else if (key->eth.type == htons(ETH_P_IPV6)) {
		enum ip6_defrag_users user = IP6_DEFRAG_CONNTRACK_IN + zone;

		memset(IP6CB(skb), 0, sizeof(struct inet6_skb_parm));
		err = nf_ct_frag6_gather(net, skb, user);
		if (err) {
			if (err != -EINPROGRESS)
				kfree_skb(skb);
			return err;
		}

		key->ip.proto = ipv6_hdr(skb)->nexthdr;
		ovs_cb.mru = IP6CB(skb)->frag_max_size;
#endif
	} else {
		kfree_skb(skb);
		return -EPFNOSUPPORT;
	}

	/* The key extracted from the fragment that completed this datagram
	 * likely didn't have an L4 header, so regenerate it.
	 */
	ovs_flow_key_update_l3l4(skb, key);

	key->ip.frag = OVS_FRAG_TYPE_NONE;
	skb_clear_hash(skb);
	skb->ignore_df = 1;
	*OVS_CB(skb) = ovs_cb;

	return 0;
}

static struct nf_conntrack_expect *
ovs_ct_expect_find(struct net *net, const struct nf_conntrack_zone *zone,
		   u16 proto, const struct sk_buff *skb)
{
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_expect *exp;

	if (!nf_ct_get_tuplepr(skb, skb_network_offset(skb), proto, net, &tuple))
		return NULL;

	exp = __nf_ct_expect_find(net, zone, &tuple);
	if (exp) {
		struct nf_conntrack_tuple_hash *h;

		/* Delete existing conntrack entry, if it clashes with the
		 * expectation.  This can happen since conntrack ALGs do not
		 * check for clashes between (new) expectations and existing
		 * conntrack entries.  nf_conntrack_in() will check the
		 * expectations only if a conntrack entry can not be found,
		 * which can lead to OVS finding the expectation (here) in the
		 * init direction, but which will not be removed by the
		 * nf_conntrack_in() call, if a matching conntrack entry is
		 * found instead.  In this case all init direction packets
		 * would be reported as new related packets, while reply
		 * direction packets would be reported as un-related
		 * established packets.
		 */
		h = nf_conntrack_find_get(net, zone, &tuple);
		if (h) {
			struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);

			nf_ct_delete(ct, 0, 0);
			nf_ct_put(ct);
		}
	}

	return exp;
}

/* This replicates logic from nf_conntrack_core.c that is not exported. */
static enum ip_conntrack_info
ovs_ct_get_info(const struct nf_conntrack_tuple_hash *h)
{
	const struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);

	if (NF_CT_DIRECTION(h) == IP_CT_DIR_REPLY)
		return IP_CT_ESTABLISHED_REPLY;
	/* Once we've had two way comms, always ESTABLISHED. */
	if (test_bit(IPS_SEEN_REPLY_BIT, &ct->status))
		return IP_CT_ESTABLISHED;
	if (test_bit(IPS_EXPECTED_BIT, &ct->status))
		return IP_CT_RELATED;
	return IP_CT_NEW;
}

/* Find an existing connection which this packet belongs to without
 * re-attributing statistics or modifying the connection state.  This allows an
 * skb->_nfct lost due to an upcall to be recovered during actions execution.
 *
 * Must be called with rcu_read_lock.
 *
 * On success, populates skb->_nfct and returns the connection.  Returns NULL
 * if there is no existing entry.
 */
static struct nf_conn *
ovs_ct_find_existing(struct net *net, const struct nf_conntrack_zone *zone,
		     u8 l3num, struct sk_buff *skb, bool natted)
{
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_tuple_hash *h;
	struct nf_conn *ct;

	if (!nf_ct_get_tuplepr(skb, skb_network_offset(skb), l3num,
			       net, &tuple)) {
		pr_debug("ovs_ct_find_existing: Can't get tuple\n");
		return NULL;
	}

	/* Must invert the tuple if skb has been transformed by NAT. */
	if (natted) {
		struct nf_conntrack_tuple inverse;

		if (!nf_ct_invert_tuple(&inverse, &tuple)) {
			pr_debug("ovs_ct_find_existing: Inversion failed!\n");
			return NULL;
		}
		tuple = inverse;
	}

	/* look for tuple match */
	h = nf_conntrack_find_get(net, zone, &tuple);
	if (!h)
		return NULL;   /* Not found. */

	ct = nf_ct_tuplehash_to_ctrack(h);

	/* Inverted packet tuple matches the reverse direction conntrack tuple,
	 * select the other tuplehash to get the right 'ctinfo' bits for this
	 * packet.
	 */
	if (natted)
		h = &ct->tuplehash[!h->tuple.dst.dir];

	nf_ct_set(skb, ct, ovs_ct_get_info(h));
	return ct;
}

static
struct nf_conn *ovs_ct_executed(struct net *net,
				const struct sw_flow_key *key,
				const struct ovs_conntrack_info *info,
				struct sk_buff *skb,
				bool *ct_executed)
{
	struct nf_conn *ct = NULL;

	/* If no ct, check if we have evidence that an existing conntrack entry
	 * might be found for this skb.  This happens when we lose a skb->_nfct
	 * due to an upcall, or if the direction is being forced.  If the
	 * connection was not confirmed, it is not cached and needs to be run
	 * through conntrack again.
	 */
	*ct_executed = (key->ct_state & OVS_CS_F_TRACKED) &&
		       !(key->ct_state & OVS_CS_F_INVALID) &&
		       (key->ct_zone == info->zone.id);

	if (*ct_executed || (!key->ct_state && info->force)) {
		ct = ovs_ct_find_existing(net, &info->zone, info->family, skb,
					  !!(key->ct_state &
					  OVS_CS_F_NAT_MASK));
	}

	return ct;
}

/* Determine whether skb->_nfct is equal to the result of conntrack lookup. */
static bool skb_nfct_cached(struct net *net,
			    const struct sw_flow_key *key,
			    const struct ovs_conntrack_info *info,
			    struct sk_buff *skb)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	bool ct_executed = true;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		ct = ovs_ct_executed(net, key, info, skb, &ct_executed);

	if (ct)
		nf_ct_get(skb, &ctinfo);
	else
		return false;

	if (!net_eq(net, read_pnet(&ct->ct_net)))
		return false;
	if (!nf_ct_zone_equal_any(info->ct, nf_ct_zone(ct)))
		return false;
	if (info->helper) {
		struct nf_conn_help *help;

		help = nf_ct_ext_find(ct, NF_CT_EXT_HELPER);
		if (help && rcu_access_pointer(help->helper) != info->helper)
			return false;
	}
	if (info->nf_ct_timeout) {
		struct nf_conn_timeout *timeout_ext;

		timeout_ext = nf_ct_timeout_find(ct);
		if (!timeout_ext || info->nf_ct_timeout !=
		    rcu_dereference(timeout_ext->timeout))
			return false;
	}
	/* Force conntrack entry direction to the current packet? */
	if (info->force && CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL) {
		/* Delete the conntrack entry if confirmed, else just release
		 * the reference.
		 */
		if (nf_ct_is_confirmed(ct))
			nf_ct_delete(ct, 0, 0);

		nf_ct_put(ct);
		nf_ct_set(skb, NULL, 0);
		return false;
	}

	return ct_executed;
}

#if IS_ENABLED(CONFIG_NF_NAT)
static void ovs_nat_update_key(struct sw_flow_key *key,
			       const struct sk_buff *skb,
			       enum nf_nat_manip_type maniptype)
{
	if (maniptype == NF_NAT_MANIP_SRC) {
		__be16 src;

		key->ct_state |= OVS_CS_F_SRC_NAT;
		if (key->eth.type == htons(ETH_P_IP))
			key->ipv4.addr.src = ip_hdr(skb)->saddr;
		else if (key->eth.type == htons(ETH_P_IPV6))
			memcpy(&key->ipv6.addr.src, &ipv6_hdr(skb)->saddr,
			       sizeof(key->ipv6.addr.src));
		else
			return;

		if (key->ip.proto == IPPROTO_UDP)
			src = udp_hdr(skb)->source;
		else if (key->ip.proto == IPPROTO_TCP)
			src = tcp_hdr(skb)->source;
		else if (key->ip.proto == IPPROTO_SCTP)
			src = sctp_hdr(skb)->source;
		else
			return;

		key->tp.src = src;
	} else {
		__be16 dst;

		key->ct_state |= OVS_CS_F_DST_NAT;
		if (key->eth.type == htons(ETH_P_IP))
			key->ipv4.addr.dst = ip_hdr(skb)->daddr;
		else if (key->eth.type == htons(ETH_P_IPV6))
			memcpy(&key->ipv6.addr.dst, &ipv6_hdr(skb)->daddr,
			       sizeof(key->ipv6.addr.dst));
		else
			return;

		if (key->ip.proto == IPPROTO_UDP)
			dst = udp_hdr(skb)->dest;
		else if (key->ip.proto == IPPROTO_TCP)
			dst = tcp_hdr(skb)->dest;
		else if (key->ip.proto == IPPROTO_SCTP)
			dst = sctp_hdr(skb)->dest;
		else
			return;

		key->tp.dst = dst;
	}
}

/* Modelled after nf_nat_ipv[46]_fn().
 * range is only used for new, uninitialized NAT state.
 * Returns either NF_ACCEPT or NF_DROP.
 */
static int ovs_ct_nat_execute(struct sk_buff *skb, struct nf_conn *ct,
			      enum ip_conntrack_info ctinfo,
			      const struct nf_nat_range2 *range,
			      enum nf_nat_manip_type maniptype, struct sw_flow_key *key)
{
	int hooknum, nh_off, err = NF_ACCEPT;

	nh_off = skb_network_offset(skb);
	skb_pull_rcsum(skb, nh_off);

	/* See HOOK2MANIP(). */
	if (maniptype == NF_NAT_MANIP_SRC)
		hooknum = NF_INET_LOCAL_IN; /* Source NAT */
	else
		hooknum = NF_INET_LOCAL_OUT; /* Destination NAT */

	switch (ctinfo) {
	case IP_CT_RELATED:
	case IP_CT_RELATED_REPLY:
		if (IS_ENABLED(CONFIG_NF_NAT) &&
		    skb->protocol == htons(ETH_P_IP) &&
		    ip_hdr(skb)->protocol == IPPROTO_ICMP) {
			if (!nf_nat_icmp_reply_translation(skb, ct, ctinfo,
							   hooknum))
				err = NF_DROP;
			goto push;
		} else if (IS_ENABLED(CONFIG_IPV6) &&
			   skb->protocol == htons(ETH_P_IPV6)) {
			__be16 frag_off;
			u8 nexthdr = ipv6_hdr(skb)->nexthdr;
			int hdrlen = ipv6_skip_exthdr(skb,
						      sizeof(struct ipv6hdr),
						      &nexthdr, &frag_off);

			if (hdrlen >= 0 && nexthdr == IPPROTO_ICMPV6) {
				if (!nf_nat_icmpv6_reply_translation(skb, ct,
								     ctinfo,
								     hooknum,
								     hdrlen))
					err = NF_DROP;
				goto push;
			}
		}
		/* Non-ICMP, fall thru to initialize if needed. */
		fallthrough;
	case IP_CT_NEW:
		/* Seen it before?  This can happen for loopback, retrans,
		 * or local packets.
		 */
		if (!nf_nat_initialized(ct, maniptype)) {
			/* Initialize according to the NAT action. */
			err = (range && range->flags & NF_NAT_RANGE_MAP_IPS)
				/* Action is set up to establish a new
				 * mapping.
				 */
				? nf_nat_setup_info(ct, range, maniptype)
				: nf_nat_alloc_null_binding(ct, hooknum);
			if (err != NF_ACCEPT)
				goto push;
		}
		break;

	case IP_CT_ESTABLISHED:
	case IP_CT_ESTABLISHED_REPLY:
		break;

	default:
		err = NF_DROP;
		goto push;
	}

	err = nf_nat_packet(ct, ctinfo, hooknum, skb);
push:
	skb_push_rcsum(skb, nh_off);

	/* Update the flow key if NAT successful. */
	if (err == NF_ACCEPT)
		ovs_nat_update_key(key, skb, maniptype);

	return err;
}

/* Returns NF_DROP if the packet should be dropped, NF_ACCEPT otherwise. */
static int ovs_ct_nat(struct net *net, struct sw_flow_key *key,
		      const struct ovs_conntrack_info *info,
		      struct sk_buff *skb, struct nf_conn *ct,
		      enum ip_conntrack_info ctinfo)
{
	enum nf_nat_manip_type maniptype;
	int err;

	/* Add NAT extension if not confirmed yet. */
	if (!nf_ct_is_confirmed(ct) && !nf_ct_nat_ext_add(ct))
		return NF_ACCEPT;   /* Can't NAT. */

	/* Determine NAT type.
	 * Check if the NAT type can be deduced from the tracked connection.
	 * Make sure new expected connections (IP_CT_RELATED) are NATted only
	 * when committing.
	 */
	if (info->nat & OVS_CT_NAT && ctinfo != IP_CT_NEW &&
	    ct->status & IPS_NAT_MASK &&
	    (ctinfo != IP_CT_RELATED || info->commit)) {
		/* NAT an established or related connection like before. */
		if (CTINFO2DIR(ctinfo) == IP_CT_DIR_REPLY)
			/* This is the REPLY direction for a connection
			 * for which NAT was applied in the forward
			 * direction.  Do the reverse NAT.
			 */
			maniptype = ct->status & IPS_SRC_NAT
				? NF_NAT_MANIP_DST : NF_NAT_MANIP_SRC;
		else
			maniptype = ct->status & IPS_SRC_NAT
				? NF_NAT_MANIP_SRC : NF_NAT_MANIP_DST;
	} else if (info->nat & OVS_CT_SRC_NAT) {
		maniptype = NF_NAT_MANIP_SRC;
	} else if (info->nat & OVS_CT_DST_NAT) {
		maniptype = NF_NAT_MANIP_DST;
	} else {
		return NF_ACCEPT; /* Connection is not NATed. */
	}
	err = ovs_ct_nat_execute(skb, ct, ctinfo, &info->range, maniptype, key);

	if (err == NF_ACCEPT && ct->status & IPS_DST_NAT) {
		if (ct->status & IPS_SRC_NAT) {
			if (maniptype == NF_NAT_MANIP_SRC)
				maniptype = NF_NAT_MANIP_DST;
			else
				maniptype = NF_NAT_MANIP_SRC;

			err = ovs_ct_nat_execute(skb, ct, ctinfo, &info->range,
						 maniptype, key);
		} else if (CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL) {
			err = ovs_ct_nat_execute(skb, ct, ctinfo, NULL,
						 NF_NAT_MANIP_SRC, key);
		}
	}

	return err;
}
#else /* !CONFIG_NF_NAT */
static int ovs_ct_nat(struct net *net, struct sw_flow_key *key,
		      const struct ovs_conntrack_info *info,
		      struct sk_buff *skb, struct nf_conn *ct,
		      enum ip_conntrack_info ctinfo)
{
	return NF_ACCEPT;
}
#endif

/* Pass 'skb' through conntrack in 'net', using zone configured in 'info', if
 * not done already.  Update key with new CT state after passing the packet
 * through conntrack.
 * Note that if the packet is deemed invalid by conntrack, skb->_nfct will be
 * set to NULL and 0 will be returned.
 */
static int __ovs_ct_lookup(struct net *net, struct sw_flow_key *key,
			   const struct ovs_conntrack_info *info,
			   struct sk_buff *skb)
{
	/* If we are recirculating packets to match on conntrack fields and
	 * committing with a separate conntrack action,  then we don't need to
	 * actually run the packet through conntrack twice unless it's for a
	 * different zone.
	 */
	bool cached = skb_nfct_cached(net, key, info, skb);
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	if (!cached) {
		struct nf_hook_state state = {
			.hook = NF_INET_PRE_ROUTING,
			.pf = info->family,
			.net = net,
		};
		struct nf_conn *tmpl = info->ct;
		int err;

		/* Associate skb with specified zone. */
		if (tmpl) {
			ct = nf_ct_get(skb, &ctinfo);
			nf_ct_put(ct);
			nf_conntrack_get(&tmpl->ct_general);
			nf_ct_set(skb, tmpl, IP_CT_NEW);
		}

		err = nf_conntrack_in(skb, &state);
		if (err != NF_ACCEPT)
			return -ENOENT;

		/* Clear CT state NAT flags to mark that we have not yet done
		 * NAT after the nf_conntrack_in() call.  We can actually clear
		 * the whole state, as it will be re-initialized below.
		 */
		key->ct_state = 0;

		/* Update the key, but keep the NAT flags. */
		ovs_ct_update_key(skb, info, key, true, true);
	}

	ct = nf_ct_get(skb, &ctinfo);
	if (ct) {
		bool add_helper = false;

		/* Packets starting a new connection must be NATted before the
		 * helper, so that the helper knows about the NAT.  We enforce
		 * this by delaying both NAT and helper calls for unconfirmed
		 * connections until the committing CT action.  For later
		 * packets NAT and Helper may be called in either order.
		 *
		 * NAT will be done only if the CT action has NAT, and only
		 * once per packet (per zone), as guarded by the NAT bits in
		 * the key->ct_state.
		 */
		if (info->nat && !(key->ct_state & OVS_CS_F_NAT_MASK) &&
		    (nf_ct_is_confirmed(ct) || info->commit) &&
		    ovs_ct_nat(net, key, info, skb, ct, ctinfo) != NF_ACCEPT) {
			return -EINVAL;
		}

		/* Userspace may decide to perform a ct lookup without a helper
		 * specified followed by a (recirculate and) commit with one,
		 * or attach a helper in a later commit.  Therefore, for
		 * connections which we will commit, we may need to attach
		 * the helper here.
		 */
		if (info->commit && info->helper && !nfct_help(ct)) {
			int err = __nf_ct_try_assign_helper(ct, info->ct,
							    GFP_ATOMIC);
			if (err)
				return err;
			add_helper = true;

			/* helper installed, add seqadj if NAT is required */
			if (info->nat && !nfct_seqadj(ct)) {
				if (!nfct_seqadj_ext_add(ct))
					return -EINVAL;
			}
		}

		/* Call the helper only if:
		 * - nf_conntrack_in() was executed above ("!cached") or a
		 *   helper was just attached ("add_helper") for a confirmed
		 *   connection, or
		 * - When committing an unconfirmed connection.
		 */
		if ((nf_ct_is_confirmed(ct) ? !cached || add_helper :
					      info->commit) &&
		    ovs_ct_helper(skb, info->family) != NF_ACCEPT) {
			return -EINVAL;
		}

		if (nf_ct_protonum(ct) == IPPROTO_TCP &&
		    nf_ct_is_confirmed(ct) && nf_conntrack_tcp_established(ct)) {
			/* Be liberal for tcp packets so that out-of-window
			 * packets are not marked invalid.
			 */
			nf_ct_set_tcp_be_liberal(ct);
		}
	}

	return 0;
}

/* Lookup connection and read fields into key. */
static int ovs_ct_lookup(struct net *net, struct sw_flow_key *key,
			 const struct ovs_conntrack_info *info,
			 struct sk_buff *skb)
{
	struct nf_conntrack_expect *exp;

	/* If we pass an expected packet through nf_conntrack_in() the
	 * expectation is typically removed, but the packet could still be
	 * lost in upcall processing.  To prevent this from happening we
	 * perform an explicit expectation lookup.  Expected connections are
	 * always new, and will be passed through conntrack only when they are
	 * committed, as it is OK to remove the expectation at that time.
	 */
	exp = ovs_ct_expect_find(net, &info->zone, info->family, skb);
	if (exp) {
		u8 state;

		/* NOTE: New connections are NATted and Helped only when
		 * committed, so we are not calling into NAT here.
		 */
		state = OVS_CS_F_TRACKED | OVS_CS_F_NEW | OVS_CS_F_RELATED;
		__ovs_ct_update_key(key, state, &info->zone, exp->master);
	} else {
		struct nf_conn *ct;
		int err;

		err = __ovs_ct_lookup(net, key, info, skb);
		if (err)
			return err;

		ct = (struct nf_conn *)skb_nfct(skb);
		if (ct)
			nf_ct_deliver_cached_events(ct);
	}

	return 0;
}

static bool labels_nonzero(const struct ovs_key_ct_labels *labels)
{
	size_t i;

	for (i = 0; i < OVS_CT_LABELS_LEN_32; i++)
		if (labels->ct_labels_32[i])
			return true;

	return false;
}

#if	IS_ENABLED(CONFIG_NETFILTER_CONNCOUNT)
static struct hlist_head *ct_limit_hash_bucket(
	const struct ovs_ct_limit_info *info, u16 zone)
{
	return &info->limits[zone & (CT_LIMIT_HASH_BUCKETS - 1)];
}

/* Call with ovs_mutex */
static void ct_limit_set(const struct ovs_ct_limit_info *info,
			 struct ovs_ct_limit *new_ct_limit)
{
	struct ovs_ct_limit *ct_limit;
	struct hlist_head *head;

	head = ct_limit_hash_bucket(info, new_ct_limit->zone);
	hlist_for_each_entry_rcu(ct_limit, head, hlist_node) {
		if (ct_limit->zone == new_ct_limit->zone) {
			hlist_replace_rcu(&ct_limit->hlist_node,
					  &new_ct_limit->hlist_node);
			kfree_rcu(ct_limit, rcu);
			return;
		}
	}

	hlist_add_head_rcu(&new_ct_limit->hlist_node, head);
}

/* Call with ovs_mutex */
static void ct_limit_del(const struct ovs_ct_limit_info *info, u16 zone)
{
	struct ovs_ct_limit *ct_limit;
	struct hlist_head *head;
	struct hlist_node *n;

	head = ct_limit_hash_bucket(info, zone);
	hlist_for_each_entry_safe(ct_limit, n, head, hlist_node) {
		if (ct_limit->zone == zone) {
			hlist_del_rcu(&ct_limit->hlist_node);
			kfree_rcu(ct_limit, rcu);
			return;
		}
	}
}

/* Call with RCU read lock */
static u32 ct_limit_get(const struct ovs_ct_limit_info *info, u16 zone)
{
	struct ovs_ct_limit *ct_limit;
	struct hlist_head *head;

	head = ct_limit_hash_bucket(info, zone);
	hlist_for_each_entry_rcu(ct_limit, head, hlist_node) {
		if (ct_limit->zone == zone)
			return ct_limit->limit;
	}

	return info->default_limit;
}

static int ovs_ct_check_limit(struct net *net,
			      const struct ovs_conntrack_info *info,
			      const struct nf_conntrack_tuple *tuple)
{
	struct ovs_net *ovs_net = net_generic(net, ovs_net_id);
	const struct ovs_ct_limit_info *ct_limit_info = ovs_net->ct_limit_info;
	u32 per_zone_limit, connections;
	u32 conncount_key;

	conncount_key = info->zone.id;

	per_zone_limit = ct_limit_get(ct_limit_info, info->zone.id);
	if (per_zone_limit == OVS_CT_LIMIT_UNLIMITED)
		return 0;

	connections = nf_conncount_count(net, ct_limit_info->data,
					 &conncount_key, tuple, &info->zone);
	if (connections > per_zone_limit)
		return -ENOMEM;

	return 0;
}
#endif

/* Lookup connection and confirm if unconfirmed. */
static int ovs_ct_commit(struct net *net, struct sw_flow_key *key,
			 const struct ovs_conntrack_info *info,
			 struct sk_buff *skb)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	int err;

	err = __ovs_ct_lookup(net, key, info, skb);
	if (err)
		return err;

	/* The connection could be invalid, in which case this is a no-op.*/
	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return 0;

#if	IS_ENABLED(CONFIG_NETFILTER_CONNCOUNT)
	if (static_branch_unlikely(&ovs_ct_limit_enabled)) {
		if (!nf_ct_is_confirmed(ct)) {
			err = ovs_ct_check_limit(net, info,
				&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
			if (err) {
				net_warn_ratelimited("openvswitch: zone: %u "
					"exceeds conntrack limit\n",
					info->zone.id);
				return err;
			}
		}
	}
#endif

	/* Set the conntrack event mask if given.  NEW and DELETE events have
	 * their own groups, but the NFNLGRP_CONNTRACK_UPDATE group listener
	 * typically would receive many kinds of updates.  Setting the event
	 * mask allows those events to be filtered.  The set event mask will
	 * remain in effect for the lifetime of the connection unless changed
	 * by a further CT action with both the commit flag and the eventmask
	 * option. */
	if (info->have_eventmask) {
		struct nf_conntrack_ecache *cache = nf_ct_ecache_find(ct);

		if (cache)
			cache->ctmask = info->eventmask;
	}

	/* Apply changes before confirming the connection so that the initial
	 * conntrack NEW netlink event carries the values given in the CT
	 * action.
	 */
	if (info->mark.mask) {
		err = ovs_ct_set_mark(ct, key, info->mark.value,
				      info->mark.mask);
		if (err)
			return err;
	}
	if (!nf_ct_is_confirmed(ct)) {
		err = ovs_ct_init_labels(ct, key, &info->labels.value,
					 &info->labels.mask);
		if (err)
			return err;
	} else if (IS_ENABLED(CONFIG_NF_CONNTRACK_LABELS) &&
		   labels_nonzero(&info->labels.mask)) {
		err = ovs_ct_set_labels(ct, key, &info->labels.value,
					&info->labels.mask);
		if (err)
			return err;
	}
	/* This will take care of sending queued events even if the connection
	 * is already confirmed.
	 */
	if (nf_conntrack_confirm(skb) != NF_ACCEPT)
		return -EINVAL;

	return 0;
}

/* Trim the skb to the length specified by the IP/IPv6 header,
 * removing any trailing lower-layer padding. This prepares the skb
 * for higher-layer processing that assumes skb->len excludes padding
 * (such as nf_ip_checksum). The caller needs to pull the skb to the
 * network header, and ensure ip_hdr/ipv6_hdr points to valid data.
 */
static int ovs_skb_network_trim(struct sk_buff *skb)
{
	unsigned int len;
	int err;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		len = ntohs(ip_hdr(skb)->tot_len);
		break;
	case htons(ETH_P_IPV6):
		len = sizeof(struct ipv6hdr)
			+ ntohs(ipv6_hdr(skb)->payload_len);
		break;
	default:
		len = skb->len;
	}

	err = pskb_trim_rcsum(skb, len);
	if (err)
		kfree_skb(skb);

	return err;
}

/* Returns 0 on success, -EINPROGRESS if 'skb' is stolen, or other nonzero
 * value if 'skb' is freed.
 */
int ovs_ct_execute(struct net *net, struct sk_buff *skb,
		   struct sw_flow_key *key,
		   const struct ovs_conntrack_info *info)
{
	int nh_ofs;
	int err;

	/* The conntrack module expects to be working at L3. */
	nh_ofs = skb_network_offset(skb);
	skb_pull_rcsum(skb, nh_ofs);

	err = ovs_skb_network_trim(skb);
	if (err)
		return err;

	if (key->ip.frag != OVS_FRAG_TYPE_NONE) {
		err = handle_fragments(net, key, info->zone.id, skb);
		if (err)
			return err;
	}

	if (info->commit)
		err = ovs_ct_commit(net, key, info, skb);
	else
		err = ovs_ct_lookup(net, key, info, skb);

	skb_push_rcsum(skb, nh_ofs);
	if (err)
		kfree_skb(skb);
	return err;
}

int ovs_ct_clear(struct sk_buff *skb, struct sw_flow_key *key)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	ct = nf_ct_get(skb, &ctinfo);

	nf_ct_put(ct);
	nf_ct_set(skb, NULL, IP_CT_UNTRACKED);
	ovs_ct_fill_key(skb, key, false);

	return 0;
}

static int ovs_ct_add_helper(struct ovs_conntrack_info *info, const char *name,
			     const struct sw_flow_key *key, bool log)
{
	struct nf_conntrack_helper *helper;
	struct nf_conn_help *help;
	int ret = 0;

	helper = nf_conntrack_helper_try_module_get(name, info->family,
						    key->ip.proto);
	if (!helper) {
		OVS_NLERR(log, "Unknown helper \"%s\"", name);
		return -EINVAL;
	}

	help = nf_ct_helper_ext_add(info->ct, GFP_KERNEL);
	if (!help) {
		nf_conntrack_helper_put(helper);
		return -ENOMEM;
	}

#if IS_ENABLED(CONFIG_NF_NAT)
	if (info->nat) {
		ret = nf_nat_helper_try_module_get(name, info->family,
						   key->ip.proto);
		if (ret) {
			nf_conntrack_helper_put(helper);
			OVS_NLERR(log, "Failed to load \"%s\" NAT helper, error: %d",
				  name, ret);
			return ret;
		}
	}
#endif
	rcu_assign_pointer(help->helper, helper);
	info->helper = helper;
	return ret;
}

#if IS_ENABLED(CONFIG_NF_NAT)
static int parse_nat(const struct nlattr *attr,
		     struct ovs_conntrack_info *info, bool log)
{
	struct nlattr *a;
	int rem;
	bool have_ip_max = false;
	bool have_proto_max = false;
	bool ip_vers = (info->family == NFPROTO_IPV6);

	nla_for_each_nested(a, attr, rem) {
		static const int ovs_nat_attr_lens[OVS_NAT_ATTR_MAX + 1][2] = {
			[OVS_NAT_ATTR_SRC] = {0, 0},
			[OVS_NAT_ATTR_DST] = {0, 0},
			[OVS_NAT_ATTR_IP_MIN] = {sizeof(struct in_addr),
						 sizeof(struct in6_addr)},
			[OVS_NAT_ATTR_IP_MAX] = {sizeof(struct in_addr),
						 sizeof(struct in6_addr)},
			[OVS_NAT_ATTR_PROTO_MIN] = {sizeof(u16), sizeof(u16)},
			[OVS_NAT_ATTR_PROTO_MAX] = {sizeof(u16), sizeof(u16)},
			[OVS_NAT_ATTR_PERSISTENT] = {0, 0},
			[OVS_NAT_ATTR_PROTO_HASH] = {0, 0},
			[OVS_NAT_ATTR_PROTO_RANDOM] = {0, 0},
		};
		int type = nla_type(a);

		if (type > OVS_NAT_ATTR_MAX) {
			OVS_NLERR(log, "Unknown NAT attribute (type=%d, max=%d)",
				  type, OVS_NAT_ATTR_MAX);
			return -EINVAL;
		}

		if (nla_len(a) != ovs_nat_attr_lens[type][ip_vers]) {
			OVS_NLERR(log, "NAT attribute type %d has unexpected length (%d != %d)",
				  type, nla_len(a),
				  ovs_nat_attr_lens[type][ip_vers]);
			return -EINVAL;
		}

		switch (type) {
		case OVS_NAT_ATTR_SRC:
		case OVS_NAT_ATTR_DST:
			if (info->nat) {
				OVS_NLERR(log, "Only one type of NAT may be specified");
				return -ERANGE;
			}
			info->nat |= OVS_CT_NAT;
			info->nat |= ((type == OVS_NAT_ATTR_SRC)
					? OVS_CT_SRC_NAT : OVS_CT_DST_NAT);
			break;

		case OVS_NAT_ATTR_IP_MIN:
			nla_memcpy(&info->range.min_addr, a,
				   sizeof(info->range.min_addr));
			info->range.flags |= NF_NAT_RANGE_MAP_IPS;
			break;

		case OVS_NAT_ATTR_IP_MAX:
			have_ip_max = true;
			nla_memcpy(&info->range.max_addr, a,
				   sizeof(info->range.max_addr));
			info->range.flags |= NF_NAT_RANGE_MAP_IPS;
			break;

		case OVS_NAT_ATTR_PROTO_MIN:
			info->range.min_proto.all = htons(nla_get_u16(a));
			info->range.flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
			break;

		case OVS_NAT_ATTR_PROTO_MAX:
			have_proto_max = true;
			info->range.max_proto.all = htons(nla_get_u16(a));
			info->range.flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
			break;

		case OVS_NAT_ATTR_PERSISTENT:
			info->range.flags |= NF_NAT_RANGE_PERSISTENT;
			break;

		case OVS_NAT_ATTR_PROTO_HASH:
			info->range.flags |= NF_NAT_RANGE_PROTO_RANDOM;
			break;

		case OVS_NAT_ATTR_PROTO_RANDOM:
			info->range.flags |= NF_NAT_RANGE_PROTO_RANDOM_FULLY;
			break;

		default:
			OVS_NLERR(log, "Unknown nat attribute (%d)", type);
			return -EINVAL;
		}
	}

	if (rem > 0) {
		OVS_NLERR(log, "NAT attribute has %d unknown bytes", rem);
		return -EINVAL;
	}
	if (!info->nat) {
		/* Do not allow flags if no type is given. */
		if (info->range.flags) {
			OVS_NLERR(log,
				  "NAT flags may be given only when NAT range (SRC or DST) is also specified."
				  );
			return -EINVAL;
		}
		info->nat = OVS_CT_NAT;   /* NAT existing connections. */
	} else if (!info->commit) {
		OVS_NLERR(log,
			  "NAT attributes may be specified only when CT COMMIT flag is also specified."
			  );
		return -EINVAL;
	}
	/* Allow missing IP_MAX. */
	if (info->range.flags & NF_NAT_RANGE_MAP_IPS && !have_ip_max) {
		memcpy(&info->range.max_addr, &info->range.min_addr,
		       sizeof(info->range.max_addr));
	}
	/* Allow missing PROTO_MAX. */
	if (info->range.flags & NF_NAT_RANGE_PROTO_SPECIFIED &&
	    !have_proto_max) {
		info->range.max_proto.all = info->range.min_proto.all;
	}
	return 0;
}
#endif

static const struct ovs_ct_len_tbl ovs_ct_attr_lens[OVS_CT_ATTR_MAX + 1] = {
	[OVS_CT_ATTR_COMMIT]	= { .minlen = 0, .maxlen = 0 },
	[OVS_CT_ATTR_FORCE_COMMIT]	= { .minlen = 0, .maxlen = 0 },
	[OVS_CT_ATTR_ZONE]	= { .minlen = sizeof(u16),
				    .maxlen = sizeof(u16) },
	[OVS_CT_ATTR_MARK]	= { .minlen = sizeof(struct md_mark),
				    .maxlen = sizeof(struct md_mark) },
	[OVS_CT_ATTR_LABELS]	= { .minlen = sizeof(struct md_labels),
				    .maxlen = sizeof(struct md_labels) },
	[OVS_CT_ATTR_HELPER]	= { .minlen = 1,
				    .maxlen = NF_CT_HELPER_NAME_LEN },
#if IS_ENABLED(CONFIG_NF_NAT)
	/* NAT length is checked when parsing the nested attributes. */
	[OVS_CT_ATTR_NAT]	= { .minlen = 0, .maxlen = INT_MAX },
#endif
	[OVS_CT_ATTR_EVENTMASK]	= { .minlen = sizeof(u32),
				    .maxlen = sizeof(u32) },
	[OVS_CT_ATTR_TIMEOUT] = { .minlen = 1,
				  .maxlen = CTNL_TIMEOUT_NAME_MAX },
};

static int parse_ct(const struct nlattr *attr, struct ovs_conntrack_info *info,
		    const char **helper, bool log)
{
	struct nlattr *a;
	int rem;

	nla_for_each_nested(a, attr, rem) {
		int type = nla_type(a);
		int maxlen;
		int minlen;

		if (type > OVS_CT_ATTR_MAX) {
			OVS_NLERR(log,
				  "Unknown conntrack attr (type=%d, max=%d)",
				  type, OVS_CT_ATTR_MAX);
			return -EINVAL;
		}

		maxlen = ovs_ct_attr_lens[type].maxlen;
		minlen = ovs_ct_attr_lens[type].minlen;
		if (nla_len(a) < minlen || nla_len(a) > maxlen) {
			OVS_NLERR(log,
				  "Conntrack attr type has unexpected length (type=%d, length=%d, expected=%d)",
				  type, nla_len(a), maxlen);
			return -EINVAL;
		}

		switch (type) {
		case OVS_CT_ATTR_FORCE_COMMIT:
			info->force = true;
			fallthrough;
		case OVS_CT_ATTR_COMMIT:
			info->commit = true;
			break;
#ifdef CONFIG_NF_CONNTRACK_ZONES
		case OVS_CT_ATTR_ZONE:
			info->zone.id = nla_get_u16(a);
			break;
#endif
#ifdef CONFIG_NF_CONNTRACK_MARK
		case OVS_CT_ATTR_MARK: {
			struct md_mark *mark = nla_data(a);

			if (!mark->mask) {
				OVS_NLERR(log, "ct_mark mask cannot be 0");
				return -EINVAL;
			}
			info->mark = *mark;
			break;
		}
#endif
#ifdef CONFIG_NF_CONNTRACK_LABELS
		case OVS_CT_ATTR_LABELS: {
			struct md_labels *labels = nla_data(a);

			if (!labels_nonzero(&labels->mask)) {
				OVS_NLERR(log, "ct_labels mask cannot be 0");
				return -EINVAL;
			}
			info->labels = *labels;
			break;
		}
#endif
		case OVS_CT_ATTR_HELPER:
			*helper = nla_data(a);
			if (!memchr(*helper, '\0', nla_len(a))) {
				OVS_NLERR(log, "Invalid conntrack helper");
				return -EINVAL;
			}
			break;
#if IS_ENABLED(CONFIG_NF_NAT)
		case OVS_CT_ATTR_NAT: {
			int err = parse_nat(a, info, log);

			if (err)
				return err;
			break;
		}
#endif
		case OVS_CT_ATTR_EVENTMASK:
			info->have_eventmask = true;
			info->eventmask = nla_get_u32(a);
			break;
#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
		case OVS_CT_ATTR_TIMEOUT:
			memcpy(info->timeout, nla_data(a), nla_len(a));
			if (!memchr(info->timeout, '\0', nla_len(a))) {
				OVS_NLERR(log, "Invalid conntrack timeout");
				return -EINVAL;
			}
			break;
#endif

		default:
			OVS_NLERR(log, "Unknown conntrack attr (%d)",
				  type);
			return -EINVAL;
		}
	}

#ifdef CONFIG_NF_CONNTRACK_MARK
	if (!info->commit && info->mark.mask) {
		OVS_NLERR(log,
			  "Setting conntrack mark requires 'commit' flag.");
		return -EINVAL;
	}
#endif
#ifdef CONFIG_NF_CONNTRACK_LABELS
	if (!info->commit && labels_nonzero(&info->labels.mask)) {
		OVS_NLERR(log,
			  "Setting conntrack labels requires 'commit' flag.");
		return -EINVAL;
	}
#endif
	if (rem > 0) {
		OVS_NLERR(log, "Conntrack attr has %d unknown bytes", rem);
		return -EINVAL;
	}

	return 0;
}

bool ovs_ct_verify(struct net *net, enum ovs_key_attr attr)
{
	if (attr == OVS_KEY_ATTR_CT_STATE)
		return true;
	if (IS_ENABLED(CONFIG_NF_CONNTRACK_ZONES) &&
	    attr == OVS_KEY_ATTR_CT_ZONE)
		return true;
	if (IS_ENABLED(CONFIG_NF_CONNTRACK_MARK) &&
	    attr == OVS_KEY_ATTR_CT_MARK)
		return true;
	if (IS_ENABLED(CONFIG_NF_CONNTRACK_LABELS) &&
	    attr == OVS_KEY_ATTR_CT_LABELS) {
		struct ovs_net *ovs_net = net_generic(net, ovs_net_id);

		return ovs_net->xt_label;
	}

	return false;
}

int ovs_ct_copy_action(struct net *net, const struct nlattr *attr,
		       const struct sw_flow_key *key,
		       struct sw_flow_actions **sfa,  bool log)
{
	struct ovs_conntrack_info ct_info;
	const char *helper = NULL;
	u16 family;
	int err;

	family = key_to_nfproto(key);
	if (family == NFPROTO_UNSPEC) {
		OVS_NLERR(log, "ct family unspecified");
		return -EINVAL;
	}

	memset(&ct_info, 0, sizeof(ct_info));
	ct_info.family = family;

	nf_ct_zone_init(&ct_info.zone, NF_CT_DEFAULT_ZONE_ID,
			NF_CT_DEFAULT_ZONE_DIR, 0);

	err = parse_ct(attr, &ct_info, &helper, log);
	if (err)
		return err;

	/* Set up template for tracking connections in specific zones. */
	ct_info.ct = nf_ct_tmpl_alloc(net, &ct_info.zone, GFP_KERNEL);
	if (!ct_info.ct) {
		OVS_NLERR(log, "Failed to allocate conntrack template");
		return -ENOMEM;
	}

	if (ct_info.timeout[0]) {
		if (nf_ct_set_timeout(net, ct_info.ct, family, key->ip.proto,
				      ct_info.timeout))
			pr_info_ratelimited("Failed to associated timeout "
					    "policy `%s'\n", ct_info.timeout);
		else
			ct_info.nf_ct_timeout = rcu_dereference(
				nf_ct_timeout_find(ct_info.ct)->timeout);

	}

	if (helper) {
		err = ovs_ct_add_helper(&ct_info, helper, key, log);
		if (err)
			goto err_free_ct;
	}

	err = ovs_nla_add_action(sfa, OVS_ACTION_ATTR_CT, &ct_info,
				 sizeof(ct_info), log);
	if (err)
		goto err_free_ct;

	__set_bit(IPS_CONFIRMED_BIT, &ct_info.ct->status);
	nf_conntrack_get(&ct_info.ct->ct_general);
	return 0;
err_free_ct:
	__ovs_ct_free_action(&ct_info);
	return err;
}

#if IS_ENABLED(CONFIG_NF_NAT)
static bool ovs_ct_nat_to_attr(const struct ovs_conntrack_info *info,
			       struct sk_buff *skb)
{
	struct nlattr *start;

	start = nla_nest_start_noflag(skb, OVS_CT_ATTR_NAT);
	if (!start)
		return false;

	if (info->nat & OVS_CT_SRC_NAT) {
		if (nla_put_flag(skb, OVS_NAT_ATTR_SRC))
			return false;
	} else if (info->nat & OVS_CT_DST_NAT) {
		if (nla_put_flag(skb, OVS_NAT_ATTR_DST))
			return false;
	} else {
		goto out;
	}

	if (info->range.flags & NF_NAT_RANGE_MAP_IPS) {
		if (IS_ENABLED(CONFIG_NF_NAT) &&
		    info->family == NFPROTO_IPV4) {
			if (nla_put_in_addr(skb, OVS_NAT_ATTR_IP_MIN,
					    info->range.min_addr.ip) ||
			    (info->range.max_addr.ip
			     != info->range.min_addr.ip &&
			     (nla_put_in_addr(skb, OVS_NAT_ATTR_IP_MAX,
					      info->range.max_addr.ip))))
				return false;
		} else if (IS_ENABLED(CONFIG_IPV6) &&
			   info->family == NFPROTO_IPV6) {
			if (nla_put_in6_addr(skb, OVS_NAT_ATTR_IP_MIN,
					     &info->range.min_addr.in6) ||
			    (memcmp(&info->range.max_addr.in6,
				    &info->range.min_addr.in6,
				    sizeof(info->range.max_addr.in6)) &&
			     (nla_put_in6_addr(skb, OVS_NAT_ATTR_IP_MAX,
					       &info->range.max_addr.in6))))
				return false;
		} else {
			return false;
		}
	}
	if (info->range.flags & NF_NAT_RANGE_PROTO_SPECIFIED &&
	    (nla_put_u16(skb, OVS_NAT_ATTR_PROTO_MIN,
			 ntohs(info->range.min_proto.all)) ||
	     (info->range.max_proto.all != info->range.min_proto.all &&
	      nla_put_u16(skb, OVS_NAT_ATTR_PROTO_MAX,
			  ntohs(info->range.max_proto.all)))))
		return false;

	if (info->range.flags & NF_NAT_RANGE_PERSISTENT &&
	    nla_put_flag(skb, OVS_NAT_ATTR_PERSISTENT))
		return false;
	if (info->range.flags & NF_NAT_RANGE_PROTO_RANDOM &&
	    nla_put_flag(skb, OVS_NAT_ATTR_PROTO_HASH))
		return false;
	if (info->range.flags & NF_NAT_RANGE_PROTO_RANDOM_FULLY &&
	    nla_put_flag(skb, OVS_NAT_ATTR_PROTO_RANDOM))
		return false;
out:
	nla_nest_end(skb, start);

	return true;
}
#endif

int ovs_ct_action_to_attr(const struct ovs_conntrack_info *ct_info,
			  struct sk_buff *skb)
{
	struct nlattr *start;

	start = nla_nest_start_noflag(skb, OVS_ACTION_ATTR_CT);
	if (!start)
		return -EMSGSIZE;

	if (ct_info->commit && nla_put_flag(skb, ct_info->force
					    ? OVS_CT_ATTR_FORCE_COMMIT
					    : OVS_CT_ATTR_COMMIT))
		return -EMSGSIZE;
	if (IS_ENABLED(CONFIG_NF_CONNTRACK_ZONES) &&
	    nla_put_u16(skb, OVS_CT_ATTR_ZONE, ct_info->zone.id))
		return -EMSGSIZE;
	if (IS_ENABLED(CONFIG_NF_CONNTRACK_MARK) && ct_info->mark.mask &&
	    nla_put(skb, OVS_CT_ATTR_MARK, sizeof(ct_info->mark),
		    &ct_info->mark))
		return -EMSGSIZE;
	if (IS_ENABLED(CONFIG_NF_CONNTRACK_LABELS) &&
	    labels_nonzero(&ct_info->labels.mask) &&
	    nla_put(skb, OVS_CT_ATTR_LABELS, sizeof(ct_info->labels),
		    &ct_info->labels))
		return -EMSGSIZE;
	if (ct_info->helper) {
		if (nla_put_string(skb, OVS_CT_ATTR_HELPER,
				   ct_info->helper->name))
			return -EMSGSIZE;
	}
	if (ct_info->have_eventmask &&
	    nla_put_u32(skb, OVS_CT_ATTR_EVENTMASK, ct_info->eventmask))
		return -EMSGSIZE;
	if (ct_info->timeout[0]) {
		if (nla_put_string(skb, OVS_CT_ATTR_TIMEOUT, ct_info->timeout))
			return -EMSGSIZE;
	}

#if IS_ENABLED(CONFIG_NF_NAT)
	if (ct_info->nat && !ovs_ct_nat_to_attr(ct_info, skb))
		return -EMSGSIZE;
#endif
	nla_nest_end(skb, start);

	return 0;
}

void ovs_ct_free_action(const struct nlattr *a)
{
	struct ovs_conntrack_info *ct_info = nla_data(a);

	__ovs_ct_free_action(ct_info);
}

static void __ovs_ct_free_action(struct ovs_conntrack_info *ct_info)
{
	if (ct_info->helper) {
#if IS_ENABLED(CONFIG_NF_NAT)
		if (ct_info->nat)
			nf_nat_helper_put(ct_info->helper);
#endif
		nf_conntrack_helper_put(ct_info->helper);
	}
	if (ct_info->ct) {
		if (ct_info->timeout[0])
			nf_ct_destroy_timeout(ct_info->ct);
		nf_ct_tmpl_free(ct_info->ct);
	}
}

#if	IS_ENABLED(CONFIG_NETFILTER_CONNCOUNT)
static int ovs_ct_limit_init(struct net *net, struct ovs_net *ovs_net)
{
	int i, err;

	ovs_net->ct_limit_info = kmalloc(sizeof(*ovs_net->ct_limit_info),
					 GFP_KERNEL);
	if (!ovs_net->ct_limit_info)
		return -ENOMEM;

	ovs_net->ct_limit_info->default_limit = OVS_CT_LIMIT_DEFAULT;
	ovs_net->ct_limit_info->limits =
		kmalloc_array(CT_LIMIT_HASH_BUCKETS, sizeof(struct hlist_head),
			      GFP_KERNEL);
	if (!ovs_net->ct_limit_info->limits) {
		kfree(ovs_net->ct_limit_info);
		return -ENOMEM;
	}

	for (i = 0; i < CT_LIMIT_HASH_BUCKETS; i++)
		INIT_HLIST_HEAD(&ovs_net->ct_limit_info->limits[i]);

	ovs_net->ct_limit_info->data =
		nf_conncount_init(net, NFPROTO_INET, sizeof(u32));

	if (IS_ERR(ovs_net->ct_limit_info->data)) {
		err = PTR_ERR(ovs_net->ct_limit_info->data);
		kfree(ovs_net->ct_limit_info->limits);
		kfree(ovs_net->ct_limit_info);
		pr_err("openvswitch: failed to init nf_conncount %d\n", err);
		return err;
	}
	return 0;
}

static void ovs_ct_limit_exit(struct net *net, struct ovs_net *ovs_net)
{
	const struct ovs_ct_limit_info *info = ovs_net->ct_limit_info;
	int i;

	nf_conncount_destroy(net, NFPROTO_INET, info->data);
	for (i = 0; i < CT_LIMIT_HASH_BUCKETS; ++i) {
		struct hlist_head *head = &info->limits[i];
		struct ovs_ct_limit *ct_limit;

		hlist_for_each_entry_rcu(ct_limit, head, hlist_node,
					 lockdep_ovsl_is_held())
			kfree_rcu(ct_limit, rcu);
	}
	kfree(info->limits);
	kfree(info);
}

static struct sk_buff *
ovs_ct_limit_cmd_reply_start(struct genl_info *info, u8 cmd,
			     struct ovs_header **ovs_reply_header)
{
	struct ovs_header *ovs_header = info->userhdr;
	struct sk_buff *skb;

	skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	*ovs_reply_header = genlmsg_put(skb, info->snd_portid,
					info->snd_seq,
					&dp_ct_limit_genl_family, 0, cmd);

	if (!*ovs_reply_header) {
		nlmsg_free(skb);
		return ERR_PTR(-EMSGSIZE);
	}
	(*ovs_reply_header)->dp_ifindex = ovs_header->dp_ifindex;

	return skb;
}

static bool check_zone_id(int zone_id, u16 *pzone)
{
	if (zone_id >= 0 && zone_id <= 65535) {
		*pzone = (u16)zone_id;
		return true;
	}
	return false;
}

static int ovs_ct_limit_set_zone_limit(struct nlattr *nla_zone_limit,
				       struct ovs_ct_limit_info *info)
{
	struct ovs_zone_limit *zone_limit;
	int rem;
	u16 zone;

	rem = NLA_ALIGN(nla_len(nla_zone_limit));
	zone_limit = (struct ovs_zone_limit *)nla_data(nla_zone_limit);

	while (rem >= sizeof(*zone_limit)) {
		if (unlikely(zone_limit->zone_id ==
				OVS_ZONE_LIMIT_DEFAULT_ZONE)) {
			ovs_lock();
			info->default_limit = zone_limit->limit;
			ovs_unlock();
		} else if (unlikely(!check_zone_id(
				zone_limit->zone_id, &zone))) {
			OVS_NLERR(true, "zone id is out of range");
		} else {
			struct ovs_ct_limit *ct_limit;

			ct_limit = kmalloc(sizeof(*ct_limit), GFP_KERNEL);
			if (!ct_limit)
				return -ENOMEM;

			ct_limit->zone = zone;
			ct_limit->limit = zone_limit->limit;

			ovs_lock();
			ct_limit_set(info, ct_limit);
			ovs_unlock();
		}
		rem -= NLA_ALIGN(sizeof(*zone_limit));
		zone_limit = (struct ovs_zone_limit *)((u8 *)zone_limit +
				NLA_ALIGN(sizeof(*zone_limit)));
	}

	if (rem)
		OVS_NLERR(true, "set zone limit has %d unknown bytes", rem);

	return 0;
}

static int ovs_ct_limit_del_zone_limit(struct nlattr *nla_zone_limit,
				       struct ovs_ct_limit_info *info)
{
	struct ovs_zone_limit *zone_limit;
	int rem;
	u16 zone;

	rem = NLA_ALIGN(nla_len(nla_zone_limit));
	zone_limit = (struct ovs_zone_limit *)nla_data(nla_zone_limit);

	while (rem >= sizeof(*zone_limit)) {
		if (unlikely(zone_limit->zone_id ==
				OVS_ZONE_LIMIT_DEFAULT_ZONE)) {
			ovs_lock();
			info->default_limit = OVS_CT_LIMIT_DEFAULT;
			ovs_unlock();
		} else if (unlikely(!check_zone_id(
				zone_limit->zone_id, &zone))) {
			OVS_NLERR(true, "zone id is out of range");
		} else {
			ovs_lock();
			ct_limit_del(info, zone);
			ovs_unlock();
		}
		rem -= NLA_ALIGN(sizeof(*zone_limit));
		zone_limit = (struct ovs_zone_limit *)((u8 *)zone_limit +
				NLA_ALIGN(sizeof(*zone_limit)));
	}

	if (rem)
		OVS_NLERR(true, "del zone limit has %d unknown bytes", rem);

	return 0;
}

static int ovs_ct_limit_get_default_limit(struct ovs_ct_limit_info *info,
					  struct sk_buff *reply)
{
	struct ovs_zone_limit zone_limit = {
		.zone_id = OVS_ZONE_LIMIT_DEFAULT_ZONE,
		.limit   = info->default_limit,
	};

	return nla_put_nohdr(reply, sizeof(zone_limit), &zone_limit);
}

static int __ovs_ct_limit_get_zone_limit(struct net *net,
					 struct nf_conncount_data *data,
					 u16 zone_id, u32 limit,
					 struct sk_buff *reply)
{
	struct nf_conntrack_zone ct_zone;
	struct ovs_zone_limit zone_limit;
	u32 conncount_key = zone_id;

	zone_limit.zone_id = zone_id;
	zone_limit.limit = limit;
	nf_ct_zone_init(&ct_zone, zone_id, NF_CT_DEFAULT_ZONE_DIR, 0);

	zone_limit.count = nf_conncount_count(net, data, &conncount_key, NULL,
					      &ct_zone);
	return nla_put_nohdr(reply, sizeof(zone_limit), &zone_limit);
}

static int ovs_ct_limit_get_zone_limit(struct net *net,
				       struct nlattr *nla_zone_limit,
				       struct ovs_ct_limit_info *info,
				       struct sk_buff *reply)
{
	struct ovs_zone_limit *zone_limit;
	int rem, err;
	u32 limit;
	u16 zone;

	rem = NLA_ALIGN(nla_len(nla_zone_limit));
	zone_limit = (struct ovs_zone_limit *)nla_data(nla_zone_limit);

	while (rem >= sizeof(*zone_limit)) {
		if (unlikely(zone_limit->zone_id ==
				OVS_ZONE_LIMIT_DEFAULT_ZONE)) {
			err = ovs_ct_limit_get_default_limit(info, reply);
			if (err)
				return err;
		} else if (unlikely(!check_zone_id(zone_limit->zone_id,
							&zone))) {
			OVS_NLERR(true, "zone id is out of range");
		} else {
			rcu_read_lock();
			limit = ct_limit_get(info, zone);
			rcu_read_unlock();

			err = __ovs_ct_limit_get_zone_limit(
				net, info->data, zone, limit, reply);
			if (err)
				return err;
		}
		rem -= NLA_ALIGN(sizeof(*zone_limit));
		zone_limit = (struct ovs_zone_limit *)((u8 *)zone_limit +
				NLA_ALIGN(sizeof(*zone_limit)));
	}

	if (rem)
		OVS_NLERR(true, "get zone limit has %d unknown bytes", rem);

	return 0;
}

static int ovs_ct_limit_get_all_zone_limit(struct net *net,
					   struct ovs_ct_limit_info *info,
					   struct sk_buff *reply)
{
	struct ovs_ct_limit *ct_limit;
	struct hlist_head *head;
	int i, err = 0;

	err = ovs_ct_limit_get_default_limit(info, reply);
	if (err)
		return err;

	rcu_read_lock();
	for (i = 0; i < CT_LIMIT_HASH_BUCKETS; ++i) {
		head = &info->limits[i];
		hlist_for_each_entry_rcu(ct_limit, head, hlist_node) {
			err = __ovs_ct_limit_get_zone_limit(net, info->data,
				ct_limit->zone, ct_limit->limit, reply);
			if (err)
				goto exit_err;
		}
	}

exit_err:
	rcu_read_unlock();
	return err;
}

static int ovs_ct_limit_cmd_set(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	struct sk_buff *reply;
	struct ovs_header *ovs_reply_header;
	struct ovs_net *ovs_net = net_generic(sock_net(skb->sk), ovs_net_id);
	struct ovs_ct_limit_info *ct_limit_info = ovs_net->ct_limit_info;
	int err;

	reply = ovs_ct_limit_cmd_reply_start(info, OVS_CT_LIMIT_CMD_SET,
					     &ovs_reply_header);
	if (IS_ERR(reply))
		return PTR_ERR(reply);

	if (!a[OVS_CT_LIMIT_ATTR_ZONE_LIMIT]) {
		err = -EINVAL;
		goto exit_err;
	}

	err = ovs_ct_limit_set_zone_limit(a[OVS_CT_LIMIT_ATTR_ZONE_LIMIT],
					  ct_limit_info);
	if (err)
		goto exit_err;

	static_branch_enable(&ovs_ct_limit_enabled);

	genlmsg_end(reply, ovs_reply_header);
	return genlmsg_reply(reply, info);

exit_err:
	nlmsg_free(reply);
	return err;
}

static int ovs_ct_limit_cmd_del(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	struct sk_buff *reply;
	struct ovs_header *ovs_reply_header;
	struct ovs_net *ovs_net = net_generic(sock_net(skb->sk), ovs_net_id);
	struct ovs_ct_limit_info *ct_limit_info = ovs_net->ct_limit_info;
	int err;

	reply = ovs_ct_limit_cmd_reply_start(info, OVS_CT_LIMIT_CMD_DEL,
					     &ovs_reply_header);
	if (IS_ERR(reply))
		return PTR_ERR(reply);

	if (!a[OVS_CT_LIMIT_ATTR_ZONE_LIMIT]) {
		err = -EINVAL;
		goto exit_err;
	}

	err = ovs_ct_limit_del_zone_limit(a[OVS_CT_LIMIT_ATTR_ZONE_LIMIT],
					  ct_limit_info);
	if (err)
		goto exit_err;

	genlmsg_end(reply, ovs_reply_header);
	return genlmsg_reply(reply, info);

exit_err:
	nlmsg_free(reply);
	return err;
}

static int ovs_ct_limit_cmd_get(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **a = info->attrs;
	struct nlattr *nla_reply;
	struct sk_buff *reply;
	struct ovs_header *ovs_reply_header;
	struct net *net = sock_net(skb->sk);
	struct ovs_net *ovs_net = net_generic(net, ovs_net_id);
	struct ovs_ct_limit_info *ct_limit_info = ovs_net->ct_limit_info;
	int err;

	reply = ovs_ct_limit_cmd_reply_start(info, OVS_CT_LIMIT_CMD_GET,
					     &ovs_reply_header);
	if (IS_ERR(reply))
		return PTR_ERR(reply);

	nla_reply = nla_nest_start_noflag(reply, OVS_CT_LIMIT_ATTR_ZONE_LIMIT);
	if (!nla_reply) {
		err = -EMSGSIZE;
		goto exit_err;
	}

	if (a[OVS_CT_LIMIT_ATTR_ZONE_LIMIT]) {
		err = ovs_ct_limit_get_zone_limit(
			net, a[OVS_CT_LIMIT_ATTR_ZONE_LIMIT], ct_limit_info,
			reply);
		if (err)
			goto exit_err;
	} else {
		err = ovs_ct_limit_get_all_zone_limit(net, ct_limit_info,
						      reply);
		if (err)
			goto exit_err;
	}

	nla_nest_end(reply, nla_reply);
	genlmsg_end(reply, ovs_reply_header);
	return genlmsg_reply(reply, info);

exit_err:
	nlmsg_free(reply);
	return err;
}

static const struct genl_small_ops ct_limit_genl_ops[] = {
	{ .cmd = OVS_CT_LIMIT_CMD_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN
					   * privilege. */
		.doit = ovs_ct_limit_cmd_set,
	},
	{ .cmd = OVS_CT_LIMIT_CMD_DEL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags = GENL_ADMIN_PERM, /* Requires CAP_NET_ADMIN
					   * privilege. */
		.doit = ovs_ct_limit_cmd_del,
	},
	{ .cmd = OVS_CT_LIMIT_CMD_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags = 0,		  /* OK for unprivileged users. */
		.doit = ovs_ct_limit_cmd_get,
	},
};

static const struct genl_multicast_group ovs_ct_limit_multicast_group = {
	.name = OVS_CT_LIMIT_MCGROUP,
};

struct genl_family dp_ct_limit_genl_family __ro_after_init = {
	.hdrsize = sizeof(struct ovs_header),
	.name = OVS_CT_LIMIT_FAMILY,
	.version = OVS_CT_LIMIT_VERSION,
	.maxattr = OVS_CT_LIMIT_ATTR_MAX,
	.policy = ct_limit_policy,
	.netnsok = true,
	.parallel_ops = true,
	.small_ops = ct_limit_genl_ops,
	.n_small_ops = ARRAY_SIZE(ct_limit_genl_ops),
	.mcgrps = &ovs_ct_limit_multicast_group,
	.n_mcgrps = 1,
	.module = THIS_MODULE,
};
#endif

int ovs_ct_init(struct net *net)
{
	unsigned int n_bits = sizeof(struct ovs_key_ct_labels) * BITS_PER_BYTE;
	struct ovs_net *ovs_net = net_generic(net, ovs_net_id);

	if (nf_connlabels_get(net, n_bits - 1)) {
		ovs_net->xt_label = false;
		OVS_NLERR(true, "Failed to set connlabel length");
	} else {
		ovs_net->xt_label = true;
	}

#if	IS_ENABLED(CONFIG_NETFILTER_CONNCOUNT)
	return ovs_ct_limit_init(net, ovs_net);
#else
	return 0;
#endif
}

void ovs_ct_exit(struct net *net)
{
	struct ovs_net *ovs_net = net_generic(net, ovs_net_id);

#if	IS_ENABLED(CONFIG_NETFILTER_CONNCOUNT)
	ovs_ct_limit_exit(net, ovs_net);
#endif

	if (ovs_net->xt_label)
		nf_connlabels_put(net);
}
