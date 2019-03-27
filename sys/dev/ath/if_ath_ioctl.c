/*-
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for the Atheros Wireless LAN controller.
 *
 * This software is derived from work of Atsushi Onoe; his contribution
 * is greatly appreciated.
 */

#include "opt_inet.h"
#include "opt_ath.h"
/*
 * This is needed for register operations which are performed
 * by the driver - eg, calls to ath_hal_gettsf32().
 *
 * It's also required for any AH_DEBUG checks in here, eg the
 * module dependencies.
 */
#include "opt_ah.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/callout.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/priv.h>
#include <sys/module.h>
#include <sys/ktr.h>
#include <sys/smp.h>	/* for mp_ncpus */

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_llc.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#ifdef IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif
#ifdef IEEE80211_SUPPORT_TDMA
#include <net80211/ieee80211_tdma.h>
#endif

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <dev/ath/if_athvar.h>
#include <dev/ath/ath_hal/ah_devid.h>		/* XXX for softled */
#include <dev/ath/ath_hal/ah_diagcodes.h>

#include <dev/ath/if_ath_debug.h>
#include <dev/ath/if_ath_misc.h>
#include <dev/ath/if_ath_btcoex.h>
#include <dev/ath/if_ath_spectral.h>
#include <dev/ath/if_ath_lna_div.h>
#include <dev/ath/if_athdfs.h>

#ifdef	IEEE80211_SUPPORT_TDMA
#include <dev/ath/if_ath_tdma.h>
#endif

#include <dev/ath/if_ath_ioctl.h>

/*
 * ioctl() related pieces.
 *
 * Some subsystems (eg spectral, dfs) have their own ioctl method which
 * we call.
 */

/*
 * Fetch the rate control statistics for the given node.
 */
static int
ath_ioctl_ratestats(struct ath_softc *sc, struct ath_rateioctl *rs)
{
	struct ath_node *an;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	int error = 0;

	/* Perform a lookup on the given node */
	ni = ieee80211_find_node(&ic->ic_sta, rs->is_u.macaddr);
	if (ni == NULL) {
		error = EINVAL;
		goto bad;
	}

	/* Lock the ath_node */
	an = ATH_NODE(ni);
	ATH_NODE_LOCK(an);

	/* Fetch the rate control stats for this node */
	error = ath_rate_fetch_node_stats(sc, an, rs);

	/* No matter what happens here, just drop through */

	/* Unlock the ath_node */
	ATH_NODE_UNLOCK(an);

	/* Unref the node */
	ieee80211_node_decref(ni);

bad:
	return (error);
}

#ifdef ATH_DIAGAPI
/*
 * Diagnostic interface to the HAL.  This is used by various
 * tools to do things like retrieve register contents for
 * debugging.  The mechanism is intentionally opaque so that
 * it can change frequently w/o concern for compatibility.
 */
static int
ath_ioctl_diag(struct ath_softc *sc, struct ath_diag *ad)
{
	struct ath_hal *ah = sc->sc_ah;
	u_int id = ad->ad_id & ATH_DIAG_ID;
	void *indata = NULL;
	void *outdata = NULL;
	u_int32_t insize = ad->ad_in_size;
	u_int32_t outsize = ad->ad_out_size;
	int error = 0;

	if (ad->ad_id & ATH_DIAG_IN) {
		/*
		 * Copy in data.
		 */
		indata = malloc(insize, M_TEMP, M_NOWAIT);
		if (indata == NULL) {
			error = ENOMEM;
			goto bad;
		}
		error = copyin(ad->ad_in_data, indata, insize);
		if (error)
			goto bad;
	}
	if (ad->ad_id & ATH_DIAG_DYN) {
		/*
		 * Allocate a buffer for the results (otherwise the HAL
		 * returns a pointer to a buffer where we can read the
		 * results).  Note that we depend on the HAL leaving this
		 * pointer for us to use below in reclaiming the buffer;
		 * may want to be more defensive.
		 */
		outdata = malloc(outsize, M_TEMP, M_NOWAIT | M_ZERO);
		if (outdata == NULL) {
			error = ENOMEM;
			goto bad;
		}
	}


	ATH_LOCK(sc);
	if (id != HAL_DIAG_REGS)
		ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	if (ath_hal_getdiagstate(ah, id, indata, insize, &outdata, &outsize)) {
		if (outsize < ad->ad_out_size)
			ad->ad_out_size = outsize;
		if (outdata != NULL)
			error = copyout(outdata, ad->ad_out_data,
					ad->ad_out_size);
	} else {
		error = EINVAL;
	}

	ATH_LOCK(sc);
	if (id != HAL_DIAG_REGS)
		ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

bad:
	if ((ad->ad_id & ATH_DIAG_IN) && indata != NULL)
		free(indata, M_TEMP);
	if ((ad->ad_id & ATH_DIAG_DYN) && outdata != NULL)
		free(outdata, M_TEMP);
	return error;
}
#endif /* ATH_DIAGAPI */

int
ath_ioctl(struct ieee80211com *ic, u_long cmd, void *data)
{
	struct ifreq *ifr = data;
	struct ath_softc *sc = ic->ic_softc;

	switch (cmd) {
	case SIOCGATHSTATS: {
		struct ieee80211vap *vap;
		struct ifnet *ifp;
		const HAL_RATE_TABLE *rt;

		/* NB: embed these numbers to get a consistent view */
		sc->sc_stats.ast_tx_packets = 0;
		sc->sc_stats.ast_rx_packets = 0;
		TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
			ifp = vap->iv_ifp;
			sc->sc_stats.ast_tx_packets += ifp->if_get_counter(ifp,
			    IFCOUNTER_OPACKETS);
			sc->sc_stats.ast_rx_packets += ifp->if_get_counter(ifp,
			    IFCOUNTER_IPACKETS);
		}
		sc->sc_stats.ast_tx_rssi = ATH_RSSI(sc->sc_halstats.ns_avgtxrssi);
		sc->sc_stats.ast_rx_rssi = ATH_RSSI(sc->sc_halstats.ns_avgrssi);
#ifdef IEEE80211_SUPPORT_TDMA
		sc->sc_stats.ast_tdma_tsfadjp = TDMA_AVG(sc->sc_avgtsfdeltap);
		sc->sc_stats.ast_tdma_tsfadjm = TDMA_AVG(sc->sc_avgtsfdeltam);
#endif
		rt = sc->sc_currates;
		sc->sc_stats.ast_tx_rate =
		    rt->info[sc->sc_txrix].dot11Rate &~ IEEE80211_RATE_BASIC;
		if (rt->info[sc->sc_txrix].phy & IEEE80211_T_HT)
			sc->sc_stats.ast_tx_rate |= IEEE80211_RATE_MCS;
		return copyout(&sc->sc_stats, ifr_data_get_ptr(ifr),
		    sizeof (sc->sc_stats));
	}
	case SIOCGATHAGSTATS:
		return copyout(&sc->sc_aggr_stats, ifr_data_get_ptr(ifr),
		    sizeof (sc->sc_aggr_stats));
	case SIOCZATHSTATS: {
		int error;

		error = priv_check(curthread, PRIV_DRIVER);
		if (error == 0) {
			memset(&sc->sc_stats, 0, sizeof(sc->sc_stats));
			memset(&sc->sc_aggr_stats, 0,
			    sizeof(sc->sc_aggr_stats));
			memset(&sc->sc_intr_stats, 0,
			    sizeof(sc->sc_intr_stats));
		}
		return (error);
	}
#ifdef ATH_DIAGAPI
	case SIOCGATHDIAG:
		return (ath_ioctl_diag(sc, data));
	case SIOCGATHPHYERR:
		return (ath_ioctl_phyerr(sc, data));
#endif
	case SIOCGATHSPECTRAL:
		return (ath_ioctl_spectral(sc, data));
	case SIOCGATHNODERATESTATS:
		return (ath_ioctl_ratestats(sc, data));
	case SIOCGATHBTCOEX:
		return (ath_btcoex_ioctl(sc, data));
	default:
		/*
		 * This signals the net80211 layer that we didn't handle this
		 * ioctl.
		 */
		return (ENOTTY);
	}
}

