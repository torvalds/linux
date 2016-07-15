#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/export.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <linux/igmp.h>
#include <linux/icmp.h>
#include <linux/sctp.h>
#include <linux/dccp.h>
#include <linux/if_tunnel.h>
#include <linux/if_pppox.h>
#include <linux/ppp_defs.h>
#include <linux/stddef.h>
#include <linux/if_ether.h>
#include <linux/mpls.h>
#include <net/flow_dissector.h>
#include <scsi/fc/fc_fcoe.h>

static void dissector_set_key(struct flow_dissector *flow_dissector,
			      enum flow_dissector_key_id key_id)
{
	flow_dissector->used_keys |= (1 << key_id);
}

void skb_flow_dissector_init(struct flow_dissector *flow_dissector,
			     const struct flow_dissector_key *key,
			     unsigned int key_count)
{
	unsigned int i;

	memset(flow_dissector, 0, sizeof(*flow_dissector));

	for (i = 0; i < key_count; i++, key++) {
		/* User should make sure that every key target offset is withing
		 * boundaries of unsigned short.
		 */
		BUG_ON(key->offset > USHRT_MAX);
		BUG_ON(dissector_uses_key(flow_dissector,
					  key->key_id));

		dissector_set_key(flow_dissector, key->key_id);
		flow_dissector->offset[key->key_id] = key->offset;
	}

	/* Ensure that the dissector always includes control and basic key.
	 * That way we are able to avoid handling lack of these in fast path.
	 */
	BUG_ON(!dissector_uses_key(flow_dissector,
				   FLOW_DISSECTOR_KEY_CONTROL));
	BUG_ON(!dissector_uses_key(flow_dissector,
				   FLOW_DISSECTOR_KEY_BASIC));
}
EXPORT_SYMBOL(skb_flow_dissector_init);

/**
 * __skb_flow_get_ports - extract the upper layer ports and return them
 * @skb: sk_buff to extract the ports from
 * @thoff: transport header offset
 * @ip_proto: protocol for which to get port offset
 * @data: raw buffer pointer to the packet, if NULL use skb->data
 * @hlen: packet header length, if @data is NULL use skb_headlen(skb)
 *
 * The function will try to retrieve the ports at offset thoff + poff where poff
 * is the protocol port offset returned from proto_ports_offset
 */
__be32 __skb_flow_get_ports(const struct sk_buff *skb, int thoff, u8 ip_proto,
			    void *data, int hlen)
{
	int poff = proto_ports_offset(ip_proto);

	if (!data) {
		data = skb->data;
		hlen = skb_headlen(skb);
	}

	if (poff >= 0) {
		__be32 *ports, _ports;

		ports = __skb_header_pointer(skb, thoff + poff,
					     sizeof(_ports), data, hlen, &_ports);
		if (ports)
			return *ports;
	}

	return 0;
}
EXPORT_SYMBOL(__skb_flow_get_ports);

/**
 * __skb_flow_dissect - extract the flow_keys struct and return it
 * @skb: sk_buff to extract the flow from, can be NULL if the rest are specified
 * @flow_dissector: list of keys to dissect
 * @target_container: target structure to put dissected values into
 * @data: raw buffer pointer to the packet, if NULL use skb->data
 * @proto: protocol for which to get the flow, if @data is NULL use skb->protocol
 * @nhoff: network header offset, if @data is NULL use skb_network_offset(skb)
 * @hlen: packet header length, if @data is NULL use skb_headlen(skb)
 *
 * The function will try to retrieve individual keys into target specified
 * by flow_dissector from either the skbuff or a raw buffer specified by the
 * rest parameters.
 *
 * Caller must take care of zeroing target container memory.
 */
bool __skb_flow_dissect(const struct sk_buff *skb,
			struct flow_dissector *flow_dissector,
			void *target_container,
			void *data, __be16 proto, int nhoff, int hlen,
			unsigned int flags)
{
	struct flow_dissector_key_control *key_control;
	struct flow_dissector_key_basic *key_basic;
	struct flow_dissector_key_addrs *key_addrs;
	struct flow_dissector_key_ports *key_ports;
	struct flow_dissector_key_tags *key_tags;
	struct flow_dissector_key_keyid *key_keyid;
	u8 ip_proto = 0;
	bool ret = false;

	if (!data) {
		data = skb->data;
		proto = skb->protocol;
		nhoff = skb_network_offset(skb);
		hlen = skb_headlen(skb);
	}

	/* It is ensured by skb_flow_dissector_init() that control key will
	 * be always present.
	 */
	key_control = skb_flow_dissector_target(flow_dissector,
						FLOW_DISSECTOR_KEY_CONTROL,
						target_container);

	/* It is ensured by skb_flow_dissector_init() that basic key will
	 * be always present.
	 */
	key_basic = skb_flow_dissector_target(flow_dissector,
					      FLOW_DISSECTOR_KEY_BASIC,
					      target_container);

	if (dissector_uses_key(flow_dissector,
			       FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct ethhdr *eth = eth_hdr(skb);
		struct flow_dissector_key_eth_addrs *key_eth_addrs;

		key_eth_addrs = skb_flow_dissector_target(flow_dissector,
							  FLOW_DISSECTOR_KEY_ETH_ADDRS,
							  target_container);
		memcpy(key_eth_addrs, &eth->h_dest, sizeof(*key_eth_addrs));
	}

again:
	switch (proto) {
	case htons(ETH_P_IP): {
		const struct iphdr *iph;
		struct iphdr _iph;
ip:
		iph = __skb_header_pointer(skb, nhoff, sizeof(_iph), data, hlen, &_iph);
		if (!iph || iph->ihl < 5)
			goto out_bad;
		nhoff += iph->ihl * 4;

		ip_proto = iph->protocol;

		if (dissector_uses_key(flow_dissector,
				       FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
			key_addrs = skb_flow_dissector_target(flow_dissector,
							      FLOW_DISSECTOR_KEY_IPV4_ADDRS,
							      target_container);

			memcpy(&key_addrs->v4addrs, &iph->saddr,
			       sizeof(key_addrs->v4addrs));
			key_control->addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
		}

		if (ip_is_fragment(iph)) {
			key_control->flags |= FLOW_DIS_IS_FRAGMENT;

			if (iph->frag_off & htons(IP_OFFSET)) {
				goto out_good;
			} else {
				key_control->flags |= FLOW_DIS_FIRST_FRAG;
				if (!(flags & FLOW_DISSECTOR_F_PARSE_1ST_FRAG))
					goto out_good;
			}
		}

		if (flags & FLOW_DISSECTOR_F_STOP_AT_L3)
			goto out_good;

		break;
	}
	case htons(ETH_P_IPV6): {
		const struct ipv6hdr *iph;
		struct ipv6hdr _iph;

ipv6:
		iph = __skb_header_pointer(skb, nhoff, sizeof(_iph), data, hlen, &_iph);
		if (!iph)
			goto out_bad;

		ip_proto = iph->nexthdr;
		nhoff += sizeof(struct ipv6hdr);

		if (dissector_uses_key(flow_dissector,
				       FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
			key_addrs = skb_flow_dissector_target(flow_dissector,
							      FLOW_DISSECTOR_KEY_IPV6_ADDRS,
							      target_container);

			memcpy(&key_addrs->v6addrs, &iph->saddr,
			       sizeof(key_addrs->v6addrs));
			key_control->addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
		}

		if ((dissector_uses_key(flow_dissector,
					FLOW_DISSECTOR_KEY_FLOW_LABEL) ||
		     (flags & FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL)) &&
		    ip6_flowlabel(iph)) {
			__be32 flow_label = ip6_flowlabel(iph);

			if (dissector_uses_key(flow_dissector,
					       FLOW_DISSECTOR_KEY_FLOW_LABEL)) {
				key_tags = skb_flow_dissector_target(flow_dissector,
								     FLOW_DISSECTOR_KEY_FLOW_LABEL,
								     target_container);
				key_tags->flow_label = ntohl(flow_label);
			}
			if (flags & FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL)
				goto out_good;
		}

		if (flags & FLOW_DISSECTOR_F_STOP_AT_L3)
			goto out_good;

		break;
	}
	case htons(ETH_P_8021AD):
	case htons(ETH_P_8021Q): {
		const struct vlan_hdr *vlan;
		struct vlan_hdr _vlan;

		vlan = __skb_header_pointer(skb, nhoff, sizeof(_vlan), data, hlen, &_vlan);
		if (!vlan)
			goto out_bad;

		if (dissector_uses_key(flow_dissector,
				       FLOW_DISSECTOR_KEY_VLANID)) {
			key_tags = skb_flow_dissector_target(flow_dissector,
							     FLOW_DISSECTOR_KEY_VLANID,
							     target_container);

			key_tags->vlan_id = skb_vlan_tag_get_id(skb);
		}

		proto = vlan->h_vlan_encapsulated_proto;
		nhoff += sizeof(*vlan);
		goto again;
	}
	case htons(ETH_P_PPP_SES): {
		struct {
			struct pppoe_hdr hdr;
			__be16 proto;
		} *hdr, _hdr;
		hdr = __skb_header_pointer(skb, nhoff, sizeof(_hdr), data, hlen, &_hdr);
		if (!hdr)
			goto out_bad;
		proto = hdr->proto;
		nhoff += PPPOE_SES_HLEN;
		switch (proto) {
		case htons(PPP_IP):
			goto ip;
		case htons(PPP_IPV6):
			goto ipv6;
		default:
			goto out_bad;
		}
	}
	case htons(ETH_P_TIPC): {
		struct {
			__be32 pre[3];
			__be32 srcnode;
		} *hdr, _hdr;
		hdr = __skb_header_pointer(skb, nhoff, sizeof(_hdr), data, hlen, &_hdr);
		if (!hdr)
			goto out_bad;

		if (dissector_uses_key(flow_dissector,
				       FLOW_DISSECTOR_KEY_TIPC_ADDRS)) {
			key_addrs = skb_flow_dissector_target(flow_dissector,
							      FLOW_DISSECTOR_KEY_TIPC_ADDRS,
							      target_container);
			key_addrs->tipcaddrs.srcnode = hdr->srcnode;
			key_control->addr_type = FLOW_DISSECTOR_KEY_TIPC_ADDRS;
		}
		goto out_good;
	}

	case htons(ETH_P_MPLS_UC):
	case htons(ETH_P_MPLS_MC): {
		struct mpls_label *hdr, _hdr[2];
mpls:
		hdr = __skb_header_pointer(skb, nhoff, sizeof(_hdr), data,
					   hlen, &_hdr);
		if (!hdr)
			goto out_bad;

		if ((ntohl(hdr[0].entry) & MPLS_LS_LABEL_MASK) >>
		     MPLS_LS_LABEL_SHIFT == MPLS_LABEL_ENTROPY) {
			if (dissector_uses_key(flow_dissector,
					       FLOW_DISSECTOR_KEY_MPLS_ENTROPY)) {
				key_keyid = skb_flow_dissector_target(flow_dissector,
								      FLOW_DISSECTOR_KEY_MPLS_ENTROPY,
								      target_container);
				key_keyid->keyid = hdr[1].entry &
					htonl(MPLS_LS_LABEL_MASK);
			}

			goto out_good;
		}

		goto out_good;
	}

	case htons(ETH_P_FCOE):
		if ((hlen - nhoff) < FCOE_HEADER_LEN)
			goto out_bad;

		nhoff += FCOE_HEADER_LEN;
		goto out_good;
	default:
		goto out_bad;
	}

ip_proto_again:
	switch (ip_proto) {
	case IPPROTO_GRE: {
		struct gre_hdr {
			__be16 flags;
			__be16 proto;
		} *hdr, _hdr;

		hdr = __skb_header_pointer(skb, nhoff, sizeof(_hdr), data, hlen, &_hdr);
		if (!hdr)
			goto out_bad;
		/*
		 * Only look inside GRE if version zero and no
		 * routing
		 */
		if (hdr->flags & (GRE_VERSION | GRE_ROUTING))
			break;

		proto = hdr->proto;
		nhoff += 4;
		if (hdr->flags & GRE_CSUM)
			nhoff += 4;
		if (hdr->flags & GRE_KEY) {
			const __be32 *keyid;
			__be32 _keyid;

			keyid = __skb_header_pointer(skb, nhoff, sizeof(_keyid),
						     data, hlen, &_keyid);

			if (!keyid)
				goto out_bad;

			if (dissector_uses_key(flow_dissector,
					       FLOW_DISSECTOR_KEY_GRE_KEYID)) {
				key_keyid = skb_flow_dissector_target(flow_dissector,
								      FLOW_DISSECTOR_KEY_GRE_KEYID,
								      target_container);
				key_keyid->keyid = *keyid;
			}
			nhoff += 4;
		}
		if (hdr->flags & GRE_SEQ)
			nhoff += 4;
		if (proto == htons(ETH_P_TEB)) {
			const struct ethhdr *eth;
			struct ethhdr _eth;

			eth = __skb_header_pointer(skb, nhoff,
						   sizeof(_eth),
						   data, hlen, &_eth);
			if (!eth)
				goto out_bad;
			proto = eth->h_proto;
			nhoff += sizeof(*eth);

			/* Cap headers that we access via pointers at the
			 * end of the Ethernet header as our maximum alignment
			 * at that point is only 2 bytes.
			 */
			if (NET_IP_ALIGN)
				hlen = nhoff;
		}

		key_control->flags |= FLOW_DIS_ENCAPSULATION;
		if (flags & FLOW_DISSECTOR_F_STOP_AT_ENCAP)
			goto out_good;

		goto again;
	}
	case NEXTHDR_HOP:
	case NEXTHDR_ROUTING:
	case NEXTHDR_DEST: {
		u8 _opthdr[2], *opthdr;

		if (proto != htons(ETH_P_IPV6))
			break;

		opthdr = __skb_header_pointer(skb, nhoff, sizeof(_opthdr),
					      data, hlen, &_opthdr);
		if (!opthdr)
			goto out_bad;

		ip_proto = opthdr[0];
		nhoff += (opthdr[1] + 1) << 3;

		goto ip_proto_again;
	}
	case NEXTHDR_FRAGMENT: {
		struct frag_hdr _fh, *fh;

		if (proto != htons(ETH_P_IPV6))
			break;

		fh = __skb_header_pointer(skb, nhoff, sizeof(_fh),
					  data, hlen, &_fh);

		if (!fh)
			goto out_bad;

		key_control->flags |= FLOW_DIS_IS_FRAGMENT;

		nhoff += sizeof(_fh);
		ip_proto = fh->nexthdr;

		if (!(fh->frag_off & htons(IP6_OFFSET))) {
			key_control->flags |= FLOW_DIS_FIRST_FRAG;
			if (flags & FLOW_DISSECTOR_F_PARSE_1ST_FRAG)
				goto ip_proto_again;
		}
		goto out_good;
	}
	case IPPROTO_IPIP:
		proto = htons(ETH_P_IP);

		key_control->flags |= FLOW_DIS_ENCAPSULATION;
		if (flags & FLOW_DISSECTOR_F_STOP_AT_ENCAP)
			goto out_good;

		goto ip;
	case IPPROTO_IPV6:
		proto = htons(ETH_P_IPV6);

		key_control->flags |= FLOW_DIS_ENCAPSULATION;
		if (flags & FLOW_DISSECTOR_F_STOP_AT_ENCAP)
			goto out_good;

		goto ipv6;
	case IPPROTO_MPLS:
		proto = htons(ETH_P_MPLS_UC);
		goto mpls;
	default:
		break;
	}

	if (dissector_uses_key(flow_dissector,
			       FLOW_DISSECTOR_KEY_PORTS)) {
		key_ports = skb_flow_dissector_target(flow_dissector,
						      FLOW_DISSECTOR_KEY_PORTS,
						      target_container);
		key_ports->ports = __skb_flow_get_ports(skb, nhoff, ip_proto,
							data, hlen);
	}

out_good:
	ret = true;

out_bad:
	key_basic->n_proto = proto;
	key_basic->ip_proto = ip_proto;
	key_control->thoff = (u16)nhoff;

	return ret;
}
EXPORT_SYMBOL(__skb_flow_dissect);

static u32 hashrnd __read_mostly;
static __always_inline void __flow_hash_secret_init(void)
{
	net_get_random_once(&hashrnd, sizeof(hashrnd));
}

static __always_inline u32 __flow_hash_words(const u32 *words, u32 length,
					     u32 keyval)
{
	return jhash2(words, length, keyval);
}

static inline const u32 *flow_keys_hash_start(const struct flow_keys *flow)
{
	const void *p = flow;

	BUILD_BUG_ON(FLOW_KEYS_HASH_OFFSET % sizeof(u32));
	return (const u32 *)(p + FLOW_KEYS_HASH_OFFSET);
}

static inline size_t flow_keys_hash_length(const struct flow_keys *flow)
{
	size_t diff = FLOW_KEYS_HASH_OFFSET + sizeof(flow->addrs);
	BUILD_BUG_ON((sizeof(*flow) - FLOW_KEYS_HASH_OFFSET) % sizeof(u32));
	BUILD_BUG_ON(offsetof(typeof(*flow), addrs) !=
		     sizeof(*flow) - sizeof(flow->addrs));

	switch (flow->control.addr_type) {
	case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
		diff -= sizeof(flow->addrs.v4addrs);
		break;
	case FLOW_DISSECTOR_KEY_IPV6_ADDRS:
		diff -= sizeof(flow->addrs.v6addrs);
		break;
	case FLOW_DISSECTOR_KEY_TIPC_ADDRS:
		diff -= sizeof(flow->addrs.tipcaddrs);
		break;
	}
	return (sizeof(*flow) - diff) / sizeof(u32);
}

__be32 flow_get_u32_src(const struct flow_keys *flow)
{
	switch (flow->control.addr_type) {
	case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
		return flow->addrs.v4addrs.src;
	case FLOW_DISSECTOR_KEY_IPV6_ADDRS:
		return (__force __be32)ipv6_addr_hash(
			&flow->addrs.v6addrs.src);
	case FLOW_DISSECTOR_KEY_TIPC_ADDRS:
		return flow->addrs.tipcaddrs.srcnode;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(flow_get_u32_src);

__be32 flow_get_u32_dst(const struct flow_keys *flow)
{
	switch (flow->control.addr_type) {
	case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
		return flow->addrs.v4addrs.dst;
	case FLOW_DISSECTOR_KEY_IPV6_ADDRS:
		return (__force __be32)ipv6_addr_hash(
			&flow->addrs.v6addrs.dst);
	default:
		return 0;
	}
}
EXPORT_SYMBOL(flow_get_u32_dst);

static inline void __flow_hash_consistentify(struct flow_keys *keys)
{
	int addr_diff, i;

	switch (keys->control.addr_type) {
	case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
		addr_diff = (__force u32)keys->addrs.v4addrs.dst -
			    (__force u32)keys->addrs.v4addrs.src;
		if ((addr_diff < 0) ||
		    (addr_diff == 0 &&
		     ((__force u16)keys->ports.dst <
		      (__force u16)keys->ports.src))) {
			swap(keys->addrs.v4addrs.src, keys->addrs.v4addrs.dst);
			swap(keys->ports.src, keys->ports.dst);
		}
		break;
	case FLOW_DISSECTOR_KEY_IPV6_ADDRS:
		addr_diff = memcmp(&keys->addrs.v6addrs.dst,
				   &keys->addrs.v6addrs.src,
				   sizeof(keys->addrs.v6addrs.dst));
		if ((addr_diff < 0) ||
		    (addr_diff == 0 &&
		     ((__force u16)keys->ports.dst <
		      (__force u16)keys->ports.src))) {
			for (i = 0; i < 4; i++)
				swap(keys->addrs.v6addrs.src.s6_addr32[i],
				     keys->addrs.v6addrs.dst.s6_addr32[i]);
			swap(keys->ports.src, keys->ports.dst);
		}
		break;
	}
}

static inline u32 __flow_hash_from_keys(struct flow_keys *keys, u32 keyval)
{
	u32 hash;

	__flow_hash_consistentify(keys);

	hash = __flow_hash_words(flow_keys_hash_start(keys),
				 flow_keys_hash_length(keys), keyval);
	if (!hash)
		hash = 1;

	return hash;
}

u32 flow_hash_from_keys(struct flow_keys *keys)
{
	__flow_hash_secret_init();
	return __flow_hash_from_keys(keys, hashrnd);
}
EXPORT_SYMBOL(flow_hash_from_keys);

static inline u32 ___skb_get_hash(const struct sk_buff *skb,
				  struct flow_keys *keys, u32 keyval)
{
	skb_flow_dissect_flow_keys(skb, keys,
				   FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL);

	return __flow_hash_from_keys(keys, keyval);
}

struct _flow_keys_digest_data {
	__be16	n_proto;
	u8	ip_proto;
	u8	padding;
	__be32	ports;
	__be32	src;
	__be32	dst;
};

void make_flow_keys_digest(struct flow_keys_digest *digest,
			   const struct flow_keys *flow)
{
	struct _flow_keys_digest_data *data =
	    (struct _flow_keys_digest_data *)digest;

	BUILD_BUG_ON(sizeof(*data) > sizeof(*digest));

	memset(digest, 0, sizeof(*digest));

	data->n_proto = flow->basic.n_proto;
	data->ip_proto = flow->basic.ip_proto;
	data->ports = flow->ports.ports;
	data->src = flow->addrs.v4addrs.src;
	data->dst = flow->addrs.v4addrs.dst;
}
EXPORT_SYMBOL(make_flow_keys_digest);

static struct flow_dissector flow_keys_dissector_symmetric __read_mostly;

u32 __skb_get_hash_symmetric(struct sk_buff *skb)
{
	struct flow_keys keys;

	__flow_hash_secret_init();

	memset(&keys, 0, sizeof(keys));
	__skb_flow_dissect(skb, &flow_keys_dissector_symmetric, &keys,
			   NULL, 0, 0, 0,
			   FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL);

	return __flow_hash_from_keys(&keys, hashrnd);
}
EXPORT_SYMBOL_GPL(__skb_get_hash_symmetric);

/**
 * __skb_get_hash: calculate a flow hash
 * @skb: sk_buff to calculate flow hash from
 *
 * This function calculates a flow hash based on src/dst addresses
 * and src/dst port numbers.  Sets hash in skb to non-zero hash value
 * on success, zero indicates no valid hash.  Also, sets l4_hash in skb
 * if hash is a canonical 4-tuple hash over transport ports.
 */
void __skb_get_hash(struct sk_buff *skb)
{
	struct flow_keys keys;

	__flow_hash_secret_init();

	__skb_set_sw_hash(skb, ___skb_get_hash(skb, &keys, hashrnd),
			  flow_keys_have_l4(&keys));
}
EXPORT_SYMBOL(__skb_get_hash);

__u32 skb_get_hash_perturb(const struct sk_buff *skb, u32 perturb)
{
	struct flow_keys keys;

	return ___skb_get_hash(skb, &keys, perturb);
}
EXPORT_SYMBOL(skb_get_hash_perturb);

__u32 __skb_get_hash_flowi6(struct sk_buff *skb, const struct flowi6 *fl6)
{
	struct flow_keys keys;

	memset(&keys, 0, sizeof(keys));

	memcpy(&keys.addrs.v6addrs.src, &fl6->saddr,
	       sizeof(keys.addrs.v6addrs.src));
	memcpy(&keys.addrs.v6addrs.dst, &fl6->daddr,
	       sizeof(keys.addrs.v6addrs.dst));
	keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
	keys.ports.src = fl6->fl6_sport;
	keys.ports.dst = fl6->fl6_dport;
	keys.keyid.keyid = fl6->fl6_gre_key;
	keys.tags.flow_label = (__force u32)fl6->flowlabel;
	keys.basic.ip_proto = fl6->flowi6_proto;

	__skb_set_sw_hash(skb, flow_hash_from_keys(&keys),
			  flow_keys_have_l4(&keys));

	return skb->hash;
}
EXPORT_SYMBOL(__skb_get_hash_flowi6);

__u32 __skb_get_hash_flowi4(struct sk_buff *skb, const struct flowi4 *fl4)
{
	struct flow_keys keys;

	memset(&keys, 0, sizeof(keys));

	keys.addrs.v4addrs.src = fl4->saddr;
	keys.addrs.v4addrs.dst = fl4->daddr;
	keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
	keys.ports.src = fl4->fl4_sport;
	keys.ports.dst = fl4->fl4_dport;
	keys.keyid.keyid = fl4->fl4_gre_key;
	keys.basic.ip_proto = fl4->flowi4_proto;

	__skb_set_sw_hash(skb, flow_hash_from_keys(&keys),
			  flow_keys_have_l4(&keys));

	return skb->hash;
}
EXPORT_SYMBOL(__skb_get_hash_flowi4);

u32 __skb_get_poff(const struct sk_buff *skb, void *data,
		   const struct flow_keys *keys, int hlen)
{
	u32 poff = keys->control.thoff;

	/* skip L4 headers for fragments after the first */
	if ((keys->control.flags & FLOW_DIS_IS_FRAGMENT) &&
	    !(keys->control.flags & FLOW_DIS_FIRST_FRAG))
		return poff;

	switch (keys->basic.ip_proto) {
	case IPPROTO_TCP: {
		/* access doff as u8 to avoid unaligned access */
		const u8 *doff;
		u8 _doff;

		doff = __skb_header_pointer(skb, poff + 12, sizeof(_doff),
					    data, hlen, &_doff);
		if (!doff)
			return poff;

		poff += max_t(u32, sizeof(struct tcphdr), (*doff & 0xF0) >> 2);
		break;
	}
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
		poff += sizeof(struct udphdr);
		break;
	/* For the rest, we do not really care about header
	 * extensions at this point for now.
	 */
	case IPPROTO_ICMP:
		poff += sizeof(struct icmphdr);
		break;
	case IPPROTO_ICMPV6:
		poff += sizeof(struct icmp6hdr);
		break;
	case IPPROTO_IGMP:
		poff += sizeof(struct igmphdr);
		break;
	case IPPROTO_DCCP:
		poff += sizeof(struct dccp_hdr);
		break;
	case IPPROTO_SCTP:
		poff += sizeof(struct sctphdr);
		break;
	}

	return poff;
}

/**
 * skb_get_poff - get the offset to the payload
 * @skb: sk_buff to get the payload offset from
 *
 * The function will get the offset to the payload as far as it could
 * be dissected.  The main user is currently BPF, so that we can dynamically
 * truncate packets without needing to push actual payload to the user
 * space and can analyze headers only, instead.
 */
u32 skb_get_poff(const struct sk_buff *skb)
{
	struct flow_keys keys;

	if (!skb_flow_dissect_flow_keys(skb, &keys, 0))
		return 0;

	return __skb_get_poff(skb, skb->data, &keys, skb_headlen(skb));
}

__u32 __get_hash_from_flowi6(const struct flowi6 *fl6, struct flow_keys *keys)
{
	memset(keys, 0, sizeof(*keys));

	memcpy(&keys->addrs.v6addrs.src, &fl6->saddr,
	    sizeof(keys->addrs.v6addrs.src));
	memcpy(&keys->addrs.v6addrs.dst, &fl6->daddr,
	    sizeof(keys->addrs.v6addrs.dst));
	keys->control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
	keys->ports.src = fl6->fl6_sport;
	keys->ports.dst = fl6->fl6_dport;
	keys->keyid.keyid = fl6->fl6_gre_key;
	keys->tags.flow_label = (__force u32)fl6->flowlabel;
	keys->basic.ip_proto = fl6->flowi6_proto;

	return flow_hash_from_keys(keys);
}
EXPORT_SYMBOL(__get_hash_from_flowi6);

__u32 __get_hash_from_flowi4(const struct flowi4 *fl4, struct flow_keys *keys)
{
	memset(keys, 0, sizeof(*keys));

	keys->addrs.v4addrs.src = fl4->saddr;
	keys->addrs.v4addrs.dst = fl4->daddr;
	keys->control.addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
	keys->ports.src = fl4->fl4_sport;
	keys->ports.dst = fl4->fl4_dport;
	keys->keyid.keyid = fl4->fl4_gre_key;
	keys->basic.ip_proto = fl4->flowi4_proto;

	return flow_hash_from_keys(keys);
}
EXPORT_SYMBOL(__get_hash_from_flowi4);

static const struct flow_dissector_key flow_keys_dissector_keys[] = {
	{
		.key_id = FLOW_DISSECTOR_KEY_CONTROL,
		.offset = offsetof(struct flow_keys, control),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_BASIC,
		.offset = offsetof(struct flow_keys, basic),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_IPV4_ADDRS,
		.offset = offsetof(struct flow_keys, addrs.v4addrs),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_IPV6_ADDRS,
		.offset = offsetof(struct flow_keys, addrs.v6addrs),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_TIPC_ADDRS,
		.offset = offsetof(struct flow_keys, addrs.tipcaddrs),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_PORTS,
		.offset = offsetof(struct flow_keys, ports),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_VLANID,
		.offset = offsetof(struct flow_keys, tags),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_FLOW_LABEL,
		.offset = offsetof(struct flow_keys, tags),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_GRE_KEYID,
		.offset = offsetof(struct flow_keys, keyid),
	},
};

static const struct flow_dissector_key flow_keys_dissector_symmetric_keys[] = {
	{
		.key_id = FLOW_DISSECTOR_KEY_CONTROL,
		.offset = offsetof(struct flow_keys, control),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_BASIC,
		.offset = offsetof(struct flow_keys, basic),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_IPV4_ADDRS,
		.offset = offsetof(struct flow_keys, addrs.v4addrs),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_IPV6_ADDRS,
		.offset = offsetof(struct flow_keys, addrs.v6addrs),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_PORTS,
		.offset = offsetof(struct flow_keys, ports),
	},
};

static const struct flow_dissector_key flow_keys_buf_dissector_keys[] = {
	{
		.key_id = FLOW_DISSECTOR_KEY_CONTROL,
		.offset = offsetof(struct flow_keys, control),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_BASIC,
		.offset = offsetof(struct flow_keys, basic),
	},
};

struct flow_dissector flow_keys_dissector __read_mostly;
EXPORT_SYMBOL(flow_keys_dissector);

struct flow_dissector flow_keys_buf_dissector __read_mostly;

static int __init init_default_flow_dissectors(void)
{
	skb_flow_dissector_init(&flow_keys_dissector,
				flow_keys_dissector_keys,
				ARRAY_SIZE(flow_keys_dissector_keys));
	skb_flow_dissector_init(&flow_keys_dissector_symmetric,
				flow_keys_dissector_symmetric_keys,
				ARRAY_SIZE(flow_keys_dissector_symmetric_keys));
	skb_flow_dissector_init(&flow_keys_buf_dissector,
				flow_keys_buf_dissector_keys,
				ARRAY_SIZE(flow_keys_buf_dissector_keys));
	return 0;
}

late_initcall_sync(init_default_flow_dissectors);
