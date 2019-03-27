/*-
 * Copyright (c) 2017 Adrian Chadd <adrian@FreeBSD.org>
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
#ifdef __FreeBSD__
__FBSDID("$FreeBSD$");
#endif

/*
 * IEEE 802.11ac-2013 protocol support.
 */

#include "opt_inet.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h> 
#include <sys/endian.h>
 
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_action.h>
#include <net80211/ieee80211_input.h>
#include <net80211/ieee80211_vht.h>

/* define here, used throughout file */
#define	MS(_v, _f)	(((_v) & _f) >> _f##_S)
#define	SM(_v, _f)	(((_v) << _f##_S) & _f)

#define	ADDSHORT(frm, v) do {			\
	frm[0] = (v) & 0xff;			\
	frm[1] = (v) >> 8;			\
	frm += 2;				\
} while (0)
#define	ADDWORD(frm, v) do {			\
	frm[0] = (v) & 0xff;			\
	frm[1] = ((v) >> 8) & 0xff;		\
	frm[2] = ((v) >> 16) & 0xff;		\
	frm[3] = ((v) >> 24) & 0xff;		\
	frm += 4;				\
} while (0)

/*
 * Immediate TODO:
 *
 * + handle WLAN_ACTION_VHT_OPMODE_NOTIF and other VHT action frames
 * + ensure vhtinfo/vhtcap parameters correctly use the negotiated
 *   capabilities and ratesets
 * + group ID management operation
 */

/*
 * XXX TODO: handle WLAN_ACTION_VHT_OPMODE_NOTIF
 *
 * Look at mac80211/vht.c:ieee80211_vht_handle_opmode() for further details.
 */

static int
vht_recv_action_placeholder(struct ieee80211_node *ni,
    const struct ieee80211_frame *wh,
    const uint8_t *frm, const uint8_t *efrm)
{

#ifdef IEEE80211_DEBUG
	ieee80211_note(ni->ni_vap, "%s: called; fc=0x%.2x/0x%.2x",
	    __func__,
	    wh->i_fc[0],
	    wh->i_fc[1]);
#endif
	return (0);
}

static int
vht_send_action_placeholder(struct ieee80211_node *ni,
    int category, int action, void *arg0)
{

#ifdef IEEE80211_DEBUG
	ieee80211_note(ni->ni_vap, "%s: called; category=%d, action=%d",
	    __func__,
	    category,
	    action);
#endif
	return (EINVAL);
}

static void
ieee80211_vht_init(void)
{

	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_VHT,
	    WLAN_ACTION_VHT_COMPRESSED_BF, vht_recv_action_placeholder);
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_VHT,
	    WLAN_ACTION_VHT_GROUPID_MGMT, vht_recv_action_placeholder);
	ieee80211_recv_action_register(IEEE80211_ACTION_CAT_VHT,
	    WLAN_ACTION_VHT_OPMODE_NOTIF, vht_recv_action_placeholder);

	ieee80211_send_action_register(IEEE80211_ACTION_CAT_VHT,
	    WLAN_ACTION_VHT_COMPRESSED_BF, vht_send_action_placeholder);
	ieee80211_send_action_register(IEEE80211_ACTION_CAT_VHT,
	    WLAN_ACTION_VHT_GROUPID_MGMT, vht_send_action_placeholder);
	ieee80211_send_action_register(IEEE80211_ACTION_CAT_VHT,
	    WLAN_ACTION_VHT_OPMODE_NOTIF, vht_send_action_placeholder);
}

SYSINIT(wlan_vht, SI_SUB_DRIVERS, SI_ORDER_FIRST, ieee80211_vht_init, NULL);

void
ieee80211_vht_attach(struct ieee80211com *ic)
{
}

void
ieee80211_vht_detach(struct ieee80211com *ic)
{
}

void
ieee80211_vht_vattach(struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;

	if (! IEEE80211_CONF_VHT(ic))
		return;

	vap->iv_vhtcaps = ic->ic_vhtcaps;
	vap->iv_vhtextcaps = ic->ic_vhtextcaps;

	/* XXX assume VHT80 support; should really check vhtcaps */
	vap->iv_flags_vht =
	    IEEE80211_FVHT_VHT
	    | IEEE80211_FVHT_USEVHT40
	    | IEEE80211_FVHT_USEVHT80;
	/* XXX TODO: enable VHT80+80, VHT160 capabilities */

	memcpy(&vap->iv_vht_mcsinfo, &ic->ic_vht_mcsinfo,
	    sizeof(struct ieee80211_vht_mcs_info));
}

void
ieee80211_vht_vdetach(struct ieee80211vap *vap)
{
}

#if 0
static void
vht_announce(struct ieee80211com *ic, enum ieee80211_phymode mode)
{
}
#endif

static int
vht_mcs_to_num(int m)
{

	switch (m) {
	case IEEE80211_VHT_MCS_SUPPORT_0_7:
		return (7);
	case IEEE80211_VHT_MCS_SUPPORT_0_8:
		return (8);
	case IEEE80211_VHT_MCS_SUPPORT_0_9:
		return (9);
	default:
		return (0);
	}
}

void
ieee80211_vht_announce(struct ieee80211com *ic)
{
	int i, tx, rx;

	if (! IEEE80211_CONF_VHT(ic))
		return;

	/* Channel width */
	ic_printf(ic, "[VHT] Channel Widths: 20MHz, 40MHz, 80MHz");
	if (MS(ic->ic_vhtcaps, IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK) == 2)
		printf(" 80+80MHz");
	if (MS(ic->ic_vhtcaps, IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK) >= 1)
		printf(" 160MHz");
	printf("\n");

	/* Features */
	ic_printf(ic, "[VHT] Features: %b\n", ic->ic_vhtcaps,
	    IEEE80211_VHTCAP_BITS);

	/* For now, just 5GHz VHT.  Worry about 2GHz VHT later */
	for (i = 0; i < 7; i++) {
		/* Each stream is 2 bits */
		tx = (ic->ic_vht_mcsinfo.tx_mcs_map >> (2*i)) & 0x3;
		rx = (ic->ic_vht_mcsinfo.rx_mcs_map >> (2*i)) & 0x3;
		if (tx == 3 && rx == 3)
			continue;
		ic_printf(ic, "[VHT] NSS %d: TX MCS 0..%d, RX MCS 0..%d\n",
		    i + 1,
		    vht_mcs_to_num(tx),
		    vht_mcs_to_num(rx));
	}
}

void
ieee80211_vht_node_init(struct ieee80211_node *ni)
{

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N, ni,
	    "%s: called", __func__);
	ni->ni_flags |= IEEE80211_NODE_VHT;
}

void
ieee80211_vht_node_cleanup(struct ieee80211_node *ni)
{

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N, ni,
	    "%s: called", __func__);
	ni->ni_flags &= ~IEEE80211_NODE_VHT;
	ni->ni_vhtcap = 0;
	bzero(&ni->ni_vht_mcsinfo, sizeof(struct ieee80211_vht_mcs_info));
}

/*
 * Parse an 802.11ac VHT operation IE.
 */
void
ieee80211_parse_vhtopmode(struct ieee80211_node *ni, const uint8_t *ie)
{
	/* vht operation */
	ni->ni_vht_chanwidth = ie[2];
	ni->ni_vht_chan1 = ie[3];
	ni->ni_vht_chan2 = ie[4];
	ni->ni_vht_basicmcs = le16dec(ie + 5);

#if 0
	printf("%s: chan1=%d, chan2=%d, chanwidth=%d, basicmcs=0x%04x\n",
	    __func__,
	    ni->ni_vht_chan1,
	    ni->ni_vht_chan2,
	    ni->ni_vht_chanwidth,
	    ni->ni_vht_basicmcs);
#endif
}

/*
 * Parse an 802.11ac VHT capability IE.
 */
void
ieee80211_parse_vhtcap(struct ieee80211_node *ni, const uint8_t *ie)
{

	/* vht capability */
	ni->ni_vhtcap = le32dec(ie + 2);

	/* suppmcs */
	ni->ni_vht_mcsinfo.rx_mcs_map = le16dec(ie + 6);
	ni->ni_vht_mcsinfo.rx_highest = le16dec(ie + 8);
	ni->ni_vht_mcsinfo.tx_mcs_map = le16dec(ie + 10);
	ni->ni_vht_mcsinfo.tx_highest = le16dec(ie + 12);
}

int
ieee80211_vht_updateparams(struct ieee80211_node *ni,
    const uint8_t *vhtcap_ie,
    const uint8_t *vhtop_ie)
{

	//printf("%s: called\n", __func__);

	ieee80211_parse_vhtcap(ni, vhtcap_ie);
	ieee80211_parse_vhtopmode(ni, vhtop_ie);
	return (0);
}

void
ieee80211_setup_vht_rates(struct ieee80211_node *ni,
    const uint8_t *vhtcap_ie,
    const uint8_t *vhtop_ie)
{

	//printf("%s: called\n", __func__);
	/* XXX TODO */
}

void
ieee80211_vht_timeout(struct ieee80211com *ic)
{
}

void
ieee80211_vht_node_join(struct ieee80211_node *ni)
{

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N, ni,
	    "%s: called", __func__);
}

void
ieee80211_vht_node_leave(struct ieee80211_node *ni)
{

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_11N, ni,
	    "%s: called", __func__);
}

/*
 * Calculate the VHTCAP IE for a given node.
 *
 * This includes calculating the capability intersection based on the
 * current operating mode and intersection of the TX/RX MCS maps.
 *
 * The standard only makes it clear about MCS rate negotiation
 * and MCS basic rates (which must be a subset of the general
 * negotiated rates).  It doesn't make it clear that the AP should
 * figure out the minimum functional overlap with the STA and
 * support that.
 *
 * Note: this is in host order, not in 802.11 endian order.
 *
 * TODO: ensure I re-read 9.7.11 Rate Selection for VHT STAs.
 *
 * TODO: investigate what we should negotiate for MU-MIMO beamforming
 *       options.
 *
 * opmode is '1' for "vhtcap as if I'm a STA", 0 otherwise.
 */
void
ieee80211_vht_get_vhtcap_ie(struct ieee80211_node *ni,
    struct ieee80211_ie_vhtcap *vhtcap, int opmode)
{
	struct ieee80211vap *vap = ni->ni_vap;
//	struct ieee80211com *ic = vap->iv_ic;
	uint32_t val, val1, val2;
	uint32_t new_vhtcap;
	int i;

	vhtcap->ie = IEEE80211_ELEMID_VHT_CAP;
	vhtcap->len = sizeof(struct ieee80211_ie_vhtcap) - 2;

	/*
	 * Capabilities - it depends on whether we are a station
	 * or not.
	 */
	new_vhtcap = 0;

	/*
	 * Station - use our desired configuration based on
	 * local config, local device bits and the already-learnt
	 * vhtcap/vhtinfo IE in the node.
	 */

	/* Limit MPDU size to the smaller of the two */
	val2 = val1 = MS(vap->iv_vhtcaps, IEEE80211_VHTCAP_MAX_MPDU_MASK);
	if (opmode == 1) {
		val2 = MS(ni->ni_vhtcap, IEEE80211_VHTCAP_MAX_MPDU_MASK);
	}
	val = MIN(val1, val2);
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_MAX_MPDU_MASK);

	/* Limit supp channel config */
	val2 = val1 = MS(vap->iv_vhtcaps,
	    IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK);
	if (opmode == 1) {
		val2 = MS(ni->ni_vhtcap,
		    IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK);
	}
	if ((val2 == 2) &&
	    ((vap->iv_flags_vht & IEEE80211_FVHT_USEVHT80P80) == 0))
		val2 = 1;
	if ((val2 == 1) &&
	    ((vap->iv_flags_vht & IEEE80211_FVHT_USEVHT160) == 0))
		val2 = 0;
	val = MIN(val1, val2);
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_SUPP_CHAN_WIDTH_MASK);

	/* RX LDPC */
	val2 = val1 = MS(vap->iv_vhtcaps, IEEE80211_VHTCAP_RXLDPC);
	if (opmode == 1) {
		val2 = MS(ni->ni_vhtcap, IEEE80211_VHTCAP_RXLDPC);
	}
	val = MIN(val1, val2);
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_RXLDPC);

	/* Short-GI 80 */
	val2 = val1 = MS(vap->iv_vhtcaps, IEEE80211_VHTCAP_SHORT_GI_80);
	if (opmode == 1) {
		val2 = MS(ni->ni_vhtcap, IEEE80211_VHTCAP_SHORT_GI_80);
	}
	val = MIN(val1, val2);
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_SHORT_GI_80);

	/* Short-GI 160 */
	val2 = val1 = MS(vap->iv_vhtcaps, IEEE80211_VHTCAP_SHORT_GI_160);
	if (opmode == 1) {
		val2 = MS(ni->ni_vhtcap, IEEE80211_VHTCAP_SHORT_GI_160);
	}
	val = MIN(val1, val2);
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_SHORT_GI_160);

	/*
	 * STBC is slightly more complicated.
	 *
	 * In non-STA mode, we just announce our capabilities and that
	 * is that.
	 *
	 * In STA mode, we should calculate our capabilities based on
	 * local capabilities /and/ what the remote says. So:
	 *
	 * + Only TX STBC if we support it and the remote supports RX STBC;
	 * + Only announce RX STBC if we support it and the remote supports
	 *   TX STBC;
	 * + RX STBC should be the minimum of local and remote RX STBC;
	 */

	/* TX STBC */
	val2 = val1 = MS(vap->iv_vhtcaps, IEEE80211_VHTCAP_TXSTBC);
	if (opmode == 1) {
		/* STA mode - enable it only if node RXSTBC is non-zero */
		val2 = !! MS(ni->ni_vhtcap, IEEE80211_VHTCAP_RXSTBC_MASK);
	}
	val = MIN(val1, val2);
	/* XXX For now, use the 11n config flag */
	if ((vap->iv_flags_ht & IEEE80211_FHT_STBC_TX) == 0)
		val = 0;
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_TXSTBC);

	/* RX STBC1..4 */
	val2 = val1 = MS(vap->iv_vhtcaps, IEEE80211_VHTCAP_RXSTBC_MASK);
	if (opmode == 1) {
		/* STA mode - enable it only if node TXSTBC is non-zero */
		val2 = MS(ni->ni_vhtcap, IEEE80211_VHTCAP_TXSTBC);
	}
	val = MIN(val1, val2);
	/* XXX For now, use the 11n config flag */
	if ((vap->iv_flags_ht & IEEE80211_FHT_STBC_RX) == 0)
		val = 0;
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_RXSTBC_MASK);

	/*
	 * Finally - if RXSTBC is 0, then don't enable TXSTBC.
	 * Strictly speaking a device can TXSTBC and not RXSTBC, but
	 * it would be silly.
	 */
	if (val == 0)
		new_vhtcap &= ~IEEE80211_VHTCAP_TXSTBC;

	/*
	 * Some of these fields require other fields to exist.
	 * So before using it, the parent field needs to be checked
	 * otherwise the overridden value may be wrong.
	 *
	 * For example, if SU beamformee is set to 0, then BF STS
	 * needs to be 0.
	 */

	/* SU Beamformer capable */
	val2 = val1 = MS(vap->iv_vhtcaps,
	    IEEE80211_VHTCAP_SU_BEAMFORMER_CAPABLE);
	if (opmode == 1) {
		val2 = MS(ni->ni_vhtcap,
		    IEEE80211_VHTCAP_SU_BEAMFORMER_CAPABLE);
	}
	val = MIN(val1, val2);
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_SU_BEAMFORMER_CAPABLE);

	/* SU Beamformee capable */
	val2 = val1 = MS(vap->iv_vhtcaps,
	    IEEE80211_VHTCAP_SU_BEAMFORMEE_CAPABLE);
	if (opmode == 1) {
		val2 = MS(ni->ni_vhtcap,
		    IEEE80211_VHTCAP_SU_BEAMFORMEE_CAPABLE);
	}
	val = MIN(val1, val2);
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_SU_BEAMFORMEE_CAPABLE);

	/* Beamformee STS capability - only if SU beamformee capable */
	val2 = val1 = MS(vap->iv_vhtcaps, IEEE80211_VHTCAP_BEAMFORMEE_STS_MASK);
	if (opmode == 1) {
		val2 = MS(ni->ni_vhtcap, IEEE80211_VHTCAP_BEAMFORMEE_STS_MASK);
	}
	val = MIN(val1, val2);
	if ((new_vhtcap & IEEE80211_VHTCAP_SU_BEAMFORMEE_CAPABLE) == 0)
		val = 0;
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_BEAMFORMEE_STS_MASK);

	/* Sounding dimensions - only if SU beamformer capable */
	val2 = val1 = MS(vap->iv_vhtcaps,
	    IEEE80211_VHTCAP_SOUNDING_DIMENSIONS_MASK);
	if (opmode == 1)
		val2 = MS(ni->ni_vhtcap,
		    IEEE80211_VHTCAP_SOUNDING_DIMENSIONS_MASK);
	val = MIN(val1, val2);
	if ((new_vhtcap & IEEE80211_VHTCAP_SU_BEAMFORMER_CAPABLE) == 0)
		val = 0;
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_SOUNDING_DIMENSIONS_MASK);

	/*
	 * MU Beamformer capable - only if SU BFF capable, MU BFF capable
	 * and STA (not AP)
	 */
	val2 = val1 = MS(vap->iv_vhtcaps,
	    IEEE80211_VHTCAP_MU_BEAMFORMER_CAPABLE);
	if (opmode == 1)
		val2 = MS(ni->ni_vhtcap,
		    IEEE80211_VHTCAP_MU_BEAMFORMER_CAPABLE);
	val = MIN(val1, val2);
	if ((new_vhtcap & IEEE80211_VHTCAP_SU_BEAMFORMER_CAPABLE) == 0)
		val = 0;
	if (opmode != 1)	/* Only enable for STA mode */
		val = 0;
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_SU_BEAMFORMER_CAPABLE);

	/*
	 * MU Beamformee capable - only if SU BFE capable, MU BFE capable
	 * and AP (not STA)
	 */
	val2 = val1 = MS(vap->iv_vhtcaps,
	    IEEE80211_VHTCAP_MU_BEAMFORMEE_CAPABLE);
	if (opmode == 1)
		val2 = MS(ni->ni_vhtcap,
		    IEEE80211_VHTCAP_MU_BEAMFORMEE_CAPABLE);
	val = MIN(val1, val2);
	if ((new_vhtcap & IEEE80211_VHTCAP_SU_BEAMFORMEE_CAPABLE) == 0)
		val = 0;
	if (opmode != 0)	/* Only enable for AP mode */
		val = 0;
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_SU_BEAMFORMEE_CAPABLE);

	/* VHT TXOP PS */
	val2 = val1 = MS(vap->iv_vhtcaps, IEEE80211_VHTCAP_VHT_TXOP_PS);
	if (opmode == 1)
		val2 = MS(ni->ni_vhtcap, IEEE80211_VHTCAP_VHT_TXOP_PS);
	val = MIN(val1, val2);
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_VHT_TXOP_PS);

	/* HTC_VHT */
	val2 = val1 = MS(vap->iv_vhtcaps, IEEE80211_VHTCAP_HTC_VHT);
	if (opmode == 1)
		val2 = MS(ni->ni_vhtcap, IEEE80211_VHTCAP_HTC_VHT);
	val = MIN(val1, val2);
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_HTC_VHT);

	/* A-MPDU length max */
	/* XXX TODO: we need a userland config knob for this */
	val2 = val1 = MS(vap->iv_vhtcaps,
	    IEEE80211_VHTCAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK);
	if (opmode == 1)
		val2 = MS(ni->ni_vhtcap,
		    IEEE80211_VHTCAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK);
	val = MIN(val1, val2);
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK);

	/*
	 * Link adaptation is only valid if HTC-VHT capable is 1.
	 * Otherwise, always set it to 0.
	 */
	val2 = val1 = MS(vap->iv_vhtcaps,
	    IEEE80211_VHTCAP_VHT_LINK_ADAPTATION_VHT_MASK);
	if (opmode == 1)
		val2 = MS(ni->ni_vhtcap,
		    IEEE80211_VHTCAP_VHT_LINK_ADAPTATION_VHT_MASK);
	val = MIN(val1, val2);
	if ((new_vhtcap & IEEE80211_VHTCAP_HTC_VHT) == 0)
		val = 0;
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_VHT_LINK_ADAPTATION_VHT_MASK);

	/*
	 * The following two options are 0 if the pattern may change, 1 if it
	 * does not change.  So, downgrade to the higher value.
	 */

	/* RX antenna pattern */
	val2 = val1 = MS(vap->iv_vhtcaps, IEEE80211_VHTCAP_RX_ANTENNA_PATTERN);
	if (opmode == 1)
		val2 = MS(ni->ni_vhtcap, IEEE80211_VHTCAP_RX_ANTENNA_PATTERN);
	val = MAX(val1, val2);
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_RX_ANTENNA_PATTERN);

	/* TX antenna pattern */
	val2 = val1 = MS(vap->iv_vhtcaps, IEEE80211_VHTCAP_TX_ANTENNA_PATTERN);
	if (opmode == 1)
		val2 = MS(ni->ni_vhtcap, IEEE80211_VHTCAP_TX_ANTENNA_PATTERN);
	val = MAX(val1, val2);
	new_vhtcap |= SM(val, IEEE80211_VHTCAP_TX_ANTENNA_PATTERN);

	/*
	 * MCS set - again, we announce what we want to use
	 * based on configuration, device capabilities and
	 * already-learnt vhtcap/vhtinfo IE information.
	 */

	/* MCS set - start with whatever the device supports */
	vhtcap->supp_mcs.rx_mcs_map = vap->iv_vht_mcsinfo.rx_mcs_map;
	vhtcap->supp_mcs.rx_highest = 0;
	vhtcap->supp_mcs.tx_mcs_map = vap->iv_vht_mcsinfo.tx_mcs_map;
	vhtcap->supp_mcs.tx_highest = 0;

	vhtcap->vht_cap_info = new_vhtcap;

	/*
	 * Now, if we're a STA, mask off whatever the AP doesn't support.
	 * Ie, we continue to state we can receive whatever we can do,
	 * but we only announce that we will transmit rates that meet
	 * the AP requirement.
	 *
	 * Note: 0 - MCS0..7; 1 - MCS0..8; 2 - MCS0..9; 3 = not supported.
	 * We can't just use MIN() because '3' means "no", so special case it.
	 */
	if (opmode) {
		for (i = 0; i < 8; i++) {
			val1 = (vhtcap->supp_mcs.tx_mcs_map >> (i*2)) & 0x3;
			val2 = (ni->ni_vht_mcsinfo.tx_mcs_map >> (i*2)) & 0x3;
			val = MIN(val1, val2);
			if (val1 == 3 || val2 == 3)
				val = 3;
			vhtcap->supp_mcs.tx_mcs_map &= ~(0x3 << (i*2));
			vhtcap->supp_mcs.tx_mcs_map |= (val << (i*2));
		}
	}
}

/*
 * Add a VHTCAP field.
 *
 * If in station mode, we announce what we would like our
 * desired configuration to be.
 *
 * Else, we announce our capabilities based on our current
 * configuration.
 */
uint8_t *
ieee80211_add_vhtcap(uint8_t *frm, struct ieee80211_node *ni)
{
	struct ieee80211_ie_vhtcap vhtcap;
	int opmode;

	opmode = 0;
	if (ni->ni_vap->iv_opmode == IEEE80211_M_STA)
		opmode = 1;

	ieee80211_vht_get_vhtcap_ie(ni, &vhtcap, opmode);

	memset(frm, '\0', sizeof(struct ieee80211_ie_vhtcap));

	frm[0] = IEEE80211_ELEMID_VHT_CAP;
	frm[1] = sizeof(struct ieee80211_ie_vhtcap) - 2;
	frm += 2;

	/* 32-bit VHT capability */
	ADDWORD(frm, vhtcap.vht_cap_info);

	/* suppmcs */
	ADDSHORT(frm, vhtcap.supp_mcs.rx_mcs_map);
	ADDSHORT(frm, vhtcap.supp_mcs.rx_highest);
	ADDSHORT(frm, vhtcap.supp_mcs.tx_mcs_map);
	ADDSHORT(frm, vhtcap.supp_mcs.tx_highest);

	return (frm);
}

static uint8_t
ieee80211_vht_get_chwidth_ie(struct ieee80211_channel *c)
{

	/*
	 * XXX TODO: look at the node configuration as
	 * well?
	 */

	if (IEEE80211_IS_CHAN_VHT160(c)) {
		return IEEE80211_VHT_CHANWIDTH_160MHZ;
	}
	if (IEEE80211_IS_CHAN_VHT80_80(c)) {
		return IEEE80211_VHT_CHANWIDTH_80P80MHZ;
	}
	if (IEEE80211_IS_CHAN_VHT80(c)) {
		return IEEE80211_VHT_CHANWIDTH_80MHZ;
	}
	if (IEEE80211_IS_CHAN_VHT40(c)) {
		return IEEE80211_VHT_CHANWIDTH_USE_HT;
	}
	if (IEEE80211_IS_CHAN_VHT20(c)) {
		return IEEE80211_VHT_CHANWIDTH_USE_HT;
	}

	/* We shouldn't get here */
	printf("%s: called on a non-VHT channel (freq=%d, flags=0x%08x\n",
	    __func__,
	    (int) c->ic_freq,
	    c->ic_flags);
	return IEEE80211_VHT_CHANWIDTH_USE_HT;
}

/*
 * Note: this just uses the current channel information;
 * it doesn't use the node info after parsing.
 *
 * XXX TODO: need to make the basic MCS set configurable.
 * XXX TODO: read 802.11-2013 to determine what to set
 *           chwidth to when scanning.  I have a feeling
 *           it isn't involved in scanning and we shouldn't
 *           be sending it; and I don't yet know what to set
 *           it to for IBSS or hostap where the peer may be
 *           a completely different channel width to us.
 */
uint8_t *
ieee80211_add_vhtinfo(uint8_t *frm, struct ieee80211_node *ni)
{
	memset(frm, '\0', sizeof(struct ieee80211_ie_vht_operation));

	frm[0] = IEEE80211_ELEMID_VHT_OPMODE;
	frm[1] = sizeof(struct ieee80211_ie_vht_operation) - 2;
	frm += 2;

	/* 8-bit chanwidth */
	*frm++ = ieee80211_vht_get_chwidth_ie(ni->ni_chan);

	/* 8-bit freq1 */
	*frm++ = ni->ni_chan->ic_vht_ch_freq1;

	/* 8-bit freq2 */
	*frm++ = ni->ni_chan->ic_vht_ch_freq2;

	/* 16-bit basic MCS set - just MCS0..7 for NSS=1 for now */
	ADDSHORT(frm, 0xfffc);

	return (frm);
}

void
ieee80211_vht_update_cap(struct ieee80211_node *ni, const uint8_t *vhtcap_ie,
    const uint8_t *vhtop_ie)
{

	ieee80211_parse_vhtcap(ni, vhtcap_ie);
	ieee80211_parse_vhtopmode(ni, vhtop_ie);
}

static struct ieee80211_channel *
findvhtchan(struct ieee80211com *ic, struct ieee80211_channel *c, int vhtflags)
{

	return (ieee80211_find_channel(ic, c->ic_freq,
	    (c->ic_flags & ~IEEE80211_CHAN_VHT) | vhtflags));
}

/*
 * Handle channel promotion to VHT, similar to ieee80211_ht_adjust_channel().
 */
struct ieee80211_channel *
ieee80211_vht_adjust_channel(struct ieee80211com *ic,
    struct ieee80211_channel *chan, int flags)
{
	struct ieee80211_channel *c;

	/* First case - handle channel demotion - if VHT isn't set */
	if ((flags & IEEE80211_FVHT_VHT) == 0) {
#if 0
		printf("%s: demoting channel %d/0x%08x\n", __func__,
		    chan->ic_ieee, chan->ic_flags);
#endif
		c = ieee80211_find_channel(ic, chan->ic_freq,
		    chan->ic_flags & ~IEEE80211_CHAN_VHT);
		if (c == NULL)
			c = chan;
#if 0
		printf("%s: .. to %d/0x%08x\n", __func__,
		    c->ic_ieee, c->ic_flags);
#endif
		return (c);
	}

	/*
	 * We can upgrade to VHT - attempt to do so
	 *
	 * Note: we don't clear the HT flags, these are the hints
	 * for HT40U/HT40D when selecting VHT40 or larger channels.
	 */
	/* Start with VHT80 */
	c = NULL;
	if ((c == NULL) && (flags & IEEE80211_FVHT_USEVHT160))
		c = findvhtchan(ic, chan, IEEE80211_CHAN_VHT80);

	if ((c == NULL) && (flags & IEEE80211_FVHT_USEVHT80P80))
		c = findvhtchan(ic, chan, IEEE80211_CHAN_VHT80_80);

	if ((c == NULL) && (flags & IEEE80211_FVHT_USEVHT80))
		c = findvhtchan(ic, chan, IEEE80211_CHAN_VHT80);

	if ((c == NULL) && (flags & IEEE80211_FVHT_USEVHT40))
		c = findvhtchan(ic, chan, IEEE80211_CHAN_VHT40U);
	if ((c == NULL) && (flags & IEEE80211_FVHT_USEVHT40))
		c = findvhtchan(ic, chan, IEEE80211_CHAN_VHT40D);
	/*
	 * If we get here, VHT20 is always possible because we checked
	 * for IEEE80211_FVHT_VHT above.
	 */
	if (c == NULL)
		c = findvhtchan(ic, chan, IEEE80211_CHAN_VHT20);

	if (c != NULL)
		chan = c;

#if 0
	printf("%s: selected %d/0x%08x\n", __func__, c->ic_ieee, c->ic_flags);
#endif
	return (chan);
}

/*
 * Calculate the VHT operation IE for a given node.
 *
 * This includes calculating the suitable channel width/parameters
 * and basic MCS set.
 *
 * TODO: ensure I read 9.7.11 Rate Selection for VHT STAs.
 * TODO: ensure I read 10.39.7 - BSS Basic VHT-MCS and NSS set operation.
 */
void
ieee80211_vht_get_vhtinfo_ie(struct ieee80211_node *ni,
    struct ieee80211_ie_vht_operation *vhtop, int opmode)
{
	printf("%s: called; TODO!\n", __func__);
}
