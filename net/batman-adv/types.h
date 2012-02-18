/*
 * Copyright (C) 2007-2012 B.A.T.M.A.N. contributors:
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
 *
 */



#ifndef _NET_BATMAN_ADV_TYPES_H_
#define _NET_BATMAN_ADV_TYPES_H_

#include "packet.h"
#include "bitarray.h"

#define BAT_HEADER_LEN (ETH_HLEN + \
	((sizeof(struct unicast_packet) > sizeof(struct bcast_packet) ? \
	 sizeof(struct unicast_packet) : \
	 sizeof(struct bcast_packet))))


struct hard_iface {
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
 *	orig_node - structure for orig_list maintaining nodes of mesh
 *	@primary_addr: hosts primary interface address
 *	@last_valid: when last packet from this node was received
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
struct orig_node {
	uint8_t orig[ETH_ALEN];
	uint8_t primary_addr[ETH_ALEN];
	struct neigh_node __rcu *router; /* rcu protected pointer */
	unsigned long *bcast_own;
	uint8_t *bcast_own_sum;
	unsigned long last_valid;
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
	 * soon as I receive a new TTVN from this orig_node */
	bool tt_poss_change;
	uint32_t last_real_seqno;
	uint8_t last_ttl;
	DECLARE_BITMAP(bcast_bits, TQ_LOCAL_WINDOW_SIZE);
	uint32_t last_bcast_seqno;
	struct hlist_head neigh_list;
	struct list_head frag_list;
	spinlock_t neigh_list_lock; /* protects neigh_list and router */
	atomic_t refcount;
	struct rcu_head rcu;
	struct hlist_node hash_entry;
	struct bat_priv *bat_priv;
	unsigned long last_frag_packet;
	/* ogm_cnt_lock protects: bcast_own, bcast_own_sum,
	 * neigh_node->real_bits, neigh_node->real_packet_count */
	spinlock_t ogm_cnt_lock;
	/* bcast_seqno_lock protects bcast_bits, last_bcast_seqno */
	spinlock_t bcast_seqno_lock;
	spinlock_t tt_list_lock; /* protects tt_list */
	atomic_t bond_candidates;
	struct list_head bond_list;
};

struct gw_node {
	struct hlist_node list;
	struct orig_node *orig_node;
	unsigned long deleted;
	atomic_t refcount;
	struct rcu_head rcu;
};

/**
 *	neigh_node
 *	@last_valid: when last packet via this neighbor was received
 */
struct neigh_node {
	struct hlist_node list;
	uint8_t addr[ETH_ALEN];
	uint8_t real_packet_count;
	uint8_t tq_recv[TQ_GLOBAL_WINDOW_SIZE];
	uint8_t tq_index;
	uint8_t tq_avg;
	uint8_t last_ttl;
	struct list_head bonding_list;
	unsigned long last_valid;
	DECLARE_BITMAP(real_bits, TQ_LOCAL_WINDOW_SIZE);
	atomic_t refcount;
	struct rcu_head rcu;
	struct orig_node *orig_node;
	struct hard_iface *if_incoming;
	spinlock_t tq_lock;	/* protects: tq_recv, tq_index */
};

#ifdef CONFIG_BATMAN_ADV_BLA
struct bcast_duplist_entry {
	uint8_t orig[ETH_ALEN];
	uint16_t crc;
	unsigned long entrytime;
};
#endif

struct bat_priv {
	atomic_t mesh_state;
	struct net_device_stats stats;
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
	atomic_t ttvn; /* translation table version number */
	atomic_t tt_ogm_append_cnt;
	atomic_t tt_local_changes; /* changes registered in a OGM interval */
	atomic_t bla_num_requests; /* number of bla requests in flight */
	/* The tt_poss_change flag is used to detect an ongoing roaming phase.
	 * If true, then I received a Roaming_adv and I have to inspect every
	 * packet directed to me to check whether I am still the true
	 * destination or not. This flag will be reset to false as soon as I
	 * increase my TTVN */
	bool tt_poss_change;
	char num_ifaces;
	struct debug_log *debug_log;
	struct kobject *mesh_obj;
	struct dentry *debug_dir;
	struct hlist_head forw_bat_list;
	struct hlist_head forw_bcast_list;
	struct hlist_head gw_list;
	struct list_head tt_changes_list; /* tracks changes in a OGM int */
	struct list_head vis_send_list;
	struct hashtable_t *orig_hash;
	struct hashtable_t *tt_local_hash;
	struct hashtable_t *tt_global_hash;
#ifdef CONFIG_BATMAN_ADV_BLA
	struct hashtable_t *claim_hash;
	struct hashtable_t *backbone_hash;
#endif
	struct list_head tt_req_list; /* list of pending tt_requests */
	struct list_head tt_roam_list;
	struct hashtable_t *vis_hash;
#ifdef CONFIG_BATMAN_ADV_BLA
	struct bcast_duplist_entry bcast_duplist[DUPLIST_SIZE];
	int bcast_duplist_curr;
	struct bla_claim_dst claim_dest;
#endif
	spinlock_t forw_bat_list_lock; /* protects forw_bat_list */
	spinlock_t forw_bcast_list_lock; /* protects  */
	spinlock_t tt_changes_list_lock; /* protects tt_changes */
	spinlock_t tt_req_list_lock; /* protects tt_req_list */
	spinlock_t tt_roam_list_lock; /* protects tt_roam_list */
	spinlock_t gw_list_lock; /* protects gw_list and curr_gw */
	spinlock_t vis_hash_lock; /* protects vis_hash */
	spinlock_t vis_list_lock; /* protects vis_info::recv_list */
	atomic_t num_local_tt;
	/* Checksum of the local table, recomputed before sending a new OGM */
	atomic_t tt_crc;
	unsigned char *tt_buff;
	int16_t tt_buff_len;
	spinlock_t tt_buff_lock; /* protects tt_buff */
	struct delayed_work tt_work;
	struct delayed_work orig_work;
	struct delayed_work vis_work;
	struct delayed_work bla_work;
	struct gw_node __rcu *curr_gw;  /* rcu protected pointer */
	atomic_t gw_reselect;
	struct hard_iface __rcu *primary_if;  /* rcu protected pointer */
	struct vis_info *my_vis_info;
	struct bat_algo_ops *bat_algo_ops;
};

struct socket_client {
	struct list_head queue_list;
	unsigned int queue_len;
	unsigned char index;
	spinlock_t lock; /* protects queue_list, queue_len, index */
	wait_queue_head_t queue_wait;
	struct bat_priv *bat_priv;
};

struct socket_packet {
	struct list_head list;
	size_t icmp_len;
	struct icmp_packet_rr icmp_packet;
};

struct tt_common_entry {
	uint8_t addr[ETH_ALEN];
	struct hlist_node hash_entry;
	uint16_t flags;
	atomic_t refcount;
	struct rcu_head rcu;
};

struct tt_local_entry {
	struct tt_common_entry common;
	unsigned long last_seen;
};

struct tt_global_entry {
	struct tt_common_entry common;
	struct hlist_head orig_list;
	spinlock_t list_lock;	/* protects the list */
	unsigned long roam_at; /* time at which TT_GLOBAL_ROAM was set */
};

struct tt_orig_list_entry {
	struct orig_node *orig_node;
	uint8_t ttvn;
	struct rcu_head rcu;
	struct hlist_node list;
};

#ifdef CONFIG_BATMAN_ADV_BLA
struct backbone_gw {
	uint8_t orig[ETH_ALEN];
	short vid;		/* used VLAN ID */
	struct hlist_node hash_entry;
	struct bat_priv *bat_priv;
	unsigned long lasttime;	/* last time we heard of this backbone gw */
	atomic_t request_sent;
	atomic_t refcount;
	struct rcu_head rcu;
	uint16_t crc;		/* crc checksum over all claims */
};

struct claim {
	uint8_t addr[ETH_ALEN];
	short vid;
	struct backbone_gw *backbone_gw;
	unsigned long lasttime;	/* last time we heard of claim (locals only) */
	struct rcu_head rcu;
	atomic_t refcount;
	struct hlist_node hash_entry;
};
#endif

struct tt_change_node {
	struct list_head list;
	struct tt_change change;
};

struct tt_req_node {
	uint8_t addr[ETH_ALEN];
	unsigned long issued_at;
	struct list_head list;
};

struct tt_roam_node {
	uint8_t addr[ETH_ALEN];
	atomic_t counter;
	unsigned long first_time;
	struct list_head list;
};

/**
 *	forw_packet - structure for forw_list maintaining packets to be
 *	              send/forwarded
 */
struct forw_packet {
	struct hlist_node list;
	unsigned long send_time;
	uint8_t own;
	struct sk_buff *skb;
	uint16_t packet_len;
	uint32_t direct_link_flags;
	uint8_t num_packets;
	struct delayed_work delayed_work;
	struct hard_iface *if_incoming;
};

/* While scanning for vis-entries of a particular vis-originator
 * this list collects its interfaces to create a subgraph/cluster
 * out of them later
 */
struct if_list_entry {
	uint8_t addr[ETH_ALEN];
	bool primary;
	struct hlist_node list;
};

struct debug_log {
	char log_buff[LOG_BUF_LEN];
	unsigned long log_start;
	unsigned long log_end;
	spinlock_t lock; /* protects log_buff, log_start and log_end */
	wait_queue_head_t queue_wait;
};

struct frag_packet_list_entry {
	struct list_head list;
	uint16_t seqno;
	struct sk_buff *skb;
};

struct vis_info {
	unsigned long first_seen;
	/* list of server-neighbors we received a vis-packet
	 * from.  we should not reply to them. */
	struct list_head recv_list;
	struct list_head send_list;
	struct kref refcount;
	struct hlist_node hash_entry;
	struct bat_priv *bat_priv;
	/* this packet might be part of the vis send queue. */
	struct sk_buff *skb_packet;
	/* vis_info may follow here*/
} __packed;

struct vis_info_entry {
	uint8_t  src[ETH_ALEN];
	uint8_t  dest[ETH_ALEN];
	uint8_t  quality;	/* quality = 0 client */
} __packed;

struct recvlist_node {
	struct list_head list;
	uint8_t mac[ETH_ALEN];
};

struct bat_algo_ops {
	struct hlist_node list;
	char *name;
	/* init routing info when hard-interface is enabled */
	int (*bat_iface_enable)(struct hard_iface *hard_iface);
	/* de-init routing info when hard-interface is disabled */
	void (*bat_iface_disable)(struct hard_iface *hard_iface);
	/* called when primary interface is selected / changed */
	void (*bat_primary_iface_set)(struct hard_iface *hard_iface);
	/* init mac addresses of the OGM belonging to this hard-interface */
	void (*bat_ogm_update_mac)(struct hard_iface *hard_iface);
	/* prepare a new outgoing OGM for the send queue */
	void (*bat_ogm_schedule)(struct hard_iface *hard_iface,
				 int tt_num_changes);
	/* send scheduled OGM */
	void (*bat_ogm_emit)(struct forw_packet *forw_packet);
	/* receive incoming OGM */
	void (*bat_ogm_receive)(struct hard_iface *if_incoming,
				struct sk_buff *skb);
};

#endif /* _NET_BATMAN_ADV_TYPES_H_ */
