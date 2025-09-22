/*	$OpenBSD: mrt.h,v 1.38 2022/12/28 21:30:16 jmc Exp $ */

/*
 * Copyright (c) 2003, 2004 Claudio Jeker <claudio@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __MRT_H__
#define __MRT_H__

/*
 * MRT binary packet format
 * For more info see:
 * RFC6396 , "MRT Routing Information Export Format"
 * http://www.quagga.net/docs/docs-multi/Packet-Binary-Dump-Format.html
 */

/*
 * MRT header:
 * +--------+--------+--------+--------+
 * |             timestamp             |
 * +--------+--------+--------+--------+
 * |      type       |     subtype     |
 * +--------+--------+--------+--------+
 * |               length              | length of packet excluding this header
 * +--------+--------+--------+--------+
 *
 * ET types include an additional 32bit microsecond field coming after the
 * length field. Which is accounted in the length field.
 */
#define MRT_HEADER_SIZE		12
#define MRT_ET_HEADER_SIZE	16

struct mrt_hdr {
	uint32_t	timestamp;
	uint16_t	type;
	uint16_t	subtype;
	uint32_t	length;
} __packed;

enum MRT_MSG_TYPES {
	MSG_NULL,		/*  0 empty msg (deprecated) */
	MSG_START,		/*  1 sender is starting up */
	MSG_DIE,		/*  2 receiver should shut down (deprecated) */
	MSG_I_AM_DEAD,		/*  3 sender is shutting down */
	MSG_PEER_DOWN,		/*  4 sender's peer is down (deprecated) */
	MSG_PROTOCOL_BGP,	/*  5 msg is a BGP packet (deprecated) */
	MSG_PROTOCOL_RIP,	/*  6 msg is a RIP packet */
	MSG_PROTOCOL_IDRP,	/*  7 msg is an IDRP packet (deprecated) */
	MSG_PROTOCOL_RIPNG,	/*  8 msg is a RIPNG packet */
	MSG_PROTOCOL_BGP4PLUS,	/*  9 msg is a BGP4+ packet (deprecated) */
	MSG_PROTOCOL_BGP4PLUS1,	/* 10 msg is a BGP4+ (draft 01) (deprecated) */
	MSG_PROTOCOL_OSPF,	/* 11 msg is an OSPF packet */
	MSG_TABLE_DUMP,		/* 12 routing table dump */
	MSG_TABLE_DUMP_V2,	/* 13 routing table dump */
	MSG_PROTOCOL_BGP4MP=16,	/* 16 zebras own packet format */
	MSG_PROTOCOL_BGP4MP_ET=17,
	MSG_PROTOCOL_ISIS=32,	/* 32 msg is a ISIS package */
	MSG_PROTOCOL_ISIS_ET=33,
	MSG_PROTOCOL_OSPFV3=48,	/* 48 msg is a OSPFv3 package */
	MSG_PROTOCOL_OSPFV3_ET=49
};

/*
 * Main zebra dump format is in MSG_PROTOCOL_BGP4MP exceptions are table dumps
 * that are normally saved as MSG_TABLE_DUMP.
 * In most cases this is the format to choose to dump updates et al.
 */
enum MRT_BGP4MP_SUBTYPES {
	BGP4MP_STATE_CHANGE,	/* state change */
	BGP4MP_MESSAGE,		/* bgp message */
	BGP4MP_ENTRY,		/* table dumps (deprecated) */
	BGP4MP_SNAPSHOT,	/* file name for dump (deprecated) */
	BGP4MP_MESSAGE_AS4,	/* same as BGP4MP_MESSAGE with 4byte AS */
	BGP4MP_STATE_CHANGE_AS4,
	BGP4MP_MESSAGE_LOCAL,	  /* same as BGP4MP_MESSAGE but for self */
	BGP4MP_MESSAGE_AS4_LOCAL, /* originated updates. Not implemented */
	BGP4MP_MESSAGE_ADDPATH,	  /* same as above but for add-path peers */
	BGP4MP_MESSAGE_AS4_ADDPATH,
	BGP4MP_MESSAGE_LOCAL_ADDPATH,
	BGP4MP_MESSAGE_AS4_LOCAL_ADDPATH,
};

/* size of the BGP4MP headers without payload */
#define MRT_BGP4MP_IPv4_HEADER_SIZE		16
#define MRT_BGP4MP_IPv6_HEADER_SIZE		40
#define MRT_BGP4MP_ET_IPv4_HEADER_SIZE		20
#define MRT_BGP4MP_ET_IPv6_HEADER_SIZE		44
/* 4-byte AS variants of the previous */
#define MRT_BGP4MP_AS4_IPv4_HEADER_SIZE		20
#define MRT_BGP4MP_AS4_IPv6_HEADER_SIZE		44
#define MRT_BGP4MP_ET_AS4_IPv4_HEADER_SIZE	24
#define MRT_BGP4MP_ET_AS4_IPv6_HEADER_SIZE	48

/* If the type is PROTOCOL_BGP4MP and the subtype is either BGP4MP_STATE_CHANGE
 * or BGP4MP_MESSAGE the message consists of a common header plus the payload.
 * Header format:
 *
 * +--------+--------+--------+--------+
 * |    source_as    |     dest_as     |
 * +--------+--------+--------+--------+
 * |    if_index     |       afi       |
 * +--------+--------+--------+--------+
 * |             source_ip             |
 * +--------+--------+--------+--------+
 * |              dest_ip              |
 * +--------+--------+--------+--------+
 * |      message specific payload ...
 * +--------+--------+--------+--------+
 *
 * The source_ip and dest_ip are dependant of the afi type. For IPv6 source_ip
 * and dest_ip are both 16 bytes long.
 * For the AS4 types the source_as and dest_as numbers are both 4 bytes long.
 *
 * Payload of a BGP4MP_STATE_CHANGE packet:
 *
 * +--------+--------+--------+--------+
 * |    old_state    |    new_state    |
 * +--------+--------+--------+--------+
 *
 * The state values are the same as in RFC 1771.
 *
 * The payload of a BGP4MP_MESSAGE is the full bgp message with header.
 */

/*
 * size of the BGP4MP entries without variable stuff.
 * All until nexthop plus attr_len, not included plen, prefix and bgp attrs.
 */
#define MRT_BGP4MP_IPv4_ENTRY_SIZE	18
#define MRT_BGP4MP_IPv6_ENTRY_SIZE	30
#define MRT_BGP4MP_MAX_PREFIXLEN	256
/*
 * The "new" table dump format consists of messages of type PROTOCOL_BGP4MP
 * and subtype BGP4MP_ENTRY.
 *
 * +--------+--------+--------+--------+
 * |      view       |     status      |
 * +--------+--------+--------+--------+
 * |            originated             |
 * +--------+--------+--------+--------+
 * |       afi       |  safi  | nhlen  |
 * +--------+--------+--------+--------+
 * |              nexthop              |
 * +--------+--------+--------+--------+
 * |  plen  |  prefix variable  ...    |
 * +--------+--------+--------+--------+
 * |    attr_len     | bgp attributes
 * +--------+--------+--------+--------+
 *  bgp attributes, attr_len bytes long
 * +--------+--------+--------+--------+
 *   ...                      |
 * +--------+--------+--------+
 *
 * View is normally 0 and originated the time of last change.
 * The status seems to be 1 by default but probably something to indicate
 * the status of a prefix would be more useful.
 * The format of the nexthop address is defined via the afi value. For IPv6
 * the nexthop field is 16 bytes long.
 * The prefix field is as in the bgp update message variable length. It is
 * plen bits long but rounded to the next octet.
 */

/*
 * New MRT dump format MSG_TABLE_DUMP_V2, the dump is implemented with
 * sub-tables for peers and NLRI entries just use the index into the peer
 * table.
 */
enum MRT_DUMP_V2_SUBTYPES {
	MRT_DUMP_V2_PEER_INDEX_TABLE=1,
	MRT_DUMP_V2_RIB_IPV4_UNICAST=2,
	MRT_DUMP_V2_RIB_IPV4_MULTICAST=3,
	MRT_DUMP_V2_RIB_IPV6_UNICAST=4,
	MRT_DUMP_V2_RIB_IPV6_MULTICAST=5,
	MRT_DUMP_V2_RIB_GENERIC=6,
	MRT_DUMP_V2_RIB_IPV4_UNICAST_ADDPATH=8,
	MRT_DUMP_V2_RIB_IPV4_MULTICAST_ADDPATH=9,
	MRT_DUMP_V2_RIB_IPV6_UNICAST_ADDPATH=10,
	MRT_DUMP_V2_RIB_IPV6_MULTICAST_ADDPATH=11,
	MRT_DUMP_V2_RIB_GENERIC_ADDPATH=12,
};

/*
 * Format of the MRT_DUMP_V2_PEER_INDEX_TABLE:
 * If there is no view_name, view_name_len must be set to 0
 *
 * +--------+--------+--------+--------+
 * |         collector_bgp_id          |
 * +--------+--------+--------+--------+
 * |  view_name_len  |    view_name
 * +--------+--------+--------+--------+
 *        view_name (variable) ...     |
 * +--------+--------+--------+--------+
 * |   peer_count    |   peer_entries
 * +--------+--------+--------+--------+
 *       peer_entries (variable) ...
 * +--------+--------+--------+--------+
 *
 * The format of a peer_entry is the following:
 *
 * +--------+
 * |  type  |
 * +--------+--------+--------+--------+
 * |            peer_bgp_id            |
 * +--------+--------+--------+--------+
 * |       peer_ip_addr (variable)     |
 * +--------+--------+--------+--------+
 * |            peer_as (variable)     |
 * +--------+--------+--------+--------+
 *
 * The message is packed a bit strangely. The type byte defines what size
 * the peer addr and peer AS have.
 * The position of a peer in the PEER_INDEX_TABLE is used as the index for
 * the other messages.
 */
#define MRT_DUMP_V2_PEER_BIT_I	0x1	/* set for IPv6 addrs */
#define MRT_DUMP_V2_PEER_BIT_A	0x2	/* set for 32 bits AS number */

/*
 * AFI/SAFI specific RIB Subtypes are special to save a few bytes.
 *
 * +--------+--------+--------+--------+
 * |              seq_num              |
 * +--------+--------+--------+--------+
 * |  plen  |  prefix (variable)
 * +--------+--------+--------+--------+
 * |     #entry      | rib entries (variable)
 * +--------+--------+--------+--------+
 *
 * The RIB_GENERIC subtype is needed for the less common AFI/SAFI pairs.
 *
 * +--------+--------+--------+--------+
 * |              seq_num              |
 * +--------+--------+--------+--------+
 * |       AFI       |  SAFI  |  NLRI
 * +--------+--------+--------+--------+
 *     NLRI (variable) ...
 * +--------+--------+--------+--------+
 * |     #entry      | rib entries (variable)
 * +--------+--------+--------+--------+
 */

/*
 * The RIB entries have the following form.
 *
 * +--------+--------+
 * |   peer index    |
 * +--------+--------+--------+--------+
 * |          originated_time          |
 * +--------+--------+--------+--------+
 * [    path_id in _ADDPATH variants   ]
 * +--------+--------+--------+--------+
 * |    attr_len     |   bgp_attrs
 * +--------+--------+--------+--------+
 *      bgp_attrs (variable) ...
 * +--------+--------+--------+--------+
 *
 * Some BGP path attributes need special encoding:
 *  - the AS_PATH attribute MUST be encoded as 4-Byte AS
 *  - the MP_REACH_NLRI only consists of the nexthop len and nexthop address
 *
 * The non generic ADDPATH variants add the path-identifier between
 * originated_time and attr_len. For RIB_GENERIC_ADDPATH the path_id should
 * be part of the NLRI.
 */

/*
 * Format for routing table dumps in "old" mrt format.
 * Type MSG_TABLE_DUMP and subtype is AFI_IPv4 (1) for IPv4 and AFI_IPv6 (2)
 * for IPv6. In the IPv6 case prefix and peer_ip are both 16 bytes long.
 *
 * +--------+--------+--------+--------+
 * |      view       |      seqnum     |
 * +--------+--------+--------+--------+
 * |               prefix              |
 * +--------+--------+--------+--------+
 * |  plen  | status | originated time
 * +--------+--------+--------+--------+
 *   originated time |     peer_ip
 * +--------+--------+--------+--------+
 *       peer_ip     |     peer_as     |
 * +--------+--------+--------+--------+
 * |    attr_len     | bgp attributes
 * +--------+--------+--------+--------+
 *  bgp attributes, attr_len bytes long
 * +--------+--------+--------+--------+
 *   ...                      |
 * +--------+--------+--------+
 *
 *
 * View is normally 0 and seqnum just a simple counter for this dump.
 * The status field is unused and should be set to 1.
 */

enum MRT_DUMP_SUBTYPES {
	MRT_DUMP_AFI_IP=1,
	MRT_DUMP_AFI_IPv6=2
};

/* size of the dump header until attr_len */
#define MRT_DUMP_HEADER_SIZE	22
#define MRT_DUMP_HEADER_SIZE_V6	46

/*
 * OLD MRT message headers. These structs are here for completion but
 * will not be used to generate dumps. It seems that nobody uses those.
 *
 * Only for bgp messages (type 5, 9 and 10)
 * Nota bene for bgp dumps MSG_PROTOCOL_BGP4MP should be used.
 */
enum MRT_BGP_SUBTYPES {
	MSG_BGP_NULL,
	MSG_BGP_UPDATE,		/* raw update packet (contains both withdraws
				   and announcements) */
	MSG_BGP_PREF_UPDATE,	/* tlv preferences followed by raw update */
	MSG_BGP_STATE_CHANGE,	/* state change */
	MSG_BGP_SYNC,		/* file name for a table dump */
	MSG_BGP_OPEN,		/* BGP open messages */
	MSG_BGP_NOTIFY,		/* BGP notify messages */
	MSG_BGP_KEEPALIVE	/* BGP keepalives */
};

/* if type MSG_PROTOCOL_BGP and subtype MSG_BGP_UPDATE, MSG_BGP_OPEN,
 * MSG_BGP_NOTIFY or MSG_BGP_KEEPALIVE
 *
 * +--------+--------+--------+--------+
 * |    source_as    |    source_ip
 * +--------+--------+--------+--------+
 *      source_ip    |    dest_as      |
 * +--------+--------+--------+--------+
 * |               dest_ip             |
 * +--------+--------+--------+--------+
 * | bgp update packet with header et
 * +--------+--------+--------+--------+
 *   al. (variable length) ...
 * +--------+--------+--------+--------+
 *
 * For IPv6 the type is MSG_PROTOCOL_BGP4PLUS and the subtype remains
 * MSG_BGP_UPDATE. The source_ip and dest_ip are again extended to 16 bytes.
 *
 * For subtype MSG_BGP_STATE_CHANGE (for all BGP types or just for the
 * MSG_PROTOCOL_BGP4PLUS case? Unclear.)
 *
 * +--------+--------+--------+--------+
 * |    source_as    |    source_ip
 * +--------+--------+--------+--------+
 *      source_ip    |    old_state    |
 * +--------+--------+--------+--------+
 * |    new_state    |
 * +--------+--------+
 *
 * States are defined in RFC 1771/4271.
 */

/*
 * if type MSG_PROTOCOL_BGP and subtype MSG_BGP_SYNC OR
 * if type MSG_PROTOCOL_BGP4MP and subtype BGP4MP_SNAPSHOT
 * *DEPRECATED*
 *
 * +--------+--------+--------+--------+
 * |      view       |    filename
 * +--------+--------+--------+--------+
 *    filename variable length zero
 * +--------+--------+--------+--------+
 *    terminated ... |   0    |
 * +--------+--------+--------+
 */
#endif
