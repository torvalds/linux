/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_PORT_H
#define __DSA_PORT_H

#include <linux/types.h>
#include <net/dsa.h>

struct ifreq;
struct netdev_lag_lower_state_info;
struct netdev_lag_upper_info;
struct netlink_ext_ack;
struct switchdev_mst_state;
struct switchdev_obj_port_mdb;
struct switchdev_vlan_msti;
struct phy_device;

bool dsa_port_supports_hwtstamp(struct dsa_port *dp);
void dsa_port_set_tag_protocol(struct dsa_port *cpu_dp,
			       const struct dsa_device_ops *tag_ops);
int dsa_port_set_state(struct dsa_port *dp, u8 state, bool do_fast_age);
int dsa_port_set_mst_state(struct dsa_port *dp,
			   const struct switchdev_mst_state *state,
			   struct netlink_ext_ack *extack);
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
int dsa_port_mst_enable(struct dsa_port *dp, bool on,
			struct netlink_ext_ack *extack);
int dsa_port_vlan_msti(struct dsa_port *dp,
		       const struct switchdev_vlan_msti *msti);
int dsa_port_mtu_change(struct dsa_port *dp, int new_mtu);
int dsa_port_fdb_add(struct dsa_port *dp, const unsigned char *addr,
		     u16 vid);
int dsa_port_fdb_del(struct dsa_port *dp, const unsigned char *addr,
		     u16 vid);
int dsa_port_standalone_host_fdb_add(struct dsa_port *dp,
				     const unsigned char *addr, u16 vid);
int dsa_port_standalone_host_fdb_del(struct dsa_port *dp,
				     const unsigned char *addr, u16 vid);
int dsa_port_bridge_host_fdb_add(struct dsa_port *dp, const unsigned char *addr,
				 u16 vid);
int dsa_port_bridge_host_fdb_del(struct dsa_port *dp, const unsigned char *addr,
				 u16 vid);
int dsa_port_lag_fdb_add(struct dsa_port *dp, const unsigned char *addr,
			 u16 vid);
int dsa_port_lag_fdb_del(struct dsa_port *dp, const unsigned char *addr,
			 u16 vid);
int dsa_port_fdb_dump(struct dsa_port *dp, dsa_fdb_dump_cb_t *cb, void *data);
int dsa_port_mdb_add(const struct dsa_port *dp,
		     const struct switchdev_obj_port_mdb *mdb);
int dsa_port_mdb_del(const struct dsa_port *dp,
		     const struct switchdev_obj_port_mdb *mdb);
int dsa_port_standalone_host_mdb_add(const struct dsa_port *dp,
				     const struct switchdev_obj_port_mdb *mdb);
int dsa_port_standalone_host_mdb_del(const struct dsa_port *dp,
				     const struct switchdev_obj_port_mdb *mdb);
int dsa_port_bridge_host_mdb_add(const struct dsa_port *dp,
				 const struct switchdev_obj_port_mdb *mdb);
int dsa_port_bridge_host_mdb_del(const struct dsa_port *dp,
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
void dsa_port_phylink_destroy(struct dsa_port *dp);
int dsa_shared_port_link_register_of(struct dsa_port *dp);
void dsa_shared_port_link_unregister_of(struct dsa_port *dp);
int dsa_port_hsr_join(struct dsa_port *dp, struct net_device *hsr,
		      struct netlink_ext_ack *extack);
void dsa_port_hsr_leave(struct dsa_port *dp, struct net_device *hsr);
int dsa_port_tag_8021q_vlan_add(struct dsa_port *dp, u16 vid, bool broadcast);
void dsa_port_tag_8021q_vlan_del(struct dsa_port *dp, u16 vid, bool broadcast);
void dsa_port_set_host_flood(struct dsa_port *dp, bool uc, bool mc);
int dsa_port_change_conduit(struct dsa_port *dp, struct net_device *conduit,
			    struct netlink_ext_ack *extack);

#endif
