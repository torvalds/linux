/*
 * Copyright (c) 2014 Nicira, Inc.
 * Copyright (c) 2013 Cisco Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/in.h>
#include <linux/ip.h>
#include <linux/net.h>
#include <linux/rculist.h>
#include <linux/udp.h>
#include <linux/module.h>

#include <net/icmp.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/ip_tunnels.h>
#include <net/rtnetlink.h>
#include <net/route.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/vxlan.h>

#include "datapath.h"
#include "vport.h"
#include "vport-vxlan.h"

/**
 * struct vxlan_port - Keeps track of open UDP ports
 * @vs: vxlan_sock created for the port.
 * @name: vport name.
 */
struct vxlan_port {
	struct vxlan_sock *vs;
	char name[IFNAMSIZ];
	u32 exts; /* VXLAN_F_* in <net/vxlan.h> */
};

static struct vport_ops ovs_vxlan_vport_ops;

static inline struct vxlan_port *vxlan_vport(const struct vport *vport)
{
	return vport_priv(vport);
}

/* Called with rcu_read_lock and BH disabled. */
static void vxlan_rcv(struct vxlan_sock *vs, struct sk_buff *skb,
		      struct vxlan_metadata *md)
{
	struct ovs_tunnel_info tun_info;
	struct vxlan_port *vxlan_port;
	struct vport *vport = vs->data;
	struct iphdr *iph;
	struct ovs_vxlan_opts opts = {
		.gbp = md->gbp,
	};
	__be64 key;
	__be16 flags;

	flags = TUNNEL_KEY | (udp_hdr(skb)->check != 0 ? TUNNEL_CSUM : 0);
	vxlan_port = vxlan_vport(vport);
	if (vxlan_port->exts & VXLAN_F_GBP && md->gbp)
		flags |= TUNNEL_VXLAN_OPT;

	/* Save outer tunnel values */
	iph = ip_hdr(skb);
	key = cpu_to_be64(ntohl(md->vni) >> 8);
	ovs_flow_tun_info_init(&tun_info, iph,
			       udp_hdr(skb)->source, udp_hdr(skb)->dest,
			       key, flags, &opts, sizeof(opts));

	ovs_vport_receive(vport, skb, &tun_info);
}

static int vxlan_get_options(const struct vport *vport, struct sk_buff *skb)
{
	struct vxlan_port *vxlan_port = vxlan_vport(vport);
	__be16 dst_port = inet_sk(vxlan_port->vs->sock->sk)->inet_sport;

	if (nla_put_u16(skb, OVS_TUNNEL_ATTR_DST_PORT, ntohs(dst_port)))
		return -EMSGSIZE;

	if (vxlan_port->exts) {
		struct nlattr *exts;

		exts = nla_nest_start(skb, OVS_TUNNEL_ATTR_EXTENSION);
		if (!exts)
			return -EMSGSIZE;

		if (vxlan_port->exts & VXLAN_F_GBP &&
		    nla_put_flag(skb, OVS_VXLAN_EXT_GBP))
			return -EMSGSIZE;

		nla_nest_end(skb, exts);
	}

	return 0;
}

static void vxlan_tnl_destroy(struct vport *vport)
{
	struct vxlan_port *vxlan_port = vxlan_vport(vport);

	vxlan_sock_release(vxlan_port->vs);

	ovs_vport_deferred_free(vport);
}

static const struct nla_policy exts_policy[OVS_VXLAN_EXT_MAX+1] = {
	[OVS_VXLAN_EXT_GBP]	= { .type = NLA_FLAG, },
};

static int vxlan_configure_exts(struct vport *vport, struct nlattr *attr)
{
	struct nlattr *exts[OVS_VXLAN_EXT_MAX+1];
	struct vxlan_port *vxlan_port;
	int err;

	if (nla_len(attr) < sizeof(struct nlattr))
		return -EINVAL;

	err = nla_parse_nested(exts, OVS_VXLAN_EXT_MAX, attr, exts_policy);
	if (err < 0)
		return err;

	vxlan_port = vxlan_vport(vport);

	if (exts[OVS_VXLAN_EXT_GBP])
		vxlan_port->exts |= VXLAN_F_GBP;

	return 0;
}

static struct vport *vxlan_tnl_create(const struct vport_parms *parms)
{
	struct net *net = ovs_dp_get_net(parms->dp);
	struct nlattr *options = parms->options;
	struct vxlan_port *vxlan_port;
	struct vxlan_sock *vs;
	struct vport *vport;
	struct nlattr *a;
	u16 dst_port;
	int err;

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

	vport = ovs_vport_alloc(sizeof(struct vxlan_port),
				&ovs_vxlan_vport_ops, parms);
	if (IS_ERR(vport))
		return vport;

	vxlan_port = vxlan_vport(vport);
	strncpy(vxlan_port->name, parms->name, IFNAMSIZ);

	a = nla_find_nested(options, OVS_TUNNEL_ATTR_EXTENSION);
	if (a) {
		err = vxlan_configure_exts(vport, a);
		if (err) {
			ovs_vport_free(vport);
			goto error;
		}
	}

	vs = vxlan_sock_add(net, htons(dst_port), vxlan_rcv, vport, true,
			    vxlan_port->exts);
	if (IS_ERR(vs)) {
		ovs_vport_free(vport);
		return (void *)vs;
	}
	vxlan_port->vs = vs;

	return vport;

error:
	return ERR_PTR(err);
}

static int vxlan_ext_gbp(struct sk_buff *skb)
{
	const struct ovs_tunnel_info *tun_info;
	const struct ovs_vxlan_opts *opts;

	tun_info = OVS_CB(skb)->egress_tun_info;
	opts = tun_info->options;

	if (tun_info->tunnel.tun_flags & TUNNEL_VXLAN_OPT &&
	    tun_info->options_len >= sizeof(*opts))
		return opts->gbp;
	else
		return 0;
}

static int vxlan_tnl_send(struct vport *vport, struct sk_buff *skb)
{
	struct net *net = ovs_dp_get_net(vport->dp);
	struct vxlan_port *vxlan_port = vxlan_vport(vport);
	__be16 dst_port = inet_sk(vxlan_port->vs->sock->sk)->inet_sport;
	const struct ovs_key_ipv4_tunnel *tun_key;
	struct vxlan_metadata md = {0};
	struct rtable *rt;
	struct flowi4 fl;
	__be16 src_port;
	__be16 df;
	int err;
	u32 vxflags;

	if (unlikely(!OVS_CB(skb)->egress_tun_info)) {
		err = -EINVAL;
		goto error;
	}

	tun_key = &OVS_CB(skb)->egress_tun_info->tunnel;
	rt = ovs_tunnel_route_lookup(net, tun_key, skb->mark, &fl, IPPROTO_UDP);
	if (IS_ERR(rt)) {
		err = PTR_ERR(rt);
		goto error;
	}

	df = tun_key->tun_flags & TUNNEL_DONT_FRAGMENT ?
		htons(IP_DF) : 0;

	skb->ignore_df = 1;

	src_port = udp_flow_src_port(net, skb, 0, 0, true);
	md.vni = htonl(be64_to_cpu(tun_key->tun_id) << 8);
	md.gbp = vxlan_ext_gbp(skb);
	vxflags = vxlan_port->exts |
		      (tun_key->tun_flags & TUNNEL_CSUM ? VXLAN_F_UDP_CSUM : 0);

	err = vxlan_xmit_skb(rt, skb, fl.saddr, tun_key->ipv4_dst,
			     tun_key->ipv4_tos, tun_key->ipv4_ttl, df,
			     src_port, dst_port,
			     &md, false, vxflags);
	if (err < 0)
		ip_rt_put(rt);
	return err;
error:
	kfree_skb(skb);
	return err;
}

static int vxlan_get_egress_tun_info(struct vport *vport, struct sk_buff *skb,
				     struct ovs_tunnel_info *egress_tun_info)
{
	struct net *net = ovs_dp_get_net(vport->dp);
	struct vxlan_port *vxlan_port = vxlan_vport(vport);
	__be16 dst_port = inet_sk(vxlan_port->vs->sock->sk)->inet_sport;
	__be16 src_port;
	int port_min;
	int port_max;

	inet_get_local_port_range(net, &port_min, &port_max);
	src_port = udp_flow_src_port(net, skb, 0, 0, true);

	return ovs_tunnel_get_egress_info(egress_tun_info, net,
					  OVS_CB(skb)->egress_tun_info,
					  IPPROTO_UDP, skb->mark,
					  src_port, dst_port);
}

static const char *vxlan_get_name(const struct vport *vport)
{
	struct vxlan_port *vxlan_port = vxlan_vport(vport);
	return vxlan_port->name;
}

static struct vport_ops ovs_vxlan_vport_ops = {
	.type		= OVS_VPORT_TYPE_VXLAN,
	.create		= vxlan_tnl_create,
	.destroy	= vxlan_tnl_destroy,
	.get_name	= vxlan_get_name,
	.get_options	= vxlan_get_options,
	.send		= vxlan_tnl_send,
	.get_egress_tun_info	= vxlan_get_egress_tun_info,
	.owner		= THIS_MODULE,
};

static int __init ovs_vxlan_tnl_init(void)
{
	return ovs_vport_ops_register(&ovs_vxlan_vport_ops);
}

static void __exit ovs_vxlan_tnl_exit(void)
{
	ovs_vport_ops_unregister(&ovs_vxlan_vport_ops);
}

module_init(ovs_vxlan_tnl_init);
module_exit(ovs_vxlan_tnl_exit);

MODULE_DESCRIPTION("OVS: VXLAN switching port");
MODULE_LICENSE("GPL");
MODULE_ALIAS("vport-type-4");
