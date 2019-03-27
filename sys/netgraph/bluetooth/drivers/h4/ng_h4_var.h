/*
 * ng_h4_var.h
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
 * $Id: ng_h4_var.h,v 1.5 2005/10/31 17:57:43 max Exp $
 * $FreeBSD$
 * 
 * Based on:
 * ---------
 *
 * FreeBSD: src/sys/netgraph/ng_tty.h
 * Author: Archie Cobbs <archie@freebsd.org>
 */

#ifndef _NETGRAPH_H4_VAR_H_
#define _NETGRAPH_H4_VAR_H_

/*
 * Malloc declaration
 */

#ifndef NG_SEPARATE_MALLOC
MALLOC_DECLARE(M_NETGRAPH_H4);
#else
#define M_NETGRAPH_H4 M_NETGRAPH
#endif /* NG_SEPARATE_MALLOC */

/* 
 * Debug
 */

#define	NG_H4_ALERT	if (sc->debug >= NG_H4_ALERT_LEVEL) printf
#define	NG_H4_ERR	if (sc->debug >= NG_H4_ERR_LEVEL)   printf
#define	NG_H4_WARN	if (sc->debug >= NG_H4_WARN_LEVEL)  printf
#define	NG_H4_INFO	if (sc->debug >= NG_H4_INFO_LEVEL)  printf

#define NG_H4_HIWATER		256	/* High water mark on output */

/* 
 * Per-node private info 
 */

typedef struct ng_h4_info {
	struct tty		*tp;	/* Terminal device */
	node_p			 node;	/* Netgraph node */

	ng_h4_node_debug_ep	 debug;	/* Debug level */
	ng_h4_node_state_ep	 state;	/* State */
	
	ng_h4_node_stat_ep	 stat;
#define NG_H4_STAT_PCKTS_SENT(s)	(s).pckts_sent ++
#define NG_H4_STAT_BYTES_SENT(s, n)	(s).bytes_sent += (n)
#define NG_H4_STAT_PCKTS_RECV(s)	(s).pckts_recv ++
#define NG_H4_STAT_BYTES_RECV(s, n)	(s).bytes_recv += (n)
#define NG_H4_STAT_OERROR(s)		(s).oerrors ++
#define NG_H4_STAT_IERROR(s)		(s).ierrors ++
#define NG_H4_STAT_RESET(s)		bzero(&(s), sizeof((s)))

	struct ifqueue		outq;	/* Queue of outgoing mbuf's */
#define NG_H4_DEFAULTQLEN	 12     /* XXX max number of mbuf's in outq */
#define	NG_H4_LOCK(sc)		IF_LOCK(&sc->outq)
#define	NG_H4_UNLOCK(sc)	IF_UNLOCK(&sc->outq)

#define NG_H4_IBUF_SIZE		1024	/* XXX must be big enough to hold full 
					   frame */
	u_int8_t		 ibuf[NG_H4_IBUF_SIZE];	/* Incoming data */
	u_int32_t		 got;	/* Number of bytes we have received */
	u_int32_t		 want;	/* Number of bytes we want to receive */

	hook_p			 hook;	/* Upstream hook */
	struct callout		 timo;	/* See man timeout(9) */

	u_int8_t		 dying;	/* are we dying? */
} ng_h4_info_t;
typedef ng_h4_info_t *		 ng_h4_info_p;

#endif /* _NETGRAPH_H4_VAR_H_ */

