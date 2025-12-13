/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _NET_CORE_DEV_H
#define _NET_CORE_DEV_H

#include <linux/cleanup.h>
#include <linux/types.h>
#include <linux/rwsem.h>
#include <linux/netdevice.h>
#include <net/netdev_lock.h>

struct net;
struct netlink_ext_ack;
struct cpumask;

/* Random bits of netdevice that don't need to be exposed */
#define FLOW_LIMIT_HISTORY	(1 << 7)  /* must be ^2 and !overflow buckets */
struct sd_flow_limit {
	struct rcu_head		rcu;
	unsigned int		count;
	u8			log_buckets;
	unsigned int		history_head;
	u16			history[FLOW_LIMIT_HISTORY];
	u8			buckets[];
};

extern int netdev_flow_limit_table_len;

struct napi_struct *
netdev_napi_by_id_lock(struct net *net, unsigned int napi_id);
struct net_device *dev_get_by_napi_id(unsigned int napi_id);

struct net_device *netdev_get_by_index_lock(struct net *net, int ifindex);
struct net_device *__netdev_put_lock(struct net_device *dev, struct net *net);
struct net_device *
netdev_xa_find_lock(struct net *net, struct net_device *dev,
		    unsigned long *index);

DEFINE_FREE(netdev_unlock, struct net_device *, if (_T) netdev_unlock(_T));

#define for_each_netdev_lock_scoped(net, var_name, ifindex)		\
	for (struct net_device *var_name __free(netdev_unlock) = NULL;	\
	     (var_name = netdev_xa_find_lock(net, var_name, &ifindex)); \
	     ifindex++)

struct net_device *
netdev_get_by_index_lock_ops_compat(struct net *net, int ifindex);
struct net_device *
netdev_xa_find_lock_ops_compat(struct net *net, struct net_device *dev,
			       unsigned long *index);

DEFINE_FREE(netdev_unlock_ops_compat, struct net_device *,
	    if (_T) netdev_unlock_ops_compat(_T));

#define for_each_netdev_lock_ops_compat_scoped(net, var_name, ifindex)	\
	for (struct net_device *var_name __free(netdev_unlock_ops_compat) = NULL; \
	     (var_name = netdev_xa_find_lock_ops_compat(net, var_name,	\
							&ifindex));	\
	     ifindex++)

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

#if IS_ENABLED(CONFIG_NET_SHAPER)
void net_shaper_flush_netdev(struct net_device *dev);
void net_shaper_set_real_num_tx_queues(struct net_device *dev,
				       unsigned int txq);
#else
static inline void net_shaper_flush_netdev(struct net_device *dev) {}
static inline void net_shaper_set_real_num_tx_queues(struct net_device *dev,
						     unsigned int txq) {}
#endif

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
int netif_change_name(struct net_device *dev, const char *newname);
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
int netif_set_mtu_ext(struct net_device *dev, int new_mtu,
		      struct netlink_ext_ack *extack);

int dev_get_phys_port_id(struct net_device *dev,
			 struct netdev_phys_item_id *ppid);
int dev_get_phys_port_name(struct net_device *dev,
			   char *name, size_t len);

int netif_change_proto_down(struct net_device *dev, bool proto_down);
int dev_change_proto_down(struct net_device *dev, bool proto_down);
void netdev_change_proto_down_reason_locked(struct net_device *dev,
					    unsigned long mask, u32 value);

typedef int (*bpf_op_t)(struct net_device *dev, struct netdev_bpf *bpf);
int dev_change_xdp_fd(struct net_device *dev, struct netlink_ext_ack *extack,
		      int fd, int expected_fd, u32 flags);

int netif_change_tx_queue_len(struct net_device *dev, unsigned long new_len);
int dev_change_tx_queue_len(struct net_device *dev, unsigned long new_len);
void netif_set_group(struct net_device *dev, int new_group);
void dev_set_group(struct net_device *dev, int new_group);
int netif_change_carrier(struct net_device *dev, bool new_carrier);
int dev_change_carrier(struct net_device *dev, bool new_carrier);

void __dev_set_rx_mode(struct net_device *dev);

void __dev_notify_flags(struct net_device *dev, unsigned int old_flags,
			unsigned int gchanges, u32 portid,
			const struct nlmsghdr *nlh);

void unregister_netdevice_many_notify(struct list_head *head,
				      u32 portid, const struct nlmsghdr *nlh);

static inline void netif_set_up(struct net_device *dev, bool value)
{
	if (value)
		dev->flags |= IFF_UP;
	else
		dev->flags &= ~IFF_UP;

	if (!netdev_need_ops_lock(dev))
		netdev_lock(dev);
	dev->up = value;
	if (!netdev_need_ops_lock(dev))
		netdev_unlock(dev);
}

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

/**
 * napi_get_defer_hard_irqs - get the NAPI's defer_hard_irqs
 * @n: napi struct to get the defer_hard_irqs field from
 *
 * Return: the per-NAPI value of the defar_hard_irqs field.
 */
static inline u32 napi_get_defer_hard_irqs(const struct napi_struct *n)
{
	return READ_ONCE(n->defer_hard_irqs);
}

/**
 * napi_set_defer_hard_irqs - set the defer_hard_irqs for a napi
 * @n: napi_struct to set the defer_hard_irqs field
 * @defer: the value the field should be set to
 */
static inline void napi_set_defer_hard_irqs(struct napi_struct *n, u32 defer)
{
	WRITE_ONCE(n->defer_hard_irqs, defer);
}

/**
 * netdev_set_defer_hard_irqs - set defer_hard_irqs for all NAPIs of a netdev
 * @netdev: the net_device for which all NAPIs will have defer_hard_irqs set
 * @defer: the defer_hard_irqs value to set
 */
static inline void netdev_set_defer_hard_irqs(struct net_device *netdev,
					      u32 defer)
{
	unsigned int count = max(netdev->num_rx_queues,
				 netdev->num_tx_queues);
	struct napi_struct *napi;
	int i;

	WRITE_ONCE(netdev->napi_defer_hard_irqs, defer);
	list_for_each_entry(napi, &netdev->napi_list, dev_list)
		napi_set_defer_hard_irqs(napi, defer);

	for (i = 0; i < count; i++)
		netdev->napi_config[i].defer_hard_irqs = defer;
}

/**
 * napi_get_gro_flush_timeout - get the gro_flush_timeout
 * @n: napi struct to get the gro_flush_timeout from
 *
 * Return: the per-NAPI value of the gro_flush_timeout field.
 */
static inline unsigned long
napi_get_gro_flush_timeout(const struct napi_struct *n)
{
	return READ_ONCE(n->gro_flush_timeout);
}

/**
 * napi_set_gro_flush_timeout - set the gro_flush_timeout for a napi
 * @n: napi struct to set the gro_flush_timeout
 * @timeout: timeout value to set
 *
 * napi_set_gro_flush_timeout sets the per-NAPI gro_flush_timeout
 */
static inline void napi_set_gro_flush_timeout(struct napi_struct *n,
					      unsigned long timeout)
{
	WRITE_ONCE(n->gro_flush_timeout, timeout);
}

/**
 * netdev_set_gro_flush_timeout - set gro_flush_timeout of a netdev's NAPIs
 * @netdev: the net_device for which all NAPIs will have gro_flush_timeout set
 * @timeout: the timeout value to set
 */
static inline void netdev_set_gro_flush_timeout(struct net_device *netdev,
						unsigned long timeout)
{
	unsigned int count = max(netdev->num_rx_queues,
				 netdev->num_tx_queues);
	struct napi_struct *napi;
	int i;

	WRITE_ONCE(netdev->gro_flush_timeout, timeout);
	list_for_each_entry(napi, &netdev->napi_list, dev_list)
		napi_set_gro_flush_timeout(napi, timeout);

	for (i = 0; i < count; i++)
		netdev->napi_config[i].gro_flush_timeout = timeout;
}

/**
 * napi_get_irq_suspend_timeout - get the irq_suspend_timeout
 * @n: napi struct to get the irq_suspend_timeout from
 *
 * Return: the per-NAPI value of the irq_suspend_timeout field.
 */
static inline unsigned long
napi_get_irq_suspend_timeout(const struct napi_struct *n)
{
	return READ_ONCE(n->irq_suspend_timeout);
}

/**
 * napi_set_irq_suspend_timeout - set the irq_suspend_timeout for a napi
 * @n: napi struct to set the irq_suspend_timeout
 * @timeout: timeout value to set
 *
 * napi_set_irq_suspend_timeout sets the per-NAPI irq_suspend_timeout
 */
static inline void napi_set_irq_suspend_timeout(struct napi_struct *n,
						unsigned long timeout)
{
	WRITE_ONCE(n->irq_suspend_timeout, timeout);
}

static inline enum netdev_napi_threaded napi_get_threaded(struct napi_struct *n)
{
	if (test_bit(NAPI_STATE_THREADED, &n->state))
		return NETDEV_NAPI_THREADED_ENABLED;

	return NETDEV_NAPI_THREADED_DISABLED;
}

static inline enum netdev_napi_threaded
napi_get_threaded_config(struct net_device *dev, struct napi_struct *n)
{
	if (n->config)
		return n->config->threaded;
	return dev->threaded;
}

int napi_set_threaded(struct napi_struct *n,
		      enum netdev_napi_threaded threaded);

int netif_set_threaded(struct net_device *dev,
		       enum netdev_napi_threaded threaded);

int rps_cpumask_housekeeping(struct cpumask *mask);

#if defined(CONFIG_DEBUG_NET) && defined(CONFIG_BPF_SYSCALL)
void xdp_do_check_flushed(struct napi_struct *napi);
#else
static inline void xdp_do_check_flushed(struct napi_struct *napi) { }
#endif

/* Best effort check that NAPI is not idle (can't be scheduled to run) */
static inline void napi_assert_will_not_race(const struct napi_struct *napi)
{
	/* uninitialized instance, can't race */
	if (!napi->poll_list.next)
		return;

	/* SCHED bit is set on disabled instances */
	WARN_ON(!test_bit(NAPI_STATE_SCHED, &napi->state));
	WARN_ON(READ_ONCE(napi->list_owner) != -1);
}

void kick_defer_list_purge(unsigned int cpu);

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
int dev_get_hwtstamp_phylib(struct net_device *dev,
			    struct kernel_hwtstamp_config *cfg);
int net_hwtstamp_validate(const struct kernel_hwtstamp_config *cfg);

#endif
