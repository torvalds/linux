/*
 * ng_l2cap_var.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ng_l2cap_var.h,v 1.2 2003/04/28 21:44:59 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_L2CAP_VAR_H_
#define _NETGRAPH_L2CAP_VAR_H_

/* MALLOC decalation */
#ifdef NG_SEPARATE_MALLOC
MALLOC_DECLARE(M_NETGRAPH_L2CAP);
#else
#define M_NETGRAPH_L2CAP M_NETGRAPH
#endif /* NG_SEPARATE_MALLOC */

/* Debug */
#define	NG_L2CAP_ALERT	if (l2cap->debug >= NG_L2CAP_ALERT_LEVEL) printf
#define	NG_L2CAP_ERR	if (l2cap->debug >= NG_L2CAP_ERR_LEVEL)   printf
#define	NG_L2CAP_WARN	if (l2cap->debug >= NG_L2CAP_WARN_LEVEL)  printf
#define	NG_L2CAP_INFO	if (l2cap->debug >= NG_L2CAP_INFO_LEVEL)  printf

/* Wrapper around m_pullup */
#define NG_L2CAP_M_PULLUP(m, s) \
	do { \
		if ((m)->m_len < (s)) \
			(m) = m_pullup((m), (s)); \
		if ((m) == NULL) \
			NG_L2CAP_ALERT("%s: %s - m_pullup(%zd) failed\n", \
				__func__, NG_NODE_NAME(l2cap->node), (s)); \
	} while (0)

/*
 * L2CAP signaling command ident's are assigned relative to the connection,
 * because there is only one signaling channel (cid == 0x01) for every 
 * connection. So up to 254 (0xff - 0x01) L2CAP commands can be pending at the 
 * same time for the same connection.
 */

#define NG_L2CAP_NULL_IDENT	0x00        /* DO NOT USE THIS IDENT */ 
#define NG_L2CAP_FIRST_IDENT	0x01        /* dynamically alloc. (start) */ 
#define NG_L2CAP_LAST_IDENT	0xff        /* dynamically alloc. (end) */ 

/* 
 * L2CAP (Node private)
 */

struct ng_l2cap_con;
struct ng_l2cap_chan;

typedef struct ng_l2cap {
	node_p				node;         /* node ptr */

	ng_l2cap_node_debug_ep		debug;        /* debug level */
	ng_l2cap_node_flags_ep		flags;        /* L2CAP node flags */
	ng_l2cap_node_auto_discon_ep	discon_timo;  /* auto discon. timeout */

	u_int16_t			pkt_size;     /* max. ACL packet size */
	u_int16_t			num_pkts;     /* out queue size */
	bdaddr_t			bdaddr;       /* unit BDADDR */

	hook_p				hci;          /* HCI downstream hook */
	hook_p				l2c;          /* L2CAP upstream hook */
	hook_p				ctl;          /* control hook */

	LIST_HEAD(, ng_l2cap_con)	con_list;     /* ACL connections */

    	u_int16_t			cid;          /* last allocated CID */
    	u_int16_t			lecid;          /* last allocated CID for LE */

	LIST_HEAD(, ng_l2cap_chan)	chan_list;    /* L2CAP channels */
} ng_l2cap_t;
typedef ng_l2cap_t *			ng_l2cap_p;

/* 
 * L2CAP connection descriptor
 */

struct ng_l2cap_cmd;

typedef struct ng_l2cap_con {
	ng_l2cap_p			 l2cap;      /* pointer to L2CAP */

	u_int16_t			 state;      /* ACL connection state */
	u_int16_t			 flags;      /* ACL connection flags */

	int32_t				 refcnt;     /* reference count */

	bdaddr_t			 remote;     /* remote unit address */
	u_int16_t			 con_handle; /* ACL connection handle */
	struct callout			 con_timo;   /* connection timeout */

	u_int8_t			 ident;      /* last allocated ident */
	uint8_t				 linktype;
	uint8_t				 encryption;
	
	TAILQ_HEAD(, ng_l2cap_cmd)	 cmd_list;   /* pending L2CAP cmds */

	struct mbuf			*tx_pkt;     /* xmitted L2CAP packet */
	int				 pending;    /* num. of pending pkts */

	struct mbuf			*rx_pkt;     /* received L2CAP packet */
	int				 rx_pkt_len; /* packet len. so far */

	LIST_ENTRY(ng_l2cap_con)	 next;       /* link */
} ng_l2cap_con_t;
typedef ng_l2cap_con_t *		ng_l2cap_con_p;

/*
 * L2CAP channel descriptor
 */

typedef struct ng_l2cap_chan {
	ng_l2cap_con_p			con;        /* pointer to connection */

	u_int16_t			state;      /* channel state */

	u_int8_t			cfg_state;  /* configuration state */
#define NG_L2CAP_CFG_IN			(1 << 0)    /* incoming cfg path done */
#define NG_L2CAP_CFG_OUT		(1 << 1)    /* outgoing cfg path done */
#define NG_L2CAP_CFG_BOTH		(NG_L2CAP_CFG_IN|NG_L2CAP_CFG_OUT)

	u_int8_t			ident;      /* last L2CAP req. ident */

	u_int16_t			psm;        /* channel PSM */
	u_int16_t			scid;       /* source channel ID */
	u_int16_t			dcid;       /* destination channel ID */

	uint16_t			idtype;
	u_int16_t			imtu;       /* incoming channel MTU */
	ng_l2cap_flow_t			iflow;      /* incoming flow control */

	u_int16_t			omtu;       /* outgoing channel MTU */
	ng_l2cap_flow_t			oflow;      /* outgoing flow control */

	u_int16_t			flush_timo; /* flush timeout */
	u_int16_t			link_timo;  /* link timeout */

	LIST_ENTRY(ng_l2cap_chan)	next;       /* link */
} ng_l2cap_chan_t;
typedef ng_l2cap_chan_t *		ng_l2cap_chan_p;

/*
 * L2CAP command descriptor
 */

typedef struct ng_l2cap_cmd {
	ng_l2cap_con_p			 con;       /* L2CAP connection */
	ng_l2cap_chan_p			 ch;        /* L2CAP channel */

	u_int16_t 			 flags;     /* command flags */
#define NG_L2CAP_CMD_PENDING		 (1 << 0)   /* command is pending */

	u_int8_t 			 code;      /* L2CAP command opcode */
	u_int8_t			 ident;     /* L2CAP command ident */
	u_int32_t			 token;     /* L2CA message token */

	struct callout			 timo;      /* RTX/ERTX timeout */

	struct mbuf			*aux;       /* optional data */

	TAILQ_ENTRY(ng_l2cap_cmd)	 next;      /* link */
} ng_l2cap_cmd_t;
typedef ng_l2cap_cmd_t *		ng_l2cap_cmd_p;

#endif /* ndef _NETGRAPH_L2CAP_VAR_H_ */

