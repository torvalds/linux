/*
 * ng_bt3c.h
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
 * $Id: ng_bt3c.h,v 1.1 2002/11/24 19:47:05 max Exp $
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

#ifndef _NG_BT3C_H_
#define _NG_BT3C_H_

/**************************************************************************
 **************************************************************************
 **     Netgraph node hook name, type name and type cookie and commands 
 **************************************************************************  
 **************************************************************************/

#define NG_BT3C_NODE_TYPE	"btccc"	/* XXX can't use bt3c in pccard.conf */
#define NG_BT3C_HOOK		"hook"

#define NGM_BT3C_COOKIE		1014752016

/* Debug levels */
#define NG_BT3C_ALERT_LEVEL	1
#define NG_BT3C_ERR_LEVEL	2
#define NG_BT3C_WARN_LEVEL	3
#define NG_BT3C_INFO_LEVEL	4

/* Node states */
#define NG_BT3C_W4_PKT_IND	1               /* wait for packet indicator */
#define NG_BT3C_W4_PKT_HDR	2               /* wait for packet header */
#define NG_BT3C_W4_PKT_DATA	3               /* wait for packet data */

/**************************************************************************
 **************************************************************************
 **                    BT3C node command/event parameters
 **************************************************************************
 **************************************************************************/

#define NGM_BT3C_NODE_GET_STATE	1		/* get node state */
typedef u_int16_t		ng_bt3c_node_state_ep;

#define NGM_BT3C_NODE_SET_DEBUG	2		/* set debug level */
#define NGM_BT3C_NODE_GET_DEBUG	3		/* get debug level */
typedef u_int16_t		ng_bt3c_node_debug_ep; 

#define NGM_BT3C_NODE_GET_QLEN	4		/* get queue length */
#define NGM_BT3C_NODE_SET_QLEN	5		/* set queue length */
typedef struct {
	int32_t	queue;				/* queue index */
#define NGM_BT3C_NODE_IN_QUEUE	1		/* incoming queue */
#define NGM_BT3C_NODE_OUT_QUEUE	2		/* outgoing queue */

	int32_t	qlen;				/* queue length */
} ng_bt3c_node_qlen_ep;

#define NGM_BT3C_NODE_GET_STAT	6		/* get statistic */
typedef struct {
	u_int32_t	pckts_recv;		/* # of packets received */
	u_int32_t	bytes_recv;		/* # of bytes received */
	u_int32_t	pckts_sent;		/* # of packets sent */
	u_int32_t	bytes_sent;		/* # of bytes sent */
	u_int32_t	oerrors;		/* # of output errors */
	u_int32_t	ierrors;		/* # of input errors */
} ng_bt3c_node_stat_ep;

#define NGM_BT3C_NODE_RESET_STAT 7		/* reset statistic */

#define NGM_BT3C_NODE_DOWNLOAD_FIRMWARE	8	/* download firmware */

typedef struct {
	u_int32_t	block_address;
	u_int16_t	block_size;		/* in words */
	u_int16_t	block_alignment;	/* in bytes */
} ng_bt3c_firmware_block_ep;

#endif /* ndef _NG_BT3C_H_ */

