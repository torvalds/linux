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


#include <linux/netpoll.h>
#include <net/sch_generic.h>
#include <uapi/linux/if_team.h>

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

#ifdef CONFIG_NET_POLL_CONTROLLER
	struct netpoll *np;
#endif

	s32 priority; /* lower number ~ higher priority */
	u16 queue_id;
	struct list_head qom_list; /* node in queue override mapping list */
	struct rcu_head	rcu;
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

#ifdef CONFIG_NET_POLL_CONTROLLER
static inline void team_netpoll_send_skb(struct team_port *port,
					 struct sk_buff *skb)
{
	struct netpoll *np = port->np;

	if (np)
		netpoll_send_skb(np, skb);
}
#else
static inline void team_netpoll_send_skb(struct team_port *port,
					 struct sk_buff *skb)
{
}
#endif

struct team_mode_ops {
	int (*init)(struct team *team);
	void (*exit)(struct team *team);
	rx_handler_result_t (*receive)(struct team *team,
				       struct team_port *port,
				       struct sk_buff *skb);
	bool (*transmit)(struct team *team, struct sk_buff *skb);
	int (*port_enter)(struct team *team, struct team_port *port);
	void (*port_leave)(struct team *team, struct team_port *port);
	void (*port_change_dev_addr)(struct team *team, struct team_port *port);
	void (*port_enabled)(struct team *team, struct team_port *port);
	void (*port_disabled)(struct team *team, struct team_port *port);
};

extern int team_modeop_port_enter(struct team *team, struct team_port *port);
extern void team_modeop_port_change_dev_addr(struct team *team,
					     struct team_port *port);

enum team_option_type {
	TEAM_OPTION_TYPE_U32,
	TEAM_OPTION_TYPE_STRING,
	TEAM_OPTION_TYPE_BINARY,
	TEAM_OPTION_TYPE_BOOL,
	TEAM_OPTION_TYPE_S32,
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
		s32 s32_val;
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
	bool user_carrier_enabled;
	bool queue_override_enabled;
	struct list_head *qom_lists; /* array of queue override mapping lists */
	long mode_priv[TEAM_MODE_PRIV_LONGS];
};

static inline int team_dev_queue_xmit(struct team *team, struct team_port *port,
				      struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(skb->queue_mapping) !=
		     sizeof(qdisc_skb_cb(skb)->slave_dev_queue_mapping));
	skb_set_queue_mapping(skb, qdisc_skb_cb(skb)->slave_dev_queue_mapping);

	skb->dev = port->dev;
	if (unlikely(netpoll_tx_running(team->dev))) {
		team_netpoll_send_skb(port, skb);
		return 0;
	}
	return dev_queue_xmit(skb);
}

static inline struct hlist_head *team_port_index_hash(struct team *team,
						      int port_index)
{
	return &team->en_port_hlist[port_index & (TEAM_PORT_HASHENTRIES - 1)];
}

static inline struct team_port *team_get_port_by_index(struct team *team,
						       int port_index)
{
	struct team_port *port;
	struct hlist_head *head = team_port_index_hash(team, port_index);

	hlist_for_each_entry(port, head, hlist)
		if (port->index == port_index)
			return port;
	return NULL;
}
static inline struct team_port *team_get_port_by_index_rcu(struct team *team,
							   int port_index)
{
	struct team_port *port;
	struct hlist_head *head = team_port_index_hash(team, port_index);

	hlist_for_each_entry_rcu(port, head, hlist)
		if (port->index == port_index)
			return port;
	return NULL;
}

static inline struct team_port *
team_get_first_port_txable_rcu(struct team *team, struct team_port *port)
{
	struct team_port *cur;

	if (likely(team_port_txable(port)))
		return port;
	cur = port;
	list_for_each_entry_continue_rcu(cur, &team->port_list, list)
		if (team_port_txable(port))
			return cur;
	list_for_each_entry_rcu(cur, &team->port_list, list) {
		if (cur == port)
			break;
		if (team_port_txable(port))
			return cur;
	}
	return NULL;
}

extern int team_options_register(struct team *team,
				 const struct team_option *option,
				 size_t option_count);
extern void team_options_unregister(struct team *team,
				    const struct team_option *option,
				    size_t option_count);
extern int team_mode_register(const struct team_mode *mode);
extern void team_mode_unregister(const struct team_mode *mode);

#define TEAM_DEFAULT_NUM_TX_QUEUES 16
#define TEAM_DEFAULT_NUM_RX_QUEUES 16

#endif /* _LINUX_IF_TEAM_H_ */
