/*	$NetBSD: btpand.h,v 1.1 2008/08/17 13:20:57 plunky Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2008 Iain Hibbert
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* $FreeBSD$ */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/ethernet.h>

#include <assert.h>
#include <bluetooth.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "event.h"

#ifndef __arraycount
#define __arraycount(__x)	(int)(sizeof((__x)) / sizeof((__x)[0]))
#endif

#ifndef	L2CAP_PSM_INVALID
#define	L2CAP_PSM_INVALID(psm)	(((psm) & 0x0101) != 0x0001)
#endif

#ifndef	L2CAP_PSM_BNEP
#define	L2CAP_PSM_BNEP	15
#endif

typedef struct channel	channel_t;
typedef struct pfilter	pfilter_t;
typedef struct mfilter	mfilter_t;
typedef struct packet	packet_t;
typedef struct pkthdr	pkthdr_t;
typedef struct pktlist	pktlist_t;
typedef struct exthdr	exthdr_t;
typedef struct extlist	extlist_t;

LIST_HEAD(chlist, channel);
STAILQ_HEAD(extlist, exthdr);
STAILQ_HEAD(pktlist, pkthdr);

enum channel_state {
	CHANNEL_CLOSED,
	CHANNEL_WAIT_CONNECT_REQ,
	CHANNEL_WAIT_CONNECT_RSP,
	CHANNEL_OPEN,
};

#define CHANNEL_MAXQLEN		128

/* BNEP or tap channel */
struct channel {
	enum channel_state	state;
	bool			oactive;

	uint8_t			laddr[ETHER_ADDR_LEN];
	uint8_t			raddr[ETHER_ADDR_LEN];
	size_t			mru;
	size_t			mtu;

	int			npfilter;
	pfilter_t *		pfilter;

	int			nmfilter;
	mfilter_t *		mfilter;

	pktlist_t		pktlist;
	int			qlen;

	int			fd;
	struct event		rd_ev;
	struct event		wr_ev;
	uint8_t *		sendbuf;

	bool			(*send)(channel_t *, packet_t *);
	bool			(*recv)(packet_t *);

	int			tick;

	struct pidfh		*pfh;

	int			refcnt;
	LIST_ENTRY(channel)	next;
};

/* network protocol type filter */
struct pfilter {
	uint16_t	start;
	uint16_t	end;
};

/* multicast address filter */
struct mfilter {
	uint8_t		start[ETHER_ADDR_LEN];
	uint8_t		end[ETHER_ADDR_LEN];
};

/* packet data buffer */
struct packet {
	channel_t *		chan;	/* source channel */
	uint8_t *		dst;	/* dest address */
	uint8_t *		src;	/* source address */
	uint8_t *		type;	/* protocol type */
	uint8_t *		ptr;	/* data pointer */
	size_t			len;	/* data length */
	int			refcnt;	/* reference count */
	extlist_t		extlist;/* extension headers */
	uint8_t			buf[0];	/* data starts here */
};

/* extension header */
struct exthdr {
	STAILQ_ENTRY(exthdr)	next;
	uint8_t *		ptr;
	uint8_t			len;
};

/* packet header */
struct pkthdr {
	STAILQ_ENTRY(pkthdr)	next;
	packet_t *		data;
};

/* global variables */
extern const char *	control_path;
extern const char *	service_name;
extern const char *	interface_name;
extern bdaddr_t		local_bdaddr;
extern bdaddr_t		remote_bdaddr;
extern uint16_t		l2cap_psm;
extern int		l2cap_mode;
extern uint16_t		service_class;
extern int		server_limit;

/*
 * Bluetooth addresses are stored the other way around than
 * Ethernet addresses even though they are of the same family
 */
static inline void
b2eaddr(void *dst, bdaddr_t *src)
{
	uint8_t *d = dst;
	int i;

	for (i = 0; i < ETHER_ADDR_LEN; i++)
		d[i] = src->b[ETHER_ADDR_LEN - i - 1];
}

#define log_err(fmt, args...)		syslog(LOG_ERR, fmt , ##args)
#define log_info(fmt, args...)		syslog(LOG_INFO, fmt , ##args)
#define log_notice(fmt, args...)	syslog(LOG_NOTICE, fmt , ##args)
#define log_debug(fmt, args...)		syslog(LOG_DEBUG, "%s: " fmt, __func__ , ##args)

/* bnep.c */
bool		bnep_send(channel_t *, packet_t *);
bool		bnep_recv(packet_t *);
void		bnep_send_control(channel_t *, unsigned, ...);

/* channel.c */
void		channel_init(void);
channel_t *	channel_alloc(void);
bool		channel_open(channel_t *, int);
void		channel_close(channel_t *);
void		channel_free(channel_t *);
void		channel_timeout(channel_t *, int);
void		channel_put(channel_t *, packet_t *);

/* client.c */
void		client_init(void);

/* packet.c */
packet_t *	packet_alloc(channel_t *);
void		packet_free(packet_t *);
void		packet_adj(packet_t *, size_t);
pkthdr_t *	pkthdr_alloc(packet_t *);
void		pkthdr_free(pkthdr_t *);

/* server.c */
void		server_init(void);
void		server_update(int);

/* tap.c */
void		tap_init(void);
