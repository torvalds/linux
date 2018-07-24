/*
 * Copyright (c) 2006, 2018 Oracle and/or its affiliates. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/kernel.h>
#include <net/sock.h>
#include <linux/in.h>
#include <linux/ipv6.h>
#include <linux/if_arp.h>
#include <linux/jhash.h>
#include <linux/ratelimit.h>
#include "rds.h"

static struct rhashtable bind_hash_table;

static const struct rhashtable_params ht_parms = {
	.nelem_hint = 768,
	.key_len = RDS_BOUND_KEY_LEN,
	.key_offset = offsetof(struct rds_sock, rs_bound_key),
	.head_offset = offsetof(struct rds_sock, rs_bound_node),
	.max_size = 16384,
	.min_size = 1024,
};

/* Create a key for the bind hash table manipulation.  Port is in network byte
 * order.
 */
static inline void __rds_create_bind_key(u8 *key, const struct in6_addr *addr,
					 __be16 port, __u32 scope_id)
{
	memcpy(key, addr, sizeof(*addr));
	key += sizeof(*addr);
	memcpy(key, &port, sizeof(port));
	key += sizeof(port);
	memcpy(key, &scope_id, sizeof(scope_id));
}

/*
 * Return the rds_sock bound at the given local address.
 *
 * The rx path can race with rds_release.  We notice if rds_release() has
 * marked this socket and don't return a rs ref to the rx path.
 */
struct rds_sock *rds_find_bound(const struct in6_addr *addr, __be16 port,
				__u32 scope_id)
{
	u8 key[RDS_BOUND_KEY_LEN];
	struct rds_sock *rs;

	__rds_create_bind_key(key, addr, port, scope_id);
	rs = rhashtable_lookup_fast(&bind_hash_table, key, ht_parms);
	if (rs && !sock_flag(rds_rs_to_sk(rs), SOCK_DEAD))
		rds_sock_addref(rs);
	else
		rs = NULL;

	rdsdebug("returning rs %p for %pI6c:%u\n", rs, addr,
		 ntohs(port));

	return rs;
}

/* returns -ve errno or +ve port */
static int rds_add_bound(struct rds_sock *rs, const struct in6_addr *addr,
			 __be16 *port, __u32 scope_id)
{
	int ret = -EADDRINUSE;
	u16 rover, last;
	u8 key[RDS_BOUND_KEY_LEN];

	if (*port != 0) {
		rover = be16_to_cpu(*port);
		if (rover == RDS_FLAG_PROBE_PORT)
			return -EINVAL;
		last = rover;
	} else {
		rover = max_t(u16, prandom_u32(), 2);
		last = rover - 1;
	}

	do {
		if (rover == 0)
			rover++;

		if (rover == RDS_FLAG_PROBE_PORT)
			continue;
		__rds_create_bind_key(key, addr, cpu_to_be16(rover),
				      scope_id);
		if (rhashtable_lookup_fast(&bind_hash_table, key, ht_parms))
			continue;

		memcpy(rs->rs_bound_key, key, sizeof(rs->rs_bound_key));
		rs->rs_bound_addr = *addr;
		net_get_random_once(&rs->rs_hash_initval,
				    sizeof(rs->rs_hash_initval));
		rs->rs_bound_port = cpu_to_be16(rover);
		rs->rs_bound_node.next = NULL;
		rds_sock_addref(rs);
		if (!rhashtable_insert_fast(&bind_hash_table,
					    &rs->rs_bound_node, ht_parms)) {
			*port = rs->rs_bound_port;
			ret = 0;
			rdsdebug("rs %p binding to %pI4:%d\n",
			  rs, &addr, (int)ntohs(*port));
			break;
		} else {
			rs->rs_bound_addr = in6addr_any;
			rds_sock_put(rs);
			ret = -ENOMEM;
			break;
		}
	} while (rover++ != last);

	return ret;
}

void rds_remove_bound(struct rds_sock *rs)
{

	if (ipv6_addr_any(&rs->rs_bound_addr))
		return;

	rdsdebug("rs %p unbinding from %pI6c:%d\n",
		 rs, &rs->rs_bound_addr,
		 ntohs(rs->rs_bound_port));

	rhashtable_remove_fast(&bind_hash_table, &rs->rs_bound_node, ht_parms);
	rds_sock_put(rs);
	rs->rs_bound_addr = in6addr_any;
}

int rds_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct rds_sock *rs = rds_sk_to_rs(sk);
	struct in6_addr v6addr, *binding_addr;
	struct rds_transport *trans;
	__u32 scope_id = 0;
	int ret = 0;
	__be16 port;

	/* We only allow an RDS socket to be bound to an IPv4 address. IPv6
	 * address support will be added later.
	 */
	if (addr_len == sizeof(struct sockaddr_in)) {
		struct sockaddr_in *sin = (struct sockaddr_in *)uaddr;

		if (sin->sin_family != AF_INET ||
		    sin->sin_addr.s_addr == htonl(INADDR_ANY))
			return -EINVAL;
		ipv6_addr_set_v4mapped(sin->sin_addr.s_addr, &v6addr);
		binding_addr = &v6addr;
		port = sin->sin_port;
	} else if (addr_len == sizeof(struct sockaddr_in6)) {
		return -EPROTONOSUPPORT;
	} else {
		return -EINVAL;
	}
	lock_sock(sk);

	/* RDS socket does not allow re-binding. */
	if (!ipv6_addr_any(&rs->rs_bound_addr)) {
		ret = -EINVAL;
		goto out;
	}

	ret = rds_add_bound(rs, binding_addr, &port, scope_id);
	if (ret)
		goto out;

	if (rs->rs_transport) { /* previously bound */
		trans = rs->rs_transport;
		if (trans->laddr_check(sock_net(sock->sk),
				       binding_addr, scope_id) != 0) {
			ret = -ENOPROTOOPT;
			rds_remove_bound(rs);
		} else {
			ret = 0;
		}
		goto out;
	}
	trans = rds_trans_get_preferred(sock_net(sock->sk), binding_addr,
					scope_id);
	if (!trans) {
		ret = -EADDRNOTAVAIL;
		rds_remove_bound(rs);
		pr_info_ratelimited("RDS: %s could not find a transport for %pI6c, load rds_tcp or rds_rdma?\n",
				    __func__, binding_addr);
		goto out;
	}

	rs->rs_transport = trans;
	ret = 0;

out:
	release_sock(sk);
	return ret;
}

void rds_bind_lock_destroy(void)
{
	rhashtable_destroy(&bind_hash_table);
}

int rds_bind_lock_init(void)
{
	return rhashtable_init(&bind_hash_table, &ht_parms);
}
