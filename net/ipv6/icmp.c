// SPDX-License-Identifier: GPL-2.0-or-later
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
#include <net/seg6.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/icmp.h>
#include <net/xfrm.h>
#include <net/inet_common.h>
#include <net/dsfield.h>
#include <net/l3mdev.h>

#include <linux/uaccess.h>

static DEFINE_PER_CPU(struct sock *, ipv6_icmp_sk);

static int icmpv6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		       u8 type, u8 code, int offset, __be32 info)
{
	/* icmpv6_notify checks 8 bytes can be pulled, icmp6hdr is 8 bytes */
	struct icmp6hdr *icmp6 = (struct icmp6hdr *) (skb->data + offset);
	struct net *net = dev_net(skb->dev);

	if (type == ICMPV6_PKT_TOOBIG)
		ip6_update_pmtu(skb, net, info, skb->dev->ifindex, 0, sock_net_uid(net, NULL));
	else if (type == NDISC_REDIRECT)
		ip6_redirect(skb, net, skb->dev->ifindex, 0,
			     sock_net_uid(net, NULL));

	if (!(type & ICMPV6_INFOMSG_MASK))
		if (icmp6->icmp6_type == ICMPV6_ECHO_REQUEST)
			ping_err(skb, offset, ntohl(info));

	return 0;
}

static int icmpv6_rcv(struct sk_buff *skb);

static const struct inet6_protocol icmpv6_protocol = {
	.handler	=	icmpv6_rcv,
	.err_handler	=	icmpv6_err,
	.flags		=	INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

/* Called with BH disabled */
static struct sock *icmpv6_xmit_lock(struct net *net)
{
	struct sock *sk;

	sk = this_cpu_read(ipv6_icmp_sk);
	if (unlikely(!spin_trylock(&sk->sk_lock.slock))) {
		/* This can happen if the output path (f.e. SIT or
		 * ip6ip6 tunnel) signals dst_link_failure() for an
		 * outgoing ICMP6 packet.
		 */
		return NULL;
	}
	sock_net_set(sk, net);
	return sk;
}

static void icmpv6_xmit_unlock(struct sock *sk)
{
	sock_net_set(sk, &init_net);
	spin_unlock(&sk->sk_lock.slock);
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

		/* Based on RFC 8200, Section 4.5 Fragment Header, return
		 * false if this is a fragment packet with no icmp header info.
		 */
		if (!tp && frag_off != 0)
			return false;
		else if (!tp || !(*tp & ICMPV6_INFOMSG_MASK))
			return true;
	}
	return false;
}

static bool icmpv6_mask_allow(struct net *net, int type)
{
	if (type > ICMPV6_MSG_MAX)
		return true;

	/* Limit if icmp type is set in ratemask. */
	if (!test_bit(type, net->ipv6.sysctl.icmpv6_ratemask))
		return true;

	return false;
}

static bool icmpv6_global_allow(struct net *net, int type)
{
	if (icmpv6_mask_allow(net, type))
		return true;

	if (icmp_global_allow())
		return true;

	__ICMP_INC_STATS(net, ICMP_MIB_RATELIMITGLOBAL);
	return false;
}

/*
 * Check the ICMP output rate limit
 */
static bool icmpv6_xrlim_allow(struct sock *sk, u8 type,
			       struct flowi6 *fl6)
{
	struct net *net = sock_net(sk);
	struct dst_entry *dst;
	bool res = false;

	if (icmpv6_mask_allow(net, type))
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

		peer = inet_getpeer_v6(net->ipv6.peers, &fl6->daddr, 1);
		res = inet_peer_xrlim_allow(peer, tmo);
		if (peer)
			inet_putpeer(peer);
	}
	if (!res)
		__ICMP6_INC_STATS(net, ip6_dst_idev(dst),
				  ICMP6_MIB_RATELIMITHOST);
	dst_release(dst);
	return res;
}

static bool icmpv6_rt_has_prefsrc(struct sock *sk, u8 type,
				  struct flowi6 *fl6)
{
	struct net *net = sock_net(sk);
	struct dst_entry *dst;
	bool res = false;

	dst = ip6_route_output(net, sk, fl6);
	if (!dst->error) {
		struct rt6_info *rt = (struct rt6_info *)dst;
		struct in6_addr prefsrc;

		rt6_get_prefsrc(rt, &prefsrc);
		res = !ipv6_addr_any(&prefsrc);
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
	if (!op)
		return true;
	return (*op & 0xC0) == 0x80;
}

void icmpv6_push_pending_frames(struct sock *sk, struct flowi6 *fl6,
				struct icmp6hdr *thdr, int len)
{
	struct sk_buff *skb;
	struct icmp6hdr *icmp6h;

	skb = skb_peek(&sk->sk_write_queue);
	if (!skb)
		return;

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
	__wsum csum;

	csum = skb_copy_and_csum_bits(org_skb, msg->offset + offset,
				      to, len);
	skb->csum = csum_block_add(skb->csum, csum, odd);
	if (!(msg->type & ICMPV6_INFOMSG_MASK))
		nf_ct_attach(skb, org_skb);
	return 0;
}

#if IS_ENABLED(CONFIG_IPV6_MIP6)
static void mip6_addr_swap(struct sk_buff *skb, const struct inet6_skb_parm *opt)
{
	struct ipv6hdr *iph = ipv6_hdr(skb);
	struct ipv6_destopt_hao *hao;
	int off;

	if (opt->dsthao) {
		off = ipv6_find_tlv(skb, opt->dsthao, IPV6_TLV_HAO);
		if (likely(off >= 0)) {
			hao = (struct ipv6_destopt_hao *)
					(skb_network_header(skb) + off);
			swap(iph->saddr, hao->addr);
		}
	}
}
#else
static inline void mip6_addr_swap(struct sk_buff *skb, const struct inet6_skb_parm *opt) {}
#endif

static struct dst_entry *icmpv6_route_lookup(struct net *net,
					     struct sk_buff *skb,
					     struct sock *sk,
					     struct flowi6 *fl6)
{
	struct dst_entry *dst, *dst2;
	struct flowi6 fl2;
	int err;

	err = ip6_dst_lookup(net, sk, &dst, fl6);
	if (err)
		return ERR_PTR(err);

	/*
	 * We won't send icmp if the destination is known
	 * anycast unless we need to treat anycast as unicast.
	 */
	if (!READ_ONCE(net->ipv6.sysctl.icmpv6_error_anycast_as_unicast) &&
	    ipv6_anycast_destination(dst, &fl6->daddr)) {
		net_dbg_ratelimited("icmp6_send: acast source\n");
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

	err = xfrm_decode_session_reverse(net, skb, flowi6_to_flowi(&fl2), AF_INET6);
	if (err)
		goto relookup_failed;

	err = ip6_dst_lookup(net, sk, &dst2, &fl2);
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

static struct net_device *icmp6_dev(const struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;

	/* for local traffic to local address, skb dev is the loopback
	 * device. Check if there is a dst attached to the skb and if so
	 * get the real device index. Same is needed for replies to a link
	 * local address on a device enslaved to an L3 master device
	 */
	if (unlikely(dev->ifindex == LOOPBACK_IFINDEX || netif_is_l3_master(skb->dev))) {
		const struct rt6_info *rt6 = skb_rt6_info(skb);

		/* The destination could be an external IP in Ext Hdr (SRv6, RPL, etc.),
		 * and ip6_null_entry could be set to skb if no route is found.
		 */
		if (rt6 && rt6->rt6i_idev)
			dev = rt6->rt6i_idev->dev;
	}

	return dev;
}

static int icmp6_iif(const struct sk_buff *skb)
{
	return icmp6_dev(skb)->ifindex;
}

/*
 *	Send an ICMP message in response to a packet in error
 */
void icmp6_send(struct sk_buff *skb, u8 type, u8 code, __u32 info,
		const struct in6_addr *force_saddr,
		const struct inet6_skb_parm *parm)
{
	struct inet6_dev *idev = NULL;
	struct ipv6hdr *hdr = ipv6_hdr(skb);
	struct sock *sk;
	struct net *net;
	struct ipv6_pinfo *np;
	const struct in6_addr *saddr = NULL;
	struct dst_entry *dst;
	struct icmp6hdr tmp_hdr;
	struct flowi6 fl6;
	struct icmpv6_msg msg;
	struct ipcm6_cookie ipc6;
	int iif = 0;
	int addr_type = 0;
	int len;
	u32 mark;

	if ((u8 *)hdr < skb->head ||
	    (skb_network_header(skb) + sizeof(*hdr)) > skb_tail_pointer(skb))
		return;

	if (!skb->dev)
		return;
	net = dev_net(skb->dev);
	mark = IP6_REPLY_MARK(net, skb->mark);
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

	if (addr_type & IPV6_ADDR_MULTICAST || skb->pkt_type != PACKET_HOST) {
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

	if (__ipv6_addr_needs_scope_id(addr_type)) {
		iif = icmp6_iif(skb);
	} else {
		/*
		 * The source device is used for looking up which routing table
		 * to use for sending an ICMP error.
		 */
		iif = l3mdev_master_ifindex(skb->dev);
	}

	/*
	 *	Must not send error if the source does not uniquely
	 *	identify a single node (RFC2463 Section 2.4).
	 *	We check unspecified / multicast addresses here,
	 *	and anycast addresses will be checked later.
	 */
	if ((addr_type == IPV6_ADDR_ANY) || (addr_type & IPV6_ADDR_MULTICAST)) {
		net_dbg_ratelimited("icmp6_send: addr_any/mcast source [%pI6c > %pI6c]\n",
				    &hdr->saddr, &hdr->daddr);
		return;
	}

	/*
	 *	Never answer to a ICMP packet.
	 */
	if (is_ineligible(skb)) {
		net_dbg_ratelimited("icmp6_send: no reply to icmp error [%pI6c > %pI6c]\n",
				    &hdr->saddr, &hdr->daddr);
		return;
	}

	/* Needed by both icmp_global_allow and icmpv6_xmit_lock */
	local_bh_disable();

	/* Check global sysctl_icmp_msgs_per_sec ratelimit */
	if (!(skb->dev->flags & IFF_LOOPBACK) && !icmpv6_global_allow(net, type))
		goto out_bh_enable;

	mip6_addr_swap(skb, parm);

	sk = icmpv6_xmit_lock(net);
	if (!sk)
		goto out_bh_enable;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_proto = IPPROTO_ICMPV6;
	fl6.daddr = hdr->saddr;
	if (force_saddr)
		saddr = force_saddr;
	if (saddr) {
		fl6.saddr = *saddr;
	} else if (!icmpv6_rt_has_prefsrc(sk, type, &fl6)) {
		/* select a more meaningful saddr from input if */
		struct net_device *in_netdev;

		in_netdev = dev_get_by_index(net, parm->iif);
		if (in_netdev) {
			ipv6_dev_get_saddr(net, in_netdev, &fl6.daddr,
					   inet6_sk(sk)->srcprefs,
					   &fl6.saddr);
			dev_put(in_netdev);
		}
	}
	fl6.flowi6_mark = mark;
	fl6.flowi6_oif = iif;
	fl6.fl6_icmp_type = type;
	fl6.fl6_icmp_code = code;
	fl6.flowi6_uid = sock_net_uid(net, NULL);
	fl6.mp_hash = rt6_multipath_hash(net, &fl6, skb, NULL);
	security_skb_classify_flow(skb, flowi6_to_flowi_common(&fl6));

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

	ipcm6_init_sk(&ipc6, sk);
	ipc6.sockc.mark = mark;
	fl6.flowlabel = ip6_make_flowinfo(ipc6.tclass, fl6.flowlabel);

	dst = icmpv6_route_lookup(net, skb, sk, &fl6);
	if (IS_ERR(dst))
		goto out;

	ipc6.hlimit = ip6_sk_dst_hoplimit(np, &fl6, dst);

	msg.skb = skb;
	msg.offset = skb_network_offset(skb);
	msg.type = type;

	len = skb->len - msg.offset;
	len = min_t(unsigned int, len, IPV6_MIN_MTU - sizeof(struct ipv6hdr) - sizeof(struct icmp6hdr));
	if (len < 0) {
		net_dbg_ratelimited("icmp: len problem [%pI6c > %pI6c]\n",
				    &hdr->saddr, &hdr->daddr);
		goto out_dst_release;
	}

	rcu_read_lock();
	idev = __in6_dev_get(skb->dev);

	if (ip6_append_data(sk, icmpv6_getfrag, &msg,
			    len + sizeof(struct icmp6hdr),
			    sizeof(struct icmp6hdr),
			    &ipc6, &fl6, (struct rt6_info *)dst,
			    MSG_DONTWAIT)) {
		ICMP6_INC_STATS(net, idev, ICMP6_MIB_OUTERRORS);
		ip6_flush_pending_frames(sk);
	} else {
		icmpv6_push_pending_frames(sk, &fl6, &tmp_hdr,
					   len + sizeof(struct icmp6hdr));
	}
	rcu_read_unlock();
out_dst_release:
	dst_release(dst);
out:
	icmpv6_xmit_unlock(sk);
out_bh_enable:
	local_bh_enable();
}
EXPORT_SYMBOL(icmp6_send);

/* Slightly more convenient version of icmp6_send with drop reasons.
 */
void icmpv6_param_prob_reason(struct sk_buff *skb, u8 code, int pos,
			      enum skb_drop_reason reason)
{
	icmp6_send(skb, ICMPV6_PARAMPROB, code, pos, NULL, IP6CB(skb));
	kfree_skb_reason(skb, reason);
}

/* Generate icmpv6 with type/code ICMPV6_DEST_UNREACH/ICMPV6_ADDR_UNREACH
 * if sufficient data bytes are available
 * @nhs is the size of the tunnel header(s) :
 *  Either an IPv4 header for SIT encap
 *         an IPv4 header + GRE header for GRE encap
 */
int ip6_err_gen_icmpv6_unreach(struct sk_buff *skb, int nhs, int type,
			       unsigned int data_len)
{
	struct in6_addr temp_saddr;
	struct rt6_info *rt;
	struct sk_buff *skb2;
	u32 info = 0;

	if (!pskb_may_pull(skb, nhs + sizeof(struct ipv6hdr) + 8))
		return 1;

	/* RFC 4884 (partial) support for ICMP extensions */
	if (data_len < 128 || (data_len & 7) || skb->len < data_len)
		data_len = 0;

	skb2 = data_len ? skb_copy(skb, GFP_ATOMIC) : skb_clone(skb, GFP_ATOMIC);

	if (!skb2)
		return 1;

	skb_dst_drop(skb2);
	skb_pull(skb2, nhs);
	skb_reset_network_header(skb2);

	rt = rt6_lookup(dev_net(skb->dev), &ipv6_hdr(skb2)->saddr, NULL, 0,
			skb, 0);

	if (rt && rt->dst.dev)
		skb2->dev = rt->dst.dev;

	ipv6_addr_set_v4mapped(ip_hdr(skb)->saddr, &temp_saddr);

	if (data_len) {
		/* RFC 4884 (partial) support :
		 * insert 0 padding at the end, before the extensions
		 */
		__skb_push(skb2, nhs);
		skb_reset_network_header(skb2);
		memmove(skb2->data, skb2->data + nhs, data_len - nhs);
		memset(skb2->data + data_len - nhs, 0, nhs);
		/* RFC 4884 4.5 : Length is measured in 64-bit words,
		 * and stored in reserved[0]
		 */
		info = (data_len/8) << 24;
	}
	if (type == ICMP_TIME_EXCEEDED)
		icmp6_send(skb2, ICMPV6_TIME_EXCEED, ICMPV6_EXC_HOPLIMIT,
			   info, &temp_saddr, IP6CB(skb2));
	else
		icmp6_send(skb2, ICMPV6_DEST_UNREACH, ICMPV6_ADDR_UNREACH,
			   info, &temp_saddr, IP6CB(skb2));
	if (rt)
		ip6_rt_put(rt);

	kfree_skb(skb2);

	return 0;
}
EXPORT_SYMBOL(ip6_err_gen_icmpv6_unreach);

static enum skb_drop_reason icmpv6_echo_reply(struct sk_buff *skb)
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
	struct ipcm6_cookie ipc6;
	u32 mark = IP6_REPLY_MARK(net, skb->mark);
	SKB_DR(reason);
	bool acast;
	u8 type;

	if (ipv6_addr_is_multicast(&ipv6_hdr(skb)->daddr) &&
	    net->ipv6.sysctl.icmpv6_echo_ignore_multicast)
		return reason;

	saddr = &ipv6_hdr(skb)->daddr;

	acast = ipv6_anycast_destination(skb_dst(skb), saddr);
	if (acast && net->ipv6.sysctl.icmpv6_echo_ignore_anycast)
		return reason;

	if (!ipv6_unicast_destination(skb) &&
	    !(net->ipv6.sysctl.anycast_src_echo_reply && acast))
		saddr = NULL;

	if (icmph->icmp6_type == ICMPV6_EXT_ECHO_REQUEST)
		type = ICMPV6_EXT_ECHO_REPLY;
	else
		type = ICMPV6_ECHO_REPLY;

	memcpy(&tmp_hdr, icmph, sizeof(tmp_hdr));
	tmp_hdr.icmp6_type = type;

	memset(&fl6, 0, sizeof(fl6));
	if (net->ipv6.sysctl.flowlabel_reflect & FLOWLABEL_REFLECT_ICMPV6_ECHO_REPLIES)
		fl6.flowlabel = ip6_flowlabel(ipv6_hdr(skb));

	fl6.flowi6_proto = IPPROTO_ICMPV6;
	fl6.daddr = ipv6_hdr(skb)->saddr;
	if (saddr)
		fl6.saddr = *saddr;
	fl6.flowi6_oif = icmp6_iif(skb);
	fl6.fl6_icmp_type = type;
	fl6.flowi6_mark = mark;
	fl6.flowi6_uid = sock_net_uid(net, NULL);
	security_skb_classify_flow(skb, flowi6_to_flowi_common(&fl6));

	local_bh_disable();
	sk = icmpv6_xmit_lock(net);
	if (!sk)
		goto out_bh_enable;
	np = inet6_sk(sk);

	if (!fl6.flowi6_oif && ipv6_addr_is_multicast(&fl6.daddr))
		fl6.flowi6_oif = np->mcast_oif;
	else if (!fl6.flowi6_oif)
		fl6.flowi6_oif = np->ucast_oif;

	if (ip6_dst_lookup(net, sk, &dst, &fl6))
		goto out;
	dst = xfrm_lookup(net, dst, flowi6_to_flowi(&fl6), sk, 0);
	if (IS_ERR(dst))
		goto out;

	/* Check the ratelimit */
	if ((!(skb->dev->flags & IFF_LOOPBACK) && !icmpv6_global_allow(net, ICMPV6_ECHO_REPLY)) ||
	    !icmpv6_xrlim_allow(sk, ICMPV6_ECHO_REPLY, &fl6))
		goto out_dst_release;

	idev = __in6_dev_get(skb->dev);

	msg.skb = skb;
	msg.offset = 0;
	msg.type = type;

	ipcm6_init_sk(&ipc6, sk);
	ipc6.hlimit = ip6_sk_dst_hoplimit(np, &fl6, dst);
	ipc6.tclass = ipv6_get_dsfield(ipv6_hdr(skb));
	ipc6.sockc.mark = mark;

	if (icmph->icmp6_type == ICMPV6_EXT_ECHO_REQUEST)
		if (!icmp_build_probe(skb, (struct icmphdr *)&tmp_hdr))
			goto out_dst_release;

	if (ip6_append_data(sk, icmpv6_getfrag, &msg,
			    skb->len + sizeof(struct icmp6hdr),
			    sizeof(struct icmp6hdr), &ipc6, &fl6,
			    (struct rt6_info *)dst, MSG_DONTWAIT)) {
		__ICMP6_INC_STATS(net, idev, ICMP6_MIB_OUTERRORS);
		ip6_flush_pending_frames(sk);
	} else {
		icmpv6_push_pending_frames(sk, &fl6, &tmp_hdr,
					   skb->len + sizeof(struct icmp6hdr));
		reason = SKB_CONSUMED;
	}
out_dst_release:
	dst_release(dst);
out:
	icmpv6_xmit_unlock(sk);
out_bh_enable:
	local_bh_enable();
	return reason;
}

enum skb_drop_reason icmpv6_notify(struct sk_buff *skb, u8 type,
				   u8 code, __be32 info)
{
	struct inet6_skb_parm *opt = IP6CB(skb);
	struct net *net = dev_net(skb->dev);
	const struct inet6_protocol *ipprot;
	enum skb_drop_reason reason;
	int inner_offset;
	__be16 frag_off;
	u8 nexthdr;

	reason = pskb_may_pull_reason(skb, sizeof(struct ipv6hdr));
	if (reason != SKB_NOT_DROPPED_YET)
		goto out;

	seg6_icmp_srh(skb, opt);

	nexthdr = ((struct ipv6hdr *)skb->data)->nexthdr;
	if (ipv6_ext_hdr(nexthdr)) {
		/* now skip over extension headers */
		inner_offset = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr),
						&nexthdr, &frag_off);
		if (inner_offset < 0) {
			SKB_DR_SET(reason, IPV6_BAD_EXTHDR);
			goto out;
		}
	} else {
		inner_offset = sizeof(struct ipv6hdr);
	}

	/* Checkin header including 8 bytes of inner protocol header. */
	reason = pskb_may_pull_reason(skb, inner_offset + 8);
	if (reason != SKB_NOT_DROPPED_YET)
		goto out;

	/* BUGGG_FUTURE: we should try to parse exthdrs in this packet.
	   Without this we will not able f.e. to make source routed
	   pmtu discovery.
	   Corresponding argument (opt) to notifiers is already added.
	   --ANK (980726)
	 */

	ipprot = rcu_dereference(inet6_protos[nexthdr]);
	if (ipprot && ipprot->err_handler)
		ipprot->err_handler(skb, opt, type, code, inner_offset, info);

	raw6_icmp_error(skb, nexthdr, type, code, inner_offset, info);
	return SKB_CONSUMED;

out:
	__ICMP6_INC_STATS(net, __in6_dev_get(skb->dev), ICMP6_MIB_INERRORS);
	return reason;
}

/*
 *	Handle icmp messages
 */

static int icmpv6_rcv(struct sk_buff *skb)
{
	enum skb_drop_reason reason = SKB_DROP_REASON_NOT_SPECIFIED;
	struct net *net = dev_net(skb->dev);
	struct net_device *dev = icmp6_dev(skb);
	struct inet6_dev *idev = __in6_dev_get(dev);
	const struct in6_addr *saddr, *daddr;
	struct icmp6hdr *hdr;
	u8 type;

	if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb)) {
		struct sec_path *sp = skb_sec_path(skb);
		int nh;

		if (!(sp && sp->xvec[sp->len - 1]->props.flags &
				 XFRM_STATE_ICMP)) {
			reason = SKB_DROP_REASON_XFRM_POLICY;
			goto drop_no_count;
		}

		if (!pskb_may_pull(skb, sizeof(*hdr) + sizeof(struct ipv6hdr)))
			goto drop_no_count;

		nh = skb_network_offset(skb);
		skb_set_network_header(skb, sizeof(*hdr));

		if (!xfrm6_policy_check_reverse(NULL, XFRM_POLICY_IN,
						skb)) {
			reason = SKB_DROP_REASON_XFRM_POLICY;
			goto drop_no_count;
		}

		skb_set_network_header(skb, nh);
	}

	__ICMP6_INC_STATS(dev_net(dev), idev, ICMP6_MIB_INMSGS);

	saddr = &ipv6_hdr(skb)->saddr;
	daddr = &ipv6_hdr(skb)->daddr;

	if (skb_checksum_validate(skb, IPPROTO_ICMPV6, ip6_compute_pseudo)) {
		net_dbg_ratelimited("ICMPv6 checksum failed [%pI6c > %pI6c]\n",
				    saddr, daddr);
		goto csum_error;
	}

	if (!pskb_pull(skb, sizeof(*hdr)))
		goto discard_it;

	hdr = icmp6_hdr(skb);

	type = hdr->icmp6_type;

	ICMP6MSGIN_INC_STATS(dev_net(dev), idev, type);

	switch (type) {
	case ICMPV6_ECHO_REQUEST:
		if (!net->ipv6.sysctl.icmpv6_echo_ignore_all)
			reason = icmpv6_echo_reply(skb);
		break;
	case ICMPV6_EXT_ECHO_REQUEST:
		if (!net->ipv6.sysctl.icmpv6_echo_ignore_all &&
		    READ_ONCE(net->ipv4.sysctl_icmp_echo_enable_probe))
			reason = icmpv6_echo_reply(skb);
		break;

	case ICMPV6_ECHO_REPLY:
		reason = ping_rcv(skb);
		break;

	case ICMPV6_EXT_ECHO_REPLY:
		reason = ping_rcv(skb);
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

		/* to notify */
		fallthrough;
	case ICMPV6_DEST_UNREACH:
	case ICMPV6_TIME_EXCEED:
	case ICMPV6_PARAMPROB:
		reason = icmpv6_notify(skb, type, hdr->icmp6_code,
				       hdr->icmp6_mtu);
		break;

	case NDISC_ROUTER_SOLICITATION:
	case NDISC_ROUTER_ADVERTISEMENT:
	case NDISC_NEIGHBOUR_SOLICITATION:
	case NDISC_NEIGHBOUR_ADVERTISEMENT:
	case NDISC_REDIRECT:
		reason = ndisc_rcv(skb);
		break;

	case ICMPV6_MGM_QUERY:
		igmp6_event_query(skb);
		return 0;

	case ICMPV6_MGM_REPORT:
		igmp6_event_report(skb);
		return 0;

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
		/* informational */
		if (type & ICMPV6_INFOMSG_MASK)
			break;

		net_dbg_ratelimited("icmpv6: msg of unknown type [%pI6c > %pI6c]\n",
				    saddr, daddr);

		/*
		 * error of unknown type.
		 * must pass to upper level
		 */

		reason = icmpv6_notify(skb, type, hdr->icmp6_code,
				       hdr->icmp6_mtu);
	}

	/* until the v6 path can be better sorted assume failure and
	 * preserve the status quo behaviour for the rest of the paths to here
	 */
	if (reason)
		kfree_skb_reason(skb, reason);
	else
		consume_skb(skb);

	return 0;

csum_error:
	reason = SKB_DROP_REASON_ICMP_CSUM;
	__ICMP6_INC_STATS(dev_net(dev), idev, ICMP6_MIB_CSUMERRORS);
discard_it:
	__ICMP6_INC_STATS(dev_net(dev), idev, ICMP6_MIB_INERRORS);
drop_no_count:
	kfree_skb_reason(skb, reason);
	return 0;
}

void icmpv6_flow_init(const struct sock *sk, struct flowi6 *fl6, u8 type,
		      const struct in6_addr *saddr,
		      const struct in6_addr *daddr, int oif)
{
	memset(fl6, 0, sizeof(*fl6));
	fl6->saddr = *saddr;
	fl6->daddr = *daddr;
	fl6->flowi6_proto	= IPPROTO_ICMPV6;
	fl6->fl6_icmp_type	= type;
	fl6->fl6_icmp_code	= 0;
	fl6->flowi6_oif		= oif;
	security_sk_classify_flow(sk, flowi6_to_flowi_common(fl6));
}

int __init icmpv6_init(void)
{
	struct sock *sk;
	int err, i;

	for_each_possible_cpu(i) {
		err = inet_ctl_sock_create(&sk, PF_INET6,
					   SOCK_RAW, IPPROTO_ICMPV6, &init_net);
		if (err < 0) {
			pr_err("Failed to initialize the ICMP6 control socket (err %d)\n",
			       err);
			return err;
		}

		per_cpu(ipv6_icmp_sk, i) = sk;

		/* Enough space for 2 64K ICMP packets, including
		 * sk_buff struct overhead.
		 */
		sk->sk_sndbuf = 2 * SKB_TRUESIZE(64 * 1024);
	}

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
	return err;
}

void icmpv6_cleanup(void)
{
	inet6_unregister_icmp_sender(icmp6_send);
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
	{
		.procname	= "echo_ignore_all",
		.data		= &init_net.ipv6.sysctl.icmpv6_echo_ignore_all,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler = proc_dou8vec_minmax,
	},
	{
		.procname	= "echo_ignore_multicast",
		.data		= &init_net.ipv6.sysctl.icmpv6_echo_ignore_multicast,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler = proc_dou8vec_minmax,
	},
	{
		.procname	= "echo_ignore_anycast",
		.data		= &init_net.ipv6.sysctl.icmpv6_echo_ignore_anycast,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler = proc_dou8vec_minmax,
	},
	{
		.procname	= "ratemask",
		.data		= &init_net.ipv6.sysctl.icmpv6_ratemask_ptr,
		.maxlen		= ICMPV6_MSG_MAX + 1,
		.mode		= 0644,
		.proc_handler = proc_do_large_bitmap,
	},
	{
		.procname	= "error_anycast_as_unicast",
		.data		= &init_net.ipv6.sysctl.icmpv6_error_anycast_as_unicast,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.proc_handler	= proc_dou8vec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{ },
};

struct ctl_table * __net_init ipv6_icmp_sysctl_init(struct net *net)
{
	struct ctl_table *table;

	table = kmemdup(ipv6_icmp_table_template,
			sizeof(ipv6_icmp_table_template),
			GFP_KERNEL);

	if (table) {
		table[0].data = &net->ipv6.sysctl.icmpv6_time;
		table[1].data = &net->ipv6.sysctl.icmpv6_echo_ignore_all;
		table[2].data = &net->ipv6.sysctl.icmpv6_echo_ignore_multicast;
		table[3].data = &net->ipv6.sysctl.icmpv6_echo_ignore_anycast;
		table[4].data = &net->ipv6.sysctl.icmpv6_ratemask_ptr;
		table[5].data = &net->ipv6.sysctl.icmpv6_error_anycast_as_unicast;
	}
	return table;
}

size_t ipv6_icmp_sysctl_table_size(void)
{
	return ARRAY_SIZE(ipv6_icmp_table_template);
}
#endif
