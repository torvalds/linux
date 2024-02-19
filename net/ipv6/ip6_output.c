// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IPv6 output functions
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	Based on linux/net/ipv4/ip_output.c
 *
 *	Changes:
 *	A.N.Kuznetsov	:	airthmetics in fragmentation.
 *				extension headers are implemented.
 *				route changes now work.
 *				ip6_forward does not confuse sniffers.
 *				etc.
 *
 *      H. von Brand    :       Added missing #include <linux/string.h>
 *	Imran Patel	:	frag id should be in NBO
 *      Kazunori MIYAZAWA @USAGI
 *			:       add ip6_append_data and related functions
 *				for datagram xmit
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <linux/tcp.h>
#include <linux/route.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/bpf-cgroup.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/gso.h>
#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/rawv6.h>
#include <net/icmp.h>
#include <net/xfrm.h>
#include <net/checksum.h>
#include <linux/mroute6.h>
#include <net/l3mdev.h>
#include <net/lwtunnel.h>
#include <net/ip_tunnels.h>

static int ip6_finish_output2(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct net_device *dev = dst->dev;
	struct inet6_dev *idev = ip6_dst_idev(dst);
	unsigned int hh_len = LL_RESERVED_SPACE(dev);
	const struct in6_addr *daddr, *nexthop;
	struct ipv6hdr *hdr;
	struct neighbour *neigh;
	int ret;

	/* Be paranoid, rather than too clever. */
	if (unlikely(hh_len > skb_headroom(skb)) && dev->header_ops) {
		skb = skb_expand_head(skb, hh_len);
		if (!skb) {
			IP6_INC_STATS(net, idev, IPSTATS_MIB_OUTDISCARDS);
			return -ENOMEM;
		}
	}

	hdr = ipv6_hdr(skb);
	daddr = &hdr->daddr;
	if (ipv6_addr_is_multicast(daddr)) {
		if (!(dev->flags & IFF_LOOPBACK) && sk_mc_loop(sk) &&
		    ((mroute6_is_socket(net, skb) &&
		     !(IP6CB(skb)->flags & IP6SKB_FORWARDED)) ||
		     ipv6_chk_mcast_addr(dev, daddr, &hdr->saddr))) {
			struct sk_buff *newskb = skb_clone(skb, GFP_ATOMIC);

			/* Do not check for IFF_ALLMULTI; multicast routing
			   is not supported in any case.
			 */
			if (newskb)
				NF_HOOK(NFPROTO_IPV6, NF_INET_POST_ROUTING,
					net, sk, newskb, NULL, newskb->dev,
					dev_loopback_xmit);

			if (hdr->hop_limit == 0) {
				IP6_INC_STATS(net, idev,
					      IPSTATS_MIB_OUTDISCARDS);
				kfree_skb(skb);
				return 0;
			}
		}

		IP6_UPD_PO_STATS(net, idev, IPSTATS_MIB_OUTMCAST, skb->len);
		if (IPV6_ADDR_MC_SCOPE(daddr) <= IPV6_ADDR_SCOPE_NODELOCAL &&
		    !(dev->flags & IFF_LOOPBACK)) {
			kfree_skb(skb);
			return 0;
		}
	}

	if (lwtunnel_xmit_redirect(dst->lwtstate)) {
		int res = lwtunnel_xmit(skb);

		if (res != LWTUNNEL_XMIT_CONTINUE)
			return res;
	}

	IP6_UPD_PO_STATS(net, idev, IPSTATS_MIB_OUT, skb->len);

	rcu_read_lock();
	nexthop = rt6_nexthop((struct rt6_info *)dst, daddr);
	neigh = __ipv6_neigh_lookup_noref(dev, nexthop);

	if (unlikely(IS_ERR_OR_NULL(neigh))) {
		if (unlikely(!neigh))
			neigh = __neigh_create(&nd_tbl, nexthop, dev, false);
		if (IS_ERR(neigh)) {
			rcu_read_unlock();
			IP6_INC_STATS(net, idev, IPSTATS_MIB_OUTNOROUTES);
			kfree_skb_reason(skb, SKB_DROP_REASON_NEIGH_CREATEFAIL);
			return -EINVAL;
		}
	}
	sock_confirm_neigh(skb, neigh);
	ret = neigh_output(neigh, skb, false);
	rcu_read_unlock();
	return ret;
}

static int
ip6_finish_output_gso_slowpath_drop(struct net *net, struct sock *sk,
				    struct sk_buff *skb, unsigned int mtu)
{
	struct sk_buff *segs, *nskb;
	netdev_features_t features;
	int ret = 0;

	/* Please see corresponding comment in ip_finish_output_gso
	 * describing the cases where GSO segment length exceeds the
	 * egress MTU.
	 */
	features = netif_skb_features(skb);
	segs = skb_gso_segment(skb, features & ~NETIF_F_GSO_MASK);
	if (IS_ERR_OR_NULL(segs)) {
		kfree_skb(skb);
		return -ENOMEM;
	}

	consume_skb(skb);

	skb_list_walk_safe(segs, segs, nskb) {
		int err;

		skb_mark_not_on_list(segs);
		/* Last GSO segment can be smaller than gso_size (and MTU).
		 * Adding a fragment header would produce an "atomic fragment",
		 * which is considered harmful (RFC-8021). Avoid that.
		 */
		err = segs->len > mtu ?
			ip6_fragment(net, sk, segs, ip6_finish_output2) :
			ip6_finish_output2(net, sk, segs);
		if (err && ret == 0)
			ret = err;
	}

	return ret;
}

static int ip6_finish_output_gso(struct net *net, struct sock *sk,
				 struct sk_buff *skb, unsigned int mtu)
{
	if (!(IP6CB(skb)->flags & IP6SKB_FAKEJUMBO) &&
	    !skb_gso_validate_network_len(skb, mtu))
		return ip6_finish_output_gso_slowpath_drop(net, sk, skb, mtu);

	return ip6_finish_output2(net, sk, skb);
}

static int __ip6_finish_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	unsigned int mtu;

#if defined(CONFIG_NETFILTER) && defined(CONFIG_XFRM)
	/* Policy lookup after SNAT yielded a new policy */
	if (skb_dst(skb)->xfrm) {
		IP6CB(skb)->flags |= IP6SKB_REROUTED;
		return dst_output(net, sk, skb);
	}
#endif

	mtu = ip6_skb_dst_mtu(skb);
	if (skb_is_gso(skb))
		return ip6_finish_output_gso(net, sk, skb, mtu);

	if (skb->len > mtu ||
	    (IP6CB(skb)->frag_max_size && skb->len > IP6CB(skb)->frag_max_size))
		return ip6_fragment(net, sk, skb, ip6_finish_output2);

	return ip6_finish_output2(net, sk, skb);
}

static int ip6_finish_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	int ret;

	ret = BPF_CGROUP_RUN_PROG_INET_EGRESS(sk, skb);
	switch (ret) {
	case NET_XMIT_SUCCESS:
	case NET_XMIT_CN:
		return __ip6_finish_output(net, sk, skb) ? : ret;
	default:
		kfree_skb_reason(skb, SKB_DROP_REASON_BPF_CGROUP_EGRESS);
		return ret;
	}
}

int ip6_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct net_device *dev = skb_dst(skb)->dev, *indev = skb->dev;
	struct inet6_dev *idev = ip6_dst_idev(skb_dst(skb));

	skb->protocol = htons(ETH_P_IPV6);
	skb->dev = dev;

	if (unlikely(idev->cnf.disable_ipv6)) {
		IP6_INC_STATS(net, idev, IPSTATS_MIB_OUTDISCARDS);
		kfree_skb_reason(skb, SKB_DROP_REASON_IPV6DISABLED);
		return 0;
	}

	return NF_HOOK_COND(NFPROTO_IPV6, NF_INET_POST_ROUTING,
			    net, sk, skb, indev, dev,
			    ip6_finish_output,
			    !(IP6CB(skb)->flags & IP6SKB_REROUTED));
}
EXPORT_SYMBOL(ip6_output);

bool ip6_autoflowlabel(struct net *net, const struct sock *sk)
{
	if (!inet6_test_bit(AUTOFLOWLABEL_SET, sk))
		return ip6_default_np_autolabel(net);
	return inet6_test_bit(AUTOFLOWLABEL, sk);
}

/*
 * xmit an sk_buff (used by TCP, SCTP and DCCP)
 * Note : socket lock is not held for SYNACK packets, but might be modified
 * by calls to skb_set_owner_w() and ipv6_local_error(),
 * which are using proper atomic operations or spinlocks.
 */
int ip6_xmit(const struct sock *sk, struct sk_buff *skb, struct flowi6 *fl6,
	     __u32 mark, struct ipv6_txoptions *opt, int tclass, u32 priority)
{
	struct net *net = sock_net(sk);
	const struct ipv6_pinfo *np = inet6_sk(sk);
	struct in6_addr *first_hop = &fl6->daddr;
	struct dst_entry *dst = skb_dst(skb);
	struct net_device *dev = dst->dev;
	struct inet6_dev *idev = ip6_dst_idev(dst);
	struct hop_jumbo_hdr *hop_jumbo;
	int hoplen = sizeof(*hop_jumbo);
	unsigned int head_room;
	struct ipv6hdr *hdr;
	u8  proto = fl6->flowi6_proto;
	int seg_len = skb->len;
	int hlimit = -1;
	u32 mtu;

	head_room = sizeof(struct ipv6hdr) + hoplen + LL_RESERVED_SPACE(dev);
	if (opt)
		head_room += opt->opt_nflen + opt->opt_flen;

	if (unlikely(head_room > skb_headroom(skb))) {
		skb = skb_expand_head(skb, head_room);
		if (!skb) {
			IP6_INC_STATS(net, idev, IPSTATS_MIB_OUTDISCARDS);
			return -ENOBUFS;
		}
	}

	if (opt) {
		seg_len += opt->opt_nflen + opt->opt_flen;

		if (opt->opt_flen)
			ipv6_push_frag_opts(skb, opt, &proto);

		if (opt->opt_nflen)
			ipv6_push_nfrag_opts(skb, opt, &proto, &first_hop,
					     &fl6->saddr);
	}

	if (unlikely(seg_len > IPV6_MAXPLEN)) {
		hop_jumbo = skb_push(skb, hoplen);

		hop_jumbo->nexthdr = proto;
		hop_jumbo->hdrlen = 0;
		hop_jumbo->tlv_type = IPV6_TLV_JUMBO;
		hop_jumbo->tlv_len = 4;
		hop_jumbo->jumbo_payload_len = htonl(seg_len + hoplen);

		proto = IPPROTO_HOPOPTS;
		seg_len = 0;
		IP6CB(skb)->flags |= IP6SKB_FAKEJUMBO;
	}

	skb_push(skb, sizeof(struct ipv6hdr));
	skb_reset_network_header(skb);
	hdr = ipv6_hdr(skb);

	/*
	 *	Fill in the IPv6 header
	 */
	if (np)
		hlimit = READ_ONCE(np->hop_limit);
	if (hlimit < 0)
		hlimit = ip6_dst_hoplimit(dst);

	ip6_flow_hdr(hdr, tclass, ip6_make_flowlabel(net, skb, fl6->flowlabel,
				ip6_autoflowlabel(net, sk), fl6));

	hdr->payload_len = htons(seg_len);
	hdr->nexthdr = proto;
	hdr->hop_limit = hlimit;

	hdr->saddr = fl6->saddr;
	hdr->daddr = *first_hop;

	skb->protocol = htons(ETH_P_IPV6);
	skb->priority = priority;
	skb->mark = mark;

	mtu = dst_mtu(dst);
	if ((skb->len <= mtu) || skb->ignore_df || skb_is_gso(skb)) {
		IP6_INC_STATS(net, idev, IPSTATS_MIB_OUTREQUESTS);

		/* if egress device is enslaved to an L3 master device pass the
		 * skb to its handler for processing
		 */
		skb = l3mdev_ip6_out((struct sock *)sk, skb);
		if (unlikely(!skb))
			return 0;

		/* hooks should never assume socket lock is held.
		 * we promote our socket to non const
		 */
		return NF_HOOK(NFPROTO_IPV6, NF_INET_LOCAL_OUT,
			       net, (struct sock *)sk, skb, NULL, dev,
			       dst_output);
	}

	skb->dev = dev;
	/* ipv6_local_error() does not require socket lock,
	 * we promote our socket to non const
	 */
	ipv6_local_error((struct sock *)sk, EMSGSIZE, fl6, mtu);

	IP6_INC_STATS(net, idev, IPSTATS_MIB_FRAGFAILS);
	kfree_skb(skb);
	return -EMSGSIZE;
}
EXPORT_SYMBOL(ip6_xmit);

static int ip6_call_ra_chain(struct sk_buff *skb, int sel)
{
	struct ip6_ra_chain *ra;
	struct sock *last = NULL;

	read_lock(&ip6_ra_lock);
	for (ra = ip6_ra_chain; ra; ra = ra->next) {
		struct sock *sk = ra->sk;
		if (sk && ra->sel == sel &&
		    (!sk->sk_bound_dev_if ||
		     sk->sk_bound_dev_if == skb->dev->ifindex)) {

			if (inet6_test_bit(RTALERT_ISOLATE, sk) &&
			    !net_eq(sock_net(sk), dev_net(skb->dev))) {
				continue;
			}
			if (last) {
				struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
				if (skb2)
					rawv6_rcv(last, skb2);
			}
			last = sk;
		}
	}

	if (last) {
		rawv6_rcv(last, skb);
		read_unlock(&ip6_ra_lock);
		return 1;
	}
	read_unlock(&ip6_ra_lock);
	return 0;
}

static int ip6_forward_proxy_check(struct sk_buff *skb)
{
	struct ipv6hdr *hdr = ipv6_hdr(skb);
	u8 nexthdr = hdr->nexthdr;
	__be16 frag_off;
	int offset;

	if (ipv6_ext_hdr(nexthdr)) {
		offset = ipv6_skip_exthdr(skb, sizeof(*hdr), &nexthdr, &frag_off);
		if (offset < 0)
			return 0;
	} else
		offset = sizeof(struct ipv6hdr);

	if (nexthdr == IPPROTO_ICMPV6) {
		struct icmp6hdr *icmp6;

		if (!pskb_may_pull(skb, (skb_network_header(skb) +
					 offset + 1 - skb->data)))
			return 0;

		icmp6 = (struct icmp6hdr *)(skb_network_header(skb) + offset);

		switch (icmp6->icmp6_type) {
		case NDISC_ROUTER_SOLICITATION:
		case NDISC_ROUTER_ADVERTISEMENT:
		case NDISC_NEIGHBOUR_SOLICITATION:
		case NDISC_NEIGHBOUR_ADVERTISEMENT:
		case NDISC_REDIRECT:
			/* For reaction involving unicast neighbor discovery
			 * message destined to the proxied address, pass it to
			 * input function.
			 */
			return 1;
		default:
			break;
		}
	}

	/*
	 * The proxying router can't forward traffic sent to a link-local
	 * address, so signal the sender and discard the packet. This
	 * behavior is clarified by the MIPv6 specification.
	 */
	if (ipv6_addr_type(&hdr->daddr) & IPV6_ADDR_LINKLOCAL) {
		dst_link_failure(skb);
		return -1;
	}

	return 0;
}

static inline int ip6_forward_finish(struct net *net, struct sock *sk,
				     struct sk_buff *skb)
{
#ifdef CONFIG_NET_SWITCHDEV
	if (skb->offload_l3_fwd_mark) {
		consume_skb(skb);
		return 0;
	}
#endif

	skb_clear_tstamp(skb);
	return dst_output(net, sk, skb);
}

static bool ip6_pkt_too_big(const struct sk_buff *skb, unsigned int mtu)
{
	if (skb->len <= mtu)
		return false;

	/* ipv6 conntrack defrag sets max_frag_size + ignore_df */
	if (IP6CB(skb)->frag_max_size && IP6CB(skb)->frag_max_size > mtu)
		return true;

	if (skb->ignore_df)
		return false;

	if (skb_is_gso(skb) && skb_gso_validate_network_len(skb, mtu))
		return false;

	return true;
}

int ip6_forward(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct ipv6hdr *hdr = ipv6_hdr(skb);
	struct inet6_skb_parm *opt = IP6CB(skb);
	struct net *net = dev_net(dst->dev);
	struct inet6_dev *idev;
	SKB_DR(reason);
	u32 mtu;

	idev = __in6_dev_get_safely(dev_get_by_index_rcu(net, IP6CB(skb)->iif));
	if (net->ipv6.devconf_all->forwarding == 0)
		goto error;

	if (skb->pkt_type != PACKET_HOST)
		goto drop;

	if (unlikely(skb->sk))
		goto drop;

	if (skb_warn_if_lro(skb))
		goto drop;

	if (!net->ipv6.devconf_all->disable_policy &&
	    (!idev || !idev->cnf.disable_policy) &&
	    !xfrm6_policy_check(NULL, XFRM_POLICY_FWD, skb)) {
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INDISCARDS);
		goto drop;
	}

	skb_forward_csum(skb);

	/*
	 *	We DO NOT make any processing on
	 *	RA packets, pushing them to user level AS IS
	 *	without ane WARRANTY that application will be able
	 *	to interpret them. The reason is that we
	 *	cannot make anything clever here.
	 *
	 *	We are not end-node, so that if packet contains
	 *	AH/ESP, we cannot make anything.
	 *	Defragmentation also would be mistake, RA packets
	 *	cannot be fragmented, because there is no warranty
	 *	that different fragments will go along one path. --ANK
	 */
	if (unlikely(opt->flags & IP6SKB_ROUTERALERT)) {
		if (ip6_call_ra_chain(skb, ntohs(opt->ra)))
			return 0;
	}

	/*
	 *	check and decrement ttl
	 */
	if (hdr->hop_limit <= 1) {
		icmpv6_send(skb, ICMPV6_TIME_EXCEED, ICMPV6_EXC_HOPLIMIT, 0);
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INHDRERRORS);

		kfree_skb_reason(skb, SKB_DROP_REASON_IP_INHDR);
		return -ETIMEDOUT;
	}

	/* XXX: idev->cnf.proxy_ndp? */
	if (net->ipv6.devconf_all->proxy_ndp &&
	    pneigh_lookup(&nd_tbl, net, &hdr->daddr, skb->dev, 0)) {
		int proxied = ip6_forward_proxy_check(skb);
		if (proxied > 0) {
			/* It's tempting to decrease the hop limit
			 * here by 1, as we do at the end of the
			 * function too.
			 *
			 * But that would be incorrect, as proxying is
			 * not forwarding.  The ip6_input function
			 * will handle this packet locally, and it
			 * depends on the hop limit being unchanged.
			 *
			 * One example is the NDP hop limit, that
			 * always has to stay 255, but other would be
			 * similar checks around RA packets, where the
			 * user can even change the desired limit.
			 */
			return ip6_input(skb);
		} else if (proxied < 0) {
			__IP6_INC_STATS(net, idev, IPSTATS_MIB_INDISCARDS);
			goto drop;
		}
	}

	if (!xfrm6_route_forward(skb)) {
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INDISCARDS);
		SKB_DR_SET(reason, XFRM_POLICY);
		goto drop;
	}
	dst = skb_dst(skb);

	/* IPv6 specs say nothing about it, but it is clear that we cannot
	   send redirects to source routed frames.
	   We don't send redirects to frames decapsulated from IPsec.
	 */
	if (IP6CB(skb)->iif == dst->dev->ifindex &&
	    opt->srcrt == 0 && !skb_sec_path(skb)) {
		struct in6_addr *target = NULL;
		struct inet_peer *peer;
		struct rt6_info *rt;

		/*
		 *	incoming and outgoing devices are the same
		 *	send a redirect.
		 */

		rt = (struct rt6_info *) dst;
		if (rt->rt6i_flags & RTF_GATEWAY)
			target = &rt->rt6i_gateway;
		else
			target = &hdr->daddr;

		peer = inet_getpeer_v6(net->ipv6.peers, &hdr->daddr, 1);

		/* Limit redirects both by destination (here)
		   and by source (inside ndisc_send_redirect)
		 */
		if (inet_peer_xrlim_allow(peer, 1*HZ))
			ndisc_send_redirect(skb, target);
		if (peer)
			inet_putpeer(peer);
	} else {
		int addrtype = ipv6_addr_type(&hdr->saddr);

		/* This check is security critical. */
		if (addrtype == IPV6_ADDR_ANY ||
		    addrtype & (IPV6_ADDR_MULTICAST | IPV6_ADDR_LOOPBACK))
			goto error;
		if (addrtype & IPV6_ADDR_LINKLOCAL) {
			icmpv6_send(skb, ICMPV6_DEST_UNREACH,
				    ICMPV6_NOT_NEIGHBOUR, 0);
			goto error;
		}
	}

	__IP6_INC_STATS(net, ip6_dst_idev(dst), IPSTATS_MIB_OUTFORWDATAGRAMS);

	mtu = ip6_dst_mtu_maybe_forward(dst, true);
	if (mtu < IPV6_MIN_MTU)
		mtu = IPV6_MIN_MTU;

	if (ip6_pkt_too_big(skb, mtu)) {
		/* Again, force OUTPUT device used as source address */
		skb->dev = dst->dev;
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		__IP6_INC_STATS(net, idev, IPSTATS_MIB_INTOOBIGERRORS);
		__IP6_INC_STATS(net, ip6_dst_idev(dst),
				IPSTATS_MIB_FRAGFAILS);
		kfree_skb_reason(skb, SKB_DROP_REASON_PKT_TOO_BIG);
		return -EMSGSIZE;
	}

	if (skb_cow(skb, dst->dev->hard_header_len)) {
		__IP6_INC_STATS(net, ip6_dst_idev(dst),
				IPSTATS_MIB_OUTDISCARDS);
		goto drop;
	}

	hdr = ipv6_hdr(skb);

	/* Mangling hops number delayed to point after skb COW */

	hdr->hop_limit--;

	return NF_HOOK(NFPROTO_IPV6, NF_INET_FORWARD,
		       net, NULL, skb, skb->dev, dst->dev,
		       ip6_forward_finish);

error:
	__IP6_INC_STATS(net, idev, IPSTATS_MIB_INADDRERRORS);
	SKB_DR_SET(reason, IP_INADDRERRORS);
drop:
	kfree_skb_reason(skb, reason);
	return -EINVAL;
}

static void ip6_copy_metadata(struct sk_buff *to, struct sk_buff *from)
{
	to->pkt_type = from->pkt_type;
	to->priority = from->priority;
	to->protocol = from->protocol;
	skb_dst_drop(to);
	skb_dst_set(to, dst_clone(skb_dst(from)));
	to->dev = from->dev;
	to->mark = from->mark;

	skb_copy_hash(to, from);

#ifdef CONFIG_NET_SCHED
	to->tc_index = from->tc_index;
#endif
	nf_copy(to, from);
	skb_ext_copy(to, from);
	skb_copy_secmark(to, from);
}

int ip6_fraglist_init(struct sk_buff *skb, unsigned int hlen, u8 *prevhdr,
		      u8 nexthdr, __be32 frag_id,
		      struct ip6_fraglist_iter *iter)
{
	unsigned int first_len;
	struct frag_hdr *fh;

	/* BUILD HEADER */
	*prevhdr = NEXTHDR_FRAGMENT;
	iter->tmp_hdr = kmemdup(skb_network_header(skb), hlen, GFP_ATOMIC);
	if (!iter->tmp_hdr)
		return -ENOMEM;

	iter->frag = skb_shinfo(skb)->frag_list;
	skb_frag_list_init(skb);

	iter->offset = 0;
	iter->hlen = hlen;
	iter->frag_id = frag_id;
	iter->nexthdr = nexthdr;

	__skb_pull(skb, hlen);
	fh = __skb_push(skb, sizeof(struct frag_hdr));
	__skb_push(skb, hlen);
	skb_reset_network_header(skb);
	memcpy(skb_network_header(skb), iter->tmp_hdr, hlen);

	fh->nexthdr = nexthdr;
	fh->reserved = 0;
	fh->frag_off = htons(IP6_MF);
	fh->identification = frag_id;

	first_len = skb_pagelen(skb);
	skb->data_len = first_len - skb_headlen(skb);
	skb->len = first_len;
	ipv6_hdr(skb)->payload_len = htons(first_len - sizeof(struct ipv6hdr));

	return 0;
}
EXPORT_SYMBOL(ip6_fraglist_init);

void ip6_fraglist_prepare(struct sk_buff *skb,
			  struct ip6_fraglist_iter *iter)
{
	struct sk_buff *frag = iter->frag;
	unsigned int hlen = iter->hlen;
	struct frag_hdr *fh;

	frag->ip_summed = CHECKSUM_NONE;
	skb_reset_transport_header(frag);
	fh = __skb_push(frag, sizeof(struct frag_hdr));
	__skb_push(frag, hlen);
	skb_reset_network_header(frag);
	memcpy(skb_network_header(frag), iter->tmp_hdr, hlen);
	iter->offset += skb->len - hlen - sizeof(struct frag_hdr);
	fh->nexthdr = iter->nexthdr;
	fh->reserved = 0;
	fh->frag_off = htons(iter->offset);
	if (frag->next)
		fh->frag_off |= htons(IP6_MF);
	fh->identification = iter->frag_id;
	ipv6_hdr(frag)->payload_len = htons(frag->len - sizeof(struct ipv6hdr));
	ip6_copy_metadata(frag, skb);
}
EXPORT_SYMBOL(ip6_fraglist_prepare);

void ip6_frag_init(struct sk_buff *skb, unsigned int hlen, unsigned int mtu,
		   unsigned short needed_tailroom, int hdr_room, u8 *prevhdr,
		   u8 nexthdr, __be32 frag_id, struct ip6_frag_state *state)
{
	state->prevhdr = prevhdr;
	state->nexthdr = nexthdr;
	state->frag_id = frag_id;

	state->hlen = hlen;
	state->mtu = mtu;

	state->left = skb->len - hlen;	/* Space per frame */
	state->ptr = hlen;		/* Where to start from */

	state->hroom = hdr_room;
	state->troom = needed_tailroom;

	state->offset = 0;
}
EXPORT_SYMBOL(ip6_frag_init);

struct sk_buff *ip6_frag_next(struct sk_buff *skb, struct ip6_frag_state *state)
{
	u8 *prevhdr = state->prevhdr, *fragnexthdr_offset;
	struct sk_buff *frag;
	struct frag_hdr *fh;
	unsigned int len;

	len = state->left;
	/* IF: it doesn't fit, use 'mtu' - the data space left */
	if (len > state->mtu)
		len = state->mtu;
	/* IF: we are not sending up to and including the packet end
	   then align the next start on an eight byte boundary */
	if (len < state->left)
		len &= ~7;

	/* Allocate buffer */
	frag = alloc_skb(len + state->hlen + sizeof(struct frag_hdr) +
			 state->hroom + state->troom, GFP_ATOMIC);
	if (!frag)
		return ERR_PTR(-ENOMEM);

	/*
	 *	Set up data on packet
	 */

	ip6_copy_metadata(frag, skb);
	skb_reserve(frag, state->hroom);
	skb_put(frag, len + state->hlen + sizeof(struct frag_hdr));
	skb_reset_network_header(frag);
	fh = (struct frag_hdr *)(skb_network_header(frag) + state->hlen);
	frag->transport_header = (frag->network_header + state->hlen +
				  sizeof(struct frag_hdr));

	/*
	 *	Charge the memory for the fragment to any owner
	 *	it might possess
	 */
	if (skb->sk)
		skb_set_owner_w(frag, skb->sk);

	/*
	 *	Copy the packet header into the new buffer.
	 */
	skb_copy_from_linear_data(skb, skb_network_header(frag), state->hlen);

	fragnexthdr_offset = skb_network_header(frag);
	fragnexthdr_offset += prevhdr - skb_network_header(skb);
	*fragnexthdr_offset = NEXTHDR_FRAGMENT;

	/*
	 *	Build fragment header.
	 */
	fh->nexthdr = state->nexthdr;
	fh->reserved = 0;
	fh->identification = state->frag_id;

	/*
	 *	Copy a block of the IP datagram.
	 */
	BUG_ON(skb_copy_bits(skb, state->ptr, skb_transport_header(frag),
			     len));
	state->left -= len;

	fh->frag_off = htons(state->offset);
	if (state->left > 0)
		fh->frag_off |= htons(IP6_MF);
	ipv6_hdr(frag)->payload_len = htons(frag->len - sizeof(struct ipv6hdr));

	state->ptr += len;
	state->offset += len;

	return frag;
}
EXPORT_SYMBOL(ip6_frag_next);

int ip6_fragment(struct net *net, struct sock *sk, struct sk_buff *skb,
		 int (*output)(struct net *, struct sock *, struct sk_buff *))
{
	struct sk_buff *frag;
	struct rt6_info *rt = (struct rt6_info *)skb_dst(skb);
	struct ipv6_pinfo *np = skb->sk && !dev_recursion_level() ?
				inet6_sk(skb->sk) : NULL;
	bool mono_delivery_time = skb->mono_delivery_time;
	struct ip6_frag_state state;
	unsigned int mtu, hlen, nexthdr_offset;
	ktime_t tstamp = skb->tstamp;
	int hroom, err = 0;
	__be32 frag_id;
	u8 *prevhdr, nexthdr = 0;

	err = ip6_find_1stfragopt(skb, &prevhdr);
	if (err < 0)
		goto fail;
	hlen = err;
	nexthdr = *prevhdr;
	nexthdr_offset = prevhdr - skb_network_header(skb);

	mtu = ip6_skb_dst_mtu(skb);

	/* We must not fragment if the socket is set to force MTU discovery
	 * or if the skb it not generated by a local socket.
	 */
	if (unlikely(!skb->ignore_df && skb->len > mtu))
		goto fail_toobig;

	if (IP6CB(skb)->frag_max_size) {
		if (IP6CB(skb)->frag_max_size > mtu)
			goto fail_toobig;

		/* don't send fragments larger than what we received */
		mtu = IP6CB(skb)->frag_max_size;
		if (mtu < IPV6_MIN_MTU)
			mtu = IPV6_MIN_MTU;
	}

	if (np) {
		u32 frag_size = READ_ONCE(np->frag_size);

		if (frag_size && frag_size < mtu)
			mtu = frag_size;
	}
	if (mtu < hlen + sizeof(struct frag_hdr) + 8)
		goto fail_toobig;
	mtu -= hlen + sizeof(struct frag_hdr);

	frag_id = ipv6_select_ident(net, &ipv6_hdr(skb)->daddr,
				    &ipv6_hdr(skb)->saddr);

	if (skb->ip_summed == CHECKSUM_PARTIAL &&
	    (err = skb_checksum_help(skb)))
		goto fail;

	prevhdr = skb_network_header(skb) + nexthdr_offset;
	hroom = LL_RESERVED_SPACE(rt->dst.dev);
	if (skb_has_frag_list(skb)) {
		unsigned int first_len = skb_pagelen(skb);
		struct ip6_fraglist_iter iter;
		struct sk_buff *frag2;

		if (first_len - hlen > mtu ||
		    ((first_len - hlen) & 7) ||
		    skb_cloned(skb) ||
		    skb_headroom(skb) < (hroom + sizeof(struct frag_hdr)))
			goto slow_path;

		skb_walk_frags(skb, frag) {
			/* Correct geometry. */
			if (frag->len > mtu ||
			    ((frag->len & 7) && frag->next) ||
			    skb_headroom(frag) < (hlen + hroom + sizeof(struct frag_hdr)))
				goto slow_path_clean;

			/* Partially cloned skb? */
			if (skb_shared(frag))
				goto slow_path_clean;

			BUG_ON(frag->sk);
			if (skb->sk) {
				frag->sk = skb->sk;
				frag->destructor = sock_wfree;
			}
			skb->truesize -= frag->truesize;
		}

		err = ip6_fraglist_init(skb, hlen, prevhdr, nexthdr, frag_id,
					&iter);
		if (err < 0)
			goto fail;

		/* We prevent @rt from being freed. */
		rcu_read_lock();

		for (;;) {
			/* Prepare header of the next frame,
			 * before previous one went down. */
			if (iter.frag)
				ip6_fraglist_prepare(skb, &iter);

			skb_set_delivery_time(skb, tstamp, mono_delivery_time);
			err = output(net, sk, skb);
			if (!err)
				IP6_INC_STATS(net, ip6_dst_idev(&rt->dst),
					      IPSTATS_MIB_FRAGCREATES);

			if (err || !iter.frag)
				break;

			skb = ip6_fraglist_next(&iter);
		}

		kfree(iter.tmp_hdr);

		if (err == 0) {
			IP6_INC_STATS(net, ip6_dst_idev(&rt->dst),
				      IPSTATS_MIB_FRAGOKS);
			rcu_read_unlock();
			return 0;
		}

		kfree_skb_list(iter.frag);

		IP6_INC_STATS(net, ip6_dst_idev(&rt->dst),
			      IPSTATS_MIB_FRAGFAILS);
		rcu_read_unlock();
		return err;

slow_path_clean:
		skb_walk_frags(skb, frag2) {
			if (frag2 == frag)
				break;
			frag2->sk = NULL;
			frag2->destructor = NULL;
			skb->truesize += frag2->truesize;
		}
	}

slow_path:
	/*
	 *	Fragment the datagram.
	 */

	ip6_frag_init(skb, hlen, mtu, rt->dst.dev->needed_tailroom,
		      LL_RESERVED_SPACE(rt->dst.dev), prevhdr, nexthdr, frag_id,
		      &state);

	/*
	 *	Keep copying data until we run out.
	 */

	while (state.left > 0) {
		frag = ip6_frag_next(skb, &state);
		if (IS_ERR(frag)) {
			err = PTR_ERR(frag);
			goto fail;
		}

		/*
		 *	Put this fragment into the sending queue.
		 */
		skb_set_delivery_time(frag, tstamp, mono_delivery_time);
		err = output(net, sk, frag);
		if (err)
			goto fail;

		IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
			      IPSTATS_MIB_FRAGCREATES);
	}
	IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
		      IPSTATS_MIB_FRAGOKS);
	consume_skb(skb);
	return err;

fail_toobig:
	icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
	err = -EMSGSIZE;

fail:
	IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
		      IPSTATS_MIB_FRAGFAILS);
	kfree_skb(skb);
	return err;
}

static inline int ip6_rt_check(const struct rt6key *rt_key,
			       const struct in6_addr *fl_addr,
			       const struct in6_addr *addr_cache)
{
	return (rt_key->plen != 128 || !ipv6_addr_equal(fl_addr, &rt_key->addr)) &&
		(!addr_cache || !ipv6_addr_equal(fl_addr, addr_cache));
}

static struct dst_entry *ip6_sk_dst_check(struct sock *sk,
					  struct dst_entry *dst,
					  const struct flowi6 *fl6)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct rt6_info *rt;

	if (!dst)
		goto out;

	if (dst->ops->family != AF_INET6) {
		dst_release(dst);
		return NULL;
	}

	rt = (struct rt6_info *)dst;
	/* Yes, checking route validity in not connected
	 * case is not very simple. Take into account,
	 * that we do not support routing by source, TOS,
	 * and MSG_DONTROUTE		--ANK (980726)
	 *
	 * 1. ip6_rt_check(): If route was host route,
	 *    check that cached destination is current.
	 *    If it is network route, we still may
	 *    check its validity using saved pointer
	 *    to the last used address: daddr_cache.
	 *    We do not want to save whole address now,
	 *    (because main consumer of this service
	 *    is tcp, which has not this problem),
	 *    so that the last trick works only on connected
	 *    sockets.
	 * 2. oif also should be the same.
	 */
	if (ip6_rt_check(&rt->rt6i_dst, &fl6->daddr, np->daddr_cache) ||
#ifdef CONFIG_IPV6_SUBTREES
	    ip6_rt_check(&rt->rt6i_src, &fl6->saddr, np->saddr_cache) ||
#endif
	   (fl6->flowi6_oif && fl6->flowi6_oif != dst->dev->ifindex)) {
		dst_release(dst);
		dst = NULL;
	}

out:
	return dst;
}

static int ip6_dst_lookup_tail(struct net *net, const struct sock *sk,
			       struct dst_entry **dst, struct flowi6 *fl6)
{
#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
	struct neighbour *n;
	struct rt6_info *rt;
#endif
	int err;
	int flags = 0;

	/* The correct way to handle this would be to do
	 * ip6_route_get_saddr, and then ip6_route_output; however,
	 * the route-specific preferred source forces the
	 * ip6_route_output call _before_ ip6_route_get_saddr.
	 *
	 * In source specific routing (no src=any default route),
	 * ip6_route_output will fail given src=any saddr, though, so
	 * that's why we try it again later.
	 */
	if (ipv6_addr_any(&fl6->saddr)) {
		struct fib6_info *from;
		struct rt6_info *rt;

		*dst = ip6_route_output(net, sk, fl6);
		rt = (*dst)->error ? NULL : (struct rt6_info *)*dst;

		rcu_read_lock();
		from = rt ? rcu_dereference(rt->from) : NULL;
		err = ip6_route_get_saddr(net, from, &fl6->daddr,
					  sk ? READ_ONCE(inet6_sk(sk)->srcprefs) : 0,
					  &fl6->saddr);
		rcu_read_unlock();

		if (err)
			goto out_err_release;

		/* If we had an erroneous initial result, pretend it
		 * never existed and let the SA-enabled version take
		 * over.
		 */
		if ((*dst)->error) {
			dst_release(*dst);
			*dst = NULL;
		}

		if (fl6->flowi6_oif)
			flags |= RT6_LOOKUP_F_IFACE;
	}

	if (!*dst)
		*dst = ip6_route_output_flags(net, sk, fl6, flags);

	err = (*dst)->error;
	if (err)
		goto out_err_release;

#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
	/*
	 * Here if the dst entry we've looked up
	 * has a neighbour entry that is in the INCOMPLETE
	 * state and the src address from the flow is
	 * marked as OPTIMISTIC, we release the found
	 * dst entry and replace it instead with the
	 * dst entry of the nexthop router
	 */
	rt = (struct rt6_info *) *dst;
	rcu_read_lock();
	n = __ipv6_neigh_lookup_noref(rt->dst.dev,
				      rt6_nexthop(rt, &fl6->daddr));
	err = n && !(READ_ONCE(n->nud_state) & NUD_VALID) ? -EINVAL : 0;
	rcu_read_unlock();

	if (err) {
		struct inet6_ifaddr *ifp;
		struct flowi6 fl_gw6;
		int redirect;

		ifp = ipv6_get_ifaddr(net, &fl6->saddr,
				      (*dst)->dev, 1);

		redirect = (ifp && ifp->flags & IFA_F_OPTIMISTIC);
		if (ifp)
			in6_ifa_put(ifp);

		if (redirect) {
			/*
			 * We need to get the dst entry for the
			 * default router instead
			 */
			dst_release(*dst);
			memcpy(&fl_gw6, fl6, sizeof(struct flowi6));
			memset(&fl_gw6.daddr, 0, sizeof(struct in6_addr));
			*dst = ip6_route_output(net, sk, &fl_gw6);
			err = (*dst)->error;
			if (err)
				goto out_err_release;
		}
	}
#endif
	if (ipv6_addr_v4mapped(&fl6->saddr) &&
	    !(ipv6_addr_v4mapped(&fl6->daddr) || ipv6_addr_any(&fl6->daddr))) {
		err = -EAFNOSUPPORT;
		goto out_err_release;
	}

	return 0;

out_err_release:
	dst_release(*dst);
	*dst = NULL;

	if (err == -ENETUNREACH)
		IP6_INC_STATS(net, NULL, IPSTATS_MIB_OUTNOROUTES);
	return err;
}

/**
 *	ip6_dst_lookup - perform route lookup on flow
 *	@net: Network namespace to perform lookup in
 *	@sk: socket which provides route info
 *	@dst: pointer to dst_entry * for result
 *	@fl6: flow to lookup
 *
 *	This function performs a route lookup on the given flow.
 *
 *	It returns zero on success, or a standard errno code on error.
 */
int ip6_dst_lookup(struct net *net, struct sock *sk, struct dst_entry **dst,
		   struct flowi6 *fl6)
{
	*dst = NULL;
	return ip6_dst_lookup_tail(net, sk, dst, fl6);
}
EXPORT_SYMBOL_GPL(ip6_dst_lookup);

/**
 *	ip6_dst_lookup_flow - perform route lookup on flow with ipsec
 *	@net: Network namespace to perform lookup in
 *	@sk: socket which provides route info
 *	@fl6: flow to lookup
 *	@final_dst: final destination address for ipsec lookup
 *
 *	This function performs a route lookup on the given flow.
 *
 *	It returns a valid dst pointer on success, or a pointer encoded
 *	error code.
 */
struct dst_entry *ip6_dst_lookup_flow(struct net *net, const struct sock *sk, struct flowi6 *fl6,
				      const struct in6_addr *final_dst)
{
	struct dst_entry *dst = NULL;
	int err;

	err = ip6_dst_lookup_tail(net, sk, &dst, fl6);
	if (err)
		return ERR_PTR(err);
	if (final_dst)
		fl6->daddr = *final_dst;

	return xfrm_lookup_route(net, dst, flowi6_to_flowi(fl6), sk, 0);
}
EXPORT_SYMBOL_GPL(ip6_dst_lookup_flow);

/**
 *	ip6_sk_dst_lookup_flow - perform socket cached route lookup on flow
 *	@sk: socket which provides the dst cache and route info
 *	@fl6: flow to lookup
 *	@final_dst: final destination address for ipsec lookup
 *	@connected: whether @sk is connected or not
 *
 *	This function performs a route lookup on the given flow with the
 *	possibility of using the cached route in the socket if it is valid.
 *	It will take the socket dst lock when operating on the dst cache.
 *	As a result, this function can only be used in process context.
 *
 *	In addition, for a connected socket, cache the dst in the socket
 *	if the current cache is not valid.
 *
 *	It returns a valid dst pointer on success, or a pointer encoded
 *	error code.
 */
struct dst_entry *ip6_sk_dst_lookup_flow(struct sock *sk, struct flowi6 *fl6,
					 const struct in6_addr *final_dst,
					 bool connected)
{
	struct dst_entry *dst = sk_dst_check(sk, inet6_sk(sk)->dst_cookie);

	dst = ip6_sk_dst_check(sk, dst, fl6);
	if (dst)
		return dst;

	dst = ip6_dst_lookup_flow(sock_net(sk), sk, fl6, final_dst);
	if (connected && !IS_ERR(dst))
		ip6_sk_dst_store_flow(sk, dst_clone(dst), fl6);

	return dst;
}
EXPORT_SYMBOL_GPL(ip6_sk_dst_lookup_flow);

static inline struct ipv6_opt_hdr *ip6_opt_dup(struct ipv6_opt_hdr *src,
					       gfp_t gfp)
{
	return src ? kmemdup(src, (src->hdrlen + 1) * 8, gfp) : NULL;
}

static inline struct ipv6_rt_hdr *ip6_rthdr_dup(struct ipv6_rt_hdr *src,
						gfp_t gfp)
{
	return src ? kmemdup(src, (src->hdrlen + 1) * 8, gfp) : NULL;
}

static void ip6_append_data_mtu(unsigned int *mtu,
				int *maxfraglen,
				unsigned int fragheaderlen,
				struct sk_buff *skb,
				struct rt6_info *rt,
				unsigned int orig_mtu)
{
	if (!(rt->dst.flags & DST_XFRM_TUNNEL)) {
		if (!skb) {
			/* first fragment, reserve header_len */
			*mtu = orig_mtu - rt->dst.header_len;

		} else {
			/*
			 * this fragment is not first, the headers
			 * space is regarded as data space.
			 */
			*mtu = orig_mtu;
		}
		*maxfraglen = ((*mtu - fragheaderlen) & ~7)
			      + fragheaderlen - sizeof(struct frag_hdr);
	}
}

static int ip6_setup_cork(struct sock *sk, struct inet_cork_full *cork,
			  struct inet6_cork *v6_cork, struct ipcm6_cookie *ipc6,
			  struct rt6_info *rt)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	unsigned int mtu, frag_size;
	struct ipv6_txoptions *nopt, *opt = ipc6->opt;

	/* callers pass dst together with a reference, set it first so
	 * ip6_cork_release() can put it down even in case of an error.
	 */
	cork->base.dst = &rt->dst;

	/*
	 * setup for corking
	 */
	if (opt) {
		if (WARN_ON(v6_cork->opt))
			return -EINVAL;

		nopt = v6_cork->opt = kzalloc(sizeof(*opt), sk->sk_allocation);
		if (unlikely(!nopt))
			return -ENOBUFS;

		nopt->tot_len = sizeof(*opt);
		nopt->opt_flen = opt->opt_flen;
		nopt->opt_nflen = opt->opt_nflen;

		nopt->dst0opt = ip6_opt_dup(opt->dst0opt, sk->sk_allocation);
		if (opt->dst0opt && !nopt->dst0opt)
			return -ENOBUFS;

		nopt->dst1opt = ip6_opt_dup(opt->dst1opt, sk->sk_allocation);
		if (opt->dst1opt && !nopt->dst1opt)
			return -ENOBUFS;

		nopt->hopopt = ip6_opt_dup(opt->hopopt, sk->sk_allocation);
		if (opt->hopopt && !nopt->hopopt)
			return -ENOBUFS;

		nopt->srcrt = ip6_rthdr_dup(opt->srcrt, sk->sk_allocation);
		if (opt->srcrt && !nopt->srcrt)
			return -ENOBUFS;

		/* need source address above miyazawa*/
	}
	v6_cork->hop_limit = ipc6->hlimit;
	v6_cork->tclass = ipc6->tclass;
	if (rt->dst.flags & DST_XFRM_TUNNEL)
		mtu = READ_ONCE(np->pmtudisc) >= IPV6_PMTUDISC_PROBE ?
		      READ_ONCE(rt->dst.dev->mtu) : dst_mtu(&rt->dst);
	else
		mtu = READ_ONCE(np->pmtudisc) >= IPV6_PMTUDISC_PROBE ?
			READ_ONCE(rt->dst.dev->mtu) : dst_mtu(xfrm_dst_path(&rt->dst));

	frag_size = READ_ONCE(np->frag_size);
	if (frag_size && frag_size < mtu)
		mtu = frag_size;

	cork->base.fragsize = mtu;
	cork->base.gso_size = ipc6->gso_size;
	cork->base.tx_flags = 0;
	cork->base.mark = ipc6->sockc.mark;
	sock_tx_timestamp(sk, ipc6->sockc.tsflags, &cork->base.tx_flags);

	cork->base.length = 0;
	cork->base.transmit_time = ipc6->sockc.transmit_time;

	return 0;
}

static int __ip6_append_data(struct sock *sk,
			     struct sk_buff_head *queue,
			     struct inet_cork_full *cork_full,
			     struct inet6_cork *v6_cork,
			     struct page_frag *pfrag,
			     int getfrag(void *from, char *to, int offset,
					 int len, int odd, struct sk_buff *skb),
			     void *from, size_t length, int transhdrlen,
			     unsigned int flags, struct ipcm6_cookie *ipc6)
{
	struct sk_buff *skb, *skb_prev = NULL;
	struct inet_cork *cork = &cork_full->base;
	struct flowi6 *fl6 = &cork_full->fl.u.ip6;
	unsigned int maxfraglen, fragheaderlen, mtu, orig_mtu, pmtu;
	struct ubuf_info *uarg = NULL;
	int exthdrlen = 0;
	int dst_exthdrlen = 0;
	int hh_len;
	int copy;
	int err;
	int offset = 0;
	bool zc = false;
	u32 tskey = 0;
	struct rt6_info *rt = (struct rt6_info *)cork->dst;
	bool paged, hold_tskey, extra_uref = false;
	struct ipv6_txoptions *opt = v6_cork->opt;
	int csummode = CHECKSUM_NONE;
	unsigned int maxnonfragsize, headersize;
	unsigned int wmem_alloc_delta = 0;

	skb = skb_peek_tail(queue);
	if (!skb) {
		exthdrlen = opt ? opt->opt_flen : 0;
		dst_exthdrlen = rt->dst.header_len - rt->rt6i_nfheader_len;
	}

	paged = !!cork->gso_size;
	mtu = cork->gso_size ? IP6_MAX_MTU : cork->fragsize;
	orig_mtu = mtu;

	hh_len = LL_RESERVED_SPACE(rt->dst.dev);

	fragheaderlen = sizeof(struct ipv6hdr) + rt->rt6i_nfheader_len +
			(opt ? opt->opt_nflen : 0);

	headersize = sizeof(struct ipv6hdr) +
		     (opt ? opt->opt_flen + opt->opt_nflen : 0) +
		     rt->rt6i_nfheader_len;

	if (mtu <= fragheaderlen ||
	    ((mtu - fragheaderlen) & ~7) + fragheaderlen <= sizeof(struct frag_hdr))
		goto emsgsize;

	maxfraglen = ((mtu - fragheaderlen) & ~7) + fragheaderlen -
		     sizeof(struct frag_hdr);

	/* as per RFC 7112 section 5, the entire IPv6 Header Chain must fit
	 * the first fragment
	 */
	if (headersize + transhdrlen > mtu)
		goto emsgsize;

	if (cork->length + length > mtu - headersize && ipc6->dontfrag &&
	    (sk->sk_protocol == IPPROTO_UDP ||
	     sk->sk_protocol == IPPROTO_ICMPV6 ||
	     sk->sk_protocol == IPPROTO_RAW)) {
		ipv6_local_rxpmtu(sk, fl6, mtu - headersize +
				sizeof(struct ipv6hdr));
		goto emsgsize;
	}

	if (ip6_sk_ignore_df(sk))
		maxnonfragsize = sizeof(struct ipv6hdr) + IPV6_MAXPLEN;
	else
		maxnonfragsize = mtu;

	if (cork->length + length > maxnonfragsize - headersize) {
emsgsize:
		pmtu = max_t(int, mtu - headersize + sizeof(struct ipv6hdr), 0);
		ipv6_local_error(sk, EMSGSIZE, fl6, pmtu);
		return -EMSGSIZE;
	}

	/* CHECKSUM_PARTIAL only with no extension headers and when
	 * we are not going to fragment
	 */
	if (transhdrlen && sk->sk_protocol == IPPROTO_UDP &&
	    headersize == sizeof(struct ipv6hdr) &&
	    length <= mtu - headersize &&
	    (!(flags & MSG_MORE) || cork->gso_size) &&
	    rt->dst.dev->features & (NETIF_F_IPV6_CSUM | NETIF_F_HW_CSUM))
		csummode = CHECKSUM_PARTIAL;

	if ((flags & MSG_ZEROCOPY) && length) {
		struct msghdr *msg = from;

		if (getfrag == ip_generic_getfrag && msg->msg_ubuf) {
			if (skb_zcopy(skb) && msg->msg_ubuf != skb_zcopy(skb))
				return -EINVAL;

			/* Leave uarg NULL if can't zerocopy, callers should
			 * be able to handle it.
			 */
			if ((rt->dst.dev->features & NETIF_F_SG) &&
			    csummode == CHECKSUM_PARTIAL) {
				paged = true;
				zc = true;
				uarg = msg->msg_ubuf;
			}
		} else if (sock_flag(sk, SOCK_ZEROCOPY)) {
			uarg = msg_zerocopy_realloc(sk, length, skb_zcopy(skb));
			if (!uarg)
				return -ENOBUFS;
			extra_uref = !skb_zcopy(skb);	/* only ref on new uarg */
			if (rt->dst.dev->features & NETIF_F_SG &&
			    csummode == CHECKSUM_PARTIAL) {
				paged = true;
				zc = true;
			} else {
				uarg_to_msgzc(uarg)->zerocopy = 0;
				skb_zcopy_set(skb, uarg, &extra_uref);
			}
		}
	} else if ((flags & MSG_SPLICE_PAGES) && length) {
		if (inet_test_bit(HDRINCL, sk))
			return -EPERM;
		if (rt->dst.dev->features & NETIF_F_SG &&
		    getfrag == ip_generic_getfrag)
			/* We need an empty buffer to attach stuff to */
			paged = true;
		else
			flags &= ~MSG_SPLICE_PAGES;
	}

	hold_tskey = cork->tx_flags & SKBTX_ANY_TSTAMP &&
		     READ_ONCE(sk->sk_tsflags) & SOF_TIMESTAMPING_OPT_ID;
	if (hold_tskey)
		tskey = atomic_inc_return(&sk->sk_tskey) - 1;

	/*
	 * Let's try using as much space as possible.
	 * Use MTU if total length of the message fits into the MTU.
	 * Otherwise, we need to reserve fragment header and
	 * fragment alignment (= 8-15 octects, in total).
	 *
	 * Note that we may need to "move" the data from the tail
	 * of the buffer to the new fragment when we split
	 * the message.
	 *
	 * FIXME: It may be fragmented into multiple chunks
	 *        at once if non-fragmentable extension headers
	 *        are too large.
	 * --yoshfuji
	 */

	cork->length += length;
	if (!skb)
		goto alloc_new_skb;

	while (length > 0) {
		/* Check if the remaining data fits into current packet. */
		copy = (cork->length <= mtu ? mtu : maxfraglen) - skb->len;
		if (copy < length)
			copy = maxfraglen - skb->len;

		if (copy <= 0) {
			char *data;
			unsigned int datalen;
			unsigned int fraglen;
			unsigned int fraggap;
			unsigned int alloclen, alloc_extra;
			unsigned int pagedlen;
alloc_new_skb:
			/* There's no room in the current skb */
			if (skb)
				fraggap = skb->len - maxfraglen;
			else
				fraggap = 0;
			/* update mtu and maxfraglen if necessary */
			if (!skb || !skb_prev)
				ip6_append_data_mtu(&mtu, &maxfraglen,
						    fragheaderlen, skb, rt,
						    orig_mtu);

			skb_prev = skb;

			/*
			 * If remaining data exceeds the mtu,
			 * we know we need more fragment(s).
			 */
			datalen = length + fraggap;

			if (datalen > (cork->length <= mtu ? mtu : maxfraglen) - fragheaderlen)
				datalen = maxfraglen - fragheaderlen - rt->dst.trailer_len;
			fraglen = datalen + fragheaderlen;
			pagedlen = 0;

			alloc_extra = hh_len;
			alloc_extra += dst_exthdrlen;
			alloc_extra += rt->dst.trailer_len;

			/* We just reserve space for fragment header.
			 * Note: this may be overallocation if the message
			 * (without MSG_MORE) fits into the MTU.
			 */
			alloc_extra += sizeof(struct frag_hdr);

			if ((flags & MSG_MORE) &&
			    !(rt->dst.dev->features&NETIF_F_SG))
				alloclen = mtu;
			else if (!paged &&
				 (fraglen + alloc_extra < SKB_MAX_ALLOC ||
				  !(rt->dst.dev->features & NETIF_F_SG)))
				alloclen = fraglen;
			else {
				alloclen = fragheaderlen + transhdrlen;
				pagedlen = datalen - transhdrlen;
			}
			alloclen += alloc_extra;

			if (datalen != length + fraggap) {
				/*
				 * this is not the last fragment, the trailer
				 * space is regarded as data space.
				 */
				datalen += rt->dst.trailer_len;
			}

			fraglen = datalen + fragheaderlen;

			copy = datalen - transhdrlen - fraggap - pagedlen;
			/* [!] NOTE: copy may be negative if pagedlen>0
			 * because then the equation may reduces to -fraggap.
			 */
			if (copy < 0 && !(flags & MSG_SPLICE_PAGES)) {
				err = -EINVAL;
				goto error;
			}
			if (transhdrlen) {
				skb = sock_alloc_send_skb(sk, alloclen,
						(flags & MSG_DONTWAIT), &err);
			} else {
				skb = NULL;
				if (refcount_read(&sk->sk_wmem_alloc) + wmem_alloc_delta <=
				    2 * sk->sk_sndbuf)
					skb = alloc_skb(alloclen,
							sk->sk_allocation);
				if (unlikely(!skb))
					err = -ENOBUFS;
			}
			if (!skb)
				goto error;
			/*
			 *	Fill in the control structures
			 */
			skb->protocol = htons(ETH_P_IPV6);
			skb->ip_summed = csummode;
			skb->csum = 0;
			/* reserve for fragmentation and ipsec header */
			skb_reserve(skb, hh_len + sizeof(struct frag_hdr) +
				    dst_exthdrlen);

			/*
			 *	Find where to start putting bytes
			 */
			data = skb_put(skb, fraglen - pagedlen);
			skb_set_network_header(skb, exthdrlen);
			data += fragheaderlen;
			skb->transport_header = (skb->network_header +
						 fragheaderlen);
			if (fraggap) {
				skb->csum = skb_copy_and_csum_bits(
					skb_prev, maxfraglen,
					data + transhdrlen, fraggap);
				skb_prev->csum = csum_sub(skb_prev->csum,
							  skb->csum);
				data += fraggap;
				pskb_trim_unique(skb_prev, maxfraglen);
			}
			if (copy > 0 &&
			    getfrag(from, data + transhdrlen, offset,
				    copy, fraggap, skb) < 0) {
				err = -EFAULT;
				kfree_skb(skb);
				goto error;
			} else if (flags & MSG_SPLICE_PAGES) {
				copy = 0;
			}

			offset += copy;
			length -= copy + transhdrlen;
			transhdrlen = 0;
			exthdrlen = 0;
			dst_exthdrlen = 0;

			/* Only the initial fragment is time stamped */
			skb_shinfo(skb)->tx_flags = cork->tx_flags;
			cork->tx_flags = 0;
			skb_shinfo(skb)->tskey = tskey;
			tskey = 0;
			skb_zcopy_set(skb, uarg, &extra_uref);

			if ((flags & MSG_CONFIRM) && !skb_prev)
				skb_set_dst_pending_confirm(skb, 1);

			/*
			 * Put the packet on the pending queue
			 */
			if (!skb->destructor) {
				skb->destructor = sock_wfree;
				skb->sk = sk;
				wmem_alloc_delta += skb->truesize;
			}
			__skb_queue_tail(queue, skb);
			continue;
		}

		if (copy > length)
			copy = length;

		if (!(rt->dst.dev->features&NETIF_F_SG) &&
		    skb_tailroom(skb) >= copy) {
			unsigned int off;

			off = skb->len;
			if (getfrag(from, skb_put(skb, copy),
						offset, copy, off, skb) < 0) {
				__skb_trim(skb, off);
				err = -EFAULT;
				goto error;
			}
		} else if (flags & MSG_SPLICE_PAGES) {
			struct msghdr *msg = from;

			err = -EIO;
			if (WARN_ON_ONCE(copy > msg->msg_iter.count))
				goto error;

			err = skb_splice_from_iter(skb, &msg->msg_iter, copy,
						   sk->sk_allocation);
			if (err < 0)
				goto error;
			copy = err;
			wmem_alloc_delta += copy;
		} else if (!zc) {
			int i = skb_shinfo(skb)->nr_frags;

			err = -ENOMEM;
			if (!sk_page_frag_refill(sk, pfrag))
				goto error;

			skb_zcopy_downgrade_managed(skb);
			if (!skb_can_coalesce(skb, i, pfrag->page,
					      pfrag->offset)) {
				err = -EMSGSIZE;
				if (i == MAX_SKB_FRAGS)
					goto error;

				__skb_fill_page_desc(skb, i, pfrag->page,
						     pfrag->offset, 0);
				skb_shinfo(skb)->nr_frags = ++i;
				get_page(pfrag->page);
			}
			copy = min_t(int, copy, pfrag->size - pfrag->offset);
			if (getfrag(from,
				    page_address(pfrag->page) + pfrag->offset,
				    offset, copy, skb->len, skb) < 0)
				goto error_efault;

			pfrag->offset += copy;
			skb_frag_size_add(&skb_shinfo(skb)->frags[i - 1], copy);
			skb->len += copy;
			skb->data_len += copy;
			skb->truesize += copy;
			wmem_alloc_delta += copy;
		} else {
			err = skb_zerocopy_iter_dgram(skb, from, copy);
			if (err < 0)
				goto error;
		}
		offset += copy;
		length -= copy;
	}

	if (wmem_alloc_delta)
		refcount_add(wmem_alloc_delta, &sk->sk_wmem_alloc);
	return 0;

error_efault:
	err = -EFAULT;
error:
	net_zcopy_put_abort(uarg, extra_uref);
	cork->length -= length;
	IP6_INC_STATS(sock_net(sk), rt->rt6i_idev, IPSTATS_MIB_OUTDISCARDS);
	refcount_add(wmem_alloc_delta, &sk->sk_wmem_alloc);
	if (hold_tskey)
		atomic_dec(&sk->sk_tskey);
	return err;
}

int ip6_append_data(struct sock *sk,
		    int getfrag(void *from, char *to, int offset, int len,
				int odd, struct sk_buff *skb),
		    void *from, size_t length, int transhdrlen,
		    struct ipcm6_cookie *ipc6, struct flowi6 *fl6,
		    struct rt6_info *rt, unsigned int flags)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	int exthdrlen;
	int err;

	if (flags&MSG_PROBE)
		return 0;
	if (skb_queue_empty(&sk->sk_write_queue)) {
		/*
		 * setup for corking
		 */
		dst_hold(&rt->dst);
		err = ip6_setup_cork(sk, &inet->cork, &np->cork,
				     ipc6, rt);
		if (err)
			return err;

		inet->cork.fl.u.ip6 = *fl6;
		exthdrlen = (ipc6->opt ? ipc6->opt->opt_flen : 0);
		length += exthdrlen;
		transhdrlen += exthdrlen;
	} else {
		transhdrlen = 0;
	}

	return __ip6_append_data(sk, &sk->sk_write_queue, &inet->cork,
				 &np->cork, sk_page_frag(sk), getfrag,
				 from, length, transhdrlen, flags, ipc6);
}
EXPORT_SYMBOL_GPL(ip6_append_data);

static void ip6_cork_steal_dst(struct sk_buff *skb, struct inet_cork_full *cork)
{
	struct dst_entry *dst = cork->base.dst;

	cork->base.dst = NULL;
	skb_dst_set(skb, dst);
}

static void ip6_cork_release(struct inet_cork_full *cork,
			     struct inet6_cork *v6_cork)
{
	if (v6_cork->opt) {
		struct ipv6_txoptions *opt = v6_cork->opt;

		kfree(opt->dst0opt);
		kfree(opt->dst1opt);
		kfree(opt->hopopt);
		kfree(opt->srcrt);
		kfree(opt);
		v6_cork->opt = NULL;
	}

	if (cork->base.dst) {
		dst_release(cork->base.dst);
		cork->base.dst = NULL;
	}
}

struct sk_buff *__ip6_make_skb(struct sock *sk,
			       struct sk_buff_head *queue,
			       struct inet_cork_full *cork,
			       struct inet6_cork *v6_cork)
{
	struct sk_buff *skb, *tmp_skb;
	struct sk_buff **tail_skb;
	struct in6_addr *final_dst;
	struct net *net = sock_net(sk);
	struct ipv6hdr *hdr;
	struct ipv6_txoptions *opt = v6_cork->opt;
	struct rt6_info *rt = (struct rt6_info *)cork->base.dst;
	struct flowi6 *fl6 = &cork->fl.u.ip6;
	unsigned char proto = fl6->flowi6_proto;

	skb = __skb_dequeue(queue);
	if (!skb)
		goto out;
	tail_skb = &(skb_shinfo(skb)->frag_list);

	/* move skb->data to ip header from ext header */
	if (skb->data < skb_network_header(skb))
		__skb_pull(skb, skb_network_offset(skb));
	while ((tmp_skb = __skb_dequeue(queue)) != NULL) {
		__skb_pull(tmp_skb, skb_network_header_len(skb));
		*tail_skb = tmp_skb;
		tail_skb = &(tmp_skb->next);
		skb->len += tmp_skb->len;
		skb->data_len += tmp_skb->len;
		skb->truesize += tmp_skb->truesize;
		tmp_skb->destructor = NULL;
		tmp_skb->sk = NULL;
	}

	/* Allow local fragmentation. */
	skb->ignore_df = ip6_sk_ignore_df(sk);
	__skb_pull(skb, skb_network_header_len(skb));

	final_dst = &fl6->daddr;
	if (opt && opt->opt_flen)
		ipv6_push_frag_opts(skb, opt, &proto);
	if (opt && opt->opt_nflen)
		ipv6_push_nfrag_opts(skb, opt, &proto, &final_dst, &fl6->saddr);

	skb_push(skb, sizeof(struct ipv6hdr));
	skb_reset_network_header(skb);
	hdr = ipv6_hdr(skb);

	ip6_flow_hdr(hdr, v6_cork->tclass,
		     ip6_make_flowlabel(net, skb, fl6->flowlabel,
					ip6_autoflowlabel(net, sk), fl6));
	hdr->hop_limit = v6_cork->hop_limit;
	hdr->nexthdr = proto;
	hdr->saddr = fl6->saddr;
	hdr->daddr = *final_dst;

	skb->priority = READ_ONCE(sk->sk_priority);
	skb->mark = cork->base.mark;
	skb->tstamp = cork->base.transmit_time;

	ip6_cork_steal_dst(skb, cork);
	IP6_INC_STATS(net, rt->rt6i_idev, IPSTATS_MIB_OUTREQUESTS);
	if (proto == IPPROTO_ICMPV6) {
		struct inet6_dev *idev = ip6_dst_idev(skb_dst(skb));
		u8 icmp6_type;

		if (sk->sk_socket->type == SOCK_RAW &&
		   !inet_test_bit(HDRINCL, sk))
			icmp6_type = fl6->fl6_icmp_type;
		else
			icmp6_type = icmp6_hdr(skb)->icmp6_type;
		ICMP6MSGOUT_INC_STATS(net, idev, icmp6_type);
		ICMP6_INC_STATS(net, idev, ICMP6_MIB_OUTMSGS);
	}

	ip6_cork_release(cork, v6_cork);
out:
	return skb;
}

int ip6_send_skb(struct sk_buff *skb)
{
	struct net *net = sock_net(skb->sk);
	struct rt6_info *rt = (struct rt6_info *)skb_dst(skb);
	int err;

	err = ip6_local_out(net, skb->sk, skb);
	if (err) {
		if (err > 0)
			err = net_xmit_errno(err);
		if (err)
			IP6_INC_STATS(net, rt->rt6i_idev,
				      IPSTATS_MIB_OUTDISCARDS);
	}

	return err;
}

int ip6_push_pending_frames(struct sock *sk)
{
	struct sk_buff *skb;

	skb = ip6_finish_skb(sk);
	if (!skb)
		return 0;

	return ip6_send_skb(skb);
}
EXPORT_SYMBOL_GPL(ip6_push_pending_frames);

static void __ip6_flush_pending_frames(struct sock *sk,
				       struct sk_buff_head *queue,
				       struct inet_cork_full *cork,
				       struct inet6_cork *v6_cork)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue_tail(queue)) != NULL) {
		if (skb_dst(skb))
			IP6_INC_STATS(sock_net(sk), ip6_dst_idev(skb_dst(skb)),
				      IPSTATS_MIB_OUTDISCARDS);
		kfree_skb(skb);
	}

	ip6_cork_release(cork, v6_cork);
}

void ip6_flush_pending_frames(struct sock *sk)
{
	__ip6_flush_pending_frames(sk, &sk->sk_write_queue,
				   &inet_sk(sk)->cork, &inet6_sk(sk)->cork);
}
EXPORT_SYMBOL_GPL(ip6_flush_pending_frames);

struct sk_buff *ip6_make_skb(struct sock *sk,
			     int getfrag(void *from, char *to, int offset,
					 int len, int odd, struct sk_buff *skb),
			     void *from, size_t length, int transhdrlen,
			     struct ipcm6_cookie *ipc6, struct rt6_info *rt,
			     unsigned int flags, struct inet_cork_full *cork)
{
	struct inet6_cork v6_cork;
	struct sk_buff_head queue;
	int exthdrlen = (ipc6->opt ? ipc6->opt->opt_flen : 0);
	int err;

	if (flags & MSG_PROBE) {
		dst_release(&rt->dst);
		return NULL;
	}

	__skb_queue_head_init(&queue);

	cork->base.flags = 0;
	cork->base.addr = 0;
	cork->base.opt = NULL;
	v6_cork.opt = NULL;
	err = ip6_setup_cork(sk, cork, &v6_cork, ipc6, rt);
	if (err) {
		ip6_cork_release(cork, &v6_cork);
		return ERR_PTR(err);
	}
	if (ipc6->dontfrag < 0)
		ipc6->dontfrag = inet6_test_bit(DONTFRAG, sk);

	err = __ip6_append_data(sk, &queue, cork, &v6_cork,
				&current->task_frag, getfrag, from,
				length + exthdrlen, transhdrlen + exthdrlen,
				flags, ipc6);
	if (err) {
		__ip6_flush_pending_frames(sk, &queue, cork, &v6_cork);
		return ERR_PTR(err);
	}

	return __ip6_make_skb(sk, &queue, cork, &v6_cork);
}
