/*-
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/sockio.h>
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
#include <sys/linker.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwn_rx.h>

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/r12a_reg.h>
#include <dev/rtwn/rtl8812a/r12a_var.h>


int
r12a_ioctl_net(struct ieee80211com *ic, u_long cmd, void *data)
{
	struct rtwn_softc *sc = ic->ic_softc;
	struct r12a_softc *rs = sc->sc_priv;
	struct ifreq *ifr = (struct ifreq *)data;
	int error;

	error = 0;
	switch (cmd) {
	case SIOCSIFCAP:
	{
		struct ieee80211vap *vap;
		int changed, rxmask;

		rxmask = ifr->ifr_reqcap & (IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6);

		RTWN_LOCK(sc);
		changed = 0;
		if (!(rs->rs_flags & R12A_RXCKSUM_EN) ^
		    !(ifr->ifr_reqcap & IFCAP_RXCSUM)) {
			rs->rs_flags ^= R12A_RXCKSUM_EN;
			changed = 1;
		}
		if (!(rs->rs_flags & R12A_RXCKSUM6_EN) ^
		    !(ifr->ifr_reqcap & IFCAP_RXCSUM_IPV6)) {
			rs->rs_flags ^= R12A_RXCKSUM6_EN;
			changed = 1;
		}
		if (changed) {
			if (rxmask == 0)
				sc->rcr &= ~R12A_RCR_TCP_OFFLD_EN;
			else
				sc->rcr |= R12A_RCR_TCP_OFFLD_EN;

			if (sc->sc_flags & RTWN_RUNNING)
				rtwn_rxfilter_set(sc);
		}
		RTWN_UNLOCK(sc);

		IEEE80211_LOCK(ic);	/* XXX */
		TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
			struct ifnet *ifp = vap->iv_ifp;

			ifp->if_capenable &=
			    ~(IFCAP_RXCSUM | IFCAP_RXCSUM_IPV6);
			ifp->if_capenable |= rxmask;
		}
		IEEE80211_UNLOCK(ic);
		break;
	}
	default:
		error = ENOTTY;		/* for net80211 */
		break;
	}

	return (error);
}
