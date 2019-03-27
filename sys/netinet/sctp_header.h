/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2007, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _NETINET_SCTP_HEADER_H_
#define _NETINET_SCTP_HEADER_H_

#include <sys/time.h>
#include <netinet/sctp.h>
#include <netinet/sctp_constants.h>

#define SCTP_PACKED __attribute__((packed))

/*
 * Parameter structures
 */
struct sctp_ipv4addr_param {
	struct sctp_paramhdr ph;	/* type=SCTP_IPV4_PARAM_TYPE, len=8 */
	uint32_t addr;		/* IPV4 address */
}                   SCTP_PACKED;

#define SCTP_V6_ADDR_BYTES 16


struct sctp_ipv6addr_param {
	struct sctp_paramhdr ph;	/* type=SCTP_IPV6_PARAM_TYPE, len=20 */
	uint8_t addr[SCTP_V6_ADDR_BYTES];	/* IPV6 address */
}                   SCTP_PACKED;

/* Cookie Preservative */
struct sctp_cookie_perserve_param {
	struct sctp_paramhdr ph;	/* type=SCTP_COOKIE_PRESERVE, len=8 */
	uint32_t time;		/* time in ms to extend cookie */
}                          SCTP_PACKED;

#define SCTP_ARRAY_MIN_LEN 1
/* Host Name Address */
struct sctp_host_name_param {
	struct sctp_paramhdr ph;	/* type=SCTP_HOSTNAME_ADDRESS */
	char name[SCTP_ARRAY_MIN_LEN];	/* host name */
}                    SCTP_PACKED;

/*
 * This is the maximum padded size of a s-a-p
 * so paramheadr + 3 address types (6 bytes) + 2 byte pad = 12
 */
#define SCTP_MAX_ADDR_PARAMS_SIZE 12
/* supported address type */
struct sctp_supported_addr_param {
	struct sctp_paramhdr ph;	/* type=SCTP_SUPPORTED_ADDRTYPE */
	uint16_t addr_type[2];	/* array of supported address types */
}                         SCTP_PACKED;

/* heartbeat info parameter */
struct sctp_heartbeat_info_param {
	struct sctp_paramhdr ph;
	uint32_t time_value_1;
	uint32_t time_value_2;
	uint32_t random_value1;
	uint32_t random_value2;
	uint8_t addr_family;
	uint8_t addr_len;
	/* make sure that this structure is 4 byte aligned */
	uint8_t padding[2];
	char address[SCTP_ADDRMAX];
}                         SCTP_PACKED;


/* draft-ietf-tsvwg-prsctp */
/* PR-SCTP supported parameter */
struct sctp_prsctp_supported_param {
	struct sctp_paramhdr ph;
}                           SCTP_PACKED;


/* draft-ietf-tsvwg-addip-sctp */
struct sctp_asconf_paramhdr {	/* an ASCONF "parameter" */
	struct sctp_paramhdr ph;	/* a SCTP parameter header */
	uint32_t correlation_id;	/* correlation id for this param */
}                    SCTP_PACKED;

struct sctp_asconf_addr_param {	/* an ASCONF address parameter */
	struct sctp_asconf_paramhdr aph;	/* asconf "parameter" */
	struct sctp_ipv6addr_param addrp;	/* max storage size */
}                      SCTP_PACKED;


struct sctp_asconf_tag_param {	/* an ASCONF NAT-Vtag parameter */
	struct sctp_asconf_paramhdr aph;	/* asconf "parameter" */
	uint32_t local_vtag;
	uint32_t remote_vtag;
}                     SCTP_PACKED;


struct sctp_asconf_addrv4_param {	/* an ASCONF address (v4) parameter */
	struct sctp_asconf_paramhdr aph;	/* asconf "parameter" */
	struct sctp_ipv4addr_param addrp;	/* max storage size */
}                        SCTP_PACKED;

#define SCTP_MAX_SUPPORTED_EXT 256

struct sctp_supported_chunk_types_param {
	struct sctp_paramhdr ph;	/* type = 0x8008  len = x */
	uint8_t chunk_types[];
}                                SCTP_PACKED;


/*
 * Structures for DATA chunks
 */
struct sctp_data {
	uint32_t tsn;
	uint16_t sid;
	uint16_t ssn;
	uint32_t ppid;
	/* user data follows */
}         SCTP_PACKED;

struct sctp_data_chunk {
	struct sctp_chunkhdr ch;
	struct sctp_data dp;
}               SCTP_PACKED;

struct sctp_idata {
	uint32_t tsn;
	uint16_t sid;
	uint16_t reserved;	/* Where does the SSN go? */
	uint32_t mid;
	union {
		uint32_t ppid;
		uint32_t fsn;	/* Fragment Sequence Number */
	}     ppid_fsn;
	/* user data follows */
}          SCTP_PACKED;

struct sctp_idata_chunk {
	struct sctp_chunkhdr ch;
	struct sctp_idata dp;
}                SCTP_PACKED;

/*
 * Structures for the control chunks
 */

/* Initiate (INIT)/Initiate Ack (INIT ACK) */
struct sctp_init {
	uint32_t initiate_tag;	/* initiate tag */
	uint32_t a_rwnd;	/* a_rwnd */
	uint16_t num_outbound_streams;	/* OS */
	uint16_t num_inbound_streams;	/* MIS */
	uint32_t initial_tsn;	/* I-TSN */
	/* optional param's follow */
}         SCTP_PACKED;
#define SCTP_IDENTIFICATION_SIZE 16
#define SCTP_ADDRESS_SIZE 4
#define SCTP_RESERVE_SPACE 6
/* state cookie header */
struct sctp_state_cookie {	/* this is our definition... */
	uint8_t identification[SCTP_IDENTIFICATION_SIZE];	/* id of who we are */
	struct timeval time_entered;	/* the time I built cookie */
	uint32_t cookie_life;	/* life I will award this cookie */
	uint32_t tie_tag_my_vtag;	/* my tag in old association */

	uint32_t tie_tag_peer_vtag;	/* peers tag in old association */
	uint32_t peers_vtag;	/* peers tag in INIT (for quick ref) */

	uint32_t my_vtag;	/* my tag in INIT-ACK (for quick ref) */
	uint32_t address[SCTP_ADDRESS_SIZE];	/* 4 ints/128 bits */
	uint32_t addr_type;	/* address type */
	uint32_t laddress[SCTP_ADDRESS_SIZE];	/* my local from address */
	uint32_t laddr_type;	/* my local from address type */
	uint32_t scope_id;	/* v6 scope id for link-locals */

	uint16_t peerport;	/* port address of the peer in the INIT */
	uint16_t myport;	/* my port address used in the INIT */
	uint8_t ipv4_addr_legal;	/* Are V4 addr legal? */
	uint8_t ipv6_addr_legal;	/* Are V6 addr legal? */
	uint8_t local_scope;	/* IPv6 local scope flag */
	uint8_t site_scope;	/* IPv6 site scope flag */

	uint8_t ipv4_scope;	/* IPv4 private addr scope */
	uint8_t loopback_scope;	/* loopback scope information */
	uint8_t reserved[SCTP_RESERVE_SPACE];	/* Align to 64 bits */
	/*
	 * at the end is tacked on the INIT chunk and the INIT-ACK chunk
	 * (minus the cookie).
	 */
}                 SCTP_PACKED;

/* state cookie parameter */
struct sctp_state_cookie_param {
	struct sctp_paramhdr ph;
	struct sctp_state_cookie cookie;
}                       SCTP_PACKED;

struct sctp_init_chunk {
	struct sctp_chunkhdr ch;
	struct sctp_init init;
}               SCTP_PACKED;

struct sctp_init_msg {
	struct sctphdr sh;
	struct sctp_init_chunk msg;
}             SCTP_PACKED;

/* ... used for both INIT and INIT ACK */
#define sctp_init_ack		sctp_init
#define sctp_init_ack_chunk	sctp_init_chunk
#define sctp_init_ack_msg	sctp_init_msg


/* Selective Ack (SACK) */
struct sctp_gap_ack_block {
	uint16_t start;		/* Gap Ack block start */
	uint16_t end;		/* Gap Ack block end */
}                  SCTP_PACKED;

struct sctp_sack {
	uint32_t cum_tsn_ack;	/* cumulative TSN Ack */
	uint32_t a_rwnd;	/* updated a_rwnd of sender */
	uint16_t num_gap_ack_blks;	/* number of Gap Ack blocks */
	uint16_t num_dup_tsns;	/* number of duplicate TSNs */
	/* struct sctp_gap_ack_block's follow */
	/* uint32_t duplicate_tsn's follow */
}         SCTP_PACKED;

struct sctp_sack_chunk {
	struct sctp_chunkhdr ch;
	struct sctp_sack sack;
}               SCTP_PACKED;

struct sctp_nr_sack {
	uint32_t cum_tsn_ack;	/* cumulative TSN Ack */
	uint32_t a_rwnd;	/* updated a_rwnd of sender */
	uint16_t num_gap_ack_blks;	/* number of Gap Ack blocks */
	uint16_t num_nr_gap_ack_blks;	/* number of NR Gap Ack blocks */
	uint16_t num_dup_tsns;	/* number of duplicate TSNs */
	uint16_t reserved;	/* not currently used */
	/* struct sctp_gap_ack_block's follow */
	/* uint32_t duplicate_tsn's follow */
}            SCTP_PACKED;

struct sctp_nr_sack_chunk {
	struct sctp_chunkhdr ch;
	struct sctp_nr_sack nr_sack;
}                  SCTP_PACKED;


/* Heartbeat Request (HEARTBEAT) */
struct sctp_heartbeat {
	struct sctp_heartbeat_info_param hb_info;
}              SCTP_PACKED;

struct sctp_heartbeat_chunk {
	struct sctp_chunkhdr ch;
	struct sctp_heartbeat heartbeat;
}                    SCTP_PACKED;

/* ... used for Heartbeat Ack (HEARTBEAT ACK) */
#define sctp_heartbeat_ack		sctp_heartbeat
#define sctp_heartbeat_ack_chunk	sctp_heartbeat_chunk


/* Abort Asssociation (ABORT) */
struct sctp_abort_chunk {
	struct sctp_chunkhdr ch;
	/* optional error cause may follow */
}                SCTP_PACKED;

struct sctp_abort_msg {
	struct sctphdr sh;
	struct sctp_abort_chunk msg;
}              SCTP_PACKED;


/* Shutdown Association (SHUTDOWN) */
struct sctp_shutdown_chunk {
	struct sctp_chunkhdr ch;
	uint32_t cumulative_tsn_ack;
}                   SCTP_PACKED;


/* Shutdown Acknowledgment (SHUTDOWN ACK) */
struct sctp_shutdown_ack_chunk {
	struct sctp_chunkhdr ch;
}                       SCTP_PACKED;


/* Operation Error (ERROR) */
struct sctp_error_chunk {
	struct sctp_chunkhdr ch;
	/* optional error causes follow */
}                SCTP_PACKED;


/* Cookie Echo (COOKIE ECHO) */
struct sctp_cookie_echo_chunk {
	struct sctp_chunkhdr ch;
	struct sctp_state_cookie cookie;
}                      SCTP_PACKED;

/* Cookie Acknowledgment (COOKIE ACK) */
struct sctp_cookie_ack_chunk {
	struct sctp_chunkhdr ch;
}                     SCTP_PACKED;

/* Explicit Congestion Notification Echo (ECNE) */
struct old_sctp_ecne_chunk {
	struct sctp_chunkhdr ch;
	uint32_t tsn;
}                   SCTP_PACKED;

struct sctp_ecne_chunk {
	struct sctp_chunkhdr ch;
	uint32_t tsn;
	uint32_t num_pkts_since_cwr;
}               SCTP_PACKED;

/* Congestion Window Reduced (CWR) */
struct sctp_cwr_chunk {
	struct sctp_chunkhdr ch;
	uint32_t tsn;
}              SCTP_PACKED;

/* Shutdown Complete (SHUTDOWN COMPLETE) */
struct sctp_shutdown_complete_chunk {
	struct sctp_chunkhdr ch;
}                            SCTP_PACKED;

struct sctp_adaptation_layer_indication {
	struct sctp_paramhdr ph;
	uint32_t indication;
}                                SCTP_PACKED;

/*
 * draft-ietf-tsvwg-addip-sctp
 */
/* Address/Stream Configuration Change (ASCONF) */
struct sctp_asconf_chunk {
	struct sctp_chunkhdr ch;
	uint32_t serial_number;
	/* lookup address parameter (mandatory) */
	/* asconf parameters follow */
}                 SCTP_PACKED;

/* Address/Stream Configuration Acknowledge (ASCONF ACK) */
struct sctp_asconf_ack_chunk {
	struct sctp_chunkhdr ch;
	uint32_t serial_number;
	/* asconf parameters follow */
}                     SCTP_PACKED;

/* draft-ietf-tsvwg-prsctp */
/* Forward Cumulative TSN (FORWARD TSN) */
struct sctp_forward_tsn_chunk {
	struct sctp_chunkhdr ch;
	uint32_t new_cumulative_tsn;
	/* stream/sequence pairs (sctp_strseq) follow */
}                      SCTP_PACKED;

struct sctp_strseq {
	uint16_t sid;
	uint16_t ssn;
}           SCTP_PACKED;

struct sctp_strseq_mid {
	uint16_t sid;
	uint16_t flags;
	uint32_t mid;
};

struct sctp_forward_tsn_msg {
	struct sctphdr sh;
	struct sctp_forward_tsn_chunk msg;
}                    SCTP_PACKED;

/* should be a multiple of 4 - 1 aka 3/7/11 etc. */

#define SCTP_NUM_DB_TO_VERIFY 31

struct sctp_chunk_desc {
	uint8_t chunk_type;
	uint8_t data_bytes[SCTP_NUM_DB_TO_VERIFY];
	uint32_t tsn_ifany;
}               SCTP_PACKED;


struct sctp_pktdrop_chunk {
	struct sctp_chunkhdr ch;
	uint32_t bottle_bw;
	uint32_t current_onq;
	uint16_t trunc_len;
	uint16_t reserved;
	uint8_t data[];
}                  SCTP_PACKED;

/**********STREAM RESET STUFF ******************/

struct sctp_stream_reset_request {
	struct sctp_paramhdr ph;
	uint32_t request_seq;
}                         SCTP_PACKED;

struct sctp_stream_reset_out_request {
	struct sctp_paramhdr ph;
	uint32_t request_seq;	/* monotonically increasing seq no */
	uint32_t response_seq;	/* if a response, the resp seq no */
	uint32_t send_reset_at_tsn;	/* last TSN I assigned outbound */
	uint16_t list_of_streams[];	/* if not all list of streams */
}                             SCTP_PACKED;

struct sctp_stream_reset_in_request {
	struct sctp_paramhdr ph;
	uint32_t request_seq;
	uint16_t list_of_streams[];	/* if not all list of streams */
}                            SCTP_PACKED;

struct sctp_stream_reset_tsn_request {
	struct sctp_paramhdr ph;
	uint32_t request_seq;
}                             SCTP_PACKED;

struct sctp_stream_reset_response {
	struct sctp_paramhdr ph;
	uint32_t response_seq;	/* if a response, the resp seq no */
	uint32_t result;
}                          SCTP_PACKED;

struct sctp_stream_reset_response_tsn {
	struct sctp_paramhdr ph;
	uint32_t response_seq;	/* if a response, the resp seq no */
	uint32_t result;
	uint32_t senders_next_tsn;
	uint32_t receivers_next_tsn;
}                              SCTP_PACKED;

struct sctp_stream_reset_add_strm {
	struct sctp_paramhdr ph;
	uint32_t request_seq;
	uint16_t number_of_streams;
	uint16_t reserved;
}                          SCTP_PACKED;

#define SCTP_STREAM_RESET_RESULT_NOTHING_TO_DO   0x00000000	/* XXX: unused */
#define SCTP_STREAM_RESET_RESULT_PERFORMED       0x00000001
#define SCTP_STREAM_RESET_RESULT_DENIED          0x00000002
#define SCTP_STREAM_RESET_RESULT_ERR__WRONG_SSN  0x00000003	/* XXX: unused */
#define SCTP_STREAM_RESET_RESULT_ERR_IN_PROGRESS 0x00000004
#define SCTP_STREAM_RESET_RESULT_ERR_BAD_SEQNO   0x00000005
#define SCTP_STREAM_RESET_RESULT_IN_PROGRESS     0x00000006	/* XXX: unused */

/*
 * convience structures, note that if you are making a request for specific
 * streams then the request will need to be an overlay structure.
 */

struct sctp_stream_reset_tsn_req {
	struct sctp_chunkhdr ch;
	struct sctp_stream_reset_tsn_request sr_req;
}                         SCTP_PACKED;

struct sctp_stream_reset_resp {
	struct sctp_chunkhdr ch;
	struct sctp_stream_reset_response sr_resp;
}                      SCTP_PACKED;

/* respone only valid with a TSN request */
struct sctp_stream_reset_resp_tsn {
	struct sctp_chunkhdr ch;
	struct sctp_stream_reset_response_tsn sr_resp;
}                          SCTP_PACKED;

/****************************************************/

/*
 * Authenticated chunks support draft-ietf-tsvwg-sctp-auth
 */

/* Should we make the max be 32? */
#define SCTP_RANDOM_MAX_SIZE 256
struct sctp_auth_random {
	struct sctp_paramhdr ph;	/* type = 0x8002 */
	uint8_t random_data[];
}                SCTP_PACKED;

struct sctp_auth_chunk_list {
	struct sctp_paramhdr ph;	/* type = 0x8003 */
	uint8_t chunk_types[];
}                    SCTP_PACKED;

struct sctp_auth_hmac_algo {
	struct sctp_paramhdr ph;	/* type = 0x8004 */
	uint16_t hmac_ids[];
}                   SCTP_PACKED;

struct sctp_auth_chunk {
	struct sctp_chunkhdr ch;
	uint16_t shared_key_id;
	uint16_t hmac_id;
	uint8_t hmac[];
}               SCTP_PACKED;

/*
 * we pre-reserve enough room for a ECNE or CWR AND a SACK with no missing
 * pieces. If ENCE is missing we could have a couple of blocks. This way we
 * optimize so we MOST likely can bundle a SACK/ECN with the smallest size
 * data chunk I will split into. We could increase throughput slightly by
 * taking out these two but the  24-sack/8-CWR i.e. 32 bytes I pre-reserve I
 * feel is worth it for now.
 */
#ifndef SCTP_MAX_OVERHEAD
#ifdef INET6
#define SCTP_MAX_OVERHEAD (sizeof(struct sctp_data_chunk) + \
			   sizeof(struct sctphdr) + \
			   sizeof(struct sctp_ecne_chunk) + \
			   sizeof(struct sctp_sack_chunk) + \
			   sizeof(struct ip6_hdr))

#define SCTP_MED_OVERHEAD (sizeof(struct sctp_data_chunk) + \
			   sizeof(struct sctphdr) + \
			   sizeof(struct ip6_hdr))


#define SCTP_MIN_OVERHEAD (sizeof(struct ip6_hdr) + \
			   sizeof(struct sctphdr))

#else
#define SCTP_MAX_OVERHEAD (sizeof(struct sctp_data_chunk) + \
			   sizeof(struct sctphdr) + \
			   sizeof(struct sctp_ecne_chunk) + \
			   sizeof(struct sctp_sack_chunk) + \
			   sizeof(struct ip))

#define SCTP_MED_OVERHEAD (sizeof(struct sctp_data_chunk) + \
			   sizeof(struct sctphdr) + \
			   sizeof(struct ip))


#define SCTP_MIN_OVERHEAD (sizeof(struct ip) + \
			   sizeof(struct sctphdr))

#endif				/* INET6 */
#endif				/* !SCTP_MAX_OVERHEAD */

#define SCTP_MED_V4_OVERHEAD (sizeof(struct sctp_data_chunk) + \
			      sizeof(struct sctphdr) + \
			      sizeof(struct ip))

#define SCTP_MIN_V4_OVERHEAD (sizeof(struct ip) + \
			      sizeof(struct sctphdr))

#undef SCTP_PACKED
#endif				/* !__sctp_header_h__ */
