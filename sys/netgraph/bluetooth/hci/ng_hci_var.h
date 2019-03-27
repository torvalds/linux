/*
 * ng_hci_var.h
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
 * $Id: ng_hci_var.h,v 1.3 2003/04/26 22:35:21 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_HCI_VAR_H_
#define _NETGRAPH_HCI_VAR_H_

/* MALLOC decalation */
#ifdef NG_SEPARATE_MALLOC
MALLOC_DECLARE(M_NETGRAPH_HCI);
#else
#define M_NETGRAPH_HCI M_NETGRAPH
#endif /* NG_SEPARATE_MALLOC */

/* Debug */
#define	NG_HCI_ALERT	if (unit->debug >= NG_HCI_ALERT_LEVEL) printf
#define	NG_HCI_ERR	if (unit->debug >= NG_HCI_ERR_LEVEL)   printf
#define	NG_HCI_WARN	if (unit->debug >= NG_HCI_WARN_LEVEL)  printf
#define	NG_HCI_INFO	if (unit->debug >= NG_HCI_INFO_LEVEL)  printf

/* Wrapper around m_pullup */
#define NG_HCI_M_PULLUP(m, s) 				\
	do { 						\
		if ((m)->m_len < (s)) 			\
			(m) = m_pullup((m), (s)); 	\
		if ((m) == NULL) 			\
			NG_HCI_ALERT("%s: %s - m_pullup(%zd) failed\n", \
				__func__, NG_NODE_NAME(unit->node), (s)); \
	} while (0)

/*
 * Unit hardware buffer descriptor 
 */

typedef struct ng_hci_unit_buff {
	u_int8_t			cmd_free; /* space available (cmds) */

	u_int8_t			sco_size; /* max. size of one packet */
	u_int16_t			sco_pkts; /* size of buffer (packets) */
	u_int16_t			sco_free; /* space available (packets)*/

	u_int16_t			acl_size; /* max. size of one packet */
	u_int16_t			acl_pkts; /* size of buffer (packets) */
	u_int16_t			acl_free; /* space available (packets)*/
} ng_hci_unit_buff_t;

/* 
 * These macro's must be used everywhere in the code. So if extra locking 
 * is required later, it can be added without much troubles.
 */

#define NG_HCI_BUFF_CMD_SET(b, v)	(b).cmd_free = (v)
#define NG_HCI_BUFF_CMD_GET(b, v)	(v) = (b).cmd_free
#define NG_HCI_BUFF_CMD_USE(b, v)	(b).cmd_free -= (v)

#define NG_HCI_BUFF_ACL_USE(b, v)	(b).acl_free -= (v)
#define NG_HCI_BUFF_ACL_FREE(b, v) 			\
	do { 						\
		(b).acl_free += (v);			\
		if ((b).acl_free > (b).acl_pkts) 	\
			(b).acl_free = (b).acl_pkts; 	\
	} while (0)
#define NG_HCI_BUFF_ACL_AVAIL(b, v)	(v) = (b).acl_free
#define NG_HCI_BUFF_ACL_TOTAL(b, v)	(v) = (b).acl_pkts
#define NG_HCI_BUFF_ACL_SIZE(b, v)	(v) = (b).acl_size
#define NG_HCI_BUFF_ACL_SET(b, n, s, f) 		\
	do { 						\
		(b).acl_free = (f); 			\
		(b).acl_size = (s); 			\
		(b).acl_pkts = (n); 			\
	} while (0)

#define NG_HCI_BUFF_SCO_USE(b, v)	(b).sco_free -= (v)
#define NG_HCI_BUFF_SCO_FREE(b, v) 			\
	do { 						\
		(b).sco_free += (v); 			\
		if ((b).sco_free > (b).sco_pkts) 	\
			(b).sco_free = (b).sco_pkts; 	\
	} while (0)
#define NG_HCI_BUFF_SCO_AVAIL(b, v)	(v) = (b).sco_free
#define NG_HCI_BUFF_SCO_TOTAL(b, v)	(v) = (b).sco_pkts
#define NG_HCI_BUFF_SCO_SIZE(b, v)	(v) = (b).sco_size
#define NG_HCI_BUFF_SCO_SET(b, n, s, f) 		\
	do { 						\
		(b).sco_free = (f); 			\
		(b).sco_size = (s); 			\
		(b).sco_pkts = (n); 			\
	} while (0)

/* 
 * Unit (Node private)
 */

struct ng_hci_unit_con;
struct ng_hci_neighbor;

typedef struct ng_hci_unit {
	node_p				node;           /* node ptr */

	ng_hci_node_debug_ep		debug;          /* debug level */
	ng_hci_node_state_ep		state;          /* unit state */

	bdaddr_t			bdaddr;         /* unit address */
	u_int8_t			features[NG_HCI_FEATURES_SIZE];
					                /* LMP features */

	ng_hci_node_link_policy_mask_ep	link_policy_mask; /* link policy mask */
	ng_hci_node_packet_mask_ep	packet_mask;	/* packet mask */
	ng_hci_node_role_switch_ep	role_switch;	/* role switch */

	ng_hci_node_stat_ep		stat;           /* statistic */
#define NG_HCI_STAT_CMD_SENT(s)		(s).cmd_sent ++
#define NG_HCI_STAT_EVNT_RECV(s)	(s).evnt_recv ++
#define NG_HCI_STAT_ACL_SENT(s, n)	(s).acl_sent += (n)
#define NG_HCI_STAT_ACL_RECV(s)		(s).acl_recv ++
#define NG_HCI_STAT_SCO_SENT(s, n)	(s).sco_sent += (n)
#define NG_HCI_STAT_SCO_RECV(s)		(s).sco_recv ++
#define NG_HCI_STAT_BYTES_SENT(s, b)	(s).bytes_sent += (b)
#define NG_HCI_STAT_BYTES_RECV(s, b)	(s).bytes_recv += (b)
#define NG_HCI_STAT_RESET(s)		bzero(&(s), sizeof((s)))

	ng_hci_unit_buff_t		buffer;         /* buffer info */

	struct callout			cmd_timo;       /* command timeout */
	ng_bt_mbufq_t			cmdq;           /* command queue */
#define NG_HCI_CMD_QUEUE_LEN		12		/* max. size of cmd q */

	hook_p				drv;            /* driver hook */
	hook_p				acl;            /* upstream hook */
	hook_p				sco;            /* upstream hook */
	hook_p				raw;            /* upstream hook */

	LIST_HEAD(, ng_hci_unit_con)	con_list;       /* connections */
	LIST_HEAD(, ng_hci_neighbor)	neighbors;      /* unit neighbors */
} ng_hci_unit_t;
typedef ng_hci_unit_t *			ng_hci_unit_p;

/* 
 * Unit connection descriptor
 */

typedef struct ng_hci_unit_con {
	ng_hci_unit_p			unit;            /* pointer back */

	u_int16_t			state;           /* con. state */
	u_int16_t			flags;           /* con. flags */
#define NG_HCI_CON_TIMEOUT_PENDING		(1 << 0)
#define NG_HCI_CON_NOTIFY_ACL			(1 << 1)
#define NG_HCI_CON_NOTIFY_SCO			(1 << 2)

	bdaddr_t			bdaddr;          /* remote address */
	u_int16_t			con_handle;      /* con. handle */

	u_int8_t			link_type;       /* ACL or SCO */
	u_int8_t			encryption_mode; /* none, p2p, ... */
	u_int8_t			mode;            /* ACTIVE, HOLD ... */
	u_int8_t			role;            /* MASTER/SLAVE */

	struct callout			con_timo;        /* con. timeout */

	int				pending;         /* # of data pkts */
	ng_bt_itemq_t			conq;            /* con. queue */

	LIST_ENTRY(ng_hci_unit_con)	next;            /* next */
} ng_hci_unit_con_t;
typedef ng_hci_unit_con_t *		ng_hci_unit_con_p;

/*
 * Unit's neighbor descriptor. 
 * Neighbor is a remote unit that responded to our inquiry.
 */

typedef struct ng_hci_neighbor {
	struct timeval			updated;	/* entry was updated */

	bdaddr_t			bdaddr;         /* address */
	u_int8_t			features[NG_HCI_FEATURES_SIZE];
					                /* LMP features */
	u_int8_t 			addrtype;	/*Address Type*/

	u_int8_t			page_scan_rep_mode; /* PS rep. mode */
	u_int8_t			page_scan_mode; /* page scan mode */
	u_int16_t			clock_offset;   /* clock offset */
	uint8_t				extinq_size;
	uint8_t				extinq_data[NG_HCI_EXTINQ_MAX];
	LIST_ENTRY(ng_hci_neighbor)	next;
} ng_hci_neighbor_t;
typedef ng_hci_neighbor_t *		ng_hci_neighbor_p;
       
#endif /* ndef _NETGRAPH_HCI_VAR_H_ */

