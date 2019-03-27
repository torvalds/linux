/*
 * ng_btsocket_rfcomm.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: ng_btsocket_rfcomm.h,v 1.10 2003/03/29 22:27:42 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_BTSOCKET_RFCOMM_H_
#define _NETGRAPH_BTSOCKET_RFCOMM_H_

/*****************************************************************************
 *****************************************************************************
 **                              RFCOMM                                     **
 *****************************************************************************
 *****************************************************************************/

/* XXX FIXME this does not belong here */

#define RFCOMM_DEFAULT_MTU		667
#define RFCOMM_MAX_MTU			1024

#define RFCOMM_DEFAULT_CREDITS		7
#define RFCOMM_MAX_CREDITS		40

/* RFCOMM frame types */
#define RFCOMM_FRAME_SABM		0x2f
#define RFCOMM_FRAME_DISC		0x43
#define RFCOMM_FRAME_UA			0x63
#define RFCOMM_FRAME_DM			0x0f
#define RFCOMM_FRAME_UIH		0xef

/* RFCOMM MCC commands */
#define RFCOMM_MCC_TEST			0x08 /* Test */
#define RFCOMM_MCC_FCON			0x28 /* Flow Control on */
#define RFCOMM_MCC_FCOFF		0x18 /* Flow Control off */
#define RFCOMM_MCC_MSC			0x38 /* Modem Status Command */
#define RFCOMM_MCC_RPN			0x24 /* Remote Port Negotiation */
#define RFCOMM_MCC_RLS			0x14 /* Remote Line Status */
#define RFCOMM_MCC_PN			0x20 /* Port Negotiation */
#define RFCOMM_MCC_NSC			0x04 /* Non Supported Command */

/* RFCOMM modem signals */
#define RFCOMM_MODEM_FC			0x02 /* Flow Control asserted */
#define RFCOMM_MODEM_RTC		0x04 /* Ready To Communicate */
#define RFCOMM_MODEM_RTR		0x08 /* Ready To Receive */
#define	RFCOMM_MODEM_IC			0x40 /* Incoming Call */
#define RFCOMM_MODEM_DV			0x80 /* Data Valid */

/* RPN parameters - baud rate */
#define RFCOMM_RPN_BR_2400		0x0
#define RFCOMM_RPN_BR_4800		0x1
#define RFCOMM_RPN_BR_7200		0x2
#define RFCOMM_RPN_BR_9600		0x3
#define RFCOMM_RPN_BR_19200		0x4
#define RFCOMM_RPN_BR_38400		0x5
#define RFCOMM_RPN_BR_57600		0x6
#define RFCOMM_RPN_BR_115200		0x7
#define RFCOMM_RPN_BR_230400		0x8

/* RPN parameters - data bits */
#define RFCOMM_RPN_DATA_5		0x0
#define RFCOMM_RPN_DATA_6		0x2
#define RFCOMM_RPN_DATA_7		0x1
#define RFCOMM_RPN_DATA_8		0x3

/* RPN parameters - stop bit */
#define RFCOMM_RPN_STOP_1		0
#define RFCOMM_RPN_STOP_15		1

/* RPN parameters - parity */
#define RFCOMM_RPN_PARITY_NONE		0x0
#define RFCOMM_RPN_PARITY_ODD		0x4
#define RFCOMM_RPN_PARITY_EVEN		0x5
#define RFCOMM_RPN_PARITY_MARK		0x6
#define RFCOMM_RPN_PARITY_SPACE		0x7

/* RPN parameters - flow control */
#define RFCOMM_RPN_FLOW_NONE		0x00
#define RFCOMM_RPN_XON_CHAR		0x11
#define RFCOMM_RPN_XOFF_CHAR		0x13

/* RPN parameters - mask */
#define RFCOMM_RPN_PM_BITRATE		0x0001
#define RFCOMM_RPN_PM_DATA		0x0002
#define RFCOMM_RPN_PM_STOP		0x0004
#define RFCOMM_RPN_PM_PARITY		0x0008
#define RFCOMM_RPN_PM_PARITY_TYPE	0x0010
#define RFCOMM_RPN_PM_XON		0x0020
#define RFCOMM_RPN_PM_XOFF		0x0040
#define RFCOMM_RPN_PM_FLOW		0x3F00
#define RFCOMM_RPN_PM_ALL		0x3F7F

/* RFCOMM frame header */
struct rfcomm_frame_hdr 
{
	u_int8_t	address;
	u_int8_t	control;
	u_int8_t	length;	/* Actual size could be 2 bytes */
} __attribute__ ((packed));

/* RFCOMM command frame header */
struct rfcomm_cmd_hdr
{
	u_int8_t	address;
	u_int8_t	control;
	u_int8_t	length;
	u_int8_t	fcs;
} __attribute__ ((packed));
                
/* RFCOMM MCC command header */
struct rfcomm_mcc_hdr
{
	u_int8_t	type;
	u_int8_t	length; /* XXX FIXME Can actual size be 2 bytes?? */
} __attribute__ ((packed));

/* RFCOMM MSC command */
struct rfcomm_mcc_msc
{
	u_int8_t	address;
	u_int8_t	modem;
} __attribute__ ((packed));

/* RFCOMM RPN command */
struct rfcomm_mcc_rpn
{
	u_int8_t	dlci;
	u_int8_t	bit_rate;
	u_int8_t	line_settings;
	u_int8_t	flow_control;
	u_int8_t	xon_char;
	u_int8_t	xoff_char;
	u_int16_t	param_mask;
} __attribute__ ((packed));

/* RFCOMM RLS command */
struct rfcomm_mcc_rls
{
	u_int8_t	address;
	u_int8_t	status;
} __attribute__ ((packed));

/* RFCOMM PN command */
struct rfcomm_mcc_pn
{
	u_int8_t	dlci;
	u_int8_t	flow_control;
	u_int8_t	priority;
	u_int8_t	ack_timer;
	u_int16_t	mtu;
	u_int8_t	max_retrans;
	u_int8_t	credits;
} __attribute__ ((packed));

/* RFCOMM frame parsing macros */
#define RFCOMM_DLCI(b)			(((b) & 0xfc) >> 2)
#define RFCOMM_CHANNEL(b)		(((b) & 0xf8) >> 3)
#define RFCOMM_DIRECTION(b)		(((b) & 0x04) >> 2)
#define RFCOMM_TYPE(b)			(((b) & 0xef))
  
#define RFCOMM_EA(b)			(((b) & 0x01))
#define RFCOMM_CR(b)			(((b) & 0x02) >> 1)
#define RFCOMM_PF(b)			(((b) & 0x10) >> 4)

#define RFCOMM_SRVCHANNEL(dlci)		((dlci) >> 1)
  
#define RFCOMM_MKADDRESS(cr, dlci) \
	((((dlci) & 0x3f) << 2) | ((cr) << 1) | 0x01)

#define RFCOMM_MKCONTROL(type, pf)	((((type) & 0xef) | ((pf) << 4)))
#define RFCOMM_MKDLCI(dir, channel)	((((channel) & 0x1f) << 1) | (dir))

#define RFCOMM_MKLEN8(len)		(((len) << 1) | 1)
#define RFCOMM_MKLEN16(len)		((len) << 1)

/* RFCOMM MCC macros */
#define RFCOMM_MCC_TYPE(b)		(((b) & 0xfc) >> 2)
#define RFCOMM_MCC_LENGTH(b)		(((b) & 0xfe) >> 1)
#define RFCOMM_MKMCC_TYPE(cr, type)	((((type) << 2) | ((cr) << 1) | 0x01))
   
/* RPN macros */
#define RFCOMM_RPN_DATA_BITS(line)	((line) & 0x3)
#define RFCOMM_RPN_STOP_BITS(line)	(((line) >> 2) & 0x1)
#define RFCOMM_RPN_PARITY(line)		(((line) >> 3) & 0x3)
#define RFCOMM_MKRPN_LINE_SETTINGS(data, stop, parity) \
	(((data) & 0x3) | (((stop) & 0x1) << 2) | (((parity) & 0x3) << 3))

/*****************************************************************************
 *****************************************************************************
 **                      SOCK_STREAM RFCOMM sockets                         **
 *****************************************************************************
 *****************************************************************************/

#define NG_BTSOCKET_RFCOMM_SENDSPACE \
	(RFCOMM_MAX_CREDITS * RFCOMM_DEFAULT_MTU * 2)
#define NG_BTSOCKET_RFCOMM_RECVSPACE \
	(RFCOMM_MAX_CREDITS * RFCOMM_DEFAULT_MTU * 2)

/*
 * Bluetooth RFCOMM session. One L2CAP connection == one RFCOMM session
 */

struct ng_btsocket_rfcomm_pcb;
struct ng_btsocket_rfcomm_session;

struct ng_btsocket_rfcomm_session {
	struct socket				*l2so;	 /* L2CAP socket */

	u_int16_t				 state;  /* session state */
#define NG_BTSOCKET_RFCOMM_SESSION_CLOSED	 0
#define NG_BTSOCKET_RFCOMM_SESSION_LISTENING	 1
#define NG_BTSOCKET_RFCOMM_SESSION_CONNECTING	 2 
#define NG_BTSOCKET_RFCOMM_SESSION_CONNECTED	 3
#define NG_BTSOCKET_RFCOMM_SESSION_OPEN		 4
#define NG_BTSOCKET_RFCOMM_SESSION_DISCONNECTING 5

	u_int16_t				 flags;  /* session flags */
#define NG_BTSOCKET_RFCOMM_SESSION_INITIATOR	(1 << 0) /* initiator */
#define NG_BTSOCKET_RFCOMM_SESSION_LFC		(1 << 1) /* local flow */
#define NG_BTSOCKET_RFCOMM_SESSION_RFC		(1 << 2) /* remote flow */

#define INITIATOR(s) \
	(((s)->flags & NG_BTSOCKET_RFCOMM_SESSION_INITIATOR)? 1 : 0)

	u_int16_t				 mtu;    /* default MTU */
	struct ng_bt_mbufq			 outq;   /* outgoing queue */

	struct mtx				 session_mtx; /* session lock */
	LIST_HEAD(, ng_btsocket_rfcomm_pcb)	 dlcs;	 /* active DLC */

	LIST_ENTRY(ng_btsocket_rfcomm_session)	 next;	 /* link to next */
};
typedef struct ng_btsocket_rfcomm_session	ng_btsocket_rfcomm_session_t;
typedef struct ng_btsocket_rfcomm_session *	ng_btsocket_rfcomm_session_p;

/*
 * Bluetooth RFCOMM socket PCB (DLC)
 */

struct ng_btsocket_rfcomm_pcb {
	struct socket				*so;	  /* RFCOMM socket */
	struct ng_btsocket_rfcomm_session	*session; /* RFCOMM session */

	u_int16_t				 flags;   /* DLC flags */
#define NG_BTSOCKET_RFCOMM_DLC_TIMO		(1 << 0)  /* timeout pending */
#define NG_BTSOCKET_RFCOMM_DLC_CFC		(1 << 1)  /* credit flow ctrl */
#define	NG_BTSOCKET_RFCOMM_DLC_TIMEDOUT		(1 << 2)  /* timeout happened */
#define NG_BTSOCKET_RFCOMM_DLC_DETACHED		(1 << 3)  /* DLC detached */
#define NG_BTSOCKET_RFCOMM_DLC_SENDING		(1 << 4)  /* send pending */

	u_int16_t				 state;   /* DLC state */
#define NG_BTSOCKET_RFCOMM_DLC_CLOSED		0
#define NG_BTSOCKET_RFCOMM_DLC_W4_CONNECT	1
#define NG_BTSOCKET_RFCOMM_DLC_CONFIGURING	2
#define NG_BTSOCKET_RFCOMM_DLC_CONNECTING	3
#define NG_BTSOCKET_RFCOMM_DLC_CONNECTED	4
#define NG_BTSOCKET_RFCOMM_DLC_DISCONNECTING	5

	bdaddr_t				 src;     /* source address */
	bdaddr_t				 dst;     /* dest. address */

	u_int8_t				 channel; /* RFCOMM channel */
	u_int8_t				 dlci;    /* RFCOMM DLCI */

	u_int8_t				 lmodem;  /* local mdm signls */
	u_int8_t				 rmodem;  /* remote -/- */

	u_int16_t				 mtu;	  /* MTU */
	int16_t					 rx_cred; /* RX credits */
	int16_t					 tx_cred; /* TX credits */

	struct mtx				 pcb_mtx; /* PCB lock */
	struct callout				 timo;    /* timeout */

	LIST_ENTRY(ng_btsocket_rfcomm_pcb)	 session_next;/* link to next */
	LIST_ENTRY(ng_btsocket_rfcomm_pcb)	 next;	  /* link to next */
};
typedef struct ng_btsocket_rfcomm_pcb	ng_btsocket_rfcomm_pcb_t;
typedef struct ng_btsocket_rfcomm_pcb *	ng_btsocket_rfcomm_pcb_p;

#define	so2rfcomm_pcb(so) \
	((struct ng_btsocket_rfcomm_pcb *)((so)->so_pcb))

/*
 * Bluetooth RFCOMM socket methods
 */

#ifdef _KERNEL

void ng_btsocket_rfcomm_init       (void);
void ng_btsocket_rfcomm_abort      (struct socket *);
void ng_btsocket_rfcomm_close      (struct socket *);
int  ng_btsocket_rfcomm_accept     (struct socket *, struct sockaddr **);
int  ng_btsocket_rfcomm_attach     (struct socket *, int, struct thread *);
int  ng_btsocket_rfcomm_bind       (struct socket *, struct sockaddr *,
                                    struct thread *);
int  ng_btsocket_rfcomm_connect    (struct socket *, struct sockaddr *,
                                    struct thread *);
int  ng_btsocket_rfcomm_control    (struct socket *, u_long, caddr_t,
                                    struct ifnet *, struct thread *);
int  ng_btsocket_rfcomm_ctloutput  (struct socket *, struct sockopt *);
void ng_btsocket_rfcomm_detach     (struct socket *);
int  ng_btsocket_rfcomm_disconnect (struct socket *);
int  ng_btsocket_rfcomm_listen     (struct socket *, int, struct thread *);
int  ng_btsocket_rfcomm_peeraddr   (struct socket *, struct sockaddr **);
int  ng_btsocket_rfcomm_send       (struct socket *, int, struct mbuf *,
                                    struct sockaddr *, struct mbuf *,
                                    struct thread *);
int  ng_btsocket_rfcomm_sockaddr   (struct socket *, struct sockaddr **);

#endif /* _KERNEL */

#endif /* _NETGRAPH_BTSOCKET_RFCOMM_H_ */

