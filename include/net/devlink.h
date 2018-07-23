/*
 * include/net/devlink.h - Network physical device Netlink interface
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _NET_DEVLINK_H_
#define _NET_DEVLINK_H_

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>
#include <uapi/linux/devlink.h>

struct devlink_ops;

struct devlink {
	struct list_head list;
	struct list_head port_list;
	struct list_head sb_list;
	struct list_head dpipe_table_list;
	struct list_head resource_list;
	struct devlink_dpipe_headers *dpipe_headers;
	const struct devlink_ops *ops;
	struct device *dev;
	possible_net_t _net;
	struct mutex lock;
	char priv[0] __aligned(NETDEV_ALIGN);
};

struct devlink_port_attrs {
	bool set;
	enum devlink_port_flavour flavour;
	u32 port_number; /* same value as "split group" */
	bool split;
	u32 split_subport_number;
};

struct devlink_port {
	struct list_head list;
	struct devlink *devlink;
	unsigned index;
	bool registered;
	enum devlink_port_type type;
	enum devlink_port_type desired_type;
	void *type_dev;
	struct devlink_port_attrs attrs;
};

struct devlink_sb_pool_info {
	enum devlink_sb_pool_type pool_type;
	u32 size;
	enum devlink_sb_threshold_type threshold_type;
};

/**
 * struct devlink_dpipe_field - dpipe field object
 * @name: field name
 * @id: index inside the headers field array
 * @bitwidth: bitwidth
 * @mapping_type: mapping type
 */
struct devlink_dpipe_field {
	const char *name;
	unsigned int id;
	unsigned int bitwidth;
	enum devlink_dpipe_field_mapping_type mapping_type;
};

/**
 * struct devlink_dpipe_header - dpipe header object
 * @name: header name
 * @id: index, global/local detrmined by global bit
 * @fields: fields
 * @fields_count: number of fields
 * @global: indicates if header is shared like most protocol header
 *	    or driver specific
 */
struct devlink_dpipe_header {
	const char *name;
	unsigned int id;
	struct devlink_dpipe_field *fields;
	unsigned int fields_count;
	bool global;
};

/**
 * struct devlink_dpipe_match - represents match operation
 * @type: type of match
 * @header_index: header index (packets can have several headers of same
 *		  type like in case of tunnels)
 * @header: header
 * @fieled_id: field index
 */
struct devlink_dpipe_match {
	enum devlink_dpipe_match_type type;
	unsigned int header_index;
	struct devlink_dpipe_header *header;
	unsigned int field_id;
};

/**
 * struct devlink_dpipe_action - represents action operation
 * @type: type of action
 * @header_index: header index (packets can have several headers of same
 *		  type like in case of tunnels)
 * @header: header
 * @fieled_id: field index
 */
struct devlink_dpipe_action {
	enum devlink_dpipe_action_type type;
	unsigned int header_index;
	struct devlink_dpipe_header *header;
	unsigned int field_id;
};

/**
 * struct devlink_dpipe_value - represents value of match/action
 * @action: action
 * @match: match
 * @mapping_value: in case the field has some mapping this value
 *                 specified the mapping value
 * @mapping_valid: specify if mapping value is valid
 * @value_size: value size
 * @value: value
 * @mask: bit mask
 */
struct devlink_dpipe_value {
	union {
		struct devlink_dpipe_action *action;
		struct devlink_dpipe_match *match;
	};
	unsigned int mapping_value;
	bool mapping_valid;
	unsigned int value_size;
	void *value;
	void *mask;
};

/**
 * struct devlink_dpipe_entry - table entry object
 * @index: index of the entry in the table
 * @match_values: match values
 * @matche_values_count: count of matches tuples
 * @action_values: actions values
 * @action_values_count: count of actions values
 * @counter: value of counter
 * @counter_valid: Specify if value is valid from hardware
 */
struct devlink_dpipe_entry {
	u64 index;
	struct devlink_dpipe_value *match_values;
	unsigned int match_values_count;
	struct devlink_dpipe_value *action_values;
	unsigned int action_values_count;
	u64 counter;
	bool counter_valid;
};

/**
 * struct devlink_dpipe_dump_ctx - context provided to driver in order
 *				   to dump
 * @info: info
 * @cmd: devlink command
 * @skb: skb
 * @nest: top attribute
 * @hdr: hdr
 */
struct devlink_dpipe_dump_ctx {
	struct genl_info *info;
	enum devlink_command cmd;
	struct sk_buff *skb;
	struct nlattr *nest;
	void *hdr;
};

struct devlink_dpipe_table_ops;

/**
 * struct devlink_dpipe_table - table object
 * @priv: private
 * @name: table name
 * @counters_enabled: indicates if counters are active
 * @counter_control_extern: indicates if counter control is in dpipe or
 *			    external tool
 * @resource_valid: Indicate that the resource id is valid
 * @resource_id: relative resource this table is related to
 * @resource_units: number of resource's unit consumed per table's entry
 * @table_ops: table operations
 * @rcu: rcu
 */
struct devlink_dpipe_table {
	void *priv;
	struct list_head list;
	const char *name;
	bool counters_enabled;
	bool counter_control_extern;
	bool resource_valid;
	u64 resource_id;
	u64 resource_units;
	struct devlink_dpipe_table_ops *table_ops;
	struct rcu_head rcu;
};

/**
 * struct devlink_dpipe_table_ops - dpipe_table ops
 * @actions_dump - dumps all tables actions
 * @matches_dump - dumps all tables matches
 * @entries_dump - dumps all active entries in the table
 * @counters_set_update - when changing the counter status hardware sync
 *			  maybe needed to allocate/free counter related
 *			  resources
 * @size_get - get size
 */
struct devlink_dpipe_table_ops {
	int (*actions_dump)(void *priv, struct sk_buff *skb);
	int (*matches_dump)(void *priv, struct sk_buff *skb);
	int (*entries_dump)(void *priv, bool counters_enabled,
			    struct devlink_dpipe_dump_ctx *dump_ctx);
	int (*counters_set_update)(void *priv, bool enable);
	u64 (*size_get)(void *priv);
};

/**
 * struct devlink_dpipe_headers - dpipe headers
 * @headers - header array can be shared (global bit) or driver specific
 * @headers_count - count of headers
 */
struct devlink_dpipe_headers {
	struct devlink_dpipe_header **headers;
	unsigned int headers_count;
};

/**
 * struct devlink_resource_size_params - resource's size parameters
 * @size_min: minimum size which can be set
 * @size_max: maximum size which can be set
 * @size_granularity: size granularity
 * @size_unit: resource's basic unit
 */
struct devlink_resource_size_params {
	u64 size_min;
	u64 size_max;
	u64 size_granularity;
	enum devlink_resource_unit unit;
};

static inline void
devlink_resource_size_params_init(struct devlink_resource_size_params *size_params,
				  u64 size_min, u64 size_max,
				  u64 size_granularity,
				  enum devlink_resource_unit unit)
{
	size_params->size_min = size_min;
	size_params->size_max = size_max;
	size_params->size_granularity = size_granularity;
	size_params->unit = unit;
}

typedef u64 devlink_resource_occ_get_t(void *priv);

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

#define DEVLINK_RESOURCE_ID_PARENT_TOP 0

struct devlink_ops {
	int (*reload)(struct devlink *devlink, struct netlink_ext_ack *extack);
	int (*port_type_set)(struct devlink_port *devlink_port,
			     enum devlink_port_type port_type);
	int (*port_split)(struct devlink *devlink, unsigned int port_index,
			  unsigned int count, struct netlink_ext_ack *extack);
	int (*port_unsplit)(struct devlink *devlink, unsigned int port_index,
			    struct netlink_ext_ack *extack);
	int (*sb_pool_get)(struct devlink *devlink, unsigned int sb_index,
			   u16 pool_index,
			   struct devlink_sb_pool_info *pool_info);
	int (*sb_pool_set)(struct devlink *devlink, unsigned int sb_index,
			   u16 pool_index, u32 size,
			   enum devlink_sb_threshold_type threshold_type);
	int (*sb_port_pool_get)(struct devlink_port *devlink_port,
				unsigned int sb_index, u16 pool_index,
				u32 *p_threshold);
	int (*sb_port_pool_set)(struct devlink_port *devlink_port,
				unsigned int sb_index, u16 pool_index,
				u32 threshold);
	int (*sb_tc_pool_bind_get)(struct devlink_port *devlink_port,
				   unsigned int sb_index,
				   u16 tc_index,
				   enum devlink_sb_pool_type pool_type,
				   u16 *p_pool_index, u32 *p_threshold);
	int (*sb_tc_pool_bind_set)(struct devlink_port *devlink_port,
				   unsigned int sb_index,
				   u16 tc_index,
				   enum devlink_sb_pool_type pool_type,
				   u16 pool_index, u32 threshold);
	int (*sb_occ_snapshot)(struct devlink *devlink,
			       unsigned int sb_index);
	int (*sb_occ_max_clear)(struct devlink *devlink,
				unsigned int sb_index);
	int (*sb_occ_port_pool_get)(struct devlink_port *devlink_port,
				    unsigned int sb_index, u16 pool_index,
				    u32 *p_cur, u32 *p_max);
	int (*sb_occ_tc_port_bind_get)(struct devlink_port *devlink_port,
				       unsigned int sb_index,
				       u16 tc_index,
				       enum devlink_sb_pool_type pool_type,
				       u32 *p_cur, u32 *p_max);

	int (*eswitch_mode_get)(struct devlink *devlink, u16 *p_mode);
	int (*eswitch_mode_set)(struct devlink *devlink, u16 mode);
	int (*eswitch_inline_mode_get)(struct devlink *devlink, u8 *p_inline_mode);
	int (*eswitch_inline_mode_set)(struct devlink *devlink, u8 inline_mode);
	int (*eswitch_encap_mode_get)(struct devlink *devlink, u8 *p_encap_mode);
	int (*eswitch_encap_mode_set)(struct devlink *devlink, u8 encap_mode);
};

static inline void *devlink_priv(struct devlink *devlink)
{
	BUG_ON(!devlink);
	return &devlink->priv;
}

static inline struct devlink *priv_to_devlink(void *priv)
{
	BUG_ON(!priv);
	return container_of(priv, struct devlink, priv);
}

struct ib_device;

#if IS_ENABLED(CONFIG_NET_DEVLINK)

struct devlink *devlink_alloc(const struct devlink_ops *ops, size_t priv_size);
int devlink_register(struct devlink *devlink, struct device *dev);
void devlink_unregister(struct devlink *devlink);
void devlink_free(struct devlink *devlink);
int devlink_port_register(struct devlink *devlink,
			  struct devlink_port *devlink_port,
			  unsigned int port_index);
void devlink_port_unregister(struct devlink_port *devlink_port);
void devlink_port_type_eth_set(struct devlink_port *devlink_port,
			       struct net_device *netdev);
void devlink_port_type_ib_set(struct devlink_port *devlink_port,
			      struct ib_device *ibdev);
void devlink_port_type_clear(struct devlink_port *devlink_port);
void devlink_port_attrs_set(struct devlink_port *devlink_port,
			    enum devlink_port_flavour flavour,
			    u32 port_number, bool split,
			    u32 split_subport_number);
int devlink_port_get_phys_port_name(struct devlink_port *devlink_port,
				    char *name, size_t len);
int devlink_sb_register(struct devlink *devlink, unsigned int sb_index,
			u32 size, u16 ingress_pools_count,
			u16 egress_pools_count, u16 ingress_tc_count,
			u16 egress_tc_count);
void devlink_sb_unregister(struct devlink *devlink, unsigned int sb_index);
int devlink_dpipe_table_register(struct devlink *devlink,
				 const char *table_name,
				 struct devlink_dpipe_table_ops *table_ops,
				 void *priv, bool counter_control_extern);
void devlink_dpipe_table_unregister(struct devlink *devlink,
				    const char *table_name);
int devlink_dpipe_headers_register(struct devlink *devlink,
				   struct devlink_dpipe_headers *dpipe_headers);
void devlink_dpipe_headers_unregister(struct devlink *devlink);
bool devlink_dpipe_table_counter_enabled(struct devlink *devlink,
					 const char *table_name);
int devlink_dpipe_entry_ctx_prepare(struct devlink_dpipe_dump_ctx *dump_ctx);
int devlink_dpipe_entry_ctx_append(struct devlink_dpipe_dump_ctx *dump_ctx,
				   struct devlink_dpipe_entry *entry);
int devlink_dpipe_entry_ctx_close(struct devlink_dpipe_dump_ctx *dump_ctx);
void devlink_dpipe_entry_clear(struct devlink_dpipe_entry *entry);
int devlink_dpipe_action_put(struct sk_buff *skb,
			     struct devlink_dpipe_action *action);
int devlink_dpipe_match_put(struct sk_buff *skb,
			    struct devlink_dpipe_match *match);
extern struct devlink_dpipe_header devlink_dpipe_header_ethernet;
extern struct devlink_dpipe_header devlink_dpipe_header_ipv4;
extern struct devlink_dpipe_header devlink_dpipe_header_ipv6;

int devlink_resource_register(struct devlink *devlink,
			      const char *resource_name,
			      u64 resource_size,
			      u64 resource_id,
			      u64 parent_resource_id,
			      const struct devlink_resource_size_params *size_params);
void devlink_resources_unregister(struct devlink *devlink,
				  struct devlink_resource *resource);
int devlink_resource_size_get(struct devlink *devlink,
			      u64 resource_id,
			      u64 *p_resource_size);
int devlink_dpipe_table_resource_set(struct devlink *devlink,
				     const char *table_name, u64 resource_id,
				     u64 resource_units);
void devlink_resource_occ_get_register(struct devlink *devlink,
				       u64 resource_id,
				       devlink_resource_occ_get_t *occ_get,
				       void *occ_get_priv);
void devlink_resource_occ_get_unregister(struct devlink *devlink,
					 u64 resource_id);

#else

static inline struct devlink *devlink_alloc(const struct devlink_ops *ops,
					    size_t priv_size)
{
	return kzalloc(sizeof(struct devlink) + priv_size, GFP_KERNEL);
}

static inline int devlink_register(struct devlink *devlink, struct device *dev)
{
	return 0;
}

static inline void devlink_unregister(struct devlink *devlink)
{
}

static inline void devlink_free(struct devlink *devlink)
{
	kfree(devlink);
}

static inline int devlink_port_register(struct devlink *devlink,
					struct devlink_port *devlink_port,
					unsigned int port_index)
{
	return 0;
}

static inline void devlink_port_unregister(struct devlink_port *devlink_port)
{
}

static inline void devlink_port_type_eth_set(struct devlink_port *devlink_port,
					     struct net_device *netdev)
{
}

static inline void devlink_port_type_ib_set(struct devlink_port *devlink_port,
					    struct ib_device *ibdev)
{
}

static inline void devlink_port_type_clear(struct devlink_port *devlink_port)
{
}

static inline void devlink_port_attrs_set(struct devlink_port *devlink_port,
					  enum devlink_port_flavour flavour,
					  u32 port_number, bool split,
					  u32 split_subport_number)
{
}

static inline int
devlink_port_get_phys_port_name(struct devlink_port *devlink_port,
				char *name, size_t len)
{
	return -EOPNOTSUPP;
}

static inline int devlink_sb_register(struct devlink *devlink,
				      unsigned int sb_index, u32 size,
				      u16 ingress_pools_count,
				      u16 egress_pools_count,
				      u16 ingress_tc_count,
				      u16 egress_tc_count)
{
	return 0;
}

static inline void devlink_sb_unregister(struct devlink *devlink,
					 unsigned int sb_index)
{
}

static inline int
devlink_dpipe_table_register(struct devlink *devlink,
			     const char *table_name,
			     struct devlink_dpipe_table_ops *table_ops,
			     void *priv, bool counter_control_extern)
{
	return 0;
}

static inline void devlink_dpipe_table_unregister(struct devlink *devlink,
						  const char *table_name)
{
}

static inline int devlink_dpipe_headers_register(struct devlink *devlink,
						 struct devlink_dpipe_headers *
						 dpipe_headers)
{
	return 0;
}

static inline void devlink_dpipe_headers_unregister(struct devlink *devlink)
{
}

static inline bool devlink_dpipe_table_counter_enabled(struct devlink *devlink,
						       const char *table_name)
{
	return false;
}

static inline int
devlink_dpipe_entry_ctx_prepare(struct devlink_dpipe_dump_ctx *dump_ctx)
{
	return 0;
}

static inline int
devlink_dpipe_entry_ctx_append(struct devlink_dpipe_dump_ctx *dump_ctx,
			       struct devlink_dpipe_entry *entry)
{
	return 0;
}

static inline int
devlink_dpipe_entry_ctx_close(struct devlink_dpipe_dump_ctx *dump_ctx)
{
	return 0;
}

static inline void
devlink_dpipe_entry_clear(struct devlink_dpipe_entry *entry)
{
}

static inline int
devlink_dpipe_action_put(struct sk_buff *skb,
			 struct devlink_dpipe_action *action)
{
	return 0;
}

static inline int
devlink_dpipe_match_put(struct sk_buff *skb,
			struct devlink_dpipe_match *match)
{
	return 0;
}

static inline int
devlink_resource_register(struct devlink *devlink,
			  const char *resource_name,
			  u64 resource_size,
			  u64 resource_id,
			  u64 parent_resource_id,
			  const struct devlink_resource_size_params *size_params)
{
	return 0;
}

static inline void
devlink_resources_unregister(struct devlink *devlink,
			     struct devlink_resource *resource)
{
}

static inline int
devlink_resource_size_get(struct devlink *devlink, u64 resource_id,
			  u64 *p_resource_size)
{
	return -EOPNOTSUPP;
}

static inline int
devlink_dpipe_table_resource_set(struct devlink *devlink,
				 const char *table_name, u64 resource_id,
				 u64 resource_units)
{
	return -EOPNOTSUPP;
}

static inline void
devlink_resource_occ_get_register(struct devlink *devlink,
				  u64 resource_id,
				  devlink_resource_occ_get_t *occ_get,
				  void *occ_get_priv)
{
}

static inline void
devlink_resource_occ_get_unregister(struct devlink *devlink,
				    u64 resource_id)
{
}

#endif

#endif /* _NET_DEVLINK_H_ */
