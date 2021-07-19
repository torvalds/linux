/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 */

#ifndef _BR_PRIVATE_H
#define _BR_PRIVATE_H

#include <linux/netdevice.h>
#include <linux/if_bridge.h>
#include <linux/netpoll.h>
#include <linux/u64_stats_sync.h>
#include <net/route.h>
#include <net/ip6_fib.h>
#include <linux/if_vlan.h>
#include <linux/rhashtable.h>
#include <linux/refcount.h>

#define BR_HASH_BITS 8
#define BR_HASH_SIZE (1 << BR_HASH_BITS)

#define BR_HOLD_TIME (1*HZ)

#define BR_PORT_BITS	10
#define BR_MAX_PORTS	(1<<BR_PORT_BITS)

#define BR_MULTICAST_DEFAULT_HASH_MAX 4096

#define BR_VERSION	"2.3"

/* Control of forwarding link local multicast */
#define BR_GROUPFWD_DEFAULT	0
/* Don't allow forwarding of control protocols like STP, MAC PAUSE and LACP */
enum {
	BR_GROUPFWD_STP		= BIT(0),
	BR_GROUPFWD_MACPAUSE	= BIT(1),
	BR_GROUPFWD_LACP	= BIT(2),
};

#define BR_GROUPFWD_RESTRICTED (BR_GROUPFWD_STP | BR_GROUPFWD_MACPAUSE | \
				BR_GROUPFWD_LACP)
/* The Nearest Customer Bridge Group Address, 01-80-C2-00-00-[00,0B,0C,0D,0F] */
#define BR_GROUPFWD_8021AD	0xB801u

/* Path to usermode spanning tree program */
#define BR_STP_PROG	"/sbin/bridge-stp"

#define BR_FDB_NOTIFY_SETTABLE_BITS (FDB_NOTIFY_BIT | FDB_NOTIFY_INACTIVE_BIT)

typedef struct bridge_id bridge_id;
typedef struct mac_addr mac_addr;
typedef __u16 port_id;

struct bridge_id {
	unsigned char	prio[2];
	unsigned char	addr[ETH_ALEN];
};

struct mac_addr {
	unsigned char	addr[ETH_ALEN];
};

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
/* our own querier */
struct bridge_mcast_own_query {
	struct timer_list	timer;
	u32			startup_sent;
};

/* other querier */
struct bridge_mcast_other_query {
	struct timer_list		timer;
	unsigned long			delay_time;
};

/* selected querier */
struct bridge_mcast_querier {
	struct br_ip addr;
	struct net_bridge_port __rcu	*port;
};

/* IGMP/MLD statistics */
struct bridge_mcast_stats {
	struct br_mcast_stats mstats;
	struct u64_stats_sync syncp;
};
#endif

struct br_tunnel_info {
	__be64				tunnel_id;
	struct metadata_dst __rcu	*tunnel_dst;
};

/* private vlan flags */
enum {
	BR_VLFLAG_PER_PORT_STATS = BIT(0),
	BR_VLFLAG_ADDED_BY_SWITCHDEV = BIT(1),
};

/**
 * struct net_bridge_vlan - per-vlan entry
 *
 * @vnode: rhashtable member
 * @vid: VLAN id
 * @flags: bridge vlan flags
 * @priv_flags: private (in-kernel) bridge vlan flags
 * @state: STP state (e.g. blocking, learning, forwarding)
 * @stats: per-cpu VLAN statistics
 * @br: if MASTER flag set, this points to a bridge struct
 * @port: if MASTER flag unset, this points to a port struct
 * @refcnt: if MASTER flag set, this is bumped for each port referencing it
 * @brvlan: if MASTER flag unset, this points to the global per-VLAN context
 *          for this VLAN entry
 * @vlist: sorted list of VLAN entries
 * @rcu: used for entry destruction
 *
 * This structure is shared between the global per-VLAN entries contained in
 * the bridge rhashtable and the local per-port per-VLAN entries contained in
 * the port's rhashtable. The union entries should be interpreted depending on
 * the entry flags that are set.
 */
struct net_bridge_vlan {
	struct rhash_head		vnode;
	struct rhash_head		tnode;
	u16				vid;
	u16				flags;
	u16				priv_flags;
	u8				state;
	struct pcpu_sw_netstats __percpu *stats;
	union {
		struct net_bridge	*br;
		struct net_bridge_port	*port;
	};
	union {
		refcount_t		refcnt;
		struct net_bridge_vlan	*brvlan;
	};

	struct br_tunnel_info		tinfo;

	struct list_head		vlist;

	struct rcu_head			rcu;
};

/**
 * struct net_bridge_vlan_group
 *
 * @vlan_hash: VLAN entry rhashtable
 * @vlan_list: sorted VLAN entry list
 * @num_vlans: number of total VLAN entries
 * @pvid: PVID VLAN id
 * @pvid_state: PVID's STP state (e.g. forwarding, learning, blocking)
 *
 * IMPORTANT: Be careful when checking if there're VLAN entries using list
 *            primitives because the bridge can have entries in its list which
 *            are just for global context but not for filtering, i.e. they have
 *            the master flag set but not the brentry flag. If you have to check
 *            if there're "real" entries in the bridge please test @num_vlans
 */
struct net_bridge_vlan_group {
	struct rhashtable		vlan_hash;
	struct rhashtable		tunnel_hash;
	struct list_head		vlan_list;
	u16				num_vlans;
	u16				pvid;
	u8				pvid_state;
};

/* bridge fdb flags */
enum {
	BR_FDB_LOCAL,
	BR_FDB_STATIC,
	BR_FDB_STICKY,
	BR_FDB_ADDED_BY_USER,
	BR_FDB_ADDED_BY_EXT_LEARN,
	BR_FDB_OFFLOADED,
	BR_FDB_NOTIFY,
	BR_FDB_NOTIFY_INACTIVE
};

struct net_bridge_fdb_key {
	mac_addr addr;
	u16 vlan_id;
};

struct net_bridge_fdb_entry {
	struct rhash_head		rhnode;
	struct net_bridge_port		*dst;

	struct net_bridge_fdb_key	key;
	struct hlist_node		fdb_node;
	unsigned long			flags;

	/* write-heavy members should not affect lookups */
	unsigned long			updated ____cacheline_aligned_in_smp;
	unsigned long			used;

	struct rcu_head			rcu;
};

#define MDB_PG_FLAGS_PERMANENT	BIT(0)
#define MDB_PG_FLAGS_OFFLOAD	BIT(1)
#define MDB_PG_FLAGS_FAST_LEAVE	BIT(2)
#define MDB_PG_FLAGS_STAR_EXCL	BIT(3)
#define MDB_PG_FLAGS_BLOCKED	BIT(4)

#define PG_SRC_ENT_LIMIT	32

#define BR_SGRP_F_DELETE	BIT(0)
#define BR_SGRP_F_SEND		BIT(1)
#define BR_SGRP_F_INSTALLED	BIT(2)

struct net_bridge_mcast_gc {
	struct hlist_node		gc_node;
	void				(*destroy)(struct net_bridge_mcast_gc *gc);
};

struct net_bridge_group_src {
	struct hlist_node		node;

	struct br_ip			addr;
	struct net_bridge_port_group	*pg;
	u8				flags;
	u8				src_query_rexmit_cnt;
	struct timer_list		timer;

	struct net_bridge		*br;
	struct net_bridge_mcast_gc	mcast_gc;
	struct rcu_head			rcu;
};

struct net_bridge_port_group_sg_key {
	struct net_bridge_port		*port;
	struct br_ip			addr;
};

struct net_bridge_port_group {
	struct net_bridge_port_group __rcu *next;
	struct net_bridge_port_group_sg_key key;
	unsigned char			eth_addr[ETH_ALEN] __aligned(2);
	unsigned char			flags;
	unsigned char			filter_mode;
	unsigned char			grp_query_rexmit_cnt;
	unsigned char			rt_protocol;

	struct hlist_head		src_list;
	unsigned int			src_ents;
	struct timer_list		timer;
	struct timer_list		rexmit_timer;
	struct hlist_node		mglist;
	struct rb_root			eht_set_tree;
	struct rb_root			eht_host_tree;

	struct rhash_head		rhnode;
	struct net_bridge_mcast_gc	mcast_gc;
	struct rcu_head			rcu;
};

struct net_bridge_mdb_entry {
	struct rhash_head		rhnode;
	struct net_bridge		*br;
	struct net_bridge_port_group __rcu *ports;
	struct br_ip			addr;
	bool				host_joined;

	struct timer_list		timer;
	struct hlist_node		mdb_node;

	struct net_bridge_mcast_gc	mcast_gc;
	struct rcu_head			rcu;
};

struct net_bridge_port {
	struct net_bridge		*br;
	struct net_device		*dev;
	struct list_head		list;

	unsigned long			flags;
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
	struct net_bridge_vlan_group	__rcu *vlgrp;
#endif
	struct net_bridge_port		__rcu *backup_port;

	/* STP */
	u8				priority;
	u8				state;
	u16				port_no;
	unsigned char			topology_change_ack;
	unsigned char			config_pending;
	port_id				port_id;
	port_id				designated_port;
	bridge_id			designated_root;
	bridge_id			designated_bridge;
	u32				path_cost;
	u32				designated_cost;
	unsigned long			designated_age;

	struct timer_list		forward_delay_timer;
	struct timer_list		hold_timer;
	struct timer_list		message_age_timer;
	struct kobject			kobj;
	struct rcu_head			rcu;

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	struct bridge_mcast_own_query	ip4_own_query;
#if IS_ENABLED(CONFIG_IPV6)
	struct bridge_mcast_own_query	ip6_own_query;
#endif /* IS_ENABLED(CONFIG_IPV6) */
	u32				multicast_eht_hosts_limit;
	u32				multicast_eht_hosts_cnt;
	unsigned char			multicast_router;
	struct bridge_mcast_stats	__percpu *mcast_stats;
	struct timer_list		multicast_router_timer;
	struct hlist_head		mglist;
	struct hlist_node		rlist;
#endif

#ifdef CONFIG_SYSFS
	char				sysfs_name[IFNAMSIZ];
#endif

#ifdef CONFIG_NET_POLL_CONTROLLER
	struct netpoll			*np;
#endif
#ifdef CONFIG_NET_SWITCHDEV
	int				offload_fwd_mark;
#endif
	u16				group_fwd_mask;
	u16				backup_redirected_cnt;

	struct bridge_stp_xstats	stp_xstats;
};

#define kobj_to_brport(obj)	container_of(obj, struct net_bridge_port, kobj)

#define br_auto_port(p) ((p)->flags & BR_AUTO_MASK)
#define br_promisc_port(p) ((p)->flags & BR_PROMISC)

static inline struct net_bridge_port *br_port_get_rcu(const struct net_device *dev)
{
	return rcu_dereference(dev->rx_handler_data);
}

static inline struct net_bridge_port *br_port_get_rtnl(const struct net_device *dev)
{
	return netif_is_bridge_port(dev) ?
		rtnl_dereference(dev->rx_handler_data) : NULL;
}

static inline struct net_bridge_port *br_port_get_rtnl_rcu(const struct net_device *dev)
{
	return netif_is_bridge_port(dev) ?
		rcu_dereference_rtnl(dev->rx_handler_data) : NULL;
}

enum net_bridge_opts {
	BROPT_VLAN_ENABLED,
	BROPT_VLAN_STATS_ENABLED,
	BROPT_NF_CALL_IPTABLES,
	BROPT_NF_CALL_IP6TABLES,
	BROPT_NF_CALL_ARPTABLES,
	BROPT_GROUP_ADDR_SET,
	BROPT_MULTICAST_ENABLED,
	BROPT_MULTICAST_QUERIER,
	BROPT_MULTICAST_QUERY_USE_IFADDR,
	BROPT_MULTICAST_STATS_ENABLED,
	BROPT_HAS_IPV6_ADDR,
	BROPT_NEIGH_SUPPRESS_ENABLED,
	BROPT_MTU_SET_BY_USER,
	BROPT_VLAN_STATS_PER_PORT,
	BROPT_NO_LL_LEARN,
	BROPT_VLAN_BRIDGE_BINDING,
};

struct net_bridge {
	spinlock_t			lock;
	spinlock_t			hash_lock;
	struct hlist_head		frame_type_list;
	struct net_device		*dev;
	unsigned long			options;
	/* These fields are accessed on each packet */
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
	__be16				vlan_proto;
	u16				default_pvid;
	struct net_bridge_vlan_group	__rcu *vlgrp;
#endif

	struct rhashtable		fdb_hash_tbl;
	struct list_head		port_list;
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	union {
		struct rtable		fake_rtable;
		struct rt6_info		fake_rt6_info;
	};
#endif
	u16				group_fwd_mask;
	u16				group_fwd_mask_required;

	/* STP */
	bridge_id			designated_root;
	bridge_id			bridge_id;
	unsigned char			topology_change;
	unsigned char			topology_change_detected;
	u16				root_port;
	unsigned long			max_age;
	unsigned long			hello_time;
	unsigned long			forward_delay;
	unsigned long			ageing_time;
	unsigned long			bridge_max_age;
	unsigned long			bridge_hello_time;
	unsigned long			bridge_forward_delay;
	unsigned long			bridge_ageing_time;
	u32				root_path_cost;

	u8				group_addr[ETH_ALEN];

	enum {
		BR_NO_STP, 		/* no spanning tree */
		BR_KERNEL_STP,		/* old STP in kernel */
		BR_USER_STP,		/* new RSTP in userspace */
	} stp_enabled;

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING

	u32				hash_max;

	u32				multicast_last_member_count;
	u32				multicast_startup_query_count;

	u8				multicast_igmp_version;
	u8				multicast_router;
#if IS_ENABLED(CONFIG_IPV6)
	u8				multicast_mld_version;
#endif
	spinlock_t			multicast_lock;
	unsigned long			multicast_last_member_interval;
	unsigned long			multicast_membership_interval;
	unsigned long			multicast_querier_interval;
	unsigned long			multicast_query_interval;
	unsigned long			multicast_query_response_interval;
	unsigned long			multicast_startup_query_interval;

	struct rhashtable		mdb_hash_tbl;
	struct rhashtable		sg_port_tbl;

	struct hlist_head		mcast_gc_list;
	struct hlist_head		mdb_list;
	struct hlist_head		router_list;

	struct timer_list		multicast_router_timer;
	struct bridge_mcast_other_query	ip4_other_query;
	struct bridge_mcast_own_query	ip4_own_query;
	struct bridge_mcast_querier	ip4_querier;
	struct bridge_mcast_stats	__percpu *mcast_stats;
#if IS_ENABLED(CONFIG_IPV6)
	struct bridge_mcast_other_query	ip6_other_query;
	struct bridge_mcast_own_query	ip6_own_query;
	struct bridge_mcast_querier	ip6_querier;
#endif /* IS_ENABLED(CONFIG_IPV6) */
	struct work_struct		mcast_gc_work;
#endif

	struct timer_list		hello_timer;
	struct timer_list		tcn_timer;
	struct timer_list		topology_change_timer;
	struct delayed_work		gc_work;
	struct kobject			*ifobj;
	u32				auto_cnt;

#ifdef CONFIG_NET_SWITCHDEV
	int offload_fwd_mark;
#endif
	struct hlist_head		fdb_list;

#if IS_ENABLED(CONFIG_BRIDGE_MRP)
	struct hlist_head		mrp_list;
#endif
#if IS_ENABLED(CONFIG_BRIDGE_CFM)
	struct hlist_head		mep_list;
#endif
};

struct br_input_skb_cb {
	struct net_device *brdev;

	u16 frag_max_size;
#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	u8 igmp;
	u8 mrouters_only:1;
#endif
	u8 proxyarp_replied:1;
	u8 src_port_isolated:1;
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
	u8 vlan_filtered:1;
#endif
#ifdef CONFIG_NETFILTER_FAMILY_BRIDGE
	u8 br_netfilter_broute:1;
#endif

#ifdef CONFIG_NET_SWITCHDEV
	int offload_fwd_mark;
#endif
};

#define BR_INPUT_SKB_CB(__skb)	((struct br_input_skb_cb *)(__skb)->cb)

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
# define BR_INPUT_SKB_CB_MROUTERS_ONLY(__skb)	(BR_INPUT_SKB_CB(__skb)->mrouters_only)
#else
# define BR_INPUT_SKB_CB_MROUTERS_ONLY(__skb)	(0)
#endif

#define br_printk(level, br, format, args...)	\
	printk(level "%s: " format, (br)->dev->name, ##args)

#define br_err(__br, format, args...)			\
	br_printk(KERN_ERR, __br, format, ##args)
#define br_warn(__br, format, args...)			\
	br_printk(KERN_WARNING, __br, format, ##args)
#define br_notice(__br, format, args...)		\
	br_printk(KERN_NOTICE, __br, format, ##args)
#define br_info(__br, format, args...)			\
	br_printk(KERN_INFO, __br, format, ##args)

#define br_debug(br, format, args...)			\
	pr_debug("%s: " format,  (br)->dev->name, ##args)

/* called under bridge lock */
static inline int br_is_root_bridge(const struct net_bridge *br)
{
	return !memcmp(&br->bridge_id, &br->designated_root, 8);
}

/* check if a VLAN entry is global */
static inline bool br_vlan_is_master(const struct net_bridge_vlan *v)
{
	return v->flags & BRIDGE_VLAN_INFO_MASTER;
}

/* check if a VLAN entry is used by the bridge */
static inline bool br_vlan_is_brentry(const struct net_bridge_vlan *v)
{
	return v->flags & BRIDGE_VLAN_INFO_BRENTRY;
}

/* check if we should use the vlan entry, returns false if it's only context */
static inline bool br_vlan_should_use(const struct net_bridge_vlan *v)
{
	if (br_vlan_is_master(v)) {
		if (br_vlan_is_brentry(v))
			return true;
		else
			return false;
	}

	return true;
}

static inline bool nbp_state_should_learn(const struct net_bridge_port *p)
{
	return p->state == BR_STATE_LEARNING || p->state == BR_STATE_FORWARDING;
}

static inline bool br_vlan_valid_id(u16 vid, struct netlink_ext_ack *extack)
{
	bool ret = vid > 0 && vid < VLAN_VID_MASK;

	if (!ret)
		NL_SET_ERR_MSG_MOD(extack, "Vlan id is invalid");

	return ret;
}

static inline bool br_vlan_valid_range(const struct bridge_vlan_info *cur,
				       const struct bridge_vlan_info *last,
				       struct netlink_ext_ack *extack)
{
	/* pvid flag is not allowed in ranges */
	if (cur->flags & BRIDGE_VLAN_INFO_PVID) {
		NL_SET_ERR_MSG_MOD(extack, "Pvid isn't allowed in a range");
		return false;
	}

	/* when cur is the range end, check if:
	 *  - it has range start flag
	 *  - range ids are invalid (end is equal to or before start)
	 */
	if (last) {
		if (cur->flags & BRIDGE_VLAN_INFO_RANGE_BEGIN) {
			NL_SET_ERR_MSG_MOD(extack, "Found a new vlan range start while processing one");
			return false;
		} else if (!(cur->flags & BRIDGE_VLAN_INFO_RANGE_END)) {
			NL_SET_ERR_MSG_MOD(extack, "Vlan range end flag is missing");
			return false;
		} else if (cur->vid <= last->vid) {
			NL_SET_ERR_MSG_MOD(extack, "End vlan id is less than or equal to start vlan id");
			return false;
		}
	}

	/* check for required range flags */
	if (!(cur->flags & (BRIDGE_VLAN_INFO_RANGE_BEGIN |
			    BRIDGE_VLAN_INFO_RANGE_END))) {
		NL_SET_ERR_MSG_MOD(extack, "Both vlan range flags are missing");
		return false;
	}

	return true;
}

static inline int br_afspec_cmd_to_rtm(int cmd)
{
	switch (cmd) {
	case RTM_SETLINK:
		return RTM_NEWVLAN;
	case RTM_DELLINK:
		return RTM_DELVLAN;
	}

	return 0;
}

static inline int br_opt_get(const struct net_bridge *br,
			     enum net_bridge_opts opt)
{
	return test_bit(opt, &br->options);
}

int br_boolopt_toggle(struct net_bridge *br, enum br_boolopt_id opt, bool on,
		      struct netlink_ext_ack *extack);
int br_boolopt_get(const struct net_bridge *br, enum br_boolopt_id opt);
int br_boolopt_multi_toggle(struct net_bridge *br,
			    struct br_boolopt_multi *bm,
			    struct netlink_ext_ack *extack);
void br_boolopt_multi_get(const struct net_bridge *br,
			  struct br_boolopt_multi *bm);
void br_opt_toggle(struct net_bridge *br, enum net_bridge_opts opt, bool on);

/* br_device.c */
void br_dev_setup(struct net_device *dev);
void br_dev_delete(struct net_device *dev, struct list_head *list);
netdev_tx_t br_dev_xmit(struct sk_buff *skb, struct net_device *dev);
#ifdef CONFIG_NET_POLL_CONTROLLER
static inline void br_netpoll_send_skb(const struct net_bridge_port *p,
				       struct sk_buff *skb)
{
	netpoll_send_skb(p->np, skb);
}

int br_netpoll_enable(struct net_bridge_port *p);
void br_netpoll_disable(struct net_bridge_port *p);
#else
static inline void br_netpoll_send_skb(const struct net_bridge_port *p,
				       struct sk_buff *skb)
{
}

static inline int br_netpoll_enable(struct net_bridge_port *p)
{
	return 0;
}

static inline void br_netpoll_disable(struct net_bridge_port *p)
{
}
#endif

/* br_fdb.c */
int br_fdb_init(void);
void br_fdb_fini(void);
int br_fdb_hash_init(struct net_bridge *br);
void br_fdb_hash_fini(struct net_bridge *br);
void br_fdb_flush(struct net_bridge *br);
void br_fdb_find_delete_local(struct net_bridge *br,
			      const struct net_bridge_port *p,
			      const unsigned char *addr, u16 vid);
void br_fdb_changeaddr(struct net_bridge_port *p, const unsigned char *newaddr);
void br_fdb_change_mac_address(struct net_bridge *br, const u8 *newaddr);
void br_fdb_cleanup(struct work_struct *work);
void br_fdb_delete_by_port(struct net_bridge *br,
			   const struct net_bridge_port *p, u16 vid, int do_all);
struct net_bridge_fdb_entry *br_fdb_find_rcu(struct net_bridge *br,
					     const unsigned char *addr,
					     __u16 vid);
int br_fdb_test_addr(struct net_device *dev, unsigned char *addr);
int br_fdb_fillbuf(struct net_bridge *br, void *buf, unsigned long count,
		   unsigned long off);
int br_fdb_insert(struct net_bridge *br, struct net_bridge_port *source,
		  const unsigned char *addr, u16 vid);
void br_fdb_update(struct net_bridge *br, struct net_bridge_port *source,
		   const unsigned char *addr, u16 vid, unsigned long flags);

int br_fdb_delete(struct ndmsg *ndm, struct nlattr *tb[],
		  struct net_device *dev, const unsigned char *addr, u16 vid);
int br_fdb_add(struct ndmsg *nlh, struct nlattr *tb[], struct net_device *dev,
	       const unsigned char *addr, u16 vid, u16 nlh_flags,
	       struct netlink_ext_ack *extack);
int br_fdb_dump(struct sk_buff *skb, struct netlink_callback *cb,
		struct net_device *dev, struct net_device *fdev, int *idx);
int br_fdb_get(struct sk_buff *skb, struct nlattr *tb[], struct net_device *dev,
	       const unsigned char *addr, u16 vid, u32 portid, u32 seq,
	       struct netlink_ext_ack *extack);
int br_fdb_sync_static(struct net_bridge *br, struct net_bridge_port *p);
void br_fdb_unsync_static(struct net_bridge *br, struct net_bridge_port *p);
int br_fdb_external_learn_add(struct net_bridge *br, struct net_bridge_port *p,
			      const unsigned char *addr, u16 vid,
			      bool swdev_notify);
int br_fdb_external_learn_del(struct net_bridge *br, struct net_bridge_port *p,
			      const unsigned char *addr, u16 vid,
			      bool swdev_notify);
void br_fdb_offloaded_set(struct net_bridge *br, struct net_bridge_port *p,
			  const unsigned char *addr, u16 vid, bool offloaded);

/* br_forward.c */
enum br_pkt_type {
	BR_PKT_UNICAST,
	BR_PKT_MULTICAST,
	BR_PKT_BROADCAST
};
int br_dev_queue_push_xmit(struct net *net, struct sock *sk, struct sk_buff *skb);
void br_forward(const struct net_bridge_port *to, struct sk_buff *skb,
		bool local_rcv, bool local_orig);
int br_forward_finish(struct net *net, struct sock *sk, struct sk_buff *skb);
void br_flood(struct net_bridge *br, struct sk_buff *skb,
	      enum br_pkt_type pkt_type, bool local_rcv, bool local_orig);

/* return true if both source port and dest port are isolated */
static inline bool br_skb_isolated(const struct net_bridge_port *to,
				   const struct sk_buff *skb)
{
	return BR_INPUT_SKB_CB(skb)->src_port_isolated &&
	       (to->flags & BR_ISOLATED);
}

/* br_if.c */
void br_port_carrier_check(struct net_bridge_port *p, bool *notified);
int br_add_bridge(struct net *net, const char *name);
int br_del_bridge(struct net *net, const char *name);
int br_add_if(struct net_bridge *br, struct net_device *dev,
	      struct netlink_ext_ack *extack);
int br_del_if(struct net_bridge *br, struct net_device *dev);
void br_mtu_auto_adjust(struct net_bridge *br);
netdev_features_t br_features_recompute(struct net_bridge *br,
					netdev_features_t features);
void br_port_flags_change(struct net_bridge_port *port, unsigned long mask);
void br_manage_promisc(struct net_bridge *br);
int nbp_backup_change(struct net_bridge_port *p, struct net_device *backup_dev);

/* br_input.c */
int br_handle_frame_finish(struct net *net, struct sock *sk, struct sk_buff *skb);
rx_handler_func_t *br_get_rx_handler(const struct net_device *dev);

struct br_frame_type {
	__be16			type;
	int			(*frame_handler)(struct net_bridge_port *port,
						 struct sk_buff *skb);
	struct hlist_node	list;
};

void br_add_frame(struct net_bridge *br, struct br_frame_type *ft);
void br_del_frame(struct net_bridge *br, struct br_frame_type *ft);

static inline bool br_rx_handler_check_rcu(const struct net_device *dev)
{
	return rcu_dereference(dev->rx_handler) == br_get_rx_handler(dev);
}

static inline bool br_rx_handler_check_rtnl(const struct net_device *dev)
{
	return rcu_dereference_rtnl(dev->rx_handler) == br_get_rx_handler(dev);
}

static inline struct net_bridge_port *br_port_get_check_rcu(const struct net_device *dev)
{
	return br_rx_handler_check_rcu(dev) ? br_port_get_rcu(dev) : NULL;
}

static inline struct net_bridge_port *
br_port_get_check_rtnl(const struct net_device *dev)
{
	return br_rx_handler_check_rtnl(dev) ? br_port_get_rtnl_rcu(dev) : NULL;
}

/* br_ioctl.c */
int br_dev_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
int br_ioctl_deviceless_stub(struct net *net, unsigned int cmd,
			     void __user *arg);

/* br_multicast.c */
#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
int br_multicast_rcv(struct net_bridge *br, struct net_bridge_port *port,
		     struct sk_buff *skb, u16 vid);
struct net_bridge_mdb_entry *br_mdb_get(struct net_bridge *br,
					struct sk_buff *skb, u16 vid);
int br_multicast_add_port(struct net_bridge_port *port);
void br_multicast_del_port(struct net_bridge_port *port);
void br_multicast_enable_port(struct net_bridge_port *port);
void br_multicast_disable_port(struct net_bridge_port *port);
void br_multicast_init(struct net_bridge *br);
void br_multicast_join_snoopers(struct net_bridge *br);
void br_multicast_leave_snoopers(struct net_bridge *br);
void br_multicast_open(struct net_bridge *br);
void br_multicast_stop(struct net_bridge *br);
void br_multicast_dev_del(struct net_bridge *br);
void br_multicast_flood(struct net_bridge_mdb_entry *mdst,
			struct sk_buff *skb, bool local_rcv, bool local_orig);
int br_multicast_set_router(struct net_bridge *br, unsigned long val);
int br_multicast_set_port_router(struct net_bridge_port *p, unsigned long val);
int br_multicast_toggle(struct net_bridge *br, unsigned long val,
			struct netlink_ext_ack *extack);
int br_multicast_set_querier(struct net_bridge *br, unsigned long val);
int br_multicast_set_hash_max(struct net_bridge *br, unsigned long val);
int br_multicast_set_igmp_version(struct net_bridge *br, unsigned long val);
#if IS_ENABLED(CONFIG_IPV6)
int br_multicast_set_mld_version(struct net_bridge *br, unsigned long val);
#endif
struct net_bridge_mdb_entry *
br_mdb_ip_get(struct net_bridge *br, struct br_ip *dst);
struct net_bridge_mdb_entry *
br_multicast_new_group(struct net_bridge *br, struct br_ip *group);
struct net_bridge_port_group *
br_multicast_new_port_group(struct net_bridge_port *port, struct br_ip *group,
			    struct net_bridge_port_group __rcu *next,
			    unsigned char flags, const unsigned char *src,
			    u8 filter_mode, u8 rt_protocol);
int br_mdb_hash_init(struct net_bridge *br);
void br_mdb_hash_fini(struct net_bridge *br);
void br_mdb_notify(struct net_device *dev, struct net_bridge_mdb_entry *mp,
		   struct net_bridge_port_group *pg, int type);
void br_rtr_notify(struct net_device *dev, struct net_bridge_port *port,
		   int type);
void br_multicast_del_pg(struct net_bridge_mdb_entry *mp,
			 struct net_bridge_port_group *pg,
			 struct net_bridge_port_group __rcu **pp);
void br_multicast_count(struct net_bridge *br, const struct net_bridge_port *p,
			const struct sk_buff *skb, u8 type, u8 dir);
int br_multicast_init_stats(struct net_bridge *br);
void br_multicast_uninit_stats(struct net_bridge *br);
void br_multicast_get_stats(const struct net_bridge *br,
			    const struct net_bridge_port *p,
			    struct br_mcast_stats *dest);
void br_mdb_init(void);
void br_mdb_uninit(void);
void br_multicast_host_join(struct net_bridge_mdb_entry *mp, bool notify);
void br_multicast_host_leave(struct net_bridge_mdb_entry *mp, bool notify);
void br_multicast_star_g_handle_mode(struct net_bridge_port_group *pg,
				     u8 filter_mode);
void br_multicast_sg_add_exclude_ports(struct net_bridge_mdb_entry *star_mp,
				       struct net_bridge_port_group *sg);
struct net_bridge_group_src *
br_multicast_find_group_src(struct net_bridge_port_group *pg, struct br_ip *ip);
void br_multicast_del_group_src(struct net_bridge_group_src *src,
				bool fastleave);

static inline bool br_group_is_l2(const struct br_ip *group)
{
	return group->proto == 0;
}

#define mlock_dereference(X, br) \
	rcu_dereference_protected(X, lockdep_is_held(&br->multicast_lock))

static inline bool br_multicast_is_router(struct net_bridge *br)
{
	return br->multicast_router == 2 ||
	       (br->multicast_router == 1 &&
		timer_pending(&br->multicast_router_timer));
}

static inline bool
__br_multicast_querier_exists(struct net_bridge *br,
				struct bridge_mcast_other_query *querier,
				const bool is_ipv6)
{
	bool own_querier_enabled;

	if (br_opt_get(br, BROPT_MULTICAST_QUERIER)) {
		if (is_ipv6 && !br_opt_get(br, BROPT_HAS_IPV6_ADDR))
			own_querier_enabled = false;
		else
			own_querier_enabled = true;
	} else {
		own_querier_enabled = false;
	}

	return time_is_before_jiffies(querier->delay_time) &&
	       (own_querier_enabled || timer_pending(&querier->timer));
}

static inline bool br_multicast_querier_exists(struct net_bridge *br,
					       struct ethhdr *eth,
					       const struct net_bridge_mdb_entry *mdb)
{
	switch (eth->h_proto) {
	case (htons(ETH_P_IP)):
		return __br_multicast_querier_exists(br,
			&br->ip4_other_query, false);
#if IS_ENABLED(CONFIG_IPV6)
	case (htons(ETH_P_IPV6)):
		return __br_multicast_querier_exists(br,
			&br->ip6_other_query, true);
#endif
	default:
		return !!mdb && br_group_is_l2(&mdb->addr);
	}
}

static inline bool br_multicast_is_star_g(const struct br_ip *ip)
{
	switch (ip->proto) {
	case htons(ETH_P_IP):
		return ipv4_is_zeronet(ip->src.ip4);
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		return ipv6_addr_any(&ip->src.ip6);
#endif
	default:
		return false;
	}
}

static inline bool br_multicast_should_handle_mode(const struct net_bridge *br,
						   __be16 proto)
{
	switch (proto) {
	case htons(ETH_P_IP):
		return !!(br->multicast_igmp_version == 3);
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		return !!(br->multicast_mld_version == 2);
#endif
	default:
		return false;
	}
}

static inline int br_multicast_igmp_type(const struct sk_buff *skb)
{
	return BR_INPUT_SKB_CB(skb)->igmp;
}

static inline unsigned long br_multicast_lmqt(const struct net_bridge *br)
{
	return br->multicast_last_member_interval *
	       br->multicast_last_member_count;
}

static inline unsigned long br_multicast_gmi(const struct net_bridge *br)
{
	/* use the RFC default of 2 for QRV */
	return 2 * br->multicast_query_interval +
	       br->multicast_query_response_interval;
}
#else
static inline int br_multicast_rcv(struct net_bridge *br,
				   struct net_bridge_port *port,
				   struct sk_buff *skb,
				   u16 vid)
{
	return 0;
}

static inline struct net_bridge_mdb_entry *br_mdb_get(struct net_bridge *br,
						      struct sk_buff *skb, u16 vid)
{
	return NULL;
}

static inline int br_multicast_add_port(struct net_bridge_port *port)
{
	return 0;
}

static inline void br_multicast_del_port(struct net_bridge_port *port)
{
}

static inline void br_multicast_enable_port(struct net_bridge_port *port)
{
}

static inline void br_multicast_disable_port(struct net_bridge_port *port)
{
}

static inline void br_multicast_init(struct net_bridge *br)
{
}

static inline void br_multicast_join_snoopers(struct net_bridge *br)
{
}

static inline void br_multicast_leave_snoopers(struct net_bridge *br)
{
}

static inline void br_multicast_open(struct net_bridge *br)
{
}

static inline void br_multicast_stop(struct net_bridge *br)
{
}

static inline void br_multicast_dev_del(struct net_bridge *br)
{
}

static inline void br_multicast_flood(struct net_bridge_mdb_entry *mdst,
				      struct sk_buff *skb,
				      bool local_rcv, bool local_orig)
{
}

static inline bool br_multicast_is_router(struct net_bridge *br)
{
	return false;
}

static inline bool br_multicast_querier_exists(struct net_bridge *br,
					       struct ethhdr *eth,
					       const struct net_bridge_mdb_entry *mdb)
{
	return false;
}

static inline void br_mdb_init(void)
{
}

static inline void br_mdb_uninit(void)
{
}

static inline int br_mdb_hash_init(struct net_bridge *br)
{
	return 0;
}

static inline void br_mdb_hash_fini(struct net_bridge *br)
{
}

static inline void br_multicast_count(struct net_bridge *br,
				      const struct net_bridge_port *p,
				      const struct sk_buff *skb,
				      u8 type, u8 dir)
{
}

static inline int br_multicast_init_stats(struct net_bridge *br)
{
	return 0;
}

static inline void br_multicast_uninit_stats(struct net_bridge *br)
{
}

static inline int br_multicast_igmp_type(const struct sk_buff *skb)
{
	return 0;
}
#endif

/* br_vlan.c */
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
bool br_allowed_ingress(const struct net_bridge *br,
			struct net_bridge_vlan_group *vg, struct sk_buff *skb,
			u16 *vid, u8 *state);
bool br_allowed_egress(struct net_bridge_vlan_group *vg,
		       const struct sk_buff *skb);
bool br_should_learn(struct net_bridge_port *p, struct sk_buff *skb, u16 *vid);
struct sk_buff *br_handle_vlan(struct net_bridge *br,
			       const struct net_bridge_port *port,
			       struct net_bridge_vlan_group *vg,
			       struct sk_buff *skb);
int br_vlan_add(struct net_bridge *br, u16 vid, u16 flags,
		bool *changed, struct netlink_ext_ack *extack);
int br_vlan_delete(struct net_bridge *br, u16 vid);
void br_vlan_flush(struct net_bridge *br);
struct net_bridge_vlan *br_vlan_find(struct net_bridge_vlan_group *vg, u16 vid);
void br_recalculate_fwd_mask(struct net_bridge *br);
int br_vlan_filter_toggle(struct net_bridge *br, unsigned long val,
			  struct netlink_ext_ack *extack);
int __br_vlan_set_proto(struct net_bridge *br, __be16 proto,
			struct netlink_ext_ack *extack);
int br_vlan_set_proto(struct net_bridge *br, unsigned long val,
		      struct netlink_ext_ack *extack);
int br_vlan_set_stats(struct net_bridge *br, unsigned long val);
int br_vlan_set_stats_per_port(struct net_bridge *br, unsigned long val);
int br_vlan_init(struct net_bridge *br);
int br_vlan_set_default_pvid(struct net_bridge *br, unsigned long val,
			     struct netlink_ext_ack *extack);
int __br_vlan_set_default_pvid(struct net_bridge *br, u16 pvid,
			       struct netlink_ext_ack *extack);
int nbp_vlan_add(struct net_bridge_port *port, u16 vid, u16 flags,
		 bool *changed, struct netlink_ext_ack *extack);
int nbp_vlan_delete(struct net_bridge_port *port, u16 vid);
void nbp_vlan_flush(struct net_bridge_port *port);
int nbp_vlan_init(struct net_bridge_port *port, struct netlink_ext_ack *extack);
int nbp_get_num_vlan_infos(struct net_bridge_port *p, u32 filter_mask);
void br_vlan_get_stats(const struct net_bridge_vlan *v,
		       struct pcpu_sw_netstats *stats);
void br_vlan_port_event(struct net_bridge_port *p, unsigned long event);
int br_vlan_bridge_event(struct net_device *dev, unsigned long event,
			 void *ptr);
void br_vlan_rtnl_init(void);
void br_vlan_rtnl_uninit(void);
void br_vlan_notify(const struct net_bridge *br,
		    const struct net_bridge_port *p,
		    u16 vid, u16 vid_range,
		    int cmd);
bool br_vlan_can_enter_range(const struct net_bridge_vlan *v_curr,
			     const struct net_bridge_vlan *range_end);

void br_vlan_fill_forward_path_pvid(struct net_bridge *br,
				    struct net_device_path_ctx *ctx,
				    struct net_device_path *path);
int br_vlan_fill_forward_path_mode(struct net_bridge *br,
				   struct net_bridge_port *dst,
				   struct net_device_path *path);

static inline struct net_bridge_vlan_group *br_vlan_group(
					const struct net_bridge *br)
{
	return rtnl_dereference(br->vlgrp);
}

static inline struct net_bridge_vlan_group *nbp_vlan_group(
					const struct net_bridge_port *p)
{
	return rtnl_dereference(p->vlgrp);
}

static inline struct net_bridge_vlan_group *br_vlan_group_rcu(
					const struct net_bridge *br)
{
	return rcu_dereference(br->vlgrp);
}

static inline struct net_bridge_vlan_group *nbp_vlan_group_rcu(
					const struct net_bridge_port *p)
{
	return rcu_dereference(p->vlgrp);
}

/* Since bridge now depends on 8021Q module, but the time bridge sees the
 * skb, the vlan tag will always be present if the frame was tagged.
 */
static inline int br_vlan_get_tag(const struct sk_buff *skb, u16 *vid)
{
	int err = 0;

	if (skb_vlan_tag_present(skb)) {
		*vid = skb_vlan_tag_get_id(skb);
	} else {
		*vid = 0;
		err = -EINVAL;
	}

	return err;
}

static inline u16 br_get_pvid(const struct net_bridge_vlan_group *vg)
{
	if (!vg)
		return 0;

	smp_rmb();
	return vg->pvid;
}

static inline u16 br_vlan_flags(const struct net_bridge_vlan *v, u16 pvid)
{
	return v->vid == pvid ? v->flags | BRIDGE_VLAN_INFO_PVID : v->flags;
}
#else
static inline bool br_allowed_ingress(const struct net_bridge *br,
				      struct net_bridge_vlan_group *vg,
				      struct sk_buff *skb,
				      u16 *vid, u8 *state)
{
	return true;
}

static inline bool br_allowed_egress(struct net_bridge_vlan_group *vg,
				     const struct sk_buff *skb)
{
	return true;
}

static inline bool br_should_learn(struct net_bridge_port *p,
				   struct sk_buff *skb, u16 *vid)
{
	return true;
}

static inline struct sk_buff *br_handle_vlan(struct net_bridge *br,
					     const struct net_bridge_port *port,
					     struct net_bridge_vlan_group *vg,
					     struct sk_buff *skb)
{
	return skb;
}

static inline int br_vlan_add(struct net_bridge *br, u16 vid, u16 flags,
			      bool *changed, struct netlink_ext_ack *extack)
{
	*changed = false;
	return -EOPNOTSUPP;
}

static inline int br_vlan_delete(struct net_bridge *br, u16 vid)
{
	return -EOPNOTSUPP;
}

static inline void br_vlan_flush(struct net_bridge *br)
{
}

static inline void br_recalculate_fwd_mask(struct net_bridge *br)
{
}

static inline int br_vlan_init(struct net_bridge *br)
{
	return 0;
}

static inline int nbp_vlan_add(struct net_bridge_port *port, u16 vid, u16 flags,
			       bool *changed, struct netlink_ext_ack *extack)
{
	*changed = false;
	return -EOPNOTSUPP;
}

static inline int nbp_vlan_delete(struct net_bridge_port *port, u16 vid)
{
	return -EOPNOTSUPP;
}

static inline void nbp_vlan_flush(struct net_bridge_port *port)
{
}

static inline struct net_bridge_vlan *br_vlan_find(struct net_bridge_vlan_group *vg,
						   u16 vid)
{
	return NULL;
}

static inline int nbp_vlan_init(struct net_bridge_port *port,
				struct netlink_ext_ack *extack)
{
	return 0;
}

static inline u16 br_vlan_get_tag(const struct sk_buff *skb, u16 *tag)
{
	return 0;
}

static inline u16 br_get_pvid(const struct net_bridge_vlan_group *vg)
{
	return 0;
}

static inline int br_vlan_filter_toggle(struct net_bridge *br,
					unsigned long val,
					struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static inline int nbp_get_num_vlan_infos(struct net_bridge_port *p,
					 u32 filter_mask)
{
	return 0;
}

static inline void br_vlan_fill_forward_path_pvid(struct net_bridge *br,
						  struct net_device_path_ctx *ctx,
						  struct net_device_path *path)
{
}

static inline int br_vlan_fill_forward_path_mode(struct net_bridge *br,
						 struct net_bridge_port *dst,
						 struct net_device_path *path)
{
	return 0;
}

static inline struct net_bridge_vlan_group *br_vlan_group(
					const struct net_bridge *br)
{
	return NULL;
}

static inline struct net_bridge_vlan_group *nbp_vlan_group(
					const struct net_bridge_port *p)
{
	return NULL;
}

static inline struct net_bridge_vlan_group *br_vlan_group_rcu(
					const struct net_bridge *br)
{
	return NULL;
}

static inline struct net_bridge_vlan_group *nbp_vlan_group_rcu(
					const struct net_bridge_port *p)
{
	return NULL;
}

static inline void br_vlan_get_stats(const struct net_bridge_vlan *v,
				     struct pcpu_sw_netstats *stats)
{
}

static inline void br_vlan_port_event(struct net_bridge_port *p,
				      unsigned long event)
{
}

static inline int br_vlan_bridge_event(struct net_device *dev,
				       unsigned long event, void *ptr)
{
	return 0;
}

static inline void br_vlan_rtnl_init(void)
{
}

static inline void br_vlan_rtnl_uninit(void)
{
}

static inline void br_vlan_notify(const struct net_bridge *br,
				  const struct net_bridge_port *p,
				  u16 vid, u16 vid_range,
				  int cmd)
{
}

static inline bool br_vlan_can_enter_range(const struct net_bridge_vlan *v_curr,
					   const struct net_bridge_vlan *range_end)
{
	return true;
}
#endif

/* br_vlan_options.c */
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
bool br_vlan_opts_eq_range(const struct net_bridge_vlan *v_curr,
			   const struct net_bridge_vlan *range_end);
bool br_vlan_opts_fill(struct sk_buff *skb, const struct net_bridge_vlan *v);
size_t br_vlan_opts_nl_size(void);
int br_vlan_process_options(const struct net_bridge *br,
			    const struct net_bridge_port *p,
			    struct net_bridge_vlan *range_start,
			    struct net_bridge_vlan *range_end,
			    struct nlattr **tb,
			    struct netlink_ext_ack *extack);

/* vlan state manipulation helpers using *_ONCE to annotate lock-free access */
static inline u8 br_vlan_get_state(const struct net_bridge_vlan *v)
{
	return READ_ONCE(v->state);
}

static inline void br_vlan_set_state(struct net_bridge_vlan *v, u8 state)
{
	WRITE_ONCE(v->state, state);
}

static inline u8 br_vlan_get_pvid_state(const struct net_bridge_vlan_group *vg)
{
	return READ_ONCE(vg->pvid_state);
}

static inline void br_vlan_set_pvid_state(struct net_bridge_vlan_group *vg,
					  u8 state)
{
	WRITE_ONCE(vg->pvid_state, state);
}

/* learn_allow is true at ingress and false at egress */
static inline bool br_vlan_state_allowed(u8 state, bool learn_allow)
{
	switch (state) {
	case BR_STATE_LEARNING:
		return learn_allow;
	case BR_STATE_FORWARDING:
		return true;
	default:
		return false;
	}
}
#endif

struct nf_br_ops {
	int (*br_dev_xmit_hook)(struct sk_buff *skb);
};
extern const struct nf_br_ops __rcu *nf_br_ops;

/* br_netfilter.c */
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
int br_nf_core_init(void);
void br_nf_core_fini(void);
void br_netfilter_rtable_init(struct net_bridge *);
#else
static inline int br_nf_core_init(void) { return 0; }
static inline void br_nf_core_fini(void) {}
#define br_netfilter_rtable_init(x)
#endif

/* br_stp.c */
void br_set_state(struct net_bridge_port *p, unsigned int state);
struct net_bridge_port *br_get_port(struct net_bridge *br, u16 port_no);
void br_init_port(struct net_bridge_port *p);
void br_become_designated_port(struct net_bridge_port *p);

void __br_set_forward_delay(struct net_bridge *br, unsigned long t);
int br_set_forward_delay(struct net_bridge *br, unsigned long x);
int br_set_hello_time(struct net_bridge *br, unsigned long x);
int br_set_max_age(struct net_bridge *br, unsigned long x);
int __set_ageing_time(struct net_device *dev, unsigned long t);
int br_set_ageing_time(struct net_bridge *br, clock_t ageing_time);


/* br_stp_if.c */
void br_stp_enable_bridge(struct net_bridge *br);
void br_stp_disable_bridge(struct net_bridge *br);
int br_stp_set_enabled(struct net_bridge *br, unsigned long val,
		       struct netlink_ext_ack *extack);
void br_stp_enable_port(struct net_bridge_port *p);
void br_stp_disable_port(struct net_bridge_port *p);
bool br_stp_recalculate_bridge_id(struct net_bridge *br);
void br_stp_change_bridge_id(struct net_bridge *br, const unsigned char *a);
void br_stp_set_bridge_priority(struct net_bridge *br, u16 newprio);
int br_stp_set_port_priority(struct net_bridge_port *p, unsigned long newprio);
int br_stp_set_path_cost(struct net_bridge_port *p, unsigned long path_cost);
ssize_t br_show_bridge_id(char *buf, const struct bridge_id *id);

/* br_stp_bpdu.c */
struct stp_proto;
void br_stp_rcv(const struct stp_proto *proto, struct sk_buff *skb,
		struct net_device *dev);

/* br_stp_timer.c */
void br_stp_timer_init(struct net_bridge *br);
void br_stp_port_timer_init(struct net_bridge_port *p);
unsigned long br_timer_value(const struct timer_list *timer);

/* br.c */
#if IS_ENABLED(CONFIG_ATM_LANE)
extern int (*br_fdb_test_addr_hook)(struct net_device *dev, unsigned char *addr);
#endif

/* br_mrp.c */
#if IS_ENABLED(CONFIG_BRIDGE_MRP)
int br_mrp_parse(struct net_bridge *br, struct net_bridge_port *p,
		 struct nlattr *attr, int cmd, struct netlink_ext_ack *extack);
bool br_mrp_enabled(struct net_bridge *br);
void br_mrp_port_del(struct net_bridge *br, struct net_bridge_port *p);
int br_mrp_fill_info(struct sk_buff *skb, struct net_bridge *br);
#else
static inline int br_mrp_parse(struct net_bridge *br, struct net_bridge_port *p,
			       struct nlattr *attr, int cmd,
			       struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static inline bool br_mrp_enabled(struct net_bridge *br)
{
	return false;
}

static inline void br_mrp_port_del(struct net_bridge *br,
				   struct net_bridge_port *p)
{
}

static inline int br_mrp_fill_info(struct sk_buff *skb, struct net_bridge *br)
{
	return 0;
}

#endif

/* br_cfm.c */
#if IS_ENABLED(CONFIG_BRIDGE_CFM)
int br_cfm_parse(struct net_bridge *br, struct net_bridge_port *p,
		 struct nlattr *attr, int cmd, struct netlink_ext_ack *extack);
bool br_cfm_created(struct net_bridge *br);
void br_cfm_port_del(struct net_bridge *br, struct net_bridge_port *p);
int br_cfm_config_fill_info(struct sk_buff *skb, struct net_bridge *br);
int br_cfm_status_fill_info(struct sk_buff *skb,
			    struct net_bridge *br,
			    bool getlink);
int br_cfm_mep_count(struct net_bridge *br, u32 *count);
int br_cfm_peer_mep_count(struct net_bridge *br, u32 *count);
#else
static inline int br_cfm_parse(struct net_bridge *br, struct net_bridge_port *p,
			       struct nlattr *attr, int cmd,
			       struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static inline bool br_cfm_created(struct net_bridge *br)
{
	return false;
}

static inline void br_cfm_port_del(struct net_bridge *br,
				   struct net_bridge_port *p)
{
}

static inline int br_cfm_config_fill_info(struct sk_buff *skb, struct net_bridge *br)
{
	return -EOPNOTSUPP;
}

static inline int br_cfm_status_fill_info(struct sk_buff *skb,
					  struct net_bridge *br,
					  bool getlink)
{
	return -EOPNOTSUPP;
}

static inline int br_cfm_mep_count(struct net_bridge *br, u32 *count)
{
	return -EOPNOTSUPP;
}

static inline int br_cfm_peer_mep_count(struct net_bridge *br, u32 *count)
{
	return -EOPNOTSUPP;
}
#endif

/* br_netlink.c */
extern struct rtnl_link_ops br_link_ops;
int br_netlink_init(void);
void br_netlink_fini(void);
void br_ifinfo_notify(int event, const struct net_bridge *br,
		      const struct net_bridge_port *port);
void br_info_notify(int event, const struct net_bridge *br,
		    const struct net_bridge_port *port, u32 filter);
int br_setlink(struct net_device *dev, struct nlmsghdr *nlmsg, u16 flags,
	       struct netlink_ext_ack *extack);
int br_dellink(struct net_device *dev, struct nlmsghdr *nlmsg, u16 flags);
int br_getlink(struct sk_buff *skb, u32 pid, u32 seq, struct net_device *dev,
	       u32 filter_mask, int nlflags);
int br_process_vlan_info(struct net_bridge *br,
			 struct net_bridge_port *p, int cmd,
			 struct bridge_vlan_info *vinfo_curr,
			 struct bridge_vlan_info **vinfo_last,
			 bool *changed,
			 struct netlink_ext_ack *extack);

#ifdef CONFIG_SYSFS
/* br_sysfs_if.c */
extern const struct sysfs_ops brport_sysfs_ops;
int br_sysfs_addif(struct net_bridge_port *p);
int br_sysfs_renameif(struct net_bridge_port *p);

/* br_sysfs_br.c */
int br_sysfs_addbr(struct net_device *dev);
void br_sysfs_delbr(struct net_device *dev);

#else

static inline int br_sysfs_addif(struct net_bridge_port *p) { return 0; }
static inline int br_sysfs_renameif(struct net_bridge_port *p) { return 0; }
static inline int br_sysfs_addbr(struct net_device *dev) { return 0; }
static inline void br_sysfs_delbr(struct net_device *dev) { return; }
#endif /* CONFIG_SYSFS */

/* br_switchdev.c */
#ifdef CONFIG_NET_SWITCHDEV
int nbp_switchdev_mark_set(struct net_bridge_port *p);
void nbp_switchdev_frame_mark(const struct net_bridge_port *p,
			      struct sk_buff *skb);
bool nbp_switchdev_allowed_egress(const struct net_bridge_port *p,
				  const struct sk_buff *skb);
int br_switchdev_set_port_flag(struct net_bridge_port *p,
			       unsigned long flags,
			       unsigned long mask,
			       struct netlink_ext_ack *extack);
void br_switchdev_fdb_notify(const struct net_bridge_fdb_entry *fdb,
			     int type);
int br_switchdev_port_vlan_add(struct net_device *dev, u16 vid, u16 flags,
			       struct netlink_ext_ack *extack);
int br_switchdev_port_vlan_del(struct net_device *dev, u16 vid);

static inline void br_switchdev_frame_unmark(struct sk_buff *skb)
{
	skb->offload_fwd_mark = 0;
}
#else
static inline int nbp_switchdev_mark_set(struct net_bridge_port *p)
{
	return 0;
}

static inline void nbp_switchdev_frame_mark(const struct net_bridge_port *p,
					    struct sk_buff *skb)
{
}

static inline bool nbp_switchdev_allowed_egress(const struct net_bridge_port *p,
						const struct sk_buff *skb)
{
	return true;
}

static inline int br_switchdev_set_port_flag(struct net_bridge_port *p,
					     unsigned long flags,
					     unsigned long mask,
					     struct netlink_ext_ack *extack)
{
	return 0;
}

static inline int br_switchdev_port_vlan_add(struct net_device *dev,
					     u16 vid, u16 flags,
					     struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static inline int br_switchdev_port_vlan_del(struct net_device *dev, u16 vid)
{
	return -EOPNOTSUPP;
}

static inline void
br_switchdev_fdb_notify(const struct net_bridge_fdb_entry *fdb, int type)
{
}

static inline void br_switchdev_frame_unmark(struct sk_buff *skb)
{
}
#endif /* CONFIG_NET_SWITCHDEV */

/* br_arp_nd_proxy.c */
void br_recalculate_neigh_suppress_enabled(struct net_bridge *br);
void br_do_proxy_suppress_arp(struct sk_buff *skb, struct net_bridge *br,
			      u16 vid, struct net_bridge_port *p);
void br_do_suppress_nd(struct sk_buff *skb, struct net_bridge *br,
		       u16 vid, struct net_bridge_port *p, struct nd_msg *msg);
struct nd_msg *br_is_nd_neigh_msg(struct sk_buff *skb, struct nd_msg *m);
#endif
