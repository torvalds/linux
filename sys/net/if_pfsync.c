/*	$OpenBSD: if_pfsync.c,v 1.332 2025/07/07 02:28:50 jsg Exp $	*/

/*
 * Copyright (c) 2002 Michael Shalayeff
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2009, 2022, 2023 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bpfilter.h"
#include "kstat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/timeout.h>
#include <sys/sysctl.h>
#include <sys/pool.h>
#include <sys/syslog.h>
#include <sys/smr.h>
#include <sys/percpu.h>
#include <sys/refcnt.h>
#include <sys/kstat.h>
#include <sys/stdarg.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip_ipsp.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_fsm.h>
#include <netinet/udp.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#endif /* INET6 */

#include "carp.h"
#if NCARP > 0
#include <netinet/ip_carp.h>
#endif

#include <net/pfvar.h>
#include <net/pfvar_priv.h>
#include <net/if_pfsync.h>

#define PFSYNC_MINPKT ( \
	sizeof(struct ip) + \
	sizeof(struct pfsync_header))

struct pfsync_softc;

struct pfsync_deferral {
	TAILQ_ENTRY(pfsync_deferral)		 pd_entry;
	struct pf_state				*pd_st;
	struct mbuf				*pd_m;
	uint64_t				 pd_deadline;
};
TAILQ_HEAD(pfsync_deferrals, pfsync_deferral);

#define PFSYNC_DEFER_NSEC	20000000ULL
#define PFSYNC_DEFER_LIMIT	128
#define PFSYNC_BULK_SND_IVAL_MS	20

static struct pool pfsync_deferrals_pool;

enum pfsync_bulk_req_state {
	PFSYNC_BREQ_S_NONE,
	PFSYNC_BREQ_S_START,
	PFSYNC_BREQ_S_SENT,
	PFSYNC_BREQ_S_BULK,
	PFSYNC_BREQ_S_DONE,
};

static const char *pfsync_bulk_req_state_names[] = {
	[PFSYNC_BREQ_S_NONE]		= "none",
	[PFSYNC_BREQ_S_START]		= "start",
	[PFSYNC_BREQ_S_SENT]		= "sent",
	[PFSYNC_BREQ_S_BULK]		= "bulk",
	[PFSYNC_BREQ_S_DONE]		= "done",
};

enum pfsync_bulk_req_event {
	PFSYNC_BREQ_EVT_UP,
	PFSYNC_BREQ_EVT_DOWN,
	PFSYNC_BREQ_EVT_TMO,
	PFSYNC_BREQ_EVT_LINK,
	PFSYNC_BREQ_EVT_BUS_START,
	PFSYNC_BREQ_EVT_BUS_END,
};

static const char *pfsync_bulk_req_event_names[] = {
	[PFSYNC_BREQ_EVT_UP]		= "up",
	[PFSYNC_BREQ_EVT_DOWN]		= "down",
	[PFSYNC_BREQ_EVT_TMO]		= "timeout",
	[PFSYNC_BREQ_EVT_LINK]		= "link",
	[PFSYNC_BREQ_EVT_BUS_START]	= "bus-start",
	[PFSYNC_BREQ_EVT_BUS_END]	= "bus-end",
};

struct pfsync_slice {
	struct pfsync_softc	*s_pfsync;
	struct mutex		 s_mtx;

	struct pf_state_queue	 s_qs[PFSYNC_S_COUNT];
	TAILQ_HEAD(, tdb)	 s_tdb_q;
	size_t			 s_len;
	struct mbuf_list	 s_ml;

	struct taskq		*s_softnet;
	struct task		 s_task;
	struct timeout		 s_tmo;

	struct mbuf_queue	 s_sendq;
	struct task		 s_send;

	struct pfsync_deferrals	 s_deferrals;
	unsigned int		 s_deferred;
	struct task		 s_deferrals_task;
	struct timeout		 s_deferrals_tmo;

	uint64_t		 s_stat_locks;
	uint64_t		 s_stat_contended;
	uint64_t		 s_stat_write_nop;
	uint64_t		 s_stat_task_add;
	uint64_t		 s_stat_task_run;
	uint64_t		 s_stat_enqueue;
	uint64_t		 s_stat_dequeue;

	uint64_t		 s_stat_defer_add;
	uint64_t		 s_stat_defer_ack;
	uint64_t		 s_stat_defer_run;
	uint64_t		 s_stat_defer_overlimit;

	struct kstat		*s_kstat;
} __aligned(CACHELINESIZE);

#define PFSYNC_SLICE_BITS	 1
#define PFSYNC_NSLICES		 (1 << PFSYNC_SLICE_BITS)

struct pfsync_softc {
	struct ifnet		 sc_if;
	unsigned int		 sc_dead;
	unsigned int		 sc_up;
	struct refcnt		 sc_refs;

	/* config */
	struct in_addr		 sc_syncpeer;
	unsigned int		 sc_maxupdates;
	unsigned int		 sc_defer;

	/* operation */
	unsigned int		 sc_sync_ifidx;
	unsigned int		 sc_sync_if_down;
	void			*sc_inm;
	struct task		 sc_ltask;
	struct task		 sc_dtask;
	struct ip		 sc_template;

	struct pfsync_slice	 sc_slices[PFSYNC_NSLICES];

	struct {
		struct rwlock			 req_lock;
		struct timeout			 req_tmo;
		enum pfsync_bulk_req_state	 req_state;
		unsigned int			 req_tries;
		unsigned int			 req_demoted;
	}			 sc_bulk_req;

	struct {
		struct rwlock			 snd_lock;
		struct timeout			 snd_tmo;
		time_t				 snd_requested;

		struct pf_state			*snd_next;
		struct pf_state			*snd_tail;
		unsigned int			 snd_again;
	}			 sc_bulk_snd;
};

static struct pfsync_softc	*pfsyncif = NULL;
static struct cpumem		*pfsynccounters;

static inline void
pfsyncstat_inc(enum pfsync_counters c)
{
	counters_inc(pfsynccounters, c);
}

static int	pfsync_clone_create(struct if_clone *, int);
static int	pfsync_clone_destroy(struct ifnet *);

static int	pfsync_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		    struct rtentry *);
static void	pfsync_start(struct ifqueue *);

static int	pfsync_ioctl(struct ifnet *, u_long, caddr_t);
static int	pfsync_up(struct pfsync_softc *);
static int	pfsync_down(struct pfsync_softc *);

static int	pfsync_set_mtu(struct pfsync_softc *, unsigned int);
static int	pfsync_set_parent(struct pfsync_softc *,
		    const struct if_parent *);
static int	pfsync_get_parent(struct pfsync_softc *, struct if_parent *);
static int	pfsync_del_parent(struct pfsync_softc *);

static int	pfsync_get_ioc(struct pfsync_softc *, struct ifreq *);
static int	pfsync_set_ioc(struct pfsync_softc *, struct ifreq *);

static void	pfsync_syncif_link(void *);
static void	pfsync_syncif_detach(void *);

static void	pfsync_sendout(struct pfsync_softc *, struct mbuf *);
static void	pfsync_slice_drop(struct pfsync_softc *, struct pfsync_slice *);

static void	pfsync_slice_tmo(void *);
static void	pfsync_slice_task(void *);
static void	pfsync_slice_sendq(void *);

static void	pfsync_deferrals_tmo(void *);
static void	pfsync_deferrals_task(void *);
static void	pfsync_defer_output(struct pfsync_deferral *);

static void	pfsync_bulk_req_evt(struct pfsync_softc *,
		    enum pfsync_bulk_req_event);
static void	pfsync_bulk_req_tmo(void *);

static void	pfsync_bulk_snd_tmo(void *);

#if NKSTAT > 0
struct pfsync_kstat_data {
	struct kstat_kv pd_locks;
	struct kstat_kv pd_contended;
	struct kstat_kv pd_write_nop;
	struct kstat_kv pd_task_add;
	struct kstat_kv pd_task_run;
	struct kstat_kv pd_enqueue;
	struct kstat_kv pd_dequeue;
	struct kstat_kv pd_qdrop;

	struct kstat_kv pd_defer_len;
	struct kstat_kv pd_defer_add;
	struct kstat_kv pd_defer_ack;
	struct kstat_kv pd_defer_run;
	struct kstat_kv pd_defer_overlimit;
};

static const struct pfsync_kstat_data pfsync_kstat_tpl = {
	KSTAT_KV_INITIALIZER("locks",		KSTAT_KV_T_COUNTER64),
	KSTAT_KV_INITIALIZER("contended",	KSTAT_KV_T_COUNTER64),
	KSTAT_KV_INITIALIZER("write-nops",	KSTAT_KV_T_COUNTER64),
	KSTAT_KV_INITIALIZER("send-sched",	KSTAT_KV_T_COUNTER64),
	KSTAT_KV_INITIALIZER("send-run",	KSTAT_KV_T_COUNTER64),
	KSTAT_KV_INITIALIZER("enqueues",	KSTAT_KV_T_COUNTER64),
	KSTAT_KV_INITIALIZER("dequeues",	KSTAT_KV_T_COUNTER64),
	KSTAT_KV_UNIT_INITIALIZER("qdrops",
	    KSTAT_KV_T_COUNTER32, KSTAT_KV_U_PACKETS),

	KSTAT_KV_UNIT_INITIALIZER("defer-len",
	    KSTAT_KV_T_COUNTER32, KSTAT_KV_U_PACKETS),
	KSTAT_KV_INITIALIZER("defer-add",	KSTAT_KV_T_COUNTER64),
	KSTAT_KV_INITIALIZER("defer-ack",	KSTAT_KV_T_COUNTER64),
	KSTAT_KV_INITIALIZER("defer-run",	KSTAT_KV_T_COUNTER64),
	KSTAT_KV_INITIALIZER("defer-over",	KSTAT_KV_T_COUNTER64),
};

static int
pfsync_kstat_copy(struct kstat *ks, void *dst)
{
	struct pfsync_slice *s = ks->ks_softc;
	struct pfsync_kstat_data *pd = dst;

	*pd = pfsync_kstat_tpl;
	kstat_kv_u64(&pd->pd_locks) = s->s_stat_locks;
	kstat_kv_u64(&pd->pd_contended) = s->s_stat_contended;
	kstat_kv_u64(&pd->pd_write_nop) = s->s_stat_write_nop;
	kstat_kv_u64(&pd->pd_task_add) = s->s_stat_task_add;
	kstat_kv_u64(&pd->pd_task_run) = s->s_stat_task_run;
	kstat_kv_u64(&pd->pd_enqueue) = s->s_stat_enqueue;
	kstat_kv_u64(&pd->pd_dequeue) = s->s_stat_dequeue;
	kstat_kv_u32(&pd->pd_qdrop) = mq_drops(&s->s_sendq);

	kstat_kv_u32(&pd->pd_defer_len) = s->s_deferred;
	kstat_kv_u64(&pd->pd_defer_add) = s->s_stat_defer_add;
	kstat_kv_u64(&pd->pd_defer_ack) = s->s_stat_defer_ack;
	kstat_kv_u64(&pd->pd_defer_run) = s->s_stat_defer_run;
	kstat_kv_u64(&pd->pd_defer_overlimit) = s->s_stat_defer_overlimit;

	return (0);
}
#endif /* NKSTAT > 0 */

#define PFSYNC_MAX_BULKTRIES	12

struct if_clone	pfsync_cloner =
    IF_CLONE_INITIALIZER("pfsync", pfsync_clone_create, pfsync_clone_destroy);

void
pfsyncattach(int npfsync)
{
	pfsynccounters = counters_alloc(pfsyncs_ncounters);
	if_clone_attach(&pfsync_cloner);
}

static int
pfsync_clone_create(struct if_clone *ifc, int unit)
{
	struct pfsync_softc *sc;
	struct ifnet *ifp;
	size_t i, q;

	if (unit != 0)
		return (ENXIO);

	if (pfsync_deferrals_pool.pr_size == 0) {
		pool_init(&pfsync_deferrals_pool,
		    sizeof(struct pfsync_deferral), 0,
		    IPL_MPFLOOR, 0, "pfdefer", NULL);
		/* pool_cache_init(&pfsync_deferrals_pool); */
	}

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO|M_CANFAIL);
	if (sc == NULL)
		return (ENOMEM);

	/* sc_refs is "owned" by IFF_RUNNING */

	sc->sc_syncpeer.s_addr = INADDR_PFSYNC_GROUP;
	sc->sc_maxupdates = 128;
	sc->sc_defer = 0;

	task_set(&sc->sc_ltask, pfsync_syncif_link, sc);
	task_set(&sc->sc_dtask, pfsync_syncif_detach, sc);

	rw_init(&sc->sc_bulk_req.req_lock, "pfsyncbreq");
	/* need process context to take net lock to call ip_output */
	timeout_set_proc(&sc->sc_bulk_req.req_tmo, pfsync_bulk_req_tmo, sc);

	rw_init(&sc->sc_bulk_snd.snd_lock, "pfsyncbsnd");
	/* need process context to take net lock to call ip_output */
	timeout_set_proc(&sc->sc_bulk_snd.snd_tmo, pfsync_bulk_snd_tmo, sc);

	ifp = &sc->sc_if;
	snprintf(ifp->if_xname, sizeof ifp->if_xname, "%s%d",
	    ifc->ifc_name, unit);
	ifp->if_softc = sc;
	ifp->if_ioctl = pfsync_ioctl;
	ifp->if_output = pfsync_output;
	ifp->if_qstart = pfsync_start;
	ifp->if_type = IFT_PFSYNC;
	ifp->if_hdrlen = sizeof(struct pfsync_header);
	ifp->if_mtu = ETHERMTU;
	ifp->if_xflags = IFXF_CLONED | IFXF_MPSAFE;

	for (i = 0; i < nitems(sc->sc_slices); i++) {
		struct pfsync_slice *s = &sc->sc_slices[i];

		s->s_pfsync = sc;

		mtx_init_flags(&s->s_mtx, IPL_SOFTNET, "pfslice", 0);
		s->s_softnet = net_tq(i);
		timeout_set(&s->s_tmo, pfsync_slice_tmo, s);
		task_set(&s->s_task, pfsync_slice_task, s);

		mq_init(&s->s_sendq, 16, IPL_SOFTNET);
		task_set(&s->s_send, pfsync_slice_sendq, s);

		s->s_len = PFSYNC_MINPKT;
		ml_init(&s->s_ml);

		for (q = 0; q < nitems(s->s_qs); q++)
			TAILQ_INIT(&s->s_qs[q]);
		TAILQ_INIT(&s->s_tdb_q);

		/* stupid NET_LOCK */
		timeout_set(&s->s_deferrals_tmo, pfsync_deferrals_tmo, s);
		task_set(&s->s_deferrals_task, pfsync_deferrals_task, s);
		TAILQ_INIT(&s->s_deferrals);

#if NKSTAT > 0
		s->s_kstat = kstat_create(ifp->if_xname, 0, "pfsync-slice", i,
		    KSTAT_T_KV, 0);

		kstat_set_mutex(s->s_kstat, &s->s_mtx);
		s->s_kstat->ks_softc = s;
		s->s_kstat->ks_datalen = sizeof(pfsync_kstat_tpl);
		s->s_kstat->ks_copy = pfsync_kstat_copy;
		kstat_install(s->s_kstat);
#endif
	}

	if_counters_alloc(ifp);
	if_attach(ifp);
	if_alloc_sadl(ifp);

#if NCARP > 0
	if_addgroup(ifp, "carp");
#endif

#if NBPFILTER > 0
	bpfattach(&sc->sc_if.if_bpf, ifp, DLT_PFSYNC, PFSYNC_HDRLEN);
#endif

	return (0);
}

static int
pfsync_clone_destroy(struct ifnet *ifp)
{
	struct pfsync_softc *sc = ifp->if_softc;
#if NKSTAT > 0
	size_t i;
#endif

	NET_LOCK();
	sc->sc_dead = 1;

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		pfsync_down(sc);
	NET_UNLOCK();

	if_detach(ifp);

#if NKSTAT > 0
	for (i = 0; i < nitems(sc->sc_slices); i++) {
		struct pfsync_slice *s = &sc->sc_slices[i];

		kstat_destroy(s->s_kstat);
	}
#endif

	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static void
pfsync_dprintf(struct pfsync_softc *sc, const char *fmt, ...)
{
	struct ifnet *ifp = &sc->sc_if;
	va_list ap;

	if (!ISSET(ifp->if_flags, IFF_DEBUG))
		return;

	printf("%s: ", ifp->if_xname);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

static void
pfsync_syncif_link(void *arg)
{
	struct pfsync_softc *sc = arg;
	struct ifnet *ifp0;
	unsigned int sync_if_down = 1;

	ifp0 = if_get(sc->sc_sync_ifidx);
	if (ifp0 != NULL && LINK_STATE_IS_UP(ifp0->if_link_state)) {
		pfsync_bulk_req_evt(sc, PFSYNC_BREQ_EVT_LINK);
		sync_if_down = 0;
	}
	if_put(ifp0);

#if NCARP > 0
	if (sc->sc_sync_if_down != sync_if_down) {
		carp_group_demote_adj(&sc->sc_if,
		    sync_if_down ? 1 : -1, "pfsync link");
	}
#endif

	sc->sc_sync_if_down = sync_if_down;
}

static void
pfsync_syncif_detach(void *arg)
{
	struct pfsync_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_if;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		pfsync_down(sc);
		if_down(ifp);
	}

	sc->sc_sync_ifidx = 0;
}

static int
pfsync_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	m_freem(m);	/* drop packet */
	return (EAFNOSUPPORT);
}

static int
pfsync_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct pfsync_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = ENOTTY;

	switch (cmd) {
	case SIOCSIFADDR:
		error = EOPNOTSUPP;
		break;

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (!ISSET(ifp->if_flags, IFF_RUNNING))
				error = pfsync_up(sc);
			else
				error = ENETRESET;
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = pfsync_down(sc);
		}
		break;

	case SIOCSIFMTU:
		error = pfsync_set_mtu(sc, ifr->ifr_mtu);
		break;

	case SIOCSIFPARENT:
		error = pfsync_set_parent(sc, (struct if_parent *)data);
		break;
	case SIOCGIFPARENT:
		error = pfsync_get_parent(sc, (struct if_parent *)data);
		break;
	case SIOCDIFPARENT:
		error = pfsync_del_parent(sc);
		break;

	case SIOCSETPFSYNC:
		error = pfsync_set_ioc(sc, ifr);
		break;
	case SIOCGETPFSYNC:
		error = pfsync_get_ioc(sc, ifr);
		break;

	default:
		break;
	}

	if (error == ENETRESET)
		error = 0;

	return (error);
}

static int
pfsync_set_mtu(struct pfsync_softc *sc, unsigned int mtu)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;
	int error = 0;

	ifp0 = if_get(sc->sc_sync_ifidx);
	if (ifp0 == NULL)
		return (EINVAL);

	if (mtu <= PFSYNC_MINPKT || mtu > ifp0->if_mtu) {
		error = EINVAL;
		goto put;
	}

	/* commit */
	ifp->if_mtu = mtu;

put:
	if_put(ifp0);
	return (error);
}

static int
pfsync_set_parent(struct pfsync_softc *sc, const struct if_parent *p)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;
	int error = 0;

	ifp0 = if_unit(p->ifp_parent);
	if (ifp0 == NULL)
		return (ENXIO);

	if (ifp0->if_index == sc->sc_sync_ifidx)
		goto put;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		error = EBUSY;
		goto put;
	}

	/* commit */
	sc->sc_sync_ifidx = ifp0->if_index;

put:
	if_put(ifp0);
	return (error);
}

static int
pfsync_get_parent(struct pfsync_softc *sc, struct if_parent *p)
{
	struct ifnet *ifp0;
	int error = 0;

	ifp0 = if_get(sc->sc_sync_ifidx);
	if (ifp0 == NULL)
		error = EADDRNOTAVAIL;
	else
		strlcpy(p->ifp_parent, ifp0->if_xname, sizeof(p->ifp_parent));
	if_put(ifp0);

	return (error);
}

static int
pfsync_del_parent(struct pfsync_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;

	if (ISSET(ifp->if_flags, IFF_RUNNING))
		return (EBUSY);

	/* commit */
	sc->sc_sync_ifidx = 0;

	return (0);
}

static int
pfsync_get_ioc(struct pfsync_softc *sc, struct ifreq *ifr)
{
	struct pfsyncreq pfsyncr;
	struct ifnet *ifp0;

	memset(&pfsyncr, 0, sizeof(pfsyncr));

	ifp0 = if_get(sc->sc_sync_ifidx);
	if (ifp0 != NULL) {
		strlcpy(pfsyncr.pfsyncr_syncdev, ifp0->if_xname,
		    sizeof(pfsyncr.pfsyncr_syncdev));
	}
	if_put(ifp0);

	pfsyncr.pfsyncr_syncpeer = sc->sc_syncpeer;
	pfsyncr.pfsyncr_maxupdates = sc->sc_maxupdates;
	pfsyncr.pfsyncr_defer = sc->sc_defer;

	return (copyout(&pfsyncr, ifr->ifr_data, sizeof(pfsyncr)));
}

static int
pfsync_set_ioc(struct pfsync_softc *sc, struct ifreq *ifr)
{
	struct ifnet *ifp = &sc->sc_if;
	struct pfsyncreq pfsyncr;
	unsigned int sync_ifidx = sc->sc_sync_ifidx;
	int wantdown = 0;
	int error;

	error = suser(curproc);
	if (error != 0)
		return (error);

	error = copyin(ifr->ifr_data, &pfsyncr, sizeof(pfsyncr));
	if (error != 0)
		return (error);

	if (pfsyncr.pfsyncr_maxupdates > 255)
		return (EINVAL);

	if (pfsyncr.pfsyncr_syncdev[0] != '\0') { /* set */
		struct ifnet *ifp0 = if_unit(pfsyncr.pfsyncr_syncdev);
		if (ifp0 == NULL)
			return (ENXIO);

		if (ifp0->if_index != sync_ifidx)
			wantdown = 1;

		sync_ifidx = ifp0->if_index;
		if_put(ifp0);
	} else { /* del */
		wantdown = 1;
		sync_ifidx = 0;
	}

	if (pfsyncr.pfsyncr_syncpeer.s_addr == INADDR_ANY)
		pfsyncr.pfsyncr_syncpeer.s_addr = INADDR_PFSYNC_GROUP;
	if (pfsyncr.pfsyncr_syncpeer.s_addr != sc->sc_syncpeer.s_addr)
		wantdown = 1;

	if (wantdown && ISSET(ifp->if_flags, IFF_RUNNING))
		return (EBUSY);

	/* commit */
	sc->sc_sync_ifidx = sync_ifidx;
	sc->sc_syncpeer = pfsyncr.pfsyncr_syncpeer;
	sc->sc_maxupdates = pfsyncr.pfsyncr_maxupdates;
	sc->sc_defer = pfsyncr.pfsyncr_defer;

	return (0);
}

static int
pfsync_up(struct pfsync_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;
	void *inm = NULL;
	int error = 0;
	struct ip *ip;

	NET_ASSERT_LOCKED();
	KASSERT(!ISSET(ifp->if_flags, IFF_RUNNING));

	if (sc->sc_dead)
		return (ENXIO);

	/*
	 * coordinate with pfsync_down(). if sc_up is still up and
	 * we're here then something else is tearing pfsync down.
	 */
	if (sc->sc_up)
		return (EBUSY);

	if (sc->sc_syncpeer.s_addr == INADDR_ANY ||
	    sc->sc_syncpeer.s_addr == INADDR_BROADCAST)
		return (EDESTADDRREQ);

	ifp0 = if_get(sc->sc_sync_ifidx);
	if (ifp0 == NULL)
		return (ENXIO);

	if (IN_MULTICAST(sc->sc_syncpeer.s_addr)) {
		if (!ISSET(ifp0->if_flags, IFF_MULTICAST)) {
			error = ENODEV;
			goto put;
		}
		inm = in_addmulti(&sc->sc_syncpeer, ifp0);
		if (inm == NULL) {
			error = ECONNABORTED;
			goto put;
		}
	}

	sc->sc_up = 1;

	ip = &sc->sc_template;
	memset(ip, 0, sizeof(*ip));
	ip->ip_v = IPVERSION;
	ip->ip_hl = sizeof(*ip) >> 2;
	ip->ip_tos = IPTOS_LOWDELAY;
	/* len and id are set later */
	ip->ip_off = htons(IP_DF);
	ip->ip_ttl = PFSYNC_DFLTTL;
	ip->ip_p = IPPROTO_PFSYNC;
	ip->ip_src.s_addr = INADDR_ANY;
	ip->ip_dst.s_addr = sc->sc_syncpeer.s_addr;

	/* commit */
	refcnt_init(&sc->sc_refs); /* IFF_RUNNING kind of owns this */

#if NCARP > 0
	sc->sc_sync_if_down = 1;
	carp_group_demote_adj(&sc->sc_if, 1, "pfsync up");
#endif

	if_linkstatehook_add(ifp0, &sc->sc_ltask);
	if_detachhook_add(ifp0, &sc->sc_dtask);

	sc->sc_inm = inm;
	SET(ifp->if_flags, IFF_RUNNING);

	pfsync_bulk_req_evt(sc, PFSYNC_BREQ_EVT_UP);

	refcnt_take(&sc->sc_refs); /* give one to SMR */
	SMR_PTR_SET_LOCKED(&pfsyncif, sc);

	pfsync_syncif_link(sc); /* try and push the bulk req state forward */

put:
	if_put(ifp0);
	return (error);
}

static struct mbuf *
pfsync_encap(struct pfsync_softc *sc, struct mbuf *m)
{
	struct {
		struct ip		ip;
		struct pfsync_header	ph;
	} __packed __aligned(4) *h;
	unsigned int mlen = m->m_pkthdr.len;

	m = m_prepend(m, sizeof(*h), M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	h = mtod(m, void *);
	memset(h, 0, sizeof(*h));

	mlen += sizeof(h->ph);
	h->ph.version = PFSYNC_VERSION;
	h->ph.len = htons(mlen);
	/* h->ph.pfcksum */

	mlen += sizeof(h->ip);
	h->ip = sc->sc_template;
	h->ip.ip_len = htons(mlen);
	h->ip.ip_id = htons(ip_randomid());

	return (m);
}

static void
pfsync_bulk_req_send(struct pfsync_softc *sc)
{
	struct {
		struct pfsync_subheader	subh;
		struct pfsync_upd_req	ur;
	} __packed __aligned(4) *h;
	unsigned mlen = max_linkhdr +
	    sizeof(struct ip) + sizeof(struct pfsync_header) + sizeof(*h);
	struct mbuf *m;

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL)
		goto fail;

	if (mlen > MHLEN) {
		MCLGETL(m, M_DONTWAIT, mlen);
		if (!ISSET(m->m_flags, M_EXT))
			goto drop;
	}

	m_align(m, sizeof(*h));
	m->m_len = m->m_pkthdr.len = sizeof(*h);

	h = mtod(m, void *);
	memset(h, 0, sizeof(*h));

	h->subh.action = PFSYNC_ACT_UPD_REQ;
	h->subh.len = sizeof(h->ur) >> 2;
	h->subh.count = htons(1);

	h->ur.id = htobe64(0);
	h->ur.creatorid = htobe32(0);

	m = pfsync_encap(sc, m);
	if (m == NULL)
		goto fail;

	pfsync_sendout(sc, m);
	return;

drop:
	m_freem(m);
fail:
	printf("%s: unable to request bulk update\n", sc->sc_if.if_xname);
}

static void
pfsync_bulk_req_nstate(struct pfsync_softc *sc,
    enum pfsync_bulk_req_state nstate, int seconds)
{
	sc->sc_bulk_req.req_state = nstate;
	if (seconds > 0)
		timeout_add_sec(&sc->sc_bulk_req.req_tmo, seconds);
	else
		timeout_del(&sc->sc_bulk_req.req_tmo);
}

static void
pfsync_bulk_req_invstate(struct pfsync_softc *sc,
    enum pfsync_bulk_req_event evt)
{
	panic("%s: unexpected event %s in state %s", sc->sc_if.if_xname,
	    pfsync_bulk_req_event_names[evt],
	    pfsync_bulk_req_state_names[sc->sc_bulk_req.req_state]);
}

static void
pfsync_bulk_req_nstate_bulk(struct pfsync_softc *sc)
{
	/* calculate the number of packets we expect */
	int t = pf_pool_limits[PF_LIMIT_STATES].limit /
	    ((sc->sc_if.if_mtu - PFSYNC_MINPKT) /
	     sizeof(struct pfsync_state));

	/* turn it into seconds */
	t /= 1000 / PFSYNC_BULK_SND_IVAL_MS;

	if (t == 0)
		t = 1;

	pfsync_bulk_req_nstate(sc, PFSYNC_BREQ_S_BULK, t * 4);
}

static inline void
pfsync_bulk_req_nstate_done(struct pfsync_softc *sc)
{
	pfsync_bulk_req_nstate(sc, PFSYNC_BREQ_S_DONE, 0);

	KASSERT(sc->sc_bulk_req.req_demoted == 1);
	sc->sc_bulk_req.req_demoted = 0;

#if NCARP > 0
	carp_group_demote_adj(&sc->sc_if, -32, "pfsync done");
#endif
}

static void
pfsync_bulk_req_evt(struct pfsync_softc *sc, enum pfsync_bulk_req_event evt)
{
	struct ifnet *ifp = &sc->sc_if;

	rw_enter_write(&sc->sc_bulk_req.req_lock);
	pfsync_dprintf(sc, "%s state %s evt %s", __func__,
	    pfsync_bulk_req_state_names[sc->sc_bulk_req.req_state],
	    pfsync_bulk_req_event_names[evt]);

	if (evt == PFSYNC_BREQ_EVT_DOWN) {
		/* unconditionally move down */
		sc->sc_bulk_req.req_tries = 0;
		pfsync_bulk_req_nstate(sc, PFSYNC_BREQ_S_NONE, 0);

		if (sc->sc_bulk_req.req_demoted) {
			sc->sc_bulk_req.req_demoted = 0;
#if NCARP > 0
			carp_group_demote_adj(&sc->sc_if, -32,
			    "pfsync down");
#endif
		}
	} else switch (sc->sc_bulk_req.req_state) {
	case PFSYNC_BREQ_S_NONE:
		switch (evt) {
		case PFSYNC_BREQ_EVT_UP:
			KASSERT(sc->sc_bulk_req.req_demoted == 0);
			sc->sc_bulk_req.req_demoted = 1;
#if NCARP > 0
			carp_group_demote_adj(&sc->sc_if, 32,
			    "pfsync start");
#endif
			pfsync_bulk_req_nstate(sc, PFSYNC_BREQ_S_START, 30);
			break;
		default:
			pfsync_bulk_req_invstate(sc, evt);
		}

		break;

	case PFSYNC_BREQ_S_START:
		switch (evt) {
		case PFSYNC_BREQ_EVT_LINK:
			pfsync_bulk_req_send(sc);
			pfsync_bulk_req_nstate(sc, PFSYNC_BREQ_S_SENT, 2);
			break;
		case PFSYNC_BREQ_EVT_TMO:
			pfsync_dprintf(sc, "timeout waiting for link");
			pfsync_bulk_req_nstate_done(sc);
			break;
		case PFSYNC_BREQ_EVT_BUS_START:
			pfsync_bulk_req_nstate_bulk(sc);
			break;
		case PFSYNC_BREQ_EVT_BUS_END:
			/* ignore this */
			break;
		default:
			pfsync_bulk_req_invstate(sc, evt);
		}
		break;

	case PFSYNC_BREQ_S_SENT:
		switch (evt) {
		case PFSYNC_BREQ_EVT_BUS_START:
			pfsync_bulk_req_nstate_bulk(sc);
			break;
		case PFSYNC_BREQ_EVT_BUS_END:
		case PFSYNC_BREQ_EVT_LINK:
			/* ignore this */
			break;
		case PFSYNC_BREQ_EVT_TMO:
			if (++sc->sc_bulk_req.req_tries <
			    PFSYNC_MAX_BULKTRIES) {
				pfsync_bulk_req_send(sc);
				pfsync_bulk_req_nstate(sc,
				    PFSYNC_BREQ_S_SENT, 2);
				break;
			}

			pfsync_dprintf(sc,
			    "timeout waiting for bulk transfer start");
			pfsync_bulk_req_nstate_done(sc);
			break;
		default:
			pfsync_bulk_req_invstate(sc, evt);
		}
		break;

	case PFSYNC_BREQ_S_BULK:
		switch (evt) {
		case PFSYNC_BREQ_EVT_BUS_START:
		case PFSYNC_BREQ_EVT_LINK:
			/* ignore this */
			break;
		case PFSYNC_BREQ_EVT_BUS_END:
			pfsync_bulk_req_nstate_done(sc);
			break;
		case PFSYNC_BREQ_EVT_TMO:
			if (++sc->sc_bulk_req.req_tries <
			    PFSYNC_MAX_BULKTRIES) {
				pfsync_bulk_req_send(sc);
				pfsync_bulk_req_nstate(sc,
				    PFSYNC_BREQ_S_SENT, 2);
			}

			pfsync_dprintf(sc,
			    "timeout waiting for bulk transfer end");
			pfsync_bulk_req_nstate_done(sc);
			break;
		default:
			pfsync_bulk_req_invstate(sc, evt);
		}
		break;

	case PFSYNC_BREQ_S_DONE: /* pfsync is up and running */
		switch (evt) {
		case PFSYNC_BREQ_EVT_BUS_START:
		case PFSYNC_BREQ_EVT_BUS_END:
		case PFSYNC_BREQ_EVT_LINK:
			/* nops */
			break;
		default:
			pfsync_bulk_req_invstate(sc, evt);
		}
		break;

	default:
		panic("%s: unknown event %d", ifp->if_xname, evt);
		/* NOTREACHED */
	}
	rw_exit_write(&sc->sc_bulk_req.req_lock);
}

static void
pfsync_bulk_req_tmo(void *arg)
{
	struct pfsync_softc *sc = arg;

	NET_LOCK();
	pfsync_bulk_req_evt(sc, PFSYNC_BREQ_EVT_TMO);
	NET_UNLOCK();
}

static int
pfsync_down(struct pfsync_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;
	struct ifnet *ifp0;
	struct smr_entry smr;
	size_t i;
	void *inm = NULL;
	unsigned int sndbar = 0;
	struct pfsync_deferrals pds = TAILQ_HEAD_INITIALIZER(pds);
	struct pfsync_deferral *pd;

	NET_ASSERT_LOCKED();
	KASSERT(ISSET(ifp->if_flags, IFF_RUNNING));

	/*
	 * tearing down pfsync involves waiting for pfsync to stop
	 * running in various contexts including softnet taskqs.
	 * this thread cannot hold netlock while waiting for a
	 * barrier in softnet because softnet might be waiting for
	 * the netlock. sc->sc_up is used to coordinate with
	 * pfsync_up.
	 */

	CLR(ifp->if_flags, IFF_RUNNING);

	ifp0 = if_get(sc->sc_sync_ifidx);
	if (ifp0 != NULL) {
		if_linkstatehook_del(ifp0, &sc->sc_ltask);
		if_detachhook_del(ifp0, &sc->sc_dtask);
	}
	if_put(ifp0);

#if NCARP > 0
	if (sc->sc_sync_if_down)
		carp_group_demote_adj(&sc->sc_if, -1, "pfsync down");
#endif

	NET_UNLOCK();

	KASSERTMSG(SMR_PTR_GET_LOCKED(&pfsyncif) == sc,
	   "pfsyncif %p != sc %p", pfsyncif, sc);
	SMR_PTR_SET_LOCKED(&pfsyncif, NULL);
	smr_init(&smr);
	smr_call(&smr, (void (*)(void *))refcnt_rele_wake, &sc->sc_refs);

	/* stop pf producing work before cleaning up the timeouts and tasks */
	refcnt_finalize(&sc->sc_refs, "pfsyncfini");

	pfsync_bulk_req_evt(sc, PFSYNC_BREQ_EVT_DOWN);

	rw_enter_read(&pf_state_list.pfs_rwl);
	rw_enter_write(&sc->sc_bulk_snd.snd_lock);
	if (sc->sc_bulk_snd.snd_tail != NULL) {
		sndbar = !timeout_del(&sc->sc_bulk_snd.snd_tmo);

		sc->sc_bulk_snd.snd_again = 0;
		sc->sc_bulk_snd.snd_next = NULL;
		sc->sc_bulk_snd.snd_tail = NULL;
	}
	rw_exit_write(&sc->sc_bulk_snd.snd_lock);
	rw_exit_read(&pf_state_list.pfs_rwl);

	/*
	 * do a single barrier for all the timeouts. because the
	 * timeouts in each slice are configured the same way, the
	 * barrier for one will work for all of them.
	 */
	for (i = 0; i < nitems(sc->sc_slices); i++) {
		struct pfsync_slice *s = &sc->sc_slices[i];

		timeout_del(&s->s_tmo);
		task_del(s->s_softnet, &s->s_task);
		task_del(s->s_softnet, &s->s_send);

		timeout_del(&s->s_deferrals_tmo);
		task_del(s->s_softnet, &s->s_deferrals_task);
	}
	timeout_barrier(&sc->sc_slices[0].s_tmo);
	timeout_barrier(&sc->sc_bulk_req.req_tmo); /* XXX proc */
	if (sndbar) {
		/* technically the preceding barrier does the same job */
		timeout_barrier(&sc->sc_bulk_snd.snd_tmo);
	}
	net_tq_barriers("pfsyncbar");

	/* pfsync is no longer running */

	if (sc->sc_inm != NULL) {
		inm = sc->sc_inm;
		sc->sc_inm = NULL;
	}

	for (i = 0; i < nitems(sc->sc_slices); i++) {
		struct pfsync_slice *s = &sc->sc_slices[i];
		struct pf_state *st;

		pfsync_slice_drop(sc, s);
		mq_purge(&s->s_sendq);

		while ((pd = TAILQ_FIRST(&s->s_deferrals)) != NULL) {
			TAILQ_REMOVE(&s->s_deferrals, pd, pd_entry);

			st = pd->pd_st;
			st->sync_defer = NULL;

			TAILQ_INSERT_TAIL(&pds, pd, pd_entry);
		}
		s->s_deferred = 0;
	}

	NET_LOCK();
	sc->sc_up = 0;

	if (inm != NULL)
		in_delmulti(inm);

	while ((pd = TAILQ_FIRST(&pds)) != NULL) {
		TAILQ_REMOVE(&pds, pd, pd_entry);

		pfsync_defer_output(pd);
	}

	return (0);
}

int
pfsync_is_up(void)
{
	int rv;

	smr_read_enter();
	rv = SMR_PTR_GET(&pfsyncif) != NULL;
	smr_read_leave();

	return (rv);
}

static void
pfsync_start(struct ifqueue *ifq)
{
	ifq_purge(ifq);
}

struct pfsync_q {
	void		(*write)(struct pf_state *, void *);
	size_t		len;
	u_int8_t	action;
};

static struct pfsync_slice *
pfsync_slice_enter(struct pfsync_softc *sc, const struct pf_state *st)
{
	unsigned int idx = st->key[0]->hash % nitems(sc->sc_slices); 
	struct pfsync_slice *s = &sc->sc_slices[idx];

	if (!mtx_enter_try(&s->s_mtx)) {
		mtx_enter(&s->s_mtx);
		s->s_stat_contended++;
	}
	s->s_stat_locks++;

	return (s);
}

static void
pfsync_slice_leave(struct pfsync_softc *sc, struct pfsync_slice *s)
{
	mtx_leave(&s->s_mtx);
}

/* we have one of these for every PFSYNC_S_ */
static void	pfsync_out_state(struct pf_state *, void *);
static void	pfsync_out_iack(struct pf_state *, void *);
static void	pfsync_out_upd_c(struct pf_state *, void *);
static void	pfsync_out_del(struct pf_state *, void *);
#if defined(IPSEC)
static void	pfsync_out_tdb(struct tdb *, void *);
#endif

static const struct pfsync_q pfsync_qs[] = {
	{ pfsync_out_iack,  sizeof(struct pfsync_ins_ack), PFSYNC_ACT_INS_ACK },
	{ pfsync_out_upd_c, sizeof(struct pfsync_upd_c),   PFSYNC_ACT_UPD_C },
	{ pfsync_out_del,   sizeof(struct pfsync_del_c),   PFSYNC_ACT_DEL_C },
	{ pfsync_out_state, sizeof(struct pfsync_state),   PFSYNC_ACT_INS },
	{ pfsync_out_state, sizeof(struct pfsync_state),   PFSYNC_ACT_UPD }
};

static void
pfsync_out_state(struct pf_state *st, void *buf)
{
	struct pfsync_state *sp = buf;

	mtx_enter(&st->mtx);
	pf_state_export(sp, st);
	mtx_leave(&st->mtx);
}

static void
pfsync_out_iack(struct pf_state *st, void *buf)
{
	struct pfsync_ins_ack *iack = buf;

	iack->id = st->id;
	iack->creatorid = st->creatorid;
}

static void
pfsync_out_upd_c(struct pf_state *st, void *buf)
{
	struct pfsync_upd_c *up = buf;

	memset(up, 0, sizeof(*up));
	up->id = st->id;
	up->creatorid = st->creatorid;

	mtx_enter(&st->mtx);
	pf_state_peer_hton(&st->src, &up->src);
	pf_state_peer_hton(&st->dst, &up->dst);
	up->timeout = st->timeout;
	mtx_leave(&st->mtx);
}

static void
pfsync_out_del(struct pf_state *st, void *buf)
{
	struct pfsync_del_c *dp = buf;

	dp->id = st->id;
	dp->creatorid = st->creatorid;

	st->sync_state = PFSYNC_S_DEAD;
}

#if defined(IPSEC)
static inline void
pfsync_tdb_enter(struct tdb *tdb)
{
	mtx_enter(&tdb->tdb_mtx);
}

static inline void
pfsync_tdb_leave(struct tdb *tdb)
{
	unsigned int snapped = ISSET(tdb->tdb_flags, TDBF_PFSYNC_SNAPPED);
	mtx_leave(&tdb->tdb_mtx);
	if (snapped)
		wakeup_one(&tdb->tdb_updates);
}
#endif /* defined(IPSEC) */

static void
pfsync_slice_drop(struct pfsync_softc *sc, struct pfsync_slice *s)
{
	struct pf_state *st;
	int q;
#if defined(IPSEC)
	struct tdb *tdb;
#endif

	for (q = 0; q < nitems(s->s_qs); q++) {
		if (TAILQ_EMPTY(&s->s_qs[q]))
			continue;

		while ((st = TAILQ_FIRST(&s->s_qs[q])) != NULL) {
			TAILQ_REMOVE(&s->s_qs[q], st, sync_list);
#ifdef PFSYNC_DEBUG
			KASSERT(st->sync_state == q);
#endif
			st->sync_state = PFSYNC_S_NONE;
			pf_state_unref(st);
		}
	}

#if defined(IPSEC)
	while ((tdb = TAILQ_FIRST(&s->s_tdb_q)) != NULL) {
		TAILQ_REMOVE(&s->s_tdb_q, tdb, tdb_sync_entry);

		pfsync_tdb_enter(tdb);
		KASSERT(ISSET(tdb->tdb_flags, TDBF_PFSYNC));
		CLR(tdb->tdb_flags, TDBF_PFSYNC);
		pfsync_tdb_leave(tdb);
	}
#endif /* defined(IPSEC) */

	timeout_del(&s->s_tmo);
	s->s_len = PFSYNC_MINPKT;
}

static struct mbuf *
pfsync_slice_write(struct pfsync_slice *s)
{
	struct pfsync_softc *sc = s->s_pfsync;
	struct mbuf *m;

	struct ip *ip;
	struct pfsync_header *ph;
	struct pfsync_subheader *subh;

	unsigned int mlen = max_linkhdr + s->s_len;
	unsigned int q, count;
	caddr_t ptr;
	size_t off;

	MUTEX_ASSERT_LOCKED(&s->s_mtx);
	if (s->s_len == PFSYNC_MINPKT) {
		s->s_stat_write_nop++;
		return (NULL);
	}

	task_del(s->s_softnet, &s->s_task);

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL)
		goto drop;

	if (mlen > MHLEN) {
		MCLGETL(m, M_DONTWAIT, mlen);
		if (!ISSET(m->m_flags, M_EXT))
			goto drop;
	}

	m_align(m, s->s_len);
	m->m_len = m->m_pkthdr.len = s->s_len;

	ptr = mtod(m, caddr_t);
	off = 0;

	ip = (struct ip *)(ptr + off);
	off += sizeof(*ip);
	*ip = sc->sc_template;
	ip->ip_len = htons(m->m_pkthdr.len);
	ip->ip_id = htons(ip_randomid());

	ph = (struct pfsync_header *)(ptr + off);
	off += sizeof(*ph);
	memset(ph, 0, sizeof(*ph));
	ph->version = PFSYNC_VERSION;
	ph->len = htons(m->m_pkthdr.len - sizeof(*ip));

	for (q = 0; q < nitems(s->s_qs); q++) {
		struct pf_state_queue *psq = &s->s_qs[q];
		struct pf_state *st;

		if (TAILQ_EMPTY(psq))
			continue;

		subh = (struct pfsync_subheader *)(ptr + off);
		off += sizeof(*subh);

		count = 0;
		while ((st = TAILQ_FIRST(psq)) != NULL) {
			TAILQ_REMOVE(psq, st, sync_list);
			count++;

			KASSERT(st->sync_state == q);
			/* the write handler below may override this */
			st->sync_state = PFSYNC_S_NONE;

			pfsync_qs[q].write(st, ptr + off);
			off += pfsync_qs[q].len;

			pf_state_unref(st);
		}

		subh->action = pfsync_qs[q].action;
		subh->len = pfsync_qs[q].len >> 2;
		subh->count = htons(count);
	}

#if defined(IPSEC)
	if (!TAILQ_EMPTY(&s->s_tdb_q)) {
		struct tdb *tdb;

		subh = (struct pfsync_subheader *)(ptr + off);
		off += sizeof(*subh);

		count = 0;
		while ((tdb = TAILQ_FIRST(&s->s_tdb_q)) != NULL) {
			TAILQ_REMOVE(&s->s_tdb_q, tdb, tdb_sync_entry);
			count++;

			pfsync_tdb_enter(tdb);
			KASSERT(ISSET(tdb->tdb_flags, TDBF_PFSYNC));

			/* get a consistent view of the counters */
			pfsync_out_tdb(tdb, ptr + off);

			CLR(tdb->tdb_flags, TDBF_PFSYNC);
			pfsync_tdb_leave(tdb);

			off += sizeof(struct pfsync_tdb);
		}

		subh->action = PFSYNC_ACT_TDB;
		subh->len = sizeof(struct pfsync_tdb) >> 2;
		subh->count = htons(count);
	}
#endif

	timeout_del(&s->s_tmo);
	s->s_len = PFSYNC_MINPKT;

	return (m);
drop:
	m_freem(m);
	pfsyncstat_inc(pfsyncs_onomem);
	pfsync_slice_drop(sc, s);
	return (NULL);
}

static void
pfsync_sendout(struct pfsync_softc *sc, struct mbuf *m)
{
	struct ip_moptions imo;
	unsigned int len = m->m_pkthdr.len;
#if NBPFILTER > 0
	caddr_t if_bpf = sc->sc_if.if_bpf;
	if (if_bpf)
		bpf_mtap(if_bpf, m, BPF_DIRECTION_OUT);
#endif

	imo.imo_ifidx = sc->sc_sync_ifidx;
	imo.imo_ttl = PFSYNC_DFLTTL;
	imo.imo_loop = 0;
	m->m_pkthdr.ph_rtableid = sc->sc_if.if_rdomain;

	if (ip_output(m, NULL, NULL, IP_RAWOUTPUT, &imo, NULL, 0) == 0) {
		counters_pkt(sc->sc_if.if_counters, ifc_opackets,
		    ifc_obytes, len);
		pfsyncstat_inc(pfsyncs_opackets);
	} else {
		counters_inc(sc->sc_if.if_counters, ifc_oerrors);
		pfsyncstat_inc(pfsyncs_oerrors);
	}
}

static void
pfsync_slice_tmo(void *arg)
{
	struct pfsync_slice *s = arg;

	task_add(s->s_softnet, &s->s_task);
}

static void
pfsync_slice_sched(struct pfsync_slice *s)
{
	s->s_stat_task_add++;
	task_add(s->s_softnet, &s->s_task);
}

static void
pfsync_slice_task(void *arg)
{
	struct pfsync_slice *s = arg;
	struct mbuf *m;

	mtx_enter(&s->s_mtx);
	s->s_stat_task_run++;

	m = pfsync_slice_write(s);
	mtx_leave(&s->s_mtx);
	if (m != NULL) {
		NET_LOCK();
		pfsync_sendout(s->s_pfsync, m);
		NET_UNLOCK();
	}
}

static void
pfsync_slice_sendq(void *arg)
{
	struct pfsync_slice *s = arg;
	struct mbuf_list ml;
	struct mbuf *m;

	mq_delist(&s->s_sendq, &ml);
	if (ml_empty(&ml))
		return;

	mtx_enter(&s->s_mtx);
	s->s_stat_dequeue++;
	mtx_leave(&s->s_mtx);

	NET_LOCK();
	while ((m = ml_dequeue(&ml)) != NULL)
		pfsync_sendout(s->s_pfsync, m);
	NET_UNLOCK();
}

static void
pfsync_q_ins(struct pfsync_slice *s, struct pf_state *st, unsigned int q)
{
	size_t nlen = pfsync_qs[q].len;
	struct mbuf *m = NULL;

	MUTEX_ASSERT_LOCKED(&s->s_mtx);
	KASSERT(st->sync_state == PFSYNC_S_NONE);
	KASSERT(s->s_len >= PFSYNC_MINPKT);

	if (TAILQ_EMPTY(&s->s_qs[q]))
		nlen += sizeof(struct pfsync_subheader);

	if (s->s_len + nlen > s->s_pfsync->sc_if.if_mtu) {
		m = pfsync_slice_write(s);
		if (m != NULL) {
			s->s_stat_enqueue++;
			if (mq_enqueue(&s->s_sendq, m) == 0)
				task_add(s->s_softnet, &s->s_send);
		}

		nlen = sizeof(struct pfsync_subheader) + pfsync_qs[q].len;
	}

	s->s_len += nlen;
	pf_state_ref(st);
	TAILQ_INSERT_TAIL(&s->s_qs[q], st, sync_list);
	st->sync_state = q;

	if (!timeout_pending(&s->s_tmo))
		timeout_add_sec(&s->s_tmo, 1);
}

static void
pfsync_q_del(struct pfsync_slice *s, struct pf_state *st)
{
	unsigned int q = st->sync_state;

	MUTEX_ASSERT_LOCKED(&s->s_mtx);
	KASSERT(st->sync_state < PFSYNC_S_NONE);

	st->sync_state = PFSYNC_S_NONE;
	TAILQ_REMOVE(&s->s_qs[q], st, sync_list);
	pf_state_unref(st);
	s->s_len -= pfsync_qs[q].len;

	if (TAILQ_EMPTY(&s->s_qs[q]))
		s->s_len -= sizeof(struct pfsync_subheader);
}

/*
 * the pfsync hooks that pf calls
 */

void
pfsync_init_state(struct pf_state *st, const struct pf_state_key *skw,
    const struct pf_state_key *sks, int flags)
{
	/* this is called before pf_state_insert */

	if (skw->proto == IPPROTO_PFSYNC)
		SET(st->state_flags, PFSTATE_NOSYNC);

	if (ISSET(st->state_flags, PFSTATE_NOSYNC)) {
		st->sync_state = PFSYNC_S_DEAD;
		return;
	}

	if (ISSET(flags, PFSYNC_SI_IOCTL)) {
		/* all good */
		return;
	}

	/* state came off the wire */
	if (ISSET(flags, PFSYNC_SI_PFSYNC)) {
		if (ISSET(st->state_flags, PFSTATE_ACK)) {
			CLR(st->state_flags, PFSTATE_ACK);

			/* peer wants an iack, not an insert */
			st->sync_state = PFSYNC_S_SYNC;
		} else
			st->sync_state = PFSYNC_S_PFSYNC;
	}
}

void
pfsync_insert_state(struct pf_state *st)
{
	struct pfsync_softc *sc;

	MUTEX_ASSERT_UNLOCKED(&st->mtx);

	if (ISSET(st->state_flags, PFSTATE_NOSYNC) ||
	    st->sync_state == PFSYNC_S_DEAD)
		return;

	smr_read_enter();
	sc = SMR_PTR_GET(&pfsyncif);
	if (sc != NULL) {
		struct pfsync_slice *s = pfsync_slice_enter(sc, st);

		switch (st->sync_state) {
		case PFSYNC_S_UPD_C:
			/* we must have lost a race after insert */
			pfsync_q_del(s, st);
			/* FALLTHROUGH */
		case PFSYNC_S_NONE:
			pfsync_q_ins(s, st, PFSYNC_S_INS);
			break;
		case PFSYNC_S_SYNC:
			st->sync_state = PFSYNC_S_NONE; /* gross */
			pfsync_q_ins(s, st, PFSYNC_S_IACK);
			pfsync_slice_sched(s); /* the peer is waiting */
			break;
		case PFSYNC_S_PFSYNC:
			/* state was just inserted by pfsync */
			st->sync_state = PFSYNC_S_NONE;
			break;
		default:
			panic("%s: state %p unexpected sync_state %d",
			    __func__, st, st->sync_state);
			/* NOTREACHED */
		}

		pfsync_slice_leave(sc, s);
	}
	smr_read_leave();
}

void
pfsync_update_state(struct pf_state *st)
{
	struct pfsync_softc *sc;

	MUTEX_ASSERT_UNLOCKED(&st->mtx);

	if (ISSET(st->state_flags, PFSTATE_NOSYNC) ||
	    st->sync_state == PFSYNC_S_DEAD)
		return;

	smr_read_enter();
	sc = SMR_PTR_GET(&pfsyncif);
	if (sc != NULL) {
		struct pfsync_slice *s = pfsync_slice_enter(sc, st);
		int sync = 0;

		switch (st->sync_state) {
		case PFSYNC_S_UPD_C:
		case PFSYNC_S_UPD:
			/* we're already handling it */
			if (st->key[PF_SK_WIRE]->proto == IPPROTO_TCP) {
				st->sync_updates++;
				if (st->sync_updates >= sc->sc_maxupdates)
					sync = 1;
			}
			/* FALLTHROUGH */
		case PFSYNC_S_INS:
		case PFSYNC_S_DEL:
		case PFSYNC_S_DEAD:
			break;

		case PFSYNC_S_IACK:
			pfsync_q_del(s, st);
			/* FALLTHROUGH */
		case PFSYNC_S_NONE:
			pfsync_q_ins(s, st, PFSYNC_S_UPD_C);
			st->sync_updates = 0;
			break;
		default:
			panic("%s: state %p unexpected sync_state %d",
			    __func__, st, st->sync_state);
			/* NOTREACHED */
		}

		if (!sync && (getuptime() - st->pfsync_time) < 2)
			sync = 1;

		if (sync)
			pfsync_slice_sched(s);
		pfsync_slice_leave(sc, s);
	}
	smr_read_leave();
}

void
pfsync_delete_state(struct pf_state *st)
{
	struct pfsync_softc *sc;

	MUTEX_ASSERT_UNLOCKED(&st->mtx);

	if (ISSET(st->state_flags, PFSTATE_NOSYNC) ||
	    st->sync_state == PFSYNC_S_DEAD)
		return;

	smr_read_enter();
	sc = SMR_PTR_GET(&pfsyncif);
	if (sc != NULL) {
		struct pfsync_slice *s = pfsync_slice_enter(sc, st);

		switch (st->sync_state) {
		case PFSYNC_S_INS:
			/* let's pretend this never happened */
			pfsync_q_del(s, st);
			break;

		case PFSYNC_S_UPD_C:
		case PFSYNC_S_UPD:
		case PFSYNC_S_IACK:
			pfsync_q_del(s, st);
			/* FALLTHROUGH */
		case PFSYNC_S_NONE:
			pfsync_q_ins(s, st, PFSYNC_S_DEL);
			st->sync_updates = 0;
			break;
		case PFSYNC_S_DEL:
		case PFSYNC_S_DEAD:
			/* XXX we should count this */
			break;
		default:
			panic("%s: state %p unexpected sync_state %d",
			    __func__, st, st->sync_state);
			/* NOTREACHED */
		}

		pfsync_slice_leave(sc, s);
	}
	smr_read_leave();
}

struct pfsync_subh_clr {
	struct pfsync_subheader	subh;
	struct pfsync_clr	clr;
} __packed __aligned(4);

void
pfsync_clear_states(u_int32_t creatorid, const char *ifname)
{
	struct pfsync_softc *sc;
	struct pfsync_subh_clr *h;
	struct mbuf *m;
	unsigned int hlen, mlen;

	smr_read_enter();
	sc = SMR_PTR_GET(&pfsyncif);
	if (sc != NULL)
		refcnt_take(&sc->sc_refs);
	smr_read_leave();

	if (sc == NULL)
		return;

	hlen = sizeof(sc->sc_template) +
	    sizeof(struct pfsync_header) +
	    sizeof(*h);

	mlen = max_linkhdr + hlen;

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL) {
		/* count error */
		goto leave;
	}

	if (mlen > MHLEN) {
		MCLGETL(m, M_DONTWAIT, mlen);
		if (!ISSET(m->m_flags, M_EXT)) {
			m_freem(m);
			goto leave;
		}
	}

	m_align(m, sizeof(*h));
	h = mtod(m, struct pfsync_subh_clr *);

	h->subh.action = PFSYNC_ACT_CLR;
	h->subh.len = sizeof(h->clr) >> 2;
	h->subh.count = htons(1);

	strlcpy(h->clr.ifname, ifname, sizeof(h->clr.ifname));
	h->clr.creatorid = creatorid;

	m->m_pkthdr.len = m->m_len = sizeof(*h);
	m = pfsync_encap(sc, m);
	if (m == NULL)
		goto leave;

	pfsync_sendout(sc, m);
leave:
	refcnt_rele_wake(&sc->sc_refs);
}

int
pfsync_state_in_use(struct pf_state *st)
{
	struct pfsync_softc *sc;
	int rv = 0;

	smr_read_enter();
	sc = SMR_PTR_GET(&pfsyncif);
	if (sc != NULL) {
		/*
		 * pfsync bulk sends run inside
		 * rw_enter_read(&pf_state_list.pfs_rwl), and this
		 * code (pfsync_state_in_use) is only called from the
		 * purge code inside
		 * rw_enter_write(&pf_state_list.pfs_rwl). therefore,
		 * those two sections are exclusive so we can safely
		 * look at the bulk send pointers.
		 */
		/* rw_assert_wrlock(&pf_state_list.pfs_rwl); */
		if (sc->sc_bulk_snd.snd_next == st ||
		    sc->sc_bulk_snd.snd_tail == st)
			rv = 1;
	}
	smr_read_leave();

	return (rv);
}

int
pfsync_defer(struct pf_state *st, struct mbuf *m)
{
	struct pfsync_softc *sc;
	struct pfsync_slice *s;
	struct pfsync_deferral *pd;
	int sched = 0;
	int rv = 0;

	if (ISSET(st->state_flags, PFSTATE_NOSYNC) ||
	    ISSET(m->m_flags, M_BCAST|M_MCAST))
		return (0);

	smr_read_enter();
	sc = SMR_PTR_GET(&pfsyncif);
	if (sc == NULL || !sc->sc_defer)
		goto leave;

	pd = pool_get(&pfsync_deferrals_pool, M_NOWAIT);
	if (pd == NULL) {
		goto leave;
	}

	s = pfsync_slice_enter(sc, st);
	s->s_stat_defer_add++;

	pd->pd_st = pf_state_ref(st);
	pd->pd_m = m;
	pd->pd_deadline = getnsecuptime() + PFSYNC_DEFER_NSEC;

	m->m_pkthdr.pf.flags |= PF_TAG_GENERATED;
	st->sync_defer = pd;

	sched = s->s_deferred++;
	TAILQ_INSERT_TAIL(&s->s_deferrals, pd, pd_entry);

	if (sched == 0)
		timeout_add_nsec(&s->s_deferrals_tmo, PFSYNC_DEFER_NSEC);
	else if (sched >= PFSYNC_DEFER_LIMIT) {
		s->s_stat_defer_overlimit++;
		timeout_del(&s->s_deferrals_tmo);
		task_add(s->s_softnet, &s->s_deferrals_task);
	}

	pfsync_slice_sched(s);
	pfsync_slice_leave(sc, s);
	rv = 1;
leave:
	smr_read_leave();

	return (rv);
}

static void
pfsync_deferred(struct pfsync_softc *sc, struct pf_state *st)
{
	struct pfsync_slice *s;
	struct pfsync_deferral *pd;

	s = pfsync_slice_enter(sc, st);

	pd = st->sync_defer;
	if (pd != NULL) {
		s->s_stat_defer_ack++;

		TAILQ_REMOVE(&s->s_deferrals, pd, pd_entry);
		s->s_deferred--;

		st = pd->pd_st;
		st->sync_defer = NULL;
	}
	pfsync_slice_leave(sc, s);

	if (pd != NULL)
		pfsync_defer_output(pd);
}

static void
pfsync_deferrals_tmo(void *arg)
{
	struct pfsync_slice *s = arg;

	if (READ_ONCE(s->s_deferred) > 0)
		task_add(s->s_softnet, &s->s_deferrals_task);
}

static void
pfsync_deferrals_task(void *arg)
{
	struct pfsync_slice *s = arg;
	struct pfsync_deferral *pd;
	struct pf_state *st;
	uint64_t now, nsec = 0;
	struct pfsync_deferrals pds = TAILQ_HEAD_INITIALIZER(pds);

	now = getnsecuptime();

	mtx_enter(&s->s_mtx);
	s->s_stat_defer_run++; /* maybe move this into the loop */
	for (;;) {
		pd = TAILQ_FIRST(&s->s_deferrals);
		if (pd == NULL)
			break;

		if (s->s_deferred < PFSYNC_DEFER_LIMIT &&
		    now < pd->pd_deadline) {
			nsec = pd->pd_deadline - now;
			break;
		}

		TAILQ_REMOVE(&s->s_deferrals, pd, pd_entry);
		s->s_deferred--;

		/*
		 * detach the pd from the state. the pd still refers
		 * to the state though.
		 */
		st = pd->pd_st;
		st->sync_defer = NULL;

		TAILQ_INSERT_TAIL(&pds, pd, pd_entry);
	}
	mtx_leave(&s->s_mtx);

	if (nsec > 0) {
		/* we were looking at a pd, but it wasn't old enough */
		timeout_add_nsec(&s->s_deferrals_tmo, nsec);
	}

	if (TAILQ_EMPTY(&pds))
		return;

	NET_LOCK();
	while ((pd = TAILQ_FIRST(&pds)) != NULL) {
		TAILQ_REMOVE(&pds, pd, pd_entry);

		pfsync_defer_output(pd);
	}
	NET_UNLOCK();
}

static void
pfsync_defer_output(struct pfsync_deferral *pd)
{
	struct pf_pdesc pdesc;
	struct pf_state *st = pd->pd_st;

	if (st->rt == PF_ROUTETO) {
		if (pf_setup_pdesc(&pdesc, st->key[PF_SK_WIRE]->af,
		    st->direction, NULL, pd->pd_m, NULL) != PF_PASS)
			return;
		switch (st->key[PF_SK_WIRE]->af) {
		case AF_INET:
			pf_route(&pdesc, st);
			break;
#ifdef INET6
		case AF_INET6:
			pf_route6(&pdesc, st);
			break;
#endif /* INET6 */
		default:
			unhandled_af(st->key[PF_SK_WIRE]->af);
		}
		pd->pd_m = pdesc.m;
	} else {
		switch (st->key[PF_SK_WIRE]->af) {
		case AF_INET:
			ip_output(pd->pd_m, NULL, NULL, 0, NULL, NULL, 0);
			break;
#ifdef INET6
		case AF_INET6:
			ip6_output(pd->pd_m, NULL, NULL, 0, NULL, NULL);
			break;
#endif /* INET6 */
		default:
			unhandled_af(st->key[PF_SK_WIRE]->af);
		}

		pd->pd_m = NULL;
	}

	pf_state_unref(st);
	m_freem(pd->pd_m);
	pool_put(&pfsync_deferrals_pool, pd);
}

struct pfsync_subh_bus {
	struct pfsync_subheader	subh;
	struct pfsync_bus	bus;
} __packed __aligned(4);

static unsigned int
pfsync_bulk_snd_bus(struct pfsync_softc *sc,
    struct mbuf *m, const unsigned int space,
    uint32_t endtime, uint8_t status)
{
	struct pfsync_subh_bus *h;
	unsigned int nlen;

	nlen = m->m_len + sizeof(*h);
	if (space < nlen)
		return (0);

	h = (struct pfsync_subh_bus *)(mtod(m, caddr_t) + m->m_len);
	memset(h, 0, sizeof(*h));

	h->subh.action = PFSYNC_ACT_BUS;
	h->subh.len = sizeof(h->bus) >> 2;
	h->subh.count = htons(1);

	h->bus.creatorid = pf_status.hostid;
	h->bus.endtime = htonl(endtime);
	h->bus.status = status;

	m->m_len = nlen;

	return (1);
}

static unsigned int
pfsync_bulk_snd_states(struct pfsync_softc *sc,
    struct mbuf *m, const unsigned int space, unsigned int len)
{
	struct pf_state *st;
	struct pfsync_state *sp;
	unsigned int nlen;
	unsigned int count = 0;

	st = sc->sc_bulk_snd.snd_next;

	for (;;) {
		nlen = len + sizeof(*sp);
		sp = (struct pfsync_state *)(mtod(m, caddr_t) + len);
		if (space < nlen)
			break;

		mtx_enter(&st->mtx);
		pf_state_export(sp, st);
		mtx_leave(&st->mtx);

		/* commit */
		count++;
		m->m_len = len = nlen;

		if (st == sc->sc_bulk_snd.snd_tail) {
			if (pfsync_bulk_snd_bus(sc, m, space,
			    0, PFSYNC_BUS_END) == 0) {
				/* couldn't fit the BUS */
				st = NULL;
				break;
			}

			/* this BUS is done */
			pfsync_dprintf(sc, "bulk send done (%s)", __func__);
			sc->sc_bulk_snd.snd_again = 0; /* XXX */
			sc->sc_bulk_snd.snd_next = NULL;
			sc->sc_bulk_snd.snd_tail = NULL;
			return (count);
		}

		st = TAILQ_NEXT(st, entry_list);
	}

	/* there's still work to do */
	sc->sc_bulk_snd.snd_next = st;
	timeout_add_msec(&sc->sc_bulk_snd.snd_tmo, PFSYNC_BULK_SND_IVAL_MS);

	return (count);
}

static unsigned int
pfsync_bulk_snd_sub(struct pfsync_softc *sc,
    struct mbuf *m, const unsigned int space)
{
	struct pfsync_subheader *subh;
	unsigned int count;
	unsigned int len, nlen;

	len = m->m_len;
	nlen = len + sizeof(*subh);
	if (nlen > space)
		return (0);

	subh = (struct pfsync_subheader *)(mtod(m, caddr_t) + len);

	/*
	 * pfsync_bulk_snd_states only updates m->m_len after
	 * filling in a state after the offset we gave it.
	 */
	count = pfsync_bulk_snd_states(sc, m, space, nlen);
	if (count == 0)
		return (0);

	subh->action = PFSYNC_ACT_UPD;
	subh->len = sizeof(struct pfsync_state) >> 2;
	subh->count = htons(count);

	return (count);
}

static void
pfsync_bulk_snd_start(struct pfsync_softc *sc)
{
	const unsigned int space = sc->sc_if.if_mtu -
	    (sizeof(struct ip) + sizeof(struct pfsync_header));
	struct mbuf *m;

	rw_enter_read(&pf_state_list.pfs_rwl);

	rw_enter_write(&sc->sc_bulk_snd.snd_lock);
	if (sc->sc_bulk_snd.snd_next != NULL) {
		sc->sc_bulk_snd.snd_again = 1;
		goto leave;
	}

	mtx_enter(&pf_state_list.pfs_mtx);
	sc->sc_bulk_snd.snd_next = TAILQ_FIRST(&pf_state_list.pfs_list);
	sc->sc_bulk_snd.snd_tail = TAILQ_LAST(&pf_state_list.pfs_list,
	    pf_state_queue);
	mtx_leave(&pf_state_list.pfs_mtx);

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL)
		goto leave;

	MCLGETL(m, M_DONTWAIT, max_linkhdr + sc->sc_if.if_mtu);
	if (!ISSET(m->m_flags, M_EXT)) {
		/* some error++ */
		m_freem(m); /* drop */
		goto leave;
	}

	m_align(m, space);
	m->m_len = 0;

	if (sc->sc_bulk_snd.snd_tail == NULL) {
		pfsync_dprintf(sc, "bulk send empty (%s)", __func__);

		/* list is empty */
		if (pfsync_bulk_snd_bus(sc, m, space, 0, PFSYNC_BUS_END) == 0)
			panic("%s: mtu is too low", __func__);
		goto encap;
	}

	pfsync_dprintf(sc, "bulk send start (%s)", __func__);

	/* start a bulk update. */
	if (pfsync_bulk_snd_bus(sc, m, space, 0, PFSYNC_BUS_START) == 0)
		panic("%s: mtu is too low", __func__);

	/* fill it up with state updates. */
	pfsync_bulk_snd_sub(sc, m, space);

encap:
	m->m_pkthdr.len = m->m_len;
	m = pfsync_encap(sc, m);
	if (m == NULL)
		goto leave;

	pfsync_sendout(sc, m);

leave:
	rw_exit_write(&sc->sc_bulk_snd.snd_lock);

	rw_exit_read(&pf_state_list.pfs_rwl);
}

static void
pfsync_bulk_snd_tmo(void *arg)
{
	struct pfsync_softc *sc = arg;
	const unsigned int space = sc->sc_if.if_mtu -
	    (sizeof(struct ip) + sizeof(struct pfsync_header));
	struct mbuf *m;

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL) {
		/* some error++ */
		/* retry later */
		timeout_add_msec(&sc->sc_bulk_snd.snd_tmo,
		    PFSYNC_BULK_SND_IVAL_MS);
		return;
	}

	MCLGETL(m, M_DONTWAIT, max_linkhdr + sc->sc_if.if_mtu);
	if (!ISSET(m->m_flags, M_EXT)) {
		/* some error++ */
		m_freem(m);
		/* retry later */
		timeout_add_msec(&sc->sc_bulk_snd.snd_tmo,
		    PFSYNC_BULK_SND_IVAL_MS);
		return;
	}

	m_align(m, space);
	m->m_len = 0;

	rw_enter_read(&pf_state_list.pfs_rwl);
	rw_enter_write(&sc->sc_bulk_snd.snd_lock);

	if (sc->sc_bulk_snd.snd_next == NULL) {
		/* there was no space in the previous packet for a BUS END */

		if (pfsync_bulk_snd_bus(sc, m, space, 0, PFSYNC_BUS_END) == 0)
			panic("%s: mtu is too low", __func__);

		/* this bulk is done */
		pfsync_dprintf(sc, "bulk send done (%s)", __func__);
		sc->sc_bulk_snd.snd_again = 0; /* XXX */
		sc->sc_bulk_snd.snd_tail = NULL;
	} else {
		pfsync_dprintf(sc, "bulk send again (%s)", __func__);

		/* fill it up with state updates. */
		pfsync_bulk_snd_sub(sc, m, space);
	}

	m->m_pkthdr.len = m->m_len;
	m = pfsync_encap(sc, m);

	rw_exit_write(&sc->sc_bulk_snd.snd_lock);
	rw_exit_read(&pf_state_list.pfs_rwl);

	if (m != NULL) {
		NET_LOCK();
		pfsync_sendout(sc, m);
		NET_UNLOCK();
	}
}

static void
pfsync_update_state_req(struct pfsync_softc *sc, struct pf_state *st)
{
	struct pfsync_slice *s = pfsync_slice_enter(sc, st);

	switch (st->sync_state) {
	case PFSYNC_S_UPD_C:
	case PFSYNC_S_IACK:
		pfsync_q_del(s, st);
		/* FALLTHROUGH */
	case PFSYNC_S_NONE:
		pfsync_q_ins(s, st, PFSYNC_S_UPD);
		break;

	case PFSYNC_S_INS:
	case PFSYNC_S_UPD:
	case PFSYNC_S_DEL:
		/* we're already handling it */
		break;
	default:
		panic("%s: state %p unexpected sync_state %d",
		    __func__, st, st->sync_state);
	}

	pfsync_slice_sched(s);
	pfsync_slice_leave(sc, s);
}

#if defined(IPSEC)
static void
pfsync_out_tdb(struct tdb *tdb, void *buf)
{
	struct pfsync_tdb *ut = buf;

	memset(ut, 0, sizeof(*ut));
	ut->spi = tdb->tdb_spi;
	memcpy(&ut->dst, &tdb->tdb_dst, sizeof(ut->dst));
	/*
	 * When a failover happens, the master's rpl is probably above
	 * what we see here (we may be up to a second late), so
	 * increase it a bit for outbound tdbs to manage most such
	 * situations.
	 *
	 * For now, just add an offset that is likely to be larger
	 * than the number of packets we can see in one second. The RFC
	 * just says the next packet must have a higher seq value.
	 *
	 * XXX What is a good algorithm for this? We could use
	 * a rate-determined increase, but to know it, we would have
	 * to extend struct tdb.
	 * XXX pt->rpl can wrap over MAXINT, but if so the real tdb
	 * will soon be replaced anyway. For now, just don't handle
	 * this edge case.
	 */
#define RPL_INCR 16384
	ut->rpl = htobe64(tdb->tdb_rpl +
	    (ISSET(tdb->tdb_flags, TDBF_PFSYNC_RPL) ? RPL_INCR : 0));
	ut->cur_bytes = htobe64(tdb->tdb_cur_bytes);
	ut->sproto = tdb->tdb_sproto;
	ut->rdomain = htons(tdb->tdb_rdomain);
}

static struct pfsync_slice *
pfsync_slice_enter_tdb(struct pfsync_softc *sc, const struct tdb *t)
{
	/*
	 * just use the first slice for all ipsec (for now) until
	 * it's more obvious what property (eg, spi) we can distribute
	 * tdbs over slices with.
	 */
	struct pfsync_slice *s = &sc->sc_slices[0];

	if (!mtx_enter_try(&s->s_mtx)) {
		mtx_enter(&s->s_mtx);
		s->s_stat_contended++;
	}
	s->s_stat_locks++;

	return (s);
}

static void
pfsync_tdb_ins(struct pfsync_slice *s, struct tdb *tdb)
{
	size_t nlen = sizeof(struct pfsync_tdb);
	struct mbuf *m = NULL;

	KASSERT(s->s_len >= PFSYNC_MINPKT);

	MUTEX_ASSERT_LOCKED(&s->s_mtx);
	MUTEX_ASSERT_UNLOCKED(&tdb->tdb_mtx);

	if (TAILQ_EMPTY(&s->s_tdb_q))
		nlen += sizeof(struct pfsync_subheader);

	if (s->s_len + nlen > s->s_pfsync->sc_if.if_mtu) {
		m = pfsync_slice_write(s);
		if (m != NULL) {
			s->s_stat_enqueue++;
			if (mq_enqueue(&s->s_sendq, m) == 0)
				task_add(s->s_softnet, &s->s_send);
		}

		nlen = sizeof(struct pfsync_subheader) +
		    sizeof(struct pfsync_tdb);
	}

	s->s_len += nlen;
	TAILQ_INSERT_TAIL(&s->s_tdb_q, tdb, tdb_sync_entry);
	tdb->tdb_updates = 0;

	if (!timeout_pending(&s->s_tmo))
		timeout_add_sec(&s->s_tmo, 1);
}

static void
pfsync_tdb_del(struct pfsync_slice *s, struct tdb *tdb)
{
	MUTEX_ASSERT_LOCKED(&s->s_mtx);
	MUTEX_ASSERT_UNLOCKED(&tdb->tdb_mtx);

	TAILQ_REMOVE(&s->s_tdb_q, tdb, tdb_sync_entry);

	s->s_len -= sizeof(struct pfsync_tdb);
	if (TAILQ_EMPTY(&s->s_tdb_q))
		s->s_len -= sizeof(struct pfsync_subheader);
}

/*
 * the reference that pfsync has to a tdb is accounted for by the
 * TDBF_PFSYNC flag, not by tdb_ref/tdb_unref. tdb_delete_tdb() is
 * called after all other references to a tdb are dropped (with
 * tdb_unref) as part of the tdb_free().
 *
 * tdb_free() needs to wait for pfsync to let go of the tdb though,
 * which would be best handled by a reference count, but tdb_free
 * needs the NET_LOCK which pfsync is already fighting with. instead
 * use the TDBF_PFSYNC_SNAPPED flag to coordinate the pfsync write/drop
 * with tdb_free.
 */

void
pfsync_update_tdb(struct tdb *tdb, int output)
{
	struct pfsync_softc *sc;

	MUTEX_ASSERT_UNLOCKED(&tdb->tdb_mtx);

	smr_read_enter();
	sc = SMR_PTR_GET(&pfsyncif);
	if (sc != NULL) {
		struct pfsync_slice *s = pfsync_slice_enter_tdb(sc, tdb);

		/* TDBF_PFSYNC is only changed while the slice mtx is held */
		if (!ISSET(tdb->tdb_flags, TDBF_PFSYNC)) {
			mtx_enter(&tdb->tdb_mtx);
			SET(tdb->tdb_flags, TDBF_PFSYNC);
			mtx_leave(&tdb->tdb_mtx);

			pfsync_tdb_ins(s, tdb);
		} else if (++tdb->tdb_updates >= sc->sc_maxupdates)
			pfsync_slice_sched(s);

		/* XXX no sync timestamp on tdbs to check */

		pfsync_slice_leave(sc, s);
	}
	smr_read_leave();
}

void
pfsync_delete_tdb(struct tdb *tdb)
{
	struct pfsync_softc *sc;

	MUTEX_ASSERT_UNLOCKED(&tdb->tdb_mtx);

	smr_read_enter();
	sc = SMR_PTR_GET(&pfsyncif);
	if (sc != NULL) {
		struct pfsync_slice *s = pfsync_slice_enter_tdb(sc, tdb);

		/* TDBF_PFSYNC is only changed while the slice mtx is held */
		if (ISSET(tdb->tdb_flags, TDBF_PFSYNC)) {
			pfsync_tdb_del(s, tdb);

			mtx_enter(&tdb->tdb_mtx);
			CLR(tdb->tdb_flags, TDBF_PFSYNC);
			mtx_leave(&tdb->tdb_mtx);
		}

		pfsync_slice_leave(sc, s);
	}
	smr_read_leave();

	/*
	 * handle pfsync_slice_drop being called from pfsync_down
	 * and the smr/slice access above won't work.
	 */

	mtx_enter(&tdb->tdb_mtx);
	SET(tdb->tdb_flags, TDBF_PFSYNC_SNAPPED); /* like a thanos snap */
	while (ISSET(tdb->tdb_flags, TDBF_PFSYNC)) {
		msleep_nsec(&tdb->tdb_updates, &tdb->tdb_mtx, PWAIT,
		    "tdbfree", INFSLP);
	}
	mtx_leave(&tdb->tdb_mtx);
}
#endif /* defined(IPSEC) */

struct pfsync_act {
	void (*in)(struct pfsync_softc *, const caddr_t,
	    unsigned int, unsigned int);
	size_t len;
};

static void	pfsync_in_clr(struct pfsync_softc *,
		    const caddr_t, unsigned int, unsigned int);
static void	pfsync_in_iack(struct pfsync_softc *,
		    const caddr_t, unsigned int, unsigned int);
static void	pfsync_in_upd_c(struct pfsync_softc *,
		    const caddr_t, unsigned int, unsigned int);
static void	pfsync_in_ureq(struct pfsync_softc *,
		    const caddr_t, unsigned int, unsigned int);
static void	pfsync_in_del(struct pfsync_softc *,
		    const caddr_t, unsigned int, unsigned int);
static void	pfsync_in_del_c(struct pfsync_softc *,
		    const caddr_t, unsigned int, unsigned int);
static void	pfsync_in_bus(struct pfsync_softc *,
		    const caddr_t, unsigned int, unsigned int);
static void	pfsync_in_tdb(struct pfsync_softc *,
		    const caddr_t, unsigned int, unsigned int);
static void	pfsync_in_ins(struct pfsync_softc *,
		    const caddr_t, unsigned int, unsigned int);
static void	pfsync_in_upd(struct pfsync_softc *,
		    const caddr_t, unsigned int, unsigned int);

static const struct pfsync_act pfsync_acts[] = {
	[PFSYNC_ACT_CLR] =
	    { pfsync_in_clr,	sizeof(struct pfsync_clr) },
	[PFSYNC_ACT_INS_ACK] =
	    { pfsync_in_iack,	sizeof(struct pfsync_ins_ack) },
	[PFSYNC_ACT_UPD_C] =
	    { pfsync_in_upd_c,	sizeof(struct pfsync_upd_c) },
	[PFSYNC_ACT_UPD_REQ] =
	    { pfsync_in_ureq,	sizeof(struct pfsync_upd_req) },
	[PFSYNC_ACT_DEL] =
	    { pfsync_in_del,	sizeof(struct pfsync_state) },
	[PFSYNC_ACT_DEL_C] =
	    { pfsync_in_del_c,	sizeof(struct pfsync_del_c) },
	[PFSYNC_ACT_BUS] =
	    { pfsync_in_bus,	sizeof(struct pfsync_bus) },
	[PFSYNC_ACT_INS] =
	    { pfsync_in_ins,	sizeof(struct pfsync_state) },
	[PFSYNC_ACT_UPD] =
	    { pfsync_in_upd,	sizeof(struct pfsync_state) },
	[PFSYNC_ACT_TDB] =
	    { pfsync_in_tdb,	sizeof(struct pfsync_tdb) },
};

static void
pfsync_in_skip(struct pfsync_softc *sc,
    const caddr_t buf, unsigned int mlen, unsigned int count)
{
	/* nop */
}

static struct mbuf *
pfsync_input(struct mbuf *m, uint8_t ttl, unsigned int hlen)
{
	struct pfsync_softc *sc;
	struct pfsync_header *ph;
	struct pfsync_subheader *subh;
	unsigned int len;
	void (*in)(struct pfsync_softc *,
	    const caddr_t, unsigned int, unsigned int);

	pfsyncstat_inc(pfsyncs_ipackets);

	if (!pf_status.running)
		return (m);

	/*
	 * pfsyncif is only set if it is up and running correctly.
	 */
	smr_read_enter();
	sc = SMR_PTR_GET(&pfsyncif);
	if (sc == NULL)
		goto leave;

	if (sc->sc_sync_ifidx != m->m_pkthdr.ph_ifidx) {
		pfsyncstat_inc(pfsyncs_badif);
		goto leave;
	}

	/* verify that the IP TTL is 255. */
	if (ttl != PFSYNC_DFLTTL) {
		pfsyncstat_inc(pfsyncs_badttl);
		goto leave;
	}

	m_adj(m, hlen);

	if (m->m_pkthdr.len < sizeof(*ph)) {
		pfsyncstat_inc(pfsyncs_hdrops);
		goto leave;
	}
	if (m->m_len < sizeof(*ph)) {
		m = m_pullup(m, sizeof(*ph));
		if (m == NULL)
			goto leave;
	}

	ph = mtod(m, struct pfsync_header *);
	if (ph->version != PFSYNC_VERSION) {
		pfsyncstat_inc(pfsyncs_badver);
		goto leave;
	}

	len = ntohs(ph->len);
	if (m->m_pkthdr.len < len) {
		pfsyncstat_inc(pfsyncs_badlen);
		goto leave;
	}
	if (m->m_pkthdr.len > len)
		m->m_pkthdr.len = len;

	/* ok, it's serious now */
	refcnt_take(&sc->sc_refs);
	smr_read_leave();

	counters_pkt(sc->sc_if.if_counters, ifc_ipackets, ifc_ibytes, len);

	m_adj(m, sizeof(*ph));

	while (m->m_pkthdr.len >= sizeof(*subh)) {
		unsigned int action, mlen, count;

		if (m->m_len < sizeof(*subh)) {
			m = m_pullup(m, sizeof(*subh));
			if (m == NULL)
				goto rele;
		}
		subh = mtod(m, struct pfsync_subheader *);

		action = subh->action;
		mlen = subh->len << 2;
		count = ntohs(subh->count);

		if (action >= PFSYNC_ACT_MAX ||
		    action >= nitems(pfsync_acts) ||
		    mlen < pfsync_acts[subh->action].len) {
			/*
			 * subheaders are always followed by at least one
			 * message, so if the peer is new
			 * enough to tell us how big its messages are then we
			 * know enough to skip them.
			 */
			if (count == 0 || mlen == 0) {
				pfsyncstat_inc(pfsyncs_badact);
				goto rele;
			}

			in = pfsync_in_skip;
		} else {
			in = pfsync_acts[action].in;
			if (in == NULL)
				in = pfsync_in_skip;
		}

		m_adj(m, sizeof(*subh));
		len = mlen * count;
		if (len > m->m_pkthdr.len) {
			pfsyncstat_inc(pfsyncs_badlen);
			goto rele;
		}
		if (m->m_len < len) {
			m = m_pullup(m, len);
			if (m == NULL)
				goto rele;
		}

		(*in)(sc, mtod(m, caddr_t), mlen, count);
		m_adj(m, len);
	}

rele:
	refcnt_rele_wake(&sc->sc_refs);
	return (m);

leave:
	smr_read_leave();
	return (m);
}

static void
pfsync_in_clr(struct pfsync_softc *sc,
    const caddr_t buf, unsigned int mlen, unsigned int count)
{
	const struct pfsync_clr *clr;
	struct pf_state *head, *tail, *st, *next;
	struct pfi_kif *kif;
	uint32_t creatorid;
	unsigned int i;

	rw_enter_read(&pf_state_list.pfs_rwl);

	/* get a view of the state list */
	mtx_enter(&pf_state_list.pfs_mtx);
	head = TAILQ_FIRST(&pf_state_list.pfs_list);
	tail = TAILQ_LAST(&pf_state_list.pfs_list, pf_state_queue);
	mtx_leave(&pf_state_list.pfs_mtx);

	PF_LOCK();
	for (i = 0; i < count; i++) {
		clr = (struct pfsync_clr *)(buf + i * mlen);

		creatorid = clr->creatorid;
		if (clr->ifname[0] == '\0')
			kif = NULL;
		else {
			kif = pfi_kif_find(clr->ifname);
			if (kif == NULL)
				continue;
		}

		st = NULL;
		next = head;

		PF_STATE_ENTER_WRITE();
		while (st != tail) {
			st = next;
			next = TAILQ_NEXT(st, entry_list);

			if (creatorid != st->creatorid)
				continue;
			if (kif != NULL && kif != st->kif)
				continue;

			mtx_enter(&st->mtx);
			SET(st->state_flags, PFSTATE_NOSYNC);
			mtx_leave(&st->mtx);
			pf_remove_state(st);
		}
		PF_STATE_EXIT_WRITE();
	}
	PF_UNLOCK();

	rw_exit_read(&pf_state_list.pfs_rwl);
}

static void
pfsync_in_ins(struct pfsync_softc *sc,
    const caddr_t buf, unsigned int mlen, unsigned int count)
{
	const struct pfsync_state *sp;
	sa_family_t af1, af2;
	unsigned int i;

	PF_LOCK();
	for (i = 0; i < count; i++) {
		sp = (struct pfsync_state *)(buf + mlen * i);
		af1 = sp->key[0].af;
		af2 = sp->key[1].af;

		/* check for invalid values */
		if (sp->timeout >= PFTM_MAX ||
		    sp->src.state > PF_TCPS_PROXY_DST ||
		    sp->dst.state > PF_TCPS_PROXY_DST ||
		    sp->direction > PF_OUT ||
		    (((af1 || af2) &&
		     ((af1 != AF_INET && af1 != AF_INET6) ||
		      (af2 != AF_INET && af2 != AF_INET6))) ||
		     (sp->af != AF_INET && sp->af != AF_INET6))) {
			pfsyncstat_inc(pfsyncs_badval);
			continue;
		}

		if (pf_state_import(sp, PFSYNC_SI_PFSYNC) == ENOMEM) {
			/* drop out, but process the rest of the actions */
			break;
		}
	}
	PF_UNLOCK();
}

static void
pfsync_in_iack(struct pfsync_softc *sc,
    const caddr_t buf, unsigned int mlen, unsigned int count)
{
	const struct pfsync_ins_ack *ia;
	struct pf_state_cmp id_key;
	struct pf_state *st;
	unsigned int i;

	for (i = 0; i < count; i++) {
		ia = (struct pfsync_ins_ack *)(buf + mlen * i);

		id_key.id = ia->id;
		id_key.creatorid = ia->creatorid;

		PF_STATE_ENTER_READ();
		st = pf_find_state_byid(&id_key);
		pf_state_ref(st);
		PF_STATE_EXIT_READ();
		if (st == NULL)
			continue;

		if (READ_ONCE(st->sync_defer) != NULL)
			pfsync_deferred(sc, st);

		pf_state_unref(st);
	}
}

static int
pfsync_upd_tcp(struct pf_state *st, const struct pfsync_state_peer *src,
    const struct pfsync_state_peer *dst)
{
	int sync = 0;

	/*
	 * The state should never go backwards except
	 * for syn-proxy states.  Neither should the
	 * sequence window slide backwards.
	 */
	if ((st->src.state > src->state &&
	    (st->src.state < PF_TCPS_PROXY_SRC ||
	     src->state >= PF_TCPS_PROXY_SRC)) ||

	    (st->src.state == src->state &&
	     SEQ_GT(st->src.seqlo, ntohl(src->seqlo))))
		sync++;
	else
		pf_state_peer_ntoh(src, &st->src);

	if ((st->dst.state > dst->state) ||

	    (st->dst.state == dst->state &&
	     SEQ_GT(st->dst.seqlo, ntohl(dst->seqlo))))
		sync++;
	else
		pf_state_peer_ntoh(dst, &st->dst);

	return (sync);
}

static void
pfsync_in_updates(struct pfsync_softc *sc, struct pf_state *st,
    const struct pfsync_state_peer *src, const struct pfsync_state_peer *dst,
    uint8_t timeout)
{
	struct pf_state_scrub *sscrub = NULL;
	struct pf_state_scrub *dscrub = NULL;
	int sync;

	if (src->scrub.scrub_flag && st->src.scrub == NULL) {
		sscrub = pf_state_scrub_get();
		if (sscrub == NULL) {
			/* inc error? */
			goto out;
		}
	}
	if (dst->scrub.scrub_flag && st->dst.scrub == NULL) {
		dscrub = pf_state_scrub_get();
		if (dscrub == NULL) {
			/* inc error? */
			goto out;
		}
	}

	if (READ_ONCE(st->sync_defer) != NULL)
		pfsync_deferred(sc, st);

	mtx_enter(&st->mtx);

	/* attach the scrub memory if needed */
	if (sscrub != NULL && st->src.scrub == NULL) {
		st->src.scrub = sscrub;
		sscrub = NULL;
	}
	if (dscrub != NULL && st->dst.scrub == NULL) {
		st->dst.scrub = dscrub;
		dscrub = NULL;
	}

	if (st->key[PF_SK_WIRE]->proto == IPPROTO_TCP)
		sync = pfsync_upd_tcp(st, src, dst);
	else {
		sync = 0;

		/*
		 * Non-TCP protocol state machine always go
		 * forwards
		 */
		if (st->src.state > src->state)
			sync++;
		else
			pf_state_peer_ntoh(src, &st->src);

		if (st->dst.state > dst->state)
			sync++;
		else
			pf_state_peer_ntoh(dst, &st->dst);
	}

	st->pfsync_time = getuptime();
	if (sync < 2) {
		st->expire = st->pfsync_time;
		st->timeout = timeout;
	}

	mtx_leave(&st->mtx);

	if (sync) {
		pfsyncstat_inc(pfsyncs_stale);
		pfsync_update_state(st);
	}

out:
	if (sscrub != NULL)
		pf_state_scrub_put(sscrub);
	if (dscrub != NULL)
		pf_state_scrub_put(dscrub);
}


static void
pfsync_in_upd(struct pfsync_softc *sc,
    const caddr_t buf, unsigned int mlen, unsigned int count)
{
	const struct pfsync_state *sp;
	struct pf_state_cmp id_key;
	struct pf_state *st;
	int error;
	unsigned int i;

	for (i = 0; i < count; i++) {
		sp = (struct pfsync_state *)(buf + mlen * i);

		/* check for invalid values */
		if (sp->timeout >= PFTM_MAX ||
		    sp->src.state > PF_TCPS_PROXY_DST ||
		    sp->dst.state > PF_TCPS_PROXY_DST) {
			pfsyncstat_inc(pfsyncs_badval);
			continue;
		}

		id_key.id = sp->id;
		id_key.creatorid = sp->creatorid;

		PF_STATE_ENTER_READ();
		st = pf_find_state_byid(&id_key);
		pf_state_ref(st);
		PF_STATE_EXIT_READ();
		if (st == NULL) {
			/* insert the update */
			PF_LOCK();
			error = pf_state_import(sp, PFSYNC_SI_PFSYNC);
			if (error)
				pfsyncstat_inc(pfsyncs_badstate);
			PF_UNLOCK();
			continue;
		}

		pfsync_in_updates(sc, st, &sp->src, &sp->dst, sp->timeout);

		pf_state_unref(st);
	}
}

static struct mbuf *
pfsync_upd_req_init(struct pfsync_softc *sc, unsigned int count)
{
	struct mbuf *m;
	unsigned int mlen;

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL) {
		pfsyncstat_inc(pfsyncs_onomem);
		return (NULL);
	}

	mlen = max_linkhdr + sizeof(sc->sc_template) +
	    sizeof(struct pfsync_header) +
	    sizeof(struct pfsync_subheader) +
	    sizeof(struct pfsync_upd_req) * count;

	if (mlen > MHLEN) {
		MCLGETL(m, M_DONTWAIT, mlen);
		if (!ISSET(m->m_flags, M_EXT)) {
			m_freem(m);
			return (NULL);
		}
	}

	m_align(m, 0);
	m->m_len = 0;

	return (m);
}

static void
pfsync_in_upd_c(struct pfsync_softc *sc,
    const caddr_t buf, unsigned int mlen, unsigned int count)
{
	const struct pfsync_upd_c *up;
	struct pf_state_cmp id_key;
	struct pf_state *st;
	unsigned int i;
	struct mbuf *m = NULL;
	unsigned int rcount = 0;

	for (i = 0; i < count; i++) {
		up = (struct pfsync_upd_c *)(buf + mlen * i);

		/* check for invalid values */
		if (up->timeout >= PFTM_MAX ||
		    up->src.state > PF_TCPS_PROXY_DST ||
		    up->dst.state > PF_TCPS_PROXY_DST) {
			pfsyncstat_inc(pfsyncs_badval);
			continue;
		}

		id_key.id = up->id;
		id_key.creatorid = up->creatorid;

		PF_STATE_ENTER_READ();
		st = pf_find_state_byid(&id_key);
		pf_state_ref(st);
		PF_STATE_EXIT_READ();
		if (st == NULL) {
			/* We don't have this state. Ask for it. */
			struct pfsync_upd_req *ur;

			if (m == NULL) {
				m = pfsync_upd_req_init(sc, count);
				if (m == NULL) {
					pfsyncstat_inc(pfsyncs_onomem);
					continue;
				}
			}

			m = m_prepend(m, sizeof(*ur), M_DONTWAIT);
			if (m == NULL) {
				pfsyncstat_inc(pfsyncs_onomem);
				continue;
			}

			ur = mtod(m, struct pfsync_upd_req *);
			ur->id = up->id;
			ur->creatorid = up->creatorid;
			rcount++;

			continue;
		}

		pfsync_in_updates(sc, st, &up->src, &up->dst, up->timeout);

		pf_state_unref(st);
	}

	if (m != NULL) {
		struct pfsync_subheader *subh;

		m = m_prepend(m, sizeof(*subh), M_DONTWAIT);
		if (m == NULL) {
			pfsyncstat_inc(pfsyncs_onomem);
			return;
		}

		subh = mtod(m, struct pfsync_subheader *);
		subh->action = PFSYNC_ACT_UPD_REQ;
		subh->len = sizeof(struct pfsync_upd_req) >> 2;
		subh->count = htons(rcount);

		m = pfsync_encap(sc, m);
		if (m == NULL) {
			pfsyncstat_inc(pfsyncs_onomem);
			return;
		}

		pfsync_sendout(sc, m);
	}
}

static void
pfsync_in_ureq(struct pfsync_softc *sc,
    const caddr_t buf, unsigned int mlen, unsigned int count)
{
	const struct pfsync_upd_req *ur;
	struct pf_state_cmp id_key;
	struct pf_state *st;
	unsigned int i;

	for (i = 0; i < count; i++) {
		ur = (struct pfsync_upd_req *)(buf + mlen * i);

		id_key.id = ur->id;
		id_key.creatorid = ur->creatorid;

		if (id_key.id == 0 && id_key.creatorid == 0) {
			pfsync_bulk_snd_start(sc);
			continue;
		}

		PF_STATE_ENTER_READ();
		st = pf_find_state_byid(&id_key);
		if (st != NULL && st->timeout < PFTM_MAX &&
		    !ISSET(st->state_flags, PFSTATE_NOSYNC))
			pf_state_ref(st);
		else
			st = NULL;
		PF_STATE_EXIT_READ();
		if (st == NULL) {
			pfsyncstat_inc(pfsyncs_badstate);
			continue;
		}

		pfsync_update_state_req(sc, st);

		pf_state_unref(st);
	}
}

static void
pfsync_in_del(struct pfsync_softc *sc,
    const caddr_t buf, unsigned int mlen, unsigned int count)
{
	const struct pfsync_state *sp;
	struct pf_state_cmp id_key;
	struct pf_state *st;
	unsigned int i;

	PF_LOCK();
	PF_STATE_ENTER_WRITE();
	for (i = 0; i < count; i++) {
		sp = (struct pfsync_state *)(buf + mlen * i);

		id_key.id = sp->id;
		id_key.creatorid = sp->creatorid;

		st = pf_find_state_byid(&id_key);
		if (st == NULL) {
			pfsyncstat_inc(pfsyncs_badstate);
			continue;
		}

		mtx_enter(&st->mtx);
		SET(st->state_flags, PFSTATE_NOSYNC);
		mtx_leave(&st->mtx);
		pf_remove_state(st);
	}
	PF_STATE_EXIT_WRITE();
	PF_UNLOCK();
}

static void
pfsync_in_del_c(struct pfsync_softc *sc,
    const caddr_t buf, unsigned int mlen, unsigned int count)
{
	const struct pfsync_del_c *sp;
	struct pf_state_cmp id_key;
	struct pf_state *st;
	unsigned int i;

	PF_LOCK();
	PF_STATE_ENTER_WRITE();
	for (i = 0; i < count; i++) {
		sp = (struct pfsync_del_c *)(buf + mlen * i);

		id_key.id = sp->id;
		id_key.creatorid = sp->creatorid;

		st = pf_find_state_byid(&id_key);
		if (st == NULL) {
			pfsyncstat_inc(pfsyncs_badstate);
			continue;
		}

		mtx_enter(&st->mtx);
		SET(st->state_flags, PFSTATE_NOSYNC);
		mtx_leave(&st->mtx);
		pf_remove_state(st);
	}
	PF_STATE_EXIT_WRITE();
	PF_UNLOCK();
}

static void
pfsync_in_bus(struct pfsync_softc *sc,
    const caddr_t buf, unsigned int len, unsigned int count)
{
	const struct pfsync_bus *bus = (struct pfsync_bus *)buf;

	switch (bus->status) {
	case PFSYNC_BUS_START:
		pfsync_bulk_req_evt(sc, PFSYNC_BREQ_EVT_BUS_START);
		break;

	case PFSYNC_BUS_END:
		pfsync_bulk_req_evt(sc, PFSYNC_BREQ_EVT_BUS_END);
		break;
	}
}

#if defined(IPSEC)
/* Update an in-kernel tdb. Silently fail if no tdb is found. */
static void
pfsync_update_net_tdb(const struct pfsync_tdb *pt)
{
	struct tdb *tdb;

	NET_ASSERT_LOCKED();

	/* check for invalid values */
	if (ntohl(pt->spi) <= SPI_RESERVED_MAX ||
	    (pt->dst.sa.sa_family != AF_INET &&
	     pt->dst.sa.sa_family != AF_INET6))
		goto bad;

	tdb = gettdb(ntohs(pt->rdomain), pt->spi,
	    (union sockaddr_union *)&pt->dst, pt->sproto);
	if (tdb) {
		uint64_t rpl = betoh64(pt->rpl);
		uint64_t cur_bytes = betoh64(pt->cur_bytes);

		/* Neither replay nor byte counter should ever decrease. */
		mtx_enter(&tdb->tdb_mtx);
		if (rpl >= tdb->tdb_rpl &&
		    cur_bytes >= tdb->tdb_cur_bytes) {
			tdb->tdb_rpl = rpl;
			tdb->tdb_cur_bytes = cur_bytes;
		}
		mtx_leave(&tdb->tdb_mtx);

		tdb_unref(tdb);
	}
	return;

 bad:
	DPFPRINTF(LOG_WARNING, "pfsync_insert: PFSYNC_ACT_TDB_UPD: "
	    "invalid value");
	pfsyncstat_inc(pfsyncs_badstate);
	return;
}
#endif

static void
pfsync_in_tdb(struct pfsync_softc *sc,
    const caddr_t buf, unsigned int len, unsigned int count)
{
#if defined(IPSEC)
	const struct pfsync_tdb *tp;
	unsigned int i;

	for (i = 0; i < count; i++) {
		tp = (const struct pfsync_tdb *)(buf + len * i);
		pfsync_update_net_tdb(tp);
	}
#endif
}

int
pfsync_input4(struct mbuf **mp, int *offp, int proto, int af,
    struct netstack *ns)
{
	struct mbuf *m = *mp;
	struct ip *ip;

	ip = mtod(m, struct ip *);

	m = pfsync_input(m, ip->ip_ttl, ip->ip_hl << 2);

	m_freem(m);
	*mp = NULL;

	return (IPPROTO_DONE);
}

int
pfsync_sysctl_pfsyncstat(void *oldp, size_t *oldlenp, void *newp)
{
	struct pfsyncstats pfsyncstat;

	CTASSERT(sizeof(pfsyncstat) == (pfsyncs_ncounters * sizeof(uint64_t)));
	memset(&pfsyncstat, 0, sizeof pfsyncstat);
	counters_read(pfsynccounters, (uint64_t *)&pfsyncstat,
	    pfsyncs_ncounters, NULL);
	return (sysctl_rdstruct(oldp, oldlenp, newp,
	    &pfsyncstat, sizeof(pfsyncstat)));
}

int
pfsync_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case PFSYNCCTL_STATS:
		return (pfsync_sysctl_pfsyncstat(oldp, oldlenp, newp));
	default:
		return (ENOPROTOOPT);
	}
}
