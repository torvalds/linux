/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Chelsio Communications, Inc.
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
#include <sys/eventhandler.h>
#include <sys/lock.h>
#include <sys/types.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sx.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_clone.h>
#include <net/if_types.h>

#include "common/common.h"
#include "common/t4_msg.h"
#include "common/t4_regs.h"
#include "t4_ioctl.h"

/*
 * Locking notes
 * =============
 *
 * An interface cloner is registered during mod_load and it can be used to
 * create or destroy the tracing ifnet for an adapter at any time.  It is
 * possible for the cloned interface to outlive the adapter (adapter disappears
 * in t4_detach but the tracing ifnet may live till mod_unload when removal of
 * the cloner finally destroys any remaining cloned interfaces).  When tracing
 * filters are active, this ifnet is also receiving data.  There are potential
 * bad races between ifnet create, ifnet destroy, ifnet rx, ifnet ioctl,
 * cxgbe_detach/t4_detach, mod_unload.
 *
 * a) The driver selects an iq for tracing (sc->traceq) inside a synch op.  The
 *    iq is destroyed inside a synch op too (and sc->traceq updated).
 * b) The cloner looks for an adapter that matches the name of the ifnet it's
 *    been asked to create, starts a synch op on that adapter, and proceeds only
 *    if the adapter has a tracing iq.
 * c) The cloned ifnet and the adapter are coupled to each other via
 *    ifp->if_softc and sc->ifp.  These can be modified only with the global
 *    t4_trace_lock sx as well as the sc->ifp_lock mutex held.  Holding either
 *    of these will prevent any change.
 *
 * The order in which all the locks involved should be acquired are:
 * t4_list_lock
 * adapter lock
 * (begin synch op and let go of the above two)
 * t4_trace_lock
 * sc->ifp_lock
 */

static struct sx t4_trace_lock;
static const char *t4_cloner_name = "tXnex";
static struct if_clone *t4_cloner;

/* tracer ifnet routines.  mostly no-ops. */
static void tracer_init(void *);
static int tracer_ioctl(struct ifnet *, unsigned long, caddr_t);
static int tracer_transmit(struct ifnet *, struct mbuf *);
static void tracer_qflush(struct ifnet *);
static int tracer_media_change(struct ifnet *);
static void tracer_media_status(struct ifnet *, struct ifmediareq *);

/* match name (request/response) */
struct match_rr {
	const char *name;
	int lock;	/* set to 1 to returned sc locked. */
	struct adapter *sc;
	int rc;
};

static void
match_name(struct adapter *sc, void *arg)
{
	struct match_rr *mrr = arg;

	if (strcmp(device_get_nameunit(sc->dev), mrr->name) != 0)
		return;

	KASSERT(mrr->sc == NULL, ("%s: multiple matches (%p, %p) for %s",
	    __func__, mrr->sc, sc, mrr->name));

	mrr->sc = sc;
	if (mrr->lock)
		mrr->rc = begin_synchronized_op(mrr->sc, NULL, 0, "t4clon");
	else
		mrr->rc = 0;
}

static int
t4_cloner_match(struct if_clone *ifc, const char *name)
{

	if (strncmp(name, "t4nex", 5) != 0 &&
	    strncmp(name, "t5nex", 5) != 0 &&
	    strncmp(name, "t6nex", 5) != 0)
		return (0);
	if (name[5] < '0' || name[5] > '9')
		return (0);
	return (1);
}

static int
t4_cloner_create(struct if_clone *ifc, char *name, size_t len, caddr_t params)
{
	struct match_rr mrr;
	struct adapter *sc;
	struct ifnet *ifp;
	int rc, unit;
	const uint8_t lla[ETHER_ADDR_LEN] = {0, 0, 0, 0, 0, 0};

	mrr.name = name;
	mrr.lock = 1;
	mrr.sc = NULL;
	mrr.rc = ENOENT;
	t4_iterate(match_name, &mrr);

	if (mrr.rc != 0)
		return (mrr.rc);
	sc = mrr.sc;

	KASSERT(sc != NULL, ("%s: name (%s) matched but softc is NULL",
	    __func__, name));
	ASSERT_SYNCHRONIZED_OP(sc);

	sx_xlock(&t4_trace_lock);

	if (sc->ifp != NULL) {
		rc = EEXIST;
		goto done;
	}
	if (sc->traceq < 0) {
		rc = EAGAIN;
		goto done;
	}


	unit = -1;
	rc = ifc_alloc_unit(ifc, &unit);
	if (rc != 0)
		goto done;

	ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		ifc_free_unit(ifc, unit);
		rc = ENOMEM;
		goto done;
	}

	/* Note that if_xname is not <if_dname><if_dunit>. */
	strlcpy(ifp->if_xname, name, sizeof(ifp->if_xname));
	ifp->if_dname = t4_cloner_name;
	ifp->if_dunit = unit;
	ifp->if_init = tracer_init;
	ifp->if_flags = IFF_SIMPLEX | IFF_DRV_RUNNING;
	ifp->if_ioctl = tracer_ioctl;
	ifp->if_transmit = tracer_transmit;
	ifp->if_qflush = tracer_qflush;
	ifp->if_capabilities = IFCAP_JUMBO_MTU | IFCAP_VLAN_MTU;
	ifmedia_init(&sc->media, IFM_IMASK, tracer_media_change,
	    tracer_media_status);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_FDX | IFM_NONE, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_FDX | IFM_NONE);
	ether_ifattach(ifp, lla);

	mtx_lock(&sc->ifp_lock);
	ifp->if_softc = sc;
	sc->ifp = ifp;
	mtx_unlock(&sc->ifp_lock);
done:
	sx_xunlock(&t4_trace_lock);
	end_synchronized_op(sc, 0);
	return (rc);
}

static int
t4_cloner_destroy(struct if_clone *ifc, struct ifnet *ifp)
{
	struct adapter *sc;
	int unit = ifp->if_dunit;

	sx_xlock(&t4_trace_lock);
	sc = ifp->if_softc;
	if (sc != NULL) {
		mtx_lock(&sc->ifp_lock);
		sc->ifp = NULL;
		ifp->if_softc = NULL;
		mtx_unlock(&sc->ifp_lock);
		ifmedia_removeall(&sc->media);
	}
	ether_ifdetach(ifp);
	if_free(ifp);
	ifc_free_unit(ifc, unit);
	sx_xunlock(&t4_trace_lock);

	return (0);
}

void
t4_tracer_modload()
{

	sx_init(&t4_trace_lock, "T4/T5 tracer lock");
	t4_cloner = if_clone_advanced(t4_cloner_name, 0, t4_cloner_match,
	    t4_cloner_create, t4_cloner_destroy);
}

void
t4_tracer_modunload()
{

	if (t4_cloner != NULL) {
		/*
		 * The module is being unloaded so the nexus drivers have
		 * detached.  The tracing interfaces can not outlive the nexus
		 * (ifp->if_softc is the nexus) and must have been destroyed
		 * already.  XXX: but if_clone is opaque to us and we can't
		 * assert LIST_EMPTY(&t4_cloner->ifc_iflist) at this time.
		 */
		if_clone_detach(t4_cloner);
	}
	sx_destroy(&t4_trace_lock);
}

void
t4_tracer_port_detach(struct adapter *sc)
{

	sx_xlock(&t4_trace_lock);
	if (sc->ifp != NULL) {
		mtx_lock(&sc->ifp_lock);
		sc->ifp->if_softc = NULL;
		sc->ifp = NULL;
		mtx_unlock(&sc->ifp_lock);
	}
	ifmedia_removeall(&sc->media);
	sx_xunlock(&t4_trace_lock);
}

int
t4_get_tracer(struct adapter *sc, struct t4_tracer *t)
{
	int rc, i, enabled;
	struct trace_params tp;

	if (t->idx >= NTRACE) {
		t->idx = 0xff;
		t->enabled = 0;
		t->valid = 0;
		return (0);
	}

	rc = begin_synchronized_op(sc, NULL, HOLD_LOCK | SLEEP_OK | INTR_OK,
	    "t4gett");
	if (rc)
		return (rc);

	for (i = t->idx; i < NTRACE; i++) {
		if (isset(&sc->tracer_valid, t->idx)) {
			t4_get_trace_filter(sc, &tp, i, &enabled);
			t->idx = i;
			t->enabled = enabled;
			t->valid = 1;
			memcpy(&t->tp.data[0], &tp.data[0], sizeof(t->tp.data));
			memcpy(&t->tp.mask[0], &tp.mask[0], sizeof(t->tp.mask));
			t->tp.snap_len = tp.snap_len;
			t->tp.min_len = tp.min_len;
			t->tp.skip_ofst = tp.skip_ofst;
			t->tp.skip_len = tp.skip_len;
			t->tp.invert = tp.invert;

			/* convert channel to port iff 0 <= port < 8. */
			if (tp.port < 4)
				t->tp.port = sc->chan_map[tp.port];
			else if (tp.port < 8)
				t->tp.port = sc->chan_map[tp.port - 4] + 4;
			else
				t->tp.port = tp.port;

			goto done;
		}
	}

	t->idx = 0xff;
	t->enabled = 0;
	t->valid = 0;
done:
	end_synchronized_op(sc, LOCK_HELD);

	return (rc);
}

int
t4_set_tracer(struct adapter *sc, struct t4_tracer *t)
{
	int rc;
	struct trace_params tp, *tpp;

	if (t->idx >= NTRACE)
		return (EINVAL);

	rc = begin_synchronized_op(sc, NULL, HOLD_LOCK | SLEEP_OK | INTR_OK,
	    "t4sett");
	if (rc)
		return (rc);

	/*
	 * If no tracing filter is specified this time then check if the filter
	 * at the index is valid anyway because it was set previously.  If so
	 * then this is a legitimate enable/disable operation.
	 */
	if (t->valid == 0) {
		if (isset(&sc->tracer_valid, t->idx))
			tpp = NULL;
		else
			rc = EINVAL;
		goto done;
	}

	if (t->tp.port > 19 || t->tp.snap_len > 9600 ||
	    t->tp.min_len > M_TFMINPKTSIZE || t->tp.skip_len > M_TFLENGTH ||
	    t->tp.skip_ofst > M_TFOFFSET) {
		rc = EINVAL;
		goto done;
	}

	memcpy(&tp.data[0], &t->tp.data[0], sizeof(tp.data));
	memcpy(&tp.mask[0], &t->tp.mask[0], sizeof(tp.mask));
	tp.snap_len = t->tp.snap_len;
	tp.min_len = t->tp.min_len;
	tp.skip_ofst = t->tp.skip_ofst;
	tp.skip_len = t->tp.skip_len;
	tp.invert = !!t->tp.invert;

	/* convert port to channel iff 0 <= port < 8. */
	if (t->tp.port < 4) {
		if (sc->port[t->tp.port] == NULL) {
			rc = EINVAL;
			goto done;
		}
		tp.port = sc->port[t->tp.port]->tx_chan;
	} else if (t->tp.port < 8) {
		if (sc->port[t->tp.port - 4] == NULL) {
			rc = EINVAL;
			goto done;
		}
		tp.port = sc->port[t->tp.port - 4]->tx_chan + 4;
	}
	tpp = &tp;
done:
	if (rc == 0) {
		rc = -t4_set_trace_filter(sc, tpp, t->idx, t->enabled);
		if (rc == 0) {
			if (t->enabled) {
				setbit(&sc->tracer_valid, t->idx);
				if (sc->tracer_enabled == 0) {
					t4_set_reg_field(sc, A_MPS_TRC_CFG,
					    F_TRCEN, F_TRCEN);
				}
				setbit(&sc->tracer_enabled, t->idx);
			} else {
				clrbit(&sc->tracer_enabled, t->idx);
				if (sc->tracer_enabled == 0) {
					t4_set_reg_field(sc, A_MPS_TRC_CFG,
					    F_TRCEN, 0);
				}
			}
		}
	}
	end_synchronized_op(sc, LOCK_HELD);

	return (rc);
}

int
t4_trace_pkt(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	struct ifnet *ifp;

	KASSERT(m != NULL, ("%s: no payload with opcode %02x", __func__,
	    rss->opcode));

	mtx_lock(&sc->ifp_lock);
	ifp = sc->ifp;
	if (sc->ifp) {
		m_adj(m, sizeof(struct cpl_trace_pkt));
		m->m_pkthdr.rcvif = ifp;
		ETHER_BPF_MTAP(ifp, m);
	}
	mtx_unlock(&sc->ifp_lock);
	m_freem(m);

	return (0);
}

int
t5_trace_pkt(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	struct ifnet *ifp;

	KASSERT(m != NULL, ("%s: no payload with opcode %02x", __func__,
	    rss->opcode));

	mtx_lock(&sc->ifp_lock);
	ifp = sc->ifp;
	if (ifp != NULL) {
		m_adj(m, sizeof(struct cpl_t5_trace_pkt));
		m->m_pkthdr.rcvif = ifp;
		ETHER_BPF_MTAP(ifp, m);
	}
	mtx_unlock(&sc->ifp_lock);
	m_freem(m);

	return (0);
}


static void
tracer_init(void *arg)
{

	return;
}

static int
tracer_ioctl(struct ifnet *ifp, unsigned long cmd, caddr_t data)
{
	int rc = 0;
	struct adapter *sc;
	struct ifreq *ifr = (struct ifreq *)data;

	switch (cmd) {
	case SIOCSIFMTU:
	case SIOCSIFFLAGS:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
	case SIOCSIFCAP:
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
	case SIOCGIFXMEDIA:
		sx_xlock(&t4_trace_lock);
		sc = ifp->if_softc;
		if (sc == NULL)
			rc = EIO;
		else
			rc = ifmedia_ioctl(ifp, ifr, &sc->media, cmd);
		sx_xunlock(&t4_trace_lock);
		break;
	default:
		rc = ether_ioctl(ifp, cmd, data);
	}

	return (rc);
}

static int
tracer_transmit(struct ifnet *ifp, struct mbuf *m)
{

	m_freem(m);
	return (0);
}

static void
tracer_qflush(struct ifnet *ifp)
{

	return;
}

static int
tracer_media_change(struct ifnet *ifp)
{

	return (EOPNOTSUPP);
}

static void
tracer_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{

	ifmr->ifm_status = IFM_AVALID | IFM_ACTIVE;

	return;
}
