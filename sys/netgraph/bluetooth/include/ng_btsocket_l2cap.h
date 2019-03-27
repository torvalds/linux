/*
 * ng_btsocket_l2cap.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: ng_btsocket_l2cap.h,v 1.4 2003/03/25 23:53:33 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_BTSOCKET_L2CAP_H_
#define _NETGRAPH_BTSOCKET_L2CAP_H_

/*
 * L2CAP routing entry
 */

struct ng_hook;
struct ng_message;

struct ng_btsocket_l2cap_rtentry {
	bdaddr_t				 src;  /* source BD_ADDR */
	struct ng_hook				*hook; /* downstream hook */
	LIST_ENTRY(ng_btsocket_l2cap_rtentry)	 next; /* link to next */
};
typedef struct ng_btsocket_l2cap_rtentry	ng_btsocket_l2cap_rtentry_t;
typedef struct ng_btsocket_l2cap_rtentry *	ng_btsocket_l2cap_rtentry_p;

/*****************************************************************************
 *****************************************************************************
 **                          SOCK_RAW L2CAP sockets                         **
 *****************************************************************************
 *****************************************************************************/

#define NG_BTSOCKET_L2CAP_RAW_SENDSPACE	NG_L2CAP_MTU_DEFAULT
#define NG_BTSOCKET_L2CAP_RAW_RECVSPACE	NG_L2CAP_MTU_DEFAULT

/*
 * Bluetooth raw L2CAP socket PCB
 */

struct ng_btsocket_l2cap_raw_pcb {
	struct socket				*so;	/* socket */

	u_int32_t				 flags; /* flags */
#define NG_BTSOCKET_L2CAP_RAW_PRIVILEGED	(1 << 0)

	bdaddr_t				 src;	/* source address */
	bdaddr_t				 dst;	/* dest address */
	uint8_t			 	 	 srctype;/*source addr type*/
	uint8_t			 	 	 dsttype;/*source addr type*/
	ng_btsocket_l2cap_rtentry_p		 rt;    /* routing info */

	u_int32_t				 token;	/* message token */
	struct ng_mesg				*msg;   /* message */

	struct mtx				 pcb_mtx; /* pcb mutex */

	LIST_ENTRY(ng_btsocket_l2cap_raw_pcb)	 next;  /* link to next PCB */
};
typedef struct ng_btsocket_l2cap_raw_pcb	ng_btsocket_l2cap_raw_pcb_t;
typedef struct ng_btsocket_l2cap_raw_pcb *	ng_btsocket_l2cap_raw_pcb_p;

#define	so2l2cap_raw_pcb(so) \
	((struct ng_btsocket_l2cap_raw_pcb *)((so)->so_pcb))

/*
 * Bluetooth raw L2CAP socket methods
 */

#ifdef _KERNEL

void ng_btsocket_l2cap_raw_init       (void);
void ng_btsocket_l2cap_raw_abort      (struct socket *);
void ng_btsocket_l2cap_raw_close      (struct socket *);
int  ng_btsocket_l2cap_raw_attach     (struct socket *, int, struct thread *);
int  ng_btsocket_l2cap_raw_bind       (struct socket *, struct sockaddr *,
                                       struct thread *);
int  ng_btsocket_l2cap_raw_connect    (struct socket *, struct sockaddr *,
                                       struct thread *);
int  ng_btsocket_l2cap_raw_control    (struct socket *, u_long, caddr_t,
                                       struct ifnet *, struct thread *);
void ng_btsocket_l2cap_raw_detach     (struct socket *);
int  ng_btsocket_l2cap_raw_disconnect (struct socket *);
int  ng_btsocket_l2cap_raw_peeraddr   (struct socket *, struct sockaddr **);
int  ng_btsocket_l2cap_raw_send       (struct socket *, int, struct mbuf *,
                                       struct sockaddr *, struct mbuf *,
                                       struct thread *);
int  ng_btsocket_l2cap_raw_sockaddr   (struct socket *, struct sockaddr **);

#endif /* _KERNEL */

/*****************************************************************************
 *****************************************************************************
 **                    SOCK_SEQPACKET L2CAP sockets                         **
 *****************************************************************************
 *****************************************************************************/

#define NG_BTSOCKET_L2CAP_SENDSPACE	NG_L2CAP_MTU_DEFAULT /* (64 * 1024) */
#define NG_BTSOCKET_L2CAP_RECVSPACE	(64 * 1024)

/*
 * Bluetooth L2CAP socket PCB
 */

struct ng_btsocket_l2cap_pcb {
	struct socket			*so;	     /* Pointer to socket */

	bdaddr_t			 src;	     /* Source address */
	bdaddr_t			 dst;	     /* Destination address */
	uint8_t			 	 srctype;	/*source addr type*/
	uint8_t			 	 dsttype;	/*source addr type*/

	u_int16_t			 psm;	     /* PSM */
	u_int16_t			 cid;	     /* Local channel ID */
	uint8_t				 idtype;
	u_int16_t			 flags;      /* socket flags */
#define NG_BTSOCKET_L2CAP_CLIENT	(1 << 0)     /* socket is client */
#define NG_BTSOCKET_L2CAP_TIMO		(1 << 1)     /* timeout pending */

	u_int8_t			 state;      /* socket state */
#define NG_BTSOCKET_L2CAP_CLOSED	0            /* socket closed */
#define NG_BTSOCKET_L2CAP_CONNECTING	1            /* wait for connect */
#define NG_BTSOCKET_L2CAP_CONFIGURING	2            /* wait for config */
#define NG_BTSOCKET_L2CAP_OPEN		3            /* socket open */
#define NG_BTSOCKET_L2CAP_DISCONNECTING	4            /* wait for disconnect */
#define NG_BTSOCKET_L2CAP_W4_ENC_CHANGE 5  

	u_int8_t			 cfg_state;  /* config state */
#define	NG_BTSOCKET_L2CAP_CFG_IN	(1 << 0)     /* incoming path done */
#define	NG_BTSOCKET_L2CAP_CFG_OUT	(1 << 1)     /* outgoing path done */
#define	NG_BTSOCKET_L2CAP_CFG_BOTH \
	(NG_BTSOCKET_L2CAP_CFG_IN | NG_BTSOCKET_L2CAP_CFG_OUT)

#define	NG_BTSOCKET_L2CAP_CFG_IN_SENT	(1 << 2)     /* L2CAP ConfigReq sent */
#define	NG_BTSOCKET_L2CAP_CFG_OUT_SENT	(1 << 3)     /* ---/--- */
	uint8_t 			 encryption;
	u_int16_t			 imtu;       /* Incoming MTU */
	ng_l2cap_flow_t			 iflow;      /* Input flow spec */

	u_int16_t			 omtu;       /* Outgoing MTU */
	ng_l2cap_flow_t			 oflow;      /* Outgoing flow spec */

	u_int16_t			 flush_timo; /* flush timeout */   
	u_int16_t			 link_timo;  /* link timeout */ 

	struct callout			 timo;       /* timeout */

	u_int32_t			 token;	     /* message token */
	ng_btsocket_l2cap_rtentry_p	 rt;         /* routing info */

	struct mtx			 pcb_mtx;    /* pcb mutex */
	uint16_t			 need_encrypt; /*encryption needed*/
	
	LIST_ENTRY(ng_btsocket_l2cap_pcb) next;      /* link to next PCB */
};
typedef struct ng_btsocket_l2cap_pcb	ng_btsocket_l2cap_pcb_t;
typedef struct ng_btsocket_l2cap_pcb *	ng_btsocket_l2cap_pcb_p;

#define	so2l2cap_pcb(so) \
	((struct ng_btsocket_l2cap_pcb *)((so)->so_pcb))

/*
 * Bluetooth L2CAP socket methods
 */

#ifdef _KERNEL

void ng_btsocket_l2cap_init       (void);
void ng_btsocket_l2cap_abort      (struct socket *);
void ng_btsocket_l2cap_close      (struct socket *);
int  ng_btsocket_l2cap_accept     (struct socket *, struct sockaddr **);
int  ng_btsocket_l2cap_attach     (struct socket *, int, struct thread *);
int  ng_btsocket_l2cap_bind       (struct socket *, struct sockaddr *,
                                   struct thread *);
int  ng_btsocket_l2cap_connect    (struct socket *, struct sockaddr *,
                                   struct thread *);
int  ng_btsocket_l2cap_control    (struct socket *, u_long, caddr_t,
                                   struct ifnet *, struct thread *);
int  ng_btsocket_l2cap_ctloutput  (struct socket *, struct sockopt *);
void ng_btsocket_l2cap_detach     (struct socket *);
int  ng_btsocket_l2cap_disconnect (struct socket *);
int  ng_btsocket_l2cap_listen     (struct socket *, int, struct thread *);
int  ng_btsocket_l2cap_peeraddr   (struct socket *, struct sockaddr **);
int  ng_btsocket_l2cap_send       (struct socket *, int, struct mbuf *,
                                   struct sockaddr *, struct mbuf *,
                                   struct thread *);
int  ng_btsocket_l2cap_sockaddr   (struct socket *, struct sockaddr **);

#endif /* _KERNEL */

#endif /* _NETGRAPH_BTSOCKET_L2CAP_H_ */

