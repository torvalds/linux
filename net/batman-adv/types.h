/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2007-2020  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 */

#ifndef _NET_BATMAN_ADV_TYPES_H_
#define _NET_BATMAN_ADV_TYPES_H_

#ifndef _NET_BATMAN_ADV_MAIN_H_
#error only "main.h" can be included directly
#endif

#include <linux/average.h>
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/sched.h> /* for linux/wait.h */
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <uapi/linux/batadv_packet.h>
#include <uapi/linux/batman_adv.h>

#ifdef CONFIG_BATMAN_ADV_DAT

/**
 * typedef batadv_dat_addr_t - type used for all DHT addresses
 *
 * If it is changed, BATADV_DAT_ADDR_MAX is changed as well.
 *
 * *Please be careful: batadv_dat_addr_t must be UNSIGNED*
 */
typedef u16 batadv_dat_addr_t;

#endif /* CONFIG_BATMAN_ADV_DAT */

/**
 * enum batadv_dhcp_recipient - dhcp destination
 */
enum batadv_dhcp_recipient {
	/** @BATADV_DHCP_NO: packet is not a dhcp message */
	BATADV_DHCP_NO = 0,

	/** @BATADV_DHCP_TO_SERVER: dhcp message is directed to a server */
	BATADV_DHCP_TO_SERVER,

	/** @BATADV_DHCP_TO_CLIENT: dhcp message is directed to a client */
	BATADV_DHCP_TO_CLIENT,
};

/**
 * BATADV_TT_REMOTE_MASK - bitmask selecting the flags that are sent over the
 *  wire only
 */
#define BATADV_TT_REMOTE_MASK	0x00FF

/**
 * BATADV_TT_SYNC_MASK - bitmask of the flags that need to be kept in sync
 *  among the nodes. These flags are used to compute the global/local CRC
 */
#define BATADV_TT_SYNC_MASK	0x00F0

/**
 * struct batadv_hard_iface_bat_iv - per hard-interface B.A.T.M.A.N. IV data
 */
struct batadv_hard_iface_bat_iv {
	/** @ogm_buff: buffer holding the OGM packet */
	unsigned char *ogm_buff;

	/** @ogm_buff_len: length of the OGM packet buffer */
	int ogm_buff_len;

	/** @ogm_seqno: OGM sequence number - used to identify each OGM */
	atomic_t ogm_seqno;

	/** @ogm_buff_mutex: lock protecting ogm_buff and ogm_buff_len */
	struct mutex ogm_buff_mutex;
};

/**
 * enum batadv_v_hard_iface_flags - interface flags useful to B.A.T.M.A.N. V
 */
enum batadv_v_hard_iface_flags {
	/**
	 * @BATADV_FULL_DUPLEX: tells if the connection over this link is
	 *  full-duplex
	 */
	BATADV_FULL_DUPLEX	= BIT(0),

	/**
	 * @BATADV_WARNING_DEFAULT: tells whether we have warned the user that
	 *  no throughput data is available for this interface and that default
	 *  values are assumed.
	 */
	BATADV_WARNING_DEFAULT	= BIT(1),
};

/**
 * struct batadv_hard_iface_bat_v - per hard-interface B.A.T.M.A.N. V data
 */
struct batadv_hard_iface_bat_v {
	/** @elp_interval: time interval between two ELP transmissions */
	atomic_t elp_interval;

	/** @elp_seqno: current ELP sequence number */
	atomic_t elp_seqno;

	/** @elp_skb: base skb containing the ELP message to send */
	struct sk_buff *elp_skb;

	/** @elp_wq: workqueue used to schedule ELP transmissions */
	struct delayed_work elp_wq;

	/** @aggr_wq: workqueue used to transmit queued OGM packets */
	struct delayed_work aggr_wq;

	/** @aggr_list: queue for to be aggregated OGM packets */
	struct sk_buff_head aggr_list;

	/** @aggr_len: size of the OGM aggregate (excluding ethernet header) */
	unsigned int aggr_len;

	/**
	 * @throughput_override: throughput override to disable link
	 *  auto-detection
	 */
	atomic_t throughput_override;

	/** @flags: interface specific flags */
	u8 flags;
};

/**
 * enum batadv_hard_iface_wifi_flags - Flags describing the wifi configuration
 *  of a batadv_hard_iface
 */
enum batadv_hard_iface_wifi_flags {
	/** @BATADV_HARDIF_WIFI_WEXT_DIRECT: it is a wext wifi device */
	BATADV_HARDIF_WIFI_WEXT_DIRECT = BIT(0),

	/** @BATADV_HARDIF_WIFI_CFG80211_DIRECT: it is a cfg80211 wifi device */
	BATADV_HARDIF_WIFI_CFG80211_DIRECT = BIT(1),

	/**
	 * @BATADV_HARDIF_WIFI_WEXT_INDIRECT: link device is a wext wifi device
	 */
	BATADV_HARDIF_WIFI_WEXT_INDIRECT = BIT(2),

	/**
	 * @BATADV_HARDIF_WIFI_CFG80211_INDIRECT: link device is a cfg80211 wifi
	 * device
	 */
	BATADV_HARDIF_WIFI_CFG80211_INDIRECT = BIT(3),
};

/**
 * struct batadv_hard_iface - network device known to batman-adv
 */
struct batadv_hard_iface {
	/** @list: list node for batadv_hardif_list */
	struct list_head list;

	/** @if_status: status of the interface for batman-adv */
	char if_status;

	/**
	 * @num_bcasts: number of payload re-broadcasts on this interface (ARQ)
	 */
	u8 num_bcasts;

	/**
	 * @wifi_flags: flags whether this is (directly or indirectly) a wifi
	 *  interface
	 */
	u32 wifi_flags;

	/** @net_dev: pointer to the net_device */
	struct net_device *net_dev;

	/** @refcount: number of contexts the object is used */
	struct kref refcount;

	/**
	 * @batman_adv_ptype: packet type describing packets that should be
	 * processed by batman-adv for this interface
	 */
	struct packet_type batman_adv_ptype;

	/**
	 * @soft_iface: the batman-adv interface which uses this network
	 *  interface
	 */
	struct net_device *soft_iface;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;

	/**
	 * @hop_penalty: penalty which will be applied to the tq-field
	 * of an OGM received via this interface
	 */
	atomic_t hop_penalty;

	/** @bat_iv: per hard-interface B.A.T.M.A.N. IV data */
	struct batadv_hard_iface_bat_iv bat_iv;

#ifdef CONFIG_BATMAN_ADV_BATMAN_V
	/** @bat_v: per hard-interface B.A.T.M.A.N. V data */
	struct batadv_hard_iface_bat_v bat_v;
#endif

	/**
	 * @neigh_list: list of unique single hop neighbors via this interface
	 */
	struct hlist_head neigh_list;

	/** @neigh_list_lock: lock protecting neigh_list */
	spinlock_t neigh_list_lock;
};

/**
 * struct batadv_orig_ifinfo - B.A.T.M.A.N. IV private orig_ifinfo members
 */
struct batadv_orig_ifinfo_bat_iv {
	/**
	 * @bcast_own: bitfield which counts the number of our OGMs this
	 * orig_node rebroadcasted "back" to us  (relative to last_real_seqno)
	 */
	DECLARE_BITMAP(bcast_own, BATADV_TQ_LOCAL_WINDOW_SIZE);

	/** @bcast_own_sum: sum of bcast_own */
	u8 bcast_own_sum;
};

/**
 * struct batadv_orig_ifinfo - originator info per outgoing interface
 */
struct batadv_orig_ifinfo {
	/** @list: list node for &batadv_orig_node.ifinfo_list */
	struct hlist_node list;

	/** @if_outgoing: pointer to outgoing hard-interface */
	struct batadv_hard_iface *if_outgoing;

	/** @router: router that should be used to reach this originator */
	struct batadv_neigh_node __rcu *router;

	/** @last_real_seqno: last and best known sequence number */
	u32 last_real_seqno;

	/** @last_ttl: ttl of last received packet */
	u8 last_ttl;

	/** @last_seqno_forwarded: seqno of the OGM which was forwarded last */
	u32 last_seqno_forwarded;

	/** @batman_seqno_reset: time when the batman seqno window was reset */
	unsigned long batman_seqno_reset;

	/** @bat_iv: B.A.T.M.A.N. IV private structure */
	struct batadv_orig_ifinfo_bat_iv bat_iv;

	/** @refcount: number of contexts the object is used */
	struct kref refcount;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;
};

/**
 * struct batadv_frag_table_entry - head in the fragment buffer table
 */
struct batadv_frag_table_entry {
	/** @fragment_list: head of list with fragments */
	struct hlist_head fragment_list;

	/** @lock: lock to protect the list of fragments */
	spinlock_t lock;

	/** @timestamp: time (jiffie) of last received fragment */
	unsigned long timestamp;

	/** @seqno: sequence number of the fragments in the list */
	u16 seqno;

	/** @size: accumulated size of packets in list */
	u16 size;

	/** @total_size: expected size of the assembled packet */
	u16 total_size;
};

/**
 * struct batadv_frag_list_entry - entry in a list of fragments
 */
struct batadv_frag_list_entry {
	/** @list: list node information */
	struct hlist_node list;

	/** @skb: fragment */
	struct sk_buff *skb;

	/** @no: fragment number in the set */
	u8 no;
};

/**
 * struct batadv_vlan_tt - VLAN specific TT attributes
 */
struct batadv_vlan_tt {
	/** @crc: CRC32 checksum of the entries belonging to this vlan */
	u32 crc;

	/** @num_entries: number of TT entries for this VLAN */
	atomic_t num_entries;
};

/**
 * struct batadv_orig_node_vlan - VLAN specific data per orig_node
 */
struct batadv_orig_node_vlan {
	/** @vid: the VLAN identifier */
	unsigned short vid;

	/** @tt: VLAN specific TT attributes */
	struct batadv_vlan_tt tt;

	/** @list: list node for &batadv_orig_node.vlan_list */
	struct hlist_node list;

	/**
	 * @refcount: number of context where this object is currently in use
	 */
	struct kref refcount;

	/** @rcu: struct used for freeing in a RCU-safe manner */
	struct rcu_head rcu;
};

/**
 * struct batadv_orig_bat_iv - B.A.T.M.A.N. IV private orig_node members
 */
struct batadv_orig_bat_iv {
	/**
	 * @ogm_cnt_lock: lock protecting &batadv_orig_ifinfo_bat_iv.bcast_own,
	 * &batadv_orig_ifinfo_bat_iv.bcast_own_sum,
	 * &batadv_neigh_ifinfo_bat_iv.bat_iv.real_bits and
	 * &batadv_neigh_ifinfo_bat_iv.real_packet_count
	 */
	spinlock_t ogm_cnt_lock;
};

/**
 * struct batadv_orig_node - structure for orig_list maintaining nodes of mesh
 */
struct batadv_orig_node {
	/** @orig: originator ethernet address */
	u8 orig[ETH_ALEN];

	/** @ifinfo_list: list for routers per outgoing interface */
	struct hlist_head ifinfo_list;

	/**
	 * @last_bonding_candidate: pointer to last ifinfo of last used router
	 */
	struct batadv_orig_ifinfo *last_bonding_candidate;

#ifdef CONFIG_BATMAN_ADV_DAT
	/** @dat_addr: address of the orig node in the distributed hash */
	batadv_dat_addr_t dat_addr;
#endif

	/** @last_seen: time when last packet from this node was received */
	unsigned long last_seen;

	/**
	 * @bcast_seqno_reset: time when the broadcast seqno window was reset
	 */
	unsigned long bcast_seqno_reset;

#ifdef CONFIG_BATMAN_ADV_MCAST
	/**
	 * @mcast_handler_lock: synchronizes mcast-capability and -flag changes
	 */
	spinlock_t mcast_handler_lock;

	/** @mcast_flags: multicast flags announced by the orig node */
	u8 mcast_flags;

	/**
	 * @mcast_want_all_unsnoopables_node: a list node for the
	 *  mcast.want_all_unsnoopables list
	 */
	struct hlist_node mcast_want_all_unsnoopables_node;

	/**
	 * @mcast_want_all_ipv4_node: a list node for the mcast.want_all_ipv4
	 *  list
	 */
	struct hlist_node mcast_want_all_ipv4_node;
	/**
	 * @mcast_want_all_ipv6_node: a list node for the mcast.want_all_ipv6
	 *  list
	 */
	struct hlist_node mcast_want_all_ipv6_node;

	/**
	 * @mcast_want_all_rtr4_node: a list node for the mcast.want_all_rtr4
	 *  list
	 */
	struct hlist_node mcast_want_all_rtr4_node;
	/**
	 * @mcast_want_all_rtr6_node: a list node for the mcast.want_all_rtr6
	 *  list
	 */
	struct hlist_node mcast_want_all_rtr6_node;
#endif

	/** @capabilities: announced capabilities of this originator */
	unsigned long capabilities;

	/**
	 * @capa_initialized: bitfield to remember whether a capability was
	 *  initialized
	 */
	unsigned long capa_initialized;

	/** @last_ttvn: last seen translation table version number */
	atomic_t last_ttvn;

	/** @tt_buff: last tt changeset this node received from the orig node */
	unsigned char *tt_buff;

	/**
	 * @tt_buff_len: length of the last tt changeset this node received
	 *  from the orig node
	 */
	s16 tt_buff_len;

	/** @tt_buff_lock: lock that protects tt_buff and tt_buff_len */
	spinlock_t tt_buff_lock;

	/**
	 * @tt_lock: avoids concurrent read from and write to the table. Table
	 *  update is made up of two operations (data structure update and
	 *  metadata -CRC/TTVN-recalculation) and they have to be executed
	 *  atomically in order to avoid another thread to read the
	 *  table/metadata between those.
	 */
	spinlock_t tt_lock;

	/**
	 * @bcast_bits: bitfield containing the info which payload broadcast
	 *  originated from this orig node this host already has seen (relative
	 *  to last_bcast_seqno)
	 */
	DECLARE_BITMAP(bcast_bits, BATADV_TQ_LOCAL_WINDOW_SIZE);

	/**
	 * @last_bcast_seqno: last broadcast sequence number received by this
	 *  host
	 */
	u32 last_bcast_seqno;

	/**
	 * @neigh_list: list of potential next hop neighbor towards this orig
	 *  node
	 */
	struct hlist_head neigh_list;

	/**
	 * @neigh_list_lock: lock protecting neigh_list, ifinfo_list,
	 *  last_bonding_candidate and router
	 */
	spinlock_t neigh_list_lock;

	/** @hash_entry: hlist node for &batadv_priv.orig_hash */
	struct hlist_node hash_entry;

	/** @bat_priv: pointer to soft_iface this orig node belongs to */
	struct batadv_priv *bat_priv;

	/** @bcast_seqno_lock: lock protecting bcast_bits & last_bcast_seqno */
	spinlock_t bcast_seqno_lock;

	/** @refcount: number of contexts the object is used */
	struct kref refcount;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;

#ifdef CONFIG_BATMAN_ADV_NC
	/** @in_coding_list: list of nodes this orig can hear */
	struct list_head in_coding_list;

	/** @out_coding_list: list of nodes that can hear this orig */
	struct list_head out_coding_list;

	/** @in_coding_list_lock: protects in_coding_list */
	spinlock_t in_coding_list_lock;

	/** @out_coding_list_lock: protects out_coding_list */
	spinlock_t out_coding_list_lock;
#endif

	/** @fragments: array with heads for fragment chains */
	struct batadv_frag_table_entry fragments[BATADV_FRAG_BUFFER_COUNT];

	/**
	 * @vlan_list: a list of orig_node_vlan structs, one per VLAN served by
	 *  the originator represented by this object
	 */
	struct hlist_head vlan_list;

	/** @vlan_list_lock: lock protecting vlan_list */
	spinlock_t vlan_list_lock;

	/** @bat_iv: B.A.T.M.A.N. IV private structure */
	struct batadv_orig_bat_iv bat_iv;
};

/**
 * enum batadv_orig_capabilities - orig node capabilities
 */
enum batadv_orig_capabilities {
	/**
	 * @BATADV_ORIG_CAPA_HAS_DAT: orig node has distributed arp table
	 *  enabled
	 */
	BATADV_ORIG_CAPA_HAS_DAT,

	/** @BATADV_ORIG_CAPA_HAS_NC: orig node has network coding enabled */
	BATADV_ORIG_CAPA_HAS_NC,

	/** @BATADV_ORIG_CAPA_HAS_TT: orig node has tt capability */
	BATADV_ORIG_CAPA_HAS_TT,

	/**
	 * @BATADV_ORIG_CAPA_HAS_MCAST: orig node has some multicast capability
	 *  (= orig node announces a tvlv of type BATADV_TVLV_MCAST)
	 */
	BATADV_ORIG_CAPA_HAS_MCAST,
};

/**
 * struct batadv_gw_node - structure for orig nodes announcing gw capabilities
 */
struct batadv_gw_node {
	/** @list: list node for &batadv_priv_gw.list */
	struct hlist_node list;

	/** @orig_node: pointer to corresponding orig node */
	struct batadv_orig_node *orig_node;

	/** @bandwidth_down: advertised uplink download bandwidth */
	u32 bandwidth_down;

	/** @bandwidth_up: advertised uplink upload bandwidth */
	u32 bandwidth_up;

	/** @refcount: number of contexts the object is used */
	struct kref refcount;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;
};

DECLARE_EWMA(throughput, 10, 8)

/**
 * struct batadv_hardif_neigh_node_bat_v - B.A.T.M.A.N. V private neighbor
 *  information
 */
struct batadv_hardif_neigh_node_bat_v {
	/** @throughput: ewma link throughput towards this neighbor */
	struct ewma_throughput throughput;

	/** @elp_interval: time interval between two ELP transmissions */
	u32 elp_interval;

	/** @elp_latest_seqno: latest and best known ELP sequence number */
	u32 elp_latest_seqno;

	/**
	 * @last_unicast_tx: when the last unicast packet has been sent to this
	 *  neighbor
	 */
	unsigned long last_unicast_tx;

	/** @metric_work: work queue callback item for metric update */
	struct work_struct metric_work;
};

/**
 * struct batadv_hardif_neigh_node - unique neighbor per hard-interface
 */
struct batadv_hardif_neigh_node {
	/** @list: list node for &batadv_hard_iface.neigh_list */
	struct hlist_node list;

	/** @addr: the MAC address of the neighboring interface */
	u8 addr[ETH_ALEN];

	/**
	 * @orig: the address of the originator this neighbor node belongs to
	 */
	u8 orig[ETH_ALEN];

	/** @if_incoming: pointer to incoming hard-interface */
	struct batadv_hard_iface *if_incoming;

	/** @last_seen: when last packet via this neighbor was received */
	unsigned long last_seen;

#ifdef CONFIG_BATMAN_ADV_BATMAN_V
	/** @bat_v: B.A.T.M.A.N. V private data */
	struct batadv_hardif_neigh_node_bat_v bat_v;
#endif

	/** @refcount: number of contexts the object is used */
	struct kref refcount;

	/** @rcu: struct used for freeing in a RCU-safe manner */
	struct rcu_head rcu;
};

/**
 * struct batadv_neigh_node - structure for single hops neighbors
 */
struct batadv_neigh_node {
	/** @list: list node for &batadv_orig_node.neigh_list */
	struct hlist_node list;

	/** @orig_node: pointer to corresponding orig_node */
	struct batadv_orig_node *orig_node;

	/** @addr: the MAC address of the neighboring interface */
	u8 addr[ETH_ALEN];

	/** @ifinfo_list: list for routing metrics per outgoing interface */
	struct hlist_head ifinfo_list;

	/** @ifinfo_lock: lock protecting ifinfo_list and its members */
	spinlock_t ifinfo_lock;

	/** @if_incoming: pointer to incoming hard-interface */
	struct batadv_hard_iface *if_incoming;

	/** @last_seen: when last packet via this neighbor was received */
	unsigned long last_seen;

	/** @hardif_neigh: hardif_neigh of this neighbor */
	struct batadv_hardif_neigh_node *hardif_neigh;

	/** @refcount: number of contexts the object is used */
	struct kref refcount;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;
};

/**
 * struct batadv_neigh_ifinfo_bat_iv - neighbor information per outgoing
 *  interface for B.A.T.M.A.N. IV
 */
struct batadv_neigh_ifinfo_bat_iv {
	/** @tq_recv: ring buffer of received TQ values from this neigh node */
	u8 tq_recv[BATADV_TQ_GLOBAL_WINDOW_SIZE];

	/** @tq_index: ring buffer index */
	u8 tq_index;

	/**
	 * @tq_avg: averaged tq of all tq values in the ring buffer (tq_recv)
	 */
	u8 tq_avg;

	/**
	 * @real_bits: bitfield containing the number of OGMs received from this
	 *  neigh node (relative to orig_node->last_real_seqno)
	 */
	DECLARE_BITMAP(real_bits, BATADV_TQ_LOCAL_WINDOW_SIZE);

	/** @real_packet_count: counted result of real_bits */
	u8 real_packet_count;
};

/**
 * struct batadv_neigh_ifinfo_bat_v - neighbor information per outgoing
 *  interface for B.A.T.M.A.N. V
 */
struct batadv_neigh_ifinfo_bat_v {
	/**
	 * @throughput: last throughput metric received from originator via this
	 *  neigh
	 */
	u32 throughput;

	/** @last_seqno: last sequence number known for this neighbor */
	u32 last_seqno;
};

/**
 * struct batadv_neigh_ifinfo - neighbor information per outgoing interface
 */
struct batadv_neigh_ifinfo {
	/** @list: list node for &batadv_neigh_node.ifinfo_list */
	struct hlist_node list;

	/** @if_outgoing: pointer to outgoing hard-interface */
	struct batadv_hard_iface *if_outgoing;

	/** @bat_iv: B.A.T.M.A.N. IV private structure */
	struct batadv_neigh_ifinfo_bat_iv bat_iv;

#ifdef CONFIG_BATMAN_ADV_BATMAN_V
	/** @bat_v: B.A.T.M.A.N. V private data */
	struct batadv_neigh_ifinfo_bat_v bat_v;
#endif

	/** @last_ttl: last received ttl from this neigh node */
	u8 last_ttl;

	/** @refcount: number of contexts the object is used */
	struct kref refcount;

	/** @rcu: struct used for freeing in a RCU-safe manner */
	struct rcu_head rcu;
};

#ifdef CONFIG_BATMAN_ADV_BLA

/**
 * struct batadv_bcast_duplist_entry - structure for LAN broadcast suppression
 */
struct batadv_bcast_duplist_entry {
	/** @orig: mac address of orig node originating the broadcast */
	u8 orig[ETH_ALEN];

	/** @crc: crc32 checksum of broadcast payload */
	__be32 crc;

	/** @entrytime: time when the broadcast packet was received */
	unsigned long entrytime;
};
#endif

/**
 * enum batadv_counters - indices for traffic counters
 */
enum batadv_counters {
	/** @BATADV_CNT_TX: transmitted payload traffic packet counter */
	BATADV_CNT_TX,

	/** @BATADV_CNT_TX_BYTES: transmitted payload traffic bytes counter */
	BATADV_CNT_TX_BYTES,

	/**
	 * @BATADV_CNT_TX_DROPPED: dropped transmission payload traffic packet
	 *  counter
	 */
	BATADV_CNT_TX_DROPPED,

	/** @BATADV_CNT_RX: received payload traffic packet counter */
	BATADV_CNT_RX,

	/** @BATADV_CNT_RX_BYTES: received payload traffic bytes counter */
	BATADV_CNT_RX_BYTES,

	/** @BATADV_CNT_FORWARD: forwarded payload traffic packet counter */
	BATADV_CNT_FORWARD,

	/**
	 * @BATADV_CNT_FORWARD_BYTES: forwarded payload traffic bytes counter
	 */
	BATADV_CNT_FORWARD_BYTES,

	/**
	 * @BATADV_CNT_MGMT_TX: transmitted routing protocol traffic packet
	 *  counter
	 */
	BATADV_CNT_MGMT_TX,

	/**
	 * @BATADV_CNT_MGMT_TX_BYTES: transmitted routing protocol traffic bytes
	 *  counter
	 */
	BATADV_CNT_MGMT_TX_BYTES,

	/**
	 * @BATADV_CNT_MGMT_RX: received routing protocol traffic packet counter
	 */
	BATADV_CNT_MGMT_RX,

	/**
	 * @BATADV_CNT_MGMT_RX_BYTES: received routing protocol traffic bytes
	 *  counter
	 */
	BATADV_CNT_MGMT_RX_BYTES,

	/** @BATADV_CNT_FRAG_TX: transmitted fragment traffic packet counter */
	BATADV_CNT_FRAG_TX,

	/**
	 * @BATADV_CNT_FRAG_TX_BYTES: transmitted fragment traffic bytes counter
	 */
	BATADV_CNT_FRAG_TX_BYTES,

	/** @BATADV_CNT_FRAG_RX: received fragment traffic packet counter */
	BATADV_CNT_FRAG_RX,

	/**
	 * @BATADV_CNT_FRAG_RX_BYTES: received fragment traffic bytes counter
	 */
	BATADV_CNT_FRAG_RX_BYTES,

	/** @BATADV_CNT_FRAG_FWD: forwarded fragment traffic packet counter */
	BATADV_CNT_FRAG_FWD,

	/**
	 * @BATADV_CNT_FRAG_FWD_BYTES: forwarded fragment traffic bytes counter
	 */
	BATADV_CNT_FRAG_FWD_BYTES,

	/**
	 * @BATADV_CNT_TT_REQUEST_TX: transmitted tt req traffic packet counter
	 */
	BATADV_CNT_TT_REQUEST_TX,

	/** @BATADV_CNT_TT_REQUEST_RX: received tt req traffic packet counter */
	BATADV_CNT_TT_REQUEST_RX,

	/**
	 * @BATADV_CNT_TT_RESPONSE_TX: transmitted tt resp traffic packet
	 *  counter
	 */
	BATADV_CNT_TT_RESPONSE_TX,

	/**
	 * @BATADV_CNT_TT_RESPONSE_RX: received tt resp traffic packet counter
	 */
	BATADV_CNT_TT_RESPONSE_RX,

	/**
	 * @BATADV_CNT_TT_ROAM_ADV_TX: transmitted tt roam traffic packet
	 *  counter
	 */
	BATADV_CNT_TT_ROAM_ADV_TX,

	/**
	 * @BATADV_CNT_TT_ROAM_ADV_RX: received tt roam traffic packet counter
	 */
	BATADV_CNT_TT_ROAM_ADV_RX,

#ifdef CONFIG_BATMAN_ADV_DAT
	/**
	 * @BATADV_CNT_DAT_GET_TX: transmitted dht GET traffic packet counter
	 */
	BATADV_CNT_DAT_GET_TX,

	/** @BATADV_CNT_DAT_GET_RX: received dht GET traffic packet counter */
	BATADV_CNT_DAT_GET_RX,

	/**
	 * @BATADV_CNT_DAT_PUT_TX: transmitted dht PUT traffic packet counter
	 */
	BATADV_CNT_DAT_PUT_TX,

	/** @BATADV_CNT_DAT_PUT_RX: received dht PUT traffic packet counter */
	BATADV_CNT_DAT_PUT_RX,

	/**
	 * @BATADV_CNT_DAT_CACHED_REPLY_TX: transmitted dat cache reply traffic
	 *  packet counter
	 */
	BATADV_CNT_DAT_CACHED_REPLY_TX,
#endif

#ifdef CONFIG_BATMAN_ADV_NC
	/**
	 * @BATADV_CNT_NC_CODE: transmitted nc-combined traffic packet counter
	 */
	BATADV_CNT_NC_CODE,

	/**
	 * @BATADV_CNT_NC_CODE_BYTES: transmitted nc-combined traffic bytes
	 *  counter
	 */
	BATADV_CNT_NC_CODE_BYTES,

	/**
	 * @BATADV_CNT_NC_RECODE: transmitted nc-recombined traffic packet
	 *  counter
	 */
	BATADV_CNT_NC_RECODE,

	/**
	 * @BATADV_CNT_NC_RECODE_BYTES: transmitted nc-recombined traffic bytes
	 *  counter
	 */
	BATADV_CNT_NC_RECODE_BYTES,

	/**
	 * @BATADV_CNT_NC_BUFFER: counter for packets buffered for later nc
	 *  decoding
	 */
	BATADV_CNT_NC_BUFFER,

	/**
	 * @BATADV_CNT_NC_DECODE: received and nc-decoded traffic packet counter
	 */
	BATADV_CNT_NC_DECODE,

	/**
	 * @BATADV_CNT_NC_DECODE_BYTES: received and nc-decoded traffic bytes
	 *  counter
	 */
	BATADV_CNT_NC_DECODE_BYTES,

	/**
	 * @BATADV_CNT_NC_DECODE_FAILED: received and decode-failed traffic
	 *  packet counter
	 */
	BATADV_CNT_NC_DECODE_FAILED,

	/**
	 * @BATADV_CNT_NC_SNIFFED: counter for nc-decoded packets received in
	 *  promisc mode.
	 */
	BATADV_CNT_NC_SNIFFED,
#endif

	/** @BATADV_CNT_NUM: number of traffic counters */
	BATADV_CNT_NUM,
};

/**
 * struct batadv_priv_tt - per mesh interface translation table data
 */
struct batadv_priv_tt {
	/** @vn: translation table version number */
	atomic_t vn;

	/**
	 * @ogm_append_cnt: counter of number of OGMs containing the local tt
	 *  diff
	 */
	atomic_t ogm_append_cnt;

	/** @local_changes: changes registered in an originator interval */
	atomic_t local_changes;

	/**
	 * @changes_list: tracks tt local changes within an originator interval
	 */
	struct list_head changes_list;

	/** @local_hash: local translation table hash table */
	struct batadv_hashtable *local_hash;

	/** @global_hash: global translation table hash table */
	struct batadv_hashtable *global_hash;

	/** @req_list: list of pending & unanswered tt_requests */
	struct hlist_head req_list;

	/**
	 * @roam_list: list of the last roaming events of each client limiting
	 *  the number of roaming events to avoid route flapping
	 */
	struct list_head roam_list;

	/** @changes_list_lock: lock protecting changes_list */
	spinlock_t changes_list_lock;

	/** @req_list_lock: lock protecting req_list */
	spinlock_t req_list_lock;

	/** @roam_list_lock: lock protecting roam_list */
	spinlock_t roam_list_lock;

	/** @last_changeset: last tt changeset this host has generated */
	unsigned char *last_changeset;

	/**
	 * @last_changeset_len: length of last tt changeset this host has
	 *  generated
	 */
	s16 last_changeset_len;

	/**
	 * @last_changeset_lock: lock protecting last_changeset &
	 *  last_changeset_len
	 */
	spinlock_t last_changeset_lock;

	/**
	 * @commit_lock: prevents from executing a local TT commit while reading
	 *  the local table. The local TT commit is made up of two operations
	 *  (data structure update and metadata -CRC/TTVN- recalculation) and
	 *  they have to be executed atomically in order to avoid another thread
	 *  to read the table/metadata between those.
	 */
	spinlock_t commit_lock;

	/** @work: work queue callback item for translation table purging */
	struct delayed_work work;
};

#ifdef CONFIG_BATMAN_ADV_BLA

/**
 * struct batadv_priv_bla - per mesh interface bridge loop avoidance data
 */
struct batadv_priv_bla {
	/** @num_requests: number of bla requests in flight */
	atomic_t num_requests;

	/**
	 * @claim_hash: hash table containing mesh nodes this host has claimed
	 */
	struct batadv_hashtable *claim_hash;

	/**
	 * @backbone_hash: hash table containing all detected backbone gateways
	 */
	struct batadv_hashtable *backbone_hash;

	/** @loopdetect_addr: MAC address used for own loopdetection frames */
	u8 loopdetect_addr[ETH_ALEN];

	/**
	 * @loopdetect_lasttime: time when the loopdetection frames were sent
	 */
	unsigned long loopdetect_lasttime;

	/**
	 * @loopdetect_next: how many periods to wait for the next loopdetect
	 *  process
	 */
	atomic_t loopdetect_next;

	/**
	 * @bcast_duplist: recently received broadcast packets array (for
	 *  broadcast duplicate suppression)
	 */
	struct batadv_bcast_duplist_entry bcast_duplist[BATADV_DUPLIST_SIZE];

	/**
	 * @bcast_duplist_curr: index of last broadcast packet added to
	 *  bcast_duplist
	 */
	int bcast_duplist_curr;

	/**
	 * @bcast_duplist_lock: lock protecting bcast_duplist &
	 *  bcast_duplist_curr
	 */
	spinlock_t bcast_duplist_lock;

	/** @claim_dest: local claim data (e.g. claim group) */
	struct batadv_bla_claim_dst claim_dest;

	/** @work: work queue callback item for cleanups & bla announcements */
	struct delayed_work work;
};
#endif

#ifdef CONFIG_BATMAN_ADV_DEBUG

/**
 * struct batadv_priv_debug_log - debug logging data
 */
struct batadv_priv_debug_log {
	/** @log_buff: buffer holding the logs (ring buffer) */
	char log_buff[BATADV_LOG_BUF_LEN];

	/** @log_start: index of next character to read */
	unsigned long log_start;

	/** @log_end: index of next character to write */
	unsigned long log_end;

	/** @lock: lock protecting log_buff, log_start & log_end */
	spinlock_t lock;

	/** @queue_wait: log reader's wait queue */
	wait_queue_head_t queue_wait;
};
#endif

/**
 * struct batadv_priv_gw - per mesh interface gateway data
 */
struct batadv_priv_gw {
	/** @gateway_list: list of available gateway nodes */
	struct hlist_head gateway_list;

	/** @list_lock: lock protecting gateway_list, curr_gw, generation */
	spinlock_t list_lock;

	/** @curr_gw: pointer to currently selected gateway node */
	struct batadv_gw_node __rcu *curr_gw;

	/** @generation: current (generation) sequence number */
	unsigned int generation;

	/**
	 * @mode: gateway operation: off, client or server (see batadv_gw_modes)
	 */
	atomic_t mode;

	/** @sel_class: gateway selection class (applies if gw_mode client) */
	atomic_t sel_class;

	/**
	 * @bandwidth_down: advertised uplink download bandwidth (if gw_mode
	 *  server)
	 */
	atomic_t bandwidth_down;

	/**
	 * @bandwidth_up: advertised uplink upload bandwidth (if gw_mode server)
	 */
	atomic_t bandwidth_up;

	/** @reselect: bool indicating a gateway re-selection is in progress */
	atomic_t reselect;
};

/**
 * struct batadv_priv_tvlv - per mesh interface tvlv data
 */
struct batadv_priv_tvlv {
	/**
	 * @container_list: list of registered tvlv containers to be sent with
	 *  each OGM
	 */
	struct hlist_head container_list;

	/** @handler_list: list of the various tvlv content handlers */
	struct hlist_head handler_list;

	/** @container_list_lock: protects tvlv container list access */
	spinlock_t container_list_lock;

	/** @handler_list_lock: protects handler list access */
	spinlock_t handler_list_lock;
};

#ifdef CONFIG_BATMAN_ADV_DAT

/**
 * struct batadv_priv_dat - per mesh interface DAT private data
 */
struct batadv_priv_dat {
	/** @addr: node DAT address */
	batadv_dat_addr_t addr;

	/** @hash: hashtable representing the local ARP cache */
	struct batadv_hashtable *hash;

	/** @work: work queue callback item for cache purging */
	struct delayed_work work;
};
#endif

#ifdef CONFIG_BATMAN_ADV_MCAST
/**
 * struct batadv_mcast_querier_state - IGMP/MLD querier state when bridged
 */
struct batadv_mcast_querier_state {
	/** @exists: whether a querier exists in the mesh */
	unsigned char exists:1;

	/**
	 * @shadowing: if a querier exists, whether it is potentially shadowing
	 *  multicast listeners (i.e. querier is behind our own bridge segment)
	 */
	unsigned char shadowing:1;
};

/**
 * struct batadv_mcast_mla_flags - flags for the querier, bridge and tvlv state
 */
struct batadv_mcast_mla_flags {
	/** @querier_ipv4: the current state of an IGMP querier in the mesh */
	struct batadv_mcast_querier_state querier_ipv4;

	/** @querier_ipv6: the current state of an MLD querier in the mesh */
	struct batadv_mcast_querier_state querier_ipv6;

	/** @enabled: whether the multicast tvlv is currently enabled */
	unsigned char enabled:1;

	/** @bridged: whether the soft interface has a bridge on top */
	unsigned char bridged:1;

	/** @tvlv_flags: the flags we have last sent in our mcast tvlv */
	u8 tvlv_flags;
};

/**
 * struct batadv_priv_mcast - per mesh interface mcast data
 */
struct batadv_priv_mcast {
	/**
	 * @mla_list: list of multicast addresses we are currently announcing
	 *  via TT
	 */
	struct hlist_head mla_list; /* see __batadv_mcast_mla_update() */

	/**
	 * @want_all_unsnoopables_list: a list of orig_nodes wanting all
	 *  unsnoopable multicast traffic
	 */
	struct hlist_head want_all_unsnoopables_list;

	/**
	 * @want_all_ipv4_list: a list of orig_nodes wanting all IPv4 multicast
	 *  traffic
	 */
	struct hlist_head want_all_ipv4_list;

	/**
	 * @want_all_ipv6_list: a list of orig_nodes wanting all IPv6 multicast
	 *  traffic
	 */
	struct hlist_head want_all_ipv6_list;

	/**
	 * @want_all_rtr4_list: a list of orig_nodes wanting all routable IPv4
	 *  multicast traffic
	 */
	struct hlist_head want_all_rtr4_list;

	/**
	 * @want_all_rtr6_list: a list of orig_nodes wanting all routable IPv6
	 *  multicast traffic
	 */
	struct hlist_head want_all_rtr6_list;

	/**
	 * @mla_flags: flags for the querier, bridge and tvlv state
	 */
	struct batadv_mcast_mla_flags mla_flags;

	/**
	 * @mla_lock: a lock protecting mla_list and mla_flags
	 */
	spinlock_t mla_lock;

	/**
	 * @num_want_all_unsnoopables: number of nodes wanting unsnoopable IP
	 *  traffic
	 */
	atomic_t num_want_all_unsnoopables;

	/** @num_want_all_ipv4: counter for items in want_all_ipv4_list */
	atomic_t num_want_all_ipv4;

	/** @num_want_all_ipv6: counter for items in want_all_ipv6_list */
	atomic_t num_want_all_ipv6;

	/** @num_want_all_rtr4: counter for items in want_all_rtr4_list */
	atomic_t num_want_all_rtr4;

	/** @num_want_all_rtr6: counter for items in want_all_rtr6_list */
	atomic_t num_want_all_rtr6;

	/**
	 * @want_lists_lock: lock for protecting modifications to mcasts
	 *  want_all_{unsnoopables,ipv4,ipv6}_list (traversals are rcu-locked)
	 */
	spinlock_t want_lists_lock;

	/** @work: work queue callback item for multicast TT and TVLV updates */
	struct delayed_work work;
};
#endif

/**
 * struct batadv_priv_nc - per mesh interface network coding private data
 */
struct batadv_priv_nc {
	/** @work: work queue callback item for cleanup */
	struct delayed_work work;

	/**
	 * @min_tq: only consider neighbors for encoding if neigh_tq > min_tq
	 */
	u8 min_tq;

	/**
	 * @max_fwd_delay: maximum packet forward delay to allow coding of
	 *  packets
	 */
	u32 max_fwd_delay;

	/**
	 * @max_buffer_time: buffer time for sniffed packets used to decoding
	 */
	u32 max_buffer_time;

	/**
	 * @timestamp_fwd_flush: timestamp of last forward packet queue flush
	 */
	unsigned long timestamp_fwd_flush;

	/**
	 * @timestamp_sniffed_purge: timestamp of last sniffed packet queue
	 *  purge
	 */
	unsigned long timestamp_sniffed_purge;

	/**
	 * @coding_hash: Hash table used to buffer skbs while waiting for
	 *  another incoming skb to code it with. Skbs are added to the buffer
	 *  just before being forwarded in routing.c
	 */
	struct batadv_hashtable *coding_hash;

	/**
	 * @decoding_hash: Hash table used to buffer skbs that might be needed
	 *  to decode a received coded skb. The buffer is used for 1) skbs
	 *  arriving on the soft-interface; 2) skbs overheard on the
	 *  hard-interface; and 3) skbs forwarded by batman-adv.
	 */
	struct batadv_hashtable *decoding_hash;
};

/**
 * struct batadv_tp_unacked - unacked packet meta-information
 *
 * This struct is supposed to represent a buffer unacked packet. However, since
 * the purpose of the TP meter is to count the traffic only, there is no need to
 * store the entire sk_buff, the starting offset and the length are enough
 */
struct batadv_tp_unacked {
	/** @seqno: seqno of the unacked packet */
	u32 seqno;

	/** @len: length of the packet */
	u16 len;

	/** @list: list node for &batadv_tp_vars.unacked_list */
	struct list_head list;
};

/**
 * enum batadv_tp_meter_role - Modus in tp meter session
 */
enum batadv_tp_meter_role {
	/** @BATADV_TP_RECEIVER: Initialized as receiver */
	BATADV_TP_RECEIVER,

	/** @BATADV_TP_SENDER: Initialized as sender */
	BATADV_TP_SENDER
};

/**
 * struct batadv_tp_vars - tp meter private variables per session
 */
struct batadv_tp_vars {
	/** @list: list node for &bat_priv.tp_list */
	struct hlist_node list;

	/** @timer: timer for ack (receiver) and retry (sender) */
	struct timer_list timer;

	/** @bat_priv: pointer to the mesh object */
	struct batadv_priv *bat_priv;

	/** @start_time: start time in jiffies */
	unsigned long start_time;

	/** @other_end: mac address of remote */
	u8 other_end[ETH_ALEN];

	/** @role: receiver/sender modi */
	enum batadv_tp_meter_role role;

	/** @sending: sending binary semaphore: 1 if sending, 0 is not */
	atomic_t sending;

	/** @reason: reason for a stopped session */
	enum batadv_tp_meter_reason reason;

	/** @finish_work: work item for the finishing procedure */
	struct delayed_work finish_work;

	/** @test_length: test length in milliseconds */
	u32 test_length;

	/** @session: TP session identifier */
	u8 session[2];

	/** @icmp_uid: local ICMP "socket" index */
	u8 icmp_uid;

	/* sender variables */

	/** @dec_cwnd: decimal part of the cwnd used during linear growth */
	u16 dec_cwnd;

	/** @cwnd: current size of the congestion window */
	u32 cwnd;

	/** @cwnd_lock: lock do protect @cwnd & @dec_cwnd */
	spinlock_t cwnd_lock;

	/**
	 * @ss_threshold: Slow Start threshold. Once cwnd exceeds this value the
	 *  connection switches to the Congestion Avoidance state
	 */
	u32 ss_threshold;

	/** @last_acked: last acked byte */
	atomic_t last_acked;

	/** @last_sent: last sent byte, not yet acked */
	u32 last_sent;

	/** @tot_sent: amount of data sent/ACKed so far */
	atomic64_t tot_sent;

	/** @dup_acks: duplicate ACKs counter */
	atomic_t dup_acks;

	/** @fast_recovery: true if in Fast Recovery mode */
	unsigned char fast_recovery:1;

	/** @recover: last sent seqno when entering Fast Recovery */
	u32 recover;

	/** @rto: sender timeout */
	u32 rto;

	/** @srtt: smoothed RTT scaled by 2^3 */
	u32 srtt;

	/** @rttvar: RTT variation scaled by 2^2 */
	u32 rttvar;

	/**
	 * @more_bytes: waiting queue anchor when waiting for more ack/retry
	 *  timeout
	 */
	wait_queue_head_t more_bytes;

	/** @prerandom_offset: offset inside the prerandom buffer */
	u32 prerandom_offset;

	/** @prerandom_lock: spinlock protecting access to prerandom_offset */
	spinlock_t prerandom_lock;

	/* receiver variables */

	/** @last_recv: last in-order received packet */
	u32 last_recv;

	/** @unacked_list: list of unacked packets (meta-info only) */
	struct list_head unacked_list;

	/** @unacked_lock: protect unacked_list */
	spinlock_t unacked_lock;

	/** @last_recv_time: time (jiffies) a msg was received */
	unsigned long last_recv_time;

	/** @refcount: number of context where the object is used */
	struct kref refcount;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;
};

/**
 * struct batadv_softif_vlan - per VLAN attributes set
 */
struct batadv_softif_vlan {
	/** @bat_priv: pointer to the mesh object */
	struct batadv_priv *bat_priv;

	/** @vid: VLAN identifier */
	unsigned short vid;

	/** @ap_isolation: AP isolation state */
	atomic_t ap_isolation;		/* boolean */

	/** @tt: TT private attributes (VLAN specific) */
	struct batadv_vlan_tt tt;

	/** @list: list node for &bat_priv.softif_vlan_list */
	struct hlist_node list;

	/**
	 * @refcount: number of context where this object is currently in use
	 */
	struct kref refcount;

	/** @rcu: struct used for freeing in a RCU-safe manner */
	struct rcu_head rcu;
};

/**
 * struct batadv_priv_bat_v - B.A.T.M.A.N. V per soft-interface private data
 */
struct batadv_priv_bat_v {
	/** @ogm_buff: buffer holding the OGM packet */
	unsigned char *ogm_buff;

	/** @ogm_buff_len: length of the OGM packet buffer */
	int ogm_buff_len;

	/** @ogm_seqno: OGM sequence number - used to identify each OGM */
	atomic_t ogm_seqno;

	/** @ogm_buff_mutex: lock protecting ogm_buff and ogm_buff_len */
	struct mutex ogm_buff_mutex;

	/** @ogm_wq: workqueue used to schedule OGM transmissions */
	struct delayed_work ogm_wq;
};

/**
 * struct batadv_priv - per mesh interface data
 */
struct batadv_priv {
	/**
	 * @mesh_state: current status of the mesh
	 *  (inactive/active/deactivating)
	 */
	atomic_t mesh_state;

	/** @soft_iface: net device which holds this struct as private data */
	struct net_device *soft_iface;

	/**
	 * @bat_counters: mesh internal traffic statistic counters (see
	 *  batadv_counters)
	 */
	u64 __percpu *bat_counters; /* Per cpu counters */

	/**
	 * @aggregated_ogms: bool indicating whether OGM aggregation is enabled
	 */
	atomic_t aggregated_ogms;

	/** @bonding: bool indicating whether traffic bonding is enabled */
	atomic_t bonding;

	/**
	 * @fragmentation: bool indicating whether traffic fragmentation is
	 *  enabled
	 */
	atomic_t fragmentation;

	/**
	 * @packet_size_max: max packet size that can be transmitted via
	 *  multiple fragmented skbs or a single frame if fragmentation is
	 *  disabled
	 */
	atomic_t packet_size_max;

	/**
	 * @frag_seqno: incremental counter to identify chains of egress
	 *  fragments
	 */
	atomic_t frag_seqno;

#ifdef CONFIG_BATMAN_ADV_BLA
	/**
	 * @bridge_loop_avoidance: bool indicating whether bridge loop
	 *  avoidance is enabled
	 */
	atomic_t bridge_loop_avoidance;
#endif

#ifdef CONFIG_BATMAN_ADV_DAT
	/**
	 * @distributed_arp_table: bool indicating whether distributed ARP table
	 *  is enabled
	 */
	atomic_t distributed_arp_table;
#endif

#ifdef CONFIG_BATMAN_ADV_MCAST
	/**
	 * @multicast_mode: Enable or disable multicast optimizations on this
	 *  node's sender/originating side
	 */
	atomic_t multicast_mode;

	/**
	 * @multicast_fanout: Maximum number of packet copies to generate for a
	 *  multicast-to-unicast conversion
	 */
	atomic_t multicast_fanout;
#endif

	/** @orig_interval: OGM broadcast interval in milliseconds */
	atomic_t orig_interval;

	/**
	 * @hop_penalty: penalty which will be applied to an OGM's tq-field on
	 *  every hop
	 */
	atomic_t hop_penalty;

#ifdef CONFIG_BATMAN_ADV_DEBUG
	/** @log_level: configured log level (see batadv_dbg_level) */
	atomic_t log_level;
#endif

	/**
	 * @isolation_mark: the skb->mark value used to match packets for AP
	 *  isolation
	 */
	u32 isolation_mark;

	/**
	 * @isolation_mark_mask: bitmask identifying the bits in skb->mark to be
	 *  used for the isolation mark
	 */
	u32 isolation_mark_mask;

	/** @bcast_seqno: last sent broadcast packet sequence number */
	atomic_t bcast_seqno;

	/**
	 * @bcast_queue_left: number of remaining buffered broadcast packet
	 *  slots
	 */
	atomic_t bcast_queue_left;

	/** @batman_queue_left: number of remaining OGM packet slots */
	atomic_t batman_queue_left;

	/** @forw_bat_list: list of aggregated OGMs that will be forwarded */
	struct hlist_head forw_bat_list;

	/**
	 * @forw_bcast_list: list of broadcast packets that will be
	 *  rebroadcasted
	 */
	struct hlist_head forw_bcast_list;

	/** @tp_list: list of tp sessions */
	struct hlist_head tp_list;

	/** @tp_num: number of currently active tp sessions */
	struct batadv_hashtable *orig_hash;

	/** @orig_hash: hash table containing mesh participants (orig nodes) */
	spinlock_t forw_bat_list_lock;

	/** @forw_bat_list_lock: lock protecting forw_bat_list */
	spinlock_t forw_bcast_list_lock;

	/** @forw_bcast_list_lock: lock protecting forw_bcast_list */
	spinlock_t tp_list_lock;

	/** @tp_list_lock: spinlock protecting @tp_list */
	atomic_t tp_num;

	/** @orig_work: work queue callback item for orig node purging */
	struct delayed_work orig_work;

	/**
	 * @primary_if: one of the hard-interfaces assigned to this mesh
	 *  interface becomes the primary interface
	 */
	struct batadv_hard_iface __rcu *primary_if;  /* rcu protected pointer */

	/** @algo_ops: routing algorithm used by this mesh interface */
	struct batadv_algo_ops *algo_ops;

	/**
	 * @softif_vlan_list: a list of softif_vlan structs, one per VLAN
	 *  created on top of the mesh interface represented by this object
	 */
	struct hlist_head softif_vlan_list;

	/** @softif_vlan_list_lock: lock protecting softif_vlan_list */
	spinlock_t softif_vlan_list_lock;

#ifdef CONFIG_BATMAN_ADV_BLA
	/** @bla: bridge loop avoidance data */
	struct batadv_priv_bla bla;
#endif

#ifdef CONFIG_BATMAN_ADV_DEBUG
	/** @debug_log: holding debug logging relevant data */
	struct batadv_priv_debug_log *debug_log;
#endif

	/** @gw: gateway data */
	struct batadv_priv_gw gw;

	/** @tt: translation table data */
	struct batadv_priv_tt tt;

	/** @tvlv: type-version-length-value data */
	struct batadv_priv_tvlv tvlv;

#ifdef CONFIG_BATMAN_ADV_DAT
	/** @dat: distributed arp table data */
	struct batadv_priv_dat dat;
#endif

#ifdef CONFIG_BATMAN_ADV_MCAST
	/** @mcast: multicast data */
	struct batadv_priv_mcast mcast;
#endif

#ifdef CONFIG_BATMAN_ADV_NC
	/**
	 * @network_coding: bool indicating whether network coding is enabled
	 */
	atomic_t network_coding;

	/** @nc: network coding data */
	struct batadv_priv_nc nc;
#endif /* CONFIG_BATMAN_ADV_NC */

#ifdef CONFIG_BATMAN_ADV_BATMAN_V
	/** @bat_v: B.A.T.M.A.N. V per soft-interface private data */
	struct batadv_priv_bat_v bat_v;
#endif
};

/**
 * struct batadv_socket_client - layer2 icmp socket client data
 */
struct batadv_socket_client {
	/**
	 * @queue_list: packet queue for packets destined for this socket client
	 */
	struct list_head queue_list;

	/** @queue_len: number of packets in the packet queue (queue_list) */
	unsigned int queue_len;

	/** @index: socket client's index in the batadv_socket_client_hash */
	unsigned char index;

	/** @lock: lock protecting queue_list, queue_len & index */
	spinlock_t lock;

	/** @queue_wait: socket client's wait queue */
	wait_queue_head_t queue_wait;

	/** @bat_priv: pointer to soft_iface this client belongs to */
	struct batadv_priv *bat_priv;
};

/**
 * struct batadv_socket_packet - layer2 icmp packet for socket client
 */
struct batadv_socket_packet {
	/** @list: list node for &batadv_socket_client.queue_list */
	struct list_head list;

	/** @icmp_len: size of the layer2 icmp packet */
	size_t icmp_len;

	/** @icmp_packet: layer2 icmp packet */
	u8 icmp_packet[BATADV_ICMP_MAX_PACKET_SIZE];
};

#ifdef CONFIG_BATMAN_ADV_BLA

/**
 * struct batadv_bla_backbone_gw - batman-adv gateway bridged into the LAN
 */
struct batadv_bla_backbone_gw {
	/**
	 * @orig: originator address of backbone node (mac address of primary
	 *  iface)
	 */
	u8 orig[ETH_ALEN];

	/** @vid: vlan id this gateway was detected on */
	unsigned short vid;

	/** @hash_entry: hlist node for &batadv_priv_bla.backbone_hash */
	struct hlist_node hash_entry;

	/** @bat_priv: pointer to soft_iface this backbone gateway belongs to */
	struct batadv_priv *bat_priv;

	/** @lasttime: last time we heard of this backbone gw */
	unsigned long lasttime;

	/**
	 * @wait_periods: grace time for bridge forward delays and bla group
	 *  forming at bootup phase - no bcast traffic is formwared until it has
	 *  elapsed
	 */
	atomic_t wait_periods;

	/**
	 * @request_sent: if this bool is set to true we are out of sync with
	 *  this backbone gateway - no bcast traffic is formwared until the
	 *  situation was resolved
	 */
	atomic_t request_sent;

	/** @crc: crc16 checksum over all claims */
	u16 crc;

	/** @crc_lock: lock protecting crc */
	spinlock_t crc_lock;

	/** @report_work: work struct for reporting detected loops */
	struct work_struct report_work;

	/** @refcount: number of contexts the object is used */
	struct kref refcount;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;
};

/**
 * struct batadv_bla_claim - claimed non-mesh client structure
 */
struct batadv_bla_claim {
	/** @addr: mac address of claimed non-mesh client */
	u8 addr[ETH_ALEN];

	/** @vid: vlan id this client was detected on */
	unsigned short vid;

	/** @backbone_gw: pointer to backbone gw claiming this client */
	struct batadv_bla_backbone_gw *backbone_gw;

	/** @backbone_lock: lock protecting backbone_gw pointer */
	spinlock_t backbone_lock;

	/** @lasttime: last time we heard of claim (locals only) */
	unsigned long lasttime;

	/** @hash_entry: hlist node for &batadv_priv_bla.claim_hash */
	struct hlist_node hash_entry;

	/** @refcount: number of contexts the object is used */
	struct rcu_head rcu;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct kref refcount;
};
#endif

/**
 * struct batadv_tt_common_entry - tt local & tt global common data
 */
struct batadv_tt_common_entry {
	/** @addr: mac address of non-mesh client */
	u8 addr[ETH_ALEN];

	/** @vid: VLAN identifier */
	unsigned short vid;

	/**
	 * @hash_entry: hlist node for &batadv_priv_tt.local_hash or for
	 *  &batadv_priv_tt.global_hash
	 */
	struct hlist_node hash_entry;

	/** @flags: various state handling flags (see batadv_tt_client_flags) */
	u16 flags;

	/** @added_at: timestamp used for purging stale tt common entries */
	unsigned long added_at;

	/** @refcount: number of contexts the object is used */
	struct kref refcount;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;
};

/**
 * struct batadv_tt_local_entry - translation table local entry data
 */
struct batadv_tt_local_entry {
	/** @common: general translation table data */
	struct batadv_tt_common_entry common;

	/** @last_seen: timestamp used for purging stale tt local entries */
	unsigned long last_seen;

	/** @vlan: soft-interface vlan of the entry */
	struct batadv_softif_vlan *vlan;
};

/**
 * struct batadv_tt_global_entry - translation table global entry data
 */
struct batadv_tt_global_entry {
	/** @common: general translation table data */
	struct batadv_tt_common_entry common;

	/** @orig_list: list of orig nodes announcing this non-mesh client */
	struct hlist_head orig_list;

	/** @orig_list_count: number of items in the orig_list */
	atomic_t orig_list_count;

	/** @list_lock: lock protecting orig_list */
	spinlock_t list_lock;

	/** @roam_at: time at which TT_GLOBAL_ROAM was set */
	unsigned long roam_at;
};

/**
 * struct batadv_tt_orig_list_entry - orig node announcing a non-mesh client
 */
struct batadv_tt_orig_list_entry {
	/** @orig_node: pointer to orig node announcing this non-mesh client */
	struct batadv_orig_node *orig_node;

	/**
	 * @ttvn: translation table version number which added the non-mesh
	 *  client
	 */
	u8 ttvn;

	/** @flags: per orig entry TT sync flags */
	u8 flags;

	/** @list: list node for &batadv_tt_global_entry.orig_list */
	struct hlist_node list;

	/** @refcount: number of contexts the object is used */
	struct kref refcount;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;
};

/**
 * struct batadv_tt_change_node - structure for tt changes occurred
 */
struct batadv_tt_change_node {
	/** @list: list node for &batadv_priv_tt.changes_list */
	struct list_head list;

	/** @change: holds the actual translation table diff data */
	struct batadv_tvlv_tt_change change;
};

/**
 * struct batadv_tt_req_node - data to keep track of the tt requests in flight
 */
struct batadv_tt_req_node {
	/**
	 * @addr: mac address of the originator this request was sent to
	 */
	u8 addr[ETH_ALEN];

	/** @issued_at: timestamp used for purging stale tt requests */
	unsigned long issued_at;

	/** @refcount: number of contexts the object is used by */
	struct kref refcount;

	/** @list: list node for &batadv_priv_tt.req_list */
	struct hlist_node list;
};

/**
 * struct batadv_tt_roam_node - roaming client data
 */
struct batadv_tt_roam_node {
	/** @addr: mac address of the client in the roaming phase */
	u8 addr[ETH_ALEN];

	/**
	 * @counter: number of allowed roaming events per client within a single
	 * OGM interval (changes are committed with each OGM)
	 */
	atomic_t counter;

	/**
	 * @first_time: timestamp used for purging stale roaming node entries
	 */
	unsigned long first_time;

	/** @list: list node for &batadv_priv_tt.roam_list */
	struct list_head list;
};

/**
 * struct batadv_nc_node - network coding node
 */
struct batadv_nc_node {
	/** @list: next and prev pointer for the list handling */
	struct list_head list;

	/** @addr: the node's mac address */
	u8 addr[ETH_ALEN];

	/** @refcount: number of contexts the object is used by */
	struct kref refcount;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;

	/** @orig_node: pointer to corresponding orig node struct */
	struct batadv_orig_node *orig_node;

	/** @last_seen: timestamp of last ogm received from this node */
	unsigned long last_seen;
};

/**
 * struct batadv_nc_path - network coding path
 */
struct batadv_nc_path {
	/** @hash_entry: next and prev pointer for the list handling */
	struct hlist_node hash_entry;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;

	/** @refcount: number of contexts the object is used by */
	struct kref refcount;

	/** @packet_list: list of buffered packets for this path */
	struct list_head packet_list;

	/** @packet_list_lock: access lock for packet list */
	spinlock_t packet_list_lock;

	/** @next_hop: next hop (destination) of path */
	u8 next_hop[ETH_ALEN];

	/** @prev_hop: previous hop (source) of path */
	u8 prev_hop[ETH_ALEN];

	/** @last_valid: timestamp for last validation of path */
	unsigned long last_valid;
};

/**
 * struct batadv_nc_packet - network coding packet used when coding and
 *  decoding packets
 */
struct batadv_nc_packet {
	/** @list: next and prev pointer for the list handling */
	struct list_head list;

	/** @packet_id: crc32 checksum of skb data */
	__be32 packet_id;

	/**
	 * @timestamp: field containing the info when the packet was added to
	 *  path
	 */
	unsigned long timestamp;

	/** @neigh_node: pointer to original next hop neighbor of skb */
	struct batadv_neigh_node *neigh_node;

	/** @skb: skb which can be encoded or used for decoding */
	struct sk_buff *skb;

	/** @nc_path: pointer to path this nc packet is attached to */
	struct batadv_nc_path *nc_path;
};

/**
 * struct batadv_skb_cb - control buffer structure used to store private data
 *  relevant to batman-adv in the skb->cb buffer in skbs.
 */
struct batadv_skb_cb {
	/**
	 * @decoded: Marks a skb as decoded, which is checked when searching for
	 *  coding opportunities in network-coding.c
	 */
	unsigned char decoded:1;

	/** @num_bcasts: Counter for broadcast packet retransmissions */
	unsigned char num_bcasts;
};

/**
 * struct batadv_forw_packet - structure for bcast packets to be sent/forwarded
 */
struct batadv_forw_packet {
	/**
	 * @list: list node for &batadv_priv.forw.bcast_list and
	 *  &batadv_priv.forw.bat_list
	 */
	struct hlist_node list;

	/** @cleanup_list: list node for purging functions */
	struct hlist_node cleanup_list;

	/** @send_time: execution time for delayed_work (packet sending) */
	unsigned long send_time;

	/**
	 * @own: bool for locally generated packets (local OGMs are re-scheduled
	 * after sending)
	 */
	u8 own;

	/** @skb: bcast packet's skb buffer */
	struct sk_buff *skb;

	/** @packet_len: size of aggregated OGM packet inside the skb buffer */
	u16 packet_len;

	/** @direct_link_flags: direct link flags for aggregated OGM packets */
	u32 direct_link_flags;

	/** @num_packets: counter for aggregated OGMv1 packets */
	u8 num_packets;

	/** @delayed_work: work queue callback item for packet sending */
	struct delayed_work delayed_work;

	/**
	 * @if_incoming: pointer to incoming hard-iface or primary iface if
	 *  locally generated packet
	 */
	struct batadv_hard_iface *if_incoming;

	/**
	 * @if_outgoing: packet where the packet should be sent to, or NULL if
	 *  unspecified
	 */
	struct batadv_hard_iface *if_outgoing;

	/** @queue_left: The queue (counter) this packet was applied to */
	atomic_t *queue_left;
};

/**
 * struct batadv_algo_iface_ops - mesh algorithm callbacks (interface specific)
 */
struct batadv_algo_iface_ops {
	/**
	 * @activate: start routing mechanisms when hard-interface is brought up
	 *  (optional)
	 */
	void (*activate)(struct batadv_hard_iface *hard_iface);

	/** @enable: init routing info when hard-interface is enabled */
	int (*enable)(struct batadv_hard_iface *hard_iface);

	/** @enabled: notification when hard-interface was enabled (optional) */
	void (*enabled)(struct batadv_hard_iface *hard_iface);

	/** @disable: de-init routing info when hard-interface is disabled */
	void (*disable)(struct batadv_hard_iface *hard_iface);

	/**
	 * @update_mac: (re-)init mac addresses of the protocol information
	 *  belonging to this hard-interface
	 */
	void (*update_mac)(struct batadv_hard_iface *hard_iface);

	/** @primary_set: called when primary interface is selected / changed */
	void (*primary_set)(struct batadv_hard_iface *hard_iface);
};

/**
 * struct batadv_algo_neigh_ops - mesh algorithm callbacks (neighbour specific)
 */
struct batadv_algo_neigh_ops {
	/** @hardif_init: called on creation of single hop entry (optional) */
	void (*hardif_init)(struct batadv_hardif_neigh_node *neigh);

	/**
	 * @cmp: compare the metrics of two neighbors for their respective
	 *  outgoing interfaces
	 */
	int (*cmp)(struct batadv_neigh_node *neigh1,
		   struct batadv_hard_iface *if_outgoing1,
		   struct batadv_neigh_node *neigh2,
		   struct batadv_hard_iface *if_outgoing2);

	/**
	 * @is_similar_or_better: check if neigh1 is equally similar or better
	 *  than neigh2 for their respective outgoing interface from the metric
	 *  prospective
	 */
	bool (*is_similar_or_better)(struct batadv_neigh_node *neigh1,
				     struct batadv_hard_iface *if_outgoing1,
				     struct batadv_neigh_node *neigh2,
				     struct batadv_hard_iface *if_outgoing2);

	/** @dump: dump neighbors to a netlink socket (optional) */
	void (*dump)(struct sk_buff *msg, struct netlink_callback *cb,
		     struct batadv_priv *priv,
		     struct batadv_hard_iface *hard_iface);
};

/**
 * struct batadv_algo_orig_ops - mesh algorithm callbacks (originator specific)
 */
struct batadv_algo_orig_ops {
	/** @dump: dump originators to a netlink socket (optional) */
	void (*dump)(struct sk_buff *msg, struct netlink_callback *cb,
		     struct batadv_priv *priv,
		     struct batadv_hard_iface *hard_iface);
};

/**
 * struct batadv_algo_gw_ops - mesh algorithm callbacks (GW specific)
 */
struct batadv_algo_gw_ops {
	/** @init_sel_class: initialize GW selection class (optional) */
	void (*init_sel_class)(struct batadv_priv *bat_priv);

	/**
	 * @store_sel_class: parse and stores a new GW selection class
	 *  (optional)
	 */
	ssize_t (*store_sel_class)(struct batadv_priv *bat_priv, char *buff,
				   size_t count);
	/**
	 * @get_best_gw_node: select the best GW from the list of available
	 *  nodes (optional)
	 */
	struct batadv_gw_node *(*get_best_gw_node)
		(struct batadv_priv *bat_priv);

	/**
	 * @is_eligible: check if a newly discovered GW is a potential candidate
	 *  for the election as best GW (optional)
	 */
	bool (*is_eligible)(struct batadv_priv *bat_priv,
			    struct batadv_orig_node *curr_gw_orig,
			    struct batadv_orig_node *orig_node);

	/** @dump: dump gateways to a netlink socket (optional) */
	void (*dump)(struct sk_buff *msg, struct netlink_callback *cb,
		     struct batadv_priv *priv);
};

/**
 * struct batadv_algo_ops - mesh algorithm callbacks
 */
struct batadv_algo_ops {
	/** @list: list node for the batadv_algo_list */
	struct hlist_node list;

	/** @name: name of the algorithm */
	char *name;

	/** @iface: callbacks related to interface handling */
	struct batadv_algo_iface_ops iface;

	/** @neigh: callbacks related to neighbors handling */
	struct batadv_algo_neigh_ops neigh;

	/** @orig: callbacks related to originators handling */
	struct batadv_algo_orig_ops orig;

	/** @gw: callbacks related to GW mode */
	struct batadv_algo_gw_ops gw;
};

/**
 * struct batadv_dat_entry - it is a single entry of batman-adv ARP backend. It
 * is used to stored ARP entries needed for the global DAT cache
 */
struct batadv_dat_entry {
	/** @ip: the IPv4 corresponding to this DAT/ARP entry */
	__be32 ip;

	/** @mac_addr: the MAC address associated to the stored IPv4 */
	u8 mac_addr[ETH_ALEN];

	/** @vid: the vlan ID associated to this entry */
	unsigned short vid;

	/**
	 * @last_update: time in jiffies when this entry was refreshed last time
	 */
	unsigned long last_update;

	/** @hash_entry: hlist node for &batadv_priv_dat.hash */
	struct hlist_node hash_entry;

	/** @refcount: number of contexts the object is used */
	struct kref refcount;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;
};

/**
 * struct batadv_hw_addr - a list entry for a MAC address
 */
struct batadv_hw_addr {
	/** @list: list node for the linking of entries */
	struct hlist_node list;

	/** @addr: the MAC address of this list entry */
	unsigned char addr[ETH_ALEN];
};

/**
 * struct batadv_dat_candidate - candidate destination for DAT operations
 */
struct batadv_dat_candidate {
	/**
	 * @type: the type of the selected candidate. It can one of the
	 *  following:
	 *	  - BATADV_DAT_CANDIDATE_NOT_FOUND
	 *	  - BATADV_DAT_CANDIDATE_ORIG
	 */
	int type;

	/**
	 * @orig_node: if type is BATADV_DAT_CANDIDATE_ORIG this field points to
	 * the corresponding originator node structure
	 */
	struct batadv_orig_node *orig_node;
};

/**
 * struct batadv_tvlv_container - container for tvlv appended to OGMs
 */
struct batadv_tvlv_container {
	/** @list: hlist node for &batadv_priv_tvlv.container_list */
	struct hlist_node list;

	/** @tvlv_hdr: tvlv header information needed to construct the tvlv */
	struct batadv_tvlv_hdr tvlv_hdr;

	/** @refcount: number of contexts the object is used */
	struct kref refcount;
};

/**
 * struct batadv_tvlv_handler - handler for specific tvlv type and version
 */
struct batadv_tvlv_handler {
	/** @list: hlist node for &batadv_priv_tvlv.handler_list */
	struct hlist_node list;

	/**
	 * @ogm_handler: handler callback which is given the tvlv payload to
	 *  process on incoming OGM packets
	 */
	void (*ogm_handler)(struct batadv_priv *bat_priv,
			    struct batadv_orig_node *orig,
			    u8 flags, void *tvlv_value, u16 tvlv_value_len);

	/**
	 * @unicast_handler: handler callback which is given the tvlv payload to
	 *  process on incoming unicast tvlv packets
	 */
	int (*unicast_handler)(struct batadv_priv *bat_priv,
			       u8 *src, u8 *dst,
			       void *tvlv_value, u16 tvlv_value_len);

	/** @type: tvlv type this handler feels responsible for */
	u8 type;

	/** @version: tvlv version this handler feels responsible for */
	u8 version;

	/** @flags: tvlv handler flags */
	u8 flags;

	/** @refcount: number of contexts the object is used */
	struct kref refcount;

	/** @rcu: struct used for freeing in an RCU-safe manner */
	struct rcu_head rcu;
};

/**
 * enum batadv_tvlv_handler_flags - tvlv handler flags definitions
 */
enum batadv_tvlv_handler_flags {
	/**
	 * @BATADV_TVLV_HANDLER_OGM_CIFNOTFND: tvlv ogm processing function
	 *  will call this handler even if its type was not found (with no data)
	 */
	BATADV_TVLV_HANDLER_OGM_CIFNOTFND = BIT(1),

	/**
	 * @BATADV_TVLV_HANDLER_OGM_CALLED: interval tvlv handling flag - the
	 *  API marks a handler as being called, so it won't be called if the
	 *  BATADV_TVLV_HANDLER_OGM_CIFNOTFND flag was set
	 */
	BATADV_TVLV_HANDLER_OGM_CALLED = BIT(2),
};

#endif /* _NET_BATMAN_ADV_TYPES_H_ */
