/*
 * Copyright (C) 2007-2011 B.A.T.M.A.N. contributors:
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

#ifndef _NET_BATMAN_ADV_PACKET_H_
#define _NET_BATMAN_ADV_PACKET_H_

#define ETH_P_BATMAN  0x4305	/* unofficial/not registered Ethertype */

enum bat_packettype {
	BAT_PACKET       = 0x01,
	BAT_ICMP         = 0x02,
	BAT_UNICAST      = 0x03,
	BAT_BCAST        = 0x04,
	BAT_VIS          = 0x05,
	BAT_UNICAST_FRAG = 0x06
};

/* this file is included by batctl which needs these defines */
#define COMPAT_VERSION 14

enum batman_flags {
	PRIMARIES_FIRST_HOP = 1 << 4,
	VIS_SERVER	    = 1 << 5,
	DIRECTLINK	    = 1 << 6
};

/* ICMP message types */
enum icmp_packettype {
	ECHO_REPLY		= 0,
	DESTINATION_UNREACHABLE = 3,
	ECHO_REQUEST		= 8,
	TTL_EXCEEDED		= 11,
	PARAMETER_PROBLEM	= 12
};

/* vis defines */
enum vis_packettype {
	VIS_TYPE_SERVER_SYNC   = 0,
	VIS_TYPE_CLIENT_UPDATE = 1
};

/* fragmentation defines */
enum unicast_frag_flags {
	UNI_FRAG_HEAD	   = 1 << 0,
	UNI_FRAG_LARGETAIL = 1 << 1
};

struct batman_packet {
	uint8_t  packet_type;
	uint8_t  version;  /* batman version field */
	uint8_t  ttl;
	uint8_t  flags;    /* 0x40: DIRECTLINK flag, 0x20 VIS_SERVER flag... */
	uint32_t seqno;
	uint8_t  orig[6];
	uint8_t  prev_sender[6];
	uint8_t  gw_flags;  /* flags related to gateway class */
	uint8_t  tq;
	uint8_t  num_tt;
	uint8_t  reserved;
} __packed;

#define BAT_PACKET_LEN sizeof(struct batman_packet)

struct icmp_packet {
	uint8_t  packet_type;
	uint8_t  version;  /* batman version field */
	uint8_t  ttl;
	uint8_t  msg_type; /* see ICMP message types above */
	uint8_t  dst[6];
	uint8_t  orig[6];
	uint16_t seqno;
	uint8_t  uid;
	uint8_t  reserved;
} __packed;

#define BAT_RR_LEN 16

/* icmp_packet_rr must start with all fields from imcp_packet
 * as this is assumed by code that handles ICMP packets */
struct icmp_packet_rr {
	uint8_t  packet_type;
	uint8_t  version;  /* batman version field */
	uint8_t  ttl;
	uint8_t  msg_type; /* see ICMP message types above */
	uint8_t  dst[6];
	uint8_t  orig[6];
	uint16_t seqno;
	uint8_t  uid;
	uint8_t  rr_cur;
	uint8_t  rr[BAT_RR_LEN][ETH_ALEN];
} __packed;

struct unicast_packet {
	uint8_t  packet_type;
	uint8_t  version;  /* batman version field */
	uint8_t  ttl;
	uint8_t  reserved;
	uint8_t  dest[6];
} __packed;

struct unicast_frag_packet {
	uint8_t  packet_type;
	uint8_t  version;  /* batman version field */
	uint8_t  ttl;
	uint8_t  reserved;
	uint8_t  dest[6];
	uint8_t  flags;
	uint8_t  align;
	uint8_t  orig[6];
	uint16_t seqno;
} __packed;

struct bcast_packet {
	uint8_t  packet_type;
	uint8_t  version;  /* batman version field */
	uint8_t  ttl;
	uint8_t  reserved;
	uint32_t seqno;
	uint8_t  orig[6];
} __packed;

struct vis_packet {
	uint8_t  packet_type;
	uint8_t  version;        /* batman version field */
	uint8_t  ttl;		 /* TTL */
	uint8_t  vis_type;	 /* which type of vis-participant sent this? */
	uint32_t seqno;		 /* sequence number */
	uint8_t  entries;	 /* number of entries behind this struct */
	uint8_t  reserved;
	uint8_t  vis_orig[6];	 /* originator that announces its neighbors */
	uint8_t  target_orig[6]; /* who should receive this packet */
	uint8_t  sender_orig[6]; /* who sent or rebroadcasted this packet */
} __packed;

#endif /* _NET_BATMAN_ADV_PACKET_H_ */
