/*
 * include/linux/if_team.h - Network team device driver header
 * Copyright (c) 2011 Jiri Pirko <jpirko@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _LINUX_IF_TEAM_H_
#define _LINUX_IF_TEAM_H_

#ifdef __KERNEL__

struct team_pcpu_stats {
	u64			rx_packets;
	u64			rx_bytes;
	u64			rx_multicast;
	u64			tx_packets;
	u64			tx_bytes;
	struct u64_stats_sync	syncp;
	u32			rx_dropped;
	u32			tx_dropped;
};

struct team;

struct team_port {
	struct net_device *dev;
	struct hlist_node hlist; /* node in enabled ports hash list */
	struct list_head list; /* node in ordinary list */
	struct team *team;
	int index; /* index of enabled port. If disabled, it's set to -1 */

	bool linkup; /* either state.linkup or user.linkup */

	struct {
		bool linkup;
		u32 speed;
		u8 duplex;
	} state;

	/* Values set by userspace */
	struct {
		bool linkup;
		bool linkup_enabled;
	} user;

	/* Custom gennetlink interface related flags */
	bool changed;
	bool removed;

	/*
	 * A place for storing original values of the device before it
	 * become a port.
	 */
	struct {
		unsigned char dev_addr[MAX_ADDR_LEN];
		unsigned int mtu;
	} orig;

	long mode_priv[0];
};

static inline bool team_port_enabled(struct team_port *port)
{
	return port->index != -1;
}

static inline bool team_port_txable(struct team_port *port)
{
	return port->linkup && team_port_enabled(port);
}

struct team_mode_ops {
	int (*init)(struct team *team);
	void (*exit)(struct team *team);
	rx_handler_result_t (*receive)(struct team *team,
				       struct team_port *port,
				       struct sk_buff *skb);
	bool (*transmit)(struct team *team, struct sk_buff *skb);
	int (*port_enter)(struct team *team, struct team_port *port);
	void (*port_leave)(struct team *team, struct team_port *port);
	void (*port_change_mac)(struct team *team, struct team_port *port);
	void (*port_enabled)(struct team *team, struct team_port *port);
	void (*port_disabled)(struct team *team, struct team_port *port);
};

enum team_option_type {
	TEAM_OPTION_TYPE_U32,
	TEAM_OPTION_TYPE_STRING,
	TEAM_OPTION_TYPE_BINARY,
	TEAM_OPTION_TYPE_BOOL,
};

struct team_option_inst_info {
	u32 array_index;
	struct team_port *port; /* != NULL if per-port */
};

struct team_gsetter_ctx {
	union {
		u32 u32_val;
		const char *str_val;
		struct {
			const void *ptr;
			u32 len;
		} bin_val;
		bool bool_val;
	} data;
	struct team_option_inst_info *info;
};

struct team_option {
	struct list_head list;
	const char *name;
	bool per_port;
	unsigned int array_size; /* != 0 means the option is array */
	enum team_option_type type;
	int (*init)(struct team *team, struct team_option_inst_info *info);
	int (*getter)(struct team *team, struct team_gsetter_ctx *ctx);
	int (*setter)(struct team *team, struct team_gsetter_ctx *ctx);
};

extern void team_option_inst_set_change(struct team_option_inst_info *opt_inst_info);
extern void team_options_change_check(struct team *team);

struct team_mode {
	const char *kind;
	struct module *owner;
	size_t priv_size;
	size_t port_priv_size;
	const struct team_mode_ops *ops;
};

#define TEAM_PORT_HASHBITS 4
#define TEAM_PORT_HASHENTRIES (1 << TEAM_PORT_HASHBITS)

#define TEAM_MODE_PRIV_LONGS 4
#define TEAM_MODE_PRIV_SIZE (sizeof(long) * TEAM_MODE_PRIV_LONGS)

struct team {
	struct net_device *dev; /* associated netdevice */
	struct team_pcpu_stats __percpu *pcpu_stats;

	struct mutex lock; /* used for overall locking, e.g. port lists write */

	/*
	 * List of enabled ports and their count
	 */
	int en_port_count;
	struct hlist_head en_port_hlist[TEAM_PORT_HASHENTRIES];

	struct list_head port_list; /* list of all ports */

	struct list_head option_list;
	struct list_head option_inst_list; /* list of option instances */

	const struct team_mode *mode;
	struct team_mode_ops ops;
	long mode_priv[TEAM_MODE_PRIV_LONGS];
};

static inline struct hlist_head *team_port_index_hash(struct team *team,
						      int port_index)
{
	return &team->en_port_hlist[port_index & (TEAM_PORT_HASHENTRIES - 1)];
}

static inline struct team_port *team_get_port_by_index(struct team *team,
						       int port_index)
{
	struct hlist_node *p;
	struct team_port *port;
	struct hlist_head *head = team_port_index_hash(team, port_index);

	hlist_for_each_entry(port, p, head, hlist)
		if (port->index == port_index)
			return port;
	return NULL;
}
static inline struct team_port *team_get_port_by_index_rcu(struct team *team,
							   int port_index)
{
	struct hlist_node *p;
	struct team_port *port;
	struct hlist_head *head = team_port_index_hash(team, port_index);

	hlist_for_each_entry_rcu(port, p, head, hlist)
		if (port->index == port_index)
			return port;
	return NULL;
}

extern int team_port_set_team_mac(struct team_port *port);
extern int team_options_register(struct team *team,
				 const struct team_option *option,
				 size_t option_count);
extern void team_options_unregister(struct team *team,
				    const struct team_option *option,
				    size_t option_count);
extern int team_mode_register(const struct team_mode *mode);
extern void team_mode_unregister(const struct team_mode *mode);

#endif /* __KERNEL__ */

#define TEAM_STRING_MAX_LEN 32

/**********************************
 * NETLINK_GENERIC netlink family.
 **********************************/

enum {
	TEAM_CMD_NOOP,
	TEAM_CMD_OPTIONS_SET,
	TEAM_CMD_OPTIONS_GET,
	TEAM_CMD_PORT_LIST_GET,

	__TEAM_CMD_MAX,
	TEAM_CMD_MAX = (__TEAM_CMD_MAX - 1),
};

enum {
	TEAM_ATTR_UNSPEC,
	TEAM_ATTR_TEAM_IFINDEX,		/* u32 */
	TEAM_ATTR_LIST_OPTION,		/* nest */
	TEAM_ATTR_LIST_PORT,		/* nest */

	__TEAM_ATTR_MAX,
	TEAM_ATTR_MAX = __TEAM_ATTR_MAX - 1,
};

/* Nested layout of get/set msg:
 *
 *	[TEAM_ATTR_LIST_OPTION]
 *		[TEAM_ATTR_ITEM_OPTION]
 *			[TEAM_ATTR_OPTION_*], ...
 *		[TEAM_ATTR_ITEM_OPTION]
 *			[TEAM_ATTR_OPTION_*], ...
 *		...
 *	[TEAM_ATTR_LIST_PORT]
 *		[TEAM_ATTR_ITEM_PORT]
 *			[TEAM_ATTR_PORT_*], ...
 *		[TEAM_ATTR_ITEM_PORT]
 *			[TEAM_ATTR_PORT_*], ...
 *		...
 */

enum {
	TEAM_ATTR_ITEM_OPTION_UNSPEC,
	TEAM_ATTR_ITEM_OPTION,		/* nest */

	__TEAM_ATTR_ITEM_OPTION_MAX,
	TEAM_ATTR_ITEM_OPTION_MAX = __TEAM_ATTR_ITEM_OPTION_MAX - 1,
};

enum {
	TEAM_ATTR_OPTION_UNSPEC,
	TEAM_ATTR_OPTION_NAME,		/* string */
	TEAM_ATTR_OPTION_CHANGED,	/* flag */
	TEAM_ATTR_OPTION_TYPE,		/* u8 */
	TEAM_ATTR_OPTION_DATA,		/* dynamic */
	TEAM_ATTR_OPTION_REMOVED,	/* flag */
	TEAM_ATTR_OPTION_PORT_IFINDEX,	/* u32 */ /* for per-port options */
	TEAM_ATTR_OPTION_ARRAY_INDEX,	/* u32 */ /* for array options */

	__TEAM_ATTR_OPTION_MAX,
	TEAM_ATTR_OPTION_MAX = __TEAM_ATTR_OPTION_MAX - 1,
};

enum {
	TEAM_ATTR_ITEM_PORT_UNSPEC,
	TEAM_ATTR_ITEM_PORT,		/* nest */

	__TEAM_ATTR_ITEM_PORT_MAX,
	TEAM_ATTR_ITEM_PORT_MAX = __TEAM_ATTR_ITEM_PORT_MAX - 1,
};

enum {
	TEAM_ATTR_PORT_UNSPEC,
	TEAM_ATTR_PORT_IFINDEX,		/* u32 */
	TEAM_ATTR_PORT_CHANGED,		/* flag */
	TEAM_ATTR_PORT_LINKUP,		/* flag */
	TEAM_ATTR_PORT_SPEED,		/* u32 */
	TEAM_ATTR_PORT_DUPLEX,		/* u8 */
	TEAM_ATTR_PORT_REMOVED,		/* flag */

	__TEAM_ATTR_PORT_MAX,
	TEAM_ATTR_PORT_MAX = __TEAM_ATTR_PORT_MAX - 1,
};

/*
 * NETLINK_GENERIC related info
 */
#define TEAM_GENL_NAME "team"
#define TEAM_GENL_VERSION 0x1
#define TEAM_GENL_CHANGE_EVENT_MC_GRP_NAME "change_event"

#endif /* _LINUX_IF_TEAM_H_ */
