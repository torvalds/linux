/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1986, 1993
 *	The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)raw_cb.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#ifndef _NET_RAW_CB_H_
#define _NET_RAW_CB_H_

#include <sys/queue.h>

/*
 * Raw protocol interface control block.  Used to tie a socket to the generic
 * raw interface.
 */
struct rawcb {
	LIST_ENTRY(rawcb) list;
	struct	socket *rcb_socket;	/* back pointer to socket */
	struct	sockproto rcb_proto;	/* protocol family, protocol */
};

#define	sotorawcb(so)		((struct rawcb *)(so)->so_pcb)

/*
 * Nominal space allocated to a raw socket.
 */
#define	RAWSNDQ		8192
#define	RAWRCVQ		8192

#ifdef _KERNEL
VNET_DECLARE(LIST_HEAD(rawcb_list_head, rawcb), rawcb_list);
#define	V_rawcb_list	VNET(rawcb_list)

extern struct mtx rawcb_mtx;

/*
 * Generic protosw entries for raw socket protocols.
 */
pr_ctlinput_t	raw_ctlinput;
pr_init_t	raw_init;

/*
 * Library routines for raw socket usrreq functions; will always be wrapped
 * so that protocol-specific functions can be handled.
 */
typedef int (*raw_input_cb_fn)(struct mbuf *, struct sockproto *,
    struct sockaddr *, struct rawcb *);

int	 raw_attach(struct socket *, int);
void	 raw_detach(struct rawcb *);
void	 raw_input(struct mbuf *, struct sockproto *, struct sockaddr *);
void	 raw_input_ext(struct mbuf *, struct sockproto *, struct sockaddr *,
	    raw_input_cb_fn);

/*
 * Generic pr_usrreqs entries for raw socket protocols, usually wrapped so
 * that protocol-specific functions can be handled.
 */
extern	struct pr_usrreqs raw_usrreqs;
#endif

#endif
