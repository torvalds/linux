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

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/openvswitch.h>
#include <linux/module.h>
#include <net/udp.h>
#include <net/ip_tunnels.h>
#include <net/rtnetlink.h>
#include <net/vxlan.h>

#include "datapath.h"
#include "vport.h"
#include "vport-netdev.h"

static struct vport_ops ovs_vxlan_netdev_vport_ops;

static int vxlan_get_options(const struct vport *vport, struct sk_buff *skb)
{
	struct vxlan_dev *vxlan = netdev_priv(vport->dev);
	__be16 dst_port = vxlan->cfg.dst_port;

	if (nla_put_u16(skb, OVS_TUNNEL_ATTR_DST_PORT, ntohs(dst_port)))
		return -EMSGSIZE;

	if (vxlan->flags & VXLAN_F_GBP) {
		struct nlattr *exts;

		exts = nla_nest_start(skb, OVS_TUNNEL_ATTR_EXTENSION);
		if (!exts)
			return -EMSGSIZE;

		if (vxlan->flags & VXLAN_F_GBP &&
		    nla_put_flag(skb, OVS_VXLAN_EXT_GBP))
			return -EMSGSIZE;

		nla_nest_end(skb, exts);
	}

	return 0;
}

static const struct nla_policy exts_policy[OVS_VXLAN_EXT_MAX + 1] = {
	[OVS_VXLAN_EXT_GBP]	= { .type = NLA_FLAG, },
};

static int vxlan_configure_exts(struct vport *vport, struct nlattr *attr,
				struct vxlan_config *conf)
{
	struct nlattr *exts[OVS_VXLAN_EXT_MAX + 1];
	int err;

	if (nla_len(attr) < sizeof(struct nlattr))
		return -EINVAL;

	err = nla_parse_nested(exts, OVS_VXLAN_EXT_MAX, attr, exts_policy);
	if (err < 0)
		return err;

	if (exts[OVS_VXLAN_EXT_GBP])
		conf->flags |= VXLAN_F_GBP;

	return 0;
}

static struct vport *vxlan_tnl_create(const struct vport_parms *parms)
{
	struct net *net = ovs_dp_get_net(parms->dp);
	struct nlattr *options = parms->options;
	struct net_device *dev;
	struct vport *vport;
	struct nlattr *a;
	int err;
	struct vxlan_config conf = {
		.no_share = true,
		.flags = VXLAN_F_COLLECT_METADATA | VXLAN_F_UDP_ZERO_CSUM6_RX,
		/* Don't restrict the packets that can be sent by MTU */
		.mtu = IP_MAX_MTU,
	};

	if (!options) {
		err = -EINVAL;
		goto error;
	}

	a = nla_find_nested(options, OVS_TUNNEL_ATTR_DST_PORT);
	if (a && nla_len(a) == sizeof(u16)) {
		conf.dst_port = htons(nla_get_u16(a));
	} else {
		/* Require destination port from userspace. */
		err = -EINVAL;
		goto error;
	}

	vport = ovs_vport_alloc(0, &ovs_vxlan_netdev_vport_ops, parms);
	if (IS_ERR(vport))
		return vport;

	a = nla_find_nested(options, OVS_TUNNEL_ATTR_EXTENSION);
	if (a) {
		err = vxlan_configure_exts(vport, a, &conf);
		if (err) {
			ovs_vport_free(vport);
			goto error;
		}
	}

	rtnl_lock();
	dev = vxlan_dev_create(net, parms->name, NET_NAME_USER, &conf);
	if (IS_ERR(dev)) {
		rtnl_unlock();
		ovs_vport_free(vport);
		return ERR_CAST(dev);
	}

	dev_change_flags(dev, dev->flags | IFF_UP);
	rtnl_unlock();
	return vport;
error:
	return ERR_PTR(err);
}

static struct vport *vxlan_create(const struct vport_parms *parms)
{
	struct vport *vport;

	vport = vxlan_tnl_create(parms);
	if (IS_ERR(vport))
		return vport;

	return ovs_netdev_link(vport, parms->name);
}

static struct vport_ops ovs_vxlan_netdev_vport_ops = {
	.type			= OVS_VPORT_TYPE_VXLAN,
	.create			= vxlan_create,
	.destroy		= ovs_netdev_tunnel_destroy,
	.get_options		= vxlan_get_options,
	.send			= dev_queue_xmit,
};

static int __init ovs_vxlan_tnl_init(void)
{
	return ovs_vport_ops_register(&ovs_vxlan_netdev_vport_ops);
}

static void __exit ovs_vxlan_tnl_exit(void)
{
	ovs_vport_ops_unregister(&ovs_vxlan_netdev_vport_ops);
}

module_init(ovs_vxlan_tnl_init);
module_exit(ovs_vxlan_tnl_exit);

MODULE_DESCRIPTION("OVS: VXLAN switching port");
MODULE_LICENSE("GPL");
MODULE_ALIAS("vport-type-4");
