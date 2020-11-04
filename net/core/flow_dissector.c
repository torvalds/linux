// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/export.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/if_vlan.h>
#include <net/dsa.h>
#include <net/dst_metadata.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/gre.h>
#include <net/pptp.h>
#include <net/tipc.h>
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
#include <linux/tcp.h>
#include <net/flow_dissector.h>
#include <scsi/fc/fc_fcoe.h>
#include <uapi/linux/batadv_packet.h>
#include <linux/bpf.h>
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_labels.h>
#endif
#include <linux/bpf-netns.h>

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

#ifdef CONFIG_BPF_SYSCALL
int flow_dissector_bpf_prog_attach_check(struct net *net,
					 struct bpf_prog *prog)
{
	enum netns_bpf_attach_type type = NETNS_BPF_FLOW_DISSECTOR;

	if (net == &init_net) {
		/* BPF flow dissector in the root namespace overrides
		 * any per-net-namespace one. When attaching to root,
		 * make sure we don't have any BPF program attached
		 * to the non-root namespaces.
		 */
		struct net *ns;

		for_each_net(ns) {
			if (ns == &init_net)
				continue;
			if (rcu_access_pointer(ns->bpf.run_array[type]))
				return -EEXIST;
		}
	} else {
		/* Make sure root flow dissector is not attached
		 * when attaching to the non-root namespace.
		 */
		if (rcu_access_pointer(init_net.bpf.run_array[type]))
			return -EEXIST;
	}

	return 0;
}
#endif /* CONFIG_BPF_SYSCALL */

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

static bool icmp_has_id(u8 type)
{
	switch (type) {
	case ICMP_ECHO:
	case ICMP_ECHOREPLY:
	case ICMP_TIMESTAMP:
	case ICMP_TIMESTAMPREPLY:
	case ICMPV6_ECHO_REQUEST:
	case ICMPV6_ECHO_REPLY:
		return true;
	}

	return false;
}

/**
 * skb_flow_get_icmp_tci - extract ICMP(6) Type, Code and Identifier fields
 * @skb: sk_buff to extract from
 * @key_icmp: struct flow_dissector_key_icmp to fill
 * @data: raw buffer pointer to the packet
 * @thoff: offset to extract at
 * @hlen: packet header length
 */
void skb_flow_get_icmp_tci(const struct sk_buff *skb,
			   struct flow_dissector_key_icmp *key_icmp,
			   void *data, int thoff, int hlen)
{
	struct icmphdr *ih, _ih;

	ih = __skb_header_pointer(skb, thoff, sizeof(_ih), data, hlen, &_ih);
	if (!ih)
		return;

	key_icmp->type = ih->type;
	key_icmp->code = ih->code;

	/* As we use 0 to signal that the Id field is not present,
	 * avoid confusion with packets without such field
	 */
	if (icmp_has_id(ih->type))
		key_icmp->id = ih->un.echo.id ? : 1;
	else
		key_icmp->id = 0;
}
EXPORT_SYMBOL(skb_flow_get_icmp_tci);

/* If FLOW_DISSECTOR_KEY_ICMP is set, dissect an ICMP packet
 * using skb_flow_get_icmp_tci().
 */
static void __skb_flow_dissect_icmp(const struct sk_buff *skb,
				    struct flow_dissector *flow_dissector,
				    void *target_container,
				    void *data, int thoff, int hlen)
{
	struct flow_dissector_key_icmp *key_icmp;

	if (!dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_ICMP))
		return;

	key_icmp = skb_flow_dissector_target(flow_dissector,
					     FLOW_DISSECTOR_KEY_ICMP,
					     target_container);

	skb_flow_get_icmp_tci(skb, key_icmp, data, thoff, hlen);
}

void skb_flow_dissect_meta(const struct sk_buff *skb,
			   struct flow_dissector *flow_dissector,
			   void *target_container)
{
	struct flow_dissector_key_meta *meta;

	if (!dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_META))
		return;

	meta = skb_flow_dissector_target(flow_dissector,
					 FLOW_DISSECTOR_KEY_META,
					 target_container);
	meta->ingress_ifindex = skb->skb_iif;
}
EXPORT_SYMBOL(skb_flow_dissect_meta);

static void
skb_flow_dissect_set_enc_addr_type(enum flow_dissector_key_id type,
				   struct flow_dissector *flow_dissector,
				   void *target_container)
{
	struct flow_dissector_key_control *ctrl;

	if (!dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_ENC_CONTROL))
		return;

	ctrl = skb_flow_dissector_target(flow_dissector,
					 FLOW_DISSECTOR_KEY_ENC_CONTROL,
					 target_container);
	ctrl->addr_type = type;
}

void
skb_flow_dissect_ct(const struct sk_buff *skb,
		    struct flow_dissector *flow_dissector,
		    void *target_container,
		    u16 *ctinfo_map,
		    size_t mapsize)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	struct flow_dissector_key_ct *key;
	enum ip_conntrack_info ctinfo;
	struct nf_conn_labels *cl;
	struct nf_conn *ct;

	if (!dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_CT))
		return;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return;

	key = skb_flow_dissector_target(flow_dissector,
					FLOW_DISSECTOR_KEY_CT,
					target_container);

	if (ctinfo < mapsize)
		key->ct_state = ctinfo_map[ctinfo];
#if IS_ENABLED(CONFIG_NF_CONNTRACK_ZONES)
	key->ct_zone = ct->zone.id;
#endif
#if IS_ENABLED(CONFIG_NF_CONNTRACK_MARK)
	key->ct_mark = ct->mark;
#endif

	cl = nf_ct_labels_find(ct);
	if (cl)
		memcpy(key->ct_labels, cl->bits, sizeof(key->ct_labels));
#endif /* CONFIG_NF_CONNTRACK */
}
EXPORT_SYMBOL(skb_flow_dissect_ct);

void
skb_flow_dissect_tunnel_info(const struct sk_buff *skb,
			     struct flow_dissector *flow_dissector,
			     void *target_container)
{
	struct ip_tunnel_info *info;
	struct ip_tunnel_key *key;

	/* A quick check to see if there might be something to do. */
	if (!dissector_uses_key(flow_dissector,
				FLOW_DISSECTOR_KEY_ENC_KEYID) &&
	    !dissector_uses_key(flow_dissector,
				FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) &&
	    !dissector_uses_key(flow_dissector,
				FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS) &&
	    !dissector_uses_key(flow_dissector,
				FLOW_DISSECTOR_KEY_ENC_CONTROL) &&
	    !dissector_uses_key(flow_dissector,
				FLOW_DISSECTOR_KEY_ENC_PORTS) &&
	    !dissector_uses_key(flow_dissector,
				FLOW_DISSECTOR_KEY_ENC_IP) &&
	    !dissector_uses_key(flow_dissector,
				FLOW_DISSECTOR_KEY_ENC_OPTS))
		return;

	info = skb_tunnel_info(skb);
	if (!info)
		return;

	key = &info->key;

	switch (ip_tunnel_info_af(info)) {
	case AF_INET:
		skb_flow_dissect_set_enc_addr_type(FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						   flow_dissector,
						   target_container);
		if (dissector_uses_key(flow_dissector,
				       FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS)) {
			struct flow_dissector_key_ipv4_addrs *ipv4;

			ipv4 = skb_flow_dissector_target(flow_dissector,
							 FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS,
							 target_container);
			ipv4->src = key->u.ipv4.src;
			ipv4->dst = key->u.ipv4.dst;
		}
		break;
	case AF_INET6:
		skb_flow_dissect_set_enc_addr_type(FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						   flow_dissector,
						   target_container);
		if (dissector_uses_key(flow_dissector,
				       FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS)) {
			struct flow_dissector_key_ipv6_addrs *ipv6;

			ipv6 = skb_flow_dissector_target(flow_dissector,
							 FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS,
							 target_container);
			ipv6->src = key->u.ipv6.src;
			ipv6->dst = key->u.ipv6.dst;
		}
		break;
	}

	if (dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_dissector_key_keyid *keyid;

		keyid = skb_flow_dissector_target(flow_dissector,
						  FLOW_DISSECTOR_KEY_ENC_KEYID,
						  target_container);
		keyid->keyid = tunnel_id_to_key32(key->tun_id);
	}

	if (dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_ENC_PORTS)) {
		struct flow_dissector_key_ports *tp;

		tp = skb_flow_dissector_target(flow_dissector,
					       FLOW_DISSECTOR_KEY_ENC_PORTS,
					       target_container);
		tp->src = key->tp_src;
		tp->dst = key->tp_dst;
	}

	if (dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_ENC_IP)) {
		struct flow_dissector_key_ip *ip;

		ip = skb_flow_dissector_target(flow_dissector,
					       FLOW_DISSECTOR_KEY_ENC_IP,
					       target_container);
		ip->tos = key->tos;
		ip->ttl = key->ttl;
	}

	if (dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_ENC_OPTS)) {
		struct flow_dissector_key_enc_opts *enc_opt;

		enc_opt = skb_flow_dissector_target(flow_dissector,
						    FLOW_DISSECTOR_KEY_ENC_OPTS,
						    target_container);

		if (info->options_len) {
			enc_opt->len = info->options_len;
			ip_tunnel_info_opts_get(enc_opt->data, info);
			enc_opt->dst_opt_type = info->key.tun_flags &
						TUNNEL_OPTIONS_PRESENT;
		}
	}
}
EXPORT_SYMBOL(skb_flow_dissect_tunnel_info);

void skb_flow_dissect_hash(const struct sk_buff *skb,
			   struct flow_dissector *flow_dissector,
			   void *target_container)
{
	struct flow_dissector_key_hash *key;

	if (!dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_HASH))
		return;

	key = skb_flow_dissector_target(flow_dissector,
					FLOW_DISSECTOR_KEY_HASH,
					target_container);

	key->hash = skb_get_hash_raw(skb);
}
EXPORT_SYMBOL(skb_flow_dissect_hash);

static enum flow_dissect_ret
__skb_flow_dissect_mpls(const struct sk_buff *skb,
			struct flow_dissector *flow_dissector,
			void *target_container, void *data, int nhoff, int hlen,
			int lse_index, bool *entropy_label)
{
	struct mpls_label *hdr, _hdr;
	u32 entry, label, bos;

	if (!dissector_uses_key(flow_dissector,
				FLOW_DISSECTOR_KEY_MPLS_ENTROPY) &&
	    !dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_MPLS))
		return FLOW_DISSECT_RET_OUT_GOOD;

	if (lse_index >= FLOW_DIS_MPLS_MAX)
		return FLOW_DISSECT_RET_OUT_GOOD;

	hdr = __skb_header_pointer(skb, nhoff, sizeof(_hdr), data,
				   hlen, &_hdr);
	if (!hdr)
		return FLOW_DISSECT_RET_OUT_BAD;

	entry = ntohl(hdr->entry);
	label = (entry & MPLS_LS_LABEL_MASK) >> MPLS_LS_LABEL_SHIFT;
	bos = (entry & MPLS_LS_S_MASK) >> MPLS_LS_S_SHIFT;

	if (dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_MPLS)) {
		struct flow_dissector_key_mpls *key_mpls;
		struct flow_dissector_mpls_lse *lse;

		key_mpls = skb_flow_dissector_target(flow_dissector,
						     FLOW_DISSECTOR_KEY_MPLS,
						     target_container);
		lse = &key_mpls->ls[lse_index];

		lse->mpls_ttl = (entry & MPLS_LS_TTL_MASK) >> MPLS_LS_TTL_SHIFT;
		lse->mpls_bos = bos;
		lse->mpls_tc = (entry & MPLS_LS_TC_MASK) >> MPLS_LS_TC_SHIFT;
		lse->mpls_label = label;
		dissector_set_mpls_lse(key_mpls, lse_index);
	}

	if (*entropy_label &&
	    dissector_uses_key(flow_dissector,
			       FLOW_DISSECTOR_KEY_MPLS_ENTROPY)) {
		struct flow_dissector_key_keyid *key_keyid;

		key_keyid = skb_flow_dissector_target(flow_dissector,
						      FLOW_DISSECTOR_KEY_MPLS_ENTROPY,
						      target_container);
		key_keyid->keyid = cpu_to_be32(label);
	}

	*entropy_label = label == MPLS_LABEL_ENTROPY;

	return bos ? FLOW_DISSECT_RET_OUT_GOOD : FLOW_DISSECT_RET_PROTO_AGAIN;
}

static enum flow_dissect_ret
__skb_flow_dissect_arp(const struct sk_buff *skb,
		       struct flow_dissector *flow_dissector,
		       void *target_container, void *data, int nhoff, int hlen)
{
	struct flow_dissector_key_arp *key_arp;
	struct {
		unsigned char ar_sha[ETH_ALEN];
		unsigned char ar_sip[4];
		unsigned char ar_tha[ETH_ALEN];
		unsigned char ar_tip[4];
	} *arp_eth, _arp_eth;
	const struct arphdr *arp;
	struct arphdr _arp;

	if (!dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_ARP))
		return FLOW_DISSECT_RET_OUT_GOOD;

	arp = __skb_header_pointer(skb, nhoff, sizeof(_arp), data,
				   hlen, &_arp);
	if (!arp)
		return FLOW_DISSECT_RET_OUT_BAD;

	if (arp->ar_hrd != htons(ARPHRD_ETHER) ||
	    arp->ar_pro != htons(ETH_P_IP) ||
	    arp->ar_hln != ETH_ALEN ||
	    arp->ar_pln != 4 ||
	    (arp->ar_op != htons(ARPOP_REPLY) &&
	     arp->ar_op != htons(ARPOP_REQUEST)))
		return FLOW_DISSECT_RET_OUT_BAD;

	arp_eth = __skb_header_pointer(skb, nhoff + sizeof(_arp),
				       sizeof(_arp_eth), data,
				       hlen, &_arp_eth);
	if (!arp_eth)
		return FLOW_DISSECT_RET_OUT_BAD;

	key_arp = skb_flow_dissector_target(flow_dissector,
					    FLOW_DISSECTOR_KEY_ARP,
					    target_container);

	memcpy(&key_arp->sip, arp_eth->ar_sip, sizeof(key_arp->sip));
	memcpy(&key_arp->tip, arp_eth->ar_tip, sizeof(key_arp->tip));

	/* Only store the lower byte of the opcode;
	 * this covers ARPOP_REPLY and ARPOP_REQUEST.
	 */
	key_arp->op = ntohs(arp->ar_op) & 0xff;

	ether_addr_copy(key_arp->sha, arp_eth->ar_sha);
	ether_addr_copy(key_arp->tha, arp_eth->ar_tha);

	return FLOW_DISSECT_RET_OUT_GOOD;
}

static enum flow_dissect_ret
__skb_flow_dissect_gre(const struct sk_buff *skb,
		       struct flow_dissector_key_control *key_control,
		       struct flow_dissector *flow_dissector,
		       void *target_container, void *data,
		       __be16 *p_proto, int *p_nhoff, int *p_hlen,
		       unsigned int flags)
{
	struct flow_dissector_key_keyid *key_keyid;
	struct gre_base_hdr *hdr, _hdr;
	int offset = 0;
	u16 gre_ver;

	hdr = __skb_header_pointer(skb, *p_nhoff, sizeof(_hdr),
				   data, *p_hlen, &_hdr);
	if (!hdr)
		return FLOW_DISSECT_RET_OUT_BAD;

	/* Only look inside GRE without routing */
	if (hdr->flags & GRE_ROUTING)
		return FLOW_DISSECT_RET_OUT_GOOD;

	/* Only look inside GRE for version 0 and 1 */
	gre_ver = ntohs(hdr->flags & GRE_VERSION);
	if (gre_ver > 1)
		return FLOW_DISSECT_RET_OUT_GOOD;

	*p_proto = hdr->protocol;
	if (gre_ver) {
		/* Version1 must be PPTP, and check the flags */
		if (!(*p_proto == GRE_PROTO_PPP && (hdr->flags & GRE_KEY)))
			return FLOW_DISSECT_RET_OUT_GOOD;
	}

	offset += sizeof(struct gre_base_hdr);

	if (hdr->flags & GRE_CSUM)
		offset += sizeof_field(struct gre_full_hdr, csum) +
			  sizeof_field(struct gre_full_hdr, reserved1);

	if (hdr->flags & GRE_KEY) {
		const __be32 *keyid;
		__be32 _keyid;

		keyid = __skb_header_pointer(skb, *p_nhoff + offset,
					     sizeof(_keyid),
					     data, *p_hlen, &_keyid);
		if (!keyid)
			return FLOW_DISSECT_RET_OUT_BAD;

		if (dissector_uses_key(flow_dissector,
				       FLOW_DISSECTOR_KEY_GRE_KEYID)) {
			key_keyid = skb_flow_dissector_target(flow_dissector,
							      FLOW_DISSECTOR_KEY_GRE_KEYID,
							      target_container);
			if (gre_ver == 0)
				key_keyid->keyid = *keyid;
			else
				key_keyid->keyid = *keyid & GRE_PPTP_KEY_MASK;
		}
		offset += sizeof_field(struct gre_full_hdr, key);
	}

	if (hdr->flags & GRE_SEQ)
		offset += sizeof_field(struct pptp_gre_header, seq);

	if (gre_ver == 0) {
		if (*p_proto == htons(ETH_P_TEB)) {
			const struct ethhdr *eth;
			struct ethhdr _eth;

			eth = __skb_header_pointer(skb, *p_nhoff + offset,
						   sizeof(_eth),
						   data, *p_hlen, &_eth);
			if (!eth)
				return FLOW_DISSECT_RET_OUT_BAD;
			*p_proto = eth->h_proto;
			offset += sizeof(*eth);

			/* Cap headers that we access via pointers at the
			 * end of the Ethernet header as our maximum alignment
			 * at that point is only 2 bytes.
			 */
			if (NET_IP_ALIGN)
				*p_hlen = *p_nhoff + offset;
		}
	} else { /* version 1, must be PPTP */
		u8 _ppp_hdr[PPP_HDRLEN];
		u8 *ppp_hdr;

		if (hdr->flags & GRE_ACK)
			offset += sizeof_field(struct pptp_gre_header, ack);

		ppp_hdr = __skb_header_pointer(skb, *p_nhoff + offset,
					       sizeof(_ppp_hdr),
					       data, *p_hlen, _ppp_hdr);
		if (!ppp_hdr)
			return FLOW_DISSECT_RET_OUT_BAD;

		switch (PPP_PROTOCOL(ppp_hdr)) {
		case PPP_IP:
			*p_proto = htons(ETH_P_IP);
			break;
		case PPP_IPV6:
			*p_proto = htons(ETH_P_IPV6);
			break;
		default:
			/* Could probably catch some more like MPLS */
			break;
		}

		offset += PPP_HDRLEN;
	}

	*p_nhoff += offset;
	key_control->flags |= FLOW_DIS_ENCAPSULATION;
	if (flags & FLOW_DISSECTOR_F_STOP_AT_ENCAP)
		return FLOW_DISSECT_RET_OUT_GOOD;

	return FLOW_DISSECT_RET_PROTO_AGAIN;
}

/**
 * __skb_flow_dissect_batadv() - dissect batman-adv header
 * @skb: sk_buff to with the batman-adv header
 * @key_control: flow dissectors control key
 * @data: raw buffer pointer to the packet, if NULL use skb->data
 * @p_proto: pointer used to update the protocol to process next
 * @p_nhoff: pointer used to update inner network header offset
 * @hlen: packet header length
 * @flags: any combination of FLOW_DISSECTOR_F_*
 *
 * ETH_P_BATMAN packets are tried to be dissected. Only
 * &struct batadv_unicast packets are actually processed because they contain an
 * inner ethernet header and are usually followed by actual network header. This
 * allows the flow dissector to continue processing the packet.
 *
 * Return: FLOW_DISSECT_RET_PROTO_AGAIN when &struct batadv_unicast was found,
 *  FLOW_DISSECT_RET_OUT_GOOD when dissector should stop after encapsulation,
 *  otherwise FLOW_DISSECT_RET_OUT_BAD
 */
static enum flow_dissect_ret
__skb_flow_dissect_batadv(const struct sk_buff *skb,
			  struct flow_dissector_key_control *key_control,
			  void *data, __be16 *p_proto, int *p_nhoff, int hlen,
			  unsigned int flags)
{
	struct {
		struct batadv_unicast_packet batadv_unicast;
		struct ethhdr eth;
	} *hdr, _hdr;

	hdr = __skb_header_pointer(skb, *p_nhoff, sizeof(_hdr), data, hlen,
				   &_hdr);
	if (!hdr)
		return FLOW_DISSECT_RET_OUT_BAD;

	if (hdr->batadv_unicast.version != BATADV_COMPAT_VERSION)
		return FLOW_DISSECT_RET_OUT_BAD;

	if (hdr->batadv_unicast.packet_type != BATADV_UNICAST)
		return FLOW_DISSECT_RET_OUT_BAD;

	*p_proto = hdr->eth.h_proto;
	*p_nhoff += sizeof(*hdr);

	key_control->flags |= FLOW_DIS_ENCAPSULATION;
	if (flags & FLOW_DISSECTOR_F_STOP_AT_ENCAP)
		return FLOW_DISSECT_RET_OUT_GOOD;

	return FLOW_DISSECT_RET_PROTO_AGAIN;
}

static void
__skb_flow_dissect_tcp(const struct sk_buff *skb,
		       struct flow_dissector *flow_dissector,
		       void *target_container, void *data, int thoff, int hlen)
{
	struct flow_dissector_key_tcp *key_tcp;
	struct tcphdr *th, _th;

	if (!dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_TCP))
		return;

	th = __skb_header_pointer(skb, thoff, sizeof(_th), data, hlen, &_th);
	if (!th)
		return;

	if (unlikely(__tcp_hdrlen(th) < sizeof(_th)))
		return;

	key_tcp = skb_flow_dissector_target(flow_dissector,
					    FLOW_DISSECTOR_KEY_TCP,
					    target_container);
	key_tcp->flags = (*(__be16 *) &tcp_flag_word(th) & htons(0x0FFF));
}

static void
__skb_flow_dissect_ports(const struct sk_buff *skb,
			 struct flow_dissector *flow_dissector,
			 void *target_container, void *data, int nhoff,
			 u8 ip_proto, int hlen)
{
	enum flow_dissector_key_id dissector_ports = FLOW_DISSECTOR_KEY_MAX;
	struct flow_dissector_key_ports *key_ports;

	if (dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_PORTS))
		dissector_ports = FLOW_DISSECTOR_KEY_PORTS;
	else if (dissector_uses_key(flow_dissector,
				    FLOW_DISSECTOR_KEY_PORTS_RANGE))
		dissector_ports = FLOW_DISSECTOR_KEY_PORTS_RANGE;

	if (dissector_ports == FLOW_DISSECTOR_KEY_MAX)
		return;

	key_ports = skb_flow_dissector_target(flow_dissector,
					      dissector_ports,
					      target_container);
	key_ports->ports = __skb_flow_get_ports(skb, nhoff, ip_proto,
						data, hlen);
}

static void
__skb_flow_dissect_ipv4(const struct sk_buff *skb,
			struct flow_dissector *flow_dissector,
			void *target_container, void *data, const struct iphdr *iph)
{
	struct flow_dissector_key_ip *key_ip;

	if (!dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_IP))
		return;

	key_ip = skb_flow_dissector_target(flow_dissector,
					   FLOW_DISSECTOR_KEY_IP,
					   target_container);
	key_ip->tos = iph->tos;
	key_ip->ttl = iph->ttl;
}

static void
__skb_flow_dissect_ipv6(const struct sk_buff *skb,
			struct flow_dissector *flow_dissector,
			void *target_container, void *data, const struct ipv6hdr *iph)
{
	struct flow_dissector_key_ip *key_ip;

	if (!dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_IP))
		return;

	key_ip = skb_flow_dissector_target(flow_dissector,
					   FLOW_DISSECTOR_KEY_IP,
					   target_container);
	key_ip->tos = ipv6_get_dsfield(iph);
	key_ip->ttl = iph->hop_limit;
}

/* Maximum number of protocol headers that can be parsed in
 * __skb_flow_dissect
 */
#define MAX_FLOW_DISSECT_HDRS	15

static bool skb_flow_dissect_allowed(int *num_hdrs)
{
	++*num_hdrs;

	return (*num_hdrs <= MAX_FLOW_DISSECT_HDRS);
}

static void __skb_flow_bpf_to_target(const struct bpf_flow_keys *flow_keys,
				     struct flow_dissector *flow_dissector,
				     void *target_container)
{
	struct flow_dissector_key_ports *key_ports = NULL;
	struct flow_dissector_key_control *key_control;
	struct flow_dissector_key_basic *key_basic;
	struct flow_dissector_key_addrs *key_addrs;
	struct flow_dissector_key_tags *key_tags;

	key_control = skb_flow_dissector_target(flow_dissector,
						FLOW_DISSECTOR_KEY_CONTROL,
						target_container);
	key_control->thoff = flow_keys->thoff;
	if (flow_keys->is_frag)
		key_control->flags |= FLOW_DIS_IS_FRAGMENT;
	if (flow_keys->is_first_frag)
		key_control->flags |= FLOW_DIS_FIRST_FRAG;
	if (flow_keys->is_encap)
		key_control->flags |= FLOW_DIS_ENCAPSULATION;

	key_basic = skb_flow_dissector_target(flow_dissector,
					      FLOW_DISSECTOR_KEY_BASIC,
					      target_container);
	key_basic->n_proto = flow_keys->n_proto;
	key_basic->ip_proto = flow_keys->ip_proto;

	if (flow_keys->addr_proto == ETH_P_IP &&
	    dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		key_addrs = skb_flow_dissector_target(flow_dissector,
						      FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						      target_container);
		key_addrs->v4addrs.src = flow_keys->ipv4_src;
		key_addrs->v4addrs.dst = flow_keys->ipv4_dst;
		key_control->addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
	} else if (flow_keys->addr_proto == ETH_P_IPV6 &&
		   dissector_uses_key(flow_dissector,
				      FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		key_addrs = skb_flow_dissector_target(flow_dissector,
						      FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						      target_container);
		memcpy(&key_addrs->v6addrs, &flow_keys->ipv6_src,
		       sizeof(key_addrs->v6addrs));
		key_control->addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
	}

	if (dissector_uses_key(flow_dissector, FLOW_DISSECTOR_KEY_PORTS))
		key_ports = skb_flow_dissector_target(flow_dissector,
						      FLOW_DISSECTOR_KEY_PORTS,
						      target_container);
	else if (dissector_uses_key(flow_dissector,
				    FLOW_DISSECTOR_KEY_PORTS_RANGE))
		key_ports = skb_flow_dissector_target(flow_dissector,
						      FLOW_DISSECTOR_KEY_PORTS_RANGE,
						      target_container);

	if (key_ports) {
		key_ports->src = flow_keys->sport;
		key_ports->dst = flow_keys->dport;
	}

	if (dissector_uses_key(flow_dissector,
			       FLOW_DISSECTOR_KEY_FLOW_LABEL)) {
		key_tags = skb_flow_dissector_target(flow_dissector,
						     FLOW_DISSECTOR_KEY_FLOW_LABEL,
						     target_container);
		key_tags->flow_label = ntohl(flow_keys->flow_label);
	}
}

bool bpf_flow_dissect(struct bpf_prog *prog, struct bpf_flow_dissector *ctx,
		      __be16 proto, int nhoff, int hlen, unsigned int flags)
{
	struct bpf_flow_keys *flow_keys = ctx->flow_keys;
	u32 result;

	/* Pass parameters to the BPF program */
	memset(flow_keys, 0, sizeof(*flow_keys));
	flow_keys->n_proto = proto;
	flow_keys->nhoff = nhoff;
	flow_keys->thoff = flow_keys->nhoff;

	BUILD_BUG_ON((int)BPF_FLOW_DISSECTOR_F_PARSE_1ST_FRAG !=
		     (int)FLOW_DISSECTOR_F_PARSE_1ST_FRAG);
	BUILD_BUG_ON((int)BPF_FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL !=
		     (int)FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL);
	BUILD_BUG_ON((int)BPF_FLOW_DISSECTOR_F_STOP_AT_ENCAP !=
		     (int)FLOW_DISSECTOR_F_STOP_AT_ENCAP);
	flow_keys->flags = flags;

	result = bpf_prog_run_pin_on_cpu(prog, ctx);

	flow_keys->nhoff = clamp_t(u16, flow_keys->nhoff, nhoff, hlen);
	flow_keys->thoff = clamp_t(u16, flow_keys->thoff,
				   flow_keys->nhoff, hlen);

	return result == BPF_OK;
}

/**
 * __skb_flow_dissect - extract the flow_keys struct and return it
 * @net: associated network namespace, derived from @skb if NULL
 * @skb: sk_buff to extract the flow from, can be NULL if the rest are specified
 * @flow_dissector: list of keys to dissect
 * @target_container: target structure to put dissected values into
 * @data: raw buffer pointer to the packet, if NULL use skb->data
 * @proto: protocol for which to get the flow, if @data is NULL use skb->protocol
 * @nhoff: network header offset, if @data is NULL use skb_network_offset(skb)
 * @hlen: packet header length, if @data is NULL use skb_headlen(skb)
 * @flags: flags that control the dissection process, e.g.
 *         FLOW_DISSECTOR_F_STOP_AT_ENCAP.
 *
 * The function will try to retrieve individual keys into target specified
 * by flow_dissector from either the skbuff or a raw buffer specified by the
 * rest parameters.
 *
 * Caller must take care of zeroing target container memory.
 */
bool __skb_flow_dissect(const struct net *net,
			const struct sk_buff *skb,
			struct flow_dissector *flow_dissector,
			void *target_container,
			void *data, __be16 proto, int nhoff, int hlen,
			unsigned int flags)
{
	struct flow_dissector_key_control *key_control;
	struct flow_dissector_key_basic *key_basic;
	struct flow_dissector_key_addrs *key_addrs;
	struct flow_dissector_key_tags *key_tags;
	struct flow_dissector_key_vlan *key_vlan;
	enum flow_dissect_ret fdret;
	enum flow_dissector_key_id dissector_vlan = FLOW_DISSECTOR_KEY_MAX;
	bool mpls_el = false;
	int mpls_lse = 0;
	int num_hdrs = 0;
	u8 ip_proto = 0;
	bool ret;

	if (!data) {
		data = skb->data;
		proto = skb_vlan_tag_present(skb) ?
			 skb->vlan_proto : skb->protocol;
		nhoff = skb_network_offset(skb);
		hlen = skb_headlen(skb);
#if IS_ENABLED(CONFIG_NET_DSA)
		if (unlikely(skb->dev && netdev_uses_dsa(skb->dev) &&
			     proto == htons(ETH_P_XDSA))) {
			const struct dsa_device_ops *ops;
			int offset = 0;

			ops = skb->dev->dsa_ptr->tag_ops;
			/* Tail taggers don't break flow dissection */
			if (!ops->tail_tag) {
				if (ops->flow_dissect)
					ops->flow_dissect(skb, &proto, &offset);
				else
					dsa_tag_generic_flow_dissect(skb,
								     &proto,
								     &offset);
				hlen -= offset;
				nhoff += offset;
			}
		}
#endif
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

	if (skb) {
		if (!net) {
			if (skb->dev)
				net = dev_net(skb->dev);
			else if (skb->sk)
				net = sock_net(skb->sk);
		}
	}

	WARN_ON_ONCE(!net);
	if (net) {
		enum netns_bpf_attach_type type = NETNS_BPF_FLOW_DISSECTOR;
		struct bpf_prog_array *run_array;

		rcu_read_lock();
		run_array = rcu_dereference(init_net.bpf.run_array[type]);
		if (!run_array)
			run_array = rcu_dereference(net->bpf.run_array[type]);

		if (run_array) {
			struct bpf_flow_keys flow_keys;
			struct bpf_flow_dissector ctx = {
				.flow_keys = &flow_keys,
				.data = data,
				.data_end = data + hlen,
			};
			__be16 n_proto = proto;
			struct bpf_prog *prog;

			if (skb) {
				ctx.skb = skb;
				/* we can't use 'proto' in the skb case
				 * because it might be set to skb->vlan_proto
				 * which has been pulled from the data
				 */
				n_proto = skb->protocol;
			}

			prog = READ_ONCE(run_array->items[0].prog);
			ret = bpf_flow_dissect(prog, &ctx, n_proto, nhoff,
					       hlen, flags);
			__skb_flow_bpf_to_target(&flow_keys, flow_dissector,
						 target_container);
			rcu_read_unlock();
			return ret;
		}
		rcu_read_unlock();
	}

	if (dissector_uses_key(flow_dissector,
			       FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct ethhdr *eth = eth_hdr(skb);
		struct flow_dissector_key_eth_addrs *key_eth_addrs;

		key_eth_addrs = skb_flow_dissector_target(flow_dissector,
							  FLOW_DISSECTOR_KEY_ETH_ADDRS,
							  target_container);
		memcpy(key_eth_addrs, &eth->h_dest, sizeof(*key_eth_addrs));
	}

proto_again:
	fdret = FLOW_DISSECT_RET_CONTINUE;

	switch (proto) {
	case htons(ETH_P_IP): {
		const struct iphdr *iph;
		struct iphdr _iph;

		iph = __skb_header_pointer(skb, nhoff, sizeof(_iph), data, hlen, &_iph);
		if (!iph || iph->ihl < 5) {
			fdret = FLOW_DISSECT_RET_OUT_BAD;
			break;
		}

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
				fdret = FLOW_DISSECT_RET_OUT_GOOD;
				break;
			} else {
				key_control->flags |= FLOW_DIS_FIRST_FRAG;
				if (!(flags &
				      FLOW_DISSECTOR_F_PARSE_1ST_FRAG)) {
					fdret = FLOW_DISSECT_RET_OUT_GOOD;
					break;
				}
			}
		}

		__skb_flow_dissect_ipv4(skb, flow_dissector,
					target_container, data, iph);

		break;
	}
	case htons(ETH_P_IPV6): {
		const struct ipv6hdr *iph;
		struct ipv6hdr _iph;

		iph = __skb_header_pointer(skb, nhoff, sizeof(_iph), data, hlen, &_iph);
		if (!iph) {
			fdret = FLOW_DISSECT_RET_OUT_BAD;
			break;
		}

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
			if (flags & FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL) {
				fdret = FLOW_DISSECT_RET_OUT_GOOD;
				break;
			}
		}

		__skb_flow_dissect_ipv6(skb, flow_dissector,
					target_container, data, iph);

		break;
	}
	case htons(ETH_P_8021AD):
	case htons(ETH_P_8021Q): {
		const struct vlan_hdr *vlan = NULL;
		struct vlan_hdr _vlan;
		__be16 saved_vlan_tpid = proto;

		if (dissector_vlan == FLOW_DISSECTOR_KEY_MAX &&
		    skb && skb_vlan_tag_present(skb)) {
			proto = skb->protocol;
		} else {
			vlan = __skb_header_pointer(skb, nhoff, sizeof(_vlan),
						    data, hlen, &_vlan);
			if (!vlan) {
				fdret = FLOW_DISSECT_RET_OUT_BAD;
				break;
			}

			proto = vlan->h_vlan_encapsulated_proto;
			nhoff += sizeof(*vlan);
		}

		if (dissector_vlan == FLOW_DISSECTOR_KEY_MAX) {
			dissector_vlan = FLOW_DISSECTOR_KEY_VLAN;
		} else if (dissector_vlan == FLOW_DISSECTOR_KEY_VLAN) {
			dissector_vlan = FLOW_DISSECTOR_KEY_CVLAN;
		} else {
			fdret = FLOW_DISSECT_RET_PROTO_AGAIN;
			break;
		}

		if (dissector_uses_key(flow_dissector, dissector_vlan)) {
			key_vlan = skb_flow_dissector_target(flow_dissector,
							     dissector_vlan,
							     target_container);

			if (!vlan) {
				key_vlan->vlan_id = skb_vlan_tag_get_id(skb);
				key_vlan->vlan_priority = skb_vlan_tag_get_prio(skb);
			} else {
				key_vlan->vlan_id = ntohs(vlan->h_vlan_TCI) &
					VLAN_VID_MASK;
				key_vlan->vlan_priority =
					(ntohs(vlan->h_vlan_TCI) &
					 VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
			}
			key_vlan->vlan_tpid = saved_vlan_tpid;
		}

		fdret = FLOW_DISSECT_RET_PROTO_AGAIN;
		break;
	}
	case htons(ETH_P_PPP_SES): {
		struct {
			struct pppoe_hdr hdr;
			__be16 proto;
		} *hdr, _hdr;
		hdr = __skb_header_pointer(skb, nhoff, sizeof(_hdr), data, hlen, &_hdr);
		if (!hdr) {
			fdret = FLOW_DISSECT_RET_OUT_BAD;
			break;
		}

		proto = hdr->proto;
		nhoff += PPPOE_SES_HLEN;
		switch (proto) {
		case htons(PPP_IP):
			proto = htons(ETH_P_IP);
			fdret = FLOW_DISSECT_RET_PROTO_AGAIN;
			break;
		case htons(PPP_IPV6):
			proto = htons(ETH_P_IPV6);
			fdret = FLOW_DISSECT_RET_PROTO_AGAIN;
			break;
		default:
			fdret = FLOW_DISSECT_RET_OUT_BAD;
			break;
		}
		break;
	}
	case htons(ETH_P_TIPC): {
		struct tipc_basic_hdr *hdr, _hdr;

		hdr = __skb_header_pointer(skb, nhoff, sizeof(_hdr),
					   data, hlen, &_hdr);
		if (!hdr) {
			fdret = FLOW_DISSECT_RET_OUT_BAD;
			break;
		}

		if (dissector_uses_key(flow_dissector,
				       FLOW_DISSECTOR_KEY_TIPC)) {
			key_addrs = skb_flow_dissector_target(flow_dissector,
							      FLOW_DISSECTOR_KEY_TIPC,
							      target_container);
			key_addrs->tipckey.key = tipc_hdr_rps_key(hdr);
			key_control->addr_type = FLOW_DISSECTOR_KEY_TIPC;
		}
		fdret = FLOW_DISSECT_RET_OUT_GOOD;
		break;
	}

	case htons(ETH_P_MPLS_UC):
	case htons(ETH_P_MPLS_MC):
		fdret = __skb_flow_dissect_mpls(skb, flow_dissector,
						target_container, data,
						nhoff, hlen, mpls_lse,
						&mpls_el);
		nhoff += sizeof(struct mpls_label);
		mpls_lse++;
		break;
	case htons(ETH_P_FCOE):
		if ((hlen - nhoff) < FCOE_HEADER_LEN) {
			fdret = FLOW_DISSECT_RET_OUT_BAD;
			break;
		}

		nhoff += FCOE_HEADER_LEN;
		fdret = FLOW_DISSECT_RET_OUT_GOOD;
		break;

	case htons(ETH_P_ARP):
	case htons(ETH_P_RARP):
		fdret = __skb_flow_dissect_arp(skb, flow_dissector,
					       target_container, data,
					       nhoff, hlen);
		break;

	case htons(ETH_P_BATMAN):
		fdret = __skb_flow_dissect_batadv(skb, key_control, data,
						  &proto, &nhoff, hlen, flags);
		break;

	default:
		fdret = FLOW_DISSECT_RET_OUT_BAD;
		break;
	}

	/* Process result of proto processing */
	switch (fdret) {
	case FLOW_DISSECT_RET_OUT_GOOD:
		goto out_good;
	case FLOW_DISSECT_RET_PROTO_AGAIN:
		if (skb_flow_dissect_allowed(&num_hdrs))
			goto proto_again;
		goto out_good;
	case FLOW_DISSECT_RET_CONTINUE:
	case FLOW_DISSECT_RET_IPPROTO_AGAIN:
		break;
	case FLOW_DISSECT_RET_OUT_BAD:
	default:
		goto out_bad;
	}

ip_proto_again:
	fdret = FLOW_DISSECT_RET_CONTINUE;

	switch (ip_proto) {
	case IPPROTO_GRE:
		fdret = __skb_flow_dissect_gre(skb, key_control, flow_dissector,
					       target_container, data,
					       &proto, &nhoff, &hlen, flags);
		break;

	case NEXTHDR_HOP:
	case NEXTHDR_ROUTING:
	case NEXTHDR_DEST: {
		u8 _opthdr[2], *opthdr;

		if (proto != htons(ETH_P_IPV6))
			break;

		opthdr = __skb_header_pointer(skb, nhoff, sizeof(_opthdr),
					      data, hlen, &_opthdr);
		if (!opthdr) {
			fdret = FLOW_DISSECT_RET_OUT_BAD;
			break;
		}

		ip_proto = opthdr[0];
		nhoff += (opthdr[1] + 1) << 3;

		fdret = FLOW_DISSECT_RET_IPPROTO_AGAIN;
		break;
	}
	case NEXTHDR_FRAGMENT: {
		struct frag_hdr _fh, *fh;

		if (proto != htons(ETH_P_IPV6))
			break;

		fh = __skb_header_pointer(skb, nhoff, sizeof(_fh),
					  data, hlen, &_fh);

		if (!fh) {
			fdret = FLOW_DISSECT_RET_OUT_BAD;
			break;
		}

		key_control->flags |= FLOW_DIS_IS_FRAGMENT;

		nhoff += sizeof(_fh);
		ip_proto = fh->nexthdr;

		if (!(fh->frag_off & htons(IP6_OFFSET))) {
			key_control->flags |= FLOW_DIS_FIRST_FRAG;
			if (flags & FLOW_DISSECTOR_F_PARSE_1ST_FRAG) {
				fdret = FLOW_DISSECT_RET_IPPROTO_AGAIN;
				break;
			}
		}

		fdret = FLOW_DISSECT_RET_OUT_GOOD;
		break;
	}
	case IPPROTO_IPIP:
		proto = htons(ETH_P_IP);

		key_control->flags |= FLOW_DIS_ENCAPSULATION;
		if (flags & FLOW_DISSECTOR_F_STOP_AT_ENCAP) {
			fdret = FLOW_DISSECT_RET_OUT_GOOD;
			break;
		}

		fdret = FLOW_DISSECT_RET_PROTO_AGAIN;
		break;

	case IPPROTO_IPV6:
		proto = htons(ETH_P_IPV6);

		key_control->flags |= FLOW_DIS_ENCAPSULATION;
		if (flags & FLOW_DISSECTOR_F_STOP_AT_ENCAP) {
			fdret = FLOW_DISSECT_RET_OUT_GOOD;
			break;
		}

		fdret = FLOW_DISSECT_RET_PROTO_AGAIN;
		break;


	case IPPROTO_MPLS:
		proto = htons(ETH_P_MPLS_UC);
		fdret = FLOW_DISSECT_RET_PROTO_AGAIN;
		break;

	case IPPROTO_TCP:
		__skb_flow_dissect_tcp(skb, flow_dissector, target_container,
				       data, nhoff, hlen);
		break;

	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
		__skb_flow_dissect_icmp(skb, flow_dissector, target_container,
					data, nhoff, hlen);
		break;

	default:
		break;
	}

	if (!(key_control->flags & FLOW_DIS_IS_FRAGMENT))
		__skb_flow_dissect_ports(skb, flow_dissector, target_container,
					 data, nhoff, ip_proto, hlen);

	/* Process result of IP proto processing */
	switch (fdret) {
	case FLOW_DISSECT_RET_PROTO_AGAIN:
		if (skb_flow_dissect_allowed(&num_hdrs))
			goto proto_again;
		break;
	case FLOW_DISSECT_RET_IPPROTO_AGAIN:
		if (skb_flow_dissect_allowed(&num_hdrs))
			goto ip_proto_again;
		break;
	case FLOW_DISSECT_RET_OUT_GOOD:
	case FLOW_DISSECT_RET_CONTINUE:
		break;
	case FLOW_DISSECT_RET_OUT_BAD:
	default:
		goto out_bad;
	}

out_good:
	ret = true;

out:
	key_control->thoff = min_t(u16, nhoff, skb ? skb->len : hlen);
	key_basic->n_proto = proto;
	key_basic->ip_proto = ip_proto;

	return ret;

out_bad:
	ret = false;
	goto out;
}
EXPORT_SYMBOL(__skb_flow_dissect);

static siphash_key_t hashrnd __read_mostly;
static __always_inline void __flow_hash_secret_init(void)
{
	net_get_random_once(&hashrnd, sizeof(hashrnd));
}

static const void *flow_keys_hash_start(const struct flow_keys *flow)
{
	BUILD_BUG_ON(FLOW_KEYS_HASH_OFFSET % SIPHASH_ALIGNMENT);
	return &flow->FLOW_KEYS_HASH_START_FIELD;
}

static inline size_t flow_keys_hash_length(const struct flow_keys *flow)
{
	size_t diff = FLOW_KEYS_HASH_OFFSET + sizeof(flow->addrs);

	BUILD_BUG_ON((sizeof(*flow) - FLOW_KEYS_HASH_OFFSET) % sizeof(u32));

	switch (flow->control.addr_type) {
	case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
		diff -= sizeof(flow->addrs.v4addrs);
		break;
	case FLOW_DISSECTOR_KEY_IPV6_ADDRS:
		diff -= sizeof(flow->addrs.v6addrs);
		break;
	case FLOW_DISSECTOR_KEY_TIPC:
		diff -= sizeof(flow->addrs.tipckey);
		break;
	}
	return sizeof(*flow) - diff;
}

__be32 flow_get_u32_src(const struct flow_keys *flow)
{
	switch (flow->control.addr_type) {
	case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
		return flow->addrs.v4addrs.src;
	case FLOW_DISSECTOR_KEY_IPV6_ADDRS:
		return (__force __be32)ipv6_addr_hash(
			&flow->addrs.v6addrs.src);
	case FLOW_DISSECTOR_KEY_TIPC:
		return flow->addrs.tipckey.key;
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

/* Sort the source and destination IP (and the ports if the IP are the same),
 * to have consistent hash within the two directions
 */
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

static inline u32 __flow_hash_from_keys(struct flow_keys *keys,
					const siphash_key_t *keyval)
{
	u32 hash;

	__flow_hash_consistentify(keys);

	hash = siphash(flow_keys_hash_start(keys),
		       flow_keys_hash_length(keys), keyval);
	if (!hash)
		hash = 1;

	return hash;
}

u32 flow_hash_from_keys(struct flow_keys *keys)
{
	__flow_hash_secret_init();
	return __flow_hash_from_keys(keys, &hashrnd);
}
EXPORT_SYMBOL(flow_hash_from_keys);

static inline u32 ___skb_get_hash(const struct sk_buff *skb,
				  struct flow_keys *keys,
				  const siphash_key_t *keyval)
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

u32 __skb_get_hash_symmetric(const struct sk_buff *skb)
{
	struct flow_keys keys;

	__flow_hash_secret_init();

	memset(&keys, 0, sizeof(keys));
	__skb_flow_dissect(NULL, skb, &flow_keys_dissector_symmetric,
			   &keys, NULL, 0, 0, 0,
			   FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL);

	return __flow_hash_from_keys(&keys, &hashrnd);
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
	u32 hash;

	__flow_hash_secret_init();

	hash = ___skb_get_hash(skb, &keys, &hashrnd);

	__skb_set_sw_hash(skb, hash, flow_keys_have_l4(&keys));
}
EXPORT_SYMBOL(__skb_get_hash);

__u32 skb_get_hash_perturb(const struct sk_buff *skb,
			   const siphash_key_t *perturb)
{
	struct flow_keys keys;

	return ___skb_get_hash(skb, &keys, perturb);
}
EXPORT_SYMBOL(skb_get_hash_perturb);

u32 __skb_get_poff(const struct sk_buff *skb, void *data,
		   const struct flow_keys_basic *keys, int hlen)
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
	struct flow_keys_basic keys;

	if (!skb_flow_dissect_flow_keys_basic(NULL, skb, &keys,
					      NULL, 0, 0, 0, 0))
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
	keys->tags.flow_label = (__force u32)flowi6_get_flowlabel(fl6);
	keys->basic.ip_proto = fl6->flowi6_proto;

	return flow_hash_from_keys(keys);
}
EXPORT_SYMBOL(__get_hash_from_flowi6);

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
		.key_id = FLOW_DISSECTOR_KEY_TIPC,
		.offset = offsetof(struct flow_keys, addrs.tipckey),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_PORTS,
		.offset = offsetof(struct flow_keys, ports),
	},
	{
		.key_id = FLOW_DISSECTOR_KEY_VLAN,
		.offset = offsetof(struct flow_keys, vlan),
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

static const struct flow_dissector_key flow_keys_basic_dissector_keys[] = {
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

struct flow_dissector flow_keys_basic_dissector __read_mostly;
EXPORT_SYMBOL(flow_keys_basic_dissector);

static int __init init_default_flow_dissectors(void)
{
	skb_flow_dissector_init(&flow_keys_dissector,
				flow_keys_dissector_keys,
				ARRAY_SIZE(flow_keys_dissector_keys));
	skb_flow_dissector_init(&flow_keys_dissector_symmetric,
				flow_keys_dissector_symmetric_keys,
				ARRAY_SIZE(flow_keys_dissector_symmetric_keys));
	skb_flow_dissector_init(&flow_keys_basic_dissector,
				flow_keys_basic_dissector_keys,
				ARRAY_SIZE(flow_keys_basic_dissector_keys));
	return 0;
}
core_initcall(init_default_flow_dissectors);
