#ifndef _LINUX_IF_MACVLAN_H
#define _LINUX_IF_MACVLAN_H

#include <linux/if_link.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <net/netlink.h>
#include <linux/u64_stats_sync.h>

#if defined(CONFIG_MACVTAP) || defined(CONFIG_MACVTAP_MODULE)
struct socket *macvtap_get_socket(struct file *);
#else
#include <linux/err.h>
#include <linux/errno.h>
struct file;
struct socket;
static inline struct socket *macvtap_get_socket(struct file *f)
{
	return ERR_PTR(-EINVAL);
}
#endif /* CONFIG_MACVTAP */

struct macvlan_port;
struct macvtap_queue;

/**
 *	struct macvlan_rx_stats - MACVLAN percpu rx stats
 *	@rx_packets: number of received packets
 *	@rx_bytes: number of received bytes
 *	@rx_multicast: number of received multicast packets
 *	@syncp: synchronization point for 64bit counters
 *	@rx_errors: number of errors
 */
struct macvlan_rx_stats {
	u64			rx_packets;
	u64			rx_bytes;
	u64			rx_multicast;
	struct u64_stats_sync	syncp;
	unsigned long		rx_errors;
};

/*
 * Maximum times a macvtap device can be opened. This can be used to
 * configure the number of receive queue, e.g. for multiqueue virtio.
 */
#define MAX_MACVTAP_QUEUES	(NR_CPUS < 16 ? NR_CPUS : 16)

struct macvlan_dev {
	struct net_device	*dev;
	struct list_head	list;
	struct hlist_node	hlist;
	struct macvlan_port	*port;
	struct net_device	*lowerdev;
	struct macvlan_rx_stats __percpu *rx_stats;
	enum macvlan_mode	mode;
	int (*receive)(struct sk_buff *skb);
	int (*forward)(struct net_device *dev, struct sk_buff *skb);
	struct macvtap_queue	*taps[MAX_MACVTAP_QUEUES];
	int			numvtaps;
};

static inline void macvlan_count_rx(const struct macvlan_dev *vlan,
				    unsigned int len, bool success,
				    bool multicast)
{
	struct macvlan_rx_stats *rx_stats;

	rx_stats = this_cpu_ptr(vlan->rx_stats);
	if (likely(success)) {
		u64_stats_update_begin(&rx_stats->syncp);
		rx_stats->rx_packets++;;
		rx_stats->rx_bytes += len;
		if (multicast)
			rx_stats->rx_multicast++;
		u64_stats_update_end(&rx_stats->syncp);
	} else {
		rx_stats->rx_errors++;
	}
}

extern void macvlan_common_setup(struct net_device *dev);

extern int macvlan_common_newlink(struct net *src_net, struct net_device *dev,
				  struct nlattr *tb[], struct nlattr *data[],
				  int (*receive)(struct sk_buff *skb),
				  int (*forward)(struct net_device *dev,
						 struct sk_buff *skb));

extern void macvlan_count_rx(const struct macvlan_dev *vlan,
			     unsigned int len, bool success,
			     bool multicast);

extern void macvlan_dellink(struct net_device *dev, struct list_head *head);

extern int macvlan_link_register(struct rtnl_link_ops *ops);

extern netdev_tx_t macvlan_start_xmit(struct sk_buff *skb,
				      struct net_device *dev);

#endif /* _LINUX_IF_MACVLAN_H */
