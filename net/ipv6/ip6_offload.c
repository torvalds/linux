// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IPV6 GSO/GRO offload support
 *	Linux INET6 implementation
 */

#include <linux/kernel.h>
#include <linux/socket.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/printk.h>

#include <net/protocol.h>
#include <net/ipv6.h>
#include <net/inet_common.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/gro.h>

#include "ip6_offload.h"

/* All GRO functions are always builtin, except UDP over ipv6, which lays in
 * ipv6 module, as it depends on UDPv6 lookup function, so we need special care
 * when ipv6 is built as a module
 */
#if IS_BUILTIN(CONFIG_IPV6)
#define INDIRECT_CALL_L4(f, f2, f1, ...) INDIRECT_CALL_2(f, f2, f1, __VA_ARGS__)
#else
#define INDIRECT_CALL_L4(f, f2, f1, ...) INDIRECT_CALL_1(f, f2, __VA_ARGS__)
#endif

#define indirect_call_gro_receive_l4(f2, f1, cb, head, skb)	\
({								\
	unlikely(gro_recursion_inc_test(skb)) ?			\
		NAPI_GRO_CB(skb)->flush |= 1, NULL :		\
		INDIRECT_CALL_L4(cb, f2, f1, head, skb);	\
})

static int ipv6_gso_pull_exthdrs(struct sk_buff *skb, int proto)
{
	const struct net_offload *ops = NULL;

	for (;;) {
		struct ipv6_opt_hdr *opth;
		int len;

		if (proto != NEXTHDR_HOP) {
			ops = rcu_dereference(inet6_offloads[proto]);

			if (unlikely(!ops))
				break;

			if (!(ops->flags & INET6_PROTO_GSO_EXTHDR))
				break;
		}

		if (unlikely(!pskb_may_pull(skb, 8)))
			break;

		opth = (void *)skb->data;
		len = ipv6_optlen(opth);

		if (unlikely(!pskb_may_pull(skb, len)))
			break;

		opth = (void *)skb->data;
		proto = opth->nexthdr;
		__skb_pull(skb, len);
	}

	return proto;
}

static struct sk_buff *ipv6_gso_segment(struct sk_buff *skb,
	netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EINVAL);
	struct ipv6hdr *ipv6h;
	const struct net_offload *ops;
	int proto, nexthdr;
	struct frag_hdr *fptr;
	unsigned int payload_len;
	u8 *prevhdr;
	int offset = 0;
	bool encap, udpfrag;
	int nhoff;
	bool gso_partial;

	skb_reset_network_header(skb);
	nexthdr = ipv6_has_hopopt_jumbo(skb);
	if (nexthdr) {
		const int hophdr_len = sizeof(struct hop_jumbo_hdr);
		int err;

		err = skb_cow_head(skb, 0);
		if (err < 0)
			return ERR_PTR(err);

		/* remove the HBH header.
		 * Layout: [Ethernet header][IPv6 header][HBH][TCP header]
		 */
		memmove(skb_mac_header(skb) + hophdr_len,
			skb_mac_header(skb),
			ETH_HLEN + sizeof(struct ipv6hdr));
		skb->data += hophdr_len;
		skb->len -= hophdr_len;
		skb->network_header += hophdr_len;
		skb->mac_header += hophdr_len;
		ipv6h = (struct ipv6hdr *)skb->data;
		ipv6h->nexthdr = nexthdr;
	}
	nhoff = skb_network_header(skb) - skb_mac_header(skb);
	if (unlikely(!pskb_may_pull(skb, sizeof(*ipv6h))))
		goto out;

	encap = SKB_GSO_CB(skb)->encap_level > 0;
	if (encap)
		features &= skb->dev->hw_enc_features;
	SKB_GSO_CB(skb)->encap_level += sizeof(*ipv6h);

	ipv6h = ipv6_hdr(skb);
	__skb_pull(skb, sizeof(*ipv6h));
	segs = ERR_PTR(-EPROTONOSUPPORT);

	proto = ipv6_gso_pull_exthdrs(skb, ipv6h->nexthdr);

	if (skb->encapsulation &&
	    skb_shinfo(skb)->gso_type & (SKB_GSO_IPXIP4 | SKB_GSO_IPXIP6))
		udpfrag = proto == IPPROTO_UDP && encap &&
			  (skb_shinfo(skb)->gso_type & SKB_GSO_UDP);
	else
		udpfrag = proto == IPPROTO_UDP && !skb->encapsulation &&
			  (skb_shinfo(skb)->gso_type & SKB_GSO_UDP);

	ops = rcu_dereference(inet6_offloads[proto]);
	if (likely(ops && ops->callbacks.gso_segment)) {
		skb_reset_transport_header(skb);
		segs = ops->callbacks.gso_segment(skb, features);
		if (!segs)
			skb->network_header = skb_mac_header(skb) + nhoff - skb->head;
	}

	if (IS_ERR_OR_NULL(segs))
		goto out;

	gso_partial = !!(skb_shinfo(segs)->gso_type & SKB_GSO_PARTIAL);

	for (skb = segs; skb; skb = skb->next) {
		ipv6h = (struct ipv6hdr *)(skb_mac_header(skb) + nhoff);
		if (gso_partial && skb_is_gso(skb))
			payload_len = skb_shinfo(skb)->gso_size +
				      SKB_GSO_CB(skb)->data_offset +
				      skb->head - (unsigned char *)(ipv6h + 1);
		else
			payload_len = skb->len - nhoff - sizeof(*ipv6h);
		ipv6h->payload_len = htons(payload_len);
		skb->network_header = (u8 *)ipv6h - skb->head;
		skb_reset_mac_len(skb);

		if (udpfrag) {
			int err = ip6_find_1stfragopt(skb, &prevhdr);
			if (err < 0) {
				kfree_skb_list(segs);
				return ERR_PTR(err);
			}
			fptr = (struct frag_hdr *)((u8 *)ipv6h + err);
			fptr->frag_off = htons(offset);
			if (skb->next)
				fptr->frag_off |= htons(IP6_MF);
			offset += (ntohs(ipv6h->payload_len) -
				   sizeof(struct frag_hdr));
		}
		if (encap)
			skb_reset_inner_headers(skb);
	}

out:
	return segs;
}

/* Return the total length of all the extension hdrs, following the same
 * logic in ipv6_gso_pull_exthdrs() when parsing ext-hdrs.
 */
static int ipv6_exthdrs_len(struct ipv6hdr *iph,
			    const struct net_offload **opps)
{
	struct ipv6_opt_hdr *opth = (void *)iph;
	int len = 0, proto, optlen = sizeof(*iph);

	proto = iph->nexthdr;
	for (;;) {
		if (proto != NEXTHDR_HOP) {
			*opps = rcu_dereference(inet6_offloads[proto]);
			if (unlikely(!(*opps)))
				break;
			if (!((*opps)->flags & INET6_PROTO_GSO_EXTHDR))
				break;
		}
		opth = (void *)opth + optlen;
		optlen = ipv6_optlen(opth);
		len += optlen;
		proto = opth->nexthdr;
	}
	return len;
}

INDIRECT_CALLABLE_SCOPE struct sk_buff *ipv6_gro_receive(struct list_head *head,
							 struct sk_buff *skb)
{
	const struct net_offload *ops;
	struct sk_buff *pp = NULL;
	struct sk_buff *p;
	struct ipv6hdr *iph;
	unsigned int nlen;
	unsigned int hlen;
	unsigned int off;
	u16 flush = 1;
	int proto;

	off = skb_gro_offset(skb);
	hlen = off + sizeof(*iph);
	iph = skb_gro_header(skb, hlen, off);
	if (unlikely(!iph))
		goto out;

	skb_set_network_header(skb, off);
	skb_gro_pull(skb, sizeof(*iph));
	skb_set_transport_header(skb, skb_gro_offset(skb));

	flush += ntohs(iph->payload_len) != skb_gro_len(skb);

	proto = iph->nexthdr;
	ops = rcu_dereference(inet6_offloads[proto]);
	if (!ops || !ops->callbacks.gro_receive) {
		pskb_pull(skb, skb_gro_offset(skb));
		skb_gro_frag0_invalidate(skb);
		proto = ipv6_gso_pull_exthdrs(skb, proto);
		skb_gro_pull(skb, -skb_transport_offset(skb));
		skb_reset_transport_header(skb);
		__skb_push(skb, skb_gro_offset(skb));

		ops = rcu_dereference(inet6_offloads[proto]);
		if (!ops || !ops->callbacks.gro_receive)
			goto out;

		iph = ipv6_hdr(skb);
	}

	NAPI_GRO_CB(skb)->proto = proto;

	flush--;
	nlen = skb_network_header_len(skb);

	list_for_each_entry(p, head, list) {
		const struct ipv6hdr *iph2;
		__be32 first_word; /* <Version:4><Traffic_Class:8><Flow_Label:20> */

		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		iph2 = (struct ipv6hdr *)(p->data + off);
		first_word = *(__be32 *)iph ^ *(__be32 *)iph2;

		/* All fields must match except length and Traffic Class.
		 * XXX skbs on the gro_list have all been parsed and pulled
		 * already so we don't need to compare nlen
		 * (nlen != (sizeof(*iph2) + ipv6_exthdrs_len(iph2, &ops)))
		 * memcmp() alone below is sufficient, right?
		 */
		 if ((first_word & htonl(0xF00FFFFF)) ||
		     !ipv6_addr_equal(&iph->saddr, &iph2->saddr) ||
		     !ipv6_addr_equal(&iph->daddr, &iph2->daddr) ||
		     iph->nexthdr != iph2->nexthdr) {
not_same_flow:
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}
		if (unlikely(nlen > sizeof(struct ipv6hdr))) {
			if (memcmp(iph + 1, iph2 + 1,
				   nlen - sizeof(struct ipv6hdr)))
				goto not_same_flow;
		}
		/* flush if Traffic Class fields are different */
		NAPI_GRO_CB(p)->flush |= !!((first_word & htonl(0x0FF00000)) |
			(__force __be32)(iph->hop_limit ^ iph2->hop_limit));
		NAPI_GRO_CB(p)->flush |= flush;

		/* If the previous IP ID value was based on an atomic
		 * datagram we can overwrite the value and ignore it.
		 */
		if (NAPI_GRO_CB(skb)->is_atomic)
			NAPI_GRO_CB(p)->flush_id = 0;
	}

	NAPI_GRO_CB(skb)->is_atomic = true;
	NAPI_GRO_CB(skb)->flush |= flush;

	skb_gro_postpull_rcsum(skb, iph, nlen);

	pp = indirect_call_gro_receive_l4(tcp6_gro_receive, udp6_gro_receive,
					 ops->callbacks.gro_receive, head, skb);

out:
	skb_gro_flush_final(skb, pp, flush);

	return pp;
}

static struct sk_buff *sit_ip6ip6_gro_receive(struct list_head *head,
					      struct sk_buff *skb)
{
	/* Common GRO receive for SIT and IP6IP6 */

	if (NAPI_GRO_CB(skb)->encap_mark) {
		NAPI_GRO_CB(skb)->flush = 1;
		return NULL;
	}

	NAPI_GRO_CB(skb)->encap_mark = 1;

	return ipv6_gro_receive(head, skb);
}

static struct sk_buff *ip4ip6_gro_receive(struct list_head *head,
					  struct sk_buff *skb)
{
	/* Common GRO receive for SIT and IP6IP6 */

	if (NAPI_GRO_CB(skb)->encap_mark) {
		NAPI_GRO_CB(skb)->flush = 1;
		return NULL;
	}

	NAPI_GRO_CB(skb)->encap_mark = 1;

	return inet_gro_receive(head, skb);
}

INDIRECT_CALLABLE_SCOPE int ipv6_gro_complete(struct sk_buff *skb, int nhoff)
{
	const struct net_offload *ops;
	struct ipv6hdr *iph;
	int err = -ENOSYS;
	u32 payload_len;

	if (skb->encapsulation) {
		skb_set_inner_protocol(skb, cpu_to_be16(ETH_P_IPV6));
		skb_set_inner_network_header(skb, nhoff);
	}

	payload_len = skb->len - nhoff - sizeof(*iph);
	if (unlikely(payload_len > IPV6_MAXPLEN)) {
		struct hop_jumbo_hdr *hop_jumbo;
		int hoplen = sizeof(*hop_jumbo);

		/* Move network header left */
		memmove(skb_mac_header(skb) - hoplen, skb_mac_header(skb),
			skb->transport_header - skb->mac_header);
		skb->data -= hoplen;
		skb->len += hoplen;
		skb->mac_header -= hoplen;
		skb->network_header -= hoplen;
		iph = (struct ipv6hdr *)(skb->data + nhoff);
		hop_jumbo = (struct hop_jumbo_hdr *)(iph + 1);

		/* Build hop-by-hop options */
		hop_jumbo->nexthdr = iph->nexthdr;
		hop_jumbo->hdrlen = 0;
		hop_jumbo->tlv_type = IPV6_TLV_JUMBO;
		hop_jumbo->tlv_len = 4;
		hop_jumbo->jumbo_payload_len = htonl(payload_len + hoplen);

		iph->nexthdr = NEXTHDR_HOP;
		iph->payload_len = 0;
	} else {
		iph = (struct ipv6hdr *)(skb->data + nhoff);
		iph->payload_len = htons(payload_len);
	}

	nhoff += sizeof(*iph) + ipv6_exthdrs_len(iph, &ops);
	if (WARN_ON(!ops || !ops->callbacks.gro_complete))
		goto out;

	err = INDIRECT_CALL_L4(ops->callbacks.gro_complete, tcp6_gro_complete,
			       udp6_gro_complete, skb, nhoff);

out:
	return err;
}

static int sit_gro_complete(struct sk_buff *skb, int nhoff)
{
	skb->encapsulation = 1;
	skb_shinfo(skb)->gso_type |= SKB_GSO_IPXIP4;
	return ipv6_gro_complete(skb, nhoff);
}

static int ip6ip6_gro_complete(struct sk_buff *skb, int nhoff)
{
	skb->encapsulation = 1;
	skb_shinfo(skb)->gso_type |= SKB_GSO_IPXIP6;
	return ipv6_gro_complete(skb, nhoff);
}

static int ip4ip6_gro_complete(struct sk_buff *skb, int nhoff)
{
	skb->encapsulation = 1;
	skb_shinfo(skb)->gso_type |= SKB_GSO_IPXIP6;
	return inet_gro_complete(skb, nhoff);
}

static struct packet_offload ipv6_packet_offload __read_mostly = {
	.type = cpu_to_be16(ETH_P_IPV6),
	.callbacks = {
		.gso_segment = ipv6_gso_segment,
		.gro_receive = ipv6_gro_receive,
		.gro_complete = ipv6_gro_complete,
	},
};

static struct sk_buff *sit_gso_segment(struct sk_buff *skb,
				       netdev_features_t features)
{
	if (!(skb_shinfo(skb)->gso_type & SKB_GSO_IPXIP4))
		return ERR_PTR(-EINVAL);

	return ipv6_gso_segment(skb, features);
}

static struct sk_buff *ip4ip6_gso_segment(struct sk_buff *skb,
					  netdev_features_t features)
{
	if (!(skb_shinfo(skb)->gso_type & SKB_GSO_IPXIP6))
		return ERR_PTR(-EINVAL);

	return inet_gso_segment(skb, features);
}

static struct sk_buff *ip6ip6_gso_segment(struct sk_buff *skb,
					  netdev_features_t features)
{
	if (!(skb_shinfo(skb)->gso_type & SKB_GSO_IPXIP6))
		return ERR_PTR(-EINVAL);

	return ipv6_gso_segment(skb, features);
}

static const struct net_offload sit_offload = {
	.callbacks = {
		.gso_segment	= sit_gso_segment,
		.gro_receive    = sit_ip6ip6_gro_receive,
		.gro_complete   = sit_gro_complete,
	},
};

static const struct net_offload ip4ip6_offload = {
	.callbacks = {
		.gso_segment	= ip4ip6_gso_segment,
		.gro_receive    = ip4ip6_gro_receive,
		.gro_complete   = ip4ip6_gro_complete,
	},
};

static const struct net_offload ip6ip6_offload = {
	.callbacks = {
		.gso_segment	= ip6ip6_gso_segment,
		.gro_receive    = sit_ip6ip6_gro_receive,
		.gro_complete   = ip6ip6_gro_complete,
	},
};
static int __init ipv6_offload_init(void)
{

	if (tcpv6_offload_init() < 0)
		pr_crit("%s: Cannot add TCP protocol offload\n", __func__);
	if (ipv6_exthdrs_offload_init() < 0)
		pr_crit("%s: Cannot add EXTHDRS protocol offload\n", __func__);

	dev_add_offload(&ipv6_packet_offload);

	inet_add_offload(&sit_offload, IPPROTO_IPV6);
	inet6_add_offload(&ip6ip6_offload, IPPROTO_IPV6);
	inet6_add_offload(&ip4ip6_offload, IPPROTO_IPIP);

	return 0;
}

fs_initcall(ipv6_offload_init);
