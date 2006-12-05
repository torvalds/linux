/*
 * INET        An implementation of the TCP/IP protocol suite for the LINUX
 *             operating system.  INET is implemented using the  BSD Socket
 *             interface as the means of communication with the user level.
 *
 *             Support for INET6 connection oriented protocols.
 *
 * Authors:    See the TCPv6 sources
 *
 *             This program is free software; you can redistribute it and/or
 *             modify it under the terms of the GNU General Public License
 *             as published by the Free Software Foundation; either version
 *             2 of the License, or(at your option) any later version.
 */

#include <linux/module.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/jhash.h>

#include <net/addrconf.h>
#include <net/inet_connection_sock.h>
#include <net/inet_ecn.h>
#include <net/inet_hashtables.h>
#include <net/ip6_route.h>
#include <net/sock.h>
#include <net/inet6_connection_sock.h>

int inet6_csk_bind_conflict(const struct sock *sk,
			    const struct inet_bind_bucket *tb)
{
	const struct sock *sk2;
	const struct hlist_node *node;

	/* We must walk the whole port owner list in this case. -DaveM */
	sk_for_each_bound(sk2, node, &tb->owners) {
		if (sk != sk2 &&
		    (!sk->sk_bound_dev_if ||
		     !sk2->sk_bound_dev_if ||
		     sk->sk_bound_dev_if == sk2->sk_bound_dev_if) &&
		    (!sk->sk_reuse || !sk2->sk_reuse ||
		     sk2->sk_state == TCP_LISTEN) &&
		     ipv6_rcv_saddr_equal(sk, sk2))
			break;
	}

	return node != NULL;
}

EXPORT_SYMBOL_GPL(inet6_csk_bind_conflict);

/*
 * request_sock (formerly open request) hash tables.
 */
static u32 inet6_synq_hash(const struct in6_addr *raddr, const __be16 rport,
			   const u32 rnd, const u16 synq_hsize)
{
	u32 a = (__force u32)raddr->s6_addr32[0];
	u32 b = (__force u32)raddr->s6_addr32[1];
	u32 c = (__force u32)raddr->s6_addr32[2];

	a += JHASH_GOLDEN_RATIO;
	b += JHASH_GOLDEN_RATIO;
	c += rnd;
	__jhash_mix(a, b, c);

	a += (__force u32)raddr->s6_addr32[3];
	b += (__force u32)rport;
	__jhash_mix(a, b, c);

	return c & (synq_hsize - 1);
}

struct request_sock *inet6_csk_search_req(const struct sock *sk,
					  struct request_sock ***prevp,
					  const __be16 rport,
					  const struct in6_addr *raddr,
					  const struct in6_addr *laddr,
					  const int iif)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct listen_sock *lopt = icsk->icsk_accept_queue.listen_opt;
	struct request_sock *req, **prev;

	for (prev = &lopt->syn_table[inet6_synq_hash(raddr, rport,
						     lopt->hash_rnd,
						     lopt->nr_table_entries)];
	     (req = *prev) != NULL;
	     prev = &req->dl_next) {
		const struct inet6_request_sock *treq = inet6_rsk(req);

		if (inet_rsk(req)->rmt_port == rport &&
		    req->rsk_ops->family == AF_INET6 &&
		    ipv6_addr_equal(&treq->rmt_addr, raddr) &&
		    ipv6_addr_equal(&treq->loc_addr, laddr) &&
		    (!treq->iif || treq->iif == iif)) {
			BUG_TRAP(req->sk == NULL);
			*prevp = prev;
			return req;
		}
	}

	return NULL;
}

EXPORT_SYMBOL_GPL(inet6_csk_search_req);

void inet6_csk_reqsk_queue_hash_add(struct sock *sk,
				    struct request_sock *req,
				    const unsigned long timeout)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct listen_sock *lopt = icsk->icsk_accept_queue.listen_opt;
	const u32 h = inet6_synq_hash(&inet6_rsk(req)->rmt_addr,
				      inet_rsk(req)->rmt_port,
				      lopt->hash_rnd, lopt->nr_table_entries);

	reqsk_queue_hash_req(&icsk->icsk_accept_queue, h, req, timeout);
	inet_csk_reqsk_queue_added(sk, timeout);
}

EXPORT_SYMBOL_GPL(inet6_csk_reqsk_queue_hash_add);

void inet6_csk_addr2sockaddr(struct sock *sk, struct sockaddr * uaddr)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) uaddr;

	sin6->sin6_family = AF_INET6;
	ipv6_addr_copy(&sin6->sin6_addr, &np->daddr);
	sin6->sin6_port	= inet_sk(sk)->dport;
	/* We do not store received flowlabel for TCP */
	sin6->sin6_flowinfo = 0;
	sin6->sin6_scope_id = 0;
	if (sk->sk_bound_dev_if &&
	    ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL)
		sin6->sin6_scope_id = sk->sk_bound_dev_if;
}

EXPORT_SYMBOL_GPL(inet6_csk_addr2sockaddr);

int inet6_csk_xmit(struct sk_buff *skb, struct sock *sk, int ipfragok)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct flowi fl;
	struct dst_entry *dst;
	struct in6_addr *final_p = NULL, final;

	memset(&fl, 0, sizeof(fl));
	fl.proto = sk->sk_protocol;
	ipv6_addr_copy(&fl.fl6_dst, &np->daddr);
	ipv6_addr_copy(&fl.fl6_src, &np->saddr);
	fl.fl6_flowlabel = np->flow_label;
	IP6_ECN_flow_xmit(sk, fl.fl6_flowlabel);
	fl.oif = sk->sk_bound_dev_if;
	fl.fl_ip_sport = inet->sport;
	fl.fl_ip_dport = inet->dport;
	security_sk_classify_flow(sk, &fl);

	if (np->opt && np->opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *)np->opt->srcrt;
		ipv6_addr_copy(&final, &fl.fl6_dst);
		ipv6_addr_copy(&fl.fl6_dst, rt0->addr);
		final_p = &final;
	}

	dst = __sk_dst_check(sk, np->dst_cookie);

	if (dst == NULL) {
		int err = ip6_dst_lookup(sk, &dst, &fl);

		if (err) {
			sk->sk_err_soft = -err;
			kfree_skb(skb);
			return err;
		}

		if (final_p)
			ipv6_addr_copy(&fl.fl6_dst, final_p);

		if ((err = xfrm_lookup(&dst, &fl, sk, 0)) < 0) {
			sk->sk_route_caps = 0;
			kfree_skb(skb);
			return err;
		}

		__ip6_dst_store(sk, dst, NULL, NULL);
	}

	skb->dst = dst_clone(dst);

	/* Restore final destination back after routing done */
	ipv6_addr_copy(&fl.fl6_dst, &np->daddr);

	return ip6_xmit(sk, skb, &fl, np->opt, 0);
}

EXPORT_SYMBOL_GPL(inet6_csk_xmit);
