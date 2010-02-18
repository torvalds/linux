#ifndef _LINUX_IF_MACVLAN_H
#define _LINUX_IF_MACVLAN_H

#include <linux/if_link.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <net/netlink.h>

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
 *	@multicast: number of received multicast packets
 *	@rx_errors: number of errors
 */
struct macvlan_rx_stats {
	unsigned long rx_packets;
	unsigned long rx_bytes;
	unsigned long multicast;
	unsigned long rx_errors;
};

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
	struct macvtap_queue	*tap;
};

static inline void macvlan_count_rx(const struct macvlan_dev *vlan,
				    unsigned int len, bool success,
				    bool multicast)
{
	struct macvlan_rx_stats *rx_stats;

	rx_stats = per_cpu_ptr(vlan->rx_stats, smp_processor_id());
	if (likely(success)) {
		rx_stats->rx_packets++;;
		rx_stats->rx_bytes += len;
		if (multicast)
			rx_stats->multicast++;
	} else {
		rx_stats->rx_errors++;
	}
}

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


extern struct sk_buff *(*macvlan_handle_frame_hook)(struct sk_buff *);

#endif /* _LINUX_IF_MACVLAN_H */
