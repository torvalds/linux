/*
 *	UDP over IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Based on linux/ipv4/udp.c
 *
 *	$Id: udp.c,v 1.65 2002/02/01 22:01:04 davem Exp $
 *
 *	Fixes:
 *	Hideaki YOSHIFUJI	:	sin6_scope_id support
 *	YOSHIFUJI Hideaki @USAGI and:	Support IPV6_V6ONLY socket option, which
 *	Alexey Kuznetsov		allow both IPv4 and IPv6 sockets to bind
 *					a single port at the same time.
 *      Kazunori MIYAZAWA @USAGI:       change process style to use ip6_append_data
 *      YOSHIFUJI Hideaki @USAGI:	convert /proc/net/udp6 to seq_file.
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <asm/uaccess.h>

#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/raw.h>
#include <net/tcp_states.h>
#include <net/ip6_checksum.h>
#include <net/xfrm.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "udp_impl.h"

DEFINE_SNMP_STAT(struct udp_mib, udp_stats_in6) __read_mostly;

static inline int udp_v6_get_port(struct sock *sk, unsigned short snum)
{
	return udp_get_port(sk, snum, ipv6_rcv_saddr_equal);
}

static struct sock *__udp6_lib_lookup(struct in6_addr *saddr, __be16 sport,
				      struct in6_addr *daddr, __be16 dport,
				      int dif, struct hlist_head udptable[])
{
	struct sock *sk, *result = NULL;
	struct hlist_node *node;
	unsigned short hnum = ntohs(dport);
	int badness = -1;

 	read_lock(&udp_hash_lock);
	sk_for_each(sk, node, &udptable[hnum & (UDP_HTABLE_SIZE - 1)]) {
		struct inet_sock *inet = inet_sk(sk);

		if (inet->num == hnum && sk->sk_family == PF_INET6) {
			struct ipv6_pinfo *np = inet6_sk(sk);
			int score = 0;
			if (inet->dport) {
				if (inet->dport != sport)
					continue;
				score++;
			}
			if (!ipv6_addr_any(&np->rcv_saddr)) {
				if (!ipv6_addr_equal(&np->rcv_saddr, daddr))
					continue;
				score++;
			}
			if (!ipv6_addr_any(&np->daddr)) {
				if (!ipv6_addr_equal(&np->daddr, saddr))
					continue;
				score++;
			}
			if (sk->sk_bound_dev_if) {
				if (sk->sk_bound_dev_if != dif)
					continue;
				score++;
			}
			if(score == 4) {
				result = sk;
				break;
			} else if(score > badness) {
				result = sk;
				badness = score;
			}
		}
	}
	if (result)
		sock_hold(result);
 	read_unlock(&udp_hash_lock);
	return result;
}

/*
 * 	This should be easy, if there is something there we
 * 	return it, otherwise we block.
 */

int udpv6_recvmsg(struct kiocb *iocb, struct sock *sk,
		  struct msghdr *msg, size_t len,
		  int noblock, int flags, int *addr_len)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
  	struct sk_buff *skb;
	size_t copied;
	int err, copy_only, is_udplite = IS_UDPLITE(sk);

  	if (addr_len)
  		*addr_len=sizeof(struct sockaddr_in6);
  
	if (flags & MSG_ERRQUEUE)
		return ipv6_recv_error(sk, msg, len);

try_again:
	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;

 	copied = skb->len - sizeof(struct udphdr);
  	if (copied > len) {
  		copied = len;
  		msg->msg_flags |= MSG_TRUNC;
  	}

	/*
	 * 	Decide whether to checksum and/or copy data.
	 */
	copy_only = (skb->ip_summed==CHECKSUM_UNNECESSARY);

	if (is_udplite  ||  (!copy_only  &&  msg->msg_flags&MSG_TRUNC)) {
		if (__udp_lib_checksum_complete(skb))
			goto csum_copy_err;
		copy_only = 1;
	}

	if (copy_only)
		err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr),
					      msg->msg_iov, copied       );
	else {
		err = skb_copy_and_csum_datagram_iovec(skb, sizeof(struct udphdr), msg->msg_iov);
		if (err == -EINVAL)
			goto csum_copy_err;
	}
	if (err)
		goto out_free;

	sock_recv_timestamp(msg, sk, skb);

	/* Copy the address. */
	if (msg->msg_name) {
		struct sockaddr_in6 *sin6;
	  
		sin6 = (struct sockaddr_in6 *) msg->msg_name;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = skb->h.uh->source;
		sin6->sin6_flowinfo = 0;
		sin6->sin6_scope_id = 0;

		if (skb->protocol == htons(ETH_P_IP))
			ipv6_addr_set(&sin6->sin6_addr, 0, 0,
				      htonl(0xffff), skb->nh.iph->saddr);
		else {
			ipv6_addr_copy(&sin6->sin6_addr, &skb->nh.ipv6h->saddr);
			if (ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL)
				sin6->sin6_scope_id = IP6CB(skb)->iif;
		}

	}
	if (skb->protocol == htons(ETH_P_IP)) {
		if (inet->cmsg_flags)
			ip_cmsg_recv(msg, skb);
	} else {
		if (np->rxopt.all)
			datagram_recv_ctl(sk, msg, skb);
  	}

	err = copied;
	if (flags & MSG_TRUNC)
		err = skb->len - sizeof(struct udphdr);

out_free:
	skb_free_datagram(sk, skb);
out:
	return err;

csum_copy_err:
	skb_kill_datagram(sk, skb, flags);

	if (flags & MSG_DONTWAIT) {
		UDP6_INC_STATS_USER(UDP_MIB_INERRORS, is_udplite);
		return -EAGAIN;
	}
	goto try_again;
}

void __udp6_lib_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		    int type, int code, int offset, __be32 info,
		    struct hlist_head udptable[]                    )
{
	struct ipv6_pinfo *np;
	struct ipv6hdr *hdr = (struct ipv6hdr*)skb->data;
	struct in6_addr *saddr = &hdr->saddr;
	struct in6_addr *daddr = &hdr->daddr;
	struct udphdr *uh = (struct udphdr*)(skb->data+offset);
	struct sock *sk;
	int err;

	sk = __udp6_lib_lookup(daddr, uh->dest,
			       saddr, uh->source, inet6_iif(skb), udptable);
	if (sk == NULL)
		return;

	np = inet6_sk(sk);

	if (!icmpv6_err_convert(type, code, &err) && !np->recverr)
		goto out;

	if (sk->sk_state != TCP_ESTABLISHED && !np->recverr)
		goto out;

	if (np->recverr)
		ipv6_icmp_error(sk, skb, err, uh->dest, ntohl(info), (u8 *)(uh+1));

	sk->sk_err = err;
	sk->sk_error_report(sk);
out:
	sock_put(sk);
}

static __inline__ void udpv6_err(struct sk_buff *skb,
				 struct inet6_skb_parm *opt, int type,
				 int code, int offset, __u32 info     )
{
	return __udp6_lib_err(skb, opt, type, code, offset, info, udp_hash);
}

int udpv6_queue_rcv_skb(struct sock * sk, struct sk_buff *skb)
{
	struct udp_sock *up = udp_sk(sk);
	int rc;

	if (!xfrm6_policy_check(sk, XFRM_POLICY_IN, skb))
		goto drop;

	/*
	 * UDP-Lite specific tests, ignored on UDP sockets (see net/ipv4/udp.c).
	 */
	if ((up->pcflag & UDPLITE_RECV_CC)  &&  UDP_SKB_CB(skb)->partial_cov) {

		if (up->pcrlen == 0) {          /* full coverage was set  */
			LIMIT_NETDEBUG(KERN_WARNING "UDPLITE6: partial coverage"
				" %d while full coverage %d requested\n",
				UDP_SKB_CB(skb)->cscov, skb->len);
			goto drop;
		}
		if (UDP_SKB_CB(skb)->cscov  <  up->pcrlen) {
			LIMIT_NETDEBUG(KERN_WARNING "UDPLITE6: coverage %d "
						    "too small, need min %d\n",
				       UDP_SKB_CB(skb)->cscov, up->pcrlen);
			goto drop;
		}
	}

	if (udp_lib_checksum_complete(skb))
		goto drop;

	if ((rc = sock_queue_rcv_skb(sk,skb)) < 0) {
		/* Note that an ENOMEM error is charged twice */
		if (rc == -ENOMEM)
			UDP6_INC_STATS_BH(UDP_MIB_RCVBUFERRORS, up->pcflag);
		goto drop;
	}
	UDP6_INC_STATS_BH(UDP_MIB_INDATAGRAMS, up->pcflag);
	return 0;
drop:
	UDP6_INC_STATS_BH(UDP_MIB_INERRORS, up->pcflag);
	kfree_skb(skb);
	return -1;
}

static struct sock *udp_v6_mcast_next(struct sock *sk,
				      __be16 loc_port, struct in6_addr *loc_addr,
				      __be16 rmt_port, struct in6_addr *rmt_addr,
				      int dif)
{
	struct hlist_node *node;
	struct sock *s = sk;
	unsigned short num = ntohs(loc_port);

	sk_for_each_from(s, node) {
		struct inet_sock *inet = inet_sk(s);

		if (inet->num == num && s->sk_family == PF_INET6) {
			struct ipv6_pinfo *np = inet6_sk(s);
			if (inet->dport) {
				if (inet->dport != rmt_port)
					continue;
			}
			if (!ipv6_addr_any(&np->daddr) &&
			    !ipv6_addr_equal(&np->daddr, rmt_addr))
				continue;

			if (s->sk_bound_dev_if && s->sk_bound_dev_if != dif)
				continue;

			if (!ipv6_addr_any(&np->rcv_saddr)) {
				if (!ipv6_addr_equal(&np->rcv_saddr, loc_addr))
					continue;
			}
			if(!inet6_mc_check(s, loc_addr, rmt_addr))
				continue;
			return s;
		}
	}
	return NULL;
}

/*
 * Note: called only from the BH handler context,
 * so we don't need to lock the hashes.
 */
static int __udp6_lib_mcast_deliver(struct sk_buff *skb, struct in6_addr *saddr,
		           struct in6_addr *daddr, struct hlist_head udptable[])
{
	struct sock *sk, *sk2;
	const struct udphdr *uh = skb->h.uh;
	int dif;

	read_lock(&udp_hash_lock);
	sk = sk_head(&udptable[ntohs(uh->dest) & (UDP_HTABLE_SIZE - 1)]);
	dif = inet6_iif(skb);
	sk = udp_v6_mcast_next(sk, uh->dest, daddr, uh->source, saddr, dif);
	if (!sk) {
		kfree_skb(skb);
		goto out;
	}

	sk2 = sk;
	while ((sk2 = udp_v6_mcast_next(sk_next(sk2), uh->dest, daddr,
					uh->source, saddr, dif))) {
		struct sk_buff *buff = skb_clone(skb, GFP_ATOMIC);
		if (buff)
			udpv6_queue_rcv_skb(sk2, buff);
	}
	udpv6_queue_rcv_skb(sk, skb);
out:
	read_unlock(&udp_hash_lock);
	return 0;
}

static inline int udp6_csum_init(struct sk_buff *skb, struct udphdr *uh)

{
	if (uh->check == 0) {
		/* RFC 2460 section 8.1 says that we SHOULD log
		   this error. Well, it is reasonable.
		 */
		LIMIT_NETDEBUG(KERN_INFO "IPv6: udp checksum is 0\n");
		return 1;
	}
	if (skb->ip_summed == CHECKSUM_COMPLETE &&
	    !csum_ipv6_magic(&skb->nh.ipv6h->saddr, &skb->nh.ipv6h->daddr,
		    	     skb->len, IPPROTO_UDP, skb->csum             ))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (skb->ip_summed != CHECKSUM_UNNECESSARY)
		skb->csum = ~csum_ipv6_magic(&skb->nh.ipv6h->saddr,
					     &skb->nh.ipv6h->daddr,
					     skb->len, IPPROTO_UDP, 0);

	return (UDP_SKB_CB(skb)->partial_cov = 0);
}

int __udp6_lib_rcv(struct sk_buff **pskb, struct hlist_head udptable[],
		   int is_udplite)
{
	struct sk_buff *skb = *pskb;
	struct sock *sk;
  	struct udphdr *uh;
	struct net_device *dev = skb->dev;
	struct in6_addr *saddr, *daddr;
	u32 ulen = 0;

	if (!pskb_may_pull(skb, sizeof(struct udphdr)))
		goto short_packet;

	saddr = &skb->nh.ipv6h->saddr;
	daddr = &skb->nh.ipv6h->daddr;
	uh = skb->h.uh;

	ulen = ntohs(uh->len);
	if (ulen > skb->len)
		goto short_packet;

	if(! is_udplite ) {		/* UDP validates ulen. */

		/* Check for jumbo payload */
		if (ulen == 0)
			ulen = skb->len;

		if (ulen < sizeof(*uh))
			goto short_packet;

		if (ulen < skb->len) {
			if (pskb_trim_rcsum(skb, ulen))
				goto short_packet;
			saddr = &skb->nh.ipv6h->saddr;
			daddr = &skb->nh.ipv6h->daddr;
			uh = skb->h.uh;
		}

		if (udp6_csum_init(skb, uh))
			goto discard;

	} else 	{			/* UDP-Lite validates cscov. */
		if (udplite6_csum_init(skb, uh))
			goto discard;
	}

	/* 
	 *	Multicast receive code 
	 */
	if (ipv6_addr_is_multicast(daddr))
		return __udp6_lib_mcast_deliver(skb, saddr, daddr, udptable);

	/* Unicast */

	/* 
	 * check socket cache ... must talk to Alan about his plans
	 * for sock caches... i'll skip this for now.
	 */
	sk = __udp6_lib_lookup(saddr, uh->source,
			       daddr, uh->dest, inet6_iif(skb), udptable);

	if (sk == NULL) {
		if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb))
			goto discard;

		if (udp_lib_checksum_complete(skb))
			goto discard;
		UDP6_INC_STATS_BH(UDP_MIB_NOPORTS, is_udplite);

		icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0, dev);

		kfree_skb(skb);
		return(0);
	}
	
	/* deliver */
	
	udpv6_queue_rcv_skb(sk, skb);
	sock_put(sk);
	return(0);

short_packet:	
	LIMIT_NETDEBUG(KERN_DEBUG "UDP%sv6: short packet: %d/%u\n",
		       is_udplite? "-Lite" : "",  ulen, skb->len);

discard:
	UDP6_INC_STATS_BH(UDP_MIB_INERRORS, is_udplite);
	kfree_skb(skb);
	return(0);	
}

static __inline__ int udpv6_rcv(struct sk_buff **pskb)
{
	return __udp6_lib_rcv(pskb, udp_hash, 0);
}

/*
 * Throw away all pending data and cancel the corking. Socket is locked.
 */
static void udp_v6_flush_pending_frames(struct sock *sk)
{
	struct udp_sock *up = udp_sk(sk);

	if (up->pending) {
		up->len = 0;
		up->pending = 0;
		ip6_flush_pending_frames(sk);
        }
}

/*
 *	Sending
 */

static int udp_v6_push_pending_frames(struct sock *sk, struct udp_sock *up)
{
	struct sk_buff *skb;
	struct udphdr *uh;
	struct inet_sock *inet = inet_sk(sk);
	struct flowi *fl = &inet->cork.fl;
	int err = 0;
	u32 csum = 0;

	/* Grab the skbuff where UDP header space exists. */
	if ((skb = skb_peek(&sk->sk_write_queue)) == NULL)
		goto out;

	/*
	 * Create a UDP header
	 */
	uh = skb->h.uh;
	uh->source = fl->fl_ip_sport;
	uh->dest = fl->fl_ip_dport;
	uh->len = htons(up->len);
	uh->check = 0;

	if (up->pcflag)
		csum = udplite_csum_outgoing(sk, skb);
	 else
		csum = udp_csum_outgoing(sk, skb);

	/* add protocol-dependent pseudo-header */
	uh->check = csum_ipv6_magic(&fl->fl6_src, &fl->fl6_dst,
				    up->len, fl->proto, csum   );
	if (uh->check == 0)
		uh->check = -1;

	err = ip6_push_pending_frames(sk);
out:
	up->len = 0;
	up->pending = 0;
	return err;
}

int udpv6_sendmsg(struct kiocb *iocb, struct sock *sk,
		  struct msghdr *msg, size_t len)
{
	struct ipv6_txoptions opt_space;
	struct udp_sock *up = udp_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) msg->msg_name;
	struct in6_addr *daddr, *final_p = NULL, final;
	struct ipv6_txoptions *opt = NULL;
	struct ip6_flowlabel *flowlabel = NULL;
	struct flowi fl;
	struct dst_entry *dst;
	int addr_len = msg->msg_namelen;
	int ulen = len;
	int hlimit = -1;
	int tclass = -1;
	int corkreq = up->corkflag || msg->msg_flags&MSG_MORE;
	int err;
	int connected = 0;
	int is_udplite = up->pcflag;
	int (*getfrag)(void *, char *, int, int, int, struct sk_buff *);

	/* destination address check */
	if (sin6) {
		if (addr_len < offsetof(struct sockaddr, sa_data))
			return -EINVAL;

		switch (sin6->sin6_family) {
		case AF_INET6:
			if (addr_len < SIN6_LEN_RFC2133)
				return -EINVAL;
			daddr = &sin6->sin6_addr;
			break;
		case AF_INET:
			goto do_udp_sendmsg;
		case AF_UNSPEC:
			msg->msg_name = sin6 = NULL;
			msg->msg_namelen = addr_len = 0;
			daddr = NULL;
			break;
		default:
			return -EINVAL;
		}
	} else if (!up->pending) {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -EDESTADDRREQ;
		daddr = &np->daddr;
	} else 
		daddr = NULL;

	if (daddr) {
		if (ipv6_addr_type(daddr) == IPV6_ADDR_MAPPED) {
			struct sockaddr_in sin;
			sin.sin_family = AF_INET;
			sin.sin_port = sin6 ? sin6->sin6_port : inet->dport;
			sin.sin_addr.s_addr = daddr->s6_addr32[3];
			msg->msg_name = &sin;
			msg->msg_namelen = sizeof(sin);
do_udp_sendmsg:
			if (__ipv6_only_sock(sk))
				return -ENETUNREACH;
			return udp_sendmsg(iocb, sk, msg, len);
		}
	}

	if (up->pending == AF_INET)
		return udp_sendmsg(iocb, sk, msg, len);

	/* Rough check on arithmetic overflow,
	   better check is made in ip6_build_xmit
	   */
	if (len > INT_MAX - sizeof(struct udphdr))
		return -EMSGSIZE;
	
	if (up->pending) {
		/*
		 * There are pending frames.
		 * The socket lock must be held while it's corked.
		 */
		lock_sock(sk);
		if (likely(up->pending)) {
			if (unlikely(up->pending != AF_INET6)) {
				release_sock(sk);
				return -EAFNOSUPPORT;
			}
			dst = NULL;
			goto do_append_data;
		}
		release_sock(sk);
	}
	ulen += sizeof(struct udphdr);

	memset(&fl, 0, sizeof(fl));

	if (sin6) {
		if (sin6->sin6_port == 0)
			return -EINVAL;

		fl.fl_ip_dport = sin6->sin6_port;
		daddr = &sin6->sin6_addr;

		if (np->sndflow) {
			fl.fl6_flowlabel = sin6->sin6_flowinfo&IPV6_FLOWINFO_MASK;
			if (fl.fl6_flowlabel&IPV6_FLOWLABEL_MASK) {
				flowlabel = fl6_sock_lookup(sk, fl.fl6_flowlabel);
				if (flowlabel == NULL)
					return -EINVAL;
				daddr = &flowlabel->dst;
			}
		}

		/*
		 * Otherwise it will be difficult to maintain
		 * sk->sk_dst_cache.
		 */
		if (sk->sk_state == TCP_ESTABLISHED &&
		    ipv6_addr_equal(daddr, &np->daddr))
			daddr = &np->daddr;

		if (addr_len >= sizeof(struct sockaddr_in6) &&
		    sin6->sin6_scope_id &&
		    ipv6_addr_type(daddr)&IPV6_ADDR_LINKLOCAL)
			fl.oif = sin6->sin6_scope_id;
	} else {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -EDESTADDRREQ;

		fl.fl_ip_dport = inet->dport;
		daddr = &np->daddr;
		fl.fl6_flowlabel = np->flow_label;
		connected = 1;
	}

	if (!fl.oif)
		fl.oif = sk->sk_bound_dev_if;

	if (msg->msg_controllen) {
		opt = &opt_space;
		memset(opt, 0, sizeof(struct ipv6_txoptions));
		opt->tot_len = sizeof(*opt);

		err = datagram_send_ctl(msg, &fl, opt, &hlimit, &tclass);
		if (err < 0) {
			fl6_sock_release(flowlabel);
			return err;
		}
		if ((fl.fl6_flowlabel&IPV6_FLOWLABEL_MASK) && !flowlabel) {
			flowlabel = fl6_sock_lookup(sk, fl.fl6_flowlabel);
			if (flowlabel == NULL)
				return -EINVAL;
		}
		if (!(opt->opt_nflen|opt->opt_flen))
			opt = NULL;
		connected = 0;
	}
	if (opt == NULL)
		opt = np->opt;
	if (flowlabel)
		opt = fl6_merge_options(&opt_space, flowlabel, opt);
	opt = ipv6_fixup_options(&opt_space, opt);

	fl.proto = sk->sk_protocol;
	ipv6_addr_copy(&fl.fl6_dst, daddr);
	if (ipv6_addr_any(&fl.fl6_src) && !ipv6_addr_any(&np->saddr))
		ipv6_addr_copy(&fl.fl6_src, &np->saddr);
	fl.fl_ip_sport = inet->sport;
	
	/* merge ip6_build_xmit from ip6_output */
	if (opt && opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *) opt->srcrt;
		ipv6_addr_copy(&final, &fl.fl6_dst);
		ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
		final_p = &final;
		connected = 0;
	}

	if (!fl.oif && ipv6_addr_is_multicast(&fl.fl6_dst)) {
		fl.oif = np->mcast_oif;
		connected = 0;
	}

	security_sk_classify_flow(sk, &fl);

	err = ip6_sk_dst_lookup(sk, &dst, &fl);
	if (err)
		goto out;
	if (final_p)
		ipv6_addr_copy(&fl.fl6_dst, final_p);

	if ((err = xfrm_lookup(&dst, &fl, sk, 0)) < 0)
		goto out;

	if (hlimit < 0) {
		if (ipv6_addr_is_multicast(&fl.fl6_dst))
			hlimit = np->mcast_hops;
		else
			hlimit = np->hop_limit;
		if (hlimit < 0)
			hlimit = dst_metric(dst, RTAX_HOPLIMIT);
		if (hlimit < 0)
			hlimit = ipv6_get_hoplimit(dst->dev);
	}

	if (tclass < 0) {
		tclass = np->tclass;
		if (tclass < 0)
			tclass = 0;
	}

	if (msg->msg_flags&MSG_CONFIRM)
		goto do_confirm;
back_from_confirm:

	lock_sock(sk);
	if (unlikely(up->pending)) {
		/* The socket is already corked while preparing it. */
		/* ... which is an evident application bug. --ANK */
		release_sock(sk);

		LIMIT_NETDEBUG(KERN_DEBUG "udp cork app bug 2\n");
		err = -EINVAL;
		goto out;
	}

	up->pending = AF_INET6;

do_append_data:
	up->len += ulen;
	getfrag  =  is_udplite ?  udplite_getfrag : ip_generic_getfrag;
	err = ip6_append_data(sk, getfrag, msg->msg_iov, ulen,
		sizeof(struct udphdr), hlimit, tclass, opt, &fl,
		(struct rt6_info*)dst,
		corkreq ? msg->msg_flags|MSG_MORE : msg->msg_flags);
	if (err)
		udp_v6_flush_pending_frames(sk);
	else if (!corkreq)
		err = udp_v6_push_pending_frames(sk, up);
	else if (unlikely(skb_queue_empty(&sk->sk_write_queue)))
		up->pending = 0;

	if (dst) {
		if (connected) {
			ip6_dst_store(sk, dst,
				      ipv6_addr_equal(&fl.fl6_dst, &np->daddr) ?
				      &np->daddr : NULL,
#ifdef CONFIG_IPV6_SUBTREES
				      ipv6_addr_equal(&fl.fl6_src, &np->saddr) ?
				      &np->saddr :
#endif
				      NULL);
		} else {
			dst_release(dst);
		}
	}

	if (err > 0)
		err = np->recverr ? net_xmit_errno(err) : 0;
	release_sock(sk);
out:
	fl6_sock_release(flowlabel);
	if (!err) {
		UDP6_INC_STATS_USER(UDP_MIB_OUTDATAGRAMS, is_udplite);
		return len;
	}
	/*
	 * ENOBUFS = no kernel mem, SOCK_NOSPACE = no sndbuf space.  Reporting
	 * ENOBUFS might not be good (it's not tunable per se), but otherwise
	 * we don't have a good statistic (IpOutDiscards but it can be too many
	 * things).  We could add another new stat but at least for now that
	 * seems like overkill.
	 */
	if (err == -ENOBUFS || test_bit(SOCK_NOSPACE, &sk->sk_socket->flags)) {
		UDP6_INC_STATS_USER(UDP_MIB_SNDBUFERRORS, is_udplite);
	}
	return err;

do_confirm:
	dst_confirm(dst);
	if (!(msg->msg_flags&MSG_PROBE) || len)
		goto back_from_confirm;
	err = 0;
	goto out;
}

int udpv6_destroy_sock(struct sock *sk)
{
	lock_sock(sk);
	udp_v6_flush_pending_frames(sk);
	release_sock(sk);

	inet6_destroy_sock(sk);

	return 0;
}

/*
 *	Socket option code for UDP
 */
static int do_udpv6_setsockopt(struct sock *sk, int level, int optname,
			  char __user *optval, int optlen)
{
	struct udp_sock *up = udp_sk(sk);
	int val;
	int err = 0;

	if(optlen<sizeof(int))
		return -EINVAL;

	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	switch(optname) {
	case UDP_CORK:
		if (val != 0) {
			up->corkflag = 1;
		} else {
			up->corkflag = 0;
			lock_sock(sk);
			udp_v6_push_pending_frames(sk, up);
			release_sock(sk);
		}
		break;
	case UDP_ENCAP:
		switch (val) {
		case 0:
			up->encap_type = val;
			break;
		default:
			err = -ENOPROTOOPT;
			break;
		}
		break;

	case UDPLITE_SEND_CSCOV:
		if (!up->pcflag)         /* Disable the option on UDP sockets */
			return -ENOPROTOOPT;
		if (val != 0 && val < 8) /* Illegal coverage: use default (8) */
			val = 8;
		up->pcslen = val;
		up->pcflag |= UDPLITE_SEND_CC;
		break;

	case UDPLITE_RECV_CSCOV:
		if (!up->pcflag)         /* Disable the option on UDP sockets */
			return -ENOPROTOOPT;
		if (val != 0 && val < 8) /* Avoid silly minimal values.       */
			val = 8;
		up->pcrlen = val;
		up->pcflag |= UDPLITE_RECV_CC;
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	};

	return err;
}

int udpv6_setsockopt(struct sock *sk, int level, int optname,
		     char __user *optval, int optlen)
{
	if (level == SOL_UDP  ||  level == SOL_UDPLITE)
		return do_udpv6_setsockopt(sk, level, optname, optval, optlen);
	return ipv6_setsockopt(sk, level, optname, optval, optlen);
}

#ifdef CONFIG_COMPAT
int compat_udpv6_setsockopt(struct sock *sk, int level, int optname,
			    char __user *optval, int optlen)
{
	if (level == SOL_UDP  ||  level == SOL_UDPLITE)
		return do_udpv6_setsockopt(sk, level, optname, optval, optlen);
	return compat_ipv6_setsockopt(sk, level, optname, optval, optlen);
}
#endif

static int do_udpv6_getsockopt(struct sock *sk, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	struct udp_sock *up = udp_sk(sk);
	int val, len;

	if(get_user(len,optlen))
		return -EFAULT;

	len = min_t(unsigned int, len, sizeof(int));
	
	if(len < 0)
		return -EINVAL;

	switch(optname) {
	case UDP_CORK:
		val = up->corkflag;
		break;

	case UDP_ENCAP:
		val = up->encap_type;
		break;

	case UDPLITE_SEND_CSCOV:
		val = up->pcslen;
		break;

	case UDPLITE_RECV_CSCOV:
		val = up->pcrlen;
		break;

	default:
		return -ENOPROTOOPT;
	};

  	if(put_user(len, optlen))
  		return -EFAULT;
	if(copy_to_user(optval, &val,len))
		return -EFAULT;
  	return 0;
}

int udpv6_getsockopt(struct sock *sk, int level, int optname,
		     char __user *optval, int __user *optlen)
{
	if (level == SOL_UDP  ||  level == SOL_UDPLITE)
		return do_udpv6_getsockopt(sk, level, optname, optval, optlen);
	return ipv6_getsockopt(sk, level, optname, optval, optlen);
}

#ifdef CONFIG_COMPAT
int compat_udpv6_getsockopt(struct sock *sk, int level, int optname,
			    char __user *optval, int __user *optlen)
{
	if (level == SOL_UDP  ||  level == SOL_UDPLITE)
		return do_udpv6_getsockopt(sk, level, optname, optval, optlen);
	return compat_ipv6_getsockopt(sk, level, optname, optval, optlen);
}
#endif

static struct inet6_protocol udpv6_protocol = {
	.handler	=	udpv6_rcv,
	.err_handler	=	udpv6_err,
	.flags		=	INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

/* ------------------------------------------------------------------------ */
#ifdef CONFIG_PROC_FS

static void udp6_sock_seq_show(struct seq_file *seq, struct sock *sp, int bucket)
{
	struct inet_sock *inet = inet_sk(sp);
	struct ipv6_pinfo *np = inet6_sk(sp);
	struct in6_addr *dest, *src;
	__u16 destp, srcp;

	dest  = &np->daddr;
	src   = &np->rcv_saddr;
	destp = ntohs(inet->dport);
	srcp  = ntohs(inet->sport);
	seq_printf(seq,
		   "%4d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X "
		   "%02X %08X:%08X %02X:%08lX %08X %5d %8d %lu %d %p\n",
		   bucket,
		   src->s6_addr32[0], src->s6_addr32[1],
		   src->s6_addr32[2], src->s6_addr32[3], srcp,
		   dest->s6_addr32[0], dest->s6_addr32[1],
		   dest->s6_addr32[2], dest->s6_addr32[3], destp,
		   sp->sk_state, 
		   atomic_read(&sp->sk_wmem_alloc),
		   atomic_read(&sp->sk_rmem_alloc),
		   0, 0L, 0,
		   sock_i_uid(sp), 0,
		   sock_i_ino(sp),
		   atomic_read(&sp->sk_refcnt), sp);
}

int udp6_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_printf(seq,
			   "  sl  "
			   "local_address                         "
			   "remote_address                        "
			   "st tx_queue rx_queue tr tm->when retrnsmt"
			   "   uid  timeout inode\n");
	else
		udp6_sock_seq_show(seq, v, ((struct udp_iter_state *)seq->private)->bucket);
	return 0;
}

static struct file_operations udp6_seq_fops;
static struct udp_seq_afinfo udp6_seq_afinfo = {
	.owner		= THIS_MODULE,
	.name		= "udp6",
	.family		= AF_INET6,
	.hashtable	= udp_hash,
	.seq_show	= udp6_seq_show,
	.seq_fops	= &udp6_seq_fops,
};

int __init udp6_proc_init(void)
{
	return udp_proc_register(&udp6_seq_afinfo);
}

void udp6_proc_exit(void) {
	udp_proc_unregister(&udp6_seq_afinfo);
}
#endif /* CONFIG_PROC_FS */

/* ------------------------------------------------------------------------ */

struct proto udpv6_prot = {
	.name		   = "UDPv6",
	.owner		   = THIS_MODULE,
	.close		   = udp_lib_close,
	.connect	   = ip6_datagram_connect,
	.disconnect	   = udp_disconnect,
	.ioctl		   = udp_ioctl,
	.destroy	   = udpv6_destroy_sock,
	.setsockopt	   = udpv6_setsockopt,
	.getsockopt	   = udpv6_getsockopt,
	.sendmsg	   = udpv6_sendmsg,
	.recvmsg	   = udpv6_recvmsg,
	.backlog_rcv	   = udpv6_queue_rcv_skb,
	.hash		   = udp_lib_hash,
	.unhash		   = udp_lib_unhash,
	.get_port	   = udp_v6_get_port,
	.obj_size	   = sizeof(struct udp6_sock),
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_udpv6_setsockopt,
	.compat_getsockopt = compat_udpv6_getsockopt,
#endif
};

static struct inet_protosw udpv6_protosw = {
	.type =      SOCK_DGRAM,
	.protocol =  IPPROTO_UDP,
	.prot =      &udpv6_prot,
	.ops =       &inet6_dgram_ops,
	.capability =-1,
	.no_check =  UDP_CSUM_DEFAULT,
	.flags =     INET_PROTOSW_PERMANENT,
};


void __init udpv6_init(void)
{
	if (inet6_add_protocol(&udpv6_protocol, IPPROTO_UDP) < 0)
		printk(KERN_ERR "udpv6_init: Could not register protocol\n");
	inet6_register_protosw(&udpv6_protosw);
}
