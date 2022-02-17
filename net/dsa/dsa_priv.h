/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * net/dsa/dsa_priv.h - Hardware switch handling
 * Copyright (c) 2008-2009 Marvell Semiconductor
 */

#ifndef __DSA_PRIV_H
#define __DSA_PRIV_H

#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/netpoll.h>
#include <net/dsa.h>
#include <net/gro_cells.h>

#define DSA_MAX_NUM_OFFLOADING_BRIDGES		BITS_PER_LONG

enum {
	DSA_NOTIFIER_AGEING_TIME,
	DSA_NOTIFIER_BRIDGE_JOIN,
	DSA_NOTIFIER_BRIDGE_LEAVE,
	DSA_NOTIFIER_FDB_ADD,
	DSA_NOTIFIER_FDB_DEL,
	DSA_NOTIFIER_HOST_FDB_ADD,
	DSA_NOTIFIER_HOST_FDB_DEL,
	DSA_NOTIFIER_LAG_CHANGE,
	DSA_NOTIFIER_LAG_JOIN,
	DSA_NOTIFIER_LAG_LEAVE,
	DSA_NOTIFIER_MDB_ADD,
	DSA_NOTIFIER_MDB_DEL,
	DSA_NOTIFIER_HOST_MDB_ADD,
	DSA_NOTIFIER_HOST_MDB_DEL,
	DSA_NOTIFIER_VLAN_ADD,
	DSA_NOTIFIER_VLAN_DEL,
	DSA_NOTIFIER_HOST_VLAN_ADD,
	DSA_NOTIFIER_HOST_VLAN_DEL,
	DSA_NOTIFIER_MTU,
	DSA_NOTIFIER_TAG_PROTO,
	DSA_NOTIFIER_TAG_PROTO_CONNECT,
	DSA_NOTIFIER_TAG_PROTO_DISCONNECT,
	DSA_NOTIFIER_TAG_8021Q_VLAN_ADD,
	DSA_NOTIFIER_TAG_8021Q_VLAN_DEL,
	DSA_NOTIFIER_MASTER_STATE_CHANGE,
};

/* DSA_NOTIFIER_AGEING_TIME */
struct dsa_notifier_ageing_time_info {
	unsigned int ageing_time;
};

/* DSA_NOTIFIER_BRIDGE_* */
struct dsa_notifier_bridge_info {
	struct dsa_bridge bridge;
	int tree_index;
	int sw_index;
	int port;
	bool tx_fwd_offload;
};

/* DSA_NOTIFIER_FDB_* */
struct dsa_notifier_fdb_info {
	int sw_index;
	int port;
	const unsigned char *addr;
	u16 vid;
};

/* DSA_NOTIFIER_MDB_* */
struct dsa_notifier_mdb_info {
	const struct switchdev_obj_port_mdb *mdb;
	int sw_index;
	int port;
};

/* DSA_NOTIFIER_LAG_* */
struct dsa_notifier_lag_info {
	struct net_device *lag;
	int sw_index;
	int port;

	struct netdev_lag_upper_info *info;
};

/* DSA_NOTIFIER_VLAN_* */
struct dsa_notifier_vlan_info {
	const struct switchdev_obj_port_vlan *vlan;
	int sw_index;
	int port;
	struct netlink_ext_ack *extack;
};

/* DSA_NOTIFIER_MTU */
struct dsa_notifier_mtu_info {
	bool targeted_match;
	int sw_index;
	int port;
	int mtu;
};

/* DSA_NOTIFIER_TAG_PROTO_* */
struct dsa_notifier_tag_proto_info {
	const struct dsa_device_ops *tag_ops;
};

/* DSA_NOTIFIER_TAG_8021Q_VLAN_* */
struct dsa_notifier_tag_8021q_vlan_info {
	int tree_index;
	int sw_index;
	int port;
	u16 vid;
};

/* DSA_NOTIFIER_MASTER_STATE_CHANGE */
struct dsa_notifier_master_state_info {
	const struct net_device *master;
	bool operational;
};

struct dsa_switchdev_event_work {
	struct dsa_switch *ds;
	int port;
	struct net_device *dev;
	struct work_struct work;
	unsigned long event;
	/* Specific for SWITCHDEV_FDB_ADD_TO_DEVICE and
	 * SWITCHDEV_FDB_DEL_TO_DEVICE
	 */
	unsigned char addr[ETH_ALEN];
	u16 vid;
	bool host_addr;
};

struct dsa_slave_priv {
	/* Copy of CPU port xmit for faster access in slave transmit hot path */
	struct sk_buff *	(*xmit)(struct sk_buff *skb,
					struct net_device *dev);

	struct gro_cells	gcells;

	/* DSA port data, such as switch, port index, etc. */
	struct dsa_port		*dp;

#ifdef CONFIG_NET_POLL_CONTROLLER
	struct netpoll		*netpoll;
#endif

	/* TC context */
	struct list_head	mall_tc_list;
};

/* dsa.c */
const struct dsa_device_ops *dsa_tag_driver_get(int tag_protocol);
void dsa_tag_driver_put(const struct dsa_device_ops *ops);
const struct dsa_device_ops *dsa_find_tagger_by_name(const char *buf);

bool dsa_schedule_work(struct work_struct *work);
const char *dsa_tag_protocol_to_str(const struct dsa_device_ops *ops);

static inline int dsa_tag_protocol_overhead(const struct dsa_device_ops *ops)
{
	return ops->needed_headroom + ops->needed_tailroom;
}

/* master.c */
int dsa_master_setup(struct net_device *dev, struct dsa_port *cpu_dp);
void dsa_master_teardown(struct net_device *dev);

static inline struct net_device *dsa_master_find_slave(struct net_device *dev,
						       int device, int port)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	struct dsa_switch_tree *dst = cpu_dp->dst;
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dp->ds->index == device && dp->index == port &&
		    dp->type == DSA_PORT_TYPE_USER)
			return dp->slave;

	return NULL;
}

/* port.c */
void dsa_port_set_tag_protocol(struct dsa_port *cpu_dp,
			       const struct dsa_device_ops *tag_ops);
int dsa_port_set_state(struct dsa_port *dp, u8 state, bool do_fast_age);
int dsa_port_enable_rt(struct dsa_port *dp, struct phy_device *phy);
int dsa_port_enable(struct dsa_port *dp, struct phy_device *phy);
void dsa_port_disable_rt(struct dsa_port *dp);
void dsa_port_disable(struct dsa_port *dp);
int dsa_port_bridge_join(struct dsa_port *dp, struct net_device *br,
			 struct netlink_ext_ack *extack);
void dsa_port_pre_bridge_leave(struct dsa_port *dp, struct net_device *br);
void dsa_port_bridge_leave(struct dsa_port *dp, struct net_device *br);
int dsa_port_lag_change(struct dsa_port *dp,
			struct netdev_lag_lower_state_info *linfo);
int dsa_port_lag_join(struct dsa_port *dp, struct net_device *lag_dev,
		      struct netdev_lag_upper_info *uinfo,
		      struct netlink_ext_ack *extack);
void dsa_port_pre_lag_leave(struct dsa_port *dp, struct net_device *lag_dev);
void dsa_port_lag_leave(struct dsa_port *dp, struct net_device *lag_dev);
int dsa_port_vlan_filtering(struct dsa_port *dp, bool vlan_filtering,
			    struct netlink_ext_ack *extack);
bool dsa_port_skip_vlan_configuration(struct dsa_port *dp);
int dsa_port_ageing_time(struct dsa_port *dp, clock_t ageing_clock);
int dsa_port_mtu_change(struct dsa_port *dp, int new_mtu,
			bool targeted_match);
int dsa_port_fdb_add(struct dsa_port *dp, const unsigned char *addr,
		     u16 vid);
int dsa_port_fdb_del(struct dsa_port *dp, const unsigned char *addr,
		     u16 vid);
int dsa_port_host_fdb_add(struct dsa_port *dp, const unsigned char *addr,
			  u16 vid);
int dsa_port_host_fdb_del(struct dsa_port *dp, const unsigned char *addr,
			  u16 vid);
int dsa_port_fdb_dump(struct dsa_port *dp, dsa_fdb_dump_cb_t *cb, void *data);
int dsa_port_mdb_add(const struct dsa_port *dp,
		     const struct switchdev_obj_port_mdb *mdb);
int dsa_port_mdb_del(const struct dsa_port *dp,
		     const struct switchdev_obj_port_mdb *mdb);
int dsa_port_host_mdb_add(const struct dsa_port *dp,
			  const struct switchdev_obj_port_mdb *mdb);
int dsa_port_host_mdb_del(const struct dsa_port *dp,
			  const struct switchdev_obj_port_mdb *mdb);
int dsa_port_pre_bridge_flags(const struct dsa_port *dp,
			      struct switchdev_brport_flags flags,
			      struct netlink_ext_ack *extack);
int dsa_port_bridge_flags(struct dsa_port *dp,
			  struct switchdev_brport_flags flags,
			  struct netlink_ext_ack *extack);
int dsa_port_vlan_add(struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan,
		      struct netlink_ext_ack *extack);
int dsa_port_vlan_del(struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan);
int dsa_port_host_vlan_add(struct dsa_port *dp,
			   const struct switchdev_obj_port_vlan *vlan,
			   struct netlink_ext_ack *extack);
int dsa_port_host_vlan_del(struct dsa_port *dp,
			   const struct switchdev_obj_port_vlan *vlan);
int dsa_port_mrp_add(const struct dsa_port *dp,
		     const struct switchdev_obj_mrp *mrp);
int dsa_port_mrp_del(const struct dsa_port *dp,
		     const struct switchdev_obj_mrp *mrp);
int dsa_port_mrp_add_ring_role(const struct dsa_port *dp,
			       const struct switchdev_obj_ring_role_mrp *mrp);
int dsa_port_mrp_del_ring_role(const struct dsa_port *dp,
			       const struct switchdev_obj_ring_role_mrp *mrp);
int dsa_port_phylink_create(struct dsa_port *dp);
int dsa_port_link_register_of(struct dsa_port *dp);
void dsa_port_link_unregister_of(struct dsa_port *dp);
int dsa_port_hsr_join(struct dsa_port *dp, struct net_device *hsr);
void dsa_port_hsr_leave(struct dsa_port *dp, struct net_device *hsr);
int dsa_port_tag_8021q_vlan_add(struct dsa_port *dp, u16 vid, bool broadcast);
void dsa_port_tag_8021q_vlan_del(struct dsa_port *dp, u16 vid, bool broadcast);

/* slave.c */
extern const struct dsa_device_ops notag_netdev_ops;
extern struct notifier_block dsa_slave_switchdev_notifier;
extern struct notifier_block dsa_slave_switchdev_blocking_notifier;

void dsa_slave_mii_bus_init(struct dsa_switch *ds);
int dsa_slave_create(struct dsa_port *dp);
void dsa_slave_destroy(struct net_device *slave_dev);
int dsa_slave_suspend(struct net_device *slave_dev);
int dsa_slave_resume(struct net_device *slave_dev);
int dsa_slave_register_notifier(void);
void dsa_slave_unregister_notifier(void);
void dsa_slave_setup_tagger(struct net_device *slave);
int dsa_slave_change_mtu(struct net_device *dev, int new_mtu);
int dsa_slave_manage_vlan_filtering(struct net_device *dev,
				    bool vlan_filtering);

static inline struct dsa_port *dsa_slave_to_port(const struct net_device *dev)
{
	struct dsa_slave_priv *p = netdev_priv(dev);

	return p->dp;
}

static inline struct net_device *
dsa_slave_to_master(const struct net_device *dev)
{
	struct dsa_port *dp = dsa_slave_to_port(dev);

	return dp->cpu_dp->master;
}

/* If under a bridge with vlan_filtering=0, make sure to send pvid-tagged
 * frames as untagged, since the bridge will not untag them.
 */
static inline struct sk_buff *dsa_untag_bridge_pvid(struct sk_buff *skb)
{
	struct dsa_port *dp = dsa_slave_to_port(skb->dev);
	struct net_device *br = dsa_port_bridge_dev_get(dp);
	struct net_device *dev = skb->dev;
	struct net_device *upper_dev;
	u16 vid, pvid, proto;
	int err;

	if (!br || br_vlan_enabled(br))
		return skb;

	err = br_vlan_get_proto(br, &proto);
	if (err)
		return skb;

	/* Move VLAN tag from data to hwaccel */
	if (!skb_vlan_tag_present(skb) && skb->protocol == htons(proto)) {
		skb = skb_vlan_untag(skb);
		if (!skb)
			return NULL;
	}

	if (!skb_vlan_tag_present(skb))
		return skb;

	vid = skb_vlan_tag_get_id(skb);

	/* We already run under an RCU read-side critical section since
	 * we are called from netif_receive_skb_list_internal().
	 */
	err = br_vlan_get_pvid_rcu(dev, &pvid);
	if (err)
		return skb;

	if (vid != pvid)
		return skb;

	/* The sad part about attempting to untag from DSA is that we
	 * don't know, unless we check, if the skb will end up in
	 * the bridge's data path - br_allowed_ingress() - or not.
	 * For example, there might be an 8021q upper for the
	 * default_pvid of the bridge, which will steal VLAN-tagged traffic
	 * from the bridge's data path. This is a configuration that DSA
	 * supports because vlan_filtering is 0. In that case, we should
	 * definitely keep the tag, to make sure it keeps working.
	 */
	upper_dev = __vlan_find_dev_deep_rcu(br, htons(proto), vid);
	if (upper_dev)
		return skb;

	__vlan_hwaccel_clear_tag(skb);

	return skb;
}

/* For switches without hardware support for DSA tagging to be able
 * to support termination through the bridge.
 */
static inline struct net_device *
dsa_find_designated_bridge_port_by_vid(struct net_device *master, u16 vid)
{
	struct dsa_port *cpu_dp = master->dsa_ptr;
	struct dsa_switch_tree *dst = cpu_dp->dst;
	struct bridge_vlan_info vinfo;
	struct net_device *slave;
	struct dsa_port *dp;
	int err;

	list_for_each_entry(dp, &dst->ports, list) {
		if (dp->type != DSA_PORT_TYPE_USER)
			continue;

		if (!dp->bridge)
			continue;

		if (dp->stp_state != BR_STATE_LEARNING &&
		    dp->stp_state != BR_STATE_FORWARDING)
			continue;

		/* Since the bridge might learn this packet, keep the CPU port
		 * affinity with the port that will be used for the reply on
		 * xmit.
		 */
		if (dp->cpu_dp != cpu_dp)
			continue;

		slave = dp->slave;

		err = br_vlan_get_info_rcu(slave, vid, &vinfo);
		if (err)
			continue;

		return slave;
	}

	return NULL;
}

/* If the ingress port offloads the bridge, we mark the frame as autonomously
 * forwarded by hardware, so the software bridge doesn't forward in twice, back
 * to us, because we already did. However, if we're in fallback mode and we do
 * software bridging, we are not offloading it, therefore the dp->bridge
 * pointer is not populated, and flooding needs to be done by software (we are
 * effectively operating in standalone ports mode).
 */
static inline void dsa_default_offload_fwd_mark(struct sk_buff *skb)
{
	struct dsa_port *dp = dsa_slave_to_port(skb->dev);

	skb->offload_fwd_mark = !!(dp->bridge);
}

/* Helper for removing DSA header tags from packets in the RX path.
 * Must not be called before skb_pull(len).
 *                                                                 skb->data
 *                                                                         |
 *                                                                         v
 * |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
 * +-----------------------+-----------------------+---------------+-------+
 * |    Destination MAC    |      Source MAC       |  DSA header   | EType |
 * +-----------------------+-----------------------+---------------+-------+
 *                                                 |               |
 * <----- len ----->                               <----- len ----->
 *                 |
 *       >>>>>>>   v
 *       >>>>>>>   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
 *       >>>>>>>   +-----------------------+-----------------------+-------+
 *       >>>>>>>   |    Destination MAC    |      Source MAC       | EType |
 *                 +-----------------------+-----------------------+-------+
 *                                                                         ^
 *                                                                         |
 *                                                                 skb->data
 */
static inline void dsa_strip_etype_header(struct sk_buff *skb, int len)
{
	memmove(skb->data - ETH_HLEN, skb->data - ETH_HLEN - len, 2 * ETH_ALEN);
}

/* Helper for creating space for DSA header tags in TX path packets.
 * Must not be called before skb_push(len).
 *
 * Before:
 *
 *       <<<<<<<   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
 * ^     <<<<<<<   +-----------------------+-----------------------+-------+
 * |     <<<<<<<   |    Destination MAC    |      Source MAC       | EType |
 * |               +-----------------------+-----------------------+-------+
 * <----- len ----->
 * |
 * |
 * skb->data
 *
 * After:
 *
 * |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
 * +-----------------------+-----------------------+---------------+-------+
 * |    Destination MAC    |      Source MAC       |  DSA header   | EType |
 * +-----------------------+-----------------------+---------------+-------+
 * ^                                               |               |
 * |                                               <----- len ----->
 * skb->data
 */
static inline void dsa_alloc_etype_header(struct sk_buff *skb, int len)
{
	memmove(skb->data, skb->data + len, 2 * ETH_ALEN);
}

/* On RX, eth_type_trans() on the DSA master pulls ETH_HLEN bytes starting from
 * skb_mac_header(skb), which leaves skb->data pointing at the first byte after
 * what the DSA master perceives as the EtherType (the beginning of the L3
 * protocol). Since DSA EtherType header taggers treat the EtherType as part of
 * the DSA tag itself, and the EtherType is 2 bytes in length, the DSA header
 * is located 2 bytes behind skb->data. Note that EtherType in this context
 * means the first 2 bytes of the DSA header, not the encapsulated EtherType
 * that will become visible after the DSA header is stripped.
 */
static inline void *dsa_etype_header_pos_rx(struct sk_buff *skb)
{
	return skb->data - 2;
}

/* On TX, skb->data points to skb_mac_header(skb), which means that EtherType
 * header taggers start exactly where the EtherType is (the EtherType is
 * treated as part of the DSA header).
 */
static inline void *dsa_etype_header_pos_tx(struct sk_buff *skb)
{
	return skb->data + 2 * ETH_ALEN;
}

/* switch.c */
int dsa_switch_register_notifier(struct dsa_switch *ds);
void dsa_switch_unregister_notifier(struct dsa_switch *ds);

/* dsa2.c */
void dsa_lag_map(struct dsa_switch_tree *dst, struct net_device *lag);
void dsa_lag_unmap(struct dsa_switch_tree *dst, struct net_device *lag);
int dsa_tree_notify(struct dsa_switch_tree *dst, unsigned long e, void *v);
int dsa_broadcast(unsigned long e, void *v);
int dsa_tree_change_tag_proto(struct dsa_switch_tree *dst,
			      struct net_device *master,
			      const struct dsa_device_ops *tag_ops,
			      const struct dsa_device_ops *old_tag_ops);
void dsa_tree_master_admin_state_change(struct dsa_switch_tree *dst,
					struct net_device *master,
					bool up);
void dsa_tree_master_oper_state_change(struct dsa_switch_tree *dst,
				       struct net_device *master,
				       bool up);
unsigned int dsa_bridge_num_get(const struct net_device *bridge_dev, int max);
void dsa_bridge_num_put(const struct net_device *bridge_dev,
			unsigned int bridge_num);
struct dsa_bridge *dsa_tree_bridge_find(struct dsa_switch_tree *dst,
					const struct net_device *br);

/* tag_8021q.c */
int dsa_tag_8021q_bridge_join(struct dsa_switch *ds,
			      struct dsa_notifier_bridge_info *info);
int dsa_tag_8021q_bridge_leave(struct dsa_switch *ds,
			       struct dsa_notifier_bridge_info *info);
int dsa_switch_tag_8021q_vlan_add(struct dsa_switch *ds,
				  struct dsa_notifier_tag_8021q_vlan_info *info);
int dsa_switch_tag_8021q_vlan_del(struct dsa_switch *ds,
				  struct dsa_notifier_tag_8021q_vlan_info *info);

extern struct list_head dsa_tree_list;

#endif
