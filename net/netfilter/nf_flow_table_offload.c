#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/rhashtable.h>
#include <linux/netdevice.h>
#include <linux/tc_act/tc_csum.h>
#include <net/flow_offload.h>
#include <net/netfilter/nf_flow_table.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_acct.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_tuple.h>

static struct workqueue_struct *nf_flow_offload_add_wq;
static struct workqueue_struct *nf_flow_offload_del_wq;
static struct workqueue_struct *nf_flow_offload_stats_wq;

struct flow_offload_work {
	struct list_head	list;
	enum flow_cls_command	cmd;
	int			priority;
	struct nf_flowtable	*flowtable;
	struct flow_offload	*flow;
	struct work_struct	work;
};

#define NF_FLOW_DISSECTOR(__match, __type, __field)	\
	(__match)->dissector.offset[__type] =		\
		offsetof(struct nf_flow_key, __field)

static void nf_flow_rule_lwt_match(struct nf_flow_match *match,
				   struct ip_tunnel_info *tun_info)
{
	struct nf_flow_key *mask = &match->mask;
	struct nf_flow_key *key = &match->key;
	unsigned int enc_keys;

	if (!tun_info || !(tun_info->mode & IP_TUNNEL_INFO_TX))
		return;

	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_ENC_CONTROL, enc_control);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_ENC_KEYID, enc_key_id);
	key->enc_key_id.keyid = tunnel_id_to_key32(tun_info->key.tun_id);
	mask->enc_key_id.keyid = 0xffffffff;
	enc_keys = BIT(FLOW_DISSECTOR_KEY_ENC_KEYID) |
		   BIT(FLOW_DISSECTOR_KEY_ENC_CONTROL);

	if (ip_tunnel_info_af(tun_info) == AF_INET) {
		NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS,
				  enc_ipv4);
		key->enc_ipv4.src = tun_info->key.u.ipv4.dst;
		key->enc_ipv4.dst = tun_info->key.u.ipv4.src;
		if (key->enc_ipv4.src)
			mask->enc_ipv4.src = 0xffffffff;
		if (key->enc_ipv4.dst)
			mask->enc_ipv4.dst = 0xffffffff;
		enc_keys |= BIT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS);
		key->enc_control.addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
	} else {
		memcpy(&key->enc_ipv6.src, &tun_info->key.u.ipv6.dst,
		       sizeof(struct in6_addr));
		memcpy(&key->enc_ipv6.dst, &tun_info->key.u.ipv6.src,
		       sizeof(struct in6_addr));
		if (memcmp(&key->enc_ipv6.src, &in6addr_any,
			   sizeof(struct in6_addr)))
			memset(&key->enc_ipv6.src, 0xff,
			       sizeof(struct in6_addr));
		if (memcmp(&key->enc_ipv6.dst, &in6addr_any,
			   sizeof(struct in6_addr)))
			memset(&key->enc_ipv6.dst, 0xff,
			       sizeof(struct in6_addr));
		enc_keys |= BIT(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS);
		key->enc_control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
	}

	match->dissector.used_keys |= enc_keys;
}

static void nf_flow_rule_vlan_match(struct flow_dissector_key_vlan *key,
				    struct flow_dissector_key_vlan *mask,
				    u16 vlan_id, __be16 proto)
{
	key->vlan_id = vlan_id;
	mask->vlan_id = VLAN_VID_MASK;
	key->vlan_tpid = proto;
	mask->vlan_tpid = 0xffff;
}

static int nf_flow_rule_match(struct nf_flow_match *match,
			      const struct flow_offload_tuple *tuple,
			      struct dst_entry *other_dst)
{
	struct nf_flow_key *mask = &match->mask;
	struct nf_flow_key *key = &match->key;
	struct ip_tunnel_info *tun_info;
	bool vlan_encap = false;

	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_META, meta);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_CONTROL, control);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_BASIC, basic);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_IPV4_ADDRS, ipv4);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_IPV6_ADDRS, ipv6);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_TCP, tcp);
	NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_PORTS, tp);

	if (other_dst && other_dst->lwtstate) {
		tun_info = lwt_tun_info(other_dst->lwtstate);
		nf_flow_rule_lwt_match(match, tun_info);
	}

	key->meta.ingress_ifindex = tuple->iifidx;
	mask->meta.ingress_ifindex = 0xffffffff;

	if (tuple->encap_num > 0 && !(tuple->in_vlan_ingress & BIT(0)) &&
	    tuple->encap[0].proto == htons(ETH_P_8021Q)) {
		NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_VLAN, vlan);
		nf_flow_rule_vlan_match(&key->vlan, &mask->vlan,
					tuple->encap[0].id,
					tuple->encap[0].proto);
		vlan_encap = true;
	}

	if (tuple->encap_num > 1 && !(tuple->in_vlan_ingress & BIT(1)) &&
	    tuple->encap[1].proto == htons(ETH_P_8021Q)) {
		if (vlan_encap) {
			NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_CVLAN,
					  cvlan);
			nf_flow_rule_vlan_match(&key->cvlan, &mask->cvlan,
						tuple->encap[1].id,
						tuple->encap[1].proto);
		} else {
			NF_FLOW_DISSECTOR(match, FLOW_DISSECTOR_KEY_VLAN,
					  vlan);
			nf_flow_rule_vlan_match(&key->vlan, &mask->vlan,
						tuple->encap[1].id,
						tuple->encap[1].proto);
		}
	}

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
	struct flow_action_entry *entry0 = flow_action_entry_next(flow_rule);
	struct flow_action_entry *entry1 = flow_action_entry_next(flow_rule);
	const struct flow_offload_tuple *other_tuple, *this_tuple;
	struct net_device *dev = NULL;
	const unsigned char *addr;
	u32 mask, val;
	u16 val16;

	this_tuple = &flow->tuplehash[dir].tuple;

	switch (this_tuple->xmit_type) {
	case FLOW_OFFLOAD_XMIT_DIRECT:
		addr = this_tuple->out.h_source;
		break;
	case FLOW_OFFLOAD_XMIT_NEIGH:
		other_tuple = &flow->tuplehash[!dir].tuple;
		dev = dev_get_by_index(net, other_tuple->iifidx);
		if (!dev)
			return -ENOENT;

		addr = dev->dev_addr;
		break;
	default:
		return -EOPNOTSUPP;
	}

	mask = ~0xffff0000;
	memcpy(&val16, addr, 2);
	val = val16 << 16;
	flow_offload_mangle(entry0, FLOW_ACT_MANGLE_HDR_TYPE_ETH, 4,
			    &val, &mask);

	mask = ~0xffffffff;
	memcpy(&val, addr + 2, 4);
	flow_offload_mangle(entry1, FLOW_ACT_MANGLE_HDR_TYPE_ETH, 8,
			    &val, &mask);

	if (dev)
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
	const struct flow_offload_tuple *other_tuple, *this_tuple;
	const struct dst_entry *dst_cache;
	unsigned char ha[ETH_ALEN];
	struct neighbour *n;
	const void *daddr;
	u32 mask, val;
	u8 nud_state;
	u16 val16;

	this_tuple = &flow->tuplehash[dir].tuple;

	switch (this_tuple->xmit_type) {
	case FLOW_OFFLOAD_XMIT_DIRECT:
		ether_addr_copy(ha, this_tuple->out.h_dest);
		break;
	case FLOW_OFFLOAD_XMIT_NEIGH:
		other_tuple = &flow->tuplehash[!dir].tuple;
		daddr = &other_tuple->src_v4;
		dst_cache = this_tuple->dst_cache;
		n = dst_neigh_lookup(dst_cache, daddr);
		if (!n)
			return -ENOENT;

		read_lock_bh(&n->lock);
		nud_state = n->nud_state;
		ether_addr_copy(ha, n->ha);
		read_unlock_bh(&n->lock);
		neigh_release(n);

		if (!(nud_state & NUD_VALID))
			return -ENOENT;
		break;
	default:
		return -EOPNOTSUPP;
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
	int i, j;

	for (i = 0, j = 0; i < sizeof(struct in6_addr) / sizeof(u32); i += sizeof(u32), j++) {
		entry = flow_action_entry_next(flow_rule);
		flow_offload_mangle(entry, FLOW_ACT_MANGLE_HDR_TYPE_IP6,
				    offset + i, &addr[j], mask);
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

static void flow_offload_redirect(struct net *net,
				  const struct flow_offload *flow,
				  enum flow_offload_tuple_dir dir,
				  struct nf_flow_rule *flow_rule)
{
	const struct flow_offload_tuple *this_tuple, *other_tuple;
	struct flow_action_entry *entry;
	struct net_device *dev;
	int ifindex;

	this_tuple = &flow->tuplehash[dir].tuple;
	switch (this_tuple->xmit_type) {
	case FLOW_OFFLOAD_XMIT_DIRECT:
		this_tuple = &flow->tuplehash[dir].tuple;
		ifindex = this_tuple->out.hw_ifidx;
		break;
	case FLOW_OFFLOAD_XMIT_NEIGH:
		other_tuple = &flow->tuplehash[!dir].tuple;
		ifindex = other_tuple->iifidx;
		break;
	default:
		return;
	}

	dev = dev_get_by_index(net, ifindex);
	if (!dev)
		return;

	entry = flow_action_entry_next(flow_rule);
	entry->id = FLOW_ACTION_REDIRECT;
	entry->dev = dev;
}

static void flow_offload_encap_tunnel(const struct flow_offload *flow,
				      enum flow_offload_tuple_dir dir,
				      struct nf_flow_rule *flow_rule)
{
	const struct flow_offload_tuple *this_tuple;
	struct flow_action_entry *entry;
	struct dst_entry *dst;

	this_tuple = &flow->tuplehash[dir].tuple;
	if (this_tuple->xmit_type == FLOW_OFFLOAD_XMIT_DIRECT)
		return;

	dst = this_tuple->dst_cache;
	if (dst && dst->lwtstate) {
		struct ip_tunnel_info *tun_info;

		tun_info = lwt_tun_info(dst->lwtstate);
		if (tun_info && (tun_info->mode & IP_TUNNEL_INFO_TX)) {
			entry = flow_action_entry_next(flow_rule);
			entry->id = FLOW_ACTION_TUNNEL_ENCAP;
			entry->tunnel = tun_info;
		}
	}
}

static void flow_offload_decap_tunnel(const struct flow_offload *flow,
				      enum flow_offload_tuple_dir dir,
				      struct nf_flow_rule *flow_rule)
{
	const struct flow_offload_tuple *other_tuple;
	struct flow_action_entry *entry;
	struct dst_entry *dst;

	other_tuple = &flow->tuplehash[!dir].tuple;
	if (other_tuple->xmit_type == FLOW_OFFLOAD_XMIT_DIRECT)
		return;

	dst = other_tuple->dst_cache;
	if (dst && dst->lwtstate) {
		struct ip_tunnel_info *tun_info;

		tun_info = lwt_tun_info(dst->lwtstate);
		if (tun_info && (tun_info->mode & IP_TUNNEL_INFO_TX)) {
			entry = flow_action_entry_next(flow_rule);
			entry->id = FLOW_ACTION_TUNNEL_DECAP;
		}
	}
}

static int
nf_flow_rule_route_common(struct net *net, const struct flow_offload *flow,
			  enum flow_offload_tuple_dir dir,
			  struct nf_flow_rule *flow_rule)
{
	const struct flow_offload_tuple *other_tuple;
	const struct flow_offload_tuple *tuple;
	int i;

	flow_offload_decap_tunnel(flow, dir, flow_rule);
	flow_offload_encap_tunnel(flow, dir, flow_rule);

	if (flow_offload_eth_src(net, flow, dir, flow_rule) < 0 ||
	    flow_offload_eth_dst(net, flow, dir, flow_rule) < 0)
		return -1;

	tuple = &flow->tuplehash[dir].tuple;

	for (i = 0; i < tuple->encap_num; i++) {
		struct flow_action_entry *entry;

		if (tuple->in_vlan_ingress & BIT(i))
			continue;

		if (tuple->encap[i].proto == htons(ETH_P_8021Q)) {
			entry = flow_action_entry_next(flow_rule);
			entry->id = FLOW_ACTION_VLAN_POP;
		}
	}

	other_tuple = &flow->tuplehash[!dir].tuple;

	for (i = 0; i < other_tuple->encap_num; i++) {
		struct flow_action_entry *entry;

		if (other_tuple->in_vlan_ingress & BIT(i))
			continue;

		entry = flow_action_entry_next(flow_rule);

		switch (other_tuple->encap[i].proto) {
		case htons(ETH_P_PPP_SES):
			entry->id = FLOW_ACTION_PPPOE_PUSH;
			entry->pppoe.sid = other_tuple->encap[i].id;
			break;
		case htons(ETH_P_8021Q):
			entry->id = FLOW_ACTION_VLAN_PUSH;
			entry->vlan.vid = other_tuple->encap[i].id;
			entry->vlan.proto = other_tuple->encap[i].proto;
			break;
		}
	}

	return 0;
}

int nf_flow_rule_route_ipv4(struct net *net, const struct flow_offload *flow,
			    enum flow_offload_tuple_dir dir,
			    struct nf_flow_rule *flow_rule)
{
	if (nf_flow_rule_route_common(net, flow, dir, flow_rule) < 0)
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

	flow_offload_redirect(net, flow, dir, flow_rule);

	return 0;
}
EXPORT_SYMBOL_GPL(nf_flow_rule_route_ipv4);

int nf_flow_rule_route_ipv6(struct net *net, const struct flow_offload *flow,
			    enum flow_offload_tuple_dir dir,
			    struct nf_flow_rule *flow_rule)
{
	if (nf_flow_rule_route_common(net, flow, dir, flow_rule) < 0)
		return -1;

	if (test_bit(NF_FLOW_SNAT, &flow->flags)) {
		flow_offload_ipv6_snat(net, flow, dir, flow_rule);
		flow_offload_port_snat(net, flow, dir, flow_rule);
	}
	if (test_bit(NF_FLOW_DNAT, &flow->flags)) {
		flow_offload_ipv6_dnat(net, flow, dir, flow_rule);
		flow_offload_port_dnat(net, flow, dir, flow_rule);
	}

	flow_offload_redirect(net, flow, dir, flow_rule);

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
	const struct flow_offload_tuple *tuple, *other_tuple;
	const struct flow_offload *flow = offload->flow;
	struct dst_entry *other_dst = NULL;
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
	other_tuple = &flow->tuplehash[!dir].tuple;
	if (other_tuple->xmit_type == FLOW_OFFLOAD_XMIT_NEIGH)
		other_dst = other_tuple->dst_cache;

	err = nf_flow_rule_match(&flow_rule->match, tuple, other_dst);
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
				 struct flow_stats *stats,
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

	down_read(&flowtable->flow_block_lock);
	list_for_each_entry(block_cb, block_cb_list, list) {
		err = block_cb->cb(TC_SETUP_CLSFLOWER, &cls_flow,
				   block_cb->cb_priv);
		if (err < 0)
			continue;

		i++;
	}
	up_read(&flowtable->flow_block_lock);

	if (cmd == FLOW_CLS_STATS)
		memcpy(stats, &cls_flow.stats, sizeof(*stats));

	return i;
}

static int flow_offload_tuple_add(struct flow_offload_work *offload,
				  struct nf_flow_rule *flow_rule,
				  enum flow_offload_tuple_dir dir)
{
	return nf_flow_offload_tuple(offload->flowtable, offload->flow,
				     flow_rule, dir, offload->priority,
				     FLOW_CLS_REPLACE, NULL,
				     &offload->flowtable->flow_block.cb_list);
}

static void flow_offload_tuple_del(struct flow_offload_work *offload,
				   enum flow_offload_tuple_dir dir)
{
	nf_flow_offload_tuple(offload->flowtable, offload->flow, NULL, dir,
			      offload->priority, FLOW_CLS_DESTROY, NULL,
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
		goto out;

	set_bit(IPS_HW_OFFLOAD_BIT, &offload->flow->ct->status);

out:
	nf_flow_offload_destroy(flow_rule);
}

static void flow_offload_work_del(struct flow_offload_work *offload)
{
	clear_bit(IPS_HW_OFFLOAD_BIT, &offload->flow->ct->status);
	flow_offload_tuple_del(offload, FLOW_OFFLOAD_DIR_ORIGINAL);
	flow_offload_tuple_del(offload, FLOW_OFFLOAD_DIR_REPLY);
	set_bit(NF_FLOW_HW_DEAD, &offload->flow->flags);
}

static void flow_offload_tuple_stats(struct flow_offload_work *offload,
				     enum flow_offload_tuple_dir dir,
				     struct flow_stats *stats)
{
	nf_flow_offload_tuple(offload->flowtable, offload->flow, NULL, dir,
			      offload->priority, FLOW_CLS_STATS, stats,
			      &offload->flowtable->flow_block.cb_list);
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

	if (offload->flowtable->flags & NF_FLOWTABLE_COUNTER) {
		if (stats[0].pkts)
			nf_ct_acct_add(offload->flow->ct,
				       FLOW_OFFLOAD_DIR_ORIGINAL,
				       stats[0].pkts, stats[0].bytes);
		if (stats[1].pkts)
			nf_ct_acct_add(offload->flow->ct,
				       FLOW_OFFLOAD_DIR_REPLY,
				       stats[1].pkts, stats[1].bytes);
	}
}

static void flow_offload_work_handler(struct work_struct *work)
{
	struct flow_offload_work *offload;

	offload = container_of(work, struct flow_offload_work, work);
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

	clear_bit(NF_FLOW_HW_PENDING, &offload->flow->flags);
	kfree(offload);
}

static void flow_offload_queue_work(struct flow_offload_work *offload)
{
	if (offload->cmd == FLOW_CLS_REPLACE)
		queue_work(nf_flow_offload_add_wq, &offload->work);
	else if (offload->cmd == FLOW_CLS_DESTROY)
		queue_work(nf_flow_offload_del_wq, &offload->work);
	else
		queue_work(nf_flow_offload_stats_wq, &offload->work);
}

static struct flow_offload_work *
nf_flow_offload_work_alloc(struct nf_flowtable *flowtable,
			   struct flow_offload *flow, unsigned int cmd)
{
	struct flow_offload_work *offload;

	if (test_and_set_bit(NF_FLOW_HW_PENDING, &flow->flags))
		return NULL;

	offload = kmalloc(sizeof(struct flow_offload_work), GFP_ATOMIC);
	if (!offload) {
		clear_bit(NF_FLOW_HW_PENDING, &flow->flags);
		return NULL;
	}

	offload->cmd = cmd;
	offload->flow = flow;
	offload->priority = flowtable->priority;
	offload->flowtable = flowtable;
	INIT_WORK(&offload->work, flow_offload_work_handler);

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
	if (nf_flowtable_hw_offload(flowtable)) {
		flush_workqueue(nf_flow_offload_add_wq);
		flush_workqueue(nf_flow_offload_del_wq);
		flush_workqueue(nf_flow_offload_stats_wq);
	}
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

static void nf_flow_table_block_offload_init(struct flow_block_offload *bo,
					     struct net *net,
					     enum flow_block_command cmd,
					     struct nf_flowtable *flowtable,
					     struct netlink_ext_ack *extack)
{
	memset(bo, 0, sizeof(*bo));
	bo->net		= net;
	bo->block	= &flowtable->flow_block;
	bo->command	= cmd;
	bo->binder_type	= FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS;
	bo->extack	= extack;
	INIT_LIST_HEAD(&bo->cb_list);
}

static void nf_flow_table_indr_cleanup(struct flow_block_cb *block_cb)
{
	struct nf_flowtable *flowtable = block_cb->indr.data;
	struct net_device *dev = block_cb->indr.dev;

	nf_flow_table_gc_cleanup(flowtable, dev);
	down_write(&flowtable->flow_block_lock);
	list_del(&block_cb->list);
	list_del(&block_cb->driver_list);
	flow_block_cb_free(block_cb);
	up_write(&flowtable->flow_block_lock);
}

static int nf_flow_table_indr_offload_cmd(struct flow_block_offload *bo,
					  struct nf_flowtable *flowtable,
					  struct net_device *dev,
					  enum flow_block_command cmd,
					  struct netlink_ext_ack *extack)
{
	nf_flow_table_block_offload_init(bo, dev_net(dev), cmd, flowtable,
					 extack);

	return flow_indr_dev_setup_offload(dev, NULL, TC_SETUP_FT, flowtable, bo,
					   nf_flow_table_indr_cleanup);
}

static int nf_flow_table_offload_cmd(struct flow_block_offload *bo,
				     struct nf_flowtable *flowtable,
				     struct net_device *dev,
				     enum flow_block_command cmd,
				     struct netlink_ext_ack *extack)
{
	int err;

	nf_flow_table_block_offload_init(bo, dev_net(dev), cmd, flowtable,
					 extack);
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

	if (dev->netdev_ops->ndo_setup_tc)
		err = nf_flow_table_offload_cmd(&bo, flowtable, dev, cmd,
						&extack);
	else
		err = nf_flow_table_indr_offload_cmd(&bo, flowtable, dev, cmd,
						     &extack);
	if (err < 0)
		return err;

	return nf_flow_table_block_setup(flowtable, &bo, cmd);
}
EXPORT_SYMBOL_GPL(nf_flow_table_offload_setup);

int nf_flow_table_offload_init(void)
{
	nf_flow_offload_add_wq  = alloc_workqueue("nf_ft_offload_add",
						  WQ_UNBOUND | WQ_SYSFS, 0);
	if (!nf_flow_offload_add_wq)
		return -ENOMEM;

	nf_flow_offload_del_wq  = alloc_workqueue("nf_ft_offload_del",
						  WQ_UNBOUND | WQ_SYSFS, 0);
	if (!nf_flow_offload_del_wq)
		goto err_del_wq;

	nf_flow_offload_stats_wq  = alloc_workqueue("nf_ft_offload_stats",
						    WQ_UNBOUND | WQ_SYSFS, 0);
	if (!nf_flow_offload_stats_wq)
		goto err_stats_wq;

	return 0;

err_stats_wq:
	destroy_workqueue(nf_flow_offload_del_wq);
err_del_wq:
	destroy_workqueue(nf_flow_offload_add_wq);
	return -ENOMEM;
}

void nf_flow_table_offload_exit(void)
{
	destroy_workqueue(nf_flow_offload_add_wq);
	destroy_workqueue(nf_flow_offload_del_wq);
	destroy_workqueue(nf_flow_offload_stats_wq);
}
