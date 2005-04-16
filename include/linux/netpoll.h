/*
 * Common code for low-level network console, dump, and debugger code
 *
 * Derived from netconsole, kgdb-over-ethernet, and netdump patches
 */

#ifndef _LINUX_NETPOLL_H
#define _LINUX_NETPOLL_H

#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/list.h>

struct netpoll;

struct netpoll {
	struct net_device *dev;
	char dev_name[16], *name;
	int rx_flags;
	void (*rx_hook)(struct netpoll *, int, char *, int);
	void (*drop)(struct sk_buff *skb);
	u32 local_ip, remote_ip;
	u16 local_port, remote_port;
	unsigned char local_mac[6], remote_mac[6];
	spinlock_t poll_lock;
	int poll_owner;
};

void netpoll_poll(struct netpoll *np);
void netpoll_send_udp(struct netpoll *np, const char *msg, int len);
int netpoll_parse_options(struct netpoll *np, char *opt);
int netpoll_setup(struct netpoll *np);
int netpoll_trap(void);
void netpoll_set_trap(int trap);
void netpoll_cleanup(struct netpoll *np);
int __netpoll_rx(struct sk_buff *skb);
void netpoll_queue(struct sk_buff *skb);

#ifdef CONFIG_NETPOLL
static inline int netpoll_rx(struct sk_buff *skb)
{
	return skb->dev->np && skb->dev->np->rx_flags && __netpoll_rx(skb);
}

static inline void netpoll_poll_lock(struct net_device *dev)
{
	if (dev->np) {
		spin_lock(&dev->np->poll_lock);
		dev->np->poll_owner = smp_processor_id();
	}
}

static inline void netpoll_poll_unlock(struct net_device *dev)
{
	if (dev->np) {
		spin_unlock(&dev->np->poll_lock);
		dev->np->poll_owner = -1;
	}
}

#else
#define netpoll_rx(a) 0
#define netpoll_poll_lock(a)
#define netpoll_poll_unlock(a)
#endif

#endif
