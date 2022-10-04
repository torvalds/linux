// SPDX-License-Identifier: GPL-2.0-only
/*
 * BSS client mode implementation
 * Copyright 2003-2008, Jouni Malinen <j@w1.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright (C) 2015 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 - 2022 Intel Corporation
 */

#include <linux/delay.h>
#include <linux/fips.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/moduleparam.h>
#include <linux/rtnetlink.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/mac80211.h>
#include <asm/unaligned.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "led.h"
#include "fils_aead.h"

#define IEEE80211_AUTH_TIMEOUT		(HZ / 5)
#define IEEE80211_AUTH_TIMEOUT_LONG	(HZ / 2)
#define IEEE80211_AUTH_TIMEOUT_SHORT	(HZ / 10)
#define IEEE80211_AUTH_TIMEOUT_SAE	(HZ * 2)
#define IEEE80211_AUTH_MAX_TRIES	3
#define IEEE80211_AUTH_WAIT_ASSOC	(HZ * 5)
#define IEEE80211_AUTH_WAIT_SAE_RETRY	(HZ * 2)
#define IEEE80211_ASSOC_TIMEOUT		(HZ / 5)
#define IEEE80211_ASSOC_TIMEOUT_LONG	(HZ / 2)
#define IEEE80211_ASSOC_TIMEOUT_SHORT	(HZ / 10)
#define IEEE80211_ASSOC_MAX_TRIES	3

static int max_nullfunc_tries = 2;
module_param(max_nullfunc_tries, int, 0644);
MODULE_PARM_DESC(max_nullfunc_tries,
		 "Maximum nullfunc tx tries before disconnecting (reason 4).");

static int max_probe_tries = 5;
module_param(max_probe_tries, int, 0644);
MODULE_PARM_DESC(max_probe_tries,
		 "Maximum probe tries before disconnecting (reason 4).");

/*
 * Beacon loss timeout is calculated as N frames times the
 * advertised beacon interval.  This may need to be somewhat
 * higher than what hardware might detect to account for
 * delays in the host processing frames. But since we also
 * probe on beacon miss before declaring the connection lost
 * default to what we want.
 */
static int beacon_loss_count = 7;
module_param(beacon_loss_count, int, 0644);
MODULE_PARM_DESC(beacon_loss_count,
		 "Number of beacon intervals before we decide beacon was lost.");

/*
 * Time the connection can be idle before we probe
 * it to see if we can still talk to the AP.
 */
#define IEEE80211_CONNECTION_IDLE_TIME	(30 * HZ)
/*
 * Time we wait for a probe response after sending
 * a probe request because of beacon loss or for
 * checking the connection still works.
 */
static int probe_wait_ms = 500;
module_param(probe_wait_ms, int, 0644);
MODULE_PARM_DESC(probe_wait_ms,
		 "Maximum time(ms) to wait for probe response"
		 " before disconnecting (reason 4).");

/*
 * How many Beacon frames need to have been used in average signal strength
 * before starting to indicate signal change events.
 */
#define IEEE80211_SIGNAL_AVE_MIN_COUNT	4

/*
 * We can have multiple work items (and connection probing)
 * scheduling this timer, but we need to take care to only
 * reschedule it when it should fire _earlier_ than it was
 * asked for before, or if it's not pending right now. This
 * function ensures that. Note that it then is required to
 * run this function for all timeouts after the first one
 * has happened -- the work that runs from this timer will
 * do that.
 */
static void run_again(struct ieee80211_sub_if_data *sdata,
		      unsigned long timeout)
{
	sdata_assert_lock(sdata);

	if (!timer_pending(&sdata->u.mgd.timer) ||
	    time_before(timeout, sdata->u.mgd.timer.expires))
		mod_timer(&sdata->u.mgd.timer, timeout);
}

void ieee80211_sta_reset_beacon_monitor(struct ieee80211_sub_if_data *sdata)
{
	if (sdata->vif.driver_flags & IEEE80211_VIF_BEACON_FILTER)
		return;

	if (ieee80211_hw_check(&sdata->local->hw, CONNECTION_MONITOR))
		return;

	mod_timer(&sdata->u.mgd.bcn_mon_timer,
		  round_jiffies_up(jiffies + sdata->u.mgd.beacon_timeout));
}

void ieee80211_sta_reset_conn_monitor(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (unlikely(!ifmgd->associated))
		return;

	if (ifmgd->probe_send_count)
		ifmgd->probe_send_count = 0;

	if (ieee80211_hw_check(&sdata->local->hw, CONNECTION_MONITOR))
		return;

	mod_timer(&ifmgd->conn_mon_timer,
		  round_jiffies_up(jiffies + IEEE80211_CONNECTION_IDLE_TIME));
}

static int ecw2cw(int ecw)
{
	return (1 << ecw) - 1;
}

static ieee80211_conn_flags_t
ieee80211_determine_chantype(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_link_data *link,
			     ieee80211_conn_flags_t conn_flags,
			     struct ieee80211_supported_band *sband,
			     struct ieee80211_channel *channel,
			     u32 vht_cap_info,
			     const struct ieee80211_ht_operation *ht_oper,
			     const struct ieee80211_vht_operation *vht_oper,
			     const struct ieee80211_he_operation *he_oper,
			     const struct ieee80211_eht_operation *eht_oper,
			     const struct ieee80211_s1g_oper_ie *s1g_oper,
			     struct cfg80211_chan_def *chandef, bool tracking)
{
	struct cfg80211_chan_def vht_chandef;
	struct ieee80211_sta_ht_cap sta_ht_cap;
	ieee80211_conn_flags_t ret;
	u32 ht_cfreq;

	memset(chandef, 0, sizeof(struct cfg80211_chan_def));
	chandef->chan = channel;
	chandef->width = NL80211_CHAN_WIDTH_20_NOHT;
	chandef->center_freq1 = channel->center_freq;
	chandef->freq1_offset = channel->freq_offset;

	if (channel->band == NL80211_BAND_6GHZ) {
		if (!ieee80211_chandef_he_6ghz_oper(sdata, he_oper, eht_oper,
						    chandef)) {
			mlme_dbg(sdata,
				 "bad 6 GHz operation, disabling HT/VHT/HE/EHT\n");
			ret = IEEE80211_CONN_DISABLE_HT |
			      IEEE80211_CONN_DISABLE_VHT |
			      IEEE80211_CONN_DISABLE_HE |
			      IEEE80211_CONN_DISABLE_EHT;
		} else {
			ret = 0;
		}
		vht_chandef = *chandef;
		goto out;
	} else if (sband->band == NL80211_BAND_S1GHZ) {
		if (!ieee80211_chandef_s1g_oper(s1g_oper, chandef)) {
			sdata_info(sdata,
				   "Missing S1G Operation Element? Trying operating == primary\n");
			chandef->width = ieee80211_s1g_channel_width(channel);
		}

		ret = IEEE80211_CONN_DISABLE_HT | IEEE80211_CONN_DISABLE_40MHZ |
		      IEEE80211_CONN_DISABLE_VHT |
		      IEEE80211_CONN_DISABLE_80P80MHZ |
		      IEEE80211_CONN_DISABLE_160MHZ;
		goto out;
	}

	memcpy(&sta_ht_cap, &sband->ht_cap, sizeof(sta_ht_cap));
	ieee80211_apply_htcap_overrides(sdata, &sta_ht_cap);

	if (!ht_oper || !sta_ht_cap.ht_supported) {
		mlme_dbg(sdata, "HT operation missing / HT not supported\n");
		ret = IEEE80211_CONN_DISABLE_HT |
		      IEEE80211_CONN_DISABLE_VHT |
		      IEEE80211_CONN_DISABLE_HE |
		      IEEE80211_CONN_DISABLE_EHT;
		goto out;
	}

	chandef->width = NL80211_CHAN_WIDTH_20;

	ht_cfreq = ieee80211_channel_to_frequency(ht_oper->primary_chan,
						  channel->band);
	/* check that channel matches the right operating channel */
	if (!tracking && channel->center_freq != ht_cfreq) {
		/*
		 * It's possible that some APs are confused here;
		 * Netgear WNDR3700 sometimes reports 4 higher than
		 * the actual channel in association responses, but
		 * since we look at probe response/beacon data here
		 * it should be OK.
		 */
		sdata_info(sdata,
			   "Wrong control channel: center-freq: %d ht-cfreq: %d ht->primary_chan: %d band: %d - Disabling HT\n",
			   channel->center_freq, ht_cfreq,
			   ht_oper->primary_chan, channel->band);
		ret = IEEE80211_CONN_DISABLE_HT |
		      IEEE80211_CONN_DISABLE_VHT |
		      IEEE80211_CONN_DISABLE_HE |
		      IEEE80211_CONN_DISABLE_EHT;
		goto out;
	}

	/* check 40 MHz support, if we have it */
	if (sta_ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) {
		ieee80211_chandef_ht_oper(ht_oper, chandef);
	} else {
		mlme_dbg(sdata, "40 MHz not supported\n");
		/* 40 MHz (and 80 MHz) must be supported for VHT */
		ret = IEEE80211_CONN_DISABLE_VHT;
		/* also mark 40 MHz disabled */
		ret |= IEEE80211_CONN_DISABLE_40MHZ;
		goto out;
	}

	if (!vht_oper || !sband->vht_cap.vht_supported) {
		mlme_dbg(sdata, "VHT operation missing / VHT not supported\n");
		ret = IEEE80211_CONN_DISABLE_VHT;
		goto out;
	}

	vht_chandef = *chandef;
	if (!(conn_flags & IEEE80211_CONN_DISABLE_HE) &&
	    he_oper &&
	    (le32_to_cpu(he_oper->he_oper_params) &
	     IEEE80211_HE_OPERATION_VHT_OPER_INFO)) {
		struct ieee80211_vht_operation he_oper_vht_cap;

		/*
		 * Set only first 3 bytes (other 2 aren't used in
		 * ieee80211_chandef_vht_oper() anyway)
		 */
		memcpy(&he_oper_vht_cap, he_oper->optional, 3);
		he_oper_vht_cap.basic_mcs_set = cpu_to_le16(0);

		if (!ieee80211_chandef_vht_oper(&sdata->local->hw, vht_cap_info,
						&he_oper_vht_cap, ht_oper,
						&vht_chandef)) {
			if (!(conn_flags & IEEE80211_CONN_DISABLE_HE))
				sdata_info(sdata,
					   "HE AP VHT information is invalid, disabling HE\n");
			ret = IEEE80211_CONN_DISABLE_HE | IEEE80211_CONN_DISABLE_EHT;
			goto out;
		}
	} else if (!ieee80211_chandef_vht_oper(&sdata->local->hw,
					       vht_cap_info,
					       vht_oper, ht_oper,
					       &vht_chandef)) {
		if (!(conn_flags & IEEE80211_CONN_DISABLE_VHT))
			sdata_info(sdata,
				   "AP VHT information is invalid, disabling VHT\n");
		ret = IEEE80211_CONN_DISABLE_VHT;
		goto out;
	}

	if (!cfg80211_chandef_valid(&vht_chandef)) {
		if (!(conn_flags & IEEE80211_CONN_DISABLE_VHT))
			sdata_info(sdata,
				   "AP VHT information is invalid, disabling VHT\n");
		ret = IEEE80211_CONN_DISABLE_VHT;
		goto out;
	}

	if (cfg80211_chandef_identical(chandef, &vht_chandef)) {
		ret = 0;
		goto out;
	}

	if (!cfg80211_chandef_compatible(chandef, &vht_chandef)) {
		if (!(conn_flags & IEEE80211_CONN_DISABLE_VHT))
			sdata_info(sdata,
				   "AP VHT information doesn't match HT, disabling VHT\n");
		ret = IEEE80211_CONN_DISABLE_VHT;
		goto out;
	}

	*chandef = vht_chandef;

	/*
	 * handle the case that the EHT operation indicates that it holds EHT
	 * operation information (in case that the channel width differs from
	 * the channel width reported in HT/VHT/HE).
	 */
	if (eht_oper && (eht_oper->params & IEEE80211_EHT_OPER_INFO_PRESENT)) {
		struct cfg80211_chan_def eht_chandef = *chandef;

		ieee80211_chandef_eht_oper(eht_oper,
					   eht_chandef.width ==
					   NL80211_CHAN_WIDTH_160,
					   false, &eht_chandef);

		if (!cfg80211_chandef_valid(&eht_chandef)) {
			if (!(conn_flags & IEEE80211_CONN_DISABLE_EHT))
				sdata_info(sdata,
					   "AP EHT information is invalid, disabling EHT\n");
			ret = IEEE80211_CONN_DISABLE_EHT;
			goto out;
		}

		if (!cfg80211_chandef_compatible(chandef, &eht_chandef)) {
			if (!(conn_flags & IEEE80211_CONN_DISABLE_EHT))
				sdata_info(sdata,
					   "AP EHT information is incompatible, disabling EHT\n");
			ret = IEEE80211_CONN_DISABLE_EHT;
			goto out;
		}

		*chandef = eht_chandef;
	}

	ret = 0;

out:
	/*
	 * When tracking the current AP, don't do any further checks if the
	 * new chandef is identical to the one we're currently using for the
	 * connection. This keeps us from playing ping-pong with regulatory,
	 * without it the following can happen (for example):
	 *  - connect to an AP with 80 MHz, world regdom allows 80 MHz
	 *  - AP advertises regdom US
	 *  - CRDA loads regdom US with 80 MHz prohibited (old database)
	 *  - the code below detects an unsupported channel, downgrades, and
	 *    we disconnect from the AP in the caller
	 *  - disconnect causes CRDA to reload world regdomain and the game
	 *    starts anew.
	 * (see https://bugzilla.kernel.org/show_bug.cgi?id=70881)
	 *
	 * It seems possible that there are still scenarios with CSA or real
	 * bandwidth changes where a this could happen, but those cases are
	 * less common and wouldn't completely prevent using the AP.
	 */
	if (tracking &&
	    cfg80211_chandef_identical(chandef, &link->conf->chandef))
		return ret;

	/* don't print the message below for VHT mismatch if VHT is disabled */
	if (ret & IEEE80211_CONN_DISABLE_VHT)
		vht_chandef = *chandef;

	/*
	 * Ignore the DISABLED flag when we're already connected and only
	 * tracking the APs beacon for bandwidth changes - otherwise we
	 * might get disconnected here if we connect to an AP, update our
	 * regulatory information based on the AP's country IE and the
	 * information we have is wrong/outdated and disables the channel
	 * that we're actually using for the connection to the AP.
	 */
	while (!cfg80211_chandef_usable(sdata->local->hw.wiphy, chandef,
					tracking ? 0 :
						   IEEE80211_CHAN_DISABLED)) {
		if (WARN_ON(chandef->width == NL80211_CHAN_WIDTH_20_NOHT)) {
			ret = IEEE80211_CONN_DISABLE_HT |
			      IEEE80211_CONN_DISABLE_VHT |
			      IEEE80211_CONN_DISABLE_HE |
			      IEEE80211_CONN_DISABLE_EHT;
			break;
		}

		ret |= ieee80211_chandef_downgrade(chandef);
	}

	if (!he_oper || !cfg80211_chandef_usable(sdata->wdev.wiphy, chandef,
						 IEEE80211_CHAN_NO_HE))
		ret |= IEEE80211_CONN_DISABLE_HE | IEEE80211_CONN_DISABLE_EHT;

	if (!eht_oper || !cfg80211_chandef_usable(sdata->wdev.wiphy, chandef,
						  IEEE80211_CHAN_NO_EHT))
		ret |= IEEE80211_CONN_DISABLE_EHT;

	if (chandef->width != vht_chandef.width && !tracking)
		sdata_info(sdata,
			   "capabilities/regulatory prevented using AP HT/VHT configuration, downgraded\n");

	WARN_ON_ONCE(!cfg80211_chandef_valid(chandef));
	return ret;
}

static int ieee80211_config_bw(struct ieee80211_link_data *link,
			       const struct ieee80211_ht_cap *ht_cap,
			       const struct ieee80211_vht_cap *vht_cap,
			       const struct ieee80211_ht_operation *ht_oper,
			       const struct ieee80211_vht_operation *vht_oper,
			       const struct ieee80211_he_operation *he_oper,
			       const struct ieee80211_eht_operation *eht_oper,
			       const struct ieee80211_s1g_oper_ie *s1g_oper,
			       const u8 *bssid, u32 *changed)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_channel *chan = link->conf->chandef.chan;
	struct ieee80211_supported_band *sband =
		local->hw.wiphy->bands[chan->band];
	struct cfg80211_chan_def chandef;
	u16 ht_opmode;
	ieee80211_conn_flags_t flags;
	u32 vht_cap_info = 0;
	int ret;

	/* if HT was/is disabled, don't track any bandwidth changes */
	if (link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HT || !ht_oper)
		return 0;

	/* don't check VHT if we associated as non-VHT station */
	if (link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_VHT)
		vht_oper = NULL;

	/* don't check HE if we associated as non-HE station */
	if (link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HE ||
	    !ieee80211_get_he_iftype_cap(sband,
					 ieee80211_vif_type_p2p(&sdata->vif))) {
		he_oper = NULL;
		eht_oper = NULL;
	}

	/* don't check EHT if we associated as non-EHT station */
	if (link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_EHT ||
	    !ieee80211_get_eht_iftype_cap(sband,
					 ieee80211_vif_type_p2p(&sdata->vif)))
		eht_oper = NULL;

	/*
	 * if bss configuration changed store the new one -
	 * this may be applicable even if channel is identical
	 */
	ht_opmode = le16_to_cpu(ht_oper->operation_mode);
	if (link->conf->ht_operation_mode != ht_opmode) {
		*changed |= BSS_CHANGED_HT;
		link->conf->ht_operation_mode = ht_opmode;
	}

	if (vht_cap)
		vht_cap_info = le32_to_cpu(vht_cap->vht_cap_info);

	/* calculate new channel (type) based on HT/VHT/HE operation IEs */
	flags = ieee80211_determine_chantype(sdata, link,
					     link->u.mgd.conn_flags,
					     sband, chan, vht_cap_info,
					     ht_oper, vht_oper,
					     he_oper, eht_oper,
					     s1g_oper, &chandef, true);

	/*
	 * Downgrade the new channel if we associated with restricted
	 * capabilities. For example, if we associated as a 20 MHz STA
	 * to a 40 MHz AP (due to regulatory, capabilities or config
	 * reasons) then switching to a 40 MHz channel now won't do us
	 * any good -- we couldn't use it with the AP.
	 */
	if (link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_80P80MHZ &&
	    chandef.width == NL80211_CHAN_WIDTH_80P80)
		flags |= ieee80211_chandef_downgrade(&chandef);
	if (link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_160MHZ &&
	    chandef.width == NL80211_CHAN_WIDTH_160)
		flags |= ieee80211_chandef_downgrade(&chandef);
	if (link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_40MHZ &&
	    chandef.width > NL80211_CHAN_WIDTH_20)
		flags |= ieee80211_chandef_downgrade(&chandef);

	if (cfg80211_chandef_identical(&chandef, &link->conf->chandef))
		return 0;

	link_info(link,
		  "AP %pM changed bandwidth, new config is %d.%03d MHz, width %d (%d.%03d/%d MHz)\n",
		  link->u.mgd.bssid, chandef.chan->center_freq,
		  chandef.chan->freq_offset, chandef.width,
		  chandef.center_freq1, chandef.freq1_offset,
		  chandef.center_freq2);

	if (flags != (link->u.mgd.conn_flags &
				(IEEE80211_CONN_DISABLE_HT |
				 IEEE80211_CONN_DISABLE_VHT |
				 IEEE80211_CONN_DISABLE_HE |
				 IEEE80211_CONN_DISABLE_EHT |
				 IEEE80211_CONN_DISABLE_40MHZ |
				 IEEE80211_CONN_DISABLE_80P80MHZ |
				 IEEE80211_CONN_DISABLE_160MHZ |
				 IEEE80211_CONN_DISABLE_320MHZ)) ||
	    !cfg80211_chandef_valid(&chandef)) {
		sdata_info(sdata,
			   "AP %pM changed caps/bw in a way we can't support (0x%x/0x%x) - disconnect\n",
			   link->u.mgd.bssid, flags, ifmgd->flags);
		return -EINVAL;
	}

	ret = ieee80211_link_change_bandwidth(link, &chandef, changed);

	if (ret) {
		sdata_info(sdata,
			   "AP %pM changed bandwidth to incompatible one - disconnect\n",
			   link->u.mgd.bssid);
		return ret;
	}

	return 0;
}

/* frame sending functions */

static void ieee80211_add_ht_ie(struct ieee80211_sub_if_data *sdata,
				struct sk_buff *skb, u8 ap_ht_param,
				struct ieee80211_supported_band *sband,
				struct ieee80211_channel *channel,
				enum ieee80211_smps_mode smps,
				ieee80211_conn_flags_t conn_flags)
{
	u8 *pos;
	u32 flags = channel->flags;
	u16 cap;
	struct ieee80211_sta_ht_cap ht_cap;

	BUILD_BUG_ON(sizeof(ht_cap) != sizeof(sband->ht_cap));

	memcpy(&ht_cap, &sband->ht_cap, sizeof(ht_cap));
	ieee80211_apply_htcap_overrides(sdata, &ht_cap);

	/* determine capability flags */
	cap = ht_cap.cap;

	switch (ap_ht_param & IEEE80211_HT_PARAM_CHA_SEC_OFFSET) {
	case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
		if (flags & IEEE80211_CHAN_NO_HT40PLUS) {
			cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
			cap &= ~IEEE80211_HT_CAP_SGI_40;
		}
		break;
	case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
		if (flags & IEEE80211_CHAN_NO_HT40MINUS) {
			cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
			cap &= ~IEEE80211_HT_CAP_SGI_40;
		}
		break;
	}

	/*
	 * If 40 MHz was disabled associate as though we weren't
	 * capable of 40 MHz -- some broken APs will never fall
	 * back to trying to transmit in 20 MHz.
	 */
	if (conn_flags & IEEE80211_CONN_DISABLE_40MHZ) {
		cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		cap &= ~IEEE80211_HT_CAP_SGI_40;
	}

	/* set SM PS mode properly */
	cap &= ~IEEE80211_HT_CAP_SM_PS;
	switch (smps) {
	case IEEE80211_SMPS_AUTOMATIC:
	case IEEE80211_SMPS_NUM_MODES:
		WARN_ON(1);
		fallthrough;
	case IEEE80211_SMPS_OFF:
		cap |= WLAN_HT_CAP_SM_PS_DISABLED <<
			IEEE80211_HT_CAP_SM_PS_SHIFT;
		break;
	case IEEE80211_SMPS_STATIC:
		cap |= WLAN_HT_CAP_SM_PS_STATIC <<
			IEEE80211_HT_CAP_SM_PS_SHIFT;
		break;
	case IEEE80211_SMPS_DYNAMIC:
		cap |= WLAN_HT_CAP_SM_PS_DYNAMIC <<
			IEEE80211_HT_CAP_SM_PS_SHIFT;
		break;
	}

	/* reserve and fill IE */
	pos = skb_put(skb, sizeof(struct ieee80211_ht_cap) + 2);
	ieee80211_ie_build_ht_cap(pos, &ht_cap, cap);
}

/* This function determines vht capability flags for the association
 * and builds the IE.
 * Note - the function returns true to own the MU-MIMO capability
 */
static bool ieee80211_add_vht_ie(struct ieee80211_sub_if_data *sdata,
				 struct sk_buff *skb,
				 struct ieee80211_supported_band *sband,
				 struct ieee80211_vht_cap *ap_vht_cap,
				 ieee80211_conn_flags_t conn_flags)
{
	struct ieee80211_local *local = sdata->local;
	u8 *pos;
	u32 cap;
	struct ieee80211_sta_vht_cap vht_cap;
	u32 mask, ap_bf_sts, our_bf_sts;
	bool mu_mimo_owner = false;

	BUILD_BUG_ON(sizeof(vht_cap) != sizeof(sband->vht_cap));

	memcpy(&vht_cap, &sband->vht_cap, sizeof(vht_cap));
	ieee80211_apply_vhtcap_overrides(sdata, &vht_cap);

	/* determine capability flags */
	cap = vht_cap.cap;

	if (conn_flags & IEEE80211_CONN_DISABLE_80P80MHZ) {
		u32 bw = cap & IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;

		cap &= ~IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;
		if (bw == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ ||
		    bw == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ)
			cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
	}

	if (conn_flags & IEEE80211_CONN_DISABLE_160MHZ) {
		cap &= ~IEEE80211_VHT_CAP_SHORT_GI_160;
		cap &= ~IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;
	}

	/*
	 * Some APs apparently get confused if our capabilities are better
	 * than theirs, so restrict what we advertise in the assoc request.
	 */
	if (!(ap_vht_cap->vht_cap_info &
			cpu_to_le32(IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE)))
		cap &= ~(IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
			 IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE);
	else if (!(ap_vht_cap->vht_cap_info &
			cpu_to_le32(IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE)))
		cap &= ~IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;

	/*
	 * If some other vif is using the MU-MIMO capability we cannot associate
	 * using MU-MIMO - this will lead to contradictions in the group-id
	 * mechanism.
	 * Ownership is defined since association request, in order to avoid
	 * simultaneous associations with MU-MIMO.
	 */
	if (cap & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE) {
		bool disable_mu_mimo = false;
		struct ieee80211_sub_if_data *other;

		list_for_each_entry_rcu(other, &local->interfaces, list) {
			if (other->vif.bss_conf.mu_mimo_owner) {
				disable_mu_mimo = true;
				break;
			}
		}
		if (disable_mu_mimo)
			cap &= ~IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
		else
			mu_mimo_owner = true;
	}

	mask = IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK;

	ap_bf_sts = le32_to_cpu(ap_vht_cap->vht_cap_info) & mask;
	our_bf_sts = cap & mask;

	if (ap_bf_sts < our_bf_sts) {
		cap &= ~mask;
		cap |= ap_bf_sts;
	}

	/* reserve and fill IE */
	pos = skb_put(skb, sizeof(struct ieee80211_vht_cap) + 2);
	ieee80211_ie_build_vht_cap(pos, &vht_cap, cap);

	return mu_mimo_owner;
}

/* This function determines HE capability flags for the association
 * and builds the IE.
 */
static void ieee80211_add_he_ie(struct ieee80211_sub_if_data *sdata,
				struct sk_buff *skb,
				struct ieee80211_supported_band *sband,
				enum ieee80211_smps_mode smps_mode,
				ieee80211_conn_flags_t conn_flags)
{
	u8 *pos, *pre_he_pos;
	const struct ieee80211_sta_he_cap *he_cap;
	u8 he_cap_size;

	he_cap = ieee80211_get_he_iftype_cap(sband,
					     ieee80211_vif_type_p2p(&sdata->vif));
	if (WARN_ON(!he_cap))
		return;

	/* get a max size estimate */
	he_cap_size =
		2 + 1 + sizeof(he_cap->he_cap_elem) +
		ieee80211_he_mcs_nss_size(&he_cap->he_cap_elem) +
		ieee80211_he_ppe_size(he_cap->ppe_thres[0],
				      he_cap->he_cap_elem.phy_cap_info);
	pos = skb_put(skb, he_cap_size);
	pre_he_pos = pos;
	pos = ieee80211_ie_build_he_cap(conn_flags,
					pos, he_cap, pos + he_cap_size);
	/* trim excess if any */
	skb_trim(skb, skb->len - (pre_he_pos + he_cap_size - pos));

	ieee80211_ie_build_he_6ghz_cap(sdata, smps_mode, skb);
}

static void ieee80211_add_eht_ie(struct ieee80211_sub_if_data *sdata,
				 struct sk_buff *skb,
				 struct ieee80211_supported_band *sband)
{
	u8 *pos;
	const struct ieee80211_sta_he_cap *he_cap;
	const struct ieee80211_sta_eht_cap *eht_cap;
	u8 eht_cap_size;

	he_cap = ieee80211_get_he_iftype_cap(sband,
					     ieee80211_vif_type_p2p(&sdata->vif));
	eht_cap = ieee80211_get_eht_iftype_cap(sband,
					       ieee80211_vif_type_p2p(&sdata->vif));

	/*
	 * EHT capabilities element is only added if the HE capabilities element
	 * was added so assume that 'he_cap' is valid and don't check it.
	 */
	if (WARN_ON(!he_cap || !eht_cap))
		return;

	eht_cap_size =
		2 + 1 + sizeof(eht_cap->eht_cap_elem) +
		ieee80211_eht_mcs_nss_size(&he_cap->he_cap_elem,
					   &eht_cap->eht_cap_elem,
					   false) +
		ieee80211_eht_ppe_size(eht_cap->eht_ppe_thres[0],
				       eht_cap->eht_cap_elem.phy_cap_info);
	pos = skb_put(skb, eht_cap_size);
	ieee80211_ie_build_eht_cap(pos, he_cap, eht_cap, pos + eht_cap_size,
				   false);
}

static void ieee80211_assoc_add_rates(struct sk_buff *skb,
				      enum nl80211_chan_width width,
				      struct ieee80211_supported_band *sband,
				      struct ieee80211_mgd_assoc_data *assoc_data)
{
	unsigned int shift = ieee80211_chanwidth_get_shift(width);
	unsigned int rates_len, supp_rates_len;
	u32 rates = 0;
	int i, count;
	u8 *pos;

	if (assoc_data->supp_rates_len) {
		/*
		 * Get all rates supported by the device and the AP as
		 * some APs don't like getting a superset of their rates
		 * in the association request (e.g. D-Link DAP 1353 in
		 * b-only mode)...
		 */
		rates_len = ieee80211_parse_bitrates(width, sband,
						     assoc_data->supp_rates,
						     assoc_data->supp_rates_len,
						     &rates);
	} else {
		/*
		 * In case AP not provide any supported rates information
		 * before association, we send information element(s) with
		 * all rates that we support.
		 */
		rates_len = sband->n_bitrates;
		for (i = 0; i < sband->n_bitrates; i++)
			rates |= BIT(i);
	}

	supp_rates_len = rates_len;
	if (supp_rates_len > 8)
		supp_rates_len = 8;

	pos = skb_put(skb, supp_rates_len + 2);
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = supp_rates_len;

	count = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if (BIT(i) & rates) {
			int rate = DIV_ROUND_UP(sband->bitrates[i].bitrate,
						5 * (1 << shift));
			*pos++ = (u8)rate;
			if (++count == 8)
				break;
		}
	}

	if (rates_len > count) {
		pos = skb_put(skb, rates_len - count + 2);
		*pos++ = WLAN_EID_EXT_SUPP_RATES;
		*pos++ = rates_len - count;

		for (i++; i < sband->n_bitrates; i++) {
			if (BIT(i) & rates) {
				int rate;

				rate = DIV_ROUND_UP(sband->bitrates[i].bitrate,
						    5 * (1 << shift));
				*pos++ = (u8)rate;
			}
		}
	}
}

static size_t ieee80211_add_before_ht_elems(struct sk_buff *skb,
					    const u8 *elems,
					    size_t elems_len,
					    size_t offset)
{
	size_t noffset;

	static const u8 before_ht[] = {
		WLAN_EID_SSID,
		WLAN_EID_SUPP_RATES,
		WLAN_EID_EXT_SUPP_RATES,
		WLAN_EID_PWR_CAPABILITY,
		WLAN_EID_SUPPORTED_CHANNELS,
		WLAN_EID_RSN,
		WLAN_EID_QOS_CAPA,
		WLAN_EID_RRM_ENABLED_CAPABILITIES,
		WLAN_EID_MOBILITY_DOMAIN,
		WLAN_EID_FAST_BSS_TRANSITION,	/* reassoc only */
		WLAN_EID_RIC_DATA,		/* reassoc only */
		WLAN_EID_SUPPORTED_REGULATORY_CLASSES,
	};
	static const u8 after_ric[] = {
		WLAN_EID_SUPPORTED_REGULATORY_CLASSES,
		WLAN_EID_HT_CAPABILITY,
		WLAN_EID_BSS_COEX_2040,
		/* luckily this is almost always there */
		WLAN_EID_EXT_CAPABILITY,
		WLAN_EID_QOS_TRAFFIC_CAPA,
		WLAN_EID_TIM_BCAST_REQ,
		WLAN_EID_INTERWORKING,
		/* 60 GHz (Multi-band, DMG, MMS) can't happen */
		WLAN_EID_VHT_CAPABILITY,
		WLAN_EID_OPMODE_NOTIF,
	};

	if (!elems_len)
		return offset;

	noffset = ieee80211_ie_split_ric(elems, elems_len,
					 before_ht,
					 ARRAY_SIZE(before_ht),
					 after_ric,
					 ARRAY_SIZE(after_ric),
					 offset);
	skb_put_data(skb, elems + offset, noffset - offset);

	return noffset;
}

static size_t ieee80211_add_before_vht_elems(struct sk_buff *skb,
					     const u8 *elems,
					     size_t elems_len,
					     size_t offset)
{
	static const u8 before_vht[] = {
		/*
		 * no need to list the ones split off before HT
		 * or generated here
		 */
		WLAN_EID_BSS_COEX_2040,
		WLAN_EID_EXT_CAPABILITY,
		WLAN_EID_QOS_TRAFFIC_CAPA,
		WLAN_EID_TIM_BCAST_REQ,
		WLAN_EID_INTERWORKING,
		/* 60 GHz (Multi-band, DMG, MMS) can't happen */
	};
	size_t noffset;

	if (!elems_len)
		return offset;

	/* RIC already taken care of in ieee80211_add_before_ht_elems() */
	noffset = ieee80211_ie_split(elems, elems_len,
				     before_vht, ARRAY_SIZE(before_vht),
				     offset);
	skb_put_data(skb, elems + offset, noffset - offset);

	return noffset;
}

static size_t ieee80211_add_before_he_elems(struct sk_buff *skb,
					    const u8 *elems,
					    size_t elems_len,
					    size_t offset)
{
	static const u8 before_he[] = {
		/*
		 * no need to list the ones split off before VHT
		 * or generated here
		 */
		WLAN_EID_OPMODE_NOTIF,
		WLAN_EID_EXTENSION, WLAN_EID_EXT_FUTURE_CHAN_GUIDANCE,
		/* 11ai elements */
		WLAN_EID_EXTENSION, WLAN_EID_EXT_FILS_SESSION,
		WLAN_EID_EXTENSION, WLAN_EID_EXT_FILS_PUBLIC_KEY,
		WLAN_EID_EXTENSION, WLAN_EID_EXT_FILS_KEY_CONFIRM,
		WLAN_EID_EXTENSION, WLAN_EID_EXT_FILS_HLP_CONTAINER,
		WLAN_EID_EXTENSION, WLAN_EID_EXT_FILS_IP_ADDR_ASSIGN,
		/* TODO: add 11ah/11aj/11ak elements */
	};
	size_t noffset;

	if (!elems_len)
		return offset;

	/* RIC already taken care of in ieee80211_add_before_ht_elems() */
	noffset = ieee80211_ie_split(elems, elems_len,
				     before_he, ARRAY_SIZE(before_he),
				     offset);
	skb_put_data(skb, elems + offset, noffset - offset);

	return noffset;
}

#define PRESENT_ELEMS_MAX	8
#define PRESENT_ELEM_EXT_OFFS	0x100

static void ieee80211_assoc_add_ml_elem(struct ieee80211_sub_if_data *sdata,
					struct sk_buff *skb, u16 capab,
					const struct element *ext_capa,
					const u16 *present_elems);

static size_t ieee80211_assoc_link_elems(struct ieee80211_sub_if_data *sdata,
					 struct sk_buff *skb, u16 *capab,
					 const struct element *ext_capa,
					 const u8 *extra_elems,
					 size_t extra_elems_len,
					 unsigned int link_id,
					 struct ieee80211_link_data *link,
					 u16 *present_elems)
{
	enum nl80211_iftype iftype = ieee80211_vif_type_p2p(&sdata->vif);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_assoc_data *assoc_data = ifmgd->assoc_data;
	struct cfg80211_bss *cbss = assoc_data->link[link_id].bss;
	struct ieee80211_channel *chan = cbss->channel;
	const struct ieee80211_sband_iftype_data *iftd;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	enum nl80211_chan_width width = NL80211_CHAN_WIDTH_20;
	struct ieee80211_chanctx_conf *chanctx_conf;
	enum ieee80211_smps_mode smps_mode;
	u16 orig_capab = *capab;
	size_t offset = 0;
	int present_elems_len = 0;
	u8 *pos;
	int i;

#define ADD_PRESENT_ELEM(id) do {					\
	/* need a last for termination - we use 0 == SSID */		\
	if (!WARN_ON(present_elems_len >= PRESENT_ELEMS_MAX - 1))	\
		present_elems[present_elems_len++] = (id);		\
} while (0)
#define ADD_PRESENT_EXT_ELEM(id) ADD_PRESENT_ELEM(PRESENT_ELEM_EXT_OFFS | (id))

	if (link)
		smps_mode = link->smps_mode;
	else if (sdata->u.mgd.powersave)
		smps_mode = IEEE80211_SMPS_DYNAMIC;
	else
		smps_mode = IEEE80211_SMPS_OFF;

	if (link) {
		/*
		 * 5/10 MHz scenarios are only viable without MLO, in which
		 * case this pointer should be used ... All of this is a bit
		 * unclear though, not sure this even works at all.
		 */
		rcu_read_lock();
		chanctx_conf = rcu_dereference(link->conf->chanctx_conf);
		if (chanctx_conf)
			width = chanctx_conf->def.width;
		rcu_read_unlock();
	}

	sband = local->hw.wiphy->bands[chan->band];
	iftd = ieee80211_get_sband_iftype_data(sband, iftype);

	if (sband->band == NL80211_BAND_2GHZ) {
		*capab |= WLAN_CAPABILITY_SHORT_SLOT_TIME;
		*capab |= WLAN_CAPABILITY_SHORT_PREAMBLE;
	}

	if ((cbss->capability & WLAN_CAPABILITY_SPECTRUM_MGMT) &&
	    ieee80211_hw_check(&local->hw, SPECTRUM_MGMT))
		*capab |= WLAN_CAPABILITY_SPECTRUM_MGMT;

	if (sband->band != NL80211_BAND_S1GHZ)
		ieee80211_assoc_add_rates(skb, width, sband, assoc_data);

	if (*capab & WLAN_CAPABILITY_SPECTRUM_MGMT ||
	    *capab & WLAN_CAPABILITY_RADIO_MEASURE) {
		struct cfg80211_chan_def chandef = {
			.width = width,
			.chan = chan,
		};

		pos = skb_put(skb, 4);
		*pos++ = WLAN_EID_PWR_CAPABILITY;
		*pos++ = 2;
		*pos++ = 0; /* min tx power */
		 /* max tx power */
		*pos++ = ieee80211_chandef_max_power(&chandef);
		ADD_PRESENT_ELEM(WLAN_EID_PWR_CAPABILITY);
	}

	/*
	 * Per spec, we shouldn't include the list of channels if we advertise
	 * support for extended channel switching, but we've always done that;
	 * (for now?) apply this restriction only on the (new) 6 GHz band.
	 */
	if (*capab & WLAN_CAPABILITY_SPECTRUM_MGMT &&
	    (sband->band != NL80211_BAND_6GHZ ||
	     !ext_capa || ext_capa->datalen < 1 ||
	     !(ext_capa->data[0] & WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING))) {
		/* TODO: get this in reg domain format */
		pos = skb_put(skb, 2 * sband->n_channels + 2);
		*pos++ = WLAN_EID_SUPPORTED_CHANNELS;
		*pos++ = 2 * sband->n_channels;
		for (i = 0; i < sband->n_channels; i++) {
			int cf = sband->channels[i].center_freq;

			*pos++ = ieee80211_frequency_to_channel(cf);
			*pos++ = 1; /* one channel in the subband*/
		}
		ADD_PRESENT_ELEM(WLAN_EID_SUPPORTED_CHANNELS);
	}

	/* if present, add any custom IEs that go before HT */
	offset = ieee80211_add_before_ht_elems(skb, extra_elems,
					       extra_elems_len,
					       offset);

	if (sband->band != NL80211_BAND_6GHZ &&
	    !(assoc_data->link[link_id].conn_flags & IEEE80211_CONN_DISABLE_HT)) {
		ieee80211_add_ht_ie(sdata, skb,
				    assoc_data->link[link_id].ap_ht_param,
				    sband, chan, smps_mode,
				    assoc_data->link[link_id].conn_flags);
		ADD_PRESENT_ELEM(WLAN_EID_HT_CAPABILITY);
	}

	/* if present, add any custom IEs that go before VHT */
	offset = ieee80211_add_before_vht_elems(skb, extra_elems,
						extra_elems_len,
						offset);

	if (sband->band != NL80211_BAND_6GHZ &&
	    !(assoc_data->link[link_id].conn_flags & IEEE80211_CONN_DISABLE_VHT)) {
		bool mu_mimo_owner =
			ieee80211_add_vht_ie(sdata, skb, sband,
					     &assoc_data->link[link_id].ap_vht_cap,
					     assoc_data->link[link_id].conn_flags);

		if (link)
			link->conf->mu_mimo_owner = mu_mimo_owner;
		ADD_PRESENT_ELEM(WLAN_EID_VHT_CAPABILITY);
	}

	/*
	 * If AP doesn't support HT, mark HE and EHT as disabled.
	 * If on the 5GHz band, make sure it supports VHT.
	 */
	if (assoc_data->link[link_id].conn_flags & IEEE80211_CONN_DISABLE_HT ||
	    (sband->band == NL80211_BAND_5GHZ &&
	     assoc_data->link[link_id].conn_flags & IEEE80211_CONN_DISABLE_VHT))
		assoc_data->link[link_id].conn_flags |=
			IEEE80211_CONN_DISABLE_HE |
			IEEE80211_CONN_DISABLE_EHT;

	/* if present, add any custom IEs that go before HE */
	offset = ieee80211_add_before_he_elems(skb, extra_elems,
					       extra_elems_len,
					       offset);

	if (!(assoc_data->link[link_id].conn_flags & IEEE80211_CONN_DISABLE_HE)) {
		ieee80211_add_he_ie(sdata, skb, sband, smps_mode,
				    assoc_data->link[link_id].conn_flags);
		ADD_PRESENT_EXT_ELEM(WLAN_EID_EXT_HE_CAPABILITY);
	}

	/*
	 * careful - need to know about all the present elems before
	 * calling ieee80211_assoc_add_ml_elem(), so add this one if
	 * we're going to put it after the ML element
	 */
	if (!(assoc_data->link[link_id].conn_flags & IEEE80211_CONN_DISABLE_EHT))
		ADD_PRESENT_EXT_ELEM(WLAN_EID_EXT_EHT_CAPABILITY);

	if (link_id == assoc_data->assoc_link_id)
		ieee80211_assoc_add_ml_elem(sdata, skb, orig_capab, ext_capa,
					    present_elems);

	/* crash if somebody gets it wrong */
	present_elems = NULL;

	if (!(assoc_data->link[link_id].conn_flags & IEEE80211_CONN_DISABLE_EHT))
		ieee80211_add_eht_ie(sdata, skb, sband);

	if (sband->band == NL80211_BAND_S1GHZ) {
		ieee80211_add_aid_request_ie(sdata, skb);
		ieee80211_add_s1g_capab_ie(sdata, &sband->s1g_cap, skb);
	}

	if (iftd && iftd->vendor_elems.data && iftd->vendor_elems.len)
		skb_put_data(skb, iftd->vendor_elems.data, iftd->vendor_elems.len);

	if (link)
		link->u.mgd.conn_flags = assoc_data->link[link_id].conn_flags;

	return offset;
}

static void ieee80211_add_non_inheritance_elem(struct sk_buff *skb,
					       const u16 *outer,
					       const u16 *inner)
{
	unsigned int skb_len = skb->len;
	bool added = false;
	int i, j;
	u8 *len, *list_len = NULL;

	skb_put_u8(skb, WLAN_EID_EXTENSION);
	len = skb_put(skb, 1);
	skb_put_u8(skb, WLAN_EID_EXT_NON_INHERITANCE);

	for (i = 0; i < PRESENT_ELEMS_MAX && outer[i]; i++) {
		u16 elem = outer[i];
		bool have_inner = false;
		bool at_extension = false;

		/* should at least be sorted in the sense of normal -> ext */
		WARN_ON(at_extension && elem < PRESENT_ELEM_EXT_OFFS);

		/* switch to extension list */
		if (!at_extension && elem >= PRESENT_ELEM_EXT_OFFS) {
			at_extension = true;
			if (!list_len)
				skb_put_u8(skb, 0);
			list_len = NULL;
		}

		for (j = 0; j < PRESENT_ELEMS_MAX && inner[j]; j++) {
			if (elem == inner[j]) {
				have_inner = true;
				break;
			}
		}

		if (have_inner)
			continue;

		if (!list_len) {
			list_len = skb_put(skb, 1);
			*list_len = 0;
		}
		*list_len += 1;
		skb_put_u8(skb, (u8)elem);
	}

	if (!added)
		skb_trim(skb, skb_len);
	else
		*len = skb->len - skb_len - 2;
}

static void ieee80211_assoc_add_ml_elem(struct ieee80211_sub_if_data *sdata,
					struct sk_buff *skb, u16 capab,
					const struct element *ext_capa,
					const u16 *outer_present_elems)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_assoc_data *assoc_data = ifmgd->assoc_data;
	struct ieee80211_multi_link_elem *ml_elem;
	struct ieee80211_mle_basic_common_info *common;
	const struct wiphy_iftype_ext_capab *ift_ext_capa;
	__le16 eml_capa = 0, mld_capa_ops = 0;
	unsigned int link_id;
	u8 *ml_elem_len;
	void *capab_pos;

	if (!sdata->vif.valid_links)
		return;

	ift_ext_capa = cfg80211_get_iftype_ext_capa(local->hw.wiphy,
						    ieee80211_vif_type_p2p(&sdata->vif));
	if (ift_ext_capa) {
		eml_capa = cpu_to_le16(ift_ext_capa->eml_capabilities);
		mld_capa_ops = cpu_to_le16(ift_ext_capa->mld_capa_and_ops);
	}

	skb_put_u8(skb, WLAN_EID_EXTENSION);
	ml_elem_len = skb_put(skb, 1);
	skb_put_u8(skb, WLAN_EID_EXT_EHT_MULTI_LINK);
	ml_elem = skb_put(skb, sizeof(*ml_elem));
	ml_elem->control =
		cpu_to_le16(IEEE80211_ML_CONTROL_TYPE_BASIC |
			    IEEE80211_MLC_BASIC_PRES_MLD_CAPA_OP);
	common = skb_put(skb, sizeof(*common));
	common->len = sizeof(*common) +
		      2;  /* MLD capa/ops */
	memcpy(common->mld_mac_addr, sdata->vif.addr, ETH_ALEN);

	/* add EML_CAPA only if needed, see Draft P802.11be_D2.1, 35.3.17 */
	if (eml_capa &
	    cpu_to_le16((IEEE80211_EML_CAP_EMLSR_SUPP |
			 IEEE80211_EML_CAP_EMLMR_SUPPORT))) {
		common->len += 2; /* EML capabilities */
		ml_elem->control |=
			cpu_to_le16(IEEE80211_MLC_BASIC_PRES_EML_CAPA);
		skb_put_data(skb, &eml_capa, sizeof(eml_capa));
	}
	/* need indication from userspace to support this */
	mld_capa_ops &= ~cpu_to_le16(IEEE80211_MLD_CAP_OP_TID_TO_LINK_MAP_NEG_SUPP);
	skb_put_data(skb, &mld_capa_ops, sizeof(mld_capa_ops));

	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++) {
		u16 link_present_elems[PRESENT_ELEMS_MAX] = {};
		const u8 *extra_elems;
		size_t extra_elems_len;
		size_t extra_used;
		u8 *subelem_len = NULL;
		__le16 ctrl;

		if (!assoc_data->link[link_id].bss ||
		    link_id == assoc_data->assoc_link_id)
			continue;

		extra_elems = assoc_data->link[link_id].elems;
		extra_elems_len = assoc_data->link[link_id].elems_len;

		skb_put_u8(skb, IEEE80211_MLE_SUBELEM_PER_STA_PROFILE);
		subelem_len = skb_put(skb, 1);

		ctrl = cpu_to_le16(link_id |
				   IEEE80211_MLE_STA_CONTROL_COMPLETE_PROFILE |
				   IEEE80211_MLE_STA_CONTROL_STA_MAC_ADDR_PRESENT);
		skb_put_data(skb, &ctrl, sizeof(ctrl));
		skb_put_u8(skb, 1 + ETH_ALEN); /* STA Info Length */
		skb_put_data(skb, assoc_data->link[link_id].addr,
			     ETH_ALEN);
		/*
		 * Now add the contents of the (re)association request,
		 * but the "listen interval" and "current AP address"
		 * (if applicable) are skipped. So we only have
		 * the capability field (remember the position and fill
		 * later), followed by the elements added below by
		 * calling ieee80211_assoc_link_elems().
		 */
		capab_pos = skb_put(skb, 2);

		extra_used = ieee80211_assoc_link_elems(sdata, skb, &capab,
							ext_capa,
							extra_elems,
							extra_elems_len,
							link_id, NULL,
							link_present_elems);
		if (extra_elems)
			skb_put_data(skb, extra_elems + extra_used,
				     extra_elems_len - extra_used);

		put_unaligned_le16(capab, capab_pos);

		ieee80211_add_non_inheritance_elem(skb, outer_present_elems,
						   link_present_elems);

		ieee80211_fragment_element(skb, subelem_len);
	}

	ieee80211_fragment_element(skb, ml_elem_len);
}

static int ieee80211_send_assoc(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_assoc_data *assoc_data = ifmgd->assoc_data;
	struct ieee80211_link_data *link;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u8 *pos, qos_info, *ie_start;
	size_t offset, noffset;
	u16 capab = WLAN_CAPABILITY_ESS, link_capab;
	__le16 listen_int;
	struct element *ext_capa = NULL;
	enum nl80211_iftype iftype = ieee80211_vif_type_p2p(&sdata->vif);
	struct ieee80211_prep_tx_info info = {};
	unsigned int link_id, n_links = 0;
	u16 present_elems[PRESENT_ELEMS_MAX] = {};
	void *capab_pos;
	size_t size;
	int ret;

	/* we know it's writable, cast away the const */
	if (assoc_data->ie_len)
		ext_capa = (void *)cfg80211_find_elem(WLAN_EID_EXT_CAPABILITY,
						      assoc_data->ie,
						      assoc_data->ie_len);

	sdata_assert_lock(sdata);

	size = local->hw.extra_tx_headroom +
	       sizeof(*mgmt) + /* bit too much but doesn't matter */
	       2 + assoc_data->ssid_len + /* SSID */
	       assoc_data->ie_len + /* extra IEs */
	       (assoc_data->fils_kek_len ? 16 /* AES-SIV */ : 0) +
	       9; /* WMM */

	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++) {
		struct cfg80211_bss *cbss = assoc_data->link[link_id].bss;
		const struct ieee80211_sband_iftype_data *iftd;
		struct ieee80211_supported_band *sband;

		if (!cbss)
			continue;

		sband = local->hw.wiphy->bands[cbss->channel->band];

		n_links++;
		/* add STA profile elements length */
		size += assoc_data->link[link_id].elems_len;
		/* and supported rates length */
		size += 4 + sband->n_bitrates;
		/* supported channels */
		size += 2 + 2 * sband->n_channels;

		iftd = ieee80211_get_sband_iftype_data(sband, iftype);
		if (iftd)
			size += iftd->vendor_elems.len;

		/* power capability */
		size += 4;

		/* HT, VHT, HE, EHT */
		size += 2 + sizeof(struct ieee80211_ht_cap);
		size += 2 + sizeof(struct ieee80211_vht_cap);
		size += 2 + 1 + sizeof(struct ieee80211_he_cap_elem) +
			sizeof(struct ieee80211_he_mcs_nss_supp) +
			IEEE80211_HE_PPE_THRES_MAX_LEN;

		if (sband->band == NL80211_BAND_6GHZ)
			size += 2 + 1 + sizeof(struct ieee80211_he_6ghz_capa);

		size += 2 + 1 + sizeof(struct ieee80211_eht_cap_elem) +
			sizeof(struct ieee80211_eht_mcs_nss_supp) +
			IEEE80211_EHT_PPE_THRES_MAX_LEN;

		/* non-inheritance element */
		size += 2 + 2 + PRESENT_ELEMS_MAX;

		/* should be the same across all BSSes */
		if (cbss->capability & WLAN_CAPABILITY_PRIVACY)
			capab |= WLAN_CAPABILITY_PRIVACY;
	}

	if (sdata->vif.valid_links) {
		/* consider the multi-link element with STA profile */
		size += sizeof(struct ieee80211_multi_link_elem);
		/* max common info field in basic multi-link element */
		size += sizeof(struct ieee80211_mle_basic_common_info) +
			2 + /* capa & op */
			2; /* EML capa */

		/*
		 * The capability elements were already considered above;
		 * note this over-estimates a bit because there's no
		 * STA profile for the assoc link.
		 */
		size += (n_links - 1) *
			(1 + 1 + /* subelement ID/length */
			 2 + /* STA control */
			 1 + ETH_ALEN + 2 /* STA Info field */);
	}

	link = sdata_dereference(sdata->link[assoc_data->assoc_link_id], sdata);
	if (WARN_ON(!link))
		return -EINVAL;

	if (WARN_ON(!assoc_data->link[assoc_data->assoc_link_id].bss))
		return -EINVAL;

	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	if (ifmgd->flags & IEEE80211_STA_ENABLE_RRM)
		capab |= WLAN_CAPABILITY_RADIO_MEASURE;

	/* Set MBSSID support for HE AP if needed */
	if (ieee80211_hw_check(&local->hw, SUPPORTS_ONLY_HE_MULTI_BSSID) &&
	    !(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HE) &&
	    ext_capa && ext_capa->datalen >= 3)
		ext_capa->data[2] |= WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT;

	mgmt = skb_put_zero(skb, 24);
	memcpy(mgmt->da, sdata->vif.cfg.ap_addr, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, sdata->vif.cfg.ap_addr, ETH_ALEN);

	listen_int = cpu_to_le16(assoc_data->s1g ?
			ieee80211_encode_usf(local->hw.conf.listen_interval) :
			local->hw.conf.listen_interval);
	if (!is_zero_ether_addr(assoc_data->prev_ap_addr)) {
		skb_put(skb, 10);
		mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						  IEEE80211_STYPE_REASSOC_REQ);
		capab_pos = &mgmt->u.reassoc_req.capab_info;
		mgmt->u.reassoc_req.listen_interval = listen_int;
		memcpy(mgmt->u.reassoc_req.current_ap,
		       assoc_data->prev_ap_addr, ETH_ALEN);
		info.subtype = IEEE80211_STYPE_REASSOC_REQ;
	} else {
		skb_put(skb, 4);
		mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						  IEEE80211_STYPE_ASSOC_REQ);
		capab_pos = &mgmt->u.assoc_req.capab_info;
		mgmt->u.assoc_req.listen_interval = listen_int;
		info.subtype = IEEE80211_STYPE_ASSOC_REQ;
	}

	/* SSID */
	pos = skb_put(skb, 2 + assoc_data->ssid_len);
	ie_start = pos;
	*pos++ = WLAN_EID_SSID;
	*pos++ = assoc_data->ssid_len;
	memcpy(pos, assoc_data->ssid, assoc_data->ssid_len);

	/* add the elements for the assoc (main) link */
	link_capab = capab;
	offset = ieee80211_assoc_link_elems(sdata, skb, &link_capab,
					    ext_capa,
					    assoc_data->ie,
					    assoc_data->ie_len,
					    assoc_data->assoc_link_id, link,
					    present_elems);
	put_unaligned_le16(link_capab, capab_pos);

	/* if present, add any custom non-vendor IEs */
	if (assoc_data->ie_len) {
		noffset = ieee80211_ie_split_vendor(assoc_data->ie,
						    assoc_data->ie_len,
						    offset);
		skb_put_data(skb, assoc_data->ie + offset, noffset - offset);
		offset = noffset;
	}

	if (assoc_data->wmm) {
		if (assoc_data->uapsd) {
			qos_info = ifmgd->uapsd_queues;
			qos_info |= (ifmgd->uapsd_max_sp_len <<
				     IEEE80211_WMM_IE_STA_QOSINFO_SP_SHIFT);
		} else {
			qos_info = 0;
		}

		pos = ieee80211_add_wmm_info_ie(skb_put(skb, 9), qos_info);
	}

	/* add any remaining custom (i.e. vendor specific here) IEs */
	if (assoc_data->ie_len) {
		noffset = assoc_data->ie_len;
		skb_put_data(skb, assoc_data->ie + offset, noffset - offset);
	}

	if (assoc_data->fils_kek_len) {
		ret = fils_encrypt_assoc_req(skb, assoc_data);
		if (ret < 0) {
			dev_kfree_skb(skb);
			return ret;
		}
	}

	pos = skb_tail_pointer(skb);
	kfree(ifmgd->assoc_req_ies);
	ifmgd->assoc_req_ies = kmemdup(ie_start, pos - ie_start, GFP_ATOMIC);
	if (!ifmgd->assoc_req_ies) {
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	ifmgd->assoc_req_ies_len = pos - ie_start;

	drv_mgd_prepare_tx(local, sdata, &info);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS |
						IEEE80211_TX_INTFL_MLME_CONN_TX;
	ieee80211_tx_skb(sdata, skb);

	return 0;
}

void ieee80211_send_pspoll(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_pspoll *pspoll;
	struct sk_buff *skb;

	skb = ieee80211_pspoll_get(&local->hw, &sdata->vif);
	if (!skb)
		return;

	pspoll = (struct ieee80211_pspoll *) skb->data;
	pspoll->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	ieee80211_tx_skb(sdata, skb);
}

void ieee80211_send_nullfunc(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *sdata,
			     bool powersave)
{
	struct sk_buff *skb;
	struct ieee80211_hdr_3addr *nullfunc;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	skb = ieee80211_nullfunc_get(&local->hw, &sdata->vif, -1,
				     !ieee80211_hw_check(&local->hw,
							 DOESNT_SUPPORT_QOS_NDP));
	if (!skb)
		return;

	nullfunc = (struct ieee80211_hdr_3addr *) skb->data;
	if (powersave)
		nullfunc->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT |
					IEEE80211_TX_INTFL_OFFCHAN_TX_OK;

	if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;

	if (ifmgd->flags & IEEE80211_STA_CONNECTION_POLL)
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_USE_MINRATE;

	ieee80211_tx_skb(sdata, skb);
}

void ieee80211_send_4addr_nullfunc(struct ieee80211_local *local,
				   struct ieee80211_sub_if_data *sdata)
{
	struct sk_buff *skb;
	struct ieee80211_hdr *nullfunc;
	__le16 fc;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + 30);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	nullfunc = skb_put_zero(skb, 30);
	fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC |
			 IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS);
	nullfunc->frame_control = fc;
	memcpy(nullfunc->addr1, sdata->deflink.u.mgd.bssid, ETH_ALEN);
	memcpy(nullfunc->addr2, sdata->vif.addr, ETH_ALEN);
	memcpy(nullfunc->addr3, sdata->deflink.u.mgd.bssid, ETH_ALEN);
	memcpy(nullfunc->addr4, sdata->vif.addr, ETH_ALEN);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_USE_MINRATE;
	ieee80211_tx_skb(sdata, skb);
}

/* spectrum management related things */
static void ieee80211_chswitch_work(struct work_struct *work)
{
	struct ieee80211_link_data *link =
		container_of(work, struct ieee80211_link_data, u.mgd.chswitch_work);
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	int ret;

	if (!ieee80211_sdata_running(sdata))
		return;

	sdata_lock(sdata);
	mutex_lock(&local->mtx);
	mutex_lock(&local->chanctx_mtx);

	if (!ifmgd->associated)
		goto out;

	if (!link->conf->csa_active)
		goto out;

	/*
	 * using reservation isn't immediate as it may be deferred until later
	 * with multi-vif. once reservation is complete it will re-schedule the
	 * work with no reserved_chanctx so verify chandef to check if it
	 * completed successfully
	 */

	if (link->reserved_chanctx) {
		/*
		 * with multi-vif csa driver may call ieee80211_csa_finish()
		 * many times while waiting for other interfaces to use their
		 * reservations
		 */
		if (link->reserved_ready)
			goto out;

		ret = ieee80211_link_use_reserved_context(link);
		if (ret) {
			sdata_info(sdata,
				   "failed to use reserved channel context, disconnecting (err=%d)\n",
				   ret);
			ieee80211_queue_work(&sdata->local->hw,
					     &ifmgd->csa_connection_drop_work);
			goto out;
		}

		goto out;
	}

	if (!cfg80211_chandef_identical(&link->conf->chandef,
					&link->csa_chandef)) {
		sdata_info(sdata,
			   "failed to finalize channel switch, disconnecting\n");
		ieee80211_queue_work(&sdata->local->hw,
				     &ifmgd->csa_connection_drop_work);
		goto out;
	}

	link->u.mgd.csa_waiting_bcn = true;

	ieee80211_sta_reset_beacon_monitor(sdata);
	ieee80211_sta_reset_conn_monitor(sdata);

out:
	mutex_unlock(&local->chanctx_mtx);
	mutex_unlock(&local->mtx);
	sdata_unlock(sdata);
}

static void ieee80211_chswitch_post_beacon(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	int ret;

	sdata_assert_lock(sdata);

	WARN_ON(!link->conf->csa_active);

	if (link->csa_block_tx) {
		ieee80211_wake_vif_queues(local, sdata,
					  IEEE80211_QUEUE_STOP_REASON_CSA);
		link->csa_block_tx = false;
	}

	link->conf->csa_active = false;
	link->u.mgd.csa_waiting_bcn = false;
	/*
	 * If the CSA IE is still present on the beacon after the switch,
	 * we need to consider it as a new CSA (possibly to self).
	 */
	link->u.mgd.beacon_crc_valid = false;

	ret = drv_post_channel_switch(sdata);
	if (ret) {
		sdata_info(sdata,
			   "driver post channel switch failed, disconnecting\n");
		ieee80211_queue_work(&local->hw,
				     &ifmgd->csa_connection_drop_work);
		return;
	}

	cfg80211_ch_switch_notify(sdata->dev, &link->reserved_chandef, 0);
}

void ieee80211_chswitch_done(struct ieee80211_vif *vif, bool success)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (WARN_ON(sdata->vif.valid_links))
		success = false;

	trace_api_chswitch_done(sdata, success);
	if (!success) {
		sdata_info(sdata,
			   "driver channel switch failed, disconnecting\n");
		ieee80211_queue_work(&sdata->local->hw,
				     &ifmgd->csa_connection_drop_work);
	} else {
		ieee80211_queue_work(&sdata->local->hw,
				     &sdata->deflink.u.mgd.chswitch_work);
	}
}
EXPORT_SYMBOL(ieee80211_chswitch_done);

static void ieee80211_chswitch_timer(struct timer_list *t)
{
	struct ieee80211_link_data *link =
		from_timer(link, t, u.mgd.chswitch_timer);

	ieee80211_queue_work(&link->sdata->local->hw,
			     &link->u.mgd.chswitch_work);
}

static void
ieee80211_sta_abort_chanswitch(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;

	if (!local->ops->abort_channel_switch)
		return;

	mutex_lock(&local->mtx);

	mutex_lock(&local->chanctx_mtx);
	ieee80211_link_unreserve_chanctx(link);
	mutex_unlock(&local->chanctx_mtx);

	if (link->csa_block_tx)
		ieee80211_wake_vif_queues(local, sdata,
					  IEEE80211_QUEUE_STOP_REASON_CSA);

	link->csa_block_tx = false;
	link->conf->csa_active = false;

	mutex_unlock(&local->mtx);

	drv_abort_channel_switch(sdata);
}

static void
ieee80211_sta_process_chanswitch(struct ieee80211_link_data *link,
				 u64 timestamp, u32 device_timestamp,
				 struct ieee802_11_elems *elems,
				 bool beacon)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct cfg80211_bss *cbss = link->u.mgd.bss;
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *chanctx;
	enum nl80211_band current_band;
	struct ieee80211_csa_ie csa_ie;
	struct ieee80211_channel_switch ch_switch;
	struct ieee80211_bss *bss;
	int res;

	sdata_assert_lock(sdata);

	if (!cbss)
		return;

	if (local->scanning)
		return;

	current_band = cbss->channel->band;
	bss = (void *)cbss->priv;
	res = ieee80211_parse_ch_switch_ie(sdata, elems, current_band,
					   bss->vht_cap_info,
					   link->u.mgd.conn_flags,
					   link->u.mgd.bssid, &csa_ie);

	if (!res) {
		ch_switch.timestamp = timestamp;
		ch_switch.device_timestamp = device_timestamp;
		ch_switch.block_tx = csa_ie.mode;
		ch_switch.chandef = csa_ie.chandef;
		ch_switch.count = csa_ie.count;
		ch_switch.delay = csa_ie.max_switch_time;
	}

	if (res < 0)
		goto lock_and_drop_connection;

	if (beacon && link->conf->csa_active &&
	    !link->u.mgd.csa_waiting_bcn) {
		if (res)
			ieee80211_sta_abort_chanswitch(link);
		else
			drv_channel_switch_rx_beacon(sdata, &ch_switch);
		return;
	} else if (link->conf->csa_active || res) {
		/* disregard subsequent announcements if already processing */
		return;
	}

	if (link->conf->chandef.chan->band !=
	    csa_ie.chandef.chan->band) {
		sdata_info(sdata,
			   "AP %pM switches to different band (%d MHz, width:%d, CF1/2: %d/%d MHz), disconnecting\n",
			   link->u.mgd.bssid,
			   csa_ie.chandef.chan->center_freq,
			   csa_ie.chandef.width, csa_ie.chandef.center_freq1,
			   csa_ie.chandef.center_freq2);
		goto lock_and_drop_connection;
	}

	if (!cfg80211_chandef_usable(local->hw.wiphy, &csa_ie.chandef,
				     IEEE80211_CHAN_DISABLED)) {
		sdata_info(sdata,
			   "AP %pM switches to unsupported channel "
			   "(%d.%03d MHz, width:%d, CF1/2: %d.%03d/%d MHz), "
			   "disconnecting\n",
			   link->u.mgd.bssid,
			   csa_ie.chandef.chan->center_freq,
			   csa_ie.chandef.chan->freq_offset,
			   csa_ie.chandef.width, csa_ie.chandef.center_freq1,
			   csa_ie.chandef.freq1_offset,
			   csa_ie.chandef.center_freq2);
		goto lock_and_drop_connection;
	}

	if (cfg80211_chandef_identical(&csa_ie.chandef,
				       &link->conf->chandef) &&
	    (!csa_ie.mode || !beacon)) {
		if (link->u.mgd.csa_ignored_same_chan)
			return;
		sdata_info(sdata,
			   "AP %pM tries to chanswitch to same channel, ignore\n",
			   link->u.mgd.bssid);
		link->u.mgd.csa_ignored_same_chan = true;
		return;
	}

	/*
	 * Drop all TDLS peers - either we disconnect or move to a different
	 * channel from this point on. There's no telling what our peer will do.
	 * The TDLS WIDER_BW scenario is also problematic, as peers might now
	 * have an incompatible wider chandef.
	 */
	ieee80211_teardown_tdls_peers(sdata);

	mutex_lock(&local->mtx);
	mutex_lock(&local->chanctx_mtx);
	conf = rcu_dereference_protected(link->conf->chanctx_conf,
					 lockdep_is_held(&local->chanctx_mtx));
	if (!conf) {
		sdata_info(sdata,
			   "no channel context assigned to vif?, disconnecting\n");
		goto drop_connection;
	}

	chanctx = container_of(conf, struct ieee80211_chanctx, conf);

	if (local->use_chanctx &&
	    !ieee80211_hw_check(&local->hw, CHANCTX_STA_CSA)) {
		sdata_info(sdata,
			   "driver doesn't support chan-switch with channel contexts\n");
		goto drop_connection;
	}

	if (drv_pre_channel_switch(sdata, &ch_switch)) {
		sdata_info(sdata,
			   "preparing for channel switch failed, disconnecting\n");
		goto drop_connection;
	}

	res = ieee80211_link_reserve_chanctx(link, &csa_ie.chandef,
					     chanctx->mode, false);
	if (res) {
		sdata_info(sdata,
			   "failed to reserve channel context for channel switch, disconnecting (err=%d)\n",
			   res);
		goto drop_connection;
	}
	mutex_unlock(&local->chanctx_mtx);

	link->conf->csa_active = true;
	link->csa_chandef = csa_ie.chandef;
	link->csa_block_tx = csa_ie.mode;
	link->u.mgd.csa_ignored_same_chan = false;
	link->u.mgd.beacon_crc_valid = false;

	if (link->csa_block_tx)
		ieee80211_stop_vif_queues(local, sdata,
					  IEEE80211_QUEUE_STOP_REASON_CSA);
	mutex_unlock(&local->mtx);

	cfg80211_ch_switch_started_notify(sdata->dev, &csa_ie.chandef, 0,
					  csa_ie.count, csa_ie.mode);

	if (local->ops->channel_switch) {
		/* use driver's channel switch callback */
		drv_channel_switch(local, sdata, &ch_switch);
		return;
	}

	/* channel switch handled in software */
	if (csa_ie.count <= 1)
		ieee80211_queue_work(&local->hw, &link->u.mgd.chswitch_work);
	else
		mod_timer(&link->u.mgd.chswitch_timer,
			  TU_TO_EXP_TIME((csa_ie.count - 1) *
					 cbss->beacon_interval));
	return;
 lock_and_drop_connection:
	mutex_lock(&local->mtx);
	mutex_lock(&local->chanctx_mtx);
 drop_connection:
	/*
	 * This is just so that the disconnect flow will know that
	 * we were trying to switch channel and failed. In case the
	 * mode is 1 (we are not allowed to Tx), we will know not to
	 * send a deauthentication frame. Those two fields will be
	 * reset when the disconnection worker runs.
	 */
	link->conf->csa_active = true;
	link->csa_block_tx = csa_ie.mode;

	ieee80211_queue_work(&local->hw, &ifmgd->csa_connection_drop_work);
	mutex_unlock(&local->chanctx_mtx);
	mutex_unlock(&local->mtx);
}

static bool
ieee80211_find_80211h_pwr_constr(struct ieee80211_sub_if_data *sdata,
				 struct ieee80211_channel *channel,
				 const u8 *country_ie, u8 country_ie_len,
				 const u8 *pwr_constr_elem,
				 int *chan_pwr, int *pwr_reduction)
{
	struct ieee80211_country_ie_triplet *triplet;
	int chan = ieee80211_frequency_to_channel(channel->center_freq);
	int i, chan_increment;
	bool have_chan_pwr = false;

	/* Invalid IE */
	if (country_ie_len % 2 || country_ie_len < IEEE80211_COUNTRY_IE_MIN_LEN)
		return false;

	triplet = (void *)(country_ie + 3);
	country_ie_len -= 3;

	switch (channel->band) {
	default:
		WARN_ON_ONCE(1);
		fallthrough;
	case NL80211_BAND_2GHZ:
	case NL80211_BAND_60GHZ:
	case NL80211_BAND_LC:
		chan_increment = 1;
		break;
	case NL80211_BAND_5GHZ:
		chan_increment = 4;
		break;
	case NL80211_BAND_6GHZ:
		/*
		 * In the 6 GHz band, the "maximum transmit power level"
		 * field in the triplets is reserved, and thus will be
		 * zero and we shouldn't use it to control TX power.
		 * The actual TX power will be given in the transmit
		 * power envelope element instead.
		 */
		return false;
	}

	/* find channel */
	while (country_ie_len >= 3) {
		u8 first_channel = triplet->chans.first_channel;

		if (first_channel >= IEEE80211_COUNTRY_EXTENSION_ID)
			goto next;

		for (i = 0; i < triplet->chans.num_channels; i++) {
			if (first_channel + i * chan_increment == chan) {
				have_chan_pwr = true;
				*chan_pwr = triplet->chans.max_power;
				break;
			}
		}
		if (have_chan_pwr)
			break;

 next:
		triplet++;
		country_ie_len -= 3;
	}

	if (have_chan_pwr && pwr_constr_elem)
		*pwr_reduction = *pwr_constr_elem;
	else
		*pwr_reduction = 0;

	return have_chan_pwr;
}

static void ieee80211_find_cisco_dtpc(struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_channel *channel,
				      const u8 *cisco_dtpc_ie,
				      int *pwr_level)
{
	/* From practical testing, the first data byte of the DTPC element
	 * seems to contain the requested dBm level, and the CLI on Cisco
	 * APs clearly state the range is -127 to 127 dBm, which indicates
	 * a signed byte, although it seemingly never actually goes negative.
	 * The other byte seems to always be zero.
	 */
	*pwr_level = (__s8)cisco_dtpc_ie[4];
}

static u32 ieee80211_handle_pwr_constr(struct ieee80211_link_data *link,
				       struct ieee80211_channel *channel,
				       struct ieee80211_mgmt *mgmt,
				       const u8 *country_ie, u8 country_ie_len,
				       const u8 *pwr_constr_ie,
				       const u8 *cisco_dtpc_ie)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	bool has_80211h_pwr = false, has_cisco_pwr = false;
	int chan_pwr = 0, pwr_reduction_80211h = 0;
	int pwr_level_cisco, pwr_level_80211h;
	int new_ap_level;
	__le16 capab = mgmt->u.probe_resp.capab_info;

	if (ieee80211_is_s1g_beacon(mgmt->frame_control))
		return 0;	/* TODO */

	if (country_ie &&
	    (capab & cpu_to_le16(WLAN_CAPABILITY_SPECTRUM_MGMT) ||
	     capab & cpu_to_le16(WLAN_CAPABILITY_RADIO_MEASURE))) {
		has_80211h_pwr = ieee80211_find_80211h_pwr_constr(
			sdata, channel, country_ie, country_ie_len,
			pwr_constr_ie, &chan_pwr, &pwr_reduction_80211h);
		pwr_level_80211h =
			max_t(int, 0, chan_pwr - pwr_reduction_80211h);
	}

	if (cisco_dtpc_ie) {
		ieee80211_find_cisco_dtpc(
			sdata, channel, cisco_dtpc_ie, &pwr_level_cisco);
		has_cisco_pwr = true;
	}

	if (!has_80211h_pwr && !has_cisco_pwr)
		return 0;

	/* If we have both 802.11h and Cisco DTPC, apply both limits
	 * by picking the smallest of the two power levels advertised.
	 */
	if (has_80211h_pwr &&
	    (!has_cisco_pwr || pwr_level_80211h <= pwr_level_cisco)) {
		new_ap_level = pwr_level_80211h;

		if (link->ap_power_level == new_ap_level)
			return 0;

		sdata_dbg(sdata,
			  "Limiting TX power to %d (%d - %d) dBm as advertised by %pM\n",
			  pwr_level_80211h, chan_pwr, pwr_reduction_80211h,
			  link->u.mgd.bssid);
	} else {  /* has_cisco_pwr is always true here. */
		new_ap_level = pwr_level_cisco;

		if (link->ap_power_level == new_ap_level)
			return 0;

		sdata_dbg(sdata,
			  "Limiting TX power to %d dBm as advertised by %pM\n",
			  pwr_level_cisco, link->u.mgd.bssid);
	}

	link->ap_power_level = new_ap_level;
	if (__ieee80211_recalc_txpower(sdata))
		return BSS_CHANGED_TXPOWER;
	return 0;
}

/* powersave */
static void ieee80211_enable_ps(struct ieee80211_local *local,
				struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_conf *conf = &local->hw.conf;

	/*
	 * If we are scanning right now then the parameters will
	 * take effect when scan finishes.
	 */
	if (local->scanning)
		return;

	if (conf->dynamic_ps_timeout > 0 &&
	    !ieee80211_hw_check(&local->hw, SUPPORTS_DYNAMIC_PS)) {
		mod_timer(&local->dynamic_ps_timer, jiffies +
			  msecs_to_jiffies(conf->dynamic_ps_timeout));
	} else {
		if (ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK))
			ieee80211_send_nullfunc(local, sdata, true);

		if (ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK) &&
		    ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
			return;

		conf->flags |= IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}
}

static void ieee80211_change_ps(struct ieee80211_local *local)
{
	struct ieee80211_conf *conf = &local->hw.conf;

	if (local->ps_sdata) {
		ieee80211_enable_ps(local, local->ps_sdata);
	} else if (conf->flags & IEEE80211_CONF_PS) {
		conf->flags &= ~IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
		del_timer_sync(&local->dynamic_ps_timer);
		cancel_work_sync(&local->dynamic_ps_enable_work);
	}
}

static bool ieee80211_powersave_allowed(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *mgd = &sdata->u.mgd;
	struct sta_info *sta = NULL;
	bool authorized = false;

	if (!mgd->powersave)
		return false;

	if (mgd->broken_ap)
		return false;

	if (!mgd->associated)
		return false;

	if (mgd->flags & IEEE80211_STA_CONNECTION_POLL)
		return false;

	if (!(local->hw.wiphy->flags & WIPHY_FLAG_SUPPORTS_MLO) &&
	    !sdata->deflink.u.mgd.have_beacon)
		return false;

	rcu_read_lock();
	sta = sta_info_get(sdata, sdata->vif.cfg.ap_addr);
	if (sta)
		authorized = test_sta_flag(sta, WLAN_STA_AUTHORIZED);
	rcu_read_unlock();

	return authorized;
}

/* need to hold RTNL or interface lock */
void ieee80211_recalc_ps(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata, *found = NULL;
	int count = 0;
	int timeout;

	if (!ieee80211_hw_check(&local->hw, SUPPORTS_PS) ||
	    ieee80211_hw_check(&local->hw, SUPPORTS_DYNAMIC_PS)) {
		local->ps_sdata = NULL;
		return;
	}

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;
		if (sdata->vif.type == NL80211_IFTYPE_AP) {
			/* If an AP vif is found, then disable PS
			 * by setting the count to zero thereby setting
			 * ps_sdata to NULL.
			 */
			count = 0;
			break;
		}
		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			continue;
		found = sdata;
		count++;
	}

	if (count == 1 && ieee80211_powersave_allowed(found)) {
		u8 dtimper = found->deflink.u.mgd.dtim_period;

		timeout = local->dynamic_ps_forced_timeout;
		if (timeout < 0)
			timeout = 100;
		local->hw.conf.dynamic_ps_timeout = timeout;

		/* If the TIM IE is invalid, pretend the value is 1 */
		if (!dtimper)
			dtimper = 1;

		local->hw.conf.ps_dtim_period = dtimper;
		local->ps_sdata = found;
	} else {
		local->ps_sdata = NULL;
	}

	ieee80211_change_ps(local);
}

void ieee80211_recalc_ps_vif(struct ieee80211_sub_if_data *sdata)
{
	bool ps_allowed = ieee80211_powersave_allowed(sdata);

	if (sdata->vif.cfg.ps != ps_allowed) {
		sdata->vif.cfg.ps = ps_allowed;
		ieee80211_vif_cfg_change_notify(sdata, BSS_CHANGED_PS);
	}
}

void ieee80211_dynamic_ps_disable_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local,
			     dynamic_ps_disable_work);

	if (local->hw.conf.flags & IEEE80211_CONF_PS) {
		local->hw.conf.flags &= ~IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}

	ieee80211_wake_queues_by_reason(&local->hw,
					IEEE80211_MAX_QUEUE_MAP,
					IEEE80211_QUEUE_STOP_REASON_PS,
					false);
}

void ieee80211_dynamic_ps_enable_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local,
			     dynamic_ps_enable_work);
	struct ieee80211_sub_if_data *sdata = local->ps_sdata;
	struct ieee80211_if_managed *ifmgd;
	unsigned long flags;
	int q;

	/* can only happen when PS was just disabled anyway */
	if (!sdata)
		return;

	ifmgd = &sdata->u.mgd;

	if (local->hw.conf.flags & IEEE80211_CONF_PS)
		return;

	if (local->hw.conf.dynamic_ps_timeout > 0) {
		/* don't enter PS if TX frames are pending */
		if (drv_tx_frames_pending(local)) {
			mod_timer(&local->dynamic_ps_timer, jiffies +
				  msecs_to_jiffies(
				  local->hw.conf.dynamic_ps_timeout));
			return;
		}

		/*
		 * transmission can be stopped by others which leads to
		 * dynamic_ps_timer expiry. Postpone the ps timer if it
		 * is not the actual idle state.
		 */
		spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
		for (q = 0; q < local->hw.queues; q++) {
			if (local->queue_stop_reasons[q]) {
				spin_unlock_irqrestore(&local->queue_stop_reason_lock,
						       flags);
				mod_timer(&local->dynamic_ps_timer, jiffies +
					  msecs_to_jiffies(
					  local->hw.conf.dynamic_ps_timeout));
				return;
			}
		}
		spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
	}

	if (ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK) &&
	    !(ifmgd->flags & IEEE80211_STA_NULLFUNC_ACKED)) {
		if (drv_tx_frames_pending(local)) {
			mod_timer(&local->dynamic_ps_timer, jiffies +
				  msecs_to_jiffies(
				  local->hw.conf.dynamic_ps_timeout));
		} else {
			ieee80211_send_nullfunc(local, sdata, true);
			/* Flush to get the tx status of nullfunc frame */
			ieee80211_flush_queues(local, sdata, false);
		}
	}

	if (!(ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS) &&
	      ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK)) ||
	    (ifmgd->flags & IEEE80211_STA_NULLFUNC_ACKED)) {
		ifmgd->flags &= ~IEEE80211_STA_NULLFUNC_ACKED;
		local->hw.conf.flags |= IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}
}

void ieee80211_dynamic_ps_timer(struct timer_list *t)
{
	struct ieee80211_local *local = from_timer(local, t, dynamic_ps_timer);

	ieee80211_queue_work(&local->hw, &local->dynamic_ps_enable_work);
}

void ieee80211_dfs_cac_timer_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct ieee80211_link_data *link =
		container_of(delayed_work, struct ieee80211_link_data,
			     dfs_cac_timer_work);
	struct cfg80211_chan_def chandef = link->conf->chandef;
	struct ieee80211_sub_if_data *sdata = link->sdata;

	mutex_lock(&sdata->local->mtx);
	if (sdata->wdev.cac_started) {
		ieee80211_link_release_channel(link);
		cfg80211_cac_event(sdata->dev, &chandef,
				   NL80211_RADAR_CAC_FINISHED,
				   GFP_KERNEL);
	}
	mutex_unlock(&sdata->local->mtx);
}

static bool
__ieee80211_sta_handle_tspec_ac_params(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	bool ret = false;
	int ac;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		return false;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_sta_tx_tspec *tx_tspec = &ifmgd->tx_tspec[ac];
		int non_acm_ac;
		unsigned long now = jiffies;

		if (tx_tspec->action == TX_TSPEC_ACTION_NONE &&
		    tx_tspec->admitted_time &&
		    time_after(now, tx_tspec->time_slice_start + HZ)) {
			tx_tspec->consumed_tx_time = 0;
			tx_tspec->time_slice_start = now;

			if (tx_tspec->downgraded)
				tx_tspec->action =
					TX_TSPEC_ACTION_STOP_DOWNGRADE;
		}

		switch (tx_tspec->action) {
		case TX_TSPEC_ACTION_STOP_DOWNGRADE:
			/* take the original parameters */
			if (drv_conf_tx(local, &sdata->deflink, ac,
					&sdata->deflink.tx_conf[ac]))
				link_err(&sdata->deflink,
					 "failed to set TX queue parameters for queue %d\n",
					 ac);
			tx_tspec->action = TX_TSPEC_ACTION_NONE;
			tx_tspec->downgraded = false;
			ret = true;
			break;
		case TX_TSPEC_ACTION_DOWNGRADE:
			if (time_after(now, tx_tspec->time_slice_start + HZ)) {
				tx_tspec->action = TX_TSPEC_ACTION_NONE;
				ret = true;
				break;
			}
			/* downgrade next lower non-ACM AC */
			for (non_acm_ac = ac + 1;
			     non_acm_ac < IEEE80211_NUM_ACS;
			     non_acm_ac++)
				if (!(sdata->wmm_acm & BIT(7 - 2 * non_acm_ac)))
					break;
			/* Usually the loop will result in using BK even if it
			 * requires admission control, but such a configuration
			 * makes no sense and we have to transmit somehow - the
			 * AC selection does the same thing.
			 * If we started out trying to downgrade from BK, then
			 * the extra condition here might be needed.
			 */
			if (non_acm_ac >= IEEE80211_NUM_ACS)
				non_acm_ac = IEEE80211_AC_BK;
			if (drv_conf_tx(local, &sdata->deflink, ac,
					&sdata->deflink.tx_conf[non_acm_ac]))
				link_err(&sdata->deflink,
					 "failed to set TX queue parameters for queue %d\n",
					 ac);
			tx_tspec->action = TX_TSPEC_ACTION_NONE;
			ret = true;
			schedule_delayed_work(&ifmgd->tx_tspec_wk,
				tx_tspec->time_slice_start + HZ - now + 1);
			break;
		case TX_TSPEC_ACTION_NONE:
			/* nothing now */
			break;
		}
	}

	return ret;
}

void ieee80211_sta_handle_tspec_ac_params(struct ieee80211_sub_if_data *sdata)
{
	if (__ieee80211_sta_handle_tspec_ac_params(sdata))
		ieee80211_link_info_change_notify(sdata, &sdata->deflink,
						  BSS_CHANGED_QOS);
}

static void ieee80211_sta_handle_tspec_ac_params_wk(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = container_of(work, struct ieee80211_sub_if_data,
			     u.mgd.tx_tspec_wk.work);
	ieee80211_sta_handle_tspec_ac_params(sdata);
}

void ieee80211_mgd_set_link_qos_params(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_tx_queue_params *params = link->tx_conf;
	u8 ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		mlme_dbg(sdata,
			 "WMM AC=%d acm=%d aifs=%d cWmin=%d cWmax=%d txop=%d uapsd=%d, downgraded=%d\n",
			 ac, params[ac].acm,
			 params[ac].aifs, params[ac].cw_min, params[ac].cw_max,
			 params[ac].txop, params[ac].uapsd,
			 ifmgd->tx_tspec[ac].downgraded);
		if (!ifmgd->tx_tspec[ac].downgraded &&
		    drv_conf_tx(local, link, ac, &params[ac]))
			link_err(link,
				 "failed to set TX queue parameters for AC %d\n",
				 ac);
	}
}

/* MLME */
static bool
ieee80211_sta_wmm_params(struct ieee80211_local *local,
			 struct ieee80211_link_data *link,
			 const u8 *wmm_param, size_t wmm_param_len,
			 const struct ieee80211_mu_edca_param_set *mu_edca)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_tx_queue_params params[IEEE80211_NUM_ACS];
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	size_t left;
	int count, mu_edca_count, ac;
	const u8 *pos;
	u8 uapsd_queues = 0;

	if (!local->ops->conf_tx)
		return false;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		return false;

	if (!wmm_param)
		return false;

	if (wmm_param_len < 8 || wmm_param[5] /* version */ != 1)
		return false;

	if (ifmgd->flags & IEEE80211_STA_UAPSD_ENABLED)
		uapsd_queues = ifmgd->uapsd_queues;

	count = wmm_param[6] & 0x0f;
	/* -1 is the initial value of ifmgd->mu_edca_last_param_set.
	 * if mu_edca was preset before and now it disappeared tell
	 * the driver about it.
	 */
	mu_edca_count = mu_edca ? mu_edca->mu_qos_info & 0x0f : -1;
	if (count == link->u.mgd.wmm_last_param_set &&
	    mu_edca_count == link->u.mgd.mu_edca_last_param_set)
		return false;
	link->u.mgd.wmm_last_param_set = count;
	link->u.mgd.mu_edca_last_param_set = mu_edca_count;

	pos = wmm_param + 8;
	left = wmm_param_len - 8;

	memset(&params, 0, sizeof(params));

	sdata->wmm_acm = 0;
	for (; left >= 4; left -= 4, pos += 4) {
		int aci = (pos[0] >> 5) & 0x03;
		int acm = (pos[0] >> 4) & 0x01;
		bool uapsd = false;

		switch (aci) {
		case 1: /* AC_BK */
			ac = IEEE80211_AC_BK;
			if (acm)
				sdata->wmm_acm |= BIT(1) | BIT(2); /* BK/- */
			if (uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BK)
				uapsd = true;
			params[ac].mu_edca = !!mu_edca;
			if (mu_edca)
				params[ac].mu_edca_param_rec = mu_edca->ac_bk;
			break;
		case 2: /* AC_VI */
			ac = IEEE80211_AC_VI;
			if (acm)
				sdata->wmm_acm |= BIT(4) | BIT(5); /* CL/VI */
			if (uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VI)
				uapsd = true;
			params[ac].mu_edca = !!mu_edca;
			if (mu_edca)
				params[ac].mu_edca_param_rec = mu_edca->ac_vi;
			break;
		case 3: /* AC_VO */
			ac = IEEE80211_AC_VO;
			if (acm)
				sdata->wmm_acm |= BIT(6) | BIT(7); /* VO/NC */
			if (uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VO)
				uapsd = true;
			params[ac].mu_edca = !!mu_edca;
			if (mu_edca)
				params[ac].mu_edca_param_rec = mu_edca->ac_vo;
			break;
		case 0: /* AC_BE */
		default:
			ac = IEEE80211_AC_BE;
			if (acm)
				sdata->wmm_acm |= BIT(0) | BIT(3); /* BE/EE */
			if (uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BE)
				uapsd = true;
			params[ac].mu_edca = !!mu_edca;
			if (mu_edca)
				params[ac].mu_edca_param_rec = mu_edca->ac_be;
			break;
		}

		params[ac].aifs = pos[0] & 0x0f;

		if (params[ac].aifs < 2) {
			sdata_info(sdata,
				   "AP has invalid WMM params (AIFSN=%d for ACI %d), will use 2\n",
				   params[ac].aifs, aci);
			params[ac].aifs = 2;
		}
		params[ac].cw_max = ecw2cw((pos[1] & 0xf0) >> 4);
		params[ac].cw_min = ecw2cw(pos[1] & 0x0f);
		params[ac].txop = get_unaligned_le16(pos + 2);
		params[ac].acm = acm;
		params[ac].uapsd = uapsd;

		if (params[ac].cw_min == 0 ||
		    params[ac].cw_min > params[ac].cw_max) {
			sdata_info(sdata,
				   "AP has invalid WMM params (CWmin/max=%d/%d for ACI %d), using defaults\n",
				   params[ac].cw_min, params[ac].cw_max, aci);
			return false;
		}
		ieee80211_regulatory_limit_wmm_params(sdata, &params[ac], ac);
	}

	/* WMM specification requires all 4 ACIs. */
	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		if (params[ac].cw_min == 0) {
			sdata_info(sdata,
				   "AP has invalid WMM params (missing AC %d), using defaults\n",
				   ac);
			return false;
		}
	}

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		link->tx_conf[ac] = params[ac];

	ieee80211_mgd_set_link_qos_params(link);

	/* enable WMM or activate new settings */
	link->conf->qos = true;
	return true;
}

static void __ieee80211_stop_poll(struct ieee80211_sub_if_data *sdata)
{
	lockdep_assert_held(&sdata->local->mtx);

	sdata->u.mgd.flags &= ~IEEE80211_STA_CONNECTION_POLL;
	ieee80211_run_deferred_scan(sdata->local);
}

static void ieee80211_stop_poll(struct ieee80211_sub_if_data *sdata)
{
	mutex_lock(&sdata->local->mtx);
	__ieee80211_stop_poll(sdata);
	mutex_unlock(&sdata->local->mtx);
}

static u32 ieee80211_handle_bss_capability(struct ieee80211_link_data *link,
					   u16 capab, bool erp_valid, u8 erp)
{
	struct ieee80211_bss_conf *bss_conf = link->conf;
	struct ieee80211_supported_band *sband;
	u32 changed = 0;
	bool use_protection;
	bool use_short_preamble;
	bool use_short_slot;

	sband = ieee80211_get_link_sband(link);
	if (!sband)
		return changed;

	if (erp_valid) {
		use_protection = (erp & WLAN_ERP_USE_PROTECTION) != 0;
		use_short_preamble = (erp & WLAN_ERP_BARKER_PREAMBLE) == 0;
	} else {
		use_protection = false;
		use_short_preamble = !!(capab & WLAN_CAPABILITY_SHORT_PREAMBLE);
	}

	use_short_slot = !!(capab & WLAN_CAPABILITY_SHORT_SLOT_TIME);
	if (sband->band == NL80211_BAND_5GHZ ||
	    sband->band == NL80211_BAND_6GHZ)
		use_short_slot = true;

	if (use_protection != bss_conf->use_cts_prot) {
		bss_conf->use_cts_prot = use_protection;
		changed |= BSS_CHANGED_ERP_CTS_PROT;
	}

	if (use_short_preamble != bss_conf->use_short_preamble) {
		bss_conf->use_short_preamble = use_short_preamble;
		changed |= BSS_CHANGED_ERP_PREAMBLE;
	}

	if (use_short_slot != bss_conf->use_short_slot) {
		bss_conf->use_short_slot = use_short_slot;
		changed |= BSS_CHANGED_ERP_SLOT;
	}

	return changed;
}

static u32 ieee80211_link_set_associated(struct ieee80211_link_data *link,
					 struct cfg80211_bss *cbss)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_bss_conf *bss_conf = link->conf;
	struct ieee80211_bss *bss = (void *)cbss->priv;
	u32 changed = BSS_CHANGED_QOS;

	/* not really used in MLO */
	sdata->u.mgd.beacon_timeout =
		usecs_to_jiffies(ieee80211_tu_to_usec(beacon_loss_count *
						      bss_conf->beacon_int));

	changed |= ieee80211_handle_bss_capability(link,
						   bss_conf->assoc_capability,
						   bss->has_erp_value,
						   bss->erp_value);

	ieee80211_check_rate_mask(link);

	link->u.mgd.bss = cbss;
	memcpy(link->u.mgd.bssid, cbss->bssid, ETH_ALEN);

	if (sdata->vif.p2p ||
	    sdata->vif.driver_flags & IEEE80211_VIF_GET_NOA_UPDATE) {
		const struct cfg80211_bss_ies *ies;

		rcu_read_lock();
		ies = rcu_dereference(cbss->ies);
		if (ies) {
			int ret;

			ret = cfg80211_get_p2p_attr(
					ies->data, ies->len,
					IEEE80211_P2P_ATTR_ABSENCE_NOTICE,
					(u8 *) &bss_conf->p2p_noa_attr,
					sizeof(bss_conf->p2p_noa_attr));
			if (ret >= 2) {
				link->u.mgd.p2p_noa_index =
					bss_conf->p2p_noa_attr.index;
				changed |= BSS_CHANGED_P2P_PS;
			}
		}
		rcu_read_unlock();
	}

	if (link->u.mgd.have_beacon) {
		/*
		 * If the AP is buggy we may get here with no DTIM period
		 * known, so assume it's 1 which is the only safe assumption
		 * in that case, although if the TIM IE is broken powersave
		 * probably just won't work at all.
		 */
		bss_conf->dtim_period = link->u.mgd.dtim_period ?: 1;
		bss_conf->beacon_rate = bss->beacon_rate;
		changed |= BSS_CHANGED_BEACON_INFO;
	} else {
		bss_conf->beacon_rate = NULL;
		bss_conf->dtim_period = 0;
	}

	/* Tell the driver to monitor connection quality (if supported) */
	if (sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_CQM_RSSI &&
	    bss_conf->cqm_rssi_thold)
		changed |= BSS_CHANGED_CQM;

	return changed;
}

static void ieee80211_set_associated(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgd_assoc_data *assoc_data,
				     u64 changed[IEEE80211_MLD_MAX_NUM_LINKS])
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_vif_cfg *vif_cfg = &sdata->vif.cfg;
	u64 vif_changed = BSS_CHANGED_ASSOC;
	unsigned int link_id;

	sdata->u.mgd.associated = true;

	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++) {
		struct cfg80211_bss *cbss = assoc_data->link[link_id].bss;
		struct ieee80211_link_data *link;

		if (!cbss)
			continue;

		link = sdata_dereference(sdata->link[link_id], sdata);
		if (WARN_ON(!link))
			return;

		changed[link_id] |= ieee80211_link_set_associated(link, cbss);
	}

	/* just to be sure */
	ieee80211_stop_poll(sdata);

	ieee80211_led_assoc(local, 1);

	vif_cfg->assoc = 1;

	/* Enable ARP filtering */
	if (vif_cfg->arp_addr_cnt)
		vif_changed |= BSS_CHANGED_ARP_FILTER;

	if (sdata->vif.valid_links) {
		for (link_id = 0;
		     link_id < IEEE80211_MLD_MAX_NUM_LINKS;
		     link_id++) {
			struct ieee80211_link_data *link;
			struct cfg80211_bss *cbss = assoc_data->link[link_id].bss;

			if (!cbss)
				continue;

			link = sdata_dereference(sdata->link[link_id], sdata);
			if (WARN_ON(!link))
				return;

			ieee80211_link_info_change_notify(sdata, link,
							  changed[link_id]);

			ieee80211_recalc_smps(sdata, link);
		}

		ieee80211_vif_cfg_change_notify(sdata, vif_changed);
	} else {
		ieee80211_bss_info_change_notify(sdata,
						 vif_changed | changed[0]);
	}

	mutex_lock(&local->iflist_mtx);
	ieee80211_recalc_ps(local);
	mutex_unlock(&local->iflist_mtx);

	/* leave this here to not change ordering in non-MLO cases */
	if (!sdata->vif.valid_links)
		ieee80211_recalc_smps(sdata, &sdata->deflink);
	ieee80211_recalc_ps_vif(sdata);

	netif_carrier_on(sdata->dev);
}

static void ieee80211_set_disassoc(struct ieee80211_sub_if_data *sdata,
				   u16 stype, u16 reason, bool tx,
				   u8 *frame_buf)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	unsigned int link_id;
	u32 changed = 0;
	struct ieee80211_prep_tx_info info = {
		.subtype = stype,
	};

	sdata_assert_lock(sdata);

	if (WARN_ON_ONCE(tx && !frame_buf))
		return;

	if (WARN_ON(!ifmgd->associated))
		return;

	ieee80211_stop_poll(sdata);

	ifmgd->associated = false;

	/* other links will be destroyed */
	sdata->deflink.u.mgd.bss = NULL;

	netif_carrier_off(sdata->dev);

	/*
	 * if we want to get out of ps before disassoc (why?) we have
	 * to do it before sending disassoc, as otherwise the null-packet
	 * won't be valid.
	 */
	if (local->hw.conf.flags & IEEE80211_CONF_PS) {
		local->hw.conf.flags &= ~IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}
	local->ps_sdata = NULL;

	/* disable per-vif ps */
	ieee80211_recalc_ps_vif(sdata);

	/* make sure ongoing transmission finishes */
	synchronize_net();

	/*
	 * drop any frame before deauth/disassoc, this can be data or
	 * management frame. Since we are disconnecting, we should not
	 * insist sending these frames which can take time and delay
	 * the disconnection and possible the roaming.
	 */
	if (tx)
		ieee80211_flush_queues(local, sdata, true);

	/* deauthenticate/disassociate now */
	if (tx || frame_buf) {
		/*
		 * In multi channel scenarios guarantee that the virtual
		 * interface is granted immediate airtime to transmit the
		 * deauthentication frame by calling mgd_prepare_tx, if the
		 * driver requested so.
		 */
		if (ieee80211_hw_check(&local->hw, DEAUTH_NEED_MGD_TX_PREP) &&
		    !sdata->deflink.u.mgd.have_beacon) {
			drv_mgd_prepare_tx(sdata->local, sdata, &info);
		}

		ieee80211_send_deauth_disassoc(sdata, sdata->vif.cfg.ap_addr,
					       sdata->vif.cfg.ap_addr, stype,
					       reason, tx, frame_buf);
	}

	/* flush out frame - make sure the deauth was actually sent */
	if (tx)
		ieee80211_flush_queues(local, sdata, false);

	drv_mgd_complete_tx(sdata->local, sdata, &info);

	/* clear AP addr only after building the needed mgmt frames */
	eth_zero_addr(sdata->deflink.u.mgd.bssid);
	eth_zero_addr(sdata->vif.cfg.ap_addr);

	sdata->vif.cfg.ssid_len = 0;

	/* remove AP and TDLS peers */
	sta_info_flush(sdata);

	/* finally reset all BSS / config parameters */
	if (!sdata->vif.valid_links)
		changed |= ieee80211_reset_erp_info(sdata);

	ieee80211_led_assoc(local, 0);
	changed |= BSS_CHANGED_ASSOC;
	sdata->vif.cfg.assoc = false;

	sdata->deflink.u.mgd.p2p_noa_index = -1;
	memset(&sdata->vif.bss_conf.p2p_noa_attr, 0,
	       sizeof(sdata->vif.bss_conf.p2p_noa_attr));

	/* on the next assoc, re-program HT/VHT parameters */
	memset(&ifmgd->ht_capa, 0, sizeof(ifmgd->ht_capa));
	memset(&ifmgd->ht_capa_mask, 0, sizeof(ifmgd->ht_capa_mask));
	memset(&ifmgd->vht_capa, 0, sizeof(ifmgd->vht_capa));
	memset(&ifmgd->vht_capa_mask, 0, sizeof(ifmgd->vht_capa_mask));

	/*
	 * reset MU-MIMO ownership and group data in default link,
	 * if used, other links are destroyed
	 */
	memset(sdata->vif.bss_conf.mu_group.membership, 0,
	       sizeof(sdata->vif.bss_conf.mu_group.membership));
	memset(sdata->vif.bss_conf.mu_group.position, 0,
	       sizeof(sdata->vif.bss_conf.mu_group.position));
	if (!sdata->vif.valid_links)
		changed |= BSS_CHANGED_MU_GROUPS;
	sdata->vif.bss_conf.mu_mimo_owner = false;

	sdata->deflink.ap_power_level = IEEE80211_UNSET_POWER_LEVEL;

	del_timer_sync(&local->dynamic_ps_timer);
	cancel_work_sync(&local->dynamic_ps_enable_work);

	/* Disable ARP filtering */
	if (sdata->vif.cfg.arp_addr_cnt)
		changed |= BSS_CHANGED_ARP_FILTER;

	sdata->vif.bss_conf.qos = false;
	if (!sdata->vif.valid_links) {
		changed |= BSS_CHANGED_QOS;
		/* The BSSID (not really interesting) and HT changed */
		changed |= BSS_CHANGED_BSSID | BSS_CHANGED_HT;
		ieee80211_bss_info_change_notify(sdata, changed);
	} else {
		ieee80211_vif_cfg_change_notify(sdata, changed);
	}

	/* disassociated - set to defaults now */
	ieee80211_set_wmm_default(&sdata->deflink, false, false);

	del_timer_sync(&sdata->u.mgd.conn_mon_timer);
	del_timer_sync(&sdata->u.mgd.bcn_mon_timer);
	del_timer_sync(&sdata->u.mgd.timer);
	del_timer_sync(&sdata->deflink.u.mgd.chswitch_timer);

	sdata->vif.bss_conf.dtim_period = 0;
	sdata->vif.bss_conf.beacon_rate = NULL;

	sdata->deflink.u.mgd.have_beacon = false;
	sdata->deflink.u.mgd.tracking_signal_avg = false;
	sdata->deflink.u.mgd.disable_wmm_tracking = false;

	ifmgd->flags = 0;
	sdata->deflink.u.mgd.conn_flags = 0;
	mutex_lock(&local->mtx);

	for (link_id = 0; link_id < ARRAY_SIZE(sdata->link); link_id++) {
		struct ieee80211_link_data *link;

		link = sdata_dereference(sdata->link[link_id], sdata);
		if (!link)
			continue;
		ieee80211_link_release_channel(link);
	}

	sdata->vif.bss_conf.csa_active = false;
	sdata->deflink.u.mgd.csa_waiting_bcn = false;
	sdata->deflink.u.mgd.csa_ignored_same_chan = false;
	if (sdata->deflink.csa_block_tx) {
		ieee80211_wake_vif_queues(local, sdata,
					  IEEE80211_QUEUE_STOP_REASON_CSA);
		sdata->deflink.csa_block_tx = false;
	}
	mutex_unlock(&local->mtx);

	/* existing TX TSPEC sessions no longer exist */
	memset(ifmgd->tx_tspec, 0, sizeof(ifmgd->tx_tspec));
	cancel_delayed_work_sync(&ifmgd->tx_tspec_wk);

	sdata->vif.bss_conf.pwr_reduction = 0;
	sdata->vif.bss_conf.tx_pwr_env_num = 0;
	memset(sdata->vif.bss_conf.tx_pwr_env, 0,
	       sizeof(sdata->vif.bss_conf.tx_pwr_env));

	ieee80211_vif_set_links(sdata, 0);
}

static void ieee80211_reset_ap_probe(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;

	mutex_lock(&local->mtx);
	if (!(ifmgd->flags & IEEE80211_STA_CONNECTION_POLL))
		goto out;

	__ieee80211_stop_poll(sdata);

	mutex_lock(&local->iflist_mtx);
	ieee80211_recalc_ps(local);
	mutex_unlock(&local->iflist_mtx);

	if (ieee80211_hw_check(&sdata->local->hw, CONNECTION_MONITOR))
		goto out;

	/*
	 * We've received a probe response, but are not sure whether
	 * we have or will be receiving any beacons or data, so let's
	 * schedule the timers again, just in case.
	 */
	ieee80211_sta_reset_beacon_monitor(sdata);

	mod_timer(&ifmgd->conn_mon_timer,
		  round_jiffies_up(jiffies +
				   IEEE80211_CONNECTION_IDLE_TIME));
out:
	mutex_unlock(&local->mtx);
}

static void ieee80211_sta_tx_wmm_ac_notify(struct ieee80211_sub_if_data *sdata,
					   struct ieee80211_hdr *hdr,
					   u16 tx_time)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u16 tid;
	int ac;
	struct ieee80211_sta_tx_tspec *tx_tspec;
	unsigned long now = jiffies;

	if (!ieee80211_is_data_qos(hdr->frame_control))
		return;

	tid = ieee80211_get_tid(hdr);
	ac = ieee80211_ac_from_tid(tid);
	tx_tspec = &ifmgd->tx_tspec[ac];

	if (likely(!tx_tspec->admitted_time))
		return;

	if (time_after(now, tx_tspec->time_slice_start + HZ)) {
		tx_tspec->consumed_tx_time = 0;
		tx_tspec->time_slice_start = now;

		if (tx_tspec->downgraded) {
			tx_tspec->action = TX_TSPEC_ACTION_STOP_DOWNGRADE;
			schedule_delayed_work(&ifmgd->tx_tspec_wk, 0);
		}
	}

	if (tx_tspec->downgraded)
		return;

	tx_tspec->consumed_tx_time += tx_time;

	if (tx_tspec->consumed_tx_time >= tx_tspec->admitted_time) {
		tx_tspec->downgraded = true;
		tx_tspec->action = TX_TSPEC_ACTION_DOWNGRADE;
		schedule_delayed_work(&ifmgd->tx_tspec_wk, 0);
	}
}

void ieee80211_sta_tx_notify(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_hdr *hdr, bool ack, u16 tx_time)
{
	ieee80211_sta_tx_wmm_ac_notify(sdata, hdr, tx_time);

	if (!ieee80211_is_any_nullfunc(hdr->frame_control) ||
	    !sdata->u.mgd.probe_send_count)
		return;

	if (ack)
		sdata->u.mgd.probe_send_count = 0;
	else
		sdata->u.mgd.nullfunc_failed = true;
	ieee80211_queue_work(&sdata->local->hw, &sdata->work);
}

static void ieee80211_mlme_send_probe_req(struct ieee80211_sub_if_data *sdata,
					  const u8 *src, const u8 *dst,
					  const u8 *ssid, size_t ssid_len,
					  struct ieee80211_channel *channel)
{
	struct sk_buff *skb;

	skb = ieee80211_build_probe_req(sdata, src, dst, (u32)-1, channel,
					ssid, ssid_len, NULL, 0,
					IEEE80211_PROBE_FLAG_DIRECTED);
	if (skb)
		ieee80211_tx_skb(sdata, skb);
}

static void ieee80211_mgd_probe_ap_send(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 *dst = sdata->vif.cfg.ap_addr;
	u8 unicast_limit = max(1, max_probe_tries - 3);
	struct sta_info *sta;

	if (WARN_ON(sdata->vif.valid_links))
		return;

	/*
	 * Try sending broadcast probe requests for the last three
	 * probe requests after the first ones failed since some
	 * buggy APs only support broadcast probe requests.
	 */
	if (ifmgd->probe_send_count >= unicast_limit)
		dst = NULL;

	/*
	 * When the hardware reports an accurate Tx ACK status, it's
	 * better to send a nullfunc frame instead of a probe request,
	 * as it will kick us off the AP quickly if we aren't associated
	 * anymore. The timeout will be reset if the frame is ACKed by
	 * the AP.
	 */
	ifmgd->probe_send_count++;

	if (dst) {
		mutex_lock(&sdata->local->sta_mtx);
		sta = sta_info_get(sdata, dst);
		if (!WARN_ON(!sta))
			ieee80211_check_fast_rx(sta);
		mutex_unlock(&sdata->local->sta_mtx);
	}

	if (ieee80211_hw_check(&sdata->local->hw, REPORTS_TX_ACK_STATUS)) {
		ifmgd->nullfunc_failed = false;
		ieee80211_send_nullfunc(sdata->local, sdata, false);
	} else {
		ieee80211_mlme_send_probe_req(sdata, sdata->vif.addr, dst,
					      sdata->vif.cfg.ssid,
					      sdata->vif.cfg.ssid_len,
					      sdata->deflink.u.mgd.bss->channel);
	}

	ifmgd->probe_timeout = jiffies + msecs_to_jiffies(probe_wait_ms);
	run_again(sdata, ifmgd->probe_timeout);
}

static void ieee80211_mgd_probe_ap(struct ieee80211_sub_if_data *sdata,
				   bool beacon)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	bool already = false;

	if (WARN_ON(sdata->vif.valid_links))
		return;

	if (!ieee80211_sdata_running(sdata))
		return;

	sdata_lock(sdata);

	if (!ifmgd->associated)
		goto out;

	mutex_lock(&sdata->local->mtx);

	if (sdata->local->tmp_channel || sdata->local->scanning) {
		mutex_unlock(&sdata->local->mtx);
		goto out;
	}

	if (sdata->local->suspending) {
		/* reschedule after resume */
		mutex_unlock(&sdata->local->mtx);
		ieee80211_reset_ap_probe(sdata);
		goto out;
	}

	if (beacon) {
		mlme_dbg_ratelimited(sdata,
				     "detected beacon loss from AP (missed %d beacons) - probing\n",
				     beacon_loss_count);

		ieee80211_cqm_beacon_loss_notify(&sdata->vif, GFP_KERNEL);
	}

	/*
	 * The driver/our work has already reported this event or the
	 * connection monitoring has kicked in and we have already sent
	 * a probe request. Or maybe the AP died and the driver keeps
	 * reporting until we disassociate...
	 *
	 * In either case we have to ignore the current call to this
	 * function (except for setting the correct probe reason bit)
	 * because otherwise we would reset the timer every time and
	 * never check whether we received a probe response!
	 */
	if (ifmgd->flags & IEEE80211_STA_CONNECTION_POLL)
		already = true;

	ifmgd->flags |= IEEE80211_STA_CONNECTION_POLL;

	mutex_unlock(&sdata->local->mtx);

	if (already)
		goto out;

	mutex_lock(&sdata->local->iflist_mtx);
	ieee80211_recalc_ps(sdata->local);
	mutex_unlock(&sdata->local->iflist_mtx);

	ifmgd->probe_send_count = 0;
	ieee80211_mgd_probe_ap_send(sdata);
 out:
	sdata_unlock(sdata);
}

struct sk_buff *ieee80211_ap_probereq_get(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct cfg80211_bss *cbss;
	struct sk_buff *skb;
	const struct element *ssid;
	int ssid_len;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION ||
		    sdata->vif.valid_links))
		return NULL;

	sdata_assert_lock(sdata);

	if (ifmgd->associated)
		cbss = sdata->deflink.u.mgd.bss;
	else if (ifmgd->auth_data)
		cbss = ifmgd->auth_data->bss;
	else if (ifmgd->assoc_data && ifmgd->assoc_data->link[0].bss)
		cbss = ifmgd->assoc_data->link[0].bss;
	else
		return NULL;

	rcu_read_lock();
	ssid = ieee80211_bss_get_elem(cbss, WLAN_EID_SSID);
	if (WARN_ONCE(!ssid || ssid->datalen > IEEE80211_MAX_SSID_LEN,
		      "invalid SSID element (len=%d)",
		      ssid ? ssid->datalen : -1))
		ssid_len = 0;
	else
		ssid_len = ssid->datalen;

	skb = ieee80211_build_probe_req(sdata, sdata->vif.addr, cbss->bssid,
					(u32) -1, cbss->channel,
					ssid->data, ssid_len,
					NULL, 0, IEEE80211_PROBE_FLAG_DIRECTED);
	rcu_read_unlock();

	return skb;
}
EXPORT_SYMBOL(ieee80211_ap_probereq_get);

static void ieee80211_report_disconnect(struct ieee80211_sub_if_data *sdata,
					const u8 *buf, size_t len, bool tx,
					u16 reason, bool reconnect)
{
	struct ieee80211_event event = {
		.type = MLME_EVENT,
		.u.mlme.data = tx ? DEAUTH_TX_EVENT : DEAUTH_RX_EVENT,
		.u.mlme.reason = reason,
	};

	if (tx)
		cfg80211_tx_mlme_mgmt(sdata->dev, buf, len, reconnect);
	else
		cfg80211_rx_mlme_mgmt(sdata->dev, buf, len);

	drv_event_callback(sdata->local, sdata, &event);
}

static void __ieee80211_disconnect(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];
	bool tx;

	sdata_lock(sdata);
	if (!ifmgd->associated) {
		sdata_unlock(sdata);
		return;
	}

	/* in MLO assume we have a link where we can TX the frame */
	tx = sdata->vif.valid_links || !sdata->deflink.csa_block_tx;

	if (!ifmgd->driver_disconnect) {
		unsigned int link_id;

		/*
		 * AP is probably out of range (or not reachable for another
		 * reason) so remove the bss structs for that AP. In the case
		 * of multi-link, it's not clear that all of them really are
		 * out of range, but if they weren't the driver likely would
		 * have switched to just have a single link active?
		 */
		for (link_id = 0;
		     link_id < ARRAY_SIZE(sdata->link);
		     link_id++) {
			struct ieee80211_link_data *link;

			link = sdata_dereference(sdata->link[link_id], sdata);
			if (!link)
				continue;
			cfg80211_unlink_bss(local->hw.wiphy, link->u.mgd.bss);
			link->u.mgd.bss = NULL;
		}
	}

	ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DEAUTH,
			       ifmgd->driver_disconnect ?
					WLAN_REASON_DEAUTH_LEAVING :
					WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY,
			       tx, frame_buf);
	mutex_lock(&local->mtx);
	/* the other links will be destroyed */
	sdata->vif.bss_conf.csa_active = false;
	sdata->deflink.u.mgd.csa_waiting_bcn = false;
	if (sdata->deflink.csa_block_tx) {
		ieee80211_wake_vif_queues(local, sdata,
					  IEEE80211_QUEUE_STOP_REASON_CSA);
		sdata->deflink.csa_block_tx = false;
	}
	mutex_unlock(&local->mtx);

	ieee80211_report_disconnect(sdata, frame_buf, sizeof(frame_buf), tx,
				    WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY,
				    ifmgd->reconnect);
	ifmgd->reconnect = false;

	sdata_unlock(sdata);
}

static void ieee80211_beacon_connection_loss_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data,
			     u.mgd.beacon_connection_loss_work);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (ifmgd->connection_loss) {
		sdata_info(sdata, "Connection to AP %pM lost\n",
			   sdata->vif.cfg.ap_addr);
		__ieee80211_disconnect(sdata);
		ifmgd->connection_loss = false;
	} else if (ifmgd->driver_disconnect) {
		sdata_info(sdata,
			   "Driver requested disconnection from AP %pM\n",
			   sdata->vif.cfg.ap_addr);
		__ieee80211_disconnect(sdata);
		ifmgd->driver_disconnect = false;
	} else {
		if (ifmgd->associated)
			sdata->deflink.u.mgd.beacon_loss_count++;
		ieee80211_mgd_probe_ap(sdata, true);
	}
}

static void ieee80211_csa_connection_drop_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data,
			     u.mgd.csa_connection_drop_work);

	__ieee80211_disconnect(sdata);
}

void ieee80211_beacon_loss(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_hw *hw = &sdata->local->hw;

	trace_api_beacon_loss(sdata);

	sdata->u.mgd.connection_loss = false;
	ieee80211_queue_work(hw, &sdata->u.mgd.beacon_connection_loss_work);
}
EXPORT_SYMBOL(ieee80211_beacon_loss);

void ieee80211_connection_loss(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_hw *hw = &sdata->local->hw;

	trace_api_connection_loss(sdata);

	sdata->u.mgd.connection_loss = true;
	ieee80211_queue_work(hw, &sdata->u.mgd.beacon_connection_loss_work);
}
EXPORT_SYMBOL(ieee80211_connection_loss);

void ieee80211_disconnect(struct ieee80211_vif *vif, bool reconnect)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_hw *hw = &sdata->local->hw;

	trace_api_disconnect(sdata, reconnect);

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return;

	sdata->u.mgd.driver_disconnect = true;
	sdata->u.mgd.reconnect = reconnect;
	ieee80211_queue_work(hw, &sdata->u.mgd.beacon_connection_loss_work);
}
EXPORT_SYMBOL(ieee80211_disconnect);

static void ieee80211_destroy_auth_data(struct ieee80211_sub_if_data *sdata,
					bool assoc)
{
	struct ieee80211_mgd_auth_data *auth_data = sdata->u.mgd.auth_data;

	sdata_assert_lock(sdata);

	if (!assoc) {
		/*
		 * we are not authenticated yet, the only timer that could be
		 * running is the timeout for the authentication response which
		 * which is not relevant anymore.
		 */
		del_timer_sync(&sdata->u.mgd.timer);
		sta_info_destroy_addr(sdata, auth_data->ap_addr);

		/* other links are destroyed */
		sdata->deflink.u.mgd.conn_flags = 0;
		eth_zero_addr(sdata->deflink.u.mgd.bssid);
		ieee80211_link_info_change_notify(sdata, &sdata->deflink,
						  BSS_CHANGED_BSSID);
		sdata->u.mgd.flags = 0;

		mutex_lock(&sdata->local->mtx);
		ieee80211_link_release_channel(&sdata->deflink);
		ieee80211_vif_set_links(sdata, 0);
		mutex_unlock(&sdata->local->mtx);
	}

	cfg80211_put_bss(sdata->local->hw.wiphy, auth_data->bss);
	kfree(auth_data);
	sdata->u.mgd.auth_data = NULL;
}

enum assoc_status {
	ASSOC_SUCCESS,
	ASSOC_REJECTED,
	ASSOC_TIMEOUT,
	ASSOC_ABANDON,
};

static void ieee80211_destroy_assoc_data(struct ieee80211_sub_if_data *sdata,
					 enum assoc_status status)
{
	struct ieee80211_mgd_assoc_data *assoc_data = sdata->u.mgd.assoc_data;

	sdata_assert_lock(sdata);

	if (status != ASSOC_SUCCESS) {
		/*
		 * we are not associated yet, the only timer that could be
		 * running is the timeout for the association response which
		 * which is not relevant anymore.
		 */
		del_timer_sync(&sdata->u.mgd.timer);
		sta_info_destroy_addr(sdata, assoc_data->ap_addr);

		sdata->deflink.u.mgd.conn_flags = 0;
		eth_zero_addr(sdata->deflink.u.mgd.bssid);
		ieee80211_link_info_change_notify(sdata, &sdata->deflink,
						  BSS_CHANGED_BSSID);
		sdata->u.mgd.flags = 0;
		sdata->vif.bss_conf.mu_mimo_owner = false;

		if (status != ASSOC_REJECTED) {
			struct cfg80211_assoc_failure data = {
				.timeout = status == ASSOC_TIMEOUT,
			};
			int i;

			BUILD_BUG_ON(ARRAY_SIZE(data.bss) !=
				     ARRAY_SIZE(assoc_data->link));

			for (i = 0; i < ARRAY_SIZE(data.bss); i++)
				data.bss[i] = assoc_data->link[i].bss;

			if (sdata->vif.valid_links)
				data.ap_mld_addr = assoc_data->ap_addr;

			cfg80211_assoc_failure(sdata->dev, &data);
		}

		mutex_lock(&sdata->local->mtx);
		ieee80211_link_release_channel(&sdata->deflink);
		ieee80211_vif_set_links(sdata, 0);
		mutex_unlock(&sdata->local->mtx);
	}

	kfree(assoc_data);
	sdata->u.mgd.assoc_data = NULL;
}

static void ieee80211_auth_challenge(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_mgd_auth_data *auth_data = sdata->u.mgd.auth_data;
	const struct element *challenge;
	u8 *pos;
	u32 tx_flags = 0;
	struct ieee80211_prep_tx_info info = {
		.subtype = IEEE80211_STYPE_AUTH,
	};

	pos = mgmt->u.auth.variable;
	challenge = cfg80211_find_elem(WLAN_EID_CHALLENGE, pos,
				       len - (pos - (u8 *)mgmt));
	if (!challenge)
		return;
	auth_data->expected_transaction = 4;
	drv_mgd_prepare_tx(sdata->local, sdata, &info);
	if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
		tx_flags = IEEE80211_TX_CTL_REQ_TX_STATUS |
			   IEEE80211_TX_INTFL_MLME_CONN_TX;
	ieee80211_send_auth(sdata, 3, auth_data->algorithm, 0,
			    (void *)challenge,
			    challenge->datalen + sizeof(*challenge),
			    auth_data->ap_addr, auth_data->ap_addr,
			    auth_data->key, auth_data->key_len,
			    auth_data->key_idx, tx_flags);
}

static bool ieee80211_mark_sta_auth(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	const u8 *ap_addr = ifmgd->auth_data->ap_addr;
	struct sta_info *sta;
	bool result = true;

	sdata_info(sdata, "authenticated\n");
	ifmgd->auth_data->done = true;
	ifmgd->auth_data->timeout = jiffies + IEEE80211_AUTH_WAIT_ASSOC;
	ifmgd->auth_data->timeout_started = true;
	run_again(sdata, ifmgd->auth_data->timeout);

	/* move station state to auth */
	mutex_lock(&sdata->local->sta_mtx);
	sta = sta_info_get(sdata, ap_addr);
	if (!sta) {
		WARN_ONCE(1, "%s: STA %pM not found", sdata->name, ap_addr);
		result = false;
		goto out;
	}
	if (sta_info_move_state(sta, IEEE80211_STA_AUTH)) {
		sdata_info(sdata, "failed moving %pM to auth\n", ap_addr);
		result = false;
		goto out;
	}

out:
	mutex_unlock(&sdata->local->sta_mtx);
	return result;
}

static void ieee80211_rx_mgmt_auth(struct ieee80211_sub_if_data *sdata,
				   struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u16 auth_alg, auth_transaction, status_code;
	struct ieee80211_event event = {
		.type = MLME_EVENT,
		.u.mlme.data = AUTH_EVENT,
	};
	struct ieee80211_prep_tx_info info = {
		.subtype = IEEE80211_STYPE_AUTH,
	};

	sdata_assert_lock(sdata);

	if (len < 24 + 6)
		return;

	if (!ifmgd->auth_data || ifmgd->auth_data->done)
		return;

	if (!ether_addr_equal(ifmgd->auth_data->ap_addr, mgmt->bssid))
		return;

	auth_alg = le16_to_cpu(mgmt->u.auth.auth_alg);
	auth_transaction = le16_to_cpu(mgmt->u.auth.auth_transaction);
	status_code = le16_to_cpu(mgmt->u.auth.status_code);

	if (auth_alg != ifmgd->auth_data->algorithm ||
	    (auth_alg != WLAN_AUTH_SAE &&
	     auth_transaction != ifmgd->auth_data->expected_transaction) ||
	    (auth_alg == WLAN_AUTH_SAE &&
	     (auth_transaction < ifmgd->auth_data->expected_transaction ||
	      auth_transaction > 2))) {
		sdata_info(sdata, "%pM unexpected authentication state: alg %d (expected %d) transact %d (expected %d)\n",
			   mgmt->sa, auth_alg, ifmgd->auth_data->algorithm,
			   auth_transaction,
			   ifmgd->auth_data->expected_transaction);
		goto notify_driver;
	}

	if (status_code != WLAN_STATUS_SUCCESS) {
		cfg80211_rx_mlme_mgmt(sdata->dev, (u8 *)mgmt, len);

		if (auth_alg == WLAN_AUTH_SAE &&
		    (status_code == WLAN_STATUS_ANTI_CLOG_REQUIRED ||
		     (auth_transaction == 1 &&
		      (status_code == WLAN_STATUS_SAE_HASH_TO_ELEMENT ||
		       status_code == WLAN_STATUS_SAE_PK)))) {
			/* waiting for userspace now */
			ifmgd->auth_data->waiting = true;
			ifmgd->auth_data->timeout =
				jiffies + IEEE80211_AUTH_WAIT_SAE_RETRY;
			ifmgd->auth_data->timeout_started = true;
			run_again(sdata, ifmgd->auth_data->timeout);
			goto notify_driver;
		}

		sdata_info(sdata, "%pM denied authentication (status %d)\n",
			   mgmt->sa, status_code);
		ieee80211_destroy_auth_data(sdata, false);
		event.u.mlme.status = MLME_DENIED;
		event.u.mlme.reason = status_code;
		drv_event_callback(sdata->local, sdata, &event);
		goto notify_driver;
	}

	switch (ifmgd->auth_data->algorithm) {
	case WLAN_AUTH_OPEN:
	case WLAN_AUTH_LEAP:
	case WLAN_AUTH_FT:
	case WLAN_AUTH_SAE:
	case WLAN_AUTH_FILS_SK:
	case WLAN_AUTH_FILS_SK_PFS:
	case WLAN_AUTH_FILS_PK:
		break;
	case WLAN_AUTH_SHARED_KEY:
		if (ifmgd->auth_data->expected_transaction != 4) {
			ieee80211_auth_challenge(sdata, mgmt, len);
			/* need another frame */
			return;
		}
		break;
	default:
		WARN_ONCE(1, "invalid auth alg %d",
			  ifmgd->auth_data->algorithm);
		goto notify_driver;
	}

	event.u.mlme.status = MLME_SUCCESS;
	info.success = 1;
	drv_event_callback(sdata->local, sdata, &event);
	if (ifmgd->auth_data->algorithm != WLAN_AUTH_SAE ||
	    (auth_transaction == 2 &&
	     ifmgd->auth_data->expected_transaction == 2)) {
		if (!ieee80211_mark_sta_auth(sdata))
			return; /* ignore frame -- wait for timeout */
	} else if (ifmgd->auth_data->algorithm == WLAN_AUTH_SAE &&
		   auth_transaction == 2) {
		sdata_info(sdata, "SAE peer confirmed\n");
		ifmgd->auth_data->peer_confirmed = true;
	}

	cfg80211_rx_mlme_mgmt(sdata->dev, (u8 *)mgmt, len);
notify_driver:
	drv_mgd_complete_tx(sdata->local, sdata, &info);
}

#define case_WLAN(type) \
	case WLAN_REASON_##type: return #type

const char *ieee80211_get_reason_code_string(u16 reason_code)
{
	switch (reason_code) {
	case_WLAN(UNSPECIFIED);
	case_WLAN(PREV_AUTH_NOT_VALID);
	case_WLAN(DEAUTH_LEAVING);
	case_WLAN(DISASSOC_DUE_TO_INACTIVITY);
	case_WLAN(DISASSOC_AP_BUSY);
	case_WLAN(CLASS2_FRAME_FROM_NONAUTH_STA);
	case_WLAN(CLASS3_FRAME_FROM_NONASSOC_STA);
	case_WLAN(DISASSOC_STA_HAS_LEFT);
	case_WLAN(STA_REQ_ASSOC_WITHOUT_AUTH);
	case_WLAN(DISASSOC_BAD_POWER);
	case_WLAN(DISASSOC_BAD_SUPP_CHAN);
	case_WLAN(INVALID_IE);
	case_WLAN(MIC_FAILURE);
	case_WLAN(4WAY_HANDSHAKE_TIMEOUT);
	case_WLAN(GROUP_KEY_HANDSHAKE_TIMEOUT);
	case_WLAN(IE_DIFFERENT);
	case_WLAN(INVALID_GROUP_CIPHER);
	case_WLAN(INVALID_PAIRWISE_CIPHER);
	case_WLAN(INVALID_AKMP);
	case_WLAN(UNSUPP_RSN_VERSION);
	case_WLAN(INVALID_RSN_IE_CAP);
	case_WLAN(IEEE8021X_FAILED);
	case_WLAN(CIPHER_SUITE_REJECTED);
	case_WLAN(DISASSOC_UNSPECIFIED_QOS);
	case_WLAN(DISASSOC_QAP_NO_BANDWIDTH);
	case_WLAN(DISASSOC_LOW_ACK);
	case_WLAN(DISASSOC_QAP_EXCEED_TXOP);
	case_WLAN(QSTA_LEAVE_QBSS);
	case_WLAN(QSTA_NOT_USE);
	case_WLAN(QSTA_REQUIRE_SETUP);
	case_WLAN(QSTA_TIMEOUT);
	case_WLAN(QSTA_CIPHER_NOT_SUPP);
	case_WLAN(MESH_PEER_CANCELED);
	case_WLAN(MESH_MAX_PEERS);
	case_WLAN(MESH_CONFIG);
	case_WLAN(MESH_CLOSE);
	case_WLAN(MESH_MAX_RETRIES);
	case_WLAN(MESH_CONFIRM_TIMEOUT);
	case_WLAN(MESH_INVALID_GTK);
	case_WLAN(MESH_INCONSISTENT_PARAM);
	case_WLAN(MESH_INVALID_SECURITY);
	case_WLAN(MESH_PATH_ERROR);
	case_WLAN(MESH_PATH_NOFORWARD);
	case_WLAN(MESH_PATH_DEST_UNREACHABLE);
	case_WLAN(MAC_EXISTS_IN_MBSS);
	case_WLAN(MESH_CHAN_REGULATORY);
	case_WLAN(MESH_CHAN);
	default: return "<unknown>";
	}
}

static void ieee80211_rx_mgmt_deauth(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u16 reason_code = le16_to_cpu(mgmt->u.deauth.reason_code);

	sdata_assert_lock(sdata);

	if (len < 24 + 2)
		return;

	if (!ether_addr_equal(mgmt->bssid, mgmt->sa)) {
		ieee80211_tdls_handle_disconnect(sdata, mgmt->sa, reason_code);
		return;
	}

	if (ifmgd->associated &&
	    ether_addr_equal(mgmt->bssid, sdata->vif.cfg.ap_addr)) {
		sdata_info(sdata, "deauthenticated from %pM (Reason: %u=%s)\n",
			   sdata->vif.cfg.ap_addr, reason_code,
			   ieee80211_get_reason_code_string(reason_code));

		ieee80211_set_disassoc(sdata, 0, 0, false, NULL);

		ieee80211_report_disconnect(sdata, (u8 *)mgmt, len, false,
					    reason_code, false);
		return;
	}

	if (ifmgd->assoc_data &&
	    ether_addr_equal(mgmt->bssid, ifmgd->assoc_data->ap_addr)) {
		sdata_info(sdata,
			   "deauthenticated from %pM while associating (Reason: %u=%s)\n",
			   ifmgd->assoc_data->ap_addr, reason_code,
			   ieee80211_get_reason_code_string(reason_code));

		ieee80211_destroy_assoc_data(sdata, ASSOC_ABANDON);

		cfg80211_rx_mlme_mgmt(sdata->dev, (u8 *)mgmt, len);
		return;
	}
}


static void ieee80211_rx_mgmt_disassoc(struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u16 reason_code;

	sdata_assert_lock(sdata);

	if (len < 24 + 2)
		return;

	if (!ifmgd->associated ||
	    !ether_addr_equal(mgmt->bssid, sdata->vif.cfg.ap_addr))
		return;

	reason_code = le16_to_cpu(mgmt->u.disassoc.reason_code);

	if (!ether_addr_equal(mgmt->bssid, mgmt->sa)) {
		ieee80211_tdls_handle_disconnect(sdata, mgmt->sa, reason_code);
		return;
	}

	sdata_info(sdata, "disassociated from %pM (Reason: %u=%s)\n",
		   sdata->vif.cfg.ap_addr, reason_code,
		   ieee80211_get_reason_code_string(reason_code));

	ieee80211_set_disassoc(sdata, 0, 0, false, NULL);

	ieee80211_report_disconnect(sdata, (u8 *)mgmt, len, false, reason_code,
				    false);
}

static void ieee80211_get_rates(struct ieee80211_supported_band *sband,
				u8 *supp_rates, unsigned int supp_rates_len,
				u32 *rates, u32 *basic_rates,
				bool *have_higher_than_11mbit,
				int *min_rate, int *min_rate_index,
				int shift)
{
	int i, j;

	for (i = 0; i < supp_rates_len; i++) {
		int rate = supp_rates[i] & 0x7f;
		bool is_basic = !!(supp_rates[i] & 0x80);

		if ((rate * 5 * (1 << shift)) > 110)
			*have_higher_than_11mbit = true;

		/*
		 * Skip HT, VHT, HE and SAE H2E only BSS membership selectors
		 * since they're not rates.
		 *
		 * Note: Even though the membership selector and the basic
		 *	 rate flag share the same bit, they are not exactly
		 *	 the same.
		 */
		if (supp_rates[i] == (0x80 | BSS_MEMBERSHIP_SELECTOR_HT_PHY) ||
		    supp_rates[i] == (0x80 | BSS_MEMBERSHIP_SELECTOR_VHT_PHY) ||
		    supp_rates[i] == (0x80 | BSS_MEMBERSHIP_SELECTOR_HE_PHY) ||
		    supp_rates[i] == (0x80 | BSS_MEMBERSHIP_SELECTOR_SAE_H2E))
			continue;

		for (j = 0; j < sband->n_bitrates; j++) {
			struct ieee80211_rate *br;
			int brate;

			br = &sband->bitrates[j];

			brate = DIV_ROUND_UP(br->bitrate, (1 << shift) * 5);
			if (brate == rate) {
				*rates |= BIT(j);
				if (is_basic)
					*basic_rates |= BIT(j);
				if ((rate * 5) < *min_rate) {
					*min_rate = rate * 5;
					*min_rate_index = j;
				}
				break;
			}
		}
	}
}

static bool ieee80211_twt_req_supported(const struct link_sta_info *link_sta,
					const struct ieee802_11_elems *elems)
{
	if (elems->ext_capab_len < 10)
		return false;

	if (!(elems->ext_capab[9] & WLAN_EXT_CAPA10_TWT_RESPONDER_SUPPORT))
		return false;

	return link_sta->pub->he_cap.he_cap_elem.mac_cap_info[0] &
		IEEE80211_HE_MAC_CAP0_TWT_RES;
}

static int ieee80211_recalc_twt_req(struct ieee80211_link_data *link,
				    struct link_sta_info *link_sta,
				    struct ieee802_11_elems *elems)
{
	bool twt = ieee80211_twt_req_supported(link_sta, elems);

	if (link->conf->twt_requester != twt) {
		link->conf->twt_requester = twt;
		return BSS_CHANGED_TWT;
	}
	return 0;
}

static bool ieee80211_twt_bcast_support(struct ieee80211_sub_if_data *sdata,
					struct ieee80211_bss_conf *bss_conf,
					struct ieee80211_supported_band *sband,
					struct link_sta_info *link_sta)
{
	const struct ieee80211_sta_he_cap *own_he_cap =
		ieee80211_get_he_iftype_cap(sband,
					    ieee80211_vif_type_p2p(&sdata->vif));

	return bss_conf->he_support &&
		(link_sta->pub->he_cap.he_cap_elem.mac_cap_info[2] &
			IEEE80211_HE_MAC_CAP2_BCAST_TWT) &&
		own_he_cap &&
		(own_he_cap->he_cap_elem.mac_cap_info[2] &
			IEEE80211_HE_MAC_CAP2_BCAST_TWT);
}

static bool ieee80211_assoc_config_link(struct ieee80211_link_data *link,
					struct link_sta_info *link_sta,
					struct cfg80211_bss *cbss,
					struct ieee80211_mgmt *mgmt,
					const u8 *elem_start,
					unsigned int elem_len,
					u64 *changed)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_mgd_assoc_data *assoc_data = sdata->u.mgd.assoc_data;
	struct ieee80211_bss_conf *bss_conf = link->conf;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_elems_parse_params parse_params = {
		.start = elem_start,
		.len = elem_len,
		.bss = cbss,
		.link_id = link == &sdata->deflink ? -1 : link->link_id,
		.from_ap = true,
	};
	bool is_6ghz = cbss->channel->band == NL80211_BAND_6GHZ;
	bool is_s1g = cbss->channel->band == NL80211_BAND_S1GHZ;
	const struct cfg80211_bss_ies *bss_ies = NULL;
	struct ieee80211_supported_band *sband;
	struct ieee802_11_elems *elems;
	u16 capab_info;
	bool ret;

	elems = ieee802_11_parse_elems_full(&parse_params);
	if (!elems)
		return false;

	/* FIXME: use from STA profile element after parsing that */
	capab_info = le16_to_cpu(mgmt->u.assoc_resp.capab_info);

	if (!is_s1g && !elems->supp_rates) {
		sdata_info(sdata, "no SuppRates element in AssocResp\n");
		ret = false;
		goto out;
	}

	link->u.mgd.tdls_chan_switch_prohibited =
		elems->ext_capab && elems->ext_capab_len >= 5 &&
		(elems->ext_capab[4] & WLAN_EXT_CAPA5_TDLS_CH_SW_PROHIBITED);

	/*
	 * Some APs are erroneously not including some information in their
	 * (re)association response frames. Try to recover by using the data
	 * from the beacon or probe response. This seems to afflict mobile
	 * 2G/3G/4G wifi routers, reported models include the "Onda PN51T",
	 * "Vodafone PocketWiFi 2", "ZTE MF60" and a similar T-Mobile device.
	 */
	if (!is_6ghz &&
	    ((assoc_data->wmm && !elems->wmm_param) ||
	     (!(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HT) &&
	      (!elems->ht_cap_elem || !elems->ht_operation)) ||
	     (!(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_VHT) &&
	      (!elems->vht_cap_elem || !elems->vht_operation)))) {
		const struct cfg80211_bss_ies *ies;
		struct ieee802_11_elems *bss_elems;

		rcu_read_lock();
		ies = rcu_dereference(cbss->ies);
		if (ies)
			bss_ies = kmemdup(ies, sizeof(*ies) + ies->len,
					  GFP_ATOMIC);
		rcu_read_unlock();
		if (!bss_ies) {
			ret = false;
			goto out;
		}

		parse_params.start = bss_ies->data;
		parse_params.len = bss_ies->len;
		bss_elems = ieee802_11_parse_elems_full(&parse_params);
		if (!bss_elems) {
			ret = false;
			goto out;
		}

		if (assoc_data->wmm &&
		    !elems->wmm_param && bss_elems->wmm_param) {
			elems->wmm_param = bss_elems->wmm_param;
			sdata_info(sdata,
				   "AP bug: WMM param missing from AssocResp\n");
		}

		/*
		 * Also check if we requested HT/VHT, otherwise the AP doesn't
		 * have to include the IEs in the (re)association response.
		 */
		if (!elems->ht_cap_elem && bss_elems->ht_cap_elem &&
		    !(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HT)) {
			elems->ht_cap_elem = bss_elems->ht_cap_elem;
			sdata_info(sdata,
				   "AP bug: HT capability missing from AssocResp\n");
		}
		if (!elems->ht_operation && bss_elems->ht_operation &&
		    !(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HT)) {
			elems->ht_operation = bss_elems->ht_operation;
			sdata_info(sdata,
				   "AP bug: HT operation missing from AssocResp\n");
		}
		if (!elems->vht_cap_elem && bss_elems->vht_cap_elem &&
		    !(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_VHT)) {
			elems->vht_cap_elem = bss_elems->vht_cap_elem;
			sdata_info(sdata,
				   "AP bug: VHT capa missing from AssocResp\n");
		}
		if (!elems->vht_operation && bss_elems->vht_operation &&
		    !(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_VHT)) {
			elems->vht_operation = bss_elems->vht_operation;
			sdata_info(sdata,
				   "AP bug: VHT operation missing from AssocResp\n");
		}

		kfree(bss_elems);
	}

	/*
	 * We previously checked these in the beacon/probe response, so
	 * they should be present here. This is just a safety net.
	 */
	if (!is_6ghz && !(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HT) &&
	    (!elems->wmm_param || !elems->ht_cap_elem || !elems->ht_operation)) {
		sdata_info(sdata,
			   "HT AP is missing WMM params or HT capability/operation\n");
		ret = false;
		goto out;
	}

	if (!is_6ghz && !(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_VHT) &&
	    (!elems->vht_cap_elem || !elems->vht_operation)) {
		sdata_info(sdata,
			   "VHT AP is missing VHT capability/operation\n");
		ret = false;
		goto out;
	}

	if (is_6ghz && !(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HE) &&
	    !elems->he_6ghz_capa) {
		sdata_info(sdata,
			   "HE 6 GHz AP is missing HE 6 GHz band capability\n");
		ret = false;
		goto out;
	}

	if (WARN_ON(!link->conf->chandef.chan)) {
		ret = false;
		goto out;
	}
	sband = local->hw.wiphy->bands[link->conf->chandef.chan->band];

	if (!(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HE) &&
	    (!elems->he_cap || !elems->he_operation)) {
		sdata_info(sdata,
			   "HE AP is missing HE capability/operation\n");
		ret = false;
		goto out;
	}

	/* Set up internal HT/VHT capabilities */
	if (elems->ht_cap_elem && !(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HT))
		ieee80211_ht_cap_ie_to_sta_ht_cap(sdata, sband,
						  elems->ht_cap_elem,
						  link_sta);

	if (elems->vht_cap_elem && !(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_VHT))
		ieee80211_vht_cap_ie_to_sta_vht_cap(sdata, sband,
						    elems->vht_cap_elem,
						    link_sta);

	if (elems->he_operation && !(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HE) &&
	    elems->he_cap) {
		ieee80211_he_cap_ie_to_sta_he_cap(sdata, sband,
						  elems->he_cap,
						  elems->he_cap_len,
						  elems->he_6ghz_capa,
						  link_sta);

		bss_conf->he_support = link_sta->pub->he_cap.has_he;
		if (elems->rsnx && elems->rsnx_len &&
		    (elems->rsnx[0] & WLAN_RSNX_CAPA_PROTECTED_TWT) &&
		    wiphy_ext_feature_isset(local->hw.wiphy,
					    NL80211_EXT_FEATURE_PROTECTED_TWT))
			bss_conf->twt_protected = true;
		else
			bss_conf->twt_protected = false;

		*changed |= ieee80211_recalc_twt_req(link, link_sta, elems);

		if (elems->eht_operation && elems->eht_cap &&
		    !(link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_EHT)) {
			ieee80211_eht_cap_ie_to_sta_eht_cap(sdata, sband,
							    elems->he_cap,
							    elems->he_cap_len,
							    elems->eht_cap,
							    elems->eht_cap_len,
							    link_sta);

			bss_conf->eht_support = link_sta->pub->eht_cap.has_eht;
		} else {
			bss_conf->eht_support = false;
		}
	} else {
		bss_conf->he_support = false;
		bss_conf->twt_requester = false;
		bss_conf->twt_protected = false;
		bss_conf->eht_support = false;
	}

	bss_conf->twt_broadcast =
		ieee80211_twt_bcast_support(sdata, bss_conf, sband, link_sta);

	if (bss_conf->he_support) {
		bss_conf->he_bss_color.color =
			le32_get_bits(elems->he_operation->he_oper_params,
				      IEEE80211_HE_OPERATION_BSS_COLOR_MASK);
		bss_conf->he_bss_color.partial =
			le32_get_bits(elems->he_operation->he_oper_params,
				      IEEE80211_HE_OPERATION_PARTIAL_BSS_COLOR);
		bss_conf->he_bss_color.enabled =
			!le32_get_bits(elems->he_operation->he_oper_params,
				       IEEE80211_HE_OPERATION_BSS_COLOR_DISABLED);

		if (bss_conf->he_bss_color.enabled)
			*changed |= BSS_CHANGED_HE_BSS_COLOR;

		bss_conf->htc_trig_based_pkt_ext =
			le32_get_bits(elems->he_operation->he_oper_params,
				      IEEE80211_HE_OPERATION_DFLT_PE_DURATION_MASK);
		bss_conf->frame_time_rts_th =
			le32_get_bits(elems->he_operation->he_oper_params,
				      IEEE80211_HE_OPERATION_RTS_THRESHOLD_MASK);

		bss_conf->uora_exists = !!elems->uora_element;
		if (elems->uora_element)
			bss_conf->uora_ocw_range = elems->uora_element[0];

		ieee80211_he_op_ie_to_bss_conf(&sdata->vif, elems->he_operation);
		ieee80211_he_spr_ie_to_bss_conf(&sdata->vif, elems->he_spr);
		/* TODO: OPEN: what happens if BSS color disable is set? */
	}

	if (cbss->transmitted_bss) {
		bss_conf->nontransmitted = true;
		ether_addr_copy(bss_conf->transmitter_bssid,
				cbss->transmitted_bss->bssid);
		bss_conf->bssid_indicator = cbss->max_bssid_indicator;
		bss_conf->bssid_index = cbss->bssid_index;
	}

	/*
	 * Some APs, e.g. Netgear WNDR3700, report invalid HT operation data
	 * in their association response, so ignore that data for our own
	 * configuration. If it changed since the last beacon, we'll get the
	 * next beacon and update then.
	 */

	/*
	 * If an operating mode notification IE is present, override the
	 * NSS calculation (that would be done in rate_control_rate_init())
	 * and use the # of streams from that element.
	 */
	if (elems->opmode_notif &&
	    !(*elems->opmode_notif & IEEE80211_OPMODE_NOTIF_RX_NSS_TYPE_BF)) {
		u8 nss;

		nss = *elems->opmode_notif & IEEE80211_OPMODE_NOTIF_RX_NSS_MASK;
		nss >>= IEEE80211_OPMODE_NOTIF_RX_NSS_SHIFT;
		nss += 1;
		link_sta->pub->rx_nss = nss;
	}

	/*
	 * Always handle WMM once after association regardless
	 * of the first value the AP uses. Setting -1 here has
	 * that effect because the AP values is an unsigned
	 * 4-bit value.
	 */
	link->u.mgd.wmm_last_param_set = -1;
	link->u.mgd.mu_edca_last_param_set = -1;

	if (link->u.mgd.disable_wmm_tracking) {
		ieee80211_set_wmm_default(link, false, false);
	} else if (!ieee80211_sta_wmm_params(local, link, elems->wmm_param,
					     elems->wmm_param_len,
					     elems->mu_edca_param_set)) {
		/* still enable QoS since we might have HT/VHT */
		ieee80211_set_wmm_default(link, false, true);
		/* disable WMM tracking in this case to disable
		 * tracking WMM parameter changes in the beacon if
		 * the parameters weren't actually valid. Doing so
		 * avoids changing parameters very strangely when
		 * the AP is going back and forth between valid and
		 * invalid parameters.
		 */
		link->u.mgd.disable_wmm_tracking = true;
	}

	if (elems->max_idle_period_ie) {
		bss_conf->max_idle_period =
			le16_to_cpu(elems->max_idle_period_ie->max_idle_period);
		bss_conf->protected_keep_alive =
			!!(elems->max_idle_period_ie->idle_options &
			   WLAN_IDLE_OPTIONS_PROTECTED_KEEP_ALIVE);
		*changed |= BSS_CHANGED_KEEP_ALIVE;
	} else {
		bss_conf->max_idle_period = 0;
		bss_conf->protected_keep_alive = false;
	}

	/* set assoc capability (AID was already set earlier),
	 * ieee80211_set_associated() will tell the driver */
	bss_conf->assoc_capability = capab_info;

	ret = true;
out:
	kfree(elems);
	kfree(bss_ies);
	return ret;
}

static int ieee80211_mgd_setup_link_sta(struct ieee80211_link_data *link,
					struct sta_info *sta,
					struct link_sta_info *link_sta,
					struct cfg80211_bss *cbss)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_bss *bss = (void *)cbss->priv;
	u32 rates = 0, basic_rates = 0;
	bool have_higher_than_11mbit = false;
	int min_rate = INT_MAX, min_rate_index = -1;
	/* this is clearly wrong for MLO but we'll just remove it later */
	int shift = ieee80211_vif_get_shift(&sdata->vif);
	struct ieee80211_supported_band *sband;

	memcpy(link_sta->addr, cbss->bssid, ETH_ALEN);
	memcpy(link_sta->pub->addr, cbss->bssid, ETH_ALEN);

	/* TODO: S1G Basic Rate Set is expressed elsewhere */
	if (cbss->channel->band == NL80211_BAND_S1GHZ) {
		ieee80211_s1g_sta_rate_init(sta);
		return 0;
	}

	sband = local->hw.wiphy->bands[cbss->channel->band];

	ieee80211_get_rates(sband, bss->supp_rates, bss->supp_rates_len,
			    &rates, &basic_rates, &have_higher_than_11mbit,
			    &min_rate, &min_rate_index, shift);

	/*
	 * This used to be a workaround for basic rates missing
	 * in the association response frame. Now that we no
	 * longer use the basic rates from there, it probably
	 * doesn't happen any more, but keep the workaround so
	 * in case some *other* APs are buggy in different ways
	 * we can connect -- with a warning.
	 * Allow this workaround only in case the AP provided at least
	 * one rate.
	 */
	if (min_rate_index < 0) {
		link_info(link, "No legacy rates in association response\n");
		return -EINVAL;
	} else if (!basic_rates) {
		link_info(link, "No basic rates, using min rate instead\n");
		basic_rates = BIT(min_rate_index);
	}

	if (rates)
		link_sta->pub->supp_rates[cbss->channel->band] = rates;
	else
		link_info(link, "No rates found, keeping mandatory only\n");

	link->conf->basic_rates = basic_rates;

	/* cf. IEEE 802.11 9.2.12 */
	link->operating_11g_mode = sband->band == NL80211_BAND_2GHZ &&
				   have_higher_than_11mbit;

	return 0;
}

static u8 ieee80211_max_rx_chains(struct ieee80211_link_data *link,
				  struct cfg80211_bss *cbss)
{
	struct ieee80211_he_mcs_nss_supp *he_mcs_nss_supp;
	const struct element *ht_cap_elem, *vht_cap_elem;
	const struct cfg80211_bss_ies *ies;
	const struct ieee80211_ht_cap *ht_cap;
	const struct ieee80211_vht_cap *vht_cap;
	const struct ieee80211_he_cap_elem *he_cap;
	const struct element *he_cap_elem;
	u16 mcs_80_map, mcs_160_map;
	int i, mcs_nss_size;
	bool support_160;
	u8 chains = 1;

	if (link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HT)
		return chains;

	ht_cap_elem = ieee80211_bss_get_elem(cbss, WLAN_EID_HT_CAPABILITY);
	if (ht_cap_elem && ht_cap_elem->datalen >= sizeof(*ht_cap)) {
		ht_cap = (void *)ht_cap_elem->data;
		chains = ieee80211_mcs_to_chains(&ht_cap->mcs);
		/*
		 * TODO: use "Tx Maximum Number Spatial Streams Supported" and
		 *	 "Tx Unequal Modulation Supported" fields.
		 */
	}

	if (link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_VHT)
		return chains;

	vht_cap_elem = ieee80211_bss_get_elem(cbss, WLAN_EID_VHT_CAPABILITY);
	if (vht_cap_elem && vht_cap_elem->datalen >= sizeof(*vht_cap)) {
		u8 nss;
		u16 tx_mcs_map;

		vht_cap = (void *)vht_cap_elem->data;
		tx_mcs_map = le16_to_cpu(vht_cap->supp_mcs.tx_mcs_map);
		for (nss = 8; nss > 0; nss--) {
			if (((tx_mcs_map >> (2 * (nss - 1))) & 3) !=
					IEEE80211_VHT_MCS_NOT_SUPPORTED)
				break;
		}
		/* TODO: use "Tx Highest Supported Long GI Data Rate" field? */
		chains = max(chains, nss);
	}

	if (link->u.mgd.conn_flags & IEEE80211_CONN_DISABLE_HE)
		return chains;

	ies = rcu_dereference(cbss->ies);
	he_cap_elem = cfg80211_find_ext_elem(WLAN_EID_EXT_HE_CAPABILITY,
					     ies->data, ies->len);

	if (!he_cap_elem || he_cap_elem->datalen < sizeof(*he_cap))
		return chains;

	/* skip one byte ext_tag_id */
	he_cap = (void *)(he_cap_elem->data + 1);
	mcs_nss_size = ieee80211_he_mcs_nss_size(he_cap);

	/* invalid HE IE */
	if (he_cap_elem->datalen < 1 + mcs_nss_size + sizeof(*he_cap))
		return chains;

	/* mcs_nss is right after he_cap info */
	he_mcs_nss_supp = (void *)(he_cap + 1);

	mcs_80_map = le16_to_cpu(he_mcs_nss_supp->tx_mcs_80);

	for (i = 7; i >= 0; i--) {
		u8 mcs_80 = mcs_80_map >> (2 * i) & 3;

		if (mcs_80 != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
			chains = max_t(u8, chains, i + 1);
			break;
		}
	}

	support_160 = he_cap->phy_cap_info[0] &
		      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;

	if (!support_160)
		return chains;

	mcs_160_map = le16_to_cpu(he_mcs_nss_supp->tx_mcs_160);
	for (i = 7; i >= 0; i--) {
		u8 mcs_160 = mcs_160_map >> (2 * i) & 3;

		if (mcs_160 != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
			chains = max_t(u8, chains, i + 1);
			break;
		}
	}

	return chains;
}

static bool
ieee80211_verify_peer_he_mcs_support(struct ieee80211_sub_if_data *sdata,
				     const struct cfg80211_bss_ies *ies,
				     const struct ieee80211_he_operation *he_op)
{
	const struct element *he_cap_elem;
	const struct ieee80211_he_cap_elem *he_cap;
	struct ieee80211_he_mcs_nss_supp *he_mcs_nss_supp;
	u16 mcs_80_map_tx, mcs_80_map_rx;
	u16 ap_min_req_set;
	int mcs_nss_size;
	int nss;

	he_cap_elem = cfg80211_find_ext_elem(WLAN_EID_EXT_HE_CAPABILITY,
					     ies->data, ies->len);

	/* invalid HE IE */
	if (!he_cap_elem || he_cap_elem->datalen < 1 + sizeof(*he_cap)) {
		sdata_info(sdata,
			   "Invalid HE elem, Disable HE\n");
		return false;
	}

	/* skip one byte ext_tag_id */
	he_cap = (void *)(he_cap_elem->data + 1);
	mcs_nss_size = ieee80211_he_mcs_nss_size(he_cap);

	/* invalid HE IE */
	if (he_cap_elem->datalen < 1 + sizeof(*he_cap) + mcs_nss_size) {
		sdata_info(sdata,
			   "Invalid HE elem with nss size, Disable HE\n");
		return false;
	}

	/* mcs_nss is right after he_cap info */
	he_mcs_nss_supp = (void *)(he_cap + 1);

	mcs_80_map_tx = le16_to_cpu(he_mcs_nss_supp->tx_mcs_80);
	mcs_80_map_rx = le16_to_cpu(he_mcs_nss_supp->rx_mcs_80);

	/* P802.11-REVme/D0.3
	 * 27.1.1 Introduction to the HE PHY
	 * ...
	 * An HE STA shall support the following features:
	 * ...
	 * Single spatial stream HE-MCSs 0 to 7 (transmit and receive) in all
	 * supported channel widths for HE SU PPDUs
	 */
	if ((mcs_80_map_tx & 0x3) == IEEE80211_HE_MCS_NOT_SUPPORTED ||
	    (mcs_80_map_rx & 0x3) == IEEE80211_HE_MCS_NOT_SUPPORTED) {
		sdata_info(sdata,
			   "Missing mandatory rates for 1 Nss, rx 0x%x, tx 0x%x, disable HE\n",
			   mcs_80_map_tx, mcs_80_map_rx);
		return false;
	}

	if (!he_op)
		return true;

	ap_min_req_set = le16_to_cpu(he_op->he_mcs_nss_set);

	/*
	 * Apparently iPhone 13 (at least iOS version 15.3.1) sets this to all
	 * zeroes, which is nonsense, and completely inconsistent with itself
	 * (it doesn't have 8 streams). Accept the settings in this case anyway.
	 */
	if (!ap_min_req_set)
		return true;

	/* make sure the AP is consistent with itself
	 *
	 * P802.11-REVme/D0.3
	 * 26.17.1 Basic HE BSS operation
	 *
	 * A STA that is operating in an HE BSS shall be able to receive and
	 * transmit at each of the <HE-MCS, NSS> tuple values indicated by the
	 * Basic HE-MCS And NSS Set field of the HE Operation parameter of the
	 * MLME-START.request primitive and shall be able to receive at each of
	 * the <HE-MCS, NSS> tuple values indicated by the Supported HE-MCS and
	 * NSS Set field in the HE Capabilities parameter of the MLMESTART.request
	 * primitive
	 */
	for (nss = 8; nss > 0; nss--) {
		u8 ap_op_val = (ap_min_req_set >> (2 * (nss - 1))) & 3;
		u8 ap_rx_val;
		u8 ap_tx_val;

		if (ap_op_val == IEEE80211_HE_MCS_NOT_SUPPORTED)
			continue;

		ap_rx_val = (mcs_80_map_rx >> (2 * (nss - 1))) & 3;
		ap_tx_val = (mcs_80_map_tx >> (2 * (nss - 1))) & 3;

		if (ap_rx_val == IEEE80211_HE_MCS_NOT_SUPPORTED ||
		    ap_tx_val == IEEE80211_HE_MCS_NOT_SUPPORTED ||
		    ap_rx_val < ap_op_val || ap_tx_val < ap_op_val) {
			sdata_info(sdata,
				   "Invalid rates for %d Nss, rx %d, tx %d oper %d, disable HE\n",
				   nss, ap_rx_val, ap_rx_val, ap_op_val);
			return false;
		}
	}

	return true;
}

static bool
ieee80211_verify_sta_he_mcs_support(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_supported_band *sband,
				    const struct ieee80211_he_operation *he_op)
{
	const struct ieee80211_sta_he_cap *sta_he_cap =
		ieee80211_get_he_iftype_cap(sband,
					    ieee80211_vif_type_p2p(&sdata->vif));
	u16 ap_min_req_set;
	int i;

	if (!sta_he_cap || !he_op)
		return false;

	ap_min_req_set = le16_to_cpu(he_op->he_mcs_nss_set);

	/*
	 * Apparently iPhone 13 (at least iOS version 15.3.1) sets this to all
	 * zeroes, which is nonsense, and completely inconsistent with itself
	 * (it doesn't have 8 streams). Accept the settings in this case anyway.
	 */
	if (!ap_min_req_set)
		return true;

	/* Need to go over for 80MHz, 160MHz and for 80+80 */
	for (i = 0; i < 3; i++) {
		const struct ieee80211_he_mcs_nss_supp *sta_mcs_nss_supp =
			&sta_he_cap->he_mcs_nss_supp;
		u16 sta_mcs_map_rx =
			le16_to_cpu(((__le16 *)sta_mcs_nss_supp)[2 * i]);
		u16 sta_mcs_map_tx =
			le16_to_cpu(((__le16 *)sta_mcs_nss_supp)[2 * i + 1]);
		u8 nss;
		bool verified = true;

		/*
		 * For each band there is a maximum of 8 spatial streams
		 * possible. Each of the sta_mcs_map_* is a 16-bit struct built
		 * of 2 bits per NSS (1-8), with the values defined in enum
		 * ieee80211_he_mcs_support. Need to make sure STA TX and RX
		 * capabilities aren't less than the AP's minimum requirements
		 * for this HE BSS per SS.
		 * It is enough to find one such band that meets the reqs.
		 */
		for (nss = 8; nss > 0; nss--) {
			u8 sta_rx_val = (sta_mcs_map_rx >> (2 * (nss - 1))) & 3;
			u8 sta_tx_val = (sta_mcs_map_tx >> (2 * (nss - 1))) & 3;
			u8 ap_val = (ap_min_req_set >> (2 * (nss - 1))) & 3;

			if (ap_val == IEEE80211_HE_MCS_NOT_SUPPORTED)
				continue;

			/*
			 * Make sure the HE AP doesn't require MCSs that aren't
			 * supported by the client as required by spec
			 *
			 * P802.11-REVme/D0.3
			 * 26.17.1 Basic HE BSS operation
			 *
			 * An HE STA shall not attempt to join * (MLME-JOIN.request primitive)
			 * a BSS, unless it supports (i.e., is able to both transmit and
			 * receive using) all of the <HE-MCS, NSS> tuples in the basic
			 * HE-MCS and NSS set.
			 */
			if (sta_rx_val == IEEE80211_HE_MCS_NOT_SUPPORTED ||
			    sta_tx_val == IEEE80211_HE_MCS_NOT_SUPPORTED ||
			    (ap_val > sta_rx_val) || (ap_val > sta_tx_val)) {
				verified = false;
				break;
			}
		}

		if (verified)
			return true;
	}

	/* If here, STA doesn't meet AP's HE min requirements */
	return false;
}

static int ieee80211_prep_channel(struct ieee80211_sub_if_data *sdata,
				  struct ieee80211_link_data *link,
				  struct cfg80211_bss *cbss,
				  ieee80211_conn_flags_t *conn_flags)
{
	struct ieee80211_local *local = sdata->local;
	const struct ieee80211_ht_cap *ht_cap = NULL;
	const struct ieee80211_ht_operation *ht_oper = NULL;
	const struct ieee80211_vht_operation *vht_oper = NULL;
	const struct ieee80211_he_operation *he_oper = NULL;
	const struct ieee80211_eht_operation *eht_oper = NULL;
	const struct ieee80211_s1g_oper_ie *s1g_oper = NULL;
	struct ieee80211_supported_band *sband;
	struct cfg80211_chan_def chandef;
	bool is_6ghz = cbss->channel->band == NL80211_BAND_6GHZ;
	bool is_5ghz = cbss->channel->band == NL80211_BAND_5GHZ;
	struct ieee80211_bss *bss = (void *)cbss->priv;
	struct ieee80211_elems_parse_params parse_params = {
		.bss = cbss,
		.link_id = -1,
		.from_ap = true,
	};
	struct ieee802_11_elems *elems;
	const struct cfg80211_bss_ies *ies;
	int ret;
	u32 i;
	bool have_80mhz;

	rcu_read_lock();

	ies = rcu_dereference(cbss->ies);
	parse_params.start = ies->data;
	parse_params.len = ies->len;
	elems = ieee802_11_parse_elems_full(&parse_params);
	if (!elems) {
		rcu_read_unlock();
		return -ENOMEM;
	}

	sband = local->hw.wiphy->bands[cbss->channel->band];

	*conn_flags &= ~(IEEE80211_CONN_DISABLE_40MHZ |
			 IEEE80211_CONN_DISABLE_80P80MHZ |
			 IEEE80211_CONN_DISABLE_160MHZ);

	/* disable HT/VHT/HE if we don't support them */
	if (!sband->ht_cap.ht_supported && !is_6ghz) {
		mlme_dbg(sdata, "HT not supported, disabling HT/VHT/HE/EHT\n");
		*conn_flags |= IEEE80211_CONN_DISABLE_HT;
		*conn_flags |= IEEE80211_CONN_DISABLE_VHT;
		*conn_flags |= IEEE80211_CONN_DISABLE_HE;
		*conn_flags |= IEEE80211_CONN_DISABLE_EHT;
	}

	if (!sband->vht_cap.vht_supported && is_5ghz) {
		mlme_dbg(sdata, "VHT not supported, disabling VHT/HE/EHT\n");
		*conn_flags |= IEEE80211_CONN_DISABLE_VHT;
		*conn_flags |= IEEE80211_CONN_DISABLE_HE;
		*conn_flags |= IEEE80211_CONN_DISABLE_EHT;
	}

	if (!ieee80211_get_he_iftype_cap(sband,
					 ieee80211_vif_type_p2p(&sdata->vif))) {
		mlme_dbg(sdata, "HE not supported, disabling HE and EHT\n");
		*conn_flags |= IEEE80211_CONN_DISABLE_HE;
		*conn_flags |= IEEE80211_CONN_DISABLE_EHT;
	}

	if (!ieee80211_get_eht_iftype_cap(sband,
					  ieee80211_vif_type_p2p(&sdata->vif))) {
		mlme_dbg(sdata, "EHT not supported, disabling EHT\n");
		*conn_flags |= IEEE80211_CONN_DISABLE_EHT;
	}

	if (!(*conn_flags & IEEE80211_CONN_DISABLE_HT) && !is_6ghz) {
		ht_oper = elems->ht_operation;
		ht_cap = elems->ht_cap_elem;

		if (!ht_cap) {
			*conn_flags |= IEEE80211_CONN_DISABLE_HT;
			ht_oper = NULL;
		}
	}

	if (!(*conn_flags & IEEE80211_CONN_DISABLE_VHT) && !is_6ghz) {
		vht_oper = elems->vht_operation;
		if (vht_oper && !ht_oper) {
			vht_oper = NULL;
			sdata_info(sdata,
				   "AP advertised VHT without HT, disabling HT/VHT/HE\n");
			*conn_flags |= IEEE80211_CONN_DISABLE_HT;
			*conn_flags |= IEEE80211_CONN_DISABLE_VHT;
			*conn_flags |= IEEE80211_CONN_DISABLE_HE;
			*conn_flags |= IEEE80211_CONN_DISABLE_EHT;
		}

		if (!elems->vht_cap_elem) {
			sdata_info(sdata,
				   "bad VHT capabilities, disabling VHT\n");
			*conn_flags |= IEEE80211_CONN_DISABLE_VHT;
			vht_oper = NULL;
		}
	}

	if (!(*conn_flags & IEEE80211_CONN_DISABLE_HE)) {
		he_oper = elems->he_operation;

		if (link && is_6ghz) {
			struct ieee80211_bss_conf *bss_conf;
			u8 j = 0;

			bss_conf = link->conf;

			if (elems->pwr_constr_elem)
				bss_conf->pwr_reduction = *elems->pwr_constr_elem;

			BUILD_BUG_ON(ARRAY_SIZE(bss_conf->tx_pwr_env) !=
				     ARRAY_SIZE(elems->tx_pwr_env));

			for (i = 0; i < elems->tx_pwr_env_num; i++) {
				if (elems->tx_pwr_env_len[i] >
				    sizeof(bss_conf->tx_pwr_env[j]))
					continue;

				bss_conf->tx_pwr_env_num++;
				memcpy(&bss_conf->tx_pwr_env[j], elems->tx_pwr_env[i],
				       elems->tx_pwr_env_len[i]);
				j++;
			}
		}

		if (!ieee80211_verify_peer_he_mcs_support(sdata, ies, he_oper) ||
		    !ieee80211_verify_sta_he_mcs_support(sdata, sband, he_oper))
			*conn_flags |= IEEE80211_CONN_DISABLE_HE |
				       IEEE80211_CONN_DISABLE_EHT;
	}

	/*
	 * EHT requires HE to be supported as well. Specifically for 6 GHz
	 * channels, the operation channel information can only be deduced from
	 * both the 6 GHz operation information (from the HE operation IE) and
	 * EHT operation.
	 */
	if (!(*conn_flags &
			(IEEE80211_CONN_DISABLE_HE |
			 IEEE80211_CONN_DISABLE_EHT)) &&
	    he_oper) {
		const struct cfg80211_bss_ies *cbss_ies;
		const u8 *eht_oper_ie;

		cbss_ies = rcu_dereference(cbss->ies);
		eht_oper_ie = cfg80211_find_ext_ie(WLAN_EID_EXT_EHT_OPERATION,
						   cbss_ies->data, cbss_ies->len);
		if (eht_oper_ie && eht_oper_ie[1] >=
		    1 + sizeof(struct ieee80211_eht_operation))
			eht_oper = (void *)(eht_oper_ie + 3);
		else
			eht_oper = NULL;
	}

	/* Allow VHT if at least one channel on the sband supports 80 MHz */
	have_80mhz = false;
	for (i = 0; i < sband->n_channels; i++) {
		if (sband->channels[i].flags & (IEEE80211_CHAN_DISABLED |
						IEEE80211_CHAN_NO_80MHZ))
			continue;

		have_80mhz = true;
		break;
	}

	if (!have_80mhz) {
		sdata_info(sdata, "80 MHz not supported, disabling VHT\n");
		*conn_flags |= IEEE80211_CONN_DISABLE_VHT;
	}

	if (sband->band == NL80211_BAND_S1GHZ) {
		s1g_oper = elems->s1g_oper;
		if (!s1g_oper)
			sdata_info(sdata,
				   "AP missing S1G operation element?\n");
	}

	*conn_flags |=
		ieee80211_determine_chantype(sdata, link, *conn_flags,
					     sband,
					     cbss->channel,
					     bss->vht_cap_info,
					     ht_oper, vht_oper,
					     he_oper, eht_oper,
					     s1g_oper,
					     &chandef, false);

	if (link)
		link->needed_rx_chains =
			min(ieee80211_max_rx_chains(link, cbss),
			    local->rx_chains);

	rcu_read_unlock();
	/* the element data was RCU protected so no longer valid anyway */
	kfree(elems);
	elems = NULL;

	if (*conn_flags & IEEE80211_CONN_DISABLE_HE && is_6ghz) {
		sdata_info(sdata, "Rejecting non-HE 6/7 GHz connection");
		return -EINVAL;
	}

	if (!link)
		return 0;

	/* will change later if needed */
	link->smps_mode = IEEE80211_SMPS_OFF;

	mutex_lock(&local->mtx);
	/*
	 * If this fails (possibly due to channel context sharing
	 * on incompatible channels, e.g. 80+80 and 160 sharing the
	 * same control channel) try to use a smaller bandwidth.
	 */
	ret = ieee80211_link_use_channel(link, &chandef,
					 IEEE80211_CHANCTX_SHARED);

	/* don't downgrade for 5 and 10 MHz channels, though. */
	if (chandef.width == NL80211_CHAN_WIDTH_5 ||
	    chandef.width == NL80211_CHAN_WIDTH_10)
		goto out;

	while (ret && chandef.width != NL80211_CHAN_WIDTH_20_NOHT) {
		*conn_flags |=
			ieee80211_chandef_downgrade(&chandef);
		ret = ieee80211_link_use_channel(link, &chandef,
						 IEEE80211_CHANCTX_SHARED);
	}
 out:
	mutex_unlock(&local->mtx);
	return ret;
}

static bool ieee80211_get_dtim(const struct cfg80211_bss_ies *ies,
			       u8 *dtim_count, u8 *dtim_period)
{
	const u8 *tim_ie = cfg80211_find_ie(WLAN_EID_TIM, ies->data, ies->len);
	const u8 *idx_ie = cfg80211_find_ie(WLAN_EID_MULTI_BSSID_IDX, ies->data,
					 ies->len);
	const struct ieee80211_tim_ie *tim = NULL;
	const struct ieee80211_bssid_index *idx;
	bool valid = tim_ie && tim_ie[1] >= 2;

	if (valid)
		tim = (void *)(tim_ie + 2);

	if (dtim_count)
		*dtim_count = valid ? tim->dtim_count : 0;

	if (dtim_period)
		*dtim_period = valid ? tim->dtim_period : 0;

	/* Check if value is overridden by non-transmitted profile */
	if (!idx_ie || idx_ie[1] < 3)
		return valid;

	idx = (void *)(idx_ie + 2);

	if (dtim_count)
		*dtim_count = idx->dtim_count;

	if (dtim_period)
		*dtim_period = idx->dtim_period;

	return true;
}

static bool ieee80211_assoc_success(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_mgmt *mgmt,
				    struct ieee802_11_elems *elems,
				    const u8 *elem_start, unsigned int elem_len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_assoc_data *assoc_data = ifmgd->assoc_data;
	struct ieee80211_local *local = sdata->local;
	unsigned int link_id;
	struct sta_info *sta;
	u64 changed[IEEE80211_MLD_MAX_NUM_LINKS] = {};
	int err;

	mutex_lock(&sdata->local->sta_mtx);
	/*
	 * station info was already allocated and inserted before
	 * the association and should be available to us
	 */
	sta = sta_info_get(sdata, assoc_data->ap_addr);
	if (WARN_ON(!sta))
		goto out_err;

	if (sdata->vif.valid_links) {
		u16 valid_links = 0;

		for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++) {
			if (!assoc_data->link[link_id].bss)
				continue;
			valid_links |= BIT(link_id);

			if (link_id != assoc_data->assoc_link_id) {
				err = ieee80211_sta_allocate_link(sta, link_id);
				if (err)
					goto out_err;
			}
		}

		ieee80211_vif_set_links(sdata, valid_links);
	}

	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++) {
		struct ieee80211_link_data *link;
		struct link_sta_info *link_sta;

		if (!assoc_data->link[link_id].bss)
			continue;

		link = sdata_dereference(sdata->link[link_id], sdata);
		if (WARN_ON(!link))
			goto out_err;

		if (sdata->vif.valid_links)
			link_info(link,
				  "local address %pM, AP link address %pM\n",
				  link->conf->addr,
				  assoc_data->link[link_id].bss->bssid);

		link_sta = rcu_dereference_protected(sta->link[link_id],
						     lockdep_is_held(&local->sta_mtx));
		if (WARN_ON(!link_sta))
			goto out_err;

		if (link_id != assoc_data->assoc_link_id) {
			struct cfg80211_bss *cbss = assoc_data->link[link_id].bss;
			const struct cfg80211_bss_ies *ies;

			rcu_read_lock();
			ies = rcu_dereference(cbss->ies);
			ieee80211_get_dtim(ies,
					   &link->conf->sync_dtim_count,
					   &link->u.mgd.dtim_period);
			link->conf->dtim_period = link->u.mgd.dtim_period ?: 1;
			link->conf->beacon_int = cbss->beacon_interval;
			rcu_read_unlock();

			err = ieee80211_prep_channel(sdata, link, cbss,
						     &link->u.mgd.conn_flags);
			if (err) {
				link_info(link, "prep_channel failed\n");
				goto out_err;
			}
		}

		err = ieee80211_mgd_setup_link_sta(link, sta, link_sta,
						   assoc_data->link[link_id].bss);
		if (err)
			goto out_err;

		if (!ieee80211_assoc_config_link(link, link_sta,
						 assoc_data->link[link_id].bss,
						 mgmt, elem_start, elem_len,
						 &changed[link_id]))
			goto out_err;

		if (link_id != assoc_data->assoc_link_id) {
			err = ieee80211_sta_activate_link(sta, link_id);
			if (err)
				goto out_err;
		}
	}

	rate_control_rate_init(sta);

	if (ifmgd->flags & IEEE80211_STA_MFP_ENABLED) {
		set_sta_flag(sta, WLAN_STA_MFP);
		sta->sta.mfp = true;
	} else {
		sta->sta.mfp = false;
	}

	ieee80211_sta_set_max_amsdu_subframes(sta, elems->ext_capab,
					      elems->ext_capab_len);

	sta->sta.wme = (elems->wmm_param || elems->s1g_capab) &&
		       local->hw.queues >= IEEE80211_NUM_ACS;

	err = sta_info_move_state(sta, IEEE80211_STA_ASSOC);
	if (!err && !(ifmgd->flags & IEEE80211_STA_CONTROL_PORT))
		err = sta_info_move_state(sta, IEEE80211_STA_AUTHORIZED);
	if (err) {
		sdata_info(sdata,
			   "failed to move station %pM to desired state\n",
			   sta->sta.addr);
		WARN_ON(__sta_info_destroy(sta));
		goto out_err;
	}

	if (sdata->wdev.use_4addr)
		drv_sta_set_4addr(local, sdata, &sta->sta, true);

	mutex_unlock(&sdata->local->sta_mtx);

	ieee80211_set_associated(sdata, assoc_data, changed);

	/*
	 * If we're using 4-addr mode, let the AP know that we're
	 * doing so, so that it can create the STA VLAN on its side
	 */
	if (ifmgd->use_4addr)
		ieee80211_send_4addr_nullfunc(local, sdata);

	/*
	 * Start timer to probe the connection to the AP now.
	 * Also start the timer that will detect beacon loss.
	 */
	ieee80211_sta_reset_beacon_monitor(sdata);
	ieee80211_sta_reset_conn_monitor(sdata);

	return true;
out_err:
	eth_zero_addr(sdata->vif.cfg.ap_addr);
	mutex_unlock(&sdata->local->sta_mtx);
	return false;
}

static void ieee80211_rx_mgmt_assoc_resp(struct ieee80211_sub_if_data *sdata,
					 struct ieee80211_mgmt *mgmt,
					 size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_assoc_data *assoc_data = ifmgd->assoc_data;
	u16 capab_info, status_code, aid;
	struct ieee80211_elems_parse_params parse_params = {
		.bss = NULL,
		.link_id = -1,
		.from_ap = true,
	};
	struct ieee802_11_elems *elems;
	int ac;
	const u8 *elem_start;
	unsigned int elem_len;
	bool reassoc;
	struct ieee80211_event event = {
		.type = MLME_EVENT,
		.u.mlme.data = ASSOC_EVENT,
	};
	struct ieee80211_prep_tx_info info = {};
	struct cfg80211_rx_assoc_resp resp = {
		.uapsd_queues = -1,
	};
	unsigned int link_id;

	sdata_assert_lock(sdata);

	if (!assoc_data)
		return;

	if (!ether_addr_equal(assoc_data->ap_addr, mgmt->bssid) ||
	    !ether_addr_equal(assoc_data->ap_addr, mgmt->sa))
		return;

	/*
	 * AssocResp and ReassocResp have identical structure, so process both
	 * of them in this function.
	 */

	if (len < 24 + 6)
		return;

	reassoc = ieee80211_is_reassoc_resp(mgmt->frame_control);
	capab_info = le16_to_cpu(mgmt->u.assoc_resp.capab_info);
	status_code = le16_to_cpu(mgmt->u.assoc_resp.status_code);
	if (assoc_data->s1g)
		elem_start = mgmt->u.s1g_assoc_resp.variable;
	else
		elem_start = mgmt->u.assoc_resp.variable;

	/*
	 * Note: this may not be perfect, AP might misbehave - if
	 * anyone needs to rely on perfect complete notification
	 * with the exact right subtype, then we need to track what
	 * we actually transmitted.
	 */
	info.subtype = reassoc ? IEEE80211_STYPE_REASSOC_REQ :
				 IEEE80211_STYPE_ASSOC_REQ;

	if (assoc_data->fils_kek_len &&
	    fils_decrypt_assoc_resp(sdata, (u8 *)mgmt, &len, assoc_data) < 0)
		return;

	elem_len = len - (elem_start - (u8 *)mgmt);
	parse_params.start = elem_start;
	parse_params.len = elem_len;
	elems = ieee802_11_parse_elems_full(&parse_params);
	if (!elems)
		goto notify_driver;

	if (elems->aid_resp)
		aid = le16_to_cpu(elems->aid_resp->aid);
	else if (assoc_data->s1g)
		aid = 0; /* TODO */
	else
		aid = le16_to_cpu(mgmt->u.assoc_resp.aid);

	/*
	 * The 5 MSB of the AID field are reserved
	 * (802.11-2016 9.4.1.8 AID field)
	 */
	aid &= 0x7ff;

	sdata_info(sdata,
		   "RX %sssocResp from %pM (capab=0x%x status=%d aid=%d)\n",
		   reassoc ? "Rea" : "A", assoc_data->ap_addr,
		   capab_info, status_code, (u16)(aid & ~(BIT(15) | BIT(14))));

	ifmgd->broken_ap = false;

	if (status_code == WLAN_STATUS_ASSOC_REJECTED_TEMPORARILY &&
	    elems->timeout_int &&
	    elems->timeout_int->type == WLAN_TIMEOUT_ASSOC_COMEBACK) {
		u32 tu, ms;

		cfg80211_assoc_comeback(sdata->dev, assoc_data->ap_addr,
					le32_to_cpu(elems->timeout_int->value));

		tu = le32_to_cpu(elems->timeout_int->value);
		ms = tu * 1024 / 1000;
		sdata_info(sdata,
			   "%pM rejected association temporarily; comeback duration %u TU (%u ms)\n",
			   assoc_data->ap_addr, tu, ms);
		assoc_data->timeout = jiffies + msecs_to_jiffies(ms);
		assoc_data->timeout_started = true;
		if (ms > IEEE80211_ASSOC_TIMEOUT)
			run_again(sdata, assoc_data->timeout);
		goto notify_driver;
	}

	if (status_code != WLAN_STATUS_SUCCESS) {
		sdata_info(sdata, "%pM denied association (code=%d)\n",
			   assoc_data->ap_addr, status_code);
		event.u.mlme.status = MLME_DENIED;
		event.u.mlme.reason = status_code;
		drv_event_callback(sdata->local, sdata, &event);
	} else {
		if (aid == 0 || aid > IEEE80211_MAX_AID) {
			sdata_info(sdata,
				   "invalid AID value %d (out of range), turn off PS\n",
				   aid);
			aid = 0;
			ifmgd->broken_ap = true;
		}

		if (sdata->vif.valid_links) {
			if (!elems->multi_link) {
				sdata_info(sdata,
					   "MLO association with %pM but no multi-link element in response!\n",
					   assoc_data->ap_addr);
				goto abandon_assoc;
			}

			if (le16_get_bits(elems->multi_link->control,
					  IEEE80211_ML_CONTROL_TYPE) !=
					IEEE80211_ML_CONTROL_TYPE_BASIC) {
				sdata_info(sdata,
					   "bad multi-link element (control=0x%x)\n",
					   le16_to_cpu(elems->multi_link->control));
				goto abandon_assoc;
			} else {
				struct ieee80211_mle_basic_common_info *common;

				common = (void *)elems->multi_link->variable;

				if (memcmp(assoc_data->ap_addr,
					   common->mld_mac_addr, ETH_ALEN)) {
					sdata_info(sdata,
						   "AP MLD MAC address mismatch: got %pM expected %pM\n",
						   common->mld_mac_addr,
						   assoc_data->ap_addr);
					goto abandon_assoc;
				}
			}
		}

		sdata->vif.cfg.aid = aid;

		if (!ieee80211_assoc_success(sdata, mgmt, elems,
					     elem_start, elem_len)) {
			/* oops -- internal error -- send timeout for now */
			ieee80211_destroy_assoc_data(sdata, ASSOC_TIMEOUT);
			goto notify_driver;
		}
		event.u.mlme.status = MLME_SUCCESS;
		drv_event_callback(sdata->local, sdata, &event);
		sdata_info(sdata, "associated\n");

		info.success = 1;
	}

	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++) {
		struct ieee80211_link_data *link;

		link = sdata_dereference(sdata->link[link_id], sdata);
		if (!link)
			continue;
		if (!assoc_data->link[link_id].bss)
			continue;
		resp.links[link_id].bss = assoc_data->link[link_id].bss;
		resp.links[link_id].addr = link->conf->addr;

		/* get uapsd queues configuration - same for all links */
		resp.uapsd_queues = 0;
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
			if (link->tx_conf[ac].uapsd)
				resp.uapsd_queues |= ieee80211_ac_to_qos_mask[ac];
	}

	ieee80211_destroy_assoc_data(sdata,
				     status_code == WLAN_STATUS_SUCCESS ?
					ASSOC_SUCCESS :
					ASSOC_REJECTED);

	resp.buf = (u8 *)mgmt;
	resp.len = len;
	resp.req_ies = ifmgd->assoc_req_ies;
	resp.req_ies_len = ifmgd->assoc_req_ies_len;
	if (sdata->vif.valid_links)
		resp.ap_mld_addr = sdata->vif.cfg.ap_addr;
	cfg80211_rx_assoc_resp(sdata->dev, &resp);
notify_driver:
	drv_mgd_complete_tx(sdata->local, sdata, &info);
	kfree(elems);
	return;
abandon_assoc:
	ieee80211_destroy_assoc_data(sdata, ASSOC_ABANDON);
	goto notify_driver;
}

static void ieee80211_rx_bss_info(struct ieee80211_link_data *link,
				  struct ieee80211_mgmt *mgmt, size_t len,
				  struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_bss *bss;
	struct ieee80211_channel *channel;

	sdata_assert_lock(sdata);

	channel = ieee80211_get_channel_khz(local->hw.wiphy,
					ieee80211_rx_status_to_khz(rx_status));
	if (!channel)
		return;

	bss = ieee80211_bss_info_update(local, rx_status, mgmt, len, channel);
	if (bss) {
		link->conf->beacon_rate = bss->beacon_rate;
		ieee80211_rx_bss_put(local, bss);
	}
}


static void ieee80211_rx_mgmt_probe_resp(struct ieee80211_link_data *link,
					 struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct ieee80211_if_managed *ifmgd;
	struct ieee80211_rx_status *rx_status = (void *) skb->cb;
	struct ieee80211_channel *channel;
	size_t baselen, len = skb->len;

	ifmgd = &sdata->u.mgd;

	sdata_assert_lock(sdata);

	/*
	 * According to Draft P802.11ax D6.0 clause 26.17.2.3.2:
	 * "If a 6 GHz AP receives a Probe Request frame  and responds with
	 * a Probe Response frame [..], the Address 1 field of the Probe
	 * Response frame shall be set to the broadcast address [..]"
	 * So, on 6GHz band we should also accept broadcast responses.
	 */
	channel = ieee80211_get_channel(sdata->local->hw.wiphy,
					rx_status->freq);
	if (!channel)
		return;

	if (!ether_addr_equal(mgmt->da, sdata->vif.addr) &&
	    (channel->band != NL80211_BAND_6GHZ ||
	     !is_broadcast_ether_addr(mgmt->da)))
		return; /* ignore ProbeResp to foreign address */

	baselen = (u8 *) mgmt->u.probe_resp.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	ieee80211_rx_bss_info(link, mgmt, len, rx_status);

	if (ifmgd->associated &&
	    ether_addr_equal(mgmt->bssid, link->u.mgd.bssid))
		ieee80211_reset_ap_probe(sdata);
}

/*
 * This is the canonical list of information elements we care about,
 * the filter code also gives us all changes to the Microsoft OUI
 * (00:50:F2) vendor IE which is used for WMM which we need to track,
 * as well as the DTPC IE (part of the Cisco OUI) used for signaling
 * changes to requested client power.
 *
 * We implement beacon filtering in software since that means we can
 * avoid processing the frame here and in cfg80211, and userspace
 * will not be able to tell whether the hardware supports it or not.
 *
 * XXX: This list needs to be dynamic -- userspace needs to be able to
 *	add items it requires. It also needs to be able to tell us to
 *	look out for other vendor IEs.
 */
static const u64 care_about_ies =
	(1ULL << WLAN_EID_COUNTRY) |
	(1ULL << WLAN_EID_ERP_INFO) |
	(1ULL << WLAN_EID_CHANNEL_SWITCH) |
	(1ULL << WLAN_EID_PWR_CONSTRAINT) |
	(1ULL << WLAN_EID_HT_CAPABILITY) |
	(1ULL << WLAN_EID_HT_OPERATION) |
	(1ULL << WLAN_EID_EXT_CHANSWITCH_ANN);

static void ieee80211_handle_beacon_sig(struct ieee80211_link_data *link,
					struct ieee80211_if_managed *ifmgd,
					struct ieee80211_bss_conf *bss_conf,
					struct ieee80211_local *local,
					struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;

	/* Track average RSSI from the Beacon frames of the current AP */

	if (!link->u.mgd.tracking_signal_avg) {
		link->u.mgd.tracking_signal_avg = true;
		ewma_beacon_signal_init(&link->u.mgd.ave_beacon_signal);
		link->u.mgd.last_cqm_event_signal = 0;
		link->u.mgd.count_beacon_signal = 1;
		link->u.mgd.last_ave_beacon_signal = 0;
	} else {
		link->u.mgd.count_beacon_signal++;
	}

	ewma_beacon_signal_add(&link->u.mgd.ave_beacon_signal,
			       -rx_status->signal);

	if (ifmgd->rssi_min_thold != ifmgd->rssi_max_thold &&
	    link->u.mgd.count_beacon_signal >= IEEE80211_SIGNAL_AVE_MIN_COUNT) {
		int sig = -ewma_beacon_signal_read(&link->u.mgd.ave_beacon_signal);
		int last_sig = link->u.mgd.last_ave_beacon_signal;
		struct ieee80211_event event = {
			.type = RSSI_EVENT,
		};

		/*
		 * if signal crosses either of the boundaries, invoke callback
		 * with appropriate parameters
		 */
		if (sig > ifmgd->rssi_max_thold &&
		    (last_sig <= ifmgd->rssi_min_thold || last_sig == 0)) {
			link->u.mgd.last_ave_beacon_signal = sig;
			event.u.rssi.data = RSSI_EVENT_HIGH;
			drv_event_callback(local, sdata, &event);
		} else if (sig < ifmgd->rssi_min_thold &&
			   (last_sig >= ifmgd->rssi_max_thold ||
			   last_sig == 0)) {
			link->u.mgd.last_ave_beacon_signal = sig;
			event.u.rssi.data = RSSI_EVENT_LOW;
			drv_event_callback(local, sdata, &event);
		}
	}

	if (bss_conf->cqm_rssi_thold &&
	    link->u.mgd.count_beacon_signal >= IEEE80211_SIGNAL_AVE_MIN_COUNT &&
	    !(sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_CQM_RSSI)) {
		int sig = -ewma_beacon_signal_read(&link->u.mgd.ave_beacon_signal);
		int last_event = link->u.mgd.last_cqm_event_signal;
		int thold = bss_conf->cqm_rssi_thold;
		int hyst = bss_conf->cqm_rssi_hyst;

		if (sig < thold &&
		    (last_event == 0 || sig < last_event - hyst)) {
			link->u.mgd.last_cqm_event_signal = sig;
			ieee80211_cqm_rssi_notify(
				&sdata->vif,
				NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW,
				sig, GFP_KERNEL);
		} else if (sig > thold &&
			   (last_event == 0 || sig > last_event + hyst)) {
			link->u.mgd.last_cqm_event_signal = sig;
			ieee80211_cqm_rssi_notify(
				&sdata->vif,
				NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
				sig, GFP_KERNEL);
		}
	}

	if (bss_conf->cqm_rssi_low &&
	    link->u.mgd.count_beacon_signal >= IEEE80211_SIGNAL_AVE_MIN_COUNT) {
		int sig = -ewma_beacon_signal_read(&link->u.mgd.ave_beacon_signal);
		int last_event = link->u.mgd.last_cqm_event_signal;
		int low = bss_conf->cqm_rssi_low;
		int high = bss_conf->cqm_rssi_high;

		if (sig < low &&
		    (last_event == 0 || last_event >= low)) {
			link->u.mgd.last_cqm_event_signal = sig;
			ieee80211_cqm_rssi_notify(
				&sdata->vif,
				NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW,
				sig, GFP_KERNEL);
		} else if (sig > high &&
			   (last_event == 0 || last_event <= high)) {
			link->u.mgd.last_cqm_event_signal = sig;
			ieee80211_cqm_rssi_notify(
				&sdata->vif,
				NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
				sig, GFP_KERNEL);
		}
	}
}

static bool ieee80211_rx_our_beacon(const u8 *tx_bssid,
				    struct cfg80211_bss *bss)
{
	if (ether_addr_equal(tx_bssid, bss->bssid))
		return true;
	if (!bss->transmitted_bss)
		return false;
	return ether_addr_equal(tx_bssid, bss->transmitted_bss->bssid);
}

static void ieee80211_rx_mgmt_beacon(struct ieee80211_link_data *link,
				     struct ieee80211_hdr *hdr, size_t len,
				     struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_bss_conf *bss_conf = &sdata->vif.bss_conf;
	struct ieee80211_vif_cfg *vif_cfg = &sdata->vif.cfg;
	struct ieee80211_mgmt *mgmt = (void *) hdr;
	size_t baselen;
	struct ieee802_11_elems *elems;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_channel *chan;
	struct link_sta_info *link_sta;
	struct sta_info *sta;
	u32 changed = 0;
	bool erp_valid;
	u8 erp_value = 0;
	u32 ncrc = 0;
	u8 *bssid, *variable = mgmt->u.beacon.variable;
	u8 deauth_buf[IEEE80211_DEAUTH_FRAME_LEN];
	struct ieee80211_elems_parse_params parse_params = {
		.link_id = -1,
		.from_ap = true,
	};

	sdata_assert_lock(sdata);

	/* Process beacon from the current BSS */
	bssid = ieee80211_get_bssid(hdr, len, sdata->vif.type);
	if (ieee80211_is_s1g_beacon(mgmt->frame_control)) {
		struct ieee80211_ext *ext = (void *) mgmt;

		if (ieee80211_is_s1g_short_beacon(ext->frame_control))
			variable = ext->u.s1g_short_beacon.variable;
		else
			variable = ext->u.s1g_beacon.variable;
	}

	baselen = (u8 *) variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	parse_params.start = variable;
	parse_params.len = len - baselen;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(link->conf->chanctx_conf);
	if (!chanctx_conf) {
		rcu_read_unlock();
		return;
	}

	if (ieee80211_rx_status_to_khz(rx_status) !=
	    ieee80211_channel_to_khz(chanctx_conf->def.chan)) {
		rcu_read_unlock();
		return;
	}
	chan = chanctx_conf->def.chan;
	rcu_read_unlock();

	if (ifmgd->assoc_data && ifmgd->assoc_data->need_beacon &&
	    !WARN_ON(sdata->vif.valid_links) &&
	    ieee80211_rx_our_beacon(bssid, ifmgd->assoc_data->link[0].bss)) {
		parse_params.bss = ifmgd->assoc_data->link[0].bss;
		elems = ieee802_11_parse_elems_full(&parse_params);
		if (!elems)
			return;

		ieee80211_rx_bss_info(link, mgmt, len, rx_status);

		if (elems->dtim_period)
			link->u.mgd.dtim_period = elems->dtim_period;
		link->u.mgd.have_beacon = true;
		ifmgd->assoc_data->need_beacon = false;
		if (ieee80211_hw_check(&local->hw, TIMING_BEACON_ONLY)) {
			link->conf->sync_tsf =
				le64_to_cpu(mgmt->u.beacon.timestamp);
			link->conf->sync_device_ts =
				rx_status->device_timestamp;
			link->conf->sync_dtim_count = elems->dtim_count;
		}

		if (elems->mbssid_config_ie)
			bss_conf->profile_periodicity =
				elems->mbssid_config_ie->profile_periodicity;
		else
			bss_conf->profile_periodicity = 0;

		if (elems->ext_capab_len >= 11 &&
		    (elems->ext_capab[10] & WLAN_EXT_CAPA11_EMA_SUPPORT))
			bss_conf->ema_ap = true;
		else
			bss_conf->ema_ap = false;

		/* continue assoc process */
		ifmgd->assoc_data->timeout = jiffies;
		ifmgd->assoc_data->timeout_started = true;
		run_again(sdata, ifmgd->assoc_data->timeout);
		kfree(elems);
		return;
	}

	if (!ifmgd->associated ||
	    !ieee80211_rx_our_beacon(bssid, link->u.mgd.bss))
		return;
	bssid = link->u.mgd.bssid;

	if (!(rx_status->flag & RX_FLAG_NO_SIGNAL_VAL))
		ieee80211_handle_beacon_sig(link, ifmgd, bss_conf,
					    local, rx_status);

	if (ifmgd->flags & IEEE80211_STA_CONNECTION_POLL) {
		mlme_dbg_ratelimited(sdata,
				     "cancelling AP probe due to a received beacon\n");
		ieee80211_reset_ap_probe(sdata);
	}

	/*
	 * Push the beacon loss detection into the future since
	 * we are processing a beacon from the AP just now.
	 */
	ieee80211_sta_reset_beacon_monitor(sdata);

	/* TODO: CRC urrently not calculated on S1G Beacon Compatibility
	 * element (which carries the beacon interval). Don't forget to add a
	 * bit to care_about_ies[] above if mac80211 is interested in a
	 * changing S1G element.
	 */
	if (!ieee80211_is_s1g_beacon(hdr->frame_control))
		ncrc = crc32_be(0, (void *)&mgmt->u.beacon.beacon_int, 4);
	parse_params.bss = link->u.mgd.bss;
	parse_params.filter = care_about_ies;
	parse_params.crc = ncrc;
	elems = ieee802_11_parse_elems_full(&parse_params);
	if (!elems)
		return;
	ncrc = elems->crc;

	if (ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK) &&
	    ieee80211_check_tim(elems->tim, elems->tim_len, vif_cfg->aid)) {
		if (local->hw.conf.dynamic_ps_timeout > 0) {
			if (local->hw.conf.flags & IEEE80211_CONF_PS) {
				local->hw.conf.flags &= ~IEEE80211_CONF_PS;
				ieee80211_hw_config(local,
						    IEEE80211_CONF_CHANGE_PS);
			}
			ieee80211_send_nullfunc(local, sdata, false);
		} else if (!local->pspolling && sdata->u.mgd.powersave) {
			local->pspolling = true;

			/*
			 * Here is assumed that the driver will be
			 * able to send ps-poll frame and receive a
			 * response even though power save mode is
			 * enabled, but some drivers might require
			 * to disable power save here. This needs
			 * to be investigated.
			 */
			ieee80211_send_pspoll(local, sdata);
		}
	}

	if (sdata->vif.p2p ||
	    sdata->vif.driver_flags & IEEE80211_VIF_GET_NOA_UPDATE) {
		struct ieee80211_p2p_noa_attr noa = {};
		int ret;

		ret = cfg80211_get_p2p_attr(variable,
					    len - baselen,
					    IEEE80211_P2P_ATTR_ABSENCE_NOTICE,
					    (u8 *) &noa, sizeof(noa));
		if (ret >= 2) {
			if (link->u.mgd.p2p_noa_index != noa.index) {
				/* valid noa_attr and index changed */
				link->u.mgd.p2p_noa_index = noa.index;
				memcpy(&bss_conf->p2p_noa_attr, &noa, sizeof(noa));
				changed |= BSS_CHANGED_P2P_PS;
				/*
				 * make sure we update all information, the CRC
				 * mechanism doesn't look at P2P attributes.
				 */
				link->u.mgd.beacon_crc_valid = false;
			}
		} else if (link->u.mgd.p2p_noa_index != -1) {
			/* noa_attr not found and we had valid noa_attr before */
			link->u.mgd.p2p_noa_index = -1;
			memset(&bss_conf->p2p_noa_attr, 0, sizeof(bss_conf->p2p_noa_attr));
			changed |= BSS_CHANGED_P2P_PS;
			link->u.mgd.beacon_crc_valid = false;
		}
	}

	if (link->u.mgd.csa_waiting_bcn)
		ieee80211_chswitch_post_beacon(link);

	/*
	 * Update beacon timing and dtim count on every beacon appearance. This
	 * will allow the driver to use the most updated values. Do it before
	 * comparing this one with last received beacon.
	 * IMPORTANT: These parameters would possibly be out of sync by the time
	 * the driver will use them. The synchronized view is currently
	 * guaranteed only in certain callbacks.
	 */
	if (ieee80211_hw_check(&local->hw, TIMING_BEACON_ONLY) &&
	    !ieee80211_is_s1g_beacon(hdr->frame_control)) {
		link->conf->sync_tsf =
			le64_to_cpu(mgmt->u.beacon.timestamp);
		link->conf->sync_device_ts =
			rx_status->device_timestamp;
		link->conf->sync_dtim_count = elems->dtim_count;
	}

	if ((ncrc == link->u.mgd.beacon_crc && link->u.mgd.beacon_crc_valid) ||
	    ieee80211_is_s1g_short_beacon(mgmt->frame_control))
		goto free;
	link->u.mgd.beacon_crc = ncrc;
	link->u.mgd.beacon_crc_valid = true;

	ieee80211_rx_bss_info(link, mgmt, len, rx_status);

	ieee80211_sta_process_chanswitch(link, rx_status->mactime,
					 rx_status->device_timestamp,
					 elems, true);

	if (!link->u.mgd.disable_wmm_tracking &&
	    ieee80211_sta_wmm_params(local, link, elems->wmm_param,
				     elems->wmm_param_len,
				     elems->mu_edca_param_set))
		changed |= BSS_CHANGED_QOS;

	/*
	 * If we haven't had a beacon before, tell the driver about the
	 * DTIM period (and beacon timing if desired) now.
	 */
	if (!link->u.mgd.have_beacon) {
		/* a few bogus AP send dtim_period = 0 or no TIM IE */
		bss_conf->dtim_period = elems->dtim_period ?: 1;

		changed |= BSS_CHANGED_BEACON_INFO;
		link->u.mgd.have_beacon = true;

		mutex_lock(&local->iflist_mtx);
		ieee80211_recalc_ps(local);
		mutex_unlock(&local->iflist_mtx);

		ieee80211_recalc_ps_vif(sdata);
	}

	if (elems->erp_info) {
		erp_valid = true;
		erp_value = elems->erp_info[0];
	} else {
		erp_valid = false;
	}

	if (!ieee80211_is_s1g_beacon(hdr->frame_control))
		changed |= ieee80211_handle_bss_capability(link,
				le16_to_cpu(mgmt->u.beacon.capab_info),
				erp_valid, erp_value);

	mutex_lock(&local->sta_mtx);
	sta = sta_info_get(sdata, sdata->vif.cfg.ap_addr);
	if (WARN_ON(!sta)) {
		mutex_unlock(&local->sta_mtx);
		goto free;
	}
	link_sta = rcu_dereference_protected(sta->link[link->link_id],
					     lockdep_is_held(&local->sta_mtx));
	if (WARN_ON(!link_sta)) {
		mutex_unlock(&local->sta_mtx);
		goto free;
	}

	changed |= ieee80211_recalc_twt_req(link, link_sta, elems);

	if (ieee80211_config_bw(link, elems->ht_cap_elem,
				elems->vht_cap_elem, elems->ht_operation,
				elems->vht_operation, elems->he_operation,
				elems->eht_operation,
				elems->s1g_oper, bssid, &changed)) {
		mutex_unlock(&local->sta_mtx);
		sdata_info(sdata,
			   "failed to follow AP %pM bandwidth change, disconnect\n",
			   bssid);
		ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DEAUTH,
				       WLAN_REASON_DEAUTH_LEAVING,
				       true, deauth_buf);
		ieee80211_report_disconnect(sdata, deauth_buf,
					    sizeof(deauth_buf), true,
					    WLAN_REASON_DEAUTH_LEAVING,
					    false);
		goto free;
	}

	if (sta && elems->opmode_notif)
		ieee80211_vht_handle_opmode(sdata, link_sta,
					    *elems->opmode_notif,
					    rx_status->band);
	mutex_unlock(&local->sta_mtx);

	changed |= ieee80211_handle_pwr_constr(link, chan, mgmt,
					       elems->country_elem,
					       elems->country_elem_len,
					       elems->pwr_constr_elem,
					       elems->cisco_dtpc_elem);

	ieee80211_link_info_change_notify(sdata, link, changed);
free:
	kfree(elems);
}

void ieee80211_sta_rx_queued_ext(struct ieee80211_sub_if_data *sdata,
				 struct sk_buff *skb)
{
	struct ieee80211_link_data *link = &sdata->deflink;
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_hdr *hdr;
	u16 fc;

	rx_status = (struct ieee80211_rx_status *) skb->cb;
	hdr = (struct ieee80211_hdr *) skb->data;
	fc = le16_to_cpu(hdr->frame_control);

	sdata_lock(sdata);
	switch (fc & IEEE80211_FCTL_STYPE) {
	case IEEE80211_STYPE_S1G_BEACON:
		ieee80211_rx_mgmt_beacon(link, hdr, skb->len, rx_status);
		break;
	}
	sdata_unlock(sdata);
}

void ieee80211_sta_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				  struct sk_buff *skb)
{
	struct ieee80211_link_data *link = &sdata->deflink;
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_mgmt *mgmt;
	u16 fc;
	int ies_len;

	rx_status = (struct ieee80211_rx_status *) skb->cb;
	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	sdata_lock(sdata);

	if (rx_status->link_valid) {
		link = sdata_dereference(sdata->link[rx_status->link_id],
					 sdata);
		if (!link)
			goto out;
	}

	switch (fc & IEEE80211_FCTL_STYPE) {
	case IEEE80211_STYPE_BEACON:
		ieee80211_rx_mgmt_beacon(link, (void *)mgmt,
					 skb->len, rx_status);
		break;
	case IEEE80211_STYPE_PROBE_RESP:
		ieee80211_rx_mgmt_probe_resp(link, skb);
		break;
	case IEEE80211_STYPE_AUTH:
		ieee80211_rx_mgmt_auth(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_DEAUTH:
		ieee80211_rx_mgmt_deauth(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_DISASSOC:
		ieee80211_rx_mgmt_disassoc(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_ASSOC_RESP:
	case IEEE80211_STYPE_REASSOC_RESP:
		ieee80211_rx_mgmt_assoc_resp(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_ACTION:
		if (mgmt->u.action.category == WLAN_CATEGORY_SPECTRUM_MGMT) {
			struct ieee802_11_elems *elems;

			ies_len = skb->len -
				  offsetof(struct ieee80211_mgmt,
					   u.action.u.chan_switch.variable);

			if (ies_len < 0)
				break;

			/* CSA IE cannot be overridden, no need for BSSID */
			elems = ieee802_11_parse_elems(
					mgmt->u.action.u.chan_switch.variable,
					ies_len, true, NULL);

			if (elems && !elems->parse_error)
				ieee80211_sta_process_chanswitch(link,
								 rx_status->mactime,
								 rx_status->device_timestamp,
								 elems, false);
			kfree(elems);
		} else if (mgmt->u.action.category == WLAN_CATEGORY_PUBLIC) {
			struct ieee802_11_elems *elems;

			ies_len = skb->len -
				  offsetof(struct ieee80211_mgmt,
					   u.action.u.ext_chan_switch.variable);

			if (ies_len < 0)
				break;

			/*
			 * extended CSA IE can't be overridden, no need for
			 * BSSID
			 */
			elems = ieee802_11_parse_elems(
					mgmt->u.action.u.ext_chan_switch.variable,
					ies_len, true, NULL);

			if (elems && !elems->parse_error) {
				/* for the handling code pretend it was an IE */
				elems->ext_chansw_ie =
					&mgmt->u.action.u.ext_chan_switch.data;

				ieee80211_sta_process_chanswitch(link,
								 rx_status->mactime,
								 rx_status->device_timestamp,
								 elems, false);
			}

			kfree(elems);
		}
		break;
	}
out:
	sdata_unlock(sdata);
}

static void ieee80211_sta_timer(struct timer_list *t)
{
	struct ieee80211_sub_if_data *sdata =
		from_timer(sdata, t, u.mgd.timer);

	ieee80211_queue_work(&sdata->local->hw, &sdata->work);
}

void ieee80211_sta_connection_lost(struct ieee80211_sub_if_data *sdata,
				   u8 reason, bool tx)
{
	u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];

	ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DEAUTH, reason,
			       tx, frame_buf);

	ieee80211_report_disconnect(sdata, frame_buf, sizeof(frame_buf), true,
				    reason, false);
}

static int ieee80211_auth(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_auth_data *auth_data = ifmgd->auth_data;
	u32 tx_flags = 0;
	u16 trans = 1;
	u16 status = 0;
	struct ieee80211_prep_tx_info info = {
		.subtype = IEEE80211_STYPE_AUTH,
	};

	sdata_assert_lock(sdata);

	if (WARN_ON_ONCE(!auth_data))
		return -EINVAL;

	auth_data->tries++;

	if (auth_data->tries > IEEE80211_AUTH_MAX_TRIES) {
		sdata_info(sdata, "authentication with %pM timed out\n",
			   auth_data->ap_addr);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss struct for that AP.
		 */
		cfg80211_unlink_bss(local->hw.wiphy, auth_data->bss);

		return -ETIMEDOUT;
	}

	if (auth_data->algorithm == WLAN_AUTH_SAE)
		info.duration = jiffies_to_msecs(IEEE80211_AUTH_TIMEOUT_SAE);

	drv_mgd_prepare_tx(local, sdata, &info);

	sdata_info(sdata, "send auth to %pM (try %d/%d)\n",
		   auth_data->ap_addr, auth_data->tries,
		   IEEE80211_AUTH_MAX_TRIES);

	auth_data->expected_transaction = 2;

	if (auth_data->algorithm == WLAN_AUTH_SAE) {
		trans = auth_data->sae_trans;
		status = auth_data->sae_status;
		auth_data->expected_transaction = trans;
	}

	if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
		tx_flags = IEEE80211_TX_CTL_REQ_TX_STATUS |
			   IEEE80211_TX_INTFL_MLME_CONN_TX;

	ieee80211_send_auth(sdata, trans, auth_data->algorithm, status,
			    auth_data->data, auth_data->data_len,
			    auth_data->ap_addr, auth_data->ap_addr,
			    NULL, 0, 0, tx_flags);

	if (tx_flags == 0) {
		if (auth_data->algorithm == WLAN_AUTH_SAE)
			auth_data->timeout = jiffies +
				IEEE80211_AUTH_TIMEOUT_SAE;
		else
			auth_data->timeout = jiffies + IEEE80211_AUTH_TIMEOUT;
	} else {
		auth_data->timeout =
			round_jiffies_up(jiffies + IEEE80211_AUTH_TIMEOUT_LONG);
	}

	auth_data->timeout_started = true;
	run_again(sdata, auth_data->timeout);

	return 0;
}

static int ieee80211_do_assoc(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_mgd_assoc_data *assoc_data = sdata->u.mgd.assoc_data;
	struct ieee80211_local *local = sdata->local;
	int ret;

	sdata_assert_lock(sdata);

	assoc_data->tries++;
	if (assoc_data->tries > IEEE80211_ASSOC_MAX_TRIES) {
		sdata_info(sdata, "association with %pM timed out\n",
			   assoc_data->ap_addr);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss struct for that AP.
		 */
		cfg80211_unlink_bss(local->hw.wiphy,
				    assoc_data->link[assoc_data->assoc_link_id].bss);

		return -ETIMEDOUT;
	}

	sdata_info(sdata, "associate with %pM (try %d/%d)\n",
		   assoc_data->ap_addr, assoc_data->tries,
		   IEEE80211_ASSOC_MAX_TRIES);
	ret = ieee80211_send_assoc(sdata);
	if (ret)
		return ret;

	if (!ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS)) {
		assoc_data->timeout = jiffies + IEEE80211_ASSOC_TIMEOUT;
		assoc_data->timeout_started = true;
		run_again(sdata, assoc_data->timeout);
	} else {
		assoc_data->timeout =
			round_jiffies_up(jiffies +
					 IEEE80211_ASSOC_TIMEOUT_LONG);
		assoc_data->timeout_started = true;
		run_again(sdata, assoc_data->timeout);
	}

	return 0;
}

void ieee80211_mgd_conn_tx_status(struct ieee80211_sub_if_data *sdata,
				  __le16 fc, bool acked)
{
	struct ieee80211_local *local = sdata->local;

	sdata->u.mgd.status_fc = fc;
	sdata->u.mgd.status_acked = acked;
	sdata->u.mgd.status_received = true;

	ieee80211_queue_work(&local->hw, &sdata->work);
}

void ieee80211_sta_work(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	sdata_lock(sdata);

	if (ifmgd->status_received) {
		__le16 fc = ifmgd->status_fc;
		bool status_acked = ifmgd->status_acked;

		ifmgd->status_received = false;
		if (ifmgd->auth_data && ieee80211_is_auth(fc)) {
			if (status_acked) {
				if (ifmgd->auth_data->algorithm ==
				    WLAN_AUTH_SAE)
					ifmgd->auth_data->timeout =
						jiffies +
						IEEE80211_AUTH_TIMEOUT_SAE;
				else
					ifmgd->auth_data->timeout =
						jiffies +
						IEEE80211_AUTH_TIMEOUT_SHORT;
				run_again(sdata, ifmgd->auth_data->timeout);
			} else {
				ifmgd->auth_data->timeout = jiffies - 1;
			}
			ifmgd->auth_data->timeout_started = true;
		} else if (ifmgd->assoc_data &&
			   (ieee80211_is_assoc_req(fc) ||
			    ieee80211_is_reassoc_req(fc))) {
			if (status_acked) {
				ifmgd->assoc_data->timeout =
					jiffies + IEEE80211_ASSOC_TIMEOUT_SHORT;
				run_again(sdata, ifmgd->assoc_data->timeout);
			} else {
				ifmgd->assoc_data->timeout = jiffies - 1;
			}
			ifmgd->assoc_data->timeout_started = true;
		}
	}

	if (ifmgd->auth_data && ifmgd->auth_data->timeout_started &&
	    time_after(jiffies, ifmgd->auth_data->timeout)) {
		if (ifmgd->auth_data->done || ifmgd->auth_data->waiting) {
			/*
			 * ok ... we waited for assoc or continuation but
			 * userspace didn't do it, so kill the auth data
			 */
			ieee80211_destroy_auth_data(sdata, false);
		} else if (ieee80211_auth(sdata)) {
			u8 ap_addr[ETH_ALEN];
			struct ieee80211_event event = {
				.type = MLME_EVENT,
				.u.mlme.data = AUTH_EVENT,
				.u.mlme.status = MLME_TIMEOUT,
			};

			memcpy(ap_addr, ifmgd->auth_data->ap_addr, ETH_ALEN);

			ieee80211_destroy_auth_data(sdata, false);

			cfg80211_auth_timeout(sdata->dev, ap_addr);
			drv_event_callback(sdata->local, sdata, &event);
		}
	} else if (ifmgd->auth_data && ifmgd->auth_data->timeout_started)
		run_again(sdata, ifmgd->auth_data->timeout);

	if (ifmgd->assoc_data && ifmgd->assoc_data->timeout_started &&
	    time_after(jiffies, ifmgd->assoc_data->timeout)) {
		if ((ifmgd->assoc_data->need_beacon &&
		     !sdata->deflink.u.mgd.have_beacon) ||
		    ieee80211_do_assoc(sdata)) {
			struct ieee80211_event event = {
				.type = MLME_EVENT,
				.u.mlme.data = ASSOC_EVENT,
				.u.mlme.status = MLME_TIMEOUT,
			};

			ieee80211_destroy_assoc_data(sdata, ASSOC_TIMEOUT);
			drv_event_callback(sdata->local, sdata, &event);
		}
	} else if (ifmgd->assoc_data && ifmgd->assoc_data->timeout_started)
		run_again(sdata, ifmgd->assoc_data->timeout);

	if (ifmgd->flags & IEEE80211_STA_CONNECTION_POLL &&
	    ifmgd->associated) {
		u8 *bssid = sdata->deflink.u.mgd.bssid;
		int max_tries;

		if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS))
			max_tries = max_nullfunc_tries;
		else
			max_tries = max_probe_tries;

		/* ACK received for nullfunc probing frame */
		if (!ifmgd->probe_send_count)
			ieee80211_reset_ap_probe(sdata);
		else if (ifmgd->nullfunc_failed) {
			if (ifmgd->probe_send_count < max_tries) {
				mlme_dbg(sdata,
					 "No ack for nullfunc frame to AP %pM, try %d/%i\n",
					 bssid, ifmgd->probe_send_count,
					 max_tries);
				ieee80211_mgd_probe_ap_send(sdata);
			} else {
				mlme_dbg(sdata,
					 "No ack for nullfunc frame to AP %pM, disconnecting.\n",
					 bssid);
				ieee80211_sta_connection_lost(sdata,
					WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY,
					false);
			}
		} else if (time_is_after_jiffies(ifmgd->probe_timeout))
			run_again(sdata, ifmgd->probe_timeout);
		else if (ieee80211_hw_check(&local->hw, REPORTS_TX_ACK_STATUS)) {
			mlme_dbg(sdata,
				 "Failed to send nullfunc to AP %pM after %dms, disconnecting\n",
				 bssid, probe_wait_ms);
			ieee80211_sta_connection_lost(sdata,
				WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY, false);
		} else if (ifmgd->probe_send_count < max_tries) {
			mlme_dbg(sdata,
				 "No probe response from AP %pM after %dms, try %d/%i\n",
				 bssid, probe_wait_ms,
				 ifmgd->probe_send_count, max_tries);
			ieee80211_mgd_probe_ap_send(sdata);
		} else {
			/*
			 * We actually lost the connection ... or did we?
			 * Let's make sure!
			 */
			mlme_dbg(sdata,
				 "No probe response from AP %pM after %dms, disconnecting.\n",
				 bssid, probe_wait_ms);

			ieee80211_sta_connection_lost(sdata,
				WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY, false);
		}
	}

	sdata_unlock(sdata);
}

static void ieee80211_sta_bcn_mon_timer(struct timer_list *t)
{
	struct ieee80211_sub_if_data *sdata =
		from_timer(sdata, t, u.mgd.bcn_mon_timer);

	if (WARN_ON(sdata->vif.valid_links))
		return;

	if (sdata->vif.bss_conf.csa_active &&
	    !sdata->deflink.u.mgd.csa_waiting_bcn)
		return;

	if (sdata->vif.driver_flags & IEEE80211_VIF_BEACON_FILTER)
		return;

	sdata->u.mgd.connection_loss = false;
	ieee80211_queue_work(&sdata->local->hw,
			     &sdata->u.mgd.beacon_connection_loss_work);
}

static void ieee80211_sta_conn_mon_timer(struct timer_list *t)
{
	struct ieee80211_sub_if_data *sdata =
		from_timer(sdata, t, u.mgd.conn_mon_timer);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	unsigned long timeout;

	if (WARN_ON(sdata->vif.valid_links))
		return;

	if (sdata->vif.bss_conf.csa_active &&
	    !sdata->deflink.u.mgd.csa_waiting_bcn)
		return;

	sta = sta_info_get(sdata, sdata->vif.cfg.ap_addr);
	if (!sta)
		return;

	timeout = sta->deflink.status_stats.last_ack;
	if (time_before(sta->deflink.status_stats.last_ack, sta->deflink.rx_stats.last_rx))
		timeout = sta->deflink.rx_stats.last_rx;
	timeout += IEEE80211_CONNECTION_IDLE_TIME;

	/* If timeout is after now, then update timer to fire at
	 * the later date, but do not actually probe at this time.
	 */
	if (time_is_after_jiffies(timeout)) {
		mod_timer(&ifmgd->conn_mon_timer, round_jiffies_up(timeout));
		return;
	}

	ieee80211_queue_work(&local->hw, &ifmgd->monitor_work);
}

static void ieee80211_sta_monitor_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data,
			     u.mgd.monitor_work);

	ieee80211_mgd_probe_ap(sdata, false);
}

static void ieee80211_restart_sta_timer(struct ieee80211_sub_if_data *sdata)
{
	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		__ieee80211_stop_poll(sdata);

		/* let's probe the connection once */
		if (!ieee80211_hw_check(&sdata->local->hw, CONNECTION_MONITOR))
			ieee80211_queue_work(&sdata->local->hw,
					     &sdata->u.mgd.monitor_work);
	}
}

#ifdef CONFIG_PM
void ieee80211_mgd_quiesce(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];

	sdata_lock(sdata);

	if (ifmgd->auth_data || ifmgd->assoc_data) {
		const u8 *ap_addr = ifmgd->auth_data ?
				ifmgd->auth_data->ap_addr :
				ifmgd->assoc_data->ap_addr;

		/*
		 * If we are trying to authenticate / associate while suspending,
		 * cfg80211 won't know and won't actually abort those attempts,
		 * thus we need to do that ourselves.
		 */
		ieee80211_send_deauth_disassoc(sdata, ap_addr, ap_addr,
					       IEEE80211_STYPE_DEAUTH,
					       WLAN_REASON_DEAUTH_LEAVING,
					       false, frame_buf);
		if (ifmgd->assoc_data)
			ieee80211_destroy_assoc_data(sdata, ASSOC_ABANDON);
		if (ifmgd->auth_data)
			ieee80211_destroy_auth_data(sdata, false);
		cfg80211_tx_mlme_mgmt(sdata->dev, frame_buf,
				      IEEE80211_DEAUTH_FRAME_LEN,
				      false);
	}

	/* This is a bit of a hack - we should find a better and more generic
	 * solution to this. Normally when suspending, cfg80211 will in fact
	 * deauthenticate. However, it doesn't (and cannot) stop an ongoing
	 * auth (not so important) or assoc (this is the problem) process.
	 *
	 * As a consequence, it can happen that we are in the process of both
	 * associating and suspending, and receive an association response
	 * after cfg80211 has checked if it needs to disconnect, but before
	 * we actually set the flag to drop incoming frames. This will then
	 * cause the workqueue flush to process the association response in
	 * the suspend, resulting in a successful association just before it
	 * tries to remove the interface from the driver, which now though
	 * has a channel context assigned ... this results in issues.
	 *
	 * To work around this (for now) simply deauth here again if we're
	 * now connected.
	 */
	if (ifmgd->associated && !sdata->local->wowlan) {
		u8 bssid[ETH_ALEN];
		struct cfg80211_deauth_request req = {
			.reason_code = WLAN_REASON_DEAUTH_LEAVING,
			.bssid = bssid,
		};

		memcpy(bssid, sdata->vif.cfg.ap_addr, ETH_ALEN);
		ieee80211_mgd_deauth(sdata, &req);
	}

	sdata_unlock(sdata);
}
#endif

void ieee80211_sta_restart(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	sdata_lock(sdata);
	if (!ifmgd->associated) {
		sdata_unlock(sdata);
		return;
	}

	if (sdata->flags & IEEE80211_SDATA_DISCONNECT_RESUME) {
		sdata->flags &= ~IEEE80211_SDATA_DISCONNECT_RESUME;
		mlme_dbg(sdata, "driver requested disconnect after resume\n");
		ieee80211_sta_connection_lost(sdata,
					      WLAN_REASON_UNSPECIFIED,
					      true);
		sdata_unlock(sdata);
		return;
	}

	if (sdata->flags & IEEE80211_SDATA_DISCONNECT_HW_RESTART) {
		sdata->flags &= ~IEEE80211_SDATA_DISCONNECT_HW_RESTART;
		mlme_dbg(sdata, "driver requested disconnect after hardware restart\n");
		ieee80211_sta_connection_lost(sdata,
					      WLAN_REASON_UNSPECIFIED,
					      true);
		sdata_unlock(sdata);
		return;
	}

	sdata_unlock(sdata);
}

static void ieee80211_request_smps_mgd_work(struct work_struct *work)
{
	struct ieee80211_link_data *link =
		container_of(work, struct ieee80211_link_data,
			     u.mgd.request_smps_work);

	sdata_lock(link->sdata);
	__ieee80211_request_smps_mgd(link->sdata, link,
				     link->u.mgd.driver_smps_mode);
	sdata_unlock(link->sdata);
}

/* interface setup */
void ieee80211_sta_setup_sdata(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	INIT_WORK(&ifmgd->monitor_work, ieee80211_sta_monitor_work);
	INIT_WORK(&ifmgd->beacon_connection_loss_work,
		  ieee80211_beacon_connection_loss_work);
	INIT_WORK(&ifmgd->csa_connection_drop_work,
		  ieee80211_csa_connection_drop_work);
	INIT_DELAYED_WORK(&ifmgd->tdls_peer_del_work,
			  ieee80211_tdls_peer_del_work);
	timer_setup(&ifmgd->timer, ieee80211_sta_timer, 0);
	timer_setup(&ifmgd->bcn_mon_timer, ieee80211_sta_bcn_mon_timer, 0);
	timer_setup(&ifmgd->conn_mon_timer, ieee80211_sta_conn_mon_timer, 0);
	INIT_DELAYED_WORK(&ifmgd->tx_tspec_wk,
			  ieee80211_sta_handle_tspec_ac_params_wk);

	ifmgd->flags = 0;
	ifmgd->powersave = sdata->wdev.ps;
	ifmgd->uapsd_queues = sdata->local->hw.uapsd_queues;
	ifmgd->uapsd_max_sp_len = sdata->local->hw.uapsd_max_sp_len;
	/* Setup TDLS data */
	spin_lock_init(&ifmgd->teardown_lock);
	ifmgd->teardown_skb = NULL;
	ifmgd->orig_teardown_skb = NULL;
}

void ieee80211_mgd_setup_link(struct ieee80211_link_data *link)
{
	struct ieee80211_sub_if_data *sdata = link->sdata;
	struct ieee80211_local *local = sdata->local;
	unsigned int link_id = link->link_id;

	link->u.mgd.p2p_noa_index = -1;
	link->u.mgd.conn_flags = 0;
	link->conf->bssid = link->u.mgd.bssid;

	INIT_WORK(&link->u.mgd.request_smps_work,
		  ieee80211_request_smps_mgd_work);
	if (local->hw.wiphy->features & NL80211_FEATURE_DYNAMIC_SMPS)
		link->u.mgd.req_smps = IEEE80211_SMPS_AUTOMATIC;
	else
		link->u.mgd.req_smps = IEEE80211_SMPS_OFF;

	INIT_WORK(&link->u.mgd.chswitch_work, ieee80211_chswitch_work);
	timer_setup(&link->u.mgd.chswitch_timer, ieee80211_chswitch_timer, 0);

	if (sdata->u.mgd.assoc_data)
		ether_addr_copy(link->conf->addr,
				sdata->u.mgd.assoc_data->link[link_id].addr);
	else if (!is_valid_ether_addr(link->conf->addr))
		eth_random_addr(link->conf->addr);
}

/* scan finished notification */
void ieee80211_mlme_notify_scan_completed(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	/* Restart STA timers */
	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		if (ieee80211_sdata_running(sdata))
			ieee80211_restart_sta_timer(sdata);
	}
	rcu_read_unlock();
}

static int ieee80211_prep_connection(struct ieee80211_sub_if_data *sdata,
				     struct cfg80211_bss *cbss, s8 link_id,
				     const u8 *ap_mld_addr, bool assoc,
				     bool override)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_bss *bss = (void *)cbss->priv;
	struct sta_info *new_sta = NULL;
	struct ieee80211_link_data *link;
	bool have_sta = false;
	bool mlo;
	int err;

	if (link_id >= 0) {
		mlo = true;
		if (WARN_ON(!ap_mld_addr))
			return -EINVAL;
		err = ieee80211_vif_set_links(sdata, BIT(link_id));
	} else {
		if (WARN_ON(ap_mld_addr))
			return -EINVAL;
		ap_mld_addr = cbss->bssid;
		err = ieee80211_vif_set_links(sdata, 0);
		link_id = 0;
		mlo = false;
	}

	if (err)
		return err;

	link = sdata_dereference(sdata->link[link_id], sdata);
	if (WARN_ON(!link)) {
		err = -ENOLINK;
		goto out_err;
	}

	if (WARN_ON(!ifmgd->auth_data && !ifmgd->assoc_data)) {
		err = -EINVAL;
		goto out_err;
	}

	/* If a reconfig is happening, bail out */
	if (local->in_reconfig) {
		err = -EBUSY;
		goto out_err;
	}

	if (assoc) {
		rcu_read_lock();
		have_sta = sta_info_get(sdata, ap_mld_addr);
		rcu_read_unlock();
	}

	if (!have_sta) {
		if (mlo)
			new_sta = sta_info_alloc_with_link(sdata, ap_mld_addr,
							   link_id, cbss->bssid,
							   GFP_KERNEL);
		else
			new_sta = sta_info_alloc(sdata, ap_mld_addr, GFP_KERNEL);

		if (!new_sta) {
			err = -ENOMEM;
			goto out_err;
		}

		new_sta->sta.mlo = mlo;
	}

	/*
	 * Set up the information for the new channel before setting the
	 * new channel. We can't - completely race-free - change the basic
	 * rates bitmap and the channel (sband) that it refers to, but if
	 * we set it up before we at least avoid calling into the driver's
	 * bss_info_changed() method with invalid information (since we do
	 * call that from changing the channel - only for IDLE and perhaps
	 * some others, but ...).
	 *
	 * So to avoid that, just set up all the new information before the
	 * channel, but tell the driver to apply it only afterwards, since
	 * it might need the new channel for that.
	 */
	if (new_sta) {
		const struct cfg80211_bss_ies *ies;
		struct link_sta_info *link_sta;

		rcu_read_lock();
		link_sta = rcu_dereference(new_sta->link[link_id]);
		if (WARN_ON(!link_sta)) {
			rcu_read_unlock();
			sta_info_free(local, new_sta);
			err = -EINVAL;
			goto out_err;
		}

		err = ieee80211_mgd_setup_link_sta(link, new_sta,
						   link_sta, cbss);
		if (err) {
			rcu_read_unlock();
			sta_info_free(local, new_sta);
			goto out_err;
		}

		memcpy(link->u.mgd.bssid, cbss->bssid, ETH_ALEN);

		/* set timing information */
		link->conf->beacon_int = cbss->beacon_interval;
		ies = rcu_dereference(cbss->beacon_ies);
		if (ies) {
			link->conf->sync_tsf = ies->tsf;
			link->conf->sync_device_ts =
				bss->device_ts_beacon;

			ieee80211_get_dtim(ies,
					   &link->conf->sync_dtim_count,
					   NULL);
		} else if (!ieee80211_hw_check(&sdata->local->hw,
					       TIMING_BEACON_ONLY)) {
			ies = rcu_dereference(cbss->proberesp_ies);
			/* must be non-NULL since beacon IEs were NULL */
			link->conf->sync_tsf = ies->tsf;
			link->conf->sync_device_ts =
				bss->device_ts_presp;
			link->conf->sync_dtim_count = 0;
		} else {
			link->conf->sync_tsf = 0;
			link->conf->sync_device_ts = 0;
			link->conf->sync_dtim_count = 0;
		}
		rcu_read_unlock();
	}

	if (new_sta || override) {
		err = ieee80211_prep_channel(sdata, link, cbss,
					     &link->u.mgd.conn_flags);
		if (err) {
			if (new_sta)
				sta_info_free(local, new_sta);
			goto out_err;
		}
	}

	if (new_sta) {
		/*
		 * tell driver about BSSID, basic rates and timing
		 * this was set up above, before setting the channel
		 */
		ieee80211_link_info_change_notify(sdata, link,
						  BSS_CHANGED_BSSID |
						  BSS_CHANGED_BASIC_RATES |
						  BSS_CHANGED_BEACON_INT);

		if (assoc)
			sta_info_pre_move_state(new_sta, IEEE80211_STA_AUTH);

		err = sta_info_insert(new_sta);
		new_sta = NULL;
		if (err) {
			sdata_info(sdata,
				   "failed to insert STA entry for the AP (error %d)\n",
				   err);
			goto out_err;
		}
	} else
		WARN_ON_ONCE(!ether_addr_equal(link->u.mgd.bssid, cbss->bssid));

	/* Cancel scan to ensure that nothing interferes with connection */
	if (local->scanning)
		ieee80211_scan_cancel(local);

	return 0;

out_err:
	ieee80211_link_release_channel(&sdata->deflink);
	ieee80211_vif_set_links(sdata, 0);
	return err;
}

/* config hooks */
int ieee80211_mgd_auth(struct ieee80211_sub_if_data *sdata,
		       struct cfg80211_auth_request *req)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_auth_data *auth_data;
	u16 auth_alg;
	int err;
	bool cont_auth;

	/* prepare auth data structure */

	switch (req->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		auth_alg = WLAN_AUTH_OPEN;
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		if (fips_enabled)
			return -EOPNOTSUPP;
		auth_alg = WLAN_AUTH_SHARED_KEY;
		break;
	case NL80211_AUTHTYPE_FT:
		auth_alg = WLAN_AUTH_FT;
		break;
	case NL80211_AUTHTYPE_NETWORK_EAP:
		auth_alg = WLAN_AUTH_LEAP;
		break;
	case NL80211_AUTHTYPE_SAE:
		auth_alg = WLAN_AUTH_SAE;
		break;
	case NL80211_AUTHTYPE_FILS_SK:
		auth_alg = WLAN_AUTH_FILS_SK;
		break;
	case NL80211_AUTHTYPE_FILS_SK_PFS:
		auth_alg = WLAN_AUTH_FILS_SK_PFS;
		break;
	case NL80211_AUTHTYPE_FILS_PK:
		auth_alg = WLAN_AUTH_FILS_PK;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (ifmgd->assoc_data)
		return -EBUSY;

	auth_data = kzalloc(sizeof(*auth_data) + req->auth_data_len +
			    req->ie_len, GFP_KERNEL);
	if (!auth_data)
		return -ENOMEM;

	memcpy(auth_data->ap_addr,
	       req->ap_mld_addr ?: req->bss->bssid,
	       ETH_ALEN);
	auth_data->bss = req->bss;

	if (req->auth_data_len >= 4) {
		if (req->auth_type == NL80211_AUTHTYPE_SAE) {
			__le16 *pos = (__le16 *) req->auth_data;

			auth_data->sae_trans = le16_to_cpu(pos[0]);
			auth_data->sae_status = le16_to_cpu(pos[1]);
		}
		memcpy(auth_data->data, req->auth_data + 4,
		       req->auth_data_len - 4);
		auth_data->data_len += req->auth_data_len - 4;
	}

	/* Check if continuing authentication or trying to authenticate with the
	 * same BSS that we were in the process of authenticating with and avoid
	 * removal and re-addition of the STA entry in
	 * ieee80211_prep_connection().
	 */
	cont_auth = ifmgd->auth_data && req->bss == ifmgd->auth_data->bss;

	if (req->ie && req->ie_len) {
		memcpy(&auth_data->data[auth_data->data_len],
		       req->ie, req->ie_len);
		auth_data->data_len += req->ie_len;
	}

	if (req->key && req->key_len) {
		auth_data->key_len = req->key_len;
		auth_data->key_idx = req->key_idx;
		memcpy(auth_data->key, req->key, req->key_len);
	}

	auth_data->algorithm = auth_alg;

	/* try to authenticate/probe */

	if (ifmgd->auth_data) {
		if (cont_auth && req->auth_type == NL80211_AUTHTYPE_SAE) {
			auth_data->peer_confirmed =
				ifmgd->auth_data->peer_confirmed;
		}
		ieee80211_destroy_auth_data(sdata, cont_auth);
	}

	/* prep auth_data so we don't go into idle on disassoc */
	ifmgd->auth_data = auth_data;

	/* If this is continuation of an ongoing SAE authentication exchange
	 * (i.e., request to send SAE Confirm) and the peer has already
	 * confirmed, mark authentication completed since we are about to send
	 * out SAE Confirm.
	 */
	if (cont_auth && req->auth_type == NL80211_AUTHTYPE_SAE &&
	    auth_data->peer_confirmed && auth_data->sae_trans == 2)
		ieee80211_mark_sta_auth(sdata);

	if (ifmgd->associated) {
		u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];

		sdata_info(sdata,
			   "disconnect from AP %pM for new auth to %pM\n",
			   sdata->vif.cfg.ap_addr, auth_data->ap_addr);
		ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DEAUTH,
				       WLAN_REASON_UNSPECIFIED,
				       false, frame_buf);

		ieee80211_report_disconnect(sdata, frame_buf,
					    sizeof(frame_buf), true,
					    WLAN_REASON_UNSPECIFIED,
					    false);
	}

	sdata_info(sdata, "authenticate with %pM\n", auth_data->ap_addr);

	/* needed for transmitting the auth frame(s) properly */
	memcpy(sdata->vif.cfg.ap_addr, auth_data->ap_addr, ETH_ALEN);

	err = ieee80211_prep_connection(sdata, req->bss, req->link_id,
					req->ap_mld_addr, cont_auth, false);
	if (err)
		goto err_clear;

	err = ieee80211_auth(sdata);
	if (err) {
		sta_info_destroy_addr(sdata, auth_data->ap_addr);
		goto err_clear;
	}

	/* hold our own reference */
	cfg80211_ref_bss(local->hw.wiphy, auth_data->bss);
	return 0;

 err_clear:
	if (!sdata->vif.valid_links) {
		eth_zero_addr(sdata->deflink.u.mgd.bssid);
		ieee80211_link_info_change_notify(sdata, &sdata->deflink,
						  BSS_CHANGED_BSSID);
		mutex_lock(&sdata->local->mtx);
		ieee80211_link_release_channel(&sdata->deflink);
		mutex_unlock(&sdata->local->mtx);
	}
	ifmgd->auth_data = NULL;
	kfree(auth_data);
	return err;
}

static ieee80211_conn_flags_t
ieee80211_setup_assoc_link(struct ieee80211_sub_if_data *sdata,
			   struct ieee80211_mgd_assoc_data *assoc_data,
			   struct cfg80211_assoc_request *req,
			   ieee80211_conn_flags_t conn_flags,
			   unsigned int link_id)
{
	struct ieee80211_local *local = sdata->local;
	const struct cfg80211_bss_ies *beacon_ies;
	struct ieee80211_supported_band *sband;
	const struct element *ht_elem, *vht_elem;
	struct ieee80211_link_data *link;
	struct cfg80211_bss *cbss;
	struct ieee80211_bss *bss;
	bool is_5ghz, is_6ghz;

	cbss = assoc_data->link[link_id].bss;
	if (WARN_ON(!cbss))
		return 0;

	bss = (void *)cbss->priv;

	sband = local->hw.wiphy->bands[cbss->channel->band];
	if (WARN_ON(!sband))
		return 0;

	link = sdata_dereference(sdata->link[link_id], sdata);
	if (WARN_ON(!link))
		return 0;

	is_5ghz = cbss->channel->band == NL80211_BAND_5GHZ;
	is_6ghz = cbss->channel->band == NL80211_BAND_6GHZ;

	/* for MLO connections assume advertising all rates is OK */
	if (!req->ap_mld_addr) {
		assoc_data->supp_rates = bss->supp_rates;
		assoc_data->supp_rates_len = bss->supp_rates_len;
	}

	/* copy and link elems for the STA profile */
	if (req->links[link_id].elems_len) {
		memcpy(assoc_data->ie_pos, req->links[link_id].elems,
		       req->links[link_id].elems_len);
		assoc_data->link[link_id].elems = assoc_data->ie_pos;
		assoc_data->link[link_id].elems_len = req->links[link_id].elems_len;
		assoc_data->ie_pos += req->links[link_id].elems_len;
	}

	rcu_read_lock();
	ht_elem = ieee80211_bss_get_elem(cbss, WLAN_EID_HT_OPERATION);
	if (ht_elem && ht_elem->datalen >= sizeof(struct ieee80211_ht_operation))
		assoc_data->link[link_id].ap_ht_param =
			((struct ieee80211_ht_operation *)(ht_elem->data))->ht_param;
	else if (!is_6ghz)
		conn_flags |= IEEE80211_CONN_DISABLE_HT;
	vht_elem = ieee80211_bss_get_elem(cbss, WLAN_EID_VHT_CAPABILITY);
	if (vht_elem && vht_elem->datalen >= sizeof(struct ieee80211_vht_cap)) {
		memcpy(&assoc_data->link[link_id].ap_vht_cap, vht_elem->data,
		       sizeof(struct ieee80211_vht_cap));
	} else if (is_5ghz) {
		link_info(link,
			  "VHT capa missing/short, disabling VHT/HE/EHT\n");
		conn_flags |= IEEE80211_CONN_DISABLE_VHT |
			      IEEE80211_CONN_DISABLE_HE |
			      IEEE80211_CONN_DISABLE_EHT;
	}
	rcu_read_unlock();

	link->u.mgd.beacon_crc_valid = false;
	link->u.mgd.dtim_period = 0;
	link->u.mgd.have_beacon = false;

	/* override HT/VHT configuration only if the AP and we support it */
	if (!(conn_flags & IEEE80211_CONN_DISABLE_HT)) {
		struct ieee80211_sta_ht_cap sta_ht_cap;

		memcpy(&sta_ht_cap, &sband->ht_cap, sizeof(sta_ht_cap));
		ieee80211_apply_htcap_overrides(sdata, &sta_ht_cap);
	}

	rcu_read_lock();
	beacon_ies = rcu_dereference(cbss->beacon_ies);
	if (beacon_ies) {
		const struct element *elem;
		u8 dtim_count = 0;

		ieee80211_get_dtim(beacon_ies, &dtim_count,
				   &link->u.mgd.dtim_period);

		sdata->deflink.u.mgd.have_beacon = true;

		if (ieee80211_hw_check(&local->hw, TIMING_BEACON_ONLY)) {
			link->conf->sync_tsf = beacon_ies->tsf;
			link->conf->sync_device_ts = bss->device_ts_beacon;
			link->conf->sync_dtim_count = dtim_count;
		}

		elem = cfg80211_find_ext_elem(WLAN_EID_EXT_MULTIPLE_BSSID_CONFIGURATION,
					      beacon_ies->data, beacon_ies->len);
		if (elem && elem->datalen >= 3)
			link->conf->profile_periodicity = elem->data[2];
		else
			link->conf->profile_periodicity = 0;

		elem = cfg80211_find_elem(WLAN_EID_EXT_CAPABILITY,
					  beacon_ies->data, beacon_ies->len);
		if (elem && elem->datalen >= 11 &&
		    (elem->data[10] & WLAN_EXT_CAPA11_EMA_SUPPORT))
			link->conf->ema_ap = true;
		else
			link->conf->ema_ap = false;
	}
	rcu_read_unlock();

	if (bss->corrupt_data) {
		char *corrupt_type = "data";

		if (bss->corrupt_data & IEEE80211_BSS_CORRUPT_BEACON) {
			if (bss->corrupt_data & IEEE80211_BSS_CORRUPT_PROBE_RESP)
				corrupt_type = "beacon and probe response";
			else
				corrupt_type = "beacon";
		} else if (bss->corrupt_data & IEEE80211_BSS_CORRUPT_PROBE_RESP) {
			corrupt_type = "probe response";
		}
		sdata_info(sdata, "associating to AP %pM with corrupt %s\n",
			   cbss->bssid, corrupt_type);
	}

	if (link->u.mgd.req_smps == IEEE80211_SMPS_AUTOMATIC) {
		if (sdata->u.mgd.powersave)
			link->smps_mode = IEEE80211_SMPS_DYNAMIC;
		else
			link->smps_mode = IEEE80211_SMPS_OFF;
	} else {
		link->smps_mode = link->u.mgd.req_smps;
	}

	return conn_flags;
}

int ieee80211_mgd_assoc(struct ieee80211_sub_if_data *sdata,
			struct cfg80211_assoc_request *req)
{
	unsigned int assoc_link_id = req->link_id < 0 ? 0 : req->link_id;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_assoc_data *assoc_data;
	const struct element *ssid_elem;
	struct ieee80211_vif_cfg *vif_cfg = &sdata->vif.cfg;
	ieee80211_conn_flags_t conn_flags = 0;
	struct ieee80211_link_data *link;
	struct cfg80211_bss *cbss;
	struct ieee80211_bss *bss;
	bool override;
	int i, err;
	size_t size = sizeof(*assoc_data) + req->ie_len;

	for (i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++)
		size += req->links[i].elems_len;

	/* FIXME: no support for 4-addr MLO yet */
	if (sdata->u.mgd.use_4addr && req->link_id >= 0)
		return -EOPNOTSUPP;

	assoc_data = kzalloc(size, GFP_KERNEL);
	if (!assoc_data)
		return -ENOMEM;

	cbss = req->link_id < 0 ? req->bss : req->links[req->link_id].bss;

	rcu_read_lock();
	ssid_elem = ieee80211_bss_get_elem(cbss, WLAN_EID_SSID);
	if (!ssid_elem || ssid_elem->datalen > sizeof(assoc_data->ssid)) {
		rcu_read_unlock();
		kfree(assoc_data);
		return -EINVAL;
	}
	memcpy(assoc_data->ssid, ssid_elem->data, ssid_elem->datalen);
	assoc_data->ssid_len = ssid_elem->datalen;
	memcpy(vif_cfg->ssid, assoc_data->ssid, assoc_data->ssid_len);
	vif_cfg->ssid_len = assoc_data->ssid_len;
	rcu_read_unlock();

	if (req->ap_mld_addr) {
		for (i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++) {
			if (!req->links[i].bss)
				continue;
			link = sdata_dereference(sdata->link[i], sdata);
			if (link)
				ether_addr_copy(assoc_data->link[i].addr,
						link->conf->addr);
			else
				eth_random_addr(assoc_data->link[i].addr);
		}
	} else {
		memcpy(assoc_data->link[0].addr, sdata->vif.addr, ETH_ALEN);
	}

	assoc_data->s1g = cbss->channel->band == NL80211_BAND_S1GHZ;

	memcpy(assoc_data->ap_addr,
	       req->ap_mld_addr ?: req->bss->bssid,
	       ETH_ALEN);

	if (ifmgd->associated) {
		u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];

		sdata_info(sdata,
			   "disconnect from AP %pM for new assoc to %pM\n",
			   sdata->vif.cfg.ap_addr, assoc_data->ap_addr);
		ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DEAUTH,
				       WLAN_REASON_UNSPECIFIED,
				       false, frame_buf);

		ieee80211_report_disconnect(sdata, frame_buf,
					    sizeof(frame_buf), true,
					    WLAN_REASON_UNSPECIFIED,
					    false);
	}

	if (ifmgd->auth_data && !ifmgd->auth_data->done) {
		err = -EBUSY;
		goto err_free;
	}

	if (ifmgd->assoc_data) {
		err = -EBUSY;
		goto err_free;
	}

	if (ifmgd->auth_data) {
		bool match;

		/* keep sta info, bssid if matching */
		match = ether_addr_equal(ifmgd->auth_data->ap_addr,
					 assoc_data->ap_addr);
		ieee80211_destroy_auth_data(sdata, match);
	}

	/* prepare assoc data */

	bss = (void *)cbss->priv;
	assoc_data->wmm = bss->wmm_used &&
			  (local->hw.queues >= IEEE80211_NUM_ACS);

	/*
	 * IEEE802.11n does not allow TKIP/WEP as pairwise ciphers in HT mode.
	 * We still associate in non-HT mode (11a/b/g) if any one of these
	 * ciphers is configured as pairwise.
	 * We can set this to true for non-11n hardware, that'll be checked
	 * separately along with the peer capabilities.
	 */
	for (i = 0; i < req->crypto.n_ciphers_pairwise; i++) {
		if (req->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_WEP40 ||
		    req->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_TKIP ||
		    req->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_WEP104) {
			conn_flags |= IEEE80211_CONN_DISABLE_HT;
			conn_flags |= IEEE80211_CONN_DISABLE_VHT;
			conn_flags |= IEEE80211_CONN_DISABLE_HE;
			conn_flags |= IEEE80211_CONN_DISABLE_EHT;
			netdev_info(sdata->dev,
				    "disabling HT/VHT/HE due to WEP/TKIP use\n");
		}
	}

	/* also disable HT/VHT/HE/EHT if the AP doesn't use WMM */
	if (!bss->wmm_used) {
		conn_flags |= IEEE80211_CONN_DISABLE_HT;
		conn_flags |= IEEE80211_CONN_DISABLE_VHT;
		conn_flags |= IEEE80211_CONN_DISABLE_HE;
		conn_flags |= IEEE80211_CONN_DISABLE_EHT;
		netdev_info(sdata->dev,
			    "disabling HT/VHT/HE as WMM/QoS is not supported by the AP\n");
	}

	if (req->flags & ASSOC_REQ_DISABLE_HT) {
		mlme_dbg(sdata, "HT disabled by flag, disabling HT/VHT/HE\n");
		conn_flags |= IEEE80211_CONN_DISABLE_HT;
		conn_flags |= IEEE80211_CONN_DISABLE_VHT;
		conn_flags |= IEEE80211_CONN_DISABLE_HE;
		conn_flags |= IEEE80211_CONN_DISABLE_EHT;
	}

	if (req->flags & ASSOC_REQ_DISABLE_VHT) {
		mlme_dbg(sdata, "VHT disabled by flag, disabling VHT\n");
		conn_flags |= IEEE80211_CONN_DISABLE_VHT;
	}

	if (req->flags & ASSOC_REQ_DISABLE_HE) {
		mlme_dbg(sdata, "HE disabled by flag, disabling HE/EHT\n");
		conn_flags |= IEEE80211_CONN_DISABLE_HE;
		conn_flags |= IEEE80211_CONN_DISABLE_EHT;
	}

	if (req->flags & ASSOC_REQ_DISABLE_EHT)
		conn_flags |= IEEE80211_CONN_DISABLE_EHT;

	memcpy(&ifmgd->ht_capa, &req->ht_capa, sizeof(ifmgd->ht_capa));
	memcpy(&ifmgd->ht_capa_mask, &req->ht_capa_mask,
	       sizeof(ifmgd->ht_capa_mask));

	memcpy(&ifmgd->vht_capa, &req->vht_capa, sizeof(ifmgd->vht_capa));
	memcpy(&ifmgd->vht_capa_mask, &req->vht_capa_mask,
	       sizeof(ifmgd->vht_capa_mask));

	memcpy(&ifmgd->s1g_capa, &req->s1g_capa, sizeof(ifmgd->s1g_capa));
	memcpy(&ifmgd->s1g_capa_mask, &req->s1g_capa_mask,
	       sizeof(ifmgd->s1g_capa_mask));

	if (req->ie && req->ie_len) {
		memcpy(assoc_data->ie, req->ie, req->ie_len);
		assoc_data->ie_len = req->ie_len;
		assoc_data->ie_pos = assoc_data->ie + assoc_data->ie_len;
	} else {
		assoc_data->ie_pos = assoc_data->ie;
	}

	if (req->fils_kek) {
		/* should already be checked in cfg80211 - so warn */
		if (WARN_ON(req->fils_kek_len > FILS_MAX_KEK_LEN)) {
			err = -EINVAL;
			goto err_free;
		}
		memcpy(assoc_data->fils_kek, req->fils_kek,
		       req->fils_kek_len);
		assoc_data->fils_kek_len = req->fils_kek_len;
	}

	if (req->fils_nonces)
		memcpy(assoc_data->fils_nonces, req->fils_nonces,
		       2 * FILS_NONCE_LEN);

	/* default timeout */
	assoc_data->timeout = jiffies;
	assoc_data->timeout_started = true;

	assoc_data->assoc_link_id = assoc_link_id;

	if (req->ap_mld_addr) {
		for (i = 0; i < ARRAY_SIZE(assoc_data->link); i++) {
			assoc_data->link[i].conn_flags = conn_flags;
			assoc_data->link[i].bss = req->links[i].bss;
		}

		/* if there was no authentication, set up the link */
		err = ieee80211_vif_set_links(sdata, BIT(assoc_link_id));
		if (err)
			goto err_clear;
	} else {
		assoc_data->link[0].conn_flags = conn_flags;
		assoc_data->link[0].bss = cbss;
	}

	link = sdata_dereference(sdata->link[assoc_link_id], sdata);
	if (WARN_ON(!link)) {
		err = -EINVAL;
		goto err_clear;
	}

	/* keep old conn_flags from ieee80211_prep_channel() from auth */
	conn_flags |= link->u.mgd.conn_flags;
	conn_flags |= ieee80211_setup_assoc_link(sdata, assoc_data, req,
						 conn_flags, assoc_link_id);
	override = link->u.mgd.conn_flags != conn_flags;
	link->u.mgd.conn_flags |= conn_flags;

	if (WARN((sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_UAPSD) &&
		 ieee80211_hw_check(&local->hw, PS_NULLFUNC_STACK),
	     "U-APSD not supported with HW_PS_NULLFUNC_STACK\n"))
		sdata->vif.driver_flags &= ~IEEE80211_VIF_SUPPORTS_UAPSD;

	if (bss->wmm_used && bss->uapsd_supported &&
	    (sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_UAPSD)) {
		assoc_data->uapsd = true;
		ifmgd->flags |= IEEE80211_STA_UAPSD_ENABLED;
	} else {
		assoc_data->uapsd = false;
		ifmgd->flags &= ~IEEE80211_STA_UAPSD_ENABLED;
	}

	if (req->prev_bssid)
		memcpy(assoc_data->prev_ap_addr, req->prev_bssid, ETH_ALEN);

	if (req->use_mfp) {
		ifmgd->mfp = IEEE80211_MFP_REQUIRED;
		ifmgd->flags |= IEEE80211_STA_MFP_ENABLED;
	} else {
		ifmgd->mfp = IEEE80211_MFP_DISABLED;
		ifmgd->flags &= ~IEEE80211_STA_MFP_ENABLED;
	}

	if (req->flags & ASSOC_REQ_USE_RRM)
		ifmgd->flags |= IEEE80211_STA_ENABLE_RRM;
	else
		ifmgd->flags &= ~IEEE80211_STA_ENABLE_RRM;

	if (req->crypto.control_port)
		ifmgd->flags |= IEEE80211_STA_CONTROL_PORT;
	else
		ifmgd->flags &= ~IEEE80211_STA_CONTROL_PORT;

	sdata->control_port_protocol = req->crypto.control_port_ethertype;
	sdata->control_port_no_encrypt = req->crypto.control_port_no_encrypt;
	sdata->control_port_over_nl80211 =
					req->crypto.control_port_over_nl80211;
	sdata->control_port_no_preauth = req->crypto.control_port_no_preauth;

	/* kick off associate process */
	ifmgd->assoc_data = assoc_data;

	for (i = 0; i < ARRAY_SIZE(assoc_data->link); i++) {
		if (!assoc_data->link[i].bss)
			continue;
		if (i == assoc_data->assoc_link_id)
			continue;
		/* only calculate the flags, hence link == NULL */
		err = ieee80211_prep_channel(sdata, NULL, assoc_data->link[i].bss,
					     &assoc_data->link[i].conn_flags);
		if (err)
			goto err_clear;
	}

	/* needed for transmitting the assoc frames properly */
	memcpy(sdata->vif.cfg.ap_addr, assoc_data->ap_addr, ETH_ALEN);

	err = ieee80211_prep_connection(sdata, cbss, req->link_id,
					req->ap_mld_addr, true, override);
	if (err)
		goto err_clear;

	assoc_data->link[assoc_data->assoc_link_id].conn_flags =
		link->u.mgd.conn_flags;

	if (ieee80211_hw_check(&sdata->local->hw, NEED_DTIM_BEFORE_ASSOC)) {
		const struct cfg80211_bss_ies *beacon_ies;

		rcu_read_lock();
		beacon_ies = rcu_dereference(req->bss->beacon_ies);

		if (beacon_ies) {
			/*
			 * Wait up to one beacon interval ...
			 * should this be more if we miss one?
			 */
			sdata_info(sdata, "waiting for beacon from %pM\n",
				   link->u.mgd.bssid);
			assoc_data->timeout = TU_TO_EXP_TIME(req->bss->beacon_interval);
			assoc_data->timeout_started = true;
			assoc_data->need_beacon = true;
		}
		rcu_read_unlock();
	}

	run_again(sdata, assoc_data->timeout);

	return 0;
 err_clear:
	eth_zero_addr(sdata->deflink.u.mgd.bssid);
	ieee80211_link_info_change_notify(sdata, &sdata->deflink,
					  BSS_CHANGED_BSSID);
	ifmgd->assoc_data = NULL;
 err_free:
	kfree(assoc_data);
	return err;
}

int ieee80211_mgd_deauth(struct ieee80211_sub_if_data *sdata,
			 struct cfg80211_deauth_request *req)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];
	bool tx = !req->local_state_change;
	struct ieee80211_prep_tx_info info = {
		.subtype = IEEE80211_STYPE_DEAUTH,
	};

	if (ifmgd->auth_data &&
	    ether_addr_equal(ifmgd->auth_data->ap_addr, req->bssid)) {
		sdata_info(sdata,
			   "aborting authentication with %pM by local choice (Reason: %u=%s)\n",
			   req->bssid, req->reason_code,
			   ieee80211_get_reason_code_string(req->reason_code));

		drv_mgd_prepare_tx(sdata->local, sdata, &info);
		ieee80211_send_deauth_disassoc(sdata, req->bssid, req->bssid,
					       IEEE80211_STYPE_DEAUTH,
					       req->reason_code, tx,
					       frame_buf);
		ieee80211_destroy_auth_data(sdata, false);
		ieee80211_report_disconnect(sdata, frame_buf,
					    sizeof(frame_buf), true,
					    req->reason_code, false);
		drv_mgd_complete_tx(sdata->local, sdata, &info);
		return 0;
	}

	if (ifmgd->assoc_data &&
	    ether_addr_equal(ifmgd->assoc_data->ap_addr, req->bssid)) {
		sdata_info(sdata,
			   "aborting association with %pM by local choice (Reason: %u=%s)\n",
			   req->bssid, req->reason_code,
			   ieee80211_get_reason_code_string(req->reason_code));

		drv_mgd_prepare_tx(sdata->local, sdata, &info);
		ieee80211_send_deauth_disassoc(sdata, req->bssid, req->bssid,
					       IEEE80211_STYPE_DEAUTH,
					       req->reason_code, tx,
					       frame_buf);
		ieee80211_destroy_assoc_data(sdata, ASSOC_ABANDON);
		ieee80211_report_disconnect(sdata, frame_buf,
					    sizeof(frame_buf), true,
					    req->reason_code, false);
		return 0;
	}

	if (ifmgd->associated &&
	    ether_addr_equal(sdata->vif.cfg.ap_addr, req->bssid)) {
		sdata_info(sdata,
			   "deauthenticating from %pM by local choice (Reason: %u=%s)\n",
			   req->bssid, req->reason_code,
			   ieee80211_get_reason_code_string(req->reason_code));

		ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DEAUTH,
				       req->reason_code, tx, frame_buf);
		ieee80211_report_disconnect(sdata, frame_buf,
					    sizeof(frame_buf), true,
					    req->reason_code, false);
		drv_mgd_complete_tx(sdata->local, sdata, &info);
		return 0;
	}

	return -ENOTCONN;
}

int ieee80211_mgd_disassoc(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_disassoc_request *req)
{
	u8 frame_buf[IEEE80211_DEAUTH_FRAME_LEN];

	if (!sdata->u.mgd.associated ||
	    memcmp(sdata->vif.cfg.ap_addr, req->ap_addr, ETH_ALEN))
		return -ENOTCONN;

	sdata_info(sdata,
		   "disassociating from %pM by local choice (Reason: %u=%s)\n",
		   req->ap_addr, req->reason_code,
		   ieee80211_get_reason_code_string(req->reason_code));

	ieee80211_set_disassoc(sdata, IEEE80211_STYPE_DISASSOC,
			       req->reason_code, !req->local_state_change,
			       frame_buf);

	ieee80211_report_disconnect(sdata, frame_buf, sizeof(frame_buf), true,
				    req->reason_code, false);

	return 0;
}

void ieee80211_mgd_stop_link(struct ieee80211_link_data *link)
{
	cancel_work_sync(&link->u.mgd.request_smps_work);
	cancel_work_sync(&link->u.mgd.chswitch_work);
}

void ieee80211_mgd_stop(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	/*
	 * Make sure some work items will not run after this,
	 * they will not do anything but might not have been
	 * cancelled when disconnecting.
	 */
	cancel_work_sync(&ifmgd->monitor_work);
	cancel_work_sync(&ifmgd->beacon_connection_loss_work);
	cancel_work_sync(&ifmgd->csa_connection_drop_work);
	cancel_delayed_work_sync(&ifmgd->tdls_peer_del_work);

	sdata_lock(sdata);
	if (ifmgd->assoc_data)
		ieee80211_destroy_assoc_data(sdata, ASSOC_TIMEOUT);
	if (ifmgd->auth_data)
		ieee80211_destroy_auth_data(sdata, false);
	spin_lock_bh(&ifmgd->teardown_lock);
	if (ifmgd->teardown_skb) {
		kfree_skb(ifmgd->teardown_skb);
		ifmgd->teardown_skb = NULL;
		ifmgd->orig_teardown_skb = NULL;
	}
	kfree(ifmgd->assoc_req_ies);
	ifmgd->assoc_req_ies = NULL;
	ifmgd->assoc_req_ies_len = 0;
	spin_unlock_bh(&ifmgd->teardown_lock);
	del_timer_sync(&ifmgd->timer);
	sdata_unlock(sdata);
}

void ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif,
			       enum nl80211_cqm_rssi_threshold_event rssi_event,
			       s32 rssi_level,
			       gfp_t gfp)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	trace_api_cqm_rssi_notify(sdata, rssi_event, rssi_level);

	cfg80211_cqm_rssi_notify(sdata->dev, rssi_event, rssi_level, gfp);
}
EXPORT_SYMBOL(ieee80211_cqm_rssi_notify);

void ieee80211_cqm_beacon_loss_notify(struct ieee80211_vif *vif, gfp_t gfp)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	trace_api_cqm_beacon_loss_notify(sdata->local, sdata);

	cfg80211_cqm_beacon_loss_notify(sdata->dev, gfp);
}
EXPORT_SYMBOL(ieee80211_cqm_beacon_loss_notify);

static void _ieee80211_enable_rssi_reports(struct ieee80211_sub_if_data *sdata,
					    int rssi_min_thold,
					    int rssi_max_thold)
{
	trace_api_enable_rssi_reports(sdata, rssi_min_thold, rssi_max_thold);

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return;

	/*
	 * Scale up threshold values before storing it, as the RSSI averaging
	 * algorithm uses a scaled up value as well. Change this scaling
	 * factor if the RSSI averaging algorithm changes.
	 */
	sdata->u.mgd.rssi_min_thold = rssi_min_thold*16;
	sdata->u.mgd.rssi_max_thold = rssi_max_thold*16;
}

void ieee80211_enable_rssi_reports(struct ieee80211_vif *vif,
				    int rssi_min_thold,
				    int rssi_max_thold)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	WARN_ON(rssi_min_thold == rssi_max_thold ||
		rssi_min_thold > rssi_max_thold);

	_ieee80211_enable_rssi_reports(sdata, rssi_min_thold,
				       rssi_max_thold);
}
EXPORT_SYMBOL(ieee80211_enable_rssi_reports);

void ieee80211_disable_rssi_reports(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	_ieee80211_enable_rssi_reports(sdata, 0, 0);
}
EXPORT_SYMBOL(ieee80211_disable_rssi_reports);
