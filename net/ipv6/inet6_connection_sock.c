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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/jhash.h>

#include <net/addrconf.h>
#include <net/inet_connection_sock.h>
#include <net/sock.h>

/*
 * request_sock (formerly open request) hash tables.
 */
static u32 inet6_synq_hash(const struct in6_addr *raddr, const u16 rport,
			   const u32 rnd, const u16 synq_hsize)
{
	u32 a = raddr->s6_addr32[0];
	u32 b = raddr->s6_addr32[1];
	u32 c = raddr->s6_addr32[2];

	a += JHASH_GOLDEN_RATIO;
	b += JHASH_GOLDEN_RATIO;
	c += rnd;
	__jhash_mix(a, b, c);

	a += raddr->s6_addr32[3];
	b += (u32)rport;
	__jhash_mix(a, b, c);

	return c & (synq_hsize - 1);
}

struct request_sock *inet6_csk_search_req(const struct sock *sk,
					  struct request_sock ***prevp,
					  const __u16 rport,
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
