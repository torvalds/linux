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
 * struct mr_mfc - common multicast routing entries
 * @mnode: rhashtable list
 * @mfc_parent: source interface (iif)
 * @mfc_flags: entry flags
 * @expires: unresolved entry expire time
 * @unresolved: unresolved cached skbs
 * @last_assert: time of last assert
 * @minvif: minimum VIF id
 * @maxvif: maximum VIF id
 * @bytes: bytes that have passed for this entry
 * @pkt: packets that have passed for this entry
 * @wrong_if: number of wrong source interface hits
 * @lastuse: time of last use of the group (traffic or update)
 * @ttls: OIF TTL threshold array
 * @refcount: reference count for this entry
 * @list: global entry list
 * @rcu: used for entry destruction
 */
struct mr_mfc {
	struct rhlist_head mnode;
	unsigned short mfc_parent;
	int mfc_flags;

	union {
		struct {
			unsigned long expires;
			struct sk_buff_head unresolved;
		} unres;
		struct {
			unsigned long last_assert;
			int minvif;
			int maxvif;
			unsigned long bytes;
			unsigned long pkt;
			unsigned long wrong_if;
			unsigned long lastuse;
			unsigned char ttls[MAXVIFS];
			refcount_t refcount;
		} res;
	} mfc_un;
	struct list_head list;
	struct rcu_head	rcu;
};

struct mr_table;

/**
 * struct mr_table_ops - callbacks and info for protocol-specific ops
 * @rht_params: parameters for accessing the MFC hash
 * @cmparg_any: a hash key to be used for matching on (*,*) routes
 */
struct mr_table_ops {
	const struct rhashtable_params *rht_params;
	void *cmparg_any;
};

/**
 * struct mr_table - a multicast routing table
 * @list: entry within a list of multicast routing tables
 * @net: net where this table belongs
 * @ops: protocol specific operations
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
	struct mr_table_ops	ops;
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

struct mr_table *
mr_table_alloc(struct net *net, u32 id,
	       struct mr_table_ops *ops,
	       void (*expire_func)(struct timer_list *t),
	       void (*table_set)(struct mr_table *mrt,
				 struct net *net));

/* These actually return 'struct mr_mfc *', but to avoid need for explicit
 * castings they simply return void.
 */
void *mr_mfc_find_parent(struct mr_table *mrt,
			 void *hasharg, int parent);
void *mr_mfc_find_any_parent(struct mr_table *mrt, int vifi);
void *mr_mfc_find_any(struct mr_table *mrt, int vifi, void *hasharg);

#else
static inline void vif_device_init(struct vif_device *v,
				   struct net_device *dev,
				   unsigned long rate_limit,
				   unsigned char threshold,
				   unsigned short flags,
				   unsigned short get_iflink_mask)
{
}

static inline void *
mr_table_alloc(struct net *net, u32 id,
	       struct mr_table_ops *ops,
	       void (*expire_func)(struct timer_list *t),
	       void (*table_set)(struct mr_table *mrt,
				 struct net *net))
{
	return NULL;
}

static inline void *mr_mfc_find_parent(struct mr_table *mrt,
				       void *hasharg, int parent)
{
	return NULL;
}

static inline void *mr_mfc_find_any_parent(struct mr_table *mrt,
					   int vifi)
{
	return NULL;
}

static inline struct mr_mfc *mr_mfc_find_any(struct mr_table *mrt,
					     int vifi, void *hasharg)
{
	return NULL;
}
#endif

static inline void *mr_mfc_find(struct mr_table *mrt, void *hasharg)
{
	return mr_mfc_find_parent(mrt, hasharg, -1);
}
#endif
