/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common code for low-level network console, dump, and debugger code
 *
 * Derived from netconsole, kgdb-over-ethernet, and netdump patches
 */

#ifndef _LINUX_NETPOLL_H
#define _LINUX_NETPOLL_H

#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/refcount.h>

union inet_addr {
	__be32		ip;
	struct in6_addr	in6;
};

struct netpoll {
	struct net_device *dev;
	netdevice_tracker dev_tracker;
	/*
	 * Either dev_name or dev_mac can be used to specify the local
	 * interface - dev_name is used if it is a nonempty string, else
	 * dev_mac is used.
	 */
	char dev_name[IFNAMSIZ];
	u8 dev_mac[ETH_ALEN];
	const char *name;

	union inet_addr local_ip, remote_ip;
	bool ipv6;
	u16 local_port, remote_port;
	u8 remote_mac[ETH_ALEN];
	struct sk_buff_head skb_pool;
	struct work_struct refill_wq;
};

#define np_info(np, fmt, ...)				\
	pr_info("%s: " fmt, np->name, ##__VA_ARGS__)
#define np_err(np, fmt, ...)				\
	pr_err("%s: " fmt, np->name, ##__VA_ARGS__)
#define np_notice(np, fmt, ...)				\
	pr_notice("%s: " fmt, np->name, ##__VA_ARGS__)

struct netpoll_info {
	refcount_t refcnt;

	struct semaphore dev_lock;

	struct sk_buff_head txq;

	struct delayed_work tx_work;

	struct netpoll *netpoll;
	struct rcu_head rcu;
};

#ifdef CONFIG_NETPOLL
void netpoll_poll_dev(struct net_device *dev);
void netpoll_poll_disable(struct net_device *dev);
void netpoll_poll_enable(struct net_device *dev);
#else
static inline void netpoll_poll_disable(struct net_device *dev) { return; }
static inline void netpoll_poll_enable(struct net_device *dev) { return; }
#endif

int netpoll_send_udp(struct netpoll *np, const char *msg, int len);
int __netpoll_setup(struct netpoll *np, struct net_device *ndev);
int netpoll_setup(struct netpoll *np);
void __netpoll_free(struct netpoll *np);
void netpoll_cleanup(struct netpoll *np);
void do_netpoll_cleanup(struct netpoll *np);
netdev_tx_t netpoll_send_skb(struct netpoll *np, struct sk_buff *skb);

#ifdef CONFIG_NETPOLL
static inline void *netpoll_poll_lock(struct napi_struct *napi)
{
	struct net_device *dev = napi->dev;

	if (dev && rcu_access_pointer(dev->npinfo)) {
		int owner = smp_processor_id();

		while (cmpxchg(&napi->poll_owner, -1, owner) != -1)
			cpu_relax();

		return napi;
	}
	return NULL;
}

static inline void netpoll_poll_unlock(void *have)
{
	struct napi_struct *napi = have;

	if (napi)
		smp_store_release(&napi->poll_owner, -1);
}

static inline bool netpoll_tx_running(struct net_device *dev)
{
	return irqs_disabled();
}

#else
static inline void *netpoll_poll_lock(struct napi_struct *napi)
{
	return NULL;
}
static inline void netpoll_poll_unlock(void *have)
{
}
static inline bool netpoll_tx_running(struct net_device *dev)
{
	return false;
}
#endif

#endif
