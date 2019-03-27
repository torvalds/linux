/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2009 Sam Leffler, Errno Consulting
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
 *
 * $FreeBSD$
 */
#ifndef _NET80211_IEEE80211_SCAN_H_
#define _NET80211_IEEE80211_SCAN_H_

/*
 * 802.11 scanning support.
 *
 * Scanning is the procedure by which a station locates a bss to join
 * (infrastructure/ibss mode), or a channel to use (when operating as
 * an ap or ibss master).  Scans are either "active" or "passive".  An
 * active scan causes one or more probe request frames to be sent on
 * visiting each channel.  A passive request causes each channel in the
 * scan set to be visited but no frames to be transmitted; the station
 * only listens for traffic.  Note that active scanning may still need
 * to listen for traffic before sending probe request frames depending
 * on regulatory constraints; the 802.11 layer handles this by generating
 * a callback when scanning on a ``passive channel'' when the
 * IEEE80211_FEXT_PROBECHAN flag is set.
 *
 * A scan operation involves constructing a set of channels to inspect
 * (the scan set), visiting each channel and collecting information
 * (e.g. what bss are present), and then analyzing the results to make
 * decisions like which bss to join.  This process needs to be as fast
 * as possible so we do things like intelligently construct scan sets
 * and dwell on a channel only as long as necessary.  The scan code also
 * maintains a cache of recent scan results and uses it to bypass scanning
 * whenever possible.  The scan cache is also used to enable roaming
 * between access points when operating in infrastructure mode.
 *
 * Scanning is handled with pluggable modules that implement "policy"
 * per-operating mode.  The core scanning support provides an
 * instrastructure to support these modules and exports a common api
 * to the rest of the 802.11 layer.  Policy modules decide what
 * channels to visit, what state to record to make decisions (e.g. ap
 * mode scanning for auto channel selection keeps significantly less
 * state than sta mode scanning for an ap to associate to), and selects
 * the final station/channel to return as the result of a scan.
 *
 * Scanning is done synchronously when initially bringing a vap to an
 * operational state and optionally in the background to maintain the
 * scan cache for doing roaming and rogue ap monitoring.  Scanning is
 * not tied to the 802.11 state machine that governs vaps though there
 * is linkage to the IEEE80211_SCAN state.  Only one vap at a time may
 * be scanning; this scheduling policy is handled in ieee80211_new_state
 * and is invisible to the scanning code.
*/
#define	IEEE80211_SCAN_MAX	IEEE80211_CHAN_MAX

struct ieee80211_scanner;			/* scan policy state */

struct ieee80211_scan_ssid {
	int	 len;				/* length in bytes */
	uint8_t ssid[IEEE80211_NWID_LEN];	/* ssid contents */
};
#define	IEEE80211_SCAN_MAX_SSID	1		/* max # ssid's to probe */

/*
 * High-level implementation visible to ieee80211_scan.[ch].
 *
 * The default scanner (ieee80211_scan_sw.[ch]) implements a software
 * driven scanner.  Firmware driven scanning needs a different set of
 * behaviours.
 */
struct ieee80211_scan_methods {
	void (*sc_attach)(struct ieee80211com *);
	void (*sc_detach)(struct ieee80211com *);
	void (*sc_vattach)(struct ieee80211vap *);
	void (*sc_vdetach)(struct ieee80211vap *);
	void (*sc_set_scan_duration)(struct ieee80211vap *, u_int);
	int (*sc_start_scan)(const struct ieee80211_scanner *,
	    struct ieee80211vap *, int, u_int, u_int, u_int, u_int,
	    const struct ieee80211_scan_ssid ssids[]);
	int (*sc_check_scan)(const struct ieee80211_scanner *,
	    struct ieee80211vap *, int, u_int, u_int, u_int, u_int,
	    const struct ieee80211_scan_ssid ssids[]);
	int (*sc_bg_scan)(const struct ieee80211_scanner *,
	    struct ieee80211vap *, int);
	void (*sc_cancel_scan)(struct ieee80211vap *);
	void (*sc_cancel_anyscan)(struct ieee80211vap *);
	void (*sc_scan_next)(struct ieee80211vap *);
	void (*sc_scan_done)(struct ieee80211vap *);
	void (*sc_scan_probe_curchan)(struct ieee80211vap *, int);
	void (*sc_add_scan)(struct ieee80211vap *,
	    struct ieee80211_channel *,
	    const struct ieee80211_scanparams *,
	    const struct ieee80211_frame *,
	    int, int, int);
};

/*
 * Scan state visible to the 802.11 layer.  Scan parameters and
 * results are stored in this data structure.  The ieee80211_scan_state
 * structure is extended with space that is maintained private to
 * the core scanning support.  We allocate one instance and link it
 * to the ieee80211com structure; then share it between all associated
 * vaps.  We could allocate multiple of these, e.g. to hold multiple
 * scan results, but this is sufficient for current needs.
 */
struct ieee80211_scan_state {
	struct ieee80211vap *ss_vap;
	struct ieee80211com *ss_ic;
	const struct ieee80211_scanner *ss_ops;	/* policy hookup, see below */
	void		*ss_priv;		/* scanner private state */
	uint16_t	ss_flags;
#define	IEEE80211_SCAN_NOPICK	0x0001		/* scan only, no selection */
#define	IEEE80211_SCAN_ACTIVE	0x0002		/* active scan (probe req) */
#define	IEEE80211_SCAN_PICK1ST	0x0004		/* ``hey sailor'' mode */
#define	IEEE80211_SCAN_BGSCAN	0x0008		/* bg scan, exit ps at end */
#define	IEEE80211_SCAN_ONCE	0x0010		/* do one complete pass */
#define	IEEE80211_SCAN_NOBCAST	0x0020		/* no broadcast probe req */
#define	IEEE80211_SCAN_NOJOIN	0x0040		/* no auto-sequencing */
#define	IEEE80211_SCAN_GOTPICK	0x1000		/* got candidate, can stop */
	uint8_t		ss_nssid;		/* # ssid's to probe/match */
	struct ieee80211_scan_ssid ss_ssid[IEEE80211_SCAN_MAX_SSID];
						/* ssid's to probe/match */
						/* ordered channel set */
	struct ieee80211_channel *ss_chans[IEEE80211_SCAN_MAX];
	uint16_t	ss_next;		/* ix of next chan to scan */
	uint16_t	ss_last;		/* ix+1 of last chan to scan */
	unsigned long	ss_mindwell;		/* min dwell on channel */
	unsigned long	ss_maxdwell;		/* max dwell on channel */
};

/*
 * The upper 16 bits of the flags word is used to communicate
 * information to the scanning code that is NOT recorded in
 * ss_flags.  It might be better to split this stuff out into
 * a separate variable to avoid confusion.
 */
#define	IEEE80211_SCAN_FLUSH	0x00010000	/* flush candidate table */
#define	IEEE80211_SCAN_NOSSID	0x80000000	/* don't update ssid list */

struct ieee80211com;
void	ieee80211_scan_attach(struct ieee80211com *);
void	ieee80211_scan_detach(struct ieee80211com *);
void	ieee80211_scan_vattach(struct ieee80211vap *);
void	ieee80211_scan_vdetach(struct ieee80211vap *);

void	ieee80211_scan_dump_channels(const struct ieee80211_scan_state *);

#define	IEEE80211_SCAN_FOREVER	0x7fffffff
int	ieee80211_start_scan(struct ieee80211vap *, int flags,
		u_int duration, u_int mindwell, u_int maxdwell,
		u_int nssid, const struct ieee80211_scan_ssid ssids[]);
int	ieee80211_check_scan(struct ieee80211vap *, int flags,
		u_int duration, u_int mindwell, u_int maxdwell,
		u_int nssid, const struct ieee80211_scan_ssid ssids[]);
int	ieee80211_check_scan_current(struct ieee80211vap *);
int	ieee80211_bg_scan(struct ieee80211vap *, int);
void	ieee80211_cancel_scan(struct ieee80211vap *);
void	ieee80211_cancel_anyscan(struct ieee80211vap *);
void	ieee80211_scan_next(struct ieee80211vap *);
void	ieee80211_scan_done(struct ieee80211vap *);
void	ieee80211_probe_curchan(struct ieee80211vap *, int);
struct ieee80211_channel *ieee80211_scan_pickchannel(struct ieee80211com *, int);

struct ieee80211_scanparams;
void	ieee80211_add_scan(struct ieee80211vap *,
		struct ieee80211_channel *,
		const struct ieee80211_scanparams *,
		const struct ieee80211_frame *,
		int subtype, int rssi, int noise);
void	ieee80211_scan_timeout(struct ieee80211com *);

void	ieee80211_scan_assoc_success(struct ieee80211vap *,
		const uint8_t mac[IEEE80211_ADDR_LEN]);
enum {
	IEEE80211_SCAN_FAIL_TIMEOUT	= 1,	/* no response to mgmt frame */
	IEEE80211_SCAN_FAIL_STATUS	= 2	/* negative response to " " */
};
void	ieee80211_scan_assoc_fail(struct ieee80211vap *,
		const uint8_t mac[IEEE80211_ADDR_LEN], int reason);
void	ieee80211_scan_flush(struct ieee80211vap *);

struct ieee80211_scan_entry;
typedef void ieee80211_scan_iter_func(void *,
		const struct ieee80211_scan_entry *);
void	ieee80211_scan_iterate(struct ieee80211vap *,
		ieee80211_scan_iter_func, void *);
enum {
	IEEE80211_BPARSE_BADIELEN	= 0x01,	/* ie len past end of frame */
	IEEE80211_BPARSE_RATES_INVALID	= 0x02,	/* invalid RATES ie */
	IEEE80211_BPARSE_XRATES_INVALID	= 0x04,	/* invalid XRATES ie */
	IEEE80211_BPARSE_SSID_INVALID	= 0x08,	/* invalid SSID ie */
	IEEE80211_BPARSE_CHAN_INVALID	= 0x10,	/* invalid FH/DSPARMS chan */
	IEEE80211_BPARSE_OFFCHAN	= 0x20,	/* DSPARMS chan != curchan */
	IEEE80211_BPARSE_BINTVAL_INVALID= 0x40,	/* invalid beacon interval */
	IEEE80211_BPARSE_CSA_INVALID	= 0x80,	/* invalid CSA ie */
};

/*
 * Parameters supplied when adding/updating an entry in a
 * scan cache.  Pointer variables should be set to NULL
 * if no data is available.  Pointer references can be to
 * local data; any information that is saved will be copied.
 * All multi-byte values must be in host byte order.
 */
struct ieee80211_scanparams {
	uint8_t		status;		/* bitmask of IEEE80211_BPARSE_* */
	uint8_t		chan;		/* channel # from FH/DSPARMS */
	uint8_t		bchan;		/* curchan's channel # */
	uint8_t		fhindex;
	uint16_t	fhdwell;	/* FHSS dwell interval */
	uint16_t	capinfo;	/* 802.11 capabilities */
	uint16_t	erp;		/* NB: 0x100 indicates ie present */
	uint16_t	bintval;
	uint8_t		timoff;
	uint8_t		*ies;		/* all captured ies */
	size_t		ies_len;	/* length of all captured ies */
	uint8_t		*tim;
	uint8_t		*tstamp;
	uint8_t		*country;
	uint8_t		*ssid;
	uint8_t		*rates;
	uint8_t		*xrates;
	uint8_t		*doth;
	uint8_t		*wpa;
	uint8_t		*rsn;
	uint8_t		*wme;
	uint8_t		*htcap;
	uint8_t		*htinfo;
	uint8_t		*ath;
	uint8_t		*tdma;
	uint8_t		*csa;
	uint8_t		*quiet;
	uint8_t		*meshid;
	uint8_t		*meshconf;
	uint8_t		*vhtcap;
	uint8_t		*vhtopmode;
	uint8_t		*spare[1];
};

/*
 * Scan cache entry format used when exporting data from a policy
 * module; this data may be represented some other way internally.
 */
struct ieee80211_scan_entry {
	uint8_t		se_macaddr[IEEE80211_ADDR_LEN];
	uint8_t		se_bssid[IEEE80211_ADDR_LEN];
	/* XXX can point inside se_ies */
	uint8_t		se_ssid[2+IEEE80211_NWID_LEN];
	uint8_t		se_rates[2+IEEE80211_RATE_MAXSIZE];
	uint8_t		se_xrates[2+IEEE80211_RATE_MAXSIZE];
	union {
		uint8_t		data[8];
		u_int64_t	tsf;
	} se_tstamp;			/* from last rcv'd beacon */
	uint16_t	se_intval;	/* beacon interval (host byte order) */
	uint16_t	se_capinfo;	/* capabilities (host byte order) */
	struct ieee80211_channel *se_chan;/* channel where sta found */
	uint16_t	se_timoff;	/* byte offset to TIM ie */
	uint16_t	se_fhdwell;	/* FH only (host byte order) */
	uint8_t		se_fhindex;	/* FH only */
	uint8_t		se_dtimperiod;	/* DTIM period */
	uint16_t	se_erp;		/* ERP from beacon/probe resp */
	int8_t		se_rssi;	/* avg'd recv ssi */
	int8_t		se_noise;	/* noise floor */
	uint8_t		se_cc[2];	/* captured country code */
	uint8_t		se_meshid[2+IEEE80211_MESHID_LEN];
	struct ieee80211_ies se_ies;	/* captured ie's */
	u_int		se_age;		/* age of entry (0 on create) */
};
MALLOC_DECLARE(M_80211_SCAN);

/*
 * Template for an in-kernel scan policy module.
 * Modules register with the scanning code and are
 * typically loaded as needed.
 */
struct ieee80211_scanner {
	const char *scan_name;		/* printable name */
	int	(*scan_attach)(struct ieee80211_scan_state *);
	int	(*scan_detach)(struct ieee80211_scan_state *);
	int	(*scan_start)(struct ieee80211_scan_state *,
			struct ieee80211vap *);
	int	(*scan_restart)(struct ieee80211_scan_state *,
			struct ieee80211vap *);
	int	(*scan_cancel)(struct ieee80211_scan_state *,
			struct ieee80211vap *);
	int	(*scan_end)(struct ieee80211_scan_state *,
			struct ieee80211vap *);
	int	(*scan_flush)(struct ieee80211_scan_state *);
	struct ieee80211_channel *(*scan_pickchan)(
			struct ieee80211_scan_state *, int);
	/* add an entry to the cache */
	int	(*scan_add)(struct ieee80211_scan_state *,
			struct ieee80211_channel *,
			const struct ieee80211_scanparams *,
			const struct ieee80211_frame *,
			int subtype, int rssi, int noise);
	/* age and/or purge entries in the cache */
	void	(*scan_age)(struct ieee80211_scan_state *);
	/* note that association failed for an entry */
	void	(*scan_assoc_fail)(struct ieee80211_scan_state *,
			const uint8_t macaddr[IEEE80211_ADDR_LEN],
			int reason);
	/* note that association succeed for an entry */
	void	(*scan_assoc_success)(struct ieee80211_scan_state *,
			const uint8_t macaddr[IEEE80211_ADDR_LEN]);
	/* iterate over entries in the scan cache */
	void	(*scan_iterate)(struct ieee80211_scan_state *,
			ieee80211_scan_iter_func *, void *);
	void	(*scan_spare0)(void);
	void	(*scan_spare1)(void);
	void	(*scan_spare2)(void);
	void	(*scan_spare4)(void);
};
void	ieee80211_scanner_register(enum ieee80211_opmode,
		const struct ieee80211_scanner *);
void	ieee80211_scanner_unregister(enum ieee80211_opmode,
		const struct ieee80211_scanner *);
void	ieee80211_scanner_unregister_all(const struct ieee80211_scanner *);
const struct ieee80211_scanner *ieee80211_scanner_get(enum ieee80211_opmode);
void	ieee80211_scan_update_locked(struct ieee80211vap *vap,
		const struct ieee80211_scanner *scan);
void	ieee80211_scan_copy_ssid(struct ieee80211vap *vap,
		struct ieee80211_scan_state *ss,
		int nssid, const struct ieee80211_scan_ssid ssids[]);
void	ieee80211_scan_dump_probe_beacon(uint8_t subtype, int isnew,
		const uint8_t mac[IEEE80211_ADDR_LEN],
		const struct ieee80211_scanparams *sp, int rssi);
void	ieee80211_scan_dump(struct ieee80211_scan_state *ss);

#endif /* _NET80211_IEEE80211_SCAN_H_ */
