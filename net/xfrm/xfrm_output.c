// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * xfrm_output.c - Common IPsec encapsulation code.
 *
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <net/dst.h>
#include <net/icmp.h>
#include <net/inet_ecn.h>
#include <net/xfrm.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <net/ip6_route.h>
#include <net/ipv6_stubs.h>
#endif

#include "xfrm_inout.h"

static int xfrm_output2(struct net *net, struct sock *sk, struct sk_buff *skb);
static int xfrm_inner_extract_output(struct xfrm_state *x, struct sk_buff *skb);

static int xfrm_skb_check_space(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	int nhead = dst->header_len + LL_RESERVED_SPACE(dst->dev)
		- skb_headroom(skb);
	int ntail = dst->dev->needed_tailroom - skb_tailroom(skb);

	if (nhead <= 0) {
		if (ntail <= 0)
			return 0;
		nhead = 0;
	} else if (ntail < 0)
		ntail = 0;

	return pskb_expand_head(skb, nhead, ntail, GFP_ATOMIC);
}

/* Children define the path of the packet through the
 * Linux networking.  Thus, destinations are stackable.
 */

static struct dst_entry *skb_dst_pop(struct sk_buff *skb)
{
	struct dst_entry *child = dst_clone(xfrm_dst_child(skb_dst(skb)));

	skb_dst_drop(skb);
	return child;
}

/* Add encapsulation header.
 *
 * The IP header will be moved forward to make space for the encapsulation
 * header.
 */
static int xfrm4_transport_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);
	int ihl = iph->ihl * 4;

	skb_set_inner_transport_header(skb, skb_transport_offset(skb));

	skb_set_network_header(skb, -x->props.header_len);
	skb->mac_header = skb->network_header +
			  offsetof(struct iphdr, protocol);
	skb->transport_header = skb->network_header + ihl;
	__skb_pull(skb, ihl);
	memmove(skb_network_header(skb), iph, ihl);
	return 0;
}

#if IS_ENABLED(CONFIG_IPV6_MIP6)
static int mip6_rthdr_offset(struct sk_buff *skb, u8 **nexthdr, int type)
{
	const unsigned char *nh = skb_network_header(skb);
	unsigned int offset = sizeof(struct ipv6hdr);
	unsigned int packet_len;
	int found_rhdr = 0;

	packet_len = skb_tail_pointer(skb) - nh;
	*nexthdr = &ipv6_hdr(skb)->nexthdr;

	while (offset <= packet_len) {
		struct ipv6_opt_hdr *exthdr;

		switch (**nexthdr) {
		case NEXTHDR_HOP:
			break;
		case NEXTHDR_ROUTING:
			if (type == IPPROTO_ROUTING && offset + 3 <= packet_len) {
				struct ipv6_rt_hdr *rt;

				rt = (struct ipv6_rt_hdr *)(nh + offset);
				if (rt->type != 0)
					return offset;
			}
			found_rhdr = 1;
			break;
		case NEXTHDR_DEST:
			/* HAO MUST NOT appear more than once.
			 * XXX: It is better to try to find by the end of
			 * XXX: packet if HAO exists.
			 */
			if (ipv6_find_tlv(skb, offset, IPV6_TLV_HAO) >= 0) {
				net_dbg_ratelimited("mip6: hao exists already, override\n");
				return offset;
			}

			if (found_rhdr)
				return offset;

			break;
		default:
			return offset;
		}

		if (offset + sizeof(struct ipv6_opt_hdr) > packet_len)
			return -EINVAL;

		exthdr = (struct ipv6_opt_hdr *)(skb_network_header(skb) +
						 offset);
		offset += ipv6_optlen(exthdr);
		if (offset > IPV6_MAXPLEN)
			return -EINVAL;
		*nexthdr = &exthdr->nexthdr;
	}

	return -EINVAL;
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static int xfrm6_hdr_offset(struct xfrm_state *x, struct sk_buff *skb, u8 **prevhdr)
{
	switch (x->type->proto) {
#if IS_ENABLED(CONFIG_IPV6_MIP6)
	case IPPROTO_DSTOPTS:
	case IPPROTO_ROUTING:
		return mip6_rthdr_offset(skb, prevhdr, x->type->proto);
#endif
	default:
		break;
	}

	return ip6_find_1stfragopt(skb, prevhdr);
}
#endif

/* Add encapsulation header.
 *
 * The IP header and mutable extension headers will be moved forward to make
 * space for the encapsulation header.
 */
static int xfrm6_transport_output(struct xfrm_state *x, struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6hdr *iph;
	u8 *prevhdr;
	int hdr_len;

	iph = ipv6_hdr(skb);
	skb_set_inner_transport_header(skb, skb_transport_offset(skb));

	hdr_len = xfrm6_hdr_offset(x, skb, &prevhdr);
	if (hdr_len < 0)
		return hdr_len;
	skb_set_mac_header(skb,
			   (prevhdr - x->props.header_len) - skb->data);
	skb_set_network_header(skb, -x->props.header_len);
	skb->transport_header = skb->network_header + hdr_len;
	__skb_pull(skb, hdr_len);
	memmove(ipv6_hdr(skb), iph, hdr_len);
	return 0;
#else
	WARN_ON_ONCE(1);
	return -EAFNOSUPPORT;
#endif
}

/* Add route optimization header space.
 *
 * The IP header and mutable extension headers will be moved forward to make
 * space for the route optimization header.
 */
static int xfrm6_ro_output(struct xfrm_state *x, struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6hdr *iph;
	u8 *prevhdr;
	int hdr_len;

	iph = ipv6_hdr(skb);

	hdr_len = xfrm6_hdr_offset(x, skb, &prevhdr);
	if (hdr_len < 0)
		return hdr_len;
	skb_set_mac_header(skb,
			   (prevhdr - x->props.header_len) - skb->data);
	skb_set_network_header(skb, -x->props.header_len);
	skb->transport_header = skb->network_header + hdr_len;
	__skb_pull(skb, hdr_len);
	memmove(ipv6_hdr(skb), iph, hdr_len);

	x->lastused = ktime_get_real_seconds();

	return 0;
#else
	WARN_ON_ONCE(1);
	return -EAFNOSUPPORT;
#endif
}

/* Add encapsulation header.
 *
 * The top IP header will be constructed per draft-nikander-esp-beet-mode-06.txt.
 */
static int xfrm4_beet_encap_add(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ip_beet_phdr *ph;
	struct iphdr *top_iph;
	int hdrlen, optlen;

	hdrlen = 0;
	optlen = XFRM_MODE_SKB_CB(skb)->optlen;
	if (unlikely(optlen))
		hdrlen += IPV4_BEET_PHMAXLEN - (optlen & 4);

	skb_set_network_header(skb, -x->props.header_len - hdrlen +
			       (XFRM_MODE_SKB_CB(skb)->ihl - sizeof(*top_iph)));
	if (x->sel.family != AF_INET6)
		skb->network_header += IPV4_BEET_PHMAXLEN;
	skb->mac_header = skb->network_header +
			  offsetof(struct iphdr, protocol);
	skb->transport_header = skb->network_header + sizeof(*top_iph);

	xfrm4_beet_make_header(skb);

	ph = __skb_pull(skb, XFRM_MODE_SKB_CB(skb)->ihl - hdrlen);

	top_iph = ip_hdr(skb);

	if (unlikely(optlen)) {
		if (WARN_ON(optlen < 0))
			return -EINVAL;

		ph->padlen = 4 - (optlen & 4);
		ph->hdrlen = optlen / 8;
		ph->nexthdr = top_iph->protocol;
		if (ph->padlen)
			memset(ph + 1, IPOPT_NOP, ph->padlen);

		top_iph->protocol = IPPROTO_BEETPH;
		top_iph->ihl = sizeof(struct iphdr) / 4;
	}

	top_iph->saddr = x->props.saddr.a4;
	top_iph->daddr = x->id.daddr.a4;

	return 0;
}

/* Add encapsulation header.
 *
 * The top IP header will be constructed per RFC 2401.
 */
static int xfrm4_tunnel_encap_add(struct xfrm_state *x, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct iphdr *top_iph;
	int flags;

	skb_set_inner_network_header(skb, skb_network_offset(skb));
	skb_set_inner_transport_header(skb, skb_transport_offset(skb));

	skb_set_network_header(skb, -x->props.header_len);
	skb->mac_header = skb->network_header +
			  offsetof(struct iphdr, protocol);
	skb->transport_header = skb->network_header + sizeof(*top_iph);
	top_iph = ip_hdr(skb);

	top_iph->ihl = 5;
	top_iph->version = 4;

	top_iph->protocol = xfrm_af2proto(skb_dst(skb)->ops->family);

	/* DS disclosing depends on XFRM_SA_XFLAG_DONT_ENCAP_DSCP */
	if (x->props.extra_flags & XFRM_SA_XFLAG_DONT_ENCAP_DSCP)
		top_iph->tos = 0;
	else
		top_iph->tos = XFRM_MODE_SKB_CB(skb)->tos;
	top_iph->tos = INET_ECN_encapsulate(top_iph->tos,
					    XFRM_MODE_SKB_CB(skb)->tos);

	flags = x->props.flags;
	if (flags & XFRM_STATE_NOECN)
		IP_ECN_clear(top_iph);

	top_iph->frag_off = (flags & XFRM_STATE_NOPMTUDISC) ?
		0 : (XFRM_MODE_SKB_CB(skb)->frag_off & htons(IP_DF));

	top_iph->ttl = ip4_dst_hoplimit(xfrm_dst_child(dst));

	top_iph->saddr = x->props.saddr.a4;
	top_iph->daddr = x->id.daddr.a4;
	ip_select_ident(dev_net(dst->dev), skb, NULL);

	return 0;
}

#if IS_ENABLED(CONFIG_IPV6)
static int xfrm6_tunnel_encap_add(struct xfrm_state *x, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct ipv6hdr *top_iph;
	int dsfield;

	skb_set_inner_network_header(skb, skb_network_offset(skb));
	skb_set_inner_transport_header(skb, skb_transport_offset(skb));

	skb_set_network_header(skb, -x->props.header_len);
	skb->mac_header = skb->network_header +
			  offsetof(struct ipv6hdr, nexthdr);
	skb->transport_header = skb->network_header + sizeof(*top_iph);
	top_iph = ipv6_hdr(skb);

	top_iph->version = 6;

	memcpy(top_iph->flow_lbl, XFRM_MODE_SKB_CB(skb)->flow_lbl,
	       sizeof(top_iph->flow_lbl));
	top_iph->nexthdr = xfrm_af2proto(skb_dst(skb)->ops->family);

	if (x->props.extra_flags & XFRM_SA_XFLAG_DONT_ENCAP_DSCP)
		dsfield = 0;
	else
		dsfield = XFRM_MODE_SKB_CB(skb)->tos;
	dsfield = INET_ECN_encapsulate(dsfield, XFRM_MODE_SKB_CB(skb)->tos);
	if (x->props.flags & XFRM_STATE_NOECN)
		dsfield &= ~INET_ECN_MASK;
	ipv6_change_dsfield(top_iph, 0, dsfield);
	top_iph->hop_limit = ip6_dst_hoplimit(xfrm_dst_child(dst));
	top_iph->saddr = *(struct in6_addr *)&x->props.saddr;
	top_iph->daddr = *(struct in6_addr *)&x->id.daddr;
	return 0;
}

static int xfrm6_beet_encap_add(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipv6hdr *top_iph;
	struct ip_beet_phdr *ph;
	int optlen, hdr_len;

	hdr_len = 0;
	optlen = XFRM_MODE_SKB_CB(skb)->optlen;
	if (unlikely(optlen))
		hdr_len += IPV4_BEET_PHMAXLEN - (optlen & 4);

	skb_set_network_header(skb, -x->props.header_len - hdr_len);
	if (x->sel.family != AF_INET6)
		skb->network_header += IPV4_BEET_PHMAXLEN;
	skb->mac_header = skb->network_header +
			  offsetof(struct ipv6hdr, nexthdr);
	skb->transport_header = skb->network_header + sizeof(*top_iph);
	ph = __skb_pull(skb, XFRM_MODE_SKB_CB(skb)->ihl - hdr_len);

	xfrm6_beet_make_header(skb);

	top_iph = ipv6_hdr(skb);
	if (unlikely(optlen)) {
		if (WARN_ON(optlen < 0))
			return -EINVAL;

		ph->padlen = 4 - (optlen & 4);
		ph->hdrlen = optlen / 8;
		ph->nexthdr = top_iph->nexthdr;
		if (ph->padlen)
			memset(ph + 1, IPOPT_NOP, ph->padlen);

		top_iph->nexthdr = IPPROTO_BEETPH;
	}

	top_iph->saddr = *(struct in6_addr *)&x->props.saddr;
	top_iph->daddr = *(struct in6_addr *)&x->id.daddr;
	return 0;
}
#endif

/* Add encapsulation header.
 *
 * On exit, the transport header will be set to the start of the
 * encapsulation header to be filled in by x->type->output and the mac
 * header will be set to the nextheader (protocol for IPv4) field of the
 * extension header directly preceding the encapsulation header, or in
 * its absence, that of the top IP header.
 * The value of the network header will always point to the top IP header
 * while skb->data will point to the payload.
 */
static int xfrm4_prepare_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;

	err = xfrm_inner_extract_output(x, skb);
	if (err)
		return err;

	IPCB(skb)->flags |= IPSKB_XFRM_TUNNEL_SIZE;
	skb->protocol = htons(ETH_P_IP);

	switch (x->outer_mode.encap) {
	case XFRM_MODE_BEET:
		return xfrm4_beet_encap_add(x, skb);
	case XFRM_MODE_TUNNEL:
		return xfrm4_tunnel_encap_add(x, skb);
	}

	WARN_ON_ONCE(1);
	return -EOPNOTSUPP;
}

static int xfrm6_prepare_output(struct xfrm_state *x, struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_IPV6)
	int err;

	err = xfrm_inner_extract_output(x, skb);
	if (err)
		return err;

	skb->ignore_df = 1;
	skb->protocol = htons(ETH_P_IPV6);

	switch (x->outer_mode.encap) {
	case XFRM_MODE_BEET:
		return xfrm6_beet_encap_add(x, skb);
	case XFRM_MODE_TUNNEL:
		return xfrm6_tunnel_encap_add(x, skb);
	default:
		WARN_ON_ONCE(1);
		return -EOPNOTSUPP;
	}
#endif
	WARN_ON_ONCE(1);
	return -EAFNOSUPPORT;
}

static int xfrm_outer_mode_output(struct xfrm_state *x, struct sk_buff *skb)
{
	switch (x->outer_mode.encap) {
	case XFRM_MODE_BEET:
	case XFRM_MODE_TUNNEL:
		if (x->outer_mode.family == AF_INET)
			return xfrm4_prepare_output(x, skb);
		if (x->outer_mode.family == AF_INET6)
			return xfrm6_prepare_output(x, skb);
		break;
	case XFRM_MODE_TRANSPORT:
		if (x->outer_mode.family == AF_INET)
			return xfrm4_transport_output(x, skb);
		if (x->outer_mode.family == AF_INET6)
			return xfrm6_transport_output(x, skb);
		break;
	case XFRM_MODE_ROUTEOPTIMIZATION:
		if (x->outer_mode.family == AF_INET6)
			return xfrm6_ro_output(x, skb);
		WARN_ON_ONCE(1);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return -EOPNOTSUPP;
}

#if IS_ENABLED(CONFIG_NET_PKTGEN)
int pktgen_xfrm_outer_mode_output(struct xfrm_state *x, struct sk_buff *skb)
{
	return xfrm_outer_mode_output(x, skb);
}
EXPORT_SYMBOL_GPL(pktgen_xfrm_outer_mode_output);
#endif

static int xfrm_output_one(struct sk_buff *skb, int err)
{
	struct dst_entry *dst = skb_dst(skb);
	struct xfrm_state *x = dst->xfrm;
	struct net *net = xs_net(x);

	if (err <= 0)
		goto resume;

	do {
		err = xfrm_skb_check_space(skb);
		if (err) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTERROR);
			goto error_nolock;
		}

		skb->mark = xfrm_smark_get(skb->mark, x);

		err = xfrm_outer_mode_output(x, skb);
		if (err) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTSTATEMODEERROR);
			goto error_nolock;
		}

		spin_lock_bh(&x->lock);

		if (unlikely(x->km.state != XFRM_STATE_VALID)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTSTATEINVALID);
			err = -EINVAL;
			goto error;
		}

		err = xfrm_state_check_expire(x);
		if (err) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTSTATEEXPIRED);
			goto error;
		}

		err = xfrm_replay_overflow(x, skb);
		if (err) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTSTATESEQERROR);
			goto error;
		}

		x->curlft.bytes += skb->len;
		x->curlft.packets++;

		spin_unlock_bh(&x->lock);

		skb_dst_force(skb);
		if (!skb_dst(skb)) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTERROR);
			err = -EHOSTUNREACH;
			goto error_nolock;
		}

		if (xfrm_offload(skb)) {
			x->type_offload->encap(x, skb);
		} else {
			/* Inner headers are invalid now. */
			skb->encapsulation = 0;

			err = x->type->output(x, skb);
			if (err == -EINPROGRESS)
				goto out;
		}

resume:
		if (err) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTSTATEPROTOERROR);
			goto error_nolock;
		}

		dst = skb_dst_pop(skb);
		if (!dst) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTERROR);
			err = -EHOSTUNREACH;
			goto error_nolock;
		}
		skb_dst_set(skb, dst);
		x = dst->xfrm;
	} while (x && !(x->outer_mode.flags & XFRM_MODE_FLAG_TUNNEL));

	return 0;

error:
	spin_unlock_bh(&x->lock);
error_nolock:
	kfree_skb(skb);
out:
	return err;
}

int xfrm_output_resume(struct sock *sk, struct sk_buff *skb, int err)
{
	struct net *net = xs_net(skb_dst(skb)->xfrm);

	while (likely((err = xfrm_output_one(skb, err)) == 0)) {
		nf_reset_ct(skb);

		err = skb_dst(skb)->ops->local_out(net, sk, skb);
		if (unlikely(err != 1))
			goto out;

		if (!skb_dst(skb)->xfrm)
			return dst_output(net, sk, skb);

		err = nf_hook(skb_dst(skb)->ops->family,
			      NF_INET_POST_ROUTING, net, sk, skb,
			      NULL, skb_dst(skb)->dev, xfrm_output2);
		if (unlikely(err != 1))
			goto out;
	}

	if (err == -EINPROGRESS)
		err = 0;

out:
	return err;
}
EXPORT_SYMBOL_GPL(xfrm_output_resume);

static int xfrm_output2(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	return xfrm_output_resume(sk, skb, 1);
}

static int xfrm_output_gso(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff *segs, *nskb;

	BUILD_BUG_ON(sizeof(*IPCB(skb)) > SKB_GSO_CB_OFFSET);
	BUILD_BUG_ON(sizeof(*IP6CB(skb)) > SKB_GSO_CB_OFFSET);
	segs = skb_gso_segment(skb, 0);
	kfree_skb(skb);
	if (IS_ERR(segs))
		return PTR_ERR(segs);
	if (segs == NULL)
		return -EINVAL;

	skb_list_walk_safe(segs, segs, nskb) {
		int err;

		skb_mark_not_on_list(segs);
		err = xfrm_output2(net, sk, segs);

		if (unlikely(err)) {
			kfree_skb_list(nskb);
			return err;
		}
	}

	return 0;
}

/* For partial checksum offload, the outer header checksum is calculated
 * by software and the inner header checksum is calculated by hardware.
 * This requires hardware to know the inner packet type to calculate
 * the inner header checksum. Save inner ip protocol here to avoid
 * traversing the packet in the vendor's xmit code.
 * For IPsec tunnel mode save the ip protocol from the IP header of the
 * plain text packet. Otherwise If the encap type is IPIP, just save
 * skb->inner_ipproto in any other case get the ip protocol from the IP
 * header.
 */
static void xfrm_get_inner_ipproto(struct sk_buff *skb, struct xfrm_state *x)
{
	struct xfrm_offload *xo = xfrm_offload(skb);
	const struct ethhdr *eth;

	if (!xo)
		return;

	if (x->outer_mode.encap == XFRM_MODE_TUNNEL) {
		switch (x->outer_mode.family) {
		case AF_INET:
			xo->inner_ipproto = ip_hdr(skb)->protocol;
			break;
		case AF_INET6:
			xo->inner_ipproto = ipv6_hdr(skb)->nexthdr;
			break;
		default:
			break;
		}

		return;
	}

	/* non-Tunnel Mode */
	if (!skb->encapsulation)
		return;

	if (skb->inner_protocol_type == ENCAP_TYPE_IPPROTO) {
		xo->inner_ipproto = skb->inner_ipproto;
		return;
	}

	if (skb->inner_protocol_type != ENCAP_TYPE_ETHER)
		return;

	eth = (struct ethhdr *)skb_inner_mac_header(skb);

	switch (ntohs(eth->h_proto)) {
	case ETH_P_IPV6:
		xo->inner_ipproto = inner_ipv6_hdr(skb)->nexthdr;
		break;
	case ETH_P_IP:
		xo->inner_ipproto = inner_ip_hdr(skb)->protocol;
		break;
	}
}

int xfrm_output(struct sock *sk, struct sk_buff *skb)
{
	struct net *net = dev_net(skb_dst(skb)->dev);
	struct xfrm_state *x = skb_dst(skb)->xfrm;
	int err;

	switch (x->outer_mode.family) {
	case AF_INET:
		memset(IPCB(skb), 0, sizeof(*IPCB(skb)));
		IPCB(skb)->flags |= IPSKB_XFRM_TRANSFORMED;
		break;
	case AF_INET6:
		memset(IP6CB(skb), 0, sizeof(*IP6CB(skb)));

		IP6CB(skb)->flags |= IP6SKB_XFRM_TRANSFORMED;
		break;
	}

	secpath_reset(skb);

	if (xfrm_dev_offload_ok(skb, x)) {
		struct sec_path *sp;

		sp = secpath_set(skb);
		if (!sp) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTERROR);
			kfree_skb(skb);
			return -ENOMEM;
		}

		sp->olen++;
		sp->xvec[sp->len++] = x;
		xfrm_state_hold(x);

		xfrm_get_inner_ipproto(skb, x);
		skb->encapsulation = 1;

		if (skb_is_gso(skb)) {
			if (skb->inner_protocol)
				return xfrm_output_gso(net, sk, skb);

			skb_shinfo(skb)->gso_type |= SKB_GSO_ESP;
			goto out;
		}

		if (x->xso.dev && x->xso.dev->features & NETIF_F_HW_ESP_TX_CSUM)
			goto out;
	} else {
		if (skb_is_gso(skb))
			return xfrm_output_gso(net, sk, skb);
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		err = skb_checksum_help(skb);
		if (err) {
			XFRM_INC_STATS(net, LINUX_MIB_XFRMOUTERROR);
			kfree_skb(skb);
			return err;
		}
	}

out:
	return xfrm_output2(net, sk, skb);
}
EXPORT_SYMBOL_GPL(xfrm_output);

static int xfrm4_tunnel_check_size(struct sk_buff *skb)
{
	int mtu, ret = 0;

	if (IPCB(skb)->flags & IPSKB_XFRM_TUNNEL_SIZE)
		goto out;

	if (!(ip_hdr(skb)->frag_off & htons(IP_DF)) || skb->ignore_df)
		goto out;

	mtu = dst_mtu(skb_dst(skb));
	if ((!skb_is_gso(skb) && skb->len > mtu) ||
	    (skb_is_gso(skb) &&
	     !skb_gso_validate_network_len(skb, ip_skb_dst_mtu(skb->sk, skb)))) {
		skb->protocol = htons(ETH_P_IP);

		if (skb->sk)
			xfrm_local_error(skb, mtu);
		else
			icmp_send(skb, ICMP_DEST_UNREACH,
				  ICMP_FRAG_NEEDED, htonl(mtu));
		ret = -EMSGSIZE;
	}
out:
	return ret;
}

static int xfrm4_extract_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;

	if (x->outer_mode.encap == XFRM_MODE_BEET &&
	    ip_is_fragment(ip_hdr(skb))) {
		net_warn_ratelimited("BEET mode doesn't support inner IPv4 fragments\n");
		return -EAFNOSUPPORT;
	}

	err = xfrm4_tunnel_check_size(skb);
	if (err)
		return err;

	XFRM_MODE_SKB_CB(skb)->protocol = ip_hdr(skb)->protocol;

	xfrm4_extract_header(skb);
	return 0;
}

#if IS_ENABLED(CONFIG_IPV6)
static int xfrm6_tunnel_check_size(struct sk_buff *skb)
{
	int mtu, ret = 0;
	struct dst_entry *dst = skb_dst(skb);

	if (skb->ignore_df)
		goto out;

	mtu = dst_mtu(dst);
	if (mtu < IPV6_MIN_MTU)
		mtu = IPV6_MIN_MTU;

	if ((!skb_is_gso(skb) && skb->len > mtu) ||
	    (skb_is_gso(skb) &&
	     !skb_gso_validate_network_len(skb, ip6_skb_dst_mtu(skb)))) {
		skb->dev = dst->dev;
		skb->protocol = htons(ETH_P_IPV6);

		if (xfrm6_local_dontfrag(skb->sk))
			ipv6_stub->xfrm6_local_rxpmtu(skb, mtu);
		else if (skb->sk)
			xfrm_local_error(skb, mtu);
		else
			icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		ret = -EMSGSIZE;
	}
out:
	return ret;
}
#endif

static int xfrm6_extract_output(struct xfrm_state *x, struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_IPV6)
	int err;

	err = xfrm6_tunnel_check_size(skb);
	if (err)
		return err;

	XFRM_MODE_SKB_CB(skb)->protocol = ipv6_hdr(skb)->nexthdr;

	xfrm6_extract_header(skb);
	return 0;
#else
	WARN_ON_ONCE(1);
	return -EAFNOSUPPORT;
#endif
}

static int xfrm_inner_extract_output(struct xfrm_state *x, struct sk_buff *skb)
{
	const struct xfrm_mode *inner_mode;

	if (x->sel.family == AF_UNSPEC)
		inner_mode = xfrm_ip2inner_mode(x,
				xfrm_af2proto(skb_dst(skb)->ops->family));
	else
		inner_mode = &x->inner_mode;

	if (inner_mode == NULL)
		return -EAFNOSUPPORT;

	switch (inner_mode->family) {
	case AF_INET:
		return xfrm4_extract_output(x, skb);
	case AF_INET6:
		return xfrm6_extract_output(x, skb);
	}

	return -EAFNOSUPPORT;
}

void xfrm_local_error(struct sk_buff *skb, int mtu)
{
	unsigned int proto;
	struct xfrm_state_afinfo *afinfo;

	if (skb->protocol == htons(ETH_P_IP))
		proto = AF_INET;
	else if (skb->protocol == htons(ETH_P_IPV6) &&
		 skb->sk->sk_family == AF_INET6)
		proto = AF_INET6;
	else
		return;

	afinfo = xfrm_state_get_afinfo(proto);
	if (afinfo) {
		afinfo->local_error(skb, mtu);
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL_GPL(xfrm_local_error);
