/*
 * Bond several ethernet interfaces into a Cisco, running 'Etherchannel'.
 *
 * Portions are (c) Copyright 1995 Simon "Guru Aleph-Null" Janes
 * NCM: Network and Communications Management, Inc.
 *
 * BUT, I'm the one who modified it for ethernet, so:
 * (c) Copyright 1999, Thomas Davis, tadavis@lbl.gov
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU Public License, incorporated herein by reference.
 *
 */

#ifndef _NET_BONDING_H
#define _NET_BONDING_H

#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/if_bonding.h>
#include <linux/cpumask.h>
#include <linux/in6.h>
#include <linux/netpoll.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/reciprocal_div.h>
#include <linux/if_link.h>

#include <net/bond_3ad.h>
#include <net/bond_alb.h>
#include <net/bond_options.h>
#include <net/ipv6.h>
#include <net/addrconf.h>

#define BOND_MAX_ARP_TARGETS	16
#define BOND_MAX_NS_TARGETS	BOND_MAX_ARP_TARGETS

#define BOND_DEFAULT_MIIMON	100

#ifndef __long_aligned
#define __long_aligned __attribute__((aligned((sizeof(long)))))
#endif

#define slave_info(bond_dev, slave_dev, fmt, ...) \
	netdev_info(bond_dev, "(slave %s): " fmt, (slave_dev)->name, ##__VA_ARGS__)
#define slave_warn(bond_dev, slave_dev, fmt, ...) \
	netdev_warn(bond_dev, "(slave %s): " fmt, (slave_dev)->name, ##__VA_ARGS__)
#define slave_dbg(bond_dev, slave_dev, fmt, ...) \
	netdev_dbg(bond_dev, "(slave %s): " fmt, (slave_dev)->name, ##__VA_ARGS__)
#define slave_err(bond_dev, slave_dev, fmt, ...) \
	netdev_err(bond_dev, "(slave %s): " fmt, (slave_dev)->name, ##__VA_ARGS__)

#define BOND_MODE(bond) ((bond)->params.mode)

/* slave list primitives */
#define bond_slave_list(bond) (&(bond)->dev->adj_list.lower)

#define bond_has_slaves(bond) !list_empty(bond_slave_list(bond))

/* IMPORTANT: bond_first/last_slave can return NULL in case of an empty list */
#define bond_first_slave(bond) \
	(bond_has_slaves(bond) ? \
		netdev_adjacent_get_private(bond_slave_list(bond)->next) : \
		NULL)
#define bond_last_slave(bond) \
	(bond_has_slaves(bond) ? \
		netdev_adjacent_get_private(bond_slave_list(bond)->prev) : \
		NULL)

/* Caller must have rcu_read_lock */
#define bond_first_slave_rcu(bond) \
	netdev_lower_get_first_private_rcu(bond->dev)

#define bond_is_first_slave(bond, pos) (pos == bond_first_slave(bond))
#define bond_is_last_slave(bond, pos) (pos == bond_last_slave(bond))

/**
 * bond_for_each_slave - iterate over all slaves
 * @bond:	the bond holding this list
 * @pos:	current slave
 * @iter:	list_head * iterator
 *
 * Caller must hold RTNL
 */
#define bond_for_each_slave(bond, pos, iter) \
	netdev_for_each_lower_private((bond)->dev, pos, iter)

/* Caller must have rcu_read_lock */
#define bond_for_each_slave_rcu(bond, pos, iter) \
	netdev_for_each_lower_private_rcu((bond)->dev, pos, iter)

#define BOND_XFRM_FEATURES (NETIF_F_HW_ESP | NETIF_F_HW_ESP_TX_CSUM | \
			    NETIF_F_GSO_ESP)

#ifdef CONFIG_NET_POLL_CONTROLLER
extern atomic_t netpoll_block_tx;

static inline void block_netpoll_tx(void)
{
	atomic_inc(&netpoll_block_tx);
}

static inline void unblock_netpoll_tx(void)
{
	atomic_dec(&netpoll_block_tx);
}

static inline int is_netpoll_tx_blocked(struct net_device *dev)
{
	if (unlikely(netpoll_tx_running(dev)))
		return atomic_read(&netpoll_block_tx);
	return 0;
}
#else
#define block_netpoll_tx()
#define unblock_netpoll_tx()
#define is_netpoll_tx_blocked(dev) (0)
#endif

struct bond_params {
	int mode;
	int xmit_policy;
	int miimon;
	u8 num_peer_notif;
	u8 missed_max;
	int arp_interval;
	int arp_validate;
	int arp_all_targets;
	int use_carrier;
	int fail_over_mac;
	int updelay;
	int downdelay;
	int peer_notif_delay;
	int lacp_active;
	int lacp_fast;
	unsigned int min_links;
	int ad_select;
	char primary[IFNAMSIZ];
	int primary_reselect;
	__be32 arp_targets[BOND_MAX_ARP_TARGETS];
	int tx_queues;
	int all_slaves_active;
	int resend_igmp;
	int lp_interval;
	int packets_per_slave;
	int tlb_dynamic_lb;
	struct reciprocal_value reciprocal_packets_per_slave;
	u16 ad_actor_sys_prio;
	u16 ad_user_port_key;
#if IS_ENABLED(CONFIG_IPV6)
	struct in6_addr ns_targets[BOND_MAX_NS_TARGETS];
#endif

	/* 2 bytes of padding : see ether_addr_equal_64bits() */
	u8 ad_actor_system[ETH_ALEN + 2];
};

struct slave {
	struct net_device *dev; /* first - useful for panic debug */
	struct bonding *bond; /* our master */
	int    delay;
	/* all 4 in jiffies */
	unsigned long last_link_up;
	unsigned long last_tx;
	unsigned long last_rx;
	unsigned long target_last_arp_rx[BOND_MAX_ARP_TARGETS];
	s8     link;		/* one of BOND_LINK_XXXX */
	s8     link_new_state;	/* one of BOND_LINK_XXXX */
	u8     backup:1,   /* indicates backup slave. Value corresponds with
			      BOND_STATE_ACTIVE and BOND_STATE_BACKUP */
	       inactive:1, /* indicates inactive slave */
	       should_notify:1, /* indicates whether the state changed */
	       should_notify_link:1; /* indicates whether the link changed */
	u8     duplex;
	u32    original_mtu;
	u32    link_failure_count;
	u32    speed;
	u16    queue_id;
	u8     perm_hwaddr[MAX_ADDR_LEN];
	int    prio;
	struct ad_slave_info *ad_info;
	struct tlb_slave_info tlb_info;
#ifdef CONFIG_NET_POLL_CONTROLLER
	struct netpoll *np;
#endif
	struct delayed_work notify_work;
	struct kobject kobj;
	struct rtnl_link_stats64 slave_stats;
};

static inline struct slave *to_slave(struct kobject *kobj)
{
	return container_of(kobj, struct slave, kobj);
}

struct bond_up_slave {
	unsigned int	count;
	struct rcu_head rcu;
	struct slave	*arr[];
};

/*
 * Link pseudo-state only used internally by monitors
 */
#define BOND_LINK_NOCHANGE -1

struct bond_ipsec {
	struct list_head list;
	struct xfrm_state *xs;
};

/*
 * Here are the locking policies for the two bonding locks:
 * Get rcu_read_lock when reading or RTNL when writing slave list.
 */
struct bonding {
	struct   net_device *dev; /* first - useful for panic debug */
	struct   slave __rcu *curr_active_slave;
	struct   slave __rcu *current_arp_slave;
	struct   slave __rcu *primary_slave;
	struct   bond_up_slave __rcu *usable_slaves;
	struct   bond_up_slave __rcu *all_slaves;
	bool     force_primary;
	s32      slave_cnt; /* never change this value outside the attach/detach wrappers */
	int     (*recv_probe)(const struct sk_buff *, struct bonding *,
			      struct slave *);
	/* mode_lock is used for mode-specific locking needs, currently used by:
	 * 3ad mode (4) - protect against running bond_3ad_unbind_slave() and
	 *                bond_3ad_state_machine_handler() concurrently and also
	 *                the access to the state machine shared variables.
	 * TLB mode (5) - to sync the use and modifications of its hash table
	 * ALB mode (6) - to sync the use and modifications of its hash table
	 */
	spinlock_t mode_lock;
	spinlock_t stats_lock;
	u8	 send_peer_notif;
	u8       igmp_retrans;
#ifdef CONFIG_PROC_FS
	struct   proc_dir_entry *proc_entry;
	char     proc_file_name[IFNAMSIZ];
#endif /* CONFIG_PROC_FS */
	struct   list_head bond_list;
	u32 __percpu *rr_tx_counter;
	struct   ad_bond_info ad_info;
	struct   alb_bond_info alb_info;
	struct   bond_params params;
	struct   workqueue_struct *wq;
	struct   delayed_work mii_work;
	struct   delayed_work arp_work;
	struct   delayed_work alb_work;
	struct   delayed_work ad_work;
	struct   delayed_work mcast_work;
	struct   delayed_work slave_arr_work;
#ifdef CONFIG_DEBUG_FS
	/* debugging support via debugfs */
	struct	 dentry *debug_dir;
#endif /* CONFIG_DEBUG_FS */
	struct rtnl_link_stats64 bond_stats;
#ifdef CONFIG_XFRM_OFFLOAD
	struct list_head ipsec_list;
	/* protecting ipsec_list */
	spinlock_t ipsec_lock;
#endif /* CONFIG_XFRM_OFFLOAD */
	struct bpf_prog *xdp_prog;
};

#define bond_slave_get_rcu(dev) \
	((struct slave *) rcu_dereference(dev->rx_handler_data))

#define bond_slave_get_rtnl(dev) \
	((struct slave *) rtnl_dereference(dev->rx_handler_data))

void bond_queue_slave_event(struct slave *slave);
void bond_lower_state_changed(struct slave *slave);

struct bond_vlan_tag {
	__be16		vlan_proto;
	unsigned short	vlan_id;
};

/**
 * Returns NULL if the net_device does not belong to any of the bond's slaves
 *
 * Caller must hold bond lock for read
 */
static inline struct slave *bond_get_slave_by_dev(struct bonding *bond,
						  struct net_device *slave_dev)
{
	return netdev_lower_dev_get_private(bond->dev, slave_dev);
}

static inline struct bonding *bond_get_bond_by_slave(struct slave *slave)
{
	return slave->bond;
}

static inline bool bond_should_override_tx_queue(struct bonding *bond)
{
	return BOND_MODE(bond) == BOND_MODE_ACTIVEBACKUP ||
	       BOND_MODE(bond) == BOND_MODE_ROUNDROBIN;
}

static inline bool bond_is_lb(const struct bonding *bond)
{
	return BOND_MODE(bond) == BOND_MODE_TLB ||
	       BOND_MODE(bond) == BOND_MODE_ALB;
}

static inline bool bond_needs_speed_duplex(const struct bonding *bond)
{
	return BOND_MODE(bond) == BOND_MODE_8023AD || bond_is_lb(bond);
}

static inline bool bond_is_nondyn_tlb(const struct bonding *bond)
{
	return (bond_is_lb(bond) && bond->params.tlb_dynamic_lb == 0);
}

static inline bool bond_mode_can_use_xmit_hash(const struct bonding *bond)
{
	return (BOND_MODE(bond) == BOND_MODE_8023AD ||
		BOND_MODE(bond) == BOND_MODE_XOR ||
		BOND_MODE(bond) == BOND_MODE_TLB ||
		BOND_MODE(bond) == BOND_MODE_ALB);
}

static inline bool bond_mode_uses_xmit_hash(const struct bonding *bond)
{
	return (BOND_MODE(bond) == BOND_MODE_8023AD ||
		BOND_MODE(bond) == BOND_MODE_XOR ||
		bond_is_nondyn_tlb(bond));
}

static inline bool bond_mode_uses_arp(int mode)
{
	return mode != BOND_MODE_8023AD && mode != BOND_MODE_TLB &&
	       mode != BOND_MODE_ALB;
}

static inline bool bond_mode_uses_primary(int mode)
{
	return mode == BOND_MODE_ACTIVEBACKUP || mode == BOND_MODE_TLB ||
	       mode == BOND_MODE_ALB;
}

static inline bool bond_uses_primary(struct bonding *bond)
{
	return bond_mode_uses_primary(BOND_MODE(bond));
}

static inline struct net_device *bond_option_active_slave_get_rcu(struct bonding *bond)
{
	struct slave *slave = rcu_dereference_rtnl(bond->curr_active_slave);

	return bond_uses_primary(bond) && slave ? slave->dev : NULL;
}

static inline bool bond_slave_is_up(struct slave *slave)
{
	return netif_running(slave->dev) && netif_carrier_ok(slave->dev);
}

static inline void bond_set_active_slave(struct slave *slave)
{
	if (slave->backup) {
		slave->backup = 0;
		bond_queue_slave_event(slave);
		bond_lower_state_changed(slave);
	}
}

static inline void bond_set_backup_slave(struct slave *slave)
{
	if (!slave->backup) {
		slave->backup = 1;
		bond_queue_slave_event(slave);
		bond_lower_state_changed(slave);
	}
}

static inline void bond_set_slave_state(struct slave *slave,
					int slave_state, bool notify)
{
	if (slave->backup == slave_state)
		return;

	slave->backup = slave_state;
	if (notify) {
		bond_lower_state_changed(slave);
		bond_queue_slave_event(slave);
		slave->should_notify = 0;
	} else {
		if (slave->should_notify)
			slave->should_notify = 0;
		else
			slave->should_notify = 1;
	}
}

static inline void bond_slave_state_change(struct bonding *bond)
{
	struct list_head *iter;
	struct slave *tmp;

	bond_for_each_slave(bond, tmp, iter) {
		if (tmp->link == BOND_LINK_UP)
			bond_set_active_slave(tmp);
		else if (tmp->link == BOND_LINK_DOWN)
			bond_set_backup_slave(tmp);
	}
}

static inline void bond_slave_state_notify(struct bonding *bond)
{
	struct list_head *iter;
	struct slave *tmp;

	bond_for_each_slave(bond, tmp, iter) {
		if (tmp->should_notify) {
			bond_lower_state_changed(tmp);
			tmp->should_notify = 0;
		}
	}
}

static inline int bond_slave_state(struct slave *slave)
{
	return slave->backup;
}

static inline bool bond_is_active_slave(struct slave *slave)
{
	return !bond_slave_state(slave);
}

static inline bool bond_slave_can_tx(struct slave *slave)
{
	return bond_slave_is_up(slave) && slave->link == BOND_LINK_UP &&
	       bond_is_active_slave(slave);
}

static inline bool bond_is_active_slave_dev(const struct net_device *slave_dev)
{
	struct slave *slave;
	bool active;

	rcu_read_lock();
	slave = bond_slave_get_rcu(slave_dev);
	active = bond_is_active_slave(slave);
	rcu_read_unlock();

	return active;
}

static inline void bond_hw_addr_copy(u8 *dst, const u8 *src, unsigned int len)
{
	if (len == ETH_ALEN) {
		ether_addr_copy(dst, src);
		return;
	}

	memcpy(dst, src, len);
}

#define BOND_PRI_RESELECT_ALWAYS	0
#define BOND_PRI_RESELECT_BETTER	1
#define BOND_PRI_RESELECT_FAILURE	2

#define BOND_FOM_NONE			0
#define BOND_FOM_ACTIVE			1
#define BOND_FOM_FOLLOW			2

#define BOND_ARP_TARGETS_ANY		0
#define BOND_ARP_TARGETS_ALL		1

#define BOND_ARP_VALIDATE_NONE		0
#define BOND_ARP_VALIDATE_ACTIVE	(1 << BOND_STATE_ACTIVE)
#define BOND_ARP_VALIDATE_BACKUP	(1 << BOND_STATE_BACKUP)
#define BOND_ARP_VALIDATE_ALL		(BOND_ARP_VALIDATE_ACTIVE | \
					 BOND_ARP_VALIDATE_BACKUP)
#define BOND_ARP_FILTER			(BOND_ARP_VALIDATE_ALL + 1)
#define BOND_ARP_FILTER_ACTIVE		(BOND_ARP_VALIDATE_ACTIVE | \
					 BOND_ARP_FILTER)
#define BOND_ARP_FILTER_BACKUP		(BOND_ARP_VALIDATE_BACKUP | \
					 BOND_ARP_FILTER)

#define BOND_SLAVE_NOTIFY_NOW		true
#define BOND_SLAVE_NOTIFY_LATER		false

static inline int slave_do_arp_validate(struct bonding *bond,
					struct slave *slave)
{
	return bond->params.arp_validate & (1 << bond_slave_state(slave));
}

static inline int slave_do_arp_validate_only(struct bonding *bond)
{
	return bond->params.arp_validate & BOND_ARP_FILTER;
}

static inline int bond_is_ip_target_ok(__be32 addr)
{
	return !ipv4_is_lbcast(addr) && !ipv4_is_zeronet(addr);
}

#if IS_ENABLED(CONFIG_IPV6)
static inline int bond_is_ip6_target_ok(struct in6_addr *addr)
{
	return !ipv6_addr_any(addr) &&
	       !ipv6_addr_loopback(addr) &&
	       !ipv6_addr_is_multicast(addr);
}
#endif

/* Get the oldest arp which we've received on this slave for bond's
 * arp_targets.
 */
static inline unsigned long slave_oldest_target_arp_rx(struct bonding *bond,
						       struct slave *slave)
{
	int i = 1;
	unsigned long ret = slave->target_last_arp_rx[0];

	for (; (i < BOND_MAX_ARP_TARGETS) && bond->params.arp_targets[i]; i++)
		if (time_before(slave->target_last_arp_rx[i], ret))
			ret = slave->target_last_arp_rx[i];

	return ret;
}

static inline unsigned long slave_last_rx(struct bonding *bond,
					struct slave *slave)
{
	if (bond->params.arp_all_targets == BOND_ARP_TARGETS_ALL)
		return slave_oldest_target_arp_rx(bond, slave);

	return slave->last_rx;
}

static inline void slave_update_last_tx(struct slave *slave)
{
	WRITE_ONCE(slave->last_tx, jiffies);
}

static inline unsigned long slave_last_tx(struct slave *slave)
{
	return READ_ONCE(slave->last_tx);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static inline netdev_tx_t bond_netpoll_send_skb(const struct slave *slave,
					 struct sk_buff *skb)
{
	return netpoll_send_skb(slave->np, skb);
}
#else
static inline netdev_tx_t bond_netpoll_send_skb(const struct slave *slave,
					 struct sk_buff *skb)
{
	BUG();
	return NETDEV_TX_OK;
}
#endif

static inline void bond_set_slave_inactive_flags(struct slave *slave,
						 bool notify)
{
	if (!bond_is_lb(slave->bond))
		bond_set_slave_state(slave, BOND_STATE_BACKUP, notify);
	if (!slave->bond->params.all_slaves_active)
		slave->inactive = 1;
}

static inline void bond_set_slave_active_flags(struct slave *slave,
					       bool notify)
{
	bond_set_slave_state(slave, BOND_STATE_ACTIVE, notify);
	slave->inactive = 0;
}

static inline bool bond_is_slave_inactive(struct slave *slave)
{
	return slave->inactive;
}

static inline void bond_propose_link_state(struct slave *slave, int state)
{
	slave->link_new_state = state;
}

static inline void bond_commit_link_state(struct slave *slave, bool notify)
{
	if (slave->link_new_state == BOND_LINK_NOCHANGE)
		return;

	slave->link = slave->link_new_state;
	if (notify) {
		bond_queue_slave_event(slave);
		bond_lower_state_changed(slave);
		slave->should_notify_link = 0;
	} else {
		if (slave->should_notify_link)
			slave->should_notify_link = 0;
		else
			slave->should_notify_link = 1;
	}
}

static inline void bond_set_slave_link_state(struct slave *slave, int state,
					     bool notify)
{
	bond_propose_link_state(slave, state);
	bond_commit_link_state(slave, notify);
}

static inline void bond_slave_link_notify(struct bonding *bond)
{
	struct list_head *iter;
	struct slave *tmp;

	bond_for_each_slave(bond, tmp, iter) {
		if (tmp->should_notify_link) {
			bond_queue_slave_event(tmp);
			bond_lower_state_changed(tmp);
			tmp->should_notify_link = 0;
		}
	}
}

static inline __be32 bond_confirm_addr(struct net_device *dev, __be32 dst, __be32 local)
{
	struct in_device *in_dev;
	__be32 addr = 0;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(dev);

	if (in_dev)
		addr = inet_confirm_addr(dev_net(dev), in_dev, dst, local,
					 RT_SCOPE_HOST);
	rcu_read_unlock();
	return addr;
}

struct bond_net {
	struct net		*net;	/* Associated network namespace */
	struct list_head	dev_list;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry	*proc_dir;
#endif
	struct class_attribute	class_attr_bonding_masters;
};

int bond_rcv_validate(const struct sk_buff *skb, struct bonding *bond, struct slave *slave);
netdev_tx_t bond_dev_queue_xmit(struct bonding *bond, struct sk_buff *skb, struct net_device *slave_dev);
int bond_create(struct net *net, const char *name);
int bond_create_sysfs(struct bond_net *net);
void bond_destroy_sysfs(struct bond_net *net);
void bond_prepare_sysfs_group(struct bonding *bond);
int bond_sysfs_slave_add(struct slave *slave);
void bond_sysfs_slave_del(struct slave *slave);
int bond_enslave(struct net_device *bond_dev, struct net_device *slave_dev,
		 struct netlink_ext_ack *extack);
int bond_release(struct net_device *bond_dev, struct net_device *slave_dev);
u32 bond_xmit_hash(struct bonding *bond, struct sk_buff *skb);
int bond_set_carrier(struct bonding *bond);
void bond_select_active_slave(struct bonding *bond);
void bond_change_active_slave(struct bonding *bond, struct slave *new_active);
void bond_create_debugfs(void);
void bond_destroy_debugfs(void);
void bond_debug_register(struct bonding *bond);
void bond_debug_unregister(struct bonding *bond);
void bond_debug_reregister(struct bonding *bond);
const char *bond_mode_name(int mode);
void bond_setup(struct net_device *bond_dev);
unsigned int bond_get_num_tx_queues(void);
int bond_netlink_init(void);
void bond_netlink_fini(void);
struct net_device *bond_option_active_slave_get_rcu(struct bonding *bond);
const char *bond_slave_link_status(s8 link);
struct bond_vlan_tag *bond_verify_device_path(struct net_device *start_dev,
					      struct net_device *end_dev,
					      int level);
int bond_update_slave_arr(struct bonding *bond, struct slave *skipslave);
void bond_slave_arr_work_rearm(struct bonding *bond, unsigned long delay);
void bond_work_init_all(struct bonding *bond);

#ifdef CONFIG_PROC_FS
void bond_create_proc_entry(struct bonding *bond);
void bond_remove_proc_entry(struct bonding *bond);
void bond_create_proc_dir(struct bond_net *bn);
void bond_destroy_proc_dir(struct bond_net *bn);
#else
static inline void bond_create_proc_entry(struct bonding *bond)
{
}

static inline void bond_remove_proc_entry(struct bonding *bond)
{
}

static inline void bond_create_proc_dir(struct bond_net *bn)
{
}

static inline void bond_destroy_proc_dir(struct bond_net *bn)
{
}
#endif

static inline struct slave *bond_slave_has_mac(struct bonding *bond,
					       const u8 *mac)
{
	struct list_head *iter;
	struct slave *tmp;

	bond_for_each_slave(bond, tmp, iter)
		if (ether_addr_equal_64bits(mac, tmp->dev->dev_addr))
			return tmp;

	return NULL;
}

/* Caller must hold rcu_read_lock() for read */
static inline bool bond_slave_has_mac_rx(struct bonding *bond, const u8 *mac)
{
	struct list_head *iter;
	struct slave *tmp;
	struct netdev_hw_addr *ha;

	bond_for_each_slave_rcu(bond, tmp, iter)
		if (ether_addr_equal_64bits(mac, tmp->dev->dev_addr))
			return true;

	if (netdev_uc_empty(bond->dev))
		return false;

	netdev_for_each_uc_addr(ha, bond->dev)
		if (ether_addr_equal_64bits(mac, ha->addr))
			return true;

	return false;
}

/* Check if the ip is present in arp ip list, or first free slot if ip == 0
 * Returns -1 if not found, index if found
 */
static inline int bond_get_targets_ip(__be32 *targets, __be32 ip)
{
	int i;

	for (i = 0; i < BOND_MAX_ARP_TARGETS; i++)
		if (targets[i] == ip)
			return i;
		else if (targets[i] == 0)
			break;

	return -1;
}

#if IS_ENABLED(CONFIG_IPV6)
static inline int bond_get_targets_ip6(struct in6_addr *targets, struct in6_addr *ip)
{
	struct in6_addr mcaddr;
	int i;

	for (i = 0; i < BOND_MAX_NS_TARGETS; i++) {
		addrconf_addr_solict_mult(&targets[i], &mcaddr);
		if ((ipv6_addr_equal(&targets[i], ip)) ||
		    (ipv6_addr_equal(&mcaddr, ip)))
			return i;
		else if (ipv6_addr_any(&targets[i]))
			break;
	}

	return -1;
}
#endif

/* exported from bond_main.c */
extern unsigned int bond_net_id;

/* exported from bond_netlink.c */
extern struct rtnl_link_ops bond_link_ops;

/* exported from bond_sysfs_slave.c */
extern const struct sysfs_ops slave_sysfs_ops;

/* exported from bond_3ad.c */
extern const u8 lacpdu_mcast_addr[];

static inline netdev_tx_t bond_tx_drop(struct net_device *dev, struct sk_buff *skb)
{
	dev_core_stats_tx_dropped_inc(dev);
	dev_kfree_skb_any(skb);
	return NET_XMIT_DROP;
}

#endif /* _NET_BONDING_H */
