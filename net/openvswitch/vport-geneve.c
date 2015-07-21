/*
 * Copyright (c) 2014 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/in.h>
#include <linux/ip.h>
#include <linux/net.h>
#include <linux/rculist.h>
#include <linux/udp.h>
#include <linux/if_vlan.h>
#include <linux/module.h>

#include <net/geneve.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/udp.h>
#include <net/xfrm.h>

#include "datapath.h"
#include "vport.h"

static struct vport_ops ovs_geneve_vport_ops;

/**
 * struct geneve_port - Keeps track of open UDP ports
 * @gs: The socket created for this port number.
 * @name: vport name.
 */
struct geneve_port {
	struct geneve_sock *gs;
	char name[IFNAMSIZ];
};

static LIST_HEAD(geneve_ports);

static inline struct geneve_port *geneve_vport(const struct vport *vport)
{
	return vport_priv(vport);
}

/* Convert 64 bit tunnel ID to 24 bit VNI. */
static void tunnel_id_to_vni(__be64 tun_id, __u8 *vni)
{
#ifdef __BIG_ENDIAN
	vni[0] = (__force __u8)(tun_id >> 16);
	vni[1] = (__force __u8)(tun_id >> 8);
	vni[2] = (__force __u8)tun_id;
#else
	vni[0] = (__force __u8)((__force u64)tun_id >> 40);
	vni[1] = (__force __u8)((__force u64)tun_id >> 48);
	vni[2] = (__force __u8)((__force u64)tun_id >> 56);
#endif
}

/* Convert 24 bit VNI to 64 bit tunnel ID. */
static __be64 vni_to_tunnel_id(const __u8 *vni)
{
#ifdef __BIG_ENDIAN
	return (vni[0] << 16) | (vni[1] << 8) | vni[2];
#else
	return (__force __be64)(((__force u64)vni[0] << 40) |
				((__force u64)vni[1] << 48) |
				((__force u64)vni[2] << 56));
#endif
}

static void geneve_rcv(struct geneve_sock *gs, struct sk_buff *skb)
{
	struct vport *vport = gs->rcv_data;
	struct genevehdr *geneveh = geneve_hdr(skb);
	int opts_len;
	struct ip_tunnel_info tun_info;
	__be64 key;
	__be16 flags;

	opts_len = geneveh->opt_len * 4;

	flags = TUNNEL_KEY | TUNNEL_GENEVE_OPT |
		(udp_hdr(skb)->check != 0 ? TUNNEL_CSUM : 0) |
		(geneveh->oam ? TUNNEL_OAM : 0) |
		(geneveh->critical ? TUNNEL_CRIT_OPT : 0);

	key = vni_to_tunnel_id(geneveh->vni);

	ip_tunnel_info_init(&tun_info, ip_hdr(skb),
			    udp_hdr(skb)->source, udp_hdr(skb)->dest,
			    key, flags, geneveh->options, opts_len);

	ovs_vport_receive(vport, skb, &tun_info);
}

static int geneve_get_options(const struct vport *vport,
			      struct sk_buff *skb)
{
	struct geneve_port *geneve_port = geneve_vport(vport);
	struct inet_sock *sk = inet_sk(geneve_port->gs->sock->sk);

	if (nla_put_u16(skb, OVS_TUNNEL_ATTR_DST_PORT, ntohs(sk->inet_sport)))
		return -EMSGSIZE;
	return 0;
}

static void geneve_tnl_destroy(struct vport *vport)
{
	struct geneve_port *geneve_port = geneve_vport(vport);

	geneve_sock_release(geneve_port->gs);

	ovs_vport_deferred_free(vport);
}

static struct vport *geneve_tnl_create(const struct vport_parms *parms)
{
	struct net *net = ovs_dp_get_net(parms->dp);
	struct nlattr *options = parms->options;
	struct geneve_port *geneve_port;
	struct geneve_sock *gs;
	struct vport *vport;
	struct nlattr *a;
	int err;
	u16 dst_port;

	if (!options) {
		err = -EINVAL;
		goto error;
	}

	a = nla_find_nested(options, OVS_TUNNEL_ATTR_DST_PORT);
	if (a && nla_len(a) == sizeof(u16)) {
		dst_port = nla_get_u16(a);
	} else {
		/* Require destination port from userspace. */
		err = -EINVAL;
		goto error;
	}

	vport = ovs_vport_alloc(sizeof(struct geneve_port),
				&ovs_geneve_vport_ops, parms);
	if (IS_ERR(vport))
		return vport;

	geneve_port = geneve_vport(vport);
	strncpy(geneve_port->name, parms->name, IFNAMSIZ);

	gs = geneve_sock_add(net, htons(dst_port), geneve_rcv, vport, true, 0);
	if (IS_ERR(gs)) {
		ovs_vport_free(vport);
		return (void *)gs;
	}
	geneve_port->gs = gs;

	return vport;
error:
	return ERR_PTR(err);
}

static int geneve_tnl_send(struct vport *vport, struct sk_buff *skb)
{
	const struct ip_tunnel_key *tun_key;
	struct ip_tunnel_info *tun_info;
	struct net *net = ovs_dp_get_net(vport->dp);
	struct geneve_port *geneve_port = geneve_vport(vport);
	__be16 dport = inet_sk(geneve_port->gs->sock->sk)->inet_sport;
	__be16 sport;
	struct rtable *rt;
	struct flowi4 fl;
	u8 vni[3], opts_len, *opts;
	__be16 df;
	int err;

	tun_info = OVS_CB(skb)->egress_tun_info;
	if (unlikely(!tun_info)) {
		err = -EINVAL;
		goto error;
	}

	tun_key = &tun_info->key;
	rt = ovs_tunnel_route_lookup(net, tun_key, skb->mark, &fl, IPPROTO_UDP);
	if (IS_ERR(rt)) {
		err = PTR_ERR(rt);
		goto error;
	}

	df = tun_key->tun_flags & TUNNEL_DONT_FRAGMENT ? htons(IP_DF) : 0;
	sport = udp_flow_src_port(net, skb, 1, USHRT_MAX, true);
	tunnel_id_to_vni(tun_key->tun_id, vni);
	skb->ignore_df = 1;

	if (tun_key->tun_flags & TUNNEL_GENEVE_OPT) {
		opts = (u8 *)tun_info->options;
		opts_len = tun_info->options_len;
	} else {
		opts = NULL;
		opts_len = 0;
	}

	err = geneve_xmit_skb(geneve_port->gs, rt, skb, fl.saddr,
			      tun_key->ipv4_dst, tun_key->ipv4_tos,
			      tun_key->ipv4_ttl, df, sport, dport,
			      tun_key->tun_flags, vni, opts_len, opts,
			      !!(tun_key->tun_flags & TUNNEL_CSUM), false);
	if (err < 0)
		ip_rt_put(rt);
	return err;

error:
	kfree_skb(skb);
	return err;
}

static const char *geneve_get_name(const struct vport *vport)
{
	struct geneve_port *geneve_port = geneve_vport(vport);

	return geneve_port->name;
}

static int geneve_get_egress_tun_info(struct vport *vport, struct sk_buff *skb,
				      struct ip_tunnel_info *egress_tun_info)
{
	struct geneve_port *geneve_port = geneve_vport(vport);
	struct net *net = ovs_dp_get_net(vport->dp);
	__be16 dport = inet_sk(geneve_port->gs->sock->sk)->inet_sport;
	__be16 sport = udp_flow_src_port(net, skb, 1, USHRT_MAX, true);

	/* Get tp_src and tp_dst, refert to geneve_build_header().
	 */
	return ovs_tunnel_get_egress_info(egress_tun_info,
					  ovs_dp_get_net(vport->dp),
					  OVS_CB(skb)->egress_tun_info,
					  IPPROTO_UDP, skb->mark, sport, dport);
}

static struct vport_ops ovs_geneve_vport_ops = {
	.type		= OVS_VPORT_TYPE_GENEVE,
	.create		= geneve_tnl_create,
	.destroy	= geneve_tnl_destroy,
	.get_name	= geneve_get_name,
	.get_options	= geneve_get_options,
	.send		= geneve_tnl_send,
	.owner          = THIS_MODULE,
	.get_egress_tun_info	= geneve_get_egress_tun_info,
};

static int __init ovs_geneve_tnl_init(void)
{
	return ovs_vport_ops_register(&ovs_geneve_vport_ops);
}

static void __exit ovs_geneve_tnl_exit(void)
{
	ovs_vport_ops_unregister(&ovs_geneve_vport_ops);
}

module_init(ovs_geneve_tnl_init);
module_exit(ovs_geneve_tnl_exit);

MODULE_DESCRIPTION("OVS: Geneve swiching port");
MODULE_LICENSE("GPL");
MODULE_ALIAS("vport-type-5");
