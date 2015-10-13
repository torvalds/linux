/*
 *	UDP over IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	Based on linux/ipv4/udp.c
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
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/raw.h>
#include <net/tcp_states.h>
#include <net/ip6_checksum.h>
#include <net/xfrm.h>
#include <net/inet6_hashtables.h>
#include <net/busy_poll.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <trace/events/skb.h>
#include "udp_impl.h"

static u32 udp6_ehashfn(const struct net *net,
			const struct in6_addr *laddr,
			const u16 lport,
			const struct in6_addr *faddr,
			const __be16 fport)
{
	static u32 udp6_ehash_secret __read_mostly;
	static u32 udp_ipv6_hash_secret __read_mostly;

	u32 lhash, fhash;

	net_get_random_once(&udp6_ehash_secret,
			    sizeof(udp6_ehash_secret));
	net_get_random_once(&udp_ipv6_hash_secret,
			    sizeof(udp_ipv6_hash_secret));

	lhash = (__force u32)laddr->s6_addr32[3];
	fhash = __ipv6_addr_jhash(faddr, udp_ipv6_hash_secret);

	return __inet6_ehashfn(lhash, lport, fhash, fport,
			       udp_ipv6_hash_secret + net_hash_mix(net));
}

int ipv6_rcv_saddr_equal(const struct sock *sk, const struct sock *sk2)
{
	const struct in6_addr *sk2_rcv_saddr6 = inet6_rcv_saddr(sk2);
	int sk2_ipv6only = inet_v6_ipv6only(sk2);
	int addr_type = ipv6_addr_type(&sk->sk_v6_rcv_saddr);
	int addr_type2 = sk2_rcv_saddr6 ? ipv6_addr_type(sk2_rcv_saddr6) : IPV6_ADDR_MAPPED;

	/* if both are mapped, treat as IPv4 */
	if (addr_type == IPV6_ADDR_MAPPED && addr_type2 == IPV6_ADDR_MAPPED)
		return (!sk2_ipv6only &&
			(!sk->sk_rcv_saddr || !sk2->sk_rcv_saddr ||
			  sk->sk_rcv_saddr == sk2->sk_rcv_saddr));

	if (addr_type2 == IPV6_ADDR_ANY &&
	    !(sk2_ipv6only && addr_type == IPV6_ADDR_MAPPED))
		return 1;

	if (addr_type == IPV6_ADDR_ANY &&
	    !(ipv6_only_sock(sk) && addr_type2 == IPV6_ADDR_MAPPED))
		return 1;

	if (sk2_rcv_saddr6 &&
	    ipv6_addr_equal(&sk->sk_v6_rcv_saddr, sk2_rcv_saddr6))
		return 1;

	return 0;
}

static u32 udp6_portaddr_hash(const struct net *net,
			      const struct in6_addr *addr6,
			      unsigned int port)
{
	unsigned int hash, mix = net_hash_mix(net);

	if (ipv6_addr_any(addr6))
		hash = jhash_1word(0, mix);
	else if (ipv6_addr_v4mapped(addr6))
		hash = jhash_1word((__force u32)addr6->s6_addr32[3], mix);
	else
		hash = jhash2((__force u32 *)addr6->s6_addr32, 4, mix);

	return hash ^ port;
}

int udp_v6_get_port(struct sock *sk, unsigned short snum)
{
	unsigned int hash2_nulladdr =
		udp6_portaddr_hash(sock_net(sk), &in6addr_any, snum);
	unsigned int hash2_partial =
		udp6_portaddr_hash(sock_net(sk), &sk->sk_v6_rcv_saddr, 0);

	/* precompute partial secondary hash */
	udp_sk(sk)->udp_portaddr_hash = hash2_partial;
	return udp_lib_get_port(sk, snum, ipv6_rcv_saddr_equal, hash2_nulladdr);
}

static void udp_v6_rehash(struct sock *sk)
{
	u16 new_hash = udp6_portaddr_hash(sock_net(sk),
					  &sk->sk_v6_rcv_saddr,
					  inet_sk(sk)->inet_num);

	udp_lib_rehash(sk, new_hash);
}

static inline int compute_score(struct sock *sk, struct net *net,
				unsigned short hnum,
				const struct in6_addr *saddr, __be16 sport,
				const struct in6_addr *daddr, __be16 dport,
				int dif)
{
	int score;
	struct inet_sock *inet;

	if (!net_eq(sock_net(sk), net) ||
	    udp_sk(sk)->udp_port_hash != hnum ||
	    sk->sk_family != PF_INET6)
		return -1;

	score = 0;
	inet = inet_sk(sk);

	if (inet->inet_dport) {
		if (inet->inet_dport != sport)
			return -1;
		score++;
	}

	if (!ipv6_addr_any(&sk->sk_v6_rcv_saddr)) {
		if (!ipv6_addr_equal(&sk->sk_v6_rcv_saddr, daddr))
			return -1;
		score++;
	}

	if (!ipv6_addr_any(&sk->sk_v6_daddr)) {
		if (!ipv6_addr_equal(&sk->sk_v6_daddr, saddr))
			return -1;
		score++;
	}

	if (sk->sk_bound_dev_if) {
		if (sk->sk_bound_dev_if != dif)
			return -1;
		score++;
	}

	return score;
}

#define SCORE2_MAX (1 + 1 + 1)
static inline int compute_score2(struct sock *sk, struct net *net,
				 const struct in6_addr *saddr, __be16 sport,
				 const struct in6_addr *daddr,
				 unsigned short hnum, int dif)
{
	int score;
	struct inet_sock *inet;

	if (!net_eq(sock_net(sk), net) ||
	    udp_sk(sk)->udp_port_hash != hnum ||
	    sk->sk_family != PF_INET6)
		return -1;

	if (!ipv6_addr_equal(&sk->sk_v6_rcv_saddr, daddr))
		return -1;

	score = 0;
	inet = inet_sk(sk);

	if (inet->inet_dport) {
		if (inet->inet_dport != sport)
			return -1;
		score++;
	}

	if (!ipv6_addr_any(&sk->sk_v6_daddr)) {
		if (!ipv6_addr_equal(&sk->sk_v6_daddr, saddr))
			return -1;
		score++;
	}

	if (sk->sk_bound_dev_if) {
		if (sk->sk_bound_dev_if != dif)
			return -1;
		score++;
	}

	return score;
}

/* called with read_rcu_lock() */
static struct sock *udp6_lib_lookup2(struct net *net,
		const struct in6_addr *saddr, __be16 sport,
		const struct in6_addr *daddr, unsigned int hnum, int dif,
		struct udp_hslot *hslot2, unsigned int slot2)
{
	struct sock *sk, *result;
	struct hlist_nulls_node *node;
	int score, badness, matches = 0, reuseport = 0;
	u32 hash = 0;

begin:
	result = NULL;
	badness = -1;
	udp_portaddr_for_each_entry_rcu(sk, node, &hslot2->head) {
		score = compute_score2(sk, net, saddr, sport,
				      daddr, hnum, dif);
		if (score > badness) {
			result = sk;
			badness = score;
			reuseport = sk->sk_reuseport;
			if (reuseport) {
				hash = udp6_ehashfn(net, daddr, hnum,
						    saddr, sport);
				matches = 1;
			} else if (score == SCORE2_MAX)
				goto exact_match;
		} else if (score == badness && reuseport) {
			matches++;
			if (reciprocal_scale(hash, matches) == 0)
				result = sk;
			hash = next_pseudo_random32(hash);
		}
	}
	/*
	 * if the nulls value we got at the end of this lookup is
	 * not the expected one, we must restart lookup.
	 * We probably met an item that was moved to another chain.
	 */
	if (get_nulls_value(node) != slot2)
		goto begin;

	if (result) {
exact_match:
		if (unlikely(!atomic_inc_not_zero_hint(&result->sk_refcnt, 2)))
			result = NULL;
		else if (unlikely(compute_score2(result, net, saddr, sport,
				  daddr, hnum, dif) < badness)) {
			sock_put(result);
			goto begin;
		}
	}
	return result;
}

struct sock *__udp6_lib_lookup(struct net *net,
				      const struct in6_addr *saddr, __be16 sport,
				      const struct in6_addr *daddr, __be16 dport,
				      int dif, struct udp_table *udptable)
{
	struct sock *sk, *result;
	struct hlist_nulls_node *node;
	unsigned short hnum = ntohs(dport);
	unsigned int hash2, slot2, slot = udp_hashfn(net, hnum, udptable->mask);
	struct udp_hslot *hslot2, *hslot = &udptable->hash[slot];
	int score, badness, matches = 0, reuseport = 0;
	u32 hash = 0;

	rcu_read_lock();
	if (hslot->count > 10) {
		hash2 = udp6_portaddr_hash(net, daddr, hnum);
		slot2 = hash2 & udptable->mask;
		hslot2 = &udptable->hash2[slot2];
		if (hslot->count < hslot2->count)
			goto begin;

		result = udp6_lib_lookup2(net, saddr, sport,
					  daddr, hnum, dif,
					  hslot2, slot2);
		if (!result) {
			hash2 = udp6_portaddr_hash(net, &in6addr_any, hnum);
			slot2 = hash2 & udptable->mask;
			hslot2 = &udptable->hash2[slot2];
			if (hslot->count < hslot2->count)
				goto begin;

			result = udp6_lib_lookup2(net, saddr, sport,
						  &in6addr_any, hnum, dif,
						  hslot2, slot2);
		}
		rcu_read_unlock();
		return result;
	}
begin:
	result = NULL;
	badness = -1;
	sk_nulls_for_each_rcu(sk, node, &hslot->head) {
		score = compute_score(sk, net, hnum, saddr, sport, daddr, dport, dif);
		if (score > badness) {
			result = sk;
			badness = score;
			reuseport = sk->sk_reuseport;
			if (reuseport) {
				hash = udp6_ehashfn(net, daddr, hnum,
						    saddr, sport);
				matches = 1;
			}
		} else if (score == badness && reuseport) {
			matches++;
			if (reciprocal_scale(hash, matches) == 0)
				result = sk;
			hash = next_pseudo_random32(hash);
		}
	}
	/*
	 * if the nulls value we got at the end of this lookup is
	 * not the expected one, we must restart lookup.
	 * We probably met an item that was moved to another chain.
	 */
	if (get_nulls_value(node) != slot)
		goto begin;

	if (result) {
		if (unlikely(!atomic_inc_not_zero_hint(&result->sk_refcnt, 2)))
			result = NULL;
		else if (unlikely(compute_score(result, net, hnum, saddr, sport,
					daddr, dport, dif) < badness)) {
			sock_put(result);
			goto begin;
		}
	}
	rcu_read_unlock();
	return result;
}
EXPORT_SYMBOL_GPL(__udp6_lib_lookup);

static struct sock *__udp6_lib_lookup_skb(struct sk_buff *skb,
					  __be16 sport, __be16 dport,
					  struct udp_table *udptable)
{
	struct sock *sk;
	const struct ipv6hdr *iph = ipv6_hdr(skb);

	sk = skb_steal_sock(skb);
	if (unlikely(sk))
		return sk;
	return __udp6_lib_lookup(dev_net(skb_dst(skb)->dev), &iph->saddr, sport,
				 &iph->daddr, dport, inet6_iif(skb),
				 udptable);
}

struct sock *udp6_lib_lookup(struct net *net, const struct in6_addr *saddr, __be16 sport,
			     const struct in6_addr *daddr, __be16 dport, int dif)
{
	return __udp6_lib_lookup(net, saddr, sport, daddr, dport, dif, &udp_table);
}
EXPORT_SYMBOL_GPL(udp6_lib_lookup);

/*
 *	This should be easy, if there is something there we
 *	return it, otherwise we block.
 */

int udpv6_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
		  int noblock, int flags, int *addr_len)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
	struct sk_buff *skb;
	unsigned int ulen, copied;
	int peeked, off = 0;
	int err;
	int is_udplite = IS_UDPLITE(sk);
	int is_udp4;
	bool slow;

	if (flags & MSG_ERRQUEUE)
		return ipv6_recv_error(sk, msg, len, addr_len);

	if (np->rxpmtu && np->rxopt.bits.rxpmtu)
		return ipv6_recv_rxpmtu(sk, msg, len, addr_len);

try_again:
	skb = __skb_recv_datagram(sk, flags | (noblock ? MSG_DONTWAIT : 0),
				  &peeked, &off, &err);
	if (!skb)
		goto out;

	ulen = skb->len - sizeof(struct udphdr);
	copied = len;
	if (copied > ulen)
		copied = ulen;
	else if (copied < ulen)
		msg->msg_flags |= MSG_TRUNC;

	is_udp4 = (skb->protocol == htons(ETH_P_IP));

	/*
	 * If checksum is needed at all, try to do it while copying the
	 * data.  If the data is truncated, or if we only want a partial
	 * coverage checksum (UDP-Lite), do it before the copy.
	 */

	if (copied < ulen || UDP_SKB_CB(skb)->partial_cov) {
		if (udp_lib_checksum_complete(skb))
			goto csum_copy_err;
	}

	if (skb_csum_unnecessary(skb))
		err = skb_copy_datagram_msg(skb, sizeof(struct udphdr),
					    msg, copied);
	else {
		err = skb_copy_and_csum_datagram_msg(skb, sizeof(struct udphdr), msg);
		if (err == -EINVAL)
			goto csum_copy_err;
	}
	if (unlikely(err)) {
		trace_kfree_skb(skb, udpv6_recvmsg);
		if (!peeked) {
			atomic_inc(&sk->sk_drops);
			if (is_udp4)
				UDP_INC_STATS_USER(sock_net(sk),
						   UDP_MIB_INERRORS,
						   is_udplite);
			else
				UDP6_INC_STATS_USER(sock_net(sk),
						    UDP_MIB_INERRORS,
						    is_udplite);
		}
		goto out_free;
	}
	if (!peeked) {
		if (is_udp4)
			UDP_INC_STATS_USER(sock_net(sk),
					UDP_MIB_INDATAGRAMS, is_udplite);
		else
			UDP6_INC_STATS_USER(sock_net(sk),
					UDP_MIB_INDATAGRAMS, is_udplite);
	}

	sock_recv_ts_and_drops(msg, sk, skb);

	/* Copy the address. */
	if (msg->msg_name) {
		DECLARE_SOCKADDR(struct sockaddr_in6 *, sin6, msg->msg_name);
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = udp_hdr(skb)->source;
		sin6->sin6_flowinfo = 0;

		if (is_udp4) {
			ipv6_addr_set_v4mapped(ip_hdr(skb)->saddr,
					       &sin6->sin6_addr);
			sin6->sin6_scope_id = 0;
		} else {
			sin6->sin6_addr = ipv6_hdr(skb)->saddr;
			sin6->sin6_scope_id =
				ipv6_iface_scope_id(&sin6->sin6_addr,
						    inet6_iif(skb));
		}
		*addr_len = sizeof(*sin6);
	}

	if (np->rxopt.all)
		ip6_datagram_recv_common_ctl(sk, msg, skb);

	if (is_udp4) {
		if (inet->cmsg_flags)
			ip_cmsg_recv(msg, skb);
	} else {
		if (np->rxopt.all)
			ip6_datagram_recv_specific_ctl(sk, msg, skb);
	}

	err = copied;
	if (flags & MSG_TRUNC)
		err = ulen;

out_free:
	skb_free_datagram_locked(sk, skb);
out:
	return err;

csum_copy_err:
	slow = lock_sock_fast(sk);
	if (!skb_kill_datagram(sk, skb, flags)) {
		if (is_udp4) {
			UDP_INC_STATS_USER(sock_net(sk),
					UDP_MIB_CSUMERRORS, is_udplite);
			UDP_INC_STATS_USER(sock_net(sk),
					UDP_MIB_INERRORS, is_udplite);
		} else {
			UDP6_INC_STATS_USER(sock_net(sk),
					UDP_MIB_CSUMERRORS, is_udplite);
			UDP6_INC_STATS_USER(sock_net(sk),
					UDP_MIB_INERRORS, is_udplite);
		}
	}
	unlock_sock_fast(sk, slow);

	/* starting over for a new packet, but check if we need to yield */
	cond_resched();
	msg->msg_flags &= ~MSG_TRUNC;
	goto try_again;
}

void __udp6_lib_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		    u8 type, u8 code, int offset, __be32 info,
		    struct udp_table *udptable)
{
	struct ipv6_pinfo *np;
	const struct ipv6hdr *hdr = (const struct ipv6hdr *)skb->data;
	const struct in6_addr *saddr = &hdr->saddr;
	const struct in6_addr *daddr = &hdr->daddr;
	struct udphdr *uh = (struct udphdr *)(skb->data+offset);
	struct sock *sk;
	int err;
	struct net *net = dev_net(skb->dev);

	sk = __udp6_lib_lookup(net, daddr, uh->dest,
			       saddr, uh->source, inet6_iif(skb), udptable);
	if (!sk) {
		ICMP6_INC_STATS_BH(net, __in6_dev_get(skb->dev),
				   ICMP6_MIB_INERRORS);
		return;
	}

	if (type == ICMPV6_PKT_TOOBIG) {
		if (!ip6_sk_accept_pmtu(sk))
			goto out;
		ip6_sk_update_pmtu(skb, sk, info);
	}
	if (type == NDISC_REDIRECT) {
		ip6_sk_redirect(skb, sk);
		goto out;
	}

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

static int __udpv6_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	int rc;

	if (!ipv6_addr_any(&sk->sk_v6_daddr)) {
		sock_rps_save_rxhash(sk, skb);
		sk_mark_napi_id(sk, skb);
		sk_incoming_cpu_update(sk);
	}

	rc = sock_queue_rcv_skb(sk, skb);
	if (rc < 0) {
		int is_udplite = IS_UDPLITE(sk);

		/* Note that an ENOMEM error is charged twice */
		if (rc == -ENOMEM)
			UDP6_INC_STATS_BH(sock_net(sk),
					UDP_MIB_RCVBUFERRORS, is_udplite);
		UDP6_INC_STATS_BH(sock_net(sk), UDP_MIB_INERRORS, is_udplite);
		kfree_skb(skb);
		return -1;
	}
	return 0;
}

static __inline__ void udpv6_err(struct sk_buff *skb,
				 struct inet6_skb_parm *opt, u8 type,
				 u8 code, int offset, __be32 info)
{
	__udp6_lib_err(skb, opt, type, code, offset, info, &udp_table);
}

static struct static_key udpv6_encap_needed __read_mostly;
void udpv6_encap_enable(void)
{
	if (!static_key_enabled(&udpv6_encap_needed))
		static_key_slow_inc(&udpv6_encap_needed);
}
EXPORT_SYMBOL(udpv6_encap_enable);

int udpv6_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	struct udp_sock *up = udp_sk(sk);
	int rc;
	int is_udplite = IS_UDPLITE(sk);

	if (!xfrm6_policy_check(sk, XFRM_POLICY_IN, skb))
		goto drop;

	if (static_key_false(&udpv6_encap_needed) && up->encap_type) {
		int (*encap_rcv)(struct sock *sk, struct sk_buff *skb);

		/*
		 * This is an encapsulation socket so pass the skb to
		 * the socket's udp_encap_rcv() hook. Otherwise, just
		 * fall through and pass this up the UDP socket.
		 * up->encap_rcv() returns the following value:
		 * =0 if skb was successfully passed to the encap
		 *    handler or was discarded by it.
		 * >0 if skb should be passed on to UDP.
		 * <0 if skb should be resubmitted as proto -N
		 */

		/* if we're overly short, let UDP handle it */
		encap_rcv = ACCESS_ONCE(up->encap_rcv);
		if (skb->len > sizeof(struct udphdr) && encap_rcv) {
			int ret;

			/* Verify checksum before giving to encap */
			if (udp_lib_checksum_complete(skb))
				goto csum_error;

			ret = encap_rcv(sk, skb);
			if (ret <= 0) {
				UDP_INC_STATS_BH(sock_net(sk),
						 UDP_MIB_INDATAGRAMS,
						 is_udplite);
				return -ret;
			}
		}

		/* FALLTHROUGH -- it's a UDP Packet */
	}

	/*
	 * UDP-Lite specific tests, ignored on UDP sockets (see net/ipv4/udp.c).
	 */
	if ((is_udplite & UDPLITE_RECV_CC)  &&  UDP_SKB_CB(skb)->partial_cov) {

		if (up->pcrlen == 0) {          /* full coverage was set  */
			net_dbg_ratelimited("UDPLITE6: partial coverage %d while full coverage %d requested\n",
					    UDP_SKB_CB(skb)->cscov, skb->len);
			goto drop;
		}
		if (UDP_SKB_CB(skb)->cscov  <  up->pcrlen) {
			net_dbg_ratelimited("UDPLITE6: coverage %d too small, need min %d\n",
					    UDP_SKB_CB(skb)->cscov, up->pcrlen);
			goto drop;
		}
	}

	if (rcu_access_pointer(sk->sk_filter)) {
		if (udp_lib_checksum_complete(skb))
			goto csum_error;
	}

	if (sk_rcvqueues_full(sk, sk->sk_rcvbuf)) {
		UDP6_INC_STATS_BH(sock_net(sk),
				  UDP_MIB_RCVBUFERRORS, is_udplite);
		goto drop;
	}

	skb_dst_drop(skb);

	bh_lock_sock(sk);
	rc = 0;
	if (!sock_owned_by_user(sk))
		rc = __udpv6_queue_rcv_skb(sk, skb);
	else if (sk_add_backlog(sk, skb, sk->sk_rcvbuf)) {
		bh_unlock_sock(sk);
		goto drop;
	}
	bh_unlock_sock(sk);

	return rc;

csum_error:
	UDP6_INC_STATS_BH(sock_net(sk), UDP_MIB_CSUMERRORS, is_udplite);
drop:
	UDP6_INC_STATS_BH(sock_net(sk), UDP_MIB_INERRORS, is_udplite);
	atomic_inc(&sk->sk_drops);
	kfree_skb(skb);
	return -1;
}

static bool __udp_v6_is_mcast_sock(struct net *net, struct sock *sk,
				   __be16 loc_port, const struct in6_addr *loc_addr,
				   __be16 rmt_port, const struct in6_addr *rmt_addr,
				   int dif, unsigned short hnum)
{
	struct inet_sock *inet = inet_sk(sk);

	if (!net_eq(sock_net(sk), net))
		return false;

	if (udp_sk(sk)->udp_port_hash != hnum ||
	    sk->sk_family != PF_INET6 ||
	    (inet->inet_dport && inet->inet_dport != rmt_port) ||
	    (!ipv6_addr_any(&sk->sk_v6_daddr) &&
		    !ipv6_addr_equal(&sk->sk_v6_daddr, rmt_addr)) ||
	    (sk->sk_bound_dev_if && sk->sk_bound_dev_if != dif) ||
	    (!ipv6_addr_any(&sk->sk_v6_rcv_saddr) &&
		    !ipv6_addr_equal(&sk->sk_v6_rcv_saddr, loc_addr)))
		return false;
	if (!inet6_mc_check(sk, loc_addr, rmt_addr))
		return false;
	return true;
}

static void flush_stack(struct sock **stack, unsigned int count,
			struct sk_buff *skb, unsigned int final)
{
	struct sk_buff *skb1 = NULL;
	struct sock *sk;
	unsigned int i;

	for (i = 0; i < count; i++) {
		sk = stack[i];
		if (likely(!skb1))
			skb1 = (i == final) ? skb : skb_clone(skb, GFP_ATOMIC);
		if (!skb1) {
			atomic_inc(&sk->sk_drops);
			UDP6_INC_STATS_BH(sock_net(sk), UDP_MIB_RCVBUFERRORS,
					  IS_UDPLITE(sk));
			UDP6_INC_STATS_BH(sock_net(sk), UDP_MIB_INERRORS,
					  IS_UDPLITE(sk));
		}

		if (skb1 && udpv6_queue_rcv_skb(sk, skb1) <= 0)
			skb1 = NULL;
		sock_put(sk);
	}
	if (unlikely(skb1))
		kfree_skb(skb1);
}

static void udp6_csum_zero_error(struct sk_buff *skb)
{
	/* RFC 2460 section 8.1 says that we SHOULD log
	 * this error. Well, it is reasonable.
	 */
	net_dbg_ratelimited("IPv6: udp checksum is 0 for [%pI6c]:%u->[%pI6c]:%u\n",
			    &ipv6_hdr(skb)->saddr, ntohs(udp_hdr(skb)->source),
			    &ipv6_hdr(skb)->daddr, ntohs(udp_hdr(skb)->dest));
}

/*
 * Note: called only from the BH handler context,
 * so we don't need to lock the hashes.
 */
static int __udp6_lib_mcast_deliver(struct net *net, struct sk_buff *skb,
		const struct in6_addr *saddr, const struct in6_addr *daddr,
		struct udp_table *udptable, int proto)
{
	struct sock *sk, *stack[256 / sizeof(struct sock *)];
	const struct udphdr *uh = udp_hdr(skb);
	struct hlist_nulls_node *node;
	unsigned short hnum = ntohs(uh->dest);
	struct udp_hslot *hslot = udp_hashslot(udptable, net, hnum);
	int dif = inet6_iif(skb);
	unsigned int count = 0, offset = offsetof(typeof(*sk), sk_nulls_node);
	unsigned int hash2 = 0, hash2_any = 0, use_hash2 = (hslot->count > 10);
	bool inner_flushed = false;

	if (use_hash2) {
		hash2_any = udp6_portaddr_hash(net, &in6addr_any, hnum) &
			    udp_table.mask;
		hash2 = udp6_portaddr_hash(net, daddr, hnum) & udp_table.mask;
start_lookup:
		hslot = &udp_table.hash2[hash2];
		offset = offsetof(typeof(*sk), __sk_common.skc_portaddr_node);
	}

	spin_lock(&hslot->lock);
	sk_nulls_for_each_entry_offset(sk, node, &hslot->head, offset) {
		if (__udp_v6_is_mcast_sock(net, sk,
					   uh->dest, daddr,
					   uh->source, saddr,
					   dif, hnum) &&
		    /* If zero checksum and no_check is not on for
		     * the socket then skip it.
		     */
		    (uh->check || udp_sk(sk)->no_check6_rx)) {
			if (unlikely(count == ARRAY_SIZE(stack))) {
				flush_stack(stack, count, skb, ~0);
				inner_flushed = true;
				count = 0;
			}
			stack[count++] = sk;
			sock_hold(sk);
		}
	}

	spin_unlock(&hslot->lock);

	/* Also lookup *:port if we are using hash2 and haven't done so yet. */
	if (use_hash2 && hash2 != hash2_any) {
		hash2 = hash2_any;
		goto start_lookup;
	}

	if (count) {
		flush_stack(stack, count, skb, count - 1);
	} else {
		if (!inner_flushed)
			UDP_INC_STATS_BH(net, UDP_MIB_IGNOREDMULTI,
					 proto == IPPROTO_UDPLITE);
		consume_skb(skb);
	}
	return 0;
}

int __udp6_lib_rcv(struct sk_buff *skb, struct udp_table *udptable,
		   int proto)
{
	struct net *net = dev_net(skb->dev);
	struct sock *sk;
	struct udphdr *uh;
	const struct in6_addr *saddr, *daddr;
	u32 ulen = 0;

	if (!pskb_may_pull(skb, sizeof(struct udphdr)))
		goto discard;

	saddr = &ipv6_hdr(skb)->saddr;
	daddr = &ipv6_hdr(skb)->daddr;
	uh = udp_hdr(skb);

	ulen = ntohs(uh->len);
	if (ulen > skb->len)
		goto short_packet;

	if (proto == IPPROTO_UDP) {
		/* UDP validates ulen. */

		/* Check for jumbo payload */
		if (ulen == 0)
			ulen = skb->len;

		if (ulen < sizeof(*uh))
			goto short_packet;

		if (ulen < skb->len) {
			if (pskb_trim_rcsum(skb, ulen))
				goto short_packet;
			saddr = &ipv6_hdr(skb)->saddr;
			daddr = &ipv6_hdr(skb)->daddr;
			uh = udp_hdr(skb);
		}
	}

	if (udp6_csum_init(skb, uh, proto))
		goto csum_error;

	/*
	 *	Multicast receive code
	 */
	if (ipv6_addr_is_multicast(daddr))
		return __udp6_lib_mcast_deliver(net, skb,
				saddr, daddr, udptable, proto);

	/* Unicast */

	/*
	 * check socket cache ... must talk to Alan about his plans
	 * for sock caches... i'll skip this for now.
	 */
	sk = __udp6_lib_lookup_skb(skb, uh->source, uh->dest, udptable);
	if (sk) {
		int ret;

		if (!uh->check && !udp_sk(sk)->no_check6_rx) {
			sock_put(sk);
			udp6_csum_zero_error(skb);
			goto csum_error;
		}

		if (inet_get_convert_csum(sk) && uh->check && !IS_UDPLITE(sk))
			skb_checksum_try_convert(skb, IPPROTO_UDP, uh->check,
						 ip6_compute_pseudo);

		ret = udpv6_queue_rcv_skb(sk, skb);
		sock_put(sk);

		/* a return value > 0 means to resubmit the input, but
		 * it wants the return to be -protocol, or 0
		 */
		if (ret > 0)
			return -ret;

		return 0;
	}

	if (!uh->check) {
		udp6_csum_zero_error(skb);
		goto csum_error;
	}

	if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb))
		goto discard;

	if (udp_lib_checksum_complete(skb))
		goto csum_error;

	UDP6_INC_STATS_BH(net, UDP_MIB_NOPORTS, proto == IPPROTO_UDPLITE);
	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0);

	kfree_skb(skb);
	return 0;

short_packet:
	net_dbg_ratelimited("UDP%sv6: short packet: From [%pI6c]:%u %d/%d to [%pI6c]:%u\n",
			    proto == IPPROTO_UDPLITE ? "-Lite" : "",
			    saddr, ntohs(uh->source),
			    ulen, skb->len,
			    daddr, ntohs(uh->dest));
	goto discard;
csum_error:
	UDP6_INC_STATS_BH(net, UDP_MIB_CSUMERRORS, proto == IPPROTO_UDPLITE);
discard:
	UDP6_INC_STATS_BH(net, UDP_MIB_INERRORS, proto == IPPROTO_UDPLITE);
	kfree_skb(skb);
	return 0;
}

static __inline__ int udpv6_rcv(struct sk_buff *skb)
{
	return __udp6_lib_rcv(skb, &udp_table, IPPROTO_UDP);
}

/*
 * Throw away all pending data and cancel the corking. Socket is locked.
 */
static void udp_v6_flush_pending_frames(struct sock *sk)
{
	struct udp_sock *up = udp_sk(sk);

	if (up->pending == AF_INET)
		udp_flush_pending_frames(sk);
	else if (up->pending) {
		up->len = 0;
		up->pending = 0;
		ip6_flush_pending_frames(sk);
	}
}

/**
 *	udp6_hwcsum_outgoing  -  handle outgoing HW checksumming
 *	@sk:	socket we are sending on
 *	@skb:	sk_buff containing the filled-in UDP header
 *		(checksum field must be zeroed out)
 */
static void udp6_hwcsum_outgoing(struct sock *sk, struct sk_buff *skb,
				 const struct in6_addr *saddr,
				 const struct in6_addr *daddr, int len)
{
	unsigned int offset;
	struct udphdr *uh = udp_hdr(skb);
	struct sk_buff *frags = skb_shinfo(skb)->frag_list;
	__wsum csum = 0;

	if (!frags) {
		/* Only one fragment on the socket.  */
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct udphdr, check);
		uh->check = ~csum_ipv6_magic(saddr, daddr, len, IPPROTO_UDP, 0);
	} else {
		/*
		 * HW-checksum won't work as there are two or more
		 * fragments on the socket so that all csums of sk_buffs
		 * should be together
		 */
		offset = skb_transport_offset(skb);
		skb->csum = skb_checksum(skb, offset, skb->len - offset, 0);

		skb->ip_summed = CHECKSUM_NONE;

		do {
			csum = csum_add(csum, frags->csum);
		} while ((frags = frags->next));

		uh->check = csum_ipv6_magic(saddr, daddr, len, IPPROTO_UDP,
					    csum);
		if (uh->check == 0)
			uh->check = CSUM_MANGLED_0;
	}
}

/*
 *	Sending
 */

static int udp_v6_send_skb(struct sk_buff *skb, struct flowi6 *fl6)
{
	struct sock *sk = skb->sk;
	struct udphdr *uh;
	int err = 0;
	int is_udplite = IS_UDPLITE(sk);
	__wsum csum = 0;
	int offset = skb_transport_offset(skb);
	int len = skb->len - offset;

	/*
	 * Create a UDP header
	 */
	uh = udp_hdr(skb);
	uh->source = fl6->fl6_sport;
	uh->dest = fl6->fl6_dport;
	uh->len = htons(len);
	uh->check = 0;

	if (is_udplite)
		csum = udplite_csum(skb);
	else if (udp_sk(sk)->no_check6_tx) {   /* UDP csum disabled */
		skb->ip_summed = CHECKSUM_NONE;
		goto send;
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) { /* UDP hardware csum */
		udp6_hwcsum_outgoing(sk, skb, &fl6->saddr, &fl6->daddr, len);
		goto send;
	} else
		csum = udp_csum(skb);

	/* add protocol-dependent pseudo-header */
	uh->check = csum_ipv6_magic(&fl6->saddr, &fl6->daddr,
				    len, fl6->flowi6_proto, csum);
	if (uh->check == 0)
		uh->check = CSUM_MANGLED_0;

send:
	err = ip6_send_skb(skb);
	if (err) {
		if (err == -ENOBUFS && !inet6_sk(sk)->recverr) {
			UDP6_INC_STATS_USER(sock_net(sk),
					    UDP_MIB_SNDBUFERRORS, is_udplite);
			err = 0;
		}
	} else
		UDP6_INC_STATS_USER(sock_net(sk),
				    UDP_MIB_OUTDATAGRAMS, is_udplite);
	return err;
}

static int udp_v6_push_pending_frames(struct sock *sk)
{
	struct sk_buff *skb;
	struct udp_sock  *up = udp_sk(sk);
	struct flowi6 fl6;
	int err = 0;

	if (up->pending == AF_INET)
		return udp_push_pending_frames(sk);

	/* ip6_finish_skb will release the cork, so make a copy of
	 * fl6 here.
	 */
	fl6 = inet_sk(sk)->cork.fl.u.ip6;

	skb = ip6_finish_skb(sk);
	if (!skb)
		goto out;

	err = udp_v6_send_skb(skb, &fl6);

out:
	up->len = 0;
	up->pending = 0;
	return err;
}

int udpv6_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct ipv6_txoptions opt_space;
	struct udp_sock *up = udp_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	DECLARE_SOCKADDR(struct sockaddr_in6 *, sin6, msg->msg_name);
	struct in6_addr *daddr, *final_p, final;
	struct ipv6_txoptions *opt = NULL;
	struct ip6_flowlabel *flowlabel = NULL;
	struct flowi6 fl6;
	struct dst_entry *dst;
	int addr_len = msg->msg_namelen;
	int ulen = len;
	int hlimit = -1;
	int tclass = -1;
	int dontfrag = -1;
	int corkreq = up->corkflag || msg->msg_flags&MSG_MORE;
	int err;
	int connected = 0;
	int is_udplite = IS_UDPLITE(sk);
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
		daddr = &sk->sk_v6_daddr;
	} else
		daddr = NULL;

	if (daddr) {
		if (ipv6_addr_v4mapped(daddr)) {
			struct sockaddr_in sin;
			sin.sin_family = AF_INET;
			sin.sin_port = sin6 ? sin6->sin6_port : inet->inet_dport;
			sin.sin_addr.s_addr = daddr->s6_addr32[3];
			msg->msg_name = &sin;
			msg->msg_namelen = sizeof(sin);
do_udp_sendmsg:
			if (__ipv6_only_sock(sk))
				return -ENETUNREACH;
			return udp_sendmsg(sk, msg, len);
		}
	}

	if (up->pending == AF_INET)
		return udp_sendmsg(sk, msg, len);

	/* Rough check on arithmetic overflow,
	   better check is made in ip6_append_data().
	   */
	if (len > INT_MAX - sizeof(struct udphdr))
		return -EMSGSIZE;

	getfrag  =  is_udplite ?  udplite_getfrag : ip_generic_getfrag;
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

	memset(&fl6, 0, sizeof(fl6));

	if (sin6) {
		if (sin6->sin6_port == 0)
			return -EINVAL;

		fl6.fl6_dport = sin6->sin6_port;
		daddr = &sin6->sin6_addr;

		if (np->sndflow) {
			fl6.flowlabel = sin6->sin6_flowinfo&IPV6_FLOWINFO_MASK;
			if (fl6.flowlabel&IPV6_FLOWLABEL_MASK) {
				flowlabel = fl6_sock_lookup(sk, fl6.flowlabel);
				if (!flowlabel)
					return -EINVAL;
			}
		}

		/*
		 * Otherwise it will be difficult to maintain
		 * sk->sk_dst_cache.
		 */
		if (sk->sk_state == TCP_ESTABLISHED &&
		    ipv6_addr_equal(daddr, &sk->sk_v6_daddr))
			daddr = &sk->sk_v6_daddr;

		if (addr_len >= sizeof(struct sockaddr_in6) &&
		    sin6->sin6_scope_id &&
		    __ipv6_addr_needs_scope_id(__ipv6_addr_type(daddr)))
			fl6.flowi6_oif = sin6->sin6_scope_id;
	} else {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -EDESTADDRREQ;

		fl6.fl6_dport = inet->inet_dport;
		daddr = &sk->sk_v6_daddr;
		fl6.flowlabel = np->flow_label;
		connected = 1;
	}

	if (!fl6.flowi6_oif)
		fl6.flowi6_oif = sk->sk_bound_dev_if;

	if (!fl6.flowi6_oif)
		fl6.flowi6_oif = np->sticky_pktinfo.ipi6_ifindex;

	fl6.flowi6_mark = sk->sk_mark;

	if (msg->msg_controllen) {
		opt = &opt_space;
		memset(opt, 0, sizeof(struct ipv6_txoptions));
		opt->tot_len = sizeof(*opt);

		err = ip6_datagram_send_ctl(sock_net(sk), sk, msg, &fl6, opt,
					    &hlimit, &tclass, &dontfrag);
		if (err < 0) {
			fl6_sock_release(flowlabel);
			return err;
		}
		if ((fl6.flowlabel&IPV6_FLOWLABEL_MASK) && !flowlabel) {
			flowlabel = fl6_sock_lookup(sk, fl6.flowlabel);
			if (!flowlabel)
				return -EINVAL;
		}
		if (!(opt->opt_nflen|opt->opt_flen))
			opt = NULL;
		connected = 0;
	}
	if (!opt)
		opt = np->opt;
	if (flowlabel)
		opt = fl6_merge_options(&opt_space, flowlabel, opt);
	opt = ipv6_fixup_options(&opt_space, opt);

	fl6.flowi6_proto = sk->sk_protocol;
	if (!ipv6_addr_any(daddr))
		fl6.daddr = *daddr;
	else
		fl6.daddr.s6_addr[15] = 0x1; /* :: means loopback (BSD'ism) */
	if (ipv6_addr_any(&fl6.saddr) && !ipv6_addr_any(&np->saddr))
		fl6.saddr = np->saddr;
	fl6.fl6_sport = inet->inet_sport;

	final_p = fl6_update_dst(&fl6, opt, &final);
	if (final_p)
		connected = 0;

	if (!fl6.flowi6_oif && ipv6_addr_is_multicast(&fl6.daddr)) {
		fl6.flowi6_oif = np->mcast_oif;
		connected = 0;
	} else if (!fl6.flowi6_oif)
		fl6.flowi6_oif = np->ucast_oif;

	security_sk_classify_flow(sk, flowi6_to_flowi(&fl6));

	dst = ip6_sk_dst_lookup_flow(sk, &fl6, final_p);
	if (IS_ERR(dst)) {
		err = PTR_ERR(dst);
		dst = NULL;
		goto out;
	}

	if (hlimit < 0)
		hlimit = ip6_sk_dst_hoplimit(np, &fl6, dst);

	if (tclass < 0)
		tclass = np->tclass;

	if (msg->msg_flags&MSG_CONFIRM)
		goto do_confirm;
back_from_confirm:

	/* Lockless fast path for the non-corking case */
	if (!corkreq) {
		struct sk_buff *skb;

		skb = ip6_make_skb(sk, getfrag, msg, ulen,
				   sizeof(struct udphdr), hlimit, tclass, opt,
				   &fl6, (struct rt6_info *)dst,
				   msg->msg_flags, dontfrag);
		err = PTR_ERR(skb);
		if (!IS_ERR_OR_NULL(skb))
			err = udp_v6_send_skb(skb, &fl6);
		goto release_dst;
	}

	lock_sock(sk);
	if (unlikely(up->pending)) {
		/* The socket is already corked while preparing it. */
		/* ... which is an evident application bug. --ANK */
		release_sock(sk);

		net_dbg_ratelimited("udp cork app bug 2\n");
		err = -EINVAL;
		goto out;
	}

	up->pending = AF_INET6;

do_append_data:
	if (dontfrag < 0)
		dontfrag = np->dontfrag;
	up->len += ulen;
	err = ip6_append_data(sk, getfrag, msg, ulen,
		sizeof(struct udphdr), hlimit, tclass, opt, &fl6,
		(struct rt6_info *)dst,
		corkreq ? msg->msg_flags|MSG_MORE : msg->msg_flags, dontfrag);
	if (err)
		udp_v6_flush_pending_frames(sk);
	else if (!corkreq)
		err = udp_v6_push_pending_frames(sk);
	else if (unlikely(skb_queue_empty(&sk->sk_write_queue)))
		up->pending = 0;

	if (err > 0)
		err = np->recverr ? net_xmit_errno(err) : 0;
	release_sock(sk);

release_dst:
	if (dst) {
		if (connected) {
			ip6_dst_store(sk, dst,
				      ipv6_addr_equal(&fl6.daddr, &sk->sk_v6_daddr) ?
				      &sk->sk_v6_daddr : NULL,
#ifdef CONFIG_IPV6_SUBTREES
				      ipv6_addr_equal(&fl6.saddr, &np->saddr) ?
				      &np->saddr :
#endif
				      NULL);
		} else {
			dst_release(dst);
		}
		dst = NULL;
	}

out:
	dst_release(dst);
	fl6_sock_release(flowlabel);
	if (!err)
		return len;
	/*
	 * ENOBUFS = no kernel mem, SOCK_NOSPACE = no sndbuf space.  Reporting
	 * ENOBUFS might not be good (it's not tunable per se), but otherwise
	 * we don't have a good statistic (IpOutDiscards but it can be too many
	 * things).  We could add another new stat but at least for now that
	 * seems like overkill.
	 */
	if (err == -ENOBUFS || test_bit(SOCK_NOSPACE, &sk->sk_socket->flags)) {
		UDP6_INC_STATS_USER(sock_net(sk),
				UDP_MIB_SNDBUFERRORS, is_udplite);
	}
	return err;

do_confirm:
	dst_confirm(dst);
	if (!(msg->msg_flags&MSG_PROBE) || len)
		goto back_from_confirm;
	err = 0;
	goto out;
}

void udpv6_destroy_sock(struct sock *sk)
{
	struct udp_sock *up = udp_sk(sk);
	lock_sock(sk);
	udp_v6_flush_pending_frames(sk);
	release_sock(sk);

	if (static_key_false(&udpv6_encap_needed) && up->encap_type) {
		void (*encap_destroy)(struct sock *sk);
		encap_destroy = ACCESS_ONCE(up->encap_destroy);
		if (encap_destroy)
			encap_destroy(sk);
	}

	inet6_destroy_sock(sk);
}

/*
 *	Socket option code for UDP
 */
int udpv6_setsockopt(struct sock *sk, int level, int optname,
		     char __user *optval, unsigned int optlen)
{
	if (level == SOL_UDP  ||  level == SOL_UDPLITE)
		return udp_lib_setsockopt(sk, level, optname, optval, optlen,
					  udp_v6_push_pending_frames);
	return ipv6_setsockopt(sk, level, optname, optval, optlen);
}

#ifdef CONFIG_COMPAT
int compat_udpv6_setsockopt(struct sock *sk, int level, int optname,
			    char __user *optval, unsigned int optlen)
{
	if (level == SOL_UDP  ||  level == SOL_UDPLITE)
		return udp_lib_setsockopt(sk, level, optname, optval, optlen,
					  udp_v6_push_pending_frames);
	return compat_ipv6_setsockopt(sk, level, optname, optval, optlen);
}
#endif

int udpv6_getsockopt(struct sock *sk, int level, int optname,
		     char __user *optval, int __user *optlen)
{
	if (level == SOL_UDP  ||  level == SOL_UDPLITE)
		return udp_lib_getsockopt(sk, level, optname, optval, optlen);
	return ipv6_getsockopt(sk, level, optname, optval, optlen);
}

#ifdef CONFIG_COMPAT
int compat_udpv6_getsockopt(struct sock *sk, int level, int optname,
			    char __user *optval, int __user *optlen)
{
	if (level == SOL_UDP  ||  level == SOL_UDPLITE)
		return udp_lib_getsockopt(sk, level, optname, optval, optlen);
	return compat_ipv6_getsockopt(sk, level, optname, optval, optlen);
}
#endif

static const struct inet6_protocol udpv6_protocol = {
	.handler	=	udpv6_rcv,
	.err_handler	=	udpv6_err,
	.flags		=	INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

/* ------------------------------------------------------------------------ */
#ifdef CONFIG_PROC_FS
int udp6_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, IPV6_SEQ_DGRAM_HEADER);
	} else {
		int bucket = ((struct udp_iter_state *)seq->private)->bucket;
		struct inet_sock *inet = inet_sk(v);
		__u16 srcp = ntohs(inet->inet_sport);
		__u16 destp = ntohs(inet->inet_dport);
		ip6_dgram_sock_seq_show(seq, v, srcp, destp, bucket);
	}
	return 0;
}

static const struct file_operations udp6_afinfo_seq_fops = {
	.owner    = THIS_MODULE,
	.open     = udp_seq_open,
	.read     = seq_read,
	.llseek   = seq_lseek,
	.release  = seq_release_net
};

static struct udp_seq_afinfo udp6_seq_afinfo = {
	.name		= "udp6",
	.family		= AF_INET6,
	.udp_table	= &udp_table,
	.seq_fops	= &udp6_afinfo_seq_fops,
	.seq_ops	= {
		.show		= udp6_seq_show,
	},
};

int __net_init udp6_proc_init(struct net *net)
{
	return udp_proc_register(net, &udp6_seq_afinfo);
}

void udp6_proc_exit(struct net *net)
{
	udp_proc_unregister(net, &udp6_seq_afinfo);
}
#endif /* CONFIG_PROC_FS */

void udp_v6_clear_sk(struct sock *sk, int size)
{
	struct inet_sock *inet = inet_sk(sk);

	/* we do not want to clear pinet6 field, because of RCU lookups */
	sk_prot_clear_portaddr_nulls(sk, offsetof(struct inet_sock, pinet6));

	size -= offsetof(struct inet_sock, pinet6) + sizeof(inet->pinet6);
	memset(&inet->pinet6 + 1, 0, size);
}

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
	.backlog_rcv	   = __udpv6_queue_rcv_skb,
	.hash		   = udp_lib_hash,
	.unhash		   = udp_lib_unhash,
	.rehash		   = udp_v6_rehash,
	.get_port	   = udp_v6_get_port,
	.memory_allocated  = &udp_memory_allocated,
	.sysctl_mem	   = sysctl_udp_mem,
	.sysctl_wmem	   = &sysctl_udp_wmem_min,
	.sysctl_rmem	   = &sysctl_udp_rmem_min,
	.obj_size	   = sizeof(struct udp6_sock),
	.slab_flags	   = SLAB_DESTROY_BY_RCU,
	.h.udp_table	   = &udp_table,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_udpv6_setsockopt,
	.compat_getsockopt = compat_udpv6_getsockopt,
#endif
	.clear_sk	   = udp_v6_clear_sk,
};

static struct inet_protosw udpv6_protosw = {
	.type =      SOCK_DGRAM,
	.protocol =  IPPROTO_UDP,
	.prot =      &udpv6_prot,
	.ops =       &inet6_dgram_ops,
	.flags =     INET_PROTOSW_PERMANENT,
};

int __init udpv6_init(void)
{
	int ret;

	ret = inet6_add_protocol(&udpv6_protocol, IPPROTO_UDP);
	if (ret)
		goto out;

	ret = inet6_register_protosw(&udpv6_protosw);
	if (ret)
		goto out_udpv6_protocol;
out:
	return ret;

out_udpv6_protocol:
	inet6_del_protocol(&udpv6_protocol, IPPROTO_UDP);
	goto out;
}

void udpv6_exit(void)
{
	inet6_unregister_protosw(&udpv6_protosw);
	inet6_del_protocol(&udpv6_protocol, IPPROTO_UDP);
}
