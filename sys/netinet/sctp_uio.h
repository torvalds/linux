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

#ifndef _NETINET_SCTP_UIO_H_
#define _NETINET_SCTP_UIO_H_


#if ! defined(_KERNEL)
#include <stdint.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

typedef uint32_t sctp_assoc_t;

#define SCTP_FUTURE_ASSOC  0
#define SCTP_CURRENT_ASSOC 1
#define SCTP_ALL_ASSOC     2

struct sctp_event {
	sctp_assoc_t se_assoc_id;
	uint16_t se_type;
	uint8_t se_on;
};

/* Compatibility to previous define's */
#define sctp_stream_reset_events sctp_stream_reset_event

/* On/Off setup for subscription to events */
struct sctp_event_subscribe {
	uint8_t sctp_data_io_event;
	uint8_t sctp_association_event;
	uint8_t sctp_address_event;
	uint8_t sctp_send_failure_event;
	uint8_t sctp_peer_error_event;
	uint8_t sctp_shutdown_event;
	uint8_t sctp_partial_delivery_event;
	uint8_t sctp_adaptation_layer_event;
	uint8_t sctp_authentication_event;
	uint8_t sctp_sender_dry_event;
	uint8_t sctp_stream_reset_event;
};

/* ancillary data types */
#define SCTP_INIT	0x0001
#define SCTP_SNDRCV	0x0002
#define SCTP_EXTRCV	0x0003
#define SCTP_SNDINFO    0x0004
#define SCTP_RCVINFO    0x0005
#define SCTP_NXTINFO    0x0006
#define SCTP_PRINFO     0x0007
#define SCTP_AUTHINFO   0x0008
#define SCTP_DSTADDRV4  0x0009
#define SCTP_DSTADDRV6  0x000a

/*
 * ancillary data structures
 */
struct sctp_initmsg {
	uint16_t sinit_num_ostreams;
	uint16_t sinit_max_instreams;
	uint16_t sinit_max_attempts;
	uint16_t sinit_max_init_timeo;
};

/* We add 96 bytes to the size of sctp_sndrcvinfo.
 * This makes the current structure 128 bytes long
 * which is nicely 64 bit aligned but also has room
 * for us to add more and keep ABI compatibility.
 * For example, already we have the sctp_extrcvinfo
 * when enabled which is 48 bytes.
 */

/*
 * The assoc up needs a verfid
 * all sendrcvinfo's need a verfid for SENDING only.
 */


#define SCTP_ALIGN_RESV_PAD 92
#define SCTP_ALIGN_RESV_PAD_SHORT 76

struct sctp_sndrcvinfo {
	uint16_t sinfo_stream;
	uint16_t sinfo_ssn;
	uint16_t sinfo_flags;
	uint32_t sinfo_ppid;
	uint32_t sinfo_context;
	uint32_t sinfo_timetolive;
	uint32_t sinfo_tsn;
	uint32_t sinfo_cumtsn;
	sctp_assoc_t sinfo_assoc_id;
	uint16_t sinfo_keynumber;
	uint16_t sinfo_keynumber_valid;
	uint8_t __reserve_pad[SCTP_ALIGN_RESV_PAD];
};

struct sctp_extrcvinfo {
	uint16_t sinfo_stream;
	uint16_t sinfo_ssn;
	uint16_t sinfo_flags;
	uint32_t sinfo_ppid;
	uint32_t sinfo_context;
	uint32_t sinfo_timetolive;	/* should have been sinfo_pr_value */
	uint32_t sinfo_tsn;
	uint32_t sinfo_cumtsn;
	sctp_assoc_t sinfo_assoc_id;
	uint16_t serinfo_next_flags;
	uint16_t serinfo_next_stream;
	uint32_t serinfo_next_aid;
	uint32_t serinfo_next_length;
	uint32_t serinfo_next_ppid;
	uint16_t sinfo_keynumber;
	uint16_t sinfo_keynumber_valid;
	uint8_t __reserve_pad[SCTP_ALIGN_RESV_PAD_SHORT];
};
#define sinfo_pr_value sinfo_timetolive
#define sreinfo_next_flags serinfo_next_flags
#define sreinfo_next_stream serinfo_next_stream
#define sreinfo_next_aid serinfo_next_aid
#define sreinfo_next_length serinfo_next_length
#define sreinfo_next_ppid serinfo_next_ppid

struct sctp_sndinfo {
	uint16_t snd_sid;
	uint16_t snd_flags;
	uint32_t snd_ppid;
	uint32_t snd_context;
	sctp_assoc_t snd_assoc_id;
};

struct sctp_prinfo {
	uint16_t pr_policy;
	uint32_t pr_value;
};

struct sctp_default_prinfo {
	uint16_t pr_policy;
	uint32_t pr_value;
	sctp_assoc_t pr_assoc_id;
};

struct sctp_authinfo {
	uint16_t auth_keynumber;
};

struct sctp_rcvinfo {
	uint16_t rcv_sid;
	uint16_t rcv_ssn;
	uint16_t rcv_flags;
	uint32_t rcv_ppid;
	uint32_t rcv_tsn;
	uint32_t rcv_cumtsn;
	uint32_t rcv_context;
	sctp_assoc_t rcv_assoc_id;
};

struct sctp_nxtinfo {
	uint16_t nxt_sid;
	uint16_t nxt_flags;
	uint32_t nxt_ppid;
	uint32_t nxt_length;
	sctp_assoc_t nxt_assoc_id;
};

#define SCTP_NO_NEXT_MSG           0x0000
#define SCTP_NEXT_MSG_AVAIL        0x0001
#define SCTP_NEXT_MSG_ISCOMPLETE   0x0002
#define SCTP_NEXT_MSG_IS_UNORDERED 0x0004
#define SCTP_NEXT_MSG_IS_NOTIFICATION 0x0008

struct sctp_recvv_rn {
	struct sctp_rcvinfo recvv_rcvinfo;
	struct sctp_nxtinfo recvv_nxtinfo;
};

#define SCTP_RECVV_NOINFO  0
#define SCTP_RECVV_RCVINFO 1
#define SCTP_RECVV_NXTINFO 2
#define SCTP_RECVV_RN      3

#define SCTP_SENDV_NOINFO   0
#define SCTP_SENDV_SNDINFO  1
#define SCTP_SENDV_PRINFO   2
#define SCTP_SENDV_AUTHINFO 3
#define SCTP_SENDV_SPA      4

struct sctp_sendv_spa {
	uint32_t sendv_flags;
	struct sctp_sndinfo sendv_sndinfo;
	struct sctp_prinfo sendv_prinfo;
	struct sctp_authinfo sendv_authinfo;
};

#define SCTP_SEND_SNDINFO_VALID  0x00000001
#define SCTP_SEND_PRINFO_VALID   0x00000002
#define SCTP_SEND_AUTHINFO_VALID 0x00000004

struct sctp_snd_all_completes {
	uint16_t sall_stream;
	uint16_t sall_flags;
	uint32_t sall_ppid;
	uint32_t sall_context;
	uint32_t sall_num_sent;
	uint32_t sall_num_failed;
};

/* Flags that go into the sinfo->sinfo_flags field */
#define SCTP_NOTIFICATION     0x0010	/* next message is a notification */
#define SCTP_COMPLETE         0x0020	/* next message is complete */
#define SCTP_EOF              0x0100	/* Start shutdown procedures */
#define SCTP_ABORT            0x0200	/* Send an ABORT to peer */
#define SCTP_UNORDERED        0x0400	/* Message is un-ordered */
#define SCTP_ADDR_OVER        0x0800	/* Override the primary-address */
#define SCTP_SENDALL          0x1000	/* Send this on all associations */
#define SCTP_EOR              0x2000	/* end of message signal */
#define SCTP_SACK_IMMEDIATELY 0x4000	/* Set I-Bit */

#define INVALID_SINFO_FLAG(x) (((x) & 0xfffffff0 \
                                    & ~(SCTP_EOF | SCTP_ABORT | SCTP_UNORDERED |\
				        SCTP_ADDR_OVER | SCTP_SENDALL | SCTP_EOR |\
					SCTP_SACK_IMMEDIATELY)) != 0)
/* for the endpoint */

/* The lower four bits is an enumeration of PR-SCTP policies */
#define SCTP_PR_SCTP_NONE 0x0000	/* Reliable transfer */
#define SCTP_PR_SCTP_TTL  0x0001	/* Time based PR-SCTP */
#define SCTP_PR_SCTP_PRIO 0x0002	/* Buffer based PR-SCTP */
#define SCTP_PR_SCTP_BUF  SCTP_PR_SCTP_PRIO	/* For backwards compatibility */
#define SCTP_PR_SCTP_RTX  0x0003	/* Number of retransmissions based
					 * PR-SCTP */
#define SCTP_PR_SCTP_MAX  SCTP_PR_SCTP_RTX
#define SCTP_PR_SCTP_ALL  0x000f	/* Used for aggregated stats */

#define PR_SCTP_POLICY(x)         ((x) & 0x0f)
#define PR_SCTP_ENABLED(x)        ((PR_SCTP_POLICY(x) != SCTP_PR_SCTP_NONE) && \
                                   (PR_SCTP_POLICY(x) != SCTP_PR_SCTP_ALL))
#define PR_SCTP_TTL_ENABLED(x)    (PR_SCTP_POLICY(x) == SCTP_PR_SCTP_TTL)
#define PR_SCTP_BUF_ENABLED(x)    (PR_SCTP_POLICY(x) == SCTP_PR_SCTP_BUF)
#define PR_SCTP_RTX_ENABLED(x)    (PR_SCTP_POLICY(x) == SCTP_PR_SCTP_RTX)
#define PR_SCTP_INVALID_POLICY(x) (PR_SCTP_POLICY(x) > SCTP_PR_SCTP_MAX)
#define PR_SCTP_VALID_POLICY(x)   (PR_SCTP_POLICY(x) <= SCTP_PR_SCTP_MAX)

/* Stat's */
struct sctp_pcbinfo {
	uint32_t ep_count;
	uint32_t asoc_count;
	uint32_t laddr_count;
	uint32_t raddr_count;
	uint32_t chk_count;
	uint32_t readq_count;
	uint32_t free_chunks;
	uint32_t stream_oque;
};

struct sctp_sockstat {
	sctp_assoc_t ss_assoc_id;
	uint32_t ss_total_sndbuf;
	uint32_t ss_total_recv_buf;
};

/*
 * notification event structures
 */

/*
 * association change event
 */
struct sctp_assoc_change {
	uint16_t sac_type;
	uint16_t sac_flags;
	uint32_t sac_length;
	uint16_t sac_state;
	uint16_t sac_error;
	uint16_t sac_outbound_streams;
	uint16_t sac_inbound_streams;
	sctp_assoc_t sac_assoc_id;
	uint8_t sac_info[];
};

/* sac_state values */
#define SCTP_COMM_UP            0x0001
#define SCTP_COMM_LOST          0x0002
#define SCTP_RESTART            0x0003
#define SCTP_SHUTDOWN_COMP      0x0004
#define SCTP_CANT_STR_ASSOC     0x0005

/* sac_info values */
#define SCTP_ASSOC_SUPPORTS_PR			0x01
#define SCTP_ASSOC_SUPPORTS_AUTH		0x02
#define SCTP_ASSOC_SUPPORTS_ASCONF		0x03
#define SCTP_ASSOC_SUPPORTS_MULTIBUF		0x04
#define SCTP_ASSOC_SUPPORTS_RE_CONFIG		0x05
#define SCTP_ASSOC_SUPPORTS_INTERLEAVING	0x06
#define SCTP_ASSOC_SUPPORTS_MAX			0x06
/*
 * Address event
 */
struct sctp_paddr_change {
	uint16_t spc_type;
	uint16_t spc_flags;
	uint32_t spc_length;
	struct sockaddr_storage spc_aaddr;
	uint32_t spc_state;
	uint32_t spc_error;
	sctp_assoc_t spc_assoc_id;
};

/* paddr state values */
#define SCTP_ADDR_AVAILABLE	0x0001
#define SCTP_ADDR_UNREACHABLE	0x0002
#define SCTP_ADDR_REMOVED	0x0003
#define SCTP_ADDR_ADDED		0x0004
#define SCTP_ADDR_MADE_PRIM	0x0005
#define SCTP_ADDR_CONFIRMED	0x0006

#define SCTP_ACTIVE		0x0001	/* SCTP_ADDR_REACHABLE */
#define SCTP_INACTIVE		0x0002	/* neither SCTP_ADDR_REACHABLE nor
					 * SCTP_ADDR_UNCONFIRMED */
#define SCTP_UNCONFIRMED	0x0200	/* SCTP_ADDR_UNCONFIRMED */

/* remote error events */
struct sctp_remote_error {
	uint16_t sre_type;
	uint16_t sre_flags;
	uint32_t sre_length;
	uint16_t sre_error;
	sctp_assoc_t sre_assoc_id;
	uint8_t sre_data[];
};

/* data send failure event (deprecated) */
struct sctp_send_failed {
	uint16_t ssf_type;
	uint16_t ssf_flags;
	uint32_t ssf_length;
	uint32_t ssf_error;
	struct sctp_sndrcvinfo ssf_info;
	sctp_assoc_t ssf_assoc_id;
	uint8_t ssf_data[];
};

/* data send failure event (not deprecated) */
struct sctp_send_failed_event {
	uint16_t ssfe_type;
	uint16_t ssfe_flags;
	uint32_t ssfe_length;
	uint32_t ssfe_error;
	struct sctp_sndinfo ssfe_info;
	sctp_assoc_t ssfe_assoc_id;
	uint8_t ssfe_data[];
};

/* flag that indicates state of data */
#define SCTP_DATA_UNSENT	0x0001	/* inqueue never on wire */
#define SCTP_DATA_SENT		0x0002	/* on wire at failure */

/* shutdown event */
struct sctp_shutdown_event {
	uint16_t sse_type;
	uint16_t sse_flags;
	uint32_t sse_length;
	sctp_assoc_t sse_assoc_id;
};

/* Adaptation layer indication stuff */
struct sctp_adaptation_event {
	uint16_t sai_type;
	uint16_t sai_flags;
	uint32_t sai_length;
	uint32_t sai_adaptation_ind;
	sctp_assoc_t sai_assoc_id;
};

struct sctp_setadaptation {
	uint32_t ssb_adaptation_ind;
};

/* compatible old spelling */
struct sctp_adaption_event {
	uint16_t sai_type;
	uint16_t sai_flags;
	uint32_t sai_length;
	uint32_t sai_adaption_ind;
	sctp_assoc_t sai_assoc_id;
};

struct sctp_setadaption {
	uint32_t ssb_adaption_ind;
};


/*
 * Partial Delivery API event
 */
struct sctp_pdapi_event {
	uint16_t pdapi_type;
	uint16_t pdapi_flags;
	uint32_t pdapi_length;
	uint32_t pdapi_indication;
	uint16_t pdapi_stream;
	uint16_t pdapi_seq;
	sctp_assoc_t pdapi_assoc_id;
};

/* indication values */
#define SCTP_PARTIAL_DELIVERY_ABORTED	0x0001


/*
 * authentication key event
 */
struct sctp_authkey_event {
	uint16_t auth_type;
	uint16_t auth_flags;
	uint32_t auth_length;
	uint16_t auth_keynumber;
	uint16_t auth_altkeynumber;
	uint32_t auth_indication;
	sctp_assoc_t auth_assoc_id;
};

/* indication values */
#define SCTP_AUTH_NEW_KEY	0x0001
#define SCTP_AUTH_NEWKEY	SCTP_AUTH_NEW_KEY
#define SCTP_AUTH_NO_AUTH	0x0002
#define SCTP_AUTH_FREE_KEY	0x0003


struct sctp_sender_dry_event {
	uint16_t sender_dry_type;
	uint16_t sender_dry_flags;
	uint32_t sender_dry_length;
	sctp_assoc_t sender_dry_assoc_id;
};


/*
 * Stream reset event - subscribe to SCTP_STREAM_RESET_EVENT
 */
struct sctp_stream_reset_event {
	uint16_t strreset_type;
	uint16_t strreset_flags;
	uint32_t strreset_length;
	sctp_assoc_t strreset_assoc_id;
	uint16_t strreset_stream_list[];
};

/* flags in stream_reset_event (strreset_flags) */
#define SCTP_STREAM_RESET_INCOMING_SSN  0x0001
#define SCTP_STREAM_RESET_OUTGOING_SSN  0x0002
#define SCTP_STREAM_RESET_DENIED        0x0004
#define SCTP_STREAM_RESET_FAILED        0x0008

/*
 * Assoc reset event - subscribe to SCTP_ASSOC_RESET_EVENT
 */
struct sctp_assoc_reset_event {
	uint16_t assocreset_type;
	uint16_t assocreset_flags;
	uint32_t assocreset_length;
	sctp_assoc_t assocreset_assoc_id;
	uint32_t assocreset_local_tsn;
	uint32_t assocreset_remote_tsn;
};

#define SCTP_ASSOC_RESET_DENIED		0x0004
#define SCTP_ASSOC_RESET_FAILED		0x0008

/*
 * Stream change event - subscribe to SCTP_STREAM_CHANGE_EVENT
 */
struct sctp_stream_change_event {
	uint16_t strchange_type;
	uint16_t strchange_flags;
	uint32_t strchange_length;
	sctp_assoc_t strchange_assoc_id;
	uint16_t strchange_instrms;
	uint16_t strchange_outstrms;
};

#define SCTP_STREAM_CHANGE_DENIED	0x0004
#define SCTP_STREAM_CHANGE_FAILED	0x0008


/* SCTP notification event */
struct sctp_tlv {
	uint16_t sn_type;
	uint16_t sn_flags;
	uint32_t sn_length;
};

union sctp_notification {
	struct sctp_tlv sn_header;
	struct sctp_assoc_change sn_assoc_change;
	struct sctp_paddr_change sn_paddr_change;
	struct sctp_remote_error sn_remote_error;
	struct sctp_send_failed sn_send_failed;
	struct sctp_shutdown_event sn_shutdown_event;
	struct sctp_adaptation_event sn_adaptation_event;
	/* compatibility same as above */
	struct sctp_adaption_event sn_adaption_event;
	struct sctp_pdapi_event sn_pdapi_event;
	struct sctp_authkey_event sn_auth_event;
	struct sctp_sender_dry_event sn_sender_dry_event;
	struct sctp_send_failed_event sn_send_failed_event;
	struct sctp_stream_reset_event sn_strreset_event;
	struct sctp_assoc_reset_event sn_assocreset_event;
	struct sctp_stream_change_event sn_strchange_event;
};

/* notification types */
#define SCTP_ASSOC_CHANGE                       0x0001
#define SCTP_PEER_ADDR_CHANGE                   0x0002
#define SCTP_REMOTE_ERROR                       0x0003
#define SCTP_SEND_FAILED                        0x0004
#define SCTP_SHUTDOWN_EVENT                     0x0005
#define SCTP_ADAPTATION_INDICATION              0x0006
/* same as above */
#define SCTP_ADAPTION_INDICATION                0x0006
#define SCTP_PARTIAL_DELIVERY_EVENT             0x0007
#define SCTP_AUTHENTICATION_EVENT               0x0008
#define SCTP_STREAM_RESET_EVENT                 0x0009
#define SCTP_SENDER_DRY_EVENT                   0x000a
#define SCTP_NOTIFICATIONS_STOPPED_EVENT        0x000b	/* we don't send this */
#define SCTP_ASSOC_RESET_EVENT                  0x000c
#define SCTP_STREAM_CHANGE_EVENT                0x000d
#define SCTP_SEND_FAILED_EVENT                  0x000e
/*
 * socket option structs
 */

struct sctp_paddrparams {
	struct sockaddr_storage spp_address;
	sctp_assoc_t spp_assoc_id;
	uint32_t spp_hbinterval;
	uint32_t spp_pathmtu;
	uint32_t spp_flags;
	uint32_t spp_ipv6_flowlabel;
	uint16_t spp_pathmaxrxt;
	uint8_t spp_dscp;
};
#define spp_ipv4_tos spp_dscp

#define SPP_HB_ENABLE		0x00000001
#define SPP_HB_DISABLE		0x00000002
#define SPP_HB_DEMAND		0x00000004
#define SPP_PMTUD_ENABLE	0x00000008
#define SPP_PMTUD_DISABLE	0x00000010
#define SPP_HB_TIME_IS_ZERO     0x00000080
#define SPP_IPV6_FLOWLABEL      0x00000100
#define SPP_DSCP                0x00000200
#define SPP_IPV4_TOS            SPP_DSCP

struct sctp_paddrthlds {
	struct sockaddr_storage spt_address;
	sctp_assoc_t spt_assoc_id;
	uint16_t spt_pathmaxrxt;
	uint16_t spt_pathpfthld;
	uint16_t spt_pathcpthld;
};

struct sctp_paddrinfo {
	struct sockaddr_storage spinfo_address;
	sctp_assoc_t spinfo_assoc_id;
	int32_t spinfo_state;
	uint32_t spinfo_cwnd;
	uint32_t spinfo_srtt;
	uint32_t spinfo_rto;
	uint32_t spinfo_mtu;
};

struct sctp_rtoinfo {
	sctp_assoc_t srto_assoc_id;
	uint32_t srto_initial;
	uint32_t srto_max;
	uint32_t srto_min;
};

struct sctp_assocparams {
	sctp_assoc_t sasoc_assoc_id;
	uint32_t sasoc_peer_rwnd;
	uint32_t sasoc_local_rwnd;
	uint32_t sasoc_cookie_life;
	uint16_t sasoc_asocmaxrxt;
	uint16_t sasoc_number_peer_destinations;
};

struct sctp_setprim {
	struct sockaddr_storage ssp_addr;
	sctp_assoc_t ssp_assoc_id;
	uint8_t ssp_padding[4];
};

struct sctp_setpeerprim {
	struct sockaddr_storage sspp_addr;
	sctp_assoc_t sspp_assoc_id;
	uint8_t sspp_padding[4];
};

struct sctp_getaddresses {
	sctp_assoc_t sget_assoc_id;
	/* addr is filled in for N * sockaddr_storage */
	struct sockaddr addr[1];
};

struct sctp_status {
	sctp_assoc_t sstat_assoc_id;
	int32_t sstat_state;
	uint32_t sstat_rwnd;
	uint16_t sstat_unackdata;
	uint16_t sstat_penddata;
	uint16_t sstat_instrms;
	uint16_t sstat_outstrms;
	uint32_t sstat_fragmentation_point;
	struct sctp_paddrinfo sstat_primary;
};

/*
 * AUTHENTICATION support
 */
/* SCTP_AUTH_CHUNK */
struct sctp_authchunk {
	uint8_t sauth_chunk;
};

/* SCTP_AUTH_KEY */
struct sctp_authkey {
	sctp_assoc_t sca_assoc_id;
	uint16_t sca_keynumber;
	uint16_t sca_keylength;
	uint8_t sca_key[];
};

/* SCTP_HMAC_IDENT */
struct sctp_hmacalgo {
	uint32_t shmac_number_of_idents;
	uint16_t shmac_idents[];
};

/* AUTH hmac_id */
#define SCTP_AUTH_HMAC_ID_RSVD		0x0000
#define SCTP_AUTH_HMAC_ID_SHA1		0x0001	/* default, mandatory */
#define SCTP_AUTH_HMAC_ID_SHA256	0x0003

/* SCTP_AUTH_ACTIVE_KEY / SCTP_AUTH_DELETE_KEY */
struct sctp_authkeyid {
	sctp_assoc_t scact_assoc_id;
	uint16_t scact_keynumber;
};

/* SCTP_PEER_AUTH_CHUNKS / SCTP_LOCAL_AUTH_CHUNKS */
struct sctp_authchunks {
	sctp_assoc_t gauth_assoc_id;
	uint32_t gauth_number_of_chunks;
	uint8_t gauth_chunks[];
};

struct sctp_assoc_value {
	sctp_assoc_t assoc_id;
	uint32_t assoc_value;
};

struct sctp_cc_option {
	int option;
	struct sctp_assoc_value aid_value;
};

struct sctp_stream_value {
	sctp_assoc_t assoc_id;
	uint16_t stream_id;
	uint16_t stream_value;
};

struct sctp_assoc_ids {
	uint32_t gaids_number_of_ids;
	sctp_assoc_t gaids_assoc_id[];
};

struct sctp_sack_info {
	sctp_assoc_t sack_assoc_id;
	uint32_t sack_delay;
	uint32_t sack_freq;
};

struct sctp_timeouts {
	sctp_assoc_t stimo_assoc_id;
	uint32_t stimo_init;
	uint32_t stimo_data;
	uint32_t stimo_sack;
	uint32_t stimo_shutdown;
	uint32_t stimo_heartbeat;
	uint32_t stimo_cookie;
	uint32_t stimo_shutdownack;
};

struct sctp_udpencaps {
	struct sockaddr_storage sue_address;
	sctp_assoc_t sue_assoc_id;
	uint16_t sue_port;
};

struct sctp_prstatus {
	sctp_assoc_t sprstat_assoc_id;
	uint16_t sprstat_sid;
	uint16_t sprstat_policy;
	uint64_t sprstat_abandoned_unsent;
	uint64_t sprstat_abandoned_sent;
};

struct sctp_cwnd_args {
	struct sctp_nets *net;	/* network to *//* FIXME: LP64 issue */
	uint32_t cwnd_new_value;	/* cwnd in k */
	uint32_t pseudo_cumack;
	uint16_t inflight;	/* flightsize in k */
	uint16_t cwnd_augment;	/* increment to it */
	uint8_t meets_pseudo_cumack;
	uint8_t need_new_pseudo_cumack;
	uint8_t cnt_in_send;
	uint8_t cnt_in_str;
};

struct sctp_blk_args {
	uint32_t onsb;		/* in 1k bytes */
	uint32_t sndlen;	/* len of send being attempted */
	uint32_t peer_rwnd;	/* rwnd of peer */
	uint16_t send_sent_qcnt;	/* chnk cnt */
	uint16_t stream_qcnt;	/* chnk cnt */
	uint16_t chunks_on_oque;	/* chunks out */
	uint16_t flight_size;	/* flight size in k */
};

/*
 * Max we can reset in one setting, note this is dictated not by the define
 * but the size of a mbuf cluster so don't change this define and think you
 * can specify more. You must do multiple resets if you want to reset more
 * than SCTP_MAX_EXPLICIT_STR_RESET.
 */
#define SCTP_MAX_EXPLICT_STR_RESET   1000

struct sctp_reset_streams {
	sctp_assoc_t srs_assoc_id;
	uint16_t srs_flags;
	uint16_t srs_number_streams;	/* 0 == ALL */
	uint16_t srs_stream_list[];	/* list if strrst_num_streams is not 0 */
};

struct sctp_add_streams {
	sctp_assoc_t sas_assoc_id;
	uint16_t sas_instrms;
	uint16_t sas_outstrms;
};

struct sctp_get_nonce_values {
	sctp_assoc_t gn_assoc_id;
	uint32_t gn_peers_tag;
	uint32_t gn_local_tag;
};

/* Debugging logs */
struct sctp_str_log {
	void *stcb;		/* FIXME: LP64 issue */
	uint32_t n_tsn;
	uint32_t e_tsn;
	uint16_t n_sseq;
	uint16_t e_sseq;
	uint16_t strm;
};

struct sctp_sb_log {
	void *stcb;		/* FIXME: LP64 issue */
	uint32_t so_sbcc;
	uint32_t stcb_sbcc;
	uint32_t incr;
};

struct sctp_fr_log {
	uint32_t largest_tsn;
	uint32_t largest_new_tsn;
	uint32_t tsn;
};

struct sctp_fr_map {
	uint32_t base;
	uint32_t cum;
	uint32_t high;
};

struct sctp_rwnd_log {
	uint32_t rwnd;
	uint32_t send_size;
	uint32_t overhead;
	uint32_t new_rwnd;
};

struct sctp_mbcnt_log {
	uint32_t total_queue_size;
	uint32_t size_change;
	uint32_t total_queue_mb_size;
	uint32_t mbcnt_change;
};

struct sctp_sack_log {
	uint32_t cumack;
	uint32_t oldcumack;
	uint32_t tsn;
	uint16_t numGaps;
	uint16_t numDups;
};

struct sctp_lock_log {
	void *sock;		/* FIXME: LP64 issue */
	void *inp;		/* FIXME: LP64 issue */
	uint8_t tcb_lock;
	uint8_t inp_lock;
	uint8_t info_lock;
	uint8_t sock_lock;
	uint8_t sockrcvbuf_lock;
	uint8_t socksndbuf_lock;
	uint8_t create_lock;
	uint8_t resv;
};

struct sctp_rto_log {
	void *net;		/* FIXME: LP64 issue */
	uint32_t rtt;
};

struct sctp_nagle_log {
	void *stcb;		/* FIXME: LP64 issue */
	uint32_t total_flight;
	uint32_t total_in_queue;
	uint16_t count_in_queue;
	uint16_t count_in_flight;
};

struct sctp_sbwake_log {
	void *stcb;		/* FIXME: LP64 issue */
	uint16_t send_q;
	uint16_t sent_q;
	uint16_t flight;
	uint16_t wake_cnt;
	uint8_t stream_qcnt;	/* chnk cnt */
	uint8_t chunks_on_oque;	/* chunks out */
	uint8_t sbflags;
	uint8_t sctpflags;
};

struct sctp_misc_info {
	uint32_t log1;
	uint32_t log2;
	uint32_t log3;
	uint32_t log4;
};

struct sctp_log_closing {
	void *inp;		/* FIXME: LP64 issue */
	void *stcb;		/* FIXME: LP64 issue */
	uint32_t sctp_flags;
	uint16_t state;
	int16_t loc;
};

struct sctp_mbuf_log {
	struct mbuf *mp;	/* FIXME: LP64 issue */
	caddr_t ext;
	caddr_t data;
	uint16_t size;
	uint8_t refcnt;
	uint8_t mbuf_flags;
};

struct sctp_cwnd_log {
	uint64_t time_event;
	uint8_t from;
	uint8_t event_type;
	uint8_t resv[2];
	union {
		struct sctp_log_closing close;
		struct sctp_blk_args blk;
		struct sctp_cwnd_args cwnd;
		struct sctp_str_log strlog;
		struct sctp_fr_log fr;
		struct sctp_fr_map map;
		struct sctp_rwnd_log rwnd;
		struct sctp_mbcnt_log mbcnt;
		struct sctp_sack_log sack;
		struct sctp_lock_log lock;
		struct sctp_rto_log rto;
		struct sctp_sb_log sb;
		struct sctp_nagle_log nagle;
		struct sctp_sbwake_log wake;
		struct sctp_mbuf_log mb;
		struct sctp_misc_info misc;
	}     x;
};

struct sctp_cwnd_log_req {
	int32_t num_in_log;	/* Number in log */
	int32_t num_ret;	/* Number returned */
	int32_t start_at;	/* start at this one */
	int32_t end_at;		/* end at this one */
	struct sctp_cwnd_log log[];
};

struct sctp_timeval {
	uint32_t tv_sec;
	uint32_t tv_usec;
};

struct sctpstat {
	struct sctp_timeval sctps_discontinuitytime;	/* sctpStats 18
							 * (TimeStamp) */
	/* MIB according to RFC 3873 */
	uint32_t sctps_currestab;	/* sctpStats  1   (Gauge32) */
	uint32_t sctps_activeestab;	/* sctpStats  2 (Counter32) */
	uint32_t sctps_restartestab;
	uint32_t sctps_collisionestab;
	uint32_t sctps_passiveestab;	/* sctpStats  3 (Counter32) */
	uint32_t sctps_aborted;	/* sctpStats  4 (Counter32) */
	uint32_t sctps_shutdown;	/* sctpStats  5 (Counter32) */
	uint32_t sctps_outoftheblue;	/* sctpStats  6 (Counter32) */
	uint32_t sctps_checksumerrors;	/* sctpStats  7 (Counter32) */
	uint32_t sctps_outcontrolchunks;	/* sctpStats  8 (Counter64) */
	uint32_t sctps_outorderchunks;	/* sctpStats  9 (Counter64) */
	uint32_t sctps_outunorderchunks;	/* sctpStats 10 (Counter64) */
	uint32_t sctps_incontrolchunks;	/* sctpStats 11 (Counter64) */
	uint32_t sctps_inorderchunks;	/* sctpStats 12 (Counter64) */
	uint32_t sctps_inunorderchunks;	/* sctpStats 13 (Counter64) */
	uint32_t sctps_fragusrmsgs;	/* sctpStats 14 (Counter64) */
	uint32_t sctps_reasmusrmsgs;	/* sctpStats 15 (Counter64) */
	uint32_t sctps_outpackets;	/* sctpStats 16 (Counter64) */
	uint32_t sctps_inpackets;	/* sctpStats 17 (Counter64) */

	/* input statistics: */
	uint32_t sctps_recvpackets;	/* total input packets        */
	uint32_t sctps_recvdatagrams;	/* total input datagrams      */
	uint32_t sctps_recvpktwithdata;	/* total packets that had data */
	uint32_t sctps_recvsacks;	/* total input SACK chunks    */
	uint32_t sctps_recvdata;	/* total input DATA chunks    */
	uint32_t sctps_recvdupdata;	/* total input duplicate DATA chunks */
	uint32_t sctps_recvheartbeat;	/* total input HB chunks      */
	uint32_t sctps_recvheartbeatack;	/* total input HB-ACK chunks  */
	uint32_t sctps_recvecne;	/* total input ECNE chunks    */
	uint32_t sctps_recvauth;	/* total input AUTH chunks    */
	uint32_t sctps_recvauthmissing;	/* total input chunks missing AUTH */
	uint32_t sctps_recvivalhmacid;	/* total number of invalid HMAC ids
					 * received */
	uint32_t sctps_recvivalkeyid;	/* total number of invalid secret ids
					 * received */
	uint32_t sctps_recvauthfailed;	/* total number of auth failed */
	uint32_t sctps_recvexpress;	/* total fast path receives all one
					 * chunk */
	uint32_t sctps_recvexpressm;	/* total fast path multi-part data */
	uint32_t sctps_recv_spare;	/* formerly sctps_recvnocrc */
	uint32_t sctps_recvswcrc;
	uint32_t sctps_recvhwcrc;

	/* output statistics: */
	uint32_t sctps_sendpackets;	/* total output packets       */
	uint32_t sctps_sendsacks;	/* total output SACKs         */
	uint32_t sctps_senddata;	/* total output DATA chunks   */
	uint32_t sctps_sendretransdata;	/* total output retransmitted DATA
					 * chunks */
	uint32_t sctps_sendfastretrans;	/* total output fast retransmitted
					 * DATA chunks */
	uint32_t sctps_sendmultfastretrans;	/* total FR's that happened
						 * more than once to same
						 * chunk (u-del multi-fr
						 * algo). */
	uint32_t sctps_sendheartbeat;	/* total output HB chunks     */
	uint32_t sctps_sendecne;	/* total output ECNE chunks    */
	uint32_t sctps_sendauth;	/* total output AUTH chunks FIXME   */
	uint32_t sctps_senderrors;	/* ip_output error counter */
	uint32_t sctps_send_spare;	/* formerly sctps_sendnocrc */
	uint32_t sctps_sendswcrc;
	uint32_t sctps_sendhwcrc;
	/* PCKDROPREP statistics: */
	uint32_t sctps_pdrpfmbox;	/* Packet drop from middle box */
	uint32_t sctps_pdrpfehos;	/* P-drop from end host */
	uint32_t sctps_pdrpmbda;	/* P-drops with data */
	uint32_t sctps_pdrpmbct;	/* P-drops, non-data, non-endhost */
	uint32_t sctps_pdrpbwrpt;	/* P-drop, non-endhost, bandwidth rep
					 * only */
	uint32_t sctps_pdrpcrupt;	/* P-drop, not enough for chunk header */
	uint32_t sctps_pdrpnedat;	/* P-drop, not enough data to confirm */
	uint32_t sctps_pdrppdbrk;	/* P-drop, where process_chunk_drop
					 * said break */
	uint32_t sctps_pdrptsnnf;	/* P-drop, could not find TSN */
	uint32_t sctps_pdrpdnfnd;	/* P-drop, attempt reverse TSN lookup */
	uint32_t sctps_pdrpdiwnp;	/* P-drop, e-host confirms zero-rwnd */
	uint32_t sctps_pdrpdizrw;	/* P-drop, midbox confirms no space */
	uint32_t sctps_pdrpbadd;	/* P-drop, data did not match TSN */
	uint32_t sctps_pdrpmark;	/* P-drop, TSN's marked for Fast
					 * Retran */
	/* timeouts */
	uint32_t sctps_timoiterator;	/* Number of iterator timers that
					 * fired */
	uint32_t sctps_timodata;	/* Number of T3 data time outs */
	uint32_t sctps_timowindowprobe;	/* Number of window probe (T3) timers
					 * that fired */
	uint32_t sctps_timoinit;	/* Number of INIT timers that fired */
	uint32_t sctps_timosack;	/* Number of sack timers that fired */
	uint32_t sctps_timoshutdown;	/* Number of shutdown timers that
					 * fired */
	uint32_t sctps_timoheartbeat;	/* Number of heartbeat timers that
					 * fired */
	uint32_t sctps_timocookie;	/* Number of times a cookie timeout
					 * fired */
	uint32_t sctps_timosecret;	/* Number of times an endpoint changed
					 * its cookie secret */
	uint32_t sctps_timopathmtu;	/* Number of PMTU timers that fired */
	uint32_t sctps_timoshutdownack;	/* Number of shutdown ack timers that
					 * fired */
	uint32_t sctps_timoshutdownguard;	/* Number of shutdown guard
						 * timers that fired */
	uint32_t sctps_timostrmrst;	/* Number of stream reset timers that
					 * fired */
	uint32_t sctps_timoearlyfr;	/* Number of early FR timers that
					 * fired */
	uint32_t sctps_timoasconf;	/* Number of times an asconf timer
					 * fired */
	uint32_t sctps_timodelprim;	/* Number of times a prim_deleted
					 * timer fired */
	uint32_t sctps_timoautoclose;	/* Number of times auto close timer
					 * fired */
	uint32_t sctps_timoassockill;	/* Number of asoc free timers expired */
	uint32_t sctps_timoinpkill;	/* Number of inp free timers expired */
	/* former early FR counters */
	uint32_t sctps_spare[11];
	/* others */
	uint32_t sctps_hdrops;	/* packet shorter than header */
	uint32_t sctps_badsum;	/* checksum error             */
	uint32_t sctps_noport;	/* no endpoint for port       */
	uint32_t sctps_badvtag;	/* bad v-tag                  */
	uint32_t sctps_badsid;	/* bad SID                    */
	uint32_t sctps_nomem;	/* no memory                  */
	uint32_t sctps_fastretransinrtt;	/* number of multiple FR in a
						 * RTT window */
	uint32_t sctps_markedretrans;
	uint32_t sctps_naglesent;	/* nagle allowed sending      */
	uint32_t sctps_naglequeued;	/* nagle doesn't allow sending */
	uint32_t sctps_maxburstqueued;	/* max burst doesn't allow sending */
	uint32_t sctps_ifnomemqueued;	/* look ahead tells us no memory in
					 * interface ring buffer OR we had a
					 * send error and are queuing one
					 * send. */
	uint32_t sctps_windowprobed;	/* total number of window probes sent */
	uint32_t sctps_lowlevelerr;	/* total times an output error causes
					 * us to clamp down on next user send. */
	uint32_t sctps_lowlevelerrusr;	/* total times sctp_senderrors were
					 * caused from a user send from a user
					 * invoked send not a sack response */
	uint32_t sctps_datadropchklmt;	/* Number of in data drops due to
					 * chunk limit reached */
	uint32_t sctps_datadroprwnd;	/* Number of in data drops due to rwnd
					 * limit reached */
	uint32_t sctps_ecnereducedcwnd;	/* Number of times a ECN reduced the
					 * cwnd */
	uint32_t sctps_vtagexpress;	/* Used express lookup via vtag */
	uint32_t sctps_vtagbogus;	/* Collision in express lookup. */
	uint32_t sctps_primary_randry;	/* Number of times the sender ran dry
					 * of user data on primary */
	uint32_t sctps_cmt_randry;	/* Same for above */
	uint32_t sctps_slowpath_sack;	/* Sacks the slow way */
	uint32_t sctps_wu_sacks_sent;	/* Window Update only sacks sent */
	uint32_t sctps_sends_with_flags;	/* number of sends with
						 * sinfo_flags !=0 */
	uint32_t sctps_sends_with_unord;	/* number of unordered sends */
	uint32_t sctps_sends_with_eof;	/* number of sends with EOF flag set */
	uint32_t sctps_sends_with_abort;	/* number of sends with ABORT
						 * flag set */
	uint32_t sctps_protocol_drain_calls;	/* number of times protocol
						 * drain called */
	uint32_t sctps_protocol_drains_done;	/* number of times we did a
						 * protocol drain */
	uint32_t sctps_read_peeks;	/* Number of times recv was called
					 * with peek */
	uint32_t sctps_cached_chk;	/* Number of cached chunks used */
	uint32_t sctps_cached_strmoq;	/* Number of cached stream oq's used */
	uint32_t sctps_left_abandon;	/* Number of unread messages abandoned
					 * by close */
	uint32_t sctps_send_burst_avoid;	/* Unused */
	uint32_t sctps_send_cwnd_avoid;	/* Send cwnd full  avoidance, already
					 * max burst inflight to net */
	uint32_t sctps_fwdtsn_map_over;	/* number of map array over-runs via
					 * fwd-tsn's */
	uint32_t sctps_queue_upd_ecne;	/* Number of times we queued or
					 * updated an ECN chunk on send queue */
	uint32_t sctps_reserved[31];	/* Future ABI compat - remove int's
					 * from here when adding new */
};

#define SCTP_STAT_INCR(_x) SCTP_STAT_INCR_BY(_x,1)
#define SCTP_STAT_DECR(_x) SCTP_STAT_DECR_BY(_x,1)
#if defined(__FreeBSD__) && defined(SMP) && defined(SCTP_USE_PERCPU_STAT)
#define SCTP_STAT_INCR_BY(_x,_d) (SCTP_BASE_STATS[PCPU_GET(cpuid)]._x += _d)
#define SCTP_STAT_DECR_BY(_x,_d) (SCTP_BASE_STATS[PCPU_GET(cpuid)]._x -= _d)
#else
#define SCTP_STAT_INCR_BY(_x,_d) atomic_add_int(&SCTP_BASE_STAT(_x), _d)
#define SCTP_STAT_DECR_BY(_x,_d) atomic_subtract_int(&SCTP_BASE_STAT(_x), _d)
#endif
/* The following macros are for handling MIB values, */
#define SCTP_STAT_INCR_COUNTER32(_x) SCTP_STAT_INCR(_x)
#define SCTP_STAT_INCR_COUNTER64(_x) SCTP_STAT_INCR(_x)
#define SCTP_STAT_INCR_GAUGE32(_x) SCTP_STAT_INCR(_x)
#define SCTP_STAT_DECR_COUNTER32(_x) SCTP_STAT_DECR(_x)
#define SCTP_STAT_DECR_COUNTER64(_x) SCTP_STAT_DECR(_x)
#define SCTP_STAT_DECR_GAUGE32(_x) SCTP_STAT_DECR(_x)

union sctp_sockstore {
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	struct sockaddr sa;
};


/***********************************/
/* And something for us old timers */
/***********************************/

#ifndef ntohll
#include <sys/endian.h>
#define ntohll(x) be64toh(x)
#endif

#ifndef htonll
#include <sys/endian.h>
#define htonll(x) htobe64(x)
#endif
/***********************************/


struct xsctp_inpcb {
	uint32_t last;
	uint32_t flags;
	uint64_t features;
	uint32_t total_sends;
	uint32_t total_recvs;
	uint32_t total_nospaces;
	uint32_t fragmentation_point;
	uint16_t local_port;
	uint16_t qlen_old;
	uint16_t maxqlen_old;
	uint16_t __spare16;
	kvaddr_t socket;
	uint32_t qlen;
	uint32_t maxqlen;
	uint32_t extra_padding[26];	/* future */
};

struct xsctp_tcb {
	union sctp_sockstore primary_addr;	/* sctpAssocEntry 5/6 */
	uint32_t last;
	uint32_t heartbeat_interval;	/* sctpAssocEntry 7   */
	uint32_t state;		/* sctpAssocEntry 8   */
	uint32_t in_streams;	/* sctpAssocEntry 9   */
	uint32_t out_streams;	/* sctpAssocEntry 10  */
	uint32_t max_nr_retrans;	/* sctpAssocEntry 11  */
	uint32_t primary_process;	/* sctpAssocEntry 12  */
	uint32_t T1_expireries;	/* sctpAssocEntry 13  */
	uint32_t T2_expireries;	/* sctpAssocEntry 14  */
	uint32_t retransmitted_tsns;	/* sctpAssocEntry 15  */
	uint32_t total_sends;
	uint32_t total_recvs;
	uint32_t local_tag;
	uint32_t remote_tag;
	uint32_t initial_tsn;
	uint32_t highest_tsn;
	uint32_t cumulative_tsn;
	uint32_t cumulative_tsn_ack;
	uint32_t mtu;
	uint32_t refcnt;
	uint16_t local_port;	/* sctpAssocEntry 3   */
	uint16_t remote_port;	/* sctpAssocEntry 4   */
	struct sctp_timeval start_time;	/* sctpAssocEntry 16  */
	struct sctp_timeval discontinuity_time;	/* sctpAssocEntry 17  */
	uint32_t peers_rwnd;
	sctp_assoc_t assoc_id;	/* sctpAssocEntry 1   */
	uint32_t extra_padding[32];	/* future */
};

struct xsctp_laddr {
	union sctp_sockstore address;	/* sctpAssocLocalAddrEntry 1/2 */
	uint32_t last;
	struct sctp_timeval start_time;	/* sctpAssocLocalAddrEntry 3   */
	uint32_t extra_padding[32];	/* future */
};

struct xsctp_raddr {
	union sctp_sockstore address;	/* sctpAssocLocalRemEntry 1/2 */
	uint32_t last;
	uint32_t rto;		/* sctpAssocLocalRemEntry 5   */
	uint32_t max_path_rtx;	/* sctpAssocLocalRemEntry 6   */
	uint32_t rtx;		/* sctpAssocLocalRemEntry 7   */
	uint32_t error_counter;	/* */
	uint32_t cwnd;		/* */
	uint32_t flight_size;	/* */
	uint32_t mtu;		/* */
	uint8_t active;		/* sctpAssocLocalRemEntry 3   */
	uint8_t confirmed;	/* */
	uint8_t heartbeat_enabled;	/* sctpAssocLocalRemEntry 4   */
	uint8_t potentially_failed;
	struct sctp_timeval start_time;	/* sctpAssocLocalRemEntry 8   */
	uint32_t rtt;
	uint32_t heartbeat_interval;
	uint32_t ssthresh;
	uint16_t encaps_port;
	uint16_t state;
	uint32_t extra_padding[29];	/* future */
};

#define SCTP_MAX_LOGGING_SIZE 30000
#define SCTP_TRACE_PARAMS 6	/* This number MUST be even   */

struct sctp_log_entry {
	uint64_t timestamp;
	uint32_t subsys;
	uint32_t padding;
	uint32_t params[SCTP_TRACE_PARAMS];
};

struct sctp_log {
	struct sctp_log_entry entry[SCTP_MAX_LOGGING_SIZE];
	uint32_t index;
	uint32_t padding;
};

/*
 * Kernel defined for sctp_send
 */
#if defined(_KERNEL) || defined(__Userspace__)
int
sctp_lower_sosend(struct socket *so,
    struct sockaddr *addr,
    struct uio *uio,
    struct mbuf *i_pak,
    struct mbuf *control,
    int flags,
    struct sctp_sndrcvinfo *srcv
    ,struct thread *p
);

int
sctp_sorecvmsg(struct socket *so,
    struct uio *uio,
    struct mbuf **mp,
    struct sockaddr *from,
    int fromlen,
    int *msg_flags,
    struct sctp_sndrcvinfo *sinfo,
    int filling_sinfo);
#endif

/*
 * API system calls
 */
#if !(defined(_KERNEL)) && !(defined(__Userspace__))

__BEGIN_DECLS
int sctp_peeloff(int, sctp_assoc_t);
int sctp_bindx(int, struct sockaddr *, int, int);
int sctp_connectx(int, const struct sockaddr *, int, sctp_assoc_t *);
int sctp_getaddrlen(sa_family_t);
int sctp_getpaddrs(int, sctp_assoc_t, struct sockaddr **);
void sctp_freepaddrs(struct sockaddr *);
int sctp_getladdrs(int, sctp_assoc_t, struct sockaddr **);
void sctp_freeladdrs(struct sockaddr *);
int sctp_opt_info(int, sctp_assoc_t, int, void *, socklen_t *);

/* deprecated */
ssize_t
sctp_sendmsg(int, const void *, size_t, const struct sockaddr *,
    socklen_t, uint32_t, uint32_t, uint16_t, uint32_t, uint32_t);

/* deprecated */
ssize_t
sctp_send(int, const void *, size_t,
    const struct sctp_sndrcvinfo *, int);

/* deprecated */
ssize_t
sctp_sendx(int, const void *, size_t, struct sockaddr *,
    int, struct sctp_sndrcvinfo *, int);

/* deprecated */
ssize_t
sctp_sendmsgx(int sd, const void *, size_t, struct sockaddr *,
    int, uint32_t, uint32_t, uint16_t, uint32_t, uint32_t);

sctp_assoc_t sctp_getassocid(int, struct sockaddr *);

/* deprecated */
ssize_t
sctp_recvmsg(int, void *, size_t, struct sockaddr *, socklen_t *,
    struct sctp_sndrcvinfo *, int *);

ssize_t
sctp_sendv(int, const struct iovec *, int, struct sockaddr *,
    int, void *, socklen_t, unsigned int, int);

ssize_t
sctp_recvv(int, const struct iovec *, int, struct sockaddr *,
    socklen_t *, void *, socklen_t *, unsigned int *, int *);

__END_DECLS

#endif				/* !_KERNEL */
#endif				/* !__sctp_uio_h__ */
