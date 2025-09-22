/*	$OpenBSD: if_pppoe.h,v 1.11 2025/07/06 23:34:50 jsg Exp $ */
/*	$NetBSD: if_pppoe.h,v 1.5 2003/11/28 08:56:48 keihan Exp $ */

/*
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Martin Husemann <martin@NetBSD.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _NET_IF_PPPOE_H_
#define _NET_IF_PPPOE_H_

#define PPPOE_NAMELEN	512		/* should be enough */
struct pppoediscparms {
	char	ifname[IFNAMSIZ];	/* pppoe interface name */
	char	eth_ifname[IFNAMSIZ];	/* external ethernet interface name */
	char	ac_name[PPPOE_NAMELEN];	/* access concentrator name */
	char	service_name[PPPOE_NAMELEN]; /* service name */
};

#define	PPPOESETPARMS	_IOW('i', 110, struct pppoediscparms)
#define	PPPOEGETPARMS	_IOWR('i', 111, struct pppoediscparms)

#define PPPOE_STATE_INITIAL	0
#define PPPOE_STATE_PADI_SENT	1
#define	PPPOE_STATE_PADR_SENT	2
#define	PPPOE_STATE_SESSION	3
#define	PPPOE_STATE_CLOSING	4
/* passive */
#define	PPPOE_STATE_PADO_SENT	1

struct pppoeconnectionstate {
	char	ifname[IFNAMSIZ];	/* pppoe interface name */
	u_int	state;			/* one of the PPPOE_STATE_ states above */
	u_int	session_id;		/* if state == PPPOE_STATE_SESSION */
	u_int	padi_retry_no;		/* number of retries already sent */
	u_int	padr_retry_no;

	struct timeval session_time;	/* time the session was established */
};

#define PPPOEGETSESSION	_IOWR('i', 112, struct pppoeconnectionstate)

#ifdef _KERNEL

extern struct mbuf_queue pppoediscinq;
extern struct mbuf_queue pppoeinq;

struct mbuf	*pppoe_vinput(struct ifnet *, struct mbuf *, struct netstack *);

#endif /* _KERNEL */
#endif /* _NET_IF_PPPOE_H_ */
