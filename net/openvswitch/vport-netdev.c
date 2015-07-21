/*
 * Copyright (c) 2007-2012 Nicira, Inc.
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

#include <linux/if_arp.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/kernel.h>
#include <linux/llc.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/openvswitch.h>

#include <net/udp.h>
#include <net/ip_tunnels.h>
#include <net/rtnetlink.h>
#include <net/vxlan.h>

#include "datapath.h"
#include "vport.h"
#include "vport-internal_dev.h"
#include "vport-netdev.h"

static struct vport_ops ovs_netdev_vport_ops;

/* Must be called with rcu_read_lock. */
static void netdev_port_receive(struct vport *vport, struct sk_buff *skb)
{
	if (unlikely(!vport))
		goto error;

	if (unlikely(skb_warn_if_lro(skb)))
		goto error;

	/* Make our own copy of the packet.  Otherwise we will mangle the
	 * packet for anyone who came before us (e.g. tcpdump via AF_PACKET).
	 */
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		return;

	skb_push(skb, ETH_HLEN);
	ovs_skb_postpush_rcsum(skb, skb->data, ETH_HLEN);

	ovs_vport_receive(vport, skb, NULL);
	return;

error:
	kfree_skb(skb);
}

/* Called with rcu_read_lock and bottom-halves disabled. */
static rx_handler_result_t netdev_frame_hook(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct vport *vport;

	if (unlikely(skb->pkt_type == PACKET_LOOPBACK))
		return RX_HANDLER_PASS;

	vport = ovs_netdev_get_vport(skb->dev);

	netdev_port_receive(vport, skb);

	return RX_HANDLER_CONSUMED;
}

static struct net_device *get_dpdev(const struct datapath *dp)
{
	struct vport *local;

	local = ovs_vport_ovsl(dp, OVSP_LOCAL);
	BUG_ON(!local);
	return local->dev;
}

static struct vport *netdev_link(struct vport *vport, const char *name)
{
	int err;

	vport->dev = dev_get_by_name(ovs_dp_get_net(vport->dp), name);
	if (!vport->dev) {
		err = -ENODEV;
		goto error_free_vport;
	}

	if (vport->dev->flags & IFF_LOOPBACK ||
	    vport->dev->type != ARPHRD_ETHER ||
	    ovs_is_internal_dev(vport->dev)) {
		err = -EINVAL;
		goto error_put;
	}

	rtnl_lock();
	err = netdev_master_upper_dev_link(vport->dev,
					   get_dpdev(vport->dp));
	if (err)
		goto error_unlock;

	err = netdev_rx_handler_register(vport->dev, netdev_frame_hook,
					 vport);
	if (err)
		goto error_master_upper_dev_unlink;

	dev_disable_lro(vport->dev);
	dev_set_promiscuity(vport->dev, 1);
	vport->dev->priv_flags |= IFF_OVS_DATAPATH;
	rtnl_unlock();

	return vport;

error_master_upper_dev_unlink:
	netdev_upper_dev_unlink(vport->dev, get_dpdev(vport->dp));
error_unlock:
	rtnl_unlock();
error_put:
	dev_put(vport->dev);
error_free_vport:
	ovs_vport_free(vport);
	return ERR_PTR(err);
}

static struct vport *netdev_create(const struct vport_parms *parms)
{
	struct vport *vport;

	vport = ovs_vport_alloc(0, &ovs_netdev_vport_ops, parms);
	if (IS_ERR(vport))
		return vport;

	return netdev_link(vport, parms->name);
}

static void free_port_rcu(struct rcu_head *rcu)
{
	struct vport *vport = container_of(rcu, struct vport, rcu);

	if (vport->dev)
		dev_put(vport->dev);
	ovs_vport_free(vport);
}

void ovs_netdev_detach_dev(struct vport *vport)
{
	ASSERT_RTNL();
	vport->dev->priv_flags &= ~IFF_OVS_DATAPATH;
	netdev_rx_handler_unregister(vport->dev);
	netdev_upper_dev_unlink(vport->dev,
				netdev_master_upper_dev_get(vport->dev));
	dev_set_promiscuity(vport->dev, -1);
}

static void netdev_destroy(struct vport *vport)
{
	rtnl_lock();
	if (vport->dev->priv_flags & IFF_OVS_DATAPATH)
		ovs_netdev_detach_dev(vport);
	rtnl_unlock();

	call_rcu(&vport->rcu, free_port_rcu);
}

static unsigned int packet_length(const struct sk_buff *skb)
{
	unsigned int length = skb->len - ETH_HLEN;

	if (skb->protocol == htons(ETH_P_8021Q))
		length -= VLAN_HLEN;

	return length;
}

static int netdev_send(struct vport *vport, struct sk_buff *skb)
{
	int mtu = vport->dev->mtu;
	int len;

	if (unlikely(packet_length(skb) > mtu && !skb_is_gso(skb))) {
		net_warn_ratelimited("%s: dropped over-mtu packet: %d > %d\n",
				     vport->dev->name,
				     packet_length(skb), mtu);
		goto drop;
	}

	skb->dev = vport->dev;
	len = skb->len;
	dev_queue_xmit(skb);

	return len;

drop:
	kfree_skb(skb);
	return 0;
}

/* Returns null if this device is not attached to a datapath. */
struct vport *ovs_netdev_get_vport(struct net_device *dev)
{
	if (likely(dev->priv_flags & IFF_OVS_DATAPATH))
		return (struct vport *)
			rcu_dereference_rtnl(dev->rx_handler_data);
	else
		return NULL;
}

static struct vport_ops ovs_netdev_vport_ops = {
	.type		= OVS_VPORT_TYPE_NETDEV,
	.create		= netdev_create,
	.destroy	= netdev_destroy,
	.send		= netdev_send,
};

/* Compat code for old userspace. */
#if IS_ENABLED(CONFIG_VXLAN)
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
		.flags = VXLAN_F_FLOW_BASED | VXLAN_F_COLLECT_METADATA,
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

	return netdev_link(vport, parms->name);
}

static void vxlan_destroy(struct vport *vport)
{
	rtnl_lock();
	if (vport->dev->priv_flags & IFF_OVS_DATAPATH)
		ovs_netdev_detach_dev(vport);

	/* Early release so we can unregister the device */
	dev_put(vport->dev);
	rtnl_delete_link(vport->dev);
	vport->dev = NULL;
	rtnl_unlock();

	call_rcu(&vport->rcu, free_port_rcu);
}

static int vxlan_get_egress_tun_info(struct vport *vport, struct sk_buff *skb,
				     struct ip_tunnel_info *egress_tun_info)
{
	struct vxlan_dev *vxlan = netdev_priv(vport->dev);
	struct net *net = ovs_dp_get_net(vport->dp);
	__be16 dst_port = vxlan_dev_dst_port(vxlan);
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

static struct vport_ops ovs_vxlan_netdev_vport_ops = {
	.type		= OVS_VPORT_TYPE_VXLAN,
	.create		= vxlan_create,
	.destroy	= vxlan_destroy,
	.get_options	= vxlan_get_options,
	.send		= netdev_send,
	.get_egress_tun_info	= vxlan_get_egress_tun_info,
};

static int vxlan_compat_init(void)
{
	return ovs_vport_ops_register(&ovs_vxlan_netdev_vport_ops);
}

static void vxlan_compat_exit(void)
{
	ovs_vport_ops_unregister(&ovs_vxlan_netdev_vport_ops);
}
#else
static int vxlan_compat_init(void)
{
	return 0;
}

static void vxlan_compat_exit(void)
{
}
#endif

int __init ovs_netdev_init(void)
{
	int err;

	err = ovs_vport_ops_register(&ovs_netdev_vport_ops);
	if (err)
		return err;
	err = vxlan_compat_init();
	if (err)
		vxlan_compat_exit();
	return err;
}

void ovs_netdev_exit(void)
{
	ovs_vport_ops_unregister(&ovs_netdev_vport_ops);
	vxlan_compat_exit();
}
