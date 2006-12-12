/*
 *	NET3:	Implementation of the ICMP protocol layer.
 *
 *		Alan Cox, <alan@redhat.com>
 *
 *	Version: $Id: icmp.c,v 1.85 2002/02/01 22:01:03 davem Exp $
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

/*
 *	Statistics
 */
DEFINE_SNMP_STAT(struct icmp_mib, icmp_statistics) __read_mostly;

/* An array of errno for error messages from dest unreach. */
/* RFC 1122: 3.2.2.1 States that NET_UNREACH, HOST_UNREACH and SR_FAILED MUST be considered 'transient errs'. */

struct icmp_err icmp_err_convert[] = {
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

/* Control parameters for ECHO replies. */
int sysctl_icmp_echo_ignore_all __read_mostly;
int sysctl_icmp_echo_ignore_broadcasts __read_mostly = 1;

/* Control parameter - ignore bogus broadcast responses? */
int sysctl_icmp_ignore_bogus_error_responses __read_mostly = 1;

/*
 * 	Configurable global rate limit.
 *
 *	ratelimit defines tokens/packet consumed for dst->rate_token bucket
 *	ratemask defines which icmp types are ratelimited by setting
 * 	it's bit position.
 *
 *	default:
 *	dest unreachable (3), source quench (4),
 *	time exceeded (11), parameter problem (12)
 */

int sysctl_icmp_ratelimit __read_mostly = 1 * HZ;
int sysctl_icmp_ratemask __read_mostly = 0x1818;
int sysctl_icmp_errors_use_inbound_ifaddr __read_mostly;

/*
 *	ICMP control array. This specifies what to do with each ICMP.
 */

struct icmp_control {
	int output_entry;	/* Field for increment on output */
	int input_entry;	/* Field for increment on input */
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
static DEFINE_PER_CPU(struct socket *, __icmp_socket) = NULL;
#define icmp_socket	__get_cpu_var(__icmp_socket)

static __inline__ int icmp_xmit_lock(void)
{
	local_bh_disable();

	if (unlikely(!spin_trylock(&icmp_socket->sk->sk_lock.slock))) {
		/* This can happen if the output path signals a
		 * dst_link_failure() for an outgoing ICMP packet.
		 */
		local_bh_enable();
		return 1;
	}
	return 0;
}

static void icmp_xmit_unlock(void)
{
	spin_unlock_bh(&icmp_socket->sk->sk_lock.slock);
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
	unsigned long now;
	int rc = 0;

	now = jiffies;
	dst->rate_tokens += now - dst->rate_last;
	dst->rate_last = now;
	if (dst->rate_tokens > XRLIM_BURST_FACTOR * timeout)
		dst->rate_tokens = XRLIM_BURST_FACTOR * timeout;
	if (dst->rate_tokens >= timeout) {
		dst->rate_tokens -= timeout;
		rc = 1;
	}
	return rc;
}

static inline int icmpv4_xrlim_allow(struct rtable *rt, int type, int code)
{
	struct dst_entry *dst = &rt->u.dst;
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
	if ((1 << type) & sysctl_icmp_ratemask)
		rc = xrlim_allow(dst, sysctl_icmp_ratelimit);
out:
	return rc;
}

/*
 *	Maintain the counters used in the SNMP statistics for outgoing ICMP
 */
static void icmp_out_count(int type)
{
	if (type <= NR_ICMP_TYPES) {
		ICMP_INC_STATS(icmp_pointers[type].output_entry);
		ICMP_INC_STATS(ICMP_MIB_OUTMSGS);
	}
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
			    struct ipcm_cookie *ipc, struct rtable *rt)
{
	struct sk_buff *skb;

	if (ip_append_data(icmp_socket->sk, icmp_glue_bits, icmp_param,
		           icmp_param->data_len+icmp_param->head_len,
		           icmp_param->head_len,
		           ipc, rt, MSG_DONTWAIT) < 0)
		ip_flush_pending_frames(icmp_socket->sk);
	else if ((skb = skb_peek(&icmp_socket->sk->sk_write_queue)) != NULL) {
		struct icmphdr *icmph = skb->h.icmph;
		__wsum csum = 0;
		struct sk_buff *skb1;

		skb_queue_walk(&icmp_socket->sk->sk_write_queue, skb1) {
			csum = csum_add(csum, skb1->csum);
		}
		csum = csum_partial_copy_nocheck((void *)&icmp_param->data,
						 (char *)icmph,
						 icmp_param->head_len, csum);
		icmph->checksum = csum_fold(csum);
		skb->ip_summed = CHECKSUM_NONE;
		ip_push_pending_frames(icmp_socket->sk);
	}
}

/*
 *	Driving logic for building and sending ICMP messages.
 */

static void icmp_reply(struct icmp_bxm *icmp_param, struct sk_buff *skb)
{
	struct sock *sk = icmp_socket->sk;
	struct inet_sock *inet = inet_sk(sk);
	struct ipcm_cookie ipc;
	struct rtable *rt = (struct rtable *)skb->dst;
	__be32 daddr;

	if (ip_options_echo(&icmp_param->replyopts, skb))
		return;

	if (icmp_xmit_lock())
		return;

	icmp_param->data.icmph.checksum = 0;
	icmp_out_count(icmp_param->data.icmph.type);

	inet->tos = skb->nh.iph->tos;
	daddr = ipc.addr = rt->rt_src;
	ipc.opt = NULL;
	if (icmp_param->replyopts.optlen) {
		ipc.opt = &icmp_param->replyopts;
		if (ipc.opt->srr)
			daddr = icmp_param->replyopts.faddr;
	}
	{
		struct flowi fl = { .nl_u = { .ip4_u =
					      { .daddr = daddr,
						.saddr = rt->rt_spec_dst,
						.tos = RT_TOS(skb->nh.iph->tos) } },
				    .proto = IPPROTO_ICMP };
		security_skb_classify_flow(skb, &fl);
		if (ip_route_output_key(&rt, &fl))
			goto out_unlock;
	}
	if (icmpv4_xrlim_allow(rt, icmp_param->data.icmph.type,
			       icmp_param->data.icmph.code))
		icmp_push_reply(icmp_param, &ipc, rt);
	ip_rt_put(rt);
out_unlock:
	icmp_xmit_unlock();
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
	struct rtable *rt = (struct rtable *)skb_in->dst;
	struct ipcm_cookie ipc;
	__be32 saddr;
	u8  tos;

	if (!rt)
		goto out;

	/*
	 *	Find the original header. It is expected to be valid, of course.
	 *	Check this, icmp_send is called from the most obscure devices
	 *	sometimes.
	 */
	iph = skb_in->nh.iph;

	if ((u8 *)iph < skb_in->head || (u8 *)(iph + 1) > skb_in->tail)
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
						 skb_in->nh.raw +
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

	if (icmp_xmit_lock())
		return;

	/*
	 *	Construct source address and options.
	 */

	saddr = iph->daddr;
	if (!(rt->rt_flags & RTCF_LOCAL)) {
		if (sysctl_icmp_errors_use_inbound_ifaddr)
			saddr = inet_select_addr(skb_in->dev, 0, RT_SCOPE_LINK);
		else
			saddr = 0;
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
	icmp_param.offset = skb_in->nh.raw - skb_in->data;
	icmp_out_count(icmp_param.data.icmph.type);
	inet_sk(icmp_socket->sk)->tos = tos;
	ipc.addr = iph->saddr;
	ipc.opt = &icmp_param.replyopts;

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
		security_skb_classify_flow(skb_in, &fl);
		if (ip_route_output_key(&rt, &fl))
			goto out_unlock;
	}

	if (!icmpv4_xrlim_allow(rt, type, code))
		goto ende;

	/* RFC says return as much as we can without exceeding 576 bytes. */

	room = dst_mtu(&rt->u.dst);
	if (room > 576)
		room = 576;
	room -= sizeof(struct iphdr) + icmp_param.replyopts.optlen;
	room -= sizeof(struct icmphdr);

	icmp_param.data_len = skb_in->len - icmp_param.offset;
	if (icmp_param.data_len > room)
		icmp_param.data_len = room;
	icmp_param.head_len = sizeof(struct icmphdr);

	icmp_push_reply(&icmp_param, &ipc, rt);
ende:
	ip_rt_put(rt);
out_unlock:
	icmp_xmit_unlock();
out:;
}


/*
 *	Handle ICMP_DEST_UNREACH, ICMP_TIME_EXCEED, and ICMP_QUENCH.
 */

static void icmp_unreach(struct sk_buff *skb)
{
	struct iphdr *iph;
	struct icmphdr *icmph;
	int hash, protocol;
	struct net_protocol *ipprot;
	struct sock *raw_sk;
	u32 info = 0;

	/*
	 *	Incomplete header ?
	 * 	Only checks for the IP header, there should be an
	 *	additional check for longer headers in upper levels.
	 */

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto out_err;

	icmph = skb->h.icmph;
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
				LIMIT_NETDEBUG(KERN_INFO "ICMP: %u.%u.%u.%u: "
							 "fragmentation needed "
							 "and DF set.\n",
					       NIPQUAD(iph->daddr));
			} else {
				info = ip_rt_frag_needed(iph,
						     ntohs(icmph->un.frag.mtu));
				if (!info)
					goto out;
			}
			break;
		case ICMP_SR_FAILED:
			LIMIT_NETDEBUG(KERN_INFO "ICMP: %u.%u.%u.%u: Source "
						 "Route Failed.\n",
				       NIPQUAD(iph->daddr));
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

	if (!sysctl_icmp_ignore_bogus_error_responses &&
	    inet_addr_type(iph->daddr) == RTN_BROADCAST) {
		if (net_ratelimit())
			printk(KERN_WARNING "%u.%u.%u.%u sent an invalid ICMP "
					    "type %u, code %u "
					    "error to a broadcast: %u.%u.%u.%u on %s\n",
			       NIPQUAD(skb->nh.iph->saddr),
			       icmph->type, icmph->code,
			       NIPQUAD(iph->daddr),
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

	/* Note: See raw.c and net/raw.h, RAWV4_HTABLE_SIZE==MAX_INET_PROTOS */
	hash = protocol & (MAX_INET_PROTOS - 1);
	read_lock(&raw_v4_lock);
	if ((raw_sk = sk_head(&raw_v4_htable[hash])) != NULL) {
		while ((raw_sk = __raw_v4_lookup(raw_sk, protocol, iph->daddr,
						 iph->saddr,
						 skb->dev->ifindex)) != NULL) {
			raw_err(raw_sk, skb, info);
			raw_sk = sk_next(raw_sk);
			iph = (struct iphdr *)skb->data;
		}
	}
	read_unlock(&raw_v4_lock);

	rcu_read_lock();
	ipprot = rcu_dereference(inet_protos[hash]);
	if (ipprot && ipprot->err_handler)
		ipprot->err_handler(skb, info);
	rcu_read_unlock();

out:
	return;
out_err:
	ICMP_INC_STATS_BH(ICMP_MIB_INERRORS);
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

	switch (skb->h.icmph->code & 7) {
	case ICMP_REDIR_NET:
	case ICMP_REDIR_NETTOS:
		/*
		 * As per RFC recommendations now handle it as a host redirect.
		 */
	case ICMP_REDIR_HOST:
	case ICMP_REDIR_HOSTTOS:
		ip_rt_redirect(skb->nh.iph->saddr, iph->daddr,
			       skb->h.icmph->un.gateway,
			       iph->saddr, skb->dev);
		break;
  	}
out:
	return;
out_err:
	ICMP_INC_STATS_BH(ICMP_MIB_INERRORS);
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
	if (!sysctl_icmp_echo_ignore_all) {
		struct icmp_bxm icmp_param;

		icmp_param.data.icmph	   = *skb->h.icmph;
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
	struct timeval tv;
	struct icmp_bxm icmp_param;
	/*
	 *	Too short.
	 */
	if (skb->len < 4)
		goto out_err;

	/*
	 *	Fill in the current time as ms since midnight UT:
	 */
	do_gettimeofday(&tv);
	icmp_param.data.times[1] = htonl((tv.tv_sec % 86400) * 1000 +
					 tv.tv_usec / 1000);
	icmp_param.data.times[2] = icmp_param.data.times[1];
	if (skb_copy_bits(skb, 0, &icmp_param.data.times[0], 4))
		BUG();
	icmp_param.data.icmph	   = *skb->h.icmph;
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
	ICMP_INC_STATS_BH(ICMP_MIB_INERRORS);
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
 */

static void icmp_address_reply(struct sk_buff *skb)
{
	struct rtable *rt = (struct rtable *)skb->dst;
	struct net_device *dev = skb->dev;
	struct in_device *in_dev;
	struct in_ifaddr *ifa;

	if (skb->len < 4 || !(rt->rt_flags&RTCF_DIRECTSRC))
		goto out;

	in_dev = in_dev_get(dev);
	if (!in_dev)
		goto out;
	rcu_read_lock();
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
			printk(KERN_INFO "Wrong address mask %u.%u.%u.%u from "
					 "%s/%u.%u.%u.%u\n",
			       NIPQUAD(*mp), dev->name, NIPQUAD(rt->rt_src));
		}
	}
	rcu_read_unlock();
	in_dev_put(in_dev);
out:;
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
	struct rtable *rt = (struct rtable *)skb->dst;

	ICMP_INC_STATS_BH(ICMP_MIB_INMSGS);

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

	if (!pskb_pull(skb, sizeof(struct icmphdr)))
		goto error;

	icmph = skb->h.icmph;

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
		    sysctl_icmp_echo_ignore_broadcasts) {
			goto error;
		}
		if (icmph->type != ICMP_ECHO &&
		    icmph->type != ICMP_TIMESTAMP &&
		    icmph->type != ICMP_ADDRESS &&
		    icmph->type != ICMP_ADDRESSREPLY) {
			goto error;
  		}
	}

	ICMP_INC_STATS_BH(icmp_pointers[icmph->type].input_entry);
	icmp_pointers[icmph->type].handler(skb);

drop:
	kfree_skb(skb);
	return 0;
error:
	ICMP_INC_STATS_BH(ICMP_MIB_INERRORS);
	goto drop;
}

/*
 *	This table is the definition of how we handle ICMP.
 */
static const struct icmp_control icmp_pointers[NR_ICMP_TYPES + 1] = {
	[ICMP_ECHOREPLY] = {
		.output_entry = ICMP_MIB_OUTECHOREPS,
		.input_entry = ICMP_MIB_INECHOREPS,
		.handler = icmp_discard,
	},
	[1] = {
		.output_entry = ICMP_MIB_DUMMY,
		.input_entry = ICMP_MIB_INERRORS,
		.handler = icmp_discard,
		.error = 1,
	},
	[2] = {
		.output_entry = ICMP_MIB_DUMMY,
		.input_entry = ICMP_MIB_INERRORS,
		.handler = icmp_discard,
		.error = 1,
	},
	[ICMP_DEST_UNREACH] = {
		.output_entry = ICMP_MIB_OUTDESTUNREACHS,
		.input_entry = ICMP_MIB_INDESTUNREACHS,
		.handler = icmp_unreach,
		.error = 1,
	},
	[ICMP_SOURCE_QUENCH] = {
		.output_entry = ICMP_MIB_OUTSRCQUENCHS,
		.input_entry = ICMP_MIB_INSRCQUENCHS,
		.handler = icmp_unreach,
		.error = 1,
	},
	[ICMP_REDIRECT] = {
		.output_entry = ICMP_MIB_OUTREDIRECTS,
		.input_entry = ICMP_MIB_INREDIRECTS,
		.handler = icmp_redirect,
		.error = 1,
	},
	[6] = {
		.output_entry = ICMP_MIB_DUMMY,
		.input_entry = ICMP_MIB_INERRORS,
		.handler = icmp_discard,
		.error = 1,
	},
	[7] = {
		.output_entry = ICMP_MIB_DUMMY,
		.input_entry = ICMP_MIB_INERRORS,
		.handler = icmp_discard,
		.error = 1,
	},
	[ICMP_ECHO] = {
		.output_entry = ICMP_MIB_OUTECHOS,
		.input_entry = ICMP_MIB_INECHOS,
		.handler = icmp_echo,
	},
	[9] = {
		.output_entry = ICMP_MIB_DUMMY,
		.input_entry = ICMP_MIB_INERRORS,
		.handler = icmp_discard,
		.error = 1,
	},
	[10] = {
		.output_entry = ICMP_MIB_DUMMY,
		.input_entry = ICMP_MIB_INERRORS,
		.handler = icmp_discard,
		.error = 1,
	},
	[ICMP_TIME_EXCEEDED] = {
		.output_entry = ICMP_MIB_OUTTIMEEXCDS,
		.input_entry = ICMP_MIB_INTIMEEXCDS,
		.handler = icmp_unreach,
		.error = 1,
	},
	[ICMP_PARAMETERPROB] = {
		.output_entry = ICMP_MIB_OUTPARMPROBS,
		.input_entry = ICMP_MIB_INPARMPROBS,
		.handler = icmp_unreach,
		.error = 1,
	},
	[ICMP_TIMESTAMP] = {
		.output_entry = ICMP_MIB_OUTTIMESTAMPS,
		.input_entry = ICMP_MIB_INTIMESTAMPS,
		.handler = icmp_timestamp,
	},
	[ICMP_TIMESTAMPREPLY] = {
		.output_entry = ICMP_MIB_OUTTIMESTAMPREPS,
		.input_entry = ICMP_MIB_INTIMESTAMPREPS,
		.handler = icmp_discard,
	},
	[ICMP_INFO_REQUEST] = {
		.output_entry = ICMP_MIB_DUMMY,
		.input_entry = ICMP_MIB_DUMMY,
		.handler = icmp_discard,
	},
 	[ICMP_INFO_REPLY] = {
		.output_entry = ICMP_MIB_DUMMY,
		.input_entry = ICMP_MIB_DUMMY,
		.handler = icmp_discard,
	},
	[ICMP_ADDRESS] = {
		.output_entry = ICMP_MIB_OUTADDRMASKS,
		.input_entry = ICMP_MIB_INADDRMASKS,
		.handler = icmp_address,
	},
	[ICMP_ADDRESSREPLY] = {
		.output_entry = ICMP_MIB_OUTADDRMASKREPS,
		.input_entry = ICMP_MIB_INADDRMASKREPS,
		.handler = icmp_address_reply,
	},
};

void __init icmp_init(struct net_proto_family *ops)
{
	struct inet_sock *inet;
	int i;

	for_each_possible_cpu(i) {
		int err;

		err = sock_create_kern(PF_INET, SOCK_RAW, IPPROTO_ICMP,
				       &per_cpu(__icmp_socket, i));

		if (err < 0)
			panic("Failed to create the ICMP control socket.\n");

		per_cpu(__icmp_socket, i)->sk->sk_allocation = GFP_ATOMIC;

		/* Enough space for 2 64K ICMP packets, including
		 * sk_buff struct overhead.
		 */
		per_cpu(__icmp_socket, i)->sk->sk_sndbuf =
			(2 * ((64 * 1024) + sizeof(struct sk_buff)));

		inet = inet_sk(per_cpu(__icmp_socket, i)->sk);
		inet->uc_ttl = -1;
		inet->pmtudisc = IP_PMTUDISC_DONT;

		/* Unhash it so that IP input processing does not even
		 * see it, we do not wish this socket to see incoming
		 * packets.
		 */
		per_cpu(__icmp_socket, i)->sk->sk_prot->unhash(per_cpu(__icmp_socket, i)->sk);
	}
}

EXPORT_SYMBOL(icmp_err_convert);
EXPORT_SYMBOL(icmp_send);
EXPORT_SYMBOL(icmp_statistics);
EXPORT_SYMBOL(xrlim_allow);
