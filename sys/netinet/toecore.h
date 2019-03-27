/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Chelsio Communications, Inc.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _NETINET_TOE_H_
#define	_NETINET_TOE_H_

#ifndef _KERNEL
#error "no user-serviceable parts inside"
#endif

struct tcpopt;
struct tcphdr;
struct in_conninfo;
struct tcp_info;

struct toedev {
	TAILQ_ENTRY(toedev) link;	/* glue for toedev_list */
	void *tod_softc;		/* TOE driver private data */

	/*
	 * Active open.  If a failure occurs, it is reported back by the driver
	 * via toe_connect_failed.
	 */
	int (*tod_connect)(struct toedev *, struct socket *, struct rtentry *,
	    struct sockaddr *);

	/* Passive open. */
	int (*tod_listen_start)(struct toedev *, struct tcpcb *);
	int (*tod_listen_stop)(struct toedev *, struct tcpcb *);

	/*
	 * The kernel uses this routine to pass on any frame it receives for an
	 * offloaded connection to the TOE driver.  This is an unusual event.
	 */
	void (*tod_input)(struct toedev *, struct tcpcb *, struct mbuf *);

	/*
	 * This is called by the kernel during pru_rcvd for an offloaded TCP
	 * connection and provides an opportunity for the TOE driver to manage
	 * its rx window and credits.
	 */
	void (*tod_rcvd)(struct toedev *, struct tcpcb *);

	/*
	 * Transmit routine.  The kernel calls this to have the TOE driver
	 * evaluate whether there is data to be transmitted, and transmit it.
	 */
	int (*tod_output)(struct toedev *, struct tcpcb *);

	/* Immediate teardown: send RST to peer. */
	int (*tod_send_rst)(struct toedev *, struct tcpcb *);

	/* Initiate orderly disconnect by sending FIN to the peer. */
	int (*tod_send_fin)(struct toedev *, struct tcpcb *);

	/* Called to indicate that the kernel is done with this TCP PCB. */
	void (*tod_pcb_detach)(struct toedev *, struct tcpcb *);

	/*
	 * The kernel calls this once it has information about an L2 entry that
	 * the TOE driver enquired about previously (via toe_l2_resolve).
	 */
	void (*tod_l2_update)(struct toedev *, struct ifnet *,
	    struct sockaddr *, uint8_t *, uint16_t);

	/* XXX.  Route has been redirected. */
	void (*tod_route_redirect)(struct toedev *, struct ifnet *,
	    struct rtentry *, struct rtentry *);

	/* Syncache interaction. */
	void (*tod_syncache_added)(struct toedev *, void *);
	void (*tod_syncache_removed)(struct toedev *, void *);
	int (*tod_syncache_respond)(struct toedev *, void *, struct mbuf *);
	void (*tod_offload_socket)(struct toedev *, void *, struct socket *);

	/* TCP socket option */
	void (*tod_ctloutput)(struct toedev *, struct tcpcb *, int, int);

	/* Update software state */
	void (*tod_tcp_info)(struct toedev *, struct tcpcb *,
	    struct tcp_info *);
};

#include <sys/eventhandler.h>
typedef	void (*tcp_offload_listen_start_fn)(void *, struct tcpcb *);
typedef	void (*tcp_offload_listen_stop_fn)(void *, struct tcpcb *);
EVENTHANDLER_DECLARE(tcp_offload_listen_start, tcp_offload_listen_start_fn);
EVENTHANDLER_DECLARE(tcp_offload_listen_stop, tcp_offload_listen_stop_fn);

void init_toedev(struct toedev *);
int register_toedev(struct toedev *);
int unregister_toedev(struct toedev *);

/*
 * General interface for looking up L2 information for an IP address.  If an
 * answer is not available right away then the TOE driver's tod_l2_update will
 * be called later.
 */
int toe_l2_resolve(struct toedev *, struct ifnet *, struct sockaddr *,
    uint8_t *, uint16_t *);

void toe_connect_failed(struct toedev *, struct inpcb *, int);

void toe_syncache_add(struct in_conninfo *, struct tcpopt *, struct tcphdr *,
    struct inpcb *, void *, void *);
int  toe_syncache_expand(struct in_conninfo *, struct tcpopt *, struct tcphdr *,
    struct socket **);

int toe_4tuple_check(struct in_conninfo *, struct tcphdr *, struct ifnet *);
#endif
