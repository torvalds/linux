/*
 *	Internet Control Message Protocol (ICMPv6)
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	Based on net/ipv4/icmp.c
 *
 *	RFC 1885
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Changes:
 *
 *	Andi Kleen		:	exception handling
 *	Andi Kleen			add rate limits. never reply to a icmp.
 *					add more length checks and other fixes.
 *	yoshfuji		:	ensure to sent parameter problem for
 *					fragments.
 *	YOSHIFUJI Hideaki @USAGI:	added sysctl for icmp rate limit.
 *	Randy Dunlap and
 *	YOSHIFUJI Hideaki @USAGI:	Per-interface statistics support
 *	Kazunori MIYAZAWA @USAGI:       change output process to use ip6_append_data
 */

#define pr_fmt(fmt) "IPv6: " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/netfilter.h>
#include <linux/slab.h>

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/icmpv6.h>

#include <net/ip.h>
#include <net/sock.h>

#include <net/ipv6.h>
#include <net/ip6_checksum.h>
#include <net/ping.h>
#include <net/protocol.h>
#include <net/raw.h>
#include <net/rawv6.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/icmp.h>
#include <net/xfrm.h>
#include <net/inet_common.h>
#include <net/dsfield.h>

#include <asm/uaccess.h>

/*
 *	The ICMP socket(s). This is the most convenient way to flow control
 *	our ICMP output as well as maintain a clean interface throughout
 *	all layers. All Socketless IP sends will soon be gone.
 *
 *	On SMP we have one ICMP socket per-cpu.
 */
static inline struct sock *icmpv6_sk(struct net *net)
{
	return net->ipv6.icmp_sk[smp_processor_id()];
}

static void icmpv6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		       u8 type, u8 code, int offset, __be32 info)
{
	/* icmpv6_notify checks 8 bytes can be pulled, icmp6hdr is 8 bytes */
	struct icmp6hdr *icmp6 = (struct icmp6hdr *) (skb->data + offset);
	struct net *net = dev_net(skb->dev);

	if (type == ICMPV6_PKT_TOOBIG)
		ip6_update_pmtu(skb, net, info, 0, 0);
	else if (type == NDISC_REDIRECT)
		ip6_redirect(skb, net, skb->dev->ifindex, 0);

	if (!(type & ICMPV6_INFOMSG_MASK))
		if (icmp6->icmp6_type == ICMPV6_ECHO_REQUEST)
			ping_err(skb, offset, info);
}

static int icmpv6_rcv(struct sk_buff *skb);

static const struct inet6_protocol icmpv6_protocol = {
	.handler	=	icmpv6_rcv,
	.err_handler	=	icmpv6_err,
	.flags		=	INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

static __inline__ struct sock *icmpv6_xmit_lock(struct net *net)
{
	struct sock *sk;

	local_bh_disable();

	sk = icmpv6_sk(net);
	if (unlikely(!spin_trylock(&sk->sk_lock.slock))) {
		/* This can happen if the output path (f.e. SIT or
		 * ip6ip6 tunnel) signals dst_link_failure() for an
		 * outgoing ICMP6 packet.
		 */
		local_bh_enable();
		return NULL;
	}
	return sk;
}

static __inline__ void icmpv6_xmit_unlock(struct sock *sk)
{
	spin_unlock_bh(&sk->sk_lock.slock);
}

/*
 * Figure out, may we reply to this packet with icmp error.
 *
 * We do not reply, if:
 *	- it was icmp error message.
 *	- it is truncated, so that it is known, that protocol is ICMPV6
 *	  (i.e. in the middle of some exthdr)
 *
 *	--ANK (980726)
 */

static bool is_ineligible(const struct sk_buff *skb)
{
	int ptr = (u8 *)(ipv6_hdr(skb) + 1) - skb->data;
	int len = skb->len - ptr;
	__u8 nexthdr = ipv6_hdr(skb)->nexthdr;
	__be16 frag_off;

	if (len < 0)
		return true;

	ptr = ipv6_skip_exthdr(skb, ptr, &nexthdr, &frag_off);
	if (ptr < 0)
		return false;
	if (nexthdr == IPPROTO_ICMPV6) {
		u8 _type, *tp;
		tp = skb_header_pointer(skb,
			ptr+offsetof(struct icmp6hdr, icmp6_type),
			sizeof(_type), &_type);
		if (tp == NULL ||
		    !(*tp & ICMPV6_INFOMSG_MASK))
			return true;
	}
	return false;
}

/*
 * Check the ICMP output rate limit
 */
static inline bool icmpv6_xrlim_allow(struct sock *sk, u8 type,
				      struct flowi6 *fl6)
{
	struct dst_entry *dst;
	struct net *net = sock_net(sk);
	bool res = false;

	/* Informational messages are not limited. */
	if (type & ICMPV6_INFOMSG_MASK)
		return true;

	/* Do not limit pmtu discovery, it would break it. */
	if (type == ICMPV6_PKT_TOOBIG)
		return true;

	/*
	 * Look up the output route.
	 * XXX: perhaps the expire for routing entries cloned by
	 * this lookup should be more aggressive (not longer than timeout).
	 */
	dst = ip6_route_output(net, sk, fl6);
	if (dst->error) {
		IP6_INC_STATS(net, ip6_dst_idev(dst),
			      IPSTATS_MIB_OUTNOROUTES);
	} else if (dst->dev && (dst->dev->flags&IFF_LOOPBACK)) {
		res = true;
	} else {
		struct rt6_info *rt = (struct rt6_info *)dst;
		int tmo = net->ipv6.sysctl.icmpv6_time;
		struct inet_peer *peer;

		/* Give more bandwidth to wider prefixes. */
		if (rt->rt6i_dst.plen < 128)
			tmo >>= ((128 - rt->rt6i_dst.plen)>>5);

		peer = inet_getpeer_v6(net->ipv6.peers, &rt->rt6i_dst.addr, 1);
		res = inet_peer_xrlim_allow(peer, tmo);
		if (peer)
			inet_putpeer(peer);
	}
	dst_release(dst);
	return res;
}

/*
 *	an inline helper for the "simple" if statement below
 *	checks if parameter problem report is caused by an
 *	unrecognized IPv6 option that has the Option Type
 *	highest-order two bits set to 10
 */

static bool opt_unrec(struct sk_buff *skb, __u32 offset)
{
	u8 _optval, *op;

	offset += skb_network_offset(skb);
	op = skb_header_pointer(skb, offset, sizeof(_optval), &_optval);
	if (op == NULL)
		return true;
	return (*op & 0xC0) == 0x80;
}

int icmpv6_push_pending_frames(struct sock *sk, struct flowi6 *fl6,
			       struct icmp6hdr *thdr, int len)
{
	struct sk_buff *skb;
	struct icmp6hdr *icmp6h;
	int err = 0;

	if ((skb = skb_peek(&sk->sk_write_queue)) == NULL)
		goto out;

	icmp6h = icmp6_hdr(skb);
	memcpy(icmp6h, thdr, sizeof(struct icmp6hdr));
	icmp6h->icmp6_cksum = 0;

	if (skb_queue_len(&sk->sk_write_queue) == 1) {
		skb->csum = csum_partial(icmp6h,
					sizeof(struct icmp6hdr), skb->csum);
		icmp6h->icmp6_cksum = csum_ipv6_magic(&fl6->saddr,
						      &fl6->daddr,
						      len, fl6->flowi6_proto,
						      skb->csum);
	} else {
		__wsum tmp_csum = 0;

		skb_queue_walk(&sk->sk_write_queue, skb) {
			tmp_csum = csum_add(tmp_csum, skb->csum);
		}

		tmp_csum = csum_partial(icmp6h,
					sizeof(struct icmp6hdr), tmp_csum);
		icmp6h->icmp6_cksum = csum_ipv6_magic(&fl6->saddr,
						      &fl6->daddr,
						      len, fl6->flowi6_proto,
						      tmp_csum);
	}
	ip6_push_pending_frames(sk);
out:
	return err;
}

struct icmpv6_msg {
	struct sk_buff	*skb;
	int		offset;
	uint8_t		type;
};

static int icmpv6_getfrag(void *from, char *to, int offset, int len, int odd, struct sk_buff *skb)
{
	struct icmpv6_msg *msg = (struct icmpv6_msg *) from;
	struct sk_buff *org_skb = msg->skb;
	__wsum csum = 0;

	csum = skb_copy_and_csum_bits(org_skb, msg->offset + offset,
				      to, len, csum);
	skb->csum = csum_block_add(skb->csum, csum, odd);
	if (!(msg->type & ICMPV6_INFOMSG_MASK))
		nf_ct_attach(skb, org_skb);
	return 0;
}

#if IS_ENABLED(CONFIG_IPV6_MIP6)
static void mip6_addr_swap(struct sk_buff *skb)
{
	struct ipv6hdr *iph = ipv6_hdr(skb);
	struct inet6_skb_parm *opt = IP6CB(skb);
	struct ipv6_destopt_hao *hao;
	struct in6_addr tmp;
	int off;

	if (opt->dsthao) {
		off = ipv6_find_tlv(skb, opt->dsthao, IPV6_TLV_HAO);
		if (likely(off >= 0)) {
			hao = (struct ipv6_destopt_hao *)
					(skb_network_header(skb) + off);
			tmp = iph->saddr;
			iph->saddr = hao->addr;
			hao->addr = tmp;
		}
	}
}
#else
static inline void mip6_addr_swap(struct sk_buff *skb) {}
#endif

static struct dst_entry *icmpv6_route_lookup(struct net *net,
					     struct sk_buff *skb,
					     struct sock *sk,
					     struct flowi6 *fl6)
{
	struct dst_entry *dst, *dst2;
	struct flowi6 fl2;
	int err;

	err = ip6_dst_lookup(sk, &dst, fl6);
	if (err)
		return ERR_PTR(err);

	/*
	 * We won't send icmp if the destination is known
	 * anycast.
	 */
	if (((struct rt6_info *)dst)->rt6i_flags & RTF_ANYCAST) {
		LIMIT_NETDEBUG(KERN_DEBUG "icmp6_send: acast source\n");
		dst_release(dst);
		return ERR_PTR(-EINVAL);
	}

	/* No need to clone since we're just using its address. */
	dst2 = dst;

	dst = xfrm_lookup(net, dst, flowi6_to_flowi(fl6), sk, 0);
	if (!IS_ERR(dst)) {
		if (dst != dst2)
			return dst;
	} else {
		if (PTR_ERR(dst) == -EPERM)
			dst = NULL;
		else
			return dst;
	}

	err = xfrm_decode_session_reverse(skb, flowi6_to_flowi(&fl2), AF_INET6);
	if (err)
		goto relookup_failed;

	err = ip6_dst_lookup(sk, &dst2, &fl2);
	if (err)
		goto relookup_failed;

	dst2 = xfrm_lookup(net, dst2, flowi6_to_flowi(&fl2), sk, XFRM_LOOKUP_ICMP);
	if (!IS_ERR(dst2)) {
		dst_release(dst);
		dst = dst2;
	} else {
		err = PTR_ERR(dst2);
		if (err == -EPERM) {
			dst_release(dst);
			return dst2;
		} else
			goto relookup_failed;
	}

relookup_failed:
	if (dst)
		return dst;
	return ERR_PTR(err);
}

/*
 *	Send an ICMP message in response to a packet in error
 */
static void icmp6_send(struct sk_buff *skb, u8 type, u8 code, __u32 info)
{
	struct net *net = dev_net(skb->dev);
	struct inet6_dev *idev = NULL;
	struct ipv6hdr *hdr = ipv6_hdr(skb);
	struct sock *sk;
	struct ipv6_pinfo *np;
	const struct in6_addr *saddr = NULL;
	struct dst_entry *dst;
	struct icmp6hdr tmp_hdr;
	struct flowi6 fl6;
	struct icmpv6_msg msg;
	int iif = 0;
	int addr_type = 0;
	int len;
	int hlimit;
	int err = 0;
	u32 mark = IP6_REPLY_MARK(net, skb->mark);

	if ((u8 *)hdr < skb->head ||
	    (skb_network_header(skb) + sizeof(*hdr)) > skb_tail_pointer(skb))
		return;

	/*
	 *	Make sure we respect the rules
	 *	i.e. RFC 1885 2.4(e)
	 *	Rule (e.1) is enforced by not using icmp6_send
	 *	in any code that processes icmp errors.
	 */
	addr_type = ipv6_addr_type(&hdr->daddr);

	if (ipv6_chk_addr(net, &hdr->daddr, skb->dev, 0) ||
	    ipv6_chk_acast_addr_src(net, skb->dev, &hdr->daddr))
		saddr = &hdr->daddr;

	/*
	 *	Dest addr check
	 */

	if ((addr_type & IPV6_ADDR_MULTICAST || skb->pkt_type != PACKET_HOST)) {
		if (type != ICMPV6_PKT_TOOBIG &&
		    !(type == ICMPV6_PARAMPROB &&
		      code == ICMPV6_UNK_OPTION &&
		      (opt_unrec(skb, info))))
			return;

		saddr = NULL;
	}

	addr_type = ipv6_addr_type(&hdr->saddr);

	/*
	 *	Source addr check
	 */

	if (__ipv6_addr_needs_scope_id(addr_type))
		iif = skb->dev->ifindex;

	/*
	 *	Must not send error if the source does not uniquely
	 *	identify a single node (RFC2463 Section 2.4).
	 *	We check unspecified / multicast addresses here,
	 *	and anycast addresses will be checked later.
	 */
	if ((addr_type == IPV6_ADDR_ANY) || (addr_type & IPV6_ADDR_MULTICAST)) {
		LIMIT_NETDEBUG(KERN_DEBUG "icmp6_send: addr_any/mcast source\n");
		return;
	}

	/*
	 *	Never answer to a ICMP packet.
	 */
	if (is_ineligible(skb)) {
		LIMIT_NETDEBUG(KERN_DEBUG "icmp6_send: no reply to icmp error\n");
		return;
	}

	mip6_addr_swap(skb);

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_proto = IPPROTO_ICMPV6;
	fl6.daddr = hdr->saddr;
	if (saddr)
		fl6.saddr = *saddr;
	fl6.flowi6_mark = mark;
	fl6.flowi6_oif = iif;
	fl6.fl6_icmp_type = type;
	fl6.fl6_icmp_code = code;
	security_skb_classify_flow(skb, flowi6_to_flowi(&fl6));

	sk = icmpv6_xmit_lock(net);
	if (sk == NULL)
		return;
	sk->sk_mark = mark;
	np = inet6_sk(sk);

	if (!icmpv6_xrlim_allow(sk, type, &fl6))
		goto out;

	tmp_hdr.icmp6_type = type;
	tmp_hdr.icmp6_code = code;
	tmp_hdr.icmp6_cksum = 0;
	tmp_hdr.icmp6_pointer = htonl(info);

	if (!fl6.flowi6_oif && ipv6_addr_is_multicast(&fl6.daddr))
		fl6.flowi6_oif = np->mcast_oif;
	else if (!fl6.flowi6_oif)
		fl6.flowi6_oif = np->ucast_oif;

	dst = icmpv6_route_lookup(net, skb, sk, &fl6);
	if (IS_ERR(dst))
		goto out;

	hlimit = ip6_sk_dst_hoplimit(np, &fl6, dst);

	msg.skb = skb;
	msg.offset = skb_network_offset(skb);
	msg.type = type;

	len = skb->len - msg.offset;
	len = min_t(unsigned int, len, IPV6_MIN_MTU - sizeof(struct ipv6hdr) -sizeof(struct icmp6hdr));
	if (len < 0) {
		LIMIT_NETDEBUG(KERN_DEBUG "icmp: len problem\n");
		goto out_dst_release;
	}

	rcu_read_lock();
	idev = __in6_dev_get(skb->dev);

	err = ip6_append_data(sk, icmpv6_getfrag, &msg,
			      len + sizeof(struct icmp6hdr),
			      sizeof(struct icmp6hdr), hlimit,
			      np->tclass, NULL, &fl6, (struct rt6_info *)dst,
			      MSG_DONTWAIT, np->dontfrag);
	if (err) {
		ICMP6_INC_STATS(net, idev, ICMP6_MIB_OUTERRORS);
		ip6_flush_pending_frames(sk);
	} else {
		err = icmpv6_push_pending_frames(sk, &fl6, &tmp_hdr,
						 len + sizeof(struct icmp6hdr));
	}
	rcu_read_unlock();
out_dst_release:
	dst_release(dst);
out:
	icmpv6_xmit_unlock(sk);
}

/* Slightly more convenient version of icmp6_send.
 */
void icmpv6_param_prob(struct sk_buff *skb, u8 code, int pos)
{
	icmp6_send(skb, ICMPV6_PARAMPROB, code, pos);
	kfree_skb(skb);
}

static void icmpv6_echo_reply(struct sk_buff *skb)
{
	struct net *net = dev_net(skb->dev);
	struct sock *sk;
	struct inet6_dev *idev;
	struct ipv6_pinfo *np;
	const struct in6_addr *saddr = NULL;
	struct icmp6hdr *icmph = icmp6_hdr(skb);
	struct icmp6hdr tmp_hdr;
	struct flowi6 fl6;
	struct icmpv6_msg msg;
	struct dst_entry *dst;
	int err = 0;
	int hlimit;
	u8 tclass;
	u32 mark = IP6_REPLY_MARK(net, skb->mark);

	saddr = &ipv6_hdr(skb)->daddr;

	if (!ipv6_unicast_destination(skb) &&
	    !(net->ipv6.sysctl.anycast_src_echo_reply &&
	      ipv6_anycast_destination(skb)))
		saddr = NULL;

	memcpy(&tmp_hdr, icmph, sizeof(tmp_hdr));
	tmp_hdr.icmp6_type = ICMPV6_ECHO_REPLY;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_proto = IPPROTO_ICMPV6;
	fl6.daddr = ipv6_hdr(skb)->saddr;
	if (saddr)
		fl6.saddr = *saddr;
	fl6.flowi6_oif = skb->dev->ifindex;
	fl6.fl6_icmp_type = ICMPV6_ECHO_REPLY;
	fl6.flowi6_mark = mark;
	security_skb_classify_flow(skb, flowi6_to_flowi(&fl6));

	sk = icmpv6_xmit_lock(net);
	if (sk == NULL)
		return;
	sk->sk_mark = mark;
	np = inet6_sk(sk);

	if (!fl6.flowi6_oif && ipv6_addr_is_multicast(&fl6.daddr))
		fl6.flowi6_oif = np->mcast_oif;
	else if (!fl6.flowi6_oif)
		fl6.flowi6_oif = np->ucast_oif;

	err = ip6_dst_lookup(sk, &dst, &fl6);
	if (err)
		goto out;
	dst = xfrm_lookup(net, dst, flowi6_to_flowi(&fl6), sk, 0);
	if (IS_ERR(dst))
		goto out;

	hlimit = ip6_sk_dst_hoplimit(np, &fl6, dst);

	idev = __in6_dev_get(skb->dev);

	msg.skb = skb;
	msg.offset = 0;
	msg.type = ICMPV6_ECHO_REPLY;

	tclass = ipv6_get_dsfield(ipv6_hdr(skb));
	err = ip6_append_data(sk, icmpv6_getfrag, &msg, skb->len + sizeof(struct icmp6hdr),
				sizeof(struct icmp6hdr), hlimit, tclass, NULL, &fl6,
				(struct rt6_info *)dst, MSG_DONTWAIT,
				np->dontfrag);

	if (err) {
		ICMP6_INC_STATS_BH(net, idev, ICMP6_MIB_OUTERRORS);
		ip6_flush_pending_frames(sk);
	} else {
		err = icmpv6_push_pending_frames(sk, &fl6, &tmp_hdr,
						 skb->len + sizeof(struct icmp6hdr));
	}
	dst_release(dst);
out:
	icmpv6_xmit_unlock(sk);
}

void icmpv6_notify(struct sk_buff *skb, u8 type, u8 code, __be32 info)
{
	const struct inet6_protocol *ipprot;
	int inner_offset;
	__be16 frag_off;
	u8 nexthdr;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		return;

	nexthdr = ((struct ipv6hdr *)skb->data)->nexthdr;
	if (ipv6_ext_hdr(nexthdr)) {
		/* now skip over extension headers */
		inner_offset = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr),
						&nexthdr, &frag_off);
		if (inner_offset<0)
			return;
	} else {
		inner_offset = sizeof(struct ipv6hdr);
	}

	/* Checkin header including 8 bytes of inner protocol header. */
	if (!pskb_may_pull(skb, inner_offset+8))
		return;

	/* BUGGG_FUTURE: we should try to parse exthdrs in this packet.
	   Without this we will not able f.e. to make source routed
	   pmtu discovery.
	   Corresponding argument (opt) to notifiers is already added.
	   --ANK (980726)
	 */

	rcu_read_lock();
	ipprot = rcu_dereference(inet6_protos[nexthdr]);
	if (ipprot && ipprot->err_handler)
		ipprot->err_handler(skb, NULL, type, code, inner_offset, info);
	rcu_read_unlock();

	raw6_icmp_error(skb, nexthdr, type, code, inner_offset, info);
}

/*
 *	Handle icmp messages
 */

static int icmpv6_rcv(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct inet6_dev *idev = __in6_dev_get(dev);
	const struct in6_addr *saddr, *daddr;
	struct icmp6hdr *hdr;
	u8 type;

	if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb)) {
		struct sec_path *sp = skb_sec_path(skb);
		int nh;

		if (!(sp && sp->xvec[sp->len - 1]->props.flags &
				 XFRM_STATE_ICMP))
			goto drop_no_count;

		if (!pskb_may_pull(skb, sizeof(*hdr) + sizeof(struct ipv6hdr)))
			goto drop_no_count;

		nh = skb_network_offset(skb);
		skb_set_network_header(skb, sizeof(*hdr));

		if (!xfrm6_policy_check_reverse(NULL, XFRM_POLICY_IN, skb))
			goto drop_no_count;

		skb_set_network_header(skb, nh);
	}

	ICMP6_INC_STATS_BH(dev_net(dev), idev, ICMP6_MIB_INMSGS);

	saddr = &ipv6_hdr(skb)->saddr;
	daddr = &ipv6_hdr(skb)->daddr;

	if (skb_checksum_validate(skb, IPPROTO_ICMPV6, ip6_compute_pseudo)) {
		LIMIT_NETDEBUG(KERN_DEBUG
			       "ICMPv6 checksum failed [%pI6c > %pI6c]\n",
			       saddr, daddr);
		goto csum_error;
	}

	if (!pskb_pull(skb, sizeof(*hdr)))
		goto discard_it;

	hdr = icmp6_hdr(skb);

	type = hdr->icmp6_type;

	ICMP6MSGIN_INC_STATS_BH(dev_net(dev), idev, type);

	switch (type) {
	case ICMPV6_ECHO_REQUEST:
		icmpv6_echo_reply(skb);
		break;

	case ICMPV6_ECHO_REPLY:
		ping_rcv(skb);
		break;

	case ICMPV6_PKT_TOOBIG:
		/* BUGGG_FUTURE: if packet contains rthdr, we cannot update
		   standard destination cache. Seems, only "advanced"
		   destination cache will allow to solve this problem
		   --ANK (980726)
		 */
		if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
			goto discard_it;
		hdr = icmp6_hdr(skb);

		/*
		 *	Drop through to notify
		 */

	case ICMPV6_DEST_UNREACH:
	case ICMPV6_TIME_EXCEED:
	case ICMPV6_PARAMPROB:
		icmpv6_notify(skb, type, hdr->icmp6_code, hdr->icmp6_mtu);
		break;

	case NDISC_ROUTER_SOLICITATION:
	case NDISC_ROUTER_ADVERTISEMENT:
	case NDISC_NEIGHBOUR_SOLICITATION:
	case NDISC_NEIGHBOUR_ADVERTISEMENT:
	case NDISC_REDIRECT:
		ndisc_rcv(skb);
		break;

	case ICMPV6_MGM_QUERY:
		igmp6_event_query(skb);
		break;

	case ICMPV6_MGM_REPORT:
		igmp6_event_report(skb);
		break;

	case ICMPV6_MGM_REDUCTION:
	case ICMPV6_NI_QUERY:
	case ICMPV6_NI_REPLY:
	case ICMPV6_MLD2_REPORT:
	case ICMPV6_DHAAD_REQUEST:
	case ICMPV6_DHAAD_REPLY:
	case ICMPV6_MOBILE_PREFIX_SOL:
	case ICMPV6_MOBILE_PREFIX_ADV:
		break;

	default:
		LIMIT_NETDEBUG(KERN_DEBUG "icmpv6: msg of unknown type\n");

		/* informational */
		if (type & ICMPV6_INFOMSG_MASK)
			break;

		/*
		 * error of unknown type.
		 * must pass to upper level
		 */

		icmpv6_notify(skb, type, hdr->icmp6_code, hdr->icmp6_mtu);
	}

	kfree_skb(skb);
	return 0;

csum_error:
	ICMP6_INC_STATS_BH(dev_net(dev), idev, ICMP6_MIB_CSUMERRORS);
discard_it:
	ICMP6_INC_STATS_BH(dev_net(dev), idev, ICMP6_MIB_INERRORS);
drop_no_count:
	kfree_skb(skb);
	return 0;
}

void icmpv6_flow_init(struct sock *sk, struct flowi6 *fl6,
		      u8 type,
		      const struct in6_addr *saddr,
		      const struct in6_addr *daddr,
		      int oif)
{
	memset(fl6, 0, sizeof(*fl6));
	fl6->saddr = *saddr;
	fl6->daddr = *daddr;
	fl6->flowi6_proto 	= IPPROTO_ICMPV6;
	fl6->fl6_icmp_type	= type;
	fl6->fl6_icmp_code	= 0;
	fl6->flowi6_oif		= oif;
	security_sk_classify_flow(sk, flowi6_to_flowi(fl6));
}

/*
 * Special lock-class for __icmpv6_sk:
 */
static struct lock_class_key icmpv6_socket_sk_dst_lock_key;

static int __net_init icmpv6_sk_init(struct net *net)
{
	struct sock *sk;
	int err, i, j;

	net->ipv6.icmp_sk =
		kzalloc(nr_cpu_ids * sizeof(struct sock *), GFP_KERNEL);
	if (net->ipv6.icmp_sk == NULL)
		return -ENOMEM;

	for_each_possible_cpu(i) {
		err = inet_ctl_sock_create(&sk, PF_INET6,
					   SOCK_RAW, IPPROTO_ICMPV6, net);
		if (err < 0) {
			pr_err("Failed to initialize the ICMP6 control socket (err %d)\n",
			       err);
			goto fail;
		}

		net->ipv6.icmp_sk[i] = sk;

		/*
		 * Split off their lock-class, because sk->sk_dst_lock
		 * gets used from softirqs, which is safe for
		 * __icmpv6_sk (because those never get directly used
		 * via userspace syscalls), but unsafe for normal sockets.
		 */
		lockdep_set_class(&sk->sk_dst_lock,
				  &icmpv6_socket_sk_dst_lock_key);

		/* Enough space for 2 64K ICMP packets, including
		 * sk_buff struct overhead.
		 */
		sk->sk_sndbuf = 2 * SKB_TRUESIZE(64 * 1024);
	}
	return 0;

 fail:
	for (j = 0; j < i; j++)
		inet_ctl_sock_destroy(net->ipv6.icmp_sk[j]);
	kfree(net->ipv6.icmp_sk);
	return err;
}

static void __net_exit icmpv6_sk_exit(struct net *net)
{
	int i;

	for_each_possible_cpu(i) {
		inet_ctl_sock_destroy(net->ipv6.icmp_sk[i]);
	}
	kfree(net->ipv6.icmp_sk);
}

static struct pernet_operations icmpv6_sk_ops = {
       .init = icmpv6_sk_init,
       .exit = icmpv6_sk_exit,
};

int __init icmpv6_init(void)
{
	int err;

	err = register_pernet_subsys(&icmpv6_sk_ops);
	if (err < 0)
		return err;

	err = -EAGAIN;
	if (inet6_add_protocol(&icmpv6_protocol, IPPROTO_ICMPV6) < 0)
		goto fail;

	err = inet6_register_icmp_sender(icmp6_send);
	if (err)
		goto sender_reg_err;
	return 0;

sender_reg_err:
	inet6_del_protocol(&icmpv6_protocol, IPPROTO_ICMPV6);
fail:
	pr_err("Failed to register ICMP6 protocol\n");
	unregister_pernet_subsys(&icmpv6_sk_ops);
	return err;
}

void icmpv6_cleanup(void)
{
	inet6_unregister_icmp_sender(icmp6_send);
	unregister_pernet_subsys(&icmpv6_sk_ops);
	inet6_del_protocol(&icmpv6_protocol, IPPROTO_ICMPV6);
}


static const struct icmp6_err {
	int err;
	int fatal;
} tab_unreach[] = {
	{	/* NOROUTE */
		.err	= ENETUNREACH,
		.fatal	= 0,
	},
	{	/* ADM_PROHIBITED */
		.err	= EACCES,
		.fatal	= 1,
	},
	{	/* Was NOT_NEIGHBOUR, now reserved */
		.err	= EHOSTUNREACH,
		.fatal	= 0,
	},
	{	/* ADDR_UNREACH	*/
		.err	= EHOSTUNREACH,
		.fatal	= 0,
	},
	{	/* PORT_UNREACH	*/
		.err	= ECONNREFUSED,
		.fatal	= 1,
	},
	{	/* POLICY_FAIL */
		.err	= EACCES,
		.fatal	= 1,
	},
	{	/* REJECT_ROUTE	*/
		.err	= EACCES,
		.fatal	= 1,
	},
};

int icmpv6_err_convert(u8 type, u8 code, int *err)
{
	int fatal = 0;

	*err = EPROTO;

	switch (type) {
	case ICMPV6_DEST_UNREACH:
		fatal = 1;
		if (code < ARRAY_SIZE(tab_unreach)) {
			*err  = tab_unreach[code].err;
			fatal = tab_unreach[code].fatal;
		}
		break;

	case ICMPV6_PKT_TOOBIG:
		*err = EMSGSIZE;
		break;

	case ICMPV6_PARAMPROB:
		*err = EPROTO;
		fatal = 1;
		break;

	case ICMPV6_TIME_EXCEED:
		*err = EHOSTUNREACH;
		break;
	}

	return fatal;
}
EXPORT_SYMBOL(icmpv6_err_convert);

#ifdef CONFIG_SYSCTL
static struct ctl_table ipv6_icmp_table_template[] = {
	{
		.procname	= "ratelimit",
		.data		= &init_net.ipv6.sysctl.icmpv6_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_ms_jiffies,
	},
	{ },
};

struct ctl_table * __net_init ipv6_icmp_sysctl_init(struct net *net)
{
	struct ctl_table *table;

	table = kmemdup(ipv6_icmp_table_template,
			sizeof(ipv6_icmp_table_template),
			GFP_KERNEL);

	if (table)
		table[0].data = &net->ipv6.sysctl.icmpv6_time;

	return table;
}
#endif

