/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
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
#include <linux/if_arp.h>
#include "rds.h"

/*
 * XXX this probably still needs more work.. no INADDR_ANY, and rbtrees aren't
 * particularly zippy.
 *
 * This is now called for every incoming frame so we arguably care much more
 * about it than we used to.
 */
static DEFINE_SPINLOCK(rds_bind_lock);
static struct rb_root rds_bind_tree = RB_ROOT;

static struct rds_sock *rds_bind_tree_walk(__be32 addr, __be16 port,
					   struct rds_sock *insert)
{
	struct rb_node **p = &rds_bind_tree.rb_node;
	struct rb_node *parent = NULL;
	struct rds_sock *rs;
	u64 cmp;
	u64 needle = ((u64)be32_to_cpu(addr) << 32) | be16_to_cpu(port);

	while (*p) {
		parent = *p;
		rs = rb_entry(parent, struct rds_sock, rs_bound_node);

		cmp = ((u64)be32_to_cpu(rs->rs_bound_addr) << 32) |
		      be16_to_cpu(rs->rs_bound_port);

		if (needle < cmp)
			p = &(*p)->rb_left;
		else if (needle > cmp)
			p = &(*p)->rb_right;
		else
			return rs;
	}

	if (insert) {
		rb_link_node(&insert->rs_bound_node, parent, p);
		rb_insert_color(&insert->rs_bound_node, &rds_bind_tree);
	}
	return NULL;
}

/*
 * Return the rds_sock bound at the given local address.
 *
 * The rx path can race with rds_release.  We notice if rds_release() has
 * marked this socket and don't return a rs ref to the rx path.
 */
struct rds_sock *rds_find_bound(__be32 addr, __be16 port)
{
	struct rds_sock *rs;
	unsigned long flags;

	spin_lock_irqsave(&rds_bind_lock, flags);
	rs = rds_bind_tree_walk(addr, port, NULL);
	if (rs && !sock_flag(rds_rs_to_sk(rs), SOCK_DEAD))
		rds_sock_addref(rs);
	else
		rs = NULL;
	spin_unlock_irqrestore(&rds_bind_lock, flags);

	rdsdebug("returning rs %p for %pI4:%u\n", rs, &addr,
		ntohs(port));
	return rs;
}

/* returns -ve errno or +ve port */
static int rds_add_bound(struct rds_sock *rs, __be32 addr, __be16 *port)
{
	unsigned long flags;
	int ret = -EADDRINUSE;
	u16 rover, last;

	if (*port != 0) {
		rover = be16_to_cpu(*port);
		last = rover;
	} else {
		rover = max_t(u16, net_random(), 2);
		last = rover - 1;
	}

	spin_lock_irqsave(&rds_bind_lock, flags);

	do {
		if (rover == 0)
			rover++;
		if (rds_bind_tree_walk(addr, cpu_to_be16(rover), rs) == NULL) {
			*port = cpu_to_be16(rover);
			ret = 0;
			break;
		}
	} while (rover++ != last);

	if (ret == 0)  {
		rs->rs_bound_addr = addr;
		rs->rs_bound_port = *port;
		rds_sock_addref(rs);

		rdsdebug("rs %p binding to %pI4:%d\n",
		  rs, &addr, (int)ntohs(*port));
	}

	spin_unlock_irqrestore(&rds_bind_lock, flags);

	return ret;
}

void rds_remove_bound(struct rds_sock *rs)
{
	unsigned long flags;

	spin_lock_irqsave(&rds_bind_lock, flags);

	if (rs->rs_bound_addr) {
		rdsdebug("rs %p unbinding from %pI4:%d\n",
		  rs, &rs->rs_bound_addr,
		  ntohs(rs->rs_bound_port));

		rb_erase(&rs->rs_bound_node, &rds_bind_tree);
		rds_sock_put(rs);
		rs->rs_bound_addr = 0;
	}

	spin_unlock_irqrestore(&rds_bind_lock, flags);
}

int rds_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_in *sin = (struct sockaddr_in *)uaddr;
	struct rds_sock *rs = rds_sk_to_rs(sk);
	struct rds_transport *trans;
	int ret = 0;

	lock_sock(sk);

	if (addr_len != sizeof(struct sockaddr_in) ||
	    sin->sin_family != AF_INET ||
	    rs->rs_bound_addr ||
	    sin->sin_addr.s_addr == htonl(INADDR_ANY)) {
		ret = -EINVAL;
		goto out;
	}

	ret = rds_add_bound(rs, sin->sin_addr.s_addr, &sin->sin_port);
	if (ret)
		goto out;

	trans = rds_trans_get_preferred(sin->sin_addr.s_addr);
	if (trans == NULL) {
		ret = -EADDRNOTAVAIL;
		rds_remove_bound(rs);
		goto out;
	}

	rs->rs_transport = trans;
	ret = 0;

out:
	release_sock(sk);
	return ret;
}
