// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* -
 * net/sched/act_ct.c  Connection Tracking action
 *
 * Authors:   Paul Blakey <paulb@mellanox.com>
 *            Yossi Kuperman <yossiku@mellanox.com>
 *            Marcelo Ricardo Leitner <marcelo.leitner@gmail.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/pkt_cls.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/rhashtable.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <net/act_api.h>
#include <net/ip.h>
#include <net/ipv6_frag.h>
#include <uapi/linux/tc_act/tc_ct.h>
#include <net/tc_act/tc_ct.h>
#include <net/tc_wrapper.h>

#include <net/netfilter/nf_flow_table.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_acct.h>
#include <net/netfilter/ipv6/nf_defrag_ipv6.h>
#include <net/netfilter/nf_conntrack_act_ct.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <uapi/linux/netfilter/nf_nat.h>

static struct workqueue_struct *act_ct_wq;
static struct rhashtable zones_ht;
static DEFINE_MUTEX(zones_mutex);

struct tcf_ct_flow_table {
	struct rhash_head node; /* In zones tables */

	struct rcu_work rwork;
	struct nf_flowtable nf_ft;
	refcount_t ref;
	u16 zone;

	bool dying;
};

static const struct rhashtable_params zones_params = {
	.head_offset = offsetof(struct tcf_ct_flow_table, node),
	.key_offset = offsetof(struct tcf_ct_flow_table, zone),
	.key_len = sizeof_field(struct tcf_ct_flow_table, zone),
	.automatic_shrinking = true,
};

static struct flow_action_entry *
tcf_ct_flow_table_flow_action_get_next(struct flow_action *flow_action)
{
	int i = flow_action->num_entries++;

	return &flow_action->entries[i];
}

static void tcf_ct_add_mangle_action(struct flow_action *action,
				     enum flow_action_mangle_base htype,
				     u32 offset,
				     u32 mask,
				     u32 val)
{
	struct flow_action_entry *entry;

	entry = tcf_ct_flow_table_flow_action_get_next(action);
	entry->id = FLOW_ACTION_MANGLE;
	entry->mangle.htype = htype;
	entry->mangle.mask = ~mask;
	entry->mangle.offset = offset;
	entry->mangle.val = val;
}

/* The following nat helper functions check if the inverted reverse tuple
 * (target) is different then the current dir tuple - meaning nat for ports
 * and/or ip is needed, and add the relevant mangle actions.
 */
static void
tcf_ct_flow_table_add_action_nat_ipv4(const struct nf_conntrack_tuple *tuple,
				      struct nf_conntrack_tuple target,
				      struct flow_action *action)
{
	if (memcmp(&target.src.u3, &tuple->src.u3, sizeof(target.src.u3)))
		tcf_ct_add_mangle_action(action, FLOW_ACT_MANGLE_HDR_TYPE_IP4,
					 offsetof(struct iphdr, saddr),
					 0xFFFFFFFF,
					 be32_to_cpu(target.src.u3.ip));
	if (memcmp(&target.dst.u3, &tuple->dst.u3, sizeof(target.dst.u3)))
		tcf_ct_add_mangle_action(action, FLOW_ACT_MANGLE_HDR_TYPE_IP4,
					 offsetof(struct iphdr, daddr),
					 0xFFFFFFFF,
					 be32_to_cpu(target.dst.u3.ip));
}

static void
tcf_ct_add_ipv6_addr_mangle_action(struct flow_action *action,
				   union nf_inet_addr *addr,
				   u32 offset)
{
	int i;

	for (i = 0; i < sizeof(struct in6_addr) / sizeof(u32); i++)
		tcf_ct_add_mangle_action(action, FLOW_ACT_MANGLE_HDR_TYPE_IP6,
					 i * sizeof(u32) + offset,
					 0xFFFFFFFF, be32_to_cpu(addr->ip6[i]));
}

static void
tcf_ct_flow_table_add_action_nat_ipv6(const struct nf_conntrack_tuple *tuple,
				      struct nf_conntrack_tuple target,
				      struct flow_action *action)
{
	if (memcmp(&target.src.u3, &tuple->src.u3, sizeof(target.src.u3)))
		tcf_ct_add_ipv6_addr_mangle_action(action, &target.src.u3,
						   offsetof(struct ipv6hdr,
							    saddr));
	if (memcmp(&target.dst.u3, &tuple->dst.u3, sizeof(target.dst.u3)))
		tcf_ct_add_ipv6_addr_mangle_action(action, &target.dst.u3,
						   offsetof(struct ipv6hdr,
							    daddr));
}

static void
tcf_ct_flow_table_add_action_nat_tcp(const struct nf_conntrack_tuple *tuple,
				     struct nf_conntrack_tuple target,
				     struct flow_action *action)
{
	__be16 target_src = target.src.u.tcp.port;
	__be16 target_dst = target.dst.u.tcp.port;

	if (target_src != tuple->src.u.tcp.port)
		tcf_ct_add_mangle_action(action, FLOW_ACT_MANGLE_HDR_TYPE_TCP,
					 offsetof(struct tcphdr, source),
					 0xFFFF, be16_to_cpu(target_src));
	if (target_dst != tuple->dst.u.tcp.port)
		tcf_ct_add_mangle_action(action, FLOW_ACT_MANGLE_HDR_TYPE_TCP,
					 offsetof(struct tcphdr, dest),
					 0xFFFF, be16_to_cpu(target_dst));
}

static void
tcf_ct_flow_table_add_action_nat_udp(const struct nf_conntrack_tuple *tuple,
				     struct nf_conntrack_tuple target,
				     struct flow_action *action)
{
	__be16 target_src = target.src.u.udp.port;
	__be16 target_dst = target.dst.u.udp.port;

	if (target_src != tuple->src.u.udp.port)
		tcf_ct_add_mangle_action(action, FLOW_ACT_MANGLE_HDR_TYPE_UDP,
					 offsetof(struct udphdr, source),
					 0xFFFF, be16_to_cpu(target_src));
	if (target_dst != tuple->dst.u.udp.port)
		tcf_ct_add_mangle_action(action, FLOW_ACT_MANGLE_HDR_TYPE_UDP,
					 offsetof(struct udphdr, dest),
					 0xFFFF, be16_to_cpu(target_dst));
}

static void tcf_ct_flow_table_add_action_meta(struct nf_conn *ct,
					      enum ip_conntrack_dir dir,
					      enum ip_conntrack_info ctinfo,
					      struct flow_action *action)
{
	struct nf_conn_labels *ct_labels;
	struct flow_action_entry *entry;
	u32 *act_ct_labels;

	entry = tcf_ct_flow_table_flow_action_get_next(action);
	entry->id = FLOW_ACTION_CT_METADATA;
#if IS_ENABLED(CONFIG_NF_CONNTRACK_MARK)
	entry->ct_metadata.mark = READ_ONCE(ct->mark);
#endif
	/* aligns with the CT reference on the SKB nf_ct_set */
	entry->ct_metadata.cookie = (unsigned long)ct | ctinfo;
	entry->ct_metadata.orig_dir = dir == IP_CT_DIR_ORIGINAL;

	act_ct_labels = entry->ct_metadata.labels;
	ct_labels = nf_ct_labels_find(ct);
	if (ct_labels)
		memcpy(act_ct_labels, ct_labels->bits, NF_CT_LABELS_MAX_SIZE);
	else
		memset(act_ct_labels, 0, NF_CT_LABELS_MAX_SIZE);
}

static int tcf_ct_flow_table_add_action_nat(struct net *net,
					    struct nf_conn *ct,
					    enum ip_conntrack_dir dir,
					    struct flow_action *action)
{
	const struct nf_conntrack_tuple *tuple = &ct->tuplehash[dir].tuple;
	struct nf_conntrack_tuple target;

	if (!(ct->status & IPS_NAT_MASK))
		return 0;

	nf_ct_invert_tuple(&target, &ct->tuplehash[!dir].tuple);

	switch (tuple->src.l3num) {
	case NFPROTO_IPV4:
		tcf_ct_flow_table_add_action_nat_ipv4(tuple, target,
						      action);
		break;
	case NFPROTO_IPV6:
		tcf_ct_flow_table_add_action_nat_ipv6(tuple, target,
						      action);
		break;
	default:
		return -EOPNOTSUPP;
	}

	switch (nf_ct_protonum(ct)) {
	case IPPROTO_TCP:
		tcf_ct_flow_table_add_action_nat_tcp(tuple, target, action);
		break;
	case IPPROTO_UDP:
		tcf_ct_flow_table_add_action_nat_udp(tuple, target, action);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int tcf_ct_flow_table_fill_actions(struct net *net,
					  struct flow_offload *flow,
					  enum flow_offload_tuple_dir tdir,
					  struct nf_flow_rule *flow_rule)
{
	struct flow_action *action = &flow_rule->rule->action;
	int num_entries = action->num_entries;
	struct nf_conn *ct = flow->ct;
	enum ip_conntrack_info ctinfo;
	enum ip_conntrack_dir dir;
	int i, err;

	switch (tdir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		dir = IP_CT_DIR_ORIGINAL;
		ctinfo = test_bit(IPS_SEEN_REPLY_BIT, &ct->status) ?
			IP_CT_ESTABLISHED : IP_CT_NEW;
		if (ctinfo == IP_CT_ESTABLISHED)
			set_bit(NF_FLOW_HW_ESTABLISHED, &flow->flags);
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		dir = IP_CT_DIR_REPLY;
		ctinfo = IP_CT_ESTABLISHED_REPLY;
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = tcf_ct_flow_table_add_action_nat(net, ct, dir, action);
	if (err)
		goto err_nat;

	tcf_ct_flow_table_add_action_meta(ct, dir, ctinfo, action);
	return 0;

err_nat:
	/* Clear filled actions */
	for (i = num_entries; i < action->num_entries; i++)
		memset(&action->entries[i], 0, sizeof(action->entries[i]));
	action->num_entries = num_entries;

	return err;
}

static bool tcf_ct_flow_is_outdated(const struct flow_offload *flow)
{
	return test_bit(IPS_SEEN_REPLY_BIT, &flow->ct->status) &&
	       test_bit(IPS_HW_OFFLOAD_BIT, &flow->ct->status) &&
	       !test_bit(NF_FLOW_HW_PENDING, &flow->flags) &&
	       !test_bit(NF_FLOW_HW_ESTABLISHED, &flow->flags);
}

static struct nf_flowtable_type flowtable_ct = {
	.gc		= tcf_ct_flow_is_outdated,
	.action		= tcf_ct_flow_table_fill_actions,
	.owner		= THIS_MODULE,
};

static int tcf_ct_flow_table_get(struct net *net, struct tcf_ct_params *params)
{
	struct tcf_ct_flow_table *ct_ft;
	int err = -ENOMEM;

	mutex_lock(&zones_mutex);
	ct_ft = rhashtable_lookup_fast(&zones_ht, &params->zone, zones_params);
	if (ct_ft && refcount_inc_not_zero(&ct_ft->ref))
		goto out_unlock;

	ct_ft = kzalloc(sizeof(*ct_ft), GFP_KERNEL);
	if (!ct_ft)
		goto err_alloc;
	refcount_set(&ct_ft->ref, 1);

	ct_ft->zone = params->zone;
	err = rhashtable_insert_fast(&zones_ht, &ct_ft->node, zones_params);
	if (err)
		goto err_insert;

	ct_ft->nf_ft.type = &flowtable_ct;
	ct_ft->nf_ft.flags |= NF_FLOWTABLE_HW_OFFLOAD |
			      NF_FLOWTABLE_COUNTER;
	err = nf_flow_table_init(&ct_ft->nf_ft);
	if (err)
		goto err_init;
	write_pnet(&ct_ft->nf_ft.net, net);

	__module_get(THIS_MODULE);
out_unlock:
	params->ct_ft = ct_ft;
	params->nf_ft = &ct_ft->nf_ft;
	mutex_unlock(&zones_mutex);

	return 0;

err_init:
	rhashtable_remove_fast(&zones_ht, &ct_ft->node, zones_params);
err_insert:
	kfree(ct_ft);
err_alloc:
	mutex_unlock(&zones_mutex);
	return err;
}

static void tcf_ct_flow_table_cleanup_work(struct work_struct *work)
{
	struct flow_block_cb *block_cb, *tmp_cb;
	struct tcf_ct_flow_table *ct_ft;
	struct flow_block *block;

	ct_ft = container_of(to_rcu_work(work), struct tcf_ct_flow_table,
			     rwork);
	nf_flow_table_free(&ct_ft->nf_ft);

	/* Remove any remaining callbacks before cleanup */
	block = &ct_ft->nf_ft.flow_block;
	down_write(&ct_ft->nf_ft.flow_block_lock);
	list_for_each_entry_safe(block_cb, tmp_cb, &block->cb_list, list) {
		list_del(&block_cb->list);
		flow_block_cb_free(block_cb);
	}
	up_write(&ct_ft->nf_ft.flow_block_lock);
	kfree(ct_ft);

	module_put(THIS_MODULE);
}

static void tcf_ct_flow_table_put(struct tcf_ct_flow_table *ct_ft)
{
	if (refcount_dec_and_test(&ct_ft->ref)) {
		rhashtable_remove_fast(&zones_ht, &ct_ft->node, zones_params);
		INIT_RCU_WORK(&ct_ft->rwork, tcf_ct_flow_table_cleanup_work);
		queue_rcu_work(act_ct_wq, &ct_ft->rwork);
	}
}

static void tcf_ct_flow_tc_ifidx(struct flow_offload *entry,
				 struct nf_conn_act_ct_ext *act_ct_ext, u8 dir)
{
	entry->tuplehash[dir].tuple.xmit_type = FLOW_OFFLOAD_XMIT_TC;
	entry->tuplehash[dir].tuple.tc.iifidx = act_ct_ext->ifindex[dir];
}

static void tcf_ct_flow_ct_ext_ifidx_update(struct flow_offload *entry)
{
	struct nf_conn_act_ct_ext *act_ct_ext;

	act_ct_ext = nf_conn_act_ct_ext_find(entry->ct);
	if (act_ct_ext) {
		tcf_ct_flow_tc_ifidx(entry, act_ct_ext, FLOW_OFFLOAD_DIR_ORIGINAL);
		tcf_ct_flow_tc_ifidx(entry, act_ct_ext, FLOW_OFFLOAD_DIR_REPLY);
	}
}

static void tcf_ct_flow_table_add(struct tcf_ct_flow_table *ct_ft,
				  struct nf_conn *ct,
				  bool tcp, bool bidirectional)
{
	struct nf_conn_act_ct_ext *act_ct_ext;
	struct flow_offload *entry;
	int err;

	if (test_and_set_bit(IPS_OFFLOAD_BIT, &ct->status))
		return;

	entry = flow_offload_alloc(ct);
	if (!entry) {
		WARN_ON_ONCE(1);
		goto err_alloc;
	}

	if (tcp) {
		ct->proto.tcp.seen[0].flags |= IP_CT_TCP_FLAG_BE_LIBERAL;
		ct->proto.tcp.seen[1].flags |= IP_CT_TCP_FLAG_BE_LIBERAL;
	}
	if (bidirectional)
		__set_bit(NF_FLOW_HW_BIDIRECTIONAL, &entry->flags);

	act_ct_ext = nf_conn_act_ct_ext_find(ct);
	if (act_ct_ext) {
		tcf_ct_flow_tc_ifidx(entry, act_ct_ext, FLOW_OFFLOAD_DIR_ORIGINAL);
		tcf_ct_flow_tc_ifidx(entry, act_ct_ext, FLOW_OFFLOAD_DIR_REPLY);
	}

	err = flow_offload_add(&ct_ft->nf_ft, entry);
	if (err)
		goto err_add;

	return;

err_add:
	flow_offload_free(entry);
err_alloc:
	clear_bit(IPS_OFFLOAD_BIT, &ct->status);
}

static void tcf_ct_flow_table_process_conn(struct tcf_ct_flow_table *ct_ft,
					   struct nf_conn *ct,
					   enum ip_conntrack_info ctinfo)
{
	bool tcp = false, bidirectional = true;

	switch (nf_ct_protonum(ct)) {
	case IPPROTO_TCP:
		if ((ctinfo != IP_CT_ESTABLISHED &&
		     ctinfo != IP_CT_ESTABLISHED_REPLY) ||
		    !test_bit(IPS_ASSURED_BIT, &ct->status) ||
		    ct->proto.tcp.state != TCP_CONNTRACK_ESTABLISHED)
			return;

		tcp = true;
		break;
	case IPPROTO_UDP:
		if (!nf_ct_is_confirmed(ct))
			return;
		if (!test_bit(IPS_ASSURED_BIT, &ct->status))
			bidirectional = false;
		break;
#ifdef CONFIG_NF_CT_PROTO_GRE
	case IPPROTO_GRE: {
		struct nf_conntrack_tuple *tuple;

		if ((ctinfo != IP_CT_ESTABLISHED &&
		     ctinfo != IP_CT_ESTABLISHED_REPLY) ||
		    !test_bit(IPS_ASSURED_BIT, &ct->status) ||
		    ct->status & IPS_NAT_MASK)
			return;

		tuple = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
		/* No support for GRE v1 */
		if (tuple->src.u.gre.key || tuple->dst.u.gre.key)
			return;
		break;
	}
#endif
	default:
		return;
	}

	if (nf_ct_ext_exist(ct, NF_CT_EXT_HELPER) ||
	    ct->status & IPS_SEQ_ADJUST)
		return;

	tcf_ct_flow_table_add(ct_ft, ct, tcp, bidirectional);
}

static bool
tcf_ct_flow_table_fill_tuple_ipv4(struct sk_buff *skb,
				  struct flow_offload_tuple *tuple,
				  struct tcphdr **tcph)
{
	struct flow_ports *ports;
	unsigned int thoff;
	struct iphdr *iph;
	size_t hdrsize;
	u8 ipproto;

	if (!pskb_network_may_pull(skb, sizeof(*iph)))
		return false;

	iph = ip_hdr(skb);
	thoff = iph->ihl * 4;

	if (ip_is_fragment(iph) ||
	    unlikely(thoff != sizeof(struct iphdr)))
		return false;

	ipproto = iph->protocol;
	switch (ipproto) {
	case IPPROTO_TCP:
		hdrsize = sizeof(struct tcphdr);
		break;
	case IPPROTO_UDP:
		hdrsize = sizeof(*ports);
		break;
#ifdef CONFIG_NF_CT_PROTO_GRE
	case IPPROTO_GRE:
		hdrsize = sizeof(struct gre_base_hdr);
		break;
#endif
	default:
		return false;
	}

	if (iph->ttl <= 1)
		return false;

	if (!pskb_network_may_pull(skb, thoff + hdrsize))
		return false;

	switch (ipproto) {
	case IPPROTO_TCP:
		*tcph = (void *)(skb_network_header(skb) + thoff);
		fallthrough;
	case IPPROTO_UDP:
		ports = (struct flow_ports *)(skb_network_header(skb) + thoff);
		tuple->src_port = ports->source;
		tuple->dst_port = ports->dest;
		break;
	case IPPROTO_GRE: {
		struct gre_base_hdr *greh;

		greh = (struct gre_base_hdr *)(skb_network_header(skb) + thoff);
		if ((greh->flags & GRE_VERSION) != GRE_VERSION_0)
			return false;
		break;
	}
	}

	iph = ip_hdr(skb);

	tuple->src_v4.s_addr = iph->saddr;
	tuple->dst_v4.s_addr = iph->daddr;
	tuple->l3proto = AF_INET;
	tuple->l4proto = ipproto;

	return true;
}

static bool
tcf_ct_flow_table_fill_tuple_ipv6(struct sk_buff *skb,
				  struct flow_offload_tuple *tuple,
				  struct tcphdr **tcph)
{
	struct flow_ports *ports;
	struct ipv6hdr *ip6h;
	unsigned int thoff;
	size_t hdrsize;
	u8 nexthdr;

	if (!pskb_network_may_pull(skb, sizeof(*ip6h)))
		return false;

	ip6h = ipv6_hdr(skb);
	thoff = sizeof(*ip6h);

	nexthdr = ip6h->nexthdr;
	switch (nexthdr) {
	case IPPROTO_TCP:
		hdrsize = sizeof(struct tcphdr);
		break;
	case IPPROTO_UDP:
		hdrsize = sizeof(*ports);
		break;
#ifdef CONFIG_NF_CT_PROTO_GRE
	case IPPROTO_GRE:
		hdrsize = sizeof(struct gre_base_hdr);
		break;
#endif
	default:
		return false;
	}

	if (ip6h->hop_limit <= 1)
		return false;

	if (!pskb_network_may_pull(skb, thoff + hdrsize))
		return false;

	switch (nexthdr) {
	case IPPROTO_TCP:
		*tcph = (void *)(skb_network_header(skb) + thoff);
		fallthrough;
	case IPPROTO_UDP:
		ports = (struct flow_ports *)(skb_network_header(skb) + thoff);
		tuple->src_port = ports->source;
		tuple->dst_port = ports->dest;
		break;
	case IPPROTO_GRE: {
		struct gre_base_hdr *greh;

		greh = (struct gre_base_hdr *)(skb_network_header(skb) + thoff);
		if ((greh->flags & GRE_VERSION) != GRE_VERSION_0)
			return false;
		break;
	}
	}

	ip6h = ipv6_hdr(skb);

	tuple->src_v6 = ip6h->saddr;
	tuple->dst_v6 = ip6h->daddr;
	tuple->l3proto = AF_INET6;
	tuple->l4proto = nexthdr;

	return true;
}

static bool tcf_ct_flow_table_lookup(struct tcf_ct_params *p,
				     struct sk_buff *skb,
				     u8 family)
{
	struct nf_flowtable *nf_ft = &p->ct_ft->nf_ft;
	struct flow_offload_tuple_rhash *tuplehash;
	struct flow_offload_tuple tuple = {};
	enum ip_conntrack_info ctinfo;
	struct tcphdr *tcph = NULL;
	bool force_refresh = false;
	struct flow_offload *flow;
	struct nf_conn *ct;
	u8 dir;

	switch (family) {
	case NFPROTO_IPV4:
		if (!tcf_ct_flow_table_fill_tuple_ipv4(skb, &tuple, &tcph))
			return false;
		break;
	case NFPROTO_IPV6:
		if (!tcf_ct_flow_table_fill_tuple_ipv6(skb, &tuple, &tcph))
			return false;
		break;
	default:
		return false;
	}

	tuplehash = flow_offload_lookup(nf_ft, &tuple);
	if (!tuplehash)
		return false;

	dir = tuplehash->tuple.dir;
	flow = container_of(tuplehash, struct flow_offload, tuplehash[dir]);
	ct = flow->ct;

	if (dir == FLOW_OFFLOAD_DIR_REPLY &&
	    !test_bit(NF_FLOW_HW_BIDIRECTIONAL, &flow->flags)) {
		/* Only offload reply direction after connection became
		 * assured.
		 */
		if (test_bit(IPS_ASSURED_BIT, &ct->status))
			set_bit(NF_FLOW_HW_BIDIRECTIONAL, &flow->flags);
		else if (test_bit(NF_FLOW_HW_ESTABLISHED, &flow->flags))
			/* If flow_table flow has already been updated to the
			 * established state, then don't refresh.
			 */
			return false;
		force_refresh = true;
	}

	if (tcph && (unlikely(tcph->fin || tcph->rst))) {
		flow_offload_teardown(flow);
		return false;
	}

	if (dir == FLOW_OFFLOAD_DIR_ORIGINAL)
		ctinfo = test_bit(IPS_SEEN_REPLY_BIT, &ct->status) ?
			IP_CT_ESTABLISHED : IP_CT_NEW;
	else
		ctinfo = IP_CT_ESTABLISHED_REPLY;

	nf_conn_act_ct_ext_fill(skb, ct, ctinfo);
	tcf_ct_flow_ct_ext_ifidx_update(flow);
	flow_offload_refresh(nf_ft, flow, force_refresh);
	if (!test_bit(IPS_ASSURED_BIT, &ct->status)) {
		/* Process this flow in SW to allow promoting to ASSURED */
		return false;
	}

	nf_conntrack_get(&ct->ct_general);
	nf_ct_set(skb, ct, ctinfo);
	if (nf_ft->flags & NF_FLOWTABLE_COUNTER)
		nf_ct_acct_update(ct, dir, skb->len);

	return true;
}

static int tcf_ct_flow_tables_init(void)
{
	return rhashtable_init(&zones_ht, &zones_params);
}

static void tcf_ct_flow_tables_uninit(void)
{
	rhashtable_destroy(&zones_ht);
}

static struct tc_action_ops act_ct_ops;

struct tc_ct_action_net {
	struct tc_action_net tn; /* Must be first */
	bool labels;
};

/* Determine whether skb->_nfct is equal to the result of conntrack lookup. */
static bool tcf_ct_skb_nfct_cached(struct net *net, struct sk_buff *skb,
				   struct tcf_ct_params *p)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return false;
	if (!net_eq(net, read_pnet(&ct->ct_net)))
		goto drop_ct;
	if (nf_ct_zone(ct)->id != p->zone)
		goto drop_ct;
	if (p->helper) {
		struct nf_conn_help *help;

		help = nf_ct_ext_find(ct, NF_CT_EXT_HELPER);
		if (help && rcu_access_pointer(help->helper) != p->helper)
			goto drop_ct;
	}

	/* Force conntrack entry direction. */
	if ((p->ct_action & TCA_CT_ACT_FORCE) &&
	    CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL) {
		if (nf_ct_is_confirmed(ct))
			nf_ct_kill(ct);

		goto drop_ct;
	}

	return true;

drop_ct:
	nf_ct_put(ct);
	nf_ct_set(skb, NULL, IP_CT_UNTRACKED);

	return false;
}

static u8 tcf_ct_skb_nf_family(struct sk_buff *skb)
{
	u8 family = NFPROTO_UNSPEC;

	switch (skb_protocol(skb, true)) {
	case htons(ETH_P_IP):
		family = NFPROTO_IPV4;
		break;
	case htons(ETH_P_IPV6):
		family = NFPROTO_IPV6;
		break;
	default:
		break;
	}

	return family;
}

static int tcf_ct_ipv4_is_fragment(struct sk_buff *skb, bool *frag)
{
	unsigned int len;

	len =  skb_network_offset(skb) + sizeof(struct iphdr);
	if (unlikely(skb->len < len))
		return -EINVAL;
	if (unlikely(!pskb_may_pull(skb, len)))
		return -ENOMEM;

	*frag = ip_is_fragment(ip_hdr(skb));
	return 0;
}

static int tcf_ct_ipv6_is_fragment(struct sk_buff *skb, bool *frag)
{
	unsigned int flags = 0, len, payload_ofs = 0;
	unsigned short frag_off;
	int nexthdr;

	len =  skb_network_offset(skb) + sizeof(struct ipv6hdr);
	if (unlikely(skb->len < len))
		return -EINVAL;
	if (unlikely(!pskb_may_pull(skb, len)))
		return -ENOMEM;

	nexthdr = ipv6_find_hdr(skb, &payload_ofs, -1, &frag_off, &flags);
	if (unlikely(nexthdr < 0))
		return -EPROTO;

	*frag = flags & IP6_FH_F_FRAG;
	return 0;
}

static int tcf_ct_handle_fragments(struct net *net, struct sk_buff *skb,
				   u8 family, u16 zone, bool *defrag)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	int err = 0;
	bool frag;
	u8 proto;
	u16 mru;

	/* Previously seen (loopback)? Ignore. */
	ct = nf_ct_get(skb, &ctinfo);
	if ((ct && !nf_ct_is_template(ct)) || ctinfo == IP_CT_UNTRACKED)
		return 0;

	if (family == NFPROTO_IPV4)
		err = tcf_ct_ipv4_is_fragment(skb, &frag);
	else
		err = tcf_ct_ipv6_is_fragment(skb, &frag);
	if (err || !frag)
		return err;

	skb_get(skb);
	err = nf_ct_handle_fragments(net, skb, zone, family, &proto, &mru);
	if (err)
		return err;

	*defrag = true;
	tc_skb_cb(skb)->mru = mru;

	return 0;
}

static void tcf_ct_params_free(struct tcf_ct_params *params)
{
	if (params->helper) {
#if IS_ENABLED(CONFIG_NF_NAT)
		if (params->ct_action & TCA_CT_ACT_NAT)
			nf_nat_helper_put(params->helper);
#endif
		nf_conntrack_helper_put(params->helper);
	}
	if (params->ct_ft)
		tcf_ct_flow_table_put(params->ct_ft);
	if (params->tmpl)
		nf_ct_put(params->tmpl);
	kfree(params);
}

static void tcf_ct_params_free_rcu(struct rcu_head *head)
{
	struct tcf_ct_params *params;

	params = container_of(head, struct tcf_ct_params, rcu);
	tcf_ct_params_free(params);
}

static void tcf_ct_act_set_mark(struct nf_conn *ct, u32 mark, u32 mask)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK_MARK)
	u32 new_mark;

	if (!mask)
		return;

	new_mark = mark | (READ_ONCE(ct->mark) & ~(mask));
	if (READ_ONCE(ct->mark) != new_mark) {
		WRITE_ONCE(ct->mark, new_mark);
		if (nf_ct_is_confirmed(ct))
			nf_conntrack_event_cache(IPCT_MARK, ct);
	}
#endif
}

static void tcf_ct_act_set_labels(struct nf_conn *ct,
				  u32 *labels,
				  u32 *labels_m)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK_LABELS)
	size_t labels_sz = sizeof_field(struct tcf_ct_params, labels);

	if (!memchr_inv(labels_m, 0, labels_sz))
		return;

	nf_connlabels_replace(ct, labels, labels_m, 4);
#endif
}

static int tcf_ct_act_nat(struct sk_buff *skb,
			  struct nf_conn *ct,
			  enum ip_conntrack_info ctinfo,
			  int ct_action,
			  struct nf_nat_range2 *range,
			  bool commit)
{
#if IS_ENABLED(CONFIG_NF_NAT)
	int err, action = 0;

	if (!(ct_action & TCA_CT_ACT_NAT))
		return NF_ACCEPT;
	if (ct_action & TCA_CT_ACT_NAT_SRC)
		action |= BIT(NF_NAT_MANIP_SRC);
	if (ct_action & TCA_CT_ACT_NAT_DST)
		action |= BIT(NF_NAT_MANIP_DST);

	err = nf_ct_nat(skb, ct, ctinfo, &action, range, commit);

	if (action & BIT(NF_NAT_MANIP_SRC))
		tc_skb_cb(skb)->post_ct_snat = 1;
	if (action & BIT(NF_NAT_MANIP_DST))
		tc_skb_cb(skb)->post_ct_dnat = 1;

	return err;
#else
	return NF_ACCEPT;
#endif
}

TC_INDIRECT_SCOPE int tcf_ct_act(struct sk_buff *skb, const struct tc_action *a,
				 struct tcf_result *res)
{
	struct net *net = dev_net(skb->dev);
	enum ip_conntrack_info ctinfo;
	struct tcf_ct *c = to_ct(a);
	struct nf_conn *tmpl = NULL;
	struct nf_hook_state state;
	bool cached, commit, clear;
	int nh_ofs, err, retval;
	struct tcf_ct_params *p;
	bool add_helper = false;
	bool skip_add = false;
	bool defrag = false;
	struct nf_conn *ct;
	u8 family;

	p = rcu_dereference_bh(c->params);

	retval = READ_ONCE(c->tcf_action);
	commit = p->ct_action & TCA_CT_ACT_COMMIT;
	clear = p->ct_action & TCA_CT_ACT_CLEAR;
	tmpl = p->tmpl;

	tcf_lastuse_update(&c->tcf_tm);
	tcf_action_update_bstats(&c->common, skb);

	if (clear) {
		tc_skb_cb(skb)->post_ct = false;
		ct = nf_ct_get(skb, &ctinfo);
		if (ct) {
			nf_ct_put(ct);
			nf_ct_set(skb, NULL, IP_CT_UNTRACKED);
		}

		goto out_clear;
	}

	family = tcf_ct_skb_nf_family(skb);
	if (family == NFPROTO_UNSPEC)
		goto drop;

	/* The conntrack module expects to be working at L3.
	 * We also try to pull the IPv4/6 header to linear area
	 */
	nh_ofs = skb_network_offset(skb);
	skb_pull_rcsum(skb, nh_ofs);
	err = tcf_ct_handle_fragments(net, skb, family, p->zone, &defrag);
	if (err == -EINPROGRESS) {
		retval = TC_ACT_STOLEN;
		goto out_clear;
	}
	if (err)
		goto drop;

	err = nf_ct_skb_network_trim(skb, family);
	if (err)
		goto drop;

	/* If we are recirculating packets to match on ct fields and
	 * committing with a separate ct action, then we don't need to
	 * actually run the packet through conntrack twice unless it's for a
	 * different zone.
	 */
	cached = tcf_ct_skb_nfct_cached(net, skb, p);
	if (!cached) {
		if (tcf_ct_flow_table_lookup(p, skb, family)) {
			skip_add = true;
			goto do_nat;
		}

		/* Associate skb with specified zone. */
		if (tmpl) {
			nf_conntrack_put(skb_nfct(skb));
			nf_conntrack_get(&tmpl->ct_general);
			nf_ct_set(skb, tmpl, IP_CT_NEW);
		}

		state.hook = NF_INET_PRE_ROUTING;
		state.net = net;
		state.pf = family;
		err = nf_conntrack_in(skb, &state);
		if (err != NF_ACCEPT)
			goto out_push;
	}

do_nat:
	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		goto out_push;
	nf_ct_deliver_cached_events(ct);
	nf_conn_act_ct_ext_fill(skb, ct, ctinfo);

	err = tcf_ct_act_nat(skb, ct, ctinfo, p->ct_action, &p->range, commit);
	if (err != NF_ACCEPT)
		goto drop;

	if (!nf_ct_is_confirmed(ct) && commit && p->helper && !nfct_help(ct)) {
		err = __nf_ct_try_assign_helper(ct, p->tmpl, GFP_ATOMIC);
		if (err)
			goto drop;
		add_helper = true;
		if (p->ct_action & TCA_CT_ACT_NAT && !nfct_seqadj(ct)) {
			if (!nfct_seqadj_ext_add(ct))
				goto drop;
		}
	}

	if (nf_ct_is_confirmed(ct) ? ((!cached && !skip_add) || add_helper) : commit) {
		if (nf_ct_helper(skb, ct, ctinfo, family) != NF_ACCEPT)
			goto drop;
	}

	if (commit) {
		tcf_ct_act_set_mark(ct, p->mark, p->mark_mask);
		tcf_ct_act_set_labels(ct, p->labels, p->labels_mask);

		if (!nf_ct_is_confirmed(ct))
			nf_conn_act_ct_ext_add(skb, ct, ctinfo);

		/* This will take care of sending queued events
		 * even if the connection is already confirmed.
		 */
		if (nf_conntrack_confirm(skb) != NF_ACCEPT)
			goto drop;
	}

	if (!skip_add)
		tcf_ct_flow_table_process_conn(p->ct_ft, ct, ctinfo);

out_push:
	skb_push_rcsum(skb, nh_ofs);

	tc_skb_cb(skb)->post_ct = true;
	tc_skb_cb(skb)->zone = p->zone;
out_clear:
	if (defrag)
		qdisc_skb_cb(skb)->pkt_len = skb->len;
	return retval;

drop:
	tcf_action_inc_drop_qstats(&c->common);
	return TC_ACT_SHOT;
}

static const struct nla_policy ct_policy[TCA_CT_MAX + 1] = {
	[TCA_CT_ACTION] = { .type = NLA_U16 },
	[TCA_CT_PARMS] = NLA_POLICY_EXACT_LEN(sizeof(struct tc_ct)),
	[TCA_CT_ZONE] = { .type = NLA_U16 },
	[TCA_CT_MARK] = { .type = NLA_U32 },
	[TCA_CT_MARK_MASK] = { .type = NLA_U32 },
	[TCA_CT_LABELS] = { .type = NLA_BINARY,
			    .len = 128 / BITS_PER_BYTE },
	[TCA_CT_LABELS_MASK] = { .type = NLA_BINARY,
				 .len = 128 / BITS_PER_BYTE },
	[TCA_CT_NAT_IPV4_MIN] = { .type = NLA_U32 },
	[TCA_CT_NAT_IPV4_MAX] = { .type = NLA_U32 },
	[TCA_CT_NAT_IPV6_MIN] = NLA_POLICY_EXACT_LEN(sizeof(struct in6_addr)),
	[TCA_CT_NAT_IPV6_MAX] = NLA_POLICY_EXACT_LEN(sizeof(struct in6_addr)),
	[TCA_CT_NAT_PORT_MIN] = { .type = NLA_U16 },
	[TCA_CT_NAT_PORT_MAX] = { .type = NLA_U16 },
	[TCA_CT_HELPER_NAME] = { .type = NLA_STRING, .len = NF_CT_HELPER_NAME_LEN },
	[TCA_CT_HELPER_FAMILY] = { .type = NLA_U8 },
	[TCA_CT_HELPER_PROTO] = { .type = NLA_U8 },
};

static int tcf_ct_fill_params_nat(struct tcf_ct_params *p,
				  struct tc_ct *parm,
				  struct nlattr **tb,
				  struct netlink_ext_ack *extack)
{
	struct nf_nat_range2 *range;

	if (!(p->ct_action & TCA_CT_ACT_NAT))
		return 0;

	if (!IS_ENABLED(CONFIG_NF_NAT)) {
		NL_SET_ERR_MSG_MOD(extack, "Netfilter nat isn't enabled in kernel");
		return -EOPNOTSUPP;
	}

	if (!(p->ct_action & (TCA_CT_ACT_NAT_SRC | TCA_CT_ACT_NAT_DST)))
		return 0;

	if ((p->ct_action & TCA_CT_ACT_NAT_SRC) &&
	    (p->ct_action & TCA_CT_ACT_NAT_DST)) {
		NL_SET_ERR_MSG_MOD(extack, "dnat and snat can't be enabled at the same time");
		return -EOPNOTSUPP;
	}

	range = &p->range;
	if (tb[TCA_CT_NAT_IPV4_MIN]) {
		struct nlattr *max_attr = tb[TCA_CT_NAT_IPV4_MAX];

		p->ipv4_range = true;
		range->flags |= NF_NAT_RANGE_MAP_IPS;
		range->min_addr.ip =
			nla_get_in_addr(tb[TCA_CT_NAT_IPV4_MIN]);

		range->max_addr.ip = max_attr ?
				     nla_get_in_addr(max_attr) :
				     range->min_addr.ip;
	} else if (tb[TCA_CT_NAT_IPV6_MIN]) {
		struct nlattr *max_attr = tb[TCA_CT_NAT_IPV6_MAX];

		p->ipv4_range = false;
		range->flags |= NF_NAT_RANGE_MAP_IPS;
		range->min_addr.in6 =
			nla_get_in6_addr(tb[TCA_CT_NAT_IPV6_MIN]);

		range->max_addr.in6 = max_attr ?
				      nla_get_in6_addr(max_attr) :
				      range->min_addr.in6;
	}

	if (tb[TCA_CT_NAT_PORT_MIN]) {
		range->flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
		range->min_proto.all = nla_get_be16(tb[TCA_CT_NAT_PORT_MIN]);

		range->max_proto.all = tb[TCA_CT_NAT_PORT_MAX] ?
				       nla_get_be16(tb[TCA_CT_NAT_PORT_MAX]) :
				       range->min_proto.all;
	}

	return 0;
}

static void tcf_ct_set_key_val(struct nlattr **tb,
			       void *val, int val_type,
			       void *mask, int mask_type,
			       int len)
{
	if (!tb[val_type])
		return;
	nla_memcpy(val, tb[val_type], len);

	if (!mask)
		return;

	if (mask_type == TCA_CT_UNSPEC || !tb[mask_type])
		memset(mask, 0xff, len);
	else
		nla_memcpy(mask, tb[mask_type], len);
}

static int tcf_ct_fill_params(struct net *net,
			      struct tcf_ct_params *p,
			      struct tc_ct *parm,
			      struct nlattr **tb,
			      struct netlink_ext_ack *extack)
{
	struct tc_ct_action_net *tn = net_generic(net, act_ct_ops.net_id);
	struct nf_conntrack_zone zone;
	int err, family, proto, len;
	struct nf_conn *tmpl;
	char *name;

	p->zone = NF_CT_DEFAULT_ZONE_ID;

	tcf_ct_set_key_val(tb,
			   &p->ct_action, TCA_CT_ACTION,
			   NULL, TCA_CT_UNSPEC,
			   sizeof(p->ct_action));

	if (p->ct_action & TCA_CT_ACT_CLEAR)
		return 0;

	err = tcf_ct_fill_params_nat(p, parm, tb, extack);
	if (err)
		return err;

	if (tb[TCA_CT_MARK]) {
		if (!IS_ENABLED(CONFIG_NF_CONNTRACK_MARK)) {
			NL_SET_ERR_MSG_MOD(extack, "Conntrack mark isn't enabled.");
			return -EOPNOTSUPP;
		}
		tcf_ct_set_key_val(tb,
				   &p->mark, TCA_CT_MARK,
				   &p->mark_mask, TCA_CT_MARK_MASK,
				   sizeof(p->mark));
	}

	if (tb[TCA_CT_LABELS]) {
		if (!IS_ENABLED(CONFIG_NF_CONNTRACK_LABELS)) {
			NL_SET_ERR_MSG_MOD(extack, "Conntrack labels isn't enabled.");
			return -EOPNOTSUPP;
		}

		if (!tn->labels) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to set connlabel length");
			return -EOPNOTSUPP;
		}
		tcf_ct_set_key_val(tb,
				   p->labels, TCA_CT_LABELS,
				   p->labels_mask, TCA_CT_LABELS_MASK,
				   sizeof(p->labels));
	}

	if (tb[TCA_CT_ZONE]) {
		if (!IS_ENABLED(CONFIG_NF_CONNTRACK_ZONES)) {
			NL_SET_ERR_MSG_MOD(extack, "Conntrack zones isn't enabled.");
			return -EOPNOTSUPP;
		}

		tcf_ct_set_key_val(tb,
				   &p->zone, TCA_CT_ZONE,
				   NULL, TCA_CT_UNSPEC,
				   sizeof(p->zone));
	}

	nf_ct_zone_init(&zone, p->zone, NF_CT_DEFAULT_ZONE_DIR, 0);
	tmpl = nf_ct_tmpl_alloc(net, &zone, GFP_KERNEL);
	if (!tmpl) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to allocate conntrack template");
		return -ENOMEM;
	}
	p->tmpl = tmpl;
	if (tb[TCA_CT_HELPER_NAME]) {
		name = nla_data(tb[TCA_CT_HELPER_NAME]);
		len = nla_len(tb[TCA_CT_HELPER_NAME]);
		if (len > 16 || name[len - 1] != '\0') {
			NL_SET_ERR_MSG_MOD(extack, "Failed to parse helper name.");
			err = -EINVAL;
			goto err;
		}
		family = tb[TCA_CT_HELPER_FAMILY] ? nla_get_u8(tb[TCA_CT_HELPER_FAMILY]) : AF_INET;
		proto = tb[TCA_CT_HELPER_PROTO] ? nla_get_u8(tb[TCA_CT_HELPER_PROTO]) : IPPROTO_TCP;
		err = nf_ct_add_helper(tmpl, name, family, proto,
				       p->ct_action & TCA_CT_ACT_NAT, &p->helper);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to add helper");
			goto err;
		}
	}

	if (p->ct_action & TCA_CT_ACT_COMMIT)
		__set_bit(IPS_CONFIRMED_BIT, &tmpl->status);
	return 0;
err:
	nf_ct_put(p->tmpl);
	p->tmpl = NULL;
	return err;
}

static int tcf_ct_init(struct net *net, struct nlattr *nla,
		       struct nlattr *est, struct tc_action **a,
		       struct tcf_proto *tp, u32 flags,
		       struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, act_ct_ops.net_id);
	bool bind = flags & TCA_ACT_FLAGS_BIND;
	struct tcf_ct_params *params = NULL;
	struct nlattr *tb[TCA_CT_MAX + 1];
	struct tcf_chain *goto_ch = NULL;
	struct tc_ct *parm;
	struct tcf_ct *c;
	int err, res = 0;
	u32 index;

	if (!nla) {
		NL_SET_ERR_MSG_MOD(extack, "Ct requires attributes to be passed");
		return -EINVAL;
	}

	err = nla_parse_nested(tb, TCA_CT_MAX, nla, ct_policy, extack);
	if (err < 0)
		return err;

	if (!tb[TCA_CT_PARMS]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing required ct parameters");
		return -EINVAL;
	}
	parm = nla_data(tb[TCA_CT_PARMS]);
	index = parm->index;
	err = tcf_idr_check_alloc(tn, &index, a, bind);
	if (err < 0)
		return err;

	if (!err) {
		err = tcf_idr_create_from_flags(tn, index, est, a,
						&act_ct_ops, bind, flags);
		if (err) {
			tcf_idr_cleanup(tn, index);
			return err;
		}
		res = ACT_P_CREATED;
	} else {
		if (bind)
			return 0;

		if (!(flags & TCA_ACT_FLAGS_REPLACE)) {
			tcf_idr_release(*a, bind);
			return -EEXIST;
		}
	}
	err = tcf_action_check_ctrlact(parm->action, tp, &goto_ch, extack);
	if (err < 0)
		goto cleanup;

	c = to_ct(*a);

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (unlikely(!params)) {
		err = -ENOMEM;
		goto cleanup;
	}

	err = tcf_ct_fill_params(net, params, parm, tb, extack);
	if (err)
		goto cleanup;

	err = tcf_ct_flow_table_get(net, params);
	if (err)
		goto cleanup;

	spin_lock_bh(&c->tcf_lock);
	goto_ch = tcf_action_set_ctrlact(*a, parm->action, goto_ch);
	params = rcu_replace_pointer(c->params, params,
				     lockdep_is_held(&c->tcf_lock));
	spin_unlock_bh(&c->tcf_lock);

	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);
	if (params)
		call_rcu(&params->rcu, tcf_ct_params_free_rcu);

	return res;

cleanup:
	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);
	if (params)
		tcf_ct_params_free(params);
	tcf_idr_release(*a, bind);
	return err;
}

static void tcf_ct_cleanup(struct tc_action *a)
{
	struct tcf_ct_params *params;
	struct tcf_ct *c = to_ct(a);

	params = rcu_dereference_protected(c->params, 1);
	if (params)
		call_rcu(&params->rcu, tcf_ct_params_free_rcu);
}

static int tcf_ct_dump_key_val(struct sk_buff *skb,
			       void *val, int val_type,
			       void *mask, int mask_type,
			       int len)
{
	int err;

	if (mask && !memchr_inv(mask, 0, len))
		return 0;

	err = nla_put(skb, val_type, len, val);
	if (err)
		return err;

	if (mask_type != TCA_CT_UNSPEC) {
		err = nla_put(skb, mask_type, len, mask);
		if (err)
			return err;
	}

	return 0;
}

static int tcf_ct_dump_nat(struct sk_buff *skb, struct tcf_ct_params *p)
{
	struct nf_nat_range2 *range = &p->range;

	if (!(p->ct_action & TCA_CT_ACT_NAT))
		return 0;

	if (!(p->ct_action & (TCA_CT_ACT_NAT_SRC | TCA_CT_ACT_NAT_DST)))
		return 0;

	if (range->flags & NF_NAT_RANGE_MAP_IPS) {
		if (p->ipv4_range) {
			if (nla_put_in_addr(skb, TCA_CT_NAT_IPV4_MIN,
					    range->min_addr.ip))
				return -1;
			if (nla_put_in_addr(skb, TCA_CT_NAT_IPV4_MAX,
					    range->max_addr.ip))
				return -1;
		} else {
			if (nla_put_in6_addr(skb, TCA_CT_NAT_IPV6_MIN,
					     &range->min_addr.in6))
				return -1;
			if (nla_put_in6_addr(skb, TCA_CT_NAT_IPV6_MAX,
					     &range->max_addr.in6))
				return -1;
		}
	}

	if (range->flags & NF_NAT_RANGE_PROTO_SPECIFIED) {
		if (nla_put_be16(skb, TCA_CT_NAT_PORT_MIN,
				 range->min_proto.all))
			return -1;
		if (nla_put_be16(skb, TCA_CT_NAT_PORT_MAX,
				 range->max_proto.all))
			return -1;
	}

	return 0;
}

static int tcf_ct_dump_helper(struct sk_buff *skb, struct nf_conntrack_helper *helper)
{
	if (!helper)
		return 0;

	if (nla_put_string(skb, TCA_CT_HELPER_NAME, helper->name) ||
	    nla_put_u8(skb, TCA_CT_HELPER_FAMILY, helper->tuple.src.l3num) ||
	    nla_put_u8(skb, TCA_CT_HELPER_PROTO, helper->tuple.dst.protonum))
		return -1;

	return 0;
}

static inline int tcf_ct_dump(struct sk_buff *skb, struct tc_action *a,
			      int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_ct *c = to_ct(a);
	struct tcf_ct_params *p;

	struct tc_ct opt = {
		.index   = c->tcf_index,
		.refcnt  = refcount_read(&c->tcf_refcnt) - ref,
		.bindcnt = atomic_read(&c->tcf_bindcnt) - bind,
	};
	struct tcf_t t;

	spin_lock_bh(&c->tcf_lock);
	p = rcu_dereference_protected(c->params,
				      lockdep_is_held(&c->tcf_lock));
	opt.action = c->tcf_action;

	if (tcf_ct_dump_key_val(skb,
				&p->ct_action, TCA_CT_ACTION,
				NULL, TCA_CT_UNSPEC,
				sizeof(p->ct_action)))
		goto nla_put_failure;

	if (p->ct_action & TCA_CT_ACT_CLEAR)
		goto skip_dump;

	if (IS_ENABLED(CONFIG_NF_CONNTRACK_MARK) &&
	    tcf_ct_dump_key_val(skb,
				&p->mark, TCA_CT_MARK,
				&p->mark_mask, TCA_CT_MARK_MASK,
				sizeof(p->mark)))
		goto nla_put_failure;

	if (IS_ENABLED(CONFIG_NF_CONNTRACK_LABELS) &&
	    tcf_ct_dump_key_val(skb,
				p->labels, TCA_CT_LABELS,
				p->labels_mask, TCA_CT_LABELS_MASK,
				sizeof(p->labels)))
		goto nla_put_failure;

	if (IS_ENABLED(CONFIG_NF_CONNTRACK_ZONES) &&
	    tcf_ct_dump_key_val(skb,
				&p->zone, TCA_CT_ZONE,
				NULL, TCA_CT_UNSPEC,
				sizeof(p->zone)))
		goto nla_put_failure;

	if (tcf_ct_dump_nat(skb, p))
		goto nla_put_failure;

	if (tcf_ct_dump_helper(skb, p->helper))
		goto nla_put_failure;

skip_dump:
	if (nla_put(skb, TCA_CT_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	tcf_tm_dump(&t, &c->tcf_tm);
	if (nla_put_64bit(skb, TCA_CT_TM, sizeof(t), &t, TCA_CT_PAD))
		goto nla_put_failure;
	spin_unlock_bh(&c->tcf_lock);

	return skb->len;
nla_put_failure:
	spin_unlock_bh(&c->tcf_lock);
	nlmsg_trim(skb, b);
	return -1;
}

static void tcf_stats_update(struct tc_action *a, u64 bytes, u64 packets,
			     u64 drops, u64 lastuse, bool hw)
{
	struct tcf_ct *c = to_ct(a);

	tcf_action_update_stats(a, bytes, packets, drops, hw);
	c->tcf_tm.lastuse = max_t(u64, c->tcf_tm.lastuse, lastuse);
}

static int tcf_ct_offload_act_setup(struct tc_action *act, void *entry_data,
				    u32 *index_inc, bool bind,
				    struct netlink_ext_ack *extack)
{
	if (bind) {
		struct flow_action_entry *entry = entry_data;

		if (tcf_ct_helper(act))
			return -EOPNOTSUPP;

		entry->id = FLOW_ACTION_CT;
		entry->ct.action = tcf_ct_action(act);
		entry->ct.zone = tcf_ct_zone(act);
		entry->ct.flow_table = tcf_ct_ft(act);
		*index_inc = 1;
	} else {
		struct flow_offload_action *fl_action = entry_data;

		fl_action->id = FLOW_ACTION_CT;
	}

	return 0;
}

static struct tc_action_ops act_ct_ops = {
	.kind		=	"ct",
	.id		=	TCA_ID_CT,
	.owner		=	THIS_MODULE,
	.act		=	tcf_ct_act,
	.dump		=	tcf_ct_dump,
	.init		=	tcf_ct_init,
	.cleanup	=	tcf_ct_cleanup,
	.stats_update	=	tcf_stats_update,
	.offload_act_setup =	tcf_ct_offload_act_setup,
	.size		=	sizeof(struct tcf_ct),
};

static __net_init int ct_init_net(struct net *net)
{
	unsigned int n_bits = sizeof_field(struct tcf_ct_params, labels) * 8;
	struct tc_ct_action_net *tn = net_generic(net, act_ct_ops.net_id);

	if (nf_connlabels_get(net, n_bits - 1)) {
		tn->labels = false;
		pr_err("act_ct: Failed to set connlabels length");
	} else {
		tn->labels = true;
	}

	return tc_action_net_init(net, &tn->tn, &act_ct_ops);
}

static void __net_exit ct_exit_net(struct list_head *net_list)
{
	struct net *net;

	rtnl_lock();
	list_for_each_entry(net, net_list, exit_list) {
		struct tc_ct_action_net *tn = net_generic(net, act_ct_ops.net_id);

		if (tn->labels)
			nf_connlabels_put(net);
	}
	rtnl_unlock();

	tc_action_net_exit(net_list, act_ct_ops.net_id);
}

static struct pernet_operations ct_net_ops = {
	.init = ct_init_net,
	.exit_batch = ct_exit_net,
	.id   = &act_ct_ops.net_id,
	.size = sizeof(struct tc_ct_action_net),
};

static int __init ct_init_module(void)
{
	int err;

	act_ct_wq = alloc_ordered_workqueue("act_ct_workqueue", 0);
	if (!act_ct_wq)
		return -ENOMEM;

	err = tcf_ct_flow_tables_init();
	if (err)
		goto err_tbl_init;

	err = tcf_register_action(&act_ct_ops, &ct_net_ops);
	if (err)
		goto err_register;

	static_branch_inc(&tcf_frag_xmit_count);

	return 0;

err_register:
	tcf_ct_flow_tables_uninit();
err_tbl_init:
	destroy_workqueue(act_ct_wq);
	return err;
}

static void __exit ct_cleanup_module(void)
{
	static_branch_dec(&tcf_frag_xmit_count);
	tcf_unregister_action(&act_ct_ops, &ct_net_ops);
	tcf_ct_flow_tables_uninit();
	destroy_workqueue(act_ct_wq);
}

module_init(ct_init_module);
module_exit(ct_cleanup_module);
MODULE_AUTHOR("Paul Blakey <paulb@mellanox.com>");
MODULE_AUTHOR("Yossi Kuperman <yossiku@mellanox.com>");
MODULE_AUTHOR("Marcelo Ricardo Leitner <marcelo.leitner@gmail.com>");
MODULE_DESCRIPTION("Connection tracking action");
MODULE_LICENSE("GPL v2");
