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
#include <linux/slab.h>

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
	/*
	 * See comment in inet_csk_bind_conflict about sock lookup
	 * vs net namespaces issues.
	 */
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

struct dst_entry *inet6_csk_route_req(struct sock *sk,
				      const struct request_sock *req)
{
	struct inet6_request_sock *treq = inet6_rsk(req);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct in6_addr *final_p, final;
	struct dst_entry *dst;
	struct flowi6 fl6;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_proto = IPPROTO_TCP;
	fl6.daddr = treq->rmt_addr;
	final_p = fl6_update_dst(&fl6, np->opt, &final);
	fl6.saddr = treq->loc_addr;
	fl6.flowi6_oif = sk->sk_bound_dev_if;
	fl6.flowi6_mark = inet_rsk(req)->ir_mark;
	fl6.fl6_dport = inet_rsk(req)->rmt_port;
	fl6.fl6_sport = inet_rsk(req)->loc_port;
	security_req_classify_flow(req, flowi6_to_flowi(&fl6));

	dst = ip6_dst_lookup_flow(sk, &fl6, final_p, false);
	if (IS_ERR(dst))
		return NULL;

	return dst;
}

/*
 * request_sock (formerly open request) hash tables.
 */
static u32 inet6_synq_hash(const struct in6_addr *raddr, const __be16 rport,
			   const u32 rnd, const u32 synq_hsize)
{
	u32 c;

	c = jhash_3words((__force u32)raddr->s6_addr32[0],
			 (__force u32)raddr->s6_addr32[1],
			 (__force u32)raddr->s6_addr32[2],
			 rnd);

	c = jhash_2words((__force u32)raddr->s6_addr32[3],
			 (__force u32)rport,
			 c);

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
			WARN_ON(req->sk != NULL);
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
	sin6->sin6_addr = np->daddr;
	sin6->sin6_port	= inet_sk(sk)->inet_dport;
	/* We do not store received flowlabel for TCP */
	sin6->sin6_flowinfo = 0;
	sin6->sin6_scope_id = 0;
	if (sk->sk_bound_dev_if &&
	    ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL)
		sin6->sin6_scope_id = sk->sk_bound_dev_if;
}

EXPORT_SYMBOL_GPL(inet6_csk_addr2sockaddr);

static inline
void __inet6_csk_dst_store(struct sock *sk, struct dst_entry *dst,
			   struct in6_addr *daddr, struct in6_addr *saddr)
{
	__ip6_dst_store(sk, dst, daddr, saddr);

#ifdef CONFIG_XFRM
	{
		struct rt6_info *rt = (struct rt6_info  *)dst;
		rt->rt6i_flow_cache_genid = atomic_read(&flow_cache_genid);
	}
#endif
}

static inline
struct dst_entry *__inet6_csk_dst_check(struct sock *sk, u32 cookie)
{
	struct dst_entry *dst;

	dst = __sk_dst_check(sk, cookie);

#ifdef CONFIG_XFRM
	if (dst) {
		struct rt6_info *rt = (struct rt6_info *)dst;
		if (rt->rt6i_flow_cache_genid != atomic_read(&flow_cache_genid)) {
			__sk_dst_reset(sk);
			dst = NULL;
		}
	}
#endif

	return dst;
}

int inet6_csk_xmit(struct sk_buff *skb, struct flowi *fl_unused)
{
	struct sock *sk = skb->sk;
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct flowi6 fl6;
	struct dst_entry *dst;
	struct in6_addr *final_p, final;
	int res;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_proto = sk->sk_protocol;
	fl6.daddr = np->daddr;
	fl6.saddr = np->saddr;
	fl6.flowlabel = np->flow_label;
	IP6_ECN_flow_xmit(sk, fl6.flowlabel);
	fl6.flowi6_oif = sk->sk_bound_dev_if;
	fl6.flowi6_mark = sk->sk_mark;
	fl6.fl6_sport = inet->inet_sport;
	fl6.fl6_dport = inet->inet_dport;
	security_sk_classify_flow(sk, flowi6_to_flowi(&fl6));

	final_p = fl6_update_dst(&fl6, np->opt, &final);

	dst = __inet6_csk_dst_check(sk, np->dst_cookie);

	if (dst == NULL) {
		dst = ip6_dst_lookup_flow(sk, &fl6, final_p, false);

		if (IS_ERR(dst)) {
			sk->sk_err_soft = -PTR_ERR(dst);
			sk->sk_route_caps = 0;
			kfree_skb(skb);
			return PTR_ERR(dst);
		}

		__inet6_csk_dst_store(sk, dst, NULL, NULL);
	}

	rcu_read_lock();
	skb_dst_set_noref(skb, dst);

	/* Restore final destination back after routing done */
	fl6.daddr = np->daddr;

	res = ip6_xmit(sk, skb, &fl6, np->opt, np->tclass);
	rcu_read_unlock();
	return res;
}
EXPORT_SYMBOL_GPL(inet6_csk_xmit);
