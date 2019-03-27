/*
 * ng_bt3c_var.h
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
 * $Id: ng_bt3c_var.h,v 1.1 2002/11/24 19:46:54 max Exp $
 * $FreeBSD$
 *
 * XXX XXX XX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX 
 *
 * Based on information obrained from: Jose Orlando Pereira <jop@di.uminho.pt>
 * and disassembled w2k driver.
 *
 * XXX XXX XX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX XXX 
 *
 */

#ifndef _NG_BT3C_VAR_H_
#define _NG_BT3C_VAR_H_

/* Debug printf's */
#define NG_BT3C_ALERT	if (sc->debug >= NG_BT3C_ALERT_LEVEL) device_printf
#define NG_BT3C_ERR	if (sc->debug >= NG_BT3C_ERR_LEVEL)   device_printf
#define NG_BT3C_WARN	if (sc->debug >= NG_BT3C_WARN_LEVEL)  device_printf
#define NG_BT3C_INFO	if (sc->debug >= NG_BT3C_INFO_LEVEL)  device_printf

/* Device registers */
#define	BT3C_DATA_L		0x00		/* data low byte */
#define	BT3C_DATA_H		0x01		/* high byte */
#define	BT3C_ADDR_L		0x02		/* address low byte */
#define	BT3C_ADDR_H		0x03		/* high byte */
#define	BT3C_CONTROL		0x04		/* control */

#define BT3C_FIFO_SIZE		256

/* Device softc structure */
struct bt3c_softc {
	/* Device specific */
	device_t		 dev;		/* pointer back to device */
	int			 iobase_rid;	/* iobase RID */
	struct resource		*iobase;	/* iobase */
	bus_space_tag_t		 iot;		/* I/O tag */
	bus_space_handle_t	 ioh;		/* I/O handle */
	int			 irq_rid;       /* irq RID */
	struct resource		*irq;		/* irq */
	void			*irq_cookie;	/* irq cookie */

	/* Netgraph specific */
	node_p			 node;		/* pointer back to node */
	hook_p			 hook;		/* hook */

	ng_bt3c_node_debug_ep	 debug;		/* debug level */
	u_int16_t		 flags;		/* device flags */
#define BT3C_ANTENNA_OUT	(1 << 0)	/* antena is out */
#define BT3C_XMIT		(1 << 1)	/* xmit in progress */

	ng_bt3c_node_state_ep	 state;		/* receiving state */

	ng_bt3c_node_stat_ep	 stat;		/* statistic */
#define NG_BT3C_STAT_PCKTS_SENT(s)	(s).pckts_sent ++
#define NG_BT3C_STAT_BYTES_SENT(s, n)	(s).bytes_sent += (n)
#define NG_BT3C_STAT_PCKTS_RECV(s)	(s).pckts_recv ++
#define NG_BT3C_STAT_BYTES_RECV(s, n)	(s).bytes_recv += (n)
#define NG_BT3C_STAT_OERROR(s)		(s).oerrors ++
#define NG_BT3C_STAT_IERROR(s)		(s).ierrors ++
#define NG_BT3C_STAT_RESET(s)		bzero(&(s), sizeof((s)))

	u_int32_t		 status;	/* from ISR */
	void			*ith;		/* ithread handler */

	struct mbuf		*m;		/* current frame */
	u_int32_t		 want;		/* # of chars we want */

	struct ifqueue		 inq;		/* queue of incoming mbuf's */
	struct ifqueue		 outq;		/* queue of outgoing mbuf's */
#define BT3C_DEFAULTQLEN	 12		/* XXX max. size of out queue */
};

typedef struct bt3c_softc	bt3c_softc_t;
typedef struct bt3c_softc *	bt3c_softc_p;

#endif /* ndef _NG_BT3C_VAR_H_ */

