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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sockopt.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/tcp.h>
#include <netinet/tcp_offload.h>
#define	TCPOUTFLAGS
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_var.h>
#include <netinet/toecore.h>

int registered_toedevs;

/*
 * Provide an opportunity for a TOE driver to offload.
 */
int
tcp_offload_connect(struct socket *so, struct sockaddr *nam)
{
	struct ifnet *ifp;
	struct toedev *tod;
	struct rtentry *rt;
	int error = EOPNOTSUPP;

	INP_WLOCK_ASSERT(sotoinpcb(so));
	KASSERT(nam->sa_family == AF_INET || nam->sa_family == AF_INET6,
	    ("%s: called with sa_family %d", __func__, nam->sa_family));

	if (registered_toedevs == 0)
		return (error);

	rt = rtalloc1(nam, 0, 0);
	if (rt)
		RT_UNLOCK(rt);
	else
		return (EHOSTUNREACH);

	ifp = rt->rt_ifp;

	if (nam->sa_family == AF_INET && !(ifp->if_capenable & IFCAP_TOE4))
		goto done;
	if (nam->sa_family == AF_INET6 && !(ifp->if_capenable & IFCAP_TOE6))
		goto done;

	tod = TOEDEV(ifp);
	if (tod != NULL)
		error = tod->tod_connect(tod, so, rt, nam);
done:
	RTFREE(rt);
	return (error);
}

void
tcp_offload_listen_start(struct tcpcb *tp)
{

	INP_WLOCK_ASSERT(tp->t_inpcb);

	EVENTHANDLER_INVOKE(tcp_offload_listen_start, tp);
}

void
tcp_offload_listen_stop(struct tcpcb *tp)
{

	INP_WLOCK_ASSERT(tp->t_inpcb);

	EVENTHANDLER_INVOKE(tcp_offload_listen_stop, tp);
}

void
tcp_offload_input(struct tcpcb *tp, struct mbuf *m)
{
	struct toedev *tod = tp->tod;

	KASSERT(tod != NULL, ("%s: tp->tod is NULL, tp %p", __func__, tp));
	INP_WLOCK_ASSERT(tp->t_inpcb);

	tod->tod_input(tod, tp, m);
}

int
tcp_offload_output(struct tcpcb *tp)
{
	struct toedev *tod = tp->tod;
	int error, flags;

	KASSERT(tod != NULL, ("%s: tp->tod is NULL, tp %p", __func__, tp));
	INP_WLOCK_ASSERT(tp->t_inpcb);

	flags = tcp_outflags[tp->t_state];

	if (flags & TH_RST) {
		/* XXX: avoid repeated calls like we do for FIN */
		error = tod->tod_send_rst(tod, tp);
	} else if ((flags & TH_FIN || tp->t_flags & TF_NEEDFIN) &&
	    (tp->t_flags & TF_SENTFIN) == 0) {
		error = tod->tod_send_fin(tod, tp);
		if (error == 0)
			tp->t_flags |= TF_SENTFIN;
	} else
		error = tod->tod_output(tod, tp);

	return (error);
}

void
tcp_offload_rcvd(struct tcpcb *tp)
{
	struct toedev *tod = tp->tod;

	KASSERT(tod != NULL, ("%s: tp->tod is NULL, tp %p", __func__, tp));
	INP_WLOCK_ASSERT(tp->t_inpcb);

	tod->tod_rcvd(tod, tp);
}

void
tcp_offload_ctloutput(struct tcpcb *tp, int sopt_dir, int sopt_name)
{
	struct toedev *tod = tp->tod;

	KASSERT(tod != NULL, ("%s: tp->tod is NULL, tp %p", __func__, tp));
	INP_WLOCK_ASSERT(tp->t_inpcb);

	tod->tod_ctloutput(tod, tp, sopt_dir, sopt_name);
}

void
tcp_offload_tcp_info(struct tcpcb *tp, struct tcp_info *ti)
{
	struct toedev *tod = tp->tod;

	KASSERT(tod != NULL, ("%s: tp->tod is NULL, tp %p", __func__, tp));
	INP_WLOCK_ASSERT(tp->t_inpcb);

	tod->tod_tcp_info(tod, tp, ti);
}

void
tcp_offload_detach(struct tcpcb *tp)
{
	struct toedev *tod = tp->tod;

	KASSERT(tod != NULL, ("%s: tp->tod is NULL, tp %p", __func__, tp));
	INP_WLOCK_ASSERT(tp->t_inpcb);

	tod->tod_pcb_detach(tod, tp);
}
