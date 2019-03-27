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
 * IEEE 802.11 ioctl support (FreeBSD-specific)
 */

#include "opt_inet.h"
#include "opt_wlan.h"

#include <sys/endian.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
 
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_input.h>

#define	IS_UP_AUTO(_vap) \
	(IFNET_IS_UP_RUNNING((_vap)->iv_ifp) && \
	 (_vap)->iv_roaming == IEEE80211_ROAMING_AUTO)

static const uint8_t zerobssid[IEEE80211_ADDR_LEN];
static struct ieee80211_channel *findchannel(struct ieee80211com *,
		int ieee, int mode);
static int ieee80211_scanreq(struct ieee80211vap *,
		struct ieee80211_scan_req *);

static int
ieee80211_ioctl_getkey(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *ni;
	struct ieee80211req_key ik;
	struct ieee80211_key *wk;
	const struct ieee80211_cipher *cip;
	u_int kid;
	int error;

	if (ireq->i_len != sizeof(ik))
		return EINVAL;
	error = copyin(ireq->i_data, &ik, sizeof(ik));
	if (error)
		return error;
	kid = ik.ik_keyix;
	if (kid == IEEE80211_KEYIX_NONE) {
		ni = ieee80211_find_vap_node(&ic->ic_sta, vap, ik.ik_macaddr);
		if (ni == NULL)
			return ENOENT;
		wk = &ni->ni_ucastkey;
	} else {
		if (kid >= IEEE80211_WEP_NKID)
			return EINVAL;
		wk = &vap->iv_nw_keys[kid];
		IEEE80211_ADDR_COPY(&ik.ik_macaddr, vap->iv_bss->ni_macaddr);
		ni = NULL;
	}
	cip = wk->wk_cipher;
	ik.ik_type = cip->ic_cipher;
	ik.ik_keylen = wk->wk_keylen;
	ik.ik_flags = wk->wk_flags & (IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV);
	if (wk->wk_keyix == vap->iv_def_txkey)
		ik.ik_flags |= IEEE80211_KEY_DEFAULT;
	if (priv_check(curthread, PRIV_NET80211_GETKEY) == 0) {
		/* NB: only root can read key data */
		ik.ik_keyrsc = wk->wk_keyrsc[IEEE80211_NONQOS_TID];
		ik.ik_keytsc = wk->wk_keytsc;
		memcpy(ik.ik_keydata, wk->wk_key, wk->wk_keylen);
		if (cip->ic_cipher == IEEE80211_CIPHER_TKIP) {
			memcpy(ik.ik_keydata+wk->wk_keylen,
				wk->wk_key + IEEE80211_KEYBUF_SIZE,
				IEEE80211_MICBUF_SIZE);
			ik.ik_keylen += IEEE80211_MICBUF_SIZE;
		}
	} else {
		ik.ik_keyrsc = 0;
		ik.ik_keytsc = 0;
		memset(ik.ik_keydata, 0, sizeof(ik.ik_keydata));
	}
	if (ni != NULL)
		ieee80211_free_node(ni);
	return copyout(&ik, ireq->i_data, sizeof(ik));
}

static int
ieee80211_ioctl_getchanlist(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;

	if (sizeof(ic->ic_chan_active) < ireq->i_len)
		ireq->i_len = sizeof(ic->ic_chan_active);
	return copyout(&ic->ic_chan_active, ireq->i_data, ireq->i_len);
}

static int
ieee80211_ioctl_getchaninfo(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	uint32_t space;

	space = __offsetof(struct ieee80211req_chaninfo,
			ic_chans[ic->ic_nchans]);
	if (space > ireq->i_len)
		space = ireq->i_len;
	/* XXX assumes compatible layout */
	return copyout(&ic->ic_nchans, ireq->i_data, space);
}

static int
ieee80211_ioctl_getwpaie(struct ieee80211vap *vap,
	struct ieee80211req *ireq, int req)
{
	struct ieee80211_node *ni;
	struct ieee80211req_wpaie2 *wpaie;
	int error;

	if (ireq->i_len < IEEE80211_ADDR_LEN)
		return EINVAL;
	wpaie = IEEE80211_MALLOC(sizeof(*wpaie), M_TEMP,
	    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (wpaie == NULL)
		return ENOMEM;
	error = copyin(ireq->i_data, wpaie->wpa_macaddr, IEEE80211_ADDR_LEN);
	if (error != 0)
		goto bad;
	ni = ieee80211_find_vap_node(&vap->iv_ic->ic_sta, vap, wpaie->wpa_macaddr);
	if (ni == NULL) {
		error = ENOENT;
		goto bad;
	}
	if (ni->ni_ies.wpa_ie != NULL) {
		int ielen = ni->ni_ies.wpa_ie[1] + 2;
		if (ielen > sizeof(wpaie->wpa_ie))
			ielen = sizeof(wpaie->wpa_ie);
		memcpy(wpaie->wpa_ie, ni->ni_ies.wpa_ie, ielen);
	}
	if (req == IEEE80211_IOC_WPAIE2) {
		if (ni->ni_ies.rsn_ie != NULL) {
			int ielen = ni->ni_ies.rsn_ie[1] + 2;
			if (ielen > sizeof(wpaie->rsn_ie))
				ielen = sizeof(wpaie->rsn_ie);
			memcpy(wpaie->rsn_ie, ni->ni_ies.rsn_ie, ielen);
		}
		if (ireq->i_len > sizeof(struct ieee80211req_wpaie2))
			ireq->i_len = sizeof(struct ieee80211req_wpaie2);
	} else {
		/* compatibility op, may overwrite wpa ie */
		/* XXX check ic_flags? */
		if (ni->ni_ies.rsn_ie != NULL) {
			int ielen = ni->ni_ies.rsn_ie[1] + 2;
			if (ielen > sizeof(wpaie->wpa_ie))
				ielen = sizeof(wpaie->wpa_ie);
			memcpy(wpaie->wpa_ie, ni->ni_ies.rsn_ie, ielen);
		}
		if (ireq->i_len > sizeof(struct ieee80211req_wpaie))
			ireq->i_len = sizeof(struct ieee80211req_wpaie);
	}
	ieee80211_free_node(ni);
	error = copyout(wpaie, ireq->i_data, ireq->i_len);
bad:
	IEEE80211_FREE(wpaie, M_TEMP);
	return error;
}

static int
ieee80211_ioctl_getstastats(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_node *ni;
	uint8_t macaddr[IEEE80211_ADDR_LEN];
	const size_t off = __offsetof(struct ieee80211req_sta_stats, is_stats);
	int error;

	if (ireq->i_len < off)
		return EINVAL;
	error = copyin(ireq->i_data, macaddr, IEEE80211_ADDR_LEN);
	if (error != 0)
		return error;
	ni = ieee80211_find_vap_node(&vap->iv_ic->ic_sta, vap, macaddr);
	if (ni == NULL)
		return ENOENT;
	if (ireq->i_len > sizeof(struct ieee80211req_sta_stats))
		ireq->i_len = sizeof(struct ieee80211req_sta_stats);
	/* NB: copy out only the statistics */
	error = copyout(&ni->ni_stats, (uint8_t *) ireq->i_data + off,
			ireq->i_len - off);
	ieee80211_free_node(ni);
	return error;
}

struct scanreq {
	struct ieee80211req_scan_result *sr;
	size_t space;
};

static size_t
scan_space(const struct ieee80211_scan_entry *se, int *ielen)
{
	size_t len;

	*ielen = se->se_ies.len;
	/*
	 * NB: ie's can be no more than 255 bytes and the max 802.11
	 * packet is <3Kbytes so we are sure this doesn't overflow
	 * 16-bits; if this is a concern we can drop the ie's.
	 */
	len = sizeof(struct ieee80211req_scan_result) + se->se_ssid[1] +
	    se->se_meshid[1] + *ielen;
	return roundup(len, sizeof(uint32_t));
}

static void
get_scan_space(void *arg, const struct ieee80211_scan_entry *se)
{
	struct scanreq *req = arg;
	int ielen;

	req->space += scan_space(se, &ielen);
}

static void
get_scan_result(void *arg, const struct ieee80211_scan_entry *se)
{
	struct scanreq *req = arg;
	struct ieee80211req_scan_result *sr;
	int ielen, len, nr, nxr;
	uint8_t *cp;

	len = scan_space(se, &ielen);
	if (len > req->space)
		return;

	sr = req->sr;
	KASSERT(len <= 65535 && ielen <= 65535,
	    ("len %u ssid %u ie %u", len, se->se_ssid[1], ielen));
	sr->isr_len = len;
	sr->isr_ie_off = sizeof(struct ieee80211req_scan_result);
	sr->isr_ie_len = ielen;
	sr->isr_freq = se->se_chan->ic_freq;
	sr->isr_flags = se->se_chan->ic_flags;
	sr->isr_rssi = se->se_rssi;
	sr->isr_noise = se->se_noise;
	sr->isr_intval = se->se_intval;
	sr->isr_capinfo = se->se_capinfo;
	sr->isr_erp = se->se_erp;
	IEEE80211_ADDR_COPY(sr->isr_bssid, se->se_bssid);
	nr = min(se->se_rates[1], IEEE80211_RATE_MAXSIZE);
	memcpy(sr->isr_rates, se->se_rates+2, nr);
	nxr = min(se->se_xrates[1], IEEE80211_RATE_MAXSIZE - nr);
	memcpy(sr->isr_rates+nr, se->se_xrates+2, nxr);
	sr->isr_nrates = nr + nxr;

	/* copy SSID */
	sr->isr_ssid_len = se->se_ssid[1];
	cp = ((uint8_t *)sr) + sr->isr_ie_off;
	memcpy(cp, se->se_ssid+2, sr->isr_ssid_len);

	/* copy mesh id */
	cp += sr->isr_ssid_len;
	sr->isr_meshid_len = se->se_meshid[1];
	memcpy(cp, se->se_meshid+2, sr->isr_meshid_len);
	cp += sr->isr_meshid_len;

	if (ielen)
		memcpy(cp, se->se_ies.data, ielen);

	req->space -= len;
	req->sr = (struct ieee80211req_scan_result *)(((uint8_t *)sr) + len);
}

static int
ieee80211_ioctl_getscanresults(struct ieee80211vap *vap,
	struct ieee80211req *ireq)
{
	struct scanreq req;
	int error;

	if (ireq->i_len < sizeof(struct scanreq))
		return EFAULT;

	error = 0;
	req.space = 0;
	ieee80211_scan_iterate(vap, get_scan_space, &req);
	if (req.space > ireq->i_len)
		req.space = ireq->i_len;
	if (req.space > 0) {
		uint32_t space;
		void *p;

		space = req.space;
		/* XXX M_WAITOK after driver lock released */
		p = IEEE80211_MALLOC(space, M_TEMP,
		    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
		if (p == NULL)
			return ENOMEM;
		req.sr = p;
		ieee80211_scan_iterate(vap, get_scan_result, &req);
		ireq->i_len = space - req.space;
		error = copyout(p, ireq->i_data, ireq->i_len);
		IEEE80211_FREE(p, M_TEMP);
	} else
		ireq->i_len = 0;

	return error;
}

struct stainforeq {
	struct ieee80211req_sta_info *si;
	size_t	space;
};

static size_t
sta_space(const struct ieee80211_node *ni, size_t *ielen)
{
	*ielen = ni->ni_ies.len;
	return roundup(sizeof(struct ieee80211req_sta_info) + *ielen,
		      sizeof(uint32_t));
}

static void
get_sta_space(void *arg, struct ieee80211_node *ni)
{
	struct stainforeq *req = arg;
	size_t ielen;

	if (ni->ni_vap->iv_opmode == IEEE80211_M_HOSTAP &&
	    ni->ni_associd == 0)	/* only associated stations */
		return;
	req->space += sta_space(ni, &ielen);
}

static void
get_sta_info(void *arg, struct ieee80211_node *ni)
{
	struct stainforeq *req = arg;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211req_sta_info *si;
	size_t ielen, len;
	uint8_t *cp;

	if (vap->iv_opmode == IEEE80211_M_HOSTAP &&
	    ni->ni_associd == 0)	/* only associated stations */
		return;
	if (ni->ni_chan == IEEE80211_CHAN_ANYC)	/* XXX bogus entry */
		return;
	len = sta_space(ni, &ielen);
	if (len > req->space)
		return;
	si = req->si;
	si->isi_len = len;
	si->isi_ie_off = sizeof(struct ieee80211req_sta_info);
	si->isi_ie_len = ielen;
	si->isi_freq = ni->ni_chan->ic_freq;
	si->isi_flags = ni->ni_chan->ic_flags;
	si->isi_state = ni->ni_flags;
	si->isi_authmode = ni->ni_authmode;
	vap->iv_ic->ic_node_getsignal(ni, &si->isi_rssi, &si->isi_noise);
	vap->iv_ic->ic_node_getmimoinfo(ni, &si->isi_mimo);
	si->isi_capinfo = ni->ni_capinfo;
	si->isi_erp = ni->ni_erp;
	IEEE80211_ADDR_COPY(si->isi_macaddr, ni->ni_macaddr);
	si->isi_nrates = ni->ni_rates.rs_nrates;
	if (si->isi_nrates > 15)
		si->isi_nrates = 15;
	memcpy(si->isi_rates, ni->ni_rates.rs_rates, si->isi_nrates);
	si->isi_txrate = ni->ni_txrate;
	if (si->isi_txrate & IEEE80211_RATE_MCS) {
		const struct ieee80211_mcs_rates *mcs =
		    &ieee80211_htrates[ni->ni_txrate &~ IEEE80211_RATE_MCS];
		if (IEEE80211_IS_CHAN_HT40(ni->ni_chan)) {
			if (ni->ni_flags & IEEE80211_NODE_SGI40)
				si->isi_txmbps = mcs->ht40_rate_800ns;
			else
				si->isi_txmbps = mcs->ht40_rate_400ns;
		} else {
			if (ni->ni_flags & IEEE80211_NODE_SGI20)
				si->isi_txmbps = mcs->ht20_rate_800ns;
			else
				si->isi_txmbps = mcs->ht20_rate_400ns;
		}
	} else
		si->isi_txmbps = si->isi_txrate;
	si->isi_associd = ni->ni_associd;
	si->isi_txpower = ni->ni_txpower;
	si->isi_vlan = ni->ni_vlan;
	if (ni->ni_flags & IEEE80211_NODE_QOS) {
		memcpy(si->isi_txseqs, ni->ni_txseqs, sizeof(ni->ni_txseqs));
		memcpy(si->isi_rxseqs, ni->ni_rxseqs, sizeof(ni->ni_rxseqs));
	} else {
		si->isi_txseqs[0] = ni->ni_txseqs[IEEE80211_NONQOS_TID];
		si->isi_rxseqs[0] = ni->ni_rxseqs[IEEE80211_NONQOS_TID];
	}
	/* NB: leave all cases in case we relax ni_associd == 0 check */
	if (ieee80211_node_is_authorized(ni))
		si->isi_inact = vap->iv_inact_run;
	else if (ni->ni_associd != 0 ||
	    (vap->iv_opmode == IEEE80211_M_WDS &&
	     (vap->iv_flags_ext & IEEE80211_FEXT_WDSLEGACY)))
		si->isi_inact = vap->iv_inact_auth;
	else
		si->isi_inact = vap->iv_inact_init;
	si->isi_inact = (si->isi_inact - ni->ni_inact) * IEEE80211_INACT_WAIT;
	si->isi_localid = ni->ni_mllid;
	si->isi_peerid = ni->ni_mlpid;
	si->isi_peerstate = ni->ni_mlstate;

	if (ielen) {
		cp = ((uint8_t *)si) + si->isi_ie_off;
		memcpy(cp, ni->ni_ies.data, ielen);
	}

	req->si = (struct ieee80211req_sta_info *)(((uint8_t *)si) + len);
	req->space -= len;
}

static int
getstainfo_common(struct ieee80211vap *vap, struct ieee80211req *ireq,
	struct ieee80211_node *ni, size_t off)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct stainforeq req;
	size_t space;
	void *p;
	int error;

	error = 0;
	req.space = 0;
	if (ni == NULL) {
		ieee80211_iterate_nodes_vap(&ic->ic_sta, vap, get_sta_space,
		    &req);
	} else
		get_sta_space(&req, ni);
	if (req.space > ireq->i_len)
		req.space = ireq->i_len;
	if (req.space > 0) {
		space = req.space;
		/* XXX M_WAITOK after driver lock released */
		p = IEEE80211_MALLOC(space, M_TEMP,
		    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
		if (p == NULL) {
			error = ENOMEM;
			goto bad;
		}
		req.si = p;
		if (ni == NULL) {
			ieee80211_iterate_nodes_vap(&ic->ic_sta, vap,
			    get_sta_info, &req);
		} else
			get_sta_info(&req, ni);
		ireq->i_len = space - req.space;
		error = copyout(p, (uint8_t *) ireq->i_data+off, ireq->i_len);
		IEEE80211_FREE(p, M_TEMP);
	} else
		ireq->i_len = 0;
bad:
	if (ni != NULL)
		ieee80211_free_node(ni);
	return error;
}

static int
ieee80211_ioctl_getstainfo(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	uint8_t macaddr[IEEE80211_ADDR_LEN];
	const size_t off = __offsetof(struct ieee80211req_sta_req, info);
	struct ieee80211_node *ni;
	int error;

	if (ireq->i_len < sizeof(struct ieee80211req_sta_req))
		return EFAULT;
	error = copyin(ireq->i_data, macaddr, IEEE80211_ADDR_LEN);
	if (error != 0)
		return error;
	if (IEEE80211_ADDR_EQ(macaddr, vap->iv_ifp->if_broadcastaddr)) {
		ni = NULL;
	} else {
		ni = ieee80211_find_vap_node(&vap->iv_ic->ic_sta, vap, macaddr);
		if (ni == NULL)
			return ENOENT;
	}
	return getstainfo_common(vap, ireq, ni, off);
}

static int
ieee80211_ioctl_getstatxpow(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_node *ni;
	struct ieee80211req_sta_txpow txpow;
	int error;

	if (ireq->i_len != sizeof(txpow))
		return EINVAL;
	error = copyin(ireq->i_data, &txpow, sizeof(txpow));
	if (error != 0)
		return error;
	ni = ieee80211_find_vap_node(&vap->iv_ic->ic_sta, vap, txpow.it_macaddr);
	if (ni == NULL)
		return ENOENT;
	txpow.it_txpow = ni->ni_txpower;
	error = copyout(&txpow, ireq->i_data, sizeof(txpow));
	ieee80211_free_node(ni);
	return error;
}

static int
ieee80211_ioctl_getwmeparam(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_wme_state *wme = &ic->ic_wme;
	struct wmeParams *wmep;
	int ac;

	if ((ic->ic_caps & IEEE80211_C_WME) == 0)
		return EINVAL;

	ac = (ireq->i_len & IEEE80211_WMEPARAM_VAL);
	if (ac >= WME_NUM_AC)
		ac = WME_AC_BE;
	if (ireq->i_len & IEEE80211_WMEPARAM_BSS)
		wmep = &wme->wme_wmeBssChanParams.cap_wmeParams[ac];
	else
		wmep = &wme->wme_wmeChanParams.cap_wmeParams[ac];
	switch (ireq->i_type) {
	case IEEE80211_IOC_WME_CWMIN:		/* WME: CWmin */
		ireq->i_val = wmep->wmep_logcwmin;
		break;
	case IEEE80211_IOC_WME_CWMAX:		/* WME: CWmax */
		ireq->i_val = wmep->wmep_logcwmax;
		break;
	case IEEE80211_IOC_WME_AIFS:		/* WME: AIFS */
		ireq->i_val = wmep->wmep_aifsn;
		break;
	case IEEE80211_IOC_WME_TXOPLIMIT:	/* WME: txops limit */
		ireq->i_val = wmep->wmep_txopLimit;
		break;
	case IEEE80211_IOC_WME_ACM:		/* WME: ACM (bss only) */
		wmep = &wme->wme_wmeBssChanParams.cap_wmeParams[ac];
		ireq->i_val = wmep->wmep_acm;
		break;
	case IEEE80211_IOC_WME_ACKPOLICY:	/* WME: ACK policy (!bss only)*/
		wmep = &wme->wme_wmeChanParams.cap_wmeParams[ac];
		ireq->i_val = !wmep->wmep_noackPolicy;
		break;
	}
	return 0;
}

static int
ieee80211_ioctl_getmaccmd(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	const struct ieee80211_aclator *acl = vap->iv_acl;

	return (acl == NULL ? EINVAL : acl->iac_getioctl(vap, ireq));
}

static int
ieee80211_ioctl_getcurchan(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_channel *c;

	if (ireq->i_len != sizeof(struct ieee80211_channel))
		return EINVAL;
	/*
	 * vap's may have different operating channels when HT is
	 * in use.  When in RUN state report the vap-specific channel.
	 * Otherwise return curchan.
	 */
	if (vap->iv_state == IEEE80211_S_RUN || vap->iv_state == IEEE80211_S_SLEEP)
		c = vap->iv_bss->ni_chan;
	else
		c = ic->ic_curchan;
	return copyout(c, ireq->i_data, sizeof(*c));
}

static int
getappie(const struct ieee80211_appie *aie, struct ieee80211req *ireq)
{
	if (aie == NULL)
		return EINVAL;
	/* NB: truncate, caller can check length */
	if (ireq->i_len > aie->ie_len)
		ireq->i_len = aie->ie_len;
	return copyout(aie->ie_data, ireq->i_data, ireq->i_len);
}

static int
ieee80211_ioctl_getappie(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	uint8_t fc0;

	fc0 = ireq->i_val & 0xff;
	if ((fc0 & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_MGT)
		return EINVAL;
	/* NB: could check iv_opmode and reject but hardly worth the effort */
	switch (fc0 & IEEE80211_FC0_SUBTYPE_MASK) {
	case IEEE80211_FC0_SUBTYPE_BEACON:
		return getappie(vap->iv_appie_beacon, ireq);
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
		return getappie(vap->iv_appie_proberesp, ireq);
	case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
		return getappie(vap->iv_appie_assocresp, ireq);
	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		return getappie(vap->iv_appie_probereq, ireq);
	case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
		return getappie(vap->iv_appie_assocreq, ireq);
	case IEEE80211_FC0_SUBTYPE_BEACON|IEEE80211_FC0_SUBTYPE_PROBE_RESP:
		return getappie(vap->iv_appie_wpa, ireq);
	}
	return EINVAL;
}

static int
ieee80211_ioctl_getregdomain(struct ieee80211vap *vap,
	const struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;

	if (ireq->i_len != sizeof(ic->ic_regdomain))
		return EINVAL;
	return copyout(&ic->ic_regdomain, ireq->i_data,
	    sizeof(ic->ic_regdomain));
}

static int
ieee80211_ioctl_getroam(struct ieee80211vap *vap,
	const struct ieee80211req *ireq)
{
	size_t len = ireq->i_len;
	/* NB: accept short requests for backwards compat */
	if (len > sizeof(vap->iv_roamparms))
		len = sizeof(vap->iv_roamparms);
	return copyout(vap->iv_roamparms, ireq->i_data, len);
}

static int
ieee80211_ioctl_gettxparams(struct ieee80211vap *vap,
	const struct ieee80211req *ireq)
{
	size_t len = ireq->i_len;
	/* NB: accept short requests for backwards compat */
	if (len > sizeof(vap->iv_txparms))
		len = sizeof(vap->iv_txparms);
	return copyout(vap->iv_txparms, ireq->i_data, len);
}

static int
ieee80211_ioctl_getdevcaps(struct ieee80211com *ic,
	const struct ieee80211req *ireq)
{
	struct ieee80211_devcaps_req *dc;
	struct ieee80211req_chaninfo *ci;
	int maxchans, error;

	maxchans = 1 + ((ireq->i_len - sizeof(struct ieee80211_devcaps_req)) /
	    sizeof(struct ieee80211_channel));
	/* NB: require 1 so we know ic_nchans is accessible */
	if (maxchans < 1)
		return EINVAL;
	/* constrain max request size, 2K channels is ~24Kbytes */
	if (maxchans > 2048)
		maxchans = 2048;
	dc = (struct ieee80211_devcaps_req *)
	    IEEE80211_MALLOC(IEEE80211_DEVCAPS_SIZE(maxchans), M_TEMP,
	    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (dc == NULL)
		return ENOMEM;
	dc->dc_drivercaps = ic->ic_caps;
	dc->dc_cryptocaps = ic->ic_cryptocaps;
	dc->dc_htcaps = ic->ic_htcaps;
	dc->dc_vhtcaps = ic->ic_vhtcaps;
	ci = &dc->dc_chaninfo;
	ic->ic_getradiocaps(ic, maxchans, &ci->ic_nchans, ci->ic_chans);
	KASSERT(ci->ic_nchans <= maxchans,
	    ("nchans %d maxchans %d", ci->ic_nchans, maxchans));
	ieee80211_sort_channels(ci->ic_chans, ci->ic_nchans);
	error = copyout(dc, ireq->i_data, IEEE80211_DEVCAPS_SPACE(dc));
	IEEE80211_FREE(dc, M_TEMP);
	return error;
}

static int
ieee80211_ioctl_getstavlan(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_node *ni;
	struct ieee80211req_sta_vlan vlan;
	int error;

	if (ireq->i_len != sizeof(vlan))
		return EINVAL;
	error = copyin(ireq->i_data, &vlan, sizeof(vlan));
	if (error != 0)
		return error;
	if (!IEEE80211_ADDR_EQ(vlan.sv_macaddr, zerobssid)) {
		ni = ieee80211_find_vap_node(&vap->iv_ic->ic_sta, vap,
		    vlan.sv_macaddr);
		if (ni == NULL)
			return ENOENT;
	} else
		ni = ieee80211_ref_node(vap->iv_bss);
	vlan.sv_vlan = ni->ni_vlan;
	error = copyout(&vlan, ireq->i_data, sizeof(vlan));
	ieee80211_free_node(ni);
	return error;
}

/*
 * Dummy ioctl get handler so the linker set is defined.
 */
static int
dummy_ioctl_get(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	return ENOSYS;
}
IEEE80211_IOCTL_GET(dummy, dummy_ioctl_get);

static int
ieee80211_ioctl_getdefault(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	ieee80211_ioctl_getfunc * const *get;
	int error;

	SET_FOREACH(get, ieee80211_ioctl_getset) {
		error = (*get)(vap, ireq);
		if (error != ENOSYS)
			return error;
	}
	return EINVAL;
}

static int
ieee80211_ioctl_get80211(struct ieee80211vap *vap, u_long cmd,
    struct ieee80211req *ireq)
{
#define	MS(_v, _f)	(((_v) & _f) >> _f##_S)
	struct ieee80211com *ic = vap->iv_ic;
	u_int kid, len;
	uint8_t tmpkey[IEEE80211_KEYBUF_SIZE];
	char tmpssid[IEEE80211_NWID_LEN];
	int error = 0;

	switch (ireq->i_type) {
	case IEEE80211_IOC_SSID:
		switch (vap->iv_state) {
		case IEEE80211_S_INIT:
		case IEEE80211_S_SCAN:
			ireq->i_len = vap->iv_des_ssid[0].len;
			memcpy(tmpssid, vap->iv_des_ssid[0].ssid, ireq->i_len);
			break;
		default:
			ireq->i_len = vap->iv_bss->ni_esslen;
			memcpy(tmpssid, vap->iv_bss->ni_essid, ireq->i_len);
			break;
		}
		error = copyout(tmpssid, ireq->i_data, ireq->i_len);
		break;
	case IEEE80211_IOC_NUMSSIDS:
		ireq->i_val = 1;
		break;
	case IEEE80211_IOC_WEP:
		if ((vap->iv_flags & IEEE80211_F_PRIVACY) == 0)
			ireq->i_val = IEEE80211_WEP_OFF;
		else if (vap->iv_flags & IEEE80211_F_DROPUNENC)
			ireq->i_val = IEEE80211_WEP_ON;
		else
			ireq->i_val = IEEE80211_WEP_MIXED;
		break;
	case IEEE80211_IOC_WEPKEY:
		kid = (u_int) ireq->i_val;
		if (kid >= IEEE80211_WEP_NKID)
			return EINVAL;
		len = (u_int) vap->iv_nw_keys[kid].wk_keylen;
		/* NB: only root can read WEP keys */
		if (priv_check(curthread, PRIV_NET80211_GETKEY) == 0) {
			bcopy(vap->iv_nw_keys[kid].wk_key, tmpkey, len);
		} else {
			bzero(tmpkey, len);
		}
		ireq->i_len = len;
		error = copyout(tmpkey, ireq->i_data, len);
		break;
	case IEEE80211_IOC_NUMWEPKEYS:
		ireq->i_val = IEEE80211_WEP_NKID;
		break;
	case IEEE80211_IOC_WEPTXKEY:
		ireq->i_val = vap->iv_def_txkey;
		break;
	case IEEE80211_IOC_AUTHMODE:
		if (vap->iv_flags & IEEE80211_F_WPA)
			ireq->i_val = IEEE80211_AUTH_WPA;
		else
			ireq->i_val = vap->iv_bss->ni_authmode;
		break;
	case IEEE80211_IOC_CHANNEL:
		ireq->i_val = ieee80211_chan2ieee(ic, ic->ic_curchan);
		break;
	case IEEE80211_IOC_POWERSAVE:
		if (vap->iv_flags & IEEE80211_F_PMGTON)
			ireq->i_val = IEEE80211_POWERSAVE_ON;
		else
			ireq->i_val = IEEE80211_POWERSAVE_OFF;
		break;
	case IEEE80211_IOC_POWERSAVESLEEP:
		ireq->i_val = ic->ic_lintval;
		break;
	case IEEE80211_IOC_RTSTHRESHOLD:
		ireq->i_val = vap->iv_rtsthreshold;
		break;
	case IEEE80211_IOC_PROTMODE:
		ireq->i_val = ic->ic_protmode;
		break;
	case IEEE80211_IOC_TXPOWER:
		/*
		 * Tx power limit is the min of max regulatory
		 * power, any user-set limit, and the max the
		 * radio can do.
		 *
		 * TODO: methodize this
		 */
		ireq->i_val = 2*ic->ic_curchan->ic_maxregpower;
		if (ireq->i_val > ic->ic_txpowlimit)
			ireq->i_val = ic->ic_txpowlimit;
		if (ireq->i_val > ic->ic_curchan->ic_maxpower)
			ireq->i_val = ic->ic_curchan->ic_maxpower;
		break;
	case IEEE80211_IOC_WPA:
		switch (vap->iv_flags & IEEE80211_F_WPA) {
		case IEEE80211_F_WPA1:
			ireq->i_val = 1;
			break;
		case IEEE80211_F_WPA2:
			ireq->i_val = 2;
			break;
		case IEEE80211_F_WPA1 | IEEE80211_F_WPA2:
			ireq->i_val = 3;
			break;
		default:
			ireq->i_val = 0;
			break;
		}
		break;
	case IEEE80211_IOC_CHANLIST:
		error = ieee80211_ioctl_getchanlist(vap, ireq);
		break;
	case IEEE80211_IOC_ROAMING:
		ireq->i_val = vap->iv_roaming;
		break;
	case IEEE80211_IOC_PRIVACY:
		ireq->i_val = (vap->iv_flags & IEEE80211_F_PRIVACY) != 0;
		break;
	case IEEE80211_IOC_DROPUNENCRYPTED:
		ireq->i_val = (vap->iv_flags & IEEE80211_F_DROPUNENC) != 0;
		break;
	case IEEE80211_IOC_COUNTERMEASURES:
		ireq->i_val = (vap->iv_flags & IEEE80211_F_COUNTERM) != 0;
		break;
	case IEEE80211_IOC_WME:
		ireq->i_val = (vap->iv_flags & IEEE80211_F_WME) != 0;
		break;
	case IEEE80211_IOC_HIDESSID:
		ireq->i_val = (vap->iv_flags & IEEE80211_F_HIDESSID) != 0;
		break;
	case IEEE80211_IOC_APBRIDGE:
		ireq->i_val = (vap->iv_flags & IEEE80211_F_NOBRIDGE) == 0;
		break;
	case IEEE80211_IOC_WPAKEY:
		error = ieee80211_ioctl_getkey(vap, ireq);
		break;
	case IEEE80211_IOC_CHANINFO:
		error = ieee80211_ioctl_getchaninfo(vap, ireq);
		break;
	case IEEE80211_IOC_BSSID:
		if (ireq->i_len != IEEE80211_ADDR_LEN)
			return EINVAL;
		if (vap->iv_state == IEEE80211_S_RUN || vap->iv_state == IEEE80211_S_SLEEP) {
			error = copyout(vap->iv_opmode == IEEE80211_M_WDS ?
			    vap->iv_bss->ni_macaddr : vap->iv_bss->ni_bssid,
			    ireq->i_data, ireq->i_len);
		} else
			error = copyout(vap->iv_des_bssid, ireq->i_data,
			    ireq->i_len);
		break;
	case IEEE80211_IOC_WPAIE:
	case IEEE80211_IOC_WPAIE2:
		error = ieee80211_ioctl_getwpaie(vap, ireq, ireq->i_type);
		break;
	case IEEE80211_IOC_SCAN_RESULTS:
		error = ieee80211_ioctl_getscanresults(vap, ireq);
		break;
	case IEEE80211_IOC_STA_STATS:
		error = ieee80211_ioctl_getstastats(vap, ireq);
		break;
	case IEEE80211_IOC_TXPOWMAX:
		ireq->i_val = vap->iv_bss->ni_txpower;
		break;
	case IEEE80211_IOC_STA_TXPOW:
		error = ieee80211_ioctl_getstatxpow(vap, ireq);
		break;
	case IEEE80211_IOC_STA_INFO:
		error = ieee80211_ioctl_getstainfo(vap, ireq);
		break;
	case IEEE80211_IOC_WME_CWMIN:		/* WME: CWmin */
	case IEEE80211_IOC_WME_CWMAX:		/* WME: CWmax */
	case IEEE80211_IOC_WME_AIFS:		/* WME: AIFS */
	case IEEE80211_IOC_WME_TXOPLIMIT:	/* WME: txops limit */
	case IEEE80211_IOC_WME_ACM:		/* WME: ACM (bss only) */
	case IEEE80211_IOC_WME_ACKPOLICY:	/* WME: ACK policy (!bss only) */
		error = ieee80211_ioctl_getwmeparam(vap, ireq);
		break;
	case IEEE80211_IOC_DTIM_PERIOD:
		ireq->i_val = vap->iv_dtim_period;
		break;
	case IEEE80211_IOC_BEACON_INTERVAL:
		/* NB: get from ic_bss for station mode */
		ireq->i_val = vap->iv_bss->ni_intval;
		break;
	case IEEE80211_IOC_PUREG:
		ireq->i_val = (vap->iv_flags & IEEE80211_F_PUREG) != 0;
		break;
	case IEEE80211_IOC_QUIET:
		ireq->i_val = vap->iv_quiet;
		break;
	case IEEE80211_IOC_QUIET_COUNT:
		ireq->i_val = vap->iv_quiet_count;
		break;
	case IEEE80211_IOC_QUIET_PERIOD:
		ireq->i_val = vap->iv_quiet_period;
		break;
	case IEEE80211_IOC_QUIET_DUR:
		ireq->i_val = vap->iv_quiet_duration;
		break;
	case IEEE80211_IOC_QUIET_OFFSET:
		ireq->i_val = vap->iv_quiet_offset;
		break;
	case IEEE80211_IOC_BGSCAN:
		ireq->i_val = (vap->iv_flags & IEEE80211_F_BGSCAN) != 0;
		break;
	case IEEE80211_IOC_BGSCAN_IDLE:
		ireq->i_val = vap->iv_bgscanidle*hz/1000;	/* ms */
		break;
	case IEEE80211_IOC_BGSCAN_INTERVAL:
		ireq->i_val = vap->iv_bgscanintvl/hz;		/* seconds */
		break;
	case IEEE80211_IOC_SCANVALID:
		ireq->i_val = vap->iv_scanvalid/hz;		/* seconds */
		break;
	case IEEE80211_IOC_FRAGTHRESHOLD:
		ireq->i_val = vap->iv_fragthreshold;
		break;
	case IEEE80211_IOC_MACCMD:
		error = ieee80211_ioctl_getmaccmd(vap, ireq);
		break;
	case IEEE80211_IOC_BURST:
		ireq->i_val = (vap->iv_flags & IEEE80211_F_BURST) != 0;
		break;
	case IEEE80211_IOC_BMISSTHRESHOLD:
		ireq->i_val = vap->iv_bmissthreshold;
		break;
	case IEEE80211_IOC_CURCHAN:
		error = ieee80211_ioctl_getcurchan(vap, ireq);
		break;
	case IEEE80211_IOC_SHORTGI:
		ireq->i_val = 0;
		if (vap->iv_flags_ht & IEEE80211_FHT_SHORTGI20)
			ireq->i_val |= IEEE80211_HTCAP_SHORTGI20;
		if (vap->iv_flags_ht & IEEE80211_FHT_SHORTGI40)
			ireq->i_val |= IEEE80211_HTCAP_SHORTGI40;
		break;
	case IEEE80211_IOC_AMPDU:
		ireq->i_val = 0;
		if (vap->iv_flags_ht & IEEE80211_FHT_AMPDU_TX)
			ireq->i_val |= 1;
		if (vap->iv_flags_ht & IEEE80211_FHT_AMPDU_RX)
			ireq->i_val |= 2;
		break;
	case IEEE80211_IOC_AMPDU_LIMIT:
		/* XXX TODO: make this a per-node thing; and leave this as global */
		if (vap->iv_opmode == IEEE80211_M_HOSTAP)
			ireq->i_val = vap->iv_ampdu_rxmax;
		else if (vap->iv_state == IEEE80211_S_RUN || vap->iv_state == IEEE80211_S_SLEEP)
			/*
			 * XXX TODO: this isn't completely correct, as we've
			 * negotiated the higher of the two.
			 */
			ireq->i_val = MS(vap->iv_bss->ni_htparam,
			    IEEE80211_HTCAP_MAXRXAMPDU);
		else
			ireq->i_val = vap->iv_ampdu_limit;
		break;
	case IEEE80211_IOC_AMPDU_DENSITY:
		/* XXX TODO: make this a per-node thing; and leave this as global */
		if (vap->iv_opmode == IEEE80211_M_STA &&
		    (vap->iv_state == IEEE80211_S_RUN || vap->iv_state == IEEE80211_S_SLEEP))
			/*
			 * XXX TODO: this isn't completely correct, as we've
			 * negotiated the higher of the two.
			 */
			ireq->i_val = MS(vap->iv_bss->ni_htparam,
			    IEEE80211_HTCAP_MPDUDENSITY);
		else
			ireq->i_val = vap->iv_ampdu_density;
		break;
	case IEEE80211_IOC_AMSDU:
		ireq->i_val = 0;
		if (vap->iv_flags_ht & IEEE80211_FHT_AMSDU_TX)
			ireq->i_val |= 1;
		if (vap->iv_flags_ht & IEEE80211_FHT_AMSDU_RX)
			ireq->i_val |= 2;
		break;
	case IEEE80211_IOC_AMSDU_LIMIT:
		ireq->i_val = vap->iv_amsdu_limit;	/* XXX truncation? */
		break;
	case IEEE80211_IOC_PUREN:
		ireq->i_val = (vap->iv_flags_ht & IEEE80211_FHT_PUREN) != 0;
		break;
	case IEEE80211_IOC_DOTH:
		ireq->i_val = (vap->iv_flags & IEEE80211_F_DOTH) != 0;
		break;
	case IEEE80211_IOC_REGDOMAIN:
		error = ieee80211_ioctl_getregdomain(vap, ireq);
		break;
	case IEEE80211_IOC_ROAM:
		error = ieee80211_ioctl_getroam(vap, ireq);
		break;
	case IEEE80211_IOC_TXPARAMS:
		error = ieee80211_ioctl_gettxparams(vap, ireq);
		break;
	case IEEE80211_IOC_HTCOMPAT:
		ireq->i_val = (vap->iv_flags_ht & IEEE80211_FHT_HTCOMPAT) != 0;
		break;
	case IEEE80211_IOC_DWDS:
		ireq->i_val = (vap->iv_flags & IEEE80211_F_DWDS) != 0;
		break;
	case IEEE80211_IOC_INACTIVITY:
		ireq->i_val = (vap->iv_flags_ext & IEEE80211_FEXT_INACT) != 0;
		break;
	case IEEE80211_IOC_APPIE:
		error = ieee80211_ioctl_getappie(vap, ireq);
		break;
	case IEEE80211_IOC_WPS:
		ireq->i_val = (vap->iv_flags_ext & IEEE80211_FEXT_WPS) != 0;
		break;
	case IEEE80211_IOC_TSN:
		ireq->i_val = (vap->iv_flags_ext & IEEE80211_FEXT_TSN) != 0;
		break;
	case IEEE80211_IOC_DFS:
		ireq->i_val = (vap->iv_flags_ext & IEEE80211_FEXT_DFS) != 0;
		break;
	case IEEE80211_IOC_DOTD:
		ireq->i_val = (vap->iv_flags_ext & IEEE80211_FEXT_DOTD) != 0;
		break;
	case IEEE80211_IOC_DEVCAPS:
		error = ieee80211_ioctl_getdevcaps(ic, ireq);
		break;
	case IEEE80211_IOC_HTPROTMODE:
		ireq->i_val = ic->ic_htprotmode;
		break;
	case IEEE80211_IOC_HTCONF:
		if (vap->iv_flags_ht & IEEE80211_FHT_HT) {
			ireq->i_val = 1;
			if (vap->iv_flags_ht & IEEE80211_FHT_USEHT40)
				ireq->i_val |= 2;
		} else
			ireq->i_val = 0;
		break;
	case IEEE80211_IOC_STA_VLAN:
		error = ieee80211_ioctl_getstavlan(vap, ireq);
		break;
	case IEEE80211_IOC_SMPS:
		if (vap->iv_opmode == IEEE80211_M_STA &&
		    (vap->iv_state == IEEE80211_S_RUN || vap->iv_state == IEEE80211_S_SLEEP)) {
			if (vap->iv_bss->ni_flags & IEEE80211_NODE_MIMO_RTS)
				ireq->i_val = IEEE80211_HTCAP_SMPS_DYNAMIC;
			else if (vap->iv_bss->ni_flags & IEEE80211_NODE_MIMO_PS)
				ireq->i_val = IEEE80211_HTCAP_SMPS_ENA;
			else
				ireq->i_val = IEEE80211_HTCAP_SMPS_OFF;
		} else
			ireq->i_val = vap->iv_htcaps & IEEE80211_HTCAP_SMPS;
		break;
	case IEEE80211_IOC_RIFS:
		if (vap->iv_opmode == IEEE80211_M_STA &&
		    (vap->iv_state == IEEE80211_S_RUN || vap->iv_state == IEEE80211_S_SLEEP))
			ireq->i_val =
			    (vap->iv_bss->ni_flags & IEEE80211_NODE_RIFS) != 0;
		else
			ireq->i_val =
			    (vap->iv_flags_ht & IEEE80211_FHT_RIFS) != 0;
		break;
	case IEEE80211_IOC_STBC:
		ireq->i_val = 0;
		if (vap->iv_flags_ht & IEEE80211_FHT_STBC_TX)
			ireq->i_val |= 1;
		if (vap->iv_flags_ht & IEEE80211_FHT_STBC_RX)
			ireq->i_val |= 2;
		break;
	case IEEE80211_IOC_LDPC:
		ireq->i_val = 0;
		if (vap->iv_flags_ht & IEEE80211_FHT_LDPC_TX)
			ireq->i_val |= 1;
		if (vap->iv_flags_ht & IEEE80211_FHT_LDPC_RX)
			ireq->i_val |= 2;
		break;

	/* VHT */
	case IEEE80211_IOC_VHTCONF:
		ireq->i_val = 0;
		if (vap->iv_flags_vht & IEEE80211_FVHT_VHT)
			ireq->i_val |= 1;
		if (vap->iv_flags_vht & IEEE80211_FVHT_USEVHT40)
			ireq->i_val |= 2;
		if (vap->iv_flags_vht & IEEE80211_FVHT_USEVHT80)
			ireq->i_val |= 4;
		if (vap->iv_flags_vht & IEEE80211_FVHT_USEVHT80P80)
			ireq->i_val |= 8;
		if (vap->iv_flags_vht & IEEE80211_FVHT_USEVHT160)
			ireq->i_val |= 16;
		break;

	default:
		error = ieee80211_ioctl_getdefault(vap, ireq);
		break;
	}
	return error;
#undef MS
}

static int
ieee80211_ioctl_setkey(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211req_key ik;
	struct ieee80211_node *ni;
	struct ieee80211_key *wk;
	uint16_t kid;
	int error, i;

	if (ireq->i_len != sizeof(ik))
		return EINVAL;
	error = copyin(ireq->i_data, &ik, sizeof(ik));
	if (error)
		return error;
	/* NB: cipher support is verified by ieee80211_crypt_newkey */
	/* NB: this also checks ik->ik_keylen > sizeof(wk->wk_key) */
	if (ik.ik_keylen > sizeof(ik.ik_keydata))
		return E2BIG;
	kid = ik.ik_keyix;
	if (kid == IEEE80211_KEYIX_NONE) {
		/* XXX unicast keys currently must be tx/rx */
		if (ik.ik_flags != (IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV))
			return EINVAL;
		if (vap->iv_opmode == IEEE80211_M_STA) {
			ni = ieee80211_ref_node(vap->iv_bss);
			if (!IEEE80211_ADDR_EQ(ik.ik_macaddr, ni->ni_bssid)) {
				ieee80211_free_node(ni);
				return EADDRNOTAVAIL;
			}
		} else {
			ni = ieee80211_find_vap_node(&vap->iv_ic->ic_sta, vap,
				ik.ik_macaddr);
			if (ni == NULL)
				return ENOENT;
		}
		wk = &ni->ni_ucastkey;
	} else {
		if (kid >= IEEE80211_WEP_NKID)
			return EINVAL;
		wk = &vap->iv_nw_keys[kid];
		/*
		 * Global slots start off w/o any assigned key index.
		 * Force one here for consistency with IEEE80211_IOC_WEPKEY.
		 */
		if (wk->wk_keyix == IEEE80211_KEYIX_NONE)
			wk->wk_keyix = kid;
		ni = NULL;
	}
	error = 0;
	ieee80211_key_update_begin(vap);
	if (ieee80211_crypto_newkey(vap, ik.ik_type, ik.ik_flags, wk)) {
		wk->wk_keylen = ik.ik_keylen;
		/* NB: MIC presence is implied by cipher type */
		if (wk->wk_keylen > IEEE80211_KEYBUF_SIZE)
			wk->wk_keylen = IEEE80211_KEYBUF_SIZE;
		for (i = 0; i < IEEE80211_TID_SIZE; i++)
			wk->wk_keyrsc[i] = ik.ik_keyrsc;
		wk->wk_keytsc = 0;			/* new key, reset */
		memset(wk->wk_key, 0, sizeof(wk->wk_key));
		memcpy(wk->wk_key, ik.ik_keydata, ik.ik_keylen);
		IEEE80211_ADDR_COPY(wk->wk_macaddr,
		    ni != NULL ?  ni->ni_macaddr : ik.ik_macaddr);
		if (!ieee80211_crypto_setkey(vap, wk))
			error = EIO;
		else if ((ik.ik_flags & IEEE80211_KEY_DEFAULT))
			/*
			 * Inform the driver that this is the default
			 * transmit key.  Now, ideally we'd just set
			 * a flag in the key update that would
			 * say "yes, we're the default key", but
			 * that currently isn't the way the ioctl ->
			 * key interface works.
			 */
			ieee80211_crypto_set_deftxkey(vap, kid);
	} else
		error = ENXIO;
	ieee80211_key_update_end(vap);
	if (ni != NULL)
		ieee80211_free_node(ni);
	return error;
}

static int
ieee80211_ioctl_delkey(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211req_del_key dk;
	int kid, error;

	if (ireq->i_len != sizeof(dk))
		return EINVAL;
	error = copyin(ireq->i_data, &dk, sizeof(dk));
	if (error)
		return error;
	kid = dk.idk_keyix;
	/* XXX uint8_t -> uint16_t */
	if (dk.idk_keyix == (uint8_t) IEEE80211_KEYIX_NONE) {
		struct ieee80211_node *ni;

		if (vap->iv_opmode == IEEE80211_M_STA) {
			ni = ieee80211_ref_node(vap->iv_bss);
			if (!IEEE80211_ADDR_EQ(dk.idk_macaddr, ni->ni_bssid)) {
				ieee80211_free_node(ni);
				return EADDRNOTAVAIL;
			}
		} else {
			ni = ieee80211_find_vap_node(&vap->iv_ic->ic_sta, vap,
				dk.idk_macaddr);
			if (ni == NULL)
				return ENOENT;
		}
		/* XXX error return */
		ieee80211_node_delucastkey(ni);
		ieee80211_free_node(ni);
	} else {
		if (kid >= IEEE80211_WEP_NKID)
			return EINVAL;
		/* XXX error return */
		ieee80211_crypto_delkey(vap, &vap->iv_nw_keys[kid]);
	}
	return 0;
}

struct mlmeop {
	struct ieee80211vap *vap;
	int	op;
	int	reason;
};

static void
mlmedebug(struct ieee80211vap *vap, const uint8_t mac[IEEE80211_ADDR_LEN],
	int op, int reason)
{
#ifdef IEEE80211_DEBUG
	static const struct {
		int mask;
		const char *opstr;
	} ops[] = {
		{ 0, "op#0" },
		{ IEEE80211_MSG_IOCTL | IEEE80211_MSG_STATE |
		  IEEE80211_MSG_ASSOC, "assoc" },
		{ IEEE80211_MSG_IOCTL | IEEE80211_MSG_STATE |
		  IEEE80211_MSG_ASSOC, "disassoc" },
		{ IEEE80211_MSG_IOCTL | IEEE80211_MSG_STATE |
		  IEEE80211_MSG_AUTH, "deauth" },
		{ IEEE80211_MSG_IOCTL | IEEE80211_MSG_STATE |
		  IEEE80211_MSG_AUTH, "authorize" },
		{ IEEE80211_MSG_IOCTL | IEEE80211_MSG_STATE |
		  IEEE80211_MSG_AUTH, "unauthorize" },
	};

	if (op == IEEE80211_MLME_AUTH) {
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_IOCTL |
		    IEEE80211_MSG_STATE | IEEE80211_MSG_AUTH, mac,
		    "station authenticate %s via MLME (reason: %d (%s))",
		    reason == IEEE80211_STATUS_SUCCESS ? "ACCEPT" : "REJECT",
		    reason, ieee80211_reason_to_string(reason));
	} else if (!(IEEE80211_MLME_ASSOC <= op && op <= IEEE80211_MLME_AUTH)) {
		IEEE80211_NOTE_MAC(vap, IEEE80211_MSG_ANY, mac,
		    "unknown MLME request %d (reason: %d (%s))", op, reason,
		    ieee80211_reason_to_string(reason));
	} else if (reason == IEEE80211_STATUS_SUCCESS) {
		IEEE80211_NOTE_MAC(vap, ops[op].mask, mac,
		    "station %s via MLME", ops[op].opstr);
	} else {
		IEEE80211_NOTE_MAC(vap, ops[op].mask, mac,
		    "station %s via MLME (reason: %d (%s))", ops[op].opstr,
		    reason, ieee80211_reason_to_string(reason));
	}
#endif /* IEEE80211_DEBUG */
}

static void
domlme(void *arg, struct ieee80211_node *ni)
{
	struct mlmeop *mop = arg;
	struct ieee80211vap *vap = ni->ni_vap;

	if (vap != mop->vap)
		return;
	/*
	 * NB: if ni_associd is zero then the node is already cleaned
	 * up and we don't need to do this (we're safely holding a
	 * reference but should otherwise not modify it's state).
	 */ 
	if (ni->ni_associd == 0)
		return;
	mlmedebug(vap, ni->ni_macaddr, mop->op, mop->reason);
	if (mop->op == IEEE80211_MLME_DEAUTH) {
		IEEE80211_SEND_MGMT(ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    mop->reason);
	} else {
		IEEE80211_SEND_MGMT(ni, IEEE80211_FC0_SUBTYPE_DISASSOC,
		    mop->reason);
	}
	ieee80211_node_leave(ni);
}

static int
setmlme_dropsta(struct ieee80211vap *vap,
	const uint8_t mac[IEEE80211_ADDR_LEN], struct mlmeop *mlmeop)
{
	struct ieee80211_node_table *nt = &vap->iv_ic->ic_sta;
	struct ieee80211_node *ni;
	int error = 0;

	/* NB: the broadcast address means do 'em all */
	if (!IEEE80211_ADDR_EQ(mac, vap->iv_ifp->if_broadcastaddr)) {
		IEEE80211_NODE_LOCK(nt);
		ni = ieee80211_find_node_locked(nt, mac);
		IEEE80211_NODE_UNLOCK(nt);
		/*
		 * Don't do the node update inside the node
		 * table lock.  This unfortunately causes LORs
		 * with drivers and their TX paths.
		 */
		if (ni != NULL) {
			domlme(mlmeop, ni);
			ieee80211_free_node(ni);
		} else
			error = ENOENT;
	} else {
		ieee80211_iterate_nodes(nt, domlme, mlmeop);
	}
	return error;
}

static int
setmlme_common(struct ieee80211vap *vap, int op,
	const uint8_t mac[IEEE80211_ADDR_LEN], int reason)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node_table *nt = &ic->ic_sta;
	struct ieee80211_node *ni;
	struct mlmeop mlmeop;
	int error;

	error = 0;
	switch (op) {
	case IEEE80211_MLME_DISASSOC:
	case IEEE80211_MLME_DEAUTH:
		switch (vap->iv_opmode) {
		case IEEE80211_M_STA:
			mlmedebug(vap, vap->iv_bss->ni_macaddr, op, reason);
			/* XXX not quite right */
			ieee80211_new_state(vap, IEEE80211_S_INIT, reason);
			break;
		case IEEE80211_M_HOSTAP:
			mlmeop.vap = vap;
			mlmeop.op = op;
			mlmeop.reason = reason;
			error = setmlme_dropsta(vap, mac, &mlmeop);
			break;
		case IEEE80211_M_WDS:
			/* XXX user app should send raw frame? */
			if (op != IEEE80211_MLME_DEAUTH) {
				error = EINVAL;
				break;
			}
#if 0
			/* XXX accept any address, simplifies user code */
			if (!IEEE80211_ADDR_EQ(mac, vap->iv_bss->ni_macaddr)) {
				error = EINVAL;
				break;
			}
#endif
			mlmedebug(vap, vap->iv_bss->ni_macaddr, op, reason);
			ni = ieee80211_ref_node(vap->iv_bss);
			IEEE80211_SEND_MGMT(ni,
			    IEEE80211_FC0_SUBTYPE_DEAUTH, reason);
			ieee80211_free_node(ni);
			break;
		case IEEE80211_M_MBSS:
			IEEE80211_NODE_LOCK(nt);
			ni = ieee80211_find_node_locked(nt, mac);
			/*
			 * Don't do the node update inside the node
			 * table lock.  This unfortunately causes LORs
			 * with drivers and their TX paths.
			 */
			IEEE80211_NODE_UNLOCK(nt);
			if (ni != NULL) {
				ieee80211_node_leave(ni);
				ieee80211_free_node(ni);
			} else {
				error = ENOENT;
			}
			break;
		default:
			error = EINVAL;
			break;
		}
		break;
	case IEEE80211_MLME_AUTHORIZE:
	case IEEE80211_MLME_UNAUTHORIZE:
		if (vap->iv_opmode != IEEE80211_M_HOSTAP &&
		    vap->iv_opmode != IEEE80211_M_WDS) {
			error = EINVAL;
			break;
		}
		IEEE80211_NODE_LOCK(nt);
		ni = ieee80211_find_vap_node_locked(nt, vap, mac);
		/*
		 * Don't do the node update inside the node
		 * table lock.  This unfortunately causes LORs
		 * with drivers and their TX paths.
		 */
		IEEE80211_NODE_UNLOCK(nt);
		if (ni != NULL) {
			mlmedebug(vap, mac, op, reason);
			if (op == IEEE80211_MLME_AUTHORIZE)
				ieee80211_node_authorize(ni);
			else
				ieee80211_node_unauthorize(ni);
			ieee80211_free_node(ni);
		} else
			error = ENOENT;
		break;
	case IEEE80211_MLME_AUTH:
		if (vap->iv_opmode != IEEE80211_M_HOSTAP) {
			error = EINVAL;
			break;
		}
		IEEE80211_NODE_LOCK(nt);
		ni = ieee80211_find_vap_node_locked(nt, vap, mac);
		/*
		 * Don't do the node update inside the node
		 * table lock.  This unfortunately causes LORs
		 * with drivers and their TX paths.
		 */
		IEEE80211_NODE_UNLOCK(nt);
		if (ni != NULL) {
			mlmedebug(vap, mac, op, reason);
			if (reason == IEEE80211_STATUS_SUCCESS) {
				IEEE80211_SEND_MGMT(ni,
				    IEEE80211_FC0_SUBTYPE_AUTH, 2);
				/*
				 * For shared key auth, just continue the
				 * exchange.  Otherwise when 802.1x is not in
				 * use mark the port authorized at this point
				 * so traffic can flow.
				 */
				if (ni->ni_authmode != IEEE80211_AUTH_8021X &&
				    ni->ni_challenge == NULL)
				      ieee80211_node_authorize(ni);
			} else {
				vap->iv_stats.is_rx_acl++;
				ieee80211_send_error(ni, ni->ni_macaddr,
				    IEEE80211_FC0_SUBTYPE_AUTH, 2|(reason<<16));
				ieee80211_node_leave(ni);
			}
			ieee80211_free_node(ni);
		} else
			error = ENOENT;
		break;
	default:
		error = EINVAL;
		break;
	}
	return error;
}

struct scanlookup {
	const uint8_t *mac;
	int esslen;
	const uint8_t *essid;
	const struct ieee80211_scan_entry *se;
};

/*
 * Match mac address and any ssid.
 */
static void
mlmelookup(void *arg, const struct ieee80211_scan_entry *se)
{
	struct scanlookup *look = arg;

	if (!IEEE80211_ADDR_EQ(look->mac, se->se_macaddr))
		return;
	if (look->esslen != 0) {
		if (se->se_ssid[1] != look->esslen)
			return;
		if (memcmp(look->essid, se->se_ssid+2, look->esslen))
			return;
	}
	look->se = se;
}

static int
setmlme_assoc_sta(struct ieee80211vap *vap,
	const uint8_t mac[IEEE80211_ADDR_LEN], int ssid_len,
	const uint8_t ssid[IEEE80211_NWID_LEN])
{
	struct scanlookup lookup;

	KASSERT(vap->iv_opmode == IEEE80211_M_STA,
	    ("expected opmode STA not %s",
	    ieee80211_opmode_name[vap->iv_opmode]));

	/* NB: this is racey if roaming is !manual */
	lookup.se = NULL;
	lookup.mac = mac;
	lookup.esslen = ssid_len;
	lookup.essid = ssid;
	ieee80211_scan_iterate(vap, mlmelookup, &lookup);
	if (lookup.se == NULL)
		return ENOENT;
	mlmedebug(vap, mac, IEEE80211_MLME_ASSOC, 0);
	if (!ieee80211_sta_join(vap, lookup.se->se_chan, lookup.se))
		return EIO;		/* XXX unique but could be better */
	return 0;
}

static int
setmlme_assoc_adhoc(struct ieee80211vap *vap,
	const uint8_t mac[IEEE80211_ADDR_LEN], int ssid_len,
	const uint8_t ssid[IEEE80211_NWID_LEN])
{
	struct ieee80211_scan_req *sr;
	int error;

	KASSERT(vap->iv_opmode == IEEE80211_M_IBSS ||
	    vap->iv_opmode == IEEE80211_M_AHDEMO,
	    ("expected opmode IBSS or AHDEMO not %s",
	    ieee80211_opmode_name[vap->iv_opmode]));

	if (ssid_len == 0)
		return EINVAL;

	sr = IEEE80211_MALLOC(sizeof(*sr), M_TEMP,
	     IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (sr == NULL)
		return ENOMEM;

	/* NB: IEEE80211_IOC_SSID call missing for ap_scan=2. */
	memset(vap->iv_des_ssid[0].ssid, 0, IEEE80211_NWID_LEN);
	vap->iv_des_ssid[0].len = ssid_len;
	memcpy(vap->iv_des_ssid[0].ssid, ssid, ssid_len);
	vap->iv_des_nssid = 1;

	sr->sr_flags = IEEE80211_IOC_SCAN_ACTIVE | IEEE80211_IOC_SCAN_ONCE;
	sr->sr_duration = IEEE80211_IOC_SCAN_FOREVER;
	memcpy(sr->sr_ssid[0].ssid, ssid, ssid_len);
	sr->sr_ssid[0].len = ssid_len;
	sr->sr_nssid = 1;

	error = ieee80211_scanreq(vap, sr);

	IEEE80211_FREE(sr, M_TEMP);
	return error;
}

static int
ieee80211_ioctl_setmlme(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211req_mlme mlme;
	int error;

	if (ireq->i_len != sizeof(mlme))
		return EINVAL;
	error = copyin(ireq->i_data, &mlme, sizeof(mlme));
	if (error)
		return error;
	if  (vap->iv_opmode == IEEE80211_M_STA &&
	    mlme.im_op == IEEE80211_MLME_ASSOC)
		return setmlme_assoc_sta(vap, mlme.im_macaddr,
		    vap->iv_des_ssid[0].len, vap->iv_des_ssid[0].ssid);
	else if ((vap->iv_opmode == IEEE80211_M_IBSS || 
	    vap->iv_opmode == IEEE80211_M_AHDEMO) && 
	    mlme.im_op == IEEE80211_MLME_ASSOC)
		return setmlme_assoc_adhoc(vap, mlme.im_macaddr,
		    mlme.im_ssid_len, mlme.im_ssid);
	else
		return setmlme_common(vap, mlme.im_op,
		    mlme.im_macaddr, mlme.im_reason);
}

static int
ieee80211_ioctl_macmac(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	uint8_t mac[IEEE80211_ADDR_LEN];
	const struct ieee80211_aclator *acl = vap->iv_acl;
	int error;

	if (ireq->i_len != sizeof(mac))
		return EINVAL;
	error = copyin(ireq->i_data, mac, ireq->i_len);
	if (error)
		return error;
	if (acl == NULL) {
		acl = ieee80211_aclator_get("mac");
		if (acl == NULL || !acl->iac_attach(vap))
			return EINVAL;
		vap->iv_acl = acl;
	}
	if (ireq->i_type == IEEE80211_IOC_ADDMAC)
		acl->iac_add(vap, mac);
	else
		acl->iac_remove(vap, mac);
	return 0;
}

static int
ieee80211_ioctl_setmaccmd(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	const struct ieee80211_aclator *acl = vap->iv_acl;

	switch (ireq->i_val) {
	case IEEE80211_MACCMD_POLICY_OPEN:
	case IEEE80211_MACCMD_POLICY_ALLOW:
	case IEEE80211_MACCMD_POLICY_DENY:
	case IEEE80211_MACCMD_POLICY_RADIUS:
		if (acl == NULL) {
			acl = ieee80211_aclator_get("mac");
			if (acl == NULL || !acl->iac_attach(vap))
				return EINVAL;
			vap->iv_acl = acl;
		}
		acl->iac_setpolicy(vap, ireq->i_val);
		break;
	case IEEE80211_MACCMD_FLUSH:
		if (acl != NULL)
			acl->iac_flush(vap);
		/* NB: silently ignore when not in use */
		break;
	case IEEE80211_MACCMD_DETACH:
		if (acl != NULL) {
			vap->iv_acl = NULL;
			acl->iac_detach(vap);
		}
		break;
	default:
		if (acl == NULL)
			return EINVAL;
		else
			return acl->iac_setioctl(vap, ireq);
	}
	return 0;
}

static int
ieee80211_ioctl_setchanlist(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	uint8_t *chanlist, *list;
	int i, nchan, maxchan, error;

	if (ireq->i_len > sizeof(ic->ic_chan_active))
		ireq->i_len = sizeof(ic->ic_chan_active);
	list = IEEE80211_MALLOC(ireq->i_len + IEEE80211_CHAN_BYTES, M_TEMP,
	    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (list == NULL)
		return ENOMEM;
	error = copyin(ireq->i_data, list, ireq->i_len);
	if (error) {
		IEEE80211_FREE(list, M_TEMP);
		return error;
	}
	nchan = 0;
	chanlist = list + ireq->i_len;		/* NB: zero'd already */
	maxchan = ireq->i_len * NBBY;
	for (i = 0; i < ic->ic_nchans; i++) {
		const struct ieee80211_channel *c = &ic->ic_channels[i];
		/*
		 * Calculate the intersection of the user list and the
		 * available channels so users can do things like specify
		 * 1-255 to get all available channels.
		 */
		if (c->ic_ieee < maxchan && isset(list, c->ic_ieee)) {
			setbit(chanlist, c->ic_ieee);
			nchan++;
		}
	}
	if (nchan == 0) {
		IEEE80211_FREE(list, M_TEMP);
		return EINVAL;
	}
	if (ic->ic_bsschan != IEEE80211_CHAN_ANYC &&	/* XXX */
	    isclr(chanlist, ic->ic_bsschan->ic_ieee))
		ic->ic_bsschan = IEEE80211_CHAN_ANYC;
	memcpy(ic->ic_chan_active, chanlist, IEEE80211_CHAN_BYTES);
	ieee80211_scan_flush(vap);
	IEEE80211_FREE(list, M_TEMP);
	return ENETRESET;
}

static int
ieee80211_ioctl_setstastats(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_node *ni;
	uint8_t macaddr[IEEE80211_ADDR_LEN];
	int error;

	/*
	 * NB: we could copyin ieee80211req_sta_stats so apps
	 *     could make selective changes but that's overkill;
	 *     just clear all stats for now.
	 */
	if (ireq->i_len < IEEE80211_ADDR_LEN)
		return EINVAL;
	error = copyin(ireq->i_data, macaddr, IEEE80211_ADDR_LEN);
	if (error != 0)
		return error;
	ni = ieee80211_find_vap_node(&vap->iv_ic->ic_sta, vap, macaddr);
	if (ni == NULL)
		return ENOENT;
	/* XXX require ni_vap == vap? */
	memset(&ni->ni_stats, 0, sizeof(ni->ni_stats));
	ieee80211_free_node(ni);
	return 0;
}

static int
ieee80211_ioctl_setstatxpow(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_node *ni;
	struct ieee80211req_sta_txpow txpow;
	int error;

	if (ireq->i_len != sizeof(txpow))
		return EINVAL;
	error = copyin(ireq->i_data, &txpow, sizeof(txpow));
	if (error != 0)
		return error;
	ni = ieee80211_find_vap_node(&vap->iv_ic->ic_sta, vap, txpow.it_macaddr);
	if (ni == NULL)
		return ENOENT;
	ni->ni_txpower = txpow.it_txpow;
	ieee80211_free_node(ni);
	return error;
}

static int
ieee80211_ioctl_setwmeparam(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_wme_state *wme = &ic->ic_wme;
	struct wmeParams *wmep, *chanp;
	int isbss, ac, aggrmode;

	if ((ic->ic_caps & IEEE80211_C_WME) == 0)
		return EOPNOTSUPP;

	isbss = (ireq->i_len & IEEE80211_WMEPARAM_BSS);
	ac = (ireq->i_len & IEEE80211_WMEPARAM_VAL);
	aggrmode = (wme->wme_flags & WME_F_AGGRMODE);
	if (ac >= WME_NUM_AC)
		ac = WME_AC_BE;
	if (isbss) {
		chanp = &wme->wme_bssChanParams.cap_wmeParams[ac];
		wmep = &wme->wme_wmeBssChanParams.cap_wmeParams[ac];
	} else {
		chanp = &wme->wme_chanParams.cap_wmeParams[ac];
		wmep = &wme->wme_wmeChanParams.cap_wmeParams[ac];
	}
	switch (ireq->i_type) {
	case IEEE80211_IOC_WME_CWMIN:		/* WME: CWmin */
		wmep->wmep_logcwmin = ireq->i_val;
		if (!isbss || !aggrmode)
			chanp->wmep_logcwmin = ireq->i_val;
		break;
	case IEEE80211_IOC_WME_CWMAX:		/* WME: CWmax */
		wmep->wmep_logcwmax = ireq->i_val;
		if (!isbss || !aggrmode)
			chanp->wmep_logcwmax = ireq->i_val;
		break;
	case IEEE80211_IOC_WME_AIFS:		/* WME: AIFS */
		wmep->wmep_aifsn = ireq->i_val;
		if (!isbss || !aggrmode)
			chanp->wmep_aifsn = ireq->i_val;
		break;
	case IEEE80211_IOC_WME_TXOPLIMIT:	/* WME: txops limit */
		wmep->wmep_txopLimit = ireq->i_val;
		if (!isbss || !aggrmode)
			chanp->wmep_txopLimit = ireq->i_val;
		break;
	case IEEE80211_IOC_WME_ACM:		/* WME: ACM (bss only) */
		wmep->wmep_acm = ireq->i_val;
		if (!aggrmode)
			chanp->wmep_acm = ireq->i_val;
		break;
	case IEEE80211_IOC_WME_ACKPOLICY:	/* WME: ACK policy (!bss only)*/
		wmep->wmep_noackPolicy = chanp->wmep_noackPolicy =
			(ireq->i_val) == 0;
		break;
	}
	ieee80211_wme_updateparams(vap);
	return 0;
}

static int
find11gchannel(struct ieee80211com *ic, int start, int freq)
{
	const struct ieee80211_channel *c;
	int i;

	for (i = start+1; i < ic->ic_nchans; i++) {
		c = &ic->ic_channels[i];
		if (c->ic_freq == freq && IEEE80211_IS_CHAN_ANYG(c))
			return 1;
	}
	/* NB: should not be needed but in case things are mis-sorted */
	for (i = 0; i < start; i++) {
		c = &ic->ic_channels[i];
		if (c->ic_freq == freq && IEEE80211_IS_CHAN_ANYG(c))
			return 1;
	}
	return 0;
}

static struct ieee80211_channel *
findchannel(struct ieee80211com *ic, int ieee, int mode)
{
	static const u_int chanflags[IEEE80211_MODE_MAX] = {
	    [IEEE80211_MODE_AUTO]	= 0,
	    [IEEE80211_MODE_11A]	= IEEE80211_CHAN_A,
	    [IEEE80211_MODE_11B]	= IEEE80211_CHAN_B,
	    [IEEE80211_MODE_11G]	= IEEE80211_CHAN_G,
	    [IEEE80211_MODE_FH]		= IEEE80211_CHAN_FHSS,
	    [IEEE80211_MODE_TURBO_A]	= IEEE80211_CHAN_108A,
	    [IEEE80211_MODE_TURBO_G]	= IEEE80211_CHAN_108G,
	    [IEEE80211_MODE_STURBO_A]	= IEEE80211_CHAN_STURBO,
	    [IEEE80211_MODE_HALF]	= IEEE80211_CHAN_HALF,
	    [IEEE80211_MODE_QUARTER]	= IEEE80211_CHAN_QUARTER,
	    /* NB: handled specially below */
	    [IEEE80211_MODE_11NA]	= IEEE80211_CHAN_A,
	    [IEEE80211_MODE_11NG]	= IEEE80211_CHAN_G,
	    [IEEE80211_MODE_VHT_5GHZ]	= IEEE80211_CHAN_A,
	    [IEEE80211_MODE_VHT_2GHZ]	= IEEE80211_CHAN_G,
	};
	u_int modeflags;
	int i;

	modeflags = chanflags[mode];
	for (i = 0; i < ic->ic_nchans; i++) {
		struct ieee80211_channel *c = &ic->ic_channels[i];

		if (c->ic_ieee != ieee)
			continue;
		if (mode == IEEE80211_MODE_AUTO) {
			/* ignore turbo channels for autoselect */
			if (IEEE80211_IS_CHAN_TURBO(c))
				continue;
			/*
			 * XXX special-case 11b/g channels so we
			 *     always select the g channel if both
			 *     are present.
			 * XXX prefer HT to non-HT?
			 */
			if (!IEEE80211_IS_CHAN_B(c) ||
			    !find11gchannel(ic, i, c->ic_freq))
				return c;
		} else {
			/* must check VHT specifically */
			if ((mode == IEEE80211_MODE_VHT_5GHZ ||
			    mode == IEEE80211_MODE_VHT_2GHZ) &&
			    !IEEE80211_IS_CHAN_VHT(c))
				continue;

			/*
			 * Must check HT specially - only match on HT,
			 * not HT+VHT channels
			 */
			if ((mode == IEEE80211_MODE_11NA ||
			    mode == IEEE80211_MODE_11NG) &&
			    !IEEE80211_IS_CHAN_HT(c))
				continue;

			if ((mode == IEEE80211_MODE_11NA ||
			    mode == IEEE80211_MODE_11NG) &&
			    IEEE80211_IS_CHAN_VHT(c))
				continue;

			/* Check that the modeflags above match */
			if ((c->ic_flags & modeflags) == modeflags)
				return c;
		}
	}
	return NULL;
}

/*
 * Check the specified against any desired mode (aka netband).
 * This is only used (presently) when operating in hostap mode
 * to enforce consistency.
 */
static int
check_mode_consistency(const struct ieee80211_channel *c, int mode)
{
	KASSERT(c != IEEE80211_CHAN_ANYC, ("oops, no channel"));

	switch (mode) {
	case IEEE80211_MODE_11B:
		return (IEEE80211_IS_CHAN_B(c));
	case IEEE80211_MODE_11G:
		return (IEEE80211_IS_CHAN_ANYG(c) && !IEEE80211_IS_CHAN_HT(c));
	case IEEE80211_MODE_11A:
		return (IEEE80211_IS_CHAN_A(c) && !IEEE80211_IS_CHAN_HT(c));
	case IEEE80211_MODE_STURBO_A:
		return (IEEE80211_IS_CHAN_STURBO(c));
	case IEEE80211_MODE_11NA:
		return (IEEE80211_IS_CHAN_HTA(c));
	case IEEE80211_MODE_11NG:
		return (IEEE80211_IS_CHAN_HTG(c));
	}
	return 1;

}

/*
 * Common code to set the current channel.  If the device
 * is up and running this may result in an immediate channel
 * change or a kick of the state machine.
 */
static int
setcurchan(struct ieee80211vap *vap, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = vap->iv_ic;
	int error;

	if (c != IEEE80211_CHAN_ANYC) {
		if (IEEE80211_IS_CHAN_RADAR(c))
			return EBUSY;	/* XXX better code? */
		if (vap->iv_opmode == IEEE80211_M_HOSTAP) {
			if (IEEE80211_IS_CHAN_NOHOSTAP(c))
				return EINVAL;
			if (!check_mode_consistency(c, vap->iv_des_mode))
				return EINVAL;
		} else if (vap->iv_opmode == IEEE80211_M_IBSS) {
			if (IEEE80211_IS_CHAN_NOADHOC(c))
				return EINVAL;
		}
		if ((vap->iv_state == IEEE80211_S_RUN || vap->iv_state == IEEE80211_S_SLEEP) &&
		    vap->iv_bss->ni_chan == c)
			return 0;	/* NB: nothing to do */
	}
	vap->iv_des_chan = c;

	error = 0;
	if (vap->iv_opmode == IEEE80211_M_MONITOR &&
	    vap->iv_des_chan != IEEE80211_CHAN_ANYC) {
		/*
		 * Monitor mode can switch directly.
		 */
		if (IFNET_IS_UP_RUNNING(vap->iv_ifp)) {
			/* XXX need state machine for other vap's to follow */
			ieee80211_setcurchan(ic, vap->iv_des_chan);
			vap->iv_bss->ni_chan = ic->ic_curchan;
		} else {
			ic->ic_curchan = vap->iv_des_chan;
			ic->ic_rt = ieee80211_get_ratetable(ic->ic_curchan);
		}
	} else {
		/*
		 * Need to go through the state machine in case we
		 * need to reassociate or the like.  The state machine
		 * will pickup the desired channel and avoid scanning.
		 */
		if (IS_UP_AUTO(vap))
			ieee80211_new_state(vap, IEEE80211_S_SCAN, 0);
		else if (vap->iv_des_chan != IEEE80211_CHAN_ANYC) {
			/*
			 * When not up+running and a real channel has
			 * been specified fix the current channel so
			 * there is immediate feedback; e.g. via ifconfig.
			 */
			ic->ic_curchan = vap->iv_des_chan;
			ic->ic_rt = ieee80211_get_ratetable(ic->ic_curchan);
		}
	}
	return error;
}

/*
 * Old api for setting the current channel; this is
 * deprecated because channel numbers are ambiguous.
 */
static int
ieee80211_ioctl_setchannel(struct ieee80211vap *vap,
	const struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_channel *c;

	/* XXX 0xffff overflows 16-bit signed */
	if (ireq->i_val == 0 ||
	    ireq->i_val == (int16_t) IEEE80211_CHAN_ANY) {
		c = IEEE80211_CHAN_ANYC;
	} else {
		struct ieee80211_channel *c2;

		c = findchannel(ic, ireq->i_val, vap->iv_des_mode);
		if (c == NULL) {
			c = findchannel(ic, ireq->i_val,
				IEEE80211_MODE_AUTO);
			if (c == NULL)
				return EINVAL;
		}

		/*
		 * Fine tune channel selection based on desired mode:
		 *   if 11b is requested, find the 11b version of any
		 *      11g channel returned,
		 *   if static turbo, find the turbo version of any
		 *	11a channel return,
		 *   if 11na is requested, find the ht version of any
		 *      11a channel returned,
		 *   if 11ng is requested, find the ht version of any
		 *      11g channel returned,
		 *   if 11ac is requested, find the 11ac version
		 *      of any 11a/11na channel returned,
		 *   (TBD) 11acg (2GHz VHT)
		 *   otherwise we should be ok with what we've got.
		 */
		switch (vap->iv_des_mode) {
		case IEEE80211_MODE_11B:
			if (IEEE80211_IS_CHAN_ANYG(c)) {
				c2 = findchannel(ic, ireq->i_val,
					IEEE80211_MODE_11B);
				/* NB: should not happen, =>'s 11g w/o 11b */
				if (c2 != NULL)
					c = c2;
			}
			break;
		case IEEE80211_MODE_TURBO_A:
			if (IEEE80211_IS_CHAN_A(c)) {
				c2 = findchannel(ic, ireq->i_val,
					IEEE80211_MODE_TURBO_A);
				if (c2 != NULL)
					c = c2;
			}
			break;
		case IEEE80211_MODE_11NA:
			if (IEEE80211_IS_CHAN_A(c)) {
				c2 = findchannel(ic, ireq->i_val,
					IEEE80211_MODE_11NA);
				if (c2 != NULL)
					c = c2;
			}
			break;
		case IEEE80211_MODE_11NG:
			if (IEEE80211_IS_CHAN_ANYG(c)) {
				c2 = findchannel(ic, ireq->i_val,
					IEEE80211_MODE_11NG);
				if (c2 != NULL)
					c = c2;
			}
			break;
		case IEEE80211_MODE_VHT_2GHZ:
			printf("%s: TBD\n", __func__);
			break;
		case IEEE80211_MODE_VHT_5GHZ:
			if (IEEE80211_IS_CHAN_A(c)) {
				c2 = findchannel(ic, ireq->i_val,
					IEEE80211_MODE_VHT_5GHZ);
				if (c2 != NULL)
					c = c2;
			}
			break;
		default:		/* NB: no static turboG */
			break;
		}
	}
	return setcurchan(vap, c);
}

/*
 * New/current api for setting the current channel; a complete
 * channel description is provide so there is no ambiguity in
 * identifying the channel.
 */
static int
ieee80211_ioctl_setcurchan(struct ieee80211vap *vap,
	const struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_channel chan, *c;
	int error;

	if (ireq->i_len != sizeof(chan))
		return EINVAL;
	error = copyin(ireq->i_data, &chan, sizeof(chan));
	if (error != 0)
		return error;

	/* XXX 0xffff overflows 16-bit signed */
	if (chan.ic_freq == 0 || chan.ic_freq == IEEE80211_CHAN_ANY) {
		c = IEEE80211_CHAN_ANYC;
	} else {
		c = ieee80211_find_channel(ic, chan.ic_freq, chan.ic_flags);
		if (c == NULL)
			return EINVAL;
	}
	return setcurchan(vap, c);
}

static int
ieee80211_ioctl_setregdomain(struct ieee80211vap *vap,
	const struct ieee80211req *ireq)
{
	struct ieee80211_regdomain_req *reg;
	int nchans, error;

	nchans = 1 + ((ireq->i_len - sizeof(struct ieee80211_regdomain_req)) /
	    sizeof(struct ieee80211_channel));
	if (!(1 <= nchans && nchans <= IEEE80211_CHAN_MAX)) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_IOCTL,
		    "%s: bad # chans, i_len %d nchans %d\n", __func__,
		    ireq->i_len, nchans);
		return EINVAL;
	}
	reg = (struct ieee80211_regdomain_req *)
	    IEEE80211_MALLOC(IEEE80211_REGDOMAIN_SIZE(nchans), M_TEMP,
	      IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (reg == NULL) {
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_IOCTL,
		    "%s: no memory, nchans %d\n", __func__, nchans);
		return ENOMEM;
	}
	error = copyin(ireq->i_data, reg, IEEE80211_REGDOMAIN_SIZE(nchans));
	if (error == 0) {
		/* NB: validate inline channel count against storage size */
		if (reg->chaninfo.ic_nchans != nchans) {
			IEEE80211_DPRINTF(vap, IEEE80211_MSG_IOCTL,
			    "%s: chan cnt mismatch, %d != %d\n", __func__,
				reg->chaninfo.ic_nchans, nchans);
			error = EINVAL;
		} else
			error = ieee80211_setregdomain(vap, reg);
	}
	IEEE80211_FREE(reg, M_TEMP);

	return (error == 0 ? ENETRESET : error);
}

static int
checkrate(const struct ieee80211_rateset *rs, int rate)
{
	int i;

	if (rate == IEEE80211_FIXED_RATE_NONE)
		return 1;
	for (i = 0; i < rs->rs_nrates; i++)
		if ((rs->rs_rates[i] & IEEE80211_RATE_VAL) == rate)
			return 1;
	return 0;
}

static int
checkmcs(const struct ieee80211_htrateset *rs, int mcs)
{
	int rate_val = IEEE80211_RV(mcs);
	int i;

	if (mcs == IEEE80211_FIXED_RATE_NONE)
		return 1;
	if ((mcs & IEEE80211_RATE_MCS) == 0)	/* MCS always have 0x80 set */
		return 0;
	for (i = 0; i < rs->rs_nrates; i++)
		if (IEEE80211_RV(rs->rs_rates[i]) == rate_val)
			return 1;
	return 0;
}

static int
ieee80211_ioctl_setroam(struct ieee80211vap *vap,
        const struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_roamparams_req *parms;
	struct ieee80211_roamparam *src, *dst;
	const struct ieee80211_htrateset *rs_ht;
	const struct ieee80211_rateset *rs;
	int changed, error, mode, is11n, nmodes;

	if (ireq->i_len != sizeof(vap->iv_roamparms))
		return EINVAL;

	parms = IEEE80211_MALLOC(sizeof(*parms), M_TEMP,
	    IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (parms == NULL)
		return ENOMEM;

	error = copyin(ireq->i_data, parms, ireq->i_len);
	if (error != 0)
		goto fail;

	changed = 0;
	nmodes = IEEE80211_MODE_MAX;

	/* validate parameters and check if anything changed */
	for (mode = IEEE80211_MODE_11A; mode < nmodes; mode++) {
		if (isclr(ic->ic_modecaps, mode))
			continue;
		src = &parms->params[mode];
		dst = &vap->iv_roamparms[mode];
		rs = &ic->ic_sup_rates[mode];	/* NB: 11n maps to legacy */
		rs_ht = &ic->ic_sup_htrates;
		is11n = (mode == IEEE80211_MODE_11NA ||
			 mode == IEEE80211_MODE_11NG);
		/* XXX TODO: 11ac */
		if (src->rate != dst->rate) {
			if (!checkrate(rs, src->rate) &&
			    (!is11n || !checkmcs(rs_ht, src->rate))) {
				error = EINVAL;
				goto fail;
			}
			changed++;
		}
		if (src->rssi != dst->rssi)
			changed++;
	}
	if (changed) {
		/*
		 * Copy new parameters in place and notify the
		 * driver so it can push state to the device.
		 */
		/* XXX locking? */
		for (mode = IEEE80211_MODE_11A; mode < nmodes; mode++) {
			if (isset(ic->ic_modecaps, mode))
				vap->iv_roamparms[mode] = parms->params[mode];
		}

		if (vap->iv_roaming == IEEE80211_ROAMING_DEVICE)
			error = ERESTART;
	}

fail:	IEEE80211_FREE(parms, M_TEMP);
	return error;
}

static int
ieee80211_ioctl_settxparams(struct ieee80211vap *vap,
	const struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_txparams_req parms;	/* XXX stack use? */
	struct ieee80211_txparam *src, *dst;
	const struct ieee80211_htrateset *rs_ht;
	const struct ieee80211_rateset *rs;
	int error, mode, changed, is11n, nmodes;

	/* NB: accept short requests for backwards compat */
	if (ireq->i_len > sizeof(parms))
		return EINVAL;
	error = copyin(ireq->i_data, &parms, ireq->i_len);
	if (error != 0)
		return error;
	nmodes = ireq->i_len / sizeof(struct ieee80211_txparam);
	changed = 0;
	/* validate parameters and check if anything changed */
	for (mode = IEEE80211_MODE_11A; mode < nmodes; mode++) {
		if (isclr(ic->ic_modecaps, mode))
			continue;
		src = &parms.params[mode];
		dst = &vap->iv_txparms[mode];
		rs = &ic->ic_sup_rates[mode];	/* NB: 11n maps to legacy */
		rs_ht = &ic->ic_sup_htrates;
		is11n = (mode == IEEE80211_MODE_11NA ||
			 mode == IEEE80211_MODE_11NG);
		if (src->ucastrate != dst->ucastrate) {
			if (!checkrate(rs, src->ucastrate) &&
			    (!is11n || !checkmcs(rs_ht, src->ucastrate)))
				return EINVAL;
			changed++;
		}
		if (src->mcastrate != dst->mcastrate) {
			if (!checkrate(rs, src->mcastrate) &&
			    (!is11n || !checkmcs(rs_ht, src->mcastrate)))
				return EINVAL;
			changed++;
		}
		if (src->mgmtrate != dst->mgmtrate) {
			if (!checkrate(rs, src->mgmtrate) &&
			    (!is11n || !checkmcs(rs_ht, src->mgmtrate)))
				return EINVAL;
			changed++;
		}
		if (src->maxretry != dst->maxretry)	/* NB: no bounds */
			changed++;
	}
	if (changed) {
		/*
		 * Copy new parameters in place and notify the
		 * driver so it can push state to the device.
		 */
		for (mode = IEEE80211_MODE_11A; mode < nmodes; mode++) {
			if (isset(ic->ic_modecaps, mode))
				vap->iv_txparms[mode] = parms.params[mode];
		}
		/* XXX could be more intelligent,
		   e.g. don't reset if setting not being used */
		return ENETRESET;
	}
	return 0;
}

/*
 * Application Information Element support.
 */
static int
setappie(struct ieee80211_appie **aie, const struct ieee80211req *ireq)
{
	struct ieee80211_appie *app = *aie;
	struct ieee80211_appie *napp;
	int error;

	if (ireq->i_len == 0) {		/* delete any existing ie */
		if (app != NULL) {
			*aie = NULL;	/* XXX racey */
			IEEE80211_FREE(app, M_80211_NODE_IE);
		}
		return 0;
	}
	if (!(2 <= ireq->i_len && ireq->i_len <= IEEE80211_MAX_APPIE))
		return EINVAL;
	/*
	 * Allocate a new appie structure and copy in the user data.
	 * When done swap in the new structure.  Note that we do not
	 * guard against users holding a ref to the old structure;
	 * this must be handled outside this code.
	 *
	 * XXX bad bad bad
	 */
	napp = (struct ieee80211_appie *) IEEE80211_MALLOC(
	    sizeof(struct ieee80211_appie) + ireq->i_len, M_80211_NODE_IE,
	    IEEE80211_M_NOWAIT);
	if (napp == NULL)
		return ENOMEM;
	/* XXX holding ic lock */
	error = copyin(ireq->i_data, napp->ie_data, ireq->i_len);
	if (error) {
		IEEE80211_FREE(napp, M_80211_NODE_IE);
		return error;
	}
	napp->ie_len = ireq->i_len;
	*aie = napp;
	if (app != NULL)
		IEEE80211_FREE(app, M_80211_NODE_IE);
	return 0;
}

static void
setwparsnie(struct ieee80211vap *vap, uint8_t *ie, int space)
{
	/* validate data is present as best we can */
	if (space == 0 || 2+ie[1] > space)
		return;
	if (ie[0] == IEEE80211_ELEMID_VENDOR)
		vap->iv_wpa_ie = ie;
	else if (ie[0] == IEEE80211_ELEMID_RSN)
		vap->iv_rsn_ie = ie;
}

static int
ieee80211_ioctl_setappie_locked(struct ieee80211vap *vap,
	const struct ieee80211req *ireq, int fc0)
{
	int error;

	IEEE80211_LOCK_ASSERT(vap->iv_ic);

	switch (fc0 & IEEE80211_FC0_SUBTYPE_MASK) {
	case IEEE80211_FC0_SUBTYPE_BEACON:
		if (vap->iv_opmode != IEEE80211_M_HOSTAP &&
		    vap->iv_opmode != IEEE80211_M_IBSS) {
			error = EINVAL;
			break;
		}
		error = setappie(&vap->iv_appie_beacon, ireq);
		if (error == 0)
			ieee80211_beacon_notify(vap, IEEE80211_BEACON_APPIE);
		break;
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
		error = setappie(&vap->iv_appie_proberesp, ireq);
		break;
	case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
		if (vap->iv_opmode == IEEE80211_M_HOSTAP)
			error = setappie(&vap->iv_appie_assocresp, ireq);
		else
			error = EINVAL;
		break;
	case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
		error = setappie(&vap->iv_appie_probereq, ireq);
		break;
	case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
		if (vap->iv_opmode == IEEE80211_M_STA)
			error = setappie(&vap->iv_appie_assocreq, ireq);
		else
			error = EINVAL;
		break;
	case (IEEE80211_APPIE_WPA & IEEE80211_FC0_SUBTYPE_MASK):
		error = setappie(&vap->iv_appie_wpa, ireq);
		if (error == 0) {
			/*
			 * Must split single blob of data into separate
			 * WPA and RSN ie's because they go in different
			 * locations in the mgt frames.
			 * XXX use IEEE80211_IOC_WPA2 so user code does split
			 */
			vap->iv_wpa_ie = NULL;
			vap->iv_rsn_ie = NULL;
			if (vap->iv_appie_wpa != NULL) {
				struct ieee80211_appie *appie =
				    vap->iv_appie_wpa;
				uint8_t *data = appie->ie_data;

				/* XXX ie length validate is painful, cheat */
				setwparsnie(vap, data, appie->ie_len);
				setwparsnie(vap, data + 2 + data[1],
				    appie->ie_len - (2 + data[1]));
			}
			if (vap->iv_opmode == IEEE80211_M_HOSTAP ||
			    vap->iv_opmode == IEEE80211_M_IBSS) {
				/*
				 * Must rebuild beacon frame as the update
				 * mechanism doesn't handle WPA/RSN ie's.
				 * Could extend it but it doesn't normally
				 * change; this is just to deal with hostapd
				 * plumbing the ie after the interface is up.
				 */
				error = ENETRESET;
			}
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	return error;
}

static int
ieee80211_ioctl_setappie(struct ieee80211vap *vap,
	const struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	int error;
	uint8_t fc0;

	fc0 = ireq->i_val & 0xff;
	if ((fc0 & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_MGT)
		return EINVAL;
	/* NB: could check iv_opmode and reject but hardly worth the effort */
	IEEE80211_LOCK(ic);
	error = ieee80211_ioctl_setappie_locked(vap, ireq, fc0);
	IEEE80211_UNLOCK(ic);
	return error;
}

static int
ieee80211_ioctl_chanswitch(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_chanswitch_req csr;
	struct ieee80211_channel *c;
	int error;

	if (ireq->i_len != sizeof(csr))
		return EINVAL;
	error = copyin(ireq->i_data, &csr, sizeof(csr));
	if (error != 0)
		return error;
	/* XXX adhoc mode not supported */
	if (vap->iv_opmode != IEEE80211_M_HOSTAP ||
	    (vap->iv_flags & IEEE80211_F_DOTH) == 0)
		return EOPNOTSUPP;
	c = ieee80211_find_channel(ic,
	    csr.csa_chan.ic_freq, csr.csa_chan.ic_flags);
	if (c == NULL)
		return ENOENT;
	IEEE80211_LOCK(ic);
	if ((ic->ic_flags & IEEE80211_F_CSAPENDING) == 0)
		ieee80211_csa_startswitch(ic, c, csr.csa_mode, csr.csa_count);
	else if (csr.csa_count == 0)
		ieee80211_csa_cancelswitch(ic);
	else
		error = EBUSY;
	IEEE80211_UNLOCK(ic);
	return error;
}

static int
ieee80211_scanreq(struct ieee80211vap *vap, struct ieee80211_scan_req *sr)
{
#define	IEEE80211_IOC_SCAN_FLAGS \
	(IEEE80211_IOC_SCAN_NOPICK | IEEE80211_IOC_SCAN_ACTIVE | \
	 IEEE80211_IOC_SCAN_PICK1ST | IEEE80211_IOC_SCAN_BGSCAN | \
	 IEEE80211_IOC_SCAN_ONCE | IEEE80211_IOC_SCAN_NOBCAST | \
	 IEEE80211_IOC_SCAN_NOJOIN | IEEE80211_IOC_SCAN_FLUSH | \
	 IEEE80211_IOC_SCAN_CHECK)
	struct ieee80211com *ic = vap->iv_ic;
	int error, i;

	/* convert duration */
	if (sr->sr_duration == IEEE80211_IOC_SCAN_FOREVER)
		sr->sr_duration = IEEE80211_SCAN_FOREVER;
	else {
		if (sr->sr_duration < IEEE80211_IOC_SCAN_DURATION_MIN ||
		    sr->sr_duration > IEEE80211_IOC_SCAN_DURATION_MAX)
			return EINVAL;
		sr->sr_duration = msecs_to_ticks(sr->sr_duration);
	}
	/* convert min/max channel dwell */
	if (sr->sr_mindwell != 0)
		sr->sr_mindwell = msecs_to_ticks(sr->sr_mindwell);
	if (sr->sr_maxdwell != 0)
		sr->sr_maxdwell = msecs_to_ticks(sr->sr_maxdwell);
	/* NB: silently reduce ssid count to what is supported */
	if (sr->sr_nssid > IEEE80211_SCAN_MAX_SSID)
		sr->sr_nssid = IEEE80211_SCAN_MAX_SSID;
	for (i = 0; i < sr->sr_nssid; i++)
		if (sr->sr_ssid[i].len > IEEE80211_NWID_LEN)
			return EINVAL;
	/* cleanse flags just in case, could reject if invalid flags */
	sr->sr_flags &= IEEE80211_IOC_SCAN_FLAGS;
	/*
	 * Add an implicit NOPICK if the vap is not marked UP.  This
	 * allows applications to scan without joining a bss (or picking
	 * a channel and setting up a bss) and without forcing manual
	 * roaming mode--you just need to mark the parent device UP.
	 */
	if ((vap->iv_ifp->if_flags & IFF_UP) == 0)
		sr->sr_flags |= IEEE80211_IOC_SCAN_NOPICK;

	IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
	    "%s: flags 0x%x%s duration 0x%x mindwell %u maxdwell %u nssid %d\n",
	    __func__, sr->sr_flags,
	    (vap->iv_ifp->if_flags & IFF_UP) == 0 ? " (!IFF_UP)" : "",
	    sr->sr_duration, sr->sr_mindwell, sr->sr_maxdwell, sr->sr_nssid);
	/*
	 * If we are in INIT state then the driver has never had a chance
	 * to setup hardware state to do a scan; we must use the state
	 * machine to get us up to the SCAN state but once we reach SCAN
	 * state we then want to use the supplied params.  Stash the
	 * parameters in the vap and mark IEEE80211_FEXT_SCANREQ; the
	 * state machines will recognize this and use the stashed params
	 * to issue the scan request.
	 *
	 * Otherwise just invoke the scan machinery directly.
	 */
	IEEE80211_LOCK(ic);
	if (ic->ic_nrunning == 0) {
		IEEE80211_UNLOCK(ic);
		return ENXIO;
	}

	if (vap->iv_state == IEEE80211_S_INIT) {
		/* NB: clobbers previous settings */
		vap->iv_scanreq_flags = sr->sr_flags;
		vap->iv_scanreq_duration = sr->sr_duration;
		vap->iv_scanreq_nssid = sr->sr_nssid;
		for (i = 0; i < sr->sr_nssid; i++) {
			vap->iv_scanreq_ssid[i].len = sr->sr_ssid[i].len;
			memcpy(vap->iv_scanreq_ssid[i].ssid,
			    sr->sr_ssid[i].ssid, sr->sr_ssid[i].len);
		}
		vap->iv_flags_ext |= IEEE80211_FEXT_SCANREQ;
		IEEE80211_UNLOCK(ic);
		ieee80211_new_state(vap, IEEE80211_S_SCAN, 0);
	} else {
		vap->iv_flags_ext &= ~IEEE80211_FEXT_SCANREQ;
		IEEE80211_UNLOCK(ic);
		if (sr->sr_flags & IEEE80211_IOC_SCAN_CHECK) {
			error = ieee80211_check_scan(vap, sr->sr_flags,
			    sr->sr_duration, sr->sr_mindwell, sr->sr_maxdwell,
			    sr->sr_nssid,
			    /* NB: cheat, we assume structures are compatible */
			    (const struct ieee80211_scan_ssid *) &sr->sr_ssid[0]);
		} else {
			error = ieee80211_start_scan(vap, sr->sr_flags,
			    sr->sr_duration, sr->sr_mindwell, sr->sr_maxdwell,
			    sr->sr_nssid,
			    /* NB: cheat, we assume structures are compatible */
			    (const struct ieee80211_scan_ssid *) &sr->sr_ssid[0]);
		}
		if (error == 0)
			return EINPROGRESS;
	}
	return 0;
#undef IEEE80211_IOC_SCAN_FLAGS
}

static int
ieee80211_ioctl_scanreq(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_scan_req *sr;
	int error;

	if (ireq->i_len != sizeof(*sr))
		return EINVAL;
	sr = IEEE80211_MALLOC(sizeof(*sr), M_TEMP,
	     IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (sr == NULL)
		return ENOMEM;
	error = copyin(ireq->i_data, sr, sizeof(*sr));
	if (error != 0)
		goto bad;
	error = ieee80211_scanreq(vap, sr);
bad:
	IEEE80211_FREE(sr, M_TEMP);
	return error;
}

static int
ieee80211_ioctl_setstavlan(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	struct ieee80211_node *ni;
	struct ieee80211req_sta_vlan vlan;
	int error;

	if (ireq->i_len != sizeof(vlan))
		return EINVAL;
	error = copyin(ireq->i_data, &vlan, sizeof(vlan));
	if (error != 0)
		return error;
	if (!IEEE80211_ADDR_EQ(vlan.sv_macaddr, zerobssid)) {
		ni = ieee80211_find_vap_node(&vap->iv_ic->ic_sta, vap,
		    vlan.sv_macaddr);
		if (ni == NULL)
			return ENOENT;
	} else
		ni = ieee80211_ref_node(vap->iv_bss);
	ni->ni_vlan = vlan.sv_vlan;
	ieee80211_free_node(ni);
	return error;
}

static int
isvap11g(const struct ieee80211vap *vap)
{
	const struct ieee80211_node *bss = vap->iv_bss;
	return bss->ni_chan != IEEE80211_CHAN_ANYC &&
	    IEEE80211_IS_CHAN_ANYG(bss->ni_chan);
}

static int
isvapht(const struct ieee80211vap *vap)
{
	const struct ieee80211_node *bss = vap->iv_bss;
	return bss->ni_chan != IEEE80211_CHAN_ANYC &&
	    IEEE80211_IS_CHAN_HT(bss->ni_chan);
}

/*
 * Dummy ioctl set handler so the linker set is defined.
 */
static int
dummy_ioctl_set(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	return ENOSYS;
}
IEEE80211_IOCTL_SET(dummy, dummy_ioctl_set);

static int
ieee80211_ioctl_setdefault(struct ieee80211vap *vap, struct ieee80211req *ireq)
{
	ieee80211_ioctl_setfunc * const *set;
	int error;

	SET_FOREACH(set, ieee80211_ioctl_setset) {
		error = (*set)(vap, ireq);
		if (error != ENOSYS)
			return error;
	}
	return EINVAL;
}

static int
ieee80211_ioctl_set80211(struct ieee80211vap *vap, u_long cmd, struct ieee80211req *ireq)
{
	struct ieee80211com *ic = vap->iv_ic;
	int error;
	const struct ieee80211_authenticator *auth;
	uint8_t tmpkey[IEEE80211_KEYBUF_SIZE];
	char tmpssid[IEEE80211_NWID_LEN];
	uint8_t tmpbssid[IEEE80211_ADDR_LEN];
	struct ieee80211_key *k;
	u_int kid;
	uint32_t flags;

	error = 0;
	switch (ireq->i_type) {
	case IEEE80211_IOC_SSID:
		if (ireq->i_val != 0 ||
		    ireq->i_len > IEEE80211_NWID_LEN)
			return EINVAL;
		error = copyin(ireq->i_data, tmpssid, ireq->i_len);
		if (error)
			break;
		memset(vap->iv_des_ssid[0].ssid, 0, IEEE80211_NWID_LEN);
		vap->iv_des_ssid[0].len = ireq->i_len;
		memcpy(vap->iv_des_ssid[0].ssid, tmpssid, ireq->i_len);
		vap->iv_des_nssid = (ireq->i_len > 0);
		error = ENETRESET;
		break;
	case IEEE80211_IOC_WEP:
		switch (ireq->i_val) {
		case IEEE80211_WEP_OFF:
			vap->iv_flags &= ~IEEE80211_F_PRIVACY;
			vap->iv_flags &= ~IEEE80211_F_DROPUNENC;
			break;
		case IEEE80211_WEP_ON:
			vap->iv_flags |= IEEE80211_F_PRIVACY;
			vap->iv_flags |= IEEE80211_F_DROPUNENC;
			break;
		case IEEE80211_WEP_MIXED:
			vap->iv_flags |= IEEE80211_F_PRIVACY;
			vap->iv_flags &= ~IEEE80211_F_DROPUNENC;
			break;
		}
		error = ENETRESET;
		break;
	case IEEE80211_IOC_WEPKEY:
		kid = (u_int) ireq->i_val;
		if (kid >= IEEE80211_WEP_NKID)
			return EINVAL;
		k = &vap->iv_nw_keys[kid];
		if (ireq->i_len == 0) {
			/* zero-len =>'s delete any existing key */
			(void) ieee80211_crypto_delkey(vap, k);
			break;
		}
		if (ireq->i_len > sizeof(tmpkey))
			return EINVAL;
		memset(tmpkey, 0, sizeof(tmpkey));
		error = copyin(ireq->i_data, tmpkey, ireq->i_len);
		if (error)
			break;
		ieee80211_key_update_begin(vap);
		k->wk_keyix = kid;	/* NB: force fixed key id */
		if (ieee80211_crypto_newkey(vap, IEEE80211_CIPHER_WEP,
		    IEEE80211_KEY_XMIT | IEEE80211_KEY_RECV, k)) {
			k->wk_keylen = ireq->i_len;
			memcpy(k->wk_key, tmpkey, sizeof(tmpkey));
			IEEE80211_ADDR_COPY(k->wk_macaddr, vap->iv_myaddr);
			if  (!ieee80211_crypto_setkey(vap, k))
				error = EINVAL;
		} else
			error = EINVAL;
		ieee80211_key_update_end(vap);
		break;
	case IEEE80211_IOC_WEPTXKEY:
		kid = (u_int) ireq->i_val;
		if (kid >= IEEE80211_WEP_NKID &&
		    (uint16_t) kid != IEEE80211_KEYIX_NONE)
			return EINVAL;
		/*
		 * Firmware devices may need to be told about an explicit
		 * key index here, versus just inferring it from the
		 * key set / change.  Since we may also need to pause
		 * things like transmit before the key is updated,
		 * give the driver a chance to flush things by tying
		 * into key update begin/end.
		 */
		ieee80211_key_update_begin(vap);
		ieee80211_crypto_set_deftxkey(vap, kid);
		ieee80211_key_update_end(vap);
		break;
	case IEEE80211_IOC_AUTHMODE:
		switch (ireq->i_val) {
		case IEEE80211_AUTH_WPA:
		case IEEE80211_AUTH_8021X:	/* 802.1x */
		case IEEE80211_AUTH_OPEN:	/* open */
		case IEEE80211_AUTH_SHARED:	/* shared-key */
		case IEEE80211_AUTH_AUTO:	/* auto */
			auth = ieee80211_authenticator_get(ireq->i_val);
			if (auth == NULL)
				return EINVAL;
			break;
		default:
			return EINVAL;
		}
		switch (ireq->i_val) {
		case IEEE80211_AUTH_WPA:	/* WPA w/ 802.1x */
			vap->iv_flags |= IEEE80211_F_PRIVACY;
			ireq->i_val = IEEE80211_AUTH_8021X;
			break;
		case IEEE80211_AUTH_OPEN:	/* open */
			vap->iv_flags &= ~(IEEE80211_F_WPA|IEEE80211_F_PRIVACY);
			break;
		case IEEE80211_AUTH_SHARED:	/* shared-key */
		case IEEE80211_AUTH_8021X:	/* 802.1x */
			vap->iv_flags &= ~IEEE80211_F_WPA;
			/* both require a key so mark the PRIVACY capability */
			vap->iv_flags |= IEEE80211_F_PRIVACY;
			break;
		case IEEE80211_AUTH_AUTO:	/* auto */
			vap->iv_flags &= ~IEEE80211_F_WPA;
			/* XXX PRIVACY handling? */
			/* XXX what's the right way to do this? */
			break;
		}
		/* NB: authenticator attach/detach happens on state change */
		vap->iv_bss->ni_authmode = ireq->i_val;
		/* XXX mixed/mode/usage? */
		vap->iv_auth = auth;
		error = ENETRESET;
		break;
	case IEEE80211_IOC_CHANNEL:
		error = ieee80211_ioctl_setchannel(vap, ireq);
		break;
	case IEEE80211_IOC_POWERSAVE:
		switch (ireq->i_val) {
		case IEEE80211_POWERSAVE_OFF:
			if (vap->iv_flags & IEEE80211_F_PMGTON) {
				ieee80211_syncflag(vap, -IEEE80211_F_PMGTON);
				error = ERESTART;
			}
			break;
		case IEEE80211_POWERSAVE_ON:
			if ((vap->iv_caps & IEEE80211_C_PMGT) == 0)
				error = EOPNOTSUPP;
			else if ((vap->iv_flags & IEEE80211_F_PMGTON) == 0) {
				ieee80211_syncflag(vap, IEEE80211_F_PMGTON);
				error = ERESTART;
			}
			break;
		default:
			error = EINVAL;
			break;
		}
		break;
	case IEEE80211_IOC_POWERSAVESLEEP:
		if (ireq->i_val < 0)
			return EINVAL;
		ic->ic_lintval = ireq->i_val;
		error = ERESTART;
		break;
	case IEEE80211_IOC_RTSTHRESHOLD:
		if (!(IEEE80211_RTS_MIN <= ireq->i_val &&
		      ireq->i_val <= IEEE80211_RTS_MAX))
			return EINVAL;
		vap->iv_rtsthreshold = ireq->i_val;
		error = ERESTART;
		break;
	case IEEE80211_IOC_PROTMODE:
		if (ireq->i_val > IEEE80211_PROT_RTSCTS)
			return EINVAL;
		ic->ic_protmode = (enum ieee80211_protmode)ireq->i_val;
		/* NB: if not operating in 11g this can wait */
		if (ic->ic_bsschan != IEEE80211_CHAN_ANYC &&
		    IEEE80211_IS_CHAN_ANYG(ic->ic_bsschan))
			error = ERESTART;
		break;
	case IEEE80211_IOC_TXPOWER:
		if ((ic->ic_caps & IEEE80211_C_TXPMGT) == 0)
			return EOPNOTSUPP;
		if (!(IEEE80211_TXPOWER_MIN <= ireq->i_val &&
		      ireq->i_val <= IEEE80211_TXPOWER_MAX))
			return EINVAL;
		ic->ic_txpowlimit = ireq->i_val;
		error = ERESTART;
		break;
	case IEEE80211_IOC_ROAMING:
		if (!(IEEE80211_ROAMING_DEVICE <= ireq->i_val &&
		    ireq->i_val <= IEEE80211_ROAMING_MANUAL))
			return EINVAL;
		vap->iv_roaming = (enum ieee80211_roamingmode)ireq->i_val;
		/* XXXX reset? */
		break;
	case IEEE80211_IOC_PRIVACY:
		if (ireq->i_val) {
			/* XXX check for key state? */
			vap->iv_flags |= IEEE80211_F_PRIVACY;
		} else
			vap->iv_flags &= ~IEEE80211_F_PRIVACY;
		/* XXX ERESTART? */
		break;
	case IEEE80211_IOC_DROPUNENCRYPTED:
		if (ireq->i_val)
			vap->iv_flags |= IEEE80211_F_DROPUNENC;
		else
			vap->iv_flags &= ~IEEE80211_F_DROPUNENC;
		/* XXX ERESTART? */
		break;
	case IEEE80211_IOC_WPAKEY:
		error = ieee80211_ioctl_setkey(vap, ireq);
		break;
	case IEEE80211_IOC_DELKEY:
		error = ieee80211_ioctl_delkey(vap, ireq);
		break;
	case IEEE80211_IOC_MLME:
		error = ieee80211_ioctl_setmlme(vap, ireq);
		break;
	case IEEE80211_IOC_COUNTERMEASURES:
		if (ireq->i_val) {
			if ((vap->iv_flags & IEEE80211_F_WPA) == 0)
				return EOPNOTSUPP;
			vap->iv_flags |= IEEE80211_F_COUNTERM;
		} else
			vap->iv_flags &= ~IEEE80211_F_COUNTERM;
		/* XXX ERESTART? */
		break;
	case IEEE80211_IOC_WPA:
		if (ireq->i_val > 3)
			return EINVAL;
		/* XXX verify ciphers available */
		flags = vap->iv_flags & ~IEEE80211_F_WPA;
		switch (ireq->i_val) {
		case 0:
			/* wpa_supplicant calls this to clear the WPA config */
			break;
		case 1:
			if (!(vap->iv_caps & IEEE80211_C_WPA1))
				return EOPNOTSUPP;
			flags |= IEEE80211_F_WPA1;
			break;
		case 2:
			if (!(vap->iv_caps & IEEE80211_C_WPA2))
				return EOPNOTSUPP;
			flags |= IEEE80211_F_WPA2;
			break;
		case 3:
			if ((vap->iv_caps & IEEE80211_C_WPA) != IEEE80211_C_WPA)
				return EOPNOTSUPP;
			flags |= IEEE80211_F_WPA1 | IEEE80211_F_WPA2;
			break;
		default:	/*  Can't set any -> error */
			return EOPNOTSUPP;
		}
		vap->iv_flags = flags;
		error = ERESTART;	/* NB: can change beacon frame */
		break;
	case IEEE80211_IOC_WME:
		if (ireq->i_val) {
			if ((vap->iv_caps & IEEE80211_C_WME) == 0)
				return EOPNOTSUPP;
			ieee80211_syncflag(vap, IEEE80211_F_WME);
		} else
			ieee80211_syncflag(vap, -IEEE80211_F_WME);
		error = ERESTART;	/* NB: can change beacon frame */
		break;
	case IEEE80211_IOC_HIDESSID:
		if (ireq->i_val)
			vap->iv_flags |= IEEE80211_F_HIDESSID;
		else
			vap->iv_flags &= ~IEEE80211_F_HIDESSID;
		error = ERESTART;		/* XXX ENETRESET? */
		break;
	case IEEE80211_IOC_APBRIDGE:
		if (ireq->i_val == 0)
			vap->iv_flags |= IEEE80211_F_NOBRIDGE;
		else
			vap->iv_flags &= ~IEEE80211_F_NOBRIDGE;
		break;
	case IEEE80211_IOC_BSSID:
		if (ireq->i_len != sizeof(tmpbssid))
			return EINVAL;
		error = copyin(ireq->i_data, tmpbssid, ireq->i_len);
		if (error)
			break;
		IEEE80211_ADDR_COPY(vap->iv_des_bssid, tmpbssid);
		if (IEEE80211_ADDR_EQ(vap->iv_des_bssid, zerobssid))
			vap->iv_flags &= ~IEEE80211_F_DESBSSID;
		else
			vap->iv_flags |= IEEE80211_F_DESBSSID;
		error = ENETRESET;
		break;
	case IEEE80211_IOC_CHANLIST:
		error = ieee80211_ioctl_setchanlist(vap, ireq);
		break;
#define	OLD_IEEE80211_IOC_SCAN_REQ	23
#ifdef OLD_IEEE80211_IOC_SCAN_REQ
	case OLD_IEEE80211_IOC_SCAN_REQ:
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
			"%s: active scan request\n", __func__);
		/*
		 * If we are in INIT state then the driver has never
		 * had a chance to setup hardware state to do a scan;
		 * use the state machine to get us up the SCAN state.
		 * Otherwise just invoke the scan machinery to start
		 * a one-time scan.
		 */
		if (vap->iv_state == IEEE80211_S_INIT)
			ieee80211_new_state(vap, IEEE80211_S_SCAN, 0);
		else
			(void) ieee80211_start_scan(vap,
				IEEE80211_SCAN_ACTIVE |
				IEEE80211_SCAN_NOPICK |
				IEEE80211_SCAN_ONCE,
				IEEE80211_SCAN_FOREVER, 0, 0,
				/* XXX use ioctl params */
				vap->iv_des_nssid, vap->iv_des_ssid);
		break;
#endif /* OLD_IEEE80211_IOC_SCAN_REQ */
	case IEEE80211_IOC_SCAN_REQ:
		error = ieee80211_ioctl_scanreq(vap, ireq);
		break;
	case IEEE80211_IOC_SCAN_CANCEL:
		IEEE80211_DPRINTF(vap, IEEE80211_MSG_SCAN,
		    "%s: cancel scan\n", __func__);
		ieee80211_cancel_scan(vap);
		break;
	case IEEE80211_IOC_HTCONF:
		if (ireq->i_val & 1)
			ieee80211_syncflag_ht(vap, IEEE80211_FHT_HT);
		else
			ieee80211_syncflag_ht(vap, -IEEE80211_FHT_HT);
		if (ireq->i_val & 2)
			ieee80211_syncflag_ht(vap, IEEE80211_FHT_USEHT40);
		else
			ieee80211_syncflag_ht(vap, -IEEE80211_FHT_USEHT40);
		error = ENETRESET;
		break;
	case IEEE80211_IOC_ADDMAC:
	case IEEE80211_IOC_DELMAC:
		error = ieee80211_ioctl_macmac(vap, ireq);
		break;
	case IEEE80211_IOC_MACCMD:
		error = ieee80211_ioctl_setmaccmd(vap, ireq);
		break;
	case IEEE80211_IOC_STA_STATS:
		error = ieee80211_ioctl_setstastats(vap, ireq);
		break;
	case IEEE80211_IOC_STA_TXPOW:
		error = ieee80211_ioctl_setstatxpow(vap, ireq);
		break;
	case IEEE80211_IOC_WME_CWMIN:		/* WME: CWmin */
	case IEEE80211_IOC_WME_CWMAX:		/* WME: CWmax */
	case IEEE80211_IOC_WME_AIFS:		/* WME: AIFS */
	case IEEE80211_IOC_WME_TXOPLIMIT:	/* WME: txops limit */
	case IEEE80211_IOC_WME_ACM:		/* WME: ACM (bss only) */
	case IEEE80211_IOC_WME_ACKPOLICY:	/* WME: ACK policy (!bss only) */
		error = ieee80211_ioctl_setwmeparam(vap, ireq);
		break;
	case IEEE80211_IOC_DTIM_PERIOD:
		if (vap->iv_opmode != IEEE80211_M_HOSTAP &&
		    vap->iv_opmode != IEEE80211_M_MBSS &&
		    vap->iv_opmode != IEEE80211_M_IBSS)
			return EINVAL;
		if (IEEE80211_DTIM_MIN <= ireq->i_val &&
		    ireq->i_val <= IEEE80211_DTIM_MAX) {
			vap->iv_dtim_period = ireq->i_val;
			error = ENETRESET;		/* requires restart */
		} else
			error = EINVAL;
		break;
	case IEEE80211_IOC_BEACON_INTERVAL:
		if (vap->iv_opmode != IEEE80211_M_HOSTAP &&
		    vap->iv_opmode != IEEE80211_M_MBSS &&
		    vap->iv_opmode != IEEE80211_M_IBSS)
			return EINVAL;
		if (IEEE80211_BINTVAL_MIN <= ireq->i_val &&
		    ireq->i_val <= IEEE80211_BINTVAL_MAX) {
			ic->ic_bintval = ireq->i_val;
			error = ENETRESET;		/* requires restart */
		} else
			error = EINVAL;
		break;
	case IEEE80211_IOC_PUREG:
		if (ireq->i_val)
			vap->iv_flags |= IEEE80211_F_PUREG;
		else
			vap->iv_flags &= ~IEEE80211_F_PUREG;
		/* NB: reset only if we're operating on an 11g channel */
		if (isvap11g(vap))
			error = ENETRESET;
		break;
	case IEEE80211_IOC_QUIET:
		vap->iv_quiet= ireq->i_val;
		break;
	case IEEE80211_IOC_QUIET_COUNT:
		vap->iv_quiet_count=ireq->i_val;
		break;
	case IEEE80211_IOC_QUIET_PERIOD:
		vap->iv_quiet_period=ireq->i_val;
		break;
	case IEEE80211_IOC_QUIET_OFFSET:
		vap->iv_quiet_offset=ireq->i_val;
		break;
	case IEEE80211_IOC_QUIET_DUR:
		if(ireq->i_val < vap->iv_bss->ni_intval)
			vap->iv_quiet_duration = ireq->i_val;
		else
			error = EINVAL;
		break;
	case IEEE80211_IOC_BGSCAN:
		if (ireq->i_val) {
			if ((vap->iv_caps & IEEE80211_C_BGSCAN) == 0)
				return EOPNOTSUPP;
			vap->iv_flags |= IEEE80211_F_BGSCAN;
		} else
			vap->iv_flags &= ~IEEE80211_F_BGSCAN;
		break;
	case IEEE80211_IOC_BGSCAN_IDLE:
		if (ireq->i_val >= IEEE80211_BGSCAN_IDLE_MIN)
			vap->iv_bgscanidle = ireq->i_val*hz/1000;
		else
			error = EINVAL;
		break;
	case IEEE80211_IOC_BGSCAN_INTERVAL:
		if (ireq->i_val >= IEEE80211_BGSCAN_INTVAL_MIN)
			vap->iv_bgscanintvl = ireq->i_val*hz;
		else
			error = EINVAL;
		break;
	case IEEE80211_IOC_SCANVALID:
		if (ireq->i_val >= IEEE80211_SCAN_VALID_MIN)
			vap->iv_scanvalid = ireq->i_val*hz;
		else
			error = EINVAL;
		break;
	case IEEE80211_IOC_FRAGTHRESHOLD:
		if ((vap->iv_caps & IEEE80211_C_TXFRAG) == 0 &&
		    ireq->i_val != IEEE80211_FRAG_MAX)
			return EOPNOTSUPP;
		if (!(IEEE80211_FRAG_MIN <= ireq->i_val &&
		      ireq->i_val <= IEEE80211_FRAG_MAX))
			return EINVAL;
		vap->iv_fragthreshold = ireq->i_val;
		error = ERESTART;
		break;
	case IEEE80211_IOC_BURST:
		if (ireq->i_val) {
			if ((vap->iv_caps & IEEE80211_C_BURST) == 0)
				return EOPNOTSUPP;
			ieee80211_syncflag(vap, IEEE80211_F_BURST);
		} else
			ieee80211_syncflag(vap, -IEEE80211_F_BURST);
		error = ERESTART;
		break;
	case IEEE80211_IOC_BMISSTHRESHOLD:
		if (!(IEEE80211_HWBMISS_MIN <= ireq->i_val &&
		      ireq->i_val <= IEEE80211_HWBMISS_MAX))
			return EINVAL;
		vap->iv_bmissthreshold = ireq->i_val;
		error = ERESTART;
		break;
	case IEEE80211_IOC_CURCHAN:
		error = ieee80211_ioctl_setcurchan(vap, ireq);
		break;
	case IEEE80211_IOC_SHORTGI:
		if (ireq->i_val) {
#define	IEEE80211_HTCAP_SHORTGI \
	(IEEE80211_HTCAP_SHORTGI20 | IEEE80211_HTCAP_SHORTGI40)
			if (((ireq->i_val ^ vap->iv_htcaps) & IEEE80211_HTCAP_SHORTGI) != 0)
				return EINVAL;
			if (ireq->i_val & IEEE80211_HTCAP_SHORTGI20)
				vap->iv_flags_ht |= IEEE80211_FHT_SHORTGI20;
			if (ireq->i_val & IEEE80211_HTCAP_SHORTGI40)
				vap->iv_flags_ht |= IEEE80211_FHT_SHORTGI40;
#undef IEEE80211_HTCAP_SHORTGI
		} else
			vap->iv_flags_ht &=
			    ~(IEEE80211_FHT_SHORTGI20 | IEEE80211_FHT_SHORTGI40);
		error = ERESTART;
		break;
	case IEEE80211_IOC_AMPDU:
		if (ireq->i_val && (vap->iv_htcaps & IEEE80211_HTC_AMPDU) == 0)
			return EINVAL;
		if (ireq->i_val & 1)
			vap->iv_flags_ht |= IEEE80211_FHT_AMPDU_TX;
		else
			vap->iv_flags_ht &= ~IEEE80211_FHT_AMPDU_TX;
		if (ireq->i_val & 2)
			vap->iv_flags_ht |= IEEE80211_FHT_AMPDU_RX;
		else
			vap->iv_flags_ht &= ~IEEE80211_FHT_AMPDU_RX;
		/* NB: reset only if we're operating on an 11n channel */
		if (isvapht(vap))
			error = ERESTART;
		break;
	case IEEE80211_IOC_AMPDU_LIMIT:
		/* XXX TODO: figure out ampdu_limit versus ampdu_rxmax */
		if (!(IEEE80211_HTCAP_MAXRXAMPDU_8K <= ireq->i_val &&
		      ireq->i_val <= IEEE80211_HTCAP_MAXRXAMPDU_64K))
			return EINVAL;
		if (vap->iv_opmode == IEEE80211_M_HOSTAP)
			vap->iv_ampdu_rxmax = ireq->i_val;
		else
			vap->iv_ampdu_limit = ireq->i_val;
		error = ERESTART;
		break;
	case IEEE80211_IOC_AMPDU_DENSITY:
		if (!(IEEE80211_HTCAP_MPDUDENSITY_NA <= ireq->i_val &&
		      ireq->i_val <= IEEE80211_HTCAP_MPDUDENSITY_16))
			return EINVAL;
		vap->iv_ampdu_density = ireq->i_val;
		error = ERESTART;
		break;
	case IEEE80211_IOC_AMSDU:
		if (ireq->i_val && (vap->iv_htcaps & IEEE80211_HTC_AMSDU) == 0)
			return EINVAL;
		if (ireq->i_val & 1)
			vap->iv_flags_ht |= IEEE80211_FHT_AMSDU_TX;
		else
			vap->iv_flags_ht &= ~IEEE80211_FHT_AMSDU_TX;
		if (ireq->i_val & 2)
			vap->iv_flags_ht |= IEEE80211_FHT_AMSDU_RX;
		else
			vap->iv_flags_ht &= ~IEEE80211_FHT_AMSDU_RX;
		/* NB: reset only if we're operating on an 11n channel */
		if (isvapht(vap))
			error = ERESTART;
		break;
	case IEEE80211_IOC_AMSDU_LIMIT:
		/* XXX validate */
		vap->iv_amsdu_limit = ireq->i_val;	/* XXX truncation? */
		break;
	case IEEE80211_IOC_PUREN:
		if (ireq->i_val) {
			if ((vap->iv_flags_ht & IEEE80211_FHT_HT) == 0)
				return EINVAL;
			vap->iv_flags_ht |= IEEE80211_FHT_PUREN;
		} else
			vap->iv_flags_ht &= ~IEEE80211_FHT_PUREN;
		/* NB: reset only if we're operating on an 11n channel */
		if (isvapht(vap))
			error = ERESTART;
		break;
	case IEEE80211_IOC_DOTH:
		if (ireq->i_val) {
#if 0
			/* XXX no capability */
			if ((vap->iv_caps & IEEE80211_C_DOTH) == 0)
				return EOPNOTSUPP;
#endif
			vap->iv_flags |= IEEE80211_F_DOTH;
		} else
			vap->iv_flags &= ~IEEE80211_F_DOTH;
		error = ENETRESET;
		break;
	case IEEE80211_IOC_REGDOMAIN:
		error = ieee80211_ioctl_setregdomain(vap, ireq);
		break;
	case IEEE80211_IOC_ROAM:
		error = ieee80211_ioctl_setroam(vap, ireq);
		break;
	case IEEE80211_IOC_TXPARAMS:
		error = ieee80211_ioctl_settxparams(vap, ireq);
		break;
	case IEEE80211_IOC_HTCOMPAT:
		if (ireq->i_val) {
			if ((vap->iv_flags_ht & IEEE80211_FHT_HT) == 0)
				return EOPNOTSUPP;
			vap->iv_flags_ht |= IEEE80211_FHT_HTCOMPAT;
		} else
			vap->iv_flags_ht &= ~IEEE80211_FHT_HTCOMPAT;
		/* NB: reset only if we're operating on an 11n channel */
		if (isvapht(vap))
			error = ERESTART;
		break;
	case IEEE80211_IOC_DWDS:
		if (ireq->i_val) {
			/* NB: DWDS only makes sense for WDS-capable devices */
			if ((ic->ic_caps & IEEE80211_C_WDS) == 0)
				return EOPNOTSUPP;
			/* NB: DWDS is used only with ap+sta vaps */
			if (vap->iv_opmode != IEEE80211_M_HOSTAP &&
			    vap->iv_opmode != IEEE80211_M_STA)
				return EINVAL;
			vap->iv_flags |= IEEE80211_F_DWDS;
			if (vap->iv_opmode == IEEE80211_M_STA)
				vap->iv_flags_ext |= IEEE80211_FEXT_4ADDR;
		} else {
			vap->iv_flags &= ~IEEE80211_F_DWDS;
			if (vap->iv_opmode == IEEE80211_M_STA)
				vap->iv_flags_ext &= ~IEEE80211_FEXT_4ADDR;
		}
		break;
	case IEEE80211_IOC_INACTIVITY:
		if (ireq->i_val)
			vap->iv_flags_ext |= IEEE80211_FEXT_INACT;
		else
			vap->iv_flags_ext &= ~IEEE80211_FEXT_INACT;
		break;
	case IEEE80211_IOC_APPIE:
		error = ieee80211_ioctl_setappie(vap, ireq);
		break;
	case IEEE80211_IOC_WPS:
		if (ireq->i_val) {
			if ((vap->iv_caps & IEEE80211_C_WPA) == 0)
				return EOPNOTSUPP;
			vap->iv_flags_ext |= IEEE80211_FEXT_WPS;
		} else
			vap->iv_flags_ext &= ~IEEE80211_FEXT_WPS;
		break;
	case IEEE80211_IOC_TSN:
		if (ireq->i_val) {
			if ((vap->iv_caps & IEEE80211_C_WPA) == 0)
				return EOPNOTSUPP;
			vap->iv_flags_ext |= IEEE80211_FEXT_TSN;
		} else
			vap->iv_flags_ext &= ~IEEE80211_FEXT_TSN;
		break;
	case IEEE80211_IOC_CHANSWITCH:
		error = ieee80211_ioctl_chanswitch(vap, ireq);
		break;
	case IEEE80211_IOC_DFS:
		if (ireq->i_val) {
			if ((vap->iv_caps & IEEE80211_C_DFS) == 0)
				return EOPNOTSUPP;
			/* NB: DFS requires 11h support */
			if ((vap->iv_flags & IEEE80211_F_DOTH) == 0)
				return EINVAL;
			vap->iv_flags_ext |= IEEE80211_FEXT_DFS;
		} else
			vap->iv_flags_ext &= ~IEEE80211_FEXT_DFS;
		break;
	case IEEE80211_IOC_DOTD:
		if (ireq->i_val)
			vap->iv_flags_ext |= IEEE80211_FEXT_DOTD;
		else
			vap->iv_flags_ext &= ~IEEE80211_FEXT_DOTD;
		if (vap->iv_opmode == IEEE80211_M_STA)
			error = ENETRESET;
		break;
	case IEEE80211_IOC_HTPROTMODE:
		if (ireq->i_val > IEEE80211_PROT_RTSCTS)
			return EINVAL;
		ic->ic_htprotmode = ireq->i_val ?
		    IEEE80211_PROT_RTSCTS : IEEE80211_PROT_NONE;
		/* NB: if not operating in 11n this can wait */
		if (isvapht(vap))
			error = ERESTART;
		break;
	case IEEE80211_IOC_STA_VLAN:
		error = ieee80211_ioctl_setstavlan(vap, ireq);
		break;
	case IEEE80211_IOC_SMPS:
		if ((ireq->i_val &~ IEEE80211_HTCAP_SMPS) != 0 ||
		    ireq->i_val == 0x0008)	/* value of 2 is reserved */
			return EINVAL;
		if (ireq->i_val != IEEE80211_HTCAP_SMPS_OFF &&
		    (vap->iv_htcaps & IEEE80211_HTC_SMPS) == 0)
			return EOPNOTSUPP;
		vap->iv_htcaps = (vap->iv_htcaps &~ IEEE80211_HTCAP_SMPS) |
			ireq->i_val;
		/* NB: if not operating in 11n this can wait */
		if (isvapht(vap))
			error = ERESTART;
		break;
	case IEEE80211_IOC_RIFS:
		if (ireq->i_val != 0) {
			if ((vap->iv_htcaps & IEEE80211_HTC_RIFS) == 0)
				return EOPNOTSUPP;
			vap->iv_flags_ht |= IEEE80211_FHT_RIFS;
		} else
			vap->iv_flags_ht &= ~IEEE80211_FHT_RIFS;
		/* NB: if not operating in 11n this can wait */
		if (isvapht(vap))
			error = ERESTART;
		break;
	case IEEE80211_IOC_STBC:
		/* Check if we can do STBC TX/RX before changing the setting */
		if ((ireq->i_val & 1) &&
		    ((vap->iv_htcaps & IEEE80211_HTCAP_TXSTBC) == 0))
			return EOPNOTSUPP;
		if ((ireq->i_val & 2) &&
		    ((vap->iv_htcaps & IEEE80211_HTCAP_RXSTBC) == 0))
			return EOPNOTSUPP;

		/* TX */
		if (ireq->i_val & 1)
			vap->iv_flags_ht |= IEEE80211_FHT_STBC_TX;
		else
			vap->iv_flags_ht &= ~IEEE80211_FHT_STBC_TX;

		/* RX */
		if (ireq->i_val & 2)
			vap->iv_flags_ht |= IEEE80211_FHT_STBC_RX;
		else
			vap->iv_flags_ht &= ~IEEE80211_FHT_STBC_RX;

		/* NB: reset only if we're operating on an 11n channel */
		if (isvapht(vap))
			error = ERESTART;
		break;
	case IEEE80211_IOC_LDPC:
		/* Check if we can do LDPC TX/RX before changing the setting */
		if ((ireq->i_val & 1) &&
		    (vap->iv_htcaps & IEEE80211_HTC_TXLDPC) == 0)
			return EOPNOTSUPP;
		if ((ireq->i_val & 2) &&
		    (vap->iv_htcaps & IEEE80211_HTCAP_LDPC) == 0)
			return EOPNOTSUPP;

		/* TX */
		if (ireq->i_val & 1)
			vap->iv_flags_ht |= IEEE80211_FHT_LDPC_TX;
		else
			vap->iv_flags_ht &= ~IEEE80211_FHT_LDPC_TX;

		/* RX */
		if (ireq->i_val & 2)
			vap->iv_flags_ht |= IEEE80211_FHT_LDPC_RX;
		else
			vap->iv_flags_ht &= ~IEEE80211_FHT_LDPC_RX;

		/* NB: reset only if we're operating on an 11n channel */
		if (isvapht(vap))
			error = ERESTART;
		break;

	/* VHT */
	case IEEE80211_IOC_VHTCONF:
		if (ireq->i_val & 1)
			ieee80211_syncflag_vht(vap, IEEE80211_FVHT_VHT);
		else
			ieee80211_syncflag_vht(vap, -IEEE80211_FVHT_VHT);

		if (ireq->i_val & 2)
			ieee80211_syncflag_vht(vap, IEEE80211_FVHT_USEVHT40);
		else
			ieee80211_syncflag_vht(vap, -IEEE80211_FVHT_USEVHT40);

		if (ireq->i_val & 4)
			ieee80211_syncflag_vht(vap, IEEE80211_FVHT_USEVHT80);
		else
			ieee80211_syncflag_vht(vap, -IEEE80211_FVHT_USEVHT80);

		if (ireq->i_val & 8)
			ieee80211_syncflag_vht(vap, IEEE80211_FVHT_USEVHT80P80);
		else
			ieee80211_syncflag_vht(vap, -IEEE80211_FVHT_USEVHT80P80);

		if (ireq->i_val & 16)
			ieee80211_syncflag_vht(vap, IEEE80211_FVHT_USEVHT160);
		else
			ieee80211_syncflag_vht(vap, -IEEE80211_FVHT_USEVHT160);

		error = ENETRESET;
		break;

	default:
		error = ieee80211_ioctl_setdefault(vap, ireq);
		break;
	}
	/*
	 * The convention is that ENETRESET means an operation
	 * requires a complete re-initialization of the device (e.g.
	 * changing something that affects the association state).
	 * ERESTART means the request may be handled with only a
	 * reload of the hardware state.  We hand ERESTART requests
	 * to the iv_reset callback so the driver can decide.  If
	 * a device does not fillin iv_reset then it defaults to one
	 * that returns ENETRESET.  Otherwise a driver may return
	 * ENETRESET (in which case a full reset will be done) or
	 * 0 to mean there's no need to do anything (e.g. when the
	 * change has no effect on the driver/device).
	 */
	if (error == ERESTART)
		error = IFNET_IS_UP_RUNNING(vap->iv_ifp) ?
		    vap->iv_reset(vap, ireq->i_type) : 0;
	if (error == ENETRESET) {
		/* XXX need to re-think AUTO handling */
		if (IS_UP_AUTO(vap))
			ieee80211_init(vap);
		error = 0;
	}
	return error;
}

int
ieee80211_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ieee80211vap *vap = ifp->if_softc;
	struct ieee80211com *ic = vap->iv_ic;
	int error = 0, wait = 0, ic_used;
	struct ifreq *ifr;
	struct ifaddr *ifa;			/* XXX */

	ic_used = (cmd != SIOCSIFMTU && cmd != SIOCG80211STATS);
	if (ic_used && (error = ieee80211_com_vincref(vap)) != 0)
		return (error);

	switch (cmd) {
	case SIOCSIFFLAGS:
		IEEE80211_LOCK(ic);
		if ((ifp->if_flags ^ vap->iv_ifflags) & IFF_PROMISC) {
			/*
			 * Enable promiscuous mode when:
			 * 1. Interface is not a member of bridge, or
			 * 2. Requested by user, or
			 * 3. In monitor (or adhoc-demo) mode.
			 */
			if (ifp->if_bridge == NULL ||
			    (ifp->if_flags & IFF_PPROMISC) != 0 ||
			    vap->iv_opmode == IEEE80211_M_MONITOR ||
			    (vap->iv_opmode == IEEE80211_M_AHDEMO &&
			    (vap->iv_caps & IEEE80211_C_TDMA) == 0)) {
				ieee80211_promisc(vap,
				    ifp->if_flags & IFF_PROMISC);
				vap->iv_ifflags ^= IFF_PROMISC;
			}
		}
		if ((ifp->if_flags ^ vap->iv_ifflags) & IFF_ALLMULTI) {
			ieee80211_allmulti(vap, ifp->if_flags & IFF_ALLMULTI);
			vap->iv_ifflags ^= IFF_ALLMULTI;
		}
		if (ifp->if_flags & IFF_UP) {
			/*
			 * Bring ourself up unless we're already operational.
			 * If we're the first vap and the parent is not up
			 * then it will automatically be brought up as a
			 * side-effect of bringing ourself up.
			 */
			if (vap->iv_state == IEEE80211_S_INIT) {
				if (ic->ic_nrunning == 0)
					wait = 1;
				ieee80211_start_locked(vap);
			}
		} else if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			/*
			 * Stop ourself.  If we are the last vap to be
			 * marked down the parent will also be taken down.
			 */
			if (ic->ic_nrunning == 1)
				wait = 1;
			ieee80211_stop_locked(vap);
		}
		IEEE80211_UNLOCK(ic);
		/* Wait for parent ioctl handler if it was queued */
		if (wait) {
			ieee80211_waitfor_parent(ic);

			/*
			 * Check if the MAC address was changed
			 * via SIOCSIFLLADDR ioctl.
			 *
			 * NB: device may be detached during initialization;
			 * use if_ioctl for existence check.
			 */
			if_addr_rlock(ifp);
			if (ifp->if_ioctl == ieee80211_ioctl &&
			    (ifp->if_flags & IFF_UP) == 0 &&
			    !IEEE80211_ADDR_EQ(vap->iv_myaddr, IF_LLADDR(ifp)))
				IEEE80211_ADDR_COPY(vap->iv_myaddr,
				    IF_LLADDR(ifp));
			if_addr_runlock(ifp);
		}
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		ieee80211_runtask(ic, &ic->ic_mcast_task);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		ifr = (struct ifreq *)data;
		error = ifmedia_ioctl(ifp, ifr, &vap->iv_media, cmd);
		break;
	case SIOCG80211:
		error = ieee80211_ioctl_get80211(vap, cmd,
				(struct ieee80211req *) data);
		break;
	case SIOCS80211:
		error = priv_check(curthread, PRIV_NET80211_MANAGE);
		if (error == 0)
			error = ieee80211_ioctl_set80211(vap, cmd,
					(struct ieee80211req *) data);
		break;
	case SIOCG80211STATS:
		ifr = (struct ifreq *)data;
		copyout(&vap->iv_stats, ifr_data_get_ptr(ifr),
		    sizeof (vap->iv_stats));
		break;
	case SIOCSIFMTU:
		ifr = (struct ifreq *)data;
		if (!(IEEE80211_MTU_MIN <= ifr->ifr_mtu &&
		    ifr->ifr_mtu <= IEEE80211_MTU_MAX))
			error = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		break;
	case SIOCSIFADDR:
		/*
		 * XXX Handle this directly so we can suppress if_init calls.
		 * XXX This should be done in ether_ioctl but for the moment
		 * XXX there are too many other parts of the system that
		 * XXX set IFF_UP and so suppress if_init being called when
		 * XXX it should be.
		 */
		ifa = (struct ifaddr *) data;
		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			if ((ifp->if_flags & IFF_UP) == 0) {
				ifp->if_flags |= IFF_UP;
				ifp->if_init(ifp->if_softc);
			}
			arp_ifinit(ifp, ifa);
			break;
#endif
		default:
			if ((ifp->if_flags & IFF_UP) == 0) {
				ifp->if_flags |= IFF_UP;
				ifp->if_init(ifp->if_softc);
			}
			break;
		}
		break;
	default:
		/*
		 * Pass unknown ioctls first to the driver, and if it
		 * returns ENOTTY, then to the generic Ethernet handler.
		 */
		if (ic->ic_ioctl != NULL &&
		    (error = ic->ic_ioctl(ic, cmd, data)) != ENOTTY)
			break;
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	if (ic_used)
		ieee80211_com_vdecref(vap);

	return (error);
}
