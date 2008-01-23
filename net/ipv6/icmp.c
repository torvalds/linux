/*
 *	Internet Control Message Protocol (ICMPv6)
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	$Id: icmp.c,v 1.38 2002/02/08 03:57:19 davem Exp $
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
#include <net/protocol.h>
#include <net/raw.h>
#include <net/rawv6.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/icmp.h>

#include <asm/uaccess.h>
#include <asm/system.h>

DEFINE_SNMP_STAT(struct icmpv6_mib, icmpv6_statistics) __read_mostly;
EXPORT_SYMBOL(icmpv6_statistics);
DEFINE_SNMP_STAT(struct icmpv6msg_mib, icmpv6msg_statistics) __read_mostly;
EXPORT_SYMBOL(icmpv6msg_statistics);

/*
 *	The ICMP socket(s). This is the most convenient way to flow control
 *	our ICMP output as well as maintain a clean interface throughout
 *	all layers. All Socketless IP sends will soon be gone.
 *
 *	On SMP we have one ICMP socket per-cpu.
 */
static DEFINE_PER_CPU(struct socket *, __icmpv6_socket) = NULL;
#define icmpv6_socket	__get_cpu_var(__icmpv6_socket)

static int icmpv6_rcv(struct sk_buff *skb);

static struct inet6_protocol icmpv6_protocol = {
	.handler	=	icmpv6_rcv,
	.flags		=	INET6_PROTO_FINAL,
};

static __inline__ int icmpv6_xmit_lock(void)
{
	local_bh_disable();

	if (unlikely(!spin_trylock(&icmpv6_socket->sk->sk_lock.slock))) {
		/* This can happen if the output path (f.e. SIT or
		 * ip6ip6 tunnel) signals dst_link_failure() for an
		 * outgoing ICMP6 packet.
		 */
		local_bh_enable();
		return 1;
	}
	return 0;
}

static __inline__ void icmpv6_xmit_unlock(void)
{
	spin_unlock_bh(&icmpv6_socket->sk->sk_lock.slock);
}

/*
 * Slightly more convenient version of icmpv6_send.
 */
void icmpv6_param_prob(struct sk_buff *skb, int code, int pos)
{
	icmpv6_send(skb, ICMPV6_PARAMPROB, code, pos, skb->dev);
	kfree_skb(skb);
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

static int is_ineligible(struct sk_buff *skb)
{
	int ptr = (u8 *)(ipv6_hdr(skb) + 1) - skb->data;
	int len = skb->len - ptr;
	__u8 nexthdr = ipv6_hdr(skb)->nexthdr;

	if (len < 0)
		return 1;

	ptr = ipv6_skip_exthdr(skb, ptr, &nexthdr);
	if (ptr < 0)
		return 0;
	if (nexthdr == IPPROTO_ICMPV6) {
		u8 _type, *tp;
		tp = skb_header_pointer(skb,
			ptr+offsetof(struct icmp6hdr, icmp6_type),
			sizeof(_type), &_type);
		if (tp == NULL ||
		    !(*tp & ICMPV6_INFOMSG_MASK))
			return 1;
	}
	return 0;
}

static int sysctl_icmpv6_time __read_mostly = 1*HZ;

/*
 * Check the ICMP output rate limit
 */
static inline int icmpv6_xrlim_allow(struct sock *sk, int type,
				     struct flowi *fl)
{
	struct dst_entry *dst;
	int res = 0;

	/* Informational messages are not limited. */
	if (type & ICMPV6_INFOMSG_MASK)
		return 1;

	/* Do not limit pmtu discovery, it would break it. */
	if (type == ICMPV6_PKT_TOOBIG)
		return 1;

	/*
	 * Look up the output route.
	 * XXX: perhaps the expire for routing entries cloned by
	 * this lookup should be more aggressive (not longer than timeout).
	 */
	dst = ip6_route_output(sk, fl);
	if (dst->error) {
		IP6_INC_STATS(ip6_dst_idev(dst),
			      IPSTATS_MIB_OUTNOROUTES);
	} else if (dst->dev && (dst->dev->flags&IFF_LOOPBACK)) {
		res = 1;
	} else {
		struct rt6_info *rt = (struct rt6_info *)dst;
		int tmo = sysctl_icmpv6_time;

		/* Give more bandwidth to wider prefixes. */
		if (rt->rt6i_dst.plen < 128)
			tmo >>= ((128 - rt->rt6i_dst.plen)>>5);

		res = xrlim_allow(dst, tmo);
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

static __inline__ int opt_unrec(struct sk_buff *skb, __u32 offset)
{
	u8 _optval, *op;

	offset += skb_network_offset(skb);
	op = skb_header_pointer(skb, offset, sizeof(_optval), &_optval);
	if (op == NULL)
		return 1;
	return (*op & 0xC0) == 0x80;
}

static int icmpv6_push_pending_frames(struct sock *sk, struct flowi *fl, struct icmp6hdr *thdr, int len)
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
		skb->csum = csum_partial((char *)icmp6h,
					sizeof(struct icmp6hdr), skb->csum);
		icmp6h->icmp6_cksum = csum_ipv6_magic(&fl->fl6_src,
						      &fl->fl6_dst,
						      len, fl->proto,
						      skb->csum);
	} else {
		__wsum tmp_csum = 0;

		skb_queue_walk(&sk->sk_write_queue, skb) {
			tmp_csum = csum_add(tmp_csum, skb->csum);
		}

		tmp_csum = csum_partial((char *)icmp6h,
					sizeof(struct icmp6hdr), tmp_csum);
		icmp6h->icmp6_cksum = csum_ipv6_magic(&fl->fl6_src,
						      &fl->fl6_dst,
						      len, fl->proto,
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

#if defined(CONFIG_IPV6_MIP6) || defined(CONFIG_IPV6_MIP6_MODULE)
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
			ipv6_addr_copy(&tmp, &iph->saddr);
			ipv6_addr_copy(&iph->saddr, &hao->addr);
			ipv6_addr_copy(&hao->addr, &tmp);
		}
	}
}
#else
static inline void mip6_addr_swap(struct sk_buff *skb) {}
#endif

/*
 *	Send an ICMP message in response to a packet in error
 */
void icmpv6_send(struct sk_buff *skb, int type, int code, __u32 info,
		 struct net_device *dev)
{
	struct inet6_dev *idev = NULL;
	struct ipv6hdr *hdr = ipv6_hdr(skb);
	struct sock *sk;
	struct ipv6_pinfo *np;
	struct in6_addr *saddr = NULL;
	struct dst_entry *dst;
	struct icmp6hdr tmp_hdr;
	struct flowi fl;
	struct icmpv6_msg msg;
	int iif = 0;
	int addr_type = 0;
	int len;
	int hlimit, tclass;
	int err = 0;

	if ((u8 *)hdr < skb->head ||
	    (skb->network_header + sizeof(*hdr)) > skb->tail)
		return;

	/*
	 *	Make sure we respect the rules
	 *	i.e. RFC 1885 2.4(e)
	 *	Rule (e.1) is enforced by not using icmpv6_send
	 *	in any code that processes icmp errors.
	 */
	addr_type = ipv6_addr_type(&hdr->daddr);

	if (ipv6_chk_addr(&hdr->daddr, skb->dev, 0))
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

	if (addr_type & IPV6_ADDR_LINKLOCAL)
		iif = skb->dev->ifindex;

	/*
	 *	Must not send error if the source does not uniquely
	 *	identify a single node (RFC2463 Section 2.4).
	 *	We check unspecified / multicast addresses here,
	 *	and anycast addresses will be checked later.
	 */
	if ((addr_type == IPV6_ADDR_ANY) || (addr_type & IPV6_ADDR_MULTICAST)) {
		LIMIT_NETDEBUG(KERN_DEBUG "icmpv6_send: addr_any/mcast source\n");
		return;
	}

	/*
	 *	Never answer to a ICMP packet.
	 */
	if (is_ineligible(skb)) {
		LIMIT_NETDEBUG(KERN_DEBUG "icmpv6_send: no reply to icmp error\n");
		return;
	}

	mip6_addr_swap(skb);

	memset(&fl, 0, sizeof(fl));
	fl.proto = IPPROTO_ICMPV6;
	ipv6_addr_copy(&fl.fl6_dst, &hdr->saddr);
	if (saddr)
		ipv6_addr_copy(&fl.fl6_src, saddr);
	fl.oif = iif;
	fl.fl_icmp_type = type;
	fl.fl_icmp_code = code;
	security_skb_classify_flow(skb, &fl);

	if (icmpv6_xmit_lock())
		return;

	sk = icmpv6_socket->sk;
	np = inet6_sk(sk);

	if (!icmpv6_xrlim_allow(sk, type, &fl))
		goto out;

	tmp_hdr.icmp6_type = type;
	tmp_hdr.icmp6_code = code;
	tmp_hdr.icmp6_cksum = 0;
	tmp_hdr.icmp6_pointer = htonl(info);

	if (!fl.oif && ipv6_addr_is_multicast(&fl.fl6_dst))
		fl.oif = np->mcast_oif;

	err = ip6_dst_lookup(sk, &dst, &fl);
	if (err)
		goto out;

	/*
	 * We won't send icmp if the destination is known
	 * anycast.
	 */
	if (((struct rt6_info *)dst)->rt6i_flags & RTF_ANYCAST) {
		LIMIT_NETDEBUG(KERN_DEBUG "icmpv6_send: acast source\n");
		goto out_dst_release;
	}

	if ((err = xfrm_lookup(&dst, &fl, sk, 0)) < 0)
		goto out;

	if (ipv6_addr_is_multicast(&fl.fl6_dst))
		hlimit = np->mcast_hops;
	else
		hlimit = np->hop_limit;
	if (hlimit < 0)
		hlimit = dst_metric(dst, RTAX_HOPLIMIT);
	if (hlimit < 0)
		hlimit = ipv6_get_hoplimit(dst->dev);

	tclass = np->tclass;
	if (tclass < 0)
		tclass = 0;

	msg.skb = skb;
	msg.offset = skb_network_offset(skb);
	msg.type = type;

	len = skb->len - msg.offset;
	len = min_t(unsigned int, len, IPV6_MIN_MTU - sizeof(struct ipv6hdr) -sizeof(struct icmp6hdr));
	if (len < 0) {
		LIMIT_NETDEBUG(KERN_DEBUG "icmp: len problem\n");
		goto out_dst_release;
	}

	idev = in6_dev_get(skb->dev);

	err = ip6_append_data(sk, icmpv6_getfrag, &msg,
			      len + sizeof(struct icmp6hdr),
			      sizeof(struct icmp6hdr),
			      hlimit, tclass, NULL, &fl, (struct rt6_info*)dst,
			      MSG_DONTWAIT);
	if (err) {
		ip6_flush_pending_frames(sk);
		goto out_put;
	}
	err = icmpv6_push_pending_frames(sk, &fl, &tmp_hdr, len + sizeof(struct icmp6hdr));

out_put:
	if (likely(idev != NULL))
		in6_dev_put(idev);
out_dst_release:
	dst_release(dst);
out:
	icmpv6_xmit_unlock();
}

EXPORT_SYMBOL(icmpv6_send);

static void icmpv6_echo_reply(struct sk_buff *skb)
{
	struct sock *sk;
	struct inet6_dev *idev;
	struct ipv6_pinfo *np;
	struct in6_addr *saddr = NULL;
	struct icmp6hdr *icmph = icmp6_hdr(skb);
	struct icmp6hdr tmp_hdr;
	struct flowi fl;
	struct icmpv6_msg msg;
	struct dst_entry *dst;
	int err = 0;
	int hlimit;
	int tclass;

	saddr = &ipv6_hdr(skb)->daddr;

	if (!ipv6_unicast_destination(skb))
		saddr = NULL;

	memcpy(&tmp_hdr, icmph, sizeof(tmp_hdr));
	tmp_hdr.icmp6_type = ICMPV6_ECHO_REPLY;

	memset(&fl, 0, sizeof(fl));
	fl.proto = IPPROTO_ICMPV6;
	ipv6_addr_copy(&fl.fl6_dst, &ipv6_hdr(skb)->saddr);
	if (saddr)
		ipv6_addr_copy(&fl.fl6_src, saddr);
	fl.oif = skb->dev->ifindex;
	fl.fl_icmp_type = ICMPV6_ECHO_REPLY;
	security_skb_classify_flow(skb, &fl);

	if (icmpv6_xmit_lock())
		return;

	sk = icmpv6_socket->sk;
	np = inet6_sk(sk);

	if (!fl.oif && ipv6_addr_is_multicast(&fl.fl6_dst))
		fl.oif = np->mcast_oif;

	err = ip6_dst_lookup(sk, &dst, &fl);
	if (err)
		goto out;
	if ((err = xfrm_lookup(&dst, &fl, sk, 0)) < 0)
		goto out;

	if (ipv6_addr_is_multicast(&fl.fl6_dst))
		hlimit = np->mcast_hops;
	else
		hlimit = np->hop_limit;
	if (hlimit < 0)
		hlimit = dst_metric(dst, RTAX_HOPLIMIT);
	if (hlimit < 0)
		hlimit = ipv6_get_hoplimit(dst->dev);

	tclass = np->tclass;
	if (tclass < 0)
		tclass = 0;

	idev = in6_dev_get(skb->dev);

	msg.skb = skb;
	msg.offset = 0;
	msg.type = ICMPV6_ECHO_REPLY;

	err = ip6_append_data(sk, icmpv6_getfrag, &msg, skb->len + sizeof(struct icmp6hdr),
				sizeof(struct icmp6hdr), hlimit, tclass, NULL, &fl,
				(struct rt6_info*)dst, MSG_DONTWAIT);

	if (err) {
		ip6_flush_pending_frames(sk);
		goto out_put;
	}
	err = icmpv6_push_pending_frames(sk, &fl, &tmp_hdr, skb->len + sizeof(struct icmp6hdr));

out_put:
	if (likely(idev != NULL))
		in6_dev_put(idev);
	dst_release(dst);
out:
	icmpv6_xmit_unlock();
}

static void icmpv6_notify(struct sk_buff *skb, int type, int code, __be32 info)
{
	struct in6_addr *saddr, *daddr;
	struct inet6_protocol *ipprot;
	struct sock *sk;
	int inner_offset;
	int hash;
	u8 nexthdr;

	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		return;

	nexthdr = ((struct ipv6hdr *)skb->data)->nexthdr;
	if (ipv6_ext_hdr(nexthdr)) {
		/* now skip over extension headers */
		inner_offset = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &nexthdr);
		if (inner_offset<0)
			return;
	} else {
		inner_offset = sizeof(struct ipv6hdr);
	}

	/* Checkin header including 8 bytes of inner protocol header. */
	if (!pskb_may_pull(skb, inner_offset+8))
		return;

	saddr = &ipv6_hdr(skb)->saddr;
	daddr = &ipv6_hdr(skb)->daddr;

	/* BUGGG_FUTURE: we should try to parse exthdrs in this packet.
	   Without this we will not able f.e. to make source routed
	   pmtu discovery.
	   Corresponding argument (opt) to notifiers is already added.
	   --ANK (980726)
	 */

	hash = nexthdr & (MAX_INET_PROTOS - 1);

	rcu_read_lock();
	ipprot = rcu_dereference(inet6_protos[hash]);
	if (ipprot && ipprot->err_handler)
		ipprot->err_handler(skb, NULL, type, code, inner_offset, info);
	rcu_read_unlock();

	read_lock(&raw_v6_lock);
	if ((sk = sk_head(&raw_v6_htable[hash])) != NULL) {
		while ((sk = __raw_v6_lookup(sk, nexthdr, saddr, daddr,
					    IP6CB(skb)->iif))) {
			rawv6_err(sk, skb, NULL, type, code, inner_offset, info);
			sk = sk_next(sk);
		}
	}
	read_unlock(&raw_v6_lock);
}

/*
 *	Handle icmp messages
 */

static int icmpv6_rcv(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct inet6_dev *idev = __in6_dev_get(dev);
	struct in6_addr *saddr, *daddr;
	struct ipv6hdr *orig_hdr;
	struct icmp6hdr *hdr;
	int type;

	ICMP6_INC_STATS_BH(idev, ICMP6_MIB_INMSGS);

	saddr = &ipv6_hdr(skb)->saddr;
	daddr = &ipv6_hdr(skb)->daddr;

	/* Perform checksum. */
	switch (skb->ip_summed) {
	case CHECKSUM_COMPLETE:
		if (!csum_ipv6_magic(saddr, daddr, skb->len, IPPROTO_ICMPV6,
				     skb->csum))
			break;
		/* fall through */
	case CHECKSUM_NONE:
		skb->csum = ~csum_unfold(csum_ipv6_magic(saddr, daddr, skb->len,
					     IPPROTO_ICMPV6, 0));
		if (__skb_checksum_complete(skb)) {
			LIMIT_NETDEBUG(KERN_DEBUG "ICMPv6 checksum failed [" NIP6_FMT " > " NIP6_FMT "]\n",
				       NIP6(*saddr), NIP6(*daddr));
			goto discard_it;
		}
	}

	if (!pskb_pull(skb, sizeof(struct icmp6hdr)))
		goto discard_it;

	hdr = icmp6_hdr(skb);

	type = hdr->icmp6_type;

	ICMP6MSGIN_INC_STATS_BH(idev, type);

	switch (type) {
	case ICMPV6_ECHO_REQUEST:
		icmpv6_echo_reply(skb);
		break;

	case ICMPV6_ECHO_REPLY:
		/* we couldn't care less */
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
		orig_hdr = (struct ipv6hdr *) (hdr + 1);
		rt6_pmtu_discovery(&orig_hdr->daddr, &orig_hdr->saddr, dev,
				   ntohl(hdr->icmp6_mtu));

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

discard_it:
	ICMP6_INC_STATS_BH(idev, ICMP6_MIB_INERRORS);
	kfree_skb(skb);
	return 0;
}

/*
 * Special lock-class for __icmpv6_socket:
 */
static struct lock_class_key icmpv6_socket_sk_dst_lock_key;

int __init icmpv6_init(struct net_proto_family *ops)
{
	struct sock *sk;
	int err, i, j;

	for_each_possible_cpu(i) {
		err = sock_create_kern(PF_INET6, SOCK_RAW, IPPROTO_ICMPV6,
				       &per_cpu(__icmpv6_socket, i));
		if (err < 0) {
			printk(KERN_ERR
			       "Failed to initialize the ICMP6 control socket "
			       "(err %d).\n",
			       err);
			goto fail;
		}

		sk = per_cpu(__icmpv6_socket, i)->sk;
		sk->sk_allocation = GFP_ATOMIC;
		/*
		 * Split off their lock-class, because sk->sk_dst_lock
		 * gets used from softirqs, which is safe for
		 * __icmpv6_socket (because those never get directly used
		 * via userspace syscalls), but unsafe for normal sockets.
		 */
		lockdep_set_class(&sk->sk_dst_lock,
				  &icmpv6_socket_sk_dst_lock_key);

		/* Enough space for 2 64K ICMP packets, including
		 * sk_buff struct overhead.
		 */
		sk->sk_sndbuf =
			(2 * ((64 * 1024) + sizeof(struct sk_buff)));

		sk->sk_prot->unhash(sk);
	}


	if (inet6_add_protocol(&icmpv6_protocol, IPPROTO_ICMPV6) < 0) {
		printk(KERN_ERR "Failed to register ICMP6 protocol\n");
		err = -EAGAIN;
		goto fail;
	}

	return 0;

 fail:
	for (j = 0; j < i; j++) {
		if (!cpu_possible(j))
			continue;
		sock_release(per_cpu(__icmpv6_socket, j));
	}

	return err;
}

void icmpv6_cleanup(void)
{
	int i;

	for_each_possible_cpu(i) {
		sock_release(per_cpu(__icmpv6_socket, i));
	}
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
};

int icmpv6_err_convert(int type, int code, int *err)
{
	int fatal = 0;

	*err = EPROTO;

	switch (type) {
	case ICMPV6_DEST_UNREACH:
		fatal = 1;
		if (code <= ICMPV6_PORT_UNREACH) {
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
ctl_table ipv6_icmp_table[] = {
	{
		.ctl_name	= NET_IPV6_ICMP_RATELIMIT,
		.procname	= "ratelimit",
		.data		= &sysctl_icmpv6_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{ .ctl_name = 0 },
};
#endif

