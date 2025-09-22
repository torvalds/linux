/*	$OpenBSD: if_pppx.c,v 1.135 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright (c) 2010 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2010 David Gwynne <dlg@openbsd.org>
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

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/queue.h>
#include <sys/pool.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/vnode.h>
#include <sys/event.h>
#include <sys/mutex.h>
#include <sys/refcnt.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <net/if_dl.h>

#include <netinet/in_var.h>
#include <netinet/ip.h>

#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include "pf.h"
#if NPF > 0
#include <net/pfvar.h>
#endif

#include <net/ppp_defs.h>
#include <crypto/arc4.h>

#ifdef PIPEX
#include <net/pipex.h>
#include <net/pipex_local.h>
#else
#error PIPEX option not enabled
#endif

#ifdef PPPX_DEBUG
#define PPPX_D_INIT	(1<<0)

int pppxdebug = 0;

#define DPRINTF(_m, _p...)	do { \
					if (ISSET(pppxdebug, (_m))) \
						printf(_p); \
				} while (0)
#else
#define DPRINTF(_m, _p...)	/* _m, _p */
#endif


struct pppx_if;

/*
 * Locks used to protect struct members and global data
 *       I       immutable after creation
 *       K       kernel lock
 *       N       net lock
 *       m       pxd_mtx
 */

struct pppx_dev {
	LIST_ENTRY(pppx_dev)	pxd_entry;	/* [K] */
	int			pxd_unit;	/* [I] */

	/* kq shizz */
	struct mutex		pxd_mtx;
	struct klist		pxd_rklist;	/* [m] */
	struct klist		pxd_wklist;	/* [m] */

	/* queue of packets for userland to service - protected by splnet */
	struct mbuf_queue	pxd_svcq;
	int			pxd_waiting;	/* [N] */
	LIST_HEAD(,pppx_if)	pxd_pxis;	/* [K] */
};

LIST_HEAD(, pppx_dev)		pppx_devs =
				    LIST_HEAD_INITIALIZER(pppx_devs); /* [K] */
struct pool			pppx_if_pl;

struct pppx_dev			*pppx_dev_lookup(dev_t);
struct pppx_dev			*pppx_dev2pxd(dev_t);

struct pppx_if_key {
	int			pxik_session_id;	/* [I] */
	int			pxik_protocol;		/* [I] */
};

struct pppx_if {
	struct pppx_if_key	pxi_key;		/* [I] must be first
							    in the struct */
	struct refcnt		pxi_refcnt;

	RBT_ENTRY(pppx_if)	pxi_entry;		/* [K] */
	LIST_ENTRY(pppx_if)	pxi_list;		/* [K] */

	int			pxi_ready;		/* [K] */

	int			pxi_unit;		/* [I] */
	struct ifnet		pxi_if;
	struct pppx_dev		*pxi_dev;		/* [I] */
	struct pipex_session	*pxi_session;		/* [I] */
};

static inline int
pppx_if_cmp(const struct pppx_if *a, const struct pppx_if *b)
{
	return memcmp(&a->pxi_key, &b->pxi_key, sizeof(a->pxi_key));
}

RBT_HEAD(pppx_ifs, pppx_if) pppx_ifs = RBT_INITIALIZER(&pppx_ifs); /* [N] */
RBT_PROTOTYPE(pppx_ifs, pppx_if, pxi_entry, pppx_if_cmp);

int		pppx_if_next_unit(void);
struct pppx_if *pppx_if_find_locked(struct pppx_dev *, int, int);
static inline struct pppx_if	*pppx_if_find(struct pppx_dev *, int, int);
static inline void		 pppx_if_rele(struct pppx_if *);
int		pppx_add_session(struct pppx_dev *,
		    struct pipex_session_req *);
int		pppx_del_session(struct pppx_dev *,
		    struct pipex_session_close_req *);
int		pppx_set_session_descr(struct pppx_dev *,
		    struct pipex_session_descr_req *);

void		pppx_if_destroy(struct pppx_dev *, struct pppx_if *);
void		pppx_if_qstart(struct ifqueue *);
int		pppx_if_output(struct ifnet *, struct mbuf *,
		    struct sockaddr *, struct rtentry *);
int		pppx_if_ioctl(struct ifnet *, u_long, caddr_t);


void		pppxattach(int);

void		filt_pppx_rdetach(struct knote *);
int		filt_pppx_read(struct knote *, long);
int		filt_pppx_modify(struct kevent *, struct knote *);
int		filt_pppx_process(struct knote *, struct kevent *);

const struct filterops pppx_rd_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_pppx_rdetach,
	.f_event	= filt_pppx_read,
	.f_modify	= filt_pppx_modify,
	.f_process	= filt_pppx_process,
};

void		filt_pppx_wdetach(struct knote *);
int		filt_pppx_write(struct knote *, long);

const struct filterops pppx_wr_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_pppx_wdetach,
	.f_event	= filt_pppx_write,
	.f_modify	= filt_pppx_modify,
	.f_process	= filt_pppx_process,
};

struct pppx_dev *
pppx_dev_lookup(dev_t dev)
{
	struct pppx_dev *pxd;
	int unit = minor(dev);

	LIST_FOREACH(pxd, &pppx_devs, pxd_entry) {
		if (pxd->pxd_unit == unit)
			return (pxd);
	}

	return (NULL);
}

struct pppx_dev *
pppx_dev2pxd(dev_t dev)
{
	struct pppx_dev *pxd;

	pxd = pppx_dev_lookup(dev);

	return (pxd);
}

void
pppxattach(int n)
{
	pool_init(&pppx_if_pl, sizeof(struct pppx_if), 0, IPL_NONE,
	    PR_WAITOK, "pppxif", NULL);
	pipex_init();
}

int
pppxopen(dev_t dev, int flags, int mode, struct proc *p)
{
	struct pppx_dev *pxd;

	pxd = malloc(sizeof(*pxd), M_DEVBUF, M_WAITOK | M_ZERO);
	if (pppx_dev_lookup(dev) != NULL) {
		free(pxd, M_DEVBUF, sizeof(*pxd));
		return (EBUSY);
	}

	pxd->pxd_unit = minor(dev);
	mtx_init(&pxd->pxd_mtx, IPL_NET);
	klist_init_mutex(&pxd->pxd_rklist, &pxd->pxd_mtx);
	klist_init_mutex(&pxd->pxd_wklist, &pxd->pxd_mtx);
	LIST_INIT(&pxd->pxd_pxis);

	mq_init(&pxd->pxd_svcq, 128, IPL_NET);
	LIST_INSERT_HEAD(&pppx_devs, pxd, pxd_entry);

	return 0;
}

int
pppxread(dev_t dev, struct uio *uio, int ioflag)
{
	struct pppx_dev *pxd = pppx_dev2pxd(dev);
	struct mbuf *m, *m0;
	int error = 0;
	size_t len;

	if (!pxd)
		return (ENXIO);

	while ((m0 = mq_dequeue(&pxd->pxd_svcq)) == NULL) {
		if (ISSET(ioflag, IO_NDELAY))
			return (EWOULDBLOCK);

		NET_LOCK();
		pxd->pxd_waiting = 1;
		error = rwsleep_nsec(pxd, &netlock,
		    (PZERO + 1)|PCATCH, "pppxread", INFSLP);
		NET_UNLOCK();
		if (error != 0) {
			return (error);
		}
	}

	while (m0 != NULL && uio->uio_resid > 0 && error == 0) {
		len = ulmin(uio->uio_resid, m0->m_len);
		if (len != 0)
			error = uiomove(mtod(m0, caddr_t), len, uio);
		m = m_free(m0);
		m0 = m;
	}

	m_freem(m0);

	return (error);
}

int
pppxwrite(dev_t dev, struct uio *uio, int ioflag)
{
	struct pppx_dev *pxd = pppx_dev2pxd(dev);
	struct pppx_hdr *th;
	struct pppx_if	*pxi;
	uint32_t proto;
	struct mbuf *top, **mp, *m;
	int tlen;
	int error = 0;
	size_t mlen;

	if (uio->uio_resid < sizeof(*th) + sizeof(uint32_t) ||
	    uio->uio_resid > MCLBYTES)
		return (EMSGSIZE);

	tlen = uio->uio_resid;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);
	mlen = MHLEN;
	if (uio->uio_resid > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if (!(m->m_flags & M_EXT)) {
			m_free(m);
			return (ENOBUFS);
		}
		mlen = MCLBYTES;
	}

	top = NULL;
	mp = &top;

	while (error == 0 && uio->uio_resid > 0) {
		m->m_len = ulmin(mlen, uio->uio_resid);
		error = uiomove(mtod (m, caddr_t), m->m_len, uio);
		*mp = m;
		mp = &m->m_next;
		if (error == 0 && uio->uio_resid > 0) {
			MGET(m, M_DONTWAIT, MT_DATA);
			if (m == NULL) {
				error = ENOBUFS;
				break;
			}
			mlen = MLEN;
			if (uio->uio_resid >= MINCLSIZE) {
				MCLGET(m, M_DONTWAIT);
				if (!(m->m_flags & M_EXT)) {
					error = ENOBUFS;
					m_free(m);
					break;
				}
				mlen = MCLBYTES;
			}
		}
	}

	if (error) {
		m_freem(top);
		return (error);
	}

	top->m_pkthdr.len = tlen;

	/* Find the interface */
	th = mtod(top, struct pppx_hdr *);
	m_adj(top, sizeof(struct pppx_hdr));

	pxi = pppx_if_find(pxd, th->pppx_id, th->pppx_proto);
	if (pxi == NULL) {
		m_freem(top);
		return (EINVAL);
	}
	top->m_pkthdr.ph_ifidx = pxi->pxi_if.if_index;

#if NBPFILTER > 0
	if (pxi->pxi_if.if_bpf)
		bpf_mtap(pxi->pxi_if.if_bpf, top, BPF_DIRECTION_IN);
#endif
	/* strip the tunnel header */
	proto = ntohl(*(uint32_t *)(th + 1));
	m_adj(top, sizeof(uint32_t));

	NET_LOCK();

	switch (proto) {
	case AF_INET:
		ipv4_input(&pxi->pxi_if, top, NULL);
		break;
#ifdef INET6
	case AF_INET6:
		ipv6_input(&pxi->pxi_if, top, NULL);
		break;
#endif
	default:
		m_freem(top);
		error = EAFNOSUPPORT;
		break;
	}

	NET_UNLOCK();

	pppx_if_rele(pxi);

	return (error);
}

int
pppxioctl(dev_t dev, u_long cmd, caddr_t addr, int flags, struct proc *p)
{
	struct pppx_dev *pxd = pppx_dev2pxd(dev);
	int error = 0;

	switch (cmd) {
	case PIPEXASESSION:
		error = pppx_add_session(pxd,
		    (struct pipex_session_req *)addr);
		break;

	case PIPEXDSESSION:
		error = pppx_del_session(pxd,
		    (struct pipex_session_close_req *)addr);
		break;

	case PIPEXSIFDESCR:
		error = pppx_set_session_descr(pxd,
		    (struct pipex_session_descr_req *)addr);
		break;

	case FIONREAD:
		*(int *)addr = mq_hdatalen(&pxd->pxd_svcq);
		break;

	default:
		error = pipex_ioctl(pxd, cmd, addr);
		break;
	}

	return (error);
}

int
pppxkqfilter(dev_t dev, struct knote *kn)
{
	struct pppx_dev *pxd = pppx_dev2pxd(dev);
	struct klist *klist;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		klist = &pxd->pxd_rklist;
		kn->kn_fop = &pppx_rd_filtops;
		break;
	case EVFILT_WRITE:
		klist = &pxd->pxd_wklist;
		kn->kn_fop = &pppx_wr_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = pxd;

	klist_insert(klist, kn);

	return (0);
}

void
filt_pppx_rdetach(struct knote *kn)
{
	struct pppx_dev *pxd = kn->kn_hook;

	klist_remove(&pxd->pxd_rklist, kn);
}

int
filt_pppx_read(struct knote *kn, long hint)
{
	struct pppx_dev *pxd = kn->kn_hook;

	MUTEX_ASSERT_LOCKED(&pxd->pxd_mtx);

	kn->kn_data = mq_hdatalen(&pxd->pxd_svcq);

	return (kn->kn_data > 0);
}

void
filt_pppx_wdetach(struct knote *kn)
{
	struct pppx_dev *pxd = kn->kn_hook;

	klist_remove(&pxd->pxd_wklist, kn);
}

int
filt_pppx_write(struct knote *kn, long hint)
{
	/* We're always ready to accept a write. */
	return (1);
}

int
filt_pppx_modify(struct kevent *kev, struct knote *kn)
{
	struct pppx_dev *pxd = kn->kn_hook;
	int active;

	mtx_enter(&pxd->pxd_mtx);
	active = knote_modify(kev, kn);
	mtx_leave(&pxd->pxd_mtx);

	return (active);
}

int
filt_pppx_process(struct knote *kn, struct kevent *kev)
{
	struct pppx_dev *pxd = kn->kn_hook;
	int active;

	mtx_enter(&pxd->pxd_mtx);
	active = knote_process(kn, kev);
	mtx_leave(&pxd->pxd_mtx);

	return (active);
}

int
pppxclose(dev_t dev, int flags, int mode, struct proc *p)
{
	struct pppx_dev *pxd;
	struct pppx_if	*pxi;

	pxd = pppx_dev_lookup(dev);

	while ((pxi = LIST_FIRST(&pxd->pxd_pxis))) {
		pxi->pxi_ready = 0;
		pppx_if_destroy(pxd, pxi);
	}

	LIST_REMOVE(pxd, pxd_entry);

	mq_purge(&pxd->pxd_svcq);

	klist_free(&pxd->pxd_rklist);
	klist_free(&pxd->pxd_wklist);

	free(pxd, M_DEVBUF, sizeof(*pxd));

	return (0);
}

int
pppx_if_next_unit(void)
{
	struct pppx_if *pxi;
	int unit = 0;

	/* this is safe without splnet since we're not modifying it */
	do {
		int found = 0;
		RBT_FOREACH(pxi, pppx_ifs, &pppx_ifs) {
			if (pxi->pxi_unit == unit) {
				found = 1;
				break;
			}
		}

		if (found == 0)
			break;
		unit++;
	} while (unit > 0);

	return (unit);
}

struct pppx_if *
pppx_if_find_locked(struct pppx_dev *pxd, int session_id, int protocol)
{
	struct pppx_if_key key;
	struct pppx_if *pxi;

	memset(&key, 0, sizeof(key));
	key.pxik_session_id = session_id;
	key.pxik_protocol = protocol;

	pxi = RBT_FIND(pppx_ifs, &pppx_ifs, (struct pppx_if *)&key);
	if (pxi && pxi->pxi_ready == 0)
		pxi = NULL;

	return pxi;
}

static inline struct pppx_if *
pppx_if_find(struct pppx_dev *pxd, int session_id, int protocol)
{
	struct pppx_if *pxi;

	if ((pxi = pppx_if_find_locked(pxd, session_id, protocol)))
		refcnt_take(&pxi->pxi_refcnt);
	
	return pxi;
}

static inline void
pppx_if_rele(struct pppx_if *pxi)
{
	refcnt_rele_wake(&pxi->pxi_refcnt);
}

int
pppx_add_session(struct pppx_dev *pxd, struct pipex_session_req *req)
{
	struct pppx_if *pxi;
	struct pipex_session *session;
	struct ifnet *ifp;
	int unit, error = 0;
	struct in_ifaddr *ia;
	struct sockaddr_in ifaddr;

	/*
	 * XXX: As long as `session' is allocated as part of a `pxi'
	 *	it isn't possible to free it separately.  So disallow
	 *	the timeout feature until this is fixed.
	 */
	if (req->pr_timeout_sec != 0)
		return (EINVAL);

	error = pipex_init_session(&session, req);
	if (error)
		return (error);

	pxi = pool_get(&pppx_if_pl, PR_WAITOK | PR_ZERO);
	ifp = &pxi->pxi_if;

	pxi->pxi_session = session;

	/* try to set the interface up */
	unit = pppx_if_next_unit();
	if (unit < 0) {
		error = ENOMEM;
		goto out;
	}

	refcnt_init(&pxi->pxi_refcnt);
	pxi->pxi_unit = unit;
	pxi->pxi_key.pxik_session_id = req->pr_session_id;
	pxi->pxi_key.pxik_protocol = req->pr_protocol;
	pxi->pxi_dev = pxd;

	if (RBT_INSERT(pppx_ifs, &pppx_ifs, pxi) != NULL) {
		error = EADDRINUSE;
		goto out;
	}
	LIST_INSERT_HEAD(&pxd->pxd_pxis, pxi, pxi_list);

	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d", "pppx", unit);
	ifp->if_mtu = req->pr_peer_mru;	/* XXX */
	ifp->if_flags = IFF_POINTOPOINT | IFF_MULTICAST | IFF_UP;
	ifp->if_xflags = IFXF_CLONED | IFXF_MPSAFE;
	ifp->if_qstart = pppx_if_qstart;
	ifp->if_output = pppx_if_output;
	ifp->if_ioctl = pppx_if_ioctl;
	ifp->if_rtrequest = p2p_rtrequest;
	ifp->if_type = IFT_PPP;
	ifp->if_softc = pxi;
	/* ifp->if_rdomain = req->pr_rdomain; */
	if_counters_alloc(ifp);

	if_attach(ifp);

	NET_LOCK();
	if_addgroup(ifp, "pppx");
	if_alloc_sadl(ifp);
	NET_UNLOCK();

#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_LOOP, sizeof(u_int32_t));
#endif

	/* XXX ipv6 support?  how does the caller indicate it wants ipv6
	 * instead of ipv4?
	 */
	memset(&ifaddr, 0, sizeof(ifaddr));
	ifaddr.sin_family = AF_INET;
	ifaddr.sin_len = sizeof(ifaddr);
	ifaddr.sin_addr = req->pr_ip_srcaddr;

	ia = malloc(sizeof (*ia), M_IFADDR, M_WAITOK | M_ZERO);
	refcnt_init_trace(&ia->ia_ifa.ifa_refcnt, DT_REFCNT_IDX_IFADDR);

	ia->ia_addr.sin_family = AF_INET;
	ia->ia_addr.sin_len = sizeof(struct sockaddr_in);
	ia->ia_addr.sin_addr = req->pr_ip_srcaddr;

	ia->ia_dstaddr.sin_family = AF_INET;
	ia->ia_dstaddr.sin_len = sizeof(struct sockaddr_in);
	ia->ia_dstaddr.sin_addr = req->pr_ip_address;

	ia->ia_sockmask.sin_family = AF_INET;
	ia->ia_sockmask.sin_len = sizeof(struct sockaddr_in);
	ia->ia_sockmask.sin_addr = req->pr_ip_netmask;

	ia->ia_ifa.ifa_addr = sintosa(&ia->ia_addr);
	ia->ia_ifa.ifa_dstaddr = sintosa(&ia->ia_dstaddr);
	ia->ia_ifa.ifa_netmask = sintosa(&ia->ia_sockmask);
	ia->ia_ifa.ifa_ifp = ifp;

	ia->ia_netmask = ia->ia_sockmask.sin_addr.s_addr;

	NET_LOCK();
	error = in_ifinit(ifp, ia, &ifaddr, 1);
	if (error) {
		printf("pppx: unable to set addresses for %s, error=%d\n",
		    ifp->if_xname, error);
	} else {
		if_addrhooks_run(ifp);
	}
	NET_UNLOCK();

	error = pipex_link_session(session, ifp, pxd);
	if (error)
		goto detach;

	NET_LOCK();
	SET(ifp->if_flags, IFF_RUNNING);
	NET_UNLOCK();
	pxi->pxi_ready = 1;

	return (error);

detach:
	if_detach(ifp);

	if (RBT_REMOVE(pppx_ifs, &pppx_ifs, pxi) == NULL)
		panic("%s: inconsistent RB tree", __func__);
	LIST_REMOVE(pxi, pxi_list);
out:
	pool_put(&pppx_if_pl, pxi);
	pipex_rele_session(session);

	return (error);
}

int
pppx_del_session(struct pppx_dev *pxd, struct pipex_session_close_req *req)
{
	struct pppx_if *pxi;

	pxi = pppx_if_find_locked(pxd, req->pcr_session_id, req->pcr_protocol);
	if (pxi == NULL)
		return (EINVAL);

	pxi->pxi_ready = 0;
	pipex_export_session_stats(pxi->pxi_session, &req->pcr_stat);
	pppx_if_destroy(pxd, pxi);
	return (0);
}

int
pppx_set_session_descr(struct pppx_dev *pxd,
    struct pipex_session_descr_req *req)
{
	struct pppx_if *pxi;

	pxi = pppx_if_find(pxd, req->pdr_session_id, req->pdr_protocol);
	if (pxi == NULL)
		return (EINVAL);

	(void)memset(pxi->pxi_if.if_description, 0, IFDESCRSIZE);
	strlcpy(pxi->pxi_if.if_description, req->pdr_descr, IFDESCRSIZE);

	pppx_if_rele(pxi);

	return (0);
}

void
pppx_if_destroy(struct pppx_dev *pxd, struct pppx_if *pxi)
{
	struct ifnet *ifp;
	struct pipex_session *session;

	session = pxi->pxi_session;
	ifp = &pxi->pxi_if;

	refcnt_finalize(&pxi->pxi_refcnt, "pxifinal");

	NET_LOCK();
	CLR(ifp->if_flags, IFF_RUNNING);
	NET_UNLOCK();

	pipex_unlink_session(session);
	if_detach(ifp);

	pipex_rele_session(session);
	if (RBT_REMOVE(pppx_ifs, &pppx_ifs, pxi) == NULL)
		panic("%s: inconsistent RB tree", __func__);
	LIST_REMOVE(pxi, pxi_list);

	pool_put(&pppx_if_pl, pxi);
}

void
pppx_if_qstart(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct pppx_if *pxi = (struct pppx_if *)ifp->if_softc;
	struct mbuf *m;
	int proto;

	while ((m = ifq_dequeue(ifq)) != NULL) {
		proto = *mtod(m, int *);
		m_adj(m, sizeof(proto));

		pipex_ppp_output(m, pxi->pxi_session, proto);
	}
}

int
pppx_if_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	struct pppx_if *pxi = (struct pppx_if *)ifp->if_softc;
	struct pppx_hdr *th;
	int error = 0;
	int pipex_enable_local, proto;

	pipex_enable_local = atomic_load_int(&pipex_enable);

	NET_ASSERT_LOCKED();

	if (!ISSET(ifp->if_flags, IFF_RUNNING)) {
		m_freem(m);
		error = ENETDOWN;
		goto out;
	}

#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap_af(ifp->if_bpf, dst->sa_family, m, BPF_DIRECTION_OUT);
#endif
	if (pipex_enable_local) {
		switch (dst->sa_family) {
#ifdef INET6
		case AF_INET6:
			proto = PPP_IPV6;
			break;
#endif
		case AF_INET:
			proto = PPP_IP;
			break;
		default:
			m_freem(m);
			error = EPFNOSUPPORT;
			goto out;
		}
	} else
		proto = htonl(dst->sa_family);

	M_PREPEND(m, sizeof(int), M_DONTWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto out;
	}
	*mtod(m, int *) = proto;

	if (pipex_enable_local)
		error = if_enqueue(ifp, m);
	else {
		M_PREPEND(m, sizeof(struct pppx_hdr), M_DONTWAIT);
		if (m == NULL) {
			error = ENOBUFS;
			goto out;
		}
		th = mtod(m, struct pppx_hdr *);
		th->pppx_proto = 0;	/* not used */
		th->pppx_id = pxi->pxi_session->ppp_id;
		error = mq_enqueue(&pxi->pxi_dev->pxd_svcq, m);
		if (error == 0) {
			if (pxi->pxi_dev->pxd_waiting) {
				wakeup((caddr_t)pxi->pxi_dev);
				pxi->pxi_dev->pxd_waiting = 0;
			}
			knote(&pxi->pxi_dev->pxd_rklist, 0);
		}
	}

out:
	if (error)
		counters_inc(ifp->if_counters, ifc_oerrors);
	return (error);
}

int
pppx_if_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr)
{
	struct pppx_if *pxi = (struct pppx_if *)ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)addr;
	int error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		break;

	case SIOCSIFFLAGS:
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	case SIOCSIFMTU:
		if (ifr->ifr_mtu < 512 ||
		    ifr->ifr_mtu > pxi->pxi_session->peer_mru)
			error = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		break;

	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

RBT_GENERATE(pppx_ifs, pppx_if, pxi_entry, pppx_if_cmp);

/*
 * Locks used to protect struct members and global data
 *       I       immutable after creation
 *       K       kernel lock
 *       N       net lock
 *       m       sc_mtx
 */

struct pppac_softc {
	struct ifnet	sc_if;
	dev_t		sc_dev;		/* [I] */
	int		sc_ready;	/* [K] */
	LIST_ENTRY(pppac_softc)
			sc_entry;	/* [K] */

	struct mutex	sc_mtx;
	struct klist	sc_rklist;	/* [m] */
	struct klist	sc_wklist;	/* [m] */

	struct pipex_session
			*sc_multicast_session;

	struct mbuf_queue
			sc_mq;
};

LIST_HEAD(pppac_list, pppac_softc);	/* [K] */

static void	filt_pppac_rdetach(struct knote *);
static int	filt_pppac_read(struct knote *, long);
static int	filt_pppac_modify(struct kevent *, struct knote *);
static int	filt_pppac_process(struct knote *, struct kevent *);

static const struct filterops pppac_rd_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_pppac_rdetach,
	.f_event	= filt_pppac_read,
	.f_modify	= filt_pppac_modify,
	.f_process	= filt_pppac_process,
};

static void	filt_pppac_wdetach(struct knote *);
static int	filt_pppac_write(struct knote *, long);

static const struct filterops pppac_wr_filtops = {
	.f_flags	= FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach	= NULL,
	.f_detach	= filt_pppac_wdetach,
	.f_event	= filt_pppac_write,
	.f_modify	= filt_pppac_modify,
	.f_process	= filt_pppac_process,
};

static struct pppac_list pppac_devs = LIST_HEAD_INITIALIZER(pppac_devs);

static int	pppac_ioctl(struct ifnet *, u_long, caddr_t);

static int	pppac_add_session(struct pppac_softc *,
		    struct pipex_session_req *);
static int	pppac_del_session(struct pppac_softc *,
		    struct pipex_session_close_req *);
static int	pppac_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		    struct rtentry *);
static void	pppac_qstart(struct ifqueue *);

static inline struct pppac_softc *
pppac_lookup(dev_t dev)
{
	struct pppac_softc *sc;

	LIST_FOREACH(sc, &pppac_devs, sc_entry) {
		if (sc->sc_dev == dev) {
			if (sc->sc_ready == 0)
				break;

			return (sc);
		}
	}

	return (NULL);
}

void
pppacattach(int n)
{
	pipex_init(); /* to be sure, to be sure */
}

int
pppacopen(dev_t dev, int flags, int mode, struct proc *p)
{
	struct pppac_softc *sc, *tmp;
	struct ifnet *ifp;
	struct pipex_session *session;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK|M_ZERO);
	sc->sc_dev = dev;
	LIST_FOREACH(tmp, &pppac_devs, sc_entry) {
		if (tmp->sc_dev == dev) {
			free(sc, M_DEVBUF, sizeof(*sc));
			return (EBUSY);
		}
	}
	LIST_INSERT_HEAD(&pppac_devs, sc, sc_entry);

	/* virtual pipex_session entry for multicast */
	session = pool_get(&pipex_session_pool, PR_WAITOK | PR_ZERO);
	session->flags |= PIPEX_SFLAGS_MULTICAST;
	session->ownersc = sc;
	sc->sc_multicast_session = session;

	mtx_init(&sc->sc_mtx, IPL_SOFTNET);
	klist_init_mutex(&sc->sc_rklist, &sc->sc_mtx);
	klist_init_mutex(&sc->sc_wklist, &sc->sc_mtx);
	mq_init(&sc->sc_mq, IFQ_MAXLEN, IPL_SOFTNET);

	ifp = &sc->sc_if;
	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "pppac%u", minor(dev));

	ifp->if_softc = sc;
	ifp->if_type = IFT_L3IPVLAN;
	ifp->if_hdrlen = sizeof(uint32_t); /* for BPF */
	ifp->if_mtu = MAXMCLBYTES - sizeof(uint32_t);
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST;
	ifp->if_xflags = IFXF_CLONED | IFXF_MPSAFE;
	ifp->if_rtrequest = p2p_rtrequest; /* XXX */
	ifp->if_output = pppac_output;
	ifp->if_qstart = pppac_qstart;
	ifp->if_ioctl = pppac_ioctl;

	if_counters_alloc(ifp);
	if_attach(ifp);
	if_alloc_sadl(ifp);

#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_LOOP, sizeof(uint32_t));
#endif

	sc->sc_ready = 1;

	return (0);
}

int
pppacread(dev_t dev, struct uio *uio, int ioflag)
{
	struct pppac_softc *sc = pppac_lookup(dev);
	struct ifnet *ifp = &sc->sc_if;
	struct mbuf *m0, *m;
	int error = 0;
	size_t len;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return (EHOSTDOWN);

	m0 = mq_dequeue(&sc->sc_mq);
	if (m0 == NULL) {
		if (ISSET(ioflag, IO_NDELAY))
			return (EWOULDBLOCK);

		do {
			error = tsleep_nsec(sc, (PZERO + 1)|PCATCH,
			    "pppacrd", INFSLP);
			if (error != 0)
				return (error);

			m0 = mq_dequeue(&sc->sc_mq);
		} while (m0 == NULL);
	}

	m = m0;
	while (uio->uio_resid > 0) {
		len = ulmin(uio->uio_resid, m->m_len);
		if (len != 0) {
			error = uiomove(mtod(m, caddr_t), len, uio);
			if (error != 0)
				break;
		}

		m = m->m_next;
		if (m == NULL)
			break;
	}
	m_freem(m0);

	return (error);
}

int
pppacwrite(dev_t dev, struct uio *uio, int ioflag)
{
	struct pppac_softc *sc = pppac_lookup(dev);
	struct ifnet *ifp = &sc->sc_if;
	uint32_t proto;
	int error;
	struct mbuf *m;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return (EHOSTDOWN);

	if (uio->uio_resid < ifp->if_hdrlen || uio->uio_resid > MAXMCLBYTES)
		return (EMSGSIZE);

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (ENOMEM);

	if (uio->uio_resid > MHLEN) {
		m_clget(m, M_WAITOK, uio->uio_resid);
		if (!ISSET(m->m_flags, M_EXT)) {
			m_free(m);
			return (ENOMEM);
		}
	}

	m->m_pkthdr.len = m->m_len = uio->uio_resid;

	error = uiomove(mtod(m, void *), m->m_len, uio);
	if (error != 0) {
		m_freem(m);
		return (error);
	}

#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_IN);
#endif

	/* strip the tunnel header */
	proto = ntohl(*mtod(m, uint32_t *));
	m_adj(m, sizeof(uint32_t));

	m->m_flags &= ~(M_MCAST|M_BCAST);
	m->m_pkthdr.ph_ifidx = ifp->if_index;
	m->m_pkthdr.ph_rtableid = ifp->if_rdomain;

#if NPF > 0
	pf_pkt_addr_changed(m);
#endif

	counters_pkt(ifp->if_counters,
	    ifc_ipackets, ifc_ibytes, m->m_pkthdr.len);

	NET_LOCK();

	switch (proto) {
	case AF_INET:
		ipv4_input(ifp, m, NULL);
		break;
#ifdef INET6
	case AF_INET6:
		ipv6_input(ifp, m, NULL);
		break;
#endif
	default:
		m_freem(m);
		error = EAFNOSUPPORT;
		break;
	}

	NET_UNLOCK();

	return (error);
}

int
pppacioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct pppac_softc *sc = pppac_lookup(dev);
	int error = 0;

	switch (cmd) {
	case FIONREAD:
		*(int *)data = mq_hdatalen(&sc->sc_mq);
		break;

	case PIPEXASESSION:
		error = pppac_add_session(sc, (struct pipex_session_req *)data);
		break;
	case PIPEXDSESSION:
		error = pppac_del_session(sc,
		    (struct pipex_session_close_req *)data);
		break;
	default:
		error = pipex_ioctl(sc, cmd, data);
		break;
	}

	return (error);
}

int
pppackqfilter(dev_t dev, struct knote *kn)
{
	struct pppac_softc *sc = pppac_lookup(dev);
	struct klist *klist;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		klist = &sc->sc_rklist;
		kn->kn_fop = &pppac_rd_filtops;
		break;
	case EVFILT_WRITE:
		klist = &sc->sc_wklist;
		kn->kn_fop = &pppac_wr_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = sc;

	klist_insert(klist, kn);

	return (0);
}

static void
filt_pppac_rdetach(struct knote *kn)
{
	struct pppac_softc *sc = kn->kn_hook;

	klist_remove(&sc->sc_rklist, kn);
}

static int
filt_pppac_read(struct knote *kn, long hint)
{
	struct pppac_softc *sc = kn->kn_hook;

	MUTEX_ASSERT_LOCKED(&sc->sc_mtx);

	kn->kn_data = mq_hdatalen(&sc->sc_mq);

	return (kn->kn_data > 0);
}

static void
filt_pppac_wdetach(struct knote *kn)
{
	struct pppac_softc *sc = kn->kn_hook;

	klist_remove(&sc->sc_wklist, kn);
}

static int
filt_pppac_write(struct knote *kn, long hint)
{
	/* We're always ready to accept a write. */
	return (1);
}

static int
filt_pppac_modify(struct kevent *kev, struct knote *kn)
{
	struct pppac_softc *sc = kn->kn_hook;
	int active;

	mtx_enter(&sc->sc_mtx);
	active = knote_modify(kev, kn);
	mtx_leave(&sc->sc_mtx);

	return (active);
}

static int
filt_pppac_process(struct knote *kn, struct kevent *kev)
{
	struct pppac_softc *sc = kn->kn_hook;
	int active;

	mtx_enter(&sc->sc_mtx);
	active = knote_process(kn, kev);
	mtx_leave(&sc->sc_mtx);

	return (active);
}

int
pppacclose(dev_t dev, int flags, int mode, struct proc *p)
{
	struct pppac_softc *sc = pppac_lookup(dev);
	struct ifnet *ifp = &sc->sc_if;

	sc->sc_ready = 0;

	NET_LOCK();
	CLR(ifp->if_flags, IFF_RUNNING);
	NET_UNLOCK();

	if_detach(ifp);

	klist_free(&sc->sc_rklist);
	klist_free(&sc->sc_wklist);

	pool_put(&pipex_session_pool, sc->sc_multicast_session);
	pipex_destroy_all_sessions(sc);

	LIST_REMOVE(sc, sc_entry);
	free(sc, M_DEVBUF, sizeof(*sc));

	return (0);
}

static int
pppac_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	/* struct ifreq *ifr = (struct ifreq *)data; */
	int error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		SET(ifp->if_flags, IFF_UP); /* XXX cry cry */
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP))
			SET(ifp->if_flags, IFF_RUNNING);
		else
			CLR(ifp->if_flags, IFF_RUNNING);
		break;
	case SIOCSIFMTU:
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* XXX */
		break;

	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

static int
pppac_add_session(struct pppac_softc *sc, struct pipex_session_req *req)
{
	int error;
	struct pipex_session *session;

	error = pipex_init_session(&session, req);
	if (error != 0)
		return (error);
	error = pipex_link_session(session, &sc->sc_if, sc);
	if (error != 0)
		pipex_rele_session(session);

	return (error);
}

static int
pppac_del_session(struct pppac_softc *sc, struct pipex_session_close_req *req)
{
	struct pipex_session *session;

	mtx_enter(&pipex_list_mtx);

	session = pipex_lookup_by_session_id_locked(req->pcr_protocol,
	    req->pcr_session_id);
	if (session == NULL || session->ownersc != sc) {
		mtx_leave(&pipex_list_mtx);
		return (EINVAL);
	}
	pipex_unlink_session_locked(session);

	mtx_leave(&pipex_list_mtx);

	pipex_export_session_stats(session, &req->psr_stat);
	pipex_rele_session(session);

	return (0);
}

static int
pppac_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rt)
{
	int error;

	if (!ISSET(ifp->if_flags, IFF_RUNNING)) {
		error = EHOSTDOWN;
		goto drop;
	}

	switch (dst->sa_family) {
	case AF_INET:
#ifdef INET6
	case AF_INET6:
#endif
		break;
	default:
		error = EAFNOSUPPORT;
		goto drop;
	}

	m->m_pkthdr.ph_family = dst->sa_family;

	return (if_enqueue(ifp, m));

drop:
	m_freem(m);
	return (error);
}

static void
pppac_qstart(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct pppac_softc *sc = ifp->if_softc;
	struct mbuf *m, *m0;
	struct pipex_session *session;
	struct ip ip;
	int rv;

	while ((m = ifq_dequeue(ifq)) != NULL) {
#if NBPFILTER > 0
		if (ifp->if_bpf) {
			bpf_mtap_af(ifp->if_bpf, m->m_pkthdr.ph_family, m,
			    BPF_DIRECTION_OUT);
		}
#endif

		switch (m->m_pkthdr.ph_family) {
		case AF_INET:
			if (m->m_pkthdr.len < sizeof(struct ip))
				goto bad;
			m_copydata(m, 0, sizeof(struct ip), &ip);
			if (IN_MULTICAST(ip.ip_dst.s_addr)) {
				/* pass a copy to pipex */
				m0 = m_copym(m, 0, M_COPYALL, M_NOWAIT);
				if (m0 != NULL)
					pipex_ip_output(m0,
					    sc->sc_multicast_session);
				else
					goto bad;
			} else {
				session = pipex_lookup_by_ip_address(ip.ip_dst);
				if (session != NULL) {
					pipex_ip_output(m, session);
					pipex_rele_session(session);
					m = NULL;
				}
			}
			break;
		}
		if (m == NULL)	/* handled by pipex */
			continue;

		m = m_prepend(m, sizeof(uint32_t), M_DONTWAIT);
		if (m == NULL)
			goto bad;
		*mtod(m, uint32_t *) = htonl(m->m_pkthdr.ph_family);

		rv = mq_enqueue(&sc->sc_mq, m);
		if (rv == 1)
			counters_inc(ifp->if_counters, ifc_collisions);
		continue;
bad:
		counters_inc(ifp->if_counters, ifc_oerrors);
		if (m != NULL)
			m_freem(m);
		continue;
	}

	if (!mq_empty(&sc->sc_mq)) {
		wakeup(sc);
		knote(&sc->sc_rklist, 0);
	}
}
