/*
 * bluetooth.h
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
 * $Id: ng_bluetooth.h,v 1.4 2003/04/26 22:32:34 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_BLUETOOTH_H_
#define _NETGRAPH_BLUETOOTH_H_

#include <sys/queue.h>

/*
 * Version of the stack
 */

#define NG_BLUETOOTH_VERSION	1

/*
 * Declare the base of the Bluetooth sysctl hierarchy, 
 * but only if this file cares about sysctl's
 */

#ifdef SYSCTL_DECL
SYSCTL_DECL(_net_bluetooth);
SYSCTL_DECL(_net_bluetooth_hci);
SYSCTL_DECL(_net_bluetooth_l2cap);
SYSCTL_DECL(_net_bluetooth_rfcomm);
SYSCTL_DECL(_net_bluetooth_sco);
#endif /* SYSCTL_DECL */

/*
 * Mbuf qeueue and useful mbufq macros. We do not use ifqueue because we
 * do not need mutex and other locking stuff
 */

struct mbuf;

struct ng_bt_mbufq {
	struct mbuf	*head;   /* first item in the queue */
	struct mbuf	*tail;   /* last item in the queue */
	u_int32_t	 len;    /* number of items in the queue */
	u_int32_t	 maxlen; /* maximal number of items in the queue */
	u_int32_t	 drops;	 /* number if dropped items */
};
typedef struct ng_bt_mbufq	ng_bt_mbufq_t;
typedef struct ng_bt_mbufq *	ng_bt_mbufq_p;

#define NG_BT_MBUFQ_INIT(q, _maxlen)			\
	do {						\
		(q)->head = NULL;			\
		(q)->tail = NULL;			\
		(q)->len = 0;				\
		(q)->maxlen = (_maxlen);		\
		(q)->drops = 0;				\
	} while (0)

#define NG_BT_MBUFQ_DESTROY(q)				\
	do {						\
		NG_BT_MBUFQ_DRAIN((q));			\
	} while (0)

#define NG_BT_MBUFQ_FIRST(q)	(q)->head

#define NG_BT_MBUFQ_LEN(q)	(q)->len

#define NG_BT_MBUFQ_FULL(q)	((q)->len >= (q)->maxlen)

#define NG_BT_MBUFQ_DROP(q)	(q)->drops ++

#define NG_BT_MBUFQ_ENQUEUE(q, i)			\
	do {						\
		(i)->m_nextpkt = NULL;			\
							\
		if ((q)->tail == NULL)			\
			(q)->head = (i);		\
		else					\
			(q)->tail->m_nextpkt = (i);	\
							\
		(q)->tail = (i);			\
		(q)->len ++;				\
	} while (0)

#define NG_BT_MBUFQ_DEQUEUE(q, i)			\
	do {						\
		(i) = (q)->head;			\
		if ((i) != NULL) {			\
			(q)->head = (q)->head->m_nextpkt; \
			if ((q)->head == NULL)		\
				(q)->tail = NULL;	\
							\
			(q)->len --;			\
			(i)->m_nextpkt = NULL;		\
		} 					\
	} while (0)

#define NG_BT_MBUFQ_PREPEND(q, i)			\
	do {						\
		(i)->m_nextpkt = (q)->head;		\
		if ((q)->tail == NULL)			\
			(q)->tail = (i);		\
							\
		(q)->head = (i);			\
		(q)->len ++;				\
	} while (0)

#define NG_BT_MBUFQ_DRAIN(q)				\
	do { 						\
        	struct mbuf	*m = NULL;		\
							\
		for (;;) { 				\
			NG_BT_MBUFQ_DEQUEUE((q), m);	\
			if (m == NULL) 			\
				break; 			\
							\
			NG_FREE_M(m);	 		\
		} 					\
	} while (0)

/* 
 * Netgraph item queue and useful itemq macros
 */

struct ng_item;

struct ng_bt_itemq {
	STAILQ_HEAD(, ng_item)	queue;	/* actually items queue */
	u_int32_t	 len;    /* number of items in the queue */
	u_int32_t	 maxlen; /* maximal number of items in the queue */
	u_int32_t	 drops;  /* number if dropped items */
};
typedef struct ng_bt_itemq	ng_bt_itemq_t;
typedef struct ng_bt_itemq *	ng_bt_itemq_p;

#define NG_BT_ITEMQ_INIT(q, _maxlen)			\
	do {						\
		STAILQ_INIT(&(q)->queue);		\
		(q)->len = 0;				\
		(q)->maxlen = (_maxlen);		\
		(q)->drops = 0;				\
	} while (0)

#define NG_BT_ITEMQ_DESTROY(q)				\
	do {						\
		NG_BT_ITEMQ_DRAIN((q));			\
	} while (0)

#define NG_BT_ITEMQ_FIRST(q)	STAILQ_FIRST(&(q)->queue)

#define NG_BT_ITEMQ_LEN(q)	NG_BT_MBUFQ_LEN((q))

#define NG_BT_ITEMQ_FULL(q)	NG_BT_MBUFQ_FULL((q))

#define NG_BT_ITEMQ_DROP(q)	NG_BT_MBUFQ_DROP((q))

#define NG_BT_ITEMQ_ENQUEUE(q, i)			\
	do {						\
		STAILQ_INSERT_TAIL(&(q)->queue, (i), el_next);	\
		(q)->len ++;				\
	} while (0)

#define NG_BT_ITEMQ_DEQUEUE(q, i)			\
	do {						\
		(i) = STAILQ_FIRST(&(q)->queue);	\
		if ((i) != NULL) {			\
			STAILQ_REMOVE_HEAD(&(q)->queue, el_next);	\
			(q)->len --;			\
		} 					\
	} while (0)

#define NG_BT_ITEMQ_PREPEND(q, i)			\
	do {						\
		STAILQ_INSERT_HEAD(&(q)->queue, (i), el_next);	\
		(q)->len ++;				\
	} while (0)

#define NG_BT_ITEMQ_DRAIN(q)				\
	do { 						\
        	struct ng_item	*i = NULL;		\
							\
		for (;;) { 				\
			NG_BT_ITEMQ_DEQUEUE((q), i);	\
			if (i == NULL) 			\
				break; 			\
							\
			NG_FREE_ITEM(i); 		\
		} 					\
	} while (0)

/*
 * Get Bluetooth stack sysctl globals
 */

u_int32_t	bluetooth_hci_command_timeout	(void);
u_int32_t	bluetooth_hci_connect_timeout	(void);
u_int32_t	bluetooth_hci_max_neighbor_age	(void);
u_int32_t	bluetooth_l2cap_rtx_timeout	(void);
u_int32_t	bluetooth_l2cap_ertx_timeout	(void);
u_int32_t      bluetooth_sco_rtx_timeout       (void);

#define BDADDR_BREDR 0
#define BDADDR_LE_PUBLIC 1
#define BDADDR_LE_RANDOM 2

#endif /* _NETGRAPH_BLUETOOTH_H_ */

