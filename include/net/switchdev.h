/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * include/net/switchdev.h - Switch device API
 * Copyright (c) 2014-2015 Jiri Pirko <jiri@resnulli.us>
 * Copyright (c) 2014-2015 Scott Feldman <sfeldma@gmail.com>
 */
#ifndef _LINUX_SWITCHDEV_H_
#define _LINUX_SWITCHDEV_H_

#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/list.h>
#include <net/ip_fib.h>

#define SWITCHDEV_F_NO_RECURSE		BIT(0)
#define SWITCHDEV_F_SKIP_EOPNOTSUPP	BIT(1)
#define SWITCHDEV_F_DEFER		BIT(2)

enum switchdev_attr_id {
	SWITCHDEV_ATTR_ID_UNDEFINED,
	SWITCHDEV_ATTR_ID_PORT_STP_STATE,
	SWITCHDEV_ATTR_ID_PORT_MST_STATE,
	SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS,
	SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS,
	SWITCHDEV_ATTR_ID_PORT_MROUTER,
	SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME,
	SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING,
	SWITCHDEV_ATTR_ID_BRIDGE_VLAN_PROTOCOL,
	SWITCHDEV_ATTR_ID_BRIDGE_MC_DISABLED,
	SWITCHDEV_ATTR_ID_BRIDGE_MROUTER,
	SWITCHDEV_ATTR_ID_BRIDGE_MST,
	SWITCHDEV_ATTR_ID_MRP_PORT_ROLE,
	SWITCHDEV_ATTR_ID_VLAN_MSTI,
};

struct switchdev_mst_state {
	u16 msti;
	u8 state;
};

struct switchdev_brport_flags {
	unsigned long val;
	unsigned long mask;
};

struct switchdev_vlan_msti {
	u16 vid;
	u16 msti;
};

struct switchdev_attr {
	struct net_device *orig_dev;
	enum switchdev_attr_id id;
	u32 flags;
	void *complete_priv;
	void (*complete)(struct net_device *dev, int err, void *priv);
	union {
		u8 stp_state;				/* PORT_STP_STATE */
		struct switchdev_mst_state mst_state;	/* PORT_MST_STATE */
		struct switchdev_brport_flags brport_flags; /* PORT_BRIDGE_FLAGS */
		bool mrouter;				/* PORT_MROUTER */
		clock_t ageing_time;			/* BRIDGE_AGEING_TIME */
		bool vlan_filtering;			/* BRIDGE_VLAN_FILTERING */
		u16 vlan_protocol;			/* BRIDGE_VLAN_PROTOCOL */
		bool mst;				/* BRIDGE_MST */
		bool mc_disabled;			/* MC_DISABLED */
		u8 mrp_port_role;			/* MRP_PORT_ROLE */
		struct switchdev_vlan_msti vlan_msti;	/* VLAN_MSTI */
	} u;
};

enum switchdev_obj_id {
	SWITCHDEV_OBJ_ID_UNDEFINED,
	SWITCHDEV_OBJ_ID_PORT_VLAN,
	SWITCHDEV_OBJ_ID_PORT_MDB,
	SWITCHDEV_OBJ_ID_HOST_MDB,
	SWITCHDEV_OBJ_ID_MRP,
	SWITCHDEV_OBJ_ID_RING_TEST_MRP,
	SWITCHDEV_OBJ_ID_RING_ROLE_MRP,
	SWITCHDEV_OBJ_ID_RING_STATE_MRP,
	SWITCHDEV_OBJ_ID_IN_TEST_MRP,
	SWITCHDEV_OBJ_ID_IN_ROLE_MRP,
	SWITCHDEV_OBJ_ID_IN_STATE_MRP,
};

struct switchdev_obj {
	struct list_head list;
	struct net_device *orig_dev;
	enum switchdev_obj_id id;
	u32 flags;
	void *complete_priv;
	void (*complete)(struct net_device *dev, int err, void *priv);
};

/* SWITCHDEV_OBJ_ID_PORT_VLAN */
struct switchdev_obj_port_vlan {
	struct switchdev_obj obj;
	u16 flags;
	u16 vid;
	/* If set, the notifier signifies a change of one of the following
	 * flags for a VLAN that already exists:
	 * - BRIDGE_VLAN_INFO_PVID
	 * - BRIDGE_VLAN_INFO_UNTAGGED
	 * Entries with BRIDGE_VLAN_INFO_BRENTRY unset are not notified at all.
	 */
	bool changed;
};

#define SWITCHDEV_OBJ_PORT_VLAN(OBJ) \
	container_of((OBJ), struct switchdev_obj_port_vlan, obj)

/* SWITCHDEV_OBJ_ID_PORT_MDB */
struct switchdev_obj_port_mdb {
	struct switchdev_obj obj;
	unsigned char addr[ETH_ALEN];
	u16 vid;
};

#define SWITCHDEV_OBJ_PORT_MDB(OBJ) \
	container_of((OBJ), struct switchdev_obj_port_mdb, obj)


/* SWITCHDEV_OBJ_ID_MRP */
struct switchdev_obj_mrp {
	struct switchdev_obj obj;
	struct net_device *p_port;
	struct net_device *s_port;
	u32 ring_id;
	u16 prio;
};

#define SWITCHDEV_OBJ_MRP(OBJ) \
	container_of((OBJ), struct switchdev_obj_mrp, obj)

/* SWITCHDEV_OBJ_ID_RING_TEST_MRP */
struct switchdev_obj_ring_test_mrp {
	struct switchdev_obj obj;
	/* The value is in us and a value of 0 represents to stop */
	u32 interval;
	u8 max_miss;
	u32 ring_id;
	u32 period;
	bool monitor;
};

#define SWITCHDEV_OBJ_RING_TEST_MRP(OBJ) \
	container_of((OBJ), struct switchdev_obj_ring_test_mrp, obj)

/* SWICHDEV_OBJ_ID_RING_ROLE_MRP */
struct switchdev_obj_ring_role_mrp {
	struct switchdev_obj obj;
	u8 ring_role;
	u32 ring_id;
	u8 sw_backup;
};

#define SWITCHDEV_OBJ_RING_ROLE_MRP(OBJ) \
	container_of((OBJ), struct switchdev_obj_ring_role_mrp, obj)

struct switchdev_obj_ring_state_mrp {
	struct switchdev_obj obj;
	u8 ring_state;
	u32 ring_id;
};

#define SWITCHDEV_OBJ_RING_STATE_MRP(OBJ) \
	container_of((OBJ), struct switchdev_obj_ring_state_mrp, obj)

/* SWITCHDEV_OBJ_ID_IN_TEST_MRP */
struct switchdev_obj_in_test_mrp {
	struct switchdev_obj obj;
	/* The value is in us and a value of 0 represents to stop */
	u32 interval;
	u32 in_id;
	u32 period;
	u8 max_miss;
};

#define SWITCHDEV_OBJ_IN_TEST_MRP(OBJ) \
	container_of((OBJ), struct switchdev_obj_in_test_mrp, obj)

/* SWICHDEV_OBJ_ID_IN_ROLE_MRP */
struct switchdev_obj_in_role_mrp {
	struct switchdev_obj obj;
	struct net_device *i_port;
	u32 ring_id;
	u16 in_id;
	u8 in_role;
	u8 sw_backup;
};

#define SWITCHDEV_OBJ_IN_ROLE_MRP(OBJ) \
	container_of((OBJ), struct switchdev_obj_in_role_mrp, obj)

struct switchdev_obj_in_state_mrp {
	struct switchdev_obj obj;
	u32 in_id;
	u8 in_state;
};

#define SWITCHDEV_OBJ_IN_STATE_MRP(OBJ) \
	container_of((OBJ), struct switchdev_obj_in_state_mrp, obj)

typedef int switchdev_obj_dump_cb_t(struct switchdev_obj *obj);

struct switchdev_brport {
	struct net_device *dev;
	const void *ctx;
	struct notifier_block *atomic_nb;
	struct notifier_block *blocking_nb;
	bool tx_fwd_offload;
};

enum switchdev_notifier_type {
	SWITCHDEV_FDB_ADD_TO_BRIDGE = 1,
	SWITCHDEV_FDB_DEL_TO_BRIDGE,
	SWITCHDEV_FDB_ADD_TO_DEVICE,
	SWITCHDEV_FDB_DEL_TO_DEVICE,
	SWITCHDEV_FDB_OFFLOADED,
	SWITCHDEV_FDB_FLUSH_TO_BRIDGE,

	SWITCHDEV_PORT_OBJ_ADD, /* Blocking. */
	SWITCHDEV_PORT_OBJ_DEL, /* Blocking. */
	SWITCHDEV_PORT_ATTR_SET, /* May be blocking . */

	SWITCHDEV_VXLAN_FDB_ADD_TO_BRIDGE,
	SWITCHDEV_VXLAN_FDB_DEL_TO_BRIDGE,
	SWITCHDEV_VXLAN_FDB_ADD_TO_DEVICE,
	SWITCHDEV_VXLAN_FDB_DEL_TO_DEVICE,
	SWITCHDEV_VXLAN_FDB_OFFLOADED,

	SWITCHDEV_BRPORT_OFFLOADED,
	SWITCHDEV_BRPORT_UNOFFLOADED,
};

struct switchdev_notifier_info {
	struct net_device *dev;
	struct netlink_ext_ack *extack;
	const void *ctx;
};

/* Remember to update br_switchdev_fdb_populate() when adding
 * new members to this structure
 */
struct switchdev_notifier_fdb_info {
	struct switchdev_notifier_info info; /* must be first */
	const unsigned char *addr;
	u16 vid;
	u8 added_by_user:1,
	   is_local:1,
	   offloaded:1;
};

struct switchdev_notifier_port_obj_info {
	struct switchdev_notifier_info info; /* must be first */
	const struct switchdev_obj *obj;
	bool handled;
};

struct switchdev_notifier_port_attr_info {
	struct switchdev_notifier_info info; /* must be first */
	const struct switchdev_attr *attr;
	bool handled;
};

struct switchdev_notifier_brport_info {
	struct switchdev_notifier_info info; /* must be first */
	const struct switchdev_brport brport;
};

static inline struct net_device *
switchdev_notifier_info_to_dev(const struct switchdev_notifier_info *info)
{
	return info->dev;
}

static inline struct netlink_ext_ack *
switchdev_notifier_info_to_extack(const struct switchdev_notifier_info *info)
{
	return info->extack;
}

static inline bool
switchdev_fdb_is_dynamically_learned(const struct switchdev_notifier_fdb_info *fdb_info)
{
	return !fdb_info->added_by_user && !fdb_info->is_local;
}

#ifdef CONFIG_NET_SWITCHDEV

int switchdev_bridge_port_offload(struct net_device *brport_dev,
				  struct net_device *dev, const void *ctx,
				  struct notifier_block *atomic_nb,
				  struct notifier_block *blocking_nb,
				  bool tx_fwd_offload,
				  struct netlink_ext_ack *extack);
void switchdev_bridge_port_unoffload(struct net_device *brport_dev,
				     const void *ctx,
				     struct notifier_block *atomic_nb,
				     struct notifier_block *blocking_nb);

void switchdev_deferred_process(void);
int switchdev_port_attr_set(struct net_device *dev,
			    const struct switchdev_attr *attr,
			    struct netlink_ext_ack *extack);
bool switchdev_port_obj_act_is_deferred(struct net_device *dev,
					enum switchdev_notifier_type nt,
					const struct switchdev_obj *obj);
int switchdev_port_obj_add(struct net_device *dev,
			   const struct switchdev_obj *obj,
			   struct netlink_ext_ack *extack);
int switchdev_port_obj_del(struct net_device *dev,
			   const struct switchdev_obj *obj);

int register_switchdev_notifier(struct notifier_block *nb);
int unregister_switchdev_notifier(struct notifier_block *nb);
int call_switchdev_notifiers(unsigned long val, struct net_device *dev,
			     struct switchdev_notifier_info *info,
			     struct netlink_ext_ack *extack);

int register_switchdev_blocking_notifier(struct notifier_block *nb);
int unregister_switchdev_blocking_notifier(struct notifier_block *nb);
int call_switchdev_blocking_notifiers(unsigned long val, struct net_device *dev,
				      struct switchdev_notifier_info *info,
				      struct netlink_ext_ack *extack);

void switchdev_port_fwd_mark_set(struct net_device *dev,
				 struct net_device *group_dev,
				 bool joining);

int switchdev_handle_fdb_event_to_device(struct net_device *dev, unsigned long event,
		const struct switchdev_notifier_fdb_info *fdb_info,
		bool (*check_cb)(const struct net_device *dev),
		bool (*foreign_dev_check_cb)(const struct net_device *dev,
					     const struct net_device *foreign_dev),
		int (*mod_cb)(struct net_device *dev, struct net_device *orig_dev,
			      unsigned long event, const void *ctx,
			      const struct switchdev_notifier_fdb_info *fdb_info));

int switchdev_handle_port_obj_add(struct net_device *dev,
			struct switchdev_notifier_port_obj_info *port_obj_info,
			bool (*check_cb)(const struct net_device *dev),
			int (*add_cb)(struct net_device *dev, const void *ctx,
				      const struct switchdev_obj *obj,
				      struct netlink_ext_ack *extack));
int switchdev_handle_port_obj_add_foreign(struct net_device *dev,
			struct switchdev_notifier_port_obj_info *port_obj_info,
			bool (*check_cb)(const struct net_device *dev),
			bool (*foreign_dev_check_cb)(const struct net_device *dev,
						     const struct net_device *foreign_dev),
			int (*add_cb)(struct net_device *dev, const void *ctx,
				      const struct switchdev_obj *obj,
				      struct netlink_ext_ack *extack));
int switchdev_handle_port_obj_del(struct net_device *dev,
			struct switchdev_notifier_port_obj_info *port_obj_info,
			bool (*check_cb)(const struct net_device *dev),
			int (*del_cb)(struct net_device *dev, const void *ctx,
				      const struct switchdev_obj *obj));
int switchdev_handle_port_obj_del_foreign(struct net_device *dev,
			struct switchdev_notifier_port_obj_info *port_obj_info,
			bool (*check_cb)(const struct net_device *dev),
			bool (*foreign_dev_check_cb)(const struct net_device *dev,
						     const struct net_device *foreign_dev),
			int (*del_cb)(struct net_device *dev, const void *ctx,
				      const struct switchdev_obj *obj));

int switchdev_handle_port_attr_set(struct net_device *dev,
			struct switchdev_notifier_port_attr_info *port_attr_info,
			bool (*check_cb)(const struct net_device *dev),
			int (*set_cb)(struct net_device *dev, const void *ctx,
				      const struct switchdev_attr *attr,
				      struct netlink_ext_ack *extack));
#else

static inline int
switchdev_bridge_port_offload(struct net_device *brport_dev,
			      struct net_device *dev, const void *ctx,
			      struct notifier_block *atomic_nb,
			      struct notifier_block *blocking_nb,
			      bool tx_fwd_offload,
			      struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static inline void
switchdev_bridge_port_unoffload(struct net_device *brport_dev,
				const void *ctx,
				struct notifier_block *atomic_nb,
				struct notifier_block *blocking_nb)
{
}

static inline void switchdev_deferred_process(void)
{
}

static inline int switchdev_port_attr_set(struct net_device *dev,
					  const struct switchdev_attr *attr,
					  struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_obj_add(struct net_device *dev,
					 const struct switchdev_obj *obj,
					 struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_obj_del(struct net_device *dev,
					 const struct switchdev_obj *obj)
{
	return -EOPNOTSUPP;
}

static inline int register_switchdev_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int unregister_switchdev_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int call_switchdev_notifiers(unsigned long val,
					   struct net_device *dev,
					   struct switchdev_notifier_info *info,
					   struct netlink_ext_ack *extack)
{
	return NOTIFY_DONE;
}

static inline int
register_switchdev_blocking_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int
unregister_switchdev_blocking_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int
call_switchdev_blocking_notifiers(unsigned long val,
				  struct net_device *dev,
				  struct switchdev_notifier_info *info,
				  struct netlink_ext_ack *extack)
{
	return NOTIFY_DONE;
}

static inline int
switchdev_handle_fdb_event_to_device(struct net_device *dev, unsigned long event,
		const struct switchdev_notifier_fdb_info *fdb_info,
		bool (*check_cb)(const struct net_device *dev),
		bool (*foreign_dev_check_cb)(const struct net_device *dev,
					     const struct net_device *foreign_dev),
		int (*mod_cb)(struct net_device *dev, struct net_device *orig_dev,
			      unsigned long event, const void *ctx,
			      const struct switchdev_notifier_fdb_info *fdb_info))
{
	return 0;
}

static inline int
switchdev_handle_port_obj_add(struct net_device *dev,
			struct switchdev_notifier_port_obj_info *port_obj_info,
			bool (*check_cb)(const struct net_device *dev),
			int (*add_cb)(struct net_device *dev, const void *ctx,
				      const struct switchdev_obj *obj,
				      struct netlink_ext_ack *extack))
{
	return 0;
}

static inline int switchdev_handle_port_obj_add_foreign(struct net_device *dev,
			struct switchdev_notifier_port_obj_info *port_obj_info,
			bool (*check_cb)(const struct net_device *dev),
			bool (*foreign_dev_check_cb)(const struct net_device *dev,
						     const struct net_device *foreign_dev),
			int (*add_cb)(struct net_device *dev, const void *ctx,
				      const struct switchdev_obj *obj,
				      struct netlink_ext_ack *extack))
{
	return 0;
}

static inline int
switchdev_handle_port_obj_del(struct net_device *dev,
			struct switchdev_notifier_port_obj_info *port_obj_info,
			bool (*check_cb)(const struct net_device *dev),
			int (*del_cb)(struct net_device *dev, const void *ctx,
				      const struct switchdev_obj *obj))
{
	return 0;
}

static inline int
switchdev_handle_port_obj_del_foreign(struct net_device *dev,
			struct switchdev_notifier_port_obj_info *port_obj_info,
			bool (*check_cb)(const struct net_device *dev),
			bool (*foreign_dev_check_cb)(const struct net_device *dev,
						     const struct net_device *foreign_dev),
			int (*del_cb)(struct net_device *dev, const void *ctx,
				      const struct switchdev_obj *obj))
{
	return 0;
}

static inline int
switchdev_handle_port_attr_set(struct net_device *dev,
			struct switchdev_notifier_port_attr_info *port_attr_info,
			bool (*check_cb)(const struct net_device *dev),
			int (*set_cb)(struct net_device *dev, const void *ctx,
				      const struct switchdev_attr *attr,
				      struct netlink_ext_ack *extack))
{
	return 0;
}
#endif

#endif /* _LINUX_SWITCHDEV_H_ */
