/*	$OpenBSD: l2tp.h,v 1.15 2024/02/26 08:29:37 yasuoka Exp $	*/

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef	L2TP_H
#define	L2TP_H 1
/*@file
 * header file for the L2TP module
 */
/* $Id: l2tp.h,v 1.15 2024/02/26 08:29:37 yasuoka Exp $ */

/************************************************************************
 * Protocol Constants
 ************************************************************************/

#define	L2TP_RFC2661_VERSION			1
#define	L2TP_RFC2661_REVISION			0
#define	L2TP_AVP_MAXSIZ				1024

/* Header */

#define	L2TP_HEADER_FLAG_TOM			0x8000
#define	L2TP_HEADER_FLAG_LENGTH			0x4000
#define	L2TP_HEADER_FLAG_SEQUENCE		0x0800
#define	L2TP_HEADER_FLAG_OFFSET			0x0200
#define	L2TP_HEADER_FLAG_PRIORITY		0x0100
#define	L2TP_HEADER_FLAG_VERSION_MASK		0x000f
#define	L2TP_HEADER_VERSION_RFC2661		0x02

/* AVP Attribute Types */

/* RFC 2661 */
#define	L2TP_AVP_TYPE_MESSAGE_TYPE		0
#define	L2TP_AVP_TYPE_RESULT_CODE		1
#define	L2TP_AVP_TYPE_PROTOCOL_VERSION		2
#define	L2TP_AVP_TYPE_FRAMING_CAPABILITIES	3
#define	L2TP_AVP_TYPE_BEARER_CAPABILITIES	4
#define	L2TP_AVP_TYPE_TIE_BREAKER		5
#define	L2TP_AVP_TYPE_FIRMWARE_REVISION		6
#define	L2TP_AVP_TYPE_HOST_NAME			7
#define	L2TP_AVP_TYPE_VENDOR_NAME		8
#define	L2TP_AVP_TYPE_ASSINGED_TUNNEL_ID	9
#define	L2TP_AVP_TYPE_RECV_WINDOW_SIZE		10
#define	L2TP_AVP_TYPE_CHALLENGE			11
#define	L2TP_AVP_TYPE_CAUSE_CODE		12
#define	L2TP_AVP_TYPE_CHALLENGE_RESPONSE	13
#define	L2TP_AVP_TYPE_ASSIGNED_SESSION_ID	14
#define	L2TP_AVP_TYPE_CALL_SERIAL_NUMBER	15
#define	L2TP_AVP_TYPE_MINIMUM_BPS		16
#define	L2TP_AVP_TYPE_MAXIMUM_BPS		17
#define	L2TP_AVP_TYPE_BEARER_TYPE		18
#define	L2TP_AVP_TYPE_FRAMING_TYPE		19
#define	L2TP_AVP_TYPE_CALLED_NUMBER		21
#define	L2TP_AVP_TYPE_CALLING_NUMBER		22
#define	L2TP_AVP_TYPE_SUB_ADDRESS		23
#define	L2TP_AVP_TYPE_TX_CONNECT_SPEED		24

#define	L2TP_AVP_TYPE_PHYSICAL_CHANNEL_ID	25
#define	L2TP_AVP_TYPE_INITIAL_RECV_LCP_CONFREQ	26
#define	L2TP_AVP_TYPE_LAST_SENT_LCP_CONFREQ	27
#define	L2TP_AVP_TYPE_LAST_RECV_LCP_CONFREQ	28
#define	L2TP_AVP_TYPE_PROXY_AUTHEN_TYPE		29
#define	L2TP_AVP_TYPE_PROXY_AUTHEN_NAME		30
#define	L2TP_AVP_TYPE_PROXY_AUTHEN_CHALLENGE	31
#define	L2TP_AVP_TYPE_PROXY_AUTHEN_ID		32
#define	L2TP_AVP_TYPE_PROXY_AUTHEN_RESPONSE	33
#define	L2TP_AVP_TYPE_CALL_ERRORS		34
#define	L2TP_AVP_TYPE_ACCM			35
#define	L2TP_AVP_TYPE_RANDOM_VECTOR		36
#define	L2TP_AVP_TYPE_PRIVATE_GROUP_ID		37
#define	L2TP_AVP_TYPE_RX_CONNECT_SPEED		38
#define	L2TP_AVP_TYPE_SEQUENCING_REQUIRED	39


/* RFC 3301 */
#define	L2TP_AVP_TYPE_TX_MINIMUM		40
#define	L2TP_AVP_TYPE_CALLING_SUB_ADDRESS	44

/* RFC 3145 */
#define	L2TP_AVP_TYPE_PPP_DISCONNECT_CAUSE_CODE	46

/* RFC 3308 */
#define	L2TP_AVP_TYPE_CCDS			47
#define	L2TP_AVP_TYPE_SDS			48

/* RFC 3437 */
#define	L2TP_AVP_TYPE_LCP_WANT_OPTIONS		49
#define	L2TP_AVP_TYPE_LCP_ALLOW_OPTIONS		50
#define	L2TP_AVP_TYPE_LNS_LAST_SENT_LCP_CONFREQ	51
#define	L2TP_AVP_TYPE_LNS_LAST_RECV_LCP_CONFREQ	52

/* RFC 3573 */
#define	L2TP_AVP_TYPE_MODEM_ON_HOLD_CAPABLE	53
#define	L2TP_AVP_TYPE_MODEM_ON_HOLD_STATUS	54

/* RFC 3817 */
#define	L2TP_AVP_TYPE_PPPOE_RELAY		55
#define	L2TP_AVP_TYPE_PPPOE_RELAY_RESP_CAP	56
#define	L2TP_AVP_TYPE_PPPOE_RELAY_FORW_CAP	57

/* No RFC yet */
#define	L2TP_AVP_TYPE_EXTENDED_VENDOR_ID	58
#define	L2TP_AVP_TYPE_PSEUDOWIRE_CAP_LIST	62
#define	L2TP_AVP_TYPE_LOCAL_SESSION_ID		63
#define	L2TP_AVP_TYPE_REMOTE_SESSION_ID		64
#define	L2TP_AVP_TYPE_ASSIGNED_COOKIE		65
#define	L2TP_AVP_TYPE_REMOTE_END_ID		66
#define	L2TP_AVP_TYPE_APPLICATION_CODE		67
#define	L2TP_AVP_TYPE_PSEUDOWIRE_TYPE		68
#define	L2TP_AVP_TYPE_L2_SPECIFIC_SUBLAYER	69
#define	L2TP_AVP_TYPE_DATA_SEQUENCING		70
#define	L2TP_AVP_TYPE_CIRCUIT_STATUS		71
#define	L2TP_AVP_TYPE_PREFERRED_LANGUAGE	72
#define	L2TP_AVP_TYPE_CTRL_MSG_AUTH_NONCE	73
/* #define	L2TP_AVP_TYPE_TX_CONNECT_SPEED		74 */
/* #define	L2TP_AVP_TYPE_RX_CONNECT_SPEED		75 */
#define	L2TP_AVP_TYPE_FAILOVER_CAPABILITY	76
#define	L2TP_AVP_TYPE_TUNNEL_RECOVERY		77
#define	L2TP_AVP_TYPE_SUGGESTED_CTRL_SEQUENCE	78
#define	L2TP_AVP_TYPE_FAILOVER_SESSION_STATE	79

/* RFC 4045 */
#define	L2TP_AVP_TYPE_MULTICAST_CAPABILITY	80
#define	L2TP_AVP_TYPE_NEW_OUTGOING_SESSIONS	81
#define	L2TP_AVP_TYPE_NEW_OUTGOING_SESSIONS_ACK	82
#define	L2TP_AVP_TYPE_WITHDRAW_OUTGOING_SESSIONS 83
#define	L2TP_AVP_TYPE_MULTICAST_PACKETS_PRIORITY 84

/* Control Message Type */

#define	L2TP_AVP_MESSAGE_TYPE_SCCRQ		1
#define	L2TP_AVP_MESSAGE_TYPE_SCCRP		2
#define	L2TP_AVP_MESSAGE_TYPE_SCCCN		3
#define	L2TP_AVP_MESSAGE_TYPE_StopCCN		4
#define	L2TP_AVP_MESSAGE_TYPE_HELLO		6
#define	L2TP_AVP_MESSAGE_TYPE_OCRQ		7
#define	L2TP_AVP_MESSAGE_TYPE_OCRP		8
#define	L2TP_AVP_MESSAGE_TYPE_OCCN		9
#define	L2TP_AVP_MESSAGE_TYPE_ICRQ		10
#define	L2TP_AVP_MESSAGE_TYPE_ICRP		11
#define	L2TP_AVP_MESSAGE_TYPE_ICCN		12
#define	L2TP_AVP_MESSAGE_TYPE_CDN		14

#define L2TP_FRAMING_CAP_FLAGS_SYNC	0x00000001
#define L2TP_FRAMING_CAP_FLAGS_ASYNC	0x00000002
#define	L2TP_BEARER_CAP_FLAGS_DIGITAL	0x00000001
#define	L2TP_BEARER_CAP_FLAGS_ANALOG	0x00000002

/*
 * Constants on pp.19-22 of RFC2661
 * macro names may be inappropriate.
 */
#define	L2TP_STOP_CCN_RCODE_GENERAL			1
#define	L2TP_STOP_CCN_RCODE_GENERAL_ERROR		2
#define	L2TP_STOP_CCN_RCODE_ALREADY_EXISTS		3
#define	L2TP_STOP_CCN_RCODE_UNAUTHORIZED		4
#define	L2TP_STOP_CCN_RCODE_BAD_PROTOCOL_VERSION	5
#define	L2TP_STOP_CCN_RCODE_SHUTTING_DOWN		6
#define	L2TP_STOP_CCN_RCODE_FSM_ERROR			7

#define L2TP_CDN_RCODE_LOST_CARRIER			1
#define L2TP_CDN_RCODE_ERROR_CODE			2
#define L2TP_CDN_RCODE_ADMINISTRATIVE_REASON		3
#define L2TP_CDN_RCODE_TEMP_NOT_AVALIABLE		4
#define L2TP_CDN_RCODE_PERM_NOT_AVALIABLE		5
#define L2TP_CDN_RCODE_INVALID_DESTINATION		6
#define L2TP_CDN_RCODE_NO_CARRIER			7
#define L2TP_CDN_RCODE_BUSY				8
#define L2TP_CDN_RCODE_NO_DIALTONE			9
#define L2TP_CDN_RCODE_CALL_TIMEOUT_BY_LAC		10
#define L2TP_CDN_RCODE_NO_FRAMING_DETECTED		11

#define L2TP_ECODE_NO_CONTROL_CONNECTION		1
#define L2TP_ECODE_WRONG_LENGTH				2
#define L2TP_ECODE_INVALID_MESSAGE			3
#define L2TP_ECODE_NO_RESOURCE				4
#define L2TP_ECODE_INVALID_SESSION_ID			5
#define L2TP_ECODE_GENERIC_ERROR			6
#define L2TP_ECODE_TRY_ANOTHER				7
#define L2TP_ECODE_UNKNOWN_MANDATORY_AVP		8

/* Proxy Authen Type */
#define	L2TP_AUTH_TYPE_RESERVED				0
#define	L2TP_AUTH_TYPE_TEXUAL				1
#define	L2TP_AUTH_TYPE_PPP_CHAP				2
#define	L2TP_AUTH_TYPE_PPP_PAP				3
#define	L2TP_AUTH_TYPE_NO_AUTH				4
#define	L2TP_AUTH_TYPE_MS_CHAP_V1			5

/************************************************************************
 * Implementation Specific Constants
 ************************************************************************/
#include "l2tp_conf.h"

#define	L2TPD_TUNNEL_HASH_SIZ			127
#define L2TPD_SND_BUFSIZ			2048
#define	L2TPD_DEFAULT_SEND_WINSZ		4
#define	L2TPD_DEFAULT_LAYER2_LABEL		"L2TP"
#define	L2TPD_DIALIN_LAYER2_LABEL		"DialIn"
#define	L2TPD_CONFIG_BUFSIZ			65535
#define	L2TP_CTRL_WINDOW_SIZE			8
#ifndef	L2TPD_VENDOR_NAME
#define	L2TPD_VENDOR_NAME			""
#endif
#define L2TPD_DEFAULT_UDP_PORT			1701

/** maximum number of addresses we will listen on */
#ifndef	L2TP_NLISTENER
#define	L2TP_NLISTENER				6
#endif

/*
 * state of daemon
 */
#define	L2TPD_STATE_INIT			0
#define	L2TPD_STATE_RUNNING			1
#define	L2TPD_STATE_SHUTTING_DOWN		2
#define	L2TPD_STATE_STOPPED			3

/*
 * state of a control connection
 */
#define	L2TP_CTRL_STATE_IDLE			0
#define	L2TP_CTRL_STATE_WAIT_CTL_CONN		1
#define	L2TP_CTRL_STATE_WAIT_CTL_REPLY		2
#define	L2TP_CTRL_STATE_ESTABLISHED		3
#define	L2TP_CTRL_STATE_CLEANUP_WAIT		4

/*
 * state of a call
 */
#define	L2TP_CALL_STATE_IDLE			0
#define	L2TP_CALL_STATE_WAIT_CONN		1
#define	L2TP_CALL_STATE_ESTABLISHED		2
#define	L2TP_CALL_STATE_CLEANUP_WAIT		3

/*
 * timeout
 */
#define	L2TP_CTRL_CTRL_PKT_TIMEOUT		12
/** wait time for the first call */
#define	L2TP_CTRL_WAIT_CALL_TIMEOUT		16
#define	L2TP_CTRL_CLEANUP_WAIT_TIME		3
#define	L2TP_CTRL_DEFAULT_HELLO_INTERVAL	60
#define	L2TP_CTRL_DEFAULT_HELLO_TIMEOUT		30

#define	L2TPD_SHUTDOWN_TIMEOUT			5

/** returns whether an L2TP daemon is stopped */
#define	l2tpd_is_stopped(l2tpd)					\
	(((l2tpd)->state != L2TPD_STATE_SHUTTING_DOWN &&	\
	    (l2tpd)->state != L2TPD_STATE_RUNNING)? 1 : 0)

/** returns whether an L2TP daemon is going to shutdown */
#define	l2tpd_is_shutting_down(l2tpd)				\
	(((l2tpd)->state == L2TPD_STATE_SHUTTING_DOWN)? 1 : 0)

/** macro to retrieve a physical layer label from l2tp_ctrl */
#define	L2TP_CTRL_LISTENER_TUN_NAME(ctrl)	\
	((l2tpd_listener *)slist_get(&(ctrl)->l2tpd->listener, \
	    (ctrl)->listener_index))->tun_name

#define	L2TP_CTRL_CONF(ctrl)					\
	((l2tpd_listener *)slist_get(&(ctrl)->l2tpd->listener,	\
	    (ctrl)->listener_index))->conf

#define L2TP_CALL_DELAY_LIMIT 64

/** datatype represents L2TP daemon */
struct _l2tpd;

typedef struct _l2tpd_listener {
	/* configuration */
	struct l2tp_conf *conf;
	/** event context */
	struct event ev_sock;
	/** L2TPD itself */
	struct _l2tpd	*self;
	/** index number */
	uint16_t	index;
	/** enable/disable */
	uint16_t	enabled;
	/** listening socket */
	int		sock;
	/** listening socket address for UDP packets */
	union {
		struct sockaddr_in	sin4;
		struct sockaddr_in6	sin6;
	} bind;
	/** tunnel name */
	char	tun_name[L2TP_NAME_LEN];
} l2tpd_listener;

/** datatype represents L2TP daemon */
typedef struct _l2tpd {
	/** timeout event context */
	struct event ev_timeout;
	/** instance ID */
	u_int id;
	/** listener list */
	slist listener;
	/** state */
	int state;
	/** mappings from tunnel ID to {@link ::_l2tp_ctrl L2TP control} */
	hash_table *ctrl_map;
	/** unique and free Session-ID list */
	slist free_session_id_list;

	/** flags */
	uint32_t
	    purge_ipsec_sa:1;
} l2tpd;

/** datatype represents L2TP control connection */
typedef struct _l2tp_ctrl {
	struct event ev_timeout;
	/** ID */
	u_int id;
	/** parent L2TPD */
	l2tpd 	*l2tpd;
	/** listener index number */
	uint16_t	listener_index;
	/** state */
	int	state;
	/** tunnel Id */
	int	tunnel_id;
	/** window size */
	int	winsz;
	/** peer's tunnel Id */
	int	peer_tunnel_id;
	/** peer's window size */
	int	peer_winsz;
	/** next acknowledgement number */
	uint16_t	snd_una;
	/** next send sequence number */
	uint16_t	snd_nxt;
	/** last send sequence number */
	uint16_t	snd_last;
	/** last send ack number */
	uint16_t	snd_lastnr;
	/** receive sequence number */
	uint16_t	rcv_nxt;
	/** peer's IP address */
	struct	sockaddr_storage peer;
	/** my IP address */
	struct	sockaddr_storage sock;
	/** IPSEC NAT-T SA cookie */
	void	*sa_cookie;

	/** list of L2TP calls */
	slist	call_list;
	/*
	 * Note about send window:
	 * 	pos == lim when buffer is full.
	 * 	pos == -1, lim == 0 when buffer is empty.
	 */
	/** bytes available in send buffer.  it is a list of length #winsz */
	bytebuffer **snd_buffers;
	int snd_buffercnt;
	/** sending buffer for ZLB */
	bytebuffer *zlb_buffer;

	/** the time when last control message sent */
	time_t	last_snd_ctrl;
	/** the time when last packet received */
	time_t	last_rcv;

	/**
	 * If we are on active close and have not sent a StopCCN message yet,
	 * active_closing has the result code to be in the StopCCN message.
	 */
	int	active_closing;

	/**
	 * delay between transition to idle state and sending HELLO in seconds.
	 */
	int hello_interval;
	/** HELLO timeout */
	int hello_timeout;
	/** time when the last HELLO packet was sent */
	time_t	hello_io_time;
	/** number of calls established */
	int	ncalls;

	unsigned int /** use sequence number in L2TP Data Message? */
	    data_use_seq:1,
	    /** waiting to acknowledge HELLO? */
	    hello_wait_ack:1;

} l2tp_ctrl;

/**
 * datatype represents a L2TP call
 */
typedef struct _l2tp_call {
	/** ID */
	u_int		id;
	/** state */
	int		state;
	/** parent control connection */
	l2tp_ctrl 	*ctrl;
	/** bound {@link ::_npppd_ppp ppp} */
	void		*ppp;
	/** session ID */
	uint16_t	session_id;
	/** peer's session ID */
	uint16_t	peer_session_id;
	/** next sequence number  */
	uint16_t	snd_nxt;
	/** receiving sequence number */
	uint16_t	rcv_nxt;
	/** calling number */
	char		calling_number[32];

	uint32_t	/** Sequencing required */
			seq_required:1,
			/** Use sequencing in the data connection */
			use_seq:1;
} l2tp_call;

#ifdef __cplusplus
extern "C" {
#endif

l2tp_call        *l2tp_call_create (void);
int              l2tp_call_init (l2tp_call *, l2tp_ctrl *);
void             l2tp_call_destroy (l2tp_call *, int);
void             l2tp_call_admin_disconnect(l2tp_call *);
void             l2tp_call_drop (l2tp_call *);
int              l2tp_call_recv_packet (l2tp_ctrl *, l2tp_call *, int, u_char *, int);
void             l2tp_call_ppp_input (l2tp_call *, u_char *, int, int);

void             l2tp_ctrl_destroy (l2tp_ctrl *);
l2tp_ctrl        *l2tp_ctrl_create (void);
void             l2tp_ctrl_input (l2tpd *, int, struct sockaddr *, struct sockaddr *, void *, u_char *, int);
int              l2tp_ctrl_send(l2tp_ctrl *, const void *, int);
int              l2tp_ctrl_send_packet(l2tp_ctrl *, int, bytebuffer *);
int               l2tp_ctrl_stop (l2tp_ctrl *, int);
bytebuffer       *l2tp_ctrl_prepare_snd_buffer (l2tp_ctrl *, int);
void             l2tp_ctrl_log (l2tp_ctrl *, int, const char *, ...) __attribute__((__format__ (__printf__, 3, 4)));
int              l2tpd_init (l2tpd *);
void             l2tpd_uninit (l2tpd *);
int              l2tpd_assign_call (l2tpd *, l2tp_call *);
void             l2tpd_release_call (l2tpd *, l2tp_call *);
int              l2tpd_start (l2tpd *);
void             l2tpd_stop (l2tpd *);
void             l2tpd_stop_immediatly (l2tpd *);
l2tp_ctrl        *l2tpd_get_ctrl (l2tpd *, u_int);
void             l2tpd_add_ctrl (l2tpd *, l2tp_ctrl *);
void             l2tpd_ctrl_finished_notify(l2tpd *);
void             l2tpd_remove_ctrl (l2tpd *, u_int);
int              l2tpd_add_listener (l2tpd *, int, struct l2tp_conf *, struct sockaddr *);
void             l2tpd_log (l2tpd *, int, const char *, ...) __attribute__((__format__ (__printf__, 3, 4)));
int              l2tpd_reload(l2tpd *, struct l2tp_confs *);
void             l2tpd_log_access_deny(l2tpd *, const char *, struct sockaddr *);
#ifdef __cplusplus
}
#endif
#endif
