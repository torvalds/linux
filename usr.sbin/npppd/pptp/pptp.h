/*	$OpenBSD: pptp.h,v 1.12 2024/02/26 08:29:37 yasuoka Exp $	*/

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
#ifndef PPTP_H
#define PPTP_H	1

/*
 * PPTP protocol constants
 */
#define	PPTP_MES_TYPE_CTRL			1
#define	PPTP_MAGIC_COOKIE			0x1a2b3c4d
#define	PPTP_RFC_2637_VERSION			0x0100

#ifndef	PPTP_MAX_CALL
#define	PPTP_MAX_CALL				8192
#endif

/* Start-Control-Connection-Request */
#define	PPTP_CTRL_MES_CODE_SCCRQ	1

/* Start-Control-Connection-Reply */
#define	PPTP_CTRL_MES_CODE_SCCRP	2

/* Stop-Control-Connection-Request */
#define	PPTP_CTRL_MES_CODE_StopCCRQ	3

/* Stop-Control-Connection-Reply */
#define	PPTP_CTRL_MES_CODE_StopCCRP	4

/* Echo-Request */
#define	PPTP_CTRL_MES_CODE_ECHO_RQ	5

/* Echo-Reply */
#define	PPTP_CTRL_MES_CODE_ECHO_RP	6

/* Outgoing-Call-Request */
#define	PPTP_CTRL_MES_CODE_OCRQ		7

/* Outgoing-Call-Reply */
#define	PPTP_CTRL_MES_CODE_OCRP		8

/* Incoming-Call-Request */
#define	PPTP_CTRL_MES_CODE_ICRQ		9

/* Incoming-Call-Reply */
#define	PPTP_CTRL_MES_CODE_ICRP		10

/* Incoming-Call-Connected */
#define	PPTP_CTRL_MES_CODE_ICCN		11

/* Call-Clear-Request */
#define	PPTP_CTRL_MES_CODE_CCR		12

/* Call-Disconnect-Notify */
#define	PPTP_CTRL_MES_CODE_CDN		13

/* Set-Link-Info */
#define	PPTP_CTRL_MES_CODE_SLI		15


#define	PPTP_CTRL_FRAMING_ASYNC		1
#define	PPTP_CTRL_FRAMING_SYNC		2

#define	PPTP_CTRL_BEARER_ANALOG		1
#define	PPTP_CTRL_BEARER_DIGITAL	2


/* Result Code: Start-Control-Connection-Reply */
#define PPTP_SCCRP_RESULT_SUCCESS		1
#define PPTP_SCCRP_RESULT_GENERIC_ERROR		2
#define PPTP_SCCRP_RESULT_CHANNEL_EXISTS	3
#define PPTP_SCCRP_RESULT_NOT_AUTHORIZIZED	4
#define PPTP_SCCRP_RESULT_BAD_PROTOCOL_VERSION	5

/* General Error Code (RFC 2637 2.16 pp.36) */
#define PPTP_ERROR_NONE				0
#define PPTP_ERROR_NOT_CONNECTED		1
#define PPTP_ERROR_BAD_FORMAT			2
#define PPTP_ERROR_NO_RESOURCE			3
#define PPTP_ERROR_BAD_CALL			4
#define PPTP_ERROR_PAC_ERROR			5

/* Result Code: Outgoing-Call-Reply */
#define PPTP_OCRP_RESULT_CONNECTED		1
#define PPTP_OCRP_RESULT_GENERIC_ERROR		2
#define PPTP_OCRP_RESULT_NO_CARRIER		3
#define PPTP_OCRP_RESULT_BUSY			4
#define PPTP_OCRP_RESULT_NO_DIALTONE		5
#define PPTP_OCRP_RESULT_TIMEOUT		6
#define PPTP_OCRP_RESULT_DO_NOT_ACCEPT		7

/* Result Code: Echo-Reply */
#define PPTP_ECHO_RP_RESULT_OK			1
#define PPTP_ECHO_RP_RESULT_GENERIC_ERROR	2

/* Reason code of the Stop-Control-Connection-Request */
#define	PPTP_StopCCRQ_REASON_NONE			1
#define	PPTP_StopCCRQ_REASON_STOP_PROTOCOL		2
#define	PPTP_StopCCRQ_REASON_STOP_LOCAL_SHUTDOWN	3

/* Result code of the Stop-Control-Connection-Response */
#define	PPTP_StopCCRP_RESULT_OK			1
#define	PPTP_StopCCRP_RESULT_GENERIC_ERROR	2

#define	PPTP_CDN_RESULT_LOST_CARRIER		1
#define	PPTP_CDN_RESULT_GENRIC_ERROR		2
#define	PPTP_CDN_RESULT_ADMIN_SHUTDOWN		3
#define	PPTP_CDN_RESULT_REQUEST			4

/* Default TCP port number */
#define	PPTPD_DEFAULT_TCP_PORT			1723


#define	PPTP_GRE_PROTOCOL_TYPE			0x880b
#define	PPTP_GRE_VERSION			1

/*
 * Constants of the NPPPD implementation
 */
#include "pptp_conf.h"

/* pptpd status */
#define	PPTPD_STATE_INIT 		0
#define	PPTPD_STATE_RUNNING 		1
#define	PPTPD_STATE_SHUTTING_DOWN 	2
#define	PPTPD_STATE_STOPPED 		3

#define	PPTPD_CONFIG_BUFSIZ		65535

#define	PPTP_BACKLOG	32
#define PPTP_BUFSIZ	1024

#define PPTPD_DEFAULT_LAYER2_LABEL		"PPTP"

/* pptp control state code */
#define PPTP_CTRL_STATE_IDLE			0
#define PPTP_CTRL_STATE_WAIT_CTRL_REPLY		1
#define PPTP_CTRL_STATE_ESTABLISHED		2
#define PPTP_CTRL_STATE_WAIT_STOP_REPLY		3
#define PPTP_CTRL_STATE_DISPOSING		4

#ifndef	PPTPD_DEFAULT_VENDOR_NAME
#define	PPTPD_DEFAULT_VENDOR_NAME		""
#endif

#ifndef	PPTP_CALL_DEFAULT_MAXWINSZ
#define	PPTP_CALL_DEFAULT_MAXWINSZ		64
#endif

/* Connection speed that notified by OCRP */
/* XXX: currently we use fixed value */
#ifndef	PPTP_CALL_CONNECT_SPEED
#define	PPTP_CALL_CONNECT_SPEED			10000000
#endif

/* Initial packet processing delay that notified by OCRP */
#ifndef	PPTP_CALL_INITIAL_PPD
#define PPTP_CALL_INITIAL_PPD			0
#endif

/**
 * PPTP_CALL_DELAY_LIMIT indicates how many sequence number can be rewinded
 * by reordering.
 */
#define	PPTP_CALL_DELAY_LIMIT			64

/* pptp call state machine */
#define	PPTP_CALL_STATE_IDLE			0
#define	PPTP_CALL_STATE_WAIT_CONN		1
#define	PPTP_CALL_STATE_ESTABLISHED		2
#define	PPTP_CALL_STATE_CLEANUP_WAIT		3

/* Timeout */
#define PPTPD_SHUTDOWN_TIMEOUT			5

#define	PPTPD_IDLE_TIMEOUT			60

#define	PPTP_CALL_CLEANUP_WAIT_TIME		3

#define PPTP_CTRL_DEFAULT_ECHO_INTERVAL		60
#define PPTP_CTRL_DEFAULT_ECHO_TIMEOUT		60
#define	PPTP_CTRL_StopCCRP_WAIT_TIME		3

/* MAXIMUM bindable IP addresses */
#ifndef	PPTP_NLISTENER
#define	PPTP_NLISTENER				6
#endif

/* Utility macro */
#define	pptpd_is_stopped(pptpd)					\
	(((pptpd)->state != PPTPD_STATE_SHUTTING_DOWN &&	\
	    (pptpd)->state != PPTPD_STATE_RUNNING)? 1 : 0)

#define	pptpd_is_shutting_down(pptpd)				\
	(((pptpd)->state == PPTPD_STATE_SHUTTING_DOWN)? 1 : 0)

/*
 * types
 */
struct _pptpd;

typedef struct _pptpd_listener {
	struct event ev_sock_gre; /* GRE event context */
	struct _pptpd	*self;
	uint16_t	index;
	int		sock; /* listing socket */
	int		sock_gre; /* GRE socket */
	struct sockaddr_in bind_sin;	/* listing TCP address */
	struct sockaddr_in bind_sin_gre; /* listing GRE address */
	char	tun_name[PPTP_NAME_LEN];
	struct pptp_conf *conf;
} pptpd_listener;

typedef struct _pptpd {
	unsigned	id;
	slist listener;		/* list of listeners */
	int state;
	struct event ev_timer; /* timer event context */
	slist  ctrl_list;	/* list of PPTP controls */

	slist call_free_list;	/* Free call lists */
	hash_table *call_id_map; /* table to map between callid and call */
	/* ipv4 networks that is permitted to connect */

	uint32_t		/* flags */
	    initialized:1;
} pptpd;

#define pptp_ctrl_sock_gre(ctrl)	\
	((pptpd_listener *)slist_get(&(ctrl)->pptpd->listener,\
	    (ctrl)->listener_index))->sock_gre

/* get listner's physical layer label from pptp_ctrl */
#define	PPTP_CTRL_LISTENER_TUN_NAME(ctrl)	\
	((pptpd_listener *)slist_get(&(ctrl)->pptpd->listener,\
	    (ctrl)->listener_index))->tun_name

#define	PPTP_CTRL_CONF(ctrl)					\
	((pptpd_listener *)slist_get(&(ctrl)->pptpd->listener,	\
	    (ctrl)->listener_index))->conf

typedef struct _pptp_ctrl {
	pptpd		*pptpd;	/* parents */
	uint16_t	listener_index;
	unsigned 	id;
	int		state;

	int		sock;
	struct sockaddr_storage peer;
	struct sockaddr_storage our;
	struct event	ev_sock;
	struct event	ev_timer;

	int echo_interval; /* periods between idle state to ECHO transmit */
	int echo_timeout;

	int		send_ready; /* ready to send */
	bytebuffer	*recv_buf;
	bytebuffer	*send_buf;

	slist		call_list;

	time_t	last_snd_ctrl;	/* timestamp of latest ctrl message sent */
	time_t	last_rcv_ctrl;	/* timestamp of latest ctrl message received */
	uint32_t	echo_seq; /* identifier of Echo Request */

	uint16_t	/* flags : processing I/O events */
			on_io_event:1,
			reserved:15;
} pptp_ctrl;

typedef struct _pptp_call {
	pptp_ctrl	*ctrl; /* parent */
	unsigned	id;

	int		ifidx; /* receive interface index */

	int		state;

	unsigned	peers_call_id;
	void		*ppp;

	uint32_t	snd_una;	/* next ack notification */
	uint32_t	snd_nxt;	/* next transmit sequence # */

	uint32_t	rcv_nxt;	/* received sequence # */
	uint32_t	rcv_acked;	/* latest acked received sequence # */

	int		winsz;		/* current window size */
	int		maxwinsz;	/* maximum window size */
	int		peers_maxwinsz;

	time_t		last_io;
} pptp_call;


/*
 * function prototypes
 */
#ifdef __cplusplus
extern "C" {
#endif

int   pptpd_init (pptpd *);
void  pptpd_uninit (pptpd *);
int   pptpd_assign_call (pptpd *, pptp_call *);
void  pptpd_release_call (pptpd *, pptp_call *);
int   pptpd_start (pptpd *);
void  pptpd_stop (pptpd *);
void pptpd_stop_immediatly (pptpd *);
void  pptpd_ctrl_finished_notify(pptpd *, pptp_ctrl *);
int  pptpd_add_listener(pptpd *, int, struct pptp_conf *, struct sockaddr *);

pptp_ctrl  *pptp_ctrl_create (void);
int        pptp_ctrl_init (pptp_ctrl *);
int        pptp_ctrl_start (pptp_ctrl *);
void       pptp_ctrl_stop (pptp_ctrl *, int);
void       pptp_ctrl_destroy (pptp_ctrl *);
void       pptp_ctrl_output (pptp_ctrl *, u_char *, int);

pptp_call  *pptp_call_create (void);
int        pptp_call_init (pptp_call *, pptp_ctrl *);
int        pptp_call_start (pptp_call *);
int        pptp_call_stop (pptp_call *);
void       pptp_call_destroy (pptp_call *);
void       pptp_call_input (pptp_call *, int, u_char *, int);
void       pptp_call_gre_input (pptp_call *, uint32_t, uint32_t, int, u_char *, int);
void       pptp_call_disconnect(pptp_call *, int, int, const char *);
int        pptpd_reload(pptpd *, struct pptp_confs *);

#ifdef __cplusplus
}
#endif
#endif
