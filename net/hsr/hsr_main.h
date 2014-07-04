/* Copyright 2011-2014 Autronica Fire and Security AS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Author(s):
 *	2011-2014 Arvid Brodin, arvid.brodin@alten.se
 */

#ifndef __HSR_PRIVATE_H
#define __HSR_PRIVATE_H

#include <linux/netdevice.h>
#include <linux/list.h>


/* Time constants as specified in the HSR specification (IEC-62439-3 2010)
 * Table 8.
 * All values in milliseconds.
 */
#define HSR_LIFE_CHECK_INTERVAL		 2000 /* ms */
#define HSR_NODE_FORGET_TIME		60000 /* ms */
#define HSR_ANNOUNCE_INTERVAL		  100 /* ms */


/* By how much may slave1 and slave2 timestamps of latest received frame from
 * each node differ before we notify of communication problem?
 */
#define MAX_SLAVE_DIFF			 3000 /* ms */


/* How often shall we check for broken ring and remove node entries older than
 * HSR_NODE_FORGET_TIME?
 */
#define PRUNE_PERIOD			 3000 /* ms */


#define HSR_TLV_ANNOUNCE		   22
#define HSR_TLV_LIFE_CHECK		   23


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
static inline u16 get_hsr_tag_path(struct hsr_tag *ht)
{
	return ntohs(ht->path_and_LSDU_size) >> 12;
}

static inline u16 get_hsr_tag_LSDU_size(struct hsr_tag *ht)
{
	return ntohs(ht->path_and_LSDU_size) & 0x0FFF;
}

static inline void set_hsr_tag_path(struct hsr_tag *ht, u16 path)
{
	ht->path_and_LSDU_size = htons(
			(ntohs(ht->path_and_LSDU_size) & 0x0FFF) | (path << 12));
}

static inline void set_hsr_tag_LSDU_size(struct hsr_tag *ht, u16 LSDU_size)
{
	ht->path_and_LSDU_size = htons(
			(ntohs(ht->path_and_LSDU_size) & 0xF000) |
			(LSDU_size & 0x0FFF));
}

struct hsr_ethhdr {
	struct ethhdr	ethhdr;
	struct hsr_tag	hsr_tag;
} __packed;


/* HSR Supervision Frame data types.
 * Field names as defined in the IEC:2010 standard for HSR.
 */
struct hsr_sup_tag {
	__be16		path_and_HSR_Ver;
	__be16		sequence_nr;
	__u8		HSR_TLV_Type;
	__u8		HSR_TLV_Length;
} __packed;

struct hsr_sup_payload {
	unsigned char	MacAddressA[ETH_ALEN];
} __packed;

static inline u16 get_hsr_stag_path(struct hsr_sup_tag *hst)
{
	return get_hsr_tag_path((struct hsr_tag *) hst);
}

static inline u16 get_hsr_stag_HSR_ver(struct hsr_sup_tag *hst)
{
	return get_hsr_tag_LSDU_size((struct hsr_tag *) hst);
}

static inline void set_hsr_stag_path(struct hsr_sup_tag *hst, u16 path)
{
	set_hsr_tag_path((struct hsr_tag *) hst, path);
}

static inline void set_hsr_stag_HSR_Ver(struct hsr_sup_tag *hst, u16 HSR_Ver)
{
	set_hsr_tag_LSDU_size((struct hsr_tag *) hst, HSR_Ver);
}

struct hsr_ethhdr_sp {
	struct ethhdr		ethhdr;
	struct hsr_sup_tag	hsr_sup;
} __packed;


enum hsr_dev_idx {
	HSR_DEV_NONE = -1,
	HSR_DEV_SLAVE_A = 0,
	HSR_DEV_SLAVE_B,
	HSR_DEV_MASTER,
};
#define HSR_MAX_SLAVE	(HSR_DEV_SLAVE_B + 1)
#define HSR_MAX_DEV	(HSR_DEV_MASTER + 1)

struct hsr_priv {
	struct list_head	hsr_list;	/* List of hsr devices */
	struct rcu_head		rcu_head;
	struct net_device	*dev;
	struct net_device	*slave[HSR_MAX_SLAVE];
	struct list_head	node_db;	/* Other HSR nodes */
	struct list_head	self_node_db;	/* MACs of slaves */
	struct timer_list	announce_timer;	/* Supervision frame dispatch */
	int announce_count;
	u16 sequence_nr;
	spinlock_t seqnr_lock;			/* locking for sequence_nr */
	unsigned char		sup_multicast_addr[ETH_ALEN];
};

void register_hsr_master(struct hsr_priv *hsr);
void unregister_hsr_master(struct hsr_priv *hsr);
bool is_hsr_slave(struct net_device *dev);

#endif /*  __HSR_PRIVATE_H */
