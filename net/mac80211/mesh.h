/*
 * Copyright (c) 2008 open80211s Ltd.
 * Authors:    Luis Carlos Cobo <luisca@cozybit.com>
 *             Javier Cardona <javier@cozybit.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IEEE80211S_H
#define IEEE80211S_H

#include <linux/types.h>
#include <linux/jhash.h>
#include <asm/unaligned.h>
#include "ieee80211_i.h"


/* Data structures */

/**
 * enum mesh_path_flags - mac80211 mesh path flags
 *
 *
 *
 * @MESH_PATH_ACTIVE: the mesh path can be used for forwarding
 * @MESH_PATH_RESOLVING: the discovery process is running for this mesh path
 * @MESH_PATH_DSN_VALID: the mesh path contains a valid destination sequence
 * 	number
 * @MESH_PATH_FIXED: the mesh path has been manually set and should not be
 * 	modified
 * @MESH_PATH_RESOLVED: the mesh path can has been resolved
 *
 * MESH_PATH_RESOLVED is used by the mesh path timer to
 * decide when to stop or cancel the mesh path discovery.
 */
enum mesh_path_flags {
	MESH_PATH_ACTIVE =	BIT(0),
	MESH_PATH_RESOLVING =	BIT(1),
	MESH_PATH_DSN_VALID =	BIT(2),
	MESH_PATH_FIXED	=	BIT(3),
	MESH_PATH_RESOLVED =	BIT(4),
};

/**
 * struct mesh_path - mac80211 mesh path structure
 *
 * @dst: mesh path destination mac address
 * @sdata: mesh subif
 * @next_hop: mesh neighbor to which frames for this destination will be
 * 	forwarded
 * @timer: mesh path discovery timer
 * @frame_queue: pending queue for frames sent to this destination while the
 * 	path is unresolved
 * @dsn: destination sequence number of the destination
 * @metric: current metric to this destination
 * @hop_count: hops to destination
 * @exp_time: in jiffies, when the path will expire or when it expired
 * @discovery_timeout: timeout (lapse in jiffies) used for the last discovery
 * 	retry
 * @discovery_retries: number of discovery retries
 * @flags: mesh path flags, as specified on &enum mesh_path_flags
 * @state_lock: mesh pat state lock
 *
 *
 * The combination of dst and sdata is unique in the mesh path table. Since the
 * next_hop STA is only protected by RCU as well, deleting the STA must also
 * remove/substitute the mesh_path structure and wait until that is no longer
 * reachable before destroying the STA completely.
 */
struct mesh_path {
	u8 dst[ETH_ALEN];
	u8 mpp[ETH_ALEN];	/* used for MPP or MAP */
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *next_hop;
	struct timer_list timer;
	struct sk_buff_head frame_queue;
	struct rcu_head rcu;
	u32 dsn;
	u32 metric;
	u8 hop_count;
	unsigned long exp_time;
	u32 discovery_timeout;
	u8 discovery_retries;
	enum mesh_path_flags flags;
	spinlock_t state_lock;
};

/**
 * struct mesh_table
 *
 * @hash_buckets: array of hash buckets of the table
 * @hashwlock: array of locks to protect write operations, one per bucket
 * @hash_mask: 2^size_order - 1, used to compute hash idx
 * @hash_rnd: random value used for hash computations
 * @entries: number of entries in the table
 * @free_node: function to free nodes of the table
 * @copy_node: fuction to copy nodes of the table
 * @size_order: determines size of the table, there will be 2^size_order hash
 *	buckets
 * @mean_chain_len: maximum average length for the hash buckets' list, if it is
 *	reached, the table will grow
 */
struct mesh_table {
	/* Number of buckets will be 2^N */
	struct hlist_head *hash_buckets;
	spinlock_t *hashwlock;		/* One per bucket, for add/del */
	unsigned int hash_mask;		/* (2^size_order) - 1 */
	__u32 hash_rnd;			/* Used for hash generation */
	atomic_t entries;		/* Up to MAX_MESH_NEIGHBOURS */
	void (*free_node) (struct hlist_node *p, bool free_leafs);
	int (*copy_node) (struct hlist_node *p, struct mesh_table *newtbl);
	int size_order;
	int mean_chain_len;
};

/* Recent multicast cache */
/* RMC_BUCKETS must be a power of 2, maximum 256 */
#define RMC_BUCKETS		256
#define RMC_QUEUE_MAX_LEN	4
#define RMC_TIMEOUT		(3 * HZ)

/**
 * struct rmc_entry - entry in the Recent Multicast Cache
 *
 * @seqnum: mesh sequence number of the frame
 * @exp_time: expiration time of the entry, in jiffies
 * @sa: source address of the frame
 *
 * The Recent Multicast Cache keeps track of the latest multicast frames that
 * have been received by a mesh interface and discards received multicast frames
 * that are found in the cache.
 */
struct rmc_entry {
	struct list_head list;
	u32 seqnum;
	unsigned long exp_time;
	u8 sa[ETH_ALEN];
};

struct mesh_rmc {
	struct rmc_entry bucket[RMC_BUCKETS];
	u32 idx_mask;
};


/*
 * MESH_CFG_COMP_LEN Includes:
 * 	- Active path selection protocol ID.
 * 	- Active path selection metric ID.
 * 	- Congestion control mode identifier.
 * 	- Channel precedence.
 * Does not include mesh capabilities, which may vary across nodes in the same
 * mesh
 */
#define MESH_CFG_CMP_LEN 	(IEEE80211_MESH_CONFIG_LEN - 2)

/* Default values, timeouts in ms */
#define MESH_TTL 		5
#define MESH_MAX_RETR	 	3
#define MESH_RET_T 		100
#define MESH_CONF_T 		100
#define MESH_HOLD_T 		100

#define MESH_PATH_TIMEOUT	5000
/* Minimum interval between two consecutive PREQs originated by the same
 * interface
 */
#define MESH_PREQ_MIN_INT	10
#define MESH_DIAM_TRAVERSAL_TIME 50
/* Paths will be refreshed if they are closer than PATH_REFRESH_TIME to their
 * expiration
 */
#define MESH_PATH_REFRESH_TIME			1000
#define MESH_MIN_DISCOVERY_TIMEOUT (2 * MESH_DIAM_TRAVERSAL_TIME)

#define MESH_MAX_PREQ_RETRIES 4
#define MESH_PATH_EXPIRE (600 * HZ)

/* Default maximum number of established plinks per interface */
#define MESH_MAX_ESTAB_PLINKS	32

/* Default maximum number of plinks per interface */
#define MESH_MAX_PLINKS		256

/* Maximum number of paths per interface */
#define MESH_MAX_MPATHS		1024

/* Pending ANA approval */
#define PLINK_CATEGORY		30
#define MESH_PATH_SEL_CATEGORY	32

/* Public interfaces */
/* Various */
int ieee80211_new_mesh_header(struct ieee80211s_hdr *meshhdr,
		struct ieee80211_sub_if_data *sdata);
int mesh_rmc_check(u8 *addr, struct ieee80211s_hdr *mesh_hdr,
		struct ieee80211_sub_if_data *sdata);
bool mesh_matches_local(struct ieee802_11_elems *ie,
		struct ieee80211_sub_if_data *sdata);
void mesh_ids_set_default(struct ieee80211_if_mesh *mesh);
void mesh_mgmt_ies_add(struct sk_buff *skb,
		struct ieee80211_sub_if_data *sdata);
void mesh_rmc_free(struct ieee80211_sub_if_data *sdata);
int mesh_rmc_init(struct ieee80211_sub_if_data *sdata);
void ieee80211s_init(void);
void ieee80211s_stop(void);
void ieee80211_mesh_init_sdata(struct ieee80211_sub_if_data *sdata);
ieee80211_rx_result
ieee80211_mesh_rx_mgmt(struct ieee80211_sub_if_data *sdata, struct sk_buff *skb,
		       struct ieee80211_rx_status *rx_status);
void ieee80211_start_mesh(struct ieee80211_sub_if_data *sdata);
void ieee80211_stop_mesh(struct ieee80211_sub_if_data *sdata);

/* Mesh paths */
int mesh_nexthop_lookup(struct sk_buff *skb,
		struct ieee80211_sub_if_data *sdata);
void mesh_path_start_discovery(struct ieee80211_sub_if_data *sdata);
struct mesh_path *mesh_path_lookup(u8 *dst,
		struct ieee80211_sub_if_data *sdata);
struct mesh_path *mpp_path_lookup(u8 *dst,
				  struct ieee80211_sub_if_data *sdata);
int mpp_path_add(u8 *dst, u8 *mpp, struct ieee80211_sub_if_data *sdata);
struct mesh_path *mesh_path_lookup_by_idx(int idx,
		struct ieee80211_sub_if_data *sdata);
void mesh_path_fix_nexthop(struct mesh_path *mpath, struct sta_info *next_hop);
void mesh_path_expire(struct ieee80211_sub_if_data *sdata);
void mesh_path_flush(struct ieee80211_sub_if_data *sdata);
void mesh_rx_path_sel_frame(struct ieee80211_sub_if_data *sdata,
		struct ieee80211_mgmt *mgmt, size_t len);
int mesh_path_add(u8 *dst, struct ieee80211_sub_if_data *sdata);
/* Mesh plinks */
void mesh_neighbour_update(u8 *hw_addr, u32 rates,
		struct ieee80211_sub_if_data *sdata, bool add);
bool mesh_peer_accepts_plinks(struct ieee802_11_elems *ie);
void mesh_accept_plinks_update(struct ieee80211_sub_if_data *sdata);
void mesh_plink_broken(struct sta_info *sta);
void mesh_plink_deactivate(struct sta_info *sta);
int mesh_plink_open(struct sta_info *sta);
void mesh_plink_block(struct sta_info *sta);
void mesh_rx_plink_frame(struct ieee80211_sub_if_data *sdata,
			 struct ieee80211_mgmt *mgmt, size_t len,
			 struct ieee80211_rx_status *rx_status);

/* Private interfaces */
/* Mesh tables */
struct mesh_table *mesh_table_alloc(int size_order);
void mesh_table_free(struct mesh_table *tbl, bool free_leafs);
struct mesh_table *mesh_table_grow(struct mesh_table *tbl);
u32 mesh_table_hash(u8 *addr, struct ieee80211_sub_if_data *sdata,
		struct mesh_table *tbl);
/* Mesh paths */
int mesh_path_error_tx(u8 *dest, __le32 dest_dsn, u8 *ra,
		struct ieee80211_sub_if_data *sdata);
void mesh_path_assign_nexthop(struct mesh_path *mpath, struct sta_info *sta);
void mesh_path_flush_pending(struct mesh_path *mpath);
void mesh_path_tx_pending(struct mesh_path *mpath);
int mesh_pathtbl_init(void);
void mesh_pathtbl_unregister(void);
int mesh_path_del(u8 *addr, struct ieee80211_sub_if_data *sdata);
void mesh_path_timer(unsigned long data);
void mesh_path_flush_by_nexthop(struct sta_info *sta);
void mesh_path_discard_frame(struct sk_buff *skb,
		struct ieee80211_sub_if_data *sdata);
void mesh_path_quiesce(struct ieee80211_sub_if_data *sdata);
void mesh_path_restart(struct ieee80211_sub_if_data *sdata);

#ifdef CONFIG_MAC80211_MESH
extern int mesh_allocated;

static inline int mesh_plink_free_count(struct ieee80211_sub_if_data *sdata)
{
	return sdata->u.mesh.mshcfg.dot11MeshMaxPeerLinks -
	       atomic_read(&sdata->u.mesh.mshstats.estab_plinks);
}

static inline bool mesh_plink_availables(struct ieee80211_sub_if_data *sdata)
{
	return (min_t(long, mesh_plink_free_count(sdata),
		   MESH_MAX_PLINKS - sdata->local->num_sta)) > 0;
}

static inline void mesh_path_activate(struct mesh_path *mpath)
{
	mpath->flags |= MESH_PATH_ACTIVE | MESH_PATH_RESOLVED;
}

#define for_each_mesh_entry(x, p, node, i) \
	for (i = 0; i <= x->hash_mask; i++) \
		hlist_for_each_entry_rcu(node, p, &x->hash_buckets[i], list)

void ieee80211_mesh_notify_scan_completed(struct ieee80211_local *local);

void ieee80211_mesh_quiesce(struct ieee80211_sub_if_data *sdata);
void ieee80211_mesh_restart(struct ieee80211_sub_if_data *sdata);
void mesh_plink_quiesce(struct sta_info *sta);
void mesh_plink_restart(struct sta_info *sta);
#else
#define mesh_allocated	0
static inline void
ieee80211_mesh_notify_scan_completed(struct ieee80211_local *local) {}
static inline void ieee80211_mesh_quiesce(struct ieee80211_sub_if_data *sdata)
{}
static inline void ieee80211_mesh_restart(struct ieee80211_sub_if_data *sdata)
{}
static inline void mesh_plink_quiesce(struct sta_info *sta) {}
static inline void mesh_plink_restart(struct sta_info *sta) {}
#endif

#endif /* IEEE80211S_H */
