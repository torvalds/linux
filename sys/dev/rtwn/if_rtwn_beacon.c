/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_beacon.h>
#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_tx.h>

#include <dev/rtwn/rtl8192c/r92c_reg.h>


static void
rtwn_reset_beacon_valid(struct rtwn_softc *sc, int id)
{

	KASSERT (id == 0 || id == 1, ("wrong port id %d\n", id));

	/* XXX cannot be cleared on RTL8188CE */
	rtwn_setbits_1_shift(sc, sc->bcn_status_reg[id],
	    R92C_TDECTRL_BCN_VALID, 0, 2);

	RTWN_DPRINTF(sc, RTWN_DEBUG_BEACON,
	    "%s: 'beacon valid' bit for vap %d was unset\n",
	    __func__, id);
}

static int
rtwn_check_beacon_valid(struct rtwn_softc *sc, int id)
{
	uint16_t reg;
	int ntries;

	if (id == RTWN_VAP_ID_INVALID)
		return (0);

	reg = sc->bcn_status_reg[id];
	for (ntries = 0; ntries < 10; ntries++) {
		if (rtwn_read_4(sc, reg) & R92C_TDECTRL_BCN_VALID) {
			RTWN_DPRINTF(sc, RTWN_DEBUG_BEACON,
			    "%s: beacon for vap %d was recognized\n",
			    __func__, id);
			break;
		}
		rtwn_delay(sc, sc->bcn_check_interval);
	}
	if (ntries == 10)
		return (ETIMEDOUT);

	return (0);
}

void
rtwn_switch_bcnq(struct rtwn_softc *sc, int id)
{

	if (sc->cur_bcnq_id != id) {
		/* Wait until any previous transmit completes. */
		(void) rtwn_check_beacon_valid(sc, sc->cur_bcnq_id);

		/* Change current port. */
		rtwn_beacon_select(sc, id);
		sc->cur_bcnq_id = id;
	}

	/* Reset 'beacon valid' bit. */
	rtwn_reset_beacon_valid(sc, id);
}

int
rtwn_setup_beacon(struct rtwn_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	struct mbuf *m;

	RTWN_ASSERT_LOCKED(sc);

	if (ni->ni_chan == IEEE80211_CHAN_ANYC)
		return (EINVAL);

	m = ieee80211_beacon_alloc(ni);
	if (m == NULL) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate beacon frame\n", __func__);
		return (ENOMEM);
	}

	if (uvp->bcn_mbuf != NULL) {
		rtwn_beacon_unload(sc, uvp->id);
		m_freem(uvp->bcn_mbuf);
	}

	uvp->bcn_mbuf = m;

	rtwn_beacon_set_rate(sc, &uvp->bcn_desc.txd[0],
	    IEEE80211_IS_CHAN_5GHZ(ni->ni_chan));

	return (rtwn_tx_beacon_check(sc, uvp));
}

/*
 * Push a beacon frame into the chip. Beacon will
 * be repeated by the chip every R92C_BCN_INTERVAL.
 */
static int
rtwn_tx_beacon(struct rtwn_softc *sc, struct rtwn_vap *uvp)
{
	int error;

	RTWN_ASSERT_LOCKED(sc);

	RTWN_DPRINTF(sc, RTWN_DEBUG_BEACON,
	    "%s: sending beacon for vap %d\n", __func__, uvp->id);

	error = rtwn_tx_start(sc, NULL, uvp->bcn_mbuf, &uvp->bcn_desc.txd[0],
	    IEEE80211_FC0_TYPE_MGT, uvp->id);

	return (error);
}

void
rtwn_update_beacon(struct ieee80211vap *vap, int item)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct rtwn_softc *sc = ic->ic_softc;
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	struct ieee80211_beacon_offsets *bo = &vap->iv_bcn_off;
	struct ieee80211_node *ni = vap->iv_bss;
	int mcast = 0;

	RTWN_LOCK(sc);
	if (uvp->bcn_mbuf == NULL) {
		uvp->bcn_mbuf = ieee80211_beacon_alloc(ni);
		if (uvp->bcn_mbuf == NULL) {
			device_printf(sc->sc_dev,
			    "%s: could not allocate beacon frame\n", __func__);
			RTWN_UNLOCK(sc);
			return;
		}
	}

	RTWN_DPRINTF(sc, RTWN_DEBUG_BEACON,
	    "%s: vap id %d, iv_csa_count %d, ic_csa_count %d, item %d\n",
	    __func__, uvp->id, vap->iv_csa_count, ic->ic_csa_count, item);

	switch (item) {
	case IEEE80211_BEACON_CSA:
		if (vap->iv_csa_count != ic->ic_csa_count) {
			/*
			 * XXX two APs with different beacon intervals
			 * are not handled properly.
			 */
			/* XXX check TBTT? */
			taskqueue_enqueue_timeout(taskqueue_thread,
			    &uvp->tx_beacon_csa,
			    msecs_to_ticks(ni->ni_intval));
		}
		break;
	case IEEE80211_BEACON_TIM:
		mcast = 1;	/* XXX */
		break;
	default:
		break;
	}

	setbit(bo->bo_flags, item);

	rtwn_beacon_update_begin(sc, vap);
	RTWN_UNLOCK(sc);

	ieee80211_beacon_update(ni, uvp->bcn_mbuf, mcast);

	/* XXX clear manually */
	clrbit(bo->bo_flags, IEEE80211_BEACON_CSA);

	RTWN_LOCK(sc);
	rtwn_tx_beacon(sc, uvp);
	rtwn_beacon_update_end(sc, vap);
	RTWN_UNLOCK(sc);
}

void
rtwn_tx_beacon_csa(void *arg, int npending __unused)
{
	struct ieee80211vap *vap = arg;
	struct ieee80211com *ic = vap->iv_ic;
	struct rtwn_softc *sc = ic->ic_softc;
	struct rtwn_vap *rvp = RTWN_VAP(vap);

	KASSERT (rvp->id == 0 || rvp->id == 1,
	    ("wrong port id %d\n", rvp->id));

	IEEE80211_LOCK(ic);
	if (ic->ic_flags & IEEE80211_F_CSAPENDING) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_BEACON,
		    "%s: vap id %d, iv_csa_count %d, ic_csa_count %d\n",
		    __func__, rvp->id, vap->iv_csa_count, ic->ic_csa_count);

		rtwn_update_beacon(vap, IEEE80211_BEACON_CSA);
	}
	IEEE80211_UNLOCK(ic);

	(void) rvp;
}

int
rtwn_tx_beacon_check(struct rtwn_softc *sc, struct rtwn_vap *uvp)
{
	int ntries, error;

	for (ntries = 0; ntries < 5; ntries++) {
		rtwn_reset_beacon_valid(sc, uvp->id);

		error = rtwn_tx_beacon(sc, uvp);
		if (error != 0)
			continue;

		error = rtwn_check_beacon_valid(sc, uvp->id);
		if (error == 0)
			break;
	}
	if (ntries == 5) {
		device_printf(sc->sc_dev,
		    "%s: cannot push beacon into chip, error %d!\n",
		    __func__, error);
		return (error);
	}

	return (0);
}
