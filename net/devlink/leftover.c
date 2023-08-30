// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/core/devlink.c - Network physical/parent device Netlink interface
 *
 * Heavily inspired by net/wireless/
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/refcount.h>
#include <linux/workqueue.h>
#include <linux/u64_stats_sync.h>
#include <linux/timekeeping.h>
#include <rdma/ib_verbs.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/devlink.h>
#define CREATE_TRACE_POINTS
#include <trace/events/devlink.h>

#include "devl_internal.h"

struct devlink_linecard {
	struct list_head list;
	struct devlink *devlink;
	unsigned int index;
	const struct devlink_linecard_ops *ops;
	void *priv;
	enum devlink_linecard_state state;
	struct mutex state_lock; /* Protects state */
	const char *type;
	struct devlink_linecard_type *types;
	unsigned int types_count;
	struct devlink *nested_devlink;
};

/**
 * struct devlink_resource - devlink resource
 * @name: name of the resource
 * @id: id, per devlink instance
 * @size: size of the resource
 * @size_new: updated size of the resource, reload is needed
 * @size_valid: valid in case the total size of the resource is valid
 *              including its children
 * @parent: parent resource
 * @size_params: size parameters
 * @list: parent list
 * @resource_list: list of child resources
 * @occ_get: occupancy getter callback
 * @occ_get_priv: occupancy getter callback priv
 */
struct devlink_resource {
	const char *name;
	u64 id;
	u64 size;
	u64 size_new;
	bool size_valid;
	struct devlink_resource *parent;
	struct devlink_resource_size_params size_params;
	struct list_head list;
	struct list_head resource_list;
	devlink_resource_occ_get_t *occ_get;
	void *occ_get_priv;
};

static struct devlink_dpipe_field devlink_dpipe_fields_ethernet[] = {
	{
		.name = "destination mac",
		.id = DEVLINK_DPIPE_FIELD_ETHERNET_DST_MAC,
		.bitwidth = 48,
	},
};

struct devlink_dpipe_header devlink_dpipe_header_ethernet = {
	.name = "ethernet",
	.id = DEVLINK_DPIPE_HEADER_ETHERNET,
	.fields = devlink_dpipe_fields_ethernet,
	.fields_count = ARRAY_SIZE(devlink_dpipe_fields_ethernet),
	.global = true,
};
EXPORT_SYMBOL_GPL(devlink_dpipe_header_ethernet);

static struct devlink_dpipe_field devlink_dpipe_fields_ipv4[] = {
	{
		.name = "destination ip",
		.id = DEVLINK_DPIPE_FIELD_IPV4_DST_IP,
		.bitwidth = 32,
	},
};

struct devlink_dpipe_header devlink_dpipe_header_ipv4 = {
	.name = "ipv4",
	.id = DEVLINK_DPIPE_HEADER_IPV4,
	.fields = devlink_dpipe_fields_ipv4,
	.fields_count = ARRAY_SIZE(devlink_dpipe_fields_ipv4),
	.global = true,
};
EXPORT_SYMBOL_GPL(devlink_dpipe_header_ipv4);

static struct devlink_dpipe_field devlink_dpipe_fields_ipv6[] = {
	{
		.name = "destination ip",
		.id = DEVLINK_DPIPE_FIELD_IPV6_DST_IP,
		.bitwidth = 128,
	},
};

struct devlink_dpipe_header devlink_dpipe_header_ipv6 = {
	.name = "ipv6",
	.id = DEVLINK_DPIPE_HEADER_IPV6,
	.fields = devlink_dpipe_fields_ipv6,
	.fields_count = ARRAY_SIZE(devlink_dpipe_fields_ipv6),
	.global = true,
};
EXPORT_SYMBOL_GPL(devlink_dpipe_header_ipv6);

EXPORT_TRACEPOINT_SYMBOL_GPL(devlink_hwmsg);
EXPORT_TRACEPOINT_SYMBOL_GPL(devlink_hwerr);
EXPORT_TRACEPOINT_SYMBOL_GPL(devlink_trap_report);

#define DEVLINK_PORT_FN_CAPS_VALID_MASK \
	(_BITUL(__DEVLINK_PORT_FN_ATTR_CAPS_MAX) - 1)

static const struct nla_policy devlink_function_nl_policy[DEVLINK_PORT_FUNCTION_ATTR_MAX + 1] = {
	[DEVLINK_PORT_FUNCTION_ATTR_HW_ADDR] = { .type = NLA_BINARY },
	[DEVLINK_PORT_FN_ATTR_STATE] =
		NLA_POLICY_RANGE(NLA_U8, DEVLINK_PORT_FN_STATE_INACTIVE,
				 DEVLINK_PORT_FN_STATE_ACTIVE),
	[DEVLINK_PORT_FN_ATTR_CAPS] =
		NLA_POLICY_BITFIELD32(DEVLINK_PORT_FN_CAPS_VALID_MASK),
};

#define ASSERT_DEVLINK_PORT_REGISTERED(devlink_port)				\
	WARN_ON_ONCE(!(devlink_port)->registered)
#define ASSERT_DEVLINK_PORT_NOT_REGISTERED(devlink_port)			\
	WARN_ON_ONCE((devlink_port)->registered)
#define ASSERT_DEVLINK_PORT_INITIALIZED(devlink_port)				\
	WARN_ON_ONCE(!(devlink_port)->initialized)

static struct devlink_port *devlink_port_get_by_index(struct devlink *devlink,
						      unsigned int port_index)
{
	return xa_load(&devlink->ports, port_index);
}

struct devlink_port *devlink_port_get_from_attrs(struct devlink *devlink,
						 struct nlattr **attrs)
{
	if (attrs[DEVLINK_ATTR_PORT_INDEX]) {
		u32 port_index = nla_get_u32(attrs[DEVLINK_ATTR_PORT_INDEX]);
		struct devlink_port *devlink_port;

		devlink_port = devlink_port_get_by_index(devlink, port_index);
		if (!devlink_port)
			return ERR_PTR(-ENODEV);
		return devlink_port;
	}
	return ERR_PTR(-EINVAL);
}

struct devlink_port *devlink_port_get_from_info(struct devlink *devlink,
						struct genl_info *info)
{
	return devlink_port_get_from_attrs(devlink, info->attrs);
}

static inline bool
devlink_rate_is_leaf(struct devlink_rate *devlink_rate)
{
	return devlink_rate->type == DEVLINK_RATE_TYPE_LEAF;
}

static inline bool
devlink_rate_is_node(struct devlink_rate *devlink_rate)
{
	return devlink_rate->type == DEVLINK_RATE_TYPE_NODE;
}

static struct devlink_rate *
devlink_rate_leaf_get_from_info(struct devlink *devlink, struct genl_info *info)
{
	struct devlink_rate *devlink_rate;
	struct devlink_port *devlink_port;

	devlink_port = devlink_port_get_from_attrs(devlink, info->attrs);
	if (IS_ERR(devlink_port))
		return ERR_CAST(devlink_port);
	devlink_rate = devlink_port->devlink_rate;
	return devlink_rate ?: ERR_PTR(-ENODEV);
}

static struct devlink_rate *
devlink_rate_node_get_by_name(struct devlink *devlink, const char *node_name)
{
	static struct devlink_rate *devlink_rate;

	list_for_each_entry(devlink_rate, &devlink->rate_list, list) {
		if (devlink_rate_is_node(devlink_rate) &&
		    !strcmp(node_name, devlink_rate->name))
			return devlink_rate;
	}
	return ERR_PTR(-ENODEV);
}

static struct devlink_rate *
devlink_rate_node_get_from_attrs(struct devlink *devlink, struct nlattr **attrs)
{
	const char *rate_node_name;
	size_t len;

	if (!attrs[DEVLINK_ATTR_RATE_NODE_NAME])
		return ERR_PTR(-EINVAL);
	rate_node_name = nla_data(attrs[DEVLINK_ATTR_RATE_NODE_NAME]);
	len = strlen(rate_node_name);
	/* Name cannot be empty or decimal number */
	if (!len || strspn(rate_node_name, "0123456789") == len)
		return ERR_PTR(-EINVAL);

	return devlink_rate_node_get_by_name(devlink, rate_node_name);
}

struct devlink_rate *
devlink_rate_node_get_from_info(struct devlink *devlink, struct genl_info *info)
{
	return devlink_rate_node_get_from_attrs(devlink, info->attrs);
}

struct devlink_rate *
devlink_rate_get_from_info(struct devlink *devlink, struct genl_info *info)
{
	struct nlattr **attrs = info->attrs;

	if (attrs[DEVLINK_ATTR_PORT_INDEX])
		return devlink_rate_leaf_get_from_info(devlink, info);
	else if (attrs[DEVLINK_ATTR_RATE_NODE_NAME])
		return devlink_rate_node_get_from_info(devlink, info);
	else
		return ERR_PTR(-EINVAL);
}

static struct devlink_linecard *
devlink_linecard_get_by_index(struct devlink *devlink,
			      unsigned int linecard_index)
{
	struct devlink_linecard *devlink_linecard;

	list_for_each_entry(devlink_linecard, &devlink->linecard_list, list) {
		if (devlink_linecard->index == linecard_index)
			return devlink_linecard;
	}
	return NULL;
}

static bool devlink_linecard_index_exists(struct devlink *devlink,
					  unsigned int linecard_index)
{
	return devlink_linecard_get_by_index(devlink, linecard_index);
}

static struct devlink_linecard *
devlink_linecard_get_from_attrs(struct devlink *devlink, struct nlattr **attrs)
{
	if (attrs[DEVLINK_ATTR_LINECARD_INDEX]) {
		u32 linecard_index = nla_get_u32(attrs[DEVLINK_ATTR_LINECARD_INDEX]);
		struct devlink_linecard *linecard;

		linecard = devlink_linecard_get_by_index(devlink, linecard_index);
		if (!linecard)
			return ERR_PTR(-ENODEV);
		return linecard;
	}
	return ERR_PTR(-EINVAL);
}

struct devlink_linecard *
devlink_linecard_get_from_info(struct devlink *devlink, struct genl_info *info)
{
	return devlink_linecard_get_from_attrs(devlink, info->attrs);
}

struct devlink_sb {
	struct list_head list;
	unsigned int index;
	u32 size;
	u16 ingress_pools_count;
	u16 egress_pools_count;
	u16 ingress_tc_count;
	u16 egress_tc_count;
};

static u16 devlink_sb_pool_count(struct devlink_sb *devlink_sb)
{
	return devlink_sb->ingress_pools_count + devlink_sb->egress_pools_count;
}

static struct devlink_sb *devlink_sb_get_by_index(struct devlink *devlink,
						  unsigned int sb_index)
{
	struct devlink_sb *devlink_sb;

	list_for_each_entry(devlink_sb, &devlink->sb_list, list) {
		if (devlink_sb->index == sb_index)
			return devlink_sb;
	}
	return NULL;
}

static bool devlink_sb_index_exists(struct devlink *devlink,
				    unsigned int sb_index)
{
	return devlink_sb_get_by_index(devlink, sb_index);
}

static struct devlink_sb *devlink_sb_get_from_attrs(struct devlink *devlink,
						    struct nlattr **attrs)
{
	if (attrs[DEVLINK_ATTR_SB_INDEX]) {
		u32 sb_index = nla_get_u32(attrs[DEVLINK_ATTR_SB_INDEX]);
		struct devlink_sb *devlink_sb;

		devlink_sb = devlink_sb_get_by_index(devlink, sb_index);
		if (!devlink_sb)
			return ERR_PTR(-ENODEV);
		return devlink_sb;
	}
	return ERR_PTR(-EINVAL);
}

static struct devlink_sb *devlink_sb_get_from_info(struct devlink *devlink,
						   struct genl_info *info)
{
	return devlink_sb_get_from_attrs(devlink, info->attrs);
}

static int devlink_sb_pool_index_get_from_attrs(struct devlink_sb *devlink_sb,
						struct nlattr **attrs,
						u16 *p_pool_index)
{
	u16 val;

	if (!attrs[DEVLINK_ATTR_SB_POOL_INDEX])
		return -EINVAL;

	val = nla_get_u16(attrs[DEVLINK_ATTR_SB_POOL_INDEX]);
	if (val >= devlink_sb_pool_count(devlink_sb))
		return -EINVAL;
	*p_pool_index = val;
	return 0;
}

static int devlink_sb_pool_index_get_from_info(struct devlink_sb *devlink_sb,
					       struct genl_info *info,
					       u16 *p_pool_index)
{
	return devlink_sb_pool_index_get_from_attrs(devlink_sb, info->attrs,
						    p_pool_index);
}

static int
devlink_sb_pool_type_get_from_attrs(struct nlattr **attrs,
				    enum devlink_sb_pool_type *p_pool_type)
{
	u8 val;

	if (!attrs[DEVLINK_ATTR_SB_POOL_TYPE])
		return -EINVAL;

	val = nla_get_u8(attrs[DEVLINK_ATTR_SB_POOL_TYPE]);
	if (val != DEVLINK_SB_POOL_TYPE_INGRESS &&
	    val != DEVLINK_SB_POOL_TYPE_EGRESS)
		return -EINVAL;
	*p_pool_type = val;
	return 0;
}

static int
devlink_sb_pool_type_get_from_info(struct genl_info *info,
				   enum devlink_sb_pool_type *p_pool_type)
{
	return devlink_sb_pool_type_get_from_attrs(info->attrs, p_pool_type);
}

static int
devlink_sb_th_type_get_from_attrs(struct nlattr **attrs,
				  enum devlink_sb_threshold_type *p_th_type)
{
	u8 val;

	if (!attrs[DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE])
		return -EINVAL;

	val = nla_get_u8(attrs[DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE]);
	if (val != DEVLINK_SB_THRESHOLD_TYPE_STATIC &&
	    val != DEVLINK_SB_THRESHOLD_TYPE_DYNAMIC)
		return -EINVAL;
	*p_th_type = val;
	return 0;
}

static int
devlink_sb_th_type_get_from_info(struct genl_info *info,
				 enum devlink_sb_threshold_type *p_th_type)
{
	return devlink_sb_th_type_get_from_attrs(info->attrs, p_th_type);
}

static int
devlink_sb_tc_index_get_from_attrs(struct devlink_sb *devlink_sb,
				   struct nlattr **attrs,
				   enum devlink_sb_pool_type pool_type,
				   u16 *p_tc_index)
{
	u16 val;

	if (!attrs[DEVLINK_ATTR_SB_TC_INDEX])
		return -EINVAL;

	val = nla_get_u16(attrs[DEVLINK_ATTR_SB_TC_INDEX]);
	if (pool_type == DEVLINK_SB_POOL_TYPE_INGRESS &&
	    val >= devlink_sb->ingress_tc_count)
		return -EINVAL;
	if (pool_type == DEVLINK_SB_POOL_TYPE_EGRESS &&
	    val >= devlink_sb->egress_tc_count)
		return -EINVAL;
	*p_tc_index = val;
	return 0;
}

static void devlink_port_fn_cap_fill(struct nla_bitfield32 *caps,
				     u32 cap, bool is_enable)
{
	caps->selector |= cap;
	if (is_enable)
		caps->value |= cap;
}

static int devlink_port_fn_roce_fill(const struct devlink_ops *ops,
				     struct devlink_port *devlink_port,
				     struct nla_bitfield32 *caps,
				     struct netlink_ext_ack *extack)
{
	bool is_enable;
	int err;

	if (!ops->port_fn_roce_get)
		return 0;

	err = ops->port_fn_roce_get(devlink_port, &is_enable, extack);
	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}

	devlink_port_fn_cap_fill(caps, DEVLINK_PORT_FN_CAP_ROCE, is_enable);
	return 0;
}

static int devlink_port_fn_migratable_fill(const struct devlink_ops *ops,
					   struct devlink_port *devlink_port,
					   struct nla_bitfield32 *caps,
					   struct netlink_ext_ack *extack)
{
	bool is_enable;
	int err;

	if (!ops->port_fn_migratable_get ||
	    devlink_port->attrs.flavour != DEVLINK_PORT_FLAVOUR_PCI_VF)
		return 0;

	err = ops->port_fn_migratable_get(devlink_port, &is_enable, extack);
	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}

	devlink_port_fn_cap_fill(caps, DEVLINK_PORT_FN_CAP_MIGRATABLE, is_enable);
	return 0;
}

static int devlink_port_fn_caps_fill(const struct devlink_ops *ops,
				     struct devlink_port *devlink_port,
				     struct sk_buff *msg,
				     struct netlink_ext_ack *extack,
				     bool *msg_updated)
{
	struct nla_bitfield32 caps = {};
	int err;

	err = devlink_port_fn_roce_fill(ops, devlink_port, &caps, extack);
	if (err)
		return err;

	err = devlink_port_fn_migratable_fill(ops, devlink_port, &caps, extack);
	if (err)
		return err;

	if (!caps.selector)
		return 0;
	err = nla_put_bitfield32(msg, DEVLINK_PORT_FN_ATTR_CAPS, caps.value,
				 caps.selector);
	if (err)
		return err;

	*msg_updated = true;
	return 0;
}

static int
devlink_sb_tc_index_get_from_info(struct devlink_sb *devlink_sb,
				  struct genl_info *info,
				  enum devlink_sb_pool_type pool_type,
				  u16 *p_tc_index)
{
	return devlink_sb_tc_index_get_from_attrs(devlink_sb, info->attrs,
						  pool_type, p_tc_index);
}

struct devlink_region {
	struct devlink *devlink;
	struct devlink_port *port;
	struct list_head list;
	union {
		const struct devlink_region_ops *ops;
		const struct devlink_port_region_ops *port_ops;
	};
	struct mutex snapshot_lock; /* protects snapshot_list,
				     * max_snapshots and cur_snapshots
				     * consistency.
				     */
	struct list_head snapshot_list;
	u32 max_snapshots;
	u32 cur_snapshots;
	u64 size;
};

struct devlink_snapshot {
	struct list_head list;
	struct devlink_region *region;
	u8 *data;
	u32 id;
};

static struct devlink_region *
devlink_region_get_by_name(struct devlink *devlink, const char *region_name)
{
	struct devlink_region *region;

	list_for_each_entry(region, &devlink->region_list, list)
		if (!strcmp(region->ops->name, region_name))
			return region;

	return NULL;
}

static struct devlink_region *
devlink_port_region_get_by_name(struct devlink_port *port,
				const char *region_name)
{
	struct devlink_region *region;

	list_for_each_entry(region, &port->region_list, list)
		if (!strcmp(region->ops->name, region_name))
			return region;

	return NULL;
}

static struct devlink_snapshot *
devlink_region_snapshot_get_by_id(struct devlink_region *region, u32 id)
{
	struct devlink_snapshot *snapshot;

	list_for_each_entry(snapshot, &region->snapshot_list, list)
		if (snapshot->id == id)
			return snapshot;

	return NULL;
}

static int devlink_nl_put_nested_handle(struct sk_buff *msg, struct devlink *devlink)
{
	struct nlattr *nested_attr;

	nested_attr = nla_nest_start(msg, DEVLINK_ATTR_NESTED_DEVLINK);
	if (!nested_attr)
		return -EMSGSIZE;
	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	nla_nest_end(msg, nested_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, nested_attr);
	return -EMSGSIZE;
}

int devlink_nl_port_handle_fill(struct sk_buff *msg, struct devlink_port *devlink_port)
{
	if (devlink_nl_put_handle(msg, devlink_port->devlink))
		return -EMSGSIZE;
	if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX, devlink_port->index))
		return -EMSGSIZE;
	return 0;
}

size_t devlink_nl_port_handle_size(struct devlink_port *devlink_port)
{
	struct devlink *devlink = devlink_port->devlink;

	return nla_total_size(strlen(devlink->dev->bus->name) + 1) /* DEVLINK_ATTR_BUS_NAME */
	     + nla_total_size(strlen(dev_name(devlink->dev)) + 1) /* DEVLINK_ATTR_DEV_NAME */
	     + nla_total_size(4); /* DEVLINK_ATTR_PORT_INDEX */
}

static int devlink_nl_port_attrs_put(struct sk_buff *msg,
				     struct devlink_port *devlink_port)
{
	struct devlink_port_attrs *attrs = &devlink_port->attrs;

	if (!devlink_port->attrs_set)
		return 0;
	if (attrs->lanes) {
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_LANES, attrs->lanes))
			return -EMSGSIZE;
	}
	if (nla_put_u8(msg, DEVLINK_ATTR_PORT_SPLITTABLE, attrs->splittable))
		return -EMSGSIZE;
	if (nla_put_u16(msg, DEVLINK_ATTR_PORT_FLAVOUR, attrs->flavour))
		return -EMSGSIZE;
	switch (devlink_port->attrs.flavour) {
	case DEVLINK_PORT_FLAVOUR_PCI_PF:
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_CONTROLLER_NUMBER,
				attrs->pci_pf.controller) ||
		    nla_put_u16(msg, DEVLINK_ATTR_PORT_PCI_PF_NUMBER, attrs->pci_pf.pf))
			return -EMSGSIZE;
		if (nla_put_u8(msg, DEVLINK_ATTR_PORT_EXTERNAL, attrs->pci_pf.external))
			return -EMSGSIZE;
		break;
	case DEVLINK_PORT_FLAVOUR_PCI_VF:
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_CONTROLLER_NUMBER,
				attrs->pci_vf.controller) ||
		    nla_put_u16(msg, DEVLINK_ATTR_PORT_PCI_PF_NUMBER, attrs->pci_vf.pf) ||
		    nla_put_u16(msg, DEVLINK_ATTR_PORT_PCI_VF_NUMBER, attrs->pci_vf.vf))
			return -EMSGSIZE;
		if (nla_put_u8(msg, DEVLINK_ATTR_PORT_EXTERNAL, attrs->pci_vf.external))
			return -EMSGSIZE;
		break;
	case DEVLINK_PORT_FLAVOUR_PCI_SF:
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_CONTROLLER_NUMBER,
				attrs->pci_sf.controller) ||
		    nla_put_u16(msg, DEVLINK_ATTR_PORT_PCI_PF_NUMBER,
				attrs->pci_sf.pf) ||
		    nla_put_u32(msg, DEVLINK_ATTR_PORT_PCI_SF_NUMBER,
				attrs->pci_sf.sf))
			return -EMSGSIZE;
		break;
	case DEVLINK_PORT_FLAVOUR_PHYSICAL:
	case DEVLINK_PORT_FLAVOUR_CPU:
	case DEVLINK_PORT_FLAVOUR_DSA:
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_NUMBER,
				attrs->phys.port_number))
			return -EMSGSIZE;
		if (!attrs->split)
			return 0;
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_SPLIT_GROUP,
				attrs->phys.port_number))
			return -EMSGSIZE;
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_SPLIT_SUBPORT_NUMBER,
				attrs->phys.split_subport_number))
			return -EMSGSIZE;
		break;
	default:
		break;
	}
	return 0;
}

static int devlink_port_fn_hw_addr_fill(const struct devlink_ops *ops,
					struct devlink_port *port,
					struct sk_buff *msg,
					struct netlink_ext_ack *extack,
					bool *msg_updated)
{
	u8 hw_addr[MAX_ADDR_LEN];
	int hw_addr_len;
	int err;

	if (!ops->port_function_hw_addr_get)
		return 0;

	err = ops->port_function_hw_addr_get(port, hw_addr, &hw_addr_len,
					     extack);
	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}
	err = nla_put(msg, DEVLINK_PORT_FUNCTION_ATTR_HW_ADDR, hw_addr_len, hw_addr);
	if (err)
		return err;
	*msg_updated = true;
	return 0;
}

static int devlink_nl_rate_fill(struct sk_buff *msg,
				struct devlink_rate *devlink_rate,
				enum devlink_command cmd, u32 portid, u32 seq,
				int flags, struct netlink_ext_ack *extack)
{
	struct devlink *devlink = devlink_rate->devlink;
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_u16(msg, DEVLINK_ATTR_RATE_TYPE, devlink_rate->type))
		goto nla_put_failure;

	if (devlink_rate_is_leaf(devlink_rate)) {
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX,
				devlink_rate->devlink_port->index))
			goto nla_put_failure;
	} else if (devlink_rate_is_node(devlink_rate)) {
		if (nla_put_string(msg, DEVLINK_ATTR_RATE_NODE_NAME,
				   devlink_rate->name))
			goto nla_put_failure;
	}

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_RATE_TX_SHARE,
			      devlink_rate->tx_share, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_RATE_TX_MAX,
			      devlink_rate->tx_max, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u32(msg, DEVLINK_ATTR_RATE_TX_PRIORITY,
			devlink_rate->tx_priority))
		goto nla_put_failure;

	if (nla_put_u32(msg, DEVLINK_ATTR_RATE_TX_WEIGHT,
			devlink_rate->tx_weight))
		goto nla_put_failure;

	if (devlink_rate->parent)
		if (nla_put_string(msg, DEVLINK_ATTR_RATE_PARENT_NODE_NAME,
				   devlink_rate->parent->name))
			goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static bool
devlink_port_fn_state_valid(enum devlink_port_fn_state state)
{
	return state == DEVLINK_PORT_FN_STATE_INACTIVE ||
	       state == DEVLINK_PORT_FN_STATE_ACTIVE;
}

static bool
devlink_port_fn_opstate_valid(enum devlink_port_fn_opstate opstate)
{
	return opstate == DEVLINK_PORT_FN_OPSTATE_DETACHED ||
	       opstate == DEVLINK_PORT_FN_OPSTATE_ATTACHED;
}

static int devlink_port_fn_state_fill(const struct devlink_ops *ops,
				      struct devlink_port *port,
				      struct sk_buff *msg,
				      struct netlink_ext_ack *extack,
				      bool *msg_updated)
{
	enum devlink_port_fn_opstate opstate;
	enum devlink_port_fn_state state;
	int err;

	if (!ops->port_fn_state_get)
		return 0;

	err = ops->port_fn_state_get(port, &state, &opstate, extack);
	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}
	if (!devlink_port_fn_state_valid(state)) {
		WARN_ON_ONCE(1);
		NL_SET_ERR_MSG(extack, "Invalid state read from driver");
		return -EINVAL;
	}
	if (!devlink_port_fn_opstate_valid(opstate)) {
		WARN_ON_ONCE(1);
		NL_SET_ERR_MSG(extack, "Invalid operational state read from driver");
		return -EINVAL;
	}
	if (nla_put_u8(msg, DEVLINK_PORT_FN_ATTR_STATE, state) ||
	    nla_put_u8(msg, DEVLINK_PORT_FN_ATTR_OPSTATE, opstate))
		return -EMSGSIZE;
	*msg_updated = true;
	return 0;
}

static int
devlink_port_fn_mig_set(struct devlink_port *devlink_port, bool enable,
			struct netlink_ext_ack *extack)
{
	const struct devlink_ops *ops = devlink_port->devlink->ops;

	return ops->port_fn_migratable_set(devlink_port, enable, extack);
}

static int
devlink_port_fn_roce_set(struct devlink_port *devlink_port, bool enable,
			 struct netlink_ext_ack *extack)
{
	const struct devlink_ops *ops = devlink_port->devlink->ops;

	return ops->port_fn_roce_set(devlink_port, enable, extack);
}

static int devlink_port_fn_caps_set(struct devlink_port *devlink_port,
				    const struct nlattr *attr,
				    struct netlink_ext_ack *extack)
{
	struct nla_bitfield32 caps;
	u32 caps_value;
	int err;

	caps = nla_get_bitfield32(attr);
	caps_value = caps.value & caps.selector;
	if (caps.selector & DEVLINK_PORT_FN_CAP_ROCE) {
		err = devlink_port_fn_roce_set(devlink_port,
					       caps_value & DEVLINK_PORT_FN_CAP_ROCE,
					       extack);
		if (err)
			return err;
	}
	if (caps.selector & DEVLINK_PORT_FN_CAP_MIGRATABLE) {
		err = devlink_port_fn_mig_set(devlink_port, caps_value &
					      DEVLINK_PORT_FN_CAP_MIGRATABLE,
					      extack);
		if (err)
			return err;
	}
	return 0;
}

static int
devlink_nl_port_function_attrs_put(struct sk_buff *msg, struct devlink_port *port,
				   struct netlink_ext_ack *extack)
{
	const struct devlink_ops *ops;
	struct nlattr *function_attr;
	bool msg_updated = false;
	int err;

	function_attr = nla_nest_start_noflag(msg, DEVLINK_ATTR_PORT_FUNCTION);
	if (!function_attr)
		return -EMSGSIZE;

	ops = port->devlink->ops;
	err = devlink_port_fn_hw_addr_fill(ops, port, msg, extack,
					   &msg_updated);
	if (err)
		goto out;
	err = devlink_port_fn_caps_fill(ops, port, msg, extack,
					&msg_updated);
	if (err)
		goto out;
	err = devlink_port_fn_state_fill(ops, port, msg, extack, &msg_updated);
out:
	if (err || !msg_updated)
		nla_nest_cancel(msg, function_attr);
	else
		nla_nest_end(msg, function_attr);
	return err;
}

static int devlink_nl_port_fill(struct sk_buff *msg,
				struct devlink_port *devlink_port,
				enum devlink_command cmd, u32 portid, u32 seq,
				int flags, struct netlink_ext_ack *extack)
{
	struct devlink *devlink = devlink_port->devlink;
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX, devlink_port->index))
		goto nla_put_failure;

	spin_lock_bh(&devlink_port->type_lock);
	if (nla_put_u16(msg, DEVLINK_ATTR_PORT_TYPE, devlink_port->type))
		goto nla_put_failure_type_locked;
	if (devlink_port->desired_type != DEVLINK_PORT_TYPE_NOTSET &&
	    nla_put_u16(msg, DEVLINK_ATTR_PORT_DESIRED_TYPE,
			devlink_port->desired_type))
		goto nla_put_failure_type_locked;
	if (devlink_port->type == DEVLINK_PORT_TYPE_ETH) {
		if (devlink_port->type_eth.netdev &&
		    (nla_put_u32(msg, DEVLINK_ATTR_PORT_NETDEV_IFINDEX,
				 devlink_port->type_eth.ifindex) ||
		     nla_put_string(msg, DEVLINK_ATTR_PORT_NETDEV_NAME,
				    devlink_port->type_eth.ifname)))
			goto nla_put_failure_type_locked;
	}
	if (devlink_port->type == DEVLINK_PORT_TYPE_IB) {
		struct ib_device *ibdev = devlink_port->type_ib.ibdev;

		if (ibdev &&
		    nla_put_string(msg, DEVLINK_ATTR_PORT_IBDEV_NAME,
				   ibdev->name))
			goto nla_put_failure_type_locked;
	}
	spin_unlock_bh(&devlink_port->type_lock);
	if (devlink_nl_port_attrs_put(msg, devlink_port))
		goto nla_put_failure;
	if (devlink_nl_port_function_attrs_put(msg, devlink_port, extack))
		goto nla_put_failure;
	if (devlink_port->linecard &&
	    nla_put_u32(msg, DEVLINK_ATTR_LINECARD_INDEX,
			devlink_port->linecard->index))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure_type_locked:
	spin_unlock_bh(&devlink_port->type_lock);
nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void devlink_port_notify(struct devlink_port *devlink_port,
				enum devlink_command cmd)
{
	struct devlink *devlink = devlink_port->devlink;
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_PORT_NEW && cmd != DEVLINK_CMD_PORT_DEL);

	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_port_fill(msg, devlink_port, cmd, 0, 0, 0, NULL);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink), msg,
				0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static void devlink_rate_notify(struct devlink_rate *devlink_rate,
				enum devlink_command cmd)
{
	struct devlink *devlink = devlink_rate->devlink;
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_RATE_NEW && cmd != DEVLINK_CMD_RATE_DEL);

	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_rate_fill(msg, devlink_rate, cmd, 0, 0, 0, NULL);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink), msg,
				0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static int
devlink_nl_cmd_rate_get_dump_one(struct sk_buff *msg, struct devlink *devlink,
				 struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_rate *devlink_rate;
	int idx = 0;
	int err = 0;

	list_for_each_entry(devlink_rate, &devlink->rate_list, list) {
		enum devlink_command cmd = DEVLINK_CMD_RATE_NEW;
		u32 id = NETLINK_CB(cb->skb).portid;

		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_rate_fill(msg, devlink_rate, cmd, id,
					   cb->nlh->nlmsg_seq,
					   NLM_F_MULTI, NULL);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

const struct devlink_cmd devl_cmd_rate_get = {
	.dump_one		= devlink_nl_cmd_rate_get_dump_one,
};

static int devlink_nl_cmd_rate_get_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink_rate *devlink_rate = info->user_ptr[1];
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_rate_fill(msg, devlink_rate, DEVLINK_CMD_RATE_NEW,
				   info->snd_portid, info->snd_seq, 0,
				   info->extack);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static bool
devlink_rate_is_parent_node(struct devlink_rate *devlink_rate,
			    struct devlink_rate *parent)
{
	while (parent) {
		if (parent == devlink_rate)
			return true;
		parent = parent->parent;
	}
	return false;
}

static int devlink_nl_cmd_port_get_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_port_fill(msg, devlink_port, DEVLINK_CMD_PORT_NEW,
				   info->snd_portid, info->snd_seq, 0,
				   info->extack);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int
devlink_nl_cmd_port_get_dump_one(struct sk_buff *msg, struct devlink *devlink,
				 struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_port *devlink_port;
	unsigned long port_index;
	int err = 0;

	xa_for_each_start(&devlink->ports, port_index, devlink_port, state->idx) {
		err = devlink_nl_port_fill(msg, devlink_port,
					   DEVLINK_CMD_NEW,
					   NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq,
					   NLM_F_MULTI, cb->extack);
		if (err) {
			state->idx = port_index;
			break;
		}
	}

	return err;
}

const struct devlink_cmd devl_cmd_port_get = {
	.dump_one		= devlink_nl_cmd_port_get_dump_one,
};

static int devlink_port_type_set(struct devlink_port *devlink_port,
				 enum devlink_port_type port_type)

{
	int err;

	if (!devlink_port->devlink->ops->port_type_set)
		return -EOPNOTSUPP;

	if (port_type == devlink_port->type)
		return 0;

	err = devlink_port->devlink->ops->port_type_set(devlink_port,
							port_type);
	if (err)
		return err;

	devlink_port->desired_type = port_type;
	devlink_port_notify(devlink_port, DEVLINK_CMD_PORT_NEW);
	return 0;
}

static int devlink_port_function_hw_addr_set(struct devlink_port *port,
					     const struct nlattr *attr,
					     struct netlink_ext_ack *extack)
{
	const struct devlink_ops *ops = port->devlink->ops;
	const u8 *hw_addr;
	int hw_addr_len;

	hw_addr = nla_data(attr);
	hw_addr_len = nla_len(attr);
	if (hw_addr_len > MAX_ADDR_LEN) {
		NL_SET_ERR_MSG(extack, "Port function hardware address too long");
		return -EINVAL;
	}
	if (port->type == DEVLINK_PORT_TYPE_ETH) {
		if (hw_addr_len != ETH_ALEN) {
			NL_SET_ERR_MSG(extack, "Address must be 6 bytes for Ethernet device");
			return -EINVAL;
		}
		if (!is_unicast_ether_addr(hw_addr)) {
			NL_SET_ERR_MSG(extack, "Non-unicast hardware address unsupported");
			return -EINVAL;
		}
	}

	return ops->port_function_hw_addr_set(port, hw_addr, hw_addr_len,
					      extack);
}

static int devlink_port_fn_state_set(struct devlink_port *port,
				     const struct nlattr *attr,
				     struct netlink_ext_ack *extack)
{
	enum devlink_port_fn_state state;
	const struct devlink_ops *ops;

	state = nla_get_u8(attr);
	ops = port->devlink->ops;
	return ops->port_fn_state_set(port, state, extack);
}

static int devlink_port_function_validate(struct devlink_port *devlink_port,
					  struct nlattr **tb,
					  struct netlink_ext_ack *extack)
{
	const struct devlink_ops *ops = devlink_port->devlink->ops;
	struct nlattr *attr;

	if (tb[DEVLINK_PORT_FUNCTION_ATTR_HW_ADDR] &&
	    !ops->port_function_hw_addr_set) {
		NL_SET_ERR_MSG_ATTR(extack, tb[DEVLINK_PORT_FUNCTION_ATTR_HW_ADDR],
				    "Port doesn't support function attributes");
		return -EOPNOTSUPP;
	}
	if (tb[DEVLINK_PORT_FN_ATTR_STATE] && !ops->port_fn_state_set) {
		NL_SET_ERR_MSG_ATTR(extack, tb[DEVLINK_PORT_FUNCTION_ATTR_HW_ADDR],
				    "Function does not support state setting");
		return -EOPNOTSUPP;
	}
	attr = tb[DEVLINK_PORT_FN_ATTR_CAPS];
	if (attr) {
		struct nla_bitfield32 caps;

		caps = nla_get_bitfield32(attr);
		if (caps.selector & DEVLINK_PORT_FN_CAP_ROCE &&
		    !ops->port_fn_roce_set) {
			NL_SET_ERR_MSG_ATTR(extack, attr,
					    "Port doesn't support RoCE function attribute");
			return -EOPNOTSUPP;
		}
		if (caps.selector & DEVLINK_PORT_FN_CAP_MIGRATABLE) {
			if (!ops->port_fn_migratable_set) {
				NL_SET_ERR_MSG_ATTR(extack, attr,
						    "Port doesn't support migratable function attribute");
				return -EOPNOTSUPP;
			}
			if (devlink_port->attrs.flavour != DEVLINK_PORT_FLAVOUR_PCI_VF) {
				NL_SET_ERR_MSG_ATTR(extack, attr,
						    "migratable function attribute supported for VFs only");
				return -EOPNOTSUPP;
			}
		}
	}
	return 0;
}

static int devlink_port_function_set(struct devlink_port *port,
				     const struct nlattr *attr,
				     struct netlink_ext_ack *extack)
{
	struct nlattr *tb[DEVLINK_PORT_FUNCTION_ATTR_MAX + 1];
	int err;

	err = nla_parse_nested(tb, DEVLINK_PORT_FUNCTION_ATTR_MAX, attr,
			       devlink_function_nl_policy, extack);
	if (err < 0) {
		NL_SET_ERR_MSG(extack, "Fail to parse port function attributes");
		return err;
	}

	err = devlink_port_function_validate(port, tb, extack);
	if (err)
		return err;

	attr = tb[DEVLINK_PORT_FUNCTION_ATTR_HW_ADDR];
	if (attr) {
		err = devlink_port_function_hw_addr_set(port, attr, extack);
		if (err)
			return err;
	}

	attr = tb[DEVLINK_PORT_FN_ATTR_CAPS];
	if (attr) {
		err = devlink_port_fn_caps_set(port, attr, extack);
		if (err)
			return err;
	}

	/* Keep this as the last function attribute set, so that when
	 * multiple port function attributes are set along with state,
	 * Those can be applied first before activating the state.
	 */
	attr = tb[DEVLINK_PORT_FN_ATTR_STATE];
	if (attr)
		err = devlink_port_fn_state_set(port, attr, extack);

	if (!err)
		devlink_port_notify(port, DEVLINK_CMD_PORT_NEW);
	return err;
}

static int devlink_nl_cmd_port_set_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	int err;

	if (info->attrs[DEVLINK_ATTR_PORT_TYPE]) {
		enum devlink_port_type port_type;

		port_type = nla_get_u16(info->attrs[DEVLINK_ATTR_PORT_TYPE]);
		err = devlink_port_type_set(devlink_port, port_type);
		if (err)
			return err;
	}

	if (info->attrs[DEVLINK_ATTR_PORT_FUNCTION]) {
		struct nlattr *attr = info->attrs[DEVLINK_ATTR_PORT_FUNCTION];
		struct netlink_ext_ack *extack = info->extack;

		err = devlink_port_function_set(devlink_port, attr, extack);
		if (err)
			return err;
	}

	return 0;
}

static int devlink_nl_cmd_port_split_doit(struct sk_buff *skb,
					  struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = info->user_ptr[0];
	u32 count;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_PORT_SPLIT_COUNT))
		return -EINVAL;
	if (!devlink->ops->port_split)
		return -EOPNOTSUPP;

	count = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_SPLIT_COUNT]);

	if (!devlink_port->attrs.splittable) {
		/* Split ports cannot be split. */
		if (devlink_port->attrs.split)
			NL_SET_ERR_MSG(info->extack, "Port cannot be split further");
		else
			NL_SET_ERR_MSG(info->extack, "Port cannot be split");
		return -EINVAL;
	}

	if (count < 2 || !is_power_of_2(count) || count > devlink_port->attrs.lanes) {
		NL_SET_ERR_MSG(info->extack, "Invalid split count");
		return -EINVAL;
	}

	return devlink->ops->port_split(devlink, devlink_port, count,
					info->extack);
}

static int devlink_nl_cmd_port_unsplit_doit(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = info->user_ptr[0];

	if (!devlink->ops->port_unsplit)
		return -EOPNOTSUPP;
	return devlink->ops->port_unsplit(devlink, devlink_port, info->extack);
}

static int devlink_port_new_notify(struct devlink *devlink,
				   unsigned int port_index,
				   struct genl_info *info)
{
	struct devlink_port *devlink_port;
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	lockdep_assert_held(&devlink->lock);
	devlink_port = devlink_port_get_by_index(devlink, port_index);
	if (!devlink_port) {
		err = -ENODEV;
		goto out;
	}

	err = devlink_nl_port_fill(msg, devlink_port, DEVLINK_CMD_NEW,
				   info->snd_portid, info->snd_seq, 0, NULL);
	if (err)
		goto out;

	return genlmsg_reply(msg, info);

out:
	nlmsg_free(msg);
	return err;
}

static int devlink_nl_cmd_port_new_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink_port_new_attrs new_attrs = {};
	struct devlink *devlink = info->user_ptr[0];
	unsigned int new_port_index;
	int err;

	if (!devlink->ops->port_new || !devlink->ops->port_del)
		return -EOPNOTSUPP;

	if (!info->attrs[DEVLINK_ATTR_PORT_FLAVOUR] ||
	    !info->attrs[DEVLINK_ATTR_PORT_PCI_PF_NUMBER]) {
		NL_SET_ERR_MSG(extack, "Port flavour or PCI PF are not specified");
		return -EINVAL;
	}
	new_attrs.flavour = nla_get_u16(info->attrs[DEVLINK_ATTR_PORT_FLAVOUR]);
	new_attrs.pfnum =
		nla_get_u16(info->attrs[DEVLINK_ATTR_PORT_PCI_PF_NUMBER]);

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		/* Port index of the new port being created by driver. */
		new_attrs.port_index =
			nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);
		new_attrs.port_index_valid = true;
	}
	if (info->attrs[DEVLINK_ATTR_PORT_CONTROLLER_NUMBER]) {
		new_attrs.controller =
			nla_get_u16(info->attrs[DEVLINK_ATTR_PORT_CONTROLLER_NUMBER]);
		new_attrs.controller_valid = true;
	}
	if (new_attrs.flavour == DEVLINK_PORT_FLAVOUR_PCI_SF &&
	    info->attrs[DEVLINK_ATTR_PORT_PCI_SF_NUMBER]) {
		new_attrs.sfnum = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_PCI_SF_NUMBER]);
		new_attrs.sfnum_valid = true;
	}

	err = devlink->ops->port_new(devlink, &new_attrs, extack,
				     &new_port_index);
	if (err)
		return err;

	err = devlink_port_new_notify(devlink, new_port_index, info);
	if (err && err != -ENODEV) {
		/* Fail to send the response; destroy newly created port. */
		devlink->ops->port_del(devlink, new_port_index, extack);
	}
	return err;
}

static int devlink_nl_cmd_port_del_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	unsigned int port_index;

	if (!devlink->ops->port_del)
		return -EOPNOTSUPP;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_PORT_INDEX)) {
		NL_SET_ERR_MSG(extack, "Port index is not specified");
		return -EINVAL;
	}
	port_index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

	return devlink->ops->port_del(devlink, port_index, extack);
}

static int
devlink_nl_rate_parent_node_set(struct devlink_rate *devlink_rate,
				struct genl_info *info,
				struct nlattr *nla_parent)
{
	struct devlink *devlink = devlink_rate->devlink;
	const char *parent_name = nla_data(nla_parent);
	const struct devlink_ops *ops = devlink->ops;
	size_t len = strlen(parent_name);
	struct devlink_rate *parent;
	int err = -EOPNOTSUPP;

	parent = devlink_rate->parent;

	if (parent && !len) {
		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_parent_set(devlink_rate, NULL,
							devlink_rate->priv, NULL,
							info->extack);
		else if (devlink_rate_is_node(devlink_rate))
			err = ops->rate_node_parent_set(devlink_rate, NULL,
							devlink_rate->priv, NULL,
							info->extack);
		if (err)
			return err;

		refcount_dec(&parent->refcnt);
		devlink_rate->parent = NULL;
	} else if (len) {
		parent = devlink_rate_node_get_by_name(devlink, parent_name);
		if (IS_ERR(parent))
			return -ENODEV;

		if (parent == devlink_rate) {
			NL_SET_ERR_MSG(info->extack, "Parent to self is not allowed");
			return -EINVAL;
		}

		if (devlink_rate_is_node(devlink_rate) &&
		    devlink_rate_is_parent_node(devlink_rate, parent->parent)) {
			NL_SET_ERR_MSG(info->extack, "Node is already a parent of parent node.");
			return -EEXIST;
		}

		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_parent_set(devlink_rate, parent,
							devlink_rate->priv, parent->priv,
							info->extack);
		else if (devlink_rate_is_node(devlink_rate))
			err = ops->rate_node_parent_set(devlink_rate, parent,
							devlink_rate->priv, parent->priv,
							info->extack);
		if (err)
			return err;

		if (devlink_rate->parent)
			/* we're reassigning to other parent in this case */
			refcount_dec(&devlink_rate->parent->refcnt);

		refcount_inc(&parent->refcnt);
		devlink_rate->parent = parent;
	}

	return 0;
}

static int devlink_nl_rate_set(struct devlink_rate *devlink_rate,
			       const struct devlink_ops *ops,
			       struct genl_info *info)
{
	struct nlattr *nla_parent, **attrs = info->attrs;
	int err = -EOPNOTSUPP;
	u32 priority;
	u32 weight;
	u64 rate;

	if (attrs[DEVLINK_ATTR_RATE_TX_SHARE]) {
		rate = nla_get_u64(attrs[DEVLINK_ATTR_RATE_TX_SHARE]);
		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_tx_share_set(devlink_rate, devlink_rate->priv,
							  rate, info->extack);
		else if (devlink_rate_is_node(devlink_rate))
			err = ops->rate_node_tx_share_set(devlink_rate, devlink_rate->priv,
							  rate, info->extack);
		if (err)
			return err;
		devlink_rate->tx_share = rate;
	}

	if (attrs[DEVLINK_ATTR_RATE_TX_MAX]) {
		rate = nla_get_u64(attrs[DEVLINK_ATTR_RATE_TX_MAX]);
		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_tx_max_set(devlink_rate, devlink_rate->priv,
							rate, info->extack);
		else if (devlink_rate_is_node(devlink_rate))
			err = ops->rate_node_tx_max_set(devlink_rate, devlink_rate->priv,
							rate, info->extack);
		if (err)
			return err;
		devlink_rate->tx_max = rate;
	}

	if (attrs[DEVLINK_ATTR_RATE_TX_PRIORITY]) {
		priority = nla_get_u32(attrs[DEVLINK_ATTR_RATE_TX_PRIORITY]);
		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_tx_priority_set(devlink_rate, devlink_rate->priv,
							     priority, info->extack);
		else if (devlink_rate_is_node(devlink_rate))
			err = ops->rate_node_tx_priority_set(devlink_rate, devlink_rate->priv,
							     priority, info->extack);

		if (err)
			return err;
		devlink_rate->tx_priority = priority;
	}

	if (attrs[DEVLINK_ATTR_RATE_TX_WEIGHT]) {
		weight = nla_get_u32(attrs[DEVLINK_ATTR_RATE_TX_WEIGHT]);
		if (devlink_rate_is_leaf(devlink_rate))
			err = ops->rate_leaf_tx_weight_set(devlink_rate, devlink_rate->priv,
							   weight, info->extack);
		else if (devlink_rate_is_node(devlink_rate))
			err = ops->rate_node_tx_weight_set(devlink_rate, devlink_rate->priv,
							   weight, info->extack);

		if (err)
			return err;
		devlink_rate->tx_weight = weight;
	}

	nla_parent = attrs[DEVLINK_ATTR_RATE_PARENT_NODE_NAME];
	if (nla_parent) {
		err = devlink_nl_rate_parent_node_set(devlink_rate, info,
						      nla_parent);
		if (err)
			return err;
	}

	return 0;
}

static bool devlink_rate_set_ops_supported(const struct devlink_ops *ops,
					   struct genl_info *info,
					   enum devlink_rate_type type)
{
	struct nlattr **attrs = info->attrs;

	if (type == DEVLINK_RATE_TYPE_LEAF) {
		if (attrs[DEVLINK_ATTR_RATE_TX_SHARE] && !ops->rate_leaf_tx_share_set) {
			NL_SET_ERR_MSG(info->extack, "TX share set isn't supported for the leafs");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_MAX] && !ops->rate_leaf_tx_max_set) {
			NL_SET_ERR_MSG(info->extack, "TX max set isn't supported for the leafs");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_PARENT_NODE_NAME] &&
		    !ops->rate_leaf_parent_set) {
			NL_SET_ERR_MSG(info->extack, "Parent set isn't supported for the leafs");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_PRIORITY] && !ops->rate_leaf_tx_priority_set) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    attrs[DEVLINK_ATTR_RATE_TX_PRIORITY],
					    "TX priority set isn't supported for the leafs");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_WEIGHT] && !ops->rate_leaf_tx_weight_set) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    attrs[DEVLINK_ATTR_RATE_TX_WEIGHT],
					    "TX weight set isn't supported for the leafs");
			return false;
		}
	} else if (type == DEVLINK_RATE_TYPE_NODE) {
		if (attrs[DEVLINK_ATTR_RATE_TX_SHARE] && !ops->rate_node_tx_share_set) {
			NL_SET_ERR_MSG(info->extack, "TX share set isn't supported for the nodes");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_MAX] && !ops->rate_node_tx_max_set) {
			NL_SET_ERR_MSG(info->extack, "TX max set isn't supported for the nodes");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_PARENT_NODE_NAME] &&
		    !ops->rate_node_parent_set) {
			NL_SET_ERR_MSG(info->extack, "Parent set isn't supported for the nodes");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_PRIORITY] && !ops->rate_node_tx_priority_set) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    attrs[DEVLINK_ATTR_RATE_TX_PRIORITY],
					    "TX priority set isn't supported for the nodes");
			return false;
		}
		if (attrs[DEVLINK_ATTR_RATE_TX_WEIGHT] && !ops->rate_node_tx_weight_set) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    attrs[DEVLINK_ATTR_RATE_TX_WEIGHT],
					    "TX weight set isn't supported for the nodes");
			return false;
		}
	} else {
		WARN(1, "Unknown type of rate object");
		return false;
	}

	return true;
}

static int devlink_nl_cmd_rate_set_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink_rate *devlink_rate = info->user_ptr[1];
	struct devlink *devlink = devlink_rate->devlink;
	const struct devlink_ops *ops = devlink->ops;
	int err;

	if (!ops || !devlink_rate_set_ops_supported(ops, info, devlink_rate->type))
		return -EOPNOTSUPP;

	err = devlink_nl_rate_set(devlink_rate, ops, info);

	if (!err)
		devlink_rate_notify(devlink_rate, DEVLINK_CMD_RATE_NEW);
	return err;
}

static int devlink_nl_cmd_rate_new_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_rate *rate_node;
	const struct devlink_ops *ops;
	int err;

	ops = devlink->ops;
	if (!ops || !ops->rate_node_new || !ops->rate_node_del) {
		NL_SET_ERR_MSG(info->extack, "Rate nodes aren't supported");
		return -EOPNOTSUPP;
	}

	if (!devlink_rate_set_ops_supported(ops, info, DEVLINK_RATE_TYPE_NODE))
		return -EOPNOTSUPP;

	rate_node = devlink_rate_node_get_from_attrs(devlink, info->attrs);
	if (!IS_ERR(rate_node))
		return -EEXIST;
	else if (rate_node == ERR_PTR(-EINVAL))
		return -EINVAL;

	rate_node = kzalloc(sizeof(*rate_node), GFP_KERNEL);
	if (!rate_node)
		return -ENOMEM;

	rate_node->devlink = devlink;
	rate_node->type = DEVLINK_RATE_TYPE_NODE;
	rate_node->name = nla_strdup(info->attrs[DEVLINK_ATTR_RATE_NODE_NAME], GFP_KERNEL);
	if (!rate_node->name) {
		err = -ENOMEM;
		goto err_strdup;
	}

	err = ops->rate_node_new(rate_node, &rate_node->priv, info->extack);
	if (err)
		goto err_node_new;

	err = devlink_nl_rate_set(rate_node, ops, info);
	if (err)
		goto err_rate_set;

	refcount_set(&rate_node->refcnt, 1);
	list_add(&rate_node->list, &devlink->rate_list);
	devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_NEW);
	return 0;

err_rate_set:
	ops->rate_node_del(rate_node, rate_node->priv, info->extack);
err_node_new:
	kfree(rate_node->name);
err_strdup:
	kfree(rate_node);
	return err;
}

static int devlink_nl_cmd_rate_del_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink_rate *rate_node = info->user_ptr[1];
	struct devlink *devlink = rate_node->devlink;
	const struct devlink_ops *ops = devlink->ops;
	int err;

	if (refcount_read(&rate_node->refcnt) > 1) {
		NL_SET_ERR_MSG(info->extack, "Node has children. Cannot delete node.");
		return -EBUSY;
	}

	devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_DEL);
	err = ops->rate_node_del(rate_node, rate_node->priv, info->extack);
	if (rate_node->parent)
		refcount_dec(&rate_node->parent->refcnt);
	list_del(&rate_node->list);
	kfree(rate_node->name);
	kfree(rate_node);
	return err;
}

struct devlink_linecard_type {
	const char *type;
	const void *priv;
};

static int devlink_nl_linecard_fill(struct sk_buff *msg,
				    struct devlink *devlink,
				    struct devlink_linecard *linecard,
				    enum devlink_command cmd, u32 portid,
				    u32 seq, int flags,
				    struct netlink_ext_ack *extack)
{
	struct devlink_linecard_type *linecard_type;
	struct nlattr *attr;
	void *hdr;
	int i;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_LINECARD_INDEX, linecard->index))
		goto nla_put_failure;
	if (nla_put_u8(msg, DEVLINK_ATTR_LINECARD_STATE, linecard->state))
		goto nla_put_failure;
	if (linecard->type &&
	    nla_put_string(msg, DEVLINK_ATTR_LINECARD_TYPE, linecard->type))
		goto nla_put_failure;

	if (linecard->types_count) {
		attr = nla_nest_start(msg,
				      DEVLINK_ATTR_LINECARD_SUPPORTED_TYPES);
		if (!attr)
			goto nla_put_failure;
		for (i = 0; i < linecard->types_count; i++) {
			linecard_type = &linecard->types[i];
			if (nla_put_string(msg, DEVLINK_ATTR_LINECARD_TYPE,
					   linecard_type->type)) {
				nla_nest_cancel(msg, attr);
				goto nla_put_failure;
			}
		}
		nla_nest_end(msg, attr);
	}

	if (linecard->nested_devlink &&
	    devlink_nl_put_nested_handle(msg, linecard->nested_devlink))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void devlink_linecard_notify(struct devlink_linecard *linecard,
				    enum devlink_command cmd)
{
	struct devlink *devlink = linecard->devlink;
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_LINECARD_NEW &&
		cmd != DEVLINK_CMD_LINECARD_DEL);

	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_linecard_fill(msg, devlink, linecard, cmd, 0, 0, 0,
				       NULL);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static int devlink_nl_cmd_linecard_get_doit(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct devlink_linecard *linecard = info->user_ptr[1];
	struct devlink *devlink = linecard->devlink;
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	mutex_lock(&linecard->state_lock);
	err = devlink_nl_linecard_fill(msg, devlink, linecard,
				       DEVLINK_CMD_LINECARD_NEW,
				       info->snd_portid, info->snd_seq, 0,
				       info->extack);
	mutex_unlock(&linecard->state_lock);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int devlink_nl_cmd_linecard_get_dump_one(struct sk_buff *msg,
						struct devlink *devlink,
						struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_linecard *linecard;
	int idx = 0;
	int err = 0;

	list_for_each_entry(linecard, &devlink->linecard_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		mutex_lock(&linecard->state_lock);
		err = devlink_nl_linecard_fill(msg, devlink, linecard,
					       DEVLINK_CMD_LINECARD_NEW,
					       NETLINK_CB(cb->skb).portid,
					       cb->nlh->nlmsg_seq,
					       NLM_F_MULTI,
					       cb->extack);
		mutex_unlock(&linecard->state_lock);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

const struct devlink_cmd devl_cmd_linecard_get = {
	.dump_one		= devlink_nl_cmd_linecard_get_dump_one,
};

static struct devlink_linecard_type *
devlink_linecard_type_lookup(struct devlink_linecard *linecard,
			     const char *type)
{
	struct devlink_linecard_type *linecard_type;
	int i;

	for (i = 0; i < linecard->types_count; i++) {
		linecard_type = &linecard->types[i];
		if (!strcmp(type, linecard_type->type))
			return linecard_type;
	}
	return NULL;
}

static int devlink_linecard_type_set(struct devlink_linecard *linecard,
				     const char *type,
				     struct netlink_ext_ack *extack)
{
	const struct devlink_linecard_ops *ops = linecard->ops;
	struct devlink_linecard_type *linecard_type;
	int err;

	mutex_lock(&linecard->state_lock);
	if (linecard->state == DEVLINK_LINECARD_STATE_PROVISIONING) {
		NL_SET_ERR_MSG(extack, "Line card is currently being provisioned");
		err = -EBUSY;
		goto out;
	}
	if (linecard->state == DEVLINK_LINECARD_STATE_UNPROVISIONING) {
		NL_SET_ERR_MSG(extack, "Line card is currently being unprovisioned");
		err = -EBUSY;
		goto out;
	}

	linecard_type = devlink_linecard_type_lookup(linecard, type);
	if (!linecard_type) {
		NL_SET_ERR_MSG(extack, "Unsupported line card type provided");
		err = -EINVAL;
		goto out;
	}

	if (linecard->state != DEVLINK_LINECARD_STATE_UNPROVISIONED &&
	    linecard->state != DEVLINK_LINECARD_STATE_PROVISIONING_FAILED) {
		NL_SET_ERR_MSG(extack, "Line card already provisioned");
		err = -EBUSY;
		/* Check if the line card is provisioned in the same
		 * way the user asks. In case it is, make the operation
		 * to return success.
		 */
		if (ops->same_provision &&
		    ops->same_provision(linecard, linecard->priv,
					linecard_type->type,
					linecard_type->priv))
			err = 0;
		goto out;
	}

	linecard->state = DEVLINK_LINECARD_STATE_PROVISIONING;
	linecard->type = linecard_type->type;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
	err = ops->provision(linecard, linecard->priv, linecard_type->type,
			     linecard_type->priv, extack);
	if (err) {
		/* Provisioning failed. Assume the linecard is unprovisioned
		 * for future operations.
		 */
		mutex_lock(&linecard->state_lock);
		linecard->state = DEVLINK_LINECARD_STATE_UNPROVISIONED;
		linecard->type = NULL;
		devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
		mutex_unlock(&linecard->state_lock);
	}
	return err;

out:
	mutex_unlock(&linecard->state_lock);
	return err;
}

static int devlink_linecard_type_unset(struct devlink_linecard *linecard,
				       struct netlink_ext_ack *extack)
{
	int err;

	mutex_lock(&linecard->state_lock);
	if (linecard->state == DEVLINK_LINECARD_STATE_PROVISIONING) {
		NL_SET_ERR_MSG(extack, "Line card is currently being provisioned");
		err = -EBUSY;
		goto out;
	}
	if (linecard->state == DEVLINK_LINECARD_STATE_UNPROVISIONING) {
		NL_SET_ERR_MSG(extack, "Line card is currently being unprovisioned");
		err = -EBUSY;
		goto out;
	}
	if (linecard->state == DEVLINK_LINECARD_STATE_PROVISIONING_FAILED) {
		linecard->state = DEVLINK_LINECARD_STATE_UNPROVISIONED;
		linecard->type = NULL;
		devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
		err = 0;
		goto out;
	}

	if (linecard->state == DEVLINK_LINECARD_STATE_UNPROVISIONED) {
		NL_SET_ERR_MSG(extack, "Line card is not provisioned");
		err = 0;
		goto out;
	}
	linecard->state = DEVLINK_LINECARD_STATE_UNPROVISIONING;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
	err = linecard->ops->unprovision(linecard, linecard->priv,
					 extack);
	if (err) {
		/* Unprovisioning failed. Assume the linecard is unprovisioned
		 * for future operations.
		 */
		mutex_lock(&linecard->state_lock);
		linecard->state = DEVLINK_LINECARD_STATE_UNPROVISIONED;
		linecard->type = NULL;
		devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
		mutex_unlock(&linecard->state_lock);
	}
	return err;

out:
	mutex_unlock(&linecard->state_lock);
	return err;
}

static int devlink_nl_cmd_linecard_set_doit(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct devlink_linecard *linecard = info->user_ptr[1];
	struct netlink_ext_ack *extack = info->extack;
	int err;

	if (info->attrs[DEVLINK_ATTR_LINECARD_TYPE]) {
		const char *type;

		type = nla_data(info->attrs[DEVLINK_ATTR_LINECARD_TYPE]);
		if (strcmp(type, "")) {
			err = devlink_linecard_type_set(linecard, type, extack);
			if (err)
				return err;
		} else {
			err = devlink_linecard_type_unset(linecard, extack);
			if (err)
				return err;
		}
	}

	return 0;
}

static int devlink_nl_sb_fill(struct sk_buff *msg, struct devlink *devlink,
			      struct devlink_sb *devlink_sb,
			      enum devlink_command cmd, u32 portid,
			      u32 seq, int flags)
{
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_INDEX, devlink_sb->index))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_SIZE, devlink_sb->size))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_INGRESS_POOL_COUNT,
			devlink_sb->ingress_pools_count))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_EGRESS_POOL_COUNT,
			devlink_sb->egress_pools_count))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_INGRESS_TC_COUNT,
			devlink_sb->ingress_tc_count))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_EGRESS_TC_COUNT,
			devlink_sb->egress_tc_count))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_sb_get_doit(struct sk_buff *skb,
				      struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_sb *devlink_sb;
	struct sk_buff *msg;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_sb_fill(msg, devlink, devlink_sb,
				 DEVLINK_CMD_SB_NEW,
				 info->snd_portid, info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int
devlink_nl_cmd_sb_get_dump_one(struct sk_buff *msg, struct devlink *devlink,
			       struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_sb *devlink_sb;
	int idx = 0;
	int err = 0;

	list_for_each_entry(devlink_sb, &devlink->sb_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_sb_fill(msg, devlink, devlink_sb,
					 DEVLINK_CMD_SB_NEW,
					 NETLINK_CB(cb->skb).portid,
					 cb->nlh->nlmsg_seq,
					 NLM_F_MULTI);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

const struct devlink_cmd devl_cmd_sb_get = {
	.dump_one		= devlink_nl_cmd_sb_get_dump_one,
};

static int devlink_nl_sb_pool_fill(struct sk_buff *msg, struct devlink *devlink,
				   struct devlink_sb *devlink_sb,
				   u16 pool_index, enum devlink_command cmd,
				   u32 portid, u32 seq, int flags)
{
	struct devlink_sb_pool_info pool_info;
	void *hdr;
	int err;

	err = devlink->ops->sb_pool_get(devlink, devlink_sb->index,
					pool_index, &pool_info);
	if (err)
		return err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_INDEX, devlink_sb->index))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_POOL_INDEX, pool_index))
		goto nla_put_failure;
	if (nla_put_u8(msg, DEVLINK_ATTR_SB_POOL_TYPE, pool_info.pool_type))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_POOL_SIZE, pool_info.size))
		goto nla_put_failure;
	if (nla_put_u8(msg, DEVLINK_ATTR_SB_POOL_THRESHOLD_TYPE,
		       pool_info.threshold_type))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_POOL_CELL_SIZE,
			pool_info.cell_size))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_sb_pool_get_doit(struct sk_buff *skb,
					   struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_sb *devlink_sb;
	struct sk_buff *msg;
	u16 pool_index;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	err = devlink_sb_pool_index_get_from_info(devlink_sb, info,
						  &pool_index);
	if (err)
		return err;

	if (!devlink->ops->sb_pool_get)
		return -EOPNOTSUPP;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_sb_pool_fill(msg, devlink, devlink_sb, pool_index,
				      DEVLINK_CMD_SB_POOL_NEW,
				      info->snd_portid, info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int __sb_pool_get_dumpit(struct sk_buff *msg, int start, int *p_idx,
				struct devlink *devlink,
				struct devlink_sb *devlink_sb,
				u32 portid, u32 seq)
{
	u16 pool_count = devlink_sb_pool_count(devlink_sb);
	u16 pool_index;
	int err;

	for (pool_index = 0; pool_index < pool_count; pool_index++) {
		if (*p_idx < start) {
			(*p_idx)++;
			continue;
		}
		err = devlink_nl_sb_pool_fill(msg, devlink,
					      devlink_sb,
					      pool_index,
					      DEVLINK_CMD_SB_POOL_NEW,
					      portid, seq, NLM_F_MULTI);
		if (err)
			return err;
		(*p_idx)++;
	}
	return 0;
}

static int
devlink_nl_cmd_sb_pool_get_dump_one(struct sk_buff *msg,
				    struct devlink *devlink,
				    struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_sb *devlink_sb;
	int err = 0;
	int idx = 0;

	if (!devlink->ops->sb_pool_get)
		return 0;

	list_for_each_entry(devlink_sb, &devlink->sb_list, list) {
		err = __sb_pool_get_dumpit(msg, state->idx, &idx,
					   devlink, devlink_sb,
					   NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq);
		if (err == -EOPNOTSUPP) {
			err = 0;
		} else if (err) {
			state->idx = idx;
			break;
		}
	}

	return err;
}

const struct devlink_cmd devl_cmd_sb_pool_get = {
	.dump_one		= devlink_nl_cmd_sb_pool_get_dump_one,
};

static int devlink_sb_pool_set(struct devlink *devlink, unsigned int sb_index,
			       u16 pool_index, u32 size,
			       enum devlink_sb_threshold_type threshold_type,
			       struct netlink_ext_ack *extack)

{
	const struct devlink_ops *ops = devlink->ops;

	if (ops->sb_pool_set)
		return ops->sb_pool_set(devlink, sb_index, pool_index,
					size, threshold_type, extack);
	return -EOPNOTSUPP;
}

static int devlink_nl_cmd_sb_pool_set_doit(struct sk_buff *skb,
					   struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	enum devlink_sb_threshold_type threshold_type;
	struct devlink_sb *devlink_sb;
	u16 pool_index;
	u32 size;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	err = devlink_sb_pool_index_get_from_info(devlink_sb, info,
						  &pool_index);
	if (err)
		return err;

	err = devlink_sb_th_type_get_from_info(info, &threshold_type);
	if (err)
		return err;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_SB_POOL_SIZE))
		return -EINVAL;

	size = nla_get_u32(info->attrs[DEVLINK_ATTR_SB_POOL_SIZE]);
	return devlink_sb_pool_set(devlink, devlink_sb->index,
				   pool_index, size, threshold_type,
				   info->extack);
}

static int devlink_nl_sb_port_pool_fill(struct sk_buff *msg,
					struct devlink *devlink,
					struct devlink_port *devlink_port,
					struct devlink_sb *devlink_sb,
					u16 pool_index,
					enum devlink_command cmd,
					u32 portid, u32 seq, int flags)
{
	const struct devlink_ops *ops = devlink->ops;
	u32 threshold;
	void *hdr;
	int err;

	err = ops->sb_port_pool_get(devlink_port, devlink_sb->index,
				    pool_index, &threshold);
	if (err)
		return err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX, devlink_port->index))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_INDEX, devlink_sb->index))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_POOL_INDEX, pool_index))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_THRESHOLD, threshold))
		goto nla_put_failure;

	if (ops->sb_occ_port_pool_get) {
		u32 cur;
		u32 max;

		err = ops->sb_occ_port_pool_get(devlink_port, devlink_sb->index,
						pool_index, &cur, &max);
		if (err && err != -EOPNOTSUPP)
			goto sb_occ_get_failure;
		if (!err) {
			if (nla_put_u32(msg, DEVLINK_ATTR_SB_OCC_CUR, cur))
				goto nla_put_failure;
			if (nla_put_u32(msg, DEVLINK_ATTR_SB_OCC_MAX, max))
				goto nla_put_failure;
		}
	}

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	err = -EMSGSIZE;
sb_occ_get_failure:
	genlmsg_cancel(msg, hdr);
	return err;
}

static int devlink_nl_cmd_sb_port_pool_get_doit(struct sk_buff *skb,
						struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = devlink_port->devlink;
	struct devlink_sb *devlink_sb;
	struct sk_buff *msg;
	u16 pool_index;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	err = devlink_sb_pool_index_get_from_info(devlink_sb, info,
						  &pool_index);
	if (err)
		return err;

	if (!devlink->ops->sb_port_pool_get)
		return -EOPNOTSUPP;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_sb_port_pool_fill(msg, devlink, devlink_port,
					   devlink_sb, pool_index,
					   DEVLINK_CMD_SB_PORT_POOL_NEW,
					   info->snd_portid, info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int __sb_port_pool_get_dumpit(struct sk_buff *msg, int start, int *p_idx,
				     struct devlink *devlink,
				     struct devlink_sb *devlink_sb,
				     u32 portid, u32 seq)
{
	struct devlink_port *devlink_port;
	u16 pool_count = devlink_sb_pool_count(devlink_sb);
	unsigned long port_index;
	u16 pool_index;
	int err;

	xa_for_each(&devlink->ports, port_index, devlink_port) {
		for (pool_index = 0; pool_index < pool_count; pool_index++) {
			if (*p_idx < start) {
				(*p_idx)++;
				continue;
			}
			err = devlink_nl_sb_port_pool_fill(msg, devlink,
							   devlink_port,
							   devlink_sb,
							   pool_index,
							   DEVLINK_CMD_SB_PORT_POOL_NEW,
							   portid, seq,
							   NLM_F_MULTI);
			if (err)
				return err;
			(*p_idx)++;
		}
	}
	return 0;
}

static int
devlink_nl_cmd_sb_port_pool_get_dump_one(struct sk_buff *msg,
					 struct devlink *devlink,
					 struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_sb *devlink_sb;
	int idx = 0;
	int err = 0;

	if (!devlink->ops->sb_port_pool_get)
		return 0;

	list_for_each_entry(devlink_sb, &devlink->sb_list, list) {
		err = __sb_port_pool_get_dumpit(msg, state->idx, &idx,
						devlink, devlink_sb,
						NETLINK_CB(cb->skb).portid,
						cb->nlh->nlmsg_seq);
		if (err == -EOPNOTSUPP) {
			err = 0;
		} else if (err) {
			state->idx = idx;
			break;
		}
	}

	return err;
}

const struct devlink_cmd devl_cmd_sb_port_pool_get = {
	.dump_one		= devlink_nl_cmd_sb_port_pool_get_dump_one,
};

static int devlink_sb_port_pool_set(struct devlink_port *devlink_port,
				    unsigned int sb_index, u16 pool_index,
				    u32 threshold,
				    struct netlink_ext_ack *extack)

{
	const struct devlink_ops *ops = devlink_port->devlink->ops;

	if (ops->sb_port_pool_set)
		return ops->sb_port_pool_set(devlink_port, sb_index,
					     pool_index, threshold, extack);
	return -EOPNOTSUPP;
}

static int devlink_nl_cmd_sb_port_pool_set_doit(struct sk_buff *skb,
						struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_sb *devlink_sb;
	u16 pool_index;
	u32 threshold;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	err = devlink_sb_pool_index_get_from_info(devlink_sb, info,
						  &pool_index);
	if (err)
		return err;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_SB_THRESHOLD))
		return -EINVAL;

	threshold = nla_get_u32(info->attrs[DEVLINK_ATTR_SB_THRESHOLD]);
	return devlink_sb_port_pool_set(devlink_port, devlink_sb->index,
					pool_index, threshold, info->extack);
}

static int
devlink_nl_sb_tc_pool_bind_fill(struct sk_buff *msg, struct devlink *devlink,
				struct devlink_port *devlink_port,
				struct devlink_sb *devlink_sb, u16 tc_index,
				enum devlink_sb_pool_type pool_type,
				enum devlink_command cmd,
				u32 portid, u32 seq, int flags)
{
	const struct devlink_ops *ops = devlink->ops;
	u16 pool_index;
	u32 threshold;
	void *hdr;
	int err;

	err = ops->sb_tc_pool_bind_get(devlink_port, devlink_sb->index,
				       tc_index, pool_type,
				       &pool_index, &threshold);
	if (err)
		return err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX, devlink_port->index))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_INDEX, devlink_sb->index))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_TC_INDEX, tc_index))
		goto nla_put_failure;
	if (nla_put_u8(msg, DEVLINK_ATTR_SB_POOL_TYPE, pool_type))
		goto nla_put_failure;
	if (nla_put_u16(msg, DEVLINK_ATTR_SB_POOL_INDEX, pool_index))
		goto nla_put_failure;
	if (nla_put_u32(msg, DEVLINK_ATTR_SB_THRESHOLD, threshold))
		goto nla_put_failure;

	if (ops->sb_occ_tc_port_bind_get) {
		u32 cur;
		u32 max;

		err = ops->sb_occ_tc_port_bind_get(devlink_port,
						   devlink_sb->index,
						   tc_index, pool_type,
						   &cur, &max);
		if (err && err != -EOPNOTSUPP)
			return err;
		if (!err) {
			if (nla_put_u32(msg, DEVLINK_ATTR_SB_OCC_CUR, cur))
				goto nla_put_failure;
			if (nla_put_u32(msg, DEVLINK_ATTR_SB_OCC_MAX, max))
				goto nla_put_failure;
		}
	}

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_sb_tc_pool_bind_get_doit(struct sk_buff *skb,
						   struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = devlink_port->devlink;
	struct devlink_sb *devlink_sb;
	struct sk_buff *msg;
	enum devlink_sb_pool_type pool_type;
	u16 tc_index;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	err = devlink_sb_pool_type_get_from_info(info, &pool_type);
	if (err)
		return err;

	err = devlink_sb_tc_index_get_from_info(devlink_sb, info,
						pool_type, &tc_index);
	if (err)
		return err;

	if (!devlink->ops->sb_tc_pool_bind_get)
		return -EOPNOTSUPP;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_sb_tc_pool_bind_fill(msg, devlink, devlink_port,
					      devlink_sb, tc_index, pool_type,
					      DEVLINK_CMD_SB_TC_POOL_BIND_NEW,
					      info->snd_portid,
					      info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int __sb_tc_pool_bind_get_dumpit(struct sk_buff *msg,
					int start, int *p_idx,
					struct devlink *devlink,
					struct devlink_sb *devlink_sb,
					u32 portid, u32 seq)
{
	struct devlink_port *devlink_port;
	unsigned long port_index;
	u16 tc_index;
	int err;

	xa_for_each(&devlink->ports, port_index, devlink_port) {
		for (tc_index = 0;
		     tc_index < devlink_sb->ingress_tc_count; tc_index++) {
			if (*p_idx < start) {
				(*p_idx)++;
				continue;
			}
			err = devlink_nl_sb_tc_pool_bind_fill(msg, devlink,
							      devlink_port,
							      devlink_sb,
							      tc_index,
							      DEVLINK_SB_POOL_TYPE_INGRESS,
							      DEVLINK_CMD_SB_TC_POOL_BIND_NEW,
							      portid, seq,
							      NLM_F_MULTI);
			if (err)
				return err;
			(*p_idx)++;
		}
		for (tc_index = 0;
		     tc_index < devlink_sb->egress_tc_count; tc_index++) {
			if (*p_idx < start) {
				(*p_idx)++;
				continue;
			}
			err = devlink_nl_sb_tc_pool_bind_fill(msg, devlink,
							      devlink_port,
							      devlink_sb,
							      tc_index,
							      DEVLINK_SB_POOL_TYPE_EGRESS,
							      DEVLINK_CMD_SB_TC_POOL_BIND_NEW,
							      portid, seq,
							      NLM_F_MULTI);
			if (err)
				return err;
			(*p_idx)++;
		}
	}
	return 0;
}

static int
devlink_nl_cmd_sb_tc_pool_bind_get_dump_one(struct sk_buff *msg,
					    struct devlink *devlink,
					    struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_sb *devlink_sb;
	int idx = 0;
	int err = 0;

	if (!devlink->ops->sb_tc_pool_bind_get)
		return 0;

	list_for_each_entry(devlink_sb, &devlink->sb_list, list) {
		err = __sb_tc_pool_bind_get_dumpit(msg, state->idx, &idx,
						   devlink, devlink_sb,
						   NETLINK_CB(cb->skb).portid,
						   cb->nlh->nlmsg_seq);
		if (err == -EOPNOTSUPP) {
			err = 0;
		} else if (err) {
			state->idx = idx;
			break;
		}
	}

	return err;
}

const struct devlink_cmd devl_cmd_sb_tc_pool_bind_get = {
	.dump_one		= devlink_nl_cmd_sb_tc_pool_bind_get_dump_one,
};

static int devlink_sb_tc_pool_bind_set(struct devlink_port *devlink_port,
				       unsigned int sb_index, u16 tc_index,
				       enum devlink_sb_pool_type pool_type,
				       u16 pool_index, u32 threshold,
				       struct netlink_ext_ack *extack)

{
	const struct devlink_ops *ops = devlink_port->devlink->ops;

	if (ops->sb_tc_pool_bind_set)
		return ops->sb_tc_pool_bind_set(devlink_port, sb_index,
						tc_index, pool_type,
						pool_index, threshold, extack);
	return -EOPNOTSUPP;
}

static int devlink_nl_cmd_sb_tc_pool_bind_set_doit(struct sk_buff *skb,
						   struct genl_info *info)
{
	struct devlink_port *devlink_port = info->user_ptr[1];
	struct devlink *devlink = info->user_ptr[0];
	enum devlink_sb_pool_type pool_type;
	struct devlink_sb *devlink_sb;
	u16 tc_index;
	u16 pool_index;
	u32 threshold;
	int err;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	err = devlink_sb_pool_type_get_from_info(info, &pool_type);
	if (err)
		return err;

	err = devlink_sb_tc_index_get_from_info(devlink_sb, info,
						pool_type, &tc_index);
	if (err)
		return err;

	err = devlink_sb_pool_index_get_from_info(devlink_sb, info,
						  &pool_index);
	if (err)
		return err;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_SB_THRESHOLD))
		return -EINVAL;

	threshold = nla_get_u32(info->attrs[DEVLINK_ATTR_SB_THRESHOLD]);
	return devlink_sb_tc_pool_bind_set(devlink_port, devlink_sb->index,
					   tc_index, pool_type,
					   pool_index, threshold, info->extack);
}

static int devlink_nl_cmd_sb_occ_snapshot_doit(struct sk_buff *skb,
					       struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	const struct devlink_ops *ops = devlink->ops;
	struct devlink_sb *devlink_sb;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	if (ops->sb_occ_snapshot)
		return ops->sb_occ_snapshot(devlink, devlink_sb->index);
	return -EOPNOTSUPP;
}

static int devlink_nl_cmd_sb_occ_max_clear_doit(struct sk_buff *skb,
						struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	const struct devlink_ops *ops = devlink->ops;
	struct devlink_sb *devlink_sb;

	devlink_sb = devlink_sb_get_from_info(devlink, info);
	if (IS_ERR(devlink_sb))
		return PTR_ERR(devlink_sb);

	if (ops->sb_occ_max_clear)
		return ops->sb_occ_max_clear(devlink, devlink_sb->index);
	return -EOPNOTSUPP;
}

int devlink_rate_nodes_check(struct devlink *devlink, u16 mode,
			     struct netlink_ext_ack *extack)
{
	struct devlink_rate *devlink_rate;

	list_for_each_entry(devlink_rate, &devlink->rate_list, list)
		if (devlink_rate_is_node(devlink_rate)) {
			NL_SET_ERR_MSG(extack, "Rate node(s) exists.");
			return -EBUSY;
		}
	return 0;
}

int devlink_dpipe_match_put(struct sk_buff *skb,
			    struct devlink_dpipe_match *match)
{
	struct devlink_dpipe_header *header = match->header;
	struct devlink_dpipe_field *field = &header->fields[match->field_id];
	struct nlattr *match_attr;

	match_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_MATCH);
	if (!match_attr)
		return -EMSGSIZE;

	if (nla_put_u32(skb, DEVLINK_ATTR_DPIPE_MATCH_TYPE, match->type) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_HEADER_INDEX, match->header_index) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_HEADER_ID, header->id) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_FIELD_ID, field->id) ||
	    nla_put_u8(skb, DEVLINK_ATTR_DPIPE_HEADER_GLOBAL, header->global))
		goto nla_put_failure;

	nla_nest_end(skb, match_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, match_attr);
	return -EMSGSIZE;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_match_put);

static int devlink_dpipe_matches_put(struct devlink_dpipe_table *table,
				     struct sk_buff *skb)
{
	struct nlattr *matches_attr;

	matches_attr = nla_nest_start_noflag(skb,
					     DEVLINK_ATTR_DPIPE_TABLE_MATCHES);
	if (!matches_attr)
		return -EMSGSIZE;

	if (table->table_ops->matches_dump(table->priv, skb))
		goto nla_put_failure;

	nla_nest_end(skb, matches_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, matches_attr);
	return -EMSGSIZE;
}

int devlink_dpipe_action_put(struct sk_buff *skb,
			     struct devlink_dpipe_action *action)
{
	struct devlink_dpipe_header *header = action->header;
	struct devlink_dpipe_field *field = &header->fields[action->field_id];
	struct nlattr *action_attr;

	action_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_ACTION);
	if (!action_attr)
		return -EMSGSIZE;

	if (nla_put_u32(skb, DEVLINK_ATTR_DPIPE_ACTION_TYPE, action->type) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_HEADER_INDEX, action->header_index) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_HEADER_ID, header->id) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_FIELD_ID, field->id) ||
	    nla_put_u8(skb, DEVLINK_ATTR_DPIPE_HEADER_GLOBAL, header->global))
		goto nla_put_failure;

	nla_nest_end(skb, action_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, action_attr);
	return -EMSGSIZE;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_action_put);

static int devlink_dpipe_actions_put(struct devlink_dpipe_table *table,
				     struct sk_buff *skb)
{
	struct nlattr *actions_attr;

	actions_attr = nla_nest_start_noflag(skb,
					     DEVLINK_ATTR_DPIPE_TABLE_ACTIONS);
	if (!actions_attr)
		return -EMSGSIZE;

	if (table->table_ops->actions_dump(table->priv, skb))
		goto nla_put_failure;

	nla_nest_end(skb, actions_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, actions_attr);
	return -EMSGSIZE;
}

static int devlink_dpipe_table_put(struct sk_buff *skb,
				   struct devlink_dpipe_table *table)
{
	struct nlattr *table_attr;
	u64 table_size;

	table_size = table->table_ops->size_get(table->priv);
	table_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_TABLE);
	if (!table_attr)
		return -EMSGSIZE;

	if (nla_put_string(skb, DEVLINK_ATTR_DPIPE_TABLE_NAME, table->name) ||
	    nla_put_u64_64bit(skb, DEVLINK_ATTR_DPIPE_TABLE_SIZE, table_size,
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;
	if (nla_put_u8(skb, DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED,
		       table->counters_enabled))
		goto nla_put_failure;

	if (table->resource_valid) {
		if (nla_put_u64_64bit(skb, DEVLINK_ATTR_DPIPE_TABLE_RESOURCE_ID,
				      table->resource_id, DEVLINK_ATTR_PAD) ||
		    nla_put_u64_64bit(skb, DEVLINK_ATTR_DPIPE_TABLE_RESOURCE_UNITS,
				      table->resource_units, DEVLINK_ATTR_PAD))
			goto nla_put_failure;
	}
	if (devlink_dpipe_matches_put(table, skb))
		goto nla_put_failure;

	if (devlink_dpipe_actions_put(table, skb))
		goto nla_put_failure;

	nla_nest_end(skb, table_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, table_attr);
	return -EMSGSIZE;
}

static int devlink_dpipe_send_and_alloc_skb(struct sk_buff **pskb,
					    struct genl_info *info)
{
	int err;

	if (*pskb) {
		err = genlmsg_reply(*pskb, info);
		if (err)
			return err;
	}
	*pskb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!*pskb)
		return -ENOMEM;
	return 0;
}

static int devlink_dpipe_tables_fill(struct genl_info *info,
				     enum devlink_command cmd, int flags,
				     struct list_head *dpipe_tables,
				     const char *table_name)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_dpipe_table *table;
	struct nlattr *tables_attr;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	bool incomplete;
	void *hdr;
	int i;
	int err;

	table = list_first_entry(dpipe_tables,
				 struct devlink_dpipe_table, list);
start_again:
	err = devlink_dpipe_send_and_alloc_skb(&skb, info);
	if (err)
		return err;

	hdr = genlmsg_put(skb, info->snd_portid, info->snd_seq,
			  &devlink_nl_family, NLM_F_MULTI, cmd);
	if (!hdr) {
		nlmsg_free(skb);
		return -EMSGSIZE;
	}

	if (devlink_nl_put_handle(skb, devlink))
		goto nla_put_failure;
	tables_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_TABLES);
	if (!tables_attr)
		goto nla_put_failure;

	i = 0;
	incomplete = false;
	list_for_each_entry_from(table, dpipe_tables, list) {
		if (!table_name) {
			err = devlink_dpipe_table_put(skb, table);
			if (err) {
				if (!i)
					goto err_table_put;
				incomplete = true;
				break;
			}
		} else {
			if (!strcmp(table->name, table_name)) {
				err = devlink_dpipe_table_put(skb, table);
				if (err)
					break;
			}
		}
		i++;
	}

	nla_nest_end(skb, tables_attr);
	genlmsg_end(skb, hdr);
	if (incomplete)
		goto start_again;

send_done:
	nlh = nlmsg_put(skb, info->snd_portid, info->snd_seq,
			NLMSG_DONE, 0, flags | NLM_F_MULTI);
	if (!nlh) {
		err = devlink_dpipe_send_and_alloc_skb(&skb, info);
		if (err)
			return err;
		goto send_done;
	}

	return genlmsg_reply(skb, info);

nla_put_failure:
	err = -EMSGSIZE;
err_table_put:
	nlmsg_free(skb);
	return err;
}

static int devlink_nl_cmd_dpipe_table_get(struct sk_buff *skb,
					  struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	const char *table_name =  NULL;

	if (info->attrs[DEVLINK_ATTR_DPIPE_TABLE_NAME])
		table_name = nla_data(info->attrs[DEVLINK_ATTR_DPIPE_TABLE_NAME]);

	return devlink_dpipe_tables_fill(info, DEVLINK_CMD_DPIPE_TABLE_GET, 0,
					 &devlink->dpipe_table_list,
					 table_name);
}

static int devlink_dpipe_value_put(struct sk_buff *skb,
				   struct devlink_dpipe_value *value)
{
	if (nla_put(skb, DEVLINK_ATTR_DPIPE_VALUE,
		    value->value_size, value->value))
		return -EMSGSIZE;
	if (value->mask)
		if (nla_put(skb, DEVLINK_ATTR_DPIPE_VALUE_MASK,
			    value->value_size, value->mask))
			return -EMSGSIZE;
	if (value->mapping_valid)
		if (nla_put_u32(skb, DEVLINK_ATTR_DPIPE_VALUE_MAPPING,
				value->mapping_value))
			return -EMSGSIZE;
	return 0;
}

static int devlink_dpipe_action_value_put(struct sk_buff *skb,
					  struct devlink_dpipe_value *value)
{
	if (!value->action)
		return -EINVAL;
	if (devlink_dpipe_action_put(skb, value->action))
		return -EMSGSIZE;
	if (devlink_dpipe_value_put(skb, value))
		return -EMSGSIZE;
	return 0;
}

static int devlink_dpipe_action_values_put(struct sk_buff *skb,
					   struct devlink_dpipe_value *values,
					   unsigned int values_count)
{
	struct nlattr *action_attr;
	int i;
	int err;

	for (i = 0; i < values_count; i++) {
		action_attr = nla_nest_start_noflag(skb,
						    DEVLINK_ATTR_DPIPE_ACTION_VALUE);
		if (!action_attr)
			return -EMSGSIZE;
		err = devlink_dpipe_action_value_put(skb, &values[i]);
		if (err)
			goto err_action_value_put;
		nla_nest_end(skb, action_attr);
	}
	return 0;

err_action_value_put:
	nla_nest_cancel(skb, action_attr);
	return err;
}

static int devlink_dpipe_match_value_put(struct sk_buff *skb,
					 struct devlink_dpipe_value *value)
{
	if (!value->match)
		return -EINVAL;
	if (devlink_dpipe_match_put(skb, value->match))
		return -EMSGSIZE;
	if (devlink_dpipe_value_put(skb, value))
		return -EMSGSIZE;
	return 0;
}

static int devlink_dpipe_match_values_put(struct sk_buff *skb,
					  struct devlink_dpipe_value *values,
					  unsigned int values_count)
{
	struct nlattr *match_attr;
	int i;
	int err;

	for (i = 0; i < values_count; i++) {
		match_attr = nla_nest_start_noflag(skb,
						   DEVLINK_ATTR_DPIPE_MATCH_VALUE);
		if (!match_attr)
			return -EMSGSIZE;
		err = devlink_dpipe_match_value_put(skb, &values[i]);
		if (err)
			goto err_match_value_put;
		nla_nest_end(skb, match_attr);
	}
	return 0;

err_match_value_put:
	nla_nest_cancel(skb, match_attr);
	return err;
}

static int devlink_dpipe_entry_put(struct sk_buff *skb,
				   struct devlink_dpipe_entry *entry)
{
	struct nlattr *entry_attr, *matches_attr, *actions_attr;
	int err;

	entry_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_ENTRY);
	if (!entry_attr)
		return  -EMSGSIZE;

	if (nla_put_u64_64bit(skb, DEVLINK_ATTR_DPIPE_ENTRY_INDEX, entry->index,
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;
	if (entry->counter_valid)
		if (nla_put_u64_64bit(skb, DEVLINK_ATTR_DPIPE_ENTRY_COUNTER,
				      entry->counter, DEVLINK_ATTR_PAD))
			goto nla_put_failure;

	matches_attr = nla_nest_start_noflag(skb,
					     DEVLINK_ATTR_DPIPE_ENTRY_MATCH_VALUES);
	if (!matches_attr)
		goto nla_put_failure;

	err = devlink_dpipe_match_values_put(skb, entry->match_values,
					     entry->match_values_count);
	if (err) {
		nla_nest_cancel(skb, matches_attr);
		goto err_match_values_put;
	}
	nla_nest_end(skb, matches_attr);

	actions_attr = nla_nest_start_noflag(skb,
					     DEVLINK_ATTR_DPIPE_ENTRY_ACTION_VALUES);
	if (!actions_attr)
		goto nla_put_failure;

	err = devlink_dpipe_action_values_put(skb, entry->action_values,
					      entry->action_values_count);
	if (err) {
		nla_nest_cancel(skb, actions_attr);
		goto err_action_values_put;
	}
	nla_nest_end(skb, actions_attr);

	nla_nest_end(skb, entry_attr);
	return 0;

nla_put_failure:
	err = -EMSGSIZE;
err_match_values_put:
err_action_values_put:
	nla_nest_cancel(skb, entry_attr);
	return err;
}

static struct devlink_dpipe_table *
devlink_dpipe_table_find(struct list_head *dpipe_tables,
			 const char *table_name, struct devlink *devlink)
{
	struct devlink_dpipe_table *table;
	list_for_each_entry_rcu(table, dpipe_tables, list,
				lockdep_is_held(&devlink->lock)) {
		if (!strcmp(table->name, table_name))
			return table;
	}
	return NULL;
}

int devlink_dpipe_entry_ctx_prepare(struct devlink_dpipe_dump_ctx *dump_ctx)
{
	struct devlink *devlink;
	int err;

	err = devlink_dpipe_send_and_alloc_skb(&dump_ctx->skb,
					       dump_ctx->info);
	if (err)
		return err;

	dump_ctx->hdr = genlmsg_put(dump_ctx->skb,
				    dump_ctx->info->snd_portid,
				    dump_ctx->info->snd_seq,
				    &devlink_nl_family, NLM_F_MULTI,
				    dump_ctx->cmd);
	if (!dump_ctx->hdr)
		goto nla_put_failure;

	devlink = dump_ctx->info->user_ptr[0];
	if (devlink_nl_put_handle(dump_ctx->skb, devlink))
		goto nla_put_failure;
	dump_ctx->nest = nla_nest_start_noflag(dump_ctx->skb,
					       DEVLINK_ATTR_DPIPE_ENTRIES);
	if (!dump_ctx->nest)
		goto nla_put_failure;
	return 0;

nla_put_failure:
	nlmsg_free(dump_ctx->skb);
	return -EMSGSIZE;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_entry_ctx_prepare);

int devlink_dpipe_entry_ctx_append(struct devlink_dpipe_dump_ctx *dump_ctx,
				   struct devlink_dpipe_entry *entry)
{
	return devlink_dpipe_entry_put(dump_ctx->skb, entry);
}
EXPORT_SYMBOL_GPL(devlink_dpipe_entry_ctx_append);

int devlink_dpipe_entry_ctx_close(struct devlink_dpipe_dump_ctx *dump_ctx)
{
	nla_nest_end(dump_ctx->skb, dump_ctx->nest);
	genlmsg_end(dump_ctx->skb, dump_ctx->hdr);
	return 0;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_entry_ctx_close);

void devlink_dpipe_entry_clear(struct devlink_dpipe_entry *entry)

{
	unsigned int value_count, value_index;
	struct devlink_dpipe_value *value;

	value = entry->action_values;
	value_count = entry->action_values_count;
	for (value_index = 0; value_index < value_count; value_index++) {
		kfree(value[value_index].value);
		kfree(value[value_index].mask);
	}

	value = entry->match_values;
	value_count = entry->match_values_count;
	for (value_index = 0; value_index < value_count; value_index++) {
		kfree(value[value_index].value);
		kfree(value[value_index].mask);
	}
}
EXPORT_SYMBOL_GPL(devlink_dpipe_entry_clear);

static int devlink_dpipe_entries_fill(struct genl_info *info,
				      enum devlink_command cmd, int flags,
				      struct devlink_dpipe_table *table)
{
	struct devlink_dpipe_dump_ctx dump_ctx;
	struct nlmsghdr *nlh;
	int err;

	dump_ctx.skb = NULL;
	dump_ctx.cmd = cmd;
	dump_ctx.info = info;

	err = table->table_ops->entries_dump(table->priv,
					     table->counters_enabled,
					     &dump_ctx);
	if (err)
		return err;

send_done:
	nlh = nlmsg_put(dump_ctx.skb, info->snd_portid, info->snd_seq,
			NLMSG_DONE, 0, flags | NLM_F_MULTI);
	if (!nlh) {
		err = devlink_dpipe_send_and_alloc_skb(&dump_ctx.skb, info);
		if (err)
			return err;
		goto send_done;
	}
	return genlmsg_reply(dump_ctx.skb, info);
}

static int devlink_nl_cmd_dpipe_entries_get(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_dpipe_table *table;
	const char *table_name;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_DPIPE_TABLE_NAME))
		return -EINVAL;

	table_name = nla_data(info->attrs[DEVLINK_ATTR_DPIPE_TABLE_NAME]);
	table = devlink_dpipe_table_find(&devlink->dpipe_table_list,
					 table_name, devlink);
	if (!table)
		return -EINVAL;

	if (!table->table_ops->entries_dump)
		return -EINVAL;

	return devlink_dpipe_entries_fill(info, DEVLINK_CMD_DPIPE_ENTRIES_GET,
					  0, table);
}

static int devlink_dpipe_fields_put(struct sk_buff *skb,
				    const struct devlink_dpipe_header *header)
{
	struct devlink_dpipe_field *field;
	struct nlattr *field_attr;
	int i;

	for (i = 0; i < header->fields_count; i++) {
		field = &header->fields[i];
		field_attr = nla_nest_start_noflag(skb,
						   DEVLINK_ATTR_DPIPE_FIELD);
		if (!field_attr)
			return -EMSGSIZE;
		if (nla_put_string(skb, DEVLINK_ATTR_DPIPE_FIELD_NAME, field->name) ||
		    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_FIELD_ID, field->id) ||
		    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_FIELD_BITWIDTH, field->bitwidth) ||
		    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_FIELD_MAPPING_TYPE, field->mapping_type))
			goto nla_put_failure;
		nla_nest_end(skb, field_attr);
	}
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, field_attr);
	return -EMSGSIZE;
}

static int devlink_dpipe_header_put(struct sk_buff *skb,
				    struct devlink_dpipe_header *header)
{
	struct nlattr *fields_attr, *header_attr;
	int err;

	header_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_HEADER);
	if (!header_attr)
		return -EMSGSIZE;

	if (nla_put_string(skb, DEVLINK_ATTR_DPIPE_HEADER_NAME, header->name) ||
	    nla_put_u32(skb, DEVLINK_ATTR_DPIPE_HEADER_ID, header->id) ||
	    nla_put_u8(skb, DEVLINK_ATTR_DPIPE_HEADER_GLOBAL, header->global))
		goto nla_put_failure;

	fields_attr = nla_nest_start_noflag(skb,
					    DEVLINK_ATTR_DPIPE_HEADER_FIELDS);
	if (!fields_attr)
		goto nla_put_failure;

	err = devlink_dpipe_fields_put(skb, header);
	if (err) {
		nla_nest_cancel(skb, fields_attr);
		goto nla_put_failure;
	}
	nla_nest_end(skb, fields_attr);
	nla_nest_end(skb, header_attr);
	return 0;

nla_put_failure:
	err = -EMSGSIZE;
	nla_nest_cancel(skb, header_attr);
	return err;
}

static int devlink_dpipe_headers_fill(struct genl_info *info,
				      enum devlink_command cmd, int flags,
				      struct devlink_dpipe_headers *
				      dpipe_headers)
{
	struct devlink *devlink = info->user_ptr[0];
	struct nlattr *headers_attr;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	void *hdr;
	int i, j;
	int err;

	i = 0;
start_again:
	err = devlink_dpipe_send_and_alloc_skb(&skb, info);
	if (err)
		return err;

	hdr = genlmsg_put(skb, info->snd_portid, info->snd_seq,
			  &devlink_nl_family, NLM_F_MULTI, cmd);
	if (!hdr) {
		nlmsg_free(skb);
		return -EMSGSIZE;
	}

	if (devlink_nl_put_handle(skb, devlink))
		goto nla_put_failure;
	headers_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_DPIPE_HEADERS);
	if (!headers_attr)
		goto nla_put_failure;

	j = 0;
	for (; i < dpipe_headers->headers_count; i++) {
		err = devlink_dpipe_header_put(skb, dpipe_headers->headers[i]);
		if (err) {
			if (!j)
				goto err_table_put;
			break;
		}
		j++;
	}
	nla_nest_end(skb, headers_attr);
	genlmsg_end(skb, hdr);
	if (i != dpipe_headers->headers_count)
		goto start_again;

send_done:
	nlh = nlmsg_put(skb, info->snd_portid, info->snd_seq,
			NLMSG_DONE, 0, flags | NLM_F_MULTI);
	if (!nlh) {
		err = devlink_dpipe_send_and_alloc_skb(&skb, info);
		if (err)
			return err;
		goto send_done;
	}
	return genlmsg_reply(skb, info);

nla_put_failure:
	err = -EMSGSIZE;
err_table_put:
	nlmsg_free(skb);
	return err;
}

static int devlink_nl_cmd_dpipe_headers_get(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];

	if (!devlink->dpipe_headers)
		return -EOPNOTSUPP;
	return devlink_dpipe_headers_fill(info, DEVLINK_CMD_DPIPE_HEADERS_GET,
					  0, devlink->dpipe_headers);
}

static int devlink_dpipe_table_counters_set(struct devlink *devlink,
					    const char *table_name,
					    bool enable)
{
	struct devlink_dpipe_table *table;

	table = devlink_dpipe_table_find(&devlink->dpipe_table_list,
					 table_name, devlink);
	if (!table)
		return -EINVAL;

	if (table->counter_control_extern)
		return -EOPNOTSUPP;

	if (!(table->counters_enabled ^ enable))
		return 0;

	table->counters_enabled = enable;
	if (table->table_ops->counters_set_update)
		table->table_ops->counters_set_update(table->priv, enable);
	return 0;
}

static int devlink_nl_cmd_dpipe_table_counters_set(struct sk_buff *skb,
						   struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	const char *table_name;
	bool counters_enable;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_DPIPE_TABLE_NAME) ||
	    GENL_REQ_ATTR_CHECK(info,
				DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED))
		return -EINVAL;

	table_name = nla_data(info->attrs[DEVLINK_ATTR_DPIPE_TABLE_NAME]);
	counters_enable = !!nla_get_u8(info->attrs[DEVLINK_ATTR_DPIPE_TABLE_COUNTERS_ENABLED]);

	return devlink_dpipe_table_counters_set(devlink, table_name,
						counters_enable);
}

static struct devlink_resource *
devlink_resource_find(struct devlink *devlink,
		      struct devlink_resource *resource, u64 resource_id)
{
	struct list_head *resource_list;

	if (resource)
		resource_list = &resource->resource_list;
	else
		resource_list = &devlink->resource_list;

	list_for_each_entry(resource, resource_list, list) {
		struct devlink_resource *child_resource;

		if (resource->id == resource_id)
			return resource;

		child_resource = devlink_resource_find(devlink, resource,
						       resource_id);
		if (child_resource)
			return child_resource;
	}
	return NULL;
}

static void
devlink_resource_validate_children(struct devlink_resource *resource)
{
	struct devlink_resource *child_resource;
	bool size_valid = true;
	u64 parts_size = 0;

	if (list_empty(&resource->resource_list))
		goto out;

	list_for_each_entry(child_resource, &resource->resource_list, list)
		parts_size += child_resource->size_new;

	if (parts_size > resource->size_new)
		size_valid = false;
out:
	resource->size_valid = size_valid;
}

static int
devlink_resource_validate_size(struct devlink_resource *resource, u64 size,
			       struct netlink_ext_ack *extack)
{
	u64 reminder;
	int err = 0;

	if (size > resource->size_params.size_max) {
		NL_SET_ERR_MSG(extack, "Size larger than maximum");
		err = -EINVAL;
	}

	if (size < resource->size_params.size_min) {
		NL_SET_ERR_MSG(extack, "Size smaller than minimum");
		err = -EINVAL;
	}

	div64_u64_rem(size, resource->size_params.size_granularity, &reminder);
	if (reminder) {
		NL_SET_ERR_MSG(extack, "Wrong granularity");
		err = -EINVAL;
	}

	return err;
}

static int devlink_nl_cmd_resource_set(struct sk_buff *skb,
				       struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_resource *resource;
	u64 resource_id;
	u64 size;
	int err;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_RESOURCE_ID) ||
	    GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_RESOURCE_SIZE))
		return -EINVAL;
	resource_id = nla_get_u64(info->attrs[DEVLINK_ATTR_RESOURCE_ID]);

	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (!resource)
		return -EINVAL;

	size = nla_get_u64(info->attrs[DEVLINK_ATTR_RESOURCE_SIZE]);
	err = devlink_resource_validate_size(resource, size, info->extack);
	if (err)
		return err;

	resource->size_new = size;
	devlink_resource_validate_children(resource);
	if (resource->parent)
		devlink_resource_validate_children(resource->parent);
	return 0;
}

static int
devlink_resource_size_params_put(struct devlink_resource *resource,
				 struct sk_buff *skb)
{
	struct devlink_resource_size_params *size_params;

	size_params = &resource->size_params;
	if (nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_SIZE_GRAN,
			      size_params->size_granularity, DEVLINK_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_SIZE_MAX,
			      size_params->size_max, DEVLINK_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_SIZE_MIN,
			      size_params->size_min, DEVLINK_ATTR_PAD) ||
	    nla_put_u8(skb, DEVLINK_ATTR_RESOURCE_UNIT, size_params->unit))
		return -EMSGSIZE;
	return 0;
}

static int devlink_resource_occ_put(struct devlink_resource *resource,
				    struct sk_buff *skb)
{
	if (!resource->occ_get)
		return 0;
	return nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_OCC,
				 resource->occ_get(resource->occ_get_priv),
				 DEVLINK_ATTR_PAD);
}

static int devlink_resource_put(struct devlink *devlink, struct sk_buff *skb,
				struct devlink_resource *resource)
{
	struct devlink_resource *child_resource;
	struct nlattr *child_resource_attr;
	struct nlattr *resource_attr;

	resource_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_RESOURCE);
	if (!resource_attr)
		return -EMSGSIZE;

	if (nla_put_string(skb, DEVLINK_ATTR_RESOURCE_NAME, resource->name) ||
	    nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_SIZE, resource->size,
			      DEVLINK_ATTR_PAD) ||
	    nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_ID, resource->id,
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;
	if (resource->size != resource->size_new &&
	    nla_put_u64_64bit(skb, DEVLINK_ATTR_RESOURCE_SIZE_NEW,
			      resource->size_new, DEVLINK_ATTR_PAD))
		goto nla_put_failure;
	if (devlink_resource_occ_put(resource, skb))
		goto nla_put_failure;
	if (devlink_resource_size_params_put(resource, skb))
		goto nla_put_failure;
	if (list_empty(&resource->resource_list))
		goto out;

	if (nla_put_u8(skb, DEVLINK_ATTR_RESOURCE_SIZE_VALID,
		       resource->size_valid))
		goto nla_put_failure;

	child_resource_attr = nla_nest_start_noflag(skb,
						    DEVLINK_ATTR_RESOURCE_LIST);
	if (!child_resource_attr)
		goto nla_put_failure;

	list_for_each_entry(child_resource, &resource->resource_list, list) {
		if (devlink_resource_put(devlink, skb, child_resource))
			goto resource_put_failure;
	}

	nla_nest_end(skb, child_resource_attr);
out:
	nla_nest_end(skb, resource_attr);
	return 0;

resource_put_failure:
	nla_nest_cancel(skb, child_resource_attr);
nla_put_failure:
	nla_nest_cancel(skb, resource_attr);
	return -EMSGSIZE;
}

static int devlink_resource_fill(struct genl_info *info,
				 enum devlink_command cmd, int flags)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_resource *resource;
	struct nlattr *resources_attr;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	bool incomplete;
	void *hdr;
	int i;
	int err;

	resource = list_first_entry(&devlink->resource_list,
				    struct devlink_resource, list);
start_again:
	err = devlink_dpipe_send_and_alloc_skb(&skb, info);
	if (err)
		return err;

	hdr = genlmsg_put(skb, info->snd_portid, info->snd_seq,
			  &devlink_nl_family, NLM_F_MULTI, cmd);
	if (!hdr) {
		nlmsg_free(skb);
		return -EMSGSIZE;
	}

	if (devlink_nl_put_handle(skb, devlink))
		goto nla_put_failure;

	resources_attr = nla_nest_start_noflag(skb,
					       DEVLINK_ATTR_RESOURCE_LIST);
	if (!resources_attr)
		goto nla_put_failure;

	incomplete = false;
	i = 0;
	list_for_each_entry_from(resource, &devlink->resource_list, list) {
		err = devlink_resource_put(devlink, skb, resource);
		if (err) {
			if (!i)
				goto err_resource_put;
			incomplete = true;
			break;
		}
		i++;
	}
	nla_nest_end(skb, resources_attr);
	genlmsg_end(skb, hdr);
	if (incomplete)
		goto start_again;
send_done:
	nlh = nlmsg_put(skb, info->snd_portid, info->snd_seq,
			NLMSG_DONE, 0, flags | NLM_F_MULTI);
	if (!nlh) {
		err = devlink_dpipe_send_and_alloc_skb(&skb, info);
		if (err)
			return err;
		goto send_done;
	}
	return genlmsg_reply(skb, info);

nla_put_failure:
	err = -EMSGSIZE;
err_resource_put:
	nlmsg_free(skb);
	return err;
}

static int devlink_nl_cmd_resource_dump(struct sk_buff *skb,
					struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];

	if (list_empty(&devlink->resource_list))
		return -EOPNOTSUPP;

	return devlink_resource_fill(info, DEVLINK_CMD_RESOURCE_DUMP, 0);
}

int devlink_resources_validate(struct devlink *devlink,
			       struct devlink_resource *resource,
			       struct genl_info *info)
{
	struct list_head *resource_list;
	int err = 0;

	if (resource)
		resource_list = &resource->resource_list;
	else
		resource_list = &devlink->resource_list;

	list_for_each_entry(resource, resource_list, list) {
		if (!resource->size_valid)
			return -EINVAL;
		err = devlink_resources_validate(devlink, resource, info);
		if (err)
			return err;
	}
	return err;
}

static const struct devlink_param devlink_param_generic[] = {
	{
		.id = DEVLINK_PARAM_GENERIC_ID_INT_ERR_RESET,
		.name = DEVLINK_PARAM_GENERIC_INT_ERR_RESET_NAME,
		.type = DEVLINK_PARAM_GENERIC_INT_ERR_RESET_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_MAX_MACS,
		.name = DEVLINK_PARAM_GENERIC_MAX_MACS_NAME,
		.type = DEVLINK_PARAM_GENERIC_MAX_MACS_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_SRIOV,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_SRIOV_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_SRIOV_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_REGION_SNAPSHOT,
		.name = DEVLINK_PARAM_GENERIC_REGION_SNAPSHOT_NAME,
		.type = DEVLINK_PARAM_GENERIC_REGION_SNAPSHOT_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_IGNORE_ARI,
		.name = DEVLINK_PARAM_GENERIC_IGNORE_ARI_NAME,
		.type = DEVLINK_PARAM_GENERIC_IGNORE_ARI_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MAX,
		.name = DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MAX_NAME,
		.type = DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MAX_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MIN,
		.name = DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MIN_NAME,
		.type = DEVLINK_PARAM_GENERIC_MSIX_VEC_PER_PF_MIN_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_FW_LOAD_POLICY,
		.name = DEVLINK_PARAM_GENERIC_FW_LOAD_POLICY_NAME,
		.type = DEVLINK_PARAM_GENERIC_FW_LOAD_POLICY_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_RESET_DEV_ON_DRV_PROBE,
		.name = DEVLINK_PARAM_GENERIC_RESET_DEV_ON_DRV_PROBE_NAME,
		.type = DEVLINK_PARAM_GENERIC_RESET_DEV_ON_DRV_PROBE_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_ROCE,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_ROCE_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_ROCE_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_REMOTE_DEV_RESET,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_REMOTE_DEV_RESET_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_REMOTE_DEV_RESET_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_ETH,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_ETH_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_ETH_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_RDMA,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_RDMA_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_RDMA_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_VNET,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_VNET_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_VNET_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_ENABLE_IWARP,
		.name = DEVLINK_PARAM_GENERIC_ENABLE_IWARP_NAME,
		.type = DEVLINK_PARAM_GENERIC_ENABLE_IWARP_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_IO_EQ_SIZE,
		.name = DEVLINK_PARAM_GENERIC_IO_EQ_SIZE_NAME,
		.type = DEVLINK_PARAM_GENERIC_IO_EQ_SIZE_TYPE,
	},
	{
		.id = DEVLINK_PARAM_GENERIC_ID_EVENT_EQ_SIZE,
		.name = DEVLINK_PARAM_GENERIC_EVENT_EQ_SIZE_NAME,
		.type = DEVLINK_PARAM_GENERIC_EVENT_EQ_SIZE_TYPE,
	},
};

static int devlink_param_generic_verify(const struct devlink_param *param)
{
	/* verify it match generic parameter by id and name */
	if (param->id > DEVLINK_PARAM_GENERIC_ID_MAX)
		return -EINVAL;
	if (strcmp(param->name, devlink_param_generic[param->id].name))
		return -ENOENT;

	WARN_ON(param->type != devlink_param_generic[param->id].type);

	return 0;
}

static int devlink_param_driver_verify(const struct devlink_param *param)
{
	int i;

	if (param->id <= DEVLINK_PARAM_GENERIC_ID_MAX)
		return -EINVAL;
	/* verify no such name in generic params */
	for (i = 0; i <= DEVLINK_PARAM_GENERIC_ID_MAX; i++)
		if (!strcmp(param->name, devlink_param_generic[i].name))
			return -EEXIST;

	return 0;
}

static struct devlink_param_item *
devlink_param_find_by_name(struct xarray *params, const char *param_name)
{
	struct devlink_param_item *param_item;
	unsigned long param_id;

	xa_for_each(params, param_id, param_item) {
		if (!strcmp(param_item->param->name, param_name))
			return param_item;
	}
	return NULL;
}

static struct devlink_param_item *
devlink_param_find_by_id(struct xarray *params, u32 param_id)
{
	return xa_load(params, param_id);
}

static bool
devlink_param_cmode_is_supported(const struct devlink_param *param,
				 enum devlink_param_cmode cmode)
{
	return test_bit(cmode, &param->supported_cmodes);
}

static int devlink_param_get(struct devlink *devlink,
			     const struct devlink_param *param,
			     struct devlink_param_gset_ctx *ctx)
{
	if (!param->get || devlink->reload_failed)
		return -EOPNOTSUPP;
	return param->get(devlink, param->id, ctx);
}

static int devlink_param_set(struct devlink *devlink,
			     const struct devlink_param *param,
			     struct devlink_param_gset_ctx *ctx)
{
	if (!param->set || devlink->reload_failed)
		return -EOPNOTSUPP;
	return param->set(devlink, param->id, ctx);
}

static int
devlink_param_type_to_nla_type(enum devlink_param_type param_type)
{
	switch (param_type) {
	case DEVLINK_PARAM_TYPE_U8:
		return NLA_U8;
	case DEVLINK_PARAM_TYPE_U16:
		return NLA_U16;
	case DEVLINK_PARAM_TYPE_U32:
		return NLA_U32;
	case DEVLINK_PARAM_TYPE_STRING:
		return NLA_STRING;
	case DEVLINK_PARAM_TYPE_BOOL:
		return NLA_FLAG;
	default:
		return -EINVAL;
	}
}

static int
devlink_nl_param_value_fill_one(struct sk_buff *msg,
				enum devlink_param_type type,
				enum devlink_param_cmode cmode,
				union devlink_param_value val)
{
	struct nlattr *param_value_attr;

	param_value_attr = nla_nest_start_noflag(msg,
						 DEVLINK_ATTR_PARAM_VALUE);
	if (!param_value_attr)
		goto nla_put_failure;

	if (nla_put_u8(msg, DEVLINK_ATTR_PARAM_VALUE_CMODE, cmode))
		goto value_nest_cancel;

	switch (type) {
	case DEVLINK_PARAM_TYPE_U8:
		if (nla_put_u8(msg, DEVLINK_ATTR_PARAM_VALUE_DATA, val.vu8))
			goto value_nest_cancel;
		break;
	case DEVLINK_PARAM_TYPE_U16:
		if (nla_put_u16(msg, DEVLINK_ATTR_PARAM_VALUE_DATA, val.vu16))
			goto value_nest_cancel;
		break;
	case DEVLINK_PARAM_TYPE_U32:
		if (nla_put_u32(msg, DEVLINK_ATTR_PARAM_VALUE_DATA, val.vu32))
			goto value_nest_cancel;
		break;
	case DEVLINK_PARAM_TYPE_STRING:
		if (nla_put_string(msg, DEVLINK_ATTR_PARAM_VALUE_DATA,
				   val.vstr))
			goto value_nest_cancel;
		break;
	case DEVLINK_PARAM_TYPE_BOOL:
		if (val.vbool &&
		    nla_put_flag(msg, DEVLINK_ATTR_PARAM_VALUE_DATA))
			goto value_nest_cancel;
		break;
	}

	nla_nest_end(msg, param_value_attr);
	return 0;

value_nest_cancel:
	nla_nest_cancel(msg, param_value_attr);
nla_put_failure:
	return -EMSGSIZE;
}

static int devlink_nl_param_fill(struct sk_buff *msg, struct devlink *devlink,
				 unsigned int port_index,
				 struct devlink_param_item *param_item,
				 enum devlink_command cmd,
				 u32 portid, u32 seq, int flags)
{
	union devlink_param_value param_value[DEVLINK_PARAM_CMODE_MAX + 1];
	bool param_value_set[DEVLINK_PARAM_CMODE_MAX + 1] = {};
	const struct devlink_param *param = param_item->param;
	struct devlink_param_gset_ctx ctx;
	struct nlattr *param_values_list;
	struct nlattr *param_attr;
	int nla_type;
	void *hdr;
	int err;
	int i;

	/* Get value from driver part to driverinit configuration mode */
	for (i = 0; i <= DEVLINK_PARAM_CMODE_MAX; i++) {
		if (!devlink_param_cmode_is_supported(param, i))
			continue;
		if (i == DEVLINK_PARAM_CMODE_DRIVERINIT) {
			if (param_item->driverinit_value_new_valid)
				param_value[i] = param_item->driverinit_value_new;
			else if (param_item->driverinit_value_valid)
				param_value[i] = param_item->driverinit_value;
			else
				return -EOPNOTSUPP;
		} else {
			ctx.cmode = i;
			err = devlink_param_get(devlink, param, &ctx);
			if (err)
				return err;
			param_value[i] = ctx.val;
		}
		param_value_set[i] = true;
	}

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto genlmsg_cancel;

	if (cmd == DEVLINK_CMD_PORT_PARAM_GET ||
	    cmd == DEVLINK_CMD_PORT_PARAM_NEW ||
	    cmd == DEVLINK_CMD_PORT_PARAM_DEL)
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX, port_index))
			goto genlmsg_cancel;

	param_attr = nla_nest_start_noflag(msg, DEVLINK_ATTR_PARAM);
	if (!param_attr)
		goto genlmsg_cancel;
	if (nla_put_string(msg, DEVLINK_ATTR_PARAM_NAME, param->name))
		goto param_nest_cancel;
	if (param->generic && nla_put_flag(msg, DEVLINK_ATTR_PARAM_GENERIC))
		goto param_nest_cancel;

	nla_type = devlink_param_type_to_nla_type(param->type);
	if (nla_type < 0)
		goto param_nest_cancel;
	if (nla_put_u8(msg, DEVLINK_ATTR_PARAM_TYPE, nla_type))
		goto param_nest_cancel;

	param_values_list = nla_nest_start_noflag(msg,
						  DEVLINK_ATTR_PARAM_VALUES_LIST);
	if (!param_values_list)
		goto param_nest_cancel;

	for (i = 0; i <= DEVLINK_PARAM_CMODE_MAX; i++) {
		if (!param_value_set[i])
			continue;
		err = devlink_nl_param_value_fill_one(msg, param->type,
						      i, param_value[i]);
		if (err)
			goto values_list_nest_cancel;
	}

	nla_nest_end(msg, param_values_list);
	nla_nest_end(msg, param_attr);
	genlmsg_end(msg, hdr);
	return 0;

values_list_nest_cancel:
	nla_nest_end(msg, param_values_list);
param_nest_cancel:
	nla_nest_cancel(msg, param_attr);
genlmsg_cancel:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void devlink_param_notify(struct devlink *devlink,
				 unsigned int port_index,
				 struct devlink_param_item *param_item,
				 enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_PARAM_NEW && cmd != DEVLINK_CMD_PARAM_DEL &&
		cmd != DEVLINK_CMD_PORT_PARAM_NEW &&
		cmd != DEVLINK_CMD_PORT_PARAM_DEL);

	/* devlink_notify_register() / devlink_notify_unregister()
	 * will replay the notifications if the params are added/removed
	 * outside of the lifetime of the instance.
	 */
	if (!devl_is_registered(devlink))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;
	err = devlink_nl_param_fill(msg, devlink, port_index, param_item, cmd,
				    0, 0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static int
devlink_nl_cmd_param_get_dump_one(struct sk_buff *msg, struct devlink *devlink,
				  struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_param_item *param_item;
	unsigned long param_id;
	int err = 0;

	xa_for_each_start(&devlink->params, param_id, param_item, state->idx) {
		err = devlink_nl_param_fill(msg, devlink, 0, param_item,
					    DEVLINK_CMD_PARAM_GET,
					    NETLINK_CB(cb->skb).portid,
					    cb->nlh->nlmsg_seq,
					    NLM_F_MULTI);
		if (err == -EOPNOTSUPP) {
			err = 0;
		} else if (err) {
			state->idx = param_id;
			break;
		}
	}

	return err;
}

const struct devlink_cmd devl_cmd_param_get = {
	.dump_one		= devlink_nl_cmd_param_get_dump_one,
};

static int
devlink_param_type_get_from_info(struct genl_info *info,
				 enum devlink_param_type *param_type)
{
	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_PARAM_TYPE))
		return -EINVAL;

	switch (nla_get_u8(info->attrs[DEVLINK_ATTR_PARAM_TYPE])) {
	case NLA_U8:
		*param_type = DEVLINK_PARAM_TYPE_U8;
		break;
	case NLA_U16:
		*param_type = DEVLINK_PARAM_TYPE_U16;
		break;
	case NLA_U32:
		*param_type = DEVLINK_PARAM_TYPE_U32;
		break;
	case NLA_STRING:
		*param_type = DEVLINK_PARAM_TYPE_STRING;
		break;
	case NLA_FLAG:
		*param_type = DEVLINK_PARAM_TYPE_BOOL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
devlink_param_value_get_from_info(const struct devlink_param *param,
				  struct genl_info *info,
				  union devlink_param_value *value)
{
	struct nlattr *param_data;
	int len;

	param_data = info->attrs[DEVLINK_ATTR_PARAM_VALUE_DATA];

	if (param->type != DEVLINK_PARAM_TYPE_BOOL && !param_data)
		return -EINVAL;

	switch (param->type) {
	case DEVLINK_PARAM_TYPE_U8:
		if (nla_len(param_data) != sizeof(u8))
			return -EINVAL;
		value->vu8 = nla_get_u8(param_data);
		break;
	case DEVLINK_PARAM_TYPE_U16:
		if (nla_len(param_data) != sizeof(u16))
			return -EINVAL;
		value->vu16 = nla_get_u16(param_data);
		break;
	case DEVLINK_PARAM_TYPE_U32:
		if (nla_len(param_data) != sizeof(u32))
			return -EINVAL;
		value->vu32 = nla_get_u32(param_data);
		break;
	case DEVLINK_PARAM_TYPE_STRING:
		len = strnlen(nla_data(param_data), nla_len(param_data));
		if (len == nla_len(param_data) ||
		    len >= __DEVLINK_PARAM_MAX_STRING_VALUE)
			return -EINVAL;
		strcpy(value->vstr, nla_data(param_data));
		break;
	case DEVLINK_PARAM_TYPE_BOOL:
		if (param_data && nla_len(param_data))
			return -EINVAL;
		value->vbool = nla_get_flag(param_data);
		break;
	}
	return 0;
}

static struct devlink_param_item *
devlink_param_get_from_info(struct xarray *params, struct genl_info *info)
{
	char *param_name;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_PARAM_NAME))
		return NULL;

	param_name = nla_data(info->attrs[DEVLINK_ATTR_PARAM_NAME]);
	return devlink_param_find_by_name(params, param_name);
}

static int devlink_nl_cmd_param_get_doit(struct sk_buff *skb,
					 struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_param_item *param_item;
	struct sk_buff *msg;
	int err;

	param_item = devlink_param_get_from_info(&devlink->params, info);
	if (!param_item)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_param_fill(msg, devlink, 0, param_item,
				    DEVLINK_CMD_PARAM_GET,
				    info->snd_portid, info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int __devlink_nl_cmd_param_set_doit(struct devlink *devlink,
					   unsigned int port_index,
					   struct xarray *params,
					   struct genl_info *info,
					   enum devlink_command cmd)
{
	enum devlink_param_type param_type;
	struct devlink_param_gset_ctx ctx;
	enum devlink_param_cmode cmode;
	struct devlink_param_item *param_item;
	const struct devlink_param *param;
	union devlink_param_value value;
	int err = 0;

	param_item = devlink_param_get_from_info(params, info);
	if (!param_item)
		return -EINVAL;
	param = param_item->param;
	err = devlink_param_type_get_from_info(info, &param_type);
	if (err)
		return err;
	if (param_type != param->type)
		return -EINVAL;
	err = devlink_param_value_get_from_info(param, info, &value);
	if (err)
		return err;
	if (param->validate) {
		err = param->validate(devlink, param->id, value, info->extack);
		if (err)
			return err;
	}

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_PARAM_VALUE_CMODE))
		return -EINVAL;
	cmode = nla_get_u8(info->attrs[DEVLINK_ATTR_PARAM_VALUE_CMODE]);
	if (!devlink_param_cmode_is_supported(param, cmode))
		return -EOPNOTSUPP;

	if (cmode == DEVLINK_PARAM_CMODE_DRIVERINIT) {
		param_item->driverinit_value_new = value;
		param_item->driverinit_value_new_valid = true;
	} else {
		if (!param->set)
			return -EOPNOTSUPP;
		ctx.val = value;
		ctx.cmode = cmode;
		err = devlink_param_set(devlink, param, &ctx);
		if (err)
			return err;
	}

	devlink_param_notify(devlink, port_index, param_item, cmd);
	return 0;
}

static int devlink_nl_cmd_param_set_doit(struct sk_buff *skb,
					 struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];

	return __devlink_nl_cmd_param_set_doit(devlink, 0, &devlink->params,
					       info, DEVLINK_CMD_PARAM_NEW);
}

static int devlink_nl_cmd_port_param_get_dumpit(struct sk_buff *msg,
						struct netlink_callback *cb)
{
	NL_SET_ERR_MSG(cb->extack, "Port params are not supported");
	return msg->len;
}

static int devlink_nl_cmd_port_param_get_doit(struct sk_buff *skb,
					      struct genl_info *info)
{
	NL_SET_ERR_MSG(info->extack, "Port params are not supported");
	return -EINVAL;
}

static int devlink_nl_cmd_port_param_set_doit(struct sk_buff *skb,
					      struct genl_info *info)
{
	NL_SET_ERR_MSG(info->extack, "Port params are not supported");
	return -EINVAL;
}

static int devlink_nl_region_snapshot_id_put(struct sk_buff *msg,
					     struct devlink *devlink,
					     struct devlink_snapshot *snapshot)
{
	struct nlattr *snap_attr;
	int err;

	snap_attr = nla_nest_start_noflag(msg, DEVLINK_ATTR_REGION_SNAPSHOT);
	if (!snap_attr)
		return -EINVAL;

	err = nla_put_u32(msg, DEVLINK_ATTR_REGION_SNAPSHOT_ID, snapshot->id);
	if (err)
		goto nla_put_failure;

	nla_nest_end(msg, snap_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, snap_attr);
	return err;
}

static int devlink_nl_region_snapshots_id_put(struct sk_buff *msg,
					      struct devlink *devlink,
					      struct devlink_region *region)
{
	struct devlink_snapshot *snapshot;
	struct nlattr *snapshots_attr;
	int err;

	snapshots_attr = nla_nest_start_noflag(msg,
					       DEVLINK_ATTR_REGION_SNAPSHOTS);
	if (!snapshots_attr)
		return -EINVAL;

	list_for_each_entry(snapshot, &region->snapshot_list, list) {
		err = devlink_nl_region_snapshot_id_put(msg, devlink, snapshot);
		if (err)
			goto nla_put_failure;
	}

	nla_nest_end(msg, snapshots_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, snapshots_attr);
	return err;
}

static int devlink_nl_region_fill(struct sk_buff *msg, struct devlink *devlink,
				  enum devlink_command cmd, u32 portid,
				  u32 seq, int flags,
				  struct devlink_region *region)
{
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	err = devlink_nl_put_handle(msg, devlink);
	if (err)
		goto nla_put_failure;

	if (region->port) {
		err = nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX,
				  region->port->index);
		if (err)
			goto nla_put_failure;
	}

	err = nla_put_string(msg, DEVLINK_ATTR_REGION_NAME, region->ops->name);
	if (err)
		goto nla_put_failure;

	err = nla_put_u64_64bit(msg, DEVLINK_ATTR_REGION_SIZE,
				region->size,
				DEVLINK_ATTR_PAD);
	if (err)
		goto nla_put_failure;

	err = nla_put_u32(msg, DEVLINK_ATTR_REGION_MAX_SNAPSHOTS,
			  region->max_snapshots);
	if (err)
		goto nla_put_failure;

	err = devlink_nl_region_snapshots_id_put(msg, devlink, region);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return err;
}

static struct sk_buff *
devlink_nl_region_notify_build(struct devlink_region *region,
			       struct devlink_snapshot *snapshot,
			       enum devlink_command cmd, u32 portid, u32 seq)
{
	struct devlink *devlink = region->devlink;
	struct sk_buff *msg;
	void *hdr;
	int err;


	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return ERR_PTR(-ENOMEM);

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, 0, cmd);
	if (!hdr) {
		err = -EMSGSIZE;
		goto out_free_msg;
	}

	err = devlink_nl_put_handle(msg, devlink);
	if (err)
		goto out_cancel_msg;

	if (region->port) {
		err = nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX,
				  region->port->index);
		if (err)
			goto out_cancel_msg;
	}

	err = nla_put_string(msg, DEVLINK_ATTR_REGION_NAME,
			     region->ops->name);
	if (err)
		goto out_cancel_msg;

	if (snapshot) {
		err = nla_put_u32(msg, DEVLINK_ATTR_REGION_SNAPSHOT_ID,
				  snapshot->id);
		if (err)
			goto out_cancel_msg;
	} else {
		err = nla_put_u64_64bit(msg, DEVLINK_ATTR_REGION_SIZE,
					region->size, DEVLINK_ATTR_PAD);
		if (err)
			goto out_cancel_msg;
	}
	genlmsg_end(msg, hdr);

	return msg;

out_cancel_msg:
	genlmsg_cancel(msg, hdr);
out_free_msg:
	nlmsg_free(msg);
	return ERR_PTR(err);
}

static void devlink_nl_region_notify(struct devlink_region *region,
				     struct devlink_snapshot *snapshot,
				     enum devlink_command cmd)
{
	struct devlink *devlink = region->devlink;
	struct sk_buff *msg;

	WARN_ON(cmd != DEVLINK_CMD_REGION_NEW && cmd != DEVLINK_CMD_REGION_DEL);
	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = devlink_nl_region_notify_build(region, snapshot, cmd, 0, 0);
	if (IS_ERR(msg))
		return;

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink), msg,
				0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

/**
 * __devlink_snapshot_id_increment - Increment number of snapshots using an id
 *	@devlink: devlink instance
 *	@id: the snapshot id
 *
 *	Track when a new snapshot begins using an id. Load the count for the
 *	given id from the snapshot xarray, increment it, and store it back.
 *
 *	Called when a new snapshot is created with the given id.
 *
 *	The id *must* have been previously allocated by
 *	devlink_region_snapshot_id_get().
 *
 *	Returns 0 on success, or an error on failure.
 */
static int __devlink_snapshot_id_increment(struct devlink *devlink, u32 id)
{
	unsigned long count;
	void *p;
	int err;

	xa_lock(&devlink->snapshot_ids);
	p = xa_load(&devlink->snapshot_ids, id);
	if (WARN_ON(!p)) {
		err = -EINVAL;
		goto unlock;
	}

	if (WARN_ON(!xa_is_value(p))) {
		err = -EINVAL;
		goto unlock;
	}

	count = xa_to_value(p);
	count++;

	err = xa_err(__xa_store(&devlink->snapshot_ids, id, xa_mk_value(count),
				GFP_ATOMIC));
unlock:
	xa_unlock(&devlink->snapshot_ids);
	return err;
}

/**
 * __devlink_snapshot_id_decrement - Decrease number of snapshots using an id
 *	@devlink: devlink instance
 *	@id: the snapshot id
 *
 *	Track when a snapshot is deleted and stops using an id. Load the count
 *	for the given id from the snapshot xarray, decrement it, and store it
 *	back.
 *
 *	If the count reaches zero, erase this id from the xarray, freeing it
 *	up for future re-use by devlink_region_snapshot_id_get().
 *
 *	Called when a snapshot using the given id is deleted, and when the
 *	initial allocator of the id is finished using it.
 */
static void __devlink_snapshot_id_decrement(struct devlink *devlink, u32 id)
{
	unsigned long count;
	void *p;

	xa_lock(&devlink->snapshot_ids);
	p = xa_load(&devlink->snapshot_ids, id);
	if (WARN_ON(!p))
		goto unlock;

	if (WARN_ON(!xa_is_value(p)))
		goto unlock;

	count = xa_to_value(p);

	if (count > 1) {
		count--;
		__xa_store(&devlink->snapshot_ids, id, xa_mk_value(count),
			   GFP_ATOMIC);
	} else {
		/* If this was the last user, we can erase this id */
		__xa_erase(&devlink->snapshot_ids, id);
	}
unlock:
	xa_unlock(&devlink->snapshot_ids);
}

/**
 *	__devlink_snapshot_id_insert - Insert a specific snapshot ID
 *	@devlink: devlink instance
 *	@id: the snapshot id
 *
 *	Mark the given snapshot id as used by inserting a zero value into the
 *	snapshot xarray.
 *
 *	This must be called while holding the devlink instance lock. Unlike
 *	devlink_snapshot_id_get, the initial reference count is zero, not one.
 *	It is expected that the id will immediately be used before
 *	releasing the devlink instance lock.
 *
 *	Returns zero on success, or an error code if the snapshot id could not
 *	be inserted.
 */
static int __devlink_snapshot_id_insert(struct devlink *devlink, u32 id)
{
	int err;

	xa_lock(&devlink->snapshot_ids);
	if (xa_load(&devlink->snapshot_ids, id)) {
		xa_unlock(&devlink->snapshot_ids);
		return -EEXIST;
	}
	err = xa_err(__xa_store(&devlink->snapshot_ids, id, xa_mk_value(0),
				GFP_ATOMIC));
	xa_unlock(&devlink->snapshot_ids);
	return err;
}

/**
 *	__devlink_region_snapshot_id_get - get snapshot ID
 *	@devlink: devlink instance
 *	@id: storage to return snapshot id
 *
 *	Allocates a new snapshot id. Returns zero on success, or a negative
 *	error on failure. Must be called while holding the devlink instance
 *	lock.
 *
 *	Snapshot IDs are tracked using an xarray which stores the number of
 *	users of the snapshot id.
 *
 *	Note that the caller of this function counts as a 'user', in order to
 *	avoid race conditions. The caller must release its hold on the
 *	snapshot by using devlink_region_snapshot_id_put.
 */
static int __devlink_region_snapshot_id_get(struct devlink *devlink, u32 *id)
{
	return xa_alloc(&devlink->snapshot_ids, id, xa_mk_value(1),
			xa_limit_32b, GFP_KERNEL);
}

/**
 *	__devlink_region_snapshot_create - create a new snapshot
 *	This will add a new snapshot of a region. The snapshot
 *	will be stored on the region struct and can be accessed
 *	from devlink. This is useful for future analyses of snapshots.
 *	Multiple snapshots can be created on a region.
 *	The @snapshot_id should be obtained using the getter function.
 *
 *	Must be called only while holding the region snapshot lock.
 *
 *	@region: devlink region of the snapshot
 *	@data: snapshot data
 *	@snapshot_id: snapshot id to be created
 */
static int
__devlink_region_snapshot_create(struct devlink_region *region,
				 u8 *data, u32 snapshot_id)
{
	struct devlink *devlink = region->devlink;
	struct devlink_snapshot *snapshot;
	int err;

	lockdep_assert_held(&region->snapshot_lock);

	/* check if region can hold one more snapshot */
	if (region->cur_snapshots == region->max_snapshots)
		return -ENOSPC;

	if (devlink_region_snapshot_get_by_id(region, snapshot_id))
		return -EEXIST;

	snapshot = kzalloc(sizeof(*snapshot), GFP_KERNEL);
	if (!snapshot)
		return -ENOMEM;

	err = __devlink_snapshot_id_increment(devlink, snapshot_id);
	if (err)
		goto err_snapshot_id_increment;

	snapshot->id = snapshot_id;
	snapshot->region = region;
	snapshot->data = data;

	list_add_tail(&snapshot->list, &region->snapshot_list);

	region->cur_snapshots++;

	devlink_nl_region_notify(region, snapshot, DEVLINK_CMD_REGION_NEW);
	return 0;

err_snapshot_id_increment:
	kfree(snapshot);
	return err;
}

static void devlink_region_snapshot_del(struct devlink_region *region,
					struct devlink_snapshot *snapshot)
{
	struct devlink *devlink = region->devlink;

	lockdep_assert_held(&region->snapshot_lock);

	devlink_nl_region_notify(region, snapshot, DEVLINK_CMD_REGION_DEL);
	region->cur_snapshots--;
	list_del(&snapshot->list);
	region->ops->destructor(snapshot->data);
	__devlink_snapshot_id_decrement(devlink, snapshot->id);
	kfree(snapshot);
}

static int devlink_nl_cmd_region_get_doit(struct sk_buff *skb,
					  struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_port *port = NULL;
	struct devlink_region *region;
	const char *region_name;
	struct sk_buff *msg;
	unsigned int index;
	int err;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_REGION_NAME))
		return -EINVAL;

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port)
			return -ENODEV;
	}

	region_name = nla_data(info->attrs[DEVLINK_ATTR_REGION_NAME]);
	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_region_fill(msg, devlink, DEVLINK_CMD_REGION_GET,
				     info->snd_portid, info->snd_seq, 0,
				     region);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int devlink_nl_cmd_region_get_port_dumpit(struct sk_buff *msg,
						 struct netlink_callback *cb,
						 struct devlink_port *port,
						 int *idx,
						 int start)
{
	struct devlink_region *region;
	int err = 0;

	list_for_each_entry(region, &port->region_list, list) {
		if (*idx < start) {
			(*idx)++;
			continue;
		}
		err = devlink_nl_region_fill(msg, port->devlink,
					     DEVLINK_CMD_REGION_GET,
					     NETLINK_CB(cb->skb).portid,
					     cb->nlh->nlmsg_seq,
					     NLM_F_MULTI, region);
		if (err)
			goto out;
		(*idx)++;
	}

out:
	return err;
}

static int
devlink_nl_cmd_region_get_dump_one(struct sk_buff *msg, struct devlink *devlink,
				   struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_region *region;
	struct devlink_port *port;
	unsigned long port_index;
	int idx = 0;
	int err;

	list_for_each_entry(region, &devlink->region_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_region_fill(msg, devlink,
					     DEVLINK_CMD_REGION_GET,
					     NETLINK_CB(cb->skb).portid,
					     cb->nlh->nlmsg_seq,
					     NLM_F_MULTI, region);
		if (err) {
			state->idx = idx;
			return err;
		}
		idx++;
	}

	xa_for_each(&devlink->ports, port_index, port) {
		err = devlink_nl_cmd_region_get_port_dumpit(msg, cb, port, &idx,
							    state->idx);
		if (err) {
			state->idx = idx;
			return err;
		}
	}

	return 0;
}

const struct devlink_cmd devl_cmd_region_get = {
	.dump_one		= devlink_nl_cmd_region_get_dump_one,
};

static int devlink_nl_cmd_region_del(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_snapshot *snapshot;
	struct devlink_port *port = NULL;
	struct devlink_region *region;
	const char *region_name;
	unsigned int index;
	u32 snapshot_id;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_REGION_NAME) ||
	    GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_REGION_SNAPSHOT_ID))
		return -EINVAL;

	region_name = nla_data(info->attrs[DEVLINK_ATTR_REGION_NAME]);
	snapshot_id = nla_get_u32(info->attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID]);

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port)
			return -ENODEV;
	}

	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region)
		return -EINVAL;

	mutex_lock(&region->snapshot_lock);
	snapshot = devlink_region_snapshot_get_by_id(region, snapshot_id);
	if (!snapshot) {
		mutex_unlock(&region->snapshot_lock);
		return -EINVAL;
	}

	devlink_region_snapshot_del(region, snapshot);
	mutex_unlock(&region->snapshot_lock);
	return 0;
}

static int
devlink_nl_cmd_region_new(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_snapshot *snapshot;
	struct devlink_port *port = NULL;
	struct nlattr *snapshot_id_attr;
	struct devlink_region *region;
	const char *region_name;
	unsigned int index;
	u32 snapshot_id;
	u8 *data;
	int err;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_REGION_NAME)) {
		NL_SET_ERR_MSG(info->extack, "No region name provided");
		return -EINVAL;
	}

	region_name = nla_data(info->attrs[DEVLINK_ATTR_REGION_NAME]);

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port)
			return -ENODEV;
	}

	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region) {
		NL_SET_ERR_MSG(info->extack, "The requested region does not exist");
		return -EINVAL;
	}

	if (!region->ops->snapshot) {
		NL_SET_ERR_MSG(info->extack, "The requested region does not support taking an immediate snapshot");
		return -EOPNOTSUPP;
	}

	mutex_lock(&region->snapshot_lock);

	if (region->cur_snapshots == region->max_snapshots) {
		NL_SET_ERR_MSG(info->extack, "The region has reached the maximum number of stored snapshots");
		err = -ENOSPC;
		goto unlock;
	}

	snapshot_id_attr = info->attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID];
	if (snapshot_id_attr) {
		snapshot_id = nla_get_u32(snapshot_id_attr);

		if (devlink_region_snapshot_get_by_id(region, snapshot_id)) {
			NL_SET_ERR_MSG(info->extack, "The requested snapshot id is already in use");
			err = -EEXIST;
			goto unlock;
		}

		err = __devlink_snapshot_id_insert(devlink, snapshot_id);
		if (err)
			goto unlock;
	} else {
		err = __devlink_region_snapshot_id_get(devlink, &snapshot_id);
		if (err) {
			NL_SET_ERR_MSG(info->extack, "Failed to allocate a new snapshot id");
			goto unlock;
		}
	}

	if (port)
		err = region->port_ops->snapshot(port, region->port_ops,
						 info->extack, &data);
	else
		err = region->ops->snapshot(devlink, region->ops,
					    info->extack, &data);
	if (err)
		goto err_snapshot_capture;

	err = __devlink_region_snapshot_create(region, data, snapshot_id);
	if (err)
		goto err_snapshot_create;

	if (!snapshot_id_attr) {
		struct sk_buff *msg;

		snapshot = devlink_region_snapshot_get_by_id(region,
							     snapshot_id);
		if (WARN_ON(!snapshot)) {
			err = -EINVAL;
			goto unlock;
		}

		msg = devlink_nl_region_notify_build(region, snapshot,
						     DEVLINK_CMD_REGION_NEW,
						     info->snd_portid,
						     info->snd_seq);
		err = PTR_ERR_OR_ZERO(msg);
		if (err)
			goto err_notify;

		err = genlmsg_reply(msg, info);
		if (err)
			goto err_notify;
	}

	mutex_unlock(&region->snapshot_lock);
	return 0;

err_snapshot_create:
	region->ops->destructor(data);
err_snapshot_capture:
	__devlink_snapshot_id_decrement(devlink, snapshot_id);
	mutex_unlock(&region->snapshot_lock);
	return err;

err_notify:
	devlink_region_snapshot_del(region, snapshot);
unlock:
	mutex_unlock(&region->snapshot_lock);
	return err;
}

static int devlink_nl_cmd_region_read_chunk_fill(struct sk_buff *msg,
						 u8 *chunk, u32 chunk_size,
						 u64 addr)
{
	struct nlattr *chunk_attr;
	int err;

	chunk_attr = nla_nest_start_noflag(msg, DEVLINK_ATTR_REGION_CHUNK);
	if (!chunk_attr)
		return -EINVAL;

	err = nla_put(msg, DEVLINK_ATTR_REGION_CHUNK_DATA, chunk_size, chunk);
	if (err)
		goto nla_put_failure;

	err = nla_put_u64_64bit(msg, DEVLINK_ATTR_REGION_CHUNK_ADDR, addr,
				DEVLINK_ATTR_PAD);
	if (err)
		goto nla_put_failure;

	nla_nest_end(msg, chunk_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, chunk_attr);
	return err;
}

#define DEVLINK_REGION_READ_CHUNK_SIZE 256

typedef int devlink_chunk_fill_t(void *cb_priv, u8 *chunk, u32 chunk_size,
				 u64 curr_offset,
				 struct netlink_ext_ack *extack);

static int
devlink_nl_region_read_fill(struct sk_buff *skb, devlink_chunk_fill_t *cb,
			    void *cb_priv, u64 start_offset, u64 end_offset,
			    u64 *new_offset, struct netlink_ext_ack *extack)
{
	u64 curr_offset = start_offset;
	int err = 0;
	u8 *data;

	/* Allocate and re-use a single buffer */
	data = kmalloc(DEVLINK_REGION_READ_CHUNK_SIZE, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	*new_offset = start_offset;

	while (curr_offset < end_offset) {
		u32 data_size;

		data_size = min_t(u32, end_offset - curr_offset,
				  DEVLINK_REGION_READ_CHUNK_SIZE);

		err = cb(cb_priv, data, data_size, curr_offset, extack);
		if (err)
			break;

		err = devlink_nl_cmd_region_read_chunk_fill(skb, data, data_size, curr_offset);
		if (err)
			break;

		curr_offset += data_size;
	}
	*new_offset = curr_offset;

	kfree(data);

	return err;
}

static int
devlink_region_snapshot_fill(void *cb_priv, u8 *chunk, u32 chunk_size,
			     u64 curr_offset,
			     struct netlink_ext_ack __always_unused *extack)
{
	struct devlink_snapshot *snapshot = cb_priv;

	memcpy(chunk, &snapshot->data[curr_offset], chunk_size);

	return 0;
}

static int
devlink_region_port_direct_fill(void *cb_priv, u8 *chunk, u32 chunk_size,
				u64 curr_offset, struct netlink_ext_ack *extack)
{
	struct devlink_region *region = cb_priv;

	return region->port_ops->read(region->port, region->port_ops, extack,
				      curr_offset, chunk_size, chunk);
}

static int
devlink_region_direct_fill(void *cb_priv, u8 *chunk, u32 chunk_size,
			   u64 curr_offset, struct netlink_ext_ack *extack)
{
	struct devlink_region *region = cb_priv;

	return region->ops->read(region->devlink, region->ops, extack,
				 curr_offset, chunk_size, chunk);
}

static int devlink_nl_cmd_region_read_dumpit(struct sk_buff *skb,
					     struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct nlattr *chunks_attr, *region_attr, *snapshot_attr;
	u64 ret_offset, start_offset, end_offset = U64_MAX;
	struct nlattr **attrs = info->attrs;
	struct devlink_port *port = NULL;
	devlink_chunk_fill_t *region_cb;
	struct devlink_region *region;
	const char *region_name;
	struct devlink *devlink;
	unsigned int index;
	void *region_cb_priv;
	void *hdr;
	int err;

	start_offset = state->start_offset;

	devlink = devlink_get_from_attrs_lock(sock_net(cb->skb->sk), attrs);
	if (IS_ERR(devlink))
		return PTR_ERR(devlink);

	if (!attrs[DEVLINK_ATTR_REGION_NAME]) {
		NL_SET_ERR_MSG(cb->extack, "No region name provided");
		err = -EINVAL;
		goto out_unlock;
	}

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port) {
			err = -ENODEV;
			goto out_unlock;
		}
	}

	region_attr = attrs[DEVLINK_ATTR_REGION_NAME];
	region_name = nla_data(region_attr);

	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region) {
		NL_SET_ERR_MSG_ATTR(cb->extack, region_attr, "Requested region does not exist");
		err = -EINVAL;
		goto out_unlock;
	}

	snapshot_attr = attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID];
	if (!snapshot_attr) {
		if (!nla_get_flag(attrs[DEVLINK_ATTR_REGION_DIRECT])) {
			NL_SET_ERR_MSG(cb->extack, "No snapshot id provided");
			err = -EINVAL;
			goto out_unlock;
		}

		if (!region->ops->read) {
			NL_SET_ERR_MSG(cb->extack, "Requested region does not support direct read");
			err = -EOPNOTSUPP;
			goto out_unlock;
		}

		if (port)
			region_cb = &devlink_region_port_direct_fill;
		else
			region_cb = &devlink_region_direct_fill;
		region_cb_priv = region;
	} else {
		struct devlink_snapshot *snapshot;
		u32 snapshot_id;

		if (nla_get_flag(attrs[DEVLINK_ATTR_REGION_DIRECT])) {
			NL_SET_ERR_MSG_ATTR(cb->extack, snapshot_attr, "Direct region read does not use snapshot");
			err = -EINVAL;
			goto out_unlock;
		}

		snapshot_id = nla_get_u32(snapshot_attr);
		snapshot = devlink_region_snapshot_get_by_id(region, snapshot_id);
		if (!snapshot) {
			NL_SET_ERR_MSG_ATTR(cb->extack, snapshot_attr, "Requested snapshot does not exist");
			err = -EINVAL;
			goto out_unlock;
		}
		region_cb = &devlink_region_snapshot_fill;
		region_cb_priv = snapshot;
	}

	if (attrs[DEVLINK_ATTR_REGION_CHUNK_ADDR] &&
	    attrs[DEVLINK_ATTR_REGION_CHUNK_LEN]) {
		if (!start_offset)
			start_offset =
				nla_get_u64(attrs[DEVLINK_ATTR_REGION_CHUNK_ADDR]);

		end_offset = nla_get_u64(attrs[DEVLINK_ATTR_REGION_CHUNK_ADDR]);
		end_offset += nla_get_u64(attrs[DEVLINK_ATTR_REGION_CHUNK_LEN]);
	}

	if (end_offset > region->size)
		end_offset = region->size;

	/* return 0 if there is no further data to read */
	if (start_offset == end_offset) {
		err = 0;
		goto out_unlock;
	}

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &devlink_nl_family, NLM_F_ACK | NLM_F_MULTI,
			  DEVLINK_CMD_REGION_READ);
	if (!hdr) {
		err = -EMSGSIZE;
		goto out_unlock;
	}

	err = devlink_nl_put_handle(skb, devlink);
	if (err)
		goto nla_put_failure;

	if (region->port) {
		err = nla_put_u32(skb, DEVLINK_ATTR_PORT_INDEX,
				  region->port->index);
		if (err)
			goto nla_put_failure;
	}

	err = nla_put_string(skb, DEVLINK_ATTR_REGION_NAME, region_name);
	if (err)
		goto nla_put_failure;

	chunks_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_REGION_CHUNKS);
	if (!chunks_attr) {
		err = -EMSGSIZE;
		goto nla_put_failure;
	}

	err = devlink_nl_region_read_fill(skb, region_cb, region_cb_priv,
					  start_offset, end_offset, &ret_offset,
					  cb->extack);

	if (err && err != -EMSGSIZE)
		goto nla_put_failure;

	/* Check if there was any progress done to prevent infinite loop */
	if (ret_offset == start_offset) {
		err = -EINVAL;
		goto nla_put_failure;
	}

	state->start_offset = ret_offset;

	nla_nest_end(skb, chunks_attr);
	genlmsg_end(skb, hdr);
	devl_unlock(devlink);
	devlink_put(devlink);
	return skb->len;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
out_unlock:
	devl_unlock(devlink);
	devlink_put(devlink);
	return err;
}

struct devlink_stats {
	u64_stats_t rx_bytes;
	u64_stats_t rx_packets;
	struct u64_stats_sync syncp;
};

/**
 * struct devlink_trap_policer_item - Packet trap policer attributes.
 * @policer: Immutable packet trap policer attributes.
 * @rate: Rate in packets / sec.
 * @burst: Burst size in packets.
 * @list: trap_policer_list member.
 *
 * Describes packet trap policer attributes. Created by devlink during trap
 * policer registration.
 */
struct devlink_trap_policer_item {
	const struct devlink_trap_policer *policer;
	u64 rate;
	u64 burst;
	struct list_head list;
};

/**
 * struct devlink_trap_group_item - Packet trap group attributes.
 * @group: Immutable packet trap group attributes.
 * @policer_item: Associated policer item. Can be NULL.
 * @list: trap_group_list member.
 * @stats: Trap group statistics.
 *
 * Describes packet trap group attributes. Created by devlink during trap
 * group registration.
 */
struct devlink_trap_group_item {
	const struct devlink_trap_group *group;
	struct devlink_trap_policer_item *policer_item;
	struct list_head list;
	struct devlink_stats __percpu *stats;
};

/**
 * struct devlink_trap_item - Packet trap attributes.
 * @trap: Immutable packet trap attributes.
 * @group_item: Associated group item.
 * @list: trap_list member.
 * @action: Trap action.
 * @stats: Trap statistics.
 * @priv: Driver private information.
 *
 * Describes both mutable and immutable packet trap attributes. Created by
 * devlink during trap registration and used for all trap related operations.
 */
struct devlink_trap_item {
	const struct devlink_trap *trap;
	struct devlink_trap_group_item *group_item;
	struct list_head list;
	enum devlink_trap_action action;
	struct devlink_stats __percpu *stats;
	void *priv;
};

static struct devlink_trap_policer_item *
devlink_trap_policer_item_lookup(struct devlink *devlink, u32 id)
{
	struct devlink_trap_policer_item *policer_item;

	list_for_each_entry(policer_item, &devlink->trap_policer_list, list) {
		if (policer_item->policer->id == id)
			return policer_item;
	}

	return NULL;
}

static struct devlink_trap_item *
devlink_trap_item_lookup(struct devlink *devlink, const char *name)
{
	struct devlink_trap_item *trap_item;

	list_for_each_entry(trap_item, &devlink->trap_list, list) {
		if (!strcmp(trap_item->trap->name, name))
			return trap_item;
	}

	return NULL;
}

static struct devlink_trap_item *
devlink_trap_item_get_from_info(struct devlink *devlink,
				struct genl_info *info)
{
	struct nlattr *attr;

	if (!info->attrs[DEVLINK_ATTR_TRAP_NAME])
		return NULL;
	attr = info->attrs[DEVLINK_ATTR_TRAP_NAME];

	return devlink_trap_item_lookup(devlink, nla_data(attr));
}

static int
devlink_trap_action_get_from_info(struct genl_info *info,
				  enum devlink_trap_action *p_trap_action)
{
	u8 val;

	val = nla_get_u8(info->attrs[DEVLINK_ATTR_TRAP_ACTION]);
	switch (val) {
	case DEVLINK_TRAP_ACTION_DROP:
	case DEVLINK_TRAP_ACTION_TRAP:
	case DEVLINK_TRAP_ACTION_MIRROR:
		*p_trap_action = val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int devlink_trap_metadata_put(struct sk_buff *msg,
				     const struct devlink_trap *trap)
{
	struct nlattr *attr;

	attr = nla_nest_start(msg, DEVLINK_ATTR_TRAP_METADATA);
	if (!attr)
		return -EMSGSIZE;

	if ((trap->metadata_cap & DEVLINK_TRAP_METADATA_TYPE_F_IN_PORT) &&
	    nla_put_flag(msg, DEVLINK_ATTR_TRAP_METADATA_TYPE_IN_PORT))
		goto nla_put_failure;
	if ((trap->metadata_cap & DEVLINK_TRAP_METADATA_TYPE_F_FA_COOKIE) &&
	    nla_put_flag(msg, DEVLINK_ATTR_TRAP_METADATA_TYPE_FA_COOKIE))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static void devlink_trap_stats_read(struct devlink_stats __percpu *trap_stats,
				    struct devlink_stats *stats)
{
	int i;

	memset(stats, 0, sizeof(*stats));
	for_each_possible_cpu(i) {
		struct devlink_stats *cpu_stats;
		u64 rx_packets, rx_bytes;
		unsigned int start;

		cpu_stats = per_cpu_ptr(trap_stats, i);
		do {
			start = u64_stats_fetch_begin(&cpu_stats->syncp);
			rx_packets = u64_stats_read(&cpu_stats->rx_packets);
			rx_bytes = u64_stats_read(&cpu_stats->rx_bytes);
		} while (u64_stats_fetch_retry(&cpu_stats->syncp, start));

		u64_stats_add(&stats->rx_packets, rx_packets);
		u64_stats_add(&stats->rx_bytes, rx_bytes);
	}
}

static int
devlink_trap_group_stats_put(struct sk_buff *msg,
			     struct devlink_stats __percpu *trap_stats)
{
	struct devlink_stats stats;
	struct nlattr *attr;

	devlink_trap_stats_read(trap_stats, &stats);

	attr = nla_nest_start(msg, DEVLINK_ATTR_STATS);
	if (!attr)
		return -EMSGSIZE;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_PACKETS,
			      u64_stats_read(&stats.rx_packets),
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_BYTES,
			      u64_stats_read(&stats.rx_bytes),
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int devlink_trap_stats_put(struct sk_buff *msg, struct devlink *devlink,
				  const struct devlink_trap_item *trap_item)
{
	struct devlink_stats stats;
	struct nlattr *attr;
	u64 drops = 0;
	int err;

	if (devlink->ops->trap_drop_counter_get) {
		err = devlink->ops->trap_drop_counter_get(devlink,
							  trap_item->trap,
							  &drops);
		if (err)
			return err;
	}

	devlink_trap_stats_read(trap_item->stats, &stats);

	attr = nla_nest_start(msg, DEVLINK_ATTR_STATS);
	if (!attr)
		return -EMSGSIZE;

	if (devlink->ops->trap_drop_counter_get &&
	    nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_DROPPED, drops,
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_PACKETS,
			      u64_stats_read(&stats.rx_packets),
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_BYTES,
			      u64_stats_read(&stats.rx_bytes),
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int devlink_nl_trap_fill(struct sk_buff *msg, struct devlink *devlink,
				const struct devlink_trap_item *trap_item,
				enum devlink_command cmd, u32 portid, u32 seq,
				int flags)
{
	struct devlink_trap_group_item *group_item = trap_item->group_item;
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_string(msg, DEVLINK_ATTR_TRAP_GROUP_NAME,
			   group_item->group->name))
		goto nla_put_failure;

	if (nla_put_string(msg, DEVLINK_ATTR_TRAP_NAME, trap_item->trap->name))
		goto nla_put_failure;

	if (nla_put_u8(msg, DEVLINK_ATTR_TRAP_TYPE, trap_item->trap->type))
		goto nla_put_failure;

	if (trap_item->trap->generic &&
	    nla_put_flag(msg, DEVLINK_ATTR_TRAP_GENERIC))
		goto nla_put_failure;

	if (nla_put_u8(msg, DEVLINK_ATTR_TRAP_ACTION, trap_item->action))
		goto nla_put_failure;

	err = devlink_trap_metadata_put(msg, trap_item->trap);
	if (err)
		goto nla_put_failure;

	err = devlink_trap_stats_put(msg, devlink, trap_item);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_trap_get_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_trap_item *trap_item;
	struct sk_buff *msg;
	int err;

	if (list_empty(&devlink->trap_list))
		return -EOPNOTSUPP;

	trap_item = devlink_trap_item_get_from_info(devlink, info);
	if (!trap_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap");
		return -ENOENT;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_trap_fill(msg, devlink, trap_item,
				   DEVLINK_CMD_TRAP_NEW, info->snd_portid,
				   info->snd_seq, 0);
	if (err)
		goto err_trap_fill;

	return genlmsg_reply(msg, info);

err_trap_fill:
	nlmsg_free(msg);
	return err;
}

static int
devlink_nl_cmd_trap_get_dump_one(struct sk_buff *msg, struct devlink *devlink,
				 struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_trap_item *trap_item;
	int idx = 0;
	int err = 0;

	list_for_each_entry(trap_item, &devlink->trap_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_trap_fill(msg, devlink, trap_item,
					   DEVLINK_CMD_TRAP_NEW,
					   NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq,
					   NLM_F_MULTI);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

const struct devlink_cmd devl_cmd_trap_get = {
	.dump_one		= devlink_nl_cmd_trap_get_dump_one,
};

static int __devlink_trap_action_set(struct devlink *devlink,
				     struct devlink_trap_item *trap_item,
				     enum devlink_trap_action trap_action,
				     struct netlink_ext_ack *extack)
{
	int err;

	if (trap_item->action != trap_action &&
	    trap_item->trap->type != DEVLINK_TRAP_TYPE_DROP) {
		NL_SET_ERR_MSG(extack, "Cannot change action of non-drop traps. Skipping");
		return 0;
	}

	err = devlink->ops->trap_action_set(devlink, trap_item->trap,
					    trap_action, extack);
	if (err)
		return err;

	trap_item->action = trap_action;

	return 0;
}

static int devlink_trap_action_set(struct devlink *devlink,
				   struct devlink_trap_item *trap_item,
				   struct genl_info *info)
{
	enum devlink_trap_action trap_action;
	int err;

	if (!info->attrs[DEVLINK_ATTR_TRAP_ACTION])
		return 0;

	err = devlink_trap_action_get_from_info(info, &trap_action);
	if (err) {
		NL_SET_ERR_MSG(info->extack, "Invalid trap action");
		return -EINVAL;
	}

	return __devlink_trap_action_set(devlink, trap_item, trap_action,
					 info->extack);
}

static int devlink_nl_cmd_trap_set_doit(struct sk_buff *skb,
					struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_trap_item *trap_item;

	if (list_empty(&devlink->trap_list))
		return -EOPNOTSUPP;

	trap_item = devlink_trap_item_get_from_info(devlink, info);
	if (!trap_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap");
		return -ENOENT;
	}

	return devlink_trap_action_set(devlink, trap_item, info);
}

static struct devlink_trap_group_item *
devlink_trap_group_item_lookup(struct devlink *devlink, const char *name)
{
	struct devlink_trap_group_item *group_item;

	list_for_each_entry(group_item, &devlink->trap_group_list, list) {
		if (!strcmp(group_item->group->name, name))
			return group_item;
	}

	return NULL;
}

static struct devlink_trap_group_item *
devlink_trap_group_item_lookup_by_id(struct devlink *devlink, u16 id)
{
	struct devlink_trap_group_item *group_item;

	list_for_each_entry(group_item, &devlink->trap_group_list, list) {
		if (group_item->group->id == id)
			return group_item;
	}

	return NULL;
}

static struct devlink_trap_group_item *
devlink_trap_group_item_get_from_info(struct devlink *devlink,
				      struct genl_info *info)
{
	char *name;

	if (!info->attrs[DEVLINK_ATTR_TRAP_GROUP_NAME])
		return NULL;
	name = nla_data(info->attrs[DEVLINK_ATTR_TRAP_GROUP_NAME]);

	return devlink_trap_group_item_lookup(devlink, name);
}

static int
devlink_nl_trap_group_fill(struct sk_buff *msg, struct devlink *devlink,
			   const struct devlink_trap_group_item *group_item,
			   enum devlink_command cmd, u32 portid, u32 seq,
			   int flags)
{
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_string(msg, DEVLINK_ATTR_TRAP_GROUP_NAME,
			   group_item->group->name))
		goto nla_put_failure;

	if (group_item->group->generic &&
	    nla_put_flag(msg, DEVLINK_ATTR_TRAP_GENERIC))
		goto nla_put_failure;

	if (group_item->policer_item &&
	    nla_put_u32(msg, DEVLINK_ATTR_TRAP_POLICER_ID,
			group_item->policer_item->policer->id))
		goto nla_put_failure;

	err = devlink_trap_group_stats_put(msg, group_item->stats);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_trap_group_get_doit(struct sk_buff *skb,
					      struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_trap_group_item *group_item;
	struct sk_buff *msg;
	int err;

	if (list_empty(&devlink->trap_group_list))
		return -EOPNOTSUPP;

	group_item = devlink_trap_group_item_get_from_info(devlink, info);
	if (!group_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap group");
		return -ENOENT;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_trap_group_fill(msg, devlink, group_item,
					 DEVLINK_CMD_TRAP_GROUP_NEW,
					 info->snd_portid, info->snd_seq, 0);
	if (err)
		goto err_trap_group_fill;

	return genlmsg_reply(msg, info);

err_trap_group_fill:
	nlmsg_free(msg);
	return err;
}

static int
devlink_nl_cmd_trap_group_get_dump_one(struct sk_buff *msg,
				       struct devlink *devlink,
				       struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_trap_group_item *group_item;
	int idx = 0;
	int err = 0;


	list_for_each_entry(group_item, &devlink->trap_group_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_trap_group_fill(msg, devlink, group_item,
						 DEVLINK_CMD_TRAP_GROUP_NEW,
						 NETLINK_CB(cb->skb).portid,
						 cb->nlh->nlmsg_seq,
						 NLM_F_MULTI);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

const struct devlink_cmd devl_cmd_trap_group_get = {
	.dump_one		= devlink_nl_cmd_trap_group_get_dump_one,
};

static int
__devlink_trap_group_action_set(struct devlink *devlink,
				struct devlink_trap_group_item *group_item,
				enum devlink_trap_action trap_action,
				struct netlink_ext_ack *extack)
{
	const char *group_name = group_item->group->name;
	struct devlink_trap_item *trap_item;
	int err;

	if (devlink->ops->trap_group_action_set) {
		err = devlink->ops->trap_group_action_set(devlink, group_item->group,
							  trap_action, extack);
		if (err)
			return err;

		list_for_each_entry(trap_item, &devlink->trap_list, list) {
			if (strcmp(trap_item->group_item->group->name, group_name))
				continue;
			if (trap_item->action != trap_action &&
			    trap_item->trap->type != DEVLINK_TRAP_TYPE_DROP)
				continue;
			trap_item->action = trap_action;
		}

		return 0;
	}

	list_for_each_entry(trap_item, &devlink->trap_list, list) {
		if (strcmp(trap_item->group_item->group->name, group_name))
			continue;
		err = __devlink_trap_action_set(devlink, trap_item,
						trap_action, extack);
		if (err)
			return err;
	}

	return 0;
}

static int
devlink_trap_group_action_set(struct devlink *devlink,
			      struct devlink_trap_group_item *group_item,
			      struct genl_info *info, bool *p_modified)
{
	enum devlink_trap_action trap_action;
	int err;

	if (!info->attrs[DEVLINK_ATTR_TRAP_ACTION])
		return 0;

	err = devlink_trap_action_get_from_info(info, &trap_action);
	if (err) {
		NL_SET_ERR_MSG(info->extack, "Invalid trap action");
		return -EINVAL;
	}

	err = __devlink_trap_group_action_set(devlink, group_item, trap_action,
					      info->extack);
	if (err)
		return err;

	*p_modified = true;

	return 0;
}

static int devlink_trap_group_set(struct devlink *devlink,
				  struct devlink_trap_group_item *group_item,
				  struct genl_info *info)
{
	struct devlink_trap_policer_item *policer_item;
	struct netlink_ext_ack *extack = info->extack;
	const struct devlink_trap_policer *policer;
	struct nlattr **attrs = info->attrs;
	u32 policer_id;
	int err;

	if (!attrs[DEVLINK_ATTR_TRAP_POLICER_ID])
		return 0;

	if (!devlink->ops->trap_group_set)
		return -EOPNOTSUPP;

	policer_id = nla_get_u32(attrs[DEVLINK_ATTR_TRAP_POLICER_ID]);
	policer_item = devlink_trap_policer_item_lookup(devlink, policer_id);
	if (policer_id && !policer_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap policer");
		return -ENOENT;
	}
	policer = policer_item ? policer_item->policer : NULL;

	err = devlink->ops->trap_group_set(devlink, group_item->group, policer,
					   extack);
	if (err)
		return err;

	group_item->policer_item = policer_item;

	return 0;
}

static int devlink_nl_cmd_trap_group_set_doit(struct sk_buff *skb,
					      struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_trap_group_item *group_item;
	bool modified = false;
	int err;

	if (list_empty(&devlink->trap_group_list))
		return -EOPNOTSUPP;

	group_item = devlink_trap_group_item_get_from_info(devlink, info);
	if (!group_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap group");
		return -ENOENT;
	}

	err = devlink_trap_group_action_set(devlink, group_item, info,
					    &modified);
	if (err)
		return err;

	err = devlink_trap_group_set(devlink, group_item, info);
	if (err)
		goto err_trap_group_set;

	return 0;

err_trap_group_set:
	if (modified)
		NL_SET_ERR_MSG(extack, "Trap group set failed, but some changes were committed already");
	return err;
}

static struct devlink_trap_policer_item *
devlink_trap_policer_item_get_from_info(struct devlink *devlink,
					struct genl_info *info)
{
	u32 id;

	if (!info->attrs[DEVLINK_ATTR_TRAP_POLICER_ID])
		return NULL;
	id = nla_get_u32(info->attrs[DEVLINK_ATTR_TRAP_POLICER_ID]);

	return devlink_trap_policer_item_lookup(devlink, id);
}

static int
devlink_trap_policer_stats_put(struct sk_buff *msg, struct devlink *devlink,
			       const struct devlink_trap_policer *policer)
{
	struct nlattr *attr;
	u64 drops;
	int err;

	if (!devlink->ops->trap_policer_counter_get)
		return 0;

	err = devlink->ops->trap_policer_counter_get(devlink, policer, &drops);
	if (err)
		return err;

	attr = nla_nest_start(msg, DEVLINK_ATTR_STATS);
	if (!attr)
		return -EMSGSIZE;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_DROPPED, drops,
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int
devlink_nl_trap_policer_fill(struct sk_buff *msg, struct devlink *devlink,
			     const struct devlink_trap_policer_item *policer_item,
			     enum devlink_command cmd, u32 portid, u32 seq,
			     int flags)
{
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_u32(msg, DEVLINK_ATTR_TRAP_POLICER_ID,
			policer_item->policer->id))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_TRAP_POLICER_RATE,
			      policer_item->rate, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_TRAP_POLICER_BURST,
			      policer_item->burst, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	err = devlink_trap_policer_stats_put(msg, devlink,
					     policer_item->policer);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int devlink_nl_cmd_trap_policer_get_doit(struct sk_buff *skb,
						struct genl_info *info)
{
	struct devlink_trap_policer_item *policer_item;
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct sk_buff *msg;
	int err;

	if (list_empty(&devlink->trap_policer_list))
		return -EOPNOTSUPP;

	policer_item = devlink_trap_policer_item_get_from_info(devlink, info);
	if (!policer_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap policer");
		return -ENOENT;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_trap_policer_fill(msg, devlink, policer_item,
					   DEVLINK_CMD_TRAP_POLICER_NEW,
					   info->snd_portid, info->snd_seq, 0);
	if (err)
		goto err_trap_policer_fill;

	return genlmsg_reply(msg, info);

err_trap_policer_fill:
	nlmsg_free(msg);
	return err;
}

static int
devlink_nl_cmd_trap_policer_get_dump_one(struct sk_buff *msg,
					 struct devlink *devlink,
					 struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_trap_policer_item *policer_item;
	int idx = 0;
	int err = 0;

	list_for_each_entry(policer_item, &devlink->trap_policer_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_trap_policer_fill(msg, devlink, policer_item,
						   DEVLINK_CMD_TRAP_POLICER_NEW,
						   NETLINK_CB(cb->skb).portid,
						   cb->nlh->nlmsg_seq,
						   NLM_F_MULTI);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

const struct devlink_cmd devl_cmd_trap_policer_get = {
	.dump_one		= devlink_nl_cmd_trap_policer_get_dump_one,
};

static int
devlink_trap_policer_set(struct devlink *devlink,
			 struct devlink_trap_policer_item *policer_item,
			 struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct nlattr **attrs = info->attrs;
	u64 rate, burst;
	int err;

	rate = policer_item->rate;
	burst = policer_item->burst;

	if (attrs[DEVLINK_ATTR_TRAP_POLICER_RATE])
		rate = nla_get_u64(attrs[DEVLINK_ATTR_TRAP_POLICER_RATE]);

	if (attrs[DEVLINK_ATTR_TRAP_POLICER_BURST])
		burst = nla_get_u64(attrs[DEVLINK_ATTR_TRAP_POLICER_BURST]);

	if (rate < policer_item->policer->min_rate) {
		NL_SET_ERR_MSG(extack, "Policer rate lower than limit");
		return -EINVAL;
	}

	if (rate > policer_item->policer->max_rate) {
		NL_SET_ERR_MSG(extack, "Policer rate higher than limit");
		return -EINVAL;
	}

	if (burst < policer_item->policer->min_burst) {
		NL_SET_ERR_MSG(extack, "Policer burst size lower than limit");
		return -EINVAL;
	}

	if (burst > policer_item->policer->max_burst) {
		NL_SET_ERR_MSG(extack, "Policer burst size higher than limit");
		return -EINVAL;
	}

	err = devlink->ops->trap_policer_set(devlink, policer_item->policer,
					     rate, burst, info->extack);
	if (err)
		return err;

	policer_item->rate = rate;
	policer_item->burst = burst;

	return 0;
}

static int devlink_nl_cmd_trap_policer_set_doit(struct sk_buff *skb,
						struct genl_info *info)
{
	struct devlink_trap_policer_item *policer_item;
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];

	if (list_empty(&devlink->trap_policer_list))
		return -EOPNOTSUPP;

	if (!devlink->ops->trap_policer_set)
		return -EOPNOTSUPP;

	policer_item = devlink_trap_policer_item_get_from_info(devlink, info);
	if (!policer_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap policer");
		return -ENOENT;
	}

	return devlink_trap_policer_set(devlink, policer_item, info);
}

const struct genl_small_ops devlink_nl_ops[56] = {
	{
		.cmd = DEVLINK_CMD_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_PORT_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_PORT_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_RATE_GET,
		.doit = devlink_nl_cmd_rate_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		.internal_flags = DEVLINK_NL_FLAG_NEED_RATE,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_RATE_SET,
		.doit = devlink_nl_cmd_rate_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_RATE,
	},
	{
		.cmd = DEVLINK_CMD_RATE_NEW,
		.doit = devlink_nl_cmd_rate_new_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_RATE_DEL,
		.doit = devlink_nl_cmd_rate_del_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_RATE_NODE,
	},
	{
		.cmd = DEVLINK_CMD_PORT_SPLIT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_split_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_PORT_UNSPLIT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_unsplit_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_PORT_NEW,
		.doit = devlink_nl_cmd_port_new_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_PORT_DEL,
		.doit = devlink_nl_cmd_port_del_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_LINECARD_GET,
		.doit = devlink_nl_cmd_linecard_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		.internal_flags = DEVLINK_NL_FLAG_NEED_LINECARD,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_LINECARD_SET,
		.doit = devlink_nl_cmd_linecard_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_LINECARD,
	},
	{
		.cmd = DEVLINK_CMD_SB_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_SB_POOL_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_pool_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_SB_POOL_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_pool_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_SB_PORT_POOL_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_port_pool_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_SB_PORT_POOL_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_port_pool_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_SB_TC_POOL_BIND_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_tc_pool_bind_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_SB_TC_POOL_BIND_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_tc_pool_bind_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_SB_OCC_SNAPSHOT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_occ_snapshot_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_SB_OCC_MAX_CLEAR,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_sb_occ_max_clear_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_ESWITCH_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_eswitch_get_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_ESWITCH_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_eswitch_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_DPIPE_TABLE_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_dpipe_table_get,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_DPIPE_ENTRIES_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_dpipe_entries_get,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_DPIPE_HEADERS_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_dpipe_headers_get,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_DPIPE_TABLE_COUNTERS_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_dpipe_table_counters_set,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_RESOURCE_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_resource_set,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_RESOURCE_DUMP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_resource_dump,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_RELOAD,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_reload,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_PARAM_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_param_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_PARAM_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_param_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_PORT_PARAM_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_param_get_doit,
		.dumpit = devlink_nl_cmd_port_param_get_dumpit,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_PORT_PARAM_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_port_param_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_PORT,
	},
	{
		.cmd = DEVLINK_CMD_REGION_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_region_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_REGION_NEW,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_region_new,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_REGION_DEL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_region_del,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_REGION_READ,
		.validate = GENL_DONT_VALIDATE_STRICT |
			    GENL_DONT_VALIDATE_DUMP_STRICT,
		.dumpit = devlink_nl_cmd_region_read_dumpit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_INFO_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_info_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_SET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_set_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_RECOVER,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_recover_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_DIAGNOSE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_diagnose_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET,
		.validate = GENL_DONT_VALIDATE_STRICT |
			    GENL_DONT_VALIDATE_DUMP_STRICT,
		.dumpit = devlink_nl_cmd_health_reporter_dump_get_dumpit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_DUMP_CLEAR,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_dump_clear_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT,
	},
	{
		.cmd = DEVLINK_CMD_HEALTH_REPORTER_TEST,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_health_reporter_test_doit,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = DEVLINK_NL_FLAG_NEED_DEVLINK_OR_PORT,
	},
	{
		.cmd = DEVLINK_CMD_FLASH_UPDATE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = devlink_nl_cmd_flash_update,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_TRAP_GET,
		.doit = devlink_nl_cmd_trap_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_TRAP_SET,
		.doit = devlink_nl_cmd_trap_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_TRAP_GROUP_GET,
		.doit = devlink_nl_cmd_trap_group_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_TRAP_GROUP_SET,
		.doit = devlink_nl_cmd_trap_group_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_TRAP_POLICER_GET,
		.doit = devlink_nl_cmd_trap_policer_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_TRAP_POLICER_SET,
		.doit = devlink_nl_cmd_trap_policer_set_doit,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = DEVLINK_CMD_SELFTESTS_GET,
		.doit = devlink_nl_cmd_selftests_get_doit,
		.dumpit = devlink_nl_instance_iter_dumpit,
		/* can be retrieved by unprivileged users */
	},
	{
		.cmd = DEVLINK_CMD_SELFTESTS_RUN,
		.doit = devlink_nl_cmd_selftests_run,
		.flags = GENL_ADMIN_PERM,
	},
	/* -- No new ops here! Use split ops going forward! -- */
};

static void
devlink_trap_policer_notify(struct devlink *devlink,
			    const struct devlink_trap_policer_item *policer_item,
			    enum devlink_command cmd);
static void
devlink_trap_group_notify(struct devlink *devlink,
			  const struct devlink_trap_group_item *group_item,
			  enum devlink_command cmd);
static void devlink_trap_notify(struct devlink *devlink,
				const struct devlink_trap_item *trap_item,
				enum devlink_command cmd);

void devlink_notify_register(struct devlink *devlink)
{
	struct devlink_trap_policer_item *policer_item;
	struct devlink_trap_group_item *group_item;
	struct devlink_param_item *param_item;
	struct devlink_trap_item *trap_item;
	struct devlink_port *devlink_port;
	struct devlink_linecard *linecard;
	struct devlink_rate *rate_node;
	struct devlink_region *region;
	unsigned long port_index;
	unsigned long param_id;

	devlink_notify(devlink, DEVLINK_CMD_NEW);
	list_for_each_entry(linecard, &devlink->linecard_list, list)
		devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);

	xa_for_each(&devlink->ports, port_index, devlink_port)
		devlink_port_notify(devlink_port, DEVLINK_CMD_PORT_NEW);

	list_for_each_entry(policer_item, &devlink->trap_policer_list, list)
		devlink_trap_policer_notify(devlink, policer_item,
					    DEVLINK_CMD_TRAP_POLICER_NEW);

	list_for_each_entry(group_item, &devlink->trap_group_list, list)
		devlink_trap_group_notify(devlink, group_item,
					  DEVLINK_CMD_TRAP_GROUP_NEW);

	list_for_each_entry(trap_item, &devlink->trap_list, list)
		devlink_trap_notify(devlink, trap_item, DEVLINK_CMD_TRAP_NEW);

	list_for_each_entry(rate_node, &devlink->rate_list, list)
		devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_NEW);

	list_for_each_entry(region, &devlink->region_list, list)
		devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_NEW);

	xa_for_each(&devlink->params, param_id, param_item)
		devlink_param_notify(devlink, 0, param_item,
				     DEVLINK_CMD_PARAM_NEW);
}

void devlink_notify_unregister(struct devlink *devlink)
{
	struct devlink_trap_policer_item *policer_item;
	struct devlink_trap_group_item *group_item;
	struct devlink_param_item *param_item;
	struct devlink_trap_item *trap_item;
	struct devlink_port *devlink_port;
	struct devlink_rate *rate_node;
	struct devlink_region *region;
	unsigned long port_index;
	unsigned long param_id;

	xa_for_each(&devlink->params, param_id, param_item)
		devlink_param_notify(devlink, 0, param_item,
				     DEVLINK_CMD_PARAM_DEL);

	list_for_each_entry_reverse(region, &devlink->region_list, list)
		devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_DEL);

	list_for_each_entry_reverse(rate_node, &devlink->rate_list, list)
		devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_DEL);

	list_for_each_entry_reverse(trap_item, &devlink->trap_list, list)
		devlink_trap_notify(devlink, trap_item, DEVLINK_CMD_TRAP_DEL);

	list_for_each_entry_reverse(group_item, &devlink->trap_group_list, list)
		devlink_trap_group_notify(devlink, group_item,
					  DEVLINK_CMD_TRAP_GROUP_DEL);
	list_for_each_entry_reverse(policer_item, &devlink->trap_policer_list,
				    list)
		devlink_trap_policer_notify(devlink, policer_item,
					    DEVLINK_CMD_TRAP_POLICER_DEL);

	xa_for_each(&devlink->ports, port_index, devlink_port)
		devlink_port_notify(devlink_port, DEVLINK_CMD_PORT_DEL);
	devlink_notify(devlink, DEVLINK_CMD_DEL);
}

static void devlink_port_type_warn(struct work_struct *work)
{
	WARN(true, "Type was not set for devlink port.");
}

static bool devlink_port_type_should_warn(struct devlink_port *devlink_port)
{
	/* Ignore CPU and DSA flavours. */
	return devlink_port->attrs.flavour != DEVLINK_PORT_FLAVOUR_CPU &&
	       devlink_port->attrs.flavour != DEVLINK_PORT_FLAVOUR_DSA &&
	       devlink_port->attrs.flavour != DEVLINK_PORT_FLAVOUR_UNUSED;
}

#define DEVLINK_PORT_TYPE_WARN_TIMEOUT (HZ * 3600)

static void devlink_port_type_warn_schedule(struct devlink_port *devlink_port)
{
	if (!devlink_port_type_should_warn(devlink_port))
		return;
	/* Schedule a work to WARN in case driver does not set port
	 * type within timeout.
	 */
	schedule_delayed_work(&devlink_port->type_warn_dw,
			      DEVLINK_PORT_TYPE_WARN_TIMEOUT);
}

static void devlink_port_type_warn_cancel(struct devlink_port *devlink_port)
{
	if (!devlink_port_type_should_warn(devlink_port))
		return;
	cancel_delayed_work_sync(&devlink_port->type_warn_dw);
}

/**
 * devlink_port_init() - Init devlink port
 *
 * @devlink: devlink
 * @devlink_port: devlink port
 *
 * Initialize essencial stuff that is needed for functions
 * that may be called before devlink port registration.
 * Call to this function is optional and not needed
 * in case the driver does not use such functions.
 */
void devlink_port_init(struct devlink *devlink,
		       struct devlink_port *devlink_port)
{
	if (devlink_port->initialized)
		return;
	devlink_port->devlink = devlink;
	INIT_LIST_HEAD(&devlink_port->region_list);
	devlink_port->initialized = true;
}
EXPORT_SYMBOL_GPL(devlink_port_init);

/**
 * devlink_port_fini() - Deinitialize devlink port
 *
 * @devlink_port: devlink port
 *
 * Deinitialize essencial stuff that is in use for functions
 * that may be called after devlink port unregistration.
 * Call to this function is optional and not needed
 * in case the driver does not use such functions.
 */
void devlink_port_fini(struct devlink_port *devlink_port)
{
	WARN_ON(!list_empty(&devlink_port->region_list));
}
EXPORT_SYMBOL_GPL(devlink_port_fini);

/**
 * devl_port_register() - Register devlink port
 *
 * @devlink: devlink
 * @devlink_port: devlink port
 * @port_index: driver-specific numerical identifier of the port
 *
 * Register devlink port with provided port index. User can use
 * any indexing, even hw-related one. devlink_port structure
 * is convenient to be embedded inside user driver private structure.
 * Note that the caller should take care of zeroing the devlink_port
 * structure.
 */
int devl_port_register(struct devlink *devlink,
		       struct devlink_port *devlink_port,
		       unsigned int port_index)
{
	int err;

	devl_assert_locked(devlink);

	ASSERT_DEVLINK_PORT_NOT_REGISTERED(devlink_port);

	devlink_port_init(devlink, devlink_port);
	devlink_port->registered = true;
	devlink_port->index = port_index;
	spin_lock_init(&devlink_port->type_lock);
	INIT_LIST_HEAD(&devlink_port->reporter_list);
	err = xa_insert(&devlink->ports, port_index, devlink_port, GFP_KERNEL);
	if (err)
		return err;

	INIT_DELAYED_WORK(&devlink_port->type_warn_dw, &devlink_port_type_warn);
	devlink_port_type_warn_schedule(devlink_port);
	devlink_port_notify(devlink_port, DEVLINK_CMD_PORT_NEW);
	return 0;
}
EXPORT_SYMBOL_GPL(devl_port_register);

/**
 *	devlink_port_register - Register devlink port
 *
 *	@devlink: devlink
 *	@devlink_port: devlink port
 *	@port_index: driver-specific numerical identifier of the port
 *
 *	Register devlink port with provided port index. User can use
 *	any indexing, even hw-related one. devlink_port structure
 *	is convenient to be embedded inside user driver private structure.
 *	Note that the caller should take care of zeroing the devlink_port
 *	structure.
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
int devlink_port_register(struct devlink *devlink,
			  struct devlink_port *devlink_port,
			  unsigned int port_index)
{
	int err;

	devl_lock(devlink);
	err = devl_port_register(devlink, devlink_port, port_index);
	devl_unlock(devlink);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_port_register);

/**
 * devl_port_unregister() - Unregister devlink port
 *
 * @devlink_port: devlink port
 */
void devl_port_unregister(struct devlink_port *devlink_port)
{
	lockdep_assert_held(&devlink_port->devlink->lock);
	WARN_ON(devlink_port->type != DEVLINK_PORT_TYPE_NOTSET);

	devlink_port_type_warn_cancel(devlink_port);
	devlink_port_notify(devlink_port, DEVLINK_CMD_PORT_DEL);
	xa_erase(&devlink_port->devlink->ports, devlink_port->index);
	WARN_ON(!list_empty(&devlink_port->reporter_list));
	devlink_port->registered = false;
}
EXPORT_SYMBOL_GPL(devl_port_unregister);

/**
 *	devlink_port_unregister - Unregister devlink port
 *
 *	@devlink_port: devlink port
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
void devlink_port_unregister(struct devlink_port *devlink_port)
{
	struct devlink *devlink = devlink_port->devlink;

	devl_lock(devlink);
	devl_port_unregister(devlink_port);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_port_unregister);

static void devlink_port_type_netdev_checks(struct devlink_port *devlink_port,
					    struct net_device *netdev)
{
	const struct net_device_ops *ops = netdev->netdev_ops;

	/* If driver registers devlink port, it should set devlink port
	 * attributes accordingly so the compat functions are called
	 * and the original ops are not used.
	 */
	if (ops->ndo_get_phys_port_name) {
		/* Some drivers use the same set of ndos for netdevs
		 * that have devlink_port registered and also for
		 * those who don't. Make sure that ndo_get_phys_port_name
		 * returns -EOPNOTSUPP here in case it is defined.
		 * Warn if not.
		 */
		char name[IFNAMSIZ];
		int err;

		err = ops->ndo_get_phys_port_name(netdev, name, sizeof(name));
		WARN_ON(err != -EOPNOTSUPP);
	}
	if (ops->ndo_get_port_parent_id) {
		/* Some drivers use the same set of ndos for netdevs
		 * that have devlink_port registered and also for
		 * those who don't. Make sure that ndo_get_port_parent_id
		 * returns -EOPNOTSUPP here in case it is defined.
		 * Warn if not.
		 */
		struct netdev_phys_item_id ppid;
		int err;

		err = ops->ndo_get_port_parent_id(netdev, &ppid);
		WARN_ON(err != -EOPNOTSUPP);
	}
}

static void __devlink_port_type_set(struct devlink_port *devlink_port,
				    enum devlink_port_type type,
				    void *type_dev)
{
	struct net_device *netdev = type_dev;

	ASSERT_DEVLINK_PORT_REGISTERED(devlink_port);

	if (type == DEVLINK_PORT_TYPE_NOTSET) {
		devlink_port_type_warn_schedule(devlink_port);
	} else {
		devlink_port_type_warn_cancel(devlink_port);
		if (type == DEVLINK_PORT_TYPE_ETH && netdev)
			devlink_port_type_netdev_checks(devlink_port, netdev);
	}

	spin_lock_bh(&devlink_port->type_lock);
	devlink_port->type = type;
	switch (type) {
	case DEVLINK_PORT_TYPE_ETH:
		devlink_port->type_eth.netdev = netdev;
		if (netdev) {
			ASSERT_RTNL();
			devlink_port->type_eth.ifindex = netdev->ifindex;
			BUILD_BUG_ON(sizeof(devlink_port->type_eth.ifname) !=
				     sizeof(netdev->name));
			strcpy(devlink_port->type_eth.ifname, netdev->name);
		}
		break;
	case DEVLINK_PORT_TYPE_IB:
		devlink_port->type_ib.ibdev = type_dev;
		break;
	default:
		break;
	}
	spin_unlock_bh(&devlink_port->type_lock);
	devlink_port_notify(devlink_port, DEVLINK_CMD_PORT_NEW);
}

/**
 *	devlink_port_type_eth_set - Set port type to Ethernet
 *
 *	@devlink_port: devlink port
 *
 *	If driver is calling this, most likely it is doing something wrong.
 */
void devlink_port_type_eth_set(struct devlink_port *devlink_port)
{
	dev_warn(devlink_port->devlink->dev,
		 "devlink port type for port %d set to Ethernet without a software interface reference, device type not supported by the kernel?\n",
		 devlink_port->index);
	__devlink_port_type_set(devlink_port, DEVLINK_PORT_TYPE_ETH, NULL);
}
EXPORT_SYMBOL_GPL(devlink_port_type_eth_set);

/**
 *	devlink_port_type_ib_set - Set port type to InfiniBand
 *
 *	@devlink_port: devlink port
 *	@ibdev: related IB device
 */
void devlink_port_type_ib_set(struct devlink_port *devlink_port,
			      struct ib_device *ibdev)
{
	__devlink_port_type_set(devlink_port, DEVLINK_PORT_TYPE_IB, ibdev);
}
EXPORT_SYMBOL_GPL(devlink_port_type_ib_set);

/**
 *	devlink_port_type_clear - Clear port type
 *
 *	@devlink_port: devlink port
 *
 *	If driver is calling this for clearing Ethernet type, most likely
 *	it is doing something wrong.
 */
void devlink_port_type_clear(struct devlink_port *devlink_port)
{
	if (devlink_port->type == DEVLINK_PORT_TYPE_ETH)
		dev_warn(devlink_port->devlink->dev,
			 "devlink port type for port %d cleared without a software interface reference, device type not supported by the kernel?\n",
			 devlink_port->index);
	__devlink_port_type_set(devlink_port, DEVLINK_PORT_TYPE_NOTSET, NULL);
}
EXPORT_SYMBOL_GPL(devlink_port_type_clear);

int devlink_port_netdevice_event(struct notifier_block *nb,
				 unsigned long event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct devlink_port *devlink_port = netdev->devlink_port;
	struct devlink *devlink;

	if (!devlink_port)
		return NOTIFY_OK;
	devlink = devlink_port->devlink;

	switch (event) {
	case NETDEV_POST_INIT:
		/* Set the type but not netdev pointer. It is going to be set
		 * later on by NETDEV_REGISTER event. Happens once during
		 * netdevice register
		 */
		__devlink_port_type_set(devlink_port, DEVLINK_PORT_TYPE_ETH,
					NULL);
		break;
	case NETDEV_REGISTER:
	case NETDEV_CHANGENAME:
		if (devlink_net(devlink) != dev_net(netdev))
			return NOTIFY_OK;
		/* Set the netdev on top of previously set type. Note this
		 * event happens also during net namespace change so here
		 * we take into account netdev pointer appearing in this
		 * namespace.
		 */
		__devlink_port_type_set(devlink_port, devlink_port->type,
					netdev);
		break;
	case NETDEV_UNREGISTER:
		if (devlink_net(devlink) != dev_net(netdev))
			return NOTIFY_OK;
		/* Clear netdev pointer, but not the type. This event happens
		 * also during net namespace change so we need to clear
		 * pointer to netdev that is going to another net namespace.
		 */
		__devlink_port_type_set(devlink_port, devlink_port->type,
					NULL);
		break;
	case NETDEV_PRE_UNINIT:
		/* Clear the type and the netdev pointer. Happens one during
		 * netdevice unregister.
		 */
		__devlink_port_type_set(devlink_port, DEVLINK_PORT_TYPE_NOTSET,
					NULL);
		break;
	}

	return NOTIFY_OK;
}

static int __devlink_port_attrs_set(struct devlink_port *devlink_port,
				    enum devlink_port_flavour flavour)
{
	struct devlink_port_attrs *attrs = &devlink_port->attrs;

	devlink_port->attrs_set = true;
	attrs->flavour = flavour;
	if (attrs->switch_id.id_len) {
		devlink_port->switch_port = true;
		if (WARN_ON(attrs->switch_id.id_len > MAX_PHYS_ITEM_ID_LEN))
			attrs->switch_id.id_len = MAX_PHYS_ITEM_ID_LEN;
	} else {
		devlink_port->switch_port = false;
	}
	return 0;
}

/**
 *	devlink_port_attrs_set - Set port attributes
 *
 *	@devlink_port: devlink port
 *	@attrs: devlink port attrs
 */
void devlink_port_attrs_set(struct devlink_port *devlink_port,
			    struct devlink_port_attrs *attrs)
{
	int ret;

	ASSERT_DEVLINK_PORT_NOT_REGISTERED(devlink_port);

	devlink_port->attrs = *attrs;
	ret = __devlink_port_attrs_set(devlink_port, attrs->flavour);
	if (ret)
		return;
	WARN_ON(attrs->splittable && attrs->split);
}
EXPORT_SYMBOL_GPL(devlink_port_attrs_set);

/**
 *	devlink_port_attrs_pci_pf_set - Set PCI PF port attributes
 *
 *	@devlink_port: devlink port
 *	@controller: associated controller number for the devlink port instance
 *	@pf: associated PF for the devlink port instance
 *	@external: indicates if the port is for an external controller
 */
void devlink_port_attrs_pci_pf_set(struct devlink_port *devlink_port, u32 controller,
				   u16 pf, bool external)
{
	struct devlink_port_attrs *attrs = &devlink_port->attrs;
	int ret;

	ASSERT_DEVLINK_PORT_NOT_REGISTERED(devlink_port);

	ret = __devlink_port_attrs_set(devlink_port,
				       DEVLINK_PORT_FLAVOUR_PCI_PF);
	if (ret)
		return;
	attrs->pci_pf.controller = controller;
	attrs->pci_pf.pf = pf;
	attrs->pci_pf.external = external;
}
EXPORT_SYMBOL_GPL(devlink_port_attrs_pci_pf_set);

/**
 *	devlink_port_attrs_pci_vf_set - Set PCI VF port attributes
 *
 *	@devlink_port: devlink port
 *	@controller: associated controller number for the devlink port instance
 *	@pf: associated PF for the devlink port instance
 *	@vf: associated VF of a PF for the devlink port instance
 *	@external: indicates if the port is for an external controller
 */
void devlink_port_attrs_pci_vf_set(struct devlink_port *devlink_port, u32 controller,
				   u16 pf, u16 vf, bool external)
{
	struct devlink_port_attrs *attrs = &devlink_port->attrs;
	int ret;

	ASSERT_DEVLINK_PORT_NOT_REGISTERED(devlink_port);

	ret = __devlink_port_attrs_set(devlink_port,
				       DEVLINK_PORT_FLAVOUR_PCI_VF);
	if (ret)
		return;
	attrs->pci_vf.controller = controller;
	attrs->pci_vf.pf = pf;
	attrs->pci_vf.vf = vf;
	attrs->pci_vf.external = external;
}
EXPORT_SYMBOL_GPL(devlink_port_attrs_pci_vf_set);

/**
 *	devlink_port_attrs_pci_sf_set - Set PCI SF port attributes
 *
 *	@devlink_port: devlink port
 *	@controller: associated controller number for the devlink port instance
 *	@pf: associated PF for the devlink port instance
 *	@sf: associated SF of a PF for the devlink port instance
 *	@external: indicates if the port is for an external controller
 */
void devlink_port_attrs_pci_sf_set(struct devlink_port *devlink_port, u32 controller,
				   u16 pf, u32 sf, bool external)
{
	struct devlink_port_attrs *attrs = &devlink_port->attrs;
	int ret;

	ASSERT_DEVLINK_PORT_NOT_REGISTERED(devlink_port);

	ret = __devlink_port_attrs_set(devlink_port,
				       DEVLINK_PORT_FLAVOUR_PCI_SF);
	if (ret)
		return;
	attrs->pci_sf.controller = controller;
	attrs->pci_sf.pf = pf;
	attrs->pci_sf.sf = sf;
	attrs->pci_sf.external = external;
}
EXPORT_SYMBOL_GPL(devlink_port_attrs_pci_sf_set);

/**
 * devl_rate_node_create - create devlink rate node
 * @devlink: devlink instance
 * @priv: driver private data
 * @node_name: name of the resulting node
 * @parent: parent devlink_rate struct
 *
 * Create devlink rate object of type node
 */
struct devlink_rate *
devl_rate_node_create(struct devlink *devlink, void *priv, char *node_name,
		      struct devlink_rate *parent)
{
	struct devlink_rate *rate_node;

	rate_node = devlink_rate_node_get_by_name(devlink, node_name);
	if (!IS_ERR(rate_node))
		return ERR_PTR(-EEXIST);

	rate_node = kzalloc(sizeof(*rate_node), GFP_KERNEL);
	if (!rate_node)
		return ERR_PTR(-ENOMEM);

	if (parent) {
		rate_node->parent = parent;
		refcount_inc(&rate_node->parent->refcnt);
	}

	rate_node->type = DEVLINK_RATE_TYPE_NODE;
	rate_node->devlink = devlink;
	rate_node->priv = priv;

	rate_node->name = kstrdup(node_name, GFP_KERNEL);
	if (!rate_node->name) {
		kfree(rate_node);
		return ERR_PTR(-ENOMEM);
	}

	refcount_set(&rate_node->refcnt, 1);
	list_add(&rate_node->list, &devlink->rate_list);
	devlink_rate_notify(rate_node, DEVLINK_CMD_RATE_NEW);
	return rate_node;
}
EXPORT_SYMBOL_GPL(devl_rate_node_create);

/**
 * devl_rate_leaf_create - create devlink rate leaf
 * @devlink_port: devlink port object to create rate object on
 * @priv: driver private data
 * @parent: parent devlink_rate struct
 *
 * Create devlink rate object of type leaf on provided @devlink_port.
 */
int devl_rate_leaf_create(struct devlink_port *devlink_port, void *priv,
			  struct devlink_rate *parent)
{
	struct devlink *devlink = devlink_port->devlink;
	struct devlink_rate *devlink_rate;

	devl_assert_locked(devlink_port->devlink);

	if (WARN_ON(devlink_port->devlink_rate))
		return -EBUSY;

	devlink_rate = kzalloc(sizeof(*devlink_rate), GFP_KERNEL);
	if (!devlink_rate)
		return -ENOMEM;

	if (parent) {
		devlink_rate->parent = parent;
		refcount_inc(&devlink_rate->parent->refcnt);
	}

	devlink_rate->type = DEVLINK_RATE_TYPE_LEAF;
	devlink_rate->devlink = devlink;
	devlink_rate->devlink_port = devlink_port;
	devlink_rate->priv = priv;
	list_add_tail(&devlink_rate->list, &devlink->rate_list);
	devlink_port->devlink_rate = devlink_rate;
	devlink_rate_notify(devlink_rate, DEVLINK_CMD_RATE_NEW);

	return 0;
}
EXPORT_SYMBOL_GPL(devl_rate_leaf_create);

/**
 * devl_rate_leaf_destroy - destroy devlink rate leaf
 *
 * @devlink_port: devlink port linked to the rate object
 *
 * Destroy the devlink rate object of type leaf on provided @devlink_port.
 */
void devl_rate_leaf_destroy(struct devlink_port *devlink_port)
{
	struct devlink_rate *devlink_rate = devlink_port->devlink_rate;

	devl_assert_locked(devlink_port->devlink);
	if (!devlink_rate)
		return;

	devlink_rate_notify(devlink_rate, DEVLINK_CMD_RATE_DEL);
	if (devlink_rate->parent)
		refcount_dec(&devlink_rate->parent->refcnt);
	list_del(&devlink_rate->list);
	devlink_port->devlink_rate = NULL;
	kfree(devlink_rate);
}
EXPORT_SYMBOL_GPL(devl_rate_leaf_destroy);

/**
 * devl_rate_nodes_destroy - destroy all devlink rate nodes on device
 * @devlink: devlink instance
 *
 * Unset parent for all rate objects and destroy all rate nodes
 * on specified device.
 */
void devl_rate_nodes_destroy(struct devlink *devlink)
{
	static struct devlink_rate *devlink_rate, *tmp;
	const struct devlink_ops *ops = devlink->ops;

	devl_assert_locked(devlink);

	list_for_each_entry(devlink_rate, &devlink->rate_list, list) {
		if (!devlink_rate->parent)
			continue;

		refcount_dec(&devlink_rate->parent->refcnt);
		if (devlink_rate_is_leaf(devlink_rate))
			ops->rate_leaf_parent_set(devlink_rate, NULL, devlink_rate->priv,
						  NULL, NULL);
		else if (devlink_rate_is_node(devlink_rate))
			ops->rate_node_parent_set(devlink_rate, NULL, devlink_rate->priv,
						  NULL, NULL);
	}
	list_for_each_entry_safe(devlink_rate, tmp, &devlink->rate_list, list) {
		if (devlink_rate_is_node(devlink_rate)) {
			ops->rate_node_del(devlink_rate, devlink_rate->priv, NULL);
			list_del(&devlink_rate->list);
			kfree(devlink_rate->name);
			kfree(devlink_rate);
		}
	}
}
EXPORT_SYMBOL_GPL(devl_rate_nodes_destroy);

/**
 *	devlink_port_linecard_set - Link port with a linecard
 *
 *	@devlink_port: devlink port
 *	@linecard: devlink linecard
 */
void devlink_port_linecard_set(struct devlink_port *devlink_port,
			       struct devlink_linecard *linecard)
{
	ASSERT_DEVLINK_PORT_NOT_REGISTERED(devlink_port);

	devlink_port->linecard = linecard;
}
EXPORT_SYMBOL_GPL(devlink_port_linecard_set);

static int __devlink_port_phys_port_name_get(struct devlink_port *devlink_port,
					     char *name, size_t len)
{
	struct devlink_port_attrs *attrs = &devlink_port->attrs;
	int n = 0;

	if (!devlink_port->attrs_set)
		return -EOPNOTSUPP;

	switch (attrs->flavour) {
	case DEVLINK_PORT_FLAVOUR_PHYSICAL:
		if (devlink_port->linecard)
			n = snprintf(name, len, "l%u",
				     devlink_port->linecard->index);
		if (n < len)
			n += snprintf(name + n, len - n, "p%u",
				      attrs->phys.port_number);
		if (n < len && attrs->split)
			n += snprintf(name + n, len - n, "s%u",
				      attrs->phys.split_subport_number);
		break;
	case DEVLINK_PORT_FLAVOUR_CPU:
	case DEVLINK_PORT_FLAVOUR_DSA:
	case DEVLINK_PORT_FLAVOUR_UNUSED:
		/* As CPU and DSA ports do not have a netdevice associated
		 * case should not ever happen.
		 */
		WARN_ON(1);
		return -EINVAL;
	case DEVLINK_PORT_FLAVOUR_PCI_PF:
		if (attrs->pci_pf.external) {
			n = snprintf(name, len, "c%u", attrs->pci_pf.controller);
			if (n >= len)
				return -EINVAL;
			len -= n;
			name += n;
		}
		n = snprintf(name, len, "pf%u", attrs->pci_pf.pf);
		break;
	case DEVLINK_PORT_FLAVOUR_PCI_VF:
		if (attrs->pci_vf.external) {
			n = snprintf(name, len, "c%u", attrs->pci_vf.controller);
			if (n >= len)
				return -EINVAL;
			len -= n;
			name += n;
		}
		n = snprintf(name, len, "pf%uvf%u",
			     attrs->pci_vf.pf, attrs->pci_vf.vf);
		break;
	case DEVLINK_PORT_FLAVOUR_PCI_SF:
		if (attrs->pci_sf.external) {
			n = snprintf(name, len, "c%u", attrs->pci_sf.controller);
			if (n >= len)
				return -EINVAL;
			len -= n;
			name += n;
		}
		n = snprintf(name, len, "pf%usf%u", attrs->pci_sf.pf,
			     attrs->pci_sf.sf);
		break;
	case DEVLINK_PORT_FLAVOUR_VIRTUAL:
		return -EOPNOTSUPP;
	}

	if (n >= len)
		return -EINVAL;

	return 0;
}

static int devlink_linecard_types_init(struct devlink_linecard *linecard)
{
	struct devlink_linecard_type *linecard_type;
	unsigned int count;
	int i;

	count = linecard->ops->types_count(linecard, linecard->priv);
	linecard->types = kmalloc_array(count, sizeof(*linecard_type),
					GFP_KERNEL);
	if (!linecard->types)
		return -ENOMEM;
	linecard->types_count = count;

	for (i = 0; i < count; i++) {
		linecard_type = &linecard->types[i];
		linecard->ops->types_get(linecard, linecard->priv, i,
					 &linecard_type->type,
					 &linecard_type->priv);
	}
	return 0;
}

static void devlink_linecard_types_fini(struct devlink_linecard *linecard)
{
	kfree(linecard->types);
}

/**
 *	devl_linecard_create - Create devlink linecard
 *
 *	@devlink: devlink
 *	@linecard_index: driver-specific numerical identifier of the linecard
 *	@ops: linecards ops
 *	@priv: user priv pointer
 *
 *	Create devlink linecard instance with provided linecard index.
 *	Caller can use any indexing, even hw-related one.
 *
 *	Return: Line card structure or an ERR_PTR() encoded error code.
 */
struct devlink_linecard *
devl_linecard_create(struct devlink *devlink, unsigned int linecard_index,
		     const struct devlink_linecard_ops *ops, void *priv)
{
	struct devlink_linecard *linecard;
	int err;

	if (WARN_ON(!ops || !ops->provision || !ops->unprovision ||
		    !ops->types_count || !ops->types_get))
		return ERR_PTR(-EINVAL);

	if (devlink_linecard_index_exists(devlink, linecard_index))
		return ERR_PTR(-EEXIST);

	linecard = kzalloc(sizeof(*linecard), GFP_KERNEL);
	if (!linecard)
		return ERR_PTR(-ENOMEM);

	linecard->devlink = devlink;
	linecard->index = linecard_index;
	linecard->ops = ops;
	linecard->priv = priv;
	linecard->state = DEVLINK_LINECARD_STATE_UNPROVISIONED;
	mutex_init(&linecard->state_lock);

	err = devlink_linecard_types_init(linecard);
	if (err) {
		mutex_destroy(&linecard->state_lock);
		kfree(linecard);
		return ERR_PTR(err);
	}

	list_add_tail(&linecard->list, &devlink->linecard_list);
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	return linecard;
}
EXPORT_SYMBOL_GPL(devl_linecard_create);

/**
 *	devl_linecard_destroy - Destroy devlink linecard
 *
 *	@linecard: devlink linecard
 */
void devl_linecard_destroy(struct devlink_linecard *linecard)
{
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_DEL);
	list_del(&linecard->list);
	devlink_linecard_types_fini(linecard);
	mutex_destroy(&linecard->state_lock);
	kfree(linecard);
}
EXPORT_SYMBOL_GPL(devl_linecard_destroy);

/**
 *	devlink_linecard_provision_set - Set provisioning on linecard
 *
 *	@linecard: devlink linecard
 *	@type: linecard type
 *
 *	This is either called directly from the provision() op call or
 *	as a result of the provision() op call asynchronously.
 */
void devlink_linecard_provision_set(struct devlink_linecard *linecard,
				    const char *type)
{
	mutex_lock(&linecard->state_lock);
	WARN_ON(linecard->type && strcmp(linecard->type, type));
	linecard->state = DEVLINK_LINECARD_STATE_PROVISIONED;
	linecard->type = type;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
}
EXPORT_SYMBOL_GPL(devlink_linecard_provision_set);

/**
 *	devlink_linecard_provision_clear - Clear provisioning on linecard
 *
 *	@linecard: devlink linecard
 *
 *	This is either called directly from the unprovision() op call or
 *	as a result of the unprovision() op call asynchronously.
 */
void devlink_linecard_provision_clear(struct devlink_linecard *linecard)
{
	mutex_lock(&linecard->state_lock);
	WARN_ON(linecard->nested_devlink);
	linecard->state = DEVLINK_LINECARD_STATE_UNPROVISIONED;
	linecard->type = NULL;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
}
EXPORT_SYMBOL_GPL(devlink_linecard_provision_clear);

/**
 *	devlink_linecard_provision_fail - Fail provisioning on linecard
 *
 *	@linecard: devlink linecard
 *
 *	This is either called directly from the provision() op call or
 *	as a result of the provision() op call asynchronously.
 */
void devlink_linecard_provision_fail(struct devlink_linecard *linecard)
{
	mutex_lock(&linecard->state_lock);
	WARN_ON(linecard->nested_devlink);
	linecard->state = DEVLINK_LINECARD_STATE_PROVISIONING_FAILED;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
}
EXPORT_SYMBOL_GPL(devlink_linecard_provision_fail);

/**
 *	devlink_linecard_activate - Set linecard active
 *
 *	@linecard: devlink linecard
 */
void devlink_linecard_activate(struct devlink_linecard *linecard)
{
	mutex_lock(&linecard->state_lock);
	WARN_ON(linecard->state != DEVLINK_LINECARD_STATE_PROVISIONED);
	linecard->state = DEVLINK_LINECARD_STATE_ACTIVE;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
}
EXPORT_SYMBOL_GPL(devlink_linecard_activate);

/**
 *	devlink_linecard_deactivate - Set linecard inactive
 *
 *	@linecard: devlink linecard
 */
void devlink_linecard_deactivate(struct devlink_linecard *linecard)
{
	mutex_lock(&linecard->state_lock);
	switch (linecard->state) {
	case DEVLINK_LINECARD_STATE_ACTIVE:
		linecard->state = DEVLINK_LINECARD_STATE_PROVISIONED;
		devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
		break;
	case DEVLINK_LINECARD_STATE_UNPROVISIONING:
		/* Line card is being deactivated as part
		 * of unprovisioning flow.
		 */
		break;
	default:
		WARN_ON(1);
		break;
	}
	mutex_unlock(&linecard->state_lock);
}
EXPORT_SYMBOL_GPL(devlink_linecard_deactivate);

/**
 *	devlink_linecard_nested_dl_set - Attach/detach nested devlink
 *					 instance to linecard.
 *
 *	@linecard: devlink linecard
 *	@nested_devlink: devlink instance to attach or NULL to detach
 */
void devlink_linecard_nested_dl_set(struct devlink_linecard *linecard,
				    struct devlink *nested_devlink)
{
	mutex_lock(&linecard->state_lock);
	linecard->nested_devlink = nested_devlink;
	devlink_linecard_notify(linecard, DEVLINK_CMD_LINECARD_NEW);
	mutex_unlock(&linecard->state_lock);
}
EXPORT_SYMBOL_GPL(devlink_linecard_nested_dl_set);

int devl_sb_register(struct devlink *devlink, unsigned int sb_index,
		     u32 size, u16 ingress_pools_count,
		     u16 egress_pools_count, u16 ingress_tc_count,
		     u16 egress_tc_count)
{
	struct devlink_sb *devlink_sb;

	lockdep_assert_held(&devlink->lock);

	if (devlink_sb_index_exists(devlink, sb_index))
		return -EEXIST;

	devlink_sb = kzalloc(sizeof(*devlink_sb), GFP_KERNEL);
	if (!devlink_sb)
		return -ENOMEM;
	devlink_sb->index = sb_index;
	devlink_sb->size = size;
	devlink_sb->ingress_pools_count = ingress_pools_count;
	devlink_sb->egress_pools_count = egress_pools_count;
	devlink_sb->ingress_tc_count = ingress_tc_count;
	devlink_sb->egress_tc_count = egress_tc_count;
	list_add_tail(&devlink_sb->list, &devlink->sb_list);
	return 0;
}
EXPORT_SYMBOL_GPL(devl_sb_register);

int devlink_sb_register(struct devlink *devlink, unsigned int sb_index,
			u32 size, u16 ingress_pools_count,
			u16 egress_pools_count, u16 ingress_tc_count,
			u16 egress_tc_count)
{
	int err;

	devl_lock(devlink);
	err = devl_sb_register(devlink, sb_index, size, ingress_pools_count,
			       egress_pools_count, ingress_tc_count,
			       egress_tc_count);
	devl_unlock(devlink);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_sb_register);

void devl_sb_unregister(struct devlink *devlink, unsigned int sb_index)
{
	struct devlink_sb *devlink_sb;

	lockdep_assert_held(&devlink->lock);

	devlink_sb = devlink_sb_get_by_index(devlink, sb_index);
	WARN_ON(!devlink_sb);
	list_del(&devlink_sb->list);
	kfree(devlink_sb);
}
EXPORT_SYMBOL_GPL(devl_sb_unregister);

void devlink_sb_unregister(struct devlink *devlink, unsigned int sb_index)
{
	devl_lock(devlink);
	devl_sb_unregister(devlink, sb_index);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_sb_unregister);

/**
 * devl_dpipe_headers_register - register dpipe headers
 *
 * @devlink: devlink
 * @dpipe_headers: dpipe header array
 *
 * Register the headers supported by hardware.
 */
void devl_dpipe_headers_register(struct devlink *devlink,
				 struct devlink_dpipe_headers *dpipe_headers)
{
	lockdep_assert_held(&devlink->lock);

	devlink->dpipe_headers = dpipe_headers;
}
EXPORT_SYMBOL_GPL(devl_dpipe_headers_register);

/**
 * devl_dpipe_headers_unregister - unregister dpipe headers
 *
 * @devlink: devlink
 *
 * Unregister the headers supported by hardware.
 */
void devl_dpipe_headers_unregister(struct devlink *devlink)
{
	lockdep_assert_held(&devlink->lock);

	devlink->dpipe_headers = NULL;
}
EXPORT_SYMBOL_GPL(devl_dpipe_headers_unregister);

/**
 *	devlink_dpipe_table_counter_enabled - check if counter allocation
 *					      required
 *	@devlink: devlink
 *	@table_name: tables name
 *
 *	Used by driver to check if counter allocation is required.
 *	After counter allocation is turned on the table entries
 *	are updated to include counter statistics.
 *
 *	After that point on the driver must respect the counter
 *	state so that each entry added to the table is added
 *	with a counter.
 */
bool devlink_dpipe_table_counter_enabled(struct devlink *devlink,
					 const char *table_name)
{
	struct devlink_dpipe_table *table;
	bool enabled;

	rcu_read_lock();
	table = devlink_dpipe_table_find(&devlink->dpipe_table_list,
					 table_name, devlink);
	enabled = false;
	if (table)
		enabled = table->counters_enabled;
	rcu_read_unlock();
	return enabled;
}
EXPORT_SYMBOL_GPL(devlink_dpipe_table_counter_enabled);

/**
 * devl_dpipe_table_register - register dpipe table
 *
 * @devlink: devlink
 * @table_name: table name
 * @table_ops: table ops
 * @priv: priv
 * @counter_control_extern: external control for counters
 */
int devl_dpipe_table_register(struct devlink *devlink,
			      const char *table_name,
			      struct devlink_dpipe_table_ops *table_ops,
			      void *priv, bool counter_control_extern)
{
	struct devlink_dpipe_table *table;

	lockdep_assert_held(&devlink->lock);

	if (WARN_ON(!table_ops->size_get))
		return -EINVAL;

	if (devlink_dpipe_table_find(&devlink->dpipe_table_list, table_name,
				     devlink))
		return -EEXIST;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	table->name = table_name;
	table->table_ops = table_ops;
	table->priv = priv;
	table->counter_control_extern = counter_control_extern;

	list_add_tail_rcu(&table->list, &devlink->dpipe_table_list);

	return 0;
}
EXPORT_SYMBOL_GPL(devl_dpipe_table_register);

/**
 * devl_dpipe_table_unregister - unregister dpipe table
 *
 * @devlink: devlink
 * @table_name: table name
 */
void devl_dpipe_table_unregister(struct devlink *devlink,
				 const char *table_name)
{
	struct devlink_dpipe_table *table;

	lockdep_assert_held(&devlink->lock);

	table = devlink_dpipe_table_find(&devlink->dpipe_table_list,
					 table_name, devlink);
	if (!table)
		return;
	list_del_rcu(&table->list);
	kfree_rcu(table, rcu);
}
EXPORT_SYMBOL_GPL(devl_dpipe_table_unregister);

/**
 * devl_resource_register - devlink resource register
 *
 * @devlink: devlink
 * @resource_name: resource's name
 * @resource_size: resource's size
 * @resource_id: resource's id
 * @parent_resource_id: resource's parent id
 * @size_params: size parameters
 *
 * Generic resources should reuse the same names across drivers.
 * Please see the generic resources list at:
 * Documentation/networking/devlink/devlink-resource.rst
 */
int devl_resource_register(struct devlink *devlink,
			   const char *resource_name,
			   u64 resource_size,
			   u64 resource_id,
			   u64 parent_resource_id,
			   const struct devlink_resource_size_params *size_params)
{
	struct devlink_resource *resource;
	struct list_head *resource_list;
	bool top_hierarchy;

	lockdep_assert_held(&devlink->lock);

	top_hierarchy = parent_resource_id == DEVLINK_RESOURCE_ID_PARENT_TOP;

	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (resource)
		return -EINVAL;

	resource = kzalloc(sizeof(*resource), GFP_KERNEL);
	if (!resource)
		return -ENOMEM;

	if (top_hierarchy) {
		resource_list = &devlink->resource_list;
	} else {
		struct devlink_resource *parent_resource;

		parent_resource = devlink_resource_find(devlink, NULL,
							parent_resource_id);
		if (parent_resource) {
			resource_list = &parent_resource->resource_list;
			resource->parent = parent_resource;
		} else {
			kfree(resource);
			return -EINVAL;
		}
	}

	resource->name = resource_name;
	resource->size = resource_size;
	resource->size_new = resource_size;
	resource->id = resource_id;
	resource->size_valid = true;
	memcpy(&resource->size_params, size_params,
	       sizeof(resource->size_params));
	INIT_LIST_HEAD(&resource->resource_list);
	list_add_tail(&resource->list, resource_list);

	return 0;
}
EXPORT_SYMBOL_GPL(devl_resource_register);

/**
 *	devlink_resource_register - devlink resource register
 *
 *	@devlink: devlink
 *	@resource_name: resource's name
 *	@resource_size: resource's size
 *	@resource_id: resource's id
 *	@parent_resource_id: resource's parent id
 *	@size_params: size parameters
 *
 *	Generic resources should reuse the same names across drivers.
 *	Please see the generic resources list at:
 *	Documentation/networking/devlink/devlink-resource.rst
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
int devlink_resource_register(struct devlink *devlink,
			      const char *resource_name,
			      u64 resource_size,
			      u64 resource_id,
			      u64 parent_resource_id,
			      const struct devlink_resource_size_params *size_params)
{
	int err;

	devl_lock(devlink);
	err = devl_resource_register(devlink, resource_name, resource_size,
				     resource_id, parent_resource_id, size_params);
	devl_unlock(devlink);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_resource_register);

static void devlink_resource_unregister(struct devlink *devlink,
					struct devlink_resource *resource)
{
	struct devlink_resource *tmp, *child_resource;

	list_for_each_entry_safe(child_resource, tmp, &resource->resource_list,
				 list) {
		devlink_resource_unregister(devlink, child_resource);
		list_del(&child_resource->list);
		kfree(child_resource);
	}
}

/**
 * devl_resources_unregister - free all resources
 *
 * @devlink: devlink
 */
void devl_resources_unregister(struct devlink *devlink)
{
	struct devlink_resource *tmp, *child_resource;

	lockdep_assert_held(&devlink->lock);

	list_for_each_entry_safe(child_resource, tmp, &devlink->resource_list,
				 list) {
		devlink_resource_unregister(devlink, child_resource);
		list_del(&child_resource->list);
		kfree(child_resource);
	}
}
EXPORT_SYMBOL_GPL(devl_resources_unregister);

/**
 *	devlink_resources_unregister - free all resources
 *
 *	@devlink: devlink
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
void devlink_resources_unregister(struct devlink *devlink)
{
	devl_lock(devlink);
	devl_resources_unregister(devlink);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_resources_unregister);

/**
 * devl_resource_size_get - get and update size
 *
 * @devlink: devlink
 * @resource_id: the requested resource id
 * @p_resource_size: ptr to update
 */
int devl_resource_size_get(struct devlink *devlink,
			   u64 resource_id,
			   u64 *p_resource_size)
{
	struct devlink_resource *resource;

	lockdep_assert_held(&devlink->lock);

	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (!resource)
		return -EINVAL;
	*p_resource_size = resource->size_new;
	resource->size = resource->size_new;
	return 0;
}
EXPORT_SYMBOL_GPL(devl_resource_size_get);

/**
 * devl_dpipe_table_resource_set - set the resource id
 *
 * @devlink: devlink
 * @table_name: table name
 * @resource_id: resource id
 * @resource_units: number of resource's units consumed per table's entry
 */
int devl_dpipe_table_resource_set(struct devlink *devlink,
				  const char *table_name, u64 resource_id,
				  u64 resource_units)
{
	struct devlink_dpipe_table *table;

	table = devlink_dpipe_table_find(&devlink->dpipe_table_list,
					 table_name, devlink);
	if (!table)
		return -EINVAL;

	table->resource_id = resource_id;
	table->resource_units = resource_units;
	table->resource_valid = true;
	return 0;
}
EXPORT_SYMBOL_GPL(devl_dpipe_table_resource_set);

/**
 * devl_resource_occ_get_register - register occupancy getter
 *
 * @devlink: devlink
 * @resource_id: resource id
 * @occ_get: occupancy getter callback
 * @occ_get_priv: occupancy getter callback priv
 */
void devl_resource_occ_get_register(struct devlink *devlink,
				    u64 resource_id,
				    devlink_resource_occ_get_t *occ_get,
				    void *occ_get_priv)
{
	struct devlink_resource *resource;

	lockdep_assert_held(&devlink->lock);

	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (WARN_ON(!resource))
		return;
	WARN_ON(resource->occ_get);

	resource->occ_get = occ_get;
	resource->occ_get_priv = occ_get_priv;
}
EXPORT_SYMBOL_GPL(devl_resource_occ_get_register);

/**
 *	devlink_resource_occ_get_register - register occupancy getter
 *
 *	@devlink: devlink
 *	@resource_id: resource id
 *	@occ_get: occupancy getter callback
 *	@occ_get_priv: occupancy getter callback priv
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
void devlink_resource_occ_get_register(struct devlink *devlink,
				       u64 resource_id,
				       devlink_resource_occ_get_t *occ_get,
				       void *occ_get_priv)
{
	devl_lock(devlink);
	devl_resource_occ_get_register(devlink, resource_id,
				       occ_get, occ_get_priv);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_resource_occ_get_register);

/**
 * devl_resource_occ_get_unregister - unregister occupancy getter
 *
 * @devlink: devlink
 * @resource_id: resource id
 */
void devl_resource_occ_get_unregister(struct devlink *devlink,
				      u64 resource_id)
{
	struct devlink_resource *resource;

	lockdep_assert_held(&devlink->lock);

	resource = devlink_resource_find(devlink, NULL, resource_id);
	if (WARN_ON(!resource))
		return;
	WARN_ON(!resource->occ_get);

	resource->occ_get = NULL;
	resource->occ_get_priv = NULL;
}
EXPORT_SYMBOL_GPL(devl_resource_occ_get_unregister);

/**
 *	devlink_resource_occ_get_unregister - unregister occupancy getter
 *
 *	@devlink: devlink
 *	@resource_id: resource id
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
void devlink_resource_occ_get_unregister(struct devlink *devlink,
					 u64 resource_id)
{
	devl_lock(devlink);
	devl_resource_occ_get_unregister(devlink, resource_id);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_resource_occ_get_unregister);

static int devlink_param_verify(const struct devlink_param *param)
{
	if (!param || !param->name || !param->supported_cmodes)
		return -EINVAL;
	if (param->generic)
		return devlink_param_generic_verify(param);
	else
		return devlink_param_driver_verify(param);
}

static int devlink_param_register(struct devlink *devlink,
				  const struct devlink_param *param)
{
	struct devlink_param_item *param_item;
	int err;

	WARN_ON(devlink_param_verify(param));
	WARN_ON(devlink_param_find_by_name(&devlink->params, param->name));

	if (param->supported_cmodes == BIT(DEVLINK_PARAM_CMODE_DRIVERINIT))
		WARN_ON(param->get || param->set);
	else
		WARN_ON(!param->get || !param->set);

	param_item = kzalloc(sizeof(*param_item), GFP_KERNEL);
	if (!param_item)
		return -ENOMEM;

	param_item->param = param;

	err = xa_insert(&devlink->params, param->id, param_item, GFP_KERNEL);
	if (err)
		goto err_xa_insert;

	devlink_param_notify(devlink, 0, param_item, DEVLINK_CMD_PARAM_NEW);
	return 0;

err_xa_insert:
	kfree(param_item);
	return err;
}

static void devlink_param_unregister(struct devlink *devlink,
				     const struct devlink_param *param)
{
	struct devlink_param_item *param_item;

	param_item = devlink_param_find_by_id(&devlink->params, param->id);
	if (WARN_ON(!param_item))
		return;
	devlink_param_notify(devlink, 0, param_item, DEVLINK_CMD_PARAM_DEL);
	xa_erase(&devlink->params, param->id);
	kfree(param_item);
}

/**
 *	devl_params_register - register configuration parameters
 *
 *	@devlink: devlink
 *	@params: configuration parameters array
 *	@params_count: number of parameters provided
 *
 *	Register the configuration parameters supported by the driver.
 */
int devl_params_register(struct devlink *devlink,
			 const struct devlink_param *params,
			 size_t params_count)
{
	const struct devlink_param *param = params;
	int i, err;

	lockdep_assert_held(&devlink->lock);

	for (i = 0; i < params_count; i++, param++) {
		err = devlink_param_register(devlink, param);
		if (err)
			goto rollback;
	}
	return 0;

rollback:
	if (!i)
		return err;

	for (param--; i > 0; i--, param--)
		devlink_param_unregister(devlink, param);
	return err;
}
EXPORT_SYMBOL_GPL(devl_params_register);

int devlink_params_register(struct devlink *devlink,
			    const struct devlink_param *params,
			    size_t params_count)
{
	int err;

	devl_lock(devlink);
	err = devl_params_register(devlink, params, params_count);
	devl_unlock(devlink);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_params_register);

/**
 *	devl_params_unregister - unregister configuration parameters
 *	@devlink: devlink
 *	@params: configuration parameters to unregister
 *	@params_count: number of parameters provided
 */
void devl_params_unregister(struct devlink *devlink,
			    const struct devlink_param *params,
			    size_t params_count)
{
	const struct devlink_param *param = params;
	int i;

	lockdep_assert_held(&devlink->lock);

	for (i = 0; i < params_count; i++, param++)
		devlink_param_unregister(devlink, param);
}
EXPORT_SYMBOL_GPL(devl_params_unregister);

void devlink_params_unregister(struct devlink *devlink,
			       const struct devlink_param *params,
			       size_t params_count)
{
	devl_lock(devlink);
	devl_params_unregister(devlink, params, params_count);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_params_unregister);

/**
 *	devl_param_driverinit_value_get - get configuration parameter
 *					  value for driver initializing
 *
 *	@devlink: devlink
 *	@param_id: parameter ID
 *	@val: pointer to store the value of parameter in driverinit
 *	      configuration mode
 *
 *	This function should be used by the driver to get driverinit
 *	configuration for initialization after reload command.
 *
 *	Note that lockless call of this function relies on the
 *	driver to maintain following basic sane behavior:
 *	1) Driver ensures a call to this function cannot race with
 *	   registering/unregistering the parameter with the same parameter ID.
 *	2) Driver ensures a call to this function cannot race with
 *	   devl_param_driverinit_value_set() call with the same parameter ID.
 *	3) Driver ensures a call to this function cannot race with
 *	   reload operation.
 *	If the driver is not able to comply, it has to take the devlink->lock
 *	while calling this.
 */
int devl_param_driverinit_value_get(struct devlink *devlink, u32 param_id,
				    union devlink_param_value *val)
{
	struct devlink_param_item *param_item;

	if (WARN_ON(!devlink_reload_supported(devlink->ops)))
		return -EOPNOTSUPP;

	param_item = devlink_param_find_by_id(&devlink->params, param_id);
	if (!param_item)
		return -EINVAL;

	if (!param_item->driverinit_value_valid)
		return -EOPNOTSUPP;

	if (WARN_ON(!devlink_param_cmode_is_supported(param_item->param,
						      DEVLINK_PARAM_CMODE_DRIVERINIT)))
		return -EOPNOTSUPP;

	*val = param_item->driverinit_value;

	return 0;
}
EXPORT_SYMBOL_GPL(devl_param_driverinit_value_get);

/**
 *	devl_param_driverinit_value_set - set value of configuration
 *					  parameter for driverinit
 *					  configuration mode
 *
 *	@devlink: devlink
 *	@param_id: parameter ID
 *	@init_val: value of parameter to set for driverinit configuration mode
 *
 *	This function should be used by the driver to set driverinit
 *	configuration mode default value.
 */
void devl_param_driverinit_value_set(struct devlink *devlink, u32 param_id,
				     union devlink_param_value init_val)
{
	struct devlink_param_item *param_item;

	devl_assert_locked(devlink);

	param_item = devlink_param_find_by_id(&devlink->params, param_id);
	if (WARN_ON(!param_item))
		return;

	if (WARN_ON(!devlink_param_cmode_is_supported(param_item->param,
						      DEVLINK_PARAM_CMODE_DRIVERINIT)))
		return;

	param_item->driverinit_value = init_val;
	param_item->driverinit_value_valid = true;

	devlink_param_notify(devlink, 0, param_item, DEVLINK_CMD_PARAM_NEW);
}
EXPORT_SYMBOL_GPL(devl_param_driverinit_value_set);

void devlink_params_driverinit_load_new(struct devlink *devlink)
{
	struct devlink_param_item *param_item;
	unsigned long param_id;

	xa_for_each(&devlink->params, param_id, param_item) {
		if (!devlink_param_cmode_is_supported(param_item->param,
						      DEVLINK_PARAM_CMODE_DRIVERINIT) ||
		    !param_item->driverinit_value_new_valid)
			continue;
		param_item->driverinit_value = param_item->driverinit_value_new;
		param_item->driverinit_value_valid = true;
		param_item->driverinit_value_new_valid = false;
	}
}

/**
 *	devl_param_value_changed - notify devlink on a parameter's value
 *				   change. Should be called by the driver
 *				   right after the change.
 *
 *	@devlink: devlink
 *	@param_id: parameter ID
 *
 *	This function should be used by the driver to notify devlink on value
 *	change, excluding driverinit configuration mode.
 *	For driverinit configuration mode driver should use the function
 */
void devl_param_value_changed(struct devlink *devlink, u32 param_id)
{
	struct devlink_param_item *param_item;

	param_item = devlink_param_find_by_id(&devlink->params, param_id);
	WARN_ON(!param_item);

	devlink_param_notify(devlink, 0, param_item, DEVLINK_CMD_PARAM_NEW);
}
EXPORT_SYMBOL_GPL(devl_param_value_changed);

/**
 * devl_region_create - create a new address region
 *
 * @devlink: devlink
 * @ops: region operations and name
 * @region_max_snapshots: Maximum supported number of snapshots for region
 * @region_size: size of region
 */
struct devlink_region *devl_region_create(struct devlink *devlink,
					  const struct devlink_region_ops *ops,
					  u32 region_max_snapshots,
					  u64 region_size)
{
	struct devlink_region *region;

	devl_assert_locked(devlink);

	if (WARN_ON(!ops) || WARN_ON(!ops->destructor))
		return ERR_PTR(-EINVAL);

	if (devlink_region_get_by_name(devlink, ops->name))
		return ERR_PTR(-EEXIST);

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return ERR_PTR(-ENOMEM);

	region->devlink = devlink;
	region->max_snapshots = region_max_snapshots;
	region->ops = ops;
	region->size = region_size;
	INIT_LIST_HEAD(&region->snapshot_list);
	mutex_init(&region->snapshot_lock);
	list_add_tail(&region->list, &devlink->region_list);
	devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_NEW);

	return region;
}
EXPORT_SYMBOL_GPL(devl_region_create);

/**
 *	devlink_region_create - create a new address region
 *
 *	@devlink: devlink
 *	@ops: region operations and name
 *	@region_max_snapshots: Maximum supported number of snapshots for region
 *	@region_size: size of region
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
struct devlink_region *
devlink_region_create(struct devlink *devlink,
		      const struct devlink_region_ops *ops,
		      u32 region_max_snapshots, u64 region_size)
{
	struct devlink_region *region;

	devl_lock(devlink);
	region = devl_region_create(devlink, ops, region_max_snapshots,
				    region_size);
	devl_unlock(devlink);
	return region;
}
EXPORT_SYMBOL_GPL(devlink_region_create);

/**
 *	devlink_port_region_create - create a new address region for a port
 *
 *	@port: devlink port
 *	@ops: region operations and name
 *	@region_max_snapshots: Maximum supported number of snapshots for region
 *	@region_size: size of region
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
struct devlink_region *
devlink_port_region_create(struct devlink_port *port,
			   const struct devlink_port_region_ops *ops,
			   u32 region_max_snapshots, u64 region_size)
{
	struct devlink *devlink = port->devlink;
	struct devlink_region *region;
	int err = 0;

	ASSERT_DEVLINK_PORT_INITIALIZED(port);

	if (WARN_ON(!ops) || WARN_ON(!ops->destructor))
		return ERR_PTR(-EINVAL);

	devl_lock(devlink);

	if (devlink_port_region_get_by_name(port, ops->name)) {
		err = -EEXIST;
		goto unlock;
	}

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region) {
		err = -ENOMEM;
		goto unlock;
	}

	region->devlink = devlink;
	region->port = port;
	region->max_snapshots = region_max_snapshots;
	region->port_ops = ops;
	region->size = region_size;
	INIT_LIST_HEAD(&region->snapshot_list);
	mutex_init(&region->snapshot_lock);
	list_add_tail(&region->list, &port->region_list);
	devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_NEW);

	devl_unlock(devlink);
	return region;

unlock:
	devl_unlock(devlink);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(devlink_port_region_create);

/**
 * devl_region_destroy - destroy address region
 *
 * @region: devlink region to destroy
 */
void devl_region_destroy(struct devlink_region *region)
{
	struct devlink *devlink = region->devlink;
	struct devlink_snapshot *snapshot, *ts;

	devl_assert_locked(devlink);

	/* Free all snapshots of region */
	mutex_lock(&region->snapshot_lock);
	list_for_each_entry_safe(snapshot, ts, &region->snapshot_list, list)
		devlink_region_snapshot_del(region, snapshot);
	mutex_unlock(&region->snapshot_lock);

	list_del(&region->list);
	mutex_destroy(&region->snapshot_lock);

	devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_DEL);
	kfree(region);
}
EXPORT_SYMBOL_GPL(devl_region_destroy);

/**
 *	devlink_region_destroy - destroy address region
 *
 *	@region: devlink region to destroy
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
void devlink_region_destroy(struct devlink_region *region)
{
	struct devlink *devlink = region->devlink;

	devl_lock(devlink);
	devl_region_destroy(region);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_region_destroy);

/**
 *	devlink_region_snapshot_id_get - get snapshot ID
 *
 *	This callback should be called when adding a new snapshot,
 *	Driver should use the same id for multiple snapshots taken
 *	on multiple regions at the same time/by the same trigger.
 *
 *	The caller of this function must use devlink_region_snapshot_id_put
 *	when finished creating regions using this id.
 *
 *	Returns zero on success, or a negative error code on failure.
 *
 *	@devlink: devlink
 *	@id: storage to return id
 */
int devlink_region_snapshot_id_get(struct devlink *devlink, u32 *id)
{
	return __devlink_region_snapshot_id_get(devlink, id);
}
EXPORT_SYMBOL_GPL(devlink_region_snapshot_id_get);

/**
 *	devlink_region_snapshot_id_put - put snapshot ID reference
 *
 *	This should be called by a driver after finishing creating snapshots
 *	with an id. Doing so ensures that the ID can later be released in the
 *	event that all snapshots using it have been destroyed.
 *
 *	@devlink: devlink
 *	@id: id to release reference on
 */
void devlink_region_snapshot_id_put(struct devlink *devlink, u32 id)
{
	__devlink_snapshot_id_decrement(devlink, id);
}
EXPORT_SYMBOL_GPL(devlink_region_snapshot_id_put);

/**
 *	devlink_region_snapshot_create - create a new snapshot
 *	This will add a new snapshot of a region. The snapshot
 *	will be stored on the region struct and can be accessed
 *	from devlink. This is useful for future analyses of snapshots.
 *	Multiple snapshots can be created on a region.
 *	The @snapshot_id should be obtained using the getter function.
 *
 *	@region: devlink region of the snapshot
 *	@data: snapshot data
 *	@snapshot_id: snapshot id to be created
 */
int devlink_region_snapshot_create(struct devlink_region *region,
				   u8 *data, u32 snapshot_id)
{
	int err;

	mutex_lock(&region->snapshot_lock);
	err = __devlink_region_snapshot_create(region, data, snapshot_id);
	mutex_unlock(&region->snapshot_lock);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_region_snapshot_create);

#define DEVLINK_TRAP(_id, _type)					      \
	{								      \
		.type = DEVLINK_TRAP_TYPE_##_type,			      \
		.id = DEVLINK_TRAP_GENERIC_ID_##_id,			      \
		.name = DEVLINK_TRAP_GENERIC_NAME_##_id,		      \
	}

static const struct devlink_trap devlink_trap_generic[] = {
	DEVLINK_TRAP(SMAC_MC, DROP),
	DEVLINK_TRAP(VLAN_TAG_MISMATCH, DROP),
	DEVLINK_TRAP(INGRESS_VLAN_FILTER, DROP),
	DEVLINK_TRAP(INGRESS_STP_FILTER, DROP),
	DEVLINK_TRAP(EMPTY_TX_LIST, DROP),
	DEVLINK_TRAP(PORT_LOOPBACK_FILTER, DROP),
	DEVLINK_TRAP(BLACKHOLE_ROUTE, DROP),
	DEVLINK_TRAP(TTL_ERROR, EXCEPTION),
	DEVLINK_TRAP(TAIL_DROP, DROP),
	DEVLINK_TRAP(NON_IP_PACKET, DROP),
	DEVLINK_TRAP(UC_DIP_MC_DMAC, DROP),
	DEVLINK_TRAP(DIP_LB, DROP),
	DEVLINK_TRAP(SIP_MC, DROP),
	DEVLINK_TRAP(SIP_LB, DROP),
	DEVLINK_TRAP(CORRUPTED_IP_HDR, DROP),
	DEVLINK_TRAP(IPV4_SIP_BC, DROP),
	DEVLINK_TRAP(IPV6_MC_DIP_RESERVED_SCOPE, DROP),
	DEVLINK_TRAP(IPV6_MC_DIP_INTERFACE_LOCAL_SCOPE, DROP),
	DEVLINK_TRAP(MTU_ERROR, EXCEPTION),
	DEVLINK_TRAP(UNRESOLVED_NEIGH, EXCEPTION),
	DEVLINK_TRAP(RPF, EXCEPTION),
	DEVLINK_TRAP(REJECT_ROUTE, EXCEPTION),
	DEVLINK_TRAP(IPV4_LPM_UNICAST_MISS, EXCEPTION),
	DEVLINK_TRAP(IPV6_LPM_UNICAST_MISS, EXCEPTION),
	DEVLINK_TRAP(NON_ROUTABLE, DROP),
	DEVLINK_TRAP(DECAP_ERROR, EXCEPTION),
	DEVLINK_TRAP(OVERLAY_SMAC_MC, DROP),
	DEVLINK_TRAP(INGRESS_FLOW_ACTION_DROP, DROP),
	DEVLINK_TRAP(EGRESS_FLOW_ACTION_DROP, DROP),
	DEVLINK_TRAP(STP, CONTROL),
	DEVLINK_TRAP(LACP, CONTROL),
	DEVLINK_TRAP(LLDP, CONTROL),
	DEVLINK_TRAP(IGMP_QUERY, CONTROL),
	DEVLINK_TRAP(IGMP_V1_REPORT, CONTROL),
	DEVLINK_TRAP(IGMP_V2_REPORT, CONTROL),
	DEVLINK_TRAP(IGMP_V3_REPORT, CONTROL),
	DEVLINK_TRAP(IGMP_V2_LEAVE, CONTROL),
	DEVLINK_TRAP(MLD_QUERY, CONTROL),
	DEVLINK_TRAP(MLD_V1_REPORT, CONTROL),
	DEVLINK_TRAP(MLD_V2_REPORT, CONTROL),
	DEVLINK_TRAP(MLD_V1_DONE, CONTROL),
	DEVLINK_TRAP(IPV4_DHCP, CONTROL),
	DEVLINK_TRAP(IPV6_DHCP, CONTROL),
	DEVLINK_TRAP(ARP_REQUEST, CONTROL),
	DEVLINK_TRAP(ARP_RESPONSE, CONTROL),
	DEVLINK_TRAP(ARP_OVERLAY, CONTROL),
	DEVLINK_TRAP(IPV6_NEIGH_SOLICIT, CONTROL),
	DEVLINK_TRAP(IPV6_NEIGH_ADVERT, CONTROL),
	DEVLINK_TRAP(IPV4_BFD, CONTROL),
	DEVLINK_TRAP(IPV6_BFD, CONTROL),
	DEVLINK_TRAP(IPV4_OSPF, CONTROL),
	DEVLINK_TRAP(IPV6_OSPF, CONTROL),
	DEVLINK_TRAP(IPV4_BGP, CONTROL),
	DEVLINK_TRAP(IPV6_BGP, CONTROL),
	DEVLINK_TRAP(IPV4_VRRP, CONTROL),
	DEVLINK_TRAP(IPV6_VRRP, CONTROL),
	DEVLINK_TRAP(IPV4_PIM, CONTROL),
	DEVLINK_TRAP(IPV6_PIM, CONTROL),
	DEVLINK_TRAP(UC_LB, CONTROL),
	DEVLINK_TRAP(LOCAL_ROUTE, CONTROL),
	DEVLINK_TRAP(EXTERNAL_ROUTE, CONTROL),
	DEVLINK_TRAP(IPV6_UC_DIP_LINK_LOCAL_SCOPE, CONTROL),
	DEVLINK_TRAP(IPV6_DIP_ALL_NODES, CONTROL),
	DEVLINK_TRAP(IPV6_DIP_ALL_ROUTERS, CONTROL),
	DEVLINK_TRAP(IPV6_ROUTER_SOLICIT, CONTROL),
	DEVLINK_TRAP(IPV6_ROUTER_ADVERT, CONTROL),
	DEVLINK_TRAP(IPV6_REDIRECT, CONTROL),
	DEVLINK_TRAP(IPV4_ROUTER_ALERT, CONTROL),
	DEVLINK_TRAP(IPV6_ROUTER_ALERT, CONTROL),
	DEVLINK_TRAP(PTP_EVENT, CONTROL),
	DEVLINK_TRAP(PTP_GENERAL, CONTROL),
	DEVLINK_TRAP(FLOW_ACTION_SAMPLE, CONTROL),
	DEVLINK_TRAP(FLOW_ACTION_TRAP, CONTROL),
	DEVLINK_TRAP(EARLY_DROP, DROP),
	DEVLINK_TRAP(VXLAN_PARSING, DROP),
	DEVLINK_TRAP(LLC_SNAP_PARSING, DROP),
	DEVLINK_TRAP(VLAN_PARSING, DROP),
	DEVLINK_TRAP(PPPOE_PPP_PARSING, DROP),
	DEVLINK_TRAP(MPLS_PARSING, DROP),
	DEVLINK_TRAP(ARP_PARSING, DROP),
	DEVLINK_TRAP(IP_1_PARSING, DROP),
	DEVLINK_TRAP(IP_N_PARSING, DROP),
	DEVLINK_TRAP(GRE_PARSING, DROP),
	DEVLINK_TRAP(UDP_PARSING, DROP),
	DEVLINK_TRAP(TCP_PARSING, DROP),
	DEVLINK_TRAP(IPSEC_PARSING, DROP),
	DEVLINK_TRAP(SCTP_PARSING, DROP),
	DEVLINK_TRAP(DCCP_PARSING, DROP),
	DEVLINK_TRAP(GTP_PARSING, DROP),
	DEVLINK_TRAP(ESP_PARSING, DROP),
	DEVLINK_TRAP(BLACKHOLE_NEXTHOP, DROP),
	DEVLINK_TRAP(DMAC_FILTER, DROP),
	DEVLINK_TRAP(EAPOL, CONTROL),
	DEVLINK_TRAP(LOCKED_PORT, DROP),
};

#define DEVLINK_TRAP_GROUP(_id)						      \
	{								      \
		.id = DEVLINK_TRAP_GROUP_GENERIC_ID_##_id,		      \
		.name = DEVLINK_TRAP_GROUP_GENERIC_NAME_##_id,		      \
	}

static const struct devlink_trap_group devlink_trap_group_generic[] = {
	DEVLINK_TRAP_GROUP(L2_DROPS),
	DEVLINK_TRAP_GROUP(L3_DROPS),
	DEVLINK_TRAP_GROUP(L3_EXCEPTIONS),
	DEVLINK_TRAP_GROUP(BUFFER_DROPS),
	DEVLINK_TRAP_GROUP(TUNNEL_DROPS),
	DEVLINK_TRAP_GROUP(ACL_DROPS),
	DEVLINK_TRAP_GROUP(STP),
	DEVLINK_TRAP_GROUP(LACP),
	DEVLINK_TRAP_GROUP(LLDP),
	DEVLINK_TRAP_GROUP(MC_SNOOPING),
	DEVLINK_TRAP_GROUP(DHCP),
	DEVLINK_TRAP_GROUP(NEIGH_DISCOVERY),
	DEVLINK_TRAP_GROUP(BFD),
	DEVLINK_TRAP_GROUP(OSPF),
	DEVLINK_TRAP_GROUP(BGP),
	DEVLINK_TRAP_GROUP(VRRP),
	DEVLINK_TRAP_GROUP(PIM),
	DEVLINK_TRAP_GROUP(UC_LB),
	DEVLINK_TRAP_GROUP(LOCAL_DELIVERY),
	DEVLINK_TRAP_GROUP(EXTERNAL_DELIVERY),
	DEVLINK_TRAP_GROUP(IPV6),
	DEVLINK_TRAP_GROUP(PTP_EVENT),
	DEVLINK_TRAP_GROUP(PTP_GENERAL),
	DEVLINK_TRAP_GROUP(ACL_SAMPLE),
	DEVLINK_TRAP_GROUP(ACL_TRAP),
	DEVLINK_TRAP_GROUP(PARSER_ERROR_DROPS),
	DEVLINK_TRAP_GROUP(EAPOL),
};

static int devlink_trap_generic_verify(const struct devlink_trap *trap)
{
	if (trap->id > DEVLINK_TRAP_GENERIC_ID_MAX)
		return -EINVAL;

	if (strcmp(trap->name, devlink_trap_generic[trap->id].name))
		return -EINVAL;

	if (trap->type != devlink_trap_generic[trap->id].type)
		return -EINVAL;

	return 0;
}

static int devlink_trap_driver_verify(const struct devlink_trap *trap)
{
	int i;

	if (trap->id <= DEVLINK_TRAP_GENERIC_ID_MAX)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(devlink_trap_generic); i++) {
		if (!strcmp(trap->name, devlink_trap_generic[i].name))
			return -EEXIST;
	}

	return 0;
}

static int devlink_trap_verify(const struct devlink_trap *trap)
{
	if (!trap || !trap->name)
		return -EINVAL;

	if (trap->generic)
		return devlink_trap_generic_verify(trap);
	else
		return devlink_trap_driver_verify(trap);
}

static int
devlink_trap_group_generic_verify(const struct devlink_trap_group *group)
{
	if (group->id > DEVLINK_TRAP_GROUP_GENERIC_ID_MAX)
		return -EINVAL;

	if (strcmp(group->name, devlink_trap_group_generic[group->id].name))
		return -EINVAL;

	return 0;
}

static int
devlink_trap_group_driver_verify(const struct devlink_trap_group *group)
{
	int i;

	if (group->id <= DEVLINK_TRAP_GROUP_GENERIC_ID_MAX)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(devlink_trap_group_generic); i++) {
		if (!strcmp(group->name, devlink_trap_group_generic[i].name))
			return -EEXIST;
	}

	return 0;
}

static int devlink_trap_group_verify(const struct devlink_trap_group *group)
{
	if (group->generic)
		return devlink_trap_group_generic_verify(group);
	else
		return devlink_trap_group_driver_verify(group);
}

static void
devlink_trap_group_notify(struct devlink *devlink,
			  const struct devlink_trap_group_item *group_item,
			  enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON_ONCE(cmd != DEVLINK_CMD_TRAP_GROUP_NEW &&
		     cmd != DEVLINK_CMD_TRAP_GROUP_DEL);
	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_trap_group_fill(msg, devlink, group_item, cmd, 0, 0,
					 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static int
devlink_trap_item_group_link(struct devlink *devlink,
			     struct devlink_trap_item *trap_item)
{
	u16 group_id = trap_item->trap->init_group_id;
	struct devlink_trap_group_item *group_item;

	group_item = devlink_trap_group_item_lookup_by_id(devlink, group_id);
	if (WARN_ON_ONCE(!group_item))
		return -EINVAL;

	trap_item->group_item = group_item;

	return 0;
}

static void devlink_trap_notify(struct devlink *devlink,
				const struct devlink_trap_item *trap_item,
				enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON_ONCE(cmd != DEVLINK_CMD_TRAP_NEW &&
		     cmd != DEVLINK_CMD_TRAP_DEL);
	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_trap_fill(msg, devlink, trap_item, cmd, 0, 0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static int
devlink_trap_register(struct devlink *devlink,
		      const struct devlink_trap *trap, void *priv)
{
	struct devlink_trap_item *trap_item;
	int err;

	if (devlink_trap_item_lookup(devlink, trap->name))
		return -EEXIST;

	trap_item = kzalloc(sizeof(*trap_item), GFP_KERNEL);
	if (!trap_item)
		return -ENOMEM;

	trap_item->stats = netdev_alloc_pcpu_stats(struct devlink_stats);
	if (!trap_item->stats) {
		err = -ENOMEM;
		goto err_stats_alloc;
	}

	trap_item->trap = trap;
	trap_item->action = trap->init_action;
	trap_item->priv = priv;

	err = devlink_trap_item_group_link(devlink, trap_item);
	if (err)
		goto err_group_link;

	err = devlink->ops->trap_init(devlink, trap, trap_item);
	if (err)
		goto err_trap_init;

	list_add_tail(&trap_item->list, &devlink->trap_list);
	devlink_trap_notify(devlink, trap_item, DEVLINK_CMD_TRAP_NEW);

	return 0;

err_trap_init:
err_group_link:
	free_percpu(trap_item->stats);
err_stats_alloc:
	kfree(trap_item);
	return err;
}

static void devlink_trap_unregister(struct devlink *devlink,
				    const struct devlink_trap *trap)
{
	struct devlink_trap_item *trap_item;

	trap_item = devlink_trap_item_lookup(devlink, trap->name);
	if (WARN_ON_ONCE(!trap_item))
		return;

	devlink_trap_notify(devlink, trap_item, DEVLINK_CMD_TRAP_DEL);
	list_del(&trap_item->list);
	if (devlink->ops->trap_fini)
		devlink->ops->trap_fini(devlink, trap, trap_item);
	free_percpu(trap_item->stats);
	kfree(trap_item);
}

static void devlink_trap_disable(struct devlink *devlink,
				 const struct devlink_trap *trap)
{
	struct devlink_trap_item *trap_item;

	trap_item = devlink_trap_item_lookup(devlink, trap->name);
	if (WARN_ON_ONCE(!trap_item))
		return;

	devlink->ops->trap_action_set(devlink, trap, DEVLINK_TRAP_ACTION_DROP,
				      NULL);
	trap_item->action = DEVLINK_TRAP_ACTION_DROP;
}

/**
 * devl_traps_register - Register packet traps with devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 * @priv: Driver private information.
 *
 * Return: Non-zero value on failure.
 */
int devl_traps_register(struct devlink *devlink,
			const struct devlink_trap *traps,
			size_t traps_count, void *priv)
{
	int i, err;

	if (!devlink->ops->trap_init || !devlink->ops->trap_action_set)
		return -EINVAL;

	devl_assert_locked(devlink);
	for (i = 0; i < traps_count; i++) {
		const struct devlink_trap *trap = &traps[i];

		err = devlink_trap_verify(trap);
		if (err)
			goto err_trap_verify;

		err = devlink_trap_register(devlink, trap, priv);
		if (err)
			goto err_trap_register;
	}

	return 0;

err_trap_register:
err_trap_verify:
	for (i--; i >= 0; i--)
		devlink_trap_unregister(devlink, &traps[i]);
	return err;
}
EXPORT_SYMBOL_GPL(devl_traps_register);

/**
 * devlink_traps_register - Register packet traps with devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 * @priv: Driver private information.
 *
 * Context: Takes and release devlink->lock <mutex>.
 *
 * Return: Non-zero value on failure.
 */
int devlink_traps_register(struct devlink *devlink,
			   const struct devlink_trap *traps,
			   size_t traps_count, void *priv)
{
	int err;

	devl_lock(devlink);
	err = devl_traps_register(devlink, traps, traps_count, priv);
	devl_unlock(devlink);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_traps_register);

/**
 * devl_traps_unregister - Unregister packet traps from devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 */
void devl_traps_unregister(struct devlink *devlink,
			   const struct devlink_trap *traps,
			   size_t traps_count)
{
	int i;

	devl_assert_locked(devlink);
	/* Make sure we do not have any packets in-flight while unregistering
	 * traps by disabling all of them and waiting for a grace period.
	 */
	for (i = traps_count - 1; i >= 0; i--)
		devlink_trap_disable(devlink, &traps[i]);
	synchronize_rcu();
	for (i = traps_count - 1; i >= 0; i--)
		devlink_trap_unregister(devlink, &traps[i]);
}
EXPORT_SYMBOL_GPL(devl_traps_unregister);

/**
 * devlink_traps_unregister - Unregister packet traps from devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 *
 * Context: Takes and release devlink->lock <mutex>.
 */
void devlink_traps_unregister(struct devlink *devlink,
			      const struct devlink_trap *traps,
			      size_t traps_count)
{
	devl_lock(devlink);
	devl_traps_unregister(devlink, traps, traps_count);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_traps_unregister);

static void
devlink_trap_stats_update(struct devlink_stats __percpu *trap_stats,
			  size_t skb_len)
{
	struct devlink_stats *stats;

	stats = this_cpu_ptr(trap_stats);
	u64_stats_update_begin(&stats->syncp);
	u64_stats_add(&stats->rx_bytes, skb_len);
	u64_stats_inc(&stats->rx_packets);
	u64_stats_update_end(&stats->syncp);
}

static void
devlink_trap_report_metadata_set(struct devlink_trap_metadata *metadata,
				 const struct devlink_trap_item *trap_item,
				 struct devlink_port *in_devlink_port,
				 const struct flow_action_cookie *fa_cookie)
{
	metadata->trap_name = trap_item->trap->name;
	metadata->trap_group_name = trap_item->group_item->group->name;
	metadata->fa_cookie = fa_cookie;
	metadata->trap_type = trap_item->trap->type;

	spin_lock(&in_devlink_port->type_lock);
	if (in_devlink_port->type == DEVLINK_PORT_TYPE_ETH)
		metadata->input_dev = in_devlink_port->type_eth.netdev;
	spin_unlock(&in_devlink_port->type_lock);
}

/**
 * devlink_trap_report - Report trapped packet to drop monitor.
 * @devlink: devlink.
 * @skb: Trapped packet.
 * @trap_ctx: Trap context.
 * @in_devlink_port: Input devlink port.
 * @fa_cookie: Flow action cookie. Could be NULL.
 */
void devlink_trap_report(struct devlink *devlink, struct sk_buff *skb,
			 void *trap_ctx, struct devlink_port *in_devlink_port,
			 const struct flow_action_cookie *fa_cookie)

{
	struct devlink_trap_item *trap_item = trap_ctx;

	devlink_trap_stats_update(trap_item->stats, skb->len);
	devlink_trap_stats_update(trap_item->group_item->stats, skb->len);

	if (trace_devlink_trap_report_enabled()) {
		struct devlink_trap_metadata metadata = {};

		devlink_trap_report_metadata_set(&metadata, trap_item,
						 in_devlink_port, fa_cookie);
		trace_devlink_trap_report(devlink, skb, &metadata);
	}
}
EXPORT_SYMBOL_GPL(devlink_trap_report);

/**
 * devlink_trap_ctx_priv - Trap context to driver private information.
 * @trap_ctx: Trap context.
 *
 * Return: Driver private information passed during registration.
 */
void *devlink_trap_ctx_priv(void *trap_ctx)
{
	struct devlink_trap_item *trap_item = trap_ctx;

	return trap_item->priv;
}
EXPORT_SYMBOL_GPL(devlink_trap_ctx_priv);

static int
devlink_trap_group_item_policer_link(struct devlink *devlink,
				     struct devlink_trap_group_item *group_item)
{
	u32 policer_id = group_item->group->init_policer_id;
	struct devlink_trap_policer_item *policer_item;

	if (policer_id == 0)
		return 0;

	policer_item = devlink_trap_policer_item_lookup(devlink, policer_id);
	if (WARN_ON_ONCE(!policer_item))
		return -EINVAL;

	group_item->policer_item = policer_item;

	return 0;
}

static int
devlink_trap_group_register(struct devlink *devlink,
			    const struct devlink_trap_group *group)
{
	struct devlink_trap_group_item *group_item;
	int err;

	if (devlink_trap_group_item_lookup(devlink, group->name))
		return -EEXIST;

	group_item = kzalloc(sizeof(*group_item), GFP_KERNEL);
	if (!group_item)
		return -ENOMEM;

	group_item->stats = netdev_alloc_pcpu_stats(struct devlink_stats);
	if (!group_item->stats) {
		err = -ENOMEM;
		goto err_stats_alloc;
	}

	group_item->group = group;

	err = devlink_trap_group_item_policer_link(devlink, group_item);
	if (err)
		goto err_policer_link;

	if (devlink->ops->trap_group_init) {
		err = devlink->ops->trap_group_init(devlink, group);
		if (err)
			goto err_group_init;
	}

	list_add_tail(&group_item->list, &devlink->trap_group_list);
	devlink_trap_group_notify(devlink, group_item,
				  DEVLINK_CMD_TRAP_GROUP_NEW);

	return 0;

err_group_init:
err_policer_link:
	free_percpu(group_item->stats);
err_stats_alloc:
	kfree(group_item);
	return err;
}

static void
devlink_trap_group_unregister(struct devlink *devlink,
			      const struct devlink_trap_group *group)
{
	struct devlink_trap_group_item *group_item;

	group_item = devlink_trap_group_item_lookup(devlink, group->name);
	if (WARN_ON_ONCE(!group_item))
		return;

	devlink_trap_group_notify(devlink, group_item,
				  DEVLINK_CMD_TRAP_GROUP_DEL);
	list_del(&group_item->list);
	free_percpu(group_item->stats);
	kfree(group_item);
}

/**
 * devl_trap_groups_register - Register packet trap groups with devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 *
 * Return: Non-zero value on failure.
 */
int devl_trap_groups_register(struct devlink *devlink,
			      const struct devlink_trap_group *groups,
			      size_t groups_count)
{
	int i, err;

	devl_assert_locked(devlink);
	for (i = 0; i < groups_count; i++) {
		const struct devlink_trap_group *group = &groups[i];

		err = devlink_trap_group_verify(group);
		if (err)
			goto err_trap_group_verify;

		err = devlink_trap_group_register(devlink, group);
		if (err)
			goto err_trap_group_register;
	}

	return 0;

err_trap_group_register:
err_trap_group_verify:
	for (i--; i >= 0; i--)
		devlink_trap_group_unregister(devlink, &groups[i]);
	return err;
}
EXPORT_SYMBOL_GPL(devl_trap_groups_register);

/**
 * devlink_trap_groups_register - Register packet trap groups with devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 *
 * Context: Takes and release devlink->lock <mutex>.
 *
 * Return: Non-zero value on failure.
 */
int devlink_trap_groups_register(struct devlink *devlink,
				 const struct devlink_trap_group *groups,
				 size_t groups_count)
{
	int err;

	devl_lock(devlink);
	err = devl_trap_groups_register(devlink, groups, groups_count);
	devl_unlock(devlink);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_trap_groups_register);

/**
 * devl_trap_groups_unregister - Unregister packet trap groups from devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 */
void devl_trap_groups_unregister(struct devlink *devlink,
				 const struct devlink_trap_group *groups,
				 size_t groups_count)
{
	int i;

	devl_assert_locked(devlink);
	for (i = groups_count - 1; i >= 0; i--)
		devlink_trap_group_unregister(devlink, &groups[i]);
}
EXPORT_SYMBOL_GPL(devl_trap_groups_unregister);

/**
 * devlink_trap_groups_unregister - Unregister packet trap groups from devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 *
 * Context: Takes and release devlink->lock <mutex>.
 */
void devlink_trap_groups_unregister(struct devlink *devlink,
				    const struct devlink_trap_group *groups,
				    size_t groups_count)
{
	devl_lock(devlink);
	devl_trap_groups_unregister(devlink, groups, groups_count);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_trap_groups_unregister);

static void
devlink_trap_policer_notify(struct devlink *devlink,
			    const struct devlink_trap_policer_item *policer_item,
			    enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON_ONCE(cmd != DEVLINK_CMD_TRAP_POLICER_NEW &&
		     cmd != DEVLINK_CMD_TRAP_POLICER_DEL);
	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_trap_policer_fill(msg, devlink, policer_item, cmd, 0,
					   0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

static int
devlink_trap_policer_register(struct devlink *devlink,
			      const struct devlink_trap_policer *policer)
{
	struct devlink_trap_policer_item *policer_item;
	int err;

	if (devlink_trap_policer_item_lookup(devlink, policer->id))
		return -EEXIST;

	policer_item = kzalloc(sizeof(*policer_item), GFP_KERNEL);
	if (!policer_item)
		return -ENOMEM;

	policer_item->policer = policer;
	policer_item->rate = policer->init_rate;
	policer_item->burst = policer->init_burst;

	if (devlink->ops->trap_policer_init) {
		err = devlink->ops->trap_policer_init(devlink, policer);
		if (err)
			goto err_policer_init;
	}

	list_add_tail(&policer_item->list, &devlink->trap_policer_list);
	devlink_trap_policer_notify(devlink, policer_item,
				    DEVLINK_CMD_TRAP_POLICER_NEW);

	return 0;

err_policer_init:
	kfree(policer_item);
	return err;
}

static void
devlink_trap_policer_unregister(struct devlink *devlink,
				const struct devlink_trap_policer *policer)
{
	struct devlink_trap_policer_item *policer_item;

	policer_item = devlink_trap_policer_item_lookup(devlink, policer->id);
	if (WARN_ON_ONCE(!policer_item))
		return;

	devlink_trap_policer_notify(devlink, policer_item,
				    DEVLINK_CMD_TRAP_POLICER_DEL);
	list_del(&policer_item->list);
	if (devlink->ops->trap_policer_fini)
		devlink->ops->trap_policer_fini(devlink, policer);
	kfree(policer_item);
}

/**
 * devl_trap_policers_register - Register packet trap policers with devlink.
 * @devlink: devlink.
 * @policers: Packet trap policers.
 * @policers_count: Count of provided packet trap policers.
 *
 * Return: Non-zero value on failure.
 */
int
devl_trap_policers_register(struct devlink *devlink,
			    const struct devlink_trap_policer *policers,
			    size_t policers_count)
{
	int i, err;

	devl_assert_locked(devlink);
	for (i = 0; i < policers_count; i++) {
		const struct devlink_trap_policer *policer = &policers[i];

		if (WARN_ON(policer->id == 0 ||
			    policer->max_rate < policer->min_rate ||
			    policer->max_burst < policer->min_burst)) {
			err = -EINVAL;
			goto err_trap_policer_verify;
		}

		err = devlink_trap_policer_register(devlink, policer);
		if (err)
			goto err_trap_policer_register;
	}
	return 0;

err_trap_policer_register:
err_trap_policer_verify:
	for (i--; i >= 0; i--)
		devlink_trap_policer_unregister(devlink, &policers[i]);
	return err;
}
EXPORT_SYMBOL_GPL(devl_trap_policers_register);

/**
 * devl_trap_policers_unregister - Unregister packet trap policers from devlink.
 * @devlink: devlink.
 * @policers: Packet trap policers.
 * @policers_count: Count of provided packet trap policers.
 */
void
devl_trap_policers_unregister(struct devlink *devlink,
			      const struct devlink_trap_policer *policers,
			      size_t policers_count)
{
	int i;

	devl_assert_locked(devlink);
	for (i = policers_count - 1; i >= 0; i--)
		devlink_trap_policer_unregister(devlink, &policers[i]);
}
EXPORT_SYMBOL_GPL(devl_trap_policers_unregister);

int devlink_compat_phys_port_name_get(struct net_device *dev,
				      char *name, size_t len)
{
	struct devlink_port *devlink_port;

	/* RTNL mutex is held here which ensures that devlink_port
	 * instance cannot disappear in the middle. No need to take
	 * any devlink lock as only permanent values are accessed.
	 */
	ASSERT_RTNL();

	devlink_port = dev->devlink_port;
	if (!devlink_port)
		return -EOPNOTSUPP;

	return __devlink_port_phys_port_name_get(devlink_port, name, len);
}

int devlink_compat_switch_id_get(struct net_device *dev,
				 struct netdev_phys_item_id *ppid)
{
	struct devlink_port *devlink_port;

	/* Caller must hold RTNL mutex or reference to dev, which ensures that
	 * devlink_port instance cannot disappear in the middle. No need to take
	 * any devlink lock as only permanent values are accessed.
	 */
	devlink_port = dev->devlink_port;
	if (!devlink_port || !devlink_port->switch_port)
		return -EOPNOTSUPP;

	memcpy(ppid, &devlink_port->attrs.switch_id, sizeof(*ppid));

	return 0;
}
