/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: Navdeep Parhar <np@FreeBSD.org>
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
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/sockopt.h>
#include <sys/sysctl.h>
#include <sys/socket.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>
#include <net/if_llatbl.h>
#include <net/route.h>

#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet6/in6_var.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/nd6.h>
#define TCPSTATES
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_syncache.h>
#include <netinet/tcp_offload.h>
#include <netinet/toecore.h>

static struct mtx toedev_lock;
static TAILQ_HEAD(, toedev) toedev_list;
static eventhandler_tag listen_start_eh;
static eventhandler_tag listen_stop_eh;
static eventhandler_tag lle_event_eh;

static int
toedev_connect(struct toedev *tod __unused, struct socket *so __unused,
    struct rtentry *rt __unused, struct sockaddr *nam __unused)
{

	return (ENOTSUP);
}

static int
toedev_listen_start(struct toedev *tod __unused, struct tcpcb *tp __unused)
{

	return (ENOTSUP);
}

static int
toedev_listen_stop(struct toedev *tod __unused, struct tcpcb *tp __unused)
{

	return (ENOTSUP);
}

static void
toedev_input(struct toedev *tod __unused, struct tcpcb *tp __unused,
    struct mbuf *m)
{

	m_freem(m);
	return;
}

static void
toedev_rcvd(struct toedev *tod __unused, struct tcpcb *tp __unused)
{

	return;
}

static int
toedev_output(struct toedev *tod __unused, struct tcpcb *tp __unused)
{

	return (ENOTSUP);
}

static void
toedev_pcb_detach(struct toedev *tod __unused, struct tcpcb *tp __unused)
{

	return;
}

static void
toedev_l2_update(struct toedev *tod __unused, struct ifnet *ifp __unused,
    struct sockaddr *sa __unused, uint8_t *lladdr __unused,
    uint16_t vtag __unused)
{

	return;
}

static void
toedev_route_redirect(struct toedev *tod __unused, struct ifnet *ifp __unused,
    struct rtentry *rt0 __unused, struct rtentry *rt1 __unused)
{

	return;
}

static void
toedev_syncache_added(struct toedev *tod __unused, void *ctx __unused)
{

	return;
}

static void
toedev_syncache_removed(struct toedev *tod __unused, void *ctx __unused)
{

	return;
}

static int
toedev_syncache_respond(struct toedev *tod __unused, void *ctx __unused,
    struct mbuf *m)
{

	m_freem(m);
	return (0);
}

static void
toedev_offload_socket(struct toedev *tod __unused, void *ctx __unused,
    struct socket *so __unused)
{

	return;
}

static void
toedev_ctloutput(struct toedev *tod __unused, struct tcpcb *tp __unused,
    int sopt_dir __unused, int sopt_name __unused)
{

	return;
}

static void
toedev_tcp_info(struct toedev *tod __unused, struct tcpcb *tp __unused,
    struct tcp_info *ti __unused)
{

	return;
}

/*
 * Inform one or more TOE devices about a listening socket.
 */
static void
toe_listen_start(struct inpcb *inp, void *arg)
{
	struct toedev *t, *tod;
	struct tcpcb *tp;

	INP_WLOCK_ASSERT(inp);
	KASSERT(inp->inp_pcbinfo == &V_tcbinfo,
	    ("%s: inp is not a TCP inp", __func__));

	if (inp->inp_flags & (INP_DROPPED | INP_TIMEWAIT))
		return;

	tp = intotcpcb(inp);
	if (tp->t_state != TCPS_LISTEN)
		return;

	t = arg;
	mtx_lock(&toedev_lock);
	TAILQ_FOREACH(tod, &toedev_list, link) {
		if (t == NULL || t == tod)
			tod->tod_listen_start(tod, tp);
	}
	mtx_unlock(&toedev_lock);
}

static void
toe_listen_start_event(void *arg __unused, struct tcpcb *tp)
{
	struct inpcb *inp = tp->t_inpcb;

	INP_WLOCK_ASSERT(inp);
	KASSERT(tp->t_state == TCPS_LISTEN,
	    ("%s: t_state %s", __func__, tcpstates[tp->t_state]));

	toe_listen_start(inp, NULL);
}

static void
toe_listen_stop_event(void *arg __unused, struct tcpcb *tp)
{
	struct toedev *tod;
#ifdef INVARIANTS
	struct inpcb *inp = tp->t_inpcb;
#endif

	INP_WLOCK_ASSERT(inp);
	KASSERT(tp->t_state == TCPS_LISTEN,
	    ("%s: t_state %s", __func__, tcpstates[tp->t_state]));

	mtx_lock(&toedev_lock);
	TAILQ_FOREACH(tod, &toedev_list, link)
	    tod->tod_listen_stop(tod, tp);
	mtx_unlock(&toedev_lock);
}

/*
 * Fill up a freshly allocated toedev struct with reasonable defaults.
 */
void
init_toedev(struct toedev *tod)
{

	tod->tod_softc = NULL;

	/*
	 * Provide no-op defaults so that the kernel can call any toedev
	 * function without having to check whether the TOE driver supplied one
	 * or not.
	 */
	tod->tod_connect = toedev_connect;
	tod->tod_listen_start = toedev_listen_start;
	tod->tod_listen_stop = toedev_listen_stop;
	tod->tod_input = toedev_input;
	tod->tod_rcvd = toedev_rcvd;
	tod->tod_output = toedev_output;
	tod->tod_send_rst = toedev_output;
	tod->tod_send_fin = toedev_output;
	tod->tod_pcb_detach = toedev_pcb_detach;
	tod->tod_l2_update = toedev_l2_update;
	tod->tod_route_redirect = toedev_route_redirect;
	tod->tod_syncache_added = toedev_syncache_added;
	tod->tod_syncache_removed = toedev_syncache_removed;
	tod->tod_syncache_respond = toedev_syncache_respond;
	tod->tod_offload_socket = toedev_offload_socket;
	tod->tod_ctloutput = toedev_ctloutput;
	tod->tod_tcp_info = toedev_tcp_info;
}

/*
 * Register an active TOE device with the system.  This allows it to receive
 * notifications from the kernel.
 */
int
register_toedev(struct toedev *tod)
{
	struct toedev *t;

	mtx_lock(&toedev_lock);
	TAILQ_FOREACH(t, &toedev_list, link) {
		if (t == tod) {
			mtx_unlock(&toedev_lock);
			return (EEXIST);
		}
	}

	TAILQ_INSERT_TAIL(&toedev_list, tod, link);
	registered_toedevs++;
	mtx_unlock(&toedev_lock);

	inp_apply_all(toe_listen_start, tod);

	return (0);
}

/*
 * Remove the TOE device from the global list of active TOE devices.  It is the
 * caller's responsibility to ensure that the TOE device is quiesced prior to
 * this call.
 */
int
unregister_toedev(struct toedev *tod)
{
	struct toedev *t, *t2;
	int rc = ENODEV;

	mtx_lock(&toedev_lock);
	TAILQ_FOREACH_SAFE(t, &toedev_list, link, t2) {
		if (t == tod) {
			TAILQ_REMOVE(&toedev_list, tod, link);
			registered_toedevs--;
			rc = 0;
			break;
		}
	}
	KASSERT(registered_toedevs >= 0,
	    ("%s: registered_toedevs (%d) < 0", __func__, registered_toedevs));
	mtx_unlock(&toedev_lock);
	return (rc);
}

void
toe_syncache_add(struct in_conninfo *inc, struct tcpopt *to, struct tcphdr *th,
    struct inpcb *inp, void *tod, void *todctx)
{
	struct socket *lso = inp->inp_socket;

	INP_WLOCK_ASSERT(inp);

	syncache_add(inc, to, th, inp, &lso, NULL, tod, todctx);
}

int
toe_syncache_expand(struct in_conninfo *inc, struct tcpopt *to,
    struct tcphdr *th, struct socket **lsop)
{

	INP_INFO_RLOCK_ASSERT(&V_tcbinfo);

	return (syncache_expand(inc, to, th, lsop, NULL));
}

/*
 * General purpose check to see if a 4-tuple is in use by the kernel.  If a TCP
 * header (presumably for an incoming SYN) is also provided, an existing 4-tuple
 * in TIME_WAIT may be assassinated freeing it up for re-use.
 *
 * Note that the TCP header must have been run through tcp_fields_to_host() or
 * equivalent.
 */
int
toe_4tuple_check(struct in_conninfo *inc, struct tcphdr *th, struct ifnet *ifp)
{
	struct inpcb *inp;

	if (inc->inc_flags & INC_ISIPV6) {
		inp = in6_pcblookup(&V_tcbinfo, &inc->inc6_faddr,
		    inc->inc_fport, &inc->inc6_laddr, inc->inc_lport,
		    INPLOOKUP_WLOCKPCB, ifp);
	} else {
		inp = in_pcblookup(&V_tcbinfo, inc->inc_faddr, inc->inc_fport,
		    inc->inc_laddr, inc->inc_lport, INPLOOKUP_WLOCKPCB, ifp);
	}
	if (inp != NULL) {
		INP_WLOCK_ASSERT(inp);

		if ((inp->inp_flags & INP_TIMEWAIT) && th != NULL) {

			INP_INFO_RLOCK_ASSERT(&V_tcbinfo); /* for twcheck */
			if (!tcp_twcheck(inp, NULL, th, NULL, 0))
				return (EADDRINUSE);
		} else {
			INP_WUNLOCK(inp);
			return (EADDRINUSE);
		}
	}

	return (0);
}

static void
toe_lle_event(void *arg __unused, struct llentry *lle, int evt)
{
	struct toedev *tod;
	struct ifnet *ifp;
	struct sockaddr *sa;
	uint8_t *lladdr;
	uint16_t vid, pcp;
	int family;
	struct sockaddr_in6 sin6;

	LLE_WLOCK_ASSERT(lle);

	ifp = lltable_get_ifp(lle->lle_tbl);
	family = lltable_get_af(lle->lle_tbl);

	if (family != AF_INET && family != AF_INET6)
		return;
	/*
	 * Not interested if the interface's TOE capability is not enabled.
	 */
	if ((family == AF_INET && !(ifp->if_capenable & IFCAP_TOE4)) ||
	    (family == AF_INET6 && !(ifp->if_capenable & IFCAP_TOE6)))
		return;

	tod = TOEDEV(ifp);
	if (tod == NULL)
		return;

	sa = (struct sockaddr *)&sin6;
	lltable_fill_sa_entry(lle, sa);

	vid = 0xfff;
	pcp = 0;
	if (evt != LLENTRY_RESOLVED) {

		/*
		 * LLENTRY_TIMEDOUT, LLENTRY_DELETED, LLENTRY_EXPIRED all mean
		 * this entry is going to be deleted.
		 */

		lladdr = NULL;
	} else {

		KASSERT(lle->la_flags & LLE_VALID,
		    ("%s: %p resolved but not valid?", __func__, lle));

		lladdr = (uint8_t *)lle->ll_addr;
		VLAN_TAG(ifp, &vid);
		VLAN_PCP(ifp, &pcp);
	}

	tod->tod_l2_update(tod, ifp, sa, lladdr, EVL_MAKETAG(vid, pcp, 0));
}

/*
 * Returns 0 or EWOULDBLOCK on success (any other value is an error).  0 means
 * lladdr and vtag are valid on return, EWOULDBLOCK means the TOE driver's
 * tod_l2_update will be called later, when the entry is resolved or times out.
 */
int
toe_l2_resolve(struct toedev *tod, struct ifnet *ifp, struct sockaddr *sa,
    uint8_t *lladdr, uint16_t *vtag)
{
	int rc;
	uint16_t vid, pcp;

	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		rc = arpresolve(ifp, 0, NULL, sa, lladdr, NULL, NULL);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		rc = nd6_resolve(ifp, 0, NULL, sa, lladdr, NULL, NULL);
		break;
#endif
	default:
		return (EPROTONOSUPPORT);
	}

	if (rc == 0) {
		vid = 0xfff;
		pcp = 0;
		if (ifp->if_type == IFT_L2VLAN) {
			VLAN_TAG(ifp, &vid);
			VLAN_PCP(ifp, &pcp);
		} else if (ifp->if_pcp != IFNET_PCP_NONE) {
			vid = 0;
			pcp = ifp->if_pcp;
		}
		*vtag = EVL_MAKETAG(vid, pcp, 0);
	}

	return (rc);
}

void
toe_connect_failed(struct toedev *tod, struct inpcb *inp, int err)
{

	INP_WLOCK_ASSERT(inp);

	if (!(inp->inp_flags & INP_DROPPED)) {
		struct tcpcb *tp = intotcpcb(inp);

		KASSERT(tp->t_flags & TF_TOE,
		    ("%s: tp %p not offloaded.", __func__, tp));

		if (err == EAGAIN) {

			/*
			 * Temporary failure during offload, take this PCB back.
			 * Detach from the TOE driver and do the rest of what
			 * TCP's pru_connect would have done if the connection
			 * wasn't offloaded.
			 */

			tod->tod_pcb_detach(tod, tp);
			KASSERT(!(tp->t_flags & TF_TOE),
			    ("%s: tp %p still offloaded.", __func__, tp));
			tcp_timer_activate(tp, TT_KEEP, TP_KEEPINIT(tp));
			(void) tp->t_fb->tfb_tcp_output(tp);
		} else {

			INP_INFO_RLOCK_ASSERT(&V_tcbinfo);
			tp = tcp_drop(tp, err);
			if (tp == NULL)
				INP_WLOCK(inp);	/* re-acquire */
		}
	}
	INP_WLOCK_ASSERT(inp);
}

static int
toecore_load(void)
{

	mtx_init(&toedev_lock, "toedev lock", NULL, MTX_DEF);
	TAILQ_INIT(&toedev_list);

	listen_start_eh = EVENTHANDLER_REGISTER(tcp_offload_listen_start,
	    toe_listen_start_event, NULL, EVENTHANDLER_PRI_ANY);
	listen_stop_eh = EVENTHANDLER_REGISTER(tcp_offload_listen_stop,
	    toe_listen_stop_event, NULL, EVENTHANDLER_PRI_ANY);
	lle_event_eh = EVENTHANDLER_REGISTER(lle_event, toe_lle_event, NULL,
	    EVENTHANDLER_PRI_ANY);

	return (0);
}

static int
toecore_unload(void)
{

	mtx_lock(&toedev_lock);
	if (!TAILQ_EMPTY(&toedev_list)) {
		mtx_unlock(&toedev_lock);
		return (EBUSY);
	}

	EVENTHANDLER_DEREGISTER(tcp_offload_listen_start, listen_start_eh);
	EVENTHANDLER_DEREGISTER(tcp_offload_listen_stop, listen_stop_eh);
	EVENTHANDLER_DEREGISTER(lle_event, lle_event_eh);

	mtx_unlock(&toedev_lock);
	mtx_destroy(&toedev_lock);

	return (0);
}

static int
toecore_mod_handler(module_t mod, int cmd, void *arg)
{

	if (cmd == MOD_LOAD)
		return (toecore_load());

	if (cmd == MOD_UNLOAD)
		return (toecore_unload());

	return (EOPNOTSUPP);
}

static moduledata_t mod_data= {
	"toecore",
	toecore_mod_handler,
	0
};

MODULE_VERSION(toecore, 1);
DECLARE_MODULE(toecore, mod_data, SI_SUB_EXEC, SI_ORDER_ANY);
