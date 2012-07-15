/* Copyright (C) 2007-2012 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#ifndef _NET_BATMAN_ADV_TYPES_H_
#define _NET_BATMAN_ADV_TYPES_H_

#include "packet.h"
#include "bitarray.h"
#include <linux/kernel.h>

#define BATADV_HEADER_LEN \
	(ETH_HLEN + max(sizeof(struct batadv_unicast_packet), \
			sizeof(struct batadv_bcast_packet)))

struct batadv_hard_iface {
	struct list_head list;
	int16_t if_num;
	char if_status;
	struct net_device *net_dev;
	atomic_t seqno;
	atomic_t frag_seqno;
	unsigned char *packet_buff;
	int packet_len;
	struct kobject *hardif_obj;
	atomic_t refcount;
	struct packet_type batman_adv_ptype;
	struct net_device *soft_iface;
	struct rcu_head rcu;
};

/**
 *	struct batadv_orig_node - structure for orig_list maintaining nodes of mesh
 *	@primary_addr: hosts primary interface address
 *	@last_seen: when last packet from this node was received
 *	@bcast_seqno_reset: time when the broadcast seqno window was reset
 *	@batman_seqno_reset: time when the batman seqno window was reset
 *	@gw_flags: flags related to gateway class
 *	@flags: for now only VIS_SERVER flag
 *	@last_real_seqno: last and best known sequence number
 *	@last_ttl: ttl of last received packet
 *	@last_bcast_seqno: last broadcast sequence number received by this host
 *
 *	@candidates: how many candidates are available
 *	@selected: next bonding candidate
 */
struct batadv_orig_node {
	uint8_t orig[ETH_ALEN];
	uint8_t primary_addr[ETH_ALEN];
	struct batadv_neigh_node __rcu *router; /* rcu protected pointer */
	unsigned long *bcast_own;
	uint8_t *bcast_own_sum;
	unsigned long last_seen;
	unsigned long bcast_seqno_reset;
	unsigned long batman_seqno_reset;
	uint8_t gw_flags;
	uint8_t flags;
	atomic_t last_ttvn; /* last seen translation table version number */
	uint16_t tt_crc;
	unsigned char *tt_buff;
	int16_t tt_buff_len;
	spinlock_t tt_buff_lock; /* protects tt_buff */
	atomic_t tt_size;
	bool tt_initialised;
	/* The tt_poss_change flag is used to detect an ongoing roaming phase.
	 * If true, then I sent a Roaming_adv to this orig_node and I have to
	 * inspect every packet directed to it to check whether it is still
	 * the true destination or not. This flag will be reset to false as
	 * soon as I receive a new TTVN from this orig_node
	 */
	bool tt_poss_change;
	uint32_t last_real_seqno;
	uint8_t last_ttl;
	DECLARE_BITMAP(bcast_bits, BATADV_TQ_LOCAL_WINDOW_SIZE);
	uint32_t last_bcast_seqno;
	struct hlist_head neigh_list;
	struct list_head frag_list;
	spinlock_t neigh_list_lock; /* protects neigh_list and router */
	atomic_t refcount;
	struct rcu_head rcu;
	struct hlist_node hash_entry;
	struct batadv_priv *bat_priv;
	unsigned long last_frag_packet;
	/* ogm_cnt_lock protects: bcast_own, bcast_own_sum,
	 * neigh_node->real_bits, neigh_node->real_packet_count
	 */
	spinlock_t ogm_cnt_lock;
	/* bcast_seqno_lock protects bcast_bits, last_bcast_seqno */
	spinlock_t bcast_seqno_lock;
	spinlock_t tt_list_lock; /* protects tt_list */
	atomic_t bond_candidates;
	struct list_head bond_list;
};

struct batadv_gw_node {
	struct hlist_node list;
	struct batadv_orig_node *orig_node;
	unsigned long deleted;
	atomic_t refcount;
	struct rcu_head rcu;
};

/*	batadv_neigh_node
 *	@last_seen: when last packet via this neighbor was received
 */
struct batadv_neigh_node {
	struct hlist_node list;
	uint8_t addr[ETH_ALEN];
	uint8_t real_packet_count;
	uint8_t tq_recv[BATADV_TQ_GLOBAL_WINDOW_SIZE];
	uint8_t tq_index;
	uint8_t tq_avg;
	uint8_t last_ttl;
	struct list_head bonding_list;
	unsigned long last_seen;
	DECLARE_BITMAP(real_bits, BATADV_TQ_LOCAL_WINDOW_SIZE);
	atomic_t refcount;
	struct rcu_head rcu;
	struct batadv_orig_node *orig_node;
	struct batadv_hard_iface *if_incoming;
	spinlock_t lq_update_lock;	/* protects: tq_recv, tq_index */
};

#ifdef CONFIG_BATMAN_ADV_BLA
struct batadv_bcast_duplist_entry {
	uint8_t orig[ETH_ALEN];
	uint16_t crc;
	unsigned long entrytime;
};
#endif

enum batadv_counters {
	BATADV_CNT_TX,
	BATADV_CNT_TX_BYTES,
	BATADV_CNT_TX_DROPPED,
	BATADV_CNT_RX,
	BATADV_CNT_RX_BYTES,
	BATADV_CNT_FORWARD,
	BATADV_CNT_FORWARD_BYTES,
	BATADV_CNT_MGMT_TX,
	BATADV_CNT_MGMT_TX_BYTES,
	BATADV_CNT_MGMT_RX,
	BATADV_CNT_MGMT_RX_BYTES,
	BATADV_CNT_TT_REQUEST_TX,
	BATADV_CNT_TT_REQUEST_RX,
	BATADV_CNT_TT_RESPONSE_TX,
	BATADV_CNT_TT_RESPONSE_RX,
	BATADV_CNT_TT_ROAM_ADV_TX,
	BATADV_CNT_TT_ROAM_ADV_RX,
	BATADV_CNT_NUM,
};

/**
 * struct batadv_priv_tt - per mesh interface translation table data
 * @vn: translation table version number
 * @local_changes: changes registered in an originator interval
 * @poss_change: Detect an ongoing roaming phase. If true, then this node
 *  received a roaming_adv and has to inspect every packet directed to it to
 *  check whether it still is the true destination or not. This flag will be
 *  reset to false as soon as the this node's ttvn is increased
 * @changes_list: tracks tt local changes within an originator interval
 * @req_list: list of pending tt_requests
 * @local_crc: Checksum of the local table, recomputed before sending a new OGM
 */
struct batadv_priv_tt {
	atomic_t vn;
	atomic_t ogm_append_cnt;
	atomic_t local_changes;
	bool poss_change;
	struct list_head changes_list;
	struct batadv_hashtable *local_hash;
	struct batadv_hashtable *global_hash;
	struct list_head req_list;
	struct list_head roam_list;
	spinlock_t changes_list_lock; /* protects changes */
	spinlock_t req_list_lock; /* protects req_list */
	spinlock_t roam_list_lock; /* protects roam_list */
	atomic_t local_entry_num;
	uint16_t local_crc;
	unsigned char *last_changeset;
	int16_t last_changeset_len;
	spinlock_t last_changeset_lock; /* protects last_changeset */
	struct delayed_work work;
};

#ifdef CONFIG_BATMAN_ADV_BLA
struct batadv_priv_bla {
	atomic_t num_requests; /* number of bla requests in flight */
	struct batadv_hashtable *claim_hash;
	struct batadv_hashtable *backbone_hash;
	struct batadv_bcast_duplist_entry bcast_duplist[BATADV_DUPLIST_SIZE];
	int bcast_duplist_curr;
	struct batadv_bla_claim_dst claim_dest;
	struct delayed_work work;
};
#endif

struct batadv_priv_gw {
	struct hlist_head list;
	spinlock_t list_lock; /* protects gw_list and curr_gw */
	struct batadv_gw_node __rcu *curr_gw;  /* rcu protected pointer */
	atomic_t reselect;
};

struct batadv_priv_vis {
	struct list_head send_list;
	struct batadv_hashtable *hash;
	spinlock_t hash_lock; /* protects hash */
	spinlock_t list_lock; /* protects info::recv_list */
	struct delayed_work work;
	struct batadv_vis_info *my_info;
};

struct batadv_priv {
	atomic_t mesh_state;
	struct net_device_stats stats;
	uint64_t __percpu *bat_counters; /* Per cpu counters */
	atomic_t aggregated_ogms;	/* boolean */
	atomic_t bonding;		/* boolean */
	atomic_t fragmentation;		/* boolean */
	atomic_t ap_isolation;		/* boolean */
	atomic_t bridge_loop_avoidance;	/* boolean */
	atomic_t vis_mode;		/* VIS_TYPE_* */
	atomic_t gw_mode;		/* GW_MODE_* */
	atomic_t gw_sel_class;		/* uint */
	atomic_t gw_bandwidth;		/* gw bandwidth */
	atomic_t orig_interval;		/* uint */
	atomic_t hop_penalty;		/* uint */
	atomic_t log_level;		/* uint */
	atomic_t bcast_seqno;
	atomic_t bcast_queue_left;
	atomic_t batman_queue_left;
	char num_ifaces;
	struct batadv_debug_log *debug_log;
	struct kobject *mesh_obj;
	struct dentry *debug_dir;
	struct hlist_head forw_bat_list;
	struct hlist_head forw_bcast_list;
	struct batadv_hashtable *orig_hash;
	spinlock_t forw_bat_list_lock; /* protects forw_bat_list */
	spinlock_t forw_bcast_list_lock; /* protects  */
	struct delayed_work orig_work;
	struct batadv_hard_iface __rcu *primary_if;  /* rcu protected pointer */
	struct batadv_algo_ops *bat_algo_ops;
#ifdef CONFIG_BATMAN_ADV_BLA
	struct batadv_priv_bla bla;
#endif
	struct batadv_priv_gw gw;
	struct batadv_priv_tt tt;
	struct batadv_priv_vis vis;
};

struct batadv_socket_client {
	struct list_head queue_list;
	unsigned int queue_len;
	unsigned char index;
	spinlock_t lock; /* protects queue_list, queue_len, index */
	wait_queue_head_t queue_wait;
	struct batadv_priv *bat_priv;
};

struct batadv_socket_packet {
	struct list_head list;
	size_t icmp_len;
	struct batadv_icmp_packet_rr icmp_packet;
};

struct batadv_tt_common_entry {
	uint8_t addr[ETH_ALEN];
	struct hlist_node hash_entry;
	uint16_t flags;
	atomic_t refcount;
	struct rcu_head rcu;
};

struct batadv_tt_local_entry {
	struct batadv_tt_common_entry common;
	unsigned long last_seen;
};

struct batadv_tt_global_entry {
	struct batadv_tt_common_entry common;
	struct hlist_head orig_list;
	spinlock_t list_lock;	/* protects the list */
	unsigned long roam_at; /* time at which TT_GLOBAL_ROAM was set */
};

struct batadv_tt_orig_list_entry {
	struct batadv_orig_node *orig_node;
	uint8_t ttvn;
	atomic_t refcount;
	struct rcu_head rcu;
	struct hlist_node list;
};

#ifdef CONFIG_BATMAN_ADV_BLA
struct batadv_backbone_gw {
	uint8_t orig[ETH_ALEN];
	short vid;		/* used VLAN ID */
	struct hlist_node hash_entry;
	struct batadv_priv *bat_priv;
	unsigned long lasttime;	/* last time we heard of this backbone gw */
	atomic_t request_sent;
	atomic_t refcount;
	struct rcu_head rcu;
	uint16_t crc;		/* crc checksum over all claims */
};

struct batadv_claim {
	uint8_t addr[ETH_ALEN];
	short vid;
	struct batadv_backbone_gw *backbone_gw;
	unsigned long lasttime;	/* last time we heard of claim (locals only) */
	struct rcu_head rcu;
	atomic_t refcount;
	struct hlist_node hash_entry;
};
#endif

struct batadv_tt_change_node {
	struct list_head list;
	struct batadv_tt_change change;
};

struct batadv_tt_req_node {
	uint8_t addr[ETH_ALEN];
	unsigned long issued_at;
	struct list_head list;
};

struct batadv_tt_roam_node {
	uint8_t addr[ETH_ALEN];
	atomic_t counter;
	unsigned long first_time;
	struct list_head list;
};

/*	forw_packet - structure for forw_list maintaining packets to be
 *	              send/forwarded
 */
struct batadv_forw_packet {
	struct hlist_node list;
	unsigned long send_time;
	uint8_t own;
	struct sk_buff *skb;
	uint16_t packet_len;
	uint32_t direct_link_flags;
	uint8_t num_packets;
	struct delayed_work delayed_work;
	struct batadv_hard_iface *if_incoming;
};

/* While scanning for vis-entries of a particular vis-originator
 * this list collects its interfaces to create a subgraph/cluster
 * out of them later
 */
struct batadv_if_list_entry {
	uint8_t addr[ETH_ALEN];
	bool primary;
	struct hlist_node list;
};

struct batadv_debug_log {
	char log_buff[BATADV_LOG_BUF_LEN];
	unsigned long log_start;
	unsigned long log_end;
	spinlock_t lock; /* protects log_buff, log_start and log_end */
	wait_queue_head_t queue_wait;
};

struct batadv_frag_packet_list_entry {
	struct list_head list;
	uint16_t seqno;
	struct sk_buff *skb;
};

struct batadv_vis_info {
	unsigned long first_seen;
	/* list of server-neighbors we received a vis-packet
	 * from.  we should not reply to them.
	 */
	struct list_head recv_list;
	struct list_head send_list;
	struct kref refcount;
	struct hlist_node hash_entry;
	struct batadv_priv *bat_priv;
	/* this packet might be part of the vis send queue. */
	struct sk_buff *skb_packet;
	/* vis_info may follow here */
} __packed;

struct batadv_vis_info_entry {
	uint8_t  src[ETH_ALEN];
	uint8_t  dest[ETH_ALEN];
	uint8_t  quality;	/* quality = 0 client */
} __packed;

struct batadv_recvlist_node {
	struct list_head list;
	uint8_t mac[ETH_ALEN];
};

struct batadv_algo_ops {
	struct hlist_node list;
	char *name;
	/* init routing info when hard-interface is enabled */
	int (*bat_iface_enable)(struct batadv_hard_iface *hard_iface);
	/* de-init routing info when hard-interface is disabled */
	void (*bat_iface_disable)(struct batadv_hard_iface *hard_iface);
	/* (re-)init mac addresses of the protocol information
	 * belonging to this hard-interface
	 */
	void (*bat_iface_update_mac)(struct batadv_hard_iface *hard_iface);
	/* called when primary interface is selected / changed */
	void (*bat_primary_iface_set)(struct batadv_hard_iface *hard_iface);
	/* prepare a new outgoing OGM for the send queue */
	void (*bat_ogm_schedule)(struct batadv_hard_iface *hard_iface);
	/* send scheduled OGM */
	void (*bat_ogm_emit)(struct batadv_forw_packet *forw_packet);
};

#endif /* _NET_BATMAN_ADV_TYPES_H_ */
