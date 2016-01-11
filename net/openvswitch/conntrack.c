/*
 * Copyright (c) 2015 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/openvswitch.h>
#include <net/ip.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_labels.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>

#include "datapath.h"
#include "conntrack.h"
#include "flow.h"
#include "flow_netlink.h"

struct ovs_ct_len_tbl {
	size_t maxlen;
	size_t minlen;
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

/* Conntrack action context for execution. */
struct ovs_conntrack_info {
	struct nf_conntrack_helper *helper;
	struct nf_conntrack_zone zone;
	struct nf_conn *ct;
	u8 commit : 1;
	u16 family;
	struct md_mark mark;
	struct md_labels labels;
};

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
	case IP_CT_NEW_REPLY:
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
	case IP_CT_NEW_REPLY:
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

static void ovs_ct_get_labels(const struct nf_conn *ct,
			      struct ovs_key_ct_labels *labels)
{
	struct nf_conn_labels *cl = ct ? nf_ct_labels_find(ct) : NULL;

	if (cl) {
		size_t len = cl->words * sizeof(long);

		if (len > OVS_CT_LABELS_LEN)
			len = OVS_CT_LABELS_LEN;
		else if (len < OVS_CT_LABELS_LEN)
			memset(labels, 0, OVS_CT_LABELS_LEN);
		memcpy(labels, cl->bits, len);
	} else {
		memset(labels, 0, OVS_CT_LABELS_LEN);
	}
}

static void __ovs_ct_update_key(struct sw_flow_key *key, u8 state,
				const struct nf_conntrack_zone *zone,
				const struct nf_conn *ct)
{
	key->ct.state = state;
	key->ct.zone = zone->id;
	key->ct.mark = ovs_ct_get_mark(ct);
	ovs_ct_get_labels(ct, &key->ct.labels);
}

/* Update 'key' based on skb->nfct. If 'post_ct' is true, then OVS has
 * previously sent the packet to conntrack via the ct action.
 */
static void ovs_ct_update_key(const struct sk_buff *skb,
			      const struct ovs_conntrack_info *info,
			      struct sw_flow_key *key, bool post_ct)
{
	const struct nf_conntrack_zone *zone = &nf_ct_zone_dflt;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	u8 state = 0;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct) {
		state = ovs_ct_get_state(ctinfo);
		if (!nf_ct_is_confirmed(ct))
			state |= OVS_CS_F_NEW;
		if (ct->master)
			state |= OVS_CS_F_RELATED;
		zone = nf_ct_zone(ct);
	} else if (post_ct) {
		state = OVS_CS_F_TRACKED | OVS_CS_F_INVALID;
		if (info)
			zone = &info->zone;
	}
	__ovs_ct_update_key(key, state, zone, ct);
}

void ovs_ct_fill_key(const struct sk_buff *skb, struct sw_flow_key *key)
{
	ovs_ct_update_key(skb, NULL, key, false);
}

int ovs_ct_put_key(const struct sw_flow_key *key, struct sk_buff *skb)
{
	if (nla_put_u32(skb, OVS_KEY_ATTR_CT_STATE, key->ct.state))
		return -EMSGSIZE;

	if (IS_ENABLED(CONFIG_NF_CONNTRACK_ZONES) &&
	    nla_put_u16(skb, OVS_KEY_ATTR_CT_ZONE, key->ct.zone))
		return -EMSGSIZE;

	if (IS_ENABLED(CONFIG_NF_CONNTRACK_MARK) &&
	    nla_put_u32(skb, OVS_KEY_ATTR_CT_MARK, key->ct.mark))
		return -EMSGSIZE;

	if (IS_ENABLED(CONFIG_NF_CONNTRACK_LABELS) &&
	    nla_put(skb, OVS_KEY_ATTR_CT_LABELS, sizeof(key->ct.labels),
		    &key->ct.labels))
		return -EMSGSIZE;

	return 0;
}

static int ovs_ct_set_mark(struct sk_buff *skb, struct sw_flow_key *key,
			   u32 ct_mark, u32 mask)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK_MARK)
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	u32 new_mark;


	/* The connection could be invalid, in which case set_mark is no-op. */
	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return 0;

	new_mark = ct_mark | (ct->mark & ~(mask));
	if (ct->mark != new_mark) {
		ct->mark = new_mark;
		nf_conntrack_event_cache(IPCT_MARK, ct);
		key->ct.mark = new_mark;
	}

	return 0;
#else
	return -ENOTSUPP;
#endif
}

static int ovs_ct_set_labels(struct sk_buff *skb, struct sw_flow_key *key,
			     const struct ovs_key_ct_labels *labels,
			     const struct ovs_key_ct_labels *mask)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn_labels *cl;
	struct nf_conn *ct;
	int err;

	/* The connection could be invalid, in which case set_label is no-op.*/
	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return 0;

	cl = nf_ct_labels_find(ct);
	if (!cl) {
		nf_ct_labels_ext_add(ct);
		cl = nf_ct_labels_find(ct);
	}
	if (!cl || cl->words * sizeof(long) < OVS_CT_LABELS_LEN)
		return -ENOSPC;

	err = nf_connlabels_replace(ct, (u32 *)labels, (u32 *)mask,
				    OVS_CT_LABELS_LEN / sizeof(u32));
	if (err)
		return err;

	ovs_ct_get_labels(ct, &key->ct.labels);
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

	return helper->help(skb, protoff, ct, ctinfo);
}

/* Returns 0 on success, -EINPROGRESS if 'skb' is stolen, or other nonzero
 * value if 'skb' is freed.
 */
static int handle_fragments(struct net *net, struct sw_flow_key *key,
			    u16 zone, struct sk_buff *skb)
{
	struct ovs_skb_cb ovs_cb = *OVS_CB(skb);

	if (key->eth.type == htons(ETH_P_IP)) {
		enum ip_defrag_users user = IP_DEFRAG_CONNTRACK_IN + zone;
		int err;

		memset(IPCB(skb), 0, sizeof(struct inet_skb_parm));
		err = ip_defrag(net, skb, user);
		if (err)
			return err;

		ovs_cb.mru = IPCB(skb)->frag_max_size;
#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV6)
	} else if (key->eth.type == htons(ETH_P_IPV6)) {
		enum ip6_defrag_users user = IP6_DEFRAG_CONNTRACK_IN + zone;
		struct sk_buff *reasm;

		memset(IP6CB(skb), 0, sizeof(struct inet6_skb_parm));
		reasm = nf_ct_frag6_gather(net, skb, user);
		if (!reasm)
			return -EINPROGRESS;

		if (skb == reasm) {
			kfree_skb(skb);
			return -EINVAL;
		}

		/* Don't free 'skb' even though it is one of the original
		 * fragments, as we're going to morph it into the head.
		 */
		skb_get(skb);
		nf_ct_frag6_consume_orig(reasm);

		key->ip.proto = ipv6_hdr(reasm)->nexthdr;
		skb_morph(skb, reasm);
		skb->next = reasm->next;
		consume_skb(reasm);
		ovs_cb.mru = IP6CB(skb)->frag_max_size;
#endif
	} else {
		kfree_skb(skb);
		return -EPFNOSUPPORT;
	}

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

	if (!nf_ct_get_tuplepr(skb, skb_network_offset(skb), proto, net, &tuple))
		return NULL;
	return __nf_ct_expect_find(net, zone, &tuple);
}

/* Determine whether skb->nfct is equal to the result of conntrack lookup. */
static bool skb_nfct_cached(const struct net *net, const struct sk_buff *skb,
			    const struct ovs_conntrack_info *info)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
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

	return true;
}

static int __ovs_ct_lookup(struct net *net, struct sw_flow_key *key,
			   const struct ovs_conntrack_info *info,
			   struct sk_buff *skb)
{
	/* If we are recirculating packets to match on conntrack fields and
	 * committing with a separate conntrack action,  then we don't need to
	 * actually run the packet through conntrack twice unless it's for a
	 * different zone.
	 */
	if (!skb_nfct_cached(net, skb, info)) {
		struct nf_conn *tmpl = info->ct;

		/* Associate skb with specified zone. */
		if (tmpl) {
			if (skb->nfct)
				nf_conntrack_put(skb->nfct);
			nf_conntrack_get(&tmpl->ct_general);
			skb->nfct = &tmpl->ct_general;
			skb->nfctinfo = IP_CT_NEW;
		}

		if (nf_conntrack_in(net, info->family, NF_INET_PRE_ROUTING,
				    skb) != NF_ACCEPT)
			return -ENOENT;

		if (ovs_ct_helper(skb, info->family) != NF_ACCEPT) {
			WARN_ONCE(1, "helper rejected packet");
			return -EINVAL;
		}
	}

	ovs_ct_update_key(skb, info, key, true);

	return 0;
}

/* Lookup connection and read fields into key. */
static int ovs_ct_lookup(struct net *net, struct sw_flow_key *key,
			 const struct ovs_conntrack_info *info,
			 struct sk_buff *skb)
{
	struct nf_conntrack_expect *exp;

	exp = ovs_ct_expect_find(net, &info->zone, info->family, skb);
	if (exp) {
		u8 state;

		state = OVS_CS_F_TRACKED | OVS_CS_F_NEW | OVS_CS_F_RELATED;
		__ovs_ct_update_key(key, state, &info->zone, exp->master);
	} else {
		int err;

		err = __ovs_ct_lookup(net, key, info, skb);
		if (err)
			return err;
	}

	return 0;
}

/* Lookup connection and confirm if unconfirmed. */
static int ovs_ct_commit(struct net *net, struct sw_flow_key *key,
			 const struct ovs_conntrack_info *info,
			 struct sk_buff *skb)
{
	u8 state;
	int err;

	state = key->ct.state;
	if (key->ct.zone == info->zone.id &&
	    ((state & OVS_CS_F_TRACKED) && !(state & OVS_CS_F_NEW))) {
		/* Previous lookup has shown that this connection is already
		 * tracked and committed. Skip committing.
		 */
		return 0;
	}

	err = __ovs_ct_lookup(net, key, info, skb);
	if (err)
		return err;
	if (nf_conntrack_confirm(skb) != NF_ACCEPT)
		return -EINVAL;

	return 0;
}

static bool labels_nonzero(const struct ovs_key_ct_labels *labels)
{
	size_t i;

	for (i = 0; i < sizeof(*labels); i++)
		if (labels->ct_labels[i])
			return true;

	return false;
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
	skb_pull(skb, nh_ofs);

	if (key->ip.frag != OVS_FRAG_TYPE_NONE) {
		err = handle_fragments(net, key, info->zone.id, skb);
		if (err)
			return err;
	}

	if (info->commit)
		err = ovs_ct_commit(net, key, info, skb);
	else
		err = ovs_ct_lookup(net, key, info, skb);
	if (err)
		goto err;

	if (info->mark.mask) {
		err = ovs_ct_set_mark(skb, key, info->mark.value,
				      info->mark.mask);
		if (err)
			goto err;
	}
	if (labels_nonzero(&info->labels.mask))
		err = ovs_ct_set_labels(skb, key, &info->labels.value,
					&info->labels.mask);
err:
	skb_push(skb, nh_ofs);
	if (err)
		kfree_skb(skb);
	return err;
}

static int ovs_ct_add_helper(struct ovs_conntrack_info *info, const char *name,
			     const struct sw_flow_key *key, bool log)
{
	struct nf_conntrack_helper *helper;
	struct nf_conn_help *help;

	helper = nf_conntrack_helper_try_module_get(name, info->family,
						    key->ip.proto);
	if (!helper) {
		OVS_NLERR(log, "Unknown helper \"%s\"", name);
		return -EINVAL;
	}

	help = nf_ct_helper_ext_add(info->ct, helper, GFP_KERNEL);
	if (!help) {
		module_put(helper->me);
		return -ENOMEM;
	}

	rcu_assign_pointer(help->helper, helper);
	info->helper = helper;
	return 0;
}

static const struct ovs_ct_len_tbl ovs_ct_attr_lens[OVS_CT_ATTR_MAX + 1] = {
	[OVS_CT_ATTR_COMMIT]	= { .minlen = 0, .maxlen = 0 },
	[OVS_CT_ATTR_ZONE]	= { .minlen = sizeof(u16),
				    .maxlen = sizeof(u16) },
	[OVS_CT_ATTR_MARK]	= { .minlen = sizeof(struct md_mark),
				    .maxlen = sizeof(struct md_mark) },
	[OVS_CT_ATTR_LABELS]	= { .minlen = sizeof(struct md_labels),
				    .maxlen = sizeof(struct md_labels) },
	[OVS_CT_ATTR_HELPER]	= { .minlen = 1,
				    .maxlen = NF_CT_HELPER_NAME_LEN }
};

static int parse_ct(const struct nlattr *attr, struct ovs_conntrack_info *info,
		    const char **helper, bool log)
{
	struct nlattr *a;
	int rem;

	nla_for_each_nested(a, attr, rem) {
		int type = nla_type(a);
		int maxlen = ovs_ct_attr_lens[type].maxlen;
		int minlen = ovs_ct_attr_lens[type].minlen;

		if (type > OVS_CT_ATTR_MAX) {
			OVS_NLERR(log,
				  "Unknown conntrack attr (type=%d, max=%d)",
				  type, OVS_CT_ATTR_MAX);
			return -EINVAL;
		}
		if (nla_len(a) < minlen || nla_len(a) > maxlen) {
			OVS_NLERR(log,
				  "Conntrack attr type has unexpected length (type=%d, length=%d, expected=%d)",
				  type, nla_len(a), maxlen);
			return -EINVAL;
		}

		switch (type) {
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
		default:
			OVS_NLERR(log, "Unknown conntrack attr (%d)",
				  type);
			return -EINVAL;
		}
	}

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

	__set_bit(IPS_CONFIRMED_BIT, &ct_info.ct->status);
	nf_conntrack_get(&ct_info.ct->ct_general);

	if (helper) {
		err = ovs_ct_add_helper(&ct_info, helper, key, log);
		if (err)
			goto err_free_ct;
	}

	err = ovs_nla_add_action(sfa, OVS_ACTION_ATTR_CT, &ct_info,
				 sizeof(ct_info), log);
	if (err)
		goto err_free_ct;

	return 0;
err_free_ct:
	__ovs_ct_free_action(&ct_info);
	return err;
}

int ovs_ct_action_to_attr(const struct ovs_conntrack_info *ct_info,
			  struct sk_buff *skb)
{
	struct nlattr *start;

	start = nla_nest_start(skb, OVS_ACTION_ATTR_CT);
	if (!start)
		return -EMSGSIZE;

	if (ct_info->commit && nla_put_flag(skb, OVS_CT_ATTR_COMMIT))
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
	if (ct_info->helper)
		module_put(ct_info->helper->me);
	if (ct_info->ct)
		nf_ct_put(ct_info->ct);
}

void ovs_ct_init(struct net *net)
{
	unsigned int n_bits = sizeof(struct ovs_key_ct_labels) * BITS_PER_BYTE;
	struct ovs_net *ovs_net = net_generic(net, ovs_net_id);

	if (nf_connlabels_get(net, n_bits)) {
		ovs_net->xt_label = false;
		OVS_NLERR(true, "Failed to set connlabel length");
	} else {
		ovs_net->xt_label = true;
	}
}

void ovs_ct_exit(struct net *net)
{
	struct ovs_net *ovs_net = net_generic(net, ovs_net_id);

	if (ovs_net->xt_label)
		nf_connlabels_put(net);
}
