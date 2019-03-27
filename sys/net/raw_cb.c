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
 *	@(#)raw_cb.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/domain.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/vnet.h>
#include <net/raw_cb.h>

/*
 * Routines to manage the raw protocol control blocks.
 *
 * TODO:
 *	hash lookups by protocol family/protocol + address family
 *	take care of unique address problems per AF?
 *	redo address binding to allow wildcards
 */

struct mtx rawcb_mtx;
VNET_DEFINE(struct rawcb_list_head, rawcb_list);

static SYSCTL_NODE(_net, OID_AUTO, raw, CTLFLAG_RW, 0,
    "Raw socket infrastructure");

static u_long	raw_sendspace = RAWSNDQ;
SYSCTL_ULONG(_net_raw, OID_AUTO, sendspace, CTLFLAG_RW, &raw_sendspace, 0,
    "Default raw socket send space");

static u_long	raw_recvspace = RAWRCVQ;
SYSCTL_ULONG(_net_raw, OID_AUTO, recvspace, CTLFLAG_RW, &raw_recvspace, 0,
    "Default raw socket receive space");

/*
 * Allocate a control block and a nominal amount of buffer space for the
 * socket.
 */
int
raw_attach(struct socket *so, int proto)
{
	struct rawcb *rp = sotorawcb(so);
	int error;

	/*
	 * It is assumed that raw_attach is called after space has been
	 * allocated for the rawcb; consumer protocols may simply allocate
	 * type struct rawcb, or a wrapper data structure that begins with a
	 * struct rawcb.
	 */
	KASSERT(rp != NULL, ("raw_attach: rp == NULL"));

	error = soreserve(so, raw_sendspace, raw_recvspace);
	if (error)
		return (error);
	rp->rcb_socket = so;
	rp->rcb_proto.sp_family = so->so_proto->pr_domain->dom_family;
	rp->rcb_proto.sp_protocol = proto;
	mtx_lock(&rawcb_mtx);
	LIST_INSERT_HEAD(&V_rawcb_list, rp, list);
	mtx_unlock(&rawcb_mtx);
	return (0);
}

/*
 * Detach the raw connection block and discard socket resources.
 */
void
raw_detach(struct rawcb *rp)
{
	struct socket *so = rp->rcb_socket;

	KASSERT(so->so_pcb == rp, ("raw_detach: so_pcb != rp"));

	so->so_pcb = NULL;
	mtx_lock(&rawcb_mtx);
	LIST_REMOVE(rp, list);
	mtx_unlock(&rawcb_mtx);
	free((caddr_t)(rp), M_PCB);
}
