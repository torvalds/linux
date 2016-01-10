/*
 * include/net/switchdev.h - Switch device API
 * Copyright (c) 2014-2015 Jiri Pirko <jiri@resnulli.us>
 * Copyright (c) 2014-2015 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

struct switchdev_trans_item {
	struct list_head list;
	void *data;
	void (*destructor)(const void *data);
};

struct switchdev_trans {
	struct list_head item_list;
	bool ph_prepare;
};

static inline bool switchdev_trans_ph_prepare(struct switchdev_trans *trans)
{
	return trans && trans->ph_prepare;
}

static inline bool switchdev_trans_ph_commit(struct switchdev_trans *trans)
{
	return trans && !trans->ph_prepare;
}

enum switchdev_attr_id {
	SWITCHDEV_ATTR_ID_UNDEFINED,
	SWITCHDEV_ATTR_ID_PORT_PARENT_ID,
	SWITCHDEV_ATTR_ID_PORT_STP_STATE,
	SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS,
	SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME,
	SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING,
};

struct switchdev_attr {
	struct net_device *orig_dev;
	enum switchdev_attr_id id;
	u32 flags;
	union {
		struct netdev_phys_item_id ppid;	/* PORT_PARENT_ID */
		u8 stp_state;				/* PORT_STP_STATE */
		unsigned long brport_flags;		/* PORT_BRIDGE_FLAGS */
		u32 ageing_time;			/* BRIDGE_AGEING_TIME */
		bool vlan_filtering;			/* BRIDGE_VLAN_FILTERING */
	} u;
};

enum switchdev_obj_id {
	SWITCHDEV_OBJ_ID_UNDEFINED,
	SWITCHDEV_OBJ_ID_PORT_VLAN,
	SWITCHDEV_OBJ_ID_IPV4_FIB,
	SWITCHDEV_OBJ_ID_PORT_FDB,
	SWITCHDEV_OBJ_ID_PORT_MDB,
};

struct switchdev_obj {
	struct net_device *orig_dev;
	enum switchdev_obj_id id;
	u32 flags;
};

/* SWITCHDEV_OBJ_ID_PORT_VLAN */
struct switchdev_obj_port_vlan {
	struct switchdev_obj obj;
	u16 flags;
	u16 vid_begin;
	u16 vid_end;
};

#define SWITCHDEV_OBJ_PORT_VLAN(obj) \
	container_of(obj, struct switchdev_obj_port_vlan, obj)

/* SWITCHDEV_OBJ_ID_IPV4_FIB */
struct switchdev_obj_ipv4_fib {
	struct switchdev_obj obj;
	u32 dst;
	int dst_len;
	struct fib_info fi;
	u8 tos;
	u8 type;
	u32 nlflags;
	u32 tb_id;
};

#define SWITCHDEV_OBJ_IPV4_FIB(obj) \
	container_of(obj, struct switchdev_obj_ipv4_fib, obj)

/* SWITCHDEV_OBJ_ID_PORT_FDB */
struct switchdev_obj_port_fdb {
	struct switchdev_obj obj;
	unsigned char addr[ETH_ALEN];
	u16 vid;
	u16 ndm_state;
};

#define SWITCHDEV_OBJ_PORT_FDB(obj) \
	container_of(obj, struct switchdev_obj_port_fdb, obj)

/* SWITCHDEV_OBJ_ID_PORT_MDB */
struct switchdev_obj_port_mdb {
	struct switchdev_obj obj;
	unsigned char addr[ETH_ALEN];
	u16 vid;
};

#define SWITCHDEV_OBJ_PORT_MDB(obj) \
	container_of(obj, struct switchdev_obj_port_mdb, obj)

void switchdev_trans_item_enqueue(struct switchdev_trans *trans,
				  void *data, void (*destructor)(void const *),
				  struct switchdev_trans_item *tritem);
void *switchdev_trans_item_dequeue(struct switchdev_trans *trans);

typedef int switchdev_obj_dump_cb_t(struct switchdev_obj *obj);

/**
 * struct switchdev_ops - switchdev operations
 *
 * @switchdev_port_attr_get: Get a port attribute (see switchdev_attr).
 *
 * @switchdev_port_attr_set: Set a port attribute (see switchdev_attr).
 *
 * @switchdev_port_obj_add: Add an object to port (see switchdev_obj_*).
 *
 * @switchdev_port_obj_del: Delete an object from port (see switchdev_obj_*).
 *
 * @switchdev_port_obj_dump: Dump port objects (see switchdev_obj_*).
 */
struct switchdev_ops {
	int	(*switchdev_port_attr_get)(struct net_device *dev,
					   struct switchdev_attr *attr);
	int	(*switchdev_port_attr_set)(struct net_device *dev,
					   const struct switchdev_attr *attr,
					   struct switchdev_trans *trans);
	int	(*switchdev_port_obj_add)(struct net_device *dev,
					  const struct switchdev_obj *obj,
					  struct switchdev_trans *trans);
	int	(*switchdev_port_obj_del)(struct net_device *dev,
					  const struct switchdev_obj *obj);
	int	(*switchdev_port_obj_dump)(struct net_device *dev,
					   struct switchdev_obj *obj,
					   switchdev_obj_dump_cb_t *cb);
};

enum switchdev_notifier_type {
	SWITCHDEV_FDB_ADD = 1,
	SWITCHDEV_FDB_DEL,
};

struct switchdev_notifier_info {
	struct net_device *dev;
};

struct switchdev_notifier_fdb_info {
	struct switchdev_notifier_info info; /* must be first */
	const unsigned char *addr;
	u16 vid;
};

static inline struct net_device *
switchdev_notifier_info_to_dev(const struct switchdev_notifier_info *info)
{
	return info->dev;
}

#ifdef CONFIG_NET_SWITCHDEV

void switchdev_deferred_process(void);
int switchdev_port_attr_get(struct net_device *dev,
			    struct switchdev_attr *attr);
int switchdev_port_attr_set(struct net_device *dev,
			    const struct switchdev_attr *attr);
int switchdev_port_obj_add(struct net_device *dev,
			   const struct switchdev_obj *obj);
int switchdev_port_obj_del(struct net_device *dev,
			   const struct switchdev_obj *obj);
int switchdev_port_obj_dump(struct net_device *dev, struct switchdev_obj *obj,
			    switchdev_obj_dump_cb_t *cb);
int register_switchdev_notifier(struct notifier_block *nb);
int unregister_switchdev_notifier(struct notifier_block *nb);
int call_switchdev_notifiers(unsigned long val, struct net_device *dev,
			     struct switchdev_notifier_info *info);
int switchdev_port_bridge_getlink(struct sk_buff *skb, u32 pid, u32 seq,
				  struct net_device *dev, u32 filter_mask,
				  int nlflags);
int switchdev_port_bridge_setlink(struct net_device *dev,
				  struct nlmsghdr *nlh, u16 flags);
int switchdev_port_bridge_dellink(struct net_device *dev,
				  struct nlmsghdr *nlh, u16 flags);
int switchdev_fib_ipv4_add(u32 dst, int dst_len, struct fib_info *fi,
			   u8 tos, u8 type, u32 nlflags, u32 tb_id);
int switchdev_fib_ipv4_del(u32 dst, int dst_len, struct fib_info *fi,
			   u8 tos, u8 type, u32 tb_id);
void switchdev_fib_ipv4_abort(struct fib_info *fi);
int switchdev_port_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			   struct net_device *dev, const unsigned char *addr,
			   u16 vid, u16 nlm_flags);
int switchdev_port_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
			   struct net_device *dev, const unsigned char *addr,
			   u16 vid);
int switchdev_port_fdb_dump(struct sk_buff *skb, struct netlink_callback *cb,
			    struct net_device *dev,
			    struct net_device *filter_dev, int idx);
void switchdev_port_fwd_mark_set(struct net_device *dev,
				 struct net_device *group_dev,
				 bool joining);

#else

static inline void switchdev_deferred_process(void)
{
}

static inline int switchdev_port_attr_get(struct net_device *dev,
					  struct switchdev_attr *attr)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_attr_set(struct net_device *dev,
					  const struct switchdev_attr *attr)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_obj_add(struct net_device *dev,
					 const struct switchdev_obj *obj)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_obj_del(struct net_device *dev,
					 const struct switchdev_obj *obj)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_obj_dump(struct net_device *dev,
					  const struct switchdev_obj *obj,
					  switchdev_obj_dump_cb_t *cb)
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
					   struct switchdev_notifier_info *info)
{
	return NOTIFY_DONE;
}

static inline int switchdev_port_bridge_getlink(struct sk_buff *skb, u32 pid,
					    u32 seq, struct net_device *dev,
					    u32 filter_mask, int nlflags)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_bridge_setlink(struct net_device *dev,
						struct nlmsghdr *nlh,
						u16 flags)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_bridge_dellink(struct net_device *dev,
						struct nlmsghdr *nlh,
						u16 flags)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_fib_ipv4_add(u32 dst, int dst_len,
					 struct fib_info *fi,
					 u8 tos, u8 type,
					 u32 nlflags, u32 tb_id)
{
	return 0;
}

static inline int switchdev_fib_ipv4_del(u32 dst, int dst_len,
					 struct fib_info *fi,
					 u8 tos, u8 type, u32 tb_id)
{
	return 0;
}

static inline void switchdev_fib_ipv4_abort(struct fib_info *fi)
{
}

static inline int switchdev_port_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
					 struct net_device *dev,
					 const unsigned char *addr,
					 u16 vid, u16 nlm_flags)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
					 struct net_device *dev,
					 const unsigned char *addr, u16 vid)
{
	return -EOPNOTSUPP;
}

static inline int switchdev_port_fdb_dump(struct sk_buff *skb,
					  struct netlink_callback *cb,
					  struct net_device *dev,
					  struct net_device *filter_dev,
					  int idx)
{
       return idx;
}

static inline void switchdev_port_fwd_mark_set(struct net_device *dev,
					       struct net_device *group_dev,
					       bool joining)
{
}

#endif

#endif /* _LINUX_SWITCHDEV_H_ */
