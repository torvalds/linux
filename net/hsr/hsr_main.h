/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 *
 * include file for HSR and PRP.
 */

#ifndef __HSR_PRIVATE_H
#define __HSR_PRIVATE_H

#include <linux/netdevice.h>
#include <linux/list.h>
#include <linux/if_vlan.h>
#include <linux/if_hsr.h>

/* Time constants as specified in the HSR specification (IEC-62439-3 2010)
 * Table 8.
 * All values in milliseconds.
 */
#define HSR_LIFE_CHECK_INTERVAL		 2000 /* ms */
#define HSR_NODE_FORGET_TIME		60000 /* ms */
#define HSR_ANNOUNCE_INTERVAL		  100 /* ms */
#define HSR_ENTRY_FORGET_TIME		  400 /* ms */

/* By how much may slave1 and slave2 timestamps of latest received frame from
 * each node differ before we notify of communication problem?
 */
#define MAX_SLAVE_DIFF			 3000 /* ms */
#define HSR_SEQNR_START			(USHRT_MAX - 1024)
#define HSR_SUP_SEQNR_START		(HSR_SEQNR_START / 2)

/* How often shall we check for broken ring and remove node entries older than
 * HSR_NODE_FORGET_TIME?
 */
#define PRUNE_PERIOD			 3000 /* ms */

#define HSR_TLV_ANNOUNCE		   22
#define HSR_TLV_LIFE_CHECK		   23
/* PRP V1 life check for Duplicate discard */
#define PRP_TLV_LIFE_CHECK_DD		   20
/* PRP V1 life check for Duplicate Accept */
#define PRP_TLV_LIFE_CHECK_DA		   21

/* HSR Tag.
 * As defined in IEC-62439-3:2010, the HSR tag is really { ethertype = 0x88FB,
 * path, LSDU_size, sequence Nr }. But we let eth_header() create { h_dest,
 * h_source, h_proto = 0x88FB }, and add { path, LSDU_size, sequence Nr,
 * encapsulated protocol } instead.
 *
 * Field names as defined in the IEC:2010 standard for HSR.
 */
struct hsr_tag {
	__be16		path_and_LSDU_size;
	__be16		sequence_nr;
	__be16		encap_proto;
} __packed;

#define HSR_HLEN	6

#define HSR_V1_SUP_LSDUSIZE		52

/* The helper functions below assumes that 'path' occupies the 4 most
 * significant bits of the 16-bit field shared by 'path' and 'LSDU_size' (or
 * equivalently, the 4 most significant bits of HSR tag byte 14).
 *
 * This is unclear in the IEC specification; its definition of MAC addresses
 * indicates the spec is written with the least significant bit first (to the
 * left). This, however, would mean that the LSDU field would be split in two
 * with the path field in-between, which seems strange. I'm guessing the MAC
 * address definition is in error.
 */

static inline void set_hsr_tag_path(struct hsr_tag *ht, u16 path)
{
	ht->path_and_LSDU_size =
		htons((ntohs(ht->path_and_LSDU_size) & 0x0FFF) | (path << 12));
}

static inline void set_hsr_tag_LSDU_size(struct hsr_tag *ht, u16 LSDU_size)
{
	ht->path_and_LSDU_size = htons((ntohs(ht->path_and_LSDU_size) &
				       0xF000) | (LSDU_size & 0x0FFF));
}

struct hsr_ethhdr {
	struct ethhdr	ethhdr;
	struct hsr_tag	hsr_tag;
} __packed;

struct hsr_vlan_ethhdr {
	struct vlan_ethhdr vlanhdr;
	struct hsr_tag	hsr_tag;
} __packed;

/* HSR/PRP Supervision Frame data types.
 * Field names as defined in the IEC:2010 standard for HSR.
 */
struct hsr_sup_tag {
	__be16		path_and_HSR_ver;
	__be16		sequence_nr;
	__u8		HSR_TLV_type;
	__u8		HSR_TLV_length;
} __packed;

struct hsr_sup_payload {
	unsigned char	macaddress_A[ETH_ALEN];
} __packed;

static inline void set_hsr_stag_path(struct hsr_sup_tag *hst, u16 path)
{
	set_hsr_tag_path((struct hsr_tag *)hst, path);
}

static inline void set_hsr_stag_HSR_ver(struct hsr_sup_tag *hst, u16 HSR_ver)
{
	set_hsr_tag_LSDU_size((struct hsr_tag *)hst, HSR_ver);
}

struct hsrv0_ethhdr_sp {
	struct ethhdr		ethhdr;
	struct hsr_sup_tag	hsr_sup;
} __packed;

struct hsrv1_ethhdr_sp {
	struct ethhdr		ethhdr;
	struct hsr_tag		hsr;
	struct hsr_sup_tag	hsr_sup;
} __packed;

enum hsr_port_type {
	HSR_PT_NONE = 0,	/* Must be 0, used by framereg */
	HSR_PT_SLAVE_A,
	HSR_PT_SLAVE_B,
	HSR_PT_INTERLINK,
	HSR_PT_MASTER,
	HSR_PT_PORTS,	/* This must be the last item in the enum */
};

/* PRP Redunancy Control Trailor (RCT).
 * As defined in IEC-62439-4:2012, the PRP RCT is really { sequence Nr,
 * Lan indentifier (LanId), LSDU_size and PRP_suffix = 0x88FB }.
 *
 * Field names as defined in the IEC:2012 standard for PRP.
 */
struct prp_rct {
	__be16          sequence_nr;
	__be16          lan_id_and_LSDU_size;
	__be16          PRP_suffix;
} __packed;

static inline u16 get_prp_LSDU_size(struct prp_rct *rct)
{
	return ntohs(rct->lan_id_and_LSDU_size) & 0x0FFF;
}

static inline void set_prp_lan_id(struct prp_rct *rct, u16 lan_id)
{
	rct->lan_id_and_LSDU_size = htons((ntohs(rct->lan_id_and_LSDU_size) &
					  0x0FFF) | (lan_id << 12));
}
static inline void set_prp_LSDU_size(struct prp_rct *rct, u16 LSDU_size)
{
	rct->lan_id_and_LSDU_size = htons((ntohs(rct->lan_id_and_LSDU_size) &
					  0xF000) | (LSDU_size & 0x0FFF));
}

struct hsr_port {
	struct list_head	port_list;
	struct net_device	*dev;
	struct hsr_priv		*hsr;
	enum hsr_port_type	type;
};

struct hsr_frame_info;
struct hsr_node;

struct hsr_proto_ops {
	/* format and send supervision frame */
	void (*send_sv_frame)(struct hsr_port *port, unsigned long *interval);
	void (*handle_san_frame)(bool san, enum hsr_port_type port,
				 struct hsr_node *node);
	bool (*drop_frame)(struct hsr_frame_info *frame, struct hsr_port *port);
	struct sk_buff * (*get_untagged_frame)(struct hsr_frame_info *frame,
					       struct hsr_port *port);
	struct sk_buff * (*create_tagged_frame)(struct hsr_frame_info *frame,
						struct hsr_port *port);
	void (*fill_frame_info)(__be16 proto, struct sk_buff *skb,
				struct hsr_frame_info *frame);
	bool (*invalid_dan_ingress_frame)(__be16 protocol);
	void (*update_san_info)(struct hsr_node *node, bool is_sup);
};

struct hsr_priv {
	struct rcu_head		rcu_head;
	struct list_head	ports;
	struct list_head	node_db;	/* Known HSR nodes */
	struct list_head	self_node_db;	/* MACs of slaves */
	struct timer_list	announce_timer;	/* Supervision frame dispatch */
	struct timer_list	prune_timer;
	int announce_count;
	u16 sequence_nr;
	u16 sup_sequence_nr;	/* For HSRv1 separate seq_nr for supervision */
	enum hsr_version prot_version;	/* Indicate if HSRv0, HSRv1 or PRPv1 */
	spinlock_t seqnr_lock;	/* locking for sequence_nr */
	spinlock_t list_lock;	/* locking for node list */
	struct hsr_proto_ops	*proto_ops;
#define PRP_LAN_ID	0x5     /* 0x1010 for A and 0x1011 for B. Bit 0 is set
				 * based on SLAVE_A or SLAVE_B
				 */
	u8 net_id;		/* for PRP, it occupies most significant 3 bits
				 * of lan_id
				 */
	unsigned char		sup_multicast_addr[ETH_ALEN] __aligned(sizeof(u16));
				/* Align to u16 boundary to avoid unaligned access
				 * in ether_addr_equal
				 */
#ifdef	CONFIG_DEBUG_FS
	struct dentry *node_tbl_root;
#endif
};

#define hsr_for_each_port(hsr, port) \
	list_for_each_entry_rcu((port), &(hsr)->ports, port_list)

struct hsr_port *hsr_port_get_hsr(struct hsr_priv *hsr, enum hsr_port_type pt);

/* Caller must ensure skb is a valid HSR frame */
static inline u16 hsr_get_skb_sequence_nr(struct sk_buff *skb)
{
	struct hsr_ethhdr *hsr_ethhdr;

	hsr_ethhdr = (struct hsr_ethhdr *)skb_mac_header(skb);
	return ntohs(hsr_ethhdr->hsr_tag.sequence_nr);
}

static inline struct prp_rct *skb_get_PRP_rct(struct sk_buff *skb)
{
	unsigned char *tail = skb_tail_pointer(skb) - HSR_HLEN;

	struct prp_rct *rct = (struct prp_rct *)tail;

	if (rct->PRP_suffix == htons(ETH_P_PRP))
		return rct;

	return NULL;
}

/* Assume caller has confirmed this skb is PRP suffixed */
static inline u16 prp_get_skb_sequence_nr(struct prp_rct *rct)
{
	return ntohs(rct->sequence_nr);
}

static inline u16 get_prp_lan_id(struct prp_rct *rct)
{
	return ntohs(rct->lan_id_and_LSDU_size) >> 12;
}

/* assume there is a valid rct */
static inline bool prp_check_lsdu_size(struct sk_buff *skb,
				       struct prp_rct *rct,
				       bool is_sup)
{
	struct ethhdr *ethhdr;
	int expected_lsdu_size;

	if (is_sup) {
		expected_lsdu_size = HSR_V1_SUP_LSDUSIZE;
	} else {
		ethhdr = (struct ethhdr *)skb_mac_header(skb);
		expected_lsdu_size = skb->len - 14;
		if (ethhdr->h_proto == htons(ETH_P_8021Q))
			expected_lsdu_size -= 4;
	}

	return (expected_lsdu_size == get_prp_LSDU_size(rct));
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
void hsr_debugfs_rename(struct net_device *dev);
void hsr_debugfs_init(struct hsr_priv *priv, struct net_device *hsr_dev);
void hsr_debugfs_term(struct hsr_priv *priv);
void hsr_debugfs_create_root(void);
void hsr_debugfs_remove_root(void);
#else
static inline void hsr_debugfs_rename(struct net_device *dev)
{
}
static inline void hsr_debugfs_init(struct hsr_priv *priv,
				    struct net_device *hsr_dev)
{}
static inline void hsr_debugfs_term(struct hsr_priv *priv)
{}
static inline void hsr_debugfs_create_root(void)
{}
static inline void hsr_debugfs_remove_root(void)
{}
#endif

#endif /*  __HSR_PRIVATE_H */
