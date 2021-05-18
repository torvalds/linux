/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IF_MACVLAN_H
#define _LINUX_IF_MACVLAN_H

#include <linux/if_link.h>
#include <linux/if_vlan.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <net/netlink.h>
#include <linux/u64_stats_sync.h>

struct macvlan_port;

#define MACVLAN_MC_FILTER_BITS	8
#define MACVLAN_MC_FILTER_SZ	(1 << MACVLAN_MC_FILTER_BITS)

struct macvlan_dev {
	struct net_device	*dev;
	struct list_head	list;
	struct hlist_node	hlist;
	struct macvlan_port	*port;
	struct net_device	*lowerdev;
	void			*accel_priv;
	struct vlan_pcpu_stats __percpu *pcpu_stats;

	DECLARE_BITMAP(mc_filter, MACVLAN_MC_FILTER_SZ);

	netdev_features_t	set_features;
	enum macvlan_mode	mode;
	u16			flags;
	unsigned int		macaddr_count;
	u32			bc_queue_len_req;
#ifdef CONFIG_NET_POLL_CONTROLLER
	struct netpoll		*netpoll;
#endif
};

static inline void macvlan_count_rx(const struct macvlan_dev *vlan,
				    unsigned int len, bool success,
				    bool multicast)
{
	if (likely(success)) {
		struct vlan_pcpu_stats *pcpu_stats;

		pcpu_stats = get_cpu_ptr(vlan->pcpu_stats);
		u64_stats_update_begin(&pcpu_stats->syncp);
		pcpu_stats->rx_packets++;
		pcpu_stats->rx_bytes += len;
		if (multicast)
			pcpu_stats->rx_multicast++;
		u64_stats_update_end(&pcpu_stats->syncp);
		put_cpu_ptr(vlan->pcpu_stats);
	} else {
		this_cpu_inc(vlan->pcpu_stats->rx_errors);
	}
}

extern void macvlan_common_setup(struct net_device *dev);

extern int macvlan_common_newlink(struct net *src_net, struct net_device *dev,
				  struct nlattr *tb[], struct nlattr *data[],
				  struct netlink_ext_ack *extack);

extern void macvlan_dellink(struct net_device *dev, struct list_head *head);

extern int macvlan_link_register(struct rtnl_link_ops *ops);

#if IS_ENABLED(CONFIG_MACVLAN)
static inline struct net_device *
macvlan_dev_real_dev(const struct net_device *dev)
{
	struct macvlan_dev *macvlan = netdev_priv(dev);

	return macvlan->lowerdev;
}
#else
static inline struct net_device *
macvlan_dev_real_dev(const struct net_device *dev)
{
	BUG();
	return NULL;
}
#endif

static inline void *macvlan_accel_priv(struct net_device *dev)
{
	struct macvlan_dev *macvlan = netdev_priv(dev);

	return macvlan->accel_priv;
}

static inline bool macvlan_supports_dest_filter(struct net_device *dev)
{
	struct macvlan_dev *macvlan = netdev_priv(dev);

	return macvlan->mode == MACVLAN_MODE_PRIVATE ||
	       macvlan->mode == MACVLAN_MODE_VEPA ||
	       macvlan->mode == MACVLAN_MODE_BRIDGE;
}

static inline int macvlan_release_l2fw_offload(struct net_device *dev)
{
	struct macvlan_dev *macvlan = netdev_priv(dev);

	macvlan->accel_priv = NULL;
	return dev_uc_add(macvlan->lowerdev, dev->dev_addr);
}
#endif /* _LINUX_IF_MACVLAN_H */
