#ifndef __LINUX_MROUTE_BASE_H
#define __LINUX_MROUTE_BASE_H

#include <linux/netdevice.h>
#include <linux/rhashtable.h>
#include <net/net_namespace.h>
#include <net/sock.h>

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

#ifndef MAXVIFS
/* This one is nasty; value is defined in uapi using different symbols for
 * mroute and morute6 but both map into same 32.
 */
#define MAXVIFS	32
#endif

#define VIF_EXISTS(_mrt, _idx) (!!((_mrt)->vif_table[_idx].dev))

/**
 * struct mr_table - a multicast routing table
 * @list: entry within a list of multicast routing tables
 * @net: net where this table belongs
 * @id: identifier of the table
 * @mroute_sk: socket associated with the table
 * @ipmr_expire_timer: timer for handling unresolved routes
 * @mfc_unres_queue: list of unresolved MFC entries
 * @vif_table: array containing all possible vifs
 * @mfc_hash: Hash table of all resolved routes for easy lookup
 * @mfc_cache_list: list of resovled routes for possible traversal
 * @maxvif: Identifier of highest value vif currently in use
 * @cache_resolve_queue_len: current size of unresolved queue
 * @mroute_do_assert: Whether to inform userspace on wrong ingress
 * @mroute_do_pim: Whether to receive IGMP PIMv1
 * @mroute_reg_vif_num: PIM-device vif index
 */
struct mr_table {
	struct list_head	list;
	possible_net_t		net;
	u32			id;
	struct sock __rcu	*mroute_sk;
	struct timer_list	ipmr_expire_timer;
	struct list_head	mfc_unres_queue;
	struct vif_device	vif_table[MAXVIFS];
	struct rhltable		mfc_hash;
	struct list_head	mfc_cache_list;
	int			maxvif;
	atomic_t		cache_resolve_queue_len;
	bool			mroute_do_assert;
	bool			mroute_do_pim;
	int			mroute_reg_vif_num;
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
