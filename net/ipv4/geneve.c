/*
 * Geneve: Generic Network Virtualization Encapsulation
 *
 * Copyright (c) 2014 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/rculist.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/igmp.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/hash.h>
#include <linux/ethtool.h>
#include <net/arp.h>
#include <net/ndisc.h>
#include <net/ip.h>
#include <net/ip_tunnels.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/rtnetlink.h>
#include <net/route.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/geneve.h>
#include <net/protocol.h>
#include <net/udp_tunnel.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/ip6_tunnel.h>
#include <net/ip6_checksum.h>
#endif

#define PORT_HASH_BITS 8
#define PORT_HASH_SIZE (1<<PORT_HASH_BITS)

/* per-network namespace private data for this module */
struct geneve_net {
	struct hlist_head	sock_list[PORT_HASH_SIZE];
	spinlock_t		sock_lock;   /* Protects sock_list */
};

static int geneve_net_id;

static struct workqueue_struct *geneve_wq;

static inline struct genevehdr *geneve_hdr(const struct sk_buff *skb)
{
	return (struct genevehdr *)(udp_hdr(skb) + 1);
}

static struct hlist_head *gs_head(struct net *net, __be16 port)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);

	return &gn->sock_list[hash_32(ntohs(port), PORT_HASH_BITS)];
}

/* Find geneve socket based on network namespace and UDP port */
static struct geneve_sock *geneve_find_sock(struct net *net, __be16 port)
{
	struct geneve_sock *gs;

	hlist_for_each_entry_rcu(gs, gs_head(net, port), hlist) {
		if (inet_sk(gs->sock->sk)->inet_sport == port)
			return gs;
	}

	return NULL;
}

static void geneve_build_header(struct genevehdr *geneveh,
				__be16 tun_flags, u8 vni[3],
				u8 options_len, u8 *options)
{
	geneveh->ver = GENEVE_VER;
	geneveh->opt_len = options_len / 4;
	geneveh->oam = !!(tun_flags & TUNNEL_OAM);
	geneveh->critical = !!(tun_flags & TUNNEL_CRIT_OPT);
	geneveh->rsvd1 = 0;
	memcpy(geneveh->vni, vni, 3);
	geneveh->proto_type = htons(ETH_P_TEB);
	geneveh->rsvd2 = 0;

	memcpy(geneveh->options, options, options_len);
}

/* Transmit a fully formatted Geneve frame.
 *
 * When calling this function. The skb->data should point
 * to the geneve header which is fully formed.
 *
 * This function will add other UDP tunnel headers.
 */
int geneve_xmit_skb(struct geneve_sock *gs, struct rtable *rt,
		    struct sk_buff *skb, __be32 src, __be32 dst, __u8 tos,
		    __u8 ttl, __be16 df, __be16 src_port, __be16 dst_port,
		    __be16 tun_flags, u8 vni[3], u8 opt_len, u8 *opt,
		    bool xnet)
{
	struct genevehdr *gnvh;
	int min_headroom;
	int err;

	skb = udp_tunnel_handle_offloads(skb, !gs->sock->sk->sk_no_check_tx);

	min_headroom = LL_RESERVED_SPACE(rt->dst.dev) + rt->dst.header_len
			+ GENEVE_BASE_HLEN + opt_len + sizeof(struct iphdr)
			+ (vlan_tx_tag_present(skb) ? VLAN_HLEN : 0);

	err = skb_cow_head(skb, min_headroom);
	if (unlikely(err))
		return err;

	skb = vlan_hwaccel_push_inside(skb);
	if (unlikely(!skb))
		return -ENOMEM;

	gnvh = (struct genevehdr *)__skb_push(skb, sizeof(*gnvh) + opt_len);
	geneve_build_header(gnvh, tun_flags, vni, opt_len, opt);

	skb_set_inner_protocol(skb, htons(ETH_P_TEB));

	return udp_tunnel_xmit_skb(gs->sock, rt, skb, src, dst,
				   tos, ttl, df, src_port, dst_port, xnet);
}
EXPORT_SYMBOL_GPL(geneve_xmit_skb);

static void geneve_notify_add_rx_port(struct geneve_sock *gs)
{
	struct sock *sk = gs->sock->sk;
	sa_family_t sa_family = sk->sk_family;
	int err;

	if (sa_family == AF_INET) {
		err = udp_add_offload(&gs->udp_offloads);
		if (err)
			pr_warn("geneve: udp_add_offload failed with status %d\n",
				err);
	}
}

static void geneve_notify_del_rx_port(struct geneve_sock *gs)
{
	struct sock *sk = gs->sock->sk;
	sa_family_t sa_family = sk->sk_family;

	if (sa_family == AF_INET)
		udp_del_offload(&gs->udp_offloads);
}

/* Callback from net/ipv4/udp.c to receive packets */
static int geneve_udp_encap_recv(struct sock *sk, struct sk_buff *skb)
{
	struct genevehdr *geneveh;
	struct geneve_sock *gs;
	int opts_len;

	/* Need Geneve and inner Ethernet header to be present */
	if (unlikely(!pskb_may_pull(skb, GENEVE_BASE_HLEN)))
		goto error;

	/* Return packets with reserved bits set */
	geneveh = geneve_hdr(skb);

	if (unlikely(geneveh->ver != GENEVE_VER))
		goto error;

	if (unlikely(geneveh->proto_type != htons(ETH_P_TEB)))
		goto error;

	opts_len = geneveh->opt_len * 4;
	if (iptunnel_pull_header(skb, GENEVE_BASE_HLEN + opts_len,
				 htons(ETH_P_TEB)))
		goto drop;

	gs = rcu_dereference_sk_user_data(sk);
	if (!gs)
		goto drop;

	gs->rcv(gs, skb);
	return 0;

drop:
	/* Consume bad packet */
	kfree_skb(skb);
	return 0;

error:
	/* Let the UDP layer deal with the skb */
	return 1;
}

static void geneve_del_work(struct work_struct *work)
{
	struct geneve_sock *gs = container_of(work, struct geneve_sock,
					      del_work);

	udp_tunnel_sock_release(gs->sock);
	kfree_rcu(gs, rcu);
}

static struct socket *geneve_create_sock(struct net *net, bool ipv6,
					 __be16 port)
{
	struct socket *sock;
	struct udp_port_cfg udp_conf;
	int err;

	memset(&udp_conf, 0, sizeof(udp_conf));

	if (ipv6) {
		udp_conf.family = AF_INET6;
	} else {
		udp_conf.family = AF_INET;
		udp_conf.local_ip.s_addr = htonl(INADDR_ANY);
	}

	udp_conf.local_udp_port = port;

	/* Open UDP socket */
	err = udp_sock_create(net, &udp_conf, &sock);
	if (err < 0)
		return ERR_PTR(err);

	return sock;
}

/* Create new listen socket if needed */
static struct geneve_sock *geneve_socket_create(struct net *net, __be16 port,
						geneve_rcv_t *rcv, void *data,
						bool ipv6)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_sock *gs;
	struct socket *sock;
	struct udp_tunnel_sock_cfg tunnel_cfg;

	gs = kzalloc(sizeof(*gs), GFP_KERNEL);
	if (!gs)
		return ERR_PTR(-ENOMEM);

	INIT_WORK(&gs->del_work, geneve_del_work);

	sock = geneve_create_sock(net, ipv6, port);
	if (IS_ERR(sock)) {
		kfree(gs);
		return ERR_CAST(sock);
	}

	gs->sock = sock;
	atomic_set(&gs->refcnt, 1);
	gs->rcv = rcv;
	gs->rcv_data = data;

	/* Initialize the geneve udp offloads structure */
	gs->udp_offloads.port = port;
	gs->udp_offloads.callbacks.gro_receive = NULL;
	gs->udp_offloads.callbacks.gro_complete = NULL;

	spin_lock(&gn->sock_lock);
	hlist_add_head_rcu(&gs->hlist, gs_head(net, port));
	geneve_notify_add_rx_port(gs);
	spin_unlock(&gn->sock_lock);

	/* Mark socket as an encapsulation socket */
	tunnel_cfg.sk_user_data = gs;
	tunnel_cfg.encap_type = 1;
	tunnel_cfg.encap_rcv = geneve_udp_encap_recv;
	tunnel_cfg.encap_destroy = NULL;
	setup_udp_tunnel_sock(net, sock, &tunnel_cfg);

	return gs;
}

struct geneve_sock *geneve_sock_add(struct net *net, __be16 port,
				    geneve_rcv_t *rcv, void *data,
				    bool no_share, bool ipv6)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_sock *gs;

	gs = geneve_socket_create(net, port, rcv, data, ipv6);
	if (!IS_ERR(gs))
		return gs;

	if (no_share)	/* Return error if sharing is not allowed. */
		return ERR_PTR(-EINVAL);

	spin_lock(&gn->sock_lock);
	gs = geneve_find_sock(net, port);
	if (gs && ((gs->rcv != rcv) ||
		   !atomic_add_unless(&gs->refcnt, 1, 0)))
			gs = ERR_PTR(-EBUSY);
	spin_unlock(&gn->sock_lock);

	if (!gs)
		gs = ERR_PTR(-EINVAL);

	return gs;
}
EXPORT_SYMBOL_GPL(geneve_sock_add);

void geneve_sock_release(struct geneve_sock *gs)
{
	struct net *net = sock_net(gs->sock->sk);
	struct geneve_net *gn = net_generic(net, geneve_net_id);

	if (!atomic_dec_and_test(&gs->refcnt))
		return;

	spin_lock(&gn->sock_lock);
	hlist_del_rcu(&gs->hlist);
	geneve_notify_del_rx_port(gs);
	spin_unlock(&gn->sock_lock);

	queue_work(geneve_wq, &gs->del_work);
}
EXPORT_SYMBOL_GPL(geneve_sock_release);

static __net_init int geneve_init_net(struct net *net)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	unsigned int h;

	spin_lock_init(&gn->sock_lock);

	for (h = 0; h < PORT_HASH_SIZE; ++h)
		INIT_HLIST_HEAD(&gn->sock_list[h]);

	return 0;
}

static struct pernet_operations geneve_net_ops = {
	.init = geneve_init_net,
	.exit = NULL,
	.id   = &geneve_net_id,
	.size = sizeof(struct geneve_net),
};

static int __init geneve_init_module(void)
{
	int rc;

	geneve_wq = alloc_workqueue("geneve", 0, 0);
	if (!geneve_wq)
		return -ENOMEM;

	rc = register_pernet_subsys(&geneve_net_ops);
	if (rc)
		return rc;

	pr_info("Geneve driver\n");

	return 0;
}
late_initcall(geneve_init_module);

static void __exit geneve_cleanup_module(void)
{
	destroy_workqueue(geneve_wq);
	unregister_pernet_subsys(&geneve_net_ops);
}
module_exit(geneve_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jesse Gross <jesse@nicira.com>");
MODULE_DESCRIPTION("Driver for GENEVE encapsulated traffic");
MODULE_ALIAS_RTNL_LINK("geneve");
