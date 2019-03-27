/*
 * ng_h4.h
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
 * $Id: ng_h4.h,v 1.1 2002/11/24 19:47:05 max Exp $
 * $FreeBSD$
 * 
 * Based on:
 * ---------
 *
 * FreeBSD: src/sys/netgraph/ng_tty.h
 * Author: Archie Cobbs <archie@freebsd.org>
 */

/*
 * This file contains everything that application needs to know about
 * Bluetooth HCI UART transport layer as per chapter H4 of the Bluetooth
 * Specification Book v1.1.
 *
 * This file can be included by both kernel and userland applications.
 */

#ifndef _NETGRAPH_H4_H_
#define _NETGRAPH_H4_H_

/**************************************************************************
 **************************************************************************
 **     Netgraph node hook name, type name and type cookie and commands
 **************************************************************************
 **************************************************************************/

/* Hook name */
#define NG_H4_HOOK		"hook"

/* Node type name and magic cookie */
#define NG_H4_NODE_TYPE		"h4"
#define NGM_H4_COOKIE		1013899512

/* Node states */
#define NG_H4_W4_PKT_IND	1	/* Waiting for packet indicator */
#define NG_H4_W4_PKT_HDR	2	/* Waiting for packet header */
#define NG_H4_W4_PKT_DATA	3	/* Waiting for packet data */

/* Debug levels */
#define NG_H4_ALERT_LEVEL	1
#define NG_H4_ERR_LEVEL		2
#define NG_H4_WARN_LEVEL	3
#define NG_H4_INFO_LEVEL	4

/**************************************************************************
 **************************************************************************
 **                    H4 node command/event parameters
 **************************************************************************
 **************************************************************************/

/* Reset node */
#define NGM_H4_NODE_RESET	1

/* Get node state (see states above) */
#define NGM_H4_NODE_GET_STATE	2
typedef u_int16_t	ng_h4_node_state_ep;

/* Get/Set node debug level (see levels above) */
#define NGM_H4_NODE_GET_DEBUG	3
#define NGM_H4_NODE_SET_DEBUG	4
typedef u_int16_t	ng_h4_node_debug_ep;

/* Get/Set max queue length for the node */
#define NGM_H4_NODE_GET_QLEN	5
#define NGM_H4_NODE_SET_QLEN	6
typedef int32_t		ng_h4_node_qlen_ep;

/* Get node statistic */
#define NGM_H4_NODE_GET_STAT	7
typedef struct {
	u_int32_t	pckts_recv; /* # of packets received */
	u_int32_t	bytes_recv; /* # of bytes received */
	u_int32_t	pckts_sent; /* # of packets sent */
	u_int32_t	bytes_sent; /* # of bytes sent */
	u_int32_t	oerrors;    /* # of output errors */
	u_int32_t	ierrors;    /* # of input errors */
} ng_h4_node_stat_ep;

/* Reset node statistic */
#define NGM_H4_NODE_RESET_STAT	8

#endif /* _NETGRAPH_H4_H_ */

