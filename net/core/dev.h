/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _NET_CORE_DEV_H
#define _NET_CORE_DEV_H

#include <linux/types.h>
#include <linux/rwsem.h>
#include <linux/netdevice.h>

struct net;
struct netlink_ext_ack;
struct cpumask;

/* Random bits of netdevice that don't need to be exposed */
#define FLOW_LIMIT_HISTORY	(1 << 7)  /* must be ^2 and !overflow buckets */
struct sd_flow_limit {
	u64			count;
	unsigned int		num_buckets;
	unsigned int		history_head;
	u16			history[FLOW_LIMIT_HISTORY];
	u8			buckets[];
};

extern int netdev_flow_limit_table_len;

struct napi_struct *netdev_napi_by_id(struct net *net, unsigned int napi_id);

#ifdef CONFIG_PROC_FS
int __init dev_proc_init(void);
#else
#define dev_proc_init() 0
#endif

void linkwatch_init_dev(struct net_device *dev);
void linkwatch_run_queue(void);

void dev_addr_flush(struct net_device *dev);
int dev_addr_init(struct net_device *dev);
void dev_addr_check(struct net_device *dev);

/* sysctls not referred to from outside net/core/ */
extern int		netdev_unregister_timeout_secs;
extern int		weight_p;
extern int		dev_weight_rx_bias;
extern int		dev_weight_tx_bias;

extern struct rw_semaphore dev_addr_sem;

/* rtnl helpers */
extern struct list_head net_todo_list;
void netdev_run_todo(void);

/* netdev management, shared between various uAPI entry points */
struct netdev_name_node {
	struct hlist_node hlist;
	struct list_head list;
	struct net_device *dev;
	const char *name;
	struct rcu_head rcu;
};

int netdev_get_name(struct net *net, char *name, int ifindex);
int dev_change_name(struct net_device *dev, const char *newname);

#define netdev_for_each_altname(dev, namenode)				\
	list_for_each_entry((namenode), &(dev)->name_node->list, list)
#define netdev_for_each_altname_safe(dev, namenode, next)		\
	list_for_each_entry_safe((namenode), (next), &(dev)->name_node->list, \
				 list)

int netdev_name_node_alt_create(struct net_device *dev, const char *name);
int netdev_name_node_alt_destroy(struct net_device *dev, const char *name);

int dev_validate_mtu(struct net_device *dev, int mtu,
		     struct netlink_ext_ack *extack);
int dev_set_mtu_ext(struct net_device *dev, int mtu,
		    struct netlink_ext_ack *extack);

int dev_get_phys_port_id(struct net_device *dev,
			 struct netdev_phys_item_id *ppid);
int dev_get_phys_port_name(struct net_device *dev,
			   char *name, size_t len);

int dev_change_proto_down(struct net_device *dev, bool proto_down);
void dev_change_proto_down_reason(struct net_device *dev, unsigned long mask,
				  u32 value);

typedef int (*bpf_op_t)(struct net_device *dev, struct netdev_bpf *bpf);
int dev_change_xdp_fd(struct net_device *dev, struct netlink_ext_ack *extack,
		      int fd, int expected_fd, u32 flags);

int dev_change_tx_queue_len(struct net_device *dev, unsigned long new_len);
void dev_set_group(struct net_device *dev, int new_group);
int dev_change_carrier(struct net_device *dev, bool new_carrier);

void __dev_set_rx_mode(struct net_device *dev);

void __dev_notify_flags(struct net_device *dev, unsigned int old_flags,
			unsigned int gchanges, u32 portid,
			const struct nlmsghdr *nlh);

void unregister_netdevice_many_notify(struct list_head *head,
				      u32 portid, const struct nlmsghdr *nlh);

static inline void netif_set_gso_max_size(struct net_device *dev,
					  unsigned int size)
{
	/* dev->gso_max_size is read locklessly from sk_setup_caps() */
	WRITE_ONCE(dev->gso_max_size, size);
	if (size <= GSO_LEGACY_MAX_SIZE)
		WRITE_ONCE(dev->gso_ipv4_max_size, size);
}

static inline void netif_set_gso_max_segs(struct net_device *dev,
					  unsigned int segs)
{
	/* dev->gso_max_segs is read locklessly from sk_setup_caps() */
	WRITE_ONCE(dev->gso_max_segs, segs);
}

static inline void netif_set_gro_max_size(struct net_device *dev,
					  unsigned int size)
{
	/* This pairs with the READ_ONCE() in skb_gro_receive() */
	WRITE_ONCE(dev->gro_max_size, size);
	if (size <= GRO_LEGACY_MAX_SIZE)
		WRITE_ONCE(dev->gro_ipv4_max_size, size);
}

static inline void netif_set_gso_ipv4_max_size(struct net_device *dev,
					       unsigned int size)
{
	/* dev->gso_ipv4_max_size is read locklessly from sk_setup_caps() */
	WRITE_ONCE(dev->gso_ipv4_max_size, size);
}

static inline void netif_set_gro_ipv4_max_size(struct net_device *dev,
					       unsigned int size)
{
	/* This pairs with the READ_ONCE() in skb_gro_receive() */
	WRITE_ONCE(dev->gro_ipv4_max_size, size);
}

int rps_cpumask_housekeeping(struct cpumask *mask);

#if defined(CONFIG_DEBUG_NET) && defined(CONFIG_BPF_SYSCALL)
void xdp_do_check_flushed(struct napi_struct *napi);
#else
static inline void xdp_do_check_flushed(struct napi_struct *napi) { }
#endif

void kick_defer_list_purge(struct softnet_data *sd, unsigned int cpu);

#define XMIT_RECURSION_LIMIT	8

#ifndef CONFIG_PREEMPT_RT
static inline bool dev_xmit_recursion(void)
{
	return unlikely(__this_cpu_read(softnet_data.xmit.recursion) >
			XMIT_RECURSION_LIMIT);
}

static inline void dev_xmit_recursion_inc(void)
{
	__this_cpu_inc(softnet_data.xmit.recursion);
}

static inline void dev_xmit_recursion_dec(void)
{
	__this_cpu_dec(softnet_data.xmit.recursion);
}
#else
static inline bool dev_xmit_recursion(void)
{
	return unlikely(current->net_xmit.recursion > XMIT_RECURSION_LIMIT);
}

static inline void dev_xmit_recursion_inc(void)
{
	current->net_xmit.recursion++;
}

static inline void dev_xmit_recursion_dec(void)
{
	current->net_xmit.recursion--;
}
#endif

int dev_set_hwtstamp_phylib(struct net_device *dev,
			    struct kernel_hwtstamp_config *cfg,
			    struct netlink_ext_ack *extack);

#endif
