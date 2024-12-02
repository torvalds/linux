// SPDX-License-Identifier: GPL-2.0-only
/*
 *	VLAN netlink control interface
 *
 * 	Copyright (c) 2007 Patrick McHardy <kaber@trash.net>
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/module.h>
#include <net/net_namespace.h>
#include <net/netlink.h>
#include <net/rtnetlink.h>
#include "vlan.h"


static const struct nla_policy vlan_policy[IFLA_VLAN_MAX + 1] = {
	[IFLA_VLAN_ID]		= { .type = NLA_U16 },
	[IFLA_VLAN_FLAGS]	= { .len = sizeof(struct ifla_vlan_flags) },
	[IFLA_VLAN_EGRESS_QOS]	= { .type = NLA_NESTED },
	[IFLA_VLAN_INGRESS_QOS] = { .type = NLA_NESTED },
	[IFLA_VLAN_PROTOCOL]	= { .type = NLA_U16 },
};

static const struct nla_policy vlan_map_policy[IFLA_VLAN_QOS_MAX + 1] = {
	[IFLA_VLAN_QOS_MAPPING] = { .len = sizeof(struct ifla_vlan_qos_mapping) },
};


static inline int vlan_validate_qos_map(struct nlattr *attr)
{
	if (!attr)
		return 0;
	return nla_validate_nested_deprecated(attr, IFLA_VLAN_QOS_MAX,
					      vlan_map_policy, NULL);
}

static int vlan_validate(struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	struct ifla_vlan_flags *flags;
	u16 id;
	int err;

	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid link address");
			return -EINVAL;
		}
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS]))) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid link address");
			return -EADDRNOTAVAIL;
		}
	}

	if (!data) {
		NL_SET_ERR_MSG_MOD(extack, "VLAN properties not specified");
		return -EINVAL;
	}

	if (data[IFLA_VLAN_PROTOCOL]) {
		switch (nla_get_be16(data[IFLA_VLAN_PROTOCOL])) {
		case htons(ETH_P_8021Q):
		case htons(ETH_P_8021AD):
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "Invalid VLAN protocol");
			return -EPROTONOSUPPORT;
		}
	}

	if (data[IFLA_VLAN_ID]) {
		id = nla_get_u16(data[IFLA_VLAN_ID]);
		if (id >= VLAN_VID_MASK) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid VLAN id");
			return -ERANGE;
		}
	}
	if (data[IFLA_VLAN_FLAGS]) {
		flags = nla_data(data[IFLA_VLAN_FLAGS]);
		if ((flags->flags & flags->mask) &
		    ~(VLAN_FLAG_REORDER_HDR | VLAN_FLAG_GVRP |
		      VLAN_FLAG_LOOSE_BINDING | VLAN_FLAG_MVRP |
		      VLAN_FLAG_BRIDGE_BINDING)) {
			NL_SET_ERR_MSG_MOD(extack, "Invalid VLAN flags");
			return -EINVAL;
		}
	}

	err = vlan_validate_qos_map(data[IFLA_VLAN_INGRESS_QOS]);
	if (err < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid ingress QOS map");
		return err;
	}
	err = vlan_validate_qos_map(data[IFLA_VLAN_EGRESS_QOS]);
	if (err < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid egress QOS map");
		return err;
	}
	return 0;
}

static int vlan_changelink(struct net_device *dev, struct nlattr *tb[],
			   struct nlattr *data[],
			   struct netlink_ext_ack *extack)
{
	struct ifla_vlan_flags *flags;
	struct ifla_vlan_qos_mapping *m;
	struct nlattr *attr;
	int rem, err;

	if (data[IFLA_VLAN_FLAGS]) {
		flags = nla_data(data[IFLA_VLAN_FLAGS]);
		err = vlan_dev_change_flags(dev, flags->flags, flags->mask);
		if (err)
			return err;
	}
	if (data[IFLA_VLAN_INGRESS_QOS]) {
		nla_for_each_nested(attr, data[IFLA_VLAN_INGRESS_QOS], rem) {
			if (nla_type(attr) != IFLA_VLAN_QOS_MAPPING)
				continue;
			m = nla_data(attr);
			vlan_dev_set_ingress_priority(dev, m->to, m->from);
		}
	}
	if (data[IFLA_VLAN_EGRESS_QOS]) {
		nla_for_each_nested(attr, data[IFLA_VLAN_EGRESS_QOS], rem) {
			if (nla_type(attr) != IFLA_VLAN_QOS_MAPPING)
				continue;
			m = nla_data(attr);
			err = vlan_dev_set_egress_priority(dev, m->from, m->to);
			if (err)
				return err;
		}
	}
	return 0;
}

static int vlan_newlink(struct net *src_net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct net_device *real_dev;
	unsigned int max_mtu;
	__be16 proto;
	int err;

	if (!data[IFLA_VLAN_ID]) {
		NL_SET_ERR_MSG_MOD(extack, "VLAN id not specified");
		return -EINVAL;
	}

	if (!tb[IFLA_LINK]) {
		NL_SET_ERR_MSG_MOD(extack, "link not specified");
		return -EINVAL;
	}

	real_dev = __dev_get_by_index(src_net, nla_get_u32(tb[IFLA_LINK]));
	if (!real_dev) {
		NL_SET_ERR_MSG_MOD(extack, "link does not exist");
		return -ENODEV;
	}

	if (data[IFLA_VLAN_PROTOCOL])
		proto = nla_get_be16(data[IFLA_VLAN_PROTOCOL]);
	else
		proto = htons(ETH_P_8021Q);

	vlan->vlan_proto = proto;
	vlan->vlan_id	 = nla_get_u16(data[IFLA_VLAN_ID]);
	vlan->real_dev	 = real_dev;
	dev->priv_flags |= (real_dev->priv_flags & IFF_XMIT_DST_RELEASE);
	vlan->flags	 = VLAN_FLAG_REORDER_HDR;

	err = vlan_check_real_dev(real_dev, vlan->vlan_proto, vlan->vlan_id,
				  extack);
	if (err < 0)
		return err;

	max_mtu = netif_reduces_vlan_mtu(real_dev) ? real_dev->mtu - VLAN_HLEN :
						     real_dev->mtu;
	if (!tb[IFLA_MTU])
		dev->mtu = max_mtu;
	else if (dev->mtu > max_mtu)
		return -EINVAL;

	/* Note: If this initial vlan_changelink() fails, we need
	 * to call vlan_dev_free_egress_priority() to free memory.
	 */
	err = vlan_changelink(dev, tb, data, extack);

	if (!err)
		err = register_vlan_dev(dev, extack);

	if (err)
		vlan_dev_free_egress_priority(dev);
	return err;
}

static inline size_t vlan_qos_map_size(unsigned int n)
{
	if (n == 0)
		return 0;
	/* IFLA_VLAN_{EGRESS,INGRESS}_QOS + n * IFLA_VLAN_QOS_MAPPING */
	return nla_total_size(sizeof(struct nlattr)) +
	       nla_total_size(sizeof(struct ifla_vlan_qos_mapping)) * n;
}

static size_t vlan_get_size(const struct net_device *dev)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);

	return nla_total_size(2) +	/* IFLA_VLAN_PROTOCOL */
	       nla_total_size(2) +	/* IFLA_VLAN_ID */
	       nla_total_size(sizeof(struct ifla_vlan_flags)) + /* IFLA_VLAN_FLAGS */
	       vlan_qos_map_size(vlan->nr_ingress_mappings) +
	       vlan_qos_map_size(vlan->nr_egress_mappings);
}

static int vlan_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct vlan_priority_tci_mapping *pm;
	struct ifla_vlan_flags f;
	struct ifla_vlan_qos_mapping m;
	struct nlattr *nest;
	unsigned int i;

	if (nla_put_be16(skb, IFLA_VLAN_PROTOCOL, vlan->vlan_proto) ||
	    nla_put_u16(skb, IFLA_VLAN_ID, vlan->vlan_id))
		goto nla_put_failure;
	if (vlan->flags) {
		f.flags = vlan->flags;
		f.mask  = ~0;
		if (nla_put(skb, IFLA_VLAN_FLAGS, sizeof(f), &f))
			goto nla_put_failure;
	}
	if (vlan->nr_ingress_mappings) {
		nest = nla_nest_start_noflag(skb, IFLA_VLAN_INGRESS_QOS);
		if (nest == NULL)
			goto nla_put_failure;

		for (i = 0; i < ARRAY_SIZE(vlan->ingress_priority_map); i++) {
			if (!vlan->ingress_priority_map[i])
				continue;

			m.from = i;
			m.to   = vlan->ingress_priority_map[i];
			if (nla_put(skb, IFLA_VLAN_QOS_MAPPING,
				    sizeof(m), &m))
				goto nla_put_failure;
		}
		nla_nest_end(skb, nest);
	}

	if (vlan->nr_egress_mappings) {
		nest = nla_nest_start_noflag(skb, IFLA_VLAN_EGRESS_QOS);
		if (nest == NULL)
			goto nla_put_failure;

		for (i = 0; i < ARRAY_SIZE(vlan->egress_priority_map); i++) {
			for (pm = vlan->egress_priority_map[i]; pm;
			     pm = pm->next) {
				if (!pm->vlan_qos)
					continue;

				m.from = pm->priority;
				m.to   = (pm->vlan_qos >> 13) & 0x7;
				if (nla_put(skb, IFLA_VLAN_QOS_MAPPING,
					    sizeof(m), &m))
					goto nla_put_failure;
			}
		}
		nla_nest_end(skb, nest);
	}
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static struct net *vlan_get_link_net(const struct net_device *dev)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;

	return dev_net(real_dev);
}

struct rtnl_link_ops vlan_link_ops __read_mostly = {
	.kind		= "vlan",
	.maxtype	= IFLA_VLAN_MAX,
	.policy		= vlan_policy,
	.priv_size	= sizeof(struct vlan_dev_priv),
	.setup		= vlan_setup,
	.validate	= vlan_validate,
	.newlink	= vlan_newlink,
	.changelink	= vlan_changelink,
	.dellink	= unregister_vlan_dev,
	.get_size	= vlan_get_size,
	.fill_info	= vlan_fill_info,
	.get_link_net	= vlan_get_link_net,
};

int __init vlan_netlink_init(void)
{
	return rtnl_link_register(&vlan_link_ops);
}

void __exit vlan_netlink_fini(void)
{
	rtnl_link_unregister(&vlan_link_ops);
}

MODULE_ALIAS_RTNL_LINK("vlan");
