/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2022-2023 NXP
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM	dsa

#if !defined(_NET_DSA_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _NET_DSA_TRACE_H

#include <net/dsa.h>
#include <net/switchdev.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/refcount.h>
#include <linux/tracepoint.h>

/* Enough to fit "bridge %s num %d" where num has 3 digits */
#define DSA_DB_BUFSIZ	(IFNAMSIZ + 16)

void dsa_db_print(const struct dsa_db *db, char buf[DSA_DB_BUFSIZ]);
const char *dsa_port_kind(const struct dsa_port *dp);

DECLARE_EVENT_CLASS(dsa_port_addr_op_hw,

	TP_PROTO(const struct dsa_port *dp, const unsigned char *addr, u16 vid,
		 const struct dsa_db *db, int err),

	TP_ARGS(dp, addr, vid, db, err),

	TP_STRUCT__entry(
		__string(dev, dev_name(dp->ds->dev))
		__string(kind, dsa_port_kind(dp))
		__field(int, port)
		__array(unsigned char, addr, ETH_ALEN)
		__field(u16, vid)
		__array(char, db_buf, DSA_DB_BUFSIZ)
		__field(int, err)
	),

	TP_fast_assign(
		__assign_str(dev);
		__assign_str(kind);
		__entry->port = dp->index;
		ether_addr_copy(__entry->addr, addr);
		__entry->vid = vid;
		dsa_db_print(db, __entry->db_buf);
		__entry->err = err;
	),

	TP_printk("%s %s port %d addr %pM vid %u db \"%s\" err %d",
		  __get_str(dev), __get_str(kind), __entry->port, __entry->addr,
		  __entry->vid, __entry->db_buf, __entry->err)
);

/* Add unicast/multicast address to hardware, either on user ports
 * (where no refcounting is kept), or on shared ports when the entry
 * is first seen and its refcount is 1.
 */
DEFINE_EVENT(dsa_port_addr_op_hw, dsa_fdb_add_hw,
	     TP_PROTO(const struct dsa_port *dp, const unsigned char *addr,
		      u16 vid, const struct dsa_db *db, int err),
	     TP_ARGS(dp, addr, vid, db, err));

DEFINE_EVENT(dsa_port_addr_op_hw, dsa_mdb_add_hw,
	     TP_PROTO(const struct dsa_port *dp, const unsigned char *addr,
		      u16 vid, const struct dsa_db *db, int err),
	     TP_ARGS(dp, addr, vid, db, err));

/* Delete unicast/multicast address from hardware, either on user ports or
 * when the refcount on shared ports reaches 0
 */
DEFINE_EVENT(dsa_port_addr_op_hw, dsa_fdb_del_hw,
	     TP_PROTO(const struct dsa_port *dp, const unsigned char *addr,
		      u16 vid, const struct dsa_db *db, int err),
	     TP_ARGS(dp, addr, vid, db, err));

DEFINE_EVENT(dsa_port_addr_op_hw, dsa_mdb_del_hw,
	     TP_PROTO(const struct dsa_port *dp, const unsigned char *addr,
		      u16 vid, const struct dsa_db *db, int err),
	     TP_ARGS(dp, addr, vid, db, err));

DECLARE_EVENT_CLASS(dsa_port_addr_op_refcount,

	TP_PROTO(const struct dsa_port *dp, const unsigned char *addr, u16 vid,
		 const struct dsa_db *db, const refcount_t *refcount),

	TP_ARGS(dp, addr, vid, db, refcount),

	TP_STRUCT__entry(
		__string(dev, dev_name(dp->ds->dev))
		__string(kind, dsa_port_kind(dp))
		__field(int, port)
		__array(unsigned char, addr, ETH_ALEN)
		__field(u16, vid)
		__array(char, db_buf, DSA_DB_BUFSIZ)
		__field(unsigned int, refcount)
	),

	TP_fast_assign(
		__assign_str(dev);
		__assign_str(kind);
		__entry->port = dp->index;
		ether_addr_copy(__entry->addr, addr);
		__entry->vid = vid;
		dsa_db_print(db, __entry->db_buf);
		__entry->refcount = refcount_read(refcount);
	),

	TP_printk("%s %s port %d addr %pM vid %u db \"%s\" refcount %u",
		  __get_str(dev), __get_str(kind), __entry->port, __entry->addr,
		  __entry->vid, __entry->db_buf, __entry->refcount)
);

/* Bump the refcount of an existing unicast/multicast address on shared ports */
DEFINE_EVENT(dsa_port_addr_op_refcount, dsa_fdb_add_bump,
	     TP_PROTO(const struct dsa_port *dp, const unsigned char *addr,
		      u16 vid, const struct dsa_db *db,
		      const refcount_t *refcount),
	     TP_ARGS(dp, addr, vid, db, refcount));

DEFINE_EVENT(dsa_port_addr_op_refcount, dsa_mdb_add_bump,
	     TP_PROTO(const struct dsa_port *dp, const unsigned char *addr,
		      u16 vid, const struct dsa_db *db,
		      const refcount_t *refcount),
	     TP_ARGS(dp, addr, vid, db, refcount));

/* Drop the refcount of a multicast address that we still keep on
 * shared ports
 */
DEFINE_EVENT(dsa_port_addr_op_refcount, dsa_fdb_del_drop,
	     TP_PROTO(const struct dsa_port *dp, const unsigned char *addr,
		      u16 vid, const struct dsa_db *db,
		      const refcount_t *refcount),
	     TP_ARGS(dp, addr, vid, db, refcount));

DEFINE_EVENT(dsa_port_addr_op_refcount, dsa_mdb_del_drop,
	     TP_PROTO(const struct dsa_port *dp, const unsigned char *addr,
		      u16 vid, const struct dsa_db *db,
		      const refcount_t *refcount),
	     TP_ARGS(dp, addr, vid, db, refcount));

DECLARE_EVENT_CLASS(dsa_port_addr_del_not_found,

	TP_PROTO(const struct dsa_port *dp, const unsigned char *addr, u16 vid,
		 const struct dsa_db *db),

	TP_ARGS(dp, addr, vid, db),

	TP_STRUCT__entry(
		__string(dev, dev_name(dp->ds->dev))
		__string(kind, dsa_port_kind(dp))
		__field(int, port)
		__array(unsigned char, addr, ETH_ALEN)
		__field(u16, vid)
		__array(char, db_buf, DSA_DB_BUFSIZ)
	),

	TP_fast_assign(
		__assign_str(dev);
		__assign_str(kind);
		__entry->port = dp->index;
		ether_addr_copy(__entry->addr, addr);
		__entry->vid = vid;
		dsa_db_print(db, __entry->db_buf);
	),

	TP_printk("%s %s port %d addr %pM vid %u db \"%s\"",
		  __get_str(dev), __get_str(kind), __entry->port,
		  __entry->addr, __entry->vid, __entry->db_buf)
);

/* Attempt to delete a unicast/multicast address on shared ports for which
 * the delete operation was called more times than the addition
 */
DEFINE_EVENT(dsa_port_addr_del_not_found, dsa_fdb_del_not_found,
	     TP_PROTO(const struct dsa_port *dp, const unsigned char *addr,
		      u16 vid, const struct dsa_db *db),
	     TP_ARGS(dp, addr, vid, db));

DEFINE_EVENT(dsa_port_addr_del_not_found, dsa_mdb_del_not_found,
	     TP_PROTO(const struct dsa_port *dp, const unsigned char *addr,
		      u16 vid, const struct dsa_db *db),
	     TP_ARGS(dp, addr, vid, db));

TRACE_EVENT(dsa_lag_fdb_add_hw,

	TP_PROTO(const struct net_device *lag_dev, const unsigned char *addr,
		 u16 vid, const struct dsa_db *db, int err),

	TP_ARGS(lag_dev, addr, vid, db, err),

	TP_STRUCT__entry(
		__string(dev, lag_dev->name)
		__array(unsigned char, addr, ETH_ALEN)
		__field(u16, vid)
		__array(char, db_buf, DSA_DB_BUFSIZ)
		__field(int, err)
	),

	TP_fast_assign(
		__assign_str(dev);
		ether_addr_copy(__entry->addr, addr);
		__entry->vid = vid;
		dsa_db_print(db, __entry->db_buf);
		__entry->err = err;
	),

	TP_printk("%s addr %pM vid %u db \"%s\" err %d",
		  __get_str(dev), __entry->addr, __entry->vid,
		  __entry->db_buf, __entry->err)
);

TRACE_EVENT(dsa_lag_fdb_add_bump,

	TP_PROTO(const struct net_device *lag_dev, const unsigned char *addr,
		 u16 vid, const struct dsa_db *db, const refcount_t *refcount),

	TP_ARGS(lag_dev, addr, vid, db, refcount),

	TP_STRUCT__entry(
		__string(dev, lag_dev->name)
		__array(unsigned char, addr, ETH_ALEN)
		__field(u16, vid)
		__array(char, db_buf, DSA_DB_BUFSIZ)
		__field(unsigned int, refcount)
	),

	TP_fast_assign(
		__assign_str(dev);
		ether_addr_copy(__entry->addr, addr);
		__entry->vid = vid;
		dsa_db_print(db, __entry->db_buf);
		__entry->refcount = refcount_read(refcount);
	),

	TP_printk("%s addr %pM vid %u db \"%s\" refcount %u",
		  __get_str(dev), __entry->addr, __entry->vid,
		  __entry->db_buf, __entry->refcount)
);

TRACE_EVENT(dsa_lag_fdb_del_hw,

	TP_PROTO(const struct net_device *lag_dev, const unsigned char *addr,
		 u16 vid, const struct dsa_db *db, int err),

	TP_ARGS(lag_dev, addr, vid, db, err),

	TP_STRUCT__entry(
		__string(dev, lag_dev->name)
		__array(unsigned char, addr, ETH_ALEN)
		__field(u16, vid)
		__array(char, db_buf, DSA_DB_BUFSIZ)
		__field(int, err)
	),

	TP_fast_assign(
		__assign_str(dev);
		ether_addr_copy(__entry->addr, addr);
		__entry->vid = vid;
		dsa_db_print(db, __entry->db_buf);
		__entry->err = err;
	),

	TP_printk("%s addr %pM vid %u db \"%s\" err %d",
		  __get_str(dev), __entry->addr, __entry->vid,
		  __entry->db_buf, __entry->err)
);

TRACE_EVENT(dsa_lag_fdb_del_drop,

	TP_PROTO(const struct net_device *lag_dev, const unsigned char *addr,
		 u16 vid, const struct dsa_db *db, const refcount_t *refcount),

	TP_ARGS(lag_dev, addr, vid, db, refcount),

	TP_STRUCT__entry(
		__string(dev, lag_dev->name)
		__array(unsigned char, addr, ETH_ALEN)
		__field(u16, vid)
		__array(char, db_buf, DSA_DB_BUFSIZ)
		__field(unsigned int, refcount)
	),

	TP_fast_assign(
		__assign_str(dev);
		ether_addr_copy(__entry->addr, addr);
		__entry->vid = vid;
		dsa_db_print(db, __entry->db_buf);
		__entry->refcount = refcount_read(refcount);
	),

	TP_printk("%s addr %pM vid %u db \"%s\" refcount %u",
		  __get_str(dev), __entry->addr, __entry->vid,
		  __entry->db_buf, __entry->refcount)
);

TRACE_EVENT(dsa_lag_fdb_del_not_found,

	TP_PROTO(const struct net_device *lag_dev, const unsigned char *addr,
		 u16 vid, const struct dsa_db *db),

	TP_ARGS(lag_dev, addr, vid, db),

	TP_STRUCT__entry(
		__string(dev, lag_dev->name)
		__array(unsigned char, addr, ETH_ALEN)
		__field(u16, vid)
		__array(char, db_buf, DSA_DB_BUFSIZ)
	),

	TP_fast_assign(
		__assign_str(dev);
		ether_addr_copy(__entry->addr, addr);
		__entry->vid = vid;
		dsa_db_print(db, __entry->db_buf);
	),

	TP_printk("%s addr %pM vid %u db \"%s\"",
		  __get_str(dev), __entry->addr, __entry->vid, __entry->db_buf)
);

DECLARE_EVENT_CLASS(dsa_vlan_op_hw,

	TP_PROTO(const struct dsa_port *dp,
		 const struct switchdev_obj_port_vlan *vlan, int err),

	TP_ARGS(dp, vlan, err),

	TP_STRUCT__entry(
		__string(dev, dev_name(dp->ds->dev))
		__string(kind, dsa_port_kind(dp))
		__field(int, port)
		__field(u16, vid)
		__field(u16, flags)
		__field(bool, changed)
		__field(int, err)
	),

	TP_fast_assign(
		__assign_str(dev);
		__assign_str(kind);
		__entry->port = dp->index;
		__entry->vid = vlan->vid;
		__entry->flags = vlan->flags;
		__entry->changed = vlan->changed;
		__entry->err = err;
	),

	TP_printk("%s %s port %d vid %u%s%s%s",
		  __get_str(dev), __get_str(kind), __entry->port, __entry->vid,
		  __entry->flags & BRIDGE_VLAN_INFO_PVID ? " pvid" : "",
		  __entry->flags & BRIDGE_VLAN_INFO_UNTAGGED ? " untagged" : "",
		  __entry->changed ? " (changed)" : "")
);

DEFINE_EVENT(dsa_vlan_op_hw, dsa_vlan_add_hw,
	     TP_PROTO(const struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan, int err),
	     TP_ARGS(dp, vlan, err));

DEFINE_EVENT(dsa_vlan_op_hw, dsa_vlan_del_hw,
	     TP_PROTO(const struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan, int err),
	     TP_ARGS(dp, vlan, err));

DECLARE_EVENT_CLASS(dsa_vlan_op_refcount,

	TP_PROTO(const struct dsa_port *dp,
		 const struct switchdev_obj_port_vlan *vlan,
		 const refcount_t *refcount),

	TP_ARGS(dp, vlan, refcount),

	TP_STRUCT__entry(
		__string(dev, dev_name(dp->ds->dev))
		__string(kind, dsa_port_kind(dp))
		__field(int, port)
		__field(u16, vid)
		__field(u16, flags)
		__field(bool, changed)
		__field(unsigned int, refcount)
	),

	TP_fast_assign(
		__assign_str(dev);
		__assign_str(kind);
		__entry->port = dp->index;
		__entry->vid = vlan->vid;
		__entry->flags = vlan->flags;
		__entry->changed = vlan->changed;
		__entry->refcount = refcount_read(refcount);
	),

	TP_printk("%s %s port %d vid %u%s%s%s refcount %u",
		  __get_str(dev), __get_str(kind), __entry->port, __entry->vid,
		  __entry->flags & BRIDGE_VLAN_INFO_PVID ? " pvid" : "",
		  __entry->flags & BRIDGE_VLAN_INFO_UNTAGGED ? " untagged" : "",
		  __entry->changed ? " (changed)" : "", __entry->refcount)
);

DEFINE_EVENT(dsa_vlan_op_refcount, dsa_vlan_add_bump,
	     TP_PROTO(const struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan,
		      const refcount_t *refcount),
	     TP_ARGS(dp, vlan, refcount));

DEFINE_EVENT(dsa_vlan_op_refcount, dsa_vlan_del_drop,
	     TP_PROTO(const struct dsa_port *dp,
		      const struct switchdev_obj_port_vlan *vlan,
		      const refcount_t *refcount),
	     TP_ARGS(dp, vlan, refcount));

TRACE_EVENT(dsa_vlan_del_not_found,

	TP_PROTO(const struct dsa_port *dp,
		 const struct switchdev_obj_port_vlan *vlan),

	TP_ARGS(dp, vlan),

	TP_STRUCT__entry(
		__string(dev, dev_name(dp->ds->dev))
		__string(kind, dsa_port_kind(dp))
		__field(int, port)
		__field(u16, vid)
	),

	TP_fast_assign(
		__assign_str(dev);
		__assign_str(kind);
		__entry->port = dp->index;
		__entry->vid = vlan->vid;
	),

	TP_printk("%s %s port %d vid %u",
		  __get_str(dev), __get_str(kind), __entry->port, __entry->vid)
);

#endif /* _NET_DSA_TRACE_H */

/* We don't want to use include/trace/events */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE	trace
/* This part must be outside protection */
#include <trace/define_trace.h>
