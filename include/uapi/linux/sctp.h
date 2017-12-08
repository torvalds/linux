/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2002 Intel Corp.
 *
 * This file is part of the SCTP kernel implementation
 *
 * This header represents the structures and constants needed to support
 * the SCTP Extension to the Sockets API.
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
 * along with GNU CC; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll    <piggy@acm.org>
 *    R. Stewart               <randall@sctp.chicago.il.us>
 *    K. Morneau               <kmorneau@cisco.com>
 *    Q. Xie                   <qxie1@email.mot.com>
 *    Karl Knutson             <karl@athena.chicago.il.us>
 *    Jon Grimm                <jgrimm@us.ibm.com>
 *    Daisy Chang              <daisyc@us.ibm.com>
 *    Ryan Layer               <rmlayer@us.ibm.com>
 *    Ardelle Fan              <ardelle.fan@intel.com>
 *    Sridhar Samudrala        <sri@us.ibm.com>
 *    Inaky Perez-Gonzalez     <inaky.gonzalez@intel.com>
 *    Vlad Yasevich            <vladislav.yasevich@hp.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#ifndef _UAPI_SCTP_H
#define _UAPI_SCTP_H

#include <linux/types.h>
#include <linux/socket.h>

typedef __s32 sctp_assoc_t;

/* The following symbols come from the Sockets API Extensions for
 * SCTP <draft-ietf-tsvwg-sctpsocket-07.txt>.
 */
#define SCTP_RTOINFO	0
#define SCTP_ASSOCINFO  1
#define SCTP_INITMSG	2
#define SCTP_NODELAY	3		/* Get/set nodelay option. */
#define SCTP_AUTOCLOSE	4
#define SCTP_SET_PEER_PRIMARY_ADDR 5
#define SCTP_PRIMARY_ADDR	6
#define SCTP_ADAPTATION_LAYER	7
#define SCTP_DISABLE_FRAGMENTS	8
#define SCTP_PEER_ADDR_PARAMS	9
#define SCTP_DEFAULT_SEND_PARAM	10
#define SCTP_EVENTS	11
#define SCTP_I_WANT_MAPPED_V4_ADDR 12	/* Turn on/off mapped v4 addresses  */
#define SCTP_MAXSEG	13		/* Get/set maximum fragment. */
#define SCTP_STATUS	14
#define SCTP_GET_PEER_ADDR_INFO	15
#define SCTP_DELAYED_ACK_TIME	16
#define SCTP_DELAYED_ACK SCTP_DELAYED_ACK_TIME
#define SCTP_DELAYED_SACK SCTP_DELAYED_ACK_TIME
#define SCTP_CONTEXT	17
#define SCTP_FRAGMENT_INTERLEAVE	18
#define SCTP_PARTIAL_DELIVERY_POINT	19 /* Set/Get partial delivery point */
#define SCTP_MAX_BURST	20		/* Set/Get max burst */
#define SCTP_AUTH_CHUNK	21	/* Set only: add a chunk type to authenticate */
#define SCTP_HMAC_IDENT	22
#define SCTP_AUTH_KEY	23
#define SCTP_AUTH_ACTIVE_KEY	24
#define SCTP_AUTH_DELETE_KEY	25
#define SCTP_PEER_AUTH_CHUNKS	26	/* Read only */
#define SCTP_LOCAL_AUTH_CHUNKS	27	/* Read only */
#define SCTP_GET_ASSOC_NUMBER	28	/* Read only */
#define SCTP_GET_ASSOC_ID_LIST	29	/* Read only */
#define SCTP_AUTO_ASCONF       30
#define SCTP_PEER_ADDR_THLDS	31
#define SCTP_RECVRCVINFO	32
#define SCTP_RECVNXTINFO	33
#define SCTP_DEFAULT_SNDINFO	34

/* Internal Socket Options. Some of the sctp library functions are
 * implemented using these socket options.
 */
#define SCTP_SOCKOPT_BINDX_ADD	100	/* BINDX requests for adding addrs */
#define SCTP_SOCKOPT_BINDX_REM	101	/* BINDX requests for removing addrs. */
#define SCTP_SOCKOPT_PEELOFF	102	/* peel off association. */
/* Options 104-106 are deprecated and removed. Do not use this space */
#define SCTP_SOCKOPT_CONNECTX_OLD	107	/* CONNECTX old requests. */
#define SCTP_GET_PEER_ADDRS	108		/* Get all peer address. */
#define SCTP_GET_LOCAL_ADDRS	109		/* Get all local address. */
#define SCTP_SOCKOPT_CONNECTX	110		/* CONNECTX requests. */
#define SCTP_SOCKOPT_CONNECTX3	111	/* CONNECTX requests (updated) */
#define SCTP_GET_ASSOC_STATS	112	/* Read only */
#define SCTP_PR_SUPPORTED	113
#define SCTP_DEFAULT_PRINFO	114
#define SCTP_PR_ASSOC_STATUS	115
#define SCTP_PR_STREAM_STATUS	116
#define SCTP_RECONFIG_SUPPORTED	117
#define SCTP_ENABLE_STREAM_RESET	118
#define SCTP_RESET_STREAMS	119
#define SCTP_RESET_ASSOC	120
#define SCTP_ADD_STREAMS	121
#define SCTP_SOCKOPT_PEELOFF_FLAGS 122
#define SCTP_STREAM_SCHEDULER	123
#define SCTP_STREAM_SCHEDULER_VALUE	124
#define SCTP_INTERLEAVING_SUPPORTED	125

/* PR-SCTP policies */
#define SCTP_PR_SCTP_NONE	0x0000
#define SCTP_PR_SCTP_TTL	0x0010
#define SCTP_PR_SCTP_RTX	0x0020
#define SCTP_PR_SCTP_PRIO	0x0030
#define SCTP_PR_SCTP_MAX	SCTP_PR_SCTP_PRIO
#define SCTP_PR_SCTP_MASK	0x0030

#define __SCTP_PR_INDEX(x)	((x >> 4) - 1)
#define SCTP_PR_INDEX(x)	__SCTP_PR_INDEX(SCTP_PR_SCTP_ ## x)

#define SCTP_PR_POLICY(x)	((x) & SCTP_PR_SCTP_MASK)
#define SCTP_PR_SET_POLICY(flags, x)	\
	do {				\
		flags &= ~SCTP_PR_SCTP_MASK;	\
		flags |= x;		\
	} while (0)

#define SCTP_PR_TTL_ENABLED(x)	(SCTP_PR_POLICY(x) == SCTP_PR_SCTP_TTL)
#define SCTP_PR_RTX_ENABLED(x)	(SCTP_PR_POLICY(x) == SCTP_PR_SCTP_RTX)
#define SCTP_PR_PRIO_ENABLED(x)	(SCTP_PR_POLICY(x) == SCTP_PR_SCTP_PRIO)

/* For enable stream reset */
#define SCTP_ENABLE_RESET_STREAM_REQ	0x01
#define SCTP_ENABLE_RESET_ASSOC_REQ	0x02
#define SCTP_ENABLE_CHANGE_ASSOC_REQ	0x04
#define SCTP_ENABLE_STRRESET_MASK	0x07

#define SCTP_STREAM_RESET_INCOMING	0x01
#define SCTP_STREAM_RESET_OUTGOING	0x02

/* These are bit fields for msghdr->msg_flags.  See section 5.1.  */
/* On user space Linux, these live in <bits/socket.h> as an enum.  */
enum sctp_msg_flags {
	MSG_NOTIFICATION = 0x8000,
#define MSG_NOTIFICATION MSG_NOTIFICATION
};

/* 5.3.1 SCTP Initiation Structure (SCTP_INIT)
 *
 *   This cmsghdr structure provides information for initializing new
 *   SCTP associations with sendmsg().  The SCTP_INITMSG socket option
 *   uses this same data structure.  This structure is not used for
 *   recvmsg().
 *
 *   cmsg_level    cmsg_type      cmsg_data[]
 *   ------------  ------------   ----------------------
 *   IPPROTO_SCTP  SCTP_INIT      struct sctp_initmsg
 */
struct sctp_initmsg {
	__u16 sinit_num_ostreams;
	__u16 sinit_max_instreams;
	__u16 sinit_max_attempts;
	__u16 sinit_max_init_timeo;
};

/* 5.3.2 SCTP Header Information Structure (SCTP_SNDRCV)
 *
 *   This cmsghdr structure specifies SCTP options for sendmsg() and
 *   describes SCTP header information about a received message through
 *   recvmsg().
 *
 *   cmsg_level    cmsg_type      cmsg_data[]
 *   ------------  ------------   ----------------------
 *   IPPROTO_SCTP  SCTP_SNDRCV    struct sctp_sndrcvinfo
 */
struct sctp_sndrcvinfo {
	__u16 sinfo_stream;
	__u16 sinfo_ssn;
	__u16 sinfo_flags;
	__u32 sinfo_ppid;
	__u32 sinfo_context;
	__u32 sinfo_timetolive;
	__u32 sinfo_tsn;
	__u32 sinfo_cumtsn;
	sctp_assoc_t sinfo_assoc_id;
};

/* 5.3.4 SCTP Send Information Structure (SCTP_SNDINFO)
 *
 *   This cmsghdr structure specifies SCTP options for sendmsg().
 *
 *   cmsg_level    cmsg_type      cmsg_data[]
 *   ------------  ------------   -------------------
 *   IPPROTO_SCTP  SCTP_SNDINFO   struct sctp_sndinfo
 */
struct sctp_sndinfo {
	__u16 snd_sid;
	__u16 snd_flags;
	__u32 snd_ppid;
	__u32 snd_context;
	sctp_assoc_t snd_assoc_id;
};

/* 5.3.5 SCTP Receive Information Structure (SCTP_RCVINFO)
 *
 *   This cmsghdr structure describes SCTP receive information
 *   about a received message through recvmsg().
 *
 *   cmsg_level    cmsg_type      cmsg_data[]
 *   ------------  ------------   -------------------
 *   IPPROTO_SCTP  SCTP_RCVINFO   struct sctp_rcvinfo
 */
struct sctp_rcvinfo {
	__u16 rcv_sid;
	__u16 rcv_ssn;
	__u16 rcv_flags;
	__u32 rcv_ppid;
	__u32 rcv_tsn;
	__u32 rcv_cumtsn;
	__u32 rcv_context;
	sctp_assoc_t rcv_assoc_id;
};

/* 5.3.6 SCTP Next Receive Information Structure (SCTP_NXTINFO)
 *
 *   This cmsghdr structure describes SCTP receive information
 *   of the next message that will be delivered through recvmsg()
 *   if this information is already available when delivering
 *   the current message.
 *
 *   cmsg_level    cmsg_type      cmsg_data[]
 *   ------------  ------------   -------------------
 *   IPPROTO_SCTP  SCTP_NXTINFO   struct sctp_nxtinfo
 */
struct sctp_nxtinfo {
	__u16 nxt_sid;
	__u16 nxt_flags;
	__u32 nxt_ppid;
	__u32 nxt_length;
	sctp_assoc_t nxt_assoc_id;
};

/*
 *  sinfo_flags: 16 bits (unsigned integer)
 *
 *   This field may contain any of the following flags and is composed of
 *   a bitwise OR of these values.
 */
enum sctp_sinfo_flags {
	SCTP_UNORDERED		= (1 << 0), /* Send/receive message unordered. */
	SCTP_ADDR_OVER		= (1 << 1), /* Override the primary destination. */
	SCTP_ABORT		= (1 << 2), /* Send an ABORT message to the peer. */
	SCTP_SACK_IMMEDIATELY	= (1 << 3), /* SACK should be sent without delay. */
	SCTP_NOTIFICATION	= MSG_NOTIFICATION, /* Next message is not user msg but notification. */
	SCTP_EOF		= MSG_FIN,  /* Initiate graceful shutdown process. */
};

typedef union {
	__u8   			raw;
	struct sctp_initmsg	init;
	struct sctp_sndrcvinfo	sndrcv;
} sctp_cmsg_data_t;

/* These are cmsg_types.  */
typedef enum sctp_cmsg_type {
	SCTP_INIT,		/* 5.2.1 SCTP Initiation Structure */
#define SCTP_INIT	SCTP_INIT
	SCTP_SNDRCV,		/* 5.2.2 SCTP Header Information Structure */
#define SCTP_SNDRCV	SCTP_SNDRCV
	SCTP_SNDINFO,		/* 5.3.4 SCTP Send Information Structure */
#define SCTP_SNDINFO	SCTP_SNDINFO
	SCTP_RCVINFO,		/* 5.3.5 SCTP Receive Information Structure */
#define SCTP_RCVINFO	SCTP_RCVINFO
	SCTP_NXTINFO,		/* 5.3.6 SCTP Next Receive Information Structure */
#define SCTP_NXTINFO	SCTP_NXTINFO
} sctp_cmsg_t;

/*
 * 5.3.1.1 SCTP_ASSOC_CHANGE
 *
 *   Communication notifications inform the ULP that an SCTP association
 *   has either begun or ended. The identifier for a new association is
 *   provided by this notificaion. The notification information has the
 *   following format:
 *
 */
struct sctp_assoc_change {
	__u16 sac_type;
	__u16 sac_flags;
	__u32 sac_length;
	__u16 sac_state;
	__u16 sac_error;
	__u16 sac_outbound_streams;
	__u16 sac_inbound_streams;
	sctp_assoc_t sac_assoc_id;
	__u8 sac_info[0];
};

/*
 *   sac_state: 32 bits (signed integer)
 *
 *   This field holds one of a number of values that communicate the
 *   event that happened to the association.  They include:
 *
 *   Note:  The following state names deviate from the API draft as
 *   the names clash too easily with other kernel symbols.
 */
enum sctp_sac_state {
	SCTP_COMM_UP,
	SCTP_COMM_LOST,
	SCTP_RESTART,
	SCTP_SHUTDOWN_COMP,
	SCTP_CANT_STR_ASSOC,
};

/*
 * 5.3.1.2 SCTP_PEER_ADDR_CHANGE
 *
 *   When a destination address on a multi-homed peer encounters a change
 *   an interface details event is sent.  The information has the
 *   following structure:
 */
struct sctp_paddr_change {
	__u16 spc_type;
	__u16 spc_flags;
	__u32 spc_length;
	struct sockaddr_storage spc_aaddr;
	int spc_state;
	int spc_error;
	sctp_assoc_t spc_assoc_id;
} __attribute__((packed, aligned(4)));

/*
 *    spc_state:  32 bits (signed integer)
 *
 *   This field holds one of a number of values that communicate the
 *   event that happened to the address.  They include:
 */
enum sctp_spc_state {
	SCTP_ADDR_AVAILABLE,
	SCTP_ADDR_UNREACHABLE,
	SCTP_ADDR_REMOVED,
	SCTP_ADDR_ADDED,
	SCTP_ADDR_MADE_PRIM,
	SCTP_ADDR_CONFIRMED,
};


/*
 * 5.3.1.3 SCTP_REMOTE_ERROR
 *
 *   A remote peer may send an Operational Error message to its peer.
 *   This message indicates a variety of error conditions on an
 *   association. The entire error TLV as it appears on the wire is
 *   included in a SCTP_REMOTE_ERROR event.  Please refer to the SCTP
 *   specification [SCTP] and any extensions for a list of possible
 *   error formats. SCTP error TLVs have the format:
 */
struct sctp_remote_error {
	__u16 sre_type;
	__u16 sre_flags;
	__u32 sre_length;
	__be16 sre_error;
	sctp_assoc_t sre_assoc_id;
	__u8 sre_data[0];
};


/*
 * 5.3.1.4 SCTP_SEND_FAILED
 *
 *   If SCTP cannot deliver a message it may return the message as a
 *   notification.
 */
struct sctp_send_failed {
	__u16 ssf_type;
	__u16 ssf_flags;
	__u32 ssf_length;
	__u32 ssf_error;
	struct sctp_sndrcvinfo ssf_info;
	sctp_assoc_t ssf_assoc_id;
	__u8 ssf_data[0];
};

/*
 *   ssf_flags: 16 bits (unsigned integer)
 *
 *   The flag value will take one of the following values
 *
 *   SCTP_DATA_UNSENT  - Indicates that the data was never put on
 *                       the wire.
 *
 *   SCTP_DATA_SENT    - Indicates that the data was put on the wire.
 *                       Note that this does not necessarily mean that the
 *                       data was (or was not) successfully delivered.
 */
enum sctp_ssf_flags {
	SCTP_DATA_UNSENT,
	SCTP_DATA_SENT,
};

/*
 * 5.3.1.5 SCTP_SHUTDOWN_EVENT
 *
 *   When a peer sends a SHUTDOWN, SCTP delivers this notification to
 *   inform the application that it should cease sending data.
 */
struct sctp_shutdown_event {
	__u16 sse_type;
	__u16 sse_flags;
	__u32 sse_length;
	sctp_assoc_t sse_assoc_id;
};

/*
 * 5.3.1.6 SCTP_ADAPTATION_INDICATION
 *
 *   When a peer sends a Adaptation Layer Indication parameter , SCTP
 *   delivers this notification to inform the application
 *   that of the peers requested adaptation layer.
 */
struct sctp_adaptation_event {
	__u16 sai_type;
	__u16 sai_flags;
	__u32 sai_length;
	__u32 sai_adaptation_ind;
	sctp_assoc_t sai_assoc_id;
};

/*
 * 5.3.1.7 SCTP_PARTIAL_DELIVERY_EVENT
 *
 *   When a receiver is engaged in a partial delivery of a
 *   message this notification will be used to indicate
 *   various events.
 */
struct sctp_pdapi_event {
	__u16 pdapi_type;
	__u16 pdapi_flags;
	__u32 pdapi_length;
	__u32 pdapi_indication;
	sctp_assoc_t pdapi_assoc_id;
};

enum { SCTP_PARTIAL_DELIVERY_ABORTED=0, };

/*
 * 5.3.1.8.  SCTP_AUTHENTICATION_EVENT
 *
 *  When a receiver is using authentication this message will provide
 *  notifications regarding new keys being made active as well as errors.
 */
struct sctp_authkey_event {
	__u16 auth_type;
	__u16 auth_flags;
	__u32 auth_length;
	__u16 auth_keynumber;
	__u16 auth_altkeynumber;
	__u32 auth_indication;
	sctp_assoc_t auth_assoc_id;
};

enum { SCTP_AUTH_NEWKEY = 0, };

/*
 * 6.1.9. SCTP_SENDER_DRY_EVENT
 *
 * When the SCTP stack has no more user data to send or retransmit, this
 * notification is given to the user. Also, at the time when a user app
 * subscribes to this event, if there is no data to be sent or
 * retransmit, the stack will immediately send up this notification.
 */
struct sctp_sender_dry_event {
	__u16 sender_dry_type;
	__u16 sender_dry_flags;
	__u32 sender_dry_length;
	sctp_assoc_t sender_dry_assoc_id;
};

#define SCTP_STREAM_RESET_INCOMING_SSN	0x0001
#define SCTP_STREAM_RESET_OUTGOING_SSN	0x0002
#define SCTP_STREAM_RESET_DENIED	0x0004
#define SCTP_STREAM_RESET_FAILED	0x0008
struct sctp_stream_reset_event {
	__u16 strreset_type;
	__u16 strreset_flags;
	__u32 strreset_length;
	sctp_assoc_t strreset_assoc_id;
	__u16 strreset_stream_list[];
};

#define SCTP_ASSOC_RESET_DENIED		0x0004
#define SCTP_ASSOC_RESET_FAILED		0x0008
struct sctp_assoc_reset_event {
	__u16 assocreset_type;
	__u16 assocreset_flags;
	__u32 assocreset_length;
	sctp_assoc_t assocreset_assoc_id;
	__u32 assocreset_local_tsn;
	__u32 assocreset_remote_tsn;
};

#define SCTP_ASSOC_CHANGE_DENIED	0x0004
#define SCTP_ASSOC_CHANGE_FAILED	0x0008
struct sctp_stream_change_event {
	__u16 strchange_type;
	__u16 strchange_flags;
	__u32 strchange_length;
	sctp_assoc_t strchange_assoc_id;
	__u16 strchange_instrms;
	__u16 strchange_outstrms;
};

/*
 * Described in Section 7.3
 *   Ancillary Data and Notification Interest Options
 */
struct sctp_event_subscribe {
	__u8 sctp_data_io_event;
	__u8 sctp_association_event;
	__u8 sctp_address_event;
	__u8 sctp_send_failure_event;
	__u8 sctp_peer_error_event;
	__u8 sctp_shutdown_event;
	__u8 sctp_partial_delivery_event;
	__u8 sctp_adaptation_layer_event;
	__u8 sctp_authentication_event;
	__u8 sctp_sender_dry_event;
	__u8 sctp_stream_reset_event;
	__u8 sctp_assoc_reset_event;
	__u8 sctp_stream_change_event;
};

/*
 * 5.3.1 SCTP Notification Structure
 *
 *   The notification structure is defined as the union of all
 *   notification types.
 *
 */
union sctp_notification {
	struct {
		__u16 sn_type;             /* Notification type. */
		__u16 sn_flags;
		__u32 sn_length;
	} sn_header;
	struct sctp_assoc_change sn_assoc_change;
	struct sctp_paddr_change sn_paddr_change;
	struct sctp_remote_error sn_remote_error;
	struct sctp_send_failed sn_send_failed;
	struct sctp_shutdown_event sn_shutdown_event;
	struct sctp_adaptation_event sn_adaptation_event;
	struct sctp_pdapi_event sn_pdapi_event;
	struct sctp_authkey_event sn_authkey_event;
	struct sctp_sender_dry_event sn_sender_dry_event;
	struct sctp_stream_reset_event sn_strreset_event;
	struct sctp_assoc_reset_event sn_assocreset_event;
	struct sctp_stream_change_event sn_strchange_event;
};

/* Section 5.3.1
 * All standard values for sn_type flags are greater than 2^15.
 * Values from 2^15 and down are reserved.
 */

enum sctp_sn_type {
	SCTP_SN_TYPE_BASE     = (1<<15),
	SCTP_ASSOC_CHANGE,
#define SCTP_ASSOC_CHANGE		SCTP_ASSOC_CHANGE
	SCTP_PEER_ADDR_CHANGE,
#define SCTP_PEER_ADDR_CHANGE		SCTP_PEER_ADDR_CHANGE
	SCTP_SEND_FAILED,
#define SCTP_SEND_FAILED		SCTP_SEND_FAILED
	SCTP_REMOTE_ERROR,
#define SCTP_REMOTE_ERROR		SCTP_REMOTE_ERROR
	SCTP_SHUTDOWN_EVENT,
#define SCTP_SHUTDOWN_EVENT		SCTP_SHUTDOWN_EVENT
	SCTP_PARTIAL_DELIVERY_EVENT,
#define SCTP_PARTIAL_DELIVERY_EVENT	SCTP_PARTIAL_DELIVERY_EVENT
	SCTP_ADAPTATION_INDICATION,
#define SCTP_ADAPTATION_INDICATION	SCTP_ADAPTATION_INDICATION
	SCTP_AUTHENTICATION_EVENT,
#define SCTP_AUTHENTICATION_INDICATION	SCTP_AUTHENTICATION_EVENT
	SCTP_SENDER_DRY_EVENT,
#define SCTP_SENDER_DRY_EVENT		SCTP_SENDER_DRY_EVENT
	SCTP_STREAM_RESET_EVENT,
#define SCTP_STREAM_RESET_EVENT		SCTP_STREAM_RESET_EVENT
	SCTP_ASSOC_RESET_EVENT,
#define SCTP_ASSOC_RESET_EVENT		SCTP_ASSOC_RESET_EVENT
	SCTP_STREAM_CHANGE_EVENT,
#define SCTP_STREAM_CHANGE_EVENT	SCTP_STREAM_CHANGE_EVENT
};

/* Notification error codes used to fill up the error fields in some
 * notifications.
 * SCTP_PEER_ADDRESS_CHAGE 	: spc_error
 * SCTP_ASSOC_CHANGE		: sac_error
 * These names should be potentially included in the draft 04 of the SCTP
 * sockets API specification.
 */
typedef enum sctp_sn_error {
	SCTP_FAILED_THRESHOLD,
	SCTP_RECEIVED_SACK,
	SCTP_HEARTBEAT_SUCCESS,
	SCTP_RESPONSE_TO_USER_REQ,
	SCTP_INTERNAL_ERROR,
	SCTP_SHUTDOWN_GUARD_EXPIRES,
	SCTP_PEER_FAULTY,
} sctp_sn_error_t;

/*
 * 7.1.1 Retransmission Timeout Parameters (SCTP_RTOINFO)
 *
 *   The protocol parameters used to initialize and bound retransmission
 *   timeout (RTO) are tunable.  See [SCTP] for more information on how
 *   these parameters are used in RTO calculation.
 */
struct sctp_rtoinfo {
	sctp_assoc_t	srto_assoc_id;
	__u32		srto_initial;
	__u32		srto_max;
	__u32		srto_min;
};

/*
 * 7.1.2 Association Parameters (SCTP_ASSOCINFO)
 *
 *   This option is used to both examine and set various association and
 *   endpoint parameters.
 */
struct sctp_assocparams {
	sctp_assoc_t	sasoc_assoc_id;
	__u16		sasoc_asocmaxrxt;
	__u16		sasoc_number_peer_destinations;
	__u32		sasoc_peer_rwnd;
	__u32		sasoc_local_rwnd;
	__u32		sasoc_cookie_life;
};

/*
 * 7.1.9 Set Peer Primary Address (SCTP_SET_PEER_PRIMARY_ADDR)
 *
 *  Requests that the peer mark the enclosed address as the association
 *  primary. The enclosed address must be one of the association's
 *  locally bound addresses. The following structure is used to make a
 *   set primary request:
 */
struct sctp_setpeerprim {
	sctp_assoc_t            sspp_assoc_id;
	struct sockaddr_storage sspp_addr;
} __attribute__((packed, aligned(4)));

/*
 * 7.1.10 Set Primary Address (SCTP_PRIMARY_ADDR)
 *
 *  Requests that the local SCTP stack use the enclosed peer address as
 *  the association primary. The enclosed address must be one of the
 *  association peer's addresses. The following structure is used to
 *  make a set peer primary request:
 */
struct sctp_prim {
	sctp_assoc_t            ssp_assoc_id;
	struct sockaddr_storage ssp_addr;
} __attribute__((packed, aligned(4)));

/* For backward compatibility use, define the old name too */
#define sctp_setprim	sctp_prim

/*
 * 7.1.11 Set Adaptation Layer Indicator (SCTP_ADAPTATION_LAYER)
 *
 * Requests that the local endpoint set the specified Adaptation Layer
 * Indication parameter for all future INIT and INIT-ACK exchanges.
 */
struct sctp_setadaptation {
	__u32	ssb_adaptation_ind;
};

/*
 * 7.1.13 Peer Address Parameters  (SCTP_PEER_ADDR_PARAMS)
 *
 *   Applications can enable or disable heartbeats for any peer address
 *   of an association, modify an address's heartbeat interval, force a
 *   heartbeat to be sent immediately, and adjust the address's maximum
 *   number of retransmissions sent before an address is considered
 *   unreachable. The following structure is used to access and modify an
 *   address's parameters:
 */
enum  sctp_spp_flags {
	SPP_HB_ENABLE = 1<<0,		/*Enable heartbeats*/
	SPP_HB_DISABLE = 1<<1,		/*Disable heartbeats*/
	SPP_HB = SPP_HB_ENABLE | SPP_HB_DISABLE,
	SPP_HB_DEMAND = 1<<2,		/*Send heartbeat immediately*/
	SPP_PMTUD_ENABLE = 1<<3,	/*Enable PMTU discovery*/
	SPP_PMTUD_DISABLE = 1<<4,	/*Disable PMTU discovery*/
	SPP_PMTUD = SPP_PMTUD_ENABLE | SPP_PMTUD_DISABLE,
	SPP_SACKDELAY_ENABLE = 1<<5,	/*Enable SACK*/
	SPP_SACKDELAY_DISABLE = 1<<6,	/*Disable SACK*/
	SPP_SACKDELAY = SPP_SACKDELAY_ENABLE | SPP_SACKDELAY_DISABLE,
	SPP_HB_TIME_IS_ZERO = 1<<7,	/* Set HB delay to 0 */
};

struct sctp_paddrparams {
	sctp_assoc_t		spp_assoc_id;
	struct sockaddr_storage	spp_address;
	__u32			spp_hbinterval;
	__u16			spp_pathmaxrxt;
	__u32			spp_pathmtu;
	__u32			spp_sackdelay;
	__u32			spp_flags;
} __attribute__((packed, aligned(4)));

/*
 * 7.1.18.  Add a chunk that must be authenticated (SCTP_AUTH_CHUNK)
 *
 * This set option adds a chunk type that the user is requesting to be
 * received only in an authenticated way.  Changes to the list of chunks
 * will only effect future associations on the socket.
 */
struct sctp_authchunk {
	__u8		sauth_chunk;
};

/*
 * 7.1.19.  Get or set the list of supported HMAC Identifiers (SCTP_HMAC_IDENT)
 *
 * This option gets or sets the list of HMAC algorithms that the local
 * endpoint requires the peer to use.
 */
#ifndef __KERNEL__
/* This here is only used by user space as is. It might not be a good idea
 * to export/reveal the whole structure with reserved fields etc.
 */
enum {
	SCTP_AUTH_HMAC_ID_SHA1 = 1,
	SCTP_AUTH_HMAC_ID_SHA256 = 3,
};
#endif

struct sctp_hmacalgo {
	__u32		shmac_num_idents;
	__u16		shmac_idents[];
};

/* Sadly, user and kernel space have different names for
 * this structure member, so this is to not break anything.
 */
#define shmac_number_of_idents	shmac_num_idents

/*
 * 7.1.20.  Set a shared key (SCTP_AUTH_KEY)
 *
 * This option will set a shared secret key which is used to build an
 * association shared key.
 */
struct sctp_authkey {
	sctp_assoc_t	sca_assoc_id;
	__u16		sca_keynumber;
	__u16		sca_keylength;
	__u8		sca_key[];
};

/*
 * 7.1.21.  Get or set the active shared key (SCTP_AUTH_ACTIVE_KEY)
 *
 * This option will get or set the active shared key to be used to build
 * the association shared key.
 */

struct sctp_authkeyid {
	sctp_assoc_t	scact_assoc_id;
	__u16		scact_keynumber;
};


/*
 * 7.1.23.  Get or set delayed ack timer (SCTP_DELAYED_SACK)
 *
 * This option will effect the way delayed acks are performed.  This
 * option allows you to get or set the delayed ack time, in
 * milliseconds.  It also allows changing the delayed ack frequency.
 * Changing the frequency to 1 disables the delayed sack algorithm.  If
 * the assoc_id is 0, then this sets or gets the endpoints default
 * values.  If the assoc_id field is non-zero, then the set or get
 * effects the specified association for the one to many model (the
 * assoc_id field is ignored by the one to one model).  Note that if
 * sack_delay or sack_freq are 0 when setting this option, then the
 * current values will remain unchanged.
 */
struct sctp_sack_info {
	sctp_assoc_t	sack_assoc_id;
	uint32_t	sack_delay;
	uint32_t	sack_freq;
};

struct sctp_assoc_value {
    sctp_assoc_t            assoc_id;
    uint32_t                assoc_value;
};

struct sctp_stream_value {
	sctp_assoc_t assoc_id;
	uint16_t stream_id;
	uint16_t stream_value;
};

/*
 * 7.2.2 Peer Address Information
 *
 *   Applications can retrieve information about a specific peer address
 *   of an association, including its reachability state, congestion
 *   window, and retransmission timer values.  This information is
 *   read-only. The following structure is used to access this
 *   information:
 */
struct sctp_paddrinfo {
	sctp_assoc_t		spinfo_assoc_id;
	struct sockaddr_storage	spinfo_address;
	__s32			spinfo_state;
	__u32			spinfo_cwnd;
	__u32			spinfo_srtt;
	__u32			spinfo_rto;
	__u32			spinfo_mtu;
} __attribute__((packed, aligned(4)));

/* Peer addresses's state. */
/* UNKNOWN: Peer address passed by the upper layer in sendmsg or connect[x]
 * calls.
 * UNCONFIRMED: Peer address received in INIT/INIT-ACK address parameters.
 *              Not yet confirmed by a heartbeat and not available for data
 *		transfers.
 * ACTIVE : Peer address confirmed, active and available for data transfers.
 * INACTIVE: Peer address inactive and not available for data transfers.
 */
enum sctp_spinfo_state {
	SCTP_INACTIVE,
	SCTP_PF,
	SCTP_ACTIVE,
	SCTP_UNCONFIRMED,
	SCTP_UNKNOWN = 0xffff  /* Value used for transport state unknown */
};

/*
 * 7.2.1 Association Status (SCTP_STATUS)
 *
 *   Applications can retrieve current status information about an
 *   association, including association state, peer receiver window size,
 *   number of unacked data chunks, and number of data chunks pending
 *   receipt.  This information is read-only.  The following structure is
 *   used to access this information:
 */
struct sctp_status {
	sctp_assoc_t		sstat_assoc_id;
	__s32			sstat_state;
	__u32			sstat_rwnd;
	__u16			sstat_unackdata;
	__u16			sstat_penddata;
	__u16			sstat_instrms;
	__u16			sstat_outstrms;
	__u32			sstat_fragmentation_point;
	struct sctp_paddrinfo	sstat_primary;
};

/*
 * 7.2.3.  Get the list of chunks the peer requires to be authenticated
 *         (SCTP_PEER_AUTH_CHUNKS)
 *
 * This option gets a list of chunks for a specified association that
 * the peer requires to be received authenticated only.
 */
struct sctp_authchunks {
	sctp_assoc_t	gauth_assoc_id;
	__u32		gauth_number_of_chunks;
	uint8_t		gauth_chunks[];
};

/* The broken spelling has been released already in lksctp-tools header,
 * so don't break anyone, now that it's fixed.
 */
#define guth_number_of_chunks	gauth_number_of_chunks

/* Association states.  */
enum sctp_sstat_state {
	SCTP_EMPTY                = 0,
	SCTP_CLOSED               = 1,
	SCTP_COOKIE_WAIT          = 2,
	SCTP_COOKIE_ECHOED        = 3,
	SCTP_ESTABLISHED          = 4,
	SCTP_SHUTDOWN_PENDING     = 5,
	SCTP_SHUTDOWN_SENT        = 6,
	SCTP_SHUTDOWN_RECEIVED    = 7,
	SCTP_SHUTDOWN_ACK_SENT    = 8,
};

/*
 * 8.2.6. Get the Current Identifiers of Associations
 *        (SCTP_GET_ASSOC_ID_LIST)
 *
 * This option gets the current list of SCTP association identifiers of
 * the SCTP associations handled by a one-to-many style socket.
 */
struct sctp_assoc_ids {
	__u32		gaids_number_of_ids;
	sctp_assoc_t	gaids_assoc_id[];
};

/*
 * 8.3, 8.5 get all peer/local addresses in an association.
 * This parameter struct is used by SCTP_GET_PEER_ADDRS and
 * SCTP_GET_LOCAL_ADDRS socket options used internally to implement
 * sctp_getpaddrs() and sctp_getladdrs() API.
 */
struct sctp_getaddrs_old {
	sctp_assoc_t            assoc_id;
	int			addr_num;
#ifdef __KERNEL__
	struct sockaddr		__user *addrs;
#else
	struct sockaddr		*addrs;
#endif
};

struct sctp_getaddrs {
	sctp_assoc_t		assoc_id; /*input*/
	__u32			addr_num; /*output*/
	__u8			addrs[0]; /*output, variable size*/
};

/* A socket user request obtained via SCTP_GET_ASSOC_STATS that retrieves
 * association stats. All stats are counts except sas_maxrto and
 * sas_obs_rto_ipaddr. maxrto is the max observed rto + transport since
 * the last call. Will return 0 when RTO was not update since last call
 */
struct sctp_assoc_stats {
	sctp_assoc_t	sas_assoc_id;    /* Input */
					 /* Transport of observed max RTO */
	struct sockaddr_storage sas_obs_rto_ipaddr;
	__u64		sas_maxrto;      /* Maximum Observed RTO for period */
	__u64		sas_isacks;	 /* SACKs received */
	__u64		sas_osacks;	 /* SACKs sent */
	__u64		sas_opackets;	 /* Packets sent */
	__u64		sas_ipackets;	 /* Packets received */
	__u64		sas_rtxchunks;   /* Retransmitted Chunks */
	__u64		sas_outofseqtsns;/* TSN received > next expected */
	__u64		sas_idupchunks;  /* Dups received (ordered+unordered) */
	__u64		sas_gapcnt;      /* Gap Acknowledgements Received */
	__u64		sas_ouodchunks;  /* Unordered data chunks sent */
	__u64		sas_iuodchunks;  /* Unordered data chunks received */
	__u64		sas_oodchunks;	 /* Ordered data chunks sent */
	__u64		sas_iodchunks;	 /* Ordered data chunks received */
	__u64		sas_octrlchunks; /* Control chunks sent */
	__u64		sas_ictrlchunks; /* Control chunks received */
};

/*
 * 8.1 sctp_bindx()
 *
 * The flags parameter is formed from the bitwise OR of zero or more of the
 * following currently defined flags:
 */
#define SCTP_BINDX_ADD_ADDR 0x01
#define SCTP_BINDX_REM_ADDR 0x02

/* This is the structure that is passed as an argument(optval) to
 * getsockopt(SCTP_SOCKOPT_PEELOFF).
 */
typedef struct {
	sctp_assoc_t associd;
	int sd;
} sctp_peeloff_arg_t;

typedef struct {
	sctp_peeloff_arg_t p_arg;
	unsigned flags;
} sctp_peeloff_flags_arg_t;

/*
 *  Peer Address Thresholds socket option
 */
struct sctp_paddrthlds {
	sctp_assoc_t spt_assoc_id;
	struct sockaddr_storage spt_address;
	__u16 spt_pathmaxrxt;
	__u16 spt_pathpfthld;
};

/*
 * Socket Option for Getting the Association/Stream-Specific PR-SCTP Status
 */
struct sctp_prstatus {
	sctp_assoc_t sprstat_assoc_id;
	__u16 sprstat_sid;
	__u16 sprstat_policy;
	__u64 sprstat_abandoned_unsent;
	__u64 sprstat_abandoned_sent;
};

struct sctp_default_prinfo {
	sctp_assoc_t pr_assoc_id;
	__u32 pr_value;
	__u16 pr_policy;
};

struct sctp_info {
	__u32	sctpi_tag;
	__u32	sctpi_state;
	__u32	sctpi_rwnd;
	__u16	sctpi_unackdata;
	__u16	sctpi_penddata;
	__u16	sctpi_instrms;
	__u16	sctpi_outstrms;
	__u32	sctpi_fragmentation_point;
	__u32	sctpi_inqueue;
	__u32	sctpi_outqueue;
	__u32	sctpi_overall_error;
	__u32	sctpi_max_burst;
	__u32	sctpi_maxseg;
	__u32	sctpi_peer_rwnd;
	__u32	sctpi_peer_tag;
	__u8	sctpi_peer_capable;
	__u8	sctpi_peer_sack;
	__u16	__reserved1;

	/* assoc status info */
	__u64	sctpi_isacks;
	__u64	sctpi_osacks;
	__u64	sctpi_opackets;
	__u64	sctpi_ipackets;
	__u64	sctpi_rtxchunks;
	__u64	sctpi_outofseqtsns;
	__u64	sctpi_idupchunks;
	__u64	sctpi_gapcnt;
	__u64	sctpi_ouodchunks;
	__u64	sctpi_iuodchunks;
	__u64	sctpi_oodchunks;
	__u64	sctpi_iodchunks;
	__u64	sctpi_octrlchunks;
	__u64	sctpi_ictrlchunks;

	/* primary transport info */
	struct sockaddr_storage	sctpi_p_address;
	__s32	sctpi_p_state;
	__u32	sctpi_p_cwnd;
	__u32	sctpi_p_srtt;
	__u32	sctpi_p_rto;
	__u32	sctpi_p_hbinterval;
	__u32	sctpi_p_pathmaxrxt;
	__u32	sctpi_p_sackdelay;
	__u32	sctpi_p_sackfreq;
	__u32	sctpi_p_ssthresh;
	__u32	sctpi_p_partial_bytes_acked;
	__u32	sctpi_p_flight_size;
	__u16	sctpi_p_error;
	__u16	__reserved2;

	/* sctp sock info */
	__u32	sctpi_s_autoclose;
	__u32	sctpi_s_adaptation_ind;
	__u32	sctpi_s_pd_point;
	__u8	sctpi_s_nodelay;
	__u8	sctpi_s_disable_fragments;
	__u8	sctpi_s_v4mapped;
	__u8	sctpi_s_frag_interleave;
	__u32	sctpi_s_type;
	__u32	__reserved3;
};

struct sctp_reset_streams {
	sctp_assoc_t srs_assoc_id;
	uint16_t srs_flags;
	uint16_t srs_number_streams;	/* 0 == ALL */
	uint16_t srs_stream_list[];	/* list if srs_num_streams is not 0 */
};

struct sctp_add_streams {
	sctp_assoc_t sas_assoc_id;
	uint16_t sas_instrms;
	uint16_t sas_outstrms;
};

/* SCTP Stream schedulers */
enum sctp_sched_type {
	SCTP_SS_FCFS,
	SCTP_SS_PRIO,
	SCTP_SS_RR,
	SCTP_SS_MAX = SCTP_SS_RR
};

#endif /* _UAPI_SCTP_H */
