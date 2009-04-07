/* SCTP kernel reference Implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * Various protocol defined structures.
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developerst@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson <karl@athena.chicago.il.us>
 *    Jon Grimm <jgrimm@us.ibm.com>
 *    Xingang Guo <xingang.guo@intel.com>
 *    randall@sctp.chicago.il.us
 *    kmorneau@cisco.com
 *    qxie1@email.mot.com
 *    Sridhar Samudrala <sri@us.ibm.com>
 *    Kevin Gao <kevin.gao@intel.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */
#ifndef __LINUX_SCTP_H__
#define __LINUX_SCTP_H__

#include <linux/in.h>		/* We need in_addr.  */
#include <linux/in6.h>		/* We need in6_addr.  */


/* Section 3.1.  SCTP Common Header Format */
typedef struct sctphdr {
	__be16 source;
	__be16 dest;
	__be32 vtag;
	__le32 checksum;
} __attribute__((packed)) sctp_sctphdr_t;

#ifdef __KERNEL__
#include <linux/skbuff.h>

static inline struct sctphdr *sctp_hdr(const struct sk_buff *skb)
{
	return (struct sctphdr *)skb_transport_header(skb);
}
#endif

/* Section 3.2.  Chunk Field Descriptions. */
typedef struct sctp_chunkhdr {
	__u8 type;
	__u8 flags;
	__be16 length;
} __attribute__((packed)) sctp_chunkhdr_t;


/* Section 3.2.  Chunk Type Values.
 * [Chunk Type] identifies the type of information contained in the Chunk
 * Value field. It takes a value from 0 to 254. The value of 255 is
 * reserved for future use as an extension field.
 */
typedef enum {
	SCTP_CID_DATA			= 0,
        SCTP_CID_INIT			= 1,
        SCTP_CID_INIT_ACK		= 2,
        SCTP_CID_SACK			= 3,
        SCTP_CID_HEARTBEAT		= 4,
        SCTP_CID_HEARTBEAT_ACK		= 5,
        SCTP_CID_ABORT			= 6,
        SCTP_CID_SHUTDOWN		= 7,
        SCTP_CID_SHUTDOWN_ACK		= 8,
        SCTP_CID_ERROR			= 9,
        SCTP_CID_COOKIE_ECHO		= 10,
        SCTP_CID_COOKIE_ACK	        = 11,
        SCTP_CID_ECN_ECNE		= 12,
        SCTP_CID_ECN_CWR		= 13,
        SCTP_CID_SHUTDOWN_COMPLETE	= 14,

	/* AUTH Extension Section 4.1 */
	SCTP_CID_AUTH			= 0x0F,

	/* PR-SCTP Sec 3.2 */
	SCTP_CID_FWD_TSN		= 0xC0,

	/* Use hex, as defined in ADDIP sec. 3.1 */
	SCTP_CID_ASCONF			= 0xC1,
	SCTP_CID_ASCONF_ACK		= 0x80,
} sctp_cid_t; /* enum */


/* Section 3.2
 *  Chunk Types are encoded such that the highest-order two bits specify
 *  the action that must be taken if the processing endpoint does not
 *  recognize the Chunk Type.
 */
typedef enum {
	SCTP_CID_ACTION_DISCARD     = 0x00,
	SCTP_CID_ACTION_DISCARD_ERR = 0x40,
	SCTP_CID_ACTION_SKIP        = 0x80,
	SCTP_CID_ACTION_SKIP_ERR    = 0xc0,
} sctp_cid_action_t;

enum { SCTP_CID_ACTION_MASK = 0xc0, };

/* This flag is used in Chunk Flags for ABORT and SHUTDOWN COMPLETE.
 *
 * 3.3.7 Abort Association (ABORT) (6):
 *    The T bit is set to 0 if the sender had a TCB that it destroyed.
 *    If the sender did not have a TCB it should set this bit to 1.
 */
enum { SCTP_CHUNK_FLAG_T = 0x01 };

/*
 *  Set the T bit
 *
 *      0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |   Type = 14   |Reserved     |T|      Length = 4               |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Chunk Flags: 8 bits
 *
 *   Reserved:  7 bits
 *     Set to 0 on transmit and ignored on receipt.
 *
 *   T bit:  1 bit
 *     The T bit is set to 0 if the sender had a TCB that it destroyed. If
 *     the sender did NOT have a TCB it should set this bit to 1.
 *
 * Note: Special rules apply to this chunk for verification, please
 * see Section 8.5.1 for details.
 */

#define sctp_test_T_bit(c)    ((c)->chunk_hdr->flags & SCTP_CHUNK_FLAG_T)

/* RFC 2960
 * Section 3.2.1 Optional/Variable-length Parmaeter Format.
 */

typedef struct sctp_paramhdr {
	__be16 type;
	__be16 length;
} __attribute__((packed)) sctp_paramhdr_t;

typedef enum {

	/* RFC 2960 Section 3.3.5 */
	SCTP_PARAM_HEARTBEAT_INFO		= cpu_to_be16(1),
	/* RFC 2960 Section 3.3.2.1 */
	SCTP_PARAM_IPV4_ADDRESS			= cpu_to_be16(5),
	SCTP_PARAM_IPV6_ADDRESS			= cpu_to_be16(6),
	SCTP_PARAM_STATE_COOKIE			= cpu_to_be16(7),
	SCTP_PARAM_UNRECOGNIZED_PARAMETERS	= cpu_to_be16(8),
	SCTP_PARAM_COOKIE_PRESERVATIVE		= cpu_to_be16(9),
	SCTP_PARAM_HOST_NAME_ADDRESS		= cpu_to_be16(11),
	SCTP_PARAM_SUPPORTED_ADDRESS_TYPES	= cpu_to_be16(12),
	SCTP_PARAM_ECN_CAPABLE			= cpu_to_be16(0x8000),

	/* AUTH Extension Section 3 */
	SCTP_PARAM_RANDOM			= cpu_to_be16(0x8002),
	SCTP_PARAM_CHUNKS			= cpu_to_be16(0x8003),
	SCTP_PARAM_HMAC_ALGO			= cpu_to_be16(0x8004),

	/* Add-IP: Supported Extensions, Section 4.2 */
	SCTP_PARAM_SUPPORTED_EXT	= cpu_to_be16(0x8008),

	/* PR-SCTP Sec 3.1 */
	SCTP_PARAM_FWD_TSN_SUPPORT	= cpu_to_be16(0xc000),

	/* Add-IP Extension. Section 3.2 */
	SCTP_PARAM_ADD_IP		= cpu_to_be16(0xc001),
	SCTP_PARAM_DEL_IP		= cpu_to_be16(0xc002),
	SCTP_PARAM_ERR_CAUSE		= cpu_to_be16(0xc003),
	SCTP_PARAM_SET_PRIMARY		= cpu_to_be16(0xc004),
	SCTP_PARAM_SUCCESS_REPORT	= cpu_to_be16(0xc005),
	SCTP_PARAM_ADAPTATION_LAYER_IND = cpu_to_be16(0xc006),

} sctp_param_t; /* enum */


/* RFC 2960 Section 3.2.1
 *  The Parameter Types are encoded such that the highest-order two bits
 *  specify the action that must be taken if the processing endpoint does
 *  not recognize the Parameter Type.
 *
 */
typedef enum {
	SCTP_PARAM_ACTION_DISCARD     = cpu_to_be16(0x0000),
	SCTP_PARAM_ACTION_DISCARD_ERR = cpu_to_be16(0x4000),
	SCTP_PARAM_ACTION_SKIP        = cpu_to_be16(0x8000),
	SCTP_PARAM_ACTION_SKIP_ERR    = cpu_to_be16(0xc000),
} sctp_param_action_t;

enum { SCTP_PARAM_ACTION_MASK = cpu_to_be16(0xc000), };

/* RFC 2960 Section 3.3.1 Payload Data (DATA) (0) */

typedef struct sctp_datahdr {
	__be32 tsn;
	__be16 stream;
	__be16 ssn;
	__be32 ppid;
	__u8  payload[0];
} __attribute__((packed)) sctp_datahdr_t;

typedef struct sctp_data_chunk {
        sctp_chunkhdr_t chunk_hdr;
        sctp_datahdr_t  data_hdr;
} __attribute__((packed)) sctp_data_chunk_t;

/* DATA Chuck Specific Flags */
enum {
	SCTP_DATA_MIDDLE_FRAG	= 0x00,
	SCTP_DATA_LAST_FRAG	= 0x01,
	SCTP_DATA_FIRST_FRAG	= 0x02,
	SCTP_DATA_NOT_FRAG	= 0x03,
	SCTP_DATA_UNORDERED	= 0x04,
};
enum { SCTP_DATA_FRAG_MASK = 0x03, };


/* RFC 2960 Section 3.3.2 Initiation (INIT) (1)
 *
 *  This chunk is used to initiate a SCTP association between two
 *  endpoints.
 */
typedef struct sctp_inithdr {
	__be32 init_tag;
	__be32 a_rwnd;
	__be16 num_outbound_streams;
	__be16 num_inbound_streams;
	__be32 initial_tsn;
	__u8  params[0];
} __attribute__((packed)) sctp_inithdr_t;

typedef struct sctp_init_chunk {
	sctp_chunkhdr_t chunk_hdr;
	sctp_inithdr_t init_hdr;
} __attribute__((packed)) sctp_init_chunk_t;


/* Section 3.3.2.1. IPv4 Address Parameter (5) */
typedef struct sctp_ipv4addr_param {
	sctp_paramhdr_t param_hdr;
	struct in_addr  addr;
} __attribute__((packed)) sctp_ipv4addr_param_t;

/* Section 3.3.2.1. IPv6 Address Parameter (6) */
typedef struct sctp_ipv6addr_param {
	sctp_paramhdr_t param_hdr;
	struct in6_addr addr;
} __attribute__((packed)) sctp_ipv6addr_param_t;

/* Section 3.3.2.1 Cookie Preservative (9) */
typedef struct sctp_cookie_preserve_param {
	sctp_paramhdr_t param_hdr;
	__be32          lifespan_increment;
} __attribute__((packed)) sctp_cookie_preserve_param_t;

/* Section 3.3.2.1 Host Name Address (11) */
typedef struct sctp_hostname_param {
	sctp_paramhdr_t param_hdr;
	uint8_t hostname[0];
} __attribute__((packed)) sctp_hostname_param_t;

/* Section 3.3.2.1 Supported Address Types (12) */
typedef struct sctp_supported_addrs_param {
	sctp_paramhdr_t param_hdr;
	__be16 types[0];
} __attribute__((packed)) sctp_supported_addrs_param_t;

/* Appendix A. ECN Capable (32768) */
typedef struct sctp_ecn_capable_param {
	sctp_paramhdr_t param_hdr;
} __attribute__((packed)) sctp_ecn_capable_param_t;

/* ADDIP Section 3.2.6 Adaptation Layer Indication */
typedef struct sctp_adaptation_ind_param {
	struct sctp_paramhdr param_hdr;
	__be32 adaptation_ind;
} __attribute__((packed)) sctp_adaptation_ind_param_t;

/* ADDIP Section 4.2.7 Supported Extensions Parameter */
typedef struct sctp_supported_ext_param {
	struct sctp_paramhdr param_hdr;
	__u8 chunks[0];
} __attribute__((packed)) sctp_supported_ext_param_t;

/* AUTH Section 3.1 Random */
typedef struct sctp_random_param {
	sctp_paramhdr_t param_hdr;
	__u8 random_val[0];
} __attribute__((packed)) sctp_random_param_t;

/* AUTH Section 3.2 Chunk List */
typedef struct sctp_chunks_param {
	sctp_paramhdr_t param_hdr;
	__u8 chunks[0];
} __attribute__((packed)) sctp_chunks_param_t;

/* AUTH Section 3.3 HMAC Algorithm */
typedef struct sctp_hmac_algo_param {
	sctp_paramhdr_t param_hdr;
	__be16 hmac_ids[0];
} __attribute__((packed)) sctp_hmac_algo_param_t;

/* RFC 2960.  Section 3.3.3 Initiation Acknowledgement (INIT ACK) (2):
 *   The INIT ACK chunk is used to acknowledge the initiation of an SCTP
 *   association.
 */
typedef sctp_init_chunk_t sctp_initack_chunk_t;

/* Section 3.3.3.1 State Cookie (7) */
typedef struct sctp_cookie_param {
	sctp_paramhdr_t p;
	__u8 body[0];
} __attribute__((packed)) sctp_cookie_param_t;

/* Section 3.3.3.1 Unrecognized Parameters (8) */
typedef struct sctp_unrecognized_param {
	sctp_paramhdr_t param_hdr;
	sctp_paramhdr_t unrecognized;
} __attribute__((packed)) sctp_unrecognized_param_t;



/*
 * 3.3.4 Selective Acknowledgement (SACK) (3):
 *
 *  This chunk is sent to the peer endpoint to acknowledge received DATA
 *  chunks and to inform the peer endpoint of gaps in the received
 *  subsequences of DATA chunks as represented by their TSNs.
 */

typedef struct sctp_gap_ack_block {
	__be16 start;
	__be16 end;
} __attribute__((packed)) sctp_gap_ack_block_t;

typedef __be32 sctp_dup_tsn_t;

typedef union {
	sctp_gap_ack_block_t	gab;
        sctp_dup_tsn_t		dup;
} sctp_sack_variable_t;

typedef struct sctp_sackhdr {
	__be32 cum_tsn_ack;
	__be32 a_rwnd;
	__be16 num_gap_ack_blocks;
	__be16 num_dup_tsns;
	sctp_sack_variable_t variable[0];
} __attribute__((packed)) sctp_sackhdr_t;

typedef struct sctp_sack_chunk {
	sctp_chunkhdr_t chunk_hdr;
	sctp_sackhdr_t sack_hdr;
} __attribute__((packed)) sctp_sack_chunk_t;


/* RFC 2960.  Section 3.3.5 Heartbeat Request (HEARTBEAT) (4):
 *
 *  An endpoint should send this chunk to its peer endpoint to probe the
 *  reachability of a particular destination transport address defined in
 *  the present association.
 */

typedef struct sctp_heartbeathdr {
	sctp_paramhdr_t info;
} __attribute__((packed)) sctp_heartbeathdr_t;

typedef struct sctp_heartbeat_chunk {
	sctp_chunkhdr_t chunk_hdr;
	sctp_heartbeathdr_t hb_hdr;
} __attribute__((packed)) sctp_heartbeat_chunk_t;


/* For the abort and shutdown ACK we must carry the init tag in the
 * common header. Just the common header is all that is needed with a
 * chunk descriptor.
 */
typedef struct sctp_abort_chunk {
        sctp_chunkhdr_t uh;
} __attribute__((packed)) sctp_abort_chunk_t;


/* For the graceful shutdown we must carry the tag (in common header)
 * and the highest consecutive acking value.
 */
typedef struct sctp_shutdownhdr {
	__be32 cum_tsn_ack;
} __attribute__((packed)) sctp_shutdownhdr_t;

struct sctp_shutdown_chunk_t {
        sctp_chunkhdr_t    chunk_hdr;
        sctp_shutdownhdr_t shutdown_hdr;
} __attribute__ ((packed));

/* RFC 2960.  Section 3.3.10 Operation Error (ERROR) (9) */

typedef struct sctp_errhdr {
	__be16 cause;
	__be16 length;
	__u8  variable[0];
} __attribute__((packed)) sctp_errhdr_t;

typedef struct sctp_operr_chunk {
        sctp_chunkhdr_t chunk_hdr;
	sctp_errhdr_t   err_hdr;
} __attribute__((packed)) sctp_operr_chunk_t;

/* RFC 2960 3.3.10 - Operation Error
 *
 * Cause Code: 16 bits (unsigned integer)
 *
 *     Defines the type of error conditions being reported.
 *    Cause Code
 *     Value           Cause Code
 *     ---------      ----------------
 *      1              Invalid Stream Identifier
 *      2              Missing Mandatory Parameter
 *      3              Stale Cookie Error
 *      4              Out of Resource
 *      5              Unresolvable Address
 *      6              Unrecognized Chunk Type
 *      7              Invalid Mandatory Parameter
 *      8              Unrecognized Parameters
 *      9              No User Data
 *     10              Cookie Received While Shutting Down
 */
typedef enum {

	SCTP_ERROR_NO_ERROR	   = cpu_to_be16(0x00),
	SCTP_ERROR_INV_STRM	   = cpu_to_be16(0x01),
	SCTP_ERROR_MISS_PARAM 	   = cpu_to_be16(0x02),
	SCTP_ERROR_STALE_COOKIE	   = cpu_to_be16(0x03),
	SCTP_ERROR_NO_RESOURCE 	   = cpu_to_be16(0x04),
	SCTP_ERROR_DNS_FAILED      = cpu_to_be16(0x05),
	SCTP_ERROR_UNKNOWN_CHUNK   = cpu_to_be16(0x06),
	SCTP_ERROR_INV_PARAM       = cpu_to_be16(0x07),
	SCTP_ERROR_UNKNOWN_PARAM   = cpu_to_be16(0x08),
	SCTP_ERROR_NO_DATA         = cpu_to_be16(0x09),
	SCTP_ERROR_COOKIE_IN_SHUTDOWN = cpu_to_be16(0x0a),


	/* SCTP Implementation Guide:
	 *  11  Restart of an association with new addresses
	 *  12  User Initiated Abort
	 *  13  Protocol Violation
	 */

	SCTP_ERROR_RESTART         = cpu_to_be16(0x0b),
	SCTP_ERROR_USER_ABORT      = cpu_to_be16(0x0c),
	SCTP_ERROR_PROTO_VIOLATION = cpu_to_be16(0x0d),

	/* ADDIP Section 3.3  New Error Causes
	 *
	 * Four new Error Causes are added to the SCTP Operational Errors,
	 * primarily for use in the ASCONF-ACK chunk.
	 *
	 * Value          Cause Code
	 * ---------      ----------------
	 * 0x0100          Request to Delete Last Remaining IP Address.
	 * 0x0101          Operation Refused Due to Resource Shortage.
	 * 0x0102          Request to Delete Source IP Address.
	 * 0x0103          Association Aborted due to illegal ASCONF-ACK
	 * 0x0104          Request refused - no authorization.
	 */
	SCTP_ERROR_DEL_LAST_IP	= cpu_to_be16(0x0100),
	SCTP_ERROR_RSRC_LOW	= cpu_to_be16(0x0101),
	SCTP_ERROR_DEL_SRC_IP	= cpu_to_be16(0x0102),
	SCTP_ERROR_ASCONF_ACK   = cpu_to_be16(0x0103),
	SCTP_ERROR_REQ_REFUSED	= cpu_to_be16(0x0104),

	/* AUTH Section 4.  New Error Cause
	 *
	 * This section defines a new error cause that will be sent if an AUTH
	 * chunk is received with an unsupported HMAC identifier.
	 * illustrates the new error cause.
	 *
	 * Cause Code      Error Cause Name
	 * --------------------------------------------------------------
	 * 0x0105          Unsupported HMAC Identifier
	 */
	 SCTP_ERROR_UNSUP_HMAC	= cpu_to_be16(0x0105)
} sctp_error_t;



/* RFC 2960.  Appendix A.  Explicit Congestion Notification.
 *   Explicit Congestion Notification Echo (ECNE) (12)
 */
typedef struct sctp_ecnehdr {
	__be32 lowest_tsn;
} sctp_ecnehdr_t;

typedef struct sctp_ecne_chunk {
	sctp_chunkhdr_t chunk_hdr;
	sctp_ecnehdr_t ence_hdr;
} __attribute__((packed)) sctp_ecne_chunk_t;

/* RFC 2960.  Appendix A.  Explicit Congestion Notification.
 *   Congestion Window Reduced (CWR) (13)
 */
typedef struct sctp_cwrhdr {
	__be32 lowest_tsn;
} sctp_cwrhdr_t;

typedef struct sctp_cwr_chunk {
	sctp_chunkhdr_t chunk_hdr;
	sctp_cwrhdr_t cwr_hdr;
} __attribute__((packed)) sctp_cwr_chunk_t;

/* PR-SCTP
 * 3.2 Forward Cumulative TSN Chunk Definition (FORWARD TSN)
 *
 * Forward Cumulative TSN chunk has the following format:
 *
 *        0                   1                   2                   3
 *        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *      |   Type = 192  |  Flags = 0x00 |        Length = Variable      |
 *      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *      |                      New Cumulative TSN                       |
 *      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *      |         Stream-1              |       Stream Sequence-1       |
 *      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *      \                                                               /
 *      /                                                               \
 *      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *      |         Stream-N              |       Stream Sequence-N       |
 *      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *      Chunk Flags:
 *
 *        Set to all zeros on transmit and ignored on receipt.
 *
 *      New Cumulative TSN: 32 bit u_int
 *
 *       This indicates the new cumulative TSN to the data receiver. Upon
 *       the reception of this value, the data receiver MUST consider
 *       any missing TSNs earlier than or equal to this value as received
 *       and stop reporting them as gaps in any subsequent SACKs.
 *
 *      Stream-N: 16 bit u_int
 *
 *       This field holds a stream number that was skipped by this
 *       FWD-TSN.
 *
 *      Stream Sequence-N: 16 bit u_int
 *       This field holds the sequence number associated with the stream
 *       that was skipped. The stream sequence field holds the largest stream
 *       sequence number in this stream being skipped.  The receiver of
 *       the FWD-TSN's can use the Stream-N and Stream Sequence-N fields
 *       to enable delivery of any stranded TSN's that remain on the stream
 *       re-ordering queues. This field MUST NOT report TSN's corresponding
 *       to DATA chunk that are marked as unordered. For ordered DATA
 *       chunks this field MUST be filled in.
 */
struct sctp_fwdtsn_skip {
	__be16 stream;
	__be16 ssn;
} __attribute__((packed));

struct sctp_fwdtsn_hdr {
	__be32 new_cum_tsn;
	struct sctp_fwdtsn_skip skip[0];
} __attribute((packed));

struct sctp_fwdtsn_chunk {
	struct sctp_chunkhdr chunk_hdr;
	struct sctp_fwdtsn_hdr fwdtsn_hdr;
} __attribute((packed));


/* ADDIP
 * Section 3.1.1 Address Configuration Change Chunk (ASCONF)
 *
 * 	Serial Number: 32 bits (unsigned integer)
 *	This value represents a Serial Number for the ASCONF Chunk. The
 *	valid range of Serial Number is from 0 to 2^32-1.
 *	Serial Numbers wrap back to 0 after reaching 2^32 -1.
 *
 *	Address Parameter: 8 or 20 bytes (depending on type)
 *	The address is an address of the sender of the ASCONF chunk,
 *	the address MUST be considered part of the association by the
 *	peer endpoint. This field may be used by the receiver of the 
 *	ASCONF to help in finding the association. This parameter MUST
 *	be present in every ASCONF message i.e. it is a mandatory TLV
 *	parameter.
 *
 *	ASCONF Parameter: TLV format
 *	Each Address configuration change is represented by a TLV
 *	parameter as defined in Section 3.2. One or more requests may
 *	be present in an ASCONF Chunk.
 *
 * Section 3.1.2 Address Configuration Acknowledgement Chunk (ASCONF-ACK)
 * 
 *	Serial Number: 32 bits (unsigned integer)
 *	This value represents the Serial Number for the received ASCONF
 *	Chunk that is acknowledged by this chunk. This value is copied
 *	from the received ASCONF Chunk. 
 *
 *	ASCONF Parameter Response: TLV format
 *	The ASCONF Parameter Response is used in the ASCONF-ACK to
 *	report status of ASCONF processing.
 */
typedef struct sctp_addip_param {
	sctp_paramhdr_t	param_hdr;
	__be32		crr_id;
} __attribute__((packed)) sctp_addip_param_t;

typedef struct sctp_addiphdr {
	__be32	serial;
	__u8	params[0];
} __attribute__((packed)) sctp_addiphdr_t;

typedef struct sctp_addip_chunk {
	sctp_chunkhdr_t chunk_hdr;
	sctp_addiphdr_t addip_hdr;
} __attribute__((packed)) sctp_addip_chunk_t;

/* AUTH
 * Section 4.1  Authentication Chunk (AUTH)
 *
 *   This chunk is used to hold the result of the HMAC calculation.
 *
 *    0                   1                   2                   3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   | Type = 0x0F   |   Flags=0     |             Length            |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |     Shared Key Identifier     |   HMAC Identifier             |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |                                                               |
 *   \                             HMAC                              /
 *   /                                                               \
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *   Type: 1 byte (unsigned integer)
 *   	This value MUST be set to 0x0F for  all AUTH-chunks.
 *
 *   Flags: 1 byte (unsigned integer)
 *	Set to zero on transmit and ignored on receipt.
 *
 *   Length: 2 bytes (unsigned integer)
 *   	This value holds the length of the HMAC in bytes plus 8.
 *
 *  Shared Key Identifier: 2 bytes (unsigned integer)
 *	This value describes which endpoint pair shared key is used.
 *
 *   HMAC Identifier: 2 bytes (unsigned integer)
 *   	This value describes which message digest is being used.  Table 2
 *	shows the currently defined values.
 *
 *    The following Table 2 shows the currently defined values for HMAC
 *       identifiers.
 *
 *	 +-----------------+--------------------------+
 *	 | HMAC Identifier | Message Digest Algorithm |
 *	 +-----------------+--------------------------+
 *	 | 0               | Reserved                 |
 *	 | 1               | SHA-1 defined in [8]     |
 *	 | 2               | Reserved                 |
 *	 | 3               | SHA-256 defined in [8]   |
 *	 +-----------------+--------------------------+
 *
 *
 *   HMAC: n bytes (unsigned integer) This hold the result of the HMAC
 *      calculation.
 */
typedef struct sctp_authhdr {
	__be16 shkey_id;
	__be16 hmac_id;
	__u8   hmac[0];
} __attribute__((packed)) sctp_authhdr_t;

typedef struct sctp_auth_chunk {
	sctp_chunkhdr_t chunk_hdr;
	sctp_authhdr_t auth_hdr;
} __attribute__((packed)) sctp_auth_chunk_t;

#endif /* __LINUX_SCTP_H__ */
