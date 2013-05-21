/* Copyright (C) 2007-2013 B.A.T.M.A.N. contributors:
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

/**
 * Maximum overhead for the encapsulation for a payload packet
 */
#define BATADV_HEADER_LEN \
	(ETH_HLEN + max(sizeof(struct batadv_unicast_packet), \
			sizeof(struct batadv_bcast_packet)))

#ifdef CONFIG_BATMAN_ADV_DAT

/* batadv_dat_addr_t is the type used for all DHT addresses. If it is changed,
 * BATADV_DAT_ADDR_MAX is changed as well.
 *
 * *Please be careful: batadv_dat_addr_t must be UNSIGNED*
 */
#define batadv_dat_addr_t uint16_t

#endif /* CONFIG_BATMAN_ADV_DAT */

/**
 * struct batadv_hard_iface_bat_iv - per hard interface B.A.T.M.A.N. IV data
 * @ogm_buff: buffer holding the OGM packet
 * @ogm_buff_len: length of the OGM packet buffer
 * @ogm_seqno: OGM sequence number - used to identify each OGM
 */
struct batadv_hard_iface_bat_iv {
	unsigned char *ogm_buff;
	int ogm_buff_len;
	atomic_t ogm_seqno;
};

/**
 * struct batadv_hard_iface - network device known to batman-adv
 * @list: list node for batadv_hardif_list
 * @if_num: identificator of the interface
 * @if_status: status of the interface for batman-adv
 * @net_dev: pointer to the net_device
 * @frag_seqno: last fragment sequence number sent by this interface
 * @hardif_obj: kobject of the per interface sysfs "mesh" directory
 * @refcount: number of contexts the object is used
 * @batman_adv_ptype: packet type describing packets that should be processed by
 *  batman-adv for this interface
 * @soft_iface: the batman-adv interface which uses this network interface
 * @rcu: struct used for freeing in an RCU-safe manner
 * @bat_iv: BATMAN IV specific per hard interface data
 * @cleanup_work: work queue callback item for hard interface deinit
 */
struct batadv_hard_iface {
	struct list_head list;
	int16_t if_num;
	char if_status;
	struct net_device *net_dev;
	atomic_t frag_seqno;
	struct kobject *hardif_obj;
	atomic_t refcount;
	struct packet_type batman_adv_ptype;
	struct net_device *soft_iface;
	struct rcu_head rcu;
	struct batadv_hard_iface_bat_iv bat_iv;
	struct work_struct cleanup_work;
};

/**
 * struct batadv_orig_node - structure for orig_list maintaining nodes of mesh
 * @orig: originator ethernet address
 * @primary_addr: hosts primary interface address
 * @router: router that should be used to reach this originator
 * @batadv_dat_addr_t:  address of the orig node in the distributed hash
 * @bcast_own: bitfield containing the number of our OGMs this orig_node
 *  rebroadcasted "back" to us (relative to last_real_seqno)
 * @bcast_own_sum: counted result of bcast_own
 * @last_seen: time when last packet from this node was received
 * @bcast_seqno_reset: time when the broadcast seqno window was reset
 * @batman_seqno_reset: time when the batman seqno window was reset
 * @gw_flags: flags related to gateway class
 * @flags: for now only VIS_SERVER flag
 * @last_ttvn: last seen translation table version number
 * @tt_crc: CRC of the translation table
 * @tt_buff: last tt changeset this node received from the orig node
 * @tt_buff_len: length of the last tt changeset this node received from the
 *  orig node
 * @tt_buff_lock: lock that protects tt_buff and tt_buff_len
 * @tt_size: number of global TT entries announced by the orig node
 * @tt_initialised: bool keeping track of whether or not this node have received
 *  any translation table information from the orig node yet
 * @last_real_seqno: last and best known sequence number
 * @last_ttl: ttl of last received packet
 * @bcast_bits: bitfield containing the info which payload broadcast originated
 *  from this orig node this host already has seen (relative to
 *  last_bcast_seqno)
 * @last_bcast_seqno: last broadcast sequence number received by this host
 * @neigh_list: list of potential next hop neighbor towards this orig node
 * @frag_list: fragmentation buffer list for fragment re-assembly
 * @last_frag_packet: time when last fragmented packet from this node was
 *  received
 * @neigh_list_lock: lock protecting neigh_list, router and bonding_list
 * @hash_entry: hlist node for batadv_priv::orig_hash
 * @bat_priv: pointer to soft_iface this orig node belongs to
 * @ogm_cnt_lock: lock protecting bcast_own, bcast_own_sum,
 *  neigh_node->real_bits & neigh_node->real_packet_count
 * @bcast_seqno_lock: lock protecting bcast_bits & last_bcast_seqno
 * @bond_candidates: how many candidates are available
 * @bond_list: list of bonding candidates
 * @refcount: number of contexts the object is used
 * @rcu: struct used for freeing in an RCU-safe manner
 * @in_coding_list: list of nodes this orig can hear
 * @out_coding_list: list of nodes that can hear this orig
 * @in_coding_list_lock: protects in_coding_list
 * @out_coding_list_lock: protects out_coding_list
 */
struct batadv_orig_node {
	uint8_t orig[ETH_ALEN];
	uint8_t primary_addr[ETH_ALEN];
	struct batadv_neigh_node __rcu *router; /* rcu protected pointer */
#ifdef CONFIG_BATMAN_ADV_DAT
	batadv_dat_addr_t dat_addr;
#endif
	unsigned long *bcast_own;
	uint8_t *bcast_own_sum;
	unsigned long last_seen;
	unsigned long bcast_seqno_reset;
	unsigned long batman_seqno_reset;
	uint8_t gw_flags;
	uint8_t flags;
	atomic_t last_ttvn;
	uint16_t tt_crc;
	unsigned char *tt_buff;
	int16_t tt_buff_len;
	spinlock_t tt_buff_lock; /* protects tt_buff & tt_buff_len */
	atomic_t tt_size;
	bool tt_initialised;
	uint32_t last_real_seqno;
	uint8_t last_ttl;
	DECLARE_BITMAP(bcast_bits, BATADV_TQ_LOCAL_WINDOW_SIZE);
	uint32_t last_bcast_seqno;
	struct hlist_head neigh_list;
	struct list_head frag_list;
	unsigned long last_frag_packet;
	/* neigh_list_lock protects: neigh_list, router & bonding_list */
	spinlock_t neigh_list_lock;
	struct hlist_node hash_entry;
	struct batadv_priv *bat_priv;
	/* ogm_cnt_lock protects: bcast_own, bcast_own_sum,
	 * neigh_node->real_bits & neigh_node->real_packet_count
	 */
	spinlock_t ogm_cnt_lock;
	/* bcast_seqno_lock protects: bcast_bits & last_bcast_seqno */
	spinlock_t bcast_seqno_lock;
	atomic_t bond_candidates;
	struct list_head bond_list;
	atomic_t refcount;
	struct rcu_head rcu;
#ifdef CONFIG_BATMAN_ADV_NC
	struct list_head in_coding_list;
	struct list_head out_coding_list;
	spinlock_t in_coding_list_lock; /* Protects in_coding_list */
	spinlock_t out_coding_list_lock; /* Protects out_coding_list */
#endif
};

/**
 * struct batadv_gw_node - structure for orig nodes announcing gw capabilities
 * @list: list node for batadv_priv_gw::list
 * @orig_node: pointer to corresponding orig node
 * @deleted: this struct is scheduled for deletion
 * @refcount: number of contexts the object is used
 * @rcu: struct used for freeing in an RCU-safe manner
 */
struct batadv_gw_node {
	struct hlist_node list;
	struct batadv_orig_node *orig_node;
	unsigned long deleted;
	atomic_t refcount;
	struct rcu_head rcu;
};

/**
 * struct batadv_neigh_node - structure for single hop neighbors
 * @list: list node for batadv_orig_node::neigh_list
 * @addr: mac address of neigh node
 * @tq_recv: ring buffer of received TQ values from this neigh node
 * @tq_index: ring buffer index
 * @tq_avg: averaged tq of all tq values in the ring buffer (tq_recv)
 * @last_ttl: last received ttl from this neigh node
 * @bonding_list: list node for batadv_orig_node::bond_list
 * @last_seen: when last packet via this neighbor was received
 * @real_bits: bitfield containing the number of OGMs received from this neigh
 *  node (relative to orig_node->last_real_seqno)
 * @real_packet_count: counted result of real_bits
 * @orig_node: pointer to corresponding orig_node
 * @if_incoming: pointer to incoming hard interface
 * @lq_update_lock: lock protecting tq_recv & tq_index
 * @refcount: number of contexts the object is used
 * @rcu: struct used for freeing in an RCU-safe manner
 */
struct batadv_neigh_node {
	struct hlist_node list;
	uint8_t addr[ETH_ALEN];
	uint8_t tq_recv[BATADV_TQ_GLOBAL_WINDOW_SIZE];
	uint8_t tq_index;
	uint8_t tq_avg;
	uint8_t last_ttl;
	struct list_head bonding_list;
	unsigned long last_seen;
	DECLARE_BITMAP(real_bits, BATADV_TQ_LOCAL_WINDOW_SIZE);
	uint8_t real_packet_count;
	struct batadv_orig_node *orig_node;
	struct batadv_hard_iface *if_incoming;
	spinlock_t lq_update_lock; /* protects tq_recv & tq_index */
	atomic_t refcount;
	struct rcu_head rcu;
};

/**
 * struct batadv_bcast_duplist_entry - structure for LAN broadcast suppression
 * @orig[ETH_ALEN]: mac address of orig node orginating the broadcast
 * @crc: crc32 checksum of broadcast payload
 * @entrytime: time when the broadcast packet was received
 */
#ifdef CONFIG_BATMAN_ADV_BLA
struct batadv_bcast_duplist_entry {
	uint8_t orig[ETH_ALEN];
	__be32 crc;
	unsigned long entrytime;
};
#endif

/**
 * enum batadv_counters - indices for traffic counters
 * @BATADV_CNT_TX: transmitted payload traffic packet counter
 * @BATADV_CNT_TX_BYTES: transmitted payload traffic bytes counter
 * @BATADV_CNT_TX_DROPPED: dropped transmission payload traffic packet counter
 * @BATADV_CNT_RX: received payload traffic packet counter
 * @BATADV_CNT_RX_BYTES: received payload traffic bytes counter
 * @BATADV_CNT_FORWARD: forwarded payload traffic packet counter
 * @BATADV_CNT_FORWARD_BYTES: forwarded payload traffic bytes counter
 * @BATADV_CNT_MGMT_TX: transmitted routing protocol traffic packet counter
 * @BATADV_CNT_MGMT_TX_BYTES: transmitted routing protocol traffic bytes counter
 * @BATADV_CNT_MGMT_RX: received routing protocol traffic packet counter
 * @BATADV_CNT_MGMT_RX_BYTES: received routing protocol traffic bytes counter
 * @BATADV_CNT_TT_REQUEST_TX: transmitted tt req traffic packet counter
 * @BATADV_CNT_TT_REQUEST_RX: received tt req traffic packet counter
 * @BATADV_CNT_TT_RESPONSE_TX: transmitted tt resp traffic packet counter
 * @BATADV_CNT_TT_RESPONSE_RX: received tt resp traffic packet counter
 * @BATADV_CNT_TT_ROAM_ADV_TX: transmitted tt roam traffic packet counter
 * @BATADV_CNT_TT_ROAM_ADV_RX: received tt roam traffic packet counter
 * @BATADV_CNT_DAT_GET_TX: transmitted dht GET traffic packet counter
 * @BATADV_CNT_DAT_GET_RX: received dht GET traffic packet counter
 * @BATADV_CNT_DAT_PUT_TX: transmitted dht PUT traffic packet counter
 * @BATADV_CNT_DAT_PUT_RX: received dht PUT traffic packet counter
 * @BATADV_CNT_DAT_CACHED_REPLY_TX: transmitted dat cache reply traffic packet
 *  counter
 * @BATADV_CNT_NC_CODE: transmitted nc-combined traffic packet counter
 * @BATADV_CNT_NC_CODE_BYTES: transmitted nc-combined traffic bytes counter
 * @BATADV_CNT_NC_RECODE: transmitted nc-recombined traffic packet counter
 * @BATADV_CNT_NC_RECODE_BYTES: transmitted nc-recombined traffic bytes counter
 * @BATADV_CNT_NC_BUFFER: counter for packets buffered for later nc decoding
 * @BATADV_CNT_NC_DECODE: received and nc-decoded traffic packet counter
 * @BATADV_CNT_NC_DECODE_BYTES: received and nc-decoded traffic bytes counter
 * @BATADV_CNT_NC_DECODE_FAILED: received and decode-failed traffic packet
 *  counter
 * @BATADV_CNT_NC_SNIFFED: counter for nc-decoded packets received in promisc
 *  mode.
 * @BATADV_CNT_NUM: number of traffic counters
 */
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
#ifdef CONFIG_BATMAN_ADV_DAT
	BATADV_CNT_DAT_GET_TX,
	BATADV_CNT_DAT_GET_RX,
	BATADV_CNT_DAT_PUT_TX,
	BATADV_CNT_DAT_PUT_RX,
	BATADV_CNT_DAT_CACHED_REPLY_TX,
#endif
#ifdef CONFIG_BATMAN_ADV_NC
	BATADV_CNT_NC_CODE,
	BATADV_CNT_NC_CODE_BYTES,
	BATADV_CNT_NC_RECODE,
	BATADV_CNT_NC_RECODE_BYTES,
	BATADV_CNT_NC_BUFFER,
	BATADV_CNT_NC_DECODE,
	BATADV_CNT_NC_DECODE_BYTES,
	BATADV_CNT_NC_DECODE_FAILED,
	BATADV_CNT_NC_SNIFFED,
#endif
	BATADV_CNT_NUM,
};

/**
 * struct batadv_priv_tt - per mesh interface translation table data
 * @vn: translation table version number
 * @ogm_append_cnt: counter of number of OGMs containing the local tt diff
 * @local_changes: changes registered in an originator interval
 * @changes_list: tracks tt local changes within an originator interval
 * @local_hash: local translation table hash table
 * @global_hash: global translation table hash table
 * @req_list: list of pending & unanswered tt_requests
 * @roam_list: list of the last roaming events of each client limiting the
 *  number of roaming events to avoid route flapping
 * @changes_list_lock: lock protecting changes_list
 * @req_list_lock: lock protecting req_list
 * @roam_list_lock: lock protecting roam_list
 * @local_entry_num: number of entries in the local hash table
 * @local_crc: Checksum of the local table, recomputed before sending a new OGM
 * @last_changeset: last tt changeset this host has generated
 * @last_changeset_len: length of last tt changeset this host has generated
 * @last_changeset_lock: lock protecting last_changeset & last_changeset_len
 * @work: work queue callback item for translation table purging
 */
struct batadv_priv_tt {
	atomic_t vn;
	atomic_t ogm_append_cnt;
	atomic_t local_changes;
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
	/* protects last_changeset & last_changeset_len */
	spinlock_t last_changeset_lock;
	struct delayed_work work;
};

/**
 * struct batadv_priv_bla - per mesh interface bridge loope avoidance data
 * @num_requests; number of bla requests in flight
 * @claim_hash: hash table containing mesh nodes this host has claimed
 * @backbone_hash: hash table containing all detected backbone gateways
 * @bcast_duplist: recently received broadcast packets array (for broadcast
 *  duplicate suppression)
 * @bcast_duplist_curr: index of last broadcast packet added to bcast_duplist
 * @bcast_duplist_lock: lock protecting bcast_duplist & bcast_duplist_curr
 * @claim_dest: local claim data (e.g. claim group)
 * @work: work queue callback item for cleanups & bla announcements
 */
#ifdef CONFIG_BATMAN_ADV_BLA
struct batadv_priv_bla {
	atomic_t num_requests;
	struct batadv_hashtable *claim_hash;
	struct batadv_hashtable *backbone_hash;
	struct batadv_bcast_duplist_entry bcast_duplist[BATADV_DUPLIST_SIZE];
	int bcast_duplist_curr;
	/* protects bcast_duplist & bcast_duplist_curr */
	spinlock_t bcast_duplist_lock;
	struct batadv_bla_claim_dst claim_dest;
	struct delayed_work work;
};
#endif

/**
 * struct batadv_debug_log - debug logging data
 * @log_buff: buffer holding the logs (ring bufer)
 * @log_start: index of next character to read
 * @log_end: index of next character to write
 * @lock: lock protecting log_buff, log_start & log_end
 * @queue_wait: log reader's wait queue
 */
#ifdef CONFIG_BATMAN_ADV_DEBUG
struct batadv_priv_debug_log {
	char log_buff[BATADV_LOG_BUF_LEN];
	unsigned long log_start;
	unsigned long log_end;
	spinlock_t lock; /* protects log_buff, log_start and log_end */
	wait_queue_head_t queue_wait;
};
#endif

/**
 * struct batadv_priv_gw - per mesh interface gateway data
 * @list: list of available gateway nodes
 * @list_lock: lock protecting gw_list & curr_gw
 * @curr_gw: pointer to currently selected gateway node
 * @reselect: bool indicating a gateway re-selection is in progress
 */
struct batadv_priv_gw {
	struct hlist_head list;
	spinlock_t list_lock; /* protects gw_list & curr_gw */
	struct batadv_gw_node __rcu *curr_gw;  /* rcu protected pointer */
	atomic_t reselect;
};

/**
 * struct batadv_priv_vis - per mesh interface vis data
 * @send_list: list of batadv_vis_info packets to sent
 * @hash: hash table containing vis data from other nodes in the network
 * @hash_lock: lock protecting the hash table
 * @list_lock: lock protecting my_info::recv_list
 * @work: work queue callback item for vis packet sending
 * @my_info: holds this node's vis data sent on a regular basis
 */
struct batadv_priv_vis {
	struct list_head send_list;
	struct batadv_hashtable *hash;
	spinlock_t hash_lock; /* protects hash */
	spinlock_t list_lock; /* protects my_info::recv_list */
	struct delayed_work work;
	struct batadv_vis_info *my_info;
};

/**
 * struct batadv_priv_dat - per mesh interface DAT private data
 * @addr: node DAT address
 * @hash: hashtable representing the local ARP cache
 * @work: work queue callback item for cache purging
 */
#ifdef CONFIG_BATMAN_ADV_DAT
struct batadv_priv_dat {
	batadv_dat_addr_t addr;
	struct batadv_hashtable *hash;
	struct delayed_work work;
};
#endif

/**
 * struct batadv_priv_nc - per mesh interface network coding private data
 * @work: work queue callback item for cleanup
 * @debug_dir: dentry for nc subdir in batman-adv directory in debugfs
 * @min_tq: only consider neighbors for encoding if neigh_tq > min_tq
 * @max_fwd_delay: maximum packet forward delay to allow coding of packets
 * @max_buffer_time: buffer time for sniffed packets used to decoding
 * @timestamp_fwd_flush: timestamp of last forward packet queue flush
 * @timestamp_sniffed_purge: timestamp of last sniffed packet queue purge
 * @coding_hash: Hash table used to buffer skbs while waiting for another
 *  incoming skb to code it with. Skbs are added to the buffer just before being
 *  forwarded in routing.c
 * @decoding_hash: Hash table used to buffer skbs that might be needed to decode
 *  a received coded skb. The buffer is used for 1) skbs arriving on the
 *  soft-interface; 2) skbs overheard on the hard-interface; and 3) skbs
 *  forwarded by batman-adv.
 */
struct batadv_priv_nc {
	struct delayed_work work;
	struct dentry *debug_dir;
	u8 min_tq;
	u32 max_fwd_delay;
	u32 max_buffer_time;
	unsigned long timestamp_fwd_flush;
	unsigned long timestamp_sniffed_purge;
	struct batadv_hashtable *coding_hash;
	struct batadv_hashtable *decoding_hash;
};

/**
 * struct batadv_priv - per mesh interface data
 * @mesh_state: current status of the mesh (inactive/active/deactivating)
 * @soft_iface: net device which holds this struct as private data
 * @stats: structure holding the data for the ndo_get_stats() call
 * @bat_counters: mesh internal traffic statistic counters (see batadv_counters)
 * @aggregated_ogms: bool indicating whether OGM aggregation is enabled
 * @bonding: bool indicating whether traffic bonding is enabled
 * @fragmentation: bool indicating whether traffic fragmentation is enabled
 * @ap_isolation: bool indicating whether ap isolation is enabled
 * @bridge_loop_avoidance: bool indicating whether bridge loop avoidance is
 *  enabled
 * @distributed_arp_table: bool indicating whether distributed ARP table is
 *  enabled
 * @vis_mode: vis operation: client or server (see batadv_vis_packettype)
 * @gw_mode: gateway operation: off, client or server (see batadv_gw_modes)
 * @gw_sel_class: gateway selection class (applies if gw_mode client)
 * @gw_bandwidth: gateway announced bandwidth (applies if gw_mode server)
 * @orig_interval: OGM broadcast interval in milliseconds
 * @hop_penalty: penalty which will be applied to an OGM's tq-field on every hop
 * @log_level: configured log level (see batadv_dbg_level)
 * @bcast_seqno: last sent broadcast packet sequence number
 * @bcast_queue_left: number of remaining buffered broadcast packet slots
 * @batman_queue_left: number of remaining OGM packet slots
 * @num_ifaces: number of interfaces assigned to this mesh interface
 * @mesh_obj: kobject for sysfs mesh subdirectory
 * @debug_dir: dentry for debugfs batman-adv subdirectory
 * @forw_bat_list: list of aggregated OGMs that will be forwarded
 * @forw_bcast_list: list of broadcast packets that will be rebroadcasted
 * @orig_hash: hash table containing mesh participants (orig nodes)
 * @forw_bat_list_lock: lock protecting forw_bat_list
 * @forw_bcast_list_lock: lock protecting forw_bcast_list
 * @orig_work: work queue callback item for orig node purging
 * @cleanup_work: work queue callback item for soft interface deinit
 * @primary_if: one of the hard interfaces assigned to this mesh interface
 *  becomes the primary interface
 * @bat_algo_ops: routing algorithm used by this mesh interface
 * @bla: bridge loope avoidance data
 * @debug_log: holding debug logging relevant data
 * @gw: gateway data
 * @tt: translation table data
 * @vis: vis data
 * @dat: distributed arp table data
 * @network_coding: bool indicating whether network coding is enabled
 * @batadv_priv_nc: network coding data
 */
struct batadv_priv {
	atomic_t mesh_state;
	struct net_device *soft_iface;
	struct net_device_stats stats;
	uint64_t __percpu *bat_counters; /* Per cpu counters */
	atomic_t aggregated_ogms;
	atomic_t bonding;
	atomic_t fragmentation;
	atomic_t ap_isolation;
#ifdef CONFIG_BATMAN_ADV_BLA
	atomic_t bridge_loop_avoidance;
#endif
#ifdef CONFIG_BATMAN_ADV_DAT
	atomic_t distributed_arp_table;
#endif
	atomic_t vis_mode;
	atomic_t gw_mode;
	atomic_t gw_sel_class;
	atomic_t gw_bandwidth;
	atomic_t orig_interval;
	atomic_t hop_penalty;
#ifdef CONFIG_BATMAN_ADV_DEBUG
	atomic_t log_level;
#endif
	atomic_t bcast_seqno;
	atomic_t bcast_queue_left;
	atomic_t batman_queue_left;
	char num_ifaces;
	struct kobject *mesh_obj;
	struct dentry *debug_dir;
	struct hlist_head forw_bat_list;
	struct hlist_head forw_bcast_list;
	struct batadv_hashtable *orig_hash;
	spinlock_t forw_bat_list_lock; /* protects forw_bat_list */
	spinlock_t forw_bcast_list_lock; /* protects forw_bcast_list */
	struct delayed_work orig_work;
	struct work_struct cleanup_work;
	struct batadv_hard_iface __rcu *primary_if;  /* rcu protected pointer */
	struct batadv_algo_ops *bat_algo_ops;
#ifdef CONFIG_BATMAN_ADV_BLA
	struct batadv_priv_bla bla;
#endif
#ifdef CONFIG_BATMAN_ADV_DEBUG
	struct batadv_priv_debug_log *debug_log;
#endif
	struct batadv_priv_gw gw;
	struct batadv_priv_tt tt;
	struct batadv_priv_vis vis;
#ifdef CONFIG_BATMAN_ADV_DAT
	struct batadv_priv_dat dat;
#endif
#ifdef CONFIG_BATMAN_ADV_NC
	atomic_t network_coding;
	struct batadv_priv_nc nc;
#endif /* CONFIG_BATMAN_ADV_NC */
};

/**
 * struct batadv_socket_client - layer2 icmp socket client data
 * @queue_list: packet queue for packets destined for this socket client
 * @queue_len: number of packets in the packet queue (queue_list)
 * @index: socket client's index in the batadv_socket_client_hash
 * @lock: lock protecting queue_list, queue_len & index
 * @queue_wait: socket client's wait queue
 * @bat_priv: pointer to soft_iface this client belongs to
 */
struct batadv_socket_client {
	struct list_head queue_list;
	unsigned int queue_len;
	unsigned char index;
	spinlock_t lock; /* protects queue_list, queue_len & index */
	wait_queue_head_t queue_wait;
	struct batadv_priv *bat_priv;
};

/**
 * struct batadv_socket_packet - layer2 icmp packet for socket client
 * @list: list node for batadv_socket_client::queue_list
 * @icmp_len: size of the layer2 icmp packet
 * @icmp_packet: layer2 icmp packet
 */
struct batadv_socket_packet {
	struct list_head list;
	size_t icmp_len;
	struct batadv_icmp_packet_rr icmp_packet;
};

/**
 * struct batadv_bla_backbone_gw - batman-adv gateway bridged into the LAN
 * @orig: originator address of backbone node (mac address of primary iface)
 * @vid: vlan id this gateway was detected on
 * @hash_entry: hlist node for batadv_priv_bla::backbone_hash
 * @bat_priv: pointer to soft_iface this backbone gateway belongs to
 * @lasttime: last time we heard of this backbone gw
 * @wait_periods: grace time for bridge forward delays and bla group forming at
 *  bootup phase - no bcast traffic is formwared until it has elapsed
 * @request_sent: if this bool is set to true we are out of sync with this
 *  backbone gateway - no bcast traffic is formwared until the situation was
 *  resolved
 * @crc: crc16 checksum over all claims
 * @refcount: number of contexts the object is used
 * @rcu: struct used for freeing in an RCU-safe manner
 */
#ifdef CONFIG_BATMAN_ADV_BLA
struct batadv_bla_backbone_gw {
	uint8_t orig[ETH_ALEN];
	short vid;
	struct hlist_node hash_entry;
	struct batadv_priv *bat_priv;
	unsigned long lasttime;
	atomic_t wait_periods;
	atomic_t request_sent;
	uint16_t crc;
	atomic_t refcount;
	struct rcu_head rcu;
};

/**
 * struct batadv_bla_claim - claimed non-mesh client structure
 * @addr: mac address of claimed non-mesh client
 * @vid: vlan id this client was detected on
 * @batadv_bla_backbone_gw: pointer to backbone gw claiming this client
 * @lasttime: last time we heard of claim (locals only)
 * @hash_entry: hlist node for batadv_priv_bla::claim_hash
 * @refcount: number of contexts the object is used
 * @rcu: struct used for freeing in an RCU-safe manner
 */
struct batadv_bla_claim {
	uint8_t addr[ETH_ALEN];
	short vid;
	struct batadv_bla_backbone_gw *backbone_gw;
	unsigned long lasttime;
	struct hlist_node hash_entry;
	struct rcu_head rcu;
	atomic_t refcount;
};
#endif

/**
 * struct batadv_tt_common_entry - tt local & tt global common data
 * @addr: mac address of non-mesh client
 * @hash_entry: hlist node for batadv_priv_tt::local_hash or for
 *  batadv_priv_tt::global_hash
 * @flags: various state handling flags (see batadv_tt_client_flags)
 * @added_at: timestamp used for purging stale tt common entries
 * @refcount: number of contexts the object is used
 * @rcu: struct used for freeing in an RCU-safe manner
 */
struct batadv_tt_common_entry {
	uint8_t addr[ETH_ALEN];
	struct hlist_node hash_entry;
	uint16_t flags;
	unsigned long added_at;
	atomic_t refcount;
	struct rcu_head rcu;
};

/**
 * struct batadv_tt_local_entry - translation table local entry data
 * @common: general translation table data
 * @last_seen: timestamp used for purging stale tt local entries
 */
struct batadv_tt_local_entry {
	struct batadv_tt_common_entry common;
	unsigned long last_seen;
};

/**
 * struct batadv_tt_global_entry - translation table global entry data
 * @common: general translation table data
 * @orig_list: list of orig nodes announcing this non-mesh client
 * @list_lock: lock protecting orig_list
 * @roam_at: time at which TT_GLOBAL_ROAM was set
 */
struct batadv_tt_global_entry {
	struct batadv_tt_common_entry common;
	struct hlist_head orig_list;
	spinlock_t list_lock;	/* protects orig_list */
	unsigned long roam_at;
};

/**
 * struct batadv_tt_orig_list_entry - orig node announcing a non-mesh client
 * @orig_node: pointer to orig node announcing this non-mesh client
 * @ttvn: translation table version number which added the non-mesh client
 * @list: list node for batadv_tt_global_entry::orig_list
 * @refcount: number of contexts the object is used
 * @rcu: struct used for freeing in an RCU-safe manner
 */
struct batadv_tt_orig_list_entry {
	struct batadv_orig_node *orig_node;
	uint8_t ttvn;
	struct hlist_node list;
	atomic_t refcount;
	struct rcu_head rcu;
};

/**
 * struct batadv_tt_change_node - structure for tt changes occured
 * @list: list node for batadv_priv_tt::changes_list
 * @change: holds the actual translation table diff data
 */
struct batadv_tt_change_node {
	struct list_head list;
	struct batadv_tt_change change;
};

/**
 * struct batadv_tt_req_node - data to keep track of the tt requests in flight
 * @addr: mac address address of the originator this request was sent to
 * @issued_at: timestamp used for purging stale tt requests
 * @list: list node for batadv_priv_tt::req_list
 */
struct batadv_tt_req_node {
	uint8_t addr[ETH_ALEN];
	unsigned long issued_at;
	struct list_head list;
};

/**
 * struct batadv_tt_roam_node - roaming client data
 * @addr: mac address of the client in the roaming phase
 * @counter: number of allowed roaming events per client within a single
 *  OGM interval (changes are committed with each OGM)
 * @first_time: timestamp used for purging stale roaming node entries
 * @list: list node for batadv_priv_tt::roam_list
 */
struct batadv_tt_roam_node {
	uint8_t addr[ETH_ALEN];
	atomic_t counter;
	unsigned long first_time;
	struct list_head list;
};

/**
 * struct batadv_nc_node - network coding node
 * @list: next and prev pointer for the list handling
 * @addr: the node's mac address
 * @refcount: number of contexts the object is used by
 * @rcu: struct used for freeing in an RCU-safe manner
 * @orig_node: pointer to corresponding orig node struct
 * @last_seen: timestamp of last ogm received from this node
 */
struct batadv_nc_node {
	struct list_head list;
	uint8_t addr[ETH_ALEN];
	atomic_t refcount;
	struct rcu_head rcu;
	struct batadv_orig_node *orig_node;
	unsigned long last_seen;
};

/**
 * struct batadv_nc_path - network coding path
 * @hash_entry: next and prev pointer for the list handling
 * @rcu: struct used for freeing in an RCU-safe manner
 * @refcount: number of contexts the object is used by
 * @packet_list: list of buffered packets for this path
 * @packet_list_lock: access lock for packet list
 * @next_hop: next hop (destination) of path
 * @prev_hop: previous hop (source) of path
 * @last_valid: timestamp for last validation of path
 */
struct batadv_nc_path {
	struct hlist_node hash_entry;
	struct rcu_head rcu;
	atomic_t refcount;
	struct list_head packet_list;
	spinlock_t packet_list_lock; /* Protects packet_list */
	uint8_t next_hop[ETH_ALEN];
	uint8_t prev_hop[ETH_ALEN];
	unsigned long last_valid;
};

/**
 * struct batadv_nc_packet - network coding packet used when coding and
 *  decoding packets
 * @list: next and prev pointer for the list handling
 * @packet_id: crc32 checksum of skb data
 * @timestamp: field containing the info when the packet was added to path
 * @neigh_node: pointer to original next hop neighbor of skb
 * @skb: skb which can be encoded or used for decoding
 * @nc_path: pointer to path this nc packet is attached to
 */
struct batadv_nc_packet {
	struct list_head list;
	__be32 packet_id;
	unsigned long timestamp;
	struct batadv_neigh_node *neigh_node;
	struct sk_buff *skb;
	struct batadv_nc_path *nc_path;
};

/**
 * batadv_skb_cb - control buffer structure used to store private data relevant
 *  to batman-adv in the skb->cb buffer in skbs.
 * @decoded: Marks a skb as decoded, which is checked when searching for coding
 *  opportunities in network-coding.c
 */
struct batadv_skb_cb {
	bool decoded;
};

/**
 * struct batadv_forw_packet - structure for bcast packets to be sent/forwarded
 * @list: list node for batadv_socket_client::queue_list
 * @send_time: execution time for delayed_work (packet sending)
 * @own: bool for locally generated packets (local OGMs are re-scheduled after
 *  sending)
 * @skb: bcast packet's skb buffer
 * @packet_len: size of aggregated OGM packet inside the skb buffer
 * @direct_link_flags: direct link flags for aggregated OGM packets
 * @num_packets: counter for bcast packet retransmission
 * @delayed_work: work queue callback item for packet sending
 * @if_incoming: pointer incoming hard-iface or primary iface if locally
 *  generated packet
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

/**
 * struct batadv_frag_packet_list_entry - storage for fragment packet
 * @list: list node for orig_node::frag_list
 * @seqno: sequence number of the fragment
 * @skb: fragment's skb buffer
 */
struct batadv_frag_packet_list_entry {
	struct list_head list;
	uint16_t seqno;
	struct sk_buff *skb;
};

/**
 * struct batadv_vis_info - local data for vis information
 * @first_seen: timestamp used for purging stale vis info entries
 * @recv_list: List of server-neighbors we have received this packet from. This
 *  packet should not be re-forward to them again. List elements are struct
 *  batadv_vis_recvlist_node
 * @send_list: list of packets to be forwarded
 * @refcount: number of contexts the object is used
 * @hash_entry: hlist node for batadv_priv_vis::hash
 * @bat_priv: pointer to soft_iface this orig node belongs to
 * @skb_packet: contains the vis packet
 */
struct batadv_vis_info {
	unsigned long first_seen;
	struct list_head recv_list;
	struct list_head send_list;
	struct kref refcount;
	struct hlist_node hash_entry;
	struct batadv_priv *bat_priv;
	struct sk_buff *skb_packet;
} __packed;

/**
 * struct batadv_vis_info_entry - contains link information for vis
 * @src: source MAC of the link, all zero for local TT entry
 * @dst: destination MAC of the link, client mac address for local TT entry
 * @quality: transmission quality of the link, or 0 for local TT entry
 */
struct batadv_vis_info_entry {
	uint8_t  src[ETH_ALEN];
	uint8_t  dest[ETH_ALEN];
	uint8_t  quality;
} __packed;

/**
 * struct batadv_vis_recvlist_node - list entry for batadv_vis_info::recv_list
 * @list: list node for batadv_vis_info::recv_list
 * @mac: MAC address of the originator from where the vis_info was received
 */
struct batadv_vis_recvlist_node {
	struct list_head list;
	uint8_t mac[ETH_ALEN];
};

/**
 * struct batadv_vis_if_list_entry - auxiliary data for vis data generation
 * @addr: MAC address of the interface
 * @primary: true if this interface is the primary interface
 * @list: list node the interface list
 *
 * While scanning for vis-entries of a particular vis-originator
 * this list collects its interfaces to create a subgraph/cluster
 * out of them later
 */
struct batadv_vis_if_list_entry {
	uint8_t addr[ETH_ALEN];
	bool primary;
	struct hlist_node list;
};

/**
 * struct batadv_algo_ops - mesh algorithm callbacks
 * @list: list node for the batadv_algo_list
 * @name: name of the algorithm
 * @bat_iface_enable: init routing info when hard-interface is enabled
 * @bat_iface_disable: de-init routing info when hard-interface is disabled
 * @bat_iface_update_mac: (re-)init mac addresses of the protocol information
 *  belonging to this hard-interface
 * @bat_primary_iface_set: called when primary interface is selected / changed
 * @bat_ogm_schedule: prepare a new outgoing OGM for the send queue
 * @bat_ogm_emit: send scheduled OGM
 */
struct batadv_algo_ops {
	struct hlist_node list;
	char *name;
	int (*bat_iface_enable)(struct batadv_hard_iface *hard_iface);
	void (*bat_iface_disable)(struct batadv_hard_iface *hard_iface);
	void (*bat_iface_update_mac)(struct batadv_hard_iface *hard_iface);
	void (*bat_primary_iface_set)(struct batadv_hard_iface *hard_iface);
	void (*bat_ogm_schedule)(struct batadv_hard_iface *hard_iface);
	void (*bat_ogm_emit)(struct batadv_forw_packet *forw_packet);
};

/**
 * struct batadv_dat_entry - it is a single entry of batman-adv ARP backend. It
 * is used to stored ARP entries needed for the global DAT cache
 * @ip: the IPv4 corresponding to this DAT/ARP entry
 * @mac_addr: the MAC address associated to the stored IPv4
 * @last_update: time in jiffies when this entry was refreshed last time
 * @hash_entry: hlist node for batadv_priv_dat::hash
 * @refcount: number of contexts the object is used
 * @rcu: struct used for freeing in an RCU-safe manner
 */
struct batadv_dat_entry {
	__be32 ip;
	uint8_t mac_addr[ETH_ALEN];
	unsigned long last_update;
	struct hlist_node hash_entry;
	atomic_t refcount;
	struct rcu_head rcu;
};

/**
 * struct batadv_dat_candidate - candidate destination for DAT operations
 * @type: the type of the selected candidate. It can one of the following:
 *	  - BATADV_DAT_CANDIDATE_NOT_FOUND
 *	  - BATADV_DAT_CANDIDATE_ORIG
 * @orig_node: if type is BATADV_DAT_CANDIDATE_ORIG this field points to the
 *	       corresponding originator node structure
 */
struct batadv_dat_candidate {
	int type;
	struct batadv_orig_node *orig_node;
};

#endif /* _NET_BATMAN_ADV_TYPES_H_ */
