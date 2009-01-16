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

struct netpoll {
	struct net_device *dev;
	char dev_name[IFNAMSIZ];
	const char *name;
	void (*rx_hook)(struct netpoll *, int, char *, int);

	u32 local_ip, remote_ip;
	u16 local_port, remote_port;
	u8 remote_mac[ETH_ALEN];
};

struct netpoll_info {
	atomic_t refcnt;
	int rx_flags;
	spinlock_t rx_lock;
	struct netpoll *rx_np; /* netpoll that registered an rx_hook */
	struct sk_buff_head arp_tx; /* list of arp requests to reply to */
	struct sk_buff_head txq;
	struct delayed_work tx_work;
};

void netpoll_poll(struct netpoll *np);
void netpoll_send_udp(struct netpoll *np, const char *msg, int len);
void netpoll_print_options(struct netpoll *np);
int netpoll_parse_options(struct netpoll *np, char *opt);
int netpoll_setup(struct netpoll *np);
int netpoll_trap(void);
void netpoll_set_trap(int trap);
void netpoll_cleanup(struct netpoll *np);
int __netpoll_rx(struct sk_buff *skb);


#ifdef CONFIG_NETPOLL
static inline int netpoll_rx(struct sk_buff *skb)
{
	struct netpoll_info *npinfo = skb->dev->npinfo;
	unsigned long flags;
	int ret = 0;

	if (!npinfo || (!npinfo->rx_np && !npinfo->rx_flags))
		return 0;

	spin_lock_irqsave(&npinfo->rx_lock, flags);
	/* check rx_flags again with the lock held */
	if (npinfo->rx_flags && __netpoll_rx(skb))
		ret = 1;
	spin_unlock_irqrestore(&npinfo->rx_lock, flags);

	return ret;
}

static inline int netpoll_receive_skb(struct sk_buff *skb)
{
	if (!list_empty(&skb->dev->napi_list))
		return netpoll_rx(skb);
	return 0;
}

static inline void *netpoll_poll_lock(struct napi_struct *napi)
{
	struct net_device *dev = napi->dev;

	rcu_read_lock(); /* deal with race on ->npinfo */
	if (dev && dev->npinfo) {
		spin_lock(&napi->poll_lock);
		napi->poll_owner = smp_processor_id();
		return napi;
	}
	return NULL;
}

static inline void netpoll_poll_unlock(void *have)
{
	struct napi_struct *napi = have;

	if (napi) {
		napi->poll_owner = -1;
		spin_unlock(&napi->poll_lock);
	}
	rcu_read_unlock();
}

#else
static inline int netpoll_rx(struct sk_buff *skb)
{
	return 0;
}
static inline int netpoll_receive_skb(struct sk_buff *skb)
{
	return 0;
}
static inline void *netpoll_poll_lock(struct napi_struct *napi)
{
	return NULL;
}
static inline void netpoll_poll_unlock(void *have)
{
}
static inline void netpoll_netdev_init(struct net_device *dev)
{
}
#endif

#endif
