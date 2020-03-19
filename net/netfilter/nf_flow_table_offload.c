#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/rhashtable.h>
#include <linux/netdevice.h>
#include <linux/tc_act/tc_csum.h>
#include <net/flow_offload.h>
#include <net/netfilter/nf_flow_table.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_tuple.h>

static struct work_struct nf_flow_offload_work;
static DEFINE_SPINLOCK(flow_offload_pending_list_lock);
static LIST_HEAD(flow_offload_pending_list);

struct flow_offload_work {
	struct list_head	list;
	enum flow_cls_command	cmd;
	int			priority;
	struct nf_flowtable	*flowtable;
	struct flow_offload	*flow;
};

struct nf_flow_key {
	struct flow_dissector_key_meta			meta;
	struct flow_dissector_key_control		control;
	struct flow_dissector_key_basic			basic;
	union {
		struct flow_dissector_key_ipv4_addrs	ipv4;
		struct flow_dissector_key_ipv6_addrs	ipv6;
	};
	struct flow_dissector_key_tcp			tcp;
	struct flow_dissector_key_ports			tp;
} __aligned(BITS_PER_LONG / 8); /* Ensure that we can do comparisons as longs. */

struct nf_flow_match {
	struct flow_dissector	dissector;
	struct nf_flow_key	key;
	struct nf_flow_key	mask;
};

struct nf_flow_rule {
	struct nf_flow_match	match;
	struct flow_rule	*rule;
};

#define NF_FLOW_DISSECTOR(__match, __type, __field)	\
	(__match)->dissector.offset[__type] =		\
		offsetof(struct nf_flow_key, __field)

static int nf_flow_rule_match(struct nf_flow_match *match,
			      const struct flow_offload_tuple *tuple)
{
	struct nf_flow_key *mask = &match->mask;
	struct nf_flow_key *key = &match->key;

	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_META, meta);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_CONTROL, control);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_BASIC, basic);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_IPV4_ADDRS, ipv4);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_IPV6_ADDRS, ipv6);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_TCP, tcp);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_PORTS, tp);

	key->meta.ingress_ifindex = tuple->iifidx;
	mask->meta.ingress_ifindex = 0xffffffff;

	switch (tuple->l3proto) {
	case AF_INET:
		key->control.addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
		key->basic.n_proto = htons(ETH_P_IP);
		key->ipv4.src = tuple->src_v4.s_addr;
		mask->ipv4.src = 0xffffffff;
		key->ipv4.dst = tuple->dst_v4.s_addr;
		mask->ipv4.dst = 0xffffffff;
		break;
       case AF_INET6:
		key->control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
		key->basic.n_proto = htons(ETH_P_IPV6);
		key->ipv6.src = tuple->src_v6;
		memset(&mask->ipv6.src, 0xff, sizeof(mask->ipv6.src));
		key->ipv6.dst = tuple->dst_v6;
		memset(&mask->ipv6.dst, 0xff, sizeof(mask->ipv6.dst));
		break;
	default:
		return -EOPNOTSUPP;
	}
	mask->control.addr_type = 0xffff;
	match->dissector.used_keys |= BIT(key->control.addr_type);
	mask->basic.n_proto = 0xffff;

	switch (tuple->l4proto) {
	case IPPROTO_TCP:
		key->tcp.flags = 0;
		mask->tcp.flags = cpu_to_be16(be32_to_cpu(TCP_FLAG_RST | TCP_FLAG_FIN) >> 16);
		match->dissector.used_keys |= BIT(FLOW_DISSECTOR_KEY_TCP);
		break;
	case IPPROTO_UDP:
		break;
	default:
		return -EOPNOTSUPP;
	}

	key->basic.ip_proto = tuple->l4proto;
	mask->basic.ip_proto = 0xff;

	key->tp.src = tuple->src_port;
	mask->tp.src = 0xffff;
	key->tp.dst = tuple->dst_port;
	mask->tp.dst = 0xffff;

	match->dissector.used_keys |= BIT(FLOW_DISSECTOR_KEY_META) |
				      BIT(FLOW_DISSECTOR_KEY_CONTROL) |
				      BIT(FLOW_DISSECTOR_KEY_BASIC) |
				      BIT(FLOW_DISSECTOR_KEY_PORTS);
	return 0;
}

static void flow_offload_mangle(struct flow_action_entry *entry,
				enum flow_action_mangle_base htype, u32 offset,
				const __be32 *value, const __be32 *mask)
{
	entry->id = FLOW_ACTION_MANGLE;
	entry->mangle.htype = htype;
	entry->mangle.offset = offset;
	memcpy(&entry->mangle.mask, mask, sizeof(u32));
	memcpy(&entry->mangle.val, value, sizeof(u32));
}

static inline struct flow_action_entry *
flow_action_entry_next(struct nf_flow_rule *flow_rule)
{
	int i = flow_rule->rule->action.num_entries++;

	return &flow_rule->rule->action.entries[i];
}

static int flow_offload_eth_src(struct net *net,
				const struct flow_offload *flow,
				enum flow_offload_tuple_dir dir,
				struct nf_flow_rule *flow_rule)
{
	const struct flow_offload_tuple *tuple = &flow->tuplehash[!dir].tuple;
	struct flow_action_entry *entry0 = flow_action_entry_next(flow_rule);
	struct flow_action_entry *entry1 = flow_action_entry_next(flow_rule);
	struct net_device *dev;
	u32 mask, val;
	u16 val16;

	dev = dev_get_by_index(net, tuple->iifidx);
	if (!dev)
		return -ENOENT;

	mask = ~0xffff0000;
	memcpy(&val16, dev->dev_addr, 2);
	val = val16 << 16;
	flow_offload_mangle(entry0, FLOW_ACT_MANGLE_HDR_TYPE_ETH, 4,
			    &val, &mask);

	mask = ~0xffffffff;
	memcpy(&val, dev->dev_addr + 2, 4);
	flow_offload_mangle(entry1, FLOW_ACT_MANGLE_HDR_TYPE_ETH, 8,
			    &val, &mask);
	dev_put(dev);

	return 0;
}

static int flow_offload_eth_dst(struct net *net,
				const struct flow_offload *flow,
				enum flow_offload_tuple_dir dir,
				struct nf_flow_rule *flow_rule)
{
	struct flow_action_entry *entry0 = flow_action_entry_next(flow_rule);
	struct flow_action_entry *entry1 = flow_action_entry_next(flow_rule);
	const void *daddr = &flow->tuplehash[!dir].tuple.src_v4;
	const struct dst_entry *dst_cache;
	unsigned char ha[ETH_ALEN];
	struct neighbour *n;
	u32 mask, val;
	u8 nud_state;
	u16 val16;

	dst_cache = flow->tuplehash[dir].tuple.dst_cache;
	n = dst_neigh_lookup(dst_cache, daddr);
	if (!n)
		return -ENOENT;

	read_lock_bh(&n->lock);
	nud_state = n->nud_state;
	ether_addr_copy(ha, n->ha);
	read_unlock_bh(&n->lock);

	if (!(nud_state & NUD_VALID)) {
		neigh_release(n);
		return -ENOENT;
	}

	mask = ~0xffffffff;
	memcpy(&val, ha, 4);
	flow_offload_mangle(entry0, FLOW_ACT_MANGLE_HDR_TYPE_ETH, 0,
			    &val, &mask);

	mask = ~0x0000ffff;
	memcpy(&val16, ha + 4, 2);
	val = val16;
	flow_offload_mangle(entry1, FLOW_ACT_MANGLE_HDR_TYPE_ETH, 4,
			    &val, &mask);
	neigh_release(n);

	return 0;
}

static void flow_offload_ipv4_snat(struct net *net,
				   const struct flow_offload *flow,
				   enum flow_offload_tuple_dir dir,
				   struct nf_flow_rule *flow_rule)
{
	struct flow_action_entry *entry = flow_action_entry_next(flow_rule);
	u32 mask = ~htonl(0xffffffff);
	__be32 addr;
	u32 offset;

	switch (dir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		addr = flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.dst_v4.s_addr;
		offset = offsetof(struct iphdr, saddr);
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		addr = flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.src_v4.s_addr;
		offset = offsetof(struct iphdr, daddr);
		break;
	default:
		return;
	}

	flow_offload_mangle(entry, FLOW_ACT_MANGLE_HDR_TYPE_IP4, offset,
			    &addr, &mask);
}

static void flow_offload_ipv4_dnat(struct net *net,
				   const struct flow_offload *flow,
				   enum flow_offload_tuple_dir dir,
				   struct nf_flow_rule *flow_rule)
{
	struct flow_action_entry *entry = flow_action_entry_next(flow_rule);
	u32 mask = ~htonl(0xffffffff);
	__be32 addr;
	u32 offset;

	switch (dir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		addr = flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.src_v4.s_addr;
		offset = offsetof(struct iphdr, daddr);
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		addr = flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.dst_v4.s_addr;
		offset = offsetof(struct iphdr, saddr);
		break;
	default:
		return;
	}

	flow_offload_mangle(entry, FLOW_ACT_MANGLE_HDR_TYPE_IP4, offset,
			    &addr, &mask);
}

static void flow_offload_ipv6_mangle(struct nf_flow_rule *flow_rule,
				     unsigned int offset,
				     const __be32 *addr, const __be32 *mask)
{
	struct flow_action_entry *entry;
	int i;

	for (i = 0; i < sizeof(struct in6_addr) / sizeof(u32); i += sizeof(u32)) {
		entry = flow_action_entry_next(flow_rule);
		flow_offload_mangle(entry, FLOW_ACT_MANGLE_HDR_TYPE_IP6,
				    offset + i, &addr[i], mask);
	}
}

static void flow_offload_ipv6_snat(struct net *net,
				   const struct flow_offload *flow,
				   enum flow_offload_tuple_dir dir,
				   struct nf_flow_rule *flow_rule)
{
	u32 mask = ~htonl(0xffffffff);
	const __be32 *addr;
	u32 offset;

	switch (dir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		addr = flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.dst_v6.s6_addr32;
		offset = offsetof(struct ipv6hdr, saddr);
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		addr = flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.src_v6.s6_addr32;
		offset = offsetof(struct ipv6hdr, daddr);
		break;
	default:
		return;
	}

	flow_offload_ipv6_mangle(flow_rule, offset, addr, &mask);
}

static void flow_offload_ipv6_dnat(struct net *net,
				   const struct flow_offload *flow,
				   enum flow_offload_tuple_dir dir,
				   struct nf_flow_rule *flow_rule)
{
	u32 mask = ~htonl(0xffffffff);
	const __be32 *addr;
	u32 offset;

	switch (dir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		addr = flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.src_v6.s6_addr32;
		offset = offsetof(struct ipv6hdr, daddr);
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		addr = flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.dst_v6.s6_addr32;
		offset = offsetof(struct ipv6hdr, saddr);
		break;
	default:
		return;
	}

	flow_offload_ipv6_mangle(flow_rule, offset, addr, &mask);
}

static int flow_offload_l4proto(const struct flow_offload *flow)
{
	u8 protonum = flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.l4proto;
	u8 type = 0;

	switch (protonum) {
	case IPPROTO_TCP:
		type = FLOW_ACT_MANGLE_HDR_TYPE_TCP;
		break;
	case IPPROTO_UDP:
		type = FLOW_ACT_MANGLE_HDR_TYPE_UDP;
		break;
	default:
		break;
	}

	return type;
}

static void flow_offload_port_snat(struct net *net,
				   const struct flow_offload *flow,
				   enum flow_offload_tuple_dir dir,
				   struct nf_flow_rule *flow_rule)
{
	struct flow_action_entry *entry = flow_action_entry_next(flow_rule);
	u32 mask, port;
	u32 offset;

	switch (dir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		port = ntohs(flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.dst_port);
		offset = 0; /* offsetof(struct tcphdr, source); */
		port = htonl(port << 16);
		mask = ~htonl(0xffff0000);
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		port = ntohs(flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.src_port);
		offset = 0; /* offsetof(struct tcphdr, dest); */
		port = htonl(port);
		mask = ~htonl(0xffff);
		break;
	default:
		return;
	}

	flow_offload_mangle(entry, flow_offload_l4proto(flow), offset,
			    &port, &mask);
}

static void flow_offload_port_dnat(struct net *net,
				   const struct flow_offload *flow,
				   enum flow_offload_tuple_dir dir,
				   struct nf_flow_rule *flow_rule)
{
	struct flow_action_entry *entry = flow_action_entry_next(flow_rule);
	u32 mask, port;
	u32 offset;

	switch (dir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		port = ntohs(flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.src_port);
		offset = 0; /* offsetof(struct tcphdr, dest); */
		port = htonl(port);
		mask = ~htonl(0xffff);
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		port = ntohs(flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.dst_port);
		offset = 0; /* offsetof(struct tcphdr, source); */
		port = htonl(port << 16);
		mask = ~htonl(0xffff0000);
		break;
	default:
		return;
	}

	flow_offload_mangle(entry, flow_offload_l4proto(flow), offset,
			    &port, &mask);
}

static void flow_offload_ipv4_checksum(struct net *net,
				       const struct flow_offload *flow,
				       struct nf_flow_rule *flow_rule)
{
	u8 protonum = flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.l4proto;
	struct flow_action_entry *entry = flow_action_entry_next(flow_rule);

	entry->id = FLOW_ACTION_CSUM;
	entry->csum_flags = TCA_CSUM_UPDATE_FLAG_IPV4HDR;

	switch (protonum) {
	case IPPROTO_TCP:
		entry->csum_flags |= TCA_CSUM_UPDATE_FLAG_TCP;
		break;
	case IPPROTO_UDP:
		entry->csum_flags |= TCA_CSUM_UPDATE_FLAG_UDP;
		break;
	}
}

static void flow_offload_redirect(const struct flow_offload *flow,
				  enum flow_offload_tuple_dir dir,
				  struct nf_flow_rule *flow_rule)
{
	struct flow_action_entry *entry = flow_action_entry_next(flow_rule);
	struct rtable *rt;

	rt = (struct rtable *)flow->tuplehash[dir].tuple.dst_cache;
	entry->id = FLOW_ACTION_REDIRECT;
	entry->dev = rt->dst.dev;
	dev_hold(rt->dst.dev);
}

int nf_flow_rule_route_ipv4(struct net *net, const struct flow_offload *flow,
			    enum flow_offload_tuple_dir dir,
			    struct nf_flow_rule *flow_rule)
{
	if (flow_offload_eth_src(net, flow, dir, flow_rule) < 0 ||
	    flow_offload_eth_dst(net, flow, dir, flow_rule) < 0)
		return -1;

	if (test_bit(NF_FLOW_SNAT, &flow->flags)) {
		flow_offload_ipv4_snat(net, flow, dir, flow_rule);
		flow_offload_port_snat(net, flow, dir, flow_rule);
	}
	if (test_bit(NF_FLOW_DNAT, &flow->flags)) {
		flow_offload_ipv4_dnat(net, flow, dir, flow_rule);
		flow_offload_port_dnat(net, flow, dir, flow_rule);
	}
	if (test_bit(NF_FLOW_SNAT, &flow->flags) ||
	    test_bit(NF_FLOW_DNAT, &flow->flags))
		flow_offload_ipv4_checksum(net, flow, flow_rule);

	flow_offload_redirect(flow, dir, flow_rule);

	return 0;
}
EXPORT_SYMBOL_GPL(nf_flow_rule_route_ipv4);

int nf_flow_rule_route_ipv6(struct net *net, const struct flow_offload *flow,
			    enum flow_offload_tuple_dir dir,
			    struct nf_flow_rule *flow_rule)
{
	if (flow_offload_eth_src(net, flow, dir, flow_rule) < 0 ||
	    flow_offload_eth_dst(net, flow, dir, flow_rule) < 0)
		return -1;

	if (test_bit(NF_FLOW_SNAT, &flow->flags)) {
		flow_offload_ipv6_snat(net, flow, dir, flow_rule);
		flow_offload_port_snat(net, flow, dir, flow_rule);
	}
	if (test_bit(NF_FLOW_DNAT, &flow->flags)) {
		flow_offload_ipv6_dnat(net, flow, dir, flow_rule);
		flow_offload_port_dnat(net, flow, dir, flow_rule);
	}

	flow_offload_redirect(flow, dir, flow_rule);

	return 0;
}
EXPORT_SYMBOL_GPL(nf_flow_rule_route_ipv6);

#define NF_FLOW_RULE_ACTION_MAX	16

static struct nf_flow_rule *
nf_flow_offload_rule_alloc(struct net *net,
			   const struct flow_offload_work *offload,
			   enum flow_offload_tuple_dir dir)
{
	const struct nf_flowtable *flowtable = offload->flowtable;
	const struct flow_offload *flow = offload->flow;
	const struct flow_offload_tuple *tuple;
	struct nf_flow_rule *flow_rule;
	int err = -ENOMEM;

	flow_rule = kzalloc(sizeof(*flow_rule), GFP_KERNEL);
	if (!flow_rule)
		goto err_flow;

	flow_rule->rule = flow_rule_alloc(NF_FLOW_RULE_ACTION_MAX);
	if (!flow_rule->rule)
		goto err_flow_rule;

	flow_rule->rule->match.dissector = &flow_rule->match.dissector;
	flow_rule->rule->match.mask = &flow_rule->match.mask;
	flow_rule->rule->match.key = &flow_rule->match.key;

	tuple = &flow->tuplehash[dir].tuple;
	err = nf_flow_rule_match(&flow_rule->match, tuple);
	if (err < 0)
		goto err_flow_match;

	flow_rule->rule->action.num_entries = 0;
	if (flowtable->type->action(net, flow, dir, flow_rule) < 0)
		goto err_flow_match;

	return flow_rule;

err_flow_match:
	kfree(flow_rule->rule);
err_flow_rule:
	kfree(flow_rule);
err_flow:
	return NULL;
}

static void __nf_flow_offload_destroy(struct nf_flow_rule *flow_rule)
{
	struct flow_action_entry *entry;
	int i;

	for (i = 0; i < flow_rule->rule->action.num_entries; i++) {
		entry = &flow_rule->rule->action.entries[i];
		if (entry->id != FLOW_ACTION_REDIRECT)
			continue;

		dev_put(entry->dev);
	}
	kfree(flow_rule->rule);
	kfree(flow_rule);
}

static void nf_flow_offload_destroy(struct nf_flow_rule *flow_rule[])
{
	int i;

	for (i = 0; i < FLOW_OFFLOAD_DIR_MAX; i++)
		__nf_flow_offload_destroy(flow_rule[i]);
}

static int nf_flow_offload_alloc(const struct flow_offload_work *offload,
				 struct nf_flow_rule *flow_rule[])
{
	struct net *net = read_pnet(&offload->flowtable->net);

	flow_rule[0] = nf_flow_offload_rule_alloc(net, offload,
						  FLOW_OFFLOAD_DIR_ORIGINAL);
	if (!flow_rule[0])
		return -ENOMEM;

	flow_rule[1] = nf_flow_offload_rule_alloc(net, offload,
						  FLOW_OFFLOAD_DIR_REPLY);
	if (!flow_rule[1]) {
		__nf_flow_offload_destroy(flow_rule[0]);
		return -ENOMEM;
	}

	return 0;
}

static void nf_flow_offload_init(struct flow_cls_offload *cls_flow,
				 __be16 proto, int priority,
				 enum flow_cls_command cmd,
				 const struct flow_offload_tuple *tuple,
				 struct netlink_ext_ack *extack)
{
	cls_flow->common.protocol = proto;
	cls_flow->common.prio = priority;
	cls_flow->common.extack = extack;
	cls_flow->command = cmd;
	cls_flow->cookie = (unsigned long)tuple;
}

static int nf_flow_offload_tuple(struct nf_flowtable *flowtable,
				 struct flow_offload *flow,
				 struct nf_flow_rule *flow_rule,
				 enum flow_offload_tuple_dir dir,
				 int priority, int cmd,
				 struct list_head *block_cb_list)
{
	struct flow_cls_offload cls_flow = {};
	struct flow_block_cb *block_cb;
	struct netlink_ext_ack extack;
	__be16 proto = ETH_P_ALL;
	int err, i = 0;

	nf_flow_offload_init(&cls_flow, proto, priority, cmd,
			     &flow->tuplehash[dir].tuple, &extack);
	if (cmd == FLOW_CLS_REPLACE)
		cls_flow.rule = flow_rule->rule;

	list_for_each_entry(block_cb, block_cb_list, list) {
		err = block_cb->cb(TC_SETUP_CLSFLOWER, &cls_flow,
				   block_cb->cb_priv);
		if (err < 0)
			continue;

		i++;
	}

	return i;
}

static int flow_offload_tuple_add(struct flow_offload_work *offload,
				  struct nf_flow_rule *flow_rule,
				  enum flow_offload_tuple_dir dir)
{
	return nf_flow_offload_tuple(offload->flowtable, offload->flow,
				     flow_rule, dir, offload->priority,
				     FLOW_CLS_REPLACE,
				     &offload->flowtable->flow_block.cb_list);
}

static void flow_offload_tuple_del(struct flow_offload_work *offload,
				   enum flow_offload_tuple_dir dir)
{
	nf_flow_offload_tuple(offload->flowtable, offload->flow, NULL, dir,
			      offload->priority, FLOW_CLS_DESTROY,
			      &offload->flowtable->flow_block.cb_list);
}

static int flow_offload_rule_add(struct flow_offload_work *offload,
				 struct nf_flow_rule *flow_rule[])
{
	int ok_count = 0;

	ok_count += flow_offload_tuple_add(offload, flow_rule[0],
					   FLOW_OFFLOAD_DIR_ORIGINAL);
	ok_count += flow_offload_tuple_add(offload, flow_rule[1],
					   FLOW_OFFLOAD_DIR_REPLY);
	if (ok_count == 0)
		return -ENOENT;

	return 0;
}

static void flow_offload_work_add(struct flow_offload_work *offload)
{
	struct nf_flow_rule *flow_rule[FLOW_OFFLOAD_DIR_MAX];
	int err;

	err = nf_flow_offload_alloc(offload, flow_rule);
	if (err < 0)
		return;

	err = flow_offload_rule_add(offload, flow_rule);
	if (err < 0)
		set_bit(NF_FLOW_HW_REFRESH, &offload->flow->flags);

	nf_flow_offload_destroy(flow_rule);
}

static void flow_offload_work_del(struct flow_offload_work *offload)
{
	flow_offload_tuple_del(offload, FLOW_OFFLOAD_DIR_ORIGINAL);
	flow_offload_tuple_del(offload, FLOW_OFFLOAD_DIR_REPLY);
	set_bit(NF_FLOW_HW_DEAD, &offload->flow->flags);
}

static void flow_offload_tuple_stats(struct flow_offload_work *offload,
				     enum flow_offload_tuple_dir dir,
				     struct flow_stats *stats)
{
	struct nf_flowtable *flowtable = offload->flowtable;
	struct flow_cls_offload cls_flow = {};
	struct flow_block_cb *block_cb;
	struct netlink_ext_ack extack;
	__be16 proto = ETH_P_ALL;

	nf_flow_offload_init(&cls_flow, proto, offload->priority,
			     FLOW_CLS_STATS,
			     &offload->flow->tuplehash[dir].tuple, &extack);

	list_for_each_entry(block_cb, &flowtable->flow_block.cb_list, list)
		block_cb->cb(TC_SETUP_CLSFLOWER, &cls_flow, block_cb->cb_priv);
	memcpy(stats, &cls_flow.stats, sizeof(*stats));
}

static void flow_offload_work_stats(struct flow_offload_work *offload)
{
	struct flow_stats stats[FLOW_OFFLOAD_DIR_MAX] = {};
	u64 lastused;

	flow_offload_tuple_stats(offload, FLOW_OFFLOAD_DIR_ORIGINAL, &stats[0]);
	flow_offload_tuple_stats(offload, FLOW_OFFLOAD_DIR_REPLY, &stats[1]);

	lastused = max_t(u64, stats[0].lastused, stats[1].lastused);
	offload->flow->timeout = max_t(u64, offload->flow->timeout,
				       lastused + NF_FLOW_TIMEOUT);
}

static void flow_offload_work_handler(struct work_struct *work)
{
	struct flow_offload_work *offload, *next;
	LIST_HEAD(offload_pending_list);

	spin_lock_bh(&flow_offload_pending_list_lock);
	list_replace_init(&flow_offload_pending_list, &offload_pending_list);
	spin_unlock_bh(&flow_offload_pending_list_lock);

	list_for_each_entry_safe(offload, next, &offload_pending_list, list) {
		switch (offload->cmd) {
		case FLOW_CLS_REPLACE:
			flow_offload_work_add(offload);
			break;
		case FLOW_CLS_DESTROY:
			flow_offload_work_del(offload);
			break;
		case FLOW_CLS_STATS:
			flow_offload_work_stats(offload);
			break;
		default:
			WARN_ON_ONCE(1);
		}
		list_del(&offload->list);
		kfree(offload);
	}
}

static void flow_offload_queue_work(struct flow_offload_work *offload)
{
	spin_lock_bh(&flow_offload_pending_list_lock);
	list_add_tail(&offload->list, &flow_offload_pending_list);
	spin_unlock_bh(&flow_offload_pending_list_lock);

	schedule_work(&nf_flow_offload_work);
}

static struct flow_offload_work *
nf_flow_offload_work_alloc(struct nf_flowtable *flowtable,
			   struct flow_offload *flow, unsigned int cmd)
{
	struct flow_offload_work *offload;

	offload = kmalloc(sizeof(struct flow_offload_work), GFP_ATOMIC);
	if (!offload)
		return NULL;

	offload->cmd = cmd;
	offload->flow = flow;
	offload->priority = flowtable->priority;
	offload->flowtable = flowtable;

	return offload;
}


void nf_flow_offload_add(struct nf_flowtable *flowtable,
			 struct flow_offload *flow)
{
	struct flow_offload_work *offload;

	offload = nf_flow_offload_work_alloc(flowtable, flow, FLOW_CLS_REPLACE);
	if (!offload)
		return;

	flow_offload_queue_work(offload);
}

void nf_flow_offload_del(struct nf_flowtable *flowtable,
			 struct flow_offload *flow)
{
	struct flow_offload_work *offload;

	offload = nf_flow_offload_work_alloc(flowtable, flow, FLOW_CLS_DESTROY);
	if (!offload)
		return;

	set_bit(NF_FLOW_HW_DYING, &flow->flags);
	flow_offload_queue_work(offload);
}

void nf_flow_offload_stats(struct nf_flowtable *flowtable,
			   struct flow_offload *flow)
{
	struct flow_offload_work *offload;
	__s32 delta;

	delta = nf_flow_timeout_delta(flow->timeout);
	if ((delta >= (9 * NF_FLOW_TIMEOUT) / 10))
		return;

	offload = nf_flow_offload_work_alloc(flowtable, flow, FLOW_CLS_STATS);
	if (!offload)
		return;

	flow_offload_queue_work(offload);
}

void nf_flow_table_offload_flush(struct nf_flowtable *flowtable)
{
	if (nf_flowtable_hw_offload(flowtable))
		flush_work(&nf_flow_offload_work);
}

static int nf_flow_table_block_setup(struct nf_flowtable *flowtable,
				     struct flow_block_offload *bo,
				     enum flow_block_command cmd)
{
	struct flow_block_cb *block_cb, *next;
	int err = 0;

	switch (cmd) {
	case FLOW_BLOCK_BIND:
		list_splice(&bo->cb_list, &flowtable->flow_block.cb_list);
		break;
	case FLOW_BLOCK_UNBIND:
		list_for_each_entry_safe(block_cb, next, &bo->cb_list, list) {
			list_del(&block_cb->list);
			flow_block_cb_free(block_cb);
		}
		break;
	default:
		WARN_ON_ONCE(1);
		err = -EOPNOTSUPP;
	}

	return err;
}

static int nf_flow_table_offload_cmd(struct flow_block_offload *bo,
				     struct nf_flowtable *flowtable,
				     struct net_device *dev,
				     enum flow_block_command cmd,
				     struct netlink_ext_ack *extack)
{
	int err;

	if (!dev->netdev_ops->ndo_setup_tc)
		return -EOPNOTSUPP;

	memset(bo, 0, sizeof(*bo));
	bo->net		= dev_net(dev);
	bo->block	= &flowtable->flow_block;
	bo->command	= cmd;
	bo->binder_type	= FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS;
	bo->extack	= extack;
	INIT_LIST_HEAD(&bo->cb_list);

	err = dev->netdev_ops->ndo_setup_tc(dev, TC_SETUP_FT, bo);
	if (err < 0)
		return err;

	return 0;
}

int nf_flow_table_offload_setup(struct nf_flowtable *flowtable,
				struct net_device *dev,
				enum flow_block_command cmd)
{
	struct netlink_ext_ack extack = {};
	struct flow_block_offload bo;
	int err;

	if (!nf_flowtable_hw_offload(flowtable))
		return 0;

	err = nf_flow_table_offload_cmd(&bo, flowtable, dev, cmd, &extack);
	if (err < 0)
		return err;

	return nf_flow_table_block_setup(flowtable, &bo, cmd);
}
EXPORT_SYMBOL_GPL(nf_flow_table_offload_setup);

int nf_flow_table_offload_init(void)
{
	INIT_WORK(&nf_flow_offload_work, flow_offload_work_handler);

	return 0;
}

void nf_flow_table_offload_exit(void)
{
	struct flow_offload_work *offload, *next;
	LIST_HEAD(offload_pending_list);

	cancel_work_sync(&nf_flow_offload_work);

	list_for_each_entry_safe(offload, next, &offload_pending_list, list) {
		list_del(&offload->list);
		kfree(offload);
	}
}
