/*	$FreeBSD$ */
/*	$OpenBSD: if_urndisreg.h,v 1.19 2013/11/21 14:08:05 mpi Exp $ */

/*
 * Copyright (c) 2010 Jonathan Armani <armani@openbsd.org>
 * Copyright (c) 2010 Fabien Romano <fabien@openbsd.org>
 * Copyright (c) 2010 Michael Knudsen <mk@openbsd.org>
 * All rights reserved.
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

#ifndef	_NET_RNDIS_H_
#define	_NET_RNDIS_H_

/* Canonical major/minor version as of 22th Aug. 2016. */
#define	RNDIS_VERSION_MAJOR		0x00000001
#define	RNDIS_VERSION_MINOR		0x00000000

#define	RNDIS_STATUS_SUCCESS 		0x00000000L
#define	RNDIS_STATUS_PENDING 		0x00000103L
#define	RNDIS_STATUS_MEDIA_CONNECT 	0x4001000BL
#define	RNDIS_STATUS_MEDIA_DISCONNECT 	0x4001000CL
#define	RNDIS_STATUS_LINK_SPEED_CHANGE	0x40010013L
#define	RNDIS_STATUS_NETWORK_CHANGE	0x40010018L
#define	RNDIS_STATUS_TASK_OFFLOAD_CURRENT_CONFIG	0x40020006L
#define	RNDIS_STATUS_BUFFER_OVERFLOW 	0x80000005L
#define	RNDIS_STATUS_FAILURE 		0xC0000001L
#define	RNDIS_STATUS_NOT_SUPPORTED 	0xC00000BBL
#define	RNDIS_STATUS_RESOURCES 		0xC000009AL
#define	RNDIS_STATUS_INVALID_DATA 	0xC0010015L

#define	OID_GEN_SUPPORTED_LIST		0x00010101
#define	OID_GEN_HARDWARE_STATUS		0x00010102
#define	OID_GEN_MEDIA_SUPPORTED		0x00010103
#define	OID_GEN_MEDIA_IN_USE		0x00010104
#define	OID_GEN_MAXIMUM_LOOKAHEAD	0x00010105
#define	OID_GEN_MAXIMUM_FRAME_SIZE	0x00010106
#define	OID_GEN_LINK_SPEED		0x00010107
#define	OID_GEN_TRANSMIT_BUFFER_SPACE	0x00010108
#define	OID_GEN_RECEIVE_BUFFER_SPACE	0x00010109
#define	OID_GEN_TRANSMIT_BLOCK_SIZE	0x0001010A
#define	OID_GEN_RECEIVE_BLOCK_SIZE	0x0001010B
#define	OID_GEN_VENDOR_ID		0x0001010C
#define	OID_GEN_VENDOR_DESCRIPTION	0x0001010D
#define	OID_GEN_CURRENT_PACKET_FILTER	0x0001010E
#define	OID_GEN_CURRENT_LOOKAHEAD	0x0001010F
#define	OID_GEN_DRIVER_VERSION		0x00010110
#define	OID_GEN_MAXIMUM_TOTAL_SIZE	0x00010111
#define	OID_GEN_PROTOCOL_OPTIONS	0x00010112
#define	OID_GEN_MAC_OPTIONS		0x00010113
#define	OID_GEN_MEDIA_CONNECT_STATUS	0x00010114
#define	OID_GEN_MAXIMUM_SEND_PACKETS	0x00010115
#define	OID_GEN_VENDOR_DRIVER_VERSION	0x00010116
#define	OID_GEN_SUPPORTED_GUIDS		0x00010117
#define	OID_GEN_NETWORK_LAYER_ADDRESSES	0x00010118
#define	OID_GEN_TRANSPORT_HEADER_OFFSET	0x00010119
#define	OID_GEN_RECEIVE_SCALE_CAPABILITIES	0x00010203
#define	OID_GEN_RECEIVE_SCALE_PARAMETERS	0x00010204
#define	OID_GEN_MACHINE_NAME		0x0001021A
#define	OID_GEN_RNDIS_CONFIG_PARAMETER	0x0001021B
#define	OID_GEN_VLAN_ID			0x0001021C

#define	OID_802_3_PERMANENT_ADDRESS	0x01010101
#define	OID_802_3_CURRENT_ADDRESS	0x01010102
#define	OID_802_3_MULTICAST_LIST	0x01010103
#define	OID_802_3_MAXIMUM_LIST_SIZE	0x01010104
#define	OID_802_3_MAC_OPTIONS		0x01010105
#define	OID_802_3_RCV_ERROR_ALIGNMENT	0x01020101
#define	OID_802_3_XMIT_ONE_COLLISION	0x01020102
#define	OID_802_3_XMIT_MORE_COLLISIONS	0x01020103
#define	OID_802_3_XMIT_DEFERRED		0x01020201
#define	OID_802_3_XMIT_MAX_COLLISIONS	0x01020202
#define	OID_802_3_RCV_OVERRUN		0x01020203
#define	OID_802_3_XMIT_UNDERRUN		0x01020204
#define	OID_802_3_XMIT_HEARTBEAT_FAILURE	0x01020205
#define	OID_802_3_XMIT_TIMES_CRS_LOST	0x01020206
#define	OID_802_3_XMIT_LATE_COLLISIONS	0x01020207

#define	OID_TCP_OFFLOAD_PARAMETERS	0xFC01020C
#define	OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES	0xFC01020D

#define	RNDIS_MEDIUM_802_3		0x00000000

/* Device flags */
#define	RNDIS_DF_CONNECTIONLESS		0x00000001
#define	RNDIS_DF_CONNECTION_ORIENTED	0x00000002

/*
 * Common RNDIS message header.
 */
struct rndis_msghdr {
	uint32_t rm_type;
	uint32_t rm_len;
};

/*
 * RNDIS data message
 */
#define	REMOTE_NDIS_PACKET_MSG		0x00000001

struct rndis_packet_msg {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_dataoffset;
	uint32_t rm_datalen;
	uint32_t rm_oobdataoffset;
	uint32_t rm_oobdatalen;
	uint32_t rm_oobdataelements;
	uint32_t rm_pktinfooffset;
	uint32_t rm_pktinfolen;
	uint32_t rm_vchandle;
	uint32_t rm_reserved;
};

/*
 * Minimum value for rm_dataoffset, rm_oobdataoffset, and
 * rm_pktinfooffset.
 */
#define	RNDIS_PACKET_MSG_OFFSET_MIN		\
	(sizeof(struct rndis_packet_msg) -	\
	 __offsetof(struct rndis_packet_msg, rm_dataoffset))

/* Offset from the beginning of rndis_packet_msg. */
#define	RNDIS_PACKET_MSG_OFFSET_ABS(ofs)	\
	((ofs) + __offsetof(struct rndis_packet_msg, rm_dataoffset))

#define	RNDIS_PACKET_MSG_OFFSET_ALIGN		4
#define	RNDIS_PACKET_MSG_OFFSET_ALIGNMASK	\
	(RNDIS_PACKET_MSG_OFFSET_ALIGN - 1)

/* Per-packet-info for RNDIS data message */
struct rndis_pktinfo {
	uint32_t rm_size;
	uint32_t rm_type;		/* NDIS_PKTINFO_TYPE_ */
	uint32_t rm_pktinfooffset;
	uint8_t rm_data[];
};

#define	RNDIS_PKTINFO_OFFSET		\
	__offsetof(struct rndis_pktinfo, rm_data[0])
#define	RNDIS_PKTINFO_SIZE_ALIGN	4
#define	RNDIS_PKTINFO_SIZE_ALIGNMASK	(RNDIS_PKTINFO_SIZE_ALIGN - 1)

#define	NDIS_PKTINFO_TYPE_CSUM		0
#define	NDIS_PKTINFO_TYPE_IPSEC		1
#define	NDIS_PKTINFO_TYPE_LSO		2
#define	NDIS_PKTINFO_TYPE_CLASSIFY	3
/* reserved 4 */
#define	NDIS_PKTINFO_TYPE_SGLIST	5
#define	NDIS_PKTINFO_TYPE_VLAN		6
#define	NDIS_PKTINFO_TYPE_ORIG		7
#define	NDIS_PKTINFO_TYPE_PKT_CANCELID	8
#define	NDIS_PKTINFO_TYPE_ORIG_NBLIST	9
#define	NDIS_PKTINFO_TYPE_CACHE_NBLIST	10
#define	NDIS_PKTINFO_TYPE_PKT_PAD	11

/*
 * RNDIS control messages
 */

/*
 * Common header for RNDIS completion messages.
 *
 * NOTE: It does not apply to REMOTE_NDIS_RESET_CMPLT.
 */
struct rndis_comp_hdr {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_rid;
	uint32_t rm_status;
};

/* Initialize the device. */
#define	REMOTE_NDIS_INITIALIZE_MSG	0x00000002
#define	REMOTE_NDIS_INITIALIZE_CMPLT	0x80000002

struct rndis_init_req {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_rid;
	uint32_t rm_ver_major;
	uint32_t rm_ver_minor;
	uint32_t rm_max_xfersz;
};

struct rndis_init_comp {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_rid;
	uint32_t rm_status;
	uint32_t rm_ver_major;
	uint32_t rm_ver_minor;
	uint32_t rm_devflags;
	uint32_t rm_medium;
	uint32_t rm_pktmaxcnt;
	uint32_t rm_pktmaxsz;
	uint32_t rm_align;
	uint32_t rm_aflistoffset;
	uint32_t rm_aflistsz;
};

#define	RNDIS_INIT_COMP_SIZE_MIN	\
	__offsetof(struct rndis_init_comp, rm_aflistsz)

/* Halt the device.  No response sent. */
#define	REMOTE_NDIS_HALT_MSG		0x00000003

struct rndis_halt_req {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_rid;
};

/* Send a query object. */
#define	REMOTE_NDIS_QUERY_MSG		0x00000004
#define	REMOTE_NDIS_QUERY_CMPLT		0x80000004

struct rndis_query_req {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_rid;
	uint32_t rm_oid;
	uint32_t rm_infobuflen;
	uint32_t rm_infobufoffset;
	uint32_t rm_devicevchdl;
};

#define	RNDIS_QUERY_REQ_INFOBUFOFFSET		\
	(sizeof(struct rndis_query_req) -	\
	 __offsetof(struct rndis_query_req, rm_rid))

struct rndis_query_comp {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_rid;
	uint32_t rm_status;
	uint32_t rm_infobuflen;
	uint32_t rm_infobufoffset;
};

/* infobuf offset from the beginning of rndis_query_comp. */
#define	RNDIS_QUERY_COMP_INFOBUFOFFSET_ABS(ofs)	\
	((ofs) + __offsetof(struct rndis_query_req, rm_rid))

/* Send a set object request. */
#define	REMOTE_NDIS_SET_MSG		0x00000005
#define	REMOTE_NDIS_SET_CMPLT		0x80000005

struct rndis_set_req {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_rid;
	uint32_t rm_oid;
	uint32_t rm_infobuflen;
	uint32_t rm_infobufoffset;
	uint32_t rm_devicevchdl;
};

#define	RNDIS_SET_REQ_INFOBUFOFFSET		\
	(sizeof(struct rndis_set_req) -		\
	 __offsetof(struct rndis_set_req, rm_rid))

struct rndis_set_comp {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_rid;
	uint32_t rm_status;
};

/*
 * Parameter used by OID_GEN_RNDIS_CONFIG_PARAMETER.
 */
#define	REMOTE_NDIS_SET_PARAM_NUMERIC	0x00000000
#define	REMOTE_NDIS_SET_PARAM_STRING	0x00000002

struct rndis_set_parameter {
	uint32_t rm_nameoffset;
	uint32_t rm_namelen;
	uint32_t rm_type;
	uint32_t rm_valueoffset;
	uint32_t rm_valuelen;
};

/* Perform a soft reset on the device. */
#define	REMOTE_NDIS_RESET_MSG		0x00000006
#define	REMOTE_NDIS_RESET_CMPLT		0x80000006

struct rndis_reset_req {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_rid;
};

struct rndis_reset_comp {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_status;
	uint32_t rm_adrreset;
};

/* 802.3 link-state or undefined message error.  Sent by device. */
#define	REMOTE_NDIS_INDICATE_STATUS_MSG	0x00000007

struct rndis_status_msg {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_status;
	uint32_t rm_stbuflen;
	uint32_t rm_stbufoffset;
	/* rndis_diag_info */
};

/* stbuf offset from the beginning of rndis_status_msg. */
#define	RNDIS_STBUFOFFSET_ABS(ofs)	\
	((ofs) + __offsetof(struct rndis_status_msg, rm_status))

/*
 * Immediately after rndis_status_msg.rm_stbufoffset, if a control
 * message is malformatted, or a packet message contains inappropriate
 * content.
 */
struct rndis_diag_info {
	uint32_t rm_diagstatus;
	uint32_t rm_erroffset;
};

/* Keepalive messsage.  May be sent by device. */
#define	REMOTE_NDIS_KEEPALIVE_MSG	0x00000008
#define	REMOTE_NDIS_KEEPALIVE_CMPLT	0x80000008

struct rndis_keepalive_req {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_rid;
};

struct rndis_keepalive_comp {
	uint32_t rm_type;
	uint32_t rm_len;
	uint32_t rm_rid;
	uint32_t rm_status;
};

/* Packet filter bits used by OID_GEN_CURRENT_PACKET_FILTER */
#define	NDIS_PACKET_TYPE_NONE			0x00000000
#define	NDIS_PACKET_TYPE_DIRECTED		0x00000001
#define	NDIS_PACKET_TYPE_MULTICAST		0x00000002
#define	NDIS_PACKET_TYPE_ALL_MULTICAST		0x00000004
#define	NDIS_PACKET_TYPE_BROADCAST		0x00000008
#define	NDIS_PACKET_TYPE_SOURCE_ROUTING		0x00000010
#define	NDIS_PACKET_TYPE_PROMISCUOUS		0x00000020
#define	NDIS_PACKET_TYPE_SMT			0x00000040
#define	NDIS_PACKET_TYPE_ALL_LOCAL		0x00000080
#define	NDIS_PACKET_TYPE_GROUP			0x00001000
#define	NDIS_PACKET_TYPE_ALL_FUNCTIONAL		0x00002000
#define	NDIS_PACKET_TYPE_FUNCTIONAL		0x00004000
#define	NDIS_PACKET_TYPE_MAC_FRAME		0x00008000

/*
 * Packet filter description for use with printf(9) %b identifier.
 */
#define	NDIS_PACKET_TYPES				\
	"\20\1DIRECT\2MULTICAST\3ALLMULTI\4BROADCAST"	\
	"\5SRCROUTE\6PROMISC\7SMT\10ALLLOCAL"		\
	"\11GROUP\12ALLFUNC\13FUNC\14MACFRAME"

/* RNDIS offsets */
#define	RNDIS_HEADER_OFFSET	((uint32_t)sizeof(struct rndis_msghdr))
#define	RNDIS_DATA_OFFSET	\
    ((uint32_t)(sizeof(struct rndis_packet_msg) - RNDIS_HEADER_OFFSET))

#endif	/* !_NET_RNDIS_H_ */
