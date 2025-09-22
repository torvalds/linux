/*	$OpenBSD: ieee80211.c,v 1.91 2025/08/01 20:39:26 stsp Exp $	*/
/*	$NetBSD: ieee80211.c,v 1.19 2004/06/06 05:45:29 dyoung Exp $	*/

/*-
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * IEEE 802.11 generic handler
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_priv.h>

#ifdef IEEE80211_DEBUG
int	ieee80211_debug = 0;
#endif

int ieee80211_cache_size = IEEE80211_CACHE_SIZE;

void ieee80211_setbasicrates(struct ieee80211com *);
int ieee80211_findrate(struct ieee80211com *, enum ieee80211_phymode, int);
void ieee80211_configure_ampdu_tx(struct ieee80211com *, int);

void
ieee80211_begin_bgscan(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;

	if ((ic->ic_flags & IEEE80211_F_BGSCAN) ||
	    ic->ic_state != IEEE80211_S_RUN || ic->ic_mgt_timer != 0)
		return;

	if ((ic->ic_flags & IEEE80211_F_RSNON) && !ic->ic_bss->ni_port_valid)
		return;

	if (ic->ic_bgscan_start != NULL && ic->ic_bgscan_start(ic) == 0) {
		/*
		 * Free the nodes table to ensure we get an up-to-date view
		 * of APs around us. In particular, we need to kick out the
		 * AP we are associated to. Otherwise, our current AP might
		 * stay cached if it is turned off while we are scanning, and
		 * we could end up picking a now non-existent AP over and over.
		 */
		ieee80211_free_allnodes(ic, 0 /* keep ic->ic_bss */);

		ic->ic_flags |= IEEE80211_F_BGSCAN;
		if (ifp->if_flags & IFF_DEBUG)
			printf("%s: begin background scan\n", ifp->if_xname);

		/* Driver calls ieee80211_end_scan() when done. */
	}
}

void
ieee80211_bgscan_timeout(void *arg)
{
	struct ifnet *ifp = arg;

	ieee80211_begin_bgscan(ifp);
}

void
ieee80211_channel_init(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;
	struct ieee80211_channel *c;
	int i;

	/*
	 * Fill in 802.11 available channel set, mark
	 * all available channels as active, and pick
	 * a default channel if not already specified.
	 */
	memset(ic->ic_chan_avail, 0, sizeof(ic->ic_chan_avail));
	ic->ic_modecaps |= 1<<IEEE80211_MODE_AUTO;
	for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
		c = &ic->ic_channels[i];
		if (c->ic_flags) {
			/*
			 * Verify driver passed us valid data.
			 */
			if (i != ieee80211_chan2ieee(ic, c)) {
				printf("%s: bad channel ignored; "
					"freq %u flags %x number %u\n",
					ifp->if_xname, c->ic_freq, c->ic_flags,
					i);
				c->ic_flags = 0;	/* NB: remove */
				continue;
			}
			setbit(ic->ic_chan_avail, i);
			/*
			 * Identify mode capabilities.
			 */
			if (IEEE80211_IS_CHAN_A(c))
				ic->ic_modecaps |= 1<<IEEE80211_MODE_11A;
			if (IEEE80211_IS_CHAN_B(c))
				ic->ic_modecaps |= 1<<IEEE80211_MODE_11B;
			if (IEEE80211_IS_CHAN_PUREG(c))
				ic->ic_modecaps |= 1<<IEEE80211_MODE_11G;
			if (IEEE80211_IS_CHAN_N(c))
				ic->ic_modecaps |= 1<<IEEE80211_MODE_11N;
			if (IEEE80211_IS_CHAN_AC(c))
				ic->ic_modecaps |= 1<<IEEE80211_MODE_11AC;
		}
	}
	/* validate ic->ic_curmode */
	if ((ic->ic_modecaps & (1<<ic->ic_curmode)) == 0)
		ic->ic_curmode = IEEE80211_MODE_AUTO;
	ic->ic_des_chan = IEEE80211_CHAN_ANYC;	/* any channel is ok */
}

void
ieee80211_ifattach(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;

	memcpy(((struct arpcom *)ifp)->ac_enaddr, ic->ic_myaddr,
		ETHER_ADDR_LEN);
	ether_ifattach(ifp);

	ifp->if_output = ieee80211_output;

#if NBPFILTER > 0
	bpfattach(&ic->ic_rawbpf, ifp, DLT_IEEE802_11,
	    sizeof(struct ieee80211_frame_addr4));
#endif
	ieee80211_crypto_attach(ifp);

	ieee80211_channel_init(ifp);

	/* IEEE 802.11 defines a MTU >= 2290 */
	ifp->if_capabilities |= IFCAP_VLAN_MTU;

	ieee80211_setbasicrates(ic);
	(void)ieee80211_setmode(ic, ic->ic_curmode);

	if (ic->ic_lintval == 0)
		ic->ic_lintval = 100;		/* default sleep */
	ic->ic_bmissthres = IEEE80211_BEACON_MISS_THRES;
	ic->ic_dtim_period = 1;	/* all TIMs are DTIMs */

	ieee80211_node_attach(ifp);
	ieee80211_proto_attach(ifp);

	if_addgroup(ifp, "wlan");
	ifp->if_priority = IF_WIRELESS_DEFAULT_PRIORITY;

	task_set(&ic->ic_rtm_80211info_task, ieee80211_rtm_80211info_task, ic);
	ieee80211_set_link_state(ic, LINK_STATE_DOWN);

	timeout_set(&ic->ic_bgscan_timeout, ieee80211_bgscan_timeout, ifp);
}

void
ieee80211_ifdetach(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;

	task_del(systq, &ic->ic_rtm_80211info_task);
	timeout_del(&ic->ic_bgscan_timeout);

	/*
	 * Undo pseudo-driver changes. Pseudo-driver detach hooks could
	 * call back into the driver, e.g. via ioctl. So deactivate the
	 * interface before freeing net80211-specific data structures.
	 */
	if_deactivate(ifp);

	ieee80211_proto_detach(ifp);
	ieee80211_crypto_detach(ifp);
	ieee80211_node_detach(ifp);
	ifmedia_delete_instance(&ic->ic_media, IFM_INST_ANY);
	ether_ifdetach(ifp);
}

/*
 * Convert MHz frequency to IEEE channel number.
 */
u_int
ieee80211_mhz2ieee(u_int freq, u_int flags)
{
	if (flags & IEEE80211_CHAN_2GHZ) {	/* 2GHz band */
		if (freq == 2484)
			return 14;
		if (freq < 2484)
			return (freq - 2407) / 5;
		else
			return 15 + ((freq - 2512) / 20);
	} else if (flags & IEEE80211_CHAN_5GHZ) {	/* 5GHz band */
		return (freq - 5000) / 5;
	} else {				/* either, guess */
		if (freq == 2484)
			return 14;
		if (freq < 2484)
			return (freq - 2407) / 5;
		if (freq < 5000)
			return 15 + ((freq - 2512) / 20);
		return (freq - 5000) / 5;
	}
}

/*
 * Convert channel to IEEE channel number.
 */
u_int
ieee80211_chan2ieee(struct ieee80211com *ic, const struct ieee80211_channel *c)
{
	struct ifnet *ifp = &ic->ic_if;
	if (ic->ic_channels <= c && c <= &ic->ic_channels[IEEE80211_CHAN_MAX])
		return c - ic->ic_channels;
	else if (c == IEEE80211_CHAN_ANYC)
		return IEEE80211_CHAN_ANY;

	panic("%s: bogus channel pointer", ifp->if_xname);
}

/*
 * Convert IEEE channel number to MHz frequency.
 */
u_int
ieee80211_ieee2mhz(u_int chan, u_int flags)
{
	if (flags & IEEE80211_CHAN_2GHZ) {	/* 2GHz band */
		if (chan == 14)
			return 2484;
		if (chan < 14)
			return 2407 + chan*5;
		else
			return 2512 + ((chan-15)*20);
	} else if (flags & IEEE80211_CHAN_5GHZ) {/* 5GHz band */
		return 5000 + (chan*5);
	} else {				/* either, guess */
		if (chan == 14)
			return 2484;
		if (chan < 14)			/* 0-13 */
			return 2407 + chan*5;
		if (chan < 27)			/* 15-26 */
			return 2512 + ((chan-15)*20);
		return 5000 + (chan*5);
	}
}

void
ieee80211_configure_ampdu_tx(struct ieee80211com *ic, int enable)
{
	if ((ic->ic_caps & IEEE80211_C_TX_AMPDU) == 0)
		return;

	/* Sending AMPDUs requires QoS support. */
	if ((ic->ic_caps & IEEE80211_C_QOS) == 0)
		return;

	if (enable)
		ic->ic_flags |= IEEE80211_F_QOS;
	else
		ic->ic_flags &= ~IEEE80211_F_QOS;
}

/*
 * Setup the media data structures according to the channel and
 * rate tables.  This must be called by the driver after
 * ieee80211_attach and before most anything else.
 */
void
ieee80211_media_init(struct ifnet *ifp,
	ifm_change_cb_t media_change, ifm_stat_cb_t media_stat)
{
#define	ADD(_ic, _s, _o) \
	ifmedia_add(&(_ic)->ic_media, \
		IFM_MAKEWORD(IFM_IEEE80211, (_s), (_o), 0), 0, NULL)
	struct ieee80211com *ic = (void *)ifp;
	struct ifmediareq imr;
	int i, j, mode, rate, maxrate, r;
	uint64_t mword, mopt;
	const struct ieee80211_rateset *rs;
	struct ieee80211_rateset allrates;

	/*
	 * Do late attach work that must wait for any subclass
	 * (i.e. driver) work such as overriding methods.
	 */
	ieee80211_node_lateattach(ifp);

	/*
	 * Fill in media characteristics.
	 */
	ifmedia_init(&ic->ic_media, 0, media_change, media_stat);
	maxrate = 0;
	memset(&allrates, 0, sizeof(allrates));
	for (mode = IEEE80211_MODE_AUTO; mode <= IEEE80211_MODE_11G; mode++) {
		static const uint64_t mopts[] = {
			IFM_AUTO,
			IFM_IEEE80211_11A,
			IFM_IEEE80211_11B,
			IFM_IEEE80211_11G,
		};
		if ((ic->ic_modecaps & (1<<mode)) == 0)
			continue;
		mopt = mopts[mode];
		ADD(ic, IFM_AUTO, mopt);	/* e.g. 11a auto */
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_caps & IEEE80211_C_IBSS)
			ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_IBSS);
		if (ic->ic_caps & IEEE80211_C_HOSTAP)
			ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_HOSTAP);
		if (ic->ic_caps & IEEE80211_C_AHDEMO)
			ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_ADHOC);
#endif
		if (ic->ic_caps & IEEE80211_C_MONITOR)
			ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_MONITOR);
		if (mode == IEEE80211_MODE_AUTO)
			continue;
		rs = &ic->ic_sup_rates[mode];
		for (i = 0; i < rs->rs_nrates; i++) {
			rate = rs->rs_rates[i];
			mword = ieee80211_rate2media(ic, rate, mode);
			if (mword == 0)
				continue;
			ADD(ic, mword, mopt);
#ifndef IEEE80211_STA_ONLY
			if (ic->ic_caps & IEEE80211_C_IBSS)
				ADD(ic, mword, mopt | IFM_IEEE80211_IBSS);
			if (ic->ic_caps & IEEE80211_C_HOSTAP)
				ADD(ic, mword, mopt | IFM_IEEE80211_HOSTAP);
			if (ic->ic_caps & IEEE80211_C_AHDEMO)
				ADD(ic, mword, mopt | IFM_IEEE80211_ADHOC);
#endif
			if (ic->ic_caps & IEEE80211_C_MONITOR)
				ADD(ic, mword, mopt | IFM_IEEE80211_MONITOR);
			/*
			 * Add rate to the collection of all rates.
			 */
			r = rate & IEEE80211_RATE_VAL;
			for (j = 0; j < allrates.rs_nrates; j++)
				if (allrates.rs_rates[j] == r)
					break;
			if (j == allrates.rs_nrates) {
				/* unique, add to the set */
				allrates.rs_rates[j] = r;
				allrates.rs_nrates++;
			}
			rate = (rate & IEEE80211_RATE_VAL) / 2;
			if (rate > maxrate)
				maxrate = rate;
		}
	}
	for (i = 0; i < allrates.rs_nrates; i++) {
		mword = ieee80211_rate2media(ic, allrates.rs_rates[i],
				IEEE80211_MODE_AUTO);
		if (mword == 0)
			continue;
		mword = IFM_SUBTYPE(mword);	/* remove media options */
		ADD(ic, mword, 0);
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_caps & IEEE80211_C_IBSS)
			ADD(ic, mword, IFM_IEEE80211_IBSS);
		if (ic->ic_caps & IEEE80211_C_HOSTAP)
			ADD(ic, mword, IFM_IEEE80211_HOSTAP);
		if (ic->ic_caps & IEEE80211_C_AHDEMO)
			ADD(ic, mword, IFM_IEEE80211_ADHOC);
#endif
		if (ic->ic_caps & IEEE80211_C_MONITOR)
			ADD(ic, mword, IFM_IEEE80211_MONITOR);
	}

	if (ic->ic_modecaps & (1 << IEEE80211_MODE_11N)) {
		mopt = IFM_IEEE80211_11N;
		ADD(ic, IFM_AUTO, mopt);
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_caps & IEEE80211_C_IBSS)
			ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_IBSS);
		if (ic->ic_caps & IEEE80211_C_HOSTAP)
			ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_HOSTAP);
#endif
		if (ic->ic_caps & IEEE80211_C_MONITOR)
			ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_MONITOR);
		for (i = 0; i < IEEE80211_HT_NUM_MCS; i++) {
			if (!isset(ic->ic_sup_mcs, i))
				continue;
			ADD(ic, IFM_IEEE80211_HT_MCS0 + i, mopt);
#ifndef IEEE80211_STA_ONLY
			if (ic->ic_caps & IEEE80211_C_IBSS)
				ADD(ic, IFM_IEEE80211_HT_MCS0 + i,
				     mopt | IFM_IEEE80211_IBSS);
			if (ic->ic_caps & IEEE80211_C_HOSTAP)
				ADD(ic, IFM_IEEE80211_HT_MCS0 + i,
				    mopt | IFM_IEEE80211_HOSTAP);
#endif
			if (ic->ic_caps & IEEE80211_C_MONITOR)
				ADD(ic, IFM_IEEE80211_HT_MCS0 + i,
				    mopt | IFM_IEEE80211_MONITOR);
		}
		ic->ic_flags |= IEEE80211_F_HTON; /* enable 11n by default */
		ieee80211_configure_ampdu_tx(ic, 1);
	}

	if (ic->ic_modecaps & (1 << IEEE80211_MODE_11AC)) {
		mopt = IFM_IEEE80211_11AC;
		ADD(ic, IFM_AUTO, mopt);
#ifndef IEEE80211_STA_ONLY
		if (ic->ic_caps & IEEE80211_C_IBSS)
			ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_IBSS);
		if (ic->ic_caps & IEEE80211_C_HOSTAP)
			ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_HOSTAP);
#endif
		if (ic->ic_caps & IEEE80211_C_MONITOR)
			ADD(ic, IFM_AUTO, mopt | IFM_IEEE80211_MONITOR);
		for (i = 0; i < IEEE80211_VHT_NUM_MCS; i++) {
#if 0
			/* TODO: Obtain VHT MCS information from VHT CAP IE. */
			if (!vht_mcs_supported)
				continue;
#endif
			ADD(ic, IFM_IEEE80211_VHT_MCS0 + i, mopt);
#ifndef IEEE80211_STA_ONLY
			if (ic->ic_caps & IEEE80211_C_IBSS)
				ADD(ic, IFM_IEEE80211_VHT_MCS0 + i,
				     mopt | IFM_IEEE80211_IBSS);
			if (ic->ic_caps & IEEE80211_C_HOSTAP)
				ADD(ic, IFM_IEEE80211_VHT_MCS0 + i,
				    mopt | IFM_IEEE80211_HOSTAP);
#endif
			if (ic->ic_caps & IEEE80211_C_MONITOR)
				ADD(ic, IFM_IEEE80211_VHT_MCS0 + i,
				    mopt | IFM_IEEE80211_MONITOR);
		}
		ic->ic_flags |= IEEE80211_F_VHTON; /* enable 11ac by default */
		ic->ic_flags |= IEEE80211_F_HTON; /* 11ac implies 11n */
		if (ic->ic_caps & IEEE80211_C_QOS)
			ic->ic_flags |= IEEE80211_F_QOS;
	}

	ieee80211_media_status(ifp, &imr);
	ifmedia_set(&ic->ic_media, imr.ifm_active);

	if (maxrate)
		ifp->if_baudrate = IF_Mbps(maxrate);

#undef ADD
}

int
ieee80211_findrate(struct ieee80211com *ic, enum ieee80211_phymode mode,
    int rate)
{
#define	IEEERATE(_ic,_m,_i) \
	((_ic)->ic_sup_rates[_m].rs_rates[_i] & IEEE80211_RATE_VAL)
	int i, nrates = ic->ic_sup_rates[mode].rs_nrates;
	for (i = 0; i < nrates; i++)
		if (IEEERATE(ic, mode, i) == rate)
			return i;
	return -1;
#undef IEEERATE
}

/*
 * Handle a media change request.
 */
int
ieee80211_media_change(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;
	struct ifmedia_entry *ime;
	enum ieee80211_opmode newopmode;
	enum ieee80211_phymode newphymode;
	int i, j, newrate, error = 0;

	ime = ic->ic_media.ifm_cur;
	/*
	 * First, identify the phy mode.
	 */
	switch (IFM_MODE(ime->ifm_media)) {
	case IFM_IEEE80211_11A:
		newphymode = IEEE80211_MODE_11A;
		break;
	case IFM_IEEE80211_11B:
		newphymode = IEEE80211_MODE_11B;
		break;
	case IFM_IEEE80211_11G:
		newphymode = IEEE80211_MODE_11G;
		break;
	case IFM_IEEE80211_11N:
		newphymode = IEEE80211_MODE_11N;
		break;
	case IFM_IEEE80211_11AC:
		newphymode = IEEE80211_MODE_11AC;
		break;
	case IFM_AUTO:
		newphymode = IEEE80211_MODE_AUTO;
		break;
	default:
		return EINVAL;
	}

	/*
	 * Validate requested mode is available.
	 */
	if ((ic->ic_modecaps & (1<<newphymode)) == 0)
		return EINVAL;

	/*
	 * Next, the fixed/variable rate.
	 */
	i = -1;
	if (IFM_SUBTYPE(ime->ifm_media) >= IFM_IEEE80211_VHT_MCS0 &&
	    IFM_SUBTYPE(ime->ifm_media) <= IFM_IEEE80211_VHT_MCS9) {
		if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11AC)) == 0)
			return EINVAL;
		if (newphymode != IEEE80211_MODE_AUTO &&
		    newphymode != IEEE80211_MODE_11AC)
			return EINVAL;
		i = ieee80211_media2mcs(ime->ifm_media);
		/* TODO: Obtain VHT MCS information from VHT CAP IE. */
		if (i == -1 /* || !vht_mcs_supported */)
			return EINVAL;
	} else if (IFM_SUBTYPE(ime->ifm_media) >= IFM_IEEE80211_HT_MCS0 &&
	    IFM_SUBTYPE(ime->ifm_media) <= IFM_IEEE80211_HT_MCS76) {
		if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11N)) == 0)
			return EINVAL;
		if (newphymode != IEEE80211_MODE_AUTO &&
		    newphymode != IEEE80211_MODE_11N)
			return EINVAL;
		i = ieee80211_media2mcs(ime->ifm_media);
		if (i == -1 || isclr(ic->ic_sup_mcs, i))
			return EINVAL;
	} else if (IFM_SUBTYPE(ime->ifm_media) != IFM_AUTO) {
		/*
		 * Convert media subtype to rate.
		 */
		newrate = ieee80211_media2rate(ime->ifm_media);
		if (newrate == 0)
			return EINVAL;
		/*
		 * Check the rate table for the specified/current phy.
		 */
		if (newphymode == IEEE80211_MODE_AUTO) {
			/*
			 * In autoselect mode search for the rate.
			 */
			for (j = IEEE80211_MODE_11A;
			     j < IEEE80211_MODE_MAX; j++) {
				if ((ic->ic_modecaps & (1<<j)) == 0)
					continue;
				i = ieee80211_findrate(ic, j, newrate);
				if (i != -1) {
					/* lock mode too */
					newphymode = j;
					break;
				}
			}
		} else {
			i = ieee80211_findrate(ic, newphymode, newrate);
		}
		if (i == -1)			/* mode/rate mismatch */
			return EINVAL;
	}
	/* NB: defer rate setting to later */

	/*
	 * Deduce new operating mode but don't install it just yet.
	 */
#ifndef IEEE80211_STA_ONLY
	if (ime->ifm_media & IFM_IEEE80211_ADHOC)
		newopmode = IEEE80211_M_AHDEMO;
	else if (ime->ifm_media & IFM_IEEE80211_HOSTAP)
		newopmode = IEEE80211_M_HOSTAP;
	else if (ime->ifm_media & IFM_IEEE80211_IBSS)
		newopmode = IEEE80211_M_IBSS;
	else
#endif
	if (ime->ifm_media & IFM_IEEE80211_MONITOR)
		newopmode = IEEE80211_M_MONITOR;
	else
		newopmode = IEEE80211_M_STA;

#ifndef IEEE80211_STA_ONLY
	/*
	 * Autoselect doesn't make sense when operating as an AP.
	 * If no phy mode has been selected, pick one and lock it
	 * down so rate tables can be used in forming beacon frames
	 * and the like.
	 */
	if (newopmode == IEEE80211_M_HOSTAP &&
	    newphymode == IEEE80211_MODE_AUTO) {
		if (ic->ic_modecaps & (1 << IEEE80211_MODE_11AC))
			newphymode = IEEE80211_MODE_11AC;
		else if (ic->ic_modecaps & (1 << IEEE80211_MODE_11N))
			newphymode = IEEE80211_MODE_11N;
		else if (ic->ic_modecaps & (1 << IEEE80211_MODE_11A))
			newphymode = IEEE80211_MODE_11A;
		else if (ic->ic_modecaps & (1 << IEEE80211_MODE_11G))
			newphymode = IEEE80211_MODE_11G;
		else
			newphymode = IEEE80211_MODE_11B;
	}
#endif

	/*
	 * Handle phy mode change.
	 */
	if (ic->ic_curmode != newphymode) {		/* change phy mode */
		error = ieee80211_setmode(ic, newphymode);
		if (error != 0)
			return error;
		error = ENETRESET;
	}

	/*
	 * Committed to changes, install the MCS/rate setting.
	 */
	ic->ic_flags &= ~(IEEE80211_F_HTON | IEEE80211_F_VHTON);
	ieee80211_configure_ampdu_tx(ic, 0);
	if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11AC)) &&
	    (newphymode == IEEE80211_MODE_AUTO ||
	    newphymode == IEEE80211_MODE_11AC)) {
		ic->ic_flags |= IEEE80211_F_VHTON;
		ic->ic_flags |= IEEE80211_F_HTON;
		ieee80211_configure_ampdu_tx(ic, 1);
	} else if ((ic->ic_modecaps & (1 << IEEE80211_MODE_11N)) &&
	    (newphymode == IEEE80211_MODE_AUTO ||
	    newphymode == IEEE80211_MODE_11N)) {
		ic->ic_flags |= IEEE80211_F_HTON;
		ieee80211_configure_ampdu_tx(ic, 1);
	}
	if ((ic->ic_flags & (IEEE80211_F_HTON | IEEE80211_F_VHTON)) == 0) {
		ic->ic_fixed_mcs = -1;
	    	if (ic->ic_fixed_rate != i) {
			ic->ic_fixed_rate = i;		/* set fixed tx rate */
			error = ENETRESET;
		}
	} else {
		ic->ic_fixed_rate = -1;
		if (ic->ic_fixed_mcs != i) {
			ic->ic_fixed_mcs = i;		/* set fixed mcs */
			error = ENETRESET;
		}
	}

	/*
	 * Handle operating mode change.
	 */
	if (ic->ic_opmode != newopmode) {
		ic->ic_opmode = newopmode;
#ifndef IEEE80211_STA_ONLY
		switch (newopmode) {
		case IEEE80211_M_AHDEMO:
		case IEEE80211_M_HOSTAP:
		case IEEE80211_M_STA:
		case IEEE80211_M_MONITOR:
			ic->ic_flags &= ~IEEE80211_F_IBSSON;
			break;
		case IEEE80211_M_IBSS:
			ic->ic_flags |= IEEE80211_F_IBSSON;
			break;
		}
#endif
		/*
		 * Yech, slot time may change depending on the
		 * operating mode so reset it to be sure everything
		 * is setup appropriately.
		 */
		ieee80211_reset_erp(ic);
		error = ENETRESET;
	}
#ifdef notdef
	if (error == 0)
		ifp->if_baudrate = ifmedia_baudrate(ime->ifm_media);
#endif
	return error;
}

void
ieee80211_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct ieee80211com *ic = (void *)ifp;
	const struct ieee80211_node *ni = NULL;

	imr->ifm_status = IFM_AVALID;
	imr->ifm_active = IFM_IEEE80211;
	if (ic->ic_state == IEEE80211_S_RUN &&
	    (ic->ic_opmode != IEEE80211_M_STA ||
	     !(ic->ic_flags & IEEE80211_F_RSNON) ||
	     ic->ic_bss->ni_port_valid))
		imr->ifm_status |= IFM_ACTIVE;
	imr->ifm_active |= IFM_AUTO;
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		ni = ic->ic_bss;
		if (ic->ic_curmode == IEEE80211_MODE_11N ||
		    ic->ic_curmode == IEEE80211_MODE_11AC)
			imr->ifm_active |= ieee80211_mcs2media(ic,
				ni->ni_txmcs, ic->ic_curmode);
		else if (ni->ni_flags & IEEE80211_NODE_VHT) /* in MODE_AUTO */
			imr->ifm_active |= ieee80211_mcs2media(ic,
				ni->ni_txmcs, IEEE80211_MODE_11AC);
		else if (ni->ni_flags & IEEE80211_NODE_HT) /* in MODE_AUTO */
			imr->ifm_active |= ieee80211_mcs2media(ic,
				ni->ni_txmcs, IEEE80211_MODE_11N);
		else
			/* calculate rate subtype */
			imr->ifm_active |= ieee80211_rate2media(ic,
				ni->ni_rates.rs_rates[ni->ni_txrate],
				ic->ic_curmode);
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
		imr->ifm_active |= IFM_IEEE80211_IBSS;
		break;
	case IEEE80211_M_AHDEMO:
		imr->ifm_active |= IFM_IEEE80211_ADHOC;
		break;
	case IEEE80211_M_HOSTAP:
		imr->ifm_active |= IFM_IEEE80211_HOSTAP;
		break;
#endif
	case IEEE80211_M_MONITOR:
		imr->ifm_active |= IFM_IEEE80211_MONITOR;
		break;
	default:
		break;
	}
	switch (ic->ic_curmode) {
	case IEEE80211_MODE_11A:
		imr->ifm_active |= IFM_IEEE80211_11A;
		break;
	case IEEE80211_MODE_11B:
		imr->ifm_active |= IFM_IEEE80211_11B;
		break;
	case IEEE80211_MODE_11G:
		imr->ifm_active |= IFM_IEEE80211_11G;
		break;
	case IEEE80211_MODE_11N:
		imr->ifm_active |= IFM_IEEE80211_11N;
		break;
	case IEEE80211_MODE_11AC:
		imr->ifm_active |= IFM_IEEE80211_11AC;
		break;
	}
}

void
ieee80211_watchdog(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;

	if (ic->ic_mgt_timer && --ic->ic_mgt_timer == 0) {
		if (ic->ic_opmode == IEEE80211_M_STA &&
		    (ic->ic_state == IEEE80211_S_AUTH ||
		    ic->ic_state == IEEE80211_S_ASSOC)) {
			struct ieee80211_node *ni;
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: %s timed out for %s\n",
				    ifp->if_xname,
				    ic->ic_state == IEEE80211_S_ASSOC ?
				    "association" : "authentication",
				    ether_sprintf(ic->ic_bss->ni_macaddr));
			ni = ieee80211_find_node(ic, ic->ic_bss->ni_macaddr);
			if (ni)
				ni->ni_fails++;
			if (ISSET(ic->ic_flags, IEEE80211_F_AUTO_JOIN))
				ieee80211_deselect_ess(ic);
		}
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	}

	if (ic->ic_mgt_timer != 0)
		ifp->if_timer = 1;
}

const struct ieee80211_rateset ieee80211_std_rateset_11a =
	{ 8, { 12, 18, 24, 36, 48, 72, 96, 108 } };

const struct ieee80211_rateset ieee80211_std_rateset_11b =
	{ 4, { 2, 4, 11, 22 } };

const struct ieee80211_rateset ieee80211_std_rateset_11g =
	{ 12, { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 } };

const struct ieee80211_ht_rateset ieee80211_std_ratesets_11n[] = {
	/* MCS 0-7, 20MHz channel, no SGI */
	{ 8, { 13, 26, 39, 52, 78, 104, 117, 130 },
	    0x000000ff, 0, 7, 0, 0},

	/* MCS 0-7, 20MHz channel, SGI */
	{ 8, { 14, 29, 43, 58, 87, 116, 130, 144 },
	    0x000000ff, 0, 7, 0, 1 },

	/* MCS 8-15, 20MHz channel, no SGI */
	{ 8, { 26, 52, 78, 104, 156, 208, 234, 260 },
	    0x0000ff00, 8, 15, 0, 0 },

	/* MCS 8-15, 20MHz channel, SGI */
	{ 8, { 29, 58, 87, 116, 173, 231, 261, 289 },
	    0x0000ff00, 8, 15, 0, 1 },

	/* MCS 16-23, 20MHz channel, no SGI */
	{ 8, { 39, 78, 117, 156, 234, 312, 351, 390 },
	    0x00ff0000, 16, 23, 0, 0 },

	/* MCS 16-23, 20MHz channel, SGI */
	{ 8, { 43, 87, 130, 173, 260, 347, 390, 433 },
	    0x00ff0000, 16, 23, 0, 1 },

	/* MCS 24-31, 20MHz channel, no SGI */
	{ 8, { 52, 104, 156, 208, 312, 416, 468, 520 },
	    0xff000000, 24, 31, 0, 0 },

	/* MCS 24-31, 20MHz channel, SGI */
	{ 8, { 58, 116, 173, 231, 347, 462, 520, 578 },
	    0xff000000, 24, 31, 0, 1 },

	/* MCS 0-7, 40MHz channel, no SGI */
	{ 8, { 27, 54, 81, 108, 162, 216, 243, 270 },
	    0x000000ff, 0, 7, 1, 0 },

	/* MCS 0-7, 40MHz channel, SGI */
	{ 8, { 30, 60, 90, 120, 180, 240, 270, 300 },
	    0x000000ff, 0, 7, 1, 1 },

	/* MCS 8-15, 40MHz channel, no SGI */
	{ 8, { 54, 108, 192, 216, 324, 432, 486, 540 },
	    0x0000ff00, 8, 15, 1, 0 },

	/* MCS 8-15, 40MHz channel, SGI */
	{ 8, { 60, 120, 180, 240, 360, 480, 540, 600 },
	    0x0000ff00, 8, 15, 1, 1 },

	/* MCS 16-23, 40MHz channel, no SGI */
	{ 8, { 81, 162, 243, 324, 486, 648, 729, 810 },
	    0x00ff0000, 16, 23, 1, 0 },

	/* MCS 16-23, 40MHz channel, SGI */
	{ 8, { 90, 180, 270, 360, 540, 720, 810, 900 },
	    0x00ff0000, 16, 23, 1, 1 },

	/* MCS 24-31, 40MHz channel, no SGI */
	{ 8, { 108, 216, 324, 432, 324, 864, 972, 1080 },
	    0xff000000, 24, 31, 1, 0 },

	/* MCS 24-31, 40MHz channel, SGI */
	{ 8, { 120, 240, 360, 480, 520, 960, 1080, 1200 },
	    0xff000000, 24, 31, 1, 1 },
};

const struct ieee80211_vht_rateset ieee80211_std_ratesets_11ac[] = {
	/* MCS 0-8 (MCS 9 N/A), 1 SS, 20MHz channel, no SGI */
	{ 0, 9, { 13, 26, 39, 52, 78, 104, 117, 130, 156 },
	    1, 0, 0, 0 },

	/* MCS 0-8 (MCS 9 N/A), 1 SS, 20MHz channel, SGI */
	{ 1, 9, { 14, 29, 43, 58, 87, 116, 130, 144, 174 },
	    1, 0, 0, 1 },

	/* MCS 0-8 (MCS 9 N/A), 2 SS, 20MHz channel, no SGI */
	{ 2, 9, { 26, 52, 78, 104, 156, 208, 234, 260, 312 },
	    2, 0, 0, 0 },

	/* MCS 0-8 (MCS 9 N/A), 2 SS, 20MHz channel, SGI */
	{ 3, 9, { 29, 58, 87, 116, 173, 231, 261, 289, 347 },
	    2, 0, 0, 1 },

	/* MCS 0-9, 1 SS, 40MHz channel, no SGI */
	{ 4, 10, { 27, 54, 81, 108, 162, 216, 243, 270, 324, 360 },
	    1, 1, 0, 0 },

	/* MCS 0-9, 1 SS, 40MHz channel, SGI */
	{ 5, 10, { 30, 60, 90, 120, 180, 240, 270, 300, 360, 400 },
	    1, 1, 0, 1 },

	/* MCS 0-9, 2 SS, 40MHz channel, no SGI */
	{ 6, 10, { 54, 108, 162, 216, 324, 432, 486, 540, 648, 720 },
	    2, 1, 0, 0 },

	/* MCS 0-9, 2 SS, 40MHz channel, SGI */
	{ 7, 10, { 60, 120, 180, 240, 360, 480, 540, 600, 720, 800 },
	    2, 1, 0, 1 },

	/* MCS 0-9, 1 SS, 80MHz channel, no SGI */
	{ 8, 10, { 59, 117, 176, 234, 351, 468, 527, 585, 702, 780 },
	    1, 0, 1, 0 },

	/* MCS 0-9, 1 SS, 80MHz channel, SGI */
	{ 9, 10, { 65, 130, 195, 260, 390, 520, 585, 650, 780, 867 },
	    1, 0, 1, 1 },

	/* MCS 0-9, 2 SS, 80MHz channel, no SGI */
	{ 10, 10, { 117, 234, 351, 468, 702, 936, 1053, 1404, 1560 },
	    2, 0, 1, 0 },

	/* MCS 0-9, 2 SS, 80MHz channel, SGI */
	{ 11, 10, { 130, 260, 390, 520, 780, 1040, 1170, 1300, 1560, 1734 },
	    2, 0, 1, 1 },
};

/*
 * Mark the basic rates for the 11g rate table based on the
 * operating mode.  For real 11g we mark all the 11b rates
 * and 6, 12, and 24 OFDM.  For 11b compatibility we mark only
 * 11b rates.  There's also a pseudo 11a-mode used to mark only
 * the basic OFDM rates.
 */
void
ieee80211_setbasicrates(struct ieee80211com *ic)
{
	static const struct ieee80211_rateset basic[] = {
	    { 0 },				/* IEEE80211_MODE_AUTO */
	    { 3, { 12, 24, 48 } },		/* IEEE80211_MODE_11A */
	    { 2, { 2, 4 } },			/* IEEE80211_MODE_11B */
	    { 4, { 2, 4, 11, 22 } },		/* IEEE80211_MODE_11G */
	    { 0 },				/* IEEE80211_MODE_11N	*/
	    { 0 },				/* IEEE80211_MODE_11AC	*/
	};
	enum ieee80211_phymode mode;
	struct ieee80211_rateset *rs;
	int i, j;

	for (mode = 0; mode < IEEE80211_MODE_MAX; mode++) {
		rs = &ic->ic_sup_rates[mode];
		for (i = 0; i < rs->rs_nrates; i++) {
			rs->rs_rates[i] &= IEEE80211_RATE_VAL;
			for (j = 0; j < basic[mode].rs_nrates; j++) {
				if (basic[mode].rs_rates[j] ==
				    rs->rs_rates[i]) {
					rs->rs_rates[i] |=
					    IEEE80211_RATE_BASIC;
					break;
				}
			}
		}
	}
}

int
ieee80211_min_basic_rate(struct ieee80211com *ic)
{
	struct ieee80211_rateset *rs = &ic->ic_bss->ni_rates;
	int i, min, rval;

	min = -1;

	for (i = 0; i < rs->rs_nrates; i++) {
		if ((rs->rs_rates[i] & IEEE80211_RATE_BASIC) == 0)
			continue;
		rval = (rs->rs_rates[i] & IEEE80211_RATE_VAL);
		if (min == -1)
			min = rval;
		else if (rval < min)
			min = rval;
	}

	/* Default to 1 Mbit/s on 2GHz and 6 Mbit/s on 5GHz. */
	if (min == -1)
		min = IEEE80211_IS_CHAN_2GHZ(ic->ic_bss->ni_chan) ? 2 : 12;

	return min;
}

int
ieee80211_max_basic_rate(struct ieee80211com *ic)
{
	struct ieee80211_rateset *rs = &ic->ic_bss->ni_rates;
	int i, max, rval;

	/* Default to 1 Mbit/s on 2GHz and 6 Mbit/s on 5GHz. */
	max = IEEE80211_IS_CHAN_2GHZ(ic->ic_bss->ni_chan) ? 2 : 12;

	for (i = 0; i < rs->rs_nrates; i++) {
		if ((rs->rs_rates[i] & IEEE80211_RATE_BASIC) == 0)
			continue;
		rval = (rs->rs_rates[i] & IEEE80211_RATE_VAL);
		if (rval > max)
			max = rval;
	}

	return max;
}

/*
 * Set the current phy mode and recalculate the active channel
 * set based on the available channels for this mode.  Also
 * select a new default/current channel if the current one is
 * inappropriate for this mode.
 */
int
ieee80211_setmode(struct ieee80211com *ic, enum ieee80211_phymode mode)
{
	struct ifnet *ifp = &ic->ic_if;
	static const u_int chanflags[] = {
		0,			/* IEEE80211_MODE_AUTO */
		IEEE80211_CHAN_A,	/* IEEE80211_MODE_11A */
		IEEE80211_CHAN_B,	/* IEEE80211_MODE_11B */
		IEEE80211_CHAN_PUREG,	/* IEEE80211_MODE_11G */
		IEEE80211_CHAN_HT,	/* IEEE80211_MODE_11N */
		IEEE80211_CHAN_VHT,	/* IEEE80211_MODE_11AC */
	};
	const struct ieee80211_channel *c;
	u_int modeflags;
	int i;

	/* validate new mode */
	if ((ic->ic_modecaps & (1<<mode)) == 0) {
		DPRINTF(("mode %u not supported (caps 0x%x)\n",
		    mode, ic->ic_modecaps));
		return EINVAL;
	}

	/*
	 * Verify at least one channel is present in the available
	 * channel list before committing to the new mode.
	 */
	if (mode >= nitems(chanflags))
		panic("%s: unexpected mode %u", __func__, mode);
	modeflags = chanflags[mode];
	for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
		c = &ic->ic_channels[i];
		if (mode == IEEE80211_MODE_AUTO) {
			if (c->ic_flags != 0)
				break;
		} else if ((c->ic_flags & modeflags) == modeflags)
			break;
	}
	if (i > IEEE80211_CHAN_MAX) {
		DPRINTF(("no channels found for mode %u\n", mode));
		return EINVAL;
	}

	/*
	 * Calculate the active channel set.
	 */
	memset(ic->ic_chan_active, 0, sizeof(ic->ic_chan_active));
	for (i = 0; i <= IEEE80211_CHAN_MAX; i++) {
		c = &ic->ic_channels[i];
		if (mode == IEEE80211_MODE_AUTO) {
			if (c->ic_flags != 0)
				setbit(ic->ic_chan_active, i);
		} else if ((c->ic_flags & modeflags) == modeflags)
			setbit(ic->ic_chan_active, i);
	}
	/*
	 * If no current/default channel is setup or the current
	 * channel is wrong for the mode then pick the first
	 * available channel from the active list.  This is likely
	 * not the right one.
	 */
	if (ic->ic_ibss_chan == NULL || isclr(ic->ic_chan_active,
	    ieee80211_chan2ieee(ic, ic->ic_ibss_chan))) {
		for (i = 0; i <= IEEE80211_CHAN_MAX; i++)
			if (isset(ic->ic_chan_active, i)) {
				ic->ic_ibss_chan = &ic->ic_channels[i];
				break;
			}
		if ((ic->ic_ibss_chan == NULL) || isclr(ic->ic_chan_active,
		    ieee80211_chan2ieee(ic, ic->ic_ibss_chan)))
			panic("Bad IBSS channel %u",
			    ieee80211_chan2ieee(ic, ic->ic_ibss_chan));
	}

	/*
	 * Reset the scan state for the new mode. This avoids scanning
	 * of invalid channels, ie. 5GHz channels in 11b mode.
	 */
	ieee80211_reset_scan(ifp);

	ic->ic_curmode = mode;
	ieee80211_reset_erp(ic);	/* reset ERP state */

	return 0;
}

enum ieee80211_phymode
ieee80211_next_mode(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;
	uint16_t mode;

	/*
	 * Indicate a wrap-around if we're running in a fixed, user-specified
	 * phy mode.
	 */
	if (IFM_MODE(ic->ic_media.ifm_cur->ifm_media) != IFM_AUTO)
		return (IEEE80211_MODE_AUTO);

	/*
	 * Always scan in AUTO mode if the driver scans all bands.
	 * The current mode might have changed during association
	 * so we must reset it here.
	 */
	if (ic->ic_caps & IEEE80211_C_SCANALLBAND) {
		ieee80211_setmode(ic, IEEE80211_MODE_AUTO);
		return (ic->ic_curmode);
	}

	/*
	 * Get the next supported mode; effectively, this alternates between
	 * the 11a (5GHz) and 11b/g (2GHz) modes. What matters is that each
	 * supported channel gets scanned.
	 */
	for (mode = ic->ic_curmode + 1; mode <= IEEE80211_MODE_MAX; mode++) {
		/*
		 * Skip over 11n mode. Its set of channels is the superset
		 * of all channels supported by the other modes.
		 */
		if (mode == IEEE80211_MODE_11N)
			continue;
		/*
		 * Skip over 11ac mode. Its set of channels is the set
		 * of all channels supported by 11a.
		 */
		if (mode == IEEE80211_MODE_11AC)
			continue;

		/* Start over if we have already tried all modes. */
		if (mode == IEEE80211_MODE_MAX) {
			mode = IEEE80211_MODE_AUTO;
			break;
		}

		if (ic->ic_modecaps & (1 << mode))
			break;
	}

	if (mode != ic->ic_curmode)
		ieee80211_setmode(ic, mode);

	return (ic->ic_curmode);
}

/*
 * Convert IEEE80211 MCS index to ifmedia subtype.
 */
uint64_t
ieee80211_mcs2media(struct ieee80211com *ic, int mcs,
    enum ieee80211_phymode mode)
{
	switch (mode) {
	case IEEE80211_MODE_11A:
	case IEEE80211_MODE_11B:
	case IEEE80211_MODE_11G:
		/* these modes use rates, not MCS */
		panic("%s: unexpected mode %d", __func__, mode);
		break;
	case IEEE80211_MODE_11N:
		if (mcs >= 0 && mcs < IEEE80211_HT_NUM_MCS)
			return (IFM_IEEE80211_11N |
			    (IFM_IEEE80211_HT_MCS0 + mcs));
		break;
	case IEEE80211_MODE_11AC:
		if (mcs >= 0 && mcs < IEEE80211_VHT_NUM_MCS)
			return (IFM_IEEE80211_11AC |
			    (IFM_IEEE80211_VHT_MCS0 + mcs));
		break;
	case IEEE80211_MODE_AUTO:
		break;
	}

	return IFM_AUTO;
}

/*
 * Convert ifmedia subtype to IEEE80211 MCS index.
 */
int
ieee80211_media2mcs(uint64_t mword)
{
	uint64_t subtype;

	subtype = IFM_SUBTYPE(mword);

	if (subtype == IFM_AUTO)
		return -1;
	else if (subtype == IFM_MANUAL || subtype == IFM_NONE)
		return 0;

	if (subtype >= IFM_IEEE80211_HT_MCS0 &&
	    subtype <= IFM_IEEE80211_HT_MCS76)
		return (int)(subtype - IFM_IEEE80211_HT_MCS0);

	if (subtype >= IFM_IEEE80211_VHT_MCS0 &&
	    subtype <= IFM_IEEE80211_VHT_MCS9)
		return (int)(subtype - IFM_IEEE80211_VHT_MCS0);

	return -1;
}

/*
 * convert IEEE80211 rate value to ifmedia subtype.
 * ieee80211 rate is in unit of 0.5Mbps.
 */
uint64_t
ieee80211_rate2media(struct ieee80211com *ic, int rate,
    enum ieee80211_phymode mode)
{
	static const struct {
		uint64_t	m;	/* rate + mode */
		uint64_t	r;	/* if_media rate */
	} rates[] = {
		{   2 | IFM_IEEE80211_11B, IFM_IEEE80211_DS1 },
		{   4 | IFM_IEEE80211_11B, IFM_IEEE80211_DS2 },
		{  11 | IFM_IEEE80211_11B, IFM_IEEE80211_DS5 },
		{  22 | IFM_IEEE80211_11B, IFM_IEEE80211_DS11 },
		{  44 | IFM_IEEE80211_11B, IFM_IEEE80211_DS22 },
		{  12 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM6 },
		{  18 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM9 },
		{  24 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM12 },
		{  36 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM18 },
		{  48 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM24 },
		{  72 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM36 },
		{  96 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM48 },
		{ 108 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM54 },
		{   2 | IFM_IEEE80211_11G, IFM_IEEE80211_DS1 },
		{   4 | IFM_IEEE80211_11G, IFM_IEEE80211_DS2 },
		{  11 | IFM_IEEE80211_11G, IFM_IEEE80211_DS5 },
		{  22 | IFM_IEEE80211_11G, IFM_IEEE80211_DS11 },
		{  12 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM6 },
		{  18 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM9 },
		{  24 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM12 },
		{  36 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM18 },
		{  48 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM24 },
		{  72 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM36 },
		{  96 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM48 },
		{ 108 | IFM_IEEE80211_11G, IFM_IEEE80211_OFDM54 },
		/* NB: OFDM72 doesn't really exist so we don't handle it */
	};
	uint64_t mask;
	int i;

	mask = rate & IEEE80211_RATE_VAL;
	switch (mode) {
	case IEEE80211_MODE_11A:
		mask |= IFM_IEEE80211_11A;
		break;
	case IEEE80211_MODE_11B:
		mask |= IFM_IEEE80211_11B;
		break;
	case IEEE80211_MODE_AUTO:
		/* NB: hack, 11g matches both 11b+11a rates */
		/* FALLTHROUGH */
	case IEEE80211_MODE_11G:
		mask |= IFM_IEEE80211_11G;
		break;
	case IEEE80211_MODE_11N:
	case IEEE80211_MODE_11AC:
		/* 11n/11ac uses MCS, not rates. */
		panic("%s: unexpected mode %d", __func__, mode);
		break;
	}
	for (i = 0; i < nitems(rates); i++)
		if (rates[i].m == mask)
			return rates[i].r;
	return IFM_AUTO;
}

int
ieee80211_media2rate(uint64_t mword)
{
	int i;
	static const struct {
		uint64_t subtype;
		int rate;
	} ieeerates[] = {
		{ IFM_AUTO,		-1	},
		{ IFM_MANUAL,		0	},
		{ IFM_NONE,		0	},
		{ IFM_IEEE80211_DS1,	2	},
		{ IFM_IEEE80211_DS2,	4	},
		{ IFM_IEEE80211_DS5,	11	},
		{ IFM_IEEE80211_DS11,	22	},
		{ IFM_IEEE80211_DS22,	44	},
		{ IFM_IEEE80211_OFDM6,	12	},
		{ IFM_IEEE80211_OFDM9,	18	},
		{ IFM_IEEE80211_OFDM12,	24	},
		{ IFM_IEEE80211_OFDM18,	36	},
		{ IFM_IEEE80211_OFDM24,	48	},
		{ IFM_IEEE80211_OFDM36,	72	},
		{ IFM_IEEE80211_OFDM48,	96	},
		{ IFM_IEEE80211_OFDM54,	108	},
		{ IFM_IEEE80211_OFDM72,	144	},
	};
	for (i = 0; i < nitems(ieeerates); i++) {
		if (ieeerates[i].subtype == IFM_SUBTYPE(mword))
			return ieeerates[i].rate;
	}
	return 0;
}

/*
 * Convert bit rate (in 0.5Mbps units) to PLCP signal (R4-R1) and vice versa.
 */
u_int8_t
ieee80211_rate2plcp(u_int8_t rate, enum ieee80211_phymode mode)
{
	rate &= IEEE80211_RATE_VAL;

	if (mode == IEEE80211_MODE_11B) {
		/* IEEE Std 802.11b-1999 page 15, subclause 18.2.3.3 */
		switch (rate) {
		case 2:		return 10;
		case 4:		return 20;
		case 11:	return 55;
		case 22:	return 110;
		/* IEEE Std 802.11g-2003 page 19, subclause 19.3.2.1 */
		case 44:	return 220;
		}
	} else if (mode == IEEE80211_MODE_11G || mode == IEEE80211_MODE_11A) {
		/* IEEE Std 802.11a-1999 page 14, subclause 17.3.4.1 */
		switch (rate) {
		case 12:	return 0x0b;
		case 18:	return 0x0f;
		case 24:	return 0x0a;
		case 36:	return 0x0e;
		case 48:	return 0x09;
		case 72:	return 0x0d;
		case 96:	return 0x08;
		case 108:	return 0x0c;
		}
        } else
		panic("%s: unexpected mode %u", __func__, mode);

	DPRINTF(("unsupported rate %u\n", rate));

	return 0;
}

u_int8_t
ieee80211_plcp2rate(u_int8_t plcp, enum ieee80211_phymode mode)
{
	if (mode == IEEE80211_MODE_11B) {
		/* IEEE Std 802.11g-2003 page 19, subclause 19.3.2.1 */
		switch (plcp) {
		case 10:	return 2;
		case 20:	return 4;
		case 55:	return 11;
		case 110:	return 22;
		/* IEEE Std 802.11g-2003 page 19, subclause 19.3.2.1 */
		case 220:	return 44;
		}
	} else if (mode == IEEE80211_MODE_11G || mode == IEEE80211_MODE_11A) {
		/* IEEE Std 802.11a-1999 page 14, subclause 17.3.4.1 */
		switch (plcp) {
		case 0x0b:	return 12;
		case 0x0f:	return 18;
		case 0x0a:	return 24;
		case 0x0e:	return 36;
		case 0x09:	return 48;
		case 0x0d:	return 72;
		case 0x08:	return 96;
		case 0x0c:	return 108;
		}
	} else
		panic("%s: unexpected mode %u", __func__, mode);

	DPRINTF(("unsupported plcp %u\n", plcp));

	return 0;
}
