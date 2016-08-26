/* net/tipc/udp_media.c: IP bearer support for TIPC
 *
 * Copyright (c) 2015, Ericsson AB
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/socket.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/inet.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/udp_tunnel.h>
#include <net/addrconf.h>
#include <linux/tipc_netlink.h>
#include "core.h"
#include "bearer.h"
#include "netlink.h"

/* IANA assigned UDP port */
#define UDP_PORT_DEFAULT	6118

#define UDP_MIN_HEADROOM        48

/**
 * struct udp_media_addr - IP/UDP addressing information
 *
 * This is the bearer level originating address used in neighbor discovery
 * messages, and all fields should be in network byte order
 */
struct udp_media_addr {
	__be16	proto;
	__be16	port;
	union {
		struct in_addr ipv4;
		struct in6_addr ipv6;
	};
};

/**
 * struct udp_bearer - ip/udp bearer data structure
 * @bearer:	associated generic tipc bearer
 * @ubsock:	bearer associated socket
 * @ifindex:	local address scope
 * @work:	used to schedule deferred work on a bearer
 */
struct udp_bearer {
	struct tipc_bearer __rcu *bearer;
	struct socket *ubsock;
	u32 ifindex;
	struct work_struct work;
};

static int tipc_udp_is_mcast_addr(struct udp_media_addr *addr)
{
	if (ntohs(addr->proto) == ETH_P_IP)
		return ipv4_is_multicast(addr->ipv4.s_addr);
#if IS_ENABLED(CONFIG_IPV6)
	else
		return ipv6_addr_is_multicast(&addr->ipv6);
#endif
	return 0;
}

/* udp_media_addr_set - convert a ip/udp address to a TIPC media address */
static void tipc_udp_media_addr_set(struct tipc_media_addr *addr,
				    struct udp_media_addr *ua)
{
	memset(addr, 0, sizeof(struct tipc_media_addr));
	addr->media_id = TIPC_MEDIA_TYPE_UDP;
	memcpy(addr->value, ua, sizeof(struct udp_media_addr));

	if (tipc_udp_is_mcast_addr(ua))
		addr->broadcast = 1;
}

/* tipc_udp_addr2str - convert ip/udp address to string */
static int tipc_udp_addr2str(struct tipc_media_addr *a, char *buf, int size)
{
	struct udp_media_addr *ua = (struct udp_media_addr *)&a->value;

	if (ntohs(ua->proto) == ETH_P_IP)
		snprintf(buf, size, "%pI4:%u", &ua->ipv4, ntohs(ua->port));
	else if (ntohs(ua->proto) == ETH_P_IPV6)
		snprintf(buf, size, "%pI6:%u", &ua->ipv6, ntohs(ua->port));
	else
		pr_err("Invalid UDP media address\n");
	return 0;
}

/* tipc_udp_msg2addr - extract an ip/udp address from a TIPC ndisc message */
static int tipc_udp_msg2addr(struct tipc_bearer *b, struct tipc_media_addr *a,
			     char *msg)
{
	struct udp_media_addr *ua;

	ua = (struct udp_media_addr *) (msg + TIPC_MEDIA_ADDR_OFFSET);
	if (msg[TIPC_MEDIA_TYPE_OFFSET] != TIPC_MEDIA_TYPE_UDP)
		return -EINVAL;
	tipc_udp_media_addr_set(a, ua);
	return 0;
}

/* tipc_udp_addr2msg - write an ip/udp address to a TIPC ndisc message */
static int tipc_udp_addr2msg(char *msg, struct tipc_media_addr *a)
{
	memset(msg, 0, TIPC_MEDIA_INFO_SIZE);
	msg[TIPC_MEDIA_TYPE_OFFSET] = TIPC_MEDIA_TYPE_UDP;
	memcpy(msg + TIPC_MEDIA_ADDR_OFFSET, a->value,
	       sizeof(struct udp_media_addr));
	return 0;
}

/* tipc_send_msg - enqueue a send request */
static int tipc_udp_xmit(struct net *net, struct sk_buff *skb,
			 struct udp_bearer *ub, struct udp_media_addr *src,
			 struct udp_media_addr *dst)
{
	int ttl, err = 0;
	struct rtable *rt;

	if (dst->proto == htons(ETH_P_IP)) {
		struct flowi4 fl = {
			.daddr = dst->ipv4.s_addr,
			.saddr = src->ipv4.s_addr,
			.flowi4_mark = skb->mark,
			.flowi4_proto = IPPROTO_UDP
		};
		rt = ip_route_output_key(net, &fl);
		if (IS_ERR(rt)) {
			err = PTR_ERR(rt);
			goto tx_error;
		}

		skb->dev = rt->dst.dev;
		ttl = ip4_dst_hoplimit(&rt->dst);
		udp_tunnel_xmit_skb(rt, ub->ubsock->sk, skb, src->ipv4.s_addr,
				    dst->ipv4.s_addr, 0, ttl, 0, src->port,
				    dst->port, false, true);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		struct dst_entry *ndst;
		struct flowi6 fl6 = {
			.flowi6_oif = ub->ifindex,
			.daddr = dst->ipv6,
			.saddr = src->ipv6,
			.flowi6_proto = IPPROTO_UDP
		};
		err = ipv6_stub->ipv6_dst_lookup(net, ub->ubsock->sk, &ndst,
						 &fl6);
		if (err)
			goto tx_error;
		ttl = ip6_dst_hoplimit(ndst);
		err = udp_tunnel6_xmit_skb(ndst, ub->ubsock->sk, skb,
					   ndst->dev, &src->ipv6,
					   &dst->ipv6, 0, ttl, 0, src->port,
					   dst->port, false);
#endif
	}
	return err;

tx_error:
	kfree_skb(skb);
	return err;
}

static int tipc_udp_send_msg(struct net *net, struct sk_buff *skb,
			     struct tipc_bearer *b,
			     struct tipc_media_addr *addr)
{
	struct udp_media_addr *src = (struct udp_media_addr *)&b->addr.value;
	struct udp_media_addr *dst = (struct udp_media_addr *)&addr->value;
	struct udp_bearer *ub;
	int err = 0;

	if (skb_headroom(skb) < UDP_MIN_HEADROOM) {
		err = pskb_expand_head(skb, UDP_MIN_HEADROOM, 0, GFP_ATOMIC);
		if (err)
			goto tx_error;
	}

	skb_set_inner_protocol(skb, htons(ETH_P_TIPC));
	ub = rcu_dereference_rtnl(b->media_ptr);
	if (!ub) {
		err = -ENODEV;
		goto tx_error;
	}

	return tipc_udp_xmit(net, skb, ub, src, dst);

tx_error:
	kfree_skb(skb);
	return err;
}

/* tipc_udp_recv - read data from bearer socket */
static int tipc_udp_recv(struct sock *sk, struct sk_buff *skb)
{
	struct udp_bearer *ub;
	struct tipc_bearer *b;

	ub = rcu_dereference_sk_user_data(sk);
	if (!ub) {
		pr_err_ratelimited("Failed to get UDP bearer reference");
		kfree_skb(skb);
		return 0;
	}

	skb_pull(skb, sizeof(struct udphdr));
	rcu_read_lock();
	b = rcu_dereference_rtnl(ub->bearer);

	if (b && test_bit(0, &b->up)) {
		tipc_rcv(sock_net(sk), skb, b);
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();
	kfree_skb(skb);
	return 0;
}

static int enable_mcast(struct udp_bearer *ub, struct udp_media_addr *remote)
{
	int err = 0;
	struct ip_mreqn mreqn;
	struct sock *sk = ub->ubsock->sk;

	if (ntohs(remote->proto) == ETH_P_IP) {
		mreqn.imr_multiaddr = remote->ipv4;
		mreqn.imr_ifindex = ub->ifindex;
		err = ip_mc_join_group(sk, &mreqn);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		err = ipv6_stub->ipv6_sock_mc_join(sk, ub->ifindex,
						   &remote->ipv6);
#endif
	}
	return err;
}

/**
 * tipc_parse_udp_addr - build udp media address from netlink data
 * @nlattr:	netlink attribute containing sockaddr storage aligned address
 * @addr:	tipc media address to fill with address, port and protocol type
 * @scope_id:	IPv6 scope id pointer, not NULL indicates it's required
 */

static int tipc_parse_udp_addr(struct nlattr *nla, struct udp_media_addr *addr,
			       u32 *scope_id)
{
	struct sockaddr_storage sa;

	nla_memcpy(&sa, nla, sizeof(sa));
	if (sa.ss_family == AF_INET) {
		struct sockaddr_in *ip4 = (struct sockaddr_in *)&sa;

		addr->proto = htons(ETH_P_IP);
		addr->port = ip4->sin_port;
		addr->ipv4.s_addr = ip4->sin_addr.s_addr;
		return 0;

#if IS_ENABLED(CONFIG_IPV6)
	} else if (sa.ss_family == AF_INET6) {
		struct sockaddr_in6 *ip6 = (struct sockaddr_in6 *)&sa;

		addr->proto = htons(ETH_P_IPV6);
		addr->port = ip6->sin6_port;
		memcpy(&addr->ipv6, &ip6->sin6_addr, sizeof(struct in6_addr));

		/* Scope ID is only interesting for local addresses */
		if (scope_id) {
			int atype;

			atype = ipv6_addr_type(&ip6->sin6_addr);
			if (__ipv6_addr_needs_scope_id(atype) &&
			    !ip6->sin6_scope_id) {
				return -EINVAL;
			}

			*scope_id = ip6->sin6_scope_id ? : 0;
		}

		return 0;
#endif
	}
	return -EADDRNOTAVAIL;
}

/**
 * tipc_udp_enable - callback to create a new udp bearer instance
 * @net:	network namespace
 * @b:		pointer to generic tipc_bearer
 * @attrs:	netlink bearer configuration
 *
 * validate the bearer parameters and initialize the udp bearer
 * rtnl_lock should be held
 */
static int tipc_udp_enable(struct net *net, struct tipc_bearer *b,
			   struct nlattr *attrs[])
{
	int err = -EINVAL;
	struct udp_bearer *ub;
	struct udp_media_addr *remote;
	struct udp_media_addr local = {0};
	struct udp_port_cfg udp_conf = {0};
	struct udp_tunnel_sock_cfg tuncfg = {NULL};
	struct nlattr *opts[TIPC_NLA_UDP_MAX + 1];

	ub = kzalloc(sizeof(*ub), GFP_ATOMIC);
	if (!ub)
		return -ENOMEM;

	if (!attrs[TIPC_NLA_BEARER_UDP_OPTS])
		goto err;

	if (nla_parse_nested(opts, TIPC_NLA_UDP_MAX,
			     attrs[TIPC_NLA_BEARER_UDP_OPTS],
			     tipc_nl_udp_policy))
		goto err;

	if (!opts[TIPC_NLA_UDP_LOCAL] || !opts[TIPC_NLA_UDP_REMOTE]) {
		pr_err("Invalid UDP bearer configuration");
		return -EINVAL;
	}

	err = tipc_parse_udp_addr(opts[TIPC_NLA_UDP_LOCAL], &local,
				  &ub->ifindex);
	if (err)
		goto err;

	remote = (struct udp_media_addr *)&b->bcast_addr.value;
	memset(remote, 0, sizeof(struct udp_media_addr));
	err = tipc_parse_udp_addr(opts[TIPC_NLA_UDP_REMOTE], remote, NULL);
	if (err)
		goto err;

	b->bcast_addr.media_id = TIPC_MEDIA_TYPE_UDP;
	b->bcast_addr.broadcast = 1;
	rcu_assign_pointer(b->media_ptr, ub);
	rcu_assign_pointer(ub->bearer, b);
	tipc_udp_media_addr_set(&b->addr, &local);
	if (local.proto == htons(ETH_P_IP)) {
		struct net_device *dev;

		dev = __ip_dev_find(net, local.ipv4.s_addr, false);
		if (!dev) {
			err = -ENODEV;
			goto err;
		}
		udp_conf.family = AF_INET;
		udp_conf.local_ip.s_addr = htonl(INADDR_ANY);
		udp_conf.use_udp_checksums = false;
		ub->ifindex = dev->ifindex;
		b->mtu = dev->mtu - sizeof(struct iphdr)
			- sizeof(struct udphdr);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (local.proto == htons(ETH_P_IPV6)) {
		udp_conf.family = AF_INET6;
		udp_conf.use_udp6_tx_checksums = true;
		udp_conf.use_udp6_rx_checksums = true;
		udp_conf.local_ip6 = in6addr_any;
		b->mtu = 1280;
#endif
	} else {
		err = -EAFNOSUPPORT;
		goto err;
	}
	udp_conf.local_udp_port = local.port;
	err = udp_sock_create(net, &udp_conf, &ub->ubsock);
	if (err)
		goto err;
	tuncfg.sk_user_data = ub;
	tuncfg.encap_type = 1;
	tuncfg.encap_rcv = tipc_udp_recv;
	tuncfg.encap_destroy = NULL;
	setup_udp_tunnel_sock(net, ub->ubsock, &tuncfg);

	if (tipc_udp_is_mcast_addr(remote)) {
		if (enable_mcast(ub, remote))
			goto err;
	}

	return 0;
err:
	kfree(ub);
	return err;
}

/* cleanup_bearer - break the socket/bearer association */
static void cleanup_bearer(struct work_struct *work)
{
	struct udp_bearer *ub = container_of(work, struct udp_bearer, work);

	if (ub->ubsock)
		udp_tunnel_sock_release(ub->ubsock);
	synchronize_net();
	kfree(ub);
}

/* tipc_udp_disable - detach bearer from socket */
static void tipc_udp_disable(struct tipc_bearer *b)
{
	struct udp_bearer *ub;

	ub = rcu_dereference_rtnl(b->media_ptr);
	if (!ub) {
		pr_err("UDP bearer instance not found\n");
		return;
	}
	if (ub->ubsock)
		sock_set_flag(ub->ubsock->sk, SOCK_DEAD);
	RCU_INIT_POINTER(ub->bearer, NULL);

	/* sock_release need to be done outside of rtnl lock */
	INIT_WORK(&ub->work, cleanup_bearer);
	schedule_work(&ub->work);
}

struct tipc_media udp_media_info = {
	.send_msg	= tipc_udp_send_msg,
	.enable_media	= tipc_udp_enable,
	.disable_media	= tipc_udp_disable,
	.addr2str	= tipc_udp_addr2str,
	.addr2msg	= tipc_udp_addr2msg,
	.msg2addr	= tipc_udp_msg2addr,
	.priority	= TIPC_DEF_LINK_PRI,
	.tolerance	= TIPC_DEF_LINK_TOL,
	.window		= TIPC_DEF_LINK_WIN,
	.type_id	= TIPC_MEDIA_TYPE_UDP,
	.hwaddr_len	= 0,
	.name		= "udp"
};
