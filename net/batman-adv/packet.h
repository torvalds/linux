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

#ifndef _NET_BATMAN_ADV_PACKET_H_
#define _NET_BATMAN_ADV_PACKET_H_

#define BATADV_ETH_P_BATMAN  0x4305 /* unofficial/not registered Ethertype */

enum batadv_packettype {
	BATADV_IV_OGM	    = 0x01,
	BATADV_ICMP	    = 0x02,
	BATADV_UNICAST	    = 0x03,
	BATADV_BCAST	    = 0x04,
	BATADV_VIS	    = 0x05,
	BATADV_UNICAST_FRAG = 0x06,
	BATADV_TT_QUERY	    = 0x07,
	BATADV_ROAM_ADV	    = 0x08,
};

/* this file is included by batctl which needs these defines */
#define BATADV_COMPAT_VERSION 14

enum batadv_iv_flags {
	BATADV_NOT_BEST_NEXT_HOP   = BIT(3),
	BATADV_PRIMARIES_FIRST_HOP = BIT(4),
	BATADV_VIS_SERVER	   = BIT(5),
	BATADV_DIRECTLINK	   = BIT(6),
};

/* ICMP message types */
enum batadv_icmp_packettype {
	BATADV_ECHO_REPLY	       = 0,
	BATADV_DESTINATION_UNREACHABLE = 3,
	BATADV_ECHO_REQUEST	       = 8,
	BATADV_TTL_EXCEEDED	       = 11,
	BATADV_PARAMETER_PROBLEM       = 12,
};

/* vis defines */
enum batadv_vis_packettype {
	BATADV_VIS_TYPE_SERVER_SYNC   = 0,
	BATADV_VIS_TYPE_CLIENT_UPDATE = 1,
};

/* fragmentation defines */
enum batadv_unicast_frag_flags {
	BATADV_UNI_FRAG_HEAD	  = BIT(0),
	BATADV_UNI_FRAG_LARGETAIL = BIT(1),
};

/* TT_QUERY subtypes */
#define BATADV_TT_QUERY_TYPE_MASK 0x3

enum batadv_tt_query_packettype {
	BATADV_TT_REQUEST  = 0,
	BATADV_TT_RESPONSE = 1,
};

/* TT_QUERY flags */
enum batadv_tt_query_flags {
	BATADV_TT_FULL_TABLE = BIT(2),
};

/* BATADV_TT_CLIENT flags.
 * Flags from BIT(0) to BIT(7) are sent on the wire, while flags from BIT(8) to
 * BIT(15) are used for local computation only
 */
enum batadv_tt_client_flags {
	BATADV_TT_CLIENT_DEL     = BIT(0),
	BATADV_TT_CLIENT_ROAM    = BIT(1),
	BATADV_TT_CLIENT_WIFI    = BIT(2),
	BATADV_TT_CLIENT_NOPURGE = BIT(8),
	BATADV_TT_CLIENT_NEW     = BIT(9),
	BATADV_TT_CLIENT_PENDING = BIT(10),
};

/* claim frame types for the bridge loop avoidance */
enum batadv_bla_claimframe {
	BATADV_CLAIM_TYPE_CLAIM		= 0x00,
	BATADV_CLAIM_TYPE_UNCLAIM	= 0x01,
	BATADV_CLAIM_TYPE_ANNOUNCE	= 0x02,
	BATADV_CLAIM_TYPE_REQUEST	= 0x03,
};

/* the destination hardware field in the ARP frame is used to
 * transport the claim type and the group id
 */
struct batadv_bla_claim_dst {
	uint8_t magic[3];	/* FF:43:05 */
	uint8_t type;		/* bla_claimframe */
	__be16 group;		/* group id */
} __packed;

struct batadv_header {
	uint8_t  packet_type;
	uint8_t  version;  /* batman version field */
	uint8_t  ttl;
} __packed;

struct batadv_ogm_packet {
	struct batadv_header header;
	uint8_t  flags;    /* 0x40: DIRECTLINK flag, 0x20 VIS_SERVER flag... */
	__be32   seqno;
	uint8_t  orig[ETH_ALEN];
	uint8_t  prev_sender[ETH_ALEN];
	uint8_t  gw_flags;  /* flags related to gateway class */
	uint8_t  tq;
	uint8_t  tt_num_changes;
	uint8_t  ttvn; /* translation table version number */
	__be16   tt_crc;
} __packed;

#define BATADV_OGM_HLEN sizeof(struct batadv_ogm_packet)

struct batadv_icmp_packet {
	struct batadv_header header;
	uint8_t  msg_type; /* see ICMP message types above */
	uint8_t  dst[ETH_ALEN];
	uint8_t  orig[ETH_ALEN];
	__be16   seqno;
	uint8_t  uid;
	uint8_t  reserved;
} __packed;

#define BATADV_RR_LEN 16

/* icmp_packet_rr must start with all fields from imcp_packet
 * as this is assumed by code that handles ICMP packets
 */
struct batadv_icmp_packet_rr {
	struct batadv_header header;
	uint8_t  msg_type; /* see ICMP message types above */
	uint8_t  dst[ETH_ALEN];
	uint8_t  orig[ETH_ALEN];
	__be16   seqno;
	uint8_t  uid;
	uint8_t  rr_cur;
	uint8_t  rr[BATADV_RR_LEN][ETH_ALEN];
} __packed;

struct batadv_unicast_packet {
	struct batadv_header header;
	uint8_t  ttvn; /* destination translation table version number */
	uint8_t  dest[ETH_ALEN];
} __packed;

struct batadv_unicast_frag_packet {
	struct batadv_header header;
	uint8_t  ttvn; /* destination translation table version number */
	uint8_t  dest[ETH_ALEN];
	uint8_t  flags;
	uint8_t  align;
	uint8_t  orig[ETH_ALEN];
	__be16   seqno;
} __packed;

struct batadv_bcast_packet {
	struct batadv_header header;
	uint8_t  reserved;
	__be32   seqno;
	uint8_t  orig[ETH_ALEN];
} __packed;

struct batadv_vis_packet {
	struct batadv_header header;
	uint8_t  vis_type;	 /* which type of vis-participant sent this? */
	__be32   seqno;		 /* sequence number */
	uint8_t  entries;	 /* number of entries behind this struct */
	uint8_t  reserved;
	uint8_t  vis_orig[ETH_ALEN];	/* originator reporting its neighbors */
	uint8_t  target_orig[ETH_ALEN]; /* who should receive this packet */
	uint8_t  sender_orig[ETH_ALEN]; /* who sent or forwarded this packet */
} __packed;

struct batadv_tt_query_packet {
	struct batadv_header header;
	/* the flag field is a combination of:
	 * - TT_REQUEST or TT_RESPONSE
	 * - TT_FULL_TABLE
	 */
	uint8_t  flags;
	uint8_t  dst[ETH_ALEN];
	uint8_t  src[ETH_ALEN];
	/* the ttvn field is:
	 * if TT_REQUEST: ttvn that triggered the
	 *		  request
	 * if TT_RESPONSE: new ttvn for the src
	 *		   orig_node
	 */
	uint8_t  ttvn;
	/* tt_data field is:
	 * if TT_REQUEST: crc associated with the
	 *		  ttvn
	 * if TT_RESPONSE: table_size
	 */
	__be16 tt_data;
} __packed;

struct batadv_roam_adv_packet {
	struct batadv_header header;
	uint8_t  reserved;
	uint8_t  dst[ETH_ALEN];
	uint8_t  src[ETH_ALEN];
	uint8_t  client[ETH_ALEN];
} __packed;

struct batadv_tt_change {
	uint8_t flags;
	uint8_t addr[ETH_ALEN];
} __packed;

#endif /* _NET_BATMAN_ADV_PACKET_H_ */
