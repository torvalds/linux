/*
 *	NET3:	Implementation of the ICMP protocol layer.
 *
 *		Alan Cox, <alan@lxorguk.ukuu.org.uk>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Some of the function names and the icmp unreach table for this
 *	module were derived from [icmp.c 1.0.11 06/02/93] by
 *	Ross Biro, Fred N. van Kempen, Mark Evans, Alan Cox, Gerhard Koerting.
 *	Other than that this module is a complete rewrite.
 *
 *	Fixes:
 *	Clemens Fruhwirth	:	introduce global icmp rate limiting
 *					with icmp type masking ability instead
 *					of broken per type icmp timeouts.
 *		Mike Shaver	:	RFC1122 checks.
 *		Alan Cox	:	Multicast ping reply as self.
 *		Alan Cox	:	Fix atomicity lockup in ip_build_xmit
 *					call.
 *		Alan Cox	:	Added 216,128 byte paths to the MTU
 *					code.
 *		Martin Mares	:	RFC1812 checks.
 *		Martin Mares	:	Can be configured to follow redirects
 *					if acting as a router _without_ a
 *					routing protocol (RFC 1812).
 *		Martin Mares	:	Echo requests may be configured to
 *					be ignored (RFC 1812).
 *		Martin Mares	:	Limitation of ICMP error message
 *					transmit rate (RFC 1812).
 *		Martin Mares	:	TOS and Precedence set correctly
 *					(RFC 1812).
 *		Martin Mares	:	Now copying as much data from the
 *					original packet as we can without
 *					exceeding 576 bytes (RFC 1812).
 *	Willy Konynenberg	:	Transparent proxying support.
 *		Keith Owens	:	RFC1191 correction for 4.2BSD based
 *					path MTU bug.
 *		Thomas Quinot	:	ICMP Dest Unreach codes up to 15 are
 *					valid (RFC 1812).
 *		Andi Kleen	:	Check all packet lengths properly
 *					and moved all kfree_skb() up to
 *					icmp_rcv.
 *		Andi Kleen	:	Move the rate limit bookkeeping
 *					into the dest entry and use a token
 *					bucket filter (thanks to ANK). Make
 *					the rates sysctl configurable.
 *		Yu Tianli	:	Fixed two ugly bugs in icmp_send
 *					- IP option length was accounted wrongly
 *					- ICMP header length was not accounted
 *					  at all.
 *              Tristan Greaves :       Added sysctl option to ignore bogus
 *              			broadcast responses from broken routers.
 *
 * To Fix:
 *
 *	- Should use skb_pull() instead of all the manual checking.
 *	  This would also greatly simply some upper layer error handlers. --AK
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/netfilter_ipv4.h>
#include <linux/slab.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/protocol.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/raw.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <net/checksum.h>
#include <net/xfrm.h>
#include <net/inet_common.h>

/*
 *	Build xmit assembly blocks
 */

struct icmp_bxm {
	struct sk_buff *skb;
	int offset;
	int data_len;

	struct {
		struct icmphdr icmph;
		__be32	       times[3];
	} data;
	int head_len;
	struct ip_options replyopts;
	unsigned char  optbuf[40];
};

/* An array of errno for error messages from dest unreach. */
/* RFC 1122: 3.2.2.1 States that NET_UNREACH, HOST_UNREACH and SR_FAILED MUST be considered 'transient errs'. */

const struct icmp_err icmp_err_convert[] = {
	{
		.errno = ENETUNREACH,	/* ICMP_NET_UNREACH */
		.fatal = 0,
	},
	{
		.errno = EHOSTUNREACH,	/* ICMP_HOST_UNREACH */
		.fatal = 0,
	},
	{
		.errno = ENOPROTOOPT	/* ICMP_PROT_UNREACH */,
		.fatal = 1,
	},
	{
		.errno = ECONNREFUSED,	/* ICMP_PORT_UNREACH */
		.fatal = 1,
	},
	{
		.errno = EMSGSIZE,	/* ICMP_FRAG_NEEDED */
		.fatal = 0,
	},
	{
		.errno = EOPNOTSUPP,	/* ICMP_SR_FAILED */
		.fatal = 0,
	},
	{
		.errno = ENETUNREACH,	/* ICMP_NET_UNKNOWN */
		.fatal = 1,
	},
	{
		.errno = EHOSTDOWN,	/* ICMP_HOST_UNKNOWN */
		.fatal = 1,
	},
	{
		.errno = ENONET,	/* ICMP_HOST_ISOLATED */
		.fatal = 1,
	},
	{
		.errno = ENETUNREACH,	/* ICMP_NET_ANO	*/
		.fatal = 1,
	},
	{
		.errno = EHOSTUNREACH,	/* ICMP_HOST_ANO */
		.fatal = 1,
	},
	{
		.errno = ENETUNREACH,	/* ICMP_NET_UNR_TOS */
		.fatal = 0,
	},
	{
		.errno = EHOSTUNREACH,	/* ICMP_HOST_UNR_TOS */
		.fatal = 0,
	},
	{
		.errno = EHOSTUNREACH,	/* ICMP_PKT_FILTERED */
		.fatal = 1,
	},
	{
		.errno = EHOSTUNREACH,	/* ICMP_PREC_VIOLATION */
		.fatal = 1,
	},
	{
		.errno = EHOSTUNREACH,	/* ICMP_PREC_CUTOFF */
		.fatal = 1,
	},
};
EXPORT_SYMBOL(icmp_err_convert);

/*
 *	ICMP control array. This specifies what to do with each ICMP.
 */

struct icmp_control {
	void (*handler)(struct sk_buff *skb);
	short   error;		/* This ICMP is classed as an error message */
};

static const struct icmp_control icmp_pointers[NR_ICMP_TYPES+1];

/*
 *	The ICMP socket(s). This is the most convenient way to flow control
 *	our ICMP output as well as maintain a clean interface throughout
 *	all layers. All Socketless IP sends will soon be gone.
 *
 *	On SMP we have one ICMP socket per-cpu.
 */
static struct sock *icmp_sk(struct net *net)
{
	return net->ipv4.icmp_sk[smp_processor_id()];
}

static inline struct sock *icmp_xmit_lock(struct net *net)
{
	struct sock *sk;

	local_bh_disable();

	sk = icmp_sk(net);

	if (unlikely(!spin_trylock(&sk->sk_lock.slock))) {
		/* This can happen if the output path signals a
		 * dst_link_failure() for an outgoing ICMP packet.
		 */
		local_bh_enable();
		return NULL;
	}
	return sk;
}

static inline void icmp_xmit_unlock(struct sock *sk)
{
	spin_unlock_bh(&sk->sk_lock.slock);
}

/*
 *	Send an ICMP frame.
 */

/*
 *	Check transmit rate limitation for given message.
 *	The rate information is held in the destination cache now.
 *	This function is generic and could be used for other purposes
 *	too. It uses a Token bucket filter as suggested by Alexey Kuznetsov.
 *
 *	Note that the same dst_entry fields are modified by functions in
 *	route.c too, but these work for packet destinations while xrlim_allow
 *	works for icmp destinations. This means the rate limiting information
 *	for one "ip object" is shared - and these ICMPs are twice limited:
 *	by source and by destination.
 *
 *	RFC 1812: 4.3.2.8 SHOULD be able to limit error message rate
 *			  SHOULD allow setting of rate limits
 *
 * 	Shared between ICMPv4 and ICMPv6.
 */
#define XRLIM_BURST_FACTOR 6
int xrlim_allow(struct dst_entry *dst, int timeout)
{
	unsigned long now, token = dst->rate_tokens;
	int rc = 0;

	now = jiffies;
	token += now - dst->rate_last;
	dst->rate_last = now;
	if (token > XRLIM_BURST_FACTOR * timeout)
		token = XRLIM_BURST_FACTOR * timeout;
	if (token >= timeout) {
		token -= timeout;
		rc = 1;
	}
	dst->rate_tokens = token;
	return rc;
}
EXPORT_SYMBOL(xrlim_allow);

static inline int icmpv4_xrlim_allow(struct net *net, struct rtable *rt,
		int type, int code)
{
	struct dst_entry *dst = &rt->dst;
	int rc = 1;

	if (type > NR_ICMP_TYPES)
		goto out;

	/* Don't limit PMTU discovery. */
	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED)
		goto out;

	/* No rate limit on loopback */
	if (dst->dev && (dst->dev->flags&IFF_LOOPBACK))
		goto out;

	/* Limit if icmp type is enabled in ratemask. */
	if ((1 << type) & net->ipv4.sysctl_icmp_ratemask)
		rc = xrlim_allow(dst, net->ipv4.sysctl_icmp_ratelimit);
out:
	return rc;
}

/*
 *	Maintain the counters used in the SNMP statistics for outgoing ICMP
 */
void icmp_out_count(struct net *net, unsigned char type)
{
	ICMPMSGOUT_INC_STATS(net, type);
	ICMP_INC_STATS(net, ICMP_MIB_OUTMSGS);
}

/*
 *	Checksum each fragment, and on the first include the headers and final
 *	checksum.
 */
static int icmp_glue_bits(void *from, char *to, int offset, int len, int odd,
			  struct sk_buff *skb)
{
	struct icmp_bxm *icmp_param = (struct icmp_bxm *)from;
	__wsum csum;

	csum = skb_copy_and_csum_bits(icmp_param->skb,
				      icmp_param->offset + offset,
				      to, len, 0);

	skb->csum = csum_block_add(skb->csum, csum, odd);
	if (icmp_pointers[icmp_param->data.icmph.type].error)
		nf_ct_attach(skb, icmp_param->skb);
	return 0;
}

static void icmp_push_reply(struct icmp_bxm *icmp_param,
			    struct ipcm_cookie *ipc, struct rtable **rt)
{
	struct sock *sk;
	struct sk_buff *skb;

	sk = icmp_sk(dev_net((*rt)->dst.dev));
	if (ip_append_data(sk, icmp_glue_bits, icmp_param,
			   icmp_param->data_len+icmp_param->head_len,
			   icmp_param->head_len,
			   ipc, rt, MSG_DONTWAIT) < 0) {
		ICMP_INC_STATS_BH(sock_net(sk), ICMP_MIB_OUTERRORS);
		ip_flush_pending_frames(sk);
	} else if ((skb = skb_peek(&sk->sk_write_queue)) != NULL) {
		struct icmphdr *icmph = icmp_hdr(skb);
		__wsum csum = 0;
		struct sk_buff *skb1;

		skb_queue_walk(&sk->sk_write_queue, skb1) {
			csum = csum_add(csum, skb1->csum);
		}
		csum = csum_partial_copy_nocheck((void *)&icmp_param->data,
						 (char *)icmph,
						 icmp_param->head_len, csum);
		icmph->checksum = csum_fold(csum);
		skb->ip_summed = CHECKSUM_NONE;
		ip_push_pending_frames(sk);
	}
}

/*
 *	Driving logic for building and sending ICMP messages.
 */

static void icmp_reply(struct icmp_bxm *icmp_param, struct sk_buff *skb)
{
	struct ipcm_cookie ipc;
	struct rtable *rt = skb_rtable(skb);
	struct net *net = dev_net(rt->dst.dev);
	struct sock *sk;
	struct inet_sock *inet;
	__be32 daddr;

	if (ip_options_echo(&icmp_param->replyopts, skb))
		return;

	sk = icmp_xmit_lock(net);
	if (sk == NULL)
		return;
	inet = inet_sk(sk);

	icmp_param->data.icmph.checksum = 0;

	inet->tos = ip_hdr(skb)->tos;
	daddr = ipc.addr = rt->rt_src;
	ipc.opt = NULL;
	ipc.tx_flags = 0;
	if (icmp_param->replyopts.optlen) {
		ipc.opt = &icmp_param->replyopts;
		if (ipc.opt->srr)
			daddr = icmp_param->replyopts.faddr;
	}
	{
		struct flowi fl = { .nl_u = { .ip4_u =
					      { .daddr = daddr,
						.saddr = rt->rt_spec_dst,
						.tos = RT_TOS(ip_hdr(skb)->tos) } },
				    .proto = IPPROTO_ICMP };
		security_skb_classify_flow(skb, &fl);
		if (ip_route_output_key(net, &rt, &fl))
			goto out_unlock;
	}
	if (icmpv4_xrlim_allow(net, rt, icmp_param->data.icmph.type,
			       icmp_param->data.icmph.code))
		icmp_push_reply(icmp_param, &ipc, &rt);
	ip_rt_put(rt);
out_unlock:
	icmp_xmit_unlock(sk);
}


/*
 *	Send an ICMP message in response to a situation
 *
 *	RFC 1122: 3.2.2	MUST send at least the IP header and 8 bytes of header.
 *		  MAY send more (we do).
 *			MUST NOT change this header information.
 *			MUST NOT reply to a multicast/broadcast IP address.
 *			MUST NOT reply to a multicast/broadcast MAC address.
 *			MUST reply to only the first fragment.
 */

void icmp_send(struct sk_buff *skb_in, int type, int code, __be32 info)
{
	struct iphdr *iph;
	int room;
	struct icmp_bxm icmp_param;
	struct rtable *rt = skb_rtable(skb_in);
	struct ipcm_cookie ipc;
	__be32 saddr;
	u8  tos;
	struct net *net;
	struct sock *sk;

	if (!rt)
		goto out;
	net = dev_net(rt->dst.dev);

	/*
	 *	Find the original header. It is expected to be valid, of course.
	 *	Check this, icmp_send is called from the most obscure devices
	 *	sometimes.
	 */
	iph = ip_hdr(skb_in);

	if ((u8 *)iph < skb_in->head ||
	    (skb_in->network_header + sizeof(*iph)) > skb_in->tail)
		goto out;

	/*
	 *	No replies to physical multicast/broadcast
	 */
	if (skb_in->pkt_type != PACKET_HOST)
		goto out;

	/*
	 *	Now check at the protocol level
	 */
	if (rt->rt_flags & (RTCF_BROADCAST | RTCF_MULTICAST))
		goto out;

	/*
	 *	Only reply to fragment 0. We byte re-order the constant
	 *	mask for efficiency.
	 */
	if (iph->frag_off & htons(IP_OFFSET))
		goto out;

	/*
	 *	If we send an ICMP error to an ICMP error a mess would result..
	 */
	if (icmp_pointers[type].error) {
		/*
		 *	We are an error, check if we are replying to an
		 *	ICMP error
		 */
		if (iph->protocol == IPPROTO_ICMP) {
			u8 _inner_type, *itp;

			itp = skb_header_pointer(skb_in,
						 skb_network_header(skb_in) +
						 (iph->ihl << 2) +
						 offsetof(struct icmphdr,
							  type) -
						 skb_in->data,
						 sizeof(_inner_type),
						 &_inner_type);
			if (itp == NULL)
				goto out;

			/*
			 *	Assume any unknown ICMP type is an error. This
			 *	isn't specified by the RFC, but think about it..
			 */
			if (*itp > NR_ICMP_TYPES ||
			    icmp_pointers[*itp].error)
				goto out;
		}
	}

	sk = icmp_xmit_lock(net);
	if (sk == NULL)
		return;

	/*
	 *	Construct source address and options.
	 */

	saddr = iph->daddr;
	if (!(rt->rt_flags & RTCF_LOCAL)) {
		struct net_device *dev = NULL;

		rcu_read_lock();
		if (rt->fl.iif &&
			net->ipv4.sysctl_icmp_errors_use_inbound_ifaddr)
			dev = dev_get_by_index_rcu(net, rt->fl.iif);

		if (dev)
			saddr = inet_select_addr(dev, 0, RT_SCOPE_LINK);
		else
			saddr = 0;
		rcu_read_unlock();
	}

	tos = icmp_pointers[type].error ? ((iph->tos & IPTOS_TOS_MASK) |
					   IPTOS_PREC_INTERNETCONTROL) :
					  iph->tos;

	if (ip_options_echo(&icmp_param.replyopts, skb_in))
		goto out_unlock;


	/*
	 *	Prepare data for ICMP header.
	 */

	icmp_param.data.icmph.type	 = type;
	icmp_param.data.icmph.code	 = code;
	icmp_param.data.icmph.un.gateway = info;
	icmp_param.data.icmph.checksum	 = 0;
	icmp_param.skb	  = skb_in;
	icmp_param.offset = skb_network_offset(skb_in);
	inet_sk(sk)->tos = tos;
	ipc.addr = iph->saddr;
	ipc.opt = &icmp_param.replyopts;
	ipc.tx_flags = 0;

	{
		struct flowi fl = {
			.nl_u = {
				.ip4_u = {
					.daddr = icmp_param.replyopts.srr ?
						icmp_param.replyopts.faddr :
						iph->saddr,
					.saddr = saddr,
					.tos = RT_TOS(tos)
				}
			},
			.proto = IPPROTO_ICMP,
			.uli_u = {
				.icmpt = {
					.type = type,
					.code = code
				}
			}
		};
		int err;
		struct rtable *rt2;

		security_skb_classify_flow(skb_in, &fl);
		if (__ip_route_output_key(net, &rt, &fl))
			goto out_unlock;

		/* No need to clone since we're just using its address. */
		rt2 = rt;

		if (!fl.nl_u.ip4_u.saddr)
			fl.nl_u.ip4_u.saddr = rt->rt_src;

		err = xfrm_lookup(net, (struct dst_entry **)&rt, &fl, NULL, 0);
		switch (err) {
		case 0:
			if (rt != rt2)
				goto route_done;
			break;
		case -EPERM:
			rt = NULL;
			break;
		default:
			goto out_unlock;
		}

		if (xfrm_decode_session_reverse(skb_in, &fl, AF_INET))
			goto relookup_failed;

		if (inet_addr_type(net, fl.fl4_src) == RTN_LOCAL)
			err = __ip_route_output_key(net, &rt2, &fl);
		else {
			struct flowi fl2 = {};
			unsigned long orefdst;

			fl2.fl4_dst = fl.fl4_src;
			if (ip_route_output_key(net, &rt2, &fl2))
				goto relookup_failed;

			/* Ugh! */
			orefdst = skb_in->_skb_refdst; /* save old refdst */
			err = ip_route_input(skb_in, fl.fl4_dst, fl.fl4_src,
					     RT_TOS(tos), rt2->dst.dev);

			dst_release(&rt2->dst);
			rt2 = skb_rtable(skb_in);
			skb_in->_skb_refdst = orefdst; /* restore old refdst */
		}

		if (err)
			goto relookup_failed;

		err = xfrm_lookup(net, (struct dst_entry **)&rt2, &fl, NULL,
				  XFRM_LOOKUP_ICMP);
		switch (err) {
		case 0:
			dst_release(&rt->dst);
			rt = rt2;
			break;
		case -EPERM:
			goto ende;
		default:
relookup_failed:
			if (!rt)
				goto out_unlock;
			break;
		}
	}

route_done:
	if (!icmpv4_xrlim_allow(net, rt, type, code))
		goto ende;

	/* RFC says return as much as we can without exceeding 576 bytes. */

	room = dst_mtu(&rt->dst);
	if (room > 576)
		room = 576;
	room -= sizeof(struct iphdr) + icmp_param.replyopts.optlen;
	room -= sizeof(struct icmphdr);

	icmp_param.data_len = skb_in->len - icmp_param.offset;
	if (icmp_param.data_len > room)
		icmp_param.data_len = room;
	icmp_param.head_len = sizeof(struct icmphdr);

	icmp_push_reply(&icmp_param, &ipc, &rt);
ende:
	ip_rt_put(rt);
out_unlock:
	icmp_xmit_unlock(sk);
out:;
}
EXPORT_SYMBOL(icmp_send);


/*
 *	Handle ICMP_DEST_UNREACH, ICMP_TIME_EXCEED, and ICMP_QUENCH.
 */

static void icmp_unreach(struct sk_buff *skb)
{
	struct iphdr *iph;
	struct icmphdr *icmph;
	int hash, protocol;
	const struct net_protocol *ipprot;
	u32 info = 0;
	struct net *net;

	net = dev_net(skb_dst(skb)->dev);

	/*
	 *	Incomplete header ?
	 * 	Only checks for the IP header, there should be an
	 *	additional check for longer headers in upper levels.
	 */

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto out_err;

	icmph = icmp_hdr(skb);
	iph   = (struct iphdr *)skb->data;

	if (iph->ihl < 5) /* Mangled header, drop. */
		goto out_err;

	if (icmph->type == ICMP_DEST_UNREACH) {
		switch (icmph->code & 15) {
		case ICMP_NET_UNREACH:
		case ICMP_HOST_UNREACH:
		case ICMP_PROT_UNREACH:
		case ICMP_PORT_UNREACH:
			break;
		case ICMP_FRAG_NEEDED:
			if (ipv4_config.no_pmtu_disc) {
				LIMIT_NETDEBUG(KERN_INFO "ICMP: %pI4: fragmentation needed and DF set.\n",
					       &iph->daddr);
			} else {
				info = ip_rt_frag_needed(net, iph,
							 ntohs(icmph->un.frag.mtu),
							 skb->dev);
				if (!info)
					goto out;
			}
			break;
		case ICMP_SR_FAILED:
			LIMIT_NETDEBUG(KERN_INFO "ICMP: %pI4: Source Route Failed.\n",
				       &iph->daddr);
			break;
		default:
			break;
		}
		if (icmph->code > NR_ICMP_UNREACH)
			goto out;
	} else if (icmph->type == ICMP_PARAMETERPROB)
		info = ntohl(icmph->un.gateway) >> 24;

	/*
	 *	Throw it at our lower layers
	 *
	 *	RFC 1122: 3.2.2 MUST extract the protocol ID from the passed
	 *		  header.
	 *	RFC 1122: 3.2.2.1 MUST pass ICMP unreach messages to the
	 *		  transport layer.
	 *	RFC 1122: 3.2.2.2 MUST pass ICMP time expired messages to
	 *		  transport layer.
	 */

	/*
	 *	Check the other end isnt violating RFC 1122. Some routers send
	 *	bogus responses to broadcast frames. If you see this message
	 *	first check your netmask matches at both ends, if it does then
	 *	get the other vendor to fix their kit.
	 */

	if (!net->ipv4.sysctl_icmp_ignore_bogus_error_responses &&
	    inet_addr_type(net, iph->daddr) == RTN_BROADCAST) {
		if (net_ratelimit())
			printk(KERN_WARNING "%pI4 sent an invalid ICMP "
					    "type %u, code %u "
					    "error to a broadcast: %pI4 on %s\n",
			       &ip_hdr(skb)->saddr,
			       icmph->type, icmph->code,
			       &iph->daddr,
			       skb->dev->name);
		goto out;
	}

	/* Checkin full IP header plus 8 bytes of protocol to
	 * avoid additional coding at protocol handlers.
	 */
	if (!pskb_may_pull(skb, iph->ihl * 4 + 8))
		goto out;

	iph = (struct iphdr *)skb->data;
	protocol = iph->protocol;

	/*
	 *	Deliver ICMP message to raw sockets. Pretty useless feature?
	 */
	raw_icmp_error(skb, protocol, info);

	hash = protocol & (MAX_INET_PROTOS - 1);
	rcu_read_lock();
	ipprot = rcu_dereference(inet_protos[hash]);
	if (ipprot && ipprot->err_handler)
		ipprot->err_handler(skb, info);
	rcu_read_unlock();

out:
	return;
out_err:
	ICMP_INC_STATS_BH(net, ICMP_MIB_INERRORS);
	goto out;
}


/*
 *	Handle ICMP_REDIRECT.
 */

static void icmp_redirect(struct sk_buff *skb)
{
	struct iphdr *iph;

	if (skb->len < sizeof(struct iphdr))
		goto out_err;

	/*
	 *	Get the copied header of the packet that caused the redirect
	 */
	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto out;

	iph = (struct iphdr *)skb->data;

	switch (icmp_hdr(skb)->code & 7) {
	case ICMP_REDIR_NET:
	case ICMP_REDIR_NETTOS:
		/*
		 * As per RFC recommendations now handle it as a host redirect.
		 */
	case ICMP_REDIR_HOST:
	case ICMP_REDIR_HOSTTOS:
		ip_rt_redirect(ip_hdr(skb)->saddr, iph->daddr,
			       icmp_hdr(skb)->un.gateway,
			       iph->saddr, skb->dev);
		break;
	}
out:
	return;
out_err:
	ICMP_INC_STATS_BH(dev_net(skb->dev), ICMP_MIB_INERRORS);
	goto out;
}

/*
 *	Handle ICMP_ECHO ("ping") requests.
 *
 *	RFC 1122: 3.2.2.6 MUST have an echo server that answers ICMP echo
 *		  requests.
 *	RFC 1122: 3.2.2.6 Data received in the ICMP_ECHO request MUST be
 *		  included in the reply.
 *	RFC 1812: 4.3.3.6 SHOULD have a config option for silently ignoring
 *		  echo requests, MUST have default=NOT.
 *	See also WRT handling of options once they are done and working.
 */

static void icmp_echo(struct sk_buff *skb)
{
	struct net *net;

	net = dev_net(skb_dst(skb)->dev);
	if (!net->ipv4.sysctl_icmp_echo_ignore_all) {
		struct icmp_bxm icmp_param;

		icmp_param.data.icmph	   = *icmp_hdr(skb);
		icmp_param.data.icmph.type = ICMP_ECHOREPLY;
		icmp_param.skb		   = skb;
		icmp_param.offset	   = 0;
		icmp_param.data_len	   = skb->len;
		icmp_param.head_len	   = sizeof(struct icmphdr);
		icmp_reply(&icmp_param, skb);
	}
}

/*
 *	Handle ICMP Timestamp requests.
 *	RFC 1122: 3.2.2.8 MAY implement ICMP timestamp requests.
 *		  SHOULD be in the kernel for minimum random latency.
 *		  MUST be accurate to a few minutes.
 *		  MUST be updated at least at 15Hz.
 */
static void icmp_timestamp(struct sk_buff *skb)
{
	struct timespec tv;
	struct icmp_bxm icmp_param;
	/*
	 *	Too short.
	 */
	if (skb->len < 4)
		goto out_err;

	/*
	 *	Fill in the current time as ms since midnight UT:
	 */
	getnstimeofday(&tv);
	icmp_param.data.times[1] = htonl((tv.tv_sec % 86400) * MSEC_PER_SEC +
					 tv.tv_nsec / NSEC_PER_MSEC);
	icmp_param.data.times[2] = icmp_param.data.times[1];
	if (skb_copy_bits(skb, 0, &icmp_param.data.times[0], 4))
		BUG();
	icmp_param.data.icmph	   = *icmp_hdr(skb);
	icmp_param.data.icmph.type = ICMP_TIMESTAMPREPLY;
	icmp_param.data.icmph.code = 0;
	icmp_param.skb		   = skb;
	icmp_param.offset	   = 0;
	icmp_param.data_len	   = 0;
	icmp_param.head_len	   = sizeof(struct icmphdr) + 12;
	icmp_reply(&icmp_param, skb);
out:
	return;
out_err:
	ICMP_INC_STATS_BH(dev_net(skb_dst(skb)->dev), ICMP_MIB_INERRORS);
	goto out;
}


/*
 *	Handle ICMP_ADDRESS_MASK requests.  (RFC950)
 *
 * RFC1122 (3.2.2.9).  A host MUST only send replies to
 * ADDRESS_MASK requests if it's been configured as an address mask
 * agent.  Receiving a request doesn't constitute implicit permission to
 * act as one. Of course, implementing this correctly requires (SHOULD)
 * a way to turn the functionality on and off.  Another one for sysctl(),
 * I guess. -- MS
 *
 * RFC1812 (4.3.3.9).	A router MUST implement it.
 *			A router SHOULD have switch turning it on/off.
 *		      	This switch MUST be ON by default.
 *
 * Gratuitous replies, zero-source replies are not implemented,
 * that complies with RFC. DO NOT implement them!!! All the idea
 * of broadcast addrmask replies as specified in RFC950 is broken.
 * The problem is that it is not uncommon to have several prefixes
 * on one physical interface. Moreover, addrmask agent can even be
 * not aware of existing another prefixes.
 * If source is zero, addrmask agent cannot choose correct prefix.
 * Gratuitous mask announcements suffer from the same problem.
 * RFC1812 explains it, but still allows to use ADDRMASK,
 * that is pretty silly. --ANK
 *
 * All these rules are so bizarre, that I removed kernel addrmask
 * support at all. It is wrong, it is obsolete, nobody uses it in
 * any case. --ANK
 *
 * Furthermore you can do it with a usermode address agent program
 * anyway...
 */

static void icmp_address(struct sk_buff *skb)
{
#if 0
	if (net_ratelimit())
		printk(KERN_DEBUG "a guy asks for address mask. Who is it?\n");
#endif
}

/*
 * RFC1812 (4.3.3.9).	A router SHOULD listen all replies, and complain
 *			loudly if an inconsistency is found.
 * called with rcu_read_lock()
 */

static void icmp_address_reply(struct sk_buff *skb)
{
	struct rtable *rt = skb_rtable(skb);
	struct net_device *dev = skb->dev;
	struct in_device *in_dev;
	struct in_ifaddr *ifa;

	if (skb->len < 4 || !(rt->rt_flags&RTCF_DIRECTSRC))
		return;

	in_dev = __in_dev_get_rcu(dev);
	if (!in_dev)
		return;

	if (in_dev->ifa_list &&
	    IN_DEV_LOG_MARTIANS(in_dev) &&
	    IN_DEV_FORWARD(in_dev)) {
		__be32 _mask, *mp;

		mp = skb_header_pointer(skb, 0, sizeof(_mask), &_mask);
		BUG_ON(mp == NULL);
		for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next) {
			if (*mp == ifa->ifa_mask &&
			    inet_ifa_match(rt->rt_src, ifa))
				break;
		}
		if (!ifa && net_ratelimit()) {
			printk(KERN_INFO "Wrong address mask %pI4 from %s/%pI4\n",
			       mp, dev->name, &rt->rt_src);
		}
	}
}

static void icmp_discard(struct sk_buff *skb)
{
}

/*
 *	Deal with incoming ICMP packets.
 */
int icmp_rcv(struct sk_buff *skb)
{
	struct icmphdr *icmph;
	struct rtable *rt = skb_rtable(skb);
	struct net *net = dev_net(rt->dst.dev);

	if (!xfrm4_policy_check(NULL, XFRM_POLICY_IN, skb)) {
		struct sec_path *sp = skb_sec_path(skb);
		int nh;

		if (!(sp && sp->xvec[sp->len - 1]->props.flags &
				 XFRM_STATE_ICMP))
			goto drop;

		if (!pskb_may_pull(skb, sizeof(*icmph) + sizeof(struct iphdr)))
			goto drop;

		nh = skb_network_offset(skb);
		skb_set_network_header(skb, sizeof(*icmph));

		if (!xfrm4_policy_check_reverse(NULL, XFRM_POLICY_IN, skb))
			goto drop;

		skb_set_network_header(skb, nh);
	}

	ICMP_INC_STATS_BH(net, ICMP_MIB_INMSGS);

	switch (skb->ip_summed) {
	case CHECKSUM_COMPLETE:
		if (!csum_fold(skb->csum))
			break;
		/* fall through */
	case CHECKSUM_NONE:
		skb->csum = 0;
		if (__skb_checksum_complete(skb))
			goto error;
	}

	if (!pskb_pull(skb, sizeof(*icmph)))
		goto error;

	icmph = icmp_hdr(skb);

	ICMPMSGIN_INC_STATS_BH(net, icmph->type);
	/*
	 *	18 is the highest 'known' ICMP type. Anything else is a mystery
	 *
	 *	RFC 1122: 3.2.2  Unknown ICMP messages types MUST be silently
	 *		  discarded.
	 */
	if (icmph->type > NR_ICMP_TYPES)
		goto error;


	/*
	 *	Parse the ICMP message
	 */

	if (rt->rt_flags & (RTCF_BROADCAST | RTCF_MULTICAST)) {
		/*
		 *	RFC 1122: 3.2.2.6 An ICMP_ECHO to broadcast MAY be
		 *	  silently ignored (we let user decide with a sysctl).
		 *	RFC 1122: 3.2.2.8 An ICMP_TIMESTAMP MAY be silently
		 *	  discarded if to broadcast/multicast.
		 */
		if ((icmph->type == ICMP_ECHO ||
		     icmph->type == ICMP_TIMESTAMP) &&
		    net->ipv4.sysctl_icmp_echo_ignore_broadcasts) {
			goto error;
		}
		if (icmph->type != ICMP_ECHO &&
		    icmph->type != ICMP_TIMESTAMP &&
		    icmph->type != ICMP_ADDRESS &&
		    icmph->type != ICMP_ADDRESSREPLY) {
			goto error;
		}
	}

	icmp_pointers[icmph->type].handler(skb);

drop:
	kfree_skb(skb);
	return 0;
error:
	ICMP_INC_STATS_BH(net, ICMP_MIB_INERRORS);
	goto drop;
}

/*
 *	This table is the definition of how we handle ICMP.
 */
static const struct icmp_control icmp_pointers[NR_ICMP_TYPES + 1] = {
	[ICMP_ECHOREPLY] = {
		.handler = icmp_discard,
	},
	[1] = {
		.handler = icmp_discard,
		.error = 1,
	},
	[2] = {
		.handler = icmp_discard,
		.error = 1,
	},
	[ICMP_DEST_UNREACH] = {
		.handler = icmp_unreach,
		.error = 1,
	},
	[ICMP_SOURCE_QUENCH] = {
		.handler = icmp_unreach,
		.error = 1,
	},
	[ICMP_REDIRECT] = {
		.handler = icmp_redirect,
		.error = 1,
	},
	[6] = {
		.handler = icmp_discard,
		.error = 1,
	},
	[7] = {
		.handler = icmp_discard,
		.error = 1,
	},
	[ICMP_ECHO] = {
		.handler = icmp_echo,
	},
	[9] = {
		.handler = icmp_discard,
		.error = 1,
	},
	[10] = {
		.handler = icmp_discard,
		.error = 1,
	},
	[ICMP_TIME_EXCEEDED] = {
		.handler = icmp_unreach,
		.error = 1,
	},
	[ICMP_PARAMETERPROB] = {
		.handler = icmp_unreach,
		.error = 1,
	},
	[ICMP_TIMESTAMP] = {
		.handler = icmp_timestamp,
	},
	[ICMP_TIMESTAMPREPLY] = {
		.handler = icmp_discard,
	},
	[ICMP_INFO_REQUEST] = {
		.handler = icmp_discard,
	},
	[ICMP_INFO_REPLY] = {
		.handler = icmp_discard,
	},
	[ICMP_ADDRESS] = {
		.handler = icmp_address,
	},
	[ICMP_ADDRESSREPLY] = {
		.handler = icmp_address_reply,
	},
};

static void __net_exit icmp_sk_exit(struct net *net)
{
	int i;

	for_each_possible_cpu(i)
		inet_ctl_sock_destroy(net->ipv4.icmp_sk[i]);
	kfree(net->ipv4.icmp_sk);
	net->ipv4.icmp_sk = NULL;
}

static int __net_init icmp_sk_init(struct net *net)
{
	int i, err;

	net->ipv4.icmp_sk =
		kzalloc(nr_cpu_ids * sizeof(struct sock *), GFP_KERNEL);
	if (net->ipv4.icmp_sk == NULL)
		return -ENOMEM;

	for_each_possible_cpu(i) {
		struct sock *sk;

		err = inet_ctl_sock_create(&sk, PF_INET,
					   SOCK_RAW, IPPROTO_ICMP, net);
		if (err < 0)
			goto fail;

		net->ipv4.icmp_sk[i] = sk;

		/* Enough space for 2 64K ICMP packets, including
		 * sk_buff struct overhead.
		 */
		sk->sk_sndbuf =
			(2 * ((64 * 1024) + sizeof(struct sk_buff)));

		/*
		 * Speedup sock_wfree()
		 */
		sock_set_flag(sk, SOCK_USE_WRITE_QUEUE);
		inet_sk(sk)->pmtudisc = IP_PMTUDISC_DONT;
	}

	/* Control parameters for ECHO replies. */
	net->ipv4.sysctl_icmp_echo_ignore_all = 0;
	net->ipv4.sysctl_icmp_echo_ignore_broadcasts = 1;

	/* Control parameter - ignore bogus broadcast responses? */
	net->ipv4.sysctl_icmp_ignore_bogus_error_responses = 1;

	/*
	 * 	Configurable global rate limit.
	 *
	 *	ratelimit defines tokens/packet consumed for dst->rate_token
	 *	bucket ratemask defines which icmp types are ratelimited by
	 *	setting	it's bit position.
	 *
	 *	default:
	 *	dest unreachable (3), source quench (4),
	 *	time exceeded (11), parameter problem (12)
	 */

	net->ipv4.sysctl_icmp_ratelimit = 1 * HZ;
	net->ipv4.sysctl_icmp_ratemask = 0x1818;
	net->ipv4.sysctl_icmp_errors_use_inbound_ifaddr = 0;

	return 0;

fail:
	for_each_possible_cpu(i)
		inet_ctl_sock_destroy(net->ipv4.icmp_sk[i]);
	kfree(net->ipv4.icmp_sk);
	return err;
}

static struct pernet_operations __net_initdata icmp_sk_ops = {
       .init = icmp_sk_init,
       .exit = icmp_sk_exit,
};

int __init icmp_init(void)
{
	return register_pernet_subsys(&icmp_sk_ops);
}
