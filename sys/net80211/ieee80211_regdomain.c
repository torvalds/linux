/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2008 Sam Leffler, Errno Consulting
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * IEEE 802.11 regdomain support.
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>

static void
null_getradiocaps(struct ieee80211com *ic, int maxchan,
	int *n, struct ieee80211_channel *c)
{
	/* just feed back the current channel list */
	if (maxchan > ic->ic_nchans)
		maxchan = ic->ic_nchans;
	memcpy(c, ic->ic_channels, maxchan*sizeof(struct ieee80211_channel));
	*n = maxchan;
}

static int
null_setregdomain(struct ieee80211com *ic,
	struct ieee80211_regdomain *rd,
	int nchans, struct ieee80211_channel chans[])
{
	return 0;		/* accept anything */
}

void
ieee80211_regdomain_attach(struct ieee80211com *ic)
{
	if (ic->ic_regdomain.regdomain == 0 &&
	    ic->ic_regdomain.country == CTRY_DEFAULT) {
		ic->ic_regdomain.location = ' ';		/* both */
		/* NB: driver calls ieee80211_init_channels or similar */
	}
	ic->ic_getradiocaps = null_getradiocaps;
	ic->ic_setregdomain = null_setregdomain;
}

void
ieee80211_regdomain_detach(struct ieee80211com *ic)
{
	if (ic->ic_countryie != NULL) {
		IEEE80211_FREE(ic->ic_countryie, M_80211_NODE_IE);
		ic->ic_countryie = NULL;
	}
}

void
ieee80211_regdomain_vattach(struct ieee80211vap *vap)
{
}

void
ieee80211_regdomain_vdetach(struct ieee80211vap *vap)
{
}

static const uint8_t def_chan_2ghz[] =
    { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
static const uint8_t def_chan_5ghz_band1[] =
    { 36, 40, 44, 48, 52, 56, 60, 64 };
static const uint8_t def_chan_5ghz_band2[] =
    { 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140 };
static const uint8_t def_chan_5ghz_band3[] =
    { 149, 153, 157, 161 };

/*
 * Setup the channel list for the specified regulatory domain,
 * country code, and operating modes.  This interface is used
 * when a driver does not obtain the channel list from another
 * source (such as firmware).
 */
int
ieee80211_init_channels(struct ieee80211com *ic,
	const struct ieee80211_regdomain *rd, const uint8_t bands[])
{
	struct ieee80211_channel *chans = ic->ic_channels;
	int *nchans = &ic->ic_nchans;
	int ht40;

	/* XXX just do something for now */
	ht40 = !!(ic->ic_htcaps & IEEE80211_HTCAP_CHWIDTH40);
	*nchans = 0;
	if (isset(bands, IEEE80211_MODE_11B) ||
	    isset(bands, IEEE80211_MODE_11G) ||
	    isset(bands, IEEE80211_MODE_11NG)) {
		int nchan = nitems(def_chan_2ghz);
		if (!(rd != NULL && rd->ecm))
			nchan -= 3;

		ieee80211_add_channel_list_2ghz(chans, IEEE80211_CHAN_MAX,
		    nchans, def_chan_2ghz, nchan, bands, ht40);
	}
	if (isset(bands, IEEE80211_MODE_11A) ||
	    isset(bands, IEEE80211_MODE_11NA)) {
		ieee80211_add_channel_list_5ghz(chans, IEEE80211_CHAN_MAX,
		    nchans, def_chan_5ghz_band1, nitems(def_chan_5ghz_band1),
		    bands, ht40);
		ieee80211_add_channel_list_5ghz(chans, IEEE80211_CHAN_MAX,
		    nchans, def_chan_5ghz_band2, nitems(def_chan_5ghz_band2),
		    bands, ht40);
		ieee80211_add_channel_list_5ghz(chans, IEEE80211_CHAN_MAX,
		    nchans, def_chan_5ghz_band3, nitems(def_chan_5ghz_band3),
		    bands, ht40);
	}
	if (rd != NULL)
		ic->ic_regdomain = *rd;

	return 0;
}

static __inline int
chancompar(const void *a, const void *b)
{
	const struct ieee80211_channel *ca = a;
	const struct ieee80211_channel *cb = b;

	return (ca->ic_freq == cb->ic_freq) ?
		(ca->ic_flags & IEEE80211_CHAN_ALL) -
		    (cb->ic_flags & IEEE80211_CHAN_ALL) :
		ca->ic_freq - cb->ic_freq;
}

/*
 * Insertion sort.
 */
#define swap(_a, _b, _size) {			\
	uint8_t *s = _b;			\
	int i = _size;				\
	do {					\
		uint8_t tmp = *_a;		\
		*_a++ = *s;			\
		*s++ = tmp;			\
	} while (--i);				\
	_a -= _size;				\
}

static void
sort_channels(void *a, size_t n, size_t size)
{
	uint8_t *aa = a;
	uint8_t *ai, *t;

	KASSERT(n > 0, ("no channels"));
	for (ai = aa+size; --n >= 1; ai += size)
		for (t = ai; t > aa; t -= size) {
			uint8_t *u = t - size;
			if (chancompar(u, t) <= 0)
				break;
			swap(u, t, size);
		}
}
#undef swap

/*
 * Order channels w/ the same frequency so that
 * b < g < htg and a < hta.  This is used to optimize
 * channel table lookups and some user applications
 * may also depend on it (though they should not).
 */
void
ieee80211_sort_channels(struct ieee80211_channel chans[], int nchans)
{
	if (nchans > 0)
		sort_channels(chans, nchans, sizeof(struct ieee80211_channel));
}

/*
 * Allocate and construct a Country Information IE.
 */
struct ieee80211_appie *
ieee80211_alloc_countryie(struct ieee80211com *ic)
{
#define	CHAN_UNINTERESTING \
    (IEEE80211_CHAN_TURBO | IEEE80211_CHAN_STURBO | \
     IEEE80211_CHAN_HALF | IEEE80211_CHAN_QUARTER)
	/* XXX what about auto? */
	/* flag set of channels to be excluded (band added below) */
	static const int skipflags[IEEE80211_MODE_MAX] = {
	    [IEEE80211_MODE_AUTO]	= CHAN_UNINTERESTING,
	    [IEEE80211_MODE_11A]	= CHAN_UNINTERESTING,
	    [IEEE80211_MODE_11B]	= CHAN_UNINTERESTING,
	    [IEEE80211_MODE_11G]	= CHAN_UNINTERESTING,
	    [IEEE80211_MODE_FH]		= CHAN_UNINTERESTING
					| IEEE80211_CHAN_OFDM
					| IEEE80211_CHAN_CCK
					| IEEE80211_CHAN_DYN,
	    [IEEE80211_MODE_TURBO_A]	= CHAN_UNINTERESTING,
	    [IEEE80211_MODE_TURBO_G]	= CHAN_UNINTERESTING,
	    [IEEE80211_MODE_STURBO_A]	= CHAN_UNINTERESTING,
	    [IEEE80211_MODE_HALF]	= IEEE80211_CHAN_TURBO
					| IEEE80211_CHAN_STURBO,
	    [IEEE80211_MODE_QUARTER]	= IEEE80211_CHAN_TURBO
					| IEEE80211_CHAN_STURBO,
	    [IEEE80211_MODE_11NA]	= CHAN_UNINTERESTING,
	    [IEEE80211_MODE_11NG]	= CHAN_UNINTERESTING,
	};
	const struct ieee80211_regdomain *rd = &ic->ic_regdomain;
	uint8_t nextchan, chans[IEEE80211_CHAN_BYTES], *frm;
	struct ieee80211_appie *aie;
	struct ieee80211_country_ie *ie;
	int i, skip, nruns;

	aie = IEEE80211_MALLOC(IEEE80211_COUNTRY_MAX_SIZE, M_80211_NODE_IE,
	    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (aie == NULL) {
		ic_printf(ic, "%s: unable to allocate memory for country ie\n",
		    __func__);
		/* XXX stat */
		return NULL;
	}
	ie = (struct ieee80211_country_ie *) aie->ie_data;
	ie->ie = IEEE80211_ELEMID_COUNTRY;
	if (rd->isocc[0] == '\0') {
		ic_printf(ic, "no ISO country string for cc %d; using blanks\n",
		    rd->country);
		ie->cc[0] = ie->cc[1] = ' ';
	} else {
		ie->cc[0] = rd->isocc[0];
		ie->cc[1] = rd->isocc[1];
	}
	/* 
	 * Indoor/Outdoor portion of country string:
	 *     'I' indoor only
	 *     'O' outdoor only
	 *     ' ' all environments
	 */
	ie->cc[2] = (rd->location == 'I' ? 'I' :
		     rd->location == 'O' ? 'O' : ' ');
	/* 
	 * Run-length encoded channel+max tx power info.
	 */
	frm = (uint8_t *)&ie->band[0];
	nextchan = 0;			/* NB: impossible channel # */
	nruns = 0;
	memset(chans, 0, sizeof(chans));
	skip = skipflags[ieee80211_chan2mode(ic->ic_bsschan)];
	if (IEEE80211_IS_CHAN_5GHZ(ic->ic_bsschan))
		skip |= IEEE80211_CHAN_2GHZ;
	else if (IEEE80211_IS_CHAN_2GHZ(ic->ic_bsschan))
		skip |= IEEE80211_CHAN_5GHZ;
	for (i = 0; i < ic->ic_nchans; i++) {
		const struct ieee80211_channel *c = &ic->ic_channels[i];

		if (isset(chans, c->ic_ieee))		/* suppress dup's */
			continue;
		if (c->ic_flags & skip)			/* skip band, etc. */
			continue;
		setbit(chans, c->ic_ieee);
		if (c->ic_ieee != nextchan ||
		    c->ic_maxregpower != frm[-1]) {	/* new run */
			if (nruns == IEEE80211_COUNTRY_MAX_BANDS) {
				ic_printf(ic, "%s: country ie too big, "
				    "runs > max %d, truncating\n",
				    __func__, IEEE80211_COUNTRY_MAX_BANDS);
				/* XXX stat? fail? */
				break;
			}
			frm[0] = c->ic_ieee;		/* starting channel # */
			frm[1] = 1;			/* # channels in run */
			frm[2] = c->ic_maxregpower;	/* tx power cap */
			frm += 3;
			nextchan = c->ic_ieee + 1;	/* overflow? */
			nruns++;
		} else {				/* extend run */
			frm[-2]++;
			nextchan++;
		}
	}
	ie->len = frm - ie->cc;
	if (ie->len & 1) {		/* Zero pad to multiple of 2 */
		ie->len++;
		*frm++ = 0;
	}
	aie->ie_len = frm - aie->ie_data;

	return aie;
#undef CHAN_UNINTERESTING
}

static int
allvapsdown(struct ieee80211com *ic)
{
	struct ieee80211vap *vap;

	IEEE80211_LOCK_ASSERT(ic);
	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next)
		if (vap->iv_state != IEEE80211_S_INIT)
			return 0;
	return 1;
}

int
ieee80211_setregdomain(struct ieee80211vap *vap,
    struct ieee80211_regdomain_req *reg)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_channel *c;
	int desfreq = 0, desflags = 0;		/* XXX silence gcc complaint */
	int error, i;

	if (reg->rd.location != 'I' && reg->rd.location != 'O' &&
	    reg->rd.location != ' ') {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_IOCTL,
		    "%s: invalid location 0x%x\n", __func__, reg->rd.location);
		return EINVAL;
	}
	if (reg->rd.isocc[0] == '\0' || reg->rd.isocc[1] == '\0') {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_IOCTL,
		    "%s: invalid iso cc 0x%x:0x%x\n", __func__,
		    reg->rd.isocc[0], reg->rd.isocc[1]);
		return EINVAL;
	}
	if (reg->chaninfo.ic_nchans > IEEE80211_CHAN_MAX) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_IOCTL,
		    "%s: too many channels %u, max %u\n", __func__,
		    reg->chaninfo.ic_nchans, IEEE80211_CHAN_MAX);
		return EINVAL;
	}
	/*
	 * Calculate freq<->IEEE mapping and default max tx power
	 * for channels not setup.  The driver can override these
	 * setting to reflect device properties/requirements.
	 */
	for (i = 0; i < reg->chaninfo.ic_nchans; i++) {
		c = &reg->chaninfo.ic_chans[i];
		if (c->ic_freq == 0 || c->ic_flags == 0) {
			IEEE80211_DPRINTF(vap, IEEE80211_MSG_IOCTL,
			    "%s: invalid channel spec at [%u]\n", __func__, i);
			return EINVAL;
		}
		if (c->ic_maxregpower == 0) {
			IEEE80211_DPRINTF(vap, IEEE80211_MSG_IOCTL,
			    "%s: invalid channel spec, zero maxregpower, "
			    "freq %u flags 0x%x\n", __func__,
			    c->ic_freq, c->ic_flags);
			return EINVAL;
		}
		if (c->ic_ieee == 0)
			c->ic_ieee = ieee80211_mhz2ieee(c->ic_freq,c->ic_flags);
		if (IEEE80211_IS_CHAN_HT40(c) && c->ic_extieee == 0)
			c->ic_extieee = ieee80211_mhz2ieee(c->ic_freq +
			    (IEEE80211_IS_CHAN_HT40U(c) ? 20 : -20),
			    c->ic_flags);
		if (c->ic_maxpower == 0)
			c->ic_maxpower = 2*c->ic_maxregpower;
	}
	IEEE80211_LOCK(ic);
	/* XXX bandaid; a running vap will likely crash */
	if (!allvapsdown(ic)) {
		IEEE80211_UNLOCK(ic);
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_IOCTL,
		    "%s: reject: vaps are running\n", __func__);
		return EBUSY;
	}
	error = ic->ic_setregdomain(ic, &reg->rd,
	    reg->chaninfo.ic_nchans, reg->chaninfo.ic_chans);
	if (error != 0) {
		IEEE80211_UNLOCK(ic);
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_IOCTL,
		    "%s: driver rejected request, error %u\n", __func__, error);
		return error;
	}
	/*
	 * Commit: copy in new channel table and reset media state.
	 * On return the state machines will be clocked so all vaps
	 * will reset their state.
	 *
	 * XXX ic_bsschan is marked undefined, must have vap's in
	 *     INIT state or we blow up forcing stations off
	 */
	/*
	 * Save any desired channel for restore below.  Note this
	 * needs to be done for all vaps but for now we only do
	 * the one where the ioctl is issued.
	 */
	if (vap->iv_des_chan != IEEE80211_CHAN_ANYC) {
		desfreq = vap->iv_des_chan->ic_freq;
		desflags = vap->iv_des_chan->ic_flags;
	}
	/* regdomain parameters */
	memcpy(&ic->ic_regdomain, &reg->rd, sizeof(reg->rd));
	/* channel table */
	memcpy(ic->ic_channels, reg->chaninfo.ic_chans,
	    reg->chaninfo.ic_nchans * sizeof(struct ieee80211_channel));
	ic->ic_nchans = reg->chaninfo.ic_nchans;
	memset(&ic->ic_channels[ic->ic_nchans], 0,
	    (IEEE80211_CHAN_MAX - ic->ic_nchans) *
	       sizeof(struct ieee80211_channel));
	ieee80211_chan_init(ic);

	/*
	 * Invalidate channel-related state.
	 */
	if (ic->ic_countryie != NULL) {
		IEEE80211_FREE(ic->ic_countryie, M_80211_NODE_IE);
		ic->ic_countryie = NULL;
	}
	ieee80211_scan_flush(vap);
	ieee80211_dfs_reset(ic);
	if (vap->iv_des_chan != IEEE80211_CHAN_ANYC) {
		c = ieee80211_find_channel(ic, desfreq, desflags);
		/* NB: may be NULL if not present in new channel list */
		vap->iv_des_chan = (c != NULL) ? c : IEEE80211_CHAN_ANYC;
	}
	IEEE80211_UNLOCK(ic);

	return 0;
}
