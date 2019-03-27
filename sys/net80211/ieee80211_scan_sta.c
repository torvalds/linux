/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
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
 * IEEE 802.11 station scanning support.
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_input.h>
#include <net80211/ieee80211_regdomain.h>
#ifdef IEEE80211_SUPPORT_TDMA
#include <net80211/ieee80211_tdma.h>
#endif
#ifdef IEEE80211_SUPPORT_MESH
#include <net80211/ieee80211_mesh.h>
#endif
#include <net80211/ieee80211_ratectl.h>
#include <net80211/ieee80211_vht.h>

#include <net/bpf.h>

/*
 * Parameters for managing cache entries:
 *
 * o a station with STA_FAILS_MAX failures is not considered
 *   when picking a candidate
 * o a station that hasn't had an update in STA_PURGE_SCANS
 *   (background) scans is discarded
 * o after STA_FAILS_AGE seconds we clear the failure count
 */
#define	STA_FAILS_MAX	2		/* assoc failures before ignored */
#define	STA_FAILS_AGE	(2*60)		/* time before clearing fails (secs) */
#define	STA_PURGE_SCANS	2		/* age for purging entries (scans) */

/* XXX tunable */
#define	STA_RSSI_MIN	8		/* min acceptable rssi */
#define	STA_RSSI_MAX	40		/* max rssi for comparison */

struct sta_entry {
	struct ieee80211_scan_entry base;
	TAILQ_ENTRY(sta_entry) se_list;
	LIST_ENTRY(sta_entry) se_hash;
	uint8_t		se_fails;		/* failure to associate count */
	uint8_t		se_seen;		/* seen during current scan */
	uint8_t		se_notseen;		/* not seen in previous scans */
	uint8_t		se_flags;
#define	STA_DEMOTE11B	0x01			/* match w/ demoted 11b chan */
	uint32_t	se_avgrssi;		/* LPF rssi state */
	unsigned long	se_lastupdate;		/* time of last update */
	unsigned long	se_lastfail;		/* time of last failure */
	unsigned long	se_lastassoc;		/* time of last association */
	u_int		se_scangen;		/* iterator scan gen# */
	u_int		se_countrygen;		/* gen# of last cc notify */
};

#define	STA_HASHSIZE	32
/* simple hash is enough for variation of macaddr */
#define	STA_HASH(addr)	\
	(((const uint8_t *)(addr))[IEEE80211_ADDR_LEN - 1] % STA_HASHSIZE)

#define	MAX_IEEE_CHAN	256			/* max acceptable IEEE chan # */
CTASSERT(MAX_IEEE_CHAN >= 256);

struct sta_table {
	ieee80211_scan_table_lock_t st_lock;	/* on scan table */
	TAILQ_HEAD(, sta_entry) st_entry;	/* all entries */
	LIST_HEAD(, sta_entry) st_hash[STA_HASHSIZE];
	ieee80211_scan_iter_lock_t st_scanlock;		/* on st_scaniter */
	u_int		st_scaniter;		/* gen# for iterator */
	u_int		st_scangen;		/* scan generation # */
	int		st_newscan;
	/* ap-related state */
	int		st_maxrssi[MAX_IEEE_CHAN];
};

static void sta_flush_table(struct sta_table *);
/*
 * match_bss returns a bitmask describing if an entry is suitable
 * for use.  If non-zero the entry was deemed not suitable and it's
 * contents explains why.  The following flags are or'd to this
 * mask and can be used to figure out why the entry was rejected.
 */
#define	MATCH_CHANNEL		0x00001	/* channel mismatch */
#define	MATCH_CAPINFO		0x00002	/* capabilities mismatch, e.g. no ess */
#define	MATCH_PRIVACY		0x00004	/* privacy mismatch */
#define	MATCH_RATE		0x00008	/* rate set mismatch */
#define	MATCH_SSID		0x00010	/* ssid mismatch */
#define	MATCH_BSSID		0x00020	/* bssid mismatch */
#define	MATCH_FAILS		0x00040	/* too many failed auth attempts */
#define	MATCH_NOTSEEN		0x00080	/* not seen in recent scans */
#define	MATCH_RSSI		0x00100	/* rssi deemed too low to use */
#define	MATCH_CC		0x00200	/* country code mismatch */
#ifdef IEEE80211_SUPPORT_TDMA
#define	MATCH_TDMA_NOIE		0x00400	/* no TDMA ie */
#define	MATCH_TDMA_NOTMASTER	0x00800	/* not TDMA master */
#define	MATCH_TDMA_NOSLOT	0x01000	/* all TDMA slots occupied */
#define	MATCH_TDMA_LOCAL	0x02000	/* local address */
#define	MATCH_TDMA_VERSION	0x04000	/* protocol version mismatch */
#endif
#define	MATCH_MESH_NOID		0x10000	/* no MESHID ie */
#define	MATCH_MESHID		0x20000	/* meshid mismatch */
static int match_bss(struct ieee80211vap *,
	const struct ieee80211_scan_state *, struct sta_entry *, int);
static void adhoc_age(struct ieee80211_scan_state *);

static __inline int
isocmp(const uint8_t cc1[], const uint8_t cc2[])
{
     return (cc1[0] == cc2[0] && cc1[1] == cc2[1]);
}

/* number of references from net80211 layer */
static	int nrefs = 0;
/*
 * Module glue.
 */
IEEE80211_SCANNER_MODULE(sta, 1);

/*
 * Attach prior to any scanning work.
 */
static int
sta_attach(struct ieee80211_scan_state *ss)
{
	struct sta_table *st;

	st = (struct sta_table *) IEEE80211_MALLOC(sizeof(struct sta_table),
		M_80211_SCAN,
		IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (st == NULL)
		return 0;
	IEEE80211_SCAN_TABLE_LOCK_INIT(st, "scantable");
	IEEE80211_SCAN_ITER_LOCK_INIT(st, "scangen");
	TAILQ_INIT(&st->st_entry);
	ss->ss_priv = st;
	nrefs++;			/* NB: we assume caller locking */
	return 1;
}

/*
 * Cleanup any private state.
 */
static int
sta_detach(struct ieee80211_scan_state *ss)
{
	struct sta_table *st = ss->ss_priv;

	if (st != NULL) {
		sta_flush_table(st);
		IEEE80211_SCAN_TABLE_LOCK_DESTROY(st);
		IEEE80211_SCAN_ITER_LOCK_DESTROY(st);
		IEEE80211_FREE(st, M_80211_SCAN);
		KASSERT(nrefs > 0, ("imbalanced attach/detach"));
		nrefs--;		/* NB: we assume caller locking */
	}
	return 1;
}

/*
 * Flush all per-scan state.
 */
static int
sta_flush(struct ieee80211_scan_state *ss)
{
	struct sta_table *st = ss->ss_priv;

	IEEE80211_SCAN_TABLE_LOCK(st);
	sta_flush_table(st);
	IEEE80211_SCAN_TABLE_UNLOCK(st);
	ss->ss_last = 0;
	return 0;
}

/*
 * Flush all entries in the scan cache.
 */
static void
sta_flush_table(struct sta_table *st)
{
	struct sta_entry *se, *next;

	TAILQ_FOREACH_SAFE(se, &st->st_entry, se_list, next) {
		TAILQ_REMOVE(&st->st_entry, se, se_list);
		LIST_REMOVE(se, se_hash);
		ieee80211_ies_cleanup(&se->base.se_ies);
		IEEE80211_FREE(se, M_80211_SCAN);
	}
	memset(st->st_maxrssi, 0, sizeof(st->st_maxrssi));
}

/*
 * Process a beacon or probe response frame; create an
 * entry in the scan cache or update any previous entry.
 */
static int
sta_add(struct ieee80211_scan_state *ss, 
	struct ieee80211_channel *curchan,
	const struct ieee80211_scanparams *sp,
	const struct ieee80211_frame *wh,
	int subtype, int rssi, int noise)
{
#define	ISPROBE(_st)	((_st) == IEEE80211_FC0_SUBTYPE_PROBE_RESP)
#define	PICK1ST(_ss) \
	((ss->ss_flags & (IEEE80211_SCAN_PICK1ST | IEEE80211_SCAN_GOTPICK)) == \
	IEEE80211_SCAN_PICK1ST)
	struct sta_table *st = ss->ss_priv;
	const uint8_t *macaddr = wh->i_addr2;
	struct ieee80211vap *vap = ss->ss_vap;
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_channel *c;
	struct sta_entry *se;
	struct ieee80211_scan_entry *ise;
	int hash;

	hash = STA_HASH(macaddr);

	IEEE80211_SCAN_TABLE_LOCK(st);
	LIST_FOREACH(se, &st->st_hash[hash], se_hash)
		if (IEEE80211_ADDR_EQ(se->base.se_macaddr, macaddr))
			goto found;
	se = (struct sta_entry *) IEEE80211_MALLOC(sizeof(struct sta_entry),
		M_80211_SCAN, IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (se == NULL) {
		IEEE80211_SCAN_TABLE_UNLOCK(st);
		return 0;
	}
	se->se_scangen = st->st_scaniter-1;
	se->se_avgrssi = IEEE80211_RSSI_DUMMY_MARKER;
	IEEE80211_ADDR_COPY(se->base.se_macaddr, macaddr);
	TAILQ_INSERT_TAIL(&st->st_entry, se, se_list);
	LIST_INSERT_HEAD(&st->st_hash[hash], se, se_hash);
found:
	ise = &se->base;
	/* XXX ap beaconing multiple ssid w/ same bssid */
	if (sp->ssid[1] != 0 &&
	    (ISPROBE(subtype) || ise->se_ssid[1] == 0))
		memcpy(ise->se_ssid, sp->ssid, 2+sp->ssid[1]);
	KASSERT(sp->rates[1] <= IEEE80211_RATE_MAXSIZE,
		("rate set too large: %u", sp->rates[1]));
	memcpy(ise->se_rates, sp->rates, 2+sp->rates[1]);
	if (sp->xrates != NULL) {
		/* XXX validate xrates[1] */
		KASSERT(sp->xrates[1] <= IEEE80211_RATE_MAXSIZE,
			("xrate set too large: %u", sp->xrates[1]));
		memcpy(ise->se_xrates, sp->xrates, 2+sp->xrates[1]);
	} else
		ise->se_xrates[1] = 0;
	IEEE80211_ADDR_COPY(ise->se_bssid, wh->i_addr3);
	if ((sp->status & IEEE80211_BPARSE_OFFCHAN) == 0) {
		/*
		 * Record rssi data using extended precision LPF filter.
		 *
		 * NB: use only on-channel data to insure we get a good
		 *     estimate of the signal we'll see when associated.
		 */
		IEEE80211_RSSI_LPF(se->se_avgrssi, rssi);
		ise->se_rssi = IEEE80211_RSSI_GET(se->se_avgrssi);
		ise->se_noise = noise;
	}
	memcpy(ise->se_tstamp.data, sp->tstamp, sizeof(ise->se_tstamp));
	ise->se_intval = sp->bintval;
	ise->se_capinfo = sp->capinfo;
#ifdef IEEE80211_SUPPORT_MESH
	if (sp->meshid != NULL && sp->meshid[1] != 0)
		memcpy(ise->se_meshid, sp->meshid, 2+sp->meshid[1]);
#endif
	/*
	 * Beware of overriding se_chan for frames seen
	 * off-channel; this can cause us to attempt an
	 * association on the wrong channel.
	 */
	if (sp->status & IEEE80211_BPARSE_OFFCHAN) {
		/*
		 * Off-channel, locate the home/bss channel for the sta
		 * using the value broadcast in the DSPARMS ie.  We know
		 * sp->chan has this value because it's used to calculate
		 * IEEE80211_BPARSE_OFFCHAN.
		 */
		c = ieee80211_find_channel_byieee(ic, sp->chan,
		    curchan->ic_flags);
		if (c != NULL) {
			ise->se_chan = c;
		} else if (ise->se_chan == NULL) {
			/* should not happen, pick something */
			ise->se_chan = curchan;
		}
	} else
		ise->se_chan = curchan;

	/* VHT demotion */
	if (IEEE80211_IS_CHAN_VHT(ise->se_chan) && sp->vhtcap == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_11N,
		    "%s: demoting VHT->HT %d/0x%08x\n",
		    __func__, ise->se_chan->ic_freq, ise->se_chan->ic_flags);
		/* Demote legacy networks to a non-VHT channel. */
		c = ieee80211_find_channel(ic, ise->se_chan->ic_freq,
		    ise->se_chan->ic_flags & ~IEEE80211_CHAN_VHT);
		KASSERT(c != NULL,
		    ("no non-VHT channel %u", ise->se_chan->ic_ieee));
		ise->se_chan = c;
	}

	/* HT demotion */
	if (IEEE80211_IS_CHAN_HT(ise->se_chan) && sp->htcap == NULL) {
		/* Demote legacy networks to a non-HT channel. */
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_11N,
		    "%s: demoting HT->legacy %d/0x%08x\n",
		    __func__, ise->se_chan->ic_freq, ise->se_chan->ic_flags);
		c = ieee80211_find_channel(ic, ise->se_chan->ic_freq,
		    ise->se_chan->ic_flags & ~IEEE80211_CHAN_HT);
		KASSERT(c != NULL,
		    ("no legacy channel %u", ise->se_chan->ic_ieee));
		ise->se_chan = c;
	}

	ise->se_fhdwell = sp->fhdwell;
	ise->se_fhindex = sp->fhindex;
	ise->se_erp = sp->erp;
	ise->se_timoff = sp->timoff;
	if (sp->tim != NULL) {
		const struct ieee80211_tim_ie *tim =
		    (const struct ieee80211_tim_ie *) sp->tim;
		ise->se_dtimperiod = tim->tim_period;
	}
	if (sp->country != NULL) {
		const struct ieee80211_country_ie *cie =
		    (const struct ieee80211_country_ie *) sp->country;
		/*
		 * If 11d is enabled and we're attempting to join a bss
		 * that advertises it's country code then compare our
		 * current settings to what we fetched from the country ie.
		 * If our country code is unspecified or different then
		 * dispatch an event to user space that identifies the
		 * country code so our regdomain config can be changed.
		 */
		/* XXX only for STA mode? */
		if ((IEEE80211_IS_CHAN_11D(ise->se_chan) ||
		    (vap->iv_flags_ext & IEEE80211_FEXT_DOTD)) &&
		    (ic->ic_regdomain.country == CTRY_DEFAULT ||
		     !isocmp(cie->cc, ic->ic_regdomain.isocc))) {
			/* only issue one notify event per scan */
			if (se->se_countrygen != st->st_scangen) {
				ieee80211_notify_country(vap, ise->se_bssid,
				    cie->cc);
				se->se_countrygen = st->st_scangen;
			}
		}
		ise->se_cc[0] = cie->cc[0];
		ise->se_cc[1] = cie->cc[1];
	}
	/* NB: no need to setup ie ptrs; they are not (currently) used */
	(void) ieee80211_ies_init(&ise->se_ies, sp->ies, sp->ies_len);

	/* clear failure count after STA_FAIL_AGE passes */
	if (se->se_fails && (ticks - se->se_lastfail) > STA_FAILS_AGE*hz) {
		se->se_fails = 0;
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_SCAN, macaddr,
		    "%s: fails %u", __func__, se->se_fails);
	}

	se->se_lastupdate = ticks;		/* update time */
	se->se_seen = 1;
	se->se_notseen = 0;

	KASSERT(sizeof(sp->bchan) == 1, ("bchan size"));
	if (rssi > st->st_maxrssi[sp->bchan])
		st->st_maxrssi[sp->bchan] = rssi;

	IEEE80211_SCAN_TABLE_UNLOCK(st);

	/*
	 * If looking for a quick choice and nothing's
	 * been found check here.
	 */
	if (PICK1ST(ss) && match_bss(vap, ss, se, IEEE80211_MSG_SCAN) == 0)
		ss->ss_flags |= IEEE80211_SCAN_GOTPICK;

	return 1;
#undef PICK1ST
#undef ISPROBE
}

/*
 * Check if a channel is excluded by user request.
 */
static int
isexcluded(struct ieee80211vap *vap, const struct ieee80211_channel *c)
{
	return (isclr(vap->iv_ic->ic_chan_active, c->ic_ieee) ||
	    (vap->iv_des_chan != IEEE80211_CHAN_ANYC &&
	     c->ic_freq != vap->iv_des_chan->ic_freq));
}

static struct ieee80211_channel *
find11gchannel(struct ieee80211com *ic, int i, int freq)
{
	struct ieee80211_channel *c;
	int j;

	/*
	 * The normal ordering in the channel list is b channel
	 * immediately followed by g so optimize the search for
	 * this.  We'll still do a full search just in case.
	 */
	for (j = i+1; j < ic->ic_nchans; j++) {
		c = &ic->ic_channels[j];
		if (c->ic_freq == freq && IEEE80211_IS_CHAN_G(c))
			return c;
	}
	for (j = 0; j < i; j++) {
		c = &ic->ic_channels[j];
		if (c->ic_freq == freq && IEEE80211_IS_CHAN_G(c))
			return c;
	}
	return NULL;
}

static const u_int chanflags[IEEE80211_MODE_MAX] = {
	[IEEE80211_MODE_AUTO]	  = IEEE80211_CHAN_B,
	[IEEE80211_MODE_11A]	  = IEEE80211_CHAN_A,
	[IEEE80211_MODE_11B]	  = IEEE80211_CHAN_B,
	[IEEE80211_MODE_11G]	  = IEEE80211_CHAN_G,
	[IEEE80211_MODE_FH]	  = IEEE80211_CHAN_FHSS,
	/* check base channel */
	[IEEE80211_MODE_TURBO_A]  = IEEE80211_CHAN_A,
	[IEEE80211_MODE_TURBO_G]  = IEEE80211_CHAN_G,
	[IEEE80211_MODE_STURBO_A] = IEEE80211_CHAN_ST,
	[IEEE80211_MODE_HALF]	  = IEEE80211_CHAN_HALF,
	[IEEE80211_MODE_QUARTER]  = IEEE80211_CHAN_QUARTER,
	/* check legacy */
	[IEEE80211_MODE_11NA]	  = IEEE80211_CHAN_A,
	[IEEE80211_MODE_11NG]	  = IEEE80211_CHAN_G,
	[IEEE80211_MODE_VHT_5GHZ] = IEEE80211_CHAN_A,
	[IEEE80211_MODE_VHT_2GHZ] = IEEE80211_CHAN_G,
};

static void
add_channels(struct ieee80211vap *vap,
	struct ieee80211_scan_state *ss,
	enum ieee80211_phymode mode, const uint16_t freq[], int nfreq)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_channel *c, *cg;
	u_int modeflags;
	int i;

	KASSERT(mode < nitems(chanflags), ("Unexpected mode %u", mode));
	modeflags = chanflags[mode];
	for (i = 0; i < nfreq; i++) {
		if (ss->ss_last >= IEEE80211_SCAN_MAX)
			break;

		c = ieee80211_find_channel(ic, freq[i], modeflags);
		if (c == NULL || isexcluded(vap, c))
			continue;
		if (mode == IEEE80211_MODE_AUTO) {
			KASSERT(IEEE80211_IS_CHAN_B(c),
			    ("%s: wrong channel for 'auto' mode %u / %u\n",
			    __func__, c->ic_freq, c->ic_flags));

			/*
			 * XXX special-case 11b/g channels so we select
			 *     the g channel if both are present.
			 */
			if ((cg = find11gchannel(ic, i, c->ic_freq)) != NULL)
				c = cg;
		}
		ss->ss_chans[ss->ss_last++] = c;
	}
}

struct scanlist {
	uint16_t	mode;
	uint16_t	count;
	const uint16_t	*list;
};

static int
checktable(const struct scanlist *scan, const struct ieee80211_channel *c)
{
	int i;

	for (; scan->list != NULL; scan++) {
		for (i = 0; i < scan->count; i++)
			if (scan->list[i] == c->ic_freq) 
				return 1;
	}
	return 0;
}

static int
onscanlist(const struct ieee80211_scan_state *ss,
	const struct ieee80211_channel *c)
{
	int i;

	for (i = 0; i < ss->ss_last; i++)
		if (ss->ss_chans[i] == c)
			return 1;
	return 0;
}

static void
sweepchannels(struct ieee80211_scan_state *ss, struct ieee80211vap *vap,
	const struct scanlist table[])
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_channel *c;
	int i;

	for (i = 0; i < ic->ic_nchans; i++) {
		if (ss->ss_last >= IEEE80211_SCAN_MAX)
			break;

		c = &ic->ic_channels[i];
		/*
		 * Ignore dynamic turbo channels; we scan them
		 * in normal mode (i.e. not boosted).  Likewise
		 * for HT/VHT channels, they get scanned using
		 * legacy rates.
		 */
		if (IEEE80211_IS_CHAN_DTURBO(c) || IEEE80211_IS_CHAN_HT(c) ||
		    IEEE80211_IS_CHAN_VHT(c))
			continue;

		/*
		 * If a desired mode was specified, scan only 
		 * channels that satisfy that constraint.
		 */
		if (vap->iv_des_mode != IEEE80211_MODE_AUTO &&
		    vap->iv_des_mode != ieee80211_chan2mode(c))
			continue;

		/*
		 * Skip channels excluded by user request.
		 */
		if (isexcluded(vap, c))
			continue;

		/*
		 * Add the channel unless it is listed in the
		 * fixed scan order tables.  This insures we
		 * don't sweep back in channels we filtered out
		 * above.
		 */
		if (checktable(table, c))
			continue;

		/* Add channel to scanning list. */
		ss->ss_chans[ss->ss_last++] = c;
	}
	/*
	 * Explicitly add any desired channel if:
	 * - not already on the scan list
	 * - allowed by any desired mode constraint
	 * - there is space in the scan list
	 * This allows the channel to be used when the filtering
	 * mechanisms would otherwise elide it (e.g HT, turbo).
	 */
	c = vap->iv_des_chan;
	if (c != IEEE80211_CHAN_ANYC &&
	    !onscanlist(ss, c) &&
	    (vap->iv_des_mode == IEEE80211_MODE_AUTO ||
	     vap->iv_des_mode == ieee80211_chan2mode(c)) &&
	    ss->ss_last < IEEE80211_SCAN_MAX)
		ss->ss_chans[ss->ss_last++] = c;
}

static void
makescanlist(struct ieee80211_scan_state *ss, struct ieee80211vap *vap,
	const struct scanlist table[])
{
	const struct scanlist *scan;
	enum ieee80211_phymode mode;

	ss->ss_last = 0;
	/*
	 * Use the table of ordered channels to construct the list
	 * of channels for scanning.  Any channels in the ordered
	 * list not in the master list will be discarded.
	 */
	for (scan = table; scan->list != NULL; scan++) {
		mode = scan->mode;

		switch (mode) {
		case IEEE80211_MODE_11B:
			if (vap->iv_des_mode == IEEE80211_MODE_11B)
				break;

			/*
			 * The scan table marks 2.4Ghz channels as b
			 * so if the desired mode is 11g / 11ng / 11acg,
			 * then use the 11b channel list but upgrade the mode.
			 *
			 * NB: 11b -> AUTO lets add_channels upgrade an
			 * 11b channel to 11g if available.
			 */
			if (vap->iv_des_mode == IEEE80211_MODE_AUTO ||
			    vap->iv_des_mode == IEEE80211_MODE_11G ||
			    vap->iv_des_mode == IEEE80211_MODE_11NG ||
			    vap->iv_des_mode == IEEE80211_MODE_VHT_2GHZ) {
				mode = vap->iv_des_mode;
				break;
			}

			continue;
		case IEEE80211_MODE_11A:
			/* Use 11a channel list for 11na / 11ac modes */
			if (vap->iv_des_mode == IEEE80211_MODE_11NA ||
			    vap->iv_des_mode == IEEE80211_MODE_VHT_5GHZ) {
				mode = vap->iv_des_mode;
				break;
			}

			/* FALLTHROUGH */
		default:
			/*
			 * If a desired mode was specified, scan only
			 * channels that satisfy that constraint.
			 */
			if (vap->iv_des_mode != IEEE80211_MODE_AUTO &&
			    vap->iv_des_mode != mode)
				continue;
		}

#ifdef IEEE80211_F_XR
		/* XR does not operate on turbo channels */
		if ((vap->iv_flags & IEEE80211_F_XR) &&
		    (mode == IEEE80211_MODE_TURBO_A ||
		     mode == IEEE80211_MODE_TURBO_G ||
		     mode == IEEE80211_MODE_STURBO_A))
			continue;
#endif
		/*
		 * Add the list of the channels; any that are not
		 * in the master channel list will be discarded.
		 */
		add_channels(vap, ss, mode, scan->list, scan->count);
	}

	/*
	 * Add the channels from the ic that are not present
	 * in the table.
	 */
	sweepchannels(ss, vap, table);
}

static const uint16_t rcl1[] =		/* 8 FCC channel: 52, 56, 60, 64, 36, 40, 44, 48 */
{ 5260, 5280, 5300, 5320, 5180, 5200, 5220, 5240 };
static const uint16_t rcl2[] =		/* 4 MKK channels: 34, 38, 42, 46 */
{ 5170, 5190, 5210, 5230 };
static const uint16_t rcl3[] =		/* 2.4Ghz ch: 1,6,11,7,13 */
{ 2412, 2437, 2462, 2442, 2472 };
static const uint16_t rcl4[] =		/* 5 FCC channel: 149, 153, 161, 165 */
{ 5745, 5765, 5785, 5805, 5825 };
static const uint16_t rcl7[] =		/* 11 ETSI channel: 100,104,108,112,116,120,124,128,132,136,140 */
{ 5500, 5520, 5540, 5560, 5580, 5600, 5620, 5640, 5660, 5680, 5700 };
static const uint16_t rcl8[] =		/* 2.4Ghz ch: 2,3,4,5,8,9,10,12 */
{ 2417, 2422, 2427, 2432, 2447, 2452, 2457, 2467 };
static const uint16_t rcl9[] =		/* 2.4Ghz ch: 14 */
{ 2484 };
static const uint16_t rcl10[] =	/* Added Korean channels 2312-2372 */
{ 2312, 2317, 2322, 2327, 2332, 2337, 2342, 2347, 2352, 2357, 2362, 2367, 2372 };
static const uint16_t rcl11[] =	/* Added Japan channels in 4.9/5.0 spectrum */
{ 5040, 5060, 5080, 4920, 4940, 4960, 4980 };
#ifdef ATH_TURBO_SCAN
static const uint16_t rcl5[] =		/* 3 static turbo channels */
{ 5210, 5250, 5290 };
static const uint16_t rcl6[] =		/* 2 static turbo channels */
{ 5760, 5800 };
static const uint16_t rcl6x[] =	/* 4 FCC3 turbo channels */
{ 5540, 5580, 5620, 5660 };
static const uint16_t rcl12[] =	/* 2.4Ghz Turbo channel 6 */
{ 2437 };
static const uint16_t rcl13[] =	/* dynamic Turbo channels */
{ 5200, 5240, 5280, 5765, 5805 };
#endif /* ATH_TURBO_SCAN */

#define	X(a)	.count = sizeof(a)/sizeof(a[0]), .list = a

static const struct scanlist staScanTable[] = {
	{ IEEE80211_MODE_11B,   	X(rcl3) },
	{ IEEE80211_MODE_11A,   	X(rcl1) },
	{ IEEE80211_MODE_11A,   	X(rcl2) },
	{ IEEE80211_MODE_11B,   	X(rcl8) },
	{ IEEE80211_MODE_11B,   	X(rcl9) },
	{ IEEE80211_MODE_11A,   	X(rcl4) },
#ifdef ATH_TURBO_SCAN
	{ IEEE80211_MODE_STURBO_A,	X(rcl5) },
	{ IEEE80211_MODE_STURBO_A,	X(rcl6) },
	{ IEEE80211_MODE_TURBO_A,	X(rcl6x) },
	{ IEEE80211_MODE_TURBO_A,	X(rcl13) },
#endif /* ATH_TURBO_SCAN */
	{ IEEE80211_MODE_11A,		X(rcl7) },
	{ IEEE80211_MODE_11B,		X(rcl10) },
	{ IEEE80211_MODE_11A,		X(rcl11) },
#ifdef ATH_TURBO_SCAN
	{ IEEE80211_MODE_TURBO_G,	X(rcl12) },
#endif /* ATH_TURBO_SCAN */
	{ .list = NULL }
};

/*
 * Start a station-mode scan by populating the channel list.
 */
static int
sta_start(struct ieee80211_scan_state *ss, struct ieee80211vap *vap)
{
	struct sta_table *st = ss->ss_priv;

	makescanlist(ss, vap, staScanTable);

	if (ss->ss_mindwell == 0)
		ss->ss_mindwell = msecs_to_ticks(20);	/* 20ms */
	if (ss->ss_maxdwell == 0)
		ss->ss_maxdwell = msecs_to_ticks(200);	/* 200ms */

	st->st_scangen++;
	st->st_newscan = 1;

	return 0;
}

/*
 * Restart a scan, typically a bg scan but can
 * also be a fg scan that came up empty.
 */
static int
sta_restart(struct ieee80211_scan_state *ss, struct ieee80211vap *vap)
{
	struct sta_table *st = ss->ss_priv;

	st->st_newscan = 1;
	return 0;
}

/*
 * Cancel an ongoing scan.
 */
static int
sta_cancel(struct ieee80211_scan_state *ss, struct ieee80211vap *vap)
{
	return 0;
}

/*
 * Demote any supplied 11g channel to 11b.  There should
 * always be an 11b channel but we check anyway...
 */
static struct ieee80211_channel *
demote11b(struct ieee80211vap *vap, struct ieee80211_channel *chan)
{
	struct ieee80211_channel *c;

	if (IEEE80211_IS_CHAN_ANYG(chan) &&
	    vap->iv_des_mode == IEEE80211_MODE_AUTO) {
		c = ieee80211_find_channel(vap->iv_ic, chan->ic_freq,
		    (chan->ic_flags &~ (IEEE80211_CHAN_PUREG | IEEE80211_CHAN_G)) |
		    IEEE80211_CHAN_B);
		if (c != NULL)
			chan = c;
	}
	return chan;
}

static int
maxrate(const struct ieee80211_scan_entry *se)
{
	const struct ieee80211_ie_htcap *htcap =
	    (const struct ieee80211_ie_htcap *) se->se_ies.htcap_ie;
	int rmax, r, i, txstream;
	uint16_t caps;
	uint8_t txparams;

	rmax = 0;
	if (htcap != NULL) {
		/*
		 * HT station; inspect supported MCS and then adjust
		 * rate by channel width.
		 */
		txparams = htcap->hc_mcsset[12];
		if (txparams & 0x3) {
			/*
			 * TX MCS parameters defined and not equal to RX,
			 * extract the number of spartial streams and
			 * map it to the highest MCS rate.
			 */
			txstream = ((txparams & 0xc) >> 2) + 1;
			i = txstream * 8 - 1;
		} else
			for (i = 31; i >= 0 && isclr(htcap->hc_mcsset, i); i--);
		if (i >= 0) {
			caps = le16dec(&htcap->hc_cap);
			if ((caps & IEEE80211_HTCAP_CHWIDTH40) &&
			    (caps & IEEE80211_HTCAP_SHORTGI40))
				rmax = ieee80211_htrates[i].ht40_rate_400ns;
			else if (caps & IEEE80211_HTCAP_CHWIDTH40)
				rmax = ieee80211_htrates[i].ht40_rate_800ns;
			else if (caps & IEEE80211_HTCAP_SHORTGI20)
				rmax = ieee80211_htrates[i].ht20_rate_400ns;
			else
				rmax = ieee80211_htrates[i].ht20_rate_800ns;
		}
	}
	for (i = 0; i < se->se_rates[1]; i++) {
		r = se->se_rates[2+i] & IEEE80211_RATE_VAL;
		if (r > rmax)
			rmax = r;
	}
	for (i = 0; i < se->se_xrates[1]; i++) {
		r = se->se_xrates[2+i] & IEEE80211_RATE_VAL;
		if (r > rmax)
			rmax = r;
	}
	return rmax;
}

/*
 * Compare the capabilities of two entries and decide which is
 * more desirable (return >0 if a is considered better).  Note
 * that we assume compatibility/usability has already been checked
 * so we don't need to (e.g. validate whether privacy is supported).
 * Used to select the best scan candidate for association in a BSS.
 *
 * TODO: should we take 11n, 11ac into account when selecting the
 * best?  Right now it just compares frequency band and RSSI.
 */
static int
sta_compare(const struct sta_entry *a, const struct sta_entry *b)
{
#define	PREFER(_a,_b,_what) do {			\
	if (((_a) ^ (_b)) & (_what))			\
		return ((_a) & (_what)) ? 1 : -1;	\
} while (0)
	int maxa, maxb;
	int8_t rssia, rssib;
	int weight;

	/* privacy support */
	PREFER(a->base.se_capinfo, b->base.se_capinfo,
		IEEE80211_CAPINFO_PRIVACY);

	/* compare count of previous failures */
	weight = b->se_fails - a->se_fails;
	if (abs(weight) > 1)
		return weight;

	/*
	 * Compare rssi.  If the two are considered equivalent
	 * then fallback to other criteria.  We threshold the
	 * comparisons to avoid selecting an ap purely by rssi
	 * when both values may be good but one ap is otherwise
	 * more desirable (e.g. an 11b-only ap with stronger
	 * signal than an 11g ap).
	 */
	rssia = MIN(a->base.se_rssi, STA_RSSI_MAX);
	rssib = MIN(b->base.se_rssi, STA_RSSI_MAX);
	if (abs(rssib - rssia) < 5) {
		/* best/max rate preferred if signal level close enough XXX */
		maxa = maxrate(&a->base);
		maxb = maxrate(&b->base);
		if (maxa != maxb)
			return maxa - maxb;
		/* XXX use freq for channel preference */
		/* for now just prefer 5Ghz band to all other bands */
		PREFER(IEEE80211_IS_CHAN_5GHZ(a->base.se_chan),
		       IEEE80211_IS_CHAN_5GHZ(b->base.se_chan), 1);
	}
	/* all things being equal, use signal level */
	return a->base.se_rssi - b->base.se_rssi;
#undef PREFER
}

/*
 * Check rate set suitability and return the best supported rate.
 * XXX inspect MCS for HT
 */
static int
check_rate(struct ieee80211vap *vap, const struct ieee80211_channel *chan,
    const struct ieee80211_scan_entry *se)
{
	const struct ieee80211_rateset *srs;
	int i, j, nrs, r, okrate, badrate, fixedrate, ucastrate;
	const uint8_t *rs;

	okrate = badrate = 0;

	srs = ieee80211_get_suprates(vap->iv_ic, chan);
	nrs = se->se_rates[1];
	rs = se->se_rates+2;
	/* XXX MCS */
	ucastrate = vap->iv_txparms[ieee80211_chan2mode(chan)].ucastrate;
	fixedrate = IEEE80211_FIXED_RATE_NONE;
again:
	for (i = 0; i < nrs; i++) {
		r = IEEE80211_RV(rs[i]);
		badrate = r;
		/*
		 * Check any fixed rate is included. 
		 */
		if (r == ucastrate)
			fixedrate = r;
		/*
		 * Check against our supported rates.
		 */
		for (j = 0; j < srs->rs_nrates; j++)
			if (r == IEEE80211_RV(srs->rs_rates[j])) {
				if (r > okrate)		/* NB: track max */
					okrate = r;
				break;
			}

		if (j == srs->rs_nrates && (rs[i] & IEEE80211_RATE_BASIC)) {
			/*
			 * Don't try joining a BSS, if we don't support
			 * one of its basic rates.
			 */
			okrate = 0;
			goto back;
		}
	}
	if (rs == se->se_rates+2) {
		/* scan xrates too; sort of an algol68-style for loop */
		nrs = se->se_xrates[1];
		rs = se->se_xrates+2;
		goto again;
	}

back:
	if (okrate == 0 || ucastrate != fixedrate)
		return badrate | IEEE80211_RATE_BASIC;
	else
		return IEEE80211_RV(okrate);
}

static __inline int
match_id(const uint8_t *ie, const uint8_t *val, int len)
{
	return (ie[1] == len && memcmp(ie+2, val, len) == 0);
}

static int
match_ssid(const uint8_t *ie,
	int nssid, const struct ieee80211_scan_ssid ssids[])
{
	int i;

	for (i = 0; i < nssid; i++) {
		if (match_id(ie, ssids[i].ssid, ssids[i].len))
			return 1;
	}
	return 0;
}

#ifdef IEEE80211_SUPPORT_TDMA
static int
tdma_isfull(const struct ieee80211_tdma_param *tdma)
{
	int slot, slotcnt;

	slotcnt = tdma->tdma_slotcnt;
	for (slot = slotcnt-1; slot >= 0; slot--)
		if (isclr(tdma->tdma_inuse, slot))
			return 0;
	return 1;
}
#endif /* IEEE80211_SUPPORT_TDMA */

/*
 * Test a scan candidate for suitability/compatibility.
 */
static int
match_bss(struct ieee80211vap *vap,
	const struct ieee80211_scan_state *ss, struct sta_entry *se0,
	int debug)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_scan_entry *se = &se0->base;
        uint8_t rate;
        int fail;

	fail = 0;
	if (isclr(ic->ic_chan_active, ieee80211_chan2ieee(ic, se->se_chan)))
		fail |= MATCH_CHANNEL;
	/*
	 * NB: normally the desired mode is used to construct
	 * the channel list, but it's possible for the scan
	 * cache to include entries for stations outside this
	 * list so we check the desired mode here to weed them
	 * out.
	 */
	if (vap->iv_des_mode != IEEE80211_MODE_AUTO &&
	    (se->se_chan->ic_flags & IEEE80211_CHAN_ALLTURBO) !=
	    chanflags[vap->iv_des_mode])
		fail |= MATCH_CHANNEL;
	if (vap->iv_opmode == IEEE80211_M_IBSS) {
		if ((se->se_capinfo & IEEE80211_CAPINFO_IBSS) == 0)
			fail |= MATCH_CAPINFO;
#ifdef IEEE80211_SUPPORT_TDMA
	} else if (vap->iv_opmode == IEEE80211_M_AHDEMO) {
		/*
		 * Adhoc demo network setup shouldn't really be scanning
		 * but just in case skip stations operating in IBSS or
		 * BSS mode.
		 */
		if (se->se_capinfo & (IEEE80211_CAPINFO_IBSS|IEEE80211_CAPINFO_ESS))
			fail |= MATCH_CAPINFO;
		/*
		 * TDMA operation cannot coexist with a normal 802.11 network;
		 * skip if IBSS or ESS capabilities are marked and require
		 * the beacon have a TDMA ie present.
		 */
		if (vap->iv_caps & IEEE80211_C_TDMA) {
			const struct ieee80211_tdma_param *tdma =
			    (const struct ieee80211_tdma_param *)se->se_ies.tdma_ie;
			const struct ieee80211_tdma_state *ts = vap->iv_tdma;

			if (tdma == NULL)
				fail |= MATCH_TDMA_NOIE;
			else if (tdma->tdma_version != ts->tdma_version)
				fail |= MATCH_TDMA_VERSION;
			else if (tdma->tdma_slot != 0)
				fail |= MATCH_TDMA_NOTMASTER;
			else if (tdma_isfull(tdma))
				fail |= MATCH_TDMA_NOSLOT;
#if 0
			else if (ieee80211_local_address(se->se_macaddr))
				fail |= MATCH_TDMA_LOCAL;
#endif
		}
#endif /* IEEE80211_SUPPORT_TDMA */
#ifdef IEEE80211_SUPPORT_MESH
	} else if (vap->iv_opmode == IEEE80211_M_MBSS) {
		const struct ieee80211_mesh_state *ms = vap->iv_mesh;
		/*
		 * Mesh nodes have IBSS & ESS bits in capinfo turned off
		 * and two special ie's that must be present.
		 */
		if (se->se_capinfo & (IEEE80211_CAPINFO_IBSS|IEEE80211_CAPINFO_ESS))
			fail |= MATCH_CAPINFO;
		else if (se->se_meshid[0] != IEEE80211_ELEMID_MESHID)
			fail |= MATCH_MESH_NOID;
		else if (ms->ms_idlen != 0 &&
		    match_id(se->se_meshid, ms->ms_id, ms->ms_idlen))
			fail |= MATCH_MESHID;
#endif
	} else {
		if ((se->se_capinfo & IEEE80211_CAPINFO_ESS) == 0)
			fail |= MATCH_CAPINFO;
		/*
		 * If 11d is enabled and we're attempting to join a bss
		 * that advertises it's country code then compare our
		 * current settings to what we fetched from the country ie.
		 * If our country code is unspecified or different then do
		 * not attempt to join the bss.  We should have already
		 * dispatched an event to user space that identifies the
		 * new country code so our regdomain config should match.
		 */
		if ((IEEE80211_IS_CHAN_11D(se->se_chan) ||
		    (vap->iv_flags_ext & IEEE80211_FEXT_DOTD)) &&
		    se->se_cc[0] != 0 &&
		    (ic->ic_regdomain.country == CTRY_DEFAULT ||
		     !isocmp(se->se_cc, ic->ic_regdomain.isocc)))
			fail |= MATCH_CC;
	}
	if (vap->iv_flags & IEEE80211_F_PRIVACY) {
		if ((se->se_capinfo & IEEE80211_CAPINFO_PRIVACY) == 0)
			fail |= MATCH_PRIVACY;
	} else {
		/* XXX does this mean privacy is supported or required? */
		if (se->se_capinfo & IEEE80211_CAPINFO_PRIVACY)
			fail |= MATCH_PRIVACY;
	}
	se0->se_flags &= ~STA_DEMOTE11B;
	rate = check_rate(vap, se->se_chan, se);
	if (rate & IEEE80211_RATE_BASIC) {
		fail |= MATCH_RATE;
		/*
		 * An 11b-only ap will give a rate mismatch if there is an
		 * OFDM fixed tx rate for 11g.  Try downgrading the channel
		 * in the scan list to 11b and retry the rate check.
		 */
		if (IEEE80211_IS_CHAN_ANYG(se->se_chan)) {
			rate = check_rate(vap, demote11b(vap, se->se_chan), se);
			if ((rate & IEEE80211_RATE_BASIC) == 0) {
				fail &= ~MATCH_RATE;
				se0->se_flags |= STA_DEMOTE11B;
			}
		}
	} else if (rate < 2*24) {
		/*
		 * This is an 11b-only ap.  Check the desired mode in
		 * case that needs to be honored (mode 11g filters out
		 * 11b-only ap's).  Otherwise force any 11g channel used
		 * in scanning to be demoted.
		 *
		 * NB: we cheat a bit here by looking at the max rate;
		 *     we could/should check the rates.
		 */
		if (!(vap->iv_des_mode == IEEE80211_MODE_AUTO ||
		      vap->iv_des_mode == IEEE80211_MODE_11B))
			fail |= MATCH_RATE;
		else
			se0->se_flags |= STA_DEMOTE11B;
	}
	if (ss->ss_nssid != 0 &&
	    !match_ssid(se->se_ssid, ss->ss_nssid, ss->ss_ssid))
		fail |= MATCH_SSID;
	if ((vap->iv_flags & IEEE80211_F_DESBSSID) &&
	    !IEEE80211_ADDR_EQ(vap->iv_des_bssid, se->se_bssid))
		fail |= MATCH_BSSID;
	if (se0->se_fails >= STA_FAILS_MAX)
		fail |= MATCH_FAILS;
	if (se0->se_notseen >= STA_PURGE_SCANS)
		fail |= MATCH_NOTSEEN;
	if (se->se_rssi < STA_RSSI_MIN)
		fail |= MATCH_RSSI;
#ifdef IEEE80211_DEBUG
	if (ieee80211_msg(vap, debug)) {
		printf(" %c %s",
		    fail & MATCH_FAILS ? '=' :
		    fail & MATCH_NOTSEEN ? '^' :
		    fail & MATCH_CC ? '$' :
#ifdef IEEE80211_SUPPORT_TDMA
		    fail & MATCH_TDMA_NOIE ? '&' :
		    fail & MATCH_TDMA_VERSION ? 'v' :
		    fail & MATCH_TDMA_NOTMASTER ? 's' :
		    fail & MATCH_TDMA_NOSLOT ? 'f' :
		    fail & MATCH_TDMA_LOCAL ? 'l' :
#endif
		    fail & MATCH_MESH_NOID ? 'm' :
		    fail ? '-' : '+', ether_sprintf(se->se_macaddr));
		printf(" %s%c", ether_sprintf(se->se_bssid),
		    fail & MATCH_BSSID ? '!' : ' ');
		printf(" %3d%c", ieee80211_chan2ieee(ic, se->se_chan),
			fail & MATCH_CHANNEL ? '!' : ' ');
		printf(" %+4d%c", se->se_rssi, fail & MATCH_RSSI ? '!' : ' ');
		printf(" %2dM%c", (rate & IEEE80211_RATE_VAL) / 2,
		    fail & MATCH_RATE ? '!' : ' ');
		printf(" %4s%c",
		    (se->se_capinfo & IEEE80211_CAPINFO_ESS) ? "ess" :
		    (se->se_capinfo & IEEE80211_CAPINFO_IBSS) ? "ibss" : "",
		    fail & MATCH_CAPINFO ? '!' : ' ');
		printf(" %3s%c ",
		    (se->se_capinfo & IEEE80211_CAPINFO_PRIVACY) ?
		    "wep" : "no",
		    fail & MATCH_PRIVACY ? '!' : ' ');
		ieee80211_print_essid(se->se_ssid+2, se->se_ssid[1]);
		printf("%s\n", fail & (MATCH_SSID | MATCH_MESHID) ? "!" : "");
	}
#endif
	return fail;
}

static void
sta_update_notseen(struct sta_table *st)
{
	struct sta_entry *se;

	IEEE80211_SCAN_TABLE_LOCK(st);
	TAILQ_FOREACH(se, &st->st_entry, se_list) {
		/*
		 * If seen the reset and don't bump the count;
		 * otherwise bump the ``not seen'' count.  Note
		 * that this insures that stations for which we
		 * see frames while not scanning but not during
		 * this scan will not be penalized.
		 */
		if (se->se_seen)
			se->se_seen = 0;
		else
			se->se_notseen++;
	}
	IEEE80211_SCAN_TABLE_UNLOCK(st);
}

static void
sta_dec_fails(struct sta_table *st)
{
	struct sta_entry *se;

	IEEE80211_SCAN_TABLE_LOCK(st);
	TAILQ_FOREACH(se, &st->st_entry, se_list)
		if (se->se_fails)
			se->se_fails--;
	IEEE80211_SCAN_TABLE_UNLOCK(st);
}

static struct sta_entry *
select_bss(struct ieee80211_scan_state *ss, struct ieee80211vap *vap, int debug)
{
	struct sta_table *st = ss->ss_priv;
	struct sta_entry *se, *selbs = NULL;

	IEEE80211_DPRINTF(vap, debug, " %s\n",
	    "macaddr          bssid         chan  rssi  rate flag  wep  essid");
	IEEE80211_SCAN_TABLE_LOCK(st);
	TAILQ_FOREACH(se, &st->st_entry, se_list) {
		ieee80211_ies_expand(&se->base.se_ies);
		if (match_bss(vap, ss, se, debug) == 0) {
			if (selbs == NULL)
				selbs = se;
			else if (sta_compare(se, selbs) > 0)
				selbs = se;
		}
	}
	IEEE80211_SCAN_TABLE_UNLOCK(st);

	return selbs;
}

/*
 * Pick an ap or ibss network to join or find a channel
 * to use to start an ibss network.
 */
static int
sta_pick_bss(struct ieee80211_scan_state *ss, struct ieee80211vap *vap)
{
	struct sta_table *st = ss->ss_priv;
	struct sta_entry *selbs;
	struct ieee80211_channel *chan;

	KASSERT(vap->iv_opmode == IEEE80211_M_STA,
		("wrong mode %u", vap->iv_opmode));

	if (st->st_newscan) {
		sta_update_notseen(st);
		st->st_newscan = 0;
	}
	if (ss->ss_flags & IEEE80211_SCAN_NOPICK) {
		/*
		 * Manual/background scan, don't select+join the
		 * bss, just return.  The scanning framework will
		 * handle notification that this has completed.
		 */
		ss->ss_flags &= ~IEEE80211_SCAN_NOPICK;
		return 1;
	}
	/*
	 * Automatic sequencing; look for a candidate and
	 * if found join the network.
	 */
	/* NB: unlocked read should be ok */
	if (TAILQ_FIRST(&st->st_entry) == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
			"%s: no scan candidate\n", __func__);
		if (ss->ss_flags & IEEE80211_SCAN_NOJOIN)
			return 0;
notfound:
		/*
		 * If nothing suitable was found decrement
		 * the failure counts so entries will be
		 * reconsidered the next time around.  We
		 * really want to do this only for sta's
		 * where we've previously had some success.
		 */
		sta_dec_fails(st);
		st->st_newscan = 1;
		return 0;			/* restart scan */
	}
	selbs = select_bss(ss, vap, IEEE80211_MSG_SCAN);
	if (ss->ss_flags & IEEE80211_SCAN_NOJOIN)
		return (selbs != NULL);
	if (selbs == NULL)
		goto notfound;
	chan = selbs->base.se_chan;
	if (selbs->se_flags & STA_DEMOTE11B)
		chan = demote11b(vap, chan);
	if (!ieee80211_sta_join(vap, chan, &selbs->base))
		goto notfound;
	return 1;				/* terminate scan */
}

/*
 * Lookup an entry in the scan cache.  We assume we're
 * called from the bottom half or such that we don't need
 * to block the bottom half so that it's safe to return
 * a reference to an entry w/o holding the lock on the table.
 */
static struct sta_entry *
sta_lookup(struct sta_table *st, const uint8_t macaddr[IEEE80211_ADDR_LEN])
{
	struct sta_entry *se;
	int hash = STA_HASH(macaddr);

	IEEE80211_SCAN_TABLE_LOCK(st);
	LIST_FOREACH(se, &st->st_hash[hash], se_hash)
		if (IEEE80211_ADDR_EQ(se->base.se_macaddr, macaddr))
			break;
	IEEE80211_SCAN_TABLE_UNLOCK(st);

	return se;		/* NB: unlocked */
}

static void
sta_roam_check(struct ieee80211_scan_state *ss, struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *ni = vap->iv_bss;
	struct sta_table *st = ss->ss_priv;
	enum ieee80211_phymode mode;
	struct sta_entry *se, *selbs;
	uint8_t roamRate, curRate, ucastRate;
	int8_t roamRssi, curRssi;

	se = sta_lookup(st, ni->ni_macaddr);
	if (se == NULL) {
		/* XXX something is wrong */
		return;
	}

	mode = ieee80211_chan2mode(ic->ic_bsschan);
	roamRate = vap->iv_roamparms[mode].rate;
	roamRssi = vap->iv_roamparms[mode].rssi;
	KASSERT(roamRate != 0 && roamRssi != 0, ("iv_roamparms are not"
	    "initialized for %s mode!", ieee80211_phymode_name[mode]));

	ucastRate = vap->iv_txparms[mode].ucastrate;
	/* NB: the most up to date rssi is in the node, not the scan cache */
	curRssi = ic->ic_node_getrssi(ni);
	if (ucastRate == IEEE80211_FIXED_RATE_NONE) {
		curRate = ni->ni_txrate;
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_ROAM,
		    "%s: currssi %d currate %u roamrssi %d roamrate %u\n",
		    __func__, curRssi, curRate, roamRssi, roamRate);
	} else {
		curRate = roamRate;	/* NB: insure compare below fails */
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_ROAM,
		    "%s: currssi %d roamrssi %d\n", __func__, curRssi, roamRssi);
	}
	/*
	 * Check if a new ap should be used and switch.
	 * XXX deauth current ap
	 */
	if (curRate < roamRate || curRssi < roamRssi) {
		if (ieee80211_time_after(ticks, ic->ic_lastscan + vap->iv_scanvalid)) {
			/*
			 * Scan cache contents are too old; force a scan now
			 * if possible so we have current state to make a
			 * decision with.  We don't kick off a bg scan if
			 * we're using dynamic turbo and boosted or if the
			 * channel is busy.
			 * XXX force immediate switch on scan complete
			 */
			if (!IEEE80211_IS_CHAN_DTURBO(ic->ic_curchan) &&
			    ((vap->iv_flags_ext & IEEE80211_FEXT_SCAN_OFFLOAD) ||
			     ieee80211_time_after(ticks, ic->ic_lastdata + vap->iv_bgscanidle)))
				ieee80211_bg_scan(vap, 0);
			return;
		}
		se->base.se_rssi = curRssi;
		selbs = select_bss(ss, vap, IEEE80211_MSG_ROAM);
		if (selbs != NULL && selbs != se) {
			struct ieee80211_channel *chan;

			IEEE80211_DPRINTF(vap,
			    IEEE80211_MSG_ROAM | IEEE80211_MSG_DEBUG,
			    "%s: ROAM: curRate %u, roamRate %u, "
			    "curRssi %d, roamRssi %d\n", __func__,
			    curRate, roamRate, curRssi, roamRssi);

			chan = selbs->base.se_chan;
			if (selbs->se_flags & STA_DEMOTE11B)
				chan = demote11b(vap, chan);
			(void) ieee80211_sta_join(vap, chan, &selbs->base);
		}
	}
}

/*
 * Age entries in the scan cache.
 * XXX also do roaming since it's convenient
 */
static void
sta_age(struct ieee80211_scan_state *ss)
{
	struct ieee80211vap *vap = ss->ss_vap;

	adhoc_age(ss);
	/*
	 * If rate control is enabled check periodically to see if
	 * we should roam from our current connection to one that
	 * might be better.  This only applies when we're operating
	 * in sta mode and automatic roaming is set.
	 * XXX defer if busy
	 * XXX repeater station
	 * XXX do when !bgscan?
	 */
	KASSERT(vap->iv_opmode == IEEE80211_M_STA,
		("wrong mode %u", vap->iv_opmode));
	if (vap->iv_roaming == IEEE80211_ROAMING_AUTO &&
	    (vap->iv_flags & IEEE80211_F_BGSCAN) &&
	    vap->iv_state >= IEEE80211_S_RUN)
		/* XXX vap is implicit */
		sta_roam_check(ss, vap);
}

/*
 * Iterate over the entries in the scan cache, invoking
 * the callback function on each one.
 */
static void
sta_iterate(struct ieee80211_scan_state *ss, 
	ieee80211_scan_iter_func *f, void *arg)
{
	struct sta_table *st = ss->ss_priv;
	struct sta_entry *se;
	u_int gen;

	IEEE80211_SCAN_ITER_LOCK(st);
	gen = st->st_scaniter++;
restart:
	IEEE80211_SCAN_TABLE_LOCK(st);
	TAILQ_FOREACH(se, &st->st_entry, se_list) {
		if (se->se_scangen != gen) {
			se->se_scangen = gen;
			/* update public state */
			se->base.se_age = ticks - se->se_lastupdate;
			IEEE80211_SCAN_TABLE_UNLOCK(st);
			(*f)(arg, &se->base);
			goto restart;
		}
	}
	IEEE80211_SCAN_TABLE_UNLOCK(st);

	IEEE80211_SCAN_ITER_UNLOCK(st);
}

static void
sta_assoc_fail(struct ieee80211_scan_state *ss,
	const uint8_t macaddr[IEEE80211_ADDR_LEN], int reason)
{
	struct sta_table *st = ss->ss_priv;
	struct sta_entry *se;

	se = sta_lookup(st, macaddr);
	if (se != NULL) {
		se->se_fails++;
		se->se_lastfail = ticks;
		IEEE80211_NOTE_MAC(ss->ss_vap, IEEE80211_MSG_SCAN,
		    macaddr, "%s: reason %u fails %u",
		    __func__, reason, se->se_fails);
	}
}

static void
sta_assoc_success(struct ieee80211_scan_state *ss,
	const uint8_t macaddr[IEEE80211_ADDR_LEN])
{
	struct sta_table *st = ss->ss_priv;
	struct sta_entry *se;

	se = sta_lookup(st, macaddr);
	if (se != NULL) {
#if 0
		se->se_fails = 0;
		IEEE80211_NOTE_MAC(ss->ss_vap, IEEE80211_MSG_SCAN,
		    macaddr, "%s: fails %u",
		    __func__, se->se_fails);
#endif
		se->se_lastassoc = ticks;
	}
}

static const struct ieee80211_scanner sta_default = {
	.scan_name		= "default",
	.scan_attach		= sta_attach,
	.scan_detach		= sta_detach,
	.scan_start		= sta_start,
	.scan_restart		= sta_restart,
	.scan_cancel		= sta_cancel,
	.scan_end		= sta_pick_bss,
	.scan_flush		= sta_flush,
	.scan_add		= sta_add,
	.scan_age		= sta_age,
	.scan_iterate		= sta_iterate,
	.scan_assoc_fail	= sta_assoc_fail,
	.scan_assoc_success	= sta_assoc_success,
};
IEEE80211_SCANNER_ALG(sta, IEEE80211_M_STA, sta_default);

/*
 * Adhoc mode-specific support.
 */

static const uint16_t adhocWorld[] =		/* 36, 40, 44, 48 */
{ 5180, 5200, 5220, 5240 };
static const uint16_t adhocFcc3[] =		/* 36, 40, 44, 48 145, 149, 153, 157, 161, 165 */
{ 5180, 5200, 5220, 5240, 5725, 5745, 5765, 5785, 5805, 5825 };
static const uint16_t adhocMkk[] =		/* 34, 38, 42, 46 */
{ 5170, 5190, 5210, 5230 };
static const uint16_t adhoc11b[] =		/* 10, 11 */
{ 2457, 2462 };

static const struct scanlist adhocScanTable[] = {
	{ IEEE80211_MODE_11B,   	X(adhoc11b) },
	{ IEEE80211_MODE_11A,   	X(adhocWorld) },
	{ IEEE80211_MODE_11A,   	X(adhocFcc3) },
	{ IEEE80211_MODE_11B,   	X(adhocMkk) },
	{ .list = NULL }
};
#undef X

/*
 * Start an adhoc-mode scan by populating the channel list.
 */
static int
adhoc_start(struct ieee80211_scan_state *ss, struct ieee80211vap *vap)
{
	struct sta_table *st = ss->ss_priv;
	
	makescanlist(ss, vap, adhocScanTable);

	if (ss->ss_mindwell == 0)
		ss->ss_mindwell = msecs_to_ticks(200);	/* 200ms */
	if (ss->ss_maxdwell == 0)
		ss->ss_maxdwell = msecs_to_ticks(200);	/* 200ms */

	st->st_scangen++;
	st->st_newscan = 1;

	return 0;
}

/*
 * Select a channel to start an adhoc network on.
 * The channel list was populated with appropriate
 * channels so select one that looks least occupied.
 */
static struct ieee80211_channel *
adhoc_pick_channel(struct ieee80211_scan_state *ss, int flags)
{
	struct sta_table *st = ss->ss_priv;
	struct sta_entry *se;
	struct ieee80211_channel *c, *bestchan;
	int i, bestrssi, maxrssi;

	bestchan = NULL;
	bestrssi = -1;

	IEEE80211_SCAN_TABLE_LOCK(st);
	for (i = 0; i < ss->ss_last; i++) {
		c = ss->ss_chans[i];
		/* never consider a channel with radar */
		if (IEEE80211_IS_CHAN_RADAR(c))
			continue;
		/* skip channels disallowed by regulatory settings */
		if (IEEE80211_IS_CHAN_NOADHOC(c))
			continue;
		/* check channel attributes for band compatibility */
		if (flags != 0 && (c->ic_flags & flags) != flags)
			continue;
		maxrssi = 0;
		TAILQ_FOREACH(se, &st->st_entry, se_list) {
			if (se->base.se_chan != c)
				continue;
			if (se->base.se_rssi > maxrssi)
				maxrssi = se->base.se_rssi;
		}
		if (bestchan == NULL || maxrssi < bestrssi)
			bestchan = c;
	}
	IEEE80211_SCAN_TABLE_UNLOCK(st);

	return bestchan;
}

/*
 * Pick an ibss network to join or find a channel
 * to use to start an ibss network.
 */
static int
adhoc_pick_bss(struct ieee80211_scan_state *ss, struct ieee80211vap *vap)
{
	struct sta_table *st = ss->ss_priv;
	struct sta_entry *selbs;
	struct ieee80211_channel *chan;
	struct ieee80211com *ic = vap->iv_ic;

	KASSERT(vap->iv_opmode == IEEE80211_M_IBSS ||
		vap->iv_opmode == IEEE80211_M_AHDEMO ||
		vap->iv_opmode == IEEE80211_M_MBSS,
		("wrong opmode %u", vap->iv_opmode));

	if (st->st_newscan) {
		sta_update_notseen(st);
		st->st_newscan = 0;
	}
	if (ss->ss_flags & IEEE80211_SCAN_NOPICK) {
		/*
		 * Manual/background scan, don't select+join the
		 * bss, just return.  The scanning framework will
		 * handle notification that this has completed.
		 */
		ss->ss_flags &= ~IEEE80211_SCAN_NOPICK;
		return 1;
	}
	/*
	 * Automatic sequencing; look for a candidate and
	 * if found join the network.
	 */
	/* NB: unlocked read should be ok */
	if (TAILQ_FIRST(&st->st_entry) == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
			"%s: no scan candidate\n", __func__);
		if (ss->ss_flags & IEEE80211_SCAN_NOJOIN)
			return 0;
notfound:
		/* NB: never auto-start a tdma network for slot !0 */
#ifdef IEEE80211_SUPPORT_TDMA
		if (vap->iv_des_nssid &&
		    ((vap->iv_caps & IEEE80211_C_TDMA) == 0 ||
		     ieee80211_tdma_getslot(vap) == 0)) {
#else
		if (vap->iv_des_nssid) {
#endif
			/*
			 * No existing adhoc network to join and we have
			 * an ssid; start one up.  If no channel was
			 * specified, try to select a channel.
			 */
			if (vap->iv_des_chan == IEEE80211_CHAN_ANYC ||
			    IEEE80211_IS_CHAN_RADAR(vap->iv_des_chan)) {
				chan = adhoc_pick_channel(ss, 0);
			} else
				chan = vap->iv_des_chan;
			if (chan != NULL) {
				/*
				 * Create a HT capable IBSS; the per-node
				 * probe request/response will result in
				 * "correct" rate control capabilities being
				 * negotiated.
				 */
				chan = ieee80211_ht_adjust_channel(ic,
				    chan, vap->iv_flags_ht);
				chan = ieee80211_vht_adjust_channel(ic,
				    chan, vap->iv_flags_vht);
				ieee80211_create_ibss(vap, chan);
				return 1;
			}
		}
		/*
		 * If nothing suitable was found decrement
		 * the failure counts so entries will be
		 * reconsidered the next time around.  We
		 * really want to do this only for sta's
		 * where we've previously had some success.
		 */
		sta_dec_fails(st);
		st->st_newscan = 1;
		return 0;			/* restart scan */
	}
	selbs = select_bss(ss, vap, IEEE80211_MSG_SCAN);
	if (ss->ss_flags & IEEE80211_SCAN_NOJOIN)
		return (selbs != NULL);
	if (selbs == NULL)
		goto notfound;
	chan = selbs->base.se_chan;
	if (selbs->se_flags & STA_DEMOTE11B)
		chan = demote11b(vap, chan);
	/*
	 * If HT is available, make it a possibility here.
	 * The intent is to enable HT20/HT40 when joining a non-HT
	 * IBSS node; we can then advertise HT IEs and speak HT
	 * to any subsequent nodes that support it.
	 */
	chan = ieee80211_ht_adjust_channel(ic,
	    chan, vap->iv_flags_ht);
	chan = ieee80211_vht_adjust_channel(ic,
	    chan, vap->iv_flags_vht);
	if (!ieee80211_sta_join(vap, chan, &selbs->base))
		goto notfound;
	return 1;				/* terminate scan */
}

/*
 * Age entries in the scan cache.
 */
static void
adhoc_age(struct ieee80211_scan_state *ss)
{
	struct sta_table *st = ss->ss_priv;
	struct sta_entry *se, *next;

	IEEE80211_SCAN_TABLE_LOCK(st);
	TAILQ_FOREACH_SAFE(se, &st->st_entry, se_list, next) {
		if (se->se_notseen > STA_PURGE_SCANS) {
			TAILQ_REMOVE(&st->st_entry, se, se_list);
			LIST_REMOVE(se, se_hash);
			ieee80211_ies_cleanup(&se->base.se_ies);
			IEEE80211_FREE(se, M_80211_SCAN);
		}
	}
	IEEE80211_SCAN_TABLE_UNLOCK(st);
}

static const struct ieee80211_scanner adhoc_default = {
	.scan_name		= "default",
	.scan_attach		= sta_attach,
	.scan_detach		= sta_detach,
	.scan_start		= adhoc_start,
	.scan_restart		= sta_restart,
	.scan_cancel		= sta_cancel,
	.scan_end		= adhoc_pick_bss,
	.scan_flush		= sta_flush,
	.scan_pickchan		= adhoc_pick_channel,
	.scan_add		= sta_add,
	.scan_age		= adhoc_age,
	.scan_iterate		= sta_iterate,
	.scan_assoc_fail	= sta_assoc_fail,
	.scan_assoc_success	= sta_assoc_success,
};
IEEE80211_SCANNER_ALG(ibss, IEEE80211_M_IBSS, adhoc_default);
IEEE80211_SCANNER_ALG(ahdemo, IEEE80211_M_AHDEMO, adhoc_default);

static int
ap_start(struct ieee80211_scan_state *ss, struct ieee80211vap *vap)
{
	struct sta_table *st = ss->ss_priv;

	makescanlist(ss, vap, staScanTable);

	if (ss->ss_mindwell == 0)
		ss->ss_mindwell = msecs_to_ticks(200);	/* 200ms */
	if (ss->ss_maxdwell == 0)
		ss->ss_maxdwell = msecs_to_ticks(200);	/* 200ms */

	st->st_scangen++;
	st->st_newscan = 1;

	return 0;
}

/*
 * Cancel an ongoing scan.
 */
static int
ap_cancel(struct ieee80211_scan_state *ss, struct ieee80211vap *vap)
{
	return 0;
}

/*
 * Pick a quiet channel to use for ap operation.
 */
static struct ieee80211_channel *
ap_pick_channel(struct ieee80211_scan_state *ss, int flags)
{
	struct sta_table *st = ss->ss_priv;
	struct ieee80211_channel *bestchan = NULL;
	int i;

	/* XXX select channel more intelligently, e.g. channel spread, power */
	/* NB: use scan list order to preserve channel preference */
	for (i = 0; i < ss->ss_last; i++) {
		struct ieee80211_channel *chan = ss->ss_chans[i];
		/*
		 * If the channel is unoccupied the max rssi
		 * should be zero; just take it.  Otherwise
		 * track the channel with the lowest rssi and
		 * use that when all channels appear occupied.
		 */
		if (IEEE80211_IS_CHAN_RADAR(chan))
			continue;
		if (IEEE80211_IS_CHAN_NOHOSTAP(chan))
			continue;
		/* check channel attributes for band compatibility */
		if (flags != 0 && (chan->ic_flags & flags) != flags)
			continue;
		KASSERT(sizeof(chan->ic_ieee) == 1, ("ic_chan size"));
		/* XXX channel have interference */
		if (st->st_maxrssi[chan->ic_ieee] == 0) {
			/* XXX use other considerations */
			return chan;
		}
		if (bestchan == NULL ||
		    st->st_maxrssi[chan->ic_ieee] < st->st_maxrssi[bestchan->ic_ieee])
			bestchan = chan;
	}
	return bestchan;
}

/*
 * Pick a quiet channel to use for ap operation.
 */
static int
ap_end(struct ieee80211_scan_state *ss, struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_channel *bestchan, *chan;

	KASSERT(vap->iv_opmode == IEEE80211_M_HOSTAP,
		("wrong opmode %u", vap->iv_opmode));
	bestchan = ap_pick_channel(ss, 0);
	if (bestchan == NULL) {
		/* no suitable channel, should not happen */
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: no suitable channel! (should not happen)\n", __func__);
		/* XXX print something? */
		return 0;			/* restart scan */
	}
	/*
	 * If this is a dynamic turbo channel, start with the unboosted one.
	 */
	if (IEEE80211_IS_CHAN_TURBO(bestchan)) {
		bestchan = ieee80211_find_channel(ic, bestchan->ic_freq,
			bestchan->ic_flags & ~IEEE80211_CHAN_TURBO);
		if (bestchan == NULL) {
			/* should never happen ?? */
			return 0;
		}
	}
	if (ss->ss_flags & (IEEE80211_SCAN_NOPICK | IEEE80211_SCAN_NOJOIN)) {
		/*
		 * Manual/background scan, don't select+join the
		 * bss, just return.  The scanning framework will
		 * handle notification that this has completed.
		 */
		ss->ss_flags &= ~IEEE80211_SCAN_NOPICK;
		return 1;
	}
	chan = ieee80211_ht_adjust_channel(ic, bestchan, vap->iv_flags_ht);
	chan = ieee80211_vht_adjust_channel(ic, chan, vap->iv_flags_vht);
	ieee80211_create_ibss(vap, chan);

	return 1;
}

static const struct ieee80211_scanner ap_default = {
	.scan_name		= "default",
	.scan_attach		= sta_attach,
	.scan_detach		= sta_detach,
	.scan_start		= ap_start,
	.scan_restart		= sta_restart,
	.scan_cancel		= ap_cancel,
	.scan_end		= ap_end,
	.scan_flush		= sta_flush,
	.scan_pickchan		= ap_pick_channel,
	.scan_add		= sta_add,
	.scan_age		= adhoc_age,
	.scan_iterate		= sta_iterate,
	.scan_assoc_success	= sta_assoc_success,
	.scan_assoc_fail	= sta_assoc_fail,
};
IEEE80211_SCANNER_ALG(ap, IEEE80211_M_HOSTAP, ap_default);

#ifdef IEEE80211_SUPPORT_MESH
/*
 * Pick an mbss network to join or find a channel
 * to use to start an mbss network.
 */
static int
mesh_pick_bss(struct ieee80211_scan_state *ss, struct ieee80211vap *vap)
{
	struct sta_table *st = ss->ss_priv;
	struct ieee80211_mesh_state *ms = vap->iv_mesh;
	struct sta_entry *selbs;
	struct ieee80211_channel *chan;

	KASSERT(vap->iv_opmode == IEEE80211_M_MBSS,
		("wrong opmode %u", vap->iv_opmode));

	if (st->st_newscan) {
		sta_update_notseen(st);
		st->st_newscan = 0;
	}
	if (ss->ss_flags & IEEE80211_SCAN_NOPICK) {
		/*
		 * Manual/background scan, don't select+join the
		 * bss, just return.  The scanning framework will
		 * handle notification that this has completed.
		 */
		ss->ss_flags &= ~IEEE80211_SCAN_NOPICK;
		return 1;
	}
	/*
	 * Automatic sequencing; look for a candidate and
	 * if found join the network.
	 */
	/* NB: unlocked read should be ok */
	if (TAILQ_FIRST(&st->st_entry) == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
			"%s: no scan candidate\n", __func__);
		if (ss->ss_flags & IEEE80211_SCAN_NOJOIN)
			return 0;
notfound:
		if (ms->ms_idlen != 0) {
			/*
			 * No existing mbss network to join and we have
			 * a meshid; start one up.  If no channel was
			 * specified, try to select a channel.
			 */
			if (vap->iv_des_chan == IEEE80211_CHAN_ANYC ||
			    IEEE80211_IS_CHAN_RADAR(vap->iv_des_chan)) {
				struct ieee80211com *ic = vap->iv_ic;

				/* XXX VHT */
				chan = adhoc_pick_channel(ss, 0);
				if (chan != NULL) {
					chan = ieee80211_ht_adjust_channel(ic,
					    chan, vap->iv_flags_ht);
					chan = ieee80211_vht_adjust_channel(ic,
					    chan, vap->iv_flags_vht);
					}
			} else
				chan = vap->iv_des_chan;
			if (chan != NULL) {
				ieee80211_create_ibss(vap, chan);
				return 1;
			}
		}
		/*
		 * If nothing suitable was found decrement
		 * the failure counts so entries will be
		 * reconsidered the next time around.  We
		 * really want to do this only for sta's
		 * where we've previously had some success.
		 */
		sta_dec_fails(st);
		st->st_newscan = 1;
		return 0;			/* restart scan */
	}
	selbs = select_bss(ss, vap, IEEE80211_MSG_SCAN);
	if (ss->ss_flags & IEEE80211_SCAN_NOJOIN)
		return (selbs != NULL);
	if (selbs == NULL)
		goto notfound;
	chan = selbs->base.se_chan;
	if (selbs->se_flags & STA_DEMOTE11B)
		chan = demote11b(vap, chan);
	if (!ieee80211_sta_join(vap, chan, &selbs->base))
		goto notfound;
	return 1;				/* terminate scan */
}

static const struct ieee80211_scanner mesh_default = {
	.scan_name		= "default",
	.scan_attach		= sta_attach,
	.scan_detach		= sta_detach,
	.scan_start		= adhoc_start,
	.scan_restart		= sta_restart,
	.scan_cancel		= sta_cancel,
	.scan_end		= mesh_pick_bss,
	.scan_flush		= sta_flush,
	.scan_pickchan		= adhoc_pick_channel,
	.scan_add		= sta_add,
	.scan_age		= adhoc_age,
	.scan_iterate		= sta_iterate,
	.scan_assoc_fail	= sta_assoc_fail,
	.scan_assoc_success	= sta_assoc_success,
};
IEEE80211_SCANNER_ALG(mesh, IEEE80211_M_MBSS, mesh_default);
#endif /* IEEE80211_SUPPORT_MESH */
