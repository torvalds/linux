/*
 * ng_btsocket_sco.h
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
 * $Id: ng_btsocket_sco.h,v 1.3 2005/10/31 18:08:52 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_BTSOCKET_SCO_H_
#define _NETGRAPH_BTSOCKET_SCO_H_

/*
 * SCO routing entry
 */

struct ng_hook;
struct ng_message;

struct ng_btsocket_sco_rtentry {
	bdaddr_t				 src;  /* source BD_ADDR */
	u_int16_t				 pkt_size; /* mtu */
	u_int16_t				 num_pkts; /* buffer size */
	int32_t					 pending; /* pending packets */
	struct ng_hook				*hook; /* downstream hook */
	LIST_ENTRY(ng_btsocket_sco_rtentry)	 next; /* link to next */
};
typedef struct ng_btsocket_sco_rtentry		ng_btsocket_sco_rtentry_t;
typedef struct ng_btsocket_sco_rtentry *	ng_btsocket_sco_rtentry_p;

/*****************************************************************************
 *****************************************************************************
 **                      SOCK_SEQPACKET SCO sockets                         **
 *****************************************************************************
 *****************************************************************************/

#define NG_BTSOCKET_SCO_SENDSPACE	1024
#define NG_BTSOCKET_SCO_RECVSPACE	(64 * 1024)

/*
 * Bluetooth SCO socket PCB
 */

struct ng_btsocket_sco_pcb {
	struct socket			*so;	     /* Pointer to socket */

	bdaddr_t			 src;	     /* Source address */
	bdaddr_t			 dst;	     /* Destination address */

	u_int16_t			 con_handle; /* connection handle */

	u_int16_t			 flags;      /* socket flags */
#define NG_BTSOCKET_SCO_CLIENT		(1 << 0)     /* socket is client */
#define NG_BTSOCKET_SCO_TIMO		(1 << 1)     /* timeout pending */

	u_int8_t			 state;      /* socket state */
#define NG_BTSOCKET_SCO_CLOSED		0            /* socket closed */
#define NG_BTSOCKET_SCO_CONNECTING	1            /* wait for connect */
#define NG_BTSOCKET_SCO_OPEN		2            /* socket open */
#define NG_BTSOCKET_SCO_DISCONNECTING	3            /* wait for disconnect */

	struct callout			 timo;       /* timeout */

	ng_btsocket_sco_rtentry_p	 rt;         /* routing info */

	struct mtx			 pcb_mtx;    /* pcb mutex */

	LIST_ENTRY(ng_btsocket_sco_pcb)	 next;       /* link to next PCB */
};
typedef struct ng_btsocket_sco_pcb	ng_btsocket_sco_pcb_t;
typedef struct ng_btsocket_sco_pcb *	ng_btsocket_sco_pcb_p;

#define	so2sco_pcb(so) \
	((struct ng_btsocket_sco_pcb *)((so)->so_pcb))

/*
 * Bluetooth SCO socket methods
 */

#ifdef _KERNEL

void ng_btsocket_sco_init       (void);
void ng_btsocket_sco_abort      (struct socket *);
void ng_btsocket_sco_close      (struct socket *);
int  ng_btsocket_sco_accept     (struct socket *, struct sockaddr **);
int  ng_btsocket_sco_attach     (struct socket *, int, struct thread *);
int  ng_btsocket_sco_bind       (struct socket *, struct sockaddr *,
                                   struct thread *);
int  ng_btsocket_sco_connect    (struct socket *, struct sockaddr *,
                                   struct thread *);
int  ng_btsocket_sco_control    (struct socket *, u_long, caddr_t,
                                   struct ifnet *, struct thread *);
int  ng_btsocket_sco_ctloutput  (struct socket *, struct sockopt *);
void ng_btsocket_sco_detach     (struct socket *);
int  ng_btsocket_sco_disconnect (struct socket *);
int  ng_btsocket_sco_listen     (struct socket *, int, struct thread *);
int  ng_btsocket_sco_peeraddr   (struct socket *, struct sockaddr **);
int  ng_btsocket_sco_send       (struct socket *, int, struct mbuf *,
                                   struct sockaddr *, struct mbuf *,
                                   struct thread *);
int  ng_btsocket_sco_sockaddr   (struct socket *, struct sockaddr **);

#endif /* _KERNEL */

#endif /* _NETGRAPH_BTSOCKET_SCO_H_ */

