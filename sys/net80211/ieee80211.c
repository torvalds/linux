/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Atsushi Onoe
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
 * IEEE 802.11 generic handler
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/sbuf.h>

#include <machine/stdarg.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#ifdef IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif
#include <net80211/ieee80211_ratectl.h>
#include <net80211/ieee80211_vht.h>

#include <net/bpf.h>

const char *ieee80211_phymode_name[IEEE80211_MODE_MAX] = {
	[IEEE80211_MODE_AUTO]	  = "auto",
	[IEEE80211_MODE_11A]	  = "11a",
	[IEEE80211_MODE_11B]	  = "11b",
	[IEEE80211_MODE_11G]	  = "11g",
	[IEEE80211_MODE_FH]	  = "FH",
	[IEEE80211_MODE_TURBO_A]  = "turboA",
	[IEEE80211_MODE_TURBO_G]  = "turboG",
	[IEEE80211_MODE_STURBO_A] = "sturboA",
	[IEEE80211_MODE_HALF]	  = "half",
	[IEEE80211_MODE_QUARTER]  = "quarter",
	[IEEE80211_MODE_11NA]	  = "11na",
	[IEEE80211_MODE_11NG]	  = "11ng",
	[IEEE80211_MODE_VHT_2GHZ]	  = "11acg",
	[IEEE80211_MODE_VHT_5GHZ]	  = "11ac",
};
/* map ieee80211_opmode to the corresponding capability bit */
const int ieee80211_opcap[IEEE80211_OPMODE_MAX] = {
	[IEEE80211_M_IBSS]	= IEEE80211_C_IBSS,
	[IEEE80211_M_WDS]	= IEEE80211_C_WDS,
	[IEEE80211_M_STA]	= IEEE80211_C_STA,
	[IEEE80211_M_AHDEMO]	= IEEE80211_C_AHDEMO,
	[IEEE80211_M_HOSTAP]	= IEEE80211_C_HOSTAP,
	[IEEE80211_M_MONITOR]	= IEEE80211_C_MONITOR,
#ifdef IEEE80211_SUPPORT_MESH
	[IEEE80211_M_MBSS]	= IEEE80211_C_MBSS,
#endif
};

const uint8_t ieee80211broadcastaddr[IEEE80211_ADDR_LEN] =
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static	void ieee80211_syncflag_locked(struct ieee80211com *ic, int flag);
static	void ieee80211_syncflag_ht_locked(struct ieee80211com *ic, int flag);
static	void ieee80211_syncflag_ext_locked(struct ieee80211com *ic, int flag);
static	void ieee80211_syncflag_vht_locked(struct ieee80211com *ic, int flag);
static	int ieee80211_media_setup(struct ieee80211com *ic,
		struct ifmedia *media, int caps, int addsta,
		ifm_change_cb_t media_change, ifm_stat_cb_t media_stat);
static	int media_status(enum ieee80211_opmode,
		const struct ieee80211_channel *);
static uint64_t ieee80211_get_counter(struct ifnet *, ift_counter);

MALLOC_DEFINE(M_80211_VAP, "80211vap", "802.11 vap state");

/*
 * Default supported rates for 802.11 operation (in IEEE .5Mb units).
 */
#define	B(r)	((r) | IEEE80211_RATE_BASIC)
static const struct ieee80211_rateset ieee80211_rateset_11a =
	{ 8, { B(12), 18, B(24), 36, B(48), 72, 96, 108 } };
static const struct ieee80211_rateset ieee80211_rateset_half =
	{ 8, { B(6), 9, B(12), 18, B(24), 36, 48, 54 } };
static const struct ieee80211_rateset ieee80211_rateset_quarter =
	{ 8, { B(3), 4, B(6), 9, B(12), 18, 24, 27 } };
static const struct ieee80211_rateset ieee80211_rateset_11b =
	{ 4, { B(2), B(4), B(11), B(22) } };
/* NB: OFDM rates are handled specially based on mode */
static const struct ieee80211_rateset ieee80211_rateset_11g =
	{ 12, { B(2), B(4), B(11), B(22), 12, 18, 24, 36, 48, 72, 96, 108 } };
#undef B

static int set_vht_extchan(struct ieee80211_channel *c);

/*
 * Fill in 802.11 available channel set, mark
 * all available channels as active, and pick
 * a default channel if not already specified.
 */
void
ieee80211_chan_init(struct ieee80211com *ic)
{
#define	DEFAULTRATES(m, def) do { \
	if (ic->ic_sup_rates[m].rs_nrates == 0) \
		ic->ic_sup_rates[m] = def; \
} while (0)
	struct ieee80211_channel *c;
	int i;

	KASSERT(0 < ic->ic_nchans && ic->ic_nchans <= IEEE80211_CHAN_MAX,
		("invalid number of channels specified: %u", ic->ic_nchans));
	memset(ic->ic_chan_avail, 0, sizeof(ic->ic_chan_avail));
	memset(ic->ic_modecaps, 0, sizeof(ic->ic_modecaps));
	setbit(ic->ic_modecaps, IEEE80211_MODE_AUTO);
	for (i = 0; i < ic->ic_nchans; i++) {
		c = &ic->ic_channels[i];
		KASSERT(c->ic_flags != 0, ("channel with no flags"));
		/*
		 * Help drivers that work only with frequencies by filling
		 * in IEEE channel #'s if not already calculated.  Note this
		 * mimics similar work done in ieee80211_setregdomain when
		 * changing regulatory state.
		 */
		if (c->ic_ieee == 0)
			c->ic_ieee = ieee80211_mhz2ieee(c->ic_freq,c->ic_flags);

		/*
		 * Setup the HT40/VHT40 upper/lower bits.
		 * The VHT80 math is done elsewhere.
		 */
		if (IEEE80211_IS_CHAN_HT40(c) && c->ic_extieee == 0)
			c->ic_extieee = ieee80211_mhz2ieee(c->ic_freq +
			    (IEEE80211_IS_CHAN_HT40U(c) ? 20 : -20),
			    c->ic_flags);

		/* Update VHT math */
		/*
		 * XXX VHT again, note that this assumes VHT80 channels
		 * are legit already
		 */
		set_vht_extchan(c);

		/* default max tx power to max regulatory */
		if (c->ic_maxpower == 0)
			c->ic_maxpower = 2*c->ic_maxregpower;
		setbit(ic->ic_chan_avail, c->ic_ieee);
		/*
		 * Identify mode capabilities.
		 */
		if (IEEE80211_IS_CHAN_A(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_11A);
		if (IEEE80211_IS_CHAN_B(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_11B);
		if (IEEE80211_IS_CHAN_ANYG(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_11G);
		if (IEEE80211_IS_CHAN_FHSS(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_FH);
		if (IEEE80211_IS_CHAN_108A(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_TURBO_A);
		if (IEEE80211_IS_CHAN_108G(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_TURBO_G);
		if (IEEE80211_IS_CHAN_ST(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_STURBO_A);
		if (IEEE80211_IS_CHAN_HALF(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_HALF);
		if (IEEE80211_IS_CHAN_QUARTER(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_QUARTER);
		if (IEEE80211_IS_CHAN_HTA(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_11NA);
		if (IEEE80211_IS_CHAN_HTG(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_11NG);
		if (IEEE80211_IS_CHAN_VHTA(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_VHT_5GHZ);
		if (IEEE80211_IS_CHAN_VHTG(c))
			setbit(ic->ic_modecaps, IEEE80211_MODE_VHT_2GHZ);
	}
	/* initialize candidate channels to all available */
	memcpy(ic->ic_chan_active, ic->ic_chan_avail,
		sizeof(ic->ic_chan_avail));

	/* sort channel table to allow lookup optimizations */
	ieee80211_sort_channels(ic->ic_channels, ic->ic_nchans);

	/* invalidate any previous state */
	ic->ic_bsschan = IEEE80211_CHAN_ANYC;
	ic->ic_prevchan = NULL;
	ic->ic_csa_newchan = NULL;
	/* arbitrarily pick the first channel */
	ic->ic_curchan = &ic->ic_channels[0];
	ic->ic_rt = ieee80211_get_ratetable(ic->ic_curchan);

	/* fillin well-known rate sets if driver has not specified */
	DEFAULTRATES(IEEE80211_MODE_11B,	 ieee80211_rateset_11b);
	DEFAULTRATES(IEEE80211_MODE_11G,	 ieee80211_rateset_11g);
	DEFAULTRATES(IEEE80211_MODE_11A,	 ieee80211_rateset_11a);
	DEFAULTRATES(IEEE80211_MODE_TURBO_A,	 ieee80211_rateset_11a);
	DEFAULTRATES(IEEE80211_MODE_TURBO_G,	 ieee80211_rateset_11g);
	DEFAULTRATES(IEEE80211_MODE_STURBO_A,	 ieee80211_rateset_11a);
	DEFAULTRATES(IEEE80211_MODE_HALF,	 ieee80211_rateset_half);
	DEFAULTRATES(IEEE80211_MODE_QUARTER,	 ieee80211_rateset_quarter);
	DEFAULTRATES(IEEE80211_MODE_11NA,	 ieee80211_rateset_11a);
	DEFAULTRATES(IEEE80211_MODE_11NG,	 ieee80211_rateset_11g);
	DEFAULTRATES(IEEE80211_MODE_VHT_2GHZ,	 ieee80211_rateset_11g);
	DEFAULTRATES(IEEE80211_MODE_VHT_5GHZ,	 ieee80211_rateset_11a);

	/*
	 * Setup required information to fill the mcsset field, if driver did
	 * not. Assume a 2T2R setup for historic reasons.
	 */
	if (ic->ic_rxstream == 0)
		ic->ic_rxstream = 2;
	if (ic->ic_txstream == 0)
		ic->ic_txstream = 2;

	ieee80211_init_suphtrates(ic);

	/*
	 * Set auto mode to reset active channel state and any desired channel.
	 */
	(void) ieee80211_setmode(ic, IEEE80211_MODE_AUTO);
#undef DEFAULTRATES
}

static void
null_update_mcast(struct ieee80211com *ic)
{

	ic_printf(ic, "need multicast update callback\n");
}

static void
null_update_promisc(struct ieee80211com *ic)
{

	ic_printf(ic, "need promiscuous mode update callback\n");
}

static void
null_update_chw(struct ieee80211com *ic)
{

	ic_printf(ic, "%s: need callback\n", __func__);
}

int
ic_printf(struct ieee80211com *ic, const char * fmt, ...)
{
	va_list ap;
	int retval;

	retval = printf("%s: ", ic->ic_name);
	va_start(ap, fmt);
	retval += vprintf(fmt, ap);
	va_end(ap);
	return (retval);
}

static LIST_HEAD(, ieee80211com) ic_head = LIST_HEAD_INITIALIZER(ic_head);
static struct mtx ic_list_mtx;
MTX_SYSINIT(ic_list, &ic_list_mtx, "ieee80211com list", MTX_DEF);

static int
sysctl_ieee80211coms(SYSCTL_HANDLER_ARGS)
{
	struct ieee80211com *ic;
	struct sbuf sb;
	char *sp;
	int error;

	error = sysctl_wire_old_buffer(req, 0);
	if (error)
		return (error);
	sbuf_new_for_sysctl(&sb, NULL, 8, req);
	sbuf_clear_flags(&sb, SBUF_INCLUDENUL);
	sp = "";
	mtx_lock(&ic_list_mtx);
	LIST_FOREACH(ic, &ic_head, ic_next) {
		sbuf_printf(&sb, "%s%s", sp, ic->ic_name);
		sp = " ";
	}
	mtx_unlock(&ic_list_mtx);
	error = sbuf_finish(&sb);
	sbuf_delete(&sb);
	return (error);
}

SYSCTL_PROC(_net_wlan, OID_AUTO, devices,
    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, 0,
    sysctl_ieee80211coms, "A", "names of available 802.11 devices");

/*
 * Attach/setup the common net80211 state.  Called by
 * the driver on attach to prior to creating any vap's.
 */
void
ieee80211_ifattach(struct ieee80211com *ic)
{

	IEEE80211_LOCK_INIT(ic, ic->ic_name);
	IEEE80211_TX_LOCK_INIT(ic, ic->ic_name);
	TAILQ_INIT(&ic->ic_vaps);

	/* Create a taskqueue for all state changes */
	ic->ic_tq = taskqueue_create("ic_taskq", M_WAITOK | M_ZERO,
	    taskqueue_thread_enqueue, &ic->ic_tq);
	taskqueue_start_threads(&ic->ic_tq, 1, PI_NET, "%s net80211 taskq",
	    ic->ic_name);
	ic->ic_ierrors = counter_u64_alloc(M_WAITOK);
	ic->ic_oerrors = counter_u64_alloc(M_WAITOK);
	/*
	 * Fill in 802.11 available channel set, mark all
	 * available channels as active, and pick a default
	 * channel if not already specified.
	 */
	ieee80211_chan_init(ic);

	ic->ic_update_mcast = null_update_mcast;
	ic->ic_update_promisc = null_update_promisc;
	ic->ic_update_chw = null_update_chw;

	ic->ic_hash_key = arc4random();
	ic->ic_bintval = IEEE80211_BINTVAL_DEFAULT;
	ic->ic_lintval = ic->ic_bintval;
	ic->ic_txpowlimit = IEEE80211_TXPOWER_MAX;

	ieee80211_crypto_attach(ic);
	ieee80211_node_attach(ic);
	ieee80211_power_attach(ic);
	ieee80211_proto_attach(ic);
#ifdef IEEE80211_SUPPORT_SUPERG
	ieee80211_superg_attach(ic);
#endif
	ieee80211_ht_attach(ic);
	ieee80211_vht_attach(ic);
	ieee80211_scan_attach(ic);
	ieee80211_regdomain_attach(ic);
	ieee80211_dfs_attach(ic);

	ieee80211_sysctl_attach(ic);

	mtx_lock(&ic_list_mtx);
	LIST_INSERT_HEAD(&ic_head, ic, ic_next);
	mtx_unlock(&ic_list_mtx);
}

/*
 * Detach net80211 state on device detach.  Tear down
 * all vap's and reclaim all common state prior to the
 * device state going away.  Note we may call back into
 * driver; it must be prepared for this.
 */
void
ieee80211_ifdetach(struct ieee80211com *ic)
{
	struct ieee80211vap *vap;

	/*
	 * We use this as an indicator that ifattach never had a chance to be
	 * called, e.g. early driver attach failed and ifdetach was called
	 * during subsequent detach.  Never fear, for we have nothing to do
	 * here.
	 */
	if (ic->ic_tq == NULL)
		return;

	mtx_lock(&ic_list_mtx);
	LIST_REMOVE(ic, ic_next);
	mtx_unlock(&ic_list_mtx);

	taskqueue_drain(taskqueue_thread, &ic->ic_restart_task);

	/*
	 * The VAP is responsible for setting and clearing
	 * the VIMAGE context.
	 */
	while ((vap = TAILQ_FIRST(&ic->ic_vaps)) != NULL) {
		ieee80211_com_vdetach(vap);
		ieee80211_vap_destroy(vap);
	}
	ieee80211_waitfor_parent(ic);

	ieee80211_sysctl_detach(ic);
	ieee80211_dfs_detach(ic);
	ieee80211_regdomain_detach(ic);
	ieee80211_scan_detach(ic);
#ifdef IEEE80211_SUPPORT_SUPERG
	ieee80211_superg_detach(ic);
#endif
	ieee80211_vht_detach(ic);
	ieee80211_ht_detach(ic);
	/* NB: must be called before ieee80211_node_detach */
	ieee80211_proto_detach(ic);
	ieee80211_crypto_detach(ic);
	ieee80211_power_detach(ic);
	ieee80211_node_detach(ic);

	counter_u64_free(ic->ic_ierrors);
	counter_u64_free(ic->ic_oerrors);

	taskqueue_free(ic->ic_tq);
	IEEE80211_TX_LOCK_DESTROY(ic);
	IEEE80211_LOCK_DESTROY(ic);
}

struct ieee80211com *
ieee80211_find_com(const char *name)
{
	struct ieee80211com *ic;

	mtx_lock(&ic_list_mtx);
	LIST_FOREACH(ic, &ic_head, ic_next)
		if (strcmp(ic->ic_name, name) == 0)
			break;
	mtx_unlock(&ic_list_mtx);

	return (ic);
}

void
ieee80211_iterate_coms(ieee80211_com_iter_func *f, void *arg)
{
	struct ieee80211com *ic;

	mtx_lock(&ic_list_mtx);
	LIST_FOREACH(ic, &ic_head, ic_next)
		(*f)(arg, ic);
	mtx_unlock(&ic_list_mtx);
}

/*
 * Default reset method for use with the ioctl support.  This
 * method is invoked after any state change in the 802.11
 * layer that should be propagated to the hardware but not
 * require re-initialization of the 802.11 state machine (e.g
 * rescanning for an ap).  We always return ENETRESET which
 * should cause the driver to re-initialize the device. Drivers
 * can override this method to implement more optimized support.
 */
static int
default_reset(struct ieee80211vap *vap, u_long cmd)
{
	return ENETRESET;
}

/*
 * Default for updating the VAP default TX key index.
 *
 * Drivers that support TX offload as well as hardware encryption offload
 * may need to be informed of key index changes separate from the key
 * update.
 */
static void
default_update_deftxkey(struct ieee80211vap *vap, ieee80211_keyix kid)
{

	/* XXX assert validity */
	/* XXX assert we're in a key update block */
	vap->iv_def_txkey = kid;
}

/*
 * Add underlying device errors to vap errors.
 */
static uint64_t
ieee80211_get_counter(struct ifnet *ifp, ift_counter cnt)
{
	struct ieee80211vap *vap = ifp->if_softc;
	struct ieee80211com *ic = vap->iv_ic;
	uint64_t rv;

	rv = if_get_counter_default(ifp, cnt);
	switch (cnt) {
	case IFCOUNTER_OERRORS:
		rv += counter_u64_fetch(ic->ic_oerrors);
		break;
	case IFCOUNTER_IERRORS:
		rv += counter_u64_fetch(ic->ic_ierrors);
		break;
	default:
		break;
	}

	return (rv);
}

/*
 * Prepare a vap for use.  Drivers use this call to
 * setup net80211 state in new vap's prior attaching
 * them with ieee80211_vap_attach (below).
 */
int
ieee80211_vap_setup(struct ieee80211com *ic, struct ieee80211vap *vap,
    const char name[IFNAMSIZ], int unit, enum ieee80211_opmode opmode,
    int flags, const uint8_t bssid[IEEE80211_ADDR_LEN])
{
	struct ifnet *ifp;

	ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		ic_printf(ic, "%s: unable to allocate ifnet\n",
		    __func__);
		return ENOMEM;
	}
	if_initname(ifp, name, unit);
	ifp->if_softc = vap;			/* back pointer */
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	ifp->if_transmit = ieee80211_vap_transmit;
	ifp->if_qflush = ieee80211_vap_qflush;
	ifp->if_ioctl = ieee80211_ioctl;
	ifp->if_init = ieee80211_init;
	ifp->if_get_counter = ieee80211_get_counter;

	vap->iv_ifp = ifp;
	vap->iv_ic = ic;
	vap->iv_flags = ic->ic_flags;		/* propagate common flags */
	vap->iv_flags_ext = ic->ic_flags_ext;
	vap->iv_flags_ven = ic->ic_flags_ven;
	vap->iv_caps = ic->ic_caps &~ IEEE80211_C_OPMODE;

	/* 11n capabilities - XXX methodize */
	vap->iv_htcaps = ic->ic_htcaps;
	vap->iv_htextcaps = ic->ic_htextcaps;

	/* 11ac capabilities - XXX methodize */
	vap->iv_vhtcaps = ic->ic_vhtcaps;
	vap->iv_vhtextcaps = ic->ic_vhtextcaps;

	vap->iv_opmode = opmode;
	vap->iv_caps |= ieee80211_opcap[opmode];
	IEEE80211_ADDR_COPY(vap->iv_myaddr, ic->ic_macaddr);
	switch (opmode) {
	case IEEE80211_M_WDS:
		/*
		 * WDS links must specify the bssid of the far end.
		 * For legacy operation this is a static relationship.
		 * For non-legacy operation the station must associate
		 * and be authorized to pass traffic.  Plumbing the
		 * vap to the proper node happens when the vap
		 * transitions to RUN state.
		 */
		IEEE80211_ADDR_COPY(vap->iv_des_bssid, bssid);
		vap->iv_flags |= IEEE80211_F_DESBSSID;
		if (flags & IEEE80211_CLONE_WDSLEGACY)
			vap->iv_flags_ext |= IEEE80211_FEXT_WDSLEGACY;
		break;
#ifdef IEEE80211_SUPPORT_TDMA
	case IEEE80211_M_AHDEMO:
		if (flags & IEEE80211_CLONE_TDMA) {
			/* NB: checked before clone operation allowed */
			KASSERT(ic->ic_caps & IEEE80211_C_TDMA,
			    ("not TDMA capable, ic_caps 0x%x", ic->ic_caps));
			/*
			 * Propagate TDMA capability to mark vap; this
			 * cannot be removed and is used to distinguish
			 * regular ahdemo operation from ahdemo+tdma.
			 */
			vap->iv_caps |= IEEE80211_C_TDMA;
		}
		break;
#endif
	default:
		break;
	}
	/* auto-enable s/w beacon miss support */
	if (flags & IEEE80211_CLONE_NOBEACONS)
		vap->iv_flags_ext |= IEEE80211_FEXT_SWBMISS;
	/* auto-generated or user supplied MAC address */
	if (flags & (IEEE80211_CLONE_BSSID|IEEE80211_CLONE_MACADDR))
		vap->iv_flags_ext |= IEEE80211_FEXT_UNIQMAC;
	/*
	 * Enable various functionality by default if we're
	 * capable; the driver can override us if it knows better.
	 */
	if (vap->iv_caps & IEEE80211_C_WME)
		vap->iv_flags |= IEEE80211_F_WME;
	if (vap->iv_caps & IEEE80211_C_BURST)
		vap->iv_flags |= IEEE80211_F_BURST;
	/* NB: bg scanning only makes sense for station mode right now */
	if (vap->iv_opmode == IEEE80211_M_STA &&
	    (vap->iv_caps & IEEE80211_C_BGSCAN))
		vap->iv_flags |= IEEE80211_F_BGSCAN;
	vap->iv_flags |= IEEE80211_F_DOTH;	/* XXX no cap, just ena */
	/* NB: DFS support only makes sense for ap mode right now */
	if (vap->iv_opmode == IEEE80211_M_HOSTAP &&
	    (vap->iv_caps & IEEE80211_C_DFS))
		vap->iv_flags_ext |= IEEE80211_FEXT_DFS;

	vap->iv_des_chan = IEEE80211_CHAN_ANYC;		/* any channel is ok */
	vap->iv_bmissthreshold = IEEE80211_HWBMISS_DEFAULT;
	vap->iv_dtim_period = IEEE80211_DTIM_DEFAULT;
	/*
	 * Install a default reset method for the ioctl support;
	 * the driver can override this.
	 */
	vap->iv_reset = default_reset;

	/*
	 * Install a default crypto key update method, the driver
	 * can override this.
	 */
	vap->iv_update_deftxkey = default_update_deftxkey;

	ieee80211_sysctl_vattach(vap);
	ieee80211_crypto_vattach(vap);
	ieee80211_node_vattach(vap);
	ieee80211_power_vattach(vap);
	ieee80211_proto_vattach(vap);
#ifdef IEEE80211_SUPPORT_SUPERG
	ieee80211_superg_vattach(vap);
#endif
	ieee80211_ht_vattach(vap);
	ieee80211_vht_vattach(vap);
	ieee80211_scan_vattach(vap);
	ieee80211_regdomain_vattach(vap);
	ieee80211_radiotap_vattach(vap);
	ieee80211_ratectl_set(vap, IEEE80211_RATECTL_NONE);

	return 0;
}

/*
 * Activate a vap.  State should have been prepared with a
 * call to ieee80211_vap_setup and by the driver.  On return
 * from this call the vap is ready for use.
 */
int
ieee80211_vap_attach(struct ieee80211vap *vap, ifm_change_cb_t media_change,
    ifm_stat_cb_t media_stat, const uint8_t macaddr[IEEE80211_ADDR_LEN])
{
	struct ifnet *ifp = vap->iv_ifp;
	struct ieee80211com *ic = vap->iv_ic;
	struct ifmediareq imr;
	int maxrate;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_STATE,
	    "%s: %s parent %s flags 0x%x flags_ext 0x%x\n",
	    __func__, ieee80211_opmode_name[vap->iv_opmode],
	    ic->ic_name, vap->iv_flags, vap->iv_flags_ext);

	/*
	 * Do late attach work that cannot happen until after
	 * the driver has had a chance to override defaults.
	 */
	ieee80211_node_latevattach(vap);
	ieee80211_power_latevattach(vap);

	maxrate = ieee80211_media_setup(ic, &vap->iv_media, vap->iv_caps,
	    vap->iv_opmode == IEEE80211_M_STA, media_change, media_stat);
	ieee80211_media_status(ifp, &imr);
	/* NB: strip explicit mode; we're actually in autoselect */
	ifmedia_set(&vap->iv_media,
	    imr.ifm_active &~ (IFM_MMASK | IFM_IEEE80211_TURBO));
	if (maxrate)
		ifp->if_baudrate = IF_Mbps(maxrate);

	ether_ifattach(ifp, macaddr);
	IEEE80211_ADDR_COPY(vap->iv_myaddr, IF_LLADDR(ifp));
	/* hook output method setup by ether_ifattach */
	vap->iv_output = ifp->if_output;
	ifp->if_output = ieee80211_output;
	/* NB: if_mtu set by ether_ifattach to ETHERMTU */

	IEEE80211_LOCK(ic);
	TAILQ_INSERT_TAIL(&ic->ic_vaps, vap, iv_next);
	ieee80211_syncflag_locked(ic, IEEE80211_F_WME);
#ifdef IEEE80211_SUPPORT_SUPERG
	ieee80211_syncflag_locked(ic, IEEE80211_F_TURBOP);
#endif
	ieee80211_syncflag_locked(ic, IEEE80211_F_PCF);
	ieee80211_syncflag_locked(ic, IEEE80211_F_BURST);
	ieee80211_syncflag_ht_locked(ic, IEEE80211_FHT_HT);
	ieee80211_syncflag_ht_locked(ic, IEEE80211_FHT_USEHT40);

	ieee80211_syncflag_vht_locked(ic, IEEE80211_FVHT_VHT);
	ieee80211_syncflag_vht_locked(ic, IEEE80211_FVHT_USEVHT40);
	ieee80211_syncflag_vht_locked(ic, IEEE80211_FVHT_USEVHT80);
	ieee80211_syncflag_vht_locked(ic, IEEE80211_FVHT_USEVHT80P80);
	ieee80211_syncflag_vht_locked(ic, IEEE80211_FVHT_USEVHT160);
	IEEE80211_UNLOCK(ic);

	return 1;
}

/*
 * Tear down vap state and reclaim the ifnet.
 * The driver is assumed to have prepared for
 * this; e.g. by turning off interrupts for the
 * underlying device.
 */
void
ieee80211_vap_detach(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ifnet *ifp = vap->iv_ifp;

	CURVNET_SET(ifp->if_vnet);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_STATE, "%s: %s parent %s\n",
	    __func__, ieee80211_opmode_name[vap->iv_opmode], ic->ic_name);

	/* NB: bpfdetach is called by ether_ifdetach and claims all taps */
	ether_ifdetach(ifp);

	ieee80211_stop(vap);

	/*
	 * Flush any deferred vap tasks.
	 */
	ieee80211_draintask(ic, &vap->iv_nstate_task);
	ieee80211_draintask(ic, &vap->iv_swbmiss_task);
	ieee80211_draintask(ic, &vap->iv_wme_task);
	ieee80211_draintask(ic, &ic->ic_parent_task);

	/* XXX band-aid until ifnet handles this for us */
	taskqueue_drain(taskqueue_swi, &ifp->if_linktask);

	IEEE80211_LOCK(ic);
	KASSERT(vap->iv_state == IEEE80211_S_INIT , ("vap still running"));
	TAILQ_REMOVE(&ic->ic_vaps, vap, iv_next);
	ieee80211_syncflag_locked(ic, IEEE80211_F_WME);
#ifdef IEEE80211_SUPPORT_SUPERG
	ieee80211_syncflag_locked(ic, IEEE80211_F_TURBOP);
#endif
	ieee80211_syncflag_locked(ic, IEEE80211_F_PCF);
	ieee80211_syncflag_locked(ic, IEEE80211_F_BURST);
	ieee80211_syncflag_ht_locked(ic, IEEE80211_FHT_HT);
	ieee80211_syncflag_ht_locked(ic, IEEE80211_FHT_USEHT40);

	ieee80211_syncflag_vht_locked(ic, IEEE80211_FVHT_VHT);
	ieee80211_syncflag_vht_locked(ic, IEEE80211_FVHT_USEVHT40);
	ieee80211_syncflag_vht_locked(ic, IEEE80211_FVHT_USEVHT80);
	ieee80211_syncflag_vht_locked(ic, IEEE80211_FVHT_USEVHT80P80);
	ieee80211_syncflag_vht_locked(ic, IEEE80211_FVHT_USEVHT160);

	/* NB: this handles the bpfdetach done below */
	ieee80211_syncflag_ext_locked(ic, IEEE80211_FEXT_BPF);
	if (vap->iv_ifflags & IFF_PROMISC)
		ieee80211_promisc(vap, false);
	if (vap->iv_ifflags & IFF_ALLMULTI)
		ieee80211_allmulti(vap, false);
	IEEE80211_UNLOCK(ic);

	ifmedia_removeall(&vap->iv_media);

	ieee80211_radiotap_vdetach(vap);
	ieee80211_regdomain_vdetach(vap);
	ieee80211_scan_vdetach(vap);
#ifdef IEEE80211_SUPPORT_SUPERG
	ieee80211_superg_vdetach(vap);
#endif
	ieee80211_vht_vdetach(vap);
	ieee80211_ht_vdetach(vap);
	/* NB: must be before ieee80211_node_vdetach */
	ieee80211_proto_vdetach(vap);
	ieee80211_crypto_vdetach(vap);
	ieee80211_power_vdetach(vap);
	ieee80211_node_vdetach(vap);
	ieee80211_sysctl_vdetach(vap);

	if_free(ifp);

	CURVNET_RESTORE();
}

/*
 * Count number of vaps in promisc, and issue promisc on
 * parent respectively.
 */
void
ieee80211_promisc(struct ieee80211vap *vap, bool on)
{
	struct ieee80211com *ic = vap->iv_ic;

	IEEE80211_LOCK_ASSERT(ic);

	if (on) {
		if (++ic->ic_promisc == 1)
			ieee80211_runtask(ic, &ic->ic_promisc_task);
	} else {
		KASSERT(ic->ic_promisc > 0, ("%s: ic %p not promisc",
		    __func__, ic));
		if (--ic->ic_promisc == 0)
			ieee80211_runtask(ic, &ic->ic_promisc_task);
	}
}

/*
 * Count number of vaps in allmulti, and issue allmulti on
 * parent respectively.
 */
void
ieee80211_allmulti(struct ieee80211vap *vap, bool on)
{
	struct ieee80211com *ic = vap->iv_ic;

	IEEE80211_LOCK_ASSERT(ic);

	if (on) {
		if (++ic->ic_allmulti == 1)
			ieee80211_runtask(ic, &ic->ic_mcast_task);
	} else {
		KASSERT(ic->ic_allmulti > 0, ("%s: ic %p not allmulti",
		    __func__, ic));
		if (--ic->ic_allmulti == 0)
			ieee80211_runtask(ic, &ic->ic_mcast_task);
	}
}

/*
 * Synchronize flag bit state in the com structure
 * according to the state of all vap's.  This is used,
 * for example, to handle state changes via ioctls.
 */
static void
ieee80211_syncflag_locked(struct ieee80211com *ic, int flag)
{
	struct ieee80211vap *vap;
	int bit;

	IEEE80211_LOCK_ASSERT(ic);

	bit = 0;
	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next)
		if (vap->iv_flags & flag) {
			bit = 1;
			break;
		}
	if (bit)
		ic->ic_flags |= flag;
	else
		ic->ic_flags &= ~flag;
}

void
ieee80211_syncflag(struct ieee80211vap *vap, int flag)
{
	struct ieee80211com *ic = vap->iv_ic;

	IEEE80211_LOCK(ic);
	if (flag < 0) {
		flag = -flag;
		vap->iv_flags &= ~flag;
	} else
		vap->iv_flags |= flag;
	ieee80211_syncflag_locked(ic, flag);
	IEEE80211_UNLOCK(ic);
}

/*
 * Synchronize flags_ht bit state in the com structure
 * according to the state of all vap's.  This is used,
 * for example, to handle state changes via ioctls.
 */
static void
ieee80211_syncflag_ht_locked(struct ieee80211com *ic, int flag)
{
	struct ieee80211vap *vap;
	int bit;

	IEEE80211_LOCK_ASSERT(ic);

	bit = 0;
	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next)
		if (vap->iv_flags_ht & flag) {
			bit = 1;
			break;
		}
	if (bit)
		ic->ic_flags_ht |= flag;
	else
		ic->ic_flags_ht &= ~flag;
}

void
ieee80211_syncflag_ht(struct ieee80211vap *vap, int flag)
{
	struct ieee80211com *ic = vap->iv_ic;

	IEEE80211_LOCK(ic);
	if (flag < 0) {
		flag = -flag;
		vap->iv_flags_ht &= ~flag;
	} else
		vap->iv_flags_ht |= flag;
	ieee80211_syncflag_ht_locked(ic, flag);
	IEEE80211_UNLOCK(ic);
}

/*
 * Synchronize flags_vht bit state in the com structure
 * according to the state of all vap's.  This is used,
 * for example, to handle state changes via ioctls.
 */
static void
ieee80211_syncflag_vht_locked(struct ieee80211com *ic, int flag)
{
	struct ieee80211vap *vap;
	int bit;

	IEEE80211_LOCK_ASSERT(ic);

	bit = 0;
	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next)
		if (vap->iv_flags_vht & flag) {
			bit = 1;
			break;
		}
	if (bit)
		ic->ic_flags_vht |= flag;
	else
		ic->ic_flags_vht &= ~flag;
}

void
ieee80211_syncflag_vht(struct ieee80211vap *vap, int flag)
{
	struct ieee80211com *ic = vap->iv_ic;

	IEEE80211_LOCK(ic);
	if (flag < 0) {
		flag = -flag;
		vap->iv_flags_vht &= ~flag;
	} else
		vap->iv_flags_vht |= flag;
	ieee80211_syncflag_vht_locked(ic, flag);
	IEEE80211_UNLOCK(ic);
}

/*
 * Synchronize flags_ext bit state in the com structure
 * according to the state of all vap's.  This is used,
 * for example, to handle state changes via ioctls.
 */
static void
ieee80211_syncflag_ext_locked(struct ieee80211com *ic, int flag)
{
	struct ieee80211vap *vap;
	int bit;

	IEEE80211_LOCK_ASSERT(ic);

	bit = 0;
	TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next)
		if (vap->iv_flags_ext & flag) {
			bit = 1;
			break;
		}
	if (bit)
		ic->ic_flags_ext |= flag;
	else
		ic->ic_flags_ext &= ~flag;
}

void
ieee80211_syncflag_ext(struct ieee80211vap *vap, int flag)
{
	struct ieee80211com *ic = vap->iv_ic;

	IEEE80211_LOCK(ic);
	if (flag < 0) {
		flag = -flag;
		vap->iv_flags_ext &= ~flag;
	} else
		vap->iv_flags_ext |= flag;
	ieee80211_syncflag_ext_locked(ic, flag);
	IEEE80211_UNLOCK(ic);
}

static __inline int
mapgsm(u_int freq, u_int flags)
{
	freq *= 10;
	if (flags & IEEE80211_CHAN_QUARTER)
		freq += 5;
	else if (flags & IEEE80211_CHAN_HALF)
		freq += 10;
	else
		freq += 20;
	/* NB: there is no 907/20 wide but leave room */
	return (freq - 906*10) / 5;
}

static __inline int
mappsb(u_int freq, u_int flags)
{
	return 37 + ((freq * 10) + ((freq % 5) == 2 ? 5 : 0) - 49400) / 5;
}

/*
 * Convert MHz frequency to IEEE channel number.
 */
int
ieee80211_mhz2ieee(u_int freq, u_int flags)
{
#define	IS_FREQ_IN_PSB(_freq) ((_freq) > 4940 && (_freq) < 4990)
	if (flags & IEEE80211_CHAN_GSM)
		return mapgsm(freq, flags);
	if (flags & IEEE80211_CHAN_2GHZ) {	/* 2GHz band */
		if (freq == 2484)
			return 14;
		if (freq < 2484)
			return ((int) freq - 2407) / 5;
		else
			return 15 + ((freq - 2512) / 20);
	} else if (flags & IEEE80211_CHAN_5GHZ) {	/* 5Ghz band */
		if (freq <= 5000) {
			/* XXX check regdomain? */
			if (IS_FREQ_IN_PSB(freq))
				return mappsb(freq, flags);
			return (freq - 4000) / 5;
		} else
			return (freq - 5000) / 5;
	} else {				/* either, guess */
		if (freq == 2484)
			return 14;
		if (freq < 2484) {
			if (907 <= freq && freq <= 922)
				return mapgsm(freq, flags);
			return ((int) freq - 2407) / 5;
		}
		if (freq < 5000) {
			if (IS_FREQ_IN_PSB(freq))
				return mappsb(freq, flags);
			else if (freq > 4900)
				return (freq - 4000) / 5;
			else
				return 15 + ((freq - 2512) / 20);
		}
		return (freq - 5000) / 5;
	}
#undef IS_FREQ_IN_PSB
}

/*
 * Convert channel to IEEE channel number.
 */
int
ieee80211_chan2ieee(struct ieee80211com *ic, const struct ieee80211_channel *c)
{
	if (c == NULL) {
		ic_printf(ic, "invalid channel (NULL)\n");
		return 0;		/* XXX */
	}
	return (c == IEEE80211_CHAN_ANYC ?  IEEE80211_CHAN_ANY : c->ic_ieee);
}

/*
 * Convert IEEE channel number to MHz frequency.
 */
u_int
ieee80211_ieee2mhz(u_int chan, u_int flags)
{
	if (flags & IEEE80211_CHAN_GSM)
		return 907 + 5 * (chan / 10);
	if (flags & IEEE80211_CHAN_2GHZ) {	/* 2GHz band */
		if (chan == 14)
			return 2484;
		if (chan < 14)
			return 2407 + chan*5;
		else
			return 2512 + ((chan-15)*20);
	} else if (flags & IEEE80211_CHAN_5GHZ) {/* 5Ghz band */
		if (flags & (IEEE80211_CHAN_HALF|IEEE80211_CHAN_QUARTER)) {
			chan -= 37;
			return 4940 + chan*5 + (chan % 5 ? 2 : 0);
		}
		return 5000 + (chan*5);
	} else {				/* either, guess */
		/* XXX can't distinguish PSB+GSM channels */
		if (chan == 14)
			return 2484;
		if (chan < 14)			/* 0-13 */
			return 2407 + chan*5;
		if (chan < 27)			/* 15-26 */
			return 2512 + ((chan-15)*20);
		return 5000 + (chan*5);
	}
}

static __inline void
set_extchan(struct ieee80211_channel *c)
{

	/*
	 * IEEE Std 802.11-2012, page 1738, subclause 20.3.15.4:
	 * "the secondary channel number shall be 'N + [1,-1] * 4'
	 */
	if (c->ic_flags & IEEE80211_CHAN_HT40U)
		c->ic_extieee = c->ic_ieee + 4;
	else if (c->ic_flags & IEEE80211_CHAN_HT40D)
		c->ic_extieee = c->ic_ieee - 4;
	else
		c->ic_extieee = 0;
}

/*
 * Populate the freq1/freq2 fields as appropriate for VHT channels.
 *
 * This for now uses a hard-coded list of 80MHz wide channels.
 *
 * For HT20/HT40, freq1 just is the centre frequency of the 40MHz
 * wide channel we've already decided upon.
 *
 * For VHT80 and VHT160, there are only a small number of fixed
 * 80/160MHz wide channels, so we just use those.
 *
 * This is all likely very very wrong - both the regulatory code
 * and this code needs to ensure that all four channels are
 * available and valid before the VHT80 (and eight for VHT160) channel
 * is created.
 */

struct vht_chan_range {
	uint16_t freq_start;
	uint16_t freq_end;
};

struct vht_chan_range vht80_chan_ranges[] = {
	{ 5170, 5250 },
	{ 5250, 5330 },
	{ 5490, 5570 },
	{ 5570, 5650 },
	{ 5650, 5730 },
	{ 5735, 5815 },
	{ 0, 0, }
};

static int
set_vht_extchan(struct ieee80211_channel *c)
{
	int i;

	if (! IEEE80211_IS_CHAN_VHT(c)) {
		return (0);
	}

	if (IEEE80211_IS_CHAN_VHT20(c)) {
		c->ic_vht_ch_freq1 = c->ic_ieee;
		return (1);
	}

	if (IEEE80211_IS_CHAN_VHT40(c)) {
		if (IEEE80211_IS_CHAN_HT40U(c))
			c->ic_vht_ch_freq1 = c->ic_ieee + 2;
		else if (IEEE80211_IS_CHAN_HT40D(c))
			c->ic_vht_ch_freq1 = c->ic_ieee - 2;
		else
			return (0);
		return (1);
	}

	if (IEEE80211_IS_CHAN_VHT80(c)) {
		for (i = 0; vht80_chan_ranges[i].freq_start != 0; i++) {
			if (c->ic_freq >= vht80_chan_ranges[i].freq_start &&
			    c->ic_freq < vht80_chan_ranges[i].freq_end) {
				int midpoint;

				midpoint = vht80_chan_ranges[i].freq_start + 40;
				c->ic_vht_ch_freq1 =
				    ieee80211_mhz2ieee(midpoint, c->ic_flags);
				c->ic_vht_ch_freq2 = 0;
#if 0
				printf("%s: %d, freq=%d, midpoint=%d, freq1=%d, freq2=%d\n",
				    __func__, c->ic_ieee, c->ic_freq, midpoint,
				    c->ic_vht_ch_freq1, c->ic_vht_ch_freq2);
#endif
				return (1);
			}
		}
		return (0);
	}

	printf("%s: unknown VHT channel type (ieee=%d, flags=0x%08x)\n",
	    __func__,
	    c->ic_ieee,
	    c->ic_flags);

	return (0);
}

/*
 * Return whether the current channel could possibly be a part of
 * a VHT80 channel.
 *
 * This doesn't check that the whole range is in the allowed list
 * according to regulatory.
 */
static int
is_vht80_valid_freq(uint16_t freq)
{
	int i;
	for (i = 0; vht80_chan_ranges[i].freq_start != 0; i++) {
		if (freq >= vht80_chan_ranges[i].freq_start &&
		    freq < vht80_chan_ranges[i].freq_end)
			return (1);
	}
	return (0);
}

static int
addchan(struct ieee80211_channel chans[], int maxchans, int *nchans,
    uint8_t ieee, uint16_t freq, int8_t maxregpower, uint32_t flags)
{
	struct ieee80211_channel *c;

	if (*nchans >= maxchans)
		return (ENOBUFS);

#if 0
	printf("%s: %d: ieee=%d, freq=%d, flags=0x%08x\n",
	    __func__,
	    *nchans,
	    ieee,
	    freq,
	    flags);
#endif

	c = &chans[(*nchans)++];
	c->ic_ieee = ieee;
	c->ic_freq = freq != 0 ? freq : ieee80211_ieee2mhz(ieee, flags);
	c->ic_maxregpower = maxregpower;
	c->ic_maxpower = 2 * maxregpower;
	c->ic_flags = flags;
	c->ic_vht_ch_freq1 = 0;
	c->ic_vht_ch_freq2 = 0;
	set_extchan(c);
	set_vht_extchan(c);

	return (0);
}

static int
copychan_prev(struct ieee80211_channel chans[], int maxchans, int *nchans,
    uint32_t flags)
{
	struct ieee80211_channel *c;

	KASSERT(*nchans > 0, ("channel list is empty\n"));

	if (*nchans >= maxchans)
		return (ENOBUFS);

#if 0
	printf("%s: %d: flags=0x%08x\n",
	    __func__,
	    *nchans,
	    flags);
#endif

	c = &chans[(*nchans)++];
	c[0] = c[-1];
	c->ic_flags = flags;
	c->ic_vht_ch_freq1 = 0;
	c->ic_vht_ch_freq2 = 0;
	set_extchan(c);
	set_vht_extchan(c);

	return (0);
}

/*
 * XXX VHT-2GHz
 */
static void
getflags_2ghz(const uint8_t bands[], uint32_t flags[], int ht40)
{
	int nmodes;

	nmodes = 0;
	if (isset(bands, IEEE80211_MODE_11B))
		flags[nmodes++] = IEEE80211_CHAN_B;
	if (isset(bands, IEEE80211_MODE_11G))
		flags[nmodes++] = IEEE80211_CHAN_G;
	if (isset(bands, IEEE80211_MODE_11NG))
		flags[nmodes++] = IEEE80211_CHAN_G | IEEE80211_CHAN_HT20;
	if (ht40) {
		flags[nmodes++] = IEEE80211_CHAN_G | IEEE80211_CHAN_HT40U;
		flags[nmodes++] = IEEE80211_CHAN_G | IEEE80211_CHAN_HT40D;
	}
	flags[nmodes] = 0;
}

static void
getflags_5ghz(const uint8_t bands[], uint32_t flags[], int ht40, int vht80)
{
	int nmodes;

	/*
	 * the addchan_list function seems to expect the flags array to
	 * be in channel width order, so the VHT bits are interspersed
	 * as appropriate to maintain said order.
	 *
	 * It also assumes HT40U is before HT40D.
	 */
	nmodes = 0;

	/* 20MHz */
	if (isset(bands, IEEE80211_MODE_11A))
		flags[nmodes++] = IEEE80211_CHAN_A;
	if (isset(bands, IEEE80211_MODE_11NA))
		flags[nmodes++] = IEEE80211_CHAN_A | IEEE80211_CHAN_HT20;
	if (isset(bands, IEEE80211_MODE_VHT_5GHZ)) {
		flags[nmodes++] = IEEE80211_CHAN_A | IEEE80211_CHAN_HT20 |
		    IEEE80211_CHAN_VHT20;
	}

	/* 40MHz */
	if (ht40) {
		flags[nmodes++] = IEEE80211_CHAN_A | IEEE80211_CHAN_HT40U;
	}
	if (ht40 && isset(bands, IEEE80211_MODE_VHT_5GHZ)) {
		flags[nmodes++] = IEEE80211_CHAN_A | IEEE80211_CHAN_HT40U
		    | IEEE80211_CHAN_VHT40U;
	}
	if (ht40) {
		flags[nmodes++] = IEEE80211_CHAN_A | IEEE80211_CHAN_HT40D;
	}
	if (ht40 && isset(bands, IEEE80211_MODE_VHT_5GHZ)) {
		flags[nmodes++] = IEEE80211_CHAN_A | IEEE80211_CHAN_HT40D
		    | IEEE80211_CHAN_VHT40D;
	}

	/* 80MHz */
	if (vht80 && isset(bands, IEEE80211_MODE_VHT_5GHZ)) {
		flags[nmodes++] = IEEE80211_CHAN_A |
		    IEEE80211_CHAN_HT40U | IEEE80211_CHAN_VHT80;
		flags[nmodes++] = IEEE80211_CHAN_A |
		    IEEE80211_CHAN_HT40D | IEEE80211_CHAN_VHT80;
	}

	/* XXX VHT80+80 */
	/* XXX VHT160 */
	flags[nmodes] = 0;
}

static void
getflags(const uint8_t bands[], uint32_t flags[], int ht40, int vht80)
{

	flags[0] = 0;
	if (isset(bands, IEEE80211_MODE_11A) ||
	    isset(bands, IEEE80211_MODE_11NA) ||
	    isset(bands, IEEE80211_MODE_VHT_5GHZ)) {
		if (isset(bands, IEEE80211_MODE_11B) ||
		    isset(bands, IEEE80211_MODE_11G) ||
		    isset(bands, IEEE80211_MODE_11NG) ||
		    isset(bands, IEEE80211_MODE_VHT_2GHZ))
			return;

		getflags_5ghz(bands, flags, ht40, vht80);
	} else
		getflags_2ghz(bands, flags, ht40);
}

/*
 * Add one 20 MHz channel into specified channel list.
 */
/* XXX VHT */
int
ieee80211_add_channel(struct ieee80211_channel chans[], int maxchans,
    int *nchans, uint8_t ieee, uint16_t freq, int8_t maxregpower,
    uint32_t chan_flags, const uint8_t bands[])
{
	uint32_t flags[IEEE80211_MODE_MAX];
	int i, error;

	getflags(bands, flags, 0, 0);
	KASSERT(flags[0] != 0, ("%s: no correct mode provided\n", __func__));

	error = addchan(chans, maxchans, nchans, ieee, freq, maxregpower,
	    flags[0] | chan_flags);
	for (i = 1; flags[i] != 0 && error == 0; i++) {
		error = copychan_prev(chans, maxchans, nchans,
		    flags[i] | chan_flags);
	}

	return (error);
}

static struct ieee80211_channel *
findchannel(struct ieee80211_channel chans[], int nchans, uint16_t freq,
    uint32_t flags)
{
	struct ieee80211_channel *c;
	int i;

	flags &= IEEE80211_CHAN_ALLTURBO;
	/* brute force search */
	for (i = 0; i < nchans; i++) {
		c = &chans[i];
		if (c->ic_freq == freq &&
		    (c->ic_flags & IEEE80211_CHAN_ALLTURBO) == flags)
			return c;
	}
	return NULL;
}

/*
 * Add 40 MHz channel pair into specified channel list.
 */
/* XXX VHT */
int
ieee80211_add_channel_ht40(struct ieee80211_channel chans[], int maxchans,
    int *nchans, uint8_t ieee, int8_t maxregpower, uint32_t flags)
{
	struct ieee80211_channel *cent, *extc;
	uint16_t freq;
	int error;

	freq = ieee80211_ieee2mhz(ieee, flags);

	/*
	 * Each entry defines an HT40 channel pair; find the
	 * center channel, then the extension channel above.
	 */
	flags |= IEEE80211_CHAN_HT20;
	cent = findchannel(chans, *nchans, freq, flags);
	if (cent == NULL)
		return (EINVAL);

	extc = findchannel(chans, *nchans, freq + 20, flags);
	if (extc == NULL)
		return (ENOENT);

	flags &= ~IEEE80211_CHAN_HT;
	error = addchan(chans, maxchans, nchans, cent->ic_ieee, cent->ic_freq,
	    maxregpower, flags | IEEE80211_CHAN_HT40U);
	if (error != 0)
		return (error);

	error = addchan(chans, maxchans, nchans, extc->ic_ieee, extc->ic_freq,
	    maxregpower, flags | IEEE80211_CHAN_HT40D);

	return (error);
}

/*
 * Fetch the center frequency for the primary channel.
 */
uint32_t
ieee80211_get_channel_center_freq(const struct ieee80211_channel *c)
{

	return (c->ic_freq);
}

/*
 * Fetch the center frequency for the primary BAND channel.
 *
 * For 5, 10, 20MHz channels it'll be the normally configured channel
 * frequency.
 *
 * For 40MHz, 80MHz, 160Mhz channels it'll the the centre of the
 * wide channel, not the centre of the primary channel (that's ic_freq).
 *
 * For 80+80MHz channels this will be the centre of the primary
 * 80MHz channel; the secondary 80MHz channel will be center_freq2().
 */
uint32_t
ieee80211_get_channel_center_freq1(const struct ieee80211_channel *c)
{

	/*
	 * VHT - use the pre-calculated centre frequency
	 * of the given channel.
	 */
	if (IEEE80211_IS_CHAN_VHT(c))
		return (ieee80211_ieee2mhz(c->ic_vht_ch_freq1, c->ic_flags));

	if (IEEE80211_IS_CHAN_HT40U(c)) {
		return (c->ic_freq + 10);
	}
	if (IEEE80211_IS_CHAN_HT40D(c)) {
		return (c->ic_freq - 10);
	}

	return (c->ic_freq);
}

/*
 * For now, no 80+80 support; it will likely always return 0.
 */
uint32_t
ieee80211_get_channel_center_freq2(const struct ieee80211_channel *c)
{

	if (IEEE80211_IS_CHAN_VHT(c) && (c->ic_vht_ch_freq2 != 0))
		return (ieee80211_ieee2mhz(c->ic_vht_ch_freq2, c->ic_flags));

	return (0);
}

/*
 * Adds channels into specified channel list (ieee[] array must be sorted).
 * Channels are already sorted.
 */
static int
add_chanlist(struct ieee80211_channel chans[], int maxchans, int *nchans,
    const uint8_t ieee[], int nieee, uint32_t flags[])
{
	uint16_t freq;
	int i, j, error;
	int is_vht;

	for (i = 0; i < nieee; i++) {
		freq = ieee80211_ieee2mhz(ieee[i], flags[0]);
		for (j = 0; flags[j] != 0; j++) {
			/*
			 * Notes:
			 * + HT40 and VHT40 channels occur together, so
			 *   we need to be careful that we actually allow that.
			 * + VHT80, VHT160 will coexist with HT40/VHT40, so
			 *   make sure it's not skipped because of the overlap
			 *   check used for (V)HT40.
			 */
			is_vht = !! (flags[j] & IEEE80211_CHAN_VHT);

			/*
			 * Test for VHT80.
			 * XXX This is all very broken right now.
			 * What we /should/ do is:
			 *
			 * + check that the frequency is in the list of
			 *   allowed VHT80 ranges; and
			 * + the other 3 channels in the list are actually
			 *   also available.
			 */
			if (is_vht && flags[j] & IEEE80211_CHAN_VHT80)
				if (! is_vht80_valid_freq(freq))
					continue;

			/*
			 * Test for (V)HT40.
			 *
			 * This is also a fall through from VHT80; as we only
			 * allow a VHT80 channel if the VHT40 combination is
			 * also valid.  If the VHT40 form is not valid then
			 * we certainly can't do VHT80..
			 */
			if (flags[j] & IEEE80211_CHAN_HT40D)
				/*
				 * Can't have a "lower" channel if we are the
				 * first channel.
				 *
				 * Can't have a "lower" channel if it's below/
				 * within 20MHz of the first channel.
				 *
				 * Can't have a "lower" channel if the channel
				 * below it is not 20MHz away.
				 */
				if (i == 0 || ieee[i] < ieee[0] + 4 ||
				    freq - 20 !=
				    ieee80211_ieee2mhz(ieee[i] - 4, flags[j]))
					continue;
			if (flags[j] & IEEE80211_CHAN_HT40U)
				/*
				 * Can't have an "upper" channel if we are
				 * the last channel.
				 *
				 * Can't have an "upper" channel be above the
				 * last channel in the list.
				 *
				 * Can't have an "upper" channel if the next
				 * channel according to the math isn't 20MHz
				 * away.  (Likely for channel 13/14.)
				 */
				if (i == nieee - 1 ||
				    ieee[i] + 4 > ieee[nieee - 1] ||
				    freq + 20 !=
				    ieee80211_ieee2mhz(ieee[i] + 4, flags[j]))
					continue;

			if (j == 0) {
				error = addchan(chans, maxchans, nchans,
				    ieee[i], freq, 0, flags[j]);
			} else {
				error = copychan_prev(chans, maxchans, nchans,
				    flags[j]);
			}
			if (error != 0)
				return (error);
		}
	}

	return (0);
}

int
ieee80211_add_channel_list_2ghz(struct ieee80211_channel chans[], int maxchans,
    int *nchans, const uint8_t ieee[], int nieee, const uint8_t bands[],
    int ht40)
{
	uint32_t flags[IEEE80211_MODE_MAX];

	/* XXX no VHT for now */
	getflags_2ghz(bands, flags, ht40);
	KASSERT(flags[0] != 0, ("%s: no correct mode provided\n", __func__));

	return (add_chanlist(chans, maxchans, nchans, ieee, nieee, flags));
}

int
ieee80211_add_channels_default_2ghz(struct ieee80211_channel chans[],
    int maxchans, int *nchans, const uint8_t bands[], int ht40)
{
	const uint8_t default_chan_list[] =
	    { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };

	return (ieee80211_add_channel_list_2ghz(chans, maxchans, nchans,
	    default_chan_list, nitems(default_chan_list), bands, ht40));
}

int
ieee80211_add_channel_list_5ghz(struct ieee80211_channel chans[], int maxchans,
    int *nchans, const uint8_t ieee[], int nieee, const uint8_t bands[],
    int ht40)
{
	uint32_t flags[IEEE80211_MODE_MAX];
	int vht80 = 0;

	/*
	 * For now, assume VHT == VHT80 support as a minimum.
	 */
	if (isset(bands, IEEE80211_MODE_VHT_5GHZ))
		vht80 = 1;

	getflags_5ghz(bands, flags, ht40, vht80);
	KASSERT(flags[0] != 0, ("%s: no correct mode provided\n", __func__));

	return (add_chanlist(chans, maxchans, nchans, ieee, nieee, flags));
}

/*
 * Locate a channel given a frequency+flags.  We cache
 * the previous lookup to optimize switching between two
 * channels--as happens with dynamic turbo.
 */
struct ieee80211_channel *
ieee80211_find_channel(struct ieee80211com *ic, int freq, int flags)
{
	struct ieee80211_channel *c;

	flags &= IEEE80211_CHAN_ALLTURBO;
	c = ic->ic_prevchan;
	if (c != NULL && c->ic_freq == freq &&
	    (c->ic_flags & IEEE80211_CHAN_ALLTURBO) == flags)
		return c;
	/* brute force search */
	return (findchannel(ic->ic_channels, ic->ic_nchans, freq, flags));
}

/*
 * Locate a channel given a channel number+flags.  We cache
 * the previous lookup to optimize switching between two
 * channels--as happens with dynamic turbo.
 */
struct ieee80211_channel *
ieee80211_find_channel_byieee(struct ieee80211com *ic, int ieee, int flags)
{
	struct ieee80211_channel *c;
	int i;

	flags &= IEEE80211_CHAN_ALLTURBO;
	c = ic->ic_prevchan;
	if (c != NULL && c->ic_ieee == ieee &&
	    (c->ic_flags & IEEE80211_CHAN_ALLTURBO) == flags)
		return c;
	/* brute force search */
	for (i = 0; i < ic->ic_nchans; i++) {
		c = &ic->ic_channels[i];
		if (c->ic_ieee == ieee &&
		    (c->ic_flags & IEEE80211_CHAN_ALLTURBO) == flags)
			return c;
	}
	return NULL;
}

/*
 * Lookup a channel suitable for the given rx status.
 *
 * This is used to find a channel for a frame (eg beacon, probe
 * response) based purely on the received PHY information.
 *
 * For now it tries to do it based on R_FREQ / R_IEEE.
 * This is enough for 11bg and 11a (and thus 11ng/11na)
 * but it will not be enough for GSM, PSB channels and the
 * like.  It also doesn't know about legacy-turbog and
 * legacy-turbo modes, which some offload NICs actually
 * support in weird ways.
 *
 * Takes the ic and rxstatus; returns the channel or NULL
 * if not found.
 *
 * XXX TODO: Add support for that when the need arises.
 */
struct ieee80211_channel *
ieee80211_lookup_channel_rxstatus(struct ieee80211vap *vap,
    const struct ieee80211_rx_stats *rxs)
{
	struct ieee80211com *ic = vap->iv_ic;
	uint32_t flags;
	struct ieee80211_channel *c;

	if (rxs == NULL)
		return (NULL);

	/*
	 * Strictly speaking we only use freq for now,
	 * however later on we may wish to just store
	 * the ieee for verification.
	 */
	if ((rxs->r_flags & IEEE80211_R_FREQ) == 0)
		return (NULL);
	if ((rxs->r_flags & IEEE80211_R_IEEE) == 0)
		return (NULL);

	/*
	 * If the rx status contains a valid ieee/freq, then
	 * ensure we populate the correct channel information
	 * in rxchan before passing it up to the scan infrastructure.
	 * Offload NICs will pass up beacons from all channels
	 * during background scans.
	 */

	/* Determine a band */
	/* XXX should be done by the driver? */
	if (rxs->c_freq < 3000) {
		flags = IEEE80211_CHAN_G;
	} else {
		flags = IEEE80211_CHAN_A;
	}

	/* Channel lookup */
	c = ieee80211_find_channel(ic, rxs->c_freq, flags);

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_INPUT,
	    "%s: freq=%d, ieee=%d, flags=0x%08x; c=%p\n",
	    __func__,
	    (int) rxs->c_freq,
	    (int) rxs->c_ieee,
	    flags,
	    c);

	return (c);
}

static void
addmedia(struct ifmedia *media, int caps, int addsta, int mode, int mword)
{
#define	ADD(_ic, _s, _o) \
	ifmedia_add(media, \
		IFM_MAKEWORD(IFM_IEEE80211, (_s), (_o), 0), 0, NULL)
	static const u_int mopts[IEEE80211_MODE_MAX] = {
	    [IEEE80211_MODE_AUTO]	= IFM_AUTO,
	    [IEEE80211_MODE_11A]	= IFM_IEEE80211_11A,
	    [IEEE80211_MODE_11B]	= IFM_IEEE80211_11B,
	    [IEEE80211_MODE_11G]	= IFM_IEEE80211_11G,
	    [IEEE80211_MODE_FH]		= IFM_IEEE80211_FH,
	    [IEEE80211_MODE_TURBO_A]	= IFM_IEEE80211_11A|IFM_IEEE80211_TURBO,
	    [IEEE80211_MODE_TURBO_G]	= IFM_IEEE80211_11G|IFM_IEEE80211_TURBO,
	    [IEEE80211_MODE_STURBO_A]	= IFM_IEEE80211_11A|IFM_IEEE80211_TURBO,
	    [IEEE80211_MODE_HALF]	= IFM_IEEE80211_11A,	/* XXX */
	    [IEEE80211_MODE_QUARTER]	= IFM_IEEE80211_11A,	/* XXX */
	    [IEEE80211_MODE_11NA]	= IFM_IEEE80211_11NA,
	    [IEEE80211_MODE_11NG]	= IFM_IEEE80211_11NG,
	    [IEEE80211_MODE_VHT_2GHZ]	= IFM_IEEE80211_VHT2G,
	    [IEEE80211_MODE_VHT_5GHZ]	= IFM_IEEE80211_VHT5G,
	};
	u_int mopt;

	mopt = mopts[mode];
	if (addsta)
		ADD(ic, mword, mopt);	/* STA mode has no cap */
	if (caps & IEEE80211_C_IBSS)
		ADD(media, mword, mopt | IFM_IEEE80211_ADHOC);
	if (caps & IEEE80211_C_HOSTAP)
		ADD(media, mword, mopt | IFM_IEEE80211_HOSTAP);
	if (caps & IEEE80211_C_AHDEMO)
		ADD(media, mword, mopt | IFM_IEEE80211_ADHOC | IFM_FLAG0);
	if (caps & IEEE80211_C_MONITOR)
		ADD(media, mword, mopt | IFM_IEEE80211_MONITOR);
	if (caps & IEEE80211_C_WDS)
		ADD(media, mword, mopt | IFM_IEEE80211_WDS);
	if (caps & IEEE80211_C_MBSS)
		ADD(media, mword, mopt | IFM_IEEE80211_MBSS);
#undef ADD
}

/*
 * Setup the media data structures according to the channel and
 * rate tables.
 */
static int
ieee80211_media_setup(struct ieee80211com *ic,
	struct ifmedia *media, int caps, int addsta,
	ifm_change_cb_t media_change, ifm_stat_cb_t media_stat)
{
	int i, j, rate, maxrate, mword, r;
	enum ieee80211_phymode mode;
	const struct ieee80211_rateset *rs;
	struct ieee80211_rateset allrates;

	/*
	 * Fill in media characteristics.
	 */
	ifmedia_init(media, 0, media_change, media_stat);
	maxrate = 0;
	/*
	 * Add media for legacy operating modes.
	 */
	memset(&allrates, 0, sizeof(allrates));
	for (mode = IEEE80211_MODE_AUTO; mode < IEEE80211_MODE_11NA; mode++) {
		if (isclr(ic->ic_modecaps, mode))
			continue;
		addmedia(media, caps, addsta, mode, IFM_AUTO);
		if (mode == IEEE80211_MODE_AUTO)
			continue;
		rs = &ic->ic_sup_rates[mode];
		for (i = 0; i < rs->rs_nrates; i++) {
			rate = rs->rs_rates[i];
			mword = ieee80211_rate2media(ic, rate, mode);
			if (mword == 0)
				continue;
			addmedia(media, caps, addsta, mode, mword);
			/*
			 * Add legacy rate to the collection of all rates.
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
		/* NB: remove media options from mword */
		addmedia(media, caps, addsta,
		    IEEE80211_MODE_AUTO, IFM_SUBTYPE(mword));
	}
	/*
	 * Add HT/11n media.  Note that we do not have enough
	 * bits in the media subtype to express the MCS so we
	 * use a "placeholder" media subtype and any fixed MCS
	 * must be specified with a different mechanism.
	 */
	for (; mode <= IEEE80211_MODE_11NG; mode++) {
		if (isclr(ic->ic_modecaps, mode))
			continue;
		addmedia(media, caps, addsta, mode, IFM_AUTO);
		addmedia(media, caps, addsta, mode, IFM_IEEE80211_MCS);
	}
	if (isset(ic->ic_modecaps, IEEE80211_MODE_11NA) ||
	    isset(ic->ic_modecaps, IEEE80211_MODE_11NG)) {
		addmedia(media, caps, addsta,
		    IEEE80211_MODE_AUTO, IFM_IEEE80211_MCS);
		i = ic->ic_txstream * 8 - 1;
		if ((ic->ic_htcaps & IEEE80211_HTCAP_CHWIDTH40) &&
		    (ic->ic_htcaps & IEEE80211_HTCAP_SHORTGI40))
			rate = ieee80211_htrates[i].ht40_rate_400ns;
		else if ((ic->ic_htcaps & IEEE80211_HTCAP_CHWIDTH40))
			rate = ieee80211_htrates[i].ht40_rate_800ns;
		else if ((ic->ic_htcaps & IEEE80211_HTCAP_SHORTGI20))
			rate = ieee80211_htrates[i].ht20_rate_400ns;
		else
			rate = ieee80211_htrates[i].ht20_rate_800ns;
		if (rate > maxrate)
			maxrate = rate;
	}

	/*
	 * Add VHT media.
	 */
	for (; mode <= IEEE80211_MODE_VHT_5GHZ; mode++) {
		if (isclr(ic->ic_modecaps, mode))
			continue;
		addmedia(media, caps, addsta, mode, IFM_AUTO);
		addmedia(media, caps, addsta, mode, IFM_IEEE80211_VHT);

		/* XXX TODO: VHT maxrate */
	}

	return maxrate;
}

/* XXX inline or eliminate? */
const struct ieee80211_rateset *
ieee80211_get_suprates(struct ieee80211com *ic, const struct ieee80211_channel *c)
{
	/* XXX does this work for 11ng basic rates? */
	return &ic->ic_sup_rates[ieee80211_chan2mode(c)];
}

/* XXX inline or eliminate? */
const struct ieee80211_htrateset *
ieee80211_get_suphtrates(struct ieee80211com *ic,
    const struct ieee80211_channel *c)
{
	return &ic->ic_sup_htrates;
}

void
ieee80211_announce(struct ieee80211com *ic)
{
	int i, rate, mword;
	enum ieee80211_phymode mode;
	const struct ieee80211_rateset *rs;

	/* NB: skip AUTO since it has no rates */
	for (mode = IEEE80211_MODE_AUTO+1; mode < IEEE80211_MODE_11NA; mode++) {
		if (isclr(ic->ic_modecaps, mode))
			continue;
		ic_printf(ic, "%s rates: ", ieee80211_phymode_name[mode]);
		rs = &ic->ic_sup_rates[mode];
		for (i = 0; i < rs->rs_nrates; i++) {
			mword = ieee80211_rate2media(ic, rs->rs_rates[i], mode);
			if (mword == 0)
				continue;
			rate = ieee80211_media2rate(mword);
			printf("%s%d%sMbps", (i != 0 ? " " : ""),
			    rate / 2, ((rate & 0x1) != 0 ? ".5" : ""));
		}
		printf("\n");
	}
	ieee80211_ht_announce(ic);
	ieee80211_vht_announce(ic);
}

void
ieee80211_announce_channels(struct ieee80211com *ic)
{
	const struct ieee80211_channel *c;
	char type;
	int i, cw;

	printf("Chan  Freq  CW  RegPwr  MinPwr  MaxPwr\n");
	for (i = 0; i < ic->ic_nchans; i++) {
		c = &ic->ic_channels[i];
		if (IEEE80211_IS_CHAN_ST(c))
			type = 'S';
		else if (IEEE80211_IS_CHAN_108A(c))
			type = 'T';
		else if (IEEE80211_IS_CHAN_108G(c))
			type = 'G';
		else if (IEEE80211_IS_CHAN_HT(c))
			type = 'n';
		else if (IEEE80211_IS_CHAN_A(c))
			type = 'a';
		else if (IEEE80211_IS_CHAN_ANYG(c))
			type = 'g';
		else if (IEEE80211_IS_CHAN_B(c))
			type = 'b';
		else
			type = 'f';
		if (IEEE80211_IS_CHAN_HT40(c) || IEEE80211_IS_CHAN_TURBO(c))
			cw = 40;
		else if (IEEE80211_IS_CHAN_HALF(c))
			cw = 10;
		else if (IEEE80211_IS_CHAN_QUARTER(c))
			cw = 5;
		else
			cw = 20;
		printf("%4d  %4d%c %2d%c %6d  %4d.%d  %4d.%d\n"
			, c->ic_ieee, c->ic_freq, type
			, cw
			, IEEE80211_IS_CHAN_HT40U(c) ? '+' :
			  IEEE80211_IS_CHAN_HT40D(c) ? '-' : ' '
			, c->ic_maxregpower
			, c->ic_minpower / 2, c->ic_minpower & 1 ? 5 : 0
			, c->ic_maxpower / 2, c->ic_maxpower & 1 ? 5 : 0
		);
	}
}

static int
media2mode(const struct ifmedia_entry *ime, uint32_t flags, uint16_t *mode)
{
	switch (IFM_MODE(ime->ifm_media)) {
	case IFM_IEEE80211_11A:
		*mode = IEEE80211_MODE_11A;
		break;
	case IFM_IEEE80211_11B:
		*mode = IEEE80211_MODE_11B;
		break;
	case IFM_IEEE80211_11G:
		*mode = IEEE80211_MODE_11G;
		break;
	case IFM_IEEE80211_FH:
		*mode = IEEE80211_MODE_FH;
		break;
	case IFM_IEEE80211_11NA:
		*mode = IEEE80211_MODE_11NA;
		break;
	case IFM_IEEE80211_11NG:
		*mode = IEEE80211_MODE_11NG;
		break;
	case IFM_AUTO:
		*mode = IEEE80211_MODE_AUTO;
		break;
	default:
		return 0;
	}
	/*
	 * Turbo mode is an ``option''.
	 * XXX does not apply to AUTO
	 */
	if (ime->ifm_media & IFM_IEEE80211_TURBO) {
		if (*mode == IEEE80211_MODE_11A) {
			if (flags & IEEE80211_F_TURBOP)
				*mode = IEEE80211_MODE_TURBO_A;
			else
				*mode = IEEE80211_MODE_STURBO_A;
		} else if (*mode == IEEE80211_MODE_11G)
			*mode = IEEE80211_MODE_TURBO_G;
		else
			return 0;
	}
	/* XXX HT40 +/- */
	return 1;
}

/*
 * Handle a media change request on the vap interface.
 */
int
ieee80211_media_change(struct ifnet *ifp)
{
	struct ieee80211vap *vap = ifp->if_softc;
	struct ifmedia_entry *ime = vap->iv_media.ifm_cur;
	uint16_t newmode;

	if (!media2mode(ime, vap->iv_flags, &newmode))
		return EINVAL;
	if (vap->iv_des_mode != newmode) {
		vap->iv_des_mode = newmode;
		/* XXX kick state machine if up+running */
	}
	return 0;
}

/*
 * Common code to calculate the media status word
 * from the operating mode and channel state.
 */
static int
media_status(enum ieee80211_opmode opmode, const struct ieee80211_channel *chan)
{
	int status;

	status = IFM_IEEE80211;
	switch (opmode) {
	case IEEE80211_M_STA:
		break;
	case IEEE80211_M_IBSS:
		status |= IFM_IEEE80211_ADHOC;
		break;
	case IEEE80211_M_HOSTAP:
		status |= IFM_IEEE80211_HOSTAP;
		break;
	case IEEE80211_M_MONITOR:
		status |= IFM_IEEE80211_MONITOR;
		break;
	case IEEE80211_M_AHDEMO:
		status |= IFM_IEEE80211_ADHOC | IFM_FLAG0;
		break;
	case IEEE80211_M_WDS:
		status |= IFM_IEEE80211_WDS;
		break;
	case IEEE80211_M_MBSS:
		status |= IFM_IEEE80211_MBSS;
		break;
	}
	if (IEEE80211_IS_CHAN_HTA(chan)) {
		status |= IFM_IEEE80211_11NA;
	} else if (IEEE80211_IS_CHAN_HTG(chan)) {
		status |= IFM_IEEE80211_11NG;
	} else if (IEEE80211_IS_CHAN_A(chan)) {
		status |= IFM_IEEE80211_11A;
	} else if (IEEE80211_IS_CHAN_B(chan)) {
		status |= IFM_IEEE80211_11B;
	} else if (IEEE80211_IS_CHAN_ANYG(chan)) {
		status |= IFM_IEEE80211_11G;
	} else if (IEEE80211_IS_CHAN_FHSS(chan)) {
		status |= IFM_IEEE80211_FH;
	}
	/* XXX else complain? */

	if (IEEE80211_IS_CHAN_TURBO(chan))
		status |= IFM_IEEE80211_TURBO;
#if 0
	if (IEEE80211_IS_CHAN_HT20(chan))
		status |= IFM_IEEE80211_HT20;
	if (IEEE80211_IS_CHAN_HT40(chan))
		status |= IFM_IEEE80211_HT40;
#endif
	return status;
}

void
ieee80211_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct ieee80211vap *vap = ifp->if_softc;
	struct ieee80211com *ic = vap->iv_ic;
	enum ieee80211_phymode mode;

	imr->ifm_status = IFM_AVALID;
	/*
	 * NB: use the current channel's mode to lock down a xmit
	 * rate only when running; otherwise we may have a mismatch
	 * in which case the rate will not be convertible.
	 */
	if (vap->iv_state == IEEE80211_S_RUN ||
	    vap->iv_state == IEEE80211_S_SLEEP) {
		imr->ifm_status |= IFM_ACTIVE;
		mode = ieee80211_chan2mode(ic->ic_curchan);
	} else
		mode = IEEE80211_MODE_AUTO;
	imr->ifm_active = media_status(vap->iv_opmode, ic->ic_curchan);
	/*
	 * Calculate a current rate if possible.
	 */
	if (vap->iv_txparms[mode].ucastrate != IEEE80211_FIXED_RATE_NONE) {
		/*
		 * A fixed rate is set, report that.
		 */
		imr->ifm_active |= ieee80211_rate2media(ic,
			vap->iv_txparms[mode].ucastrate, mode);
	} else if (vap->iv_opmode == IEEE80211_M_STA) {
		/*
		 * In station mode report the current transmit rate.
		 */
		imr->ifm_active |= ieee80211_rate2media(ic,
			vap->iv_bss->ni_txrate, mode);
	} else
		imr->ifm_active |= IFM_AUTO;
	if (imr->ifm_status & IFM_ACTIVE)
		imr->ifm_current = imr->ifm_active;
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
	/*
	 * Adjust basic rates in 11b/11g supported rate set.
	 * Note that if operating on a hal/quarter rate channel
	 * this is a noop as those rates sets are different
	 * and used instead.
	 */
	if (mode == IEEE80211_MODE_11G || mode == IEEE80211_MODE_11B)
		ieee80211_setbasicrates(&ic->ic_sup_rates[mode], mode);

	ic->ic_curmode = mode;
	ieee80211_reset_erp(ic);	/* reset ERP state */

	return 0;
}

/*
 * Return the phy mode for with the specified channel.
 */
enum ieee80211_phymode
ieee80211_chan2mode(const struct ieee80211_channel *chan)
{

	if (IEEE80211_IS_CHAN_VHT_2GHZ(chan))
		return IEEE80211_MODE_VHT_2GHZ;
	else if (IEEE80211_IS_CHAN_VHT_5GHZ(chan))
		return IEEE80211_MODE_VHT_5GHZ;
	else if (IEEE80211_IS_CHAN_HTA(chan))
		return IEEE80211_MODE_11NA;
	else if (IEEE80211_IS_CHAN_HTG(chan))
		return IEEE80211_MODE_11NG;
	else if (IEEE80211_IS_CHAN_108G(chan))
		return IEEE80211_MODE_TURBO_G;
	else if (IEEE80211_IS_CHAN_ST(chan))
		return IEEE80211_MODE_STURBO_A;
	else if (IEEE80211_IS_CHAN_TURBO(chan))
		return IEEE80211_MODE_TURBO_A;
	else if (IEEE80211_IS_CHAN_HALF(chan))
		return IEEE80211_MODE_HALF;
	else if (IEEE80211_IS_CHAN_QUARTER(chan))
		return IEEE80211_MODE_QUARTER;
	else if (IEEE80211_IS_CHAN_A(chan))
		return IEEE80211_MODE_11A;
	else if (IEEE80211_IS_CHAN_ANYG(chan))
		return IEEE80211_MODE_11G;
	else if (IEEE80211_IS_CHAN_B(chan))
		return IEEE80211_MODE_11B;
	else if (IEEE80211_IS_CHAN_FHSS(chan))
		return IEEE80211_MODE_FH;

	/* NB: should not get here */
	printf("%s: cannot map channel to mode; freq %u flags 0x%x\n",
		__func__, chan->ic_freq, chan->ic_flags);
	return IEEE80211_MODE_11B;
}

struct ratemedia {
	u_int	match;	/* rate + mode */
	u_int	media;	/* if_media rate */
};

static int
findmedia(const struct ratemedia rates[], int n, u_int match)
{
	int i;

	for (i = 0; i < n; i++)
		if (rates[i].match == match)
			return rates[i].media;
	return IFM_AUTO;
}

/*
 * Convert IEEE80211 rate value to ifmedia subtype.
 * Rate is either a legacy rate in units of 0.5Mbps
 * or an MCS index.
 */
int
ieee80211_rate2media(struct ieee80211com *ic, int rate, enum ieee80211_phymode mode)
{
	static const struct ratemedia rates[] = {
		{   2 | IFM_IEEE80211_FH, IFM_IEEE80211_FH1 },
		{   4 | IFM_IEEE80211_FH, IFM_IEEE80211_FH2 },
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
		{   6 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM3 },
		{   9 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM4 },
		{  54 | IFM_IEEE80211_11A, IFM_IEEE80211_OFDM27 },
		/* NB: OFDM72 doesn't really exist so we don't handle it */
	};
	static const struct ratemedia htrates[] = {
		{   0, IFM_IEEE80211_MCS },
		{   1, IFM_IEEE80211_MCS },
		{   2, IFM_IEEE80211_MCS },
		{   3, IFM_IEEE80211_MCS },
		{   4, IFM_IEEE80211_MCS },
		{   5, IFM_IEEE80211_MCS },
		{   6, IFM_IEEE80211_MCS },
		{   7, IFM_IEEE80211_MCS },
		{   8, IFM_IEEE80211_MCS },
		{   9, IFM_IEEE80211_MCS },
		{  10, IFM_IEEE80211_MCS },
		{  11, IFM_IEEE80211_MCS },
		{  12, IFM_IEEE80211_MCS },
		{  13, IFM_IEEE80211_MCS },
		{  14, IFM_IEEE80211_MCS },
		{  15, IFM_IEEE80211_MCS },
		{  16, IFM_IEEE80211_MCS },
		{  17, IFM_IEEE80211_MCS },
		{  18, IFM_IEEE80211_MCS },
		{  19, IFM_IEEE80211_MCS },
		{  20, IFM_IEEE80211_MCS },
		{  21, IFM_IEEE80211_MCS },
		{  22, IFM_IEEE80211_MCS },
		{  23, IFM_IEEE80211_MCS },
		{  24, IFM_IEEE80211_MCS },
		{  25, IFM_IEEE80211_MCS },
		{  26, IFM_IEEE80211_MCS },
		{  27, IFM_IEEE80211_MCS },
		{  28, IFM_IEEE80211_MCS },
		{  29, IFM_IEEE80211_MCS },
		{  30, IFM_IEEE80211_MCS },
		{  31, IFM_IEEE80211_MCS },
		{  32, IFM_IEEE80211_MCS },
		{  33, IFM_IEEE80211_MCS },
		{  34, IFM_IEEE80211_MCS },
		{  35, IFM_IEEE80211_MCS },
		{  36, IFM_IEEE80211_MCS },
		{  37, IFM_IEEE80211_MCS },
		{  38, IFM_IEEE80211_MCS },
		{  39, IFM_IEEE80211_MCS },
		{  40, IFM_IEEE80211_MCS },
		{  41, IFM_IEEE80211_MCS },
		{  42, IFM_IEEE80211_MCS },
		{  43, IFM_IEEE80211_MCS },
		{  44, IFM_IEEE80211_MCS },
		{  45, IFM_IEEE80211_MCS },
		{  46, IFM_IEEE80211_MCS },
		{  47, IFM_IEEE80211_MCS },
		{  48, IFM_IEEE80211_MCS },
		{  49, IFM_IEEE80211_MCS },
		{  50, IFM_IEEE80211_MCS },
		{  51, IFM_IEEE80211_MCS },
		{  52, IFM_IEEE80211_MCS },
		{  53, IFM_IEEE80211_MCS },
		{  54, IFM_IEEE80211_MCS },
		{  55, IFM_IEEE80211_MCS },
		{  56, IFM_IEEE80211_MCS },
		{  57, IFM_IEEE80211_MCS },
		{  58, IFM_IEEE80211_MCS },
		{  59, IFM_IEEE80211_MCS },
		{  60, IFM_IEEE80211_MCS },
		{  61, IFM_IEEE80211_MCS },
		{  62, IFM_IEEE80211_MCS },
		{  63, IFM_IEEE80211_MCS },
		{  64, IFM_IEEE80211_MCS },
		{  65, IFM_IEEE80211_MCS },
		{  66, IFM_IEEE80211_MCS },
		{  67, IFM_IEEE80211_MCS },
		{  68, IFM_IEEE80211_MCS },
		{  69, IFM_IEEE80211_MCS },
		{  70, IFM_IEEE80211_MCS },
		{  71, IFM_IEEE80211_MCS },
		{  72, IFM_IEEE80211_MCS },
		{  73, IFM_IEEE80211_MCS },
		{  74, IFM_IEEE80211_MCS },
		{  75, IFM_IEEE80211_MCS },
		{  76, IFM_IEEE80211_MCS },
	};
	int m;

	/*
	 * Check 11n rates first for match as an MCS.
	 */
	if (mode == IEEE80211_MODE_11NA) {
		if (rate & IEEE80211_RATE_MCS) {
			rate &= ~IEEE80211_RATE_MCS;
			m = findmedia(htrates, nitems(htrates), rate);
			if (m != IFM_AUTO)
				return m | IFM_IEEE80211_11NA;
		}
	} else if (mode == IEEE80211_MODE_11NG) {
		/* NB: 12 is ambiguous, it will be treated as an MCS */
		if (rate & IEEE80211_RATE_MCS) {
			rate &= ~IEEE80211_RATE_MCS;
			m = findmedia(htrates, nitems(htrates), rate);
			if (m != IFM_AUTO)
				return m | IFM_IEEE80211_11NG;
		}
	}
	rate &= IEEE80211_RATE_VAL;
	switch (mode) {
	case IEEE80211_MODE_11A:
	case IEEE80211_MODE_HALF:		/* XXX good 'nuf */
	case IEEE80211_MODE_QUARTER:
	case IEEE80211_MODE_11NA:
	case IEEE80211_MODE_TURBO_A:
	case IEEE80211_MODE_STURBO_A:
		return findmedia(rates, nitems(rates),
		    rate | IFM_IEEE80211_11A);
	case IEEE80211_MODE_11B:
		return findmedia(rates, nitems(rates),
		    rate | IFM_IEEE80211_11B);
	case IEEE80211_MODE_FH:
		return findmedia(rates, nitems(rates),
		    rate | IFM_IEEE80211_FH);
	case IEEE80211_MODE_AUTO:
		/* NB: ic may be NULL for some drivers */
		if (ic != NULL && ic->ic_phytype == IEEE80211_T_FH)
			return findmedia(rates, nitems(rates),
			    rate | IFM_IEEE80211_FH);
		/* NB: hack, 11g matches both 11b+11a rates */
		/* fall thru... */
	case IEEE80211_MODE_11G:
	case IEEE80211_MODE_11NG:
	case IEEE80211_MODE_TURBO_G:
		return findmedia(rates, nitems(rates), rate | IFM_IEEE80211_11G);
	case IEEE80211_MODE_VHT_2GHZ:
	case IEEE80211_MODE_VHT_5GHZ:
		/* XXX TODO: need to figure out mapping for VHT rates */
		return IFM_AUTO;
	}
	return IFM_AUTO;
}

int
ieee80211_media2rate(int mword)
{
	static const int ieeerates[] = {
		-1,		/* IFM_AUTO */
		0,		/* IFM_MANUAL */
		0,		/* IFM_NONE */
		2,		/* IFM_IEEE80211_FH1 */
		4,		/* IFM_IEEE80211_FH2 */
		2,		/* IFM_IEEE80211_DS1 */
		4,		/* IFM_IEEE80211_DS2 */
		11,		/* IFM_IEEE80211_DS5 */
		22,		/* IFM_IEEE80211_DS11 */
		44,		/* IFM_IEEE80211_DS22 */
		12,		/* IFM_IEEE80211_OFDM6 */
		18,		/* IFM_IEEE80211_OFDM9 */
		24,		/* IFM_IEEE80211_OFDM12 */
		36,		/* IFM_IEEE80211_OFDM18 */
		48,		/* IFM_IEEE80211_OFDM24 */
		72,		/* IFM_IEEE80211_OFDM36 */
		96,		/* IFM_IEEE80211_OFDM48 */
		108,		/* IFM_IEEE80211_OFDM54 */
		144,		/* IFM_IEEE80211_OFDM72 */
		0,		/* IFM_IEEE80211_DS354k */
		0,		/* IFM_IEEE80211_DS512k */
		6,		/* IFM_IEEE80211_OFDM3 */
		9,		/* IFM_IEEE80211_OFDM4 */
		54,		/* IFM_IEEE80211_OFDM27 */
		-1,		/* IFM_IEEE80211_MCS */
		-1,		/* IFM_IEEE80211_VHT */
	};
	return IFM_SUBTYPE(mword) < nitems(ieeerates) ?
		ieeerates[IFM_SUBTYPE(mword)] : 0;
}

/*
 * The following hash function is adapted from "Hash Functions" by Bob Jenkins
 * ("Algorithm Alley", Dr. Dobbs Journal, September 1997).
 */
#define	mix(a, b, c)							\
do {									\
	a -= b; a -= c; a ^= (c >> 13);					\
	b -= c; b -= a; b ^= (a << 8);					\
	c -= a; c -= b; c ^= (b >> 13);					\
	a -= b; a -= c; a ^= (c >> 12);					\
	b -= c; b -= a; b ^= (a << 16);					\
	c -= a; c -= b; c ^= (b >> 5);					\
	a -= b; a -= c; a ^= (c >> 3);					\
	b -= c; b -= a; b ^= (a << 10);					\
	c -= a; c -= b; c ^= (b >> 15);					\
} while (/*CONSTCOND*/0)

uint32_t
ieee80211_mac_hash(const struct ieee80211com *ic,
	const uint8_t addr[IEEE80211_ADDR_LEN])
{
	uint32_t a = 0x9e3779b9, b = 0x9e3779b9, c = ic->ic_hash_key;

	b += addr[5] << 8;
	b += addr[4];
	a += addr[3] << 24;
	a += addr[2] << 16;
	a += addr[1] << 8;
	a += addr[0];

	mix(a, b, c);

	return c;
}
#undef mix

char
ieee80211_channel_type_char(const struct ieee80211_channel *c)
{
	if (IEEE80211_IS_CHAN_ST(c))
		return 'S';
	if (IEEE80211_IS_CHAN_108A(c))
		return 'T';
	if (IEEE80211_IS_CHAN_108G(c))
		return 'G';
	if (IEEE80211_IS_CHAN_VHT(c))
		return 'v';
	if (IEEE80211_IS_CHAN_HT(c))
		return 'n';
	if (IEEE80211_IS_CHAN_A(c))
		return 'a';
	if (IEEE80211_IS_CHAN_ANYG(c))
		return 'g';
	if (IEEE80211_IS_CHAN_B(c))
		return 'b';
	return 'f';
}
