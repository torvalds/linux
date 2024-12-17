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
	__u32		all[4];
	__be32		ip;
	__be32		ip6[4];
	struct in_addr	in;
	struct in6_addr	in6;
};

struct netpoll {
	struct net_device *dev;
	netdevice_tracker dev_tracker;
	char dev_name[IFNAMSIZ];
	const char *name;

	union inet_addr local_ip, remote_ip;
	bool ipv6;
	u16 local_port, remote_port;
	u8 remote_mac[ETH_ALEN];
	struct sk_buff_head skb_pool;
};

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

void netpoll_send_udp(struct netpoll *np, const char *msg, int len);
void netpoll_print_options(struct netpoll *np);
int netpoll_parse_options(struct netpoll *np, char *opt);
int __netpoll_setup(struct netpoll *np, struct net_device *ndev);
int netpoll_setup(struct netpoll *np);
void __netpoll_cleanup(struct netpoll *np);
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
