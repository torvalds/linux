#ifndef __LINUX_MROUTE_BASE_H
#define __LINUX_MROUTE_BASE_H

#include <linux/netdevice.h>

/**
 * struct vif_device - interface representor for multicast routing
 * @dev: network device being used
 * @bytes_in: statistic; bytes ingressing
 * @bytes_out: statistic; bytes egresing
 * @pkt_in: statistic; packets ingressing
 * @pkt_out: statistic; packets egressing
 * @rate_limit: Traffic shaping (NI)
 * @threshold: TTL threshold
 * @flags: Control flags
 * @link: Physical interface index
 * @dev_parent_id: device parent id
 * @local: Local address
 * @remote: Remote address for tunnels
 */
struct vif_device {
	struct net_device *dev;
	unsigned long bytes_in, bytes_out;
	unsigned long pkt_in, pkt_out;
	unsigned long rate_limit;
	unsigned char threshold;
	unsigned short flags;
	int link;

	/* Currently only used by ipmr */
	struct netdev_phys_item_id dev_parent_id;
	__be32 local, remote;
};

#ifdef CONFIG_IP_MROUTE_COMMON
void vif_device_init(struct vif_device *v,
		     struct net_device *dev,
		     unsigned long rate_limit,
		     unsigned char threshold,
		     unsigned short flags,
		     unsigned short get_iflink_mask);
#else
static inline void vif_device_init(struct vif_device *v,
				   struct net_device *dev,
				   unsigned long rate_limit,
				   unsigned char threshold,
				   unsigned short flags,
				   unsigned short get_iflink_mask)
{
}
#endif
#endif
