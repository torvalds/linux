/*
 * NET		Generic infrastructure for INET6 connection oriented protocols.
 *
 * Authors:	Many people, see the TCPv6 sources
 *
 * 		From code originally in TCPv6
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _INET6_CONNECTION_SOCK_H
#define _INET6_CONNECTION_SOCK_H

#include <linux/types.h>

struct in6_addr;
struct inet_bind_bucket;
struct request_sock;
struct sk_buff;
struct sock;
struct sockaddr;

extern int inet6_csk_bind_conflict(const struct sock *sk,
				   const struct inet_bind_bucket *tb, bool relax);

extern struct dst_entry* inet6_csk_route_req(struct sock *sk,
					     struct flowi6 *fl6,
					     const struct request_sock *req);

extern struct request_sock *inet6_csk_search_req(const struct sock *sk,
						 struct request_sock ***prevp,
						 const __be16 rport,
						 const struct in6_addr *raddr,
						 const struct in6_addr *laddr,
						 const int iif);

extern void inet6_csk_reqsk_queue_hash_add(struct sock *sk,
					   struct request_sock *req,
					   const unsigned long timeout);

extern void inet6_csk_addr2sockaddr(struct sock *sk, struct sockaddr *uaddr);

extern int inet6_csk_xmit(struct sk_buff *skb, struct flowi *fl);

extern struct dst_entry *inet6_csk_update_pmtu(struct sock *sk, u32 mtu);
#endif /* _INET6_CONNECTION_SOCK_H */
