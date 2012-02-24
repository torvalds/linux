/*
 * BSS client mode implementation
 * Copyright 2003-2008, Jouni Malinen <j@w1.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/moduleparam.h>
#include <linux/rtnetlink.h>
#include <linux/pm_qos.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/mac80211.h>
#include <asm/unaligned.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "led.h"

#define IEEE80211_AUTH_TIMEOUT (HZ / 5)
#define IEEE80211_AUTH_MAX_TRIES 3
#define IEEE80211_AUTH_WAIT_ASSOC (HZ * 5)
#define IEEE80211_ASSOC_TIMEOUT (HZ / 5)
#define IEEE80211_ASSOC_MAX_TRIES 3

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
#define IEEE80211_BEACON_LOSS_COUNT	7

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
 * Weight given to the latest Beacon frame when calculating average signal
 * strength for Beacon frames received in the current BSS. This must be
 * between 1 and 15.
 */
#define IEEE80211_SIGNAL_AVE_WEIGHT	3

/*
 * How many Beacon frames need to have been used in average signal strength
 * before starting to indicate signal change events.
 */
#define IEEE80211_SIGNAL_AVE_MIN_COUNT	4

#define TMR_RUNNING_TIMER	0
#define TMR_RUNNING_CHANSW	1

#define DEAUTH_DISASSOC_LEN	(24 /* hdr */ + 2 /* reason */)

/*
 * All cfg80211 functions have to be called outside a locked
 * section so that they can acquire a lock themselves... This
 * is much simpler than queuing up things in cfg80211, but we
 * do need some indirection for that here.
 */
enum rx_mgmt_action {
	/* no action required */
	RX_MGMT_NONE,

	/* caller must call cfg80211_send_deauth() */
	RX_MGMT_CFG80211_DEAUTH,

	/* caller must call cfg80211_send_disassoc() */
	RX_MGMT_CFG80211_DISASSOC,

	/* caller must call cfg80211_send_rx_auth() */
	RX_MGMT_CFG80211_RX_AUTH,

	/* caller must call cfg80211_send_rx_assoc() */
	RX_MGMT_CFG80211_RX_ASSOC,

	/* caller must call cfg80211_send_assoc_timeout() */
	RX_MGMT_CFG80211_ASSOC_TIMEOUT,
};

/* utils */
static inline void ASSERT_MGD_MTX(struct ieee80211_if_managed *ifmgd)
{
	lockdep_assert_held(&ifmgd->mtx);
}

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
static void run_again(struct ieee80211_if_managed *ifmgd, unsigned long timeout)
{
	ASSERT_MGD_MTX(ifmgd);

	if (!timer_pending(&ifmgd->timer) ||
	    time_before(timeout, ifmgd->timer.expires))
		mod_timer(&ifmgd->timer, timeout);
}

void ieee80211_sta_reset_beacon_monitor(struct ieee80211_sub_if_data *sdata)
{
	if (sdata->vif.driver_flags & IEEE80211_VIF_BEACON_FILTER)
		return;

	mod_timer(&sdata->u.mgd.bcn_mon_timer,
		  round_jiffies_up(jiffies + sdata->u.mgd.beacon_timeout));
}

void ieee80211_sta_reset_conn_monitor(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (unlikely(!sdata->u.mgd.associated))
		return;

	if (sdata->local->hw.flags & IEEE80211_HW_CONNECTION_MONITOR)
		return;

	mod_timer(&sdata->u.mgd.conn_mon_timer,
		  round_jiffies_up(jiffies + IEEE80211_CONNECTION_IDLE_TIME));

	ifmgd->probe_send_count = 0;
}

static int ecw2cw(int ecw)
{
	return (1 << ecw) - 1;
}

/*
 * ieee80211_enable_ht should be called only after the operating band
 * has been determined as ht configuration depends on the hw's
 * HT abilities for a specific band.
 */
static u32 ieee80211_enable_ht(struct ieee80211_sub_if_data *sdata,
			       struct ieee80211_ht_info *hti,
			       const u8 *bssid, u16 ap_ht_cap_flags,
			       bool beacon_htcap_ie)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	struct sta_info *sta;
	u32 changed = 0;
	int hti_cfreq;
	u16 ht_opmode;
	bool enable_ht = true;
	enum nl80211_channel_type prev_chantype;
	enum nl80211_channel_type channel_type = NL80211_CHAN_NO_HT;

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	prev_chantype = sdata->vif.bss_conf.channel_type;

	/* HT is not supported */
	if (!sband->ht_cap.ht_supported)
		enable_ht = false;

	if (enable_ht) {
		hti_cfreq = ieee80211_channel_to_frequency(hti->control_chan,
							   sband->band);
		/* check that channel matches the right operating channel */
		if (local->hw.conf.channel->center_freq != hti_cfreq) {
			/* Some APs mess this up, evidently.
			 * Netgear WNDR3700 sometimes reports 4 higher than
			 * the actual channel, for instance.
			 */
			printk(KERN_DEBUG
			       "%s: Wrong control channel in association"
			       " response: configured center-freq: %d"
			       " hti-cfreq: %d  hti->control_chan: %d"
			       " band: %d.  Disabling HT.\n",
			       sdata->name,
			       local->hw.conf.channel->center_freq,
			       hti_cfreq, hti->control_chan,
			       sband->band);
			enable_ht = false;
		}
	}

	if (enable_ht) {
		channel_type = NL80211_CHAN_HT20;

		if (!(ap_ht_cap_flags & IEEE80211_HT_CAP_40MHZ_INTOLERANT) &&
		    !ieee80111_cfg_override_disables_ht40(sdata) &&
		    (sband->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) &&
		    (hti->ht_param & IEEE80211_HT_PARAM_CHAN_WIDTH_ANY)) {
			switch(hti->ht_param & IEEE80211_HT_PARAM_CHA_SEC_OFFSET) {
			case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
				if (!(local->hw.conf.channel->flags &
				    IEEE80211_CHAN_NO_HT40PLUS))
					channel_type = NL80211_CHAN_HT40PLUS;
				break;
			case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
				if (!(local->hw.conf.channel->flags &
				    IEEE80211_CHAN_NO_HT40MINUS))
					channel_type = NL80211_CHAN_HT40MINUS;
				break;
			}
		}
	}

	if (local->tmp_channel)
		local->tmp_channel_type = channel_type;

	if (!ieee80211_set_channel_type(local, sdata, channel_type)) {
		/* can only fail due to HT40+/- mismatch */
		channel_type = NL80211_CHAN_HT20;
		WARN_ON(!ieee80211_set_channel_type(local, sdata, channel_type));
	}

	if (beacon_htcap_ie && (prev_chantype != channel_type)) {
		/*
		 * Whenever the AP announces the HT mode change that can be
		 * 40MHz intolerant or etc., it would be safer to stop tx
		 * queues before doing hw config to avoid buffer overflow.
		 */
		ieee80211_stop_queues_by_reason(&sdata->local->hw,
				IEEE80211_QUEUE_STOP_REASON_CHTYPE_CHANGE);

		/* flush out all packets */
		synchronize_net();

		drv_flush(local, false);
	}

	/* channel_type change automatically detected */
	ieee80211_hw_config(local, 0);

	if (prev_chantype != channel_type) {
		rcu_read_lock();
		sta = sta_info_get(sdata, bssid);
		if (sta)
			rate_control_rate_update(local, sband, sta,
						 IEEE80211_RC_HT_CHANGED,
						 channel_type);
		rcu_read_unlock();

		if (beacon_htcap_ie)
			ieee80211_wake_queues_by_reason(&sdata->local->hw,
				IEEE80211_QUEUE_STOP_REASON_CHTYPE_CHANGE);
	}

	ht_opmode = le16_to_cpu(hti->operation_mode);

	/* if bss configuration changed store the new one */
	if (sdata->ht_opmode_valid != enable_ht ||
	    sdata->vif.bss_conf.ht_operation_mode != ht_opmode ||
	    prev_chantype != channel_type) {
		changed |= BSS_CHANGED_HT;
		sdata->vif.bss_conf.ht_operation_mode = ht_opmode;
		sdata->ht_opmode_valid = enable_ht;
	}

	return changed;
}

/* frame sending functions */

static int ieee80211_compatible_rates(const u8 *supp_rates, int supp_rates_len,
				      struct ieee80211_supported_band *sband,
				      u32 *rates)
{
	int i, j, count;
	*rates = 0;
	count = 0;
	for (i = 0; i < supp_rates_len; i++) {
		int rate = (supp_rates[i] & 0x7F) * 5;

		for (j = 0; j < sband->n_bitrates; j++)
			if (sband->bitrates[j].bitrate == rate) {
				*rates |= BIT(j);
				count++;
				break;
			}
	}

	return count;
}

static void ieee80211_add_ht_ie(struct ieee80211_sub_if_data *sdata,
				struct sk_buff *skb, const u8 *ht_info_ie,
				struct ieee80211_supported_band *sband,
				struct ieee80211_channel *channel,
				enum ieee80211_smps_mode smps)
{
	struct ieee80211_ht_info *ht_info;
	u8 *pos;
	u32 flags = channel->flags;
	u16 cap;
	struct ieee80211_sta_ht_cap ht_cap;

	BUILD_BUG_ON(sizeof(ht_cap) != sizeof(sband->ht_cap));

	if (!sband->ht_cap.ht_supported)
		return;

	if (!ht_info_ie)
		return;

	if (ht_info_ie[1] < sizeof(struct ieee80211_ht_info))
		return;

	memcpy(&ht_cap, &sband->ht_cap, sizeof(ht_cap));
	ieee80211_apply_htcap_overrides(sdata, &ht_cap);

	ht_info = (struct ieee80211_ht_info *)(ht_info_ie + 2);

	/* determine capability flags */
	cap = ht_cap.cap;

	switch (ht_info->ht_param & IEEE80211_HT_PARAM_CHA_SEC_OFFSET) {
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

	/* set SM PS mode properly */
	cap &= ~IEEE80211_HT_CAP_SM_PS;
	switch (smps) {
	case IEEE80211_SMPS_AUTOMATIC:
	case IEEE80211_SMPS_NUM_MODES:
		WARN_ON(1);
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

static void ieee80211_send_assoc(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_assoc_data *assoc_data = ifmgd->assoc_data;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u8 *pos, qos_info;
	size_t offset = 0, noffset;
	int i, count, rates_len, supp_rates_len;
	u16 capab;
	struct ieee80211_supported_band *sband;
	u32 rates = 0;
	struct ieee80211_bss *bss = (void *)assoc_data->bss->priv;

	lockdep_assert_held(&ifmgd->mtx);

	sband = local->hw.wiphy->bands[local->oper_channel->band];

	if (assoc_data->supp_rates_len) {
		/*
		 * Get all rates supported by the device and the AP as
		 * some APs don't like getting a superset of their rates
		 * in the association request (e.g. D-Link DAP 1353 in
		 * b-only mode)...
		 */
		rates_len = ieee80211_compatible_rates(assoc_data->supp_rates,
						       assoc_data->supp_rates_len,
						       sband, &rates);
	} else {
		/*
		 * In case AP not provide any supported rates information
		 * before association, we send information element(s) with
		 * all rates that we support.
		 */
		rates = ~0;
		rates_len = sband->n_bitrates;
	}

	skb = alloc_skb(local->hw.extra_tx_headroom +
			sizeof(*mgmt) + /* bit too much but doesn't matter */
			2 + assoc_data->ssid_len + /* SSID */
			4 + rates_len + /* (extended) rates */
			4 + /* power capability */
			2 + 2 * sband->n_channels + /* supported channels */
			2 + sizeof(struct ieee80211_ht_cap) + /* HT */
			assoc_data->ie_len + /* extra IEs */
			9, /* WMM */
			GFP_KERNEL);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	capab = WLAN_CAPABILITY_ESS;

	if (sband->band == IEEE80211_BAND_2GHZ) {
		if (!(local->hw.flags & IEEE80211_HW_2GHZ_SHORT_SLOT_INCAPABLE))
			capab |= WLAN_CAPABILITY_SHORT_SLOT_TIME;
		if (!(local->hw.flags & IEEE80211_HW_2GHZ_SHORT_PREAMBLE_INCAPABLE))
			capab |= WLAN_CAPABILITY_SHORT_PREAMBLE;
	}

	if (assoc_data->capability & WLAN_CAPABILITY_PRIVACY)
		capab |= WLAN_CAPABILITY_PRIVACY;

	if ((assoc_data->capability & WLAN_CAPABILITY_SPECTRUM_MGMT) &&
	    (local->hw.flags & IEEE80211_HW_SPECTRUM_MGMT))
		capab |= WLAN_CAPABILITY_SPECTRUM_MGMT;

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, assoc_data->bss->bssid, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, assoc_data->bss->bssid, ETH_ALEN);

	if (!is_zero_ether_addr(assoc_data->prev_bssid)) {
		skb_put(skb, 10);
		mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						  IEEE80211_STYPE_REASSOC_REQ);
		mgmt->u.reassoc_req.capab_info = cpu_to_le16(capab);
		mgmt->u.reassoc_req.listen_interval =
				cpu_to_le16(local->hw.conf.listen_interval);
		memcpy(mgmt->u.reassoc_req.current_ap, assoc_data->prev_bssid,
		       ETH_ALEN);
	} else {
		skb_put(skb, 4);
		mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						  IEEE80211_STYPE_ASSOC_REQ);
		mgmt->u.assoc_req.capab_info = cpu_to_le16(capab);
		mgmt->u.assoc_req.listen_interval =
				cpu_to_le16(local->hw.conf.listen_interval);
	}

	/* SSID */
	pos = skb_put(skb, 2 + assoc_data->ssid_len);
	*pos++ = WLAN_EID_SSID;
	*pos++ = assoc_data->ssid_len;
	memcpy(pos, assoc_data->ssid, assoc_data->ssid_len);

	/* add all rates which were marked to be used above */
	supp_rates_len = rates_len;
	if (supp_rates_len > 8)
		supp_rates_len = 8;

	pos = skb_put(skb, supp_rates_len + 2);
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = supp_rates_len;

	count = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if (BIT(i) & rates) {
			int rate = sband->bitrates[i].bitrate;
			*pos++ = (u8) (rate / 5);
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
				int rate = sband->bitrates[i].bitrate;
				*pos++ = (u8) (rate / 5);
			}
		}
	}

	if (capab & WLAN_CAPABILITY_SPECTRUM_MGMT) {
		/* 1. power capabilities */
		pos = skb_put(skb, 4);
		*pos++ = WLAN_EID_PWR_CAPABILITY;
		*pos++ = 2;
		*pos++ = 0; /* min tx power */
		*pos++ = local->oper_channel->max_power; /* max tx power */

		/* 2. supported channels */
		/* TODO: get this in reg domain format */
		pos = skb_put(skb, 2 * sband->n_channels + 2);
		*pos++ = WLAN_EID_SUPPORTED_CHANNELS;
		*pos++ = 2 * sband->n_channels;
		for (i = 0; i < sband->n_channels; i++) {
			*pos++ = ieee80211_frequency_to_channel(
					sband->channels[i].center_freq);
			*pos++ = 1; /* one channel in the subband*/
		}
	}

	/* if present, add any custom IEs that go before HT */
	if (assoc_data->ie_len && assoc_data->ie) {
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
			WLAN_EID_SUPPORTED_REGULATORY_CLASSES,
		};
		noffset = ieee80211_ie_split(assoc_data->ie, assoc_data->ie_len,
					     before_ht, ARRAY_SIZE(before_ht),
					     offset);
		pos = skb_put(skb, noffset - offset);
		memcpy(pos, assoc_data->ie + offset, noffset - offset);
		offset = noffset;
	}

	if (!(ifmgd->flags & IEEE80211_STA_DISABLE_11N) &&
	    bss->wmm_used && local->hw.queues >= 4)
		ieee80211_add_ht_ie(sdata, skb, assoc_data->ht_information_ie,
				    sband, local->oper_channel, ifmgd->ap_smps);

	/* if present, add any custom non-vendor IEs that go after HT */
	if (assoc_data->ie_len && assoc_data->ie) {
		noffset = ieee80211_ie_split_vendor(assoc_data->ie,
						    assoc_data->ie_len,
						    offset);
		pos = skb_put(skb, noffset - offset);
		memcpy(pos, assoc_data->ie + offset, noffset - offset);
		offset = noffset;
	}

	if (assoc_data->wmm_used && local->hw.queues >= 4) {
		if (assoc_data->uapsd_used) {
			qos_info = local->uapsd_queues;
			qos_info |= (local->uapsd_max_sp_len <<
				     IEEE80211_WMM_IE_STA_QOSINFO_SP_SHIFT);
		} else {
			qos_info = 0;
		}

		pos = skb_put(skb, 9);
		*pos++ = WLAN_EID_VENDOR_SPECIFIC;
		*pos++ = 7; /* len */
		*pos++ = 0x00; /* Microsoft OUI 00:50:F2 */
		*pos++ = 0x50;
		*pos++ = 0xf2;
		*pos++ = 2; /* WME */
		*pos++ = 0; /* WME info */
		*pos++ = 1; /* WME ver */
		*pos++ = qos_info;
	}

	/* add any remaining custom (i.e. vendor specific here) IEs */
	if (assoc_data->ie_len && assoc_data->ie) {
		noffset = assoc_data->ie_len;
		pos = skb_put(skb, noffset - offset);
		memcpy(pos, assoc_data->ie + offset, noffset - offset);
	}

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	ieee80211_tx_skb(sdata, skb);
}

static void ieee80211_send_deauth_disassoc(struct ieee80211_sub_if_data *sdata,
					   const u8 *bssid, u16 stype,
					   u16 reason, bool send_frame,
					   u8 *frame_buf)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt = (void *)frame_buf;

	/* build frame */
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | stype);
	mgmt->duration = 0; /* initialize only */
	mgmt->seq_ctrl = 0; /* initialize only */
	memcpy(mgmt->da, bssid, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);
	/* u.deauth.reason_code == u.disassoc.reason_code */
	mgmt->u.deauth.reason_code = cpu_to_le16(reason);

	if (send_frame) {
		skb = dev_alloc_skb(local->hw.extra_tx_headroom +
				    DEAUTH_DISASSOC_LEN);
		if (!skb)
			return;

		skb_reserve(skb, local->hw.extra_tx_headroom);

		/* copy in frame */
		memcpy(skb_put(skb, DEAUTH_DISASSOC_LEN),
		       mgmt, DEAUTH_DISASSOC_LEN);

		if (!(ifmgd->flags & IEEE80211_STA_MFP_ENABLED))
			IEEE80211_SKB_CB(skb)->flags |=
				IEEE80211_TX_INTFL_DONT_ENCRYPT;
		ieee80211_tx_skb(sdata, skb);
	}
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
			     int powersave)
{
	struct sk_buff *skb;
	struct ieee80211_hdr_3addr *nullfunc;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	skb = ieee80211_nullfunc_get(&local->hw, &sdata->vif);
	if (!skb)
		return;

	nullfunc = (struct ieee80211_hdr_3addr *) skb->data;
	if (powersave)
		nullfunc->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	if (ifmgd->flags & (IEEE80211_STA_BEACON_POLL |
			    IEEE80211_STA_CONNECTION_POLL))
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_USE_MINRATE;

	ieee80211_tx_skb(sdata, skb);
}

static void ieee80211_send_4addr_nullfunc(struct ieee80211_local *local,
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

	nullfunc = (struct ieee80211_hdr *) skb_put(skb, 30);
	memset(nullfunc, 0, 30);
	fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC |
			 IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS);
	nullfunc->frame_control = fc;
	memcpy(nullfunc->addr1, sdata->u.mgd.bssid, ETH_ALEN);
	memcpy(nullfunc->addr2, sdata->vif.addr, ETH_ALEN);
	memcpy(nullfunc->addr3, sdata->u.mgd.bssid, ETH_ALEN);
	memcpy(nullfunc->addr4, sdata->vif.addr, ETH_ALEN);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	ieee80211_tx_skb(sdata, skb);
}

/* spectrum management related things */
static void ieee80211_chswitch_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data, u.mgd.chswitch_work);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (!ieee80211_sdata_running(sdata))
		return;

	mutex_lock(&ifmgd->mtx);
	if (!ifmgd->associated)
		goto out;

	sdata->local->oper_channel = sdata->local->csa_channel;
	if (!sdata->local->ops->channel_switch) {
		/* call "hw_config" only if doing sw channel switch */
		ieee80211_hw_config(sdata->local,
			IEEE80211_CONF_CHANGE_CHANNEL);
	} else {
		/* update the device channel directly */
		sdata->local->hw.conf.channel = sdata->local->oper_channel;
	}

	/* XXX: shouldn't really modify cfg80211-owned data! */
	ifmgd->associated->channel = sdata->local->oper_channel;

	ieee80211_wake_queues_by_reason(&sdata->local->hw,
					IEEE80211_QUEUE_STOP_REASON_CSA);
 out:
	ifmgd->flags &= ~IEEE80211_STA_CSA_RECEIVED;
	mutex_unlock(&ifmgd->mtx);
}

void ieee80211_chswitch_done(struct ieee80211_vif *vif, bool success)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_managed *ifmgd;

	sdata = vif_to_sdata(vif);
	ifmgd = &sdata->u.mgd;

	trace_api_chswitch_done(sdata, success);
	if (!success) {
		/*
		 * If the channel switch was not successful, stay
		 * around on the old channel. We currently lack
		 * good handling of this situation, possibly we
		 * should just drop the association.
		 */
		sdata->local->csa_channel = sdata->local->oper_channel;
	}

	ieee80211_queue_work(&sdata->local->hw, &ifmgd->chswitch_work);
}
EXPORT_SYMBOL(ieee80211_chswitch_done);

static void ieee80211_chswitch_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata =
		(struct ieee80211_sub_if_data *) data;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (sdata->local->quiescing) {
		set_bit(TMR_RUNNING_CHANSW, &ifmgd->timers_running);
		return;
	}

	ieee80211_queue_work(&sdata->local->hw, &ifmgd->chswitch_work);
}

void ieee80211_sta_process_chanswitch(struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_channel_sw_ie *sw_elem,
				      struct ieee80211_bss *bss,
				      u64 timestamp)
{
	struct cfg80211_bss *cbss =
		container_of((void *)bss, struct cfg80211_bss, priv);
	struct ieee80211_channel *new_ch;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	int new_freq = ieee80211_channel_to_frequency(sw_elem->new_ch_num,
						      cbss->channel->band);

	ASSERT_MGD_MTX(ifmgd);

	if (!ifmgd->associated)
		return;

	if (sdata->local->scanning)
		return;

	/* Disregard subsequent beacons if we are already running a timer
	   processing a CSA */

	if (ifmgd->flags & IEEE80211_STA_CSA_RECEIVED)
		return;

	new_ch = ieee80211_get_channel(sdata->local->hw.wiphy, new_freq);
	if (!new_ch || new_ch->flags & IEEE80211_CHAN_DISABLED)
		return;

	sdata->local->csa_channel = new_ch;

	if (sdata->local->ops->channel_switch) {
		/* use driver's channel switch callback */
		struct ieee80211_channel_switch ch_switch;
		memset(&ch_switch, 0, sizeof(ch_switch));
		ch_switch.timestamp = timestamp;
		if (sw_elem->mode) {
			ch_switch.block_tx = true;
			ieee80211_stop_queues_by_reason(&sdata->local->hw,
					IEEE80211_QUEUE_STOP_REASON_CSA);
		}
		ch_switch.channel = new_ch;
		ch_switch.count = sw_elem->count;
		ifmgd->flags |= IEEE80211_STA_CSA_RECEIVED;
		drv_channel_switch(sdata->local, &ch_switch);
		return;
	}

	/* channel switch handled in software */
	if (sw_elem->count <= 1) {
		ieee80211_queue_work(&sdata->local->hw, &ifmgd->chswitch_work);
	} else {
		if (sw_elem->mode)
			ieee80211_stop_queues_by_reason(&sdata->local->hw,
					IEEE80211_QUEUE_STOP_REASON_CSA);
		ifmgd->flags |= IEEE80211_STA_CSA_RECEIVED;
		mod_timer(&ifmgd->chswitch_timer,
			  jiffies +
			  msecs_to_jiffies(sw_elem->count *
					   cbss->beacon_interval));
	}
}

static void ieee80211_handle_pwr_constr(struct ieee80211_sub_if_data *sdata,
					u16 capab_info, u8 *pwr_constr_elem,
					u8 pwr_constr_elem_len)
{
	struct ieee80211_conf *conf = &sdata->local->hw.conf;

	if (!(capab_info & WLAN_CAPABILITY_SPECTRUM_MGMT))
		return;

	/* Power constraint IE length should be 1 octet */
	if (pwr_constr_elem_len != 1)
		return;

	if ((*pwr_constr_elem <= conf->channel->max_reg_power) &&
	    (*pwr_constr_elem != sdata->local->power_constr_level)) {
		sdata->local->power_constr_level = *pwr_constr_elem;
		ieee80211_hw_config(sdata->local, 0);
	}
}

void ieee80211_enable_dyn_ps(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_conf *conf = &local->hw.conf;

	WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION ||
		!(local->hw.flags & IEEE80211_HW_SUPPORTS_PS) ||
		(local->hw.flags & IEEE80211_HW_SUPPORTS_DYNAMIC_PS));

	local->disable_dynamic_ps = false;
	conf->dynamic_ps_timeout = local->dynamic_ps_user_timeout;
}
EXPORT_SYMBOL(ieee80211_enable_dyn_ps);

void ieee80211_disable_dyn_ps(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_conf *conf = &local->hw.conf;

	WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION ||
		!(local->hw.flags & IEEE80211_HW_SUPPORTS_PS) ||
		(local->hw.flags & IEEE80211_HW_SUPPORTS_DYNAMIC_PS));

	local->disable_dynamic_ps = true;
	conf->dynamic_ps_timeout = 0;
	del_timer_sync(&local->dynamic_ps_timer);
	ieee80211_queue_work(&local->hw,
			     &local->dynamic_ps_enable_work);
}
EXPORT_SYMBOL(ieee80211_disable_dyn_ps);

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
	    !(local->hw.flags & IEEE80211_HW_SUPPORTS_DYNAMIC_PS)) {
		mod_timer(&local->dynamic_ps_timer, jiffies +
			  msecs_to_jiffies(conf->dynamic_ps_timeout));
	} else {
		if (local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK)
			ieee80211_send_nullfunc(local, sdata, 1);

		if ((local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK) &&
		    (local->hw.flags & IEEE80211_HW_REPORTS_TX_ACK_STATUS))
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
	struct ieee80211_if_managed *mgd = &sdata->u.mgd;
	struct sta_info *sta = NULL;
	bool authorized = false;

	if (!mgd->powersave)
		return false;

	if (mgd->broken_ap)
		return false;

	if (!mgd->associated)
		return false;

	if (!mgd->associated->beacon_ies)
		return false;

	if (mgd->flags & (IEEE80211_STA_BEACON_POLL |
			  IEEE80211_STA_CONNECTION_POLL))
		return false;

	rcu_read_lock();
	sta = sta_info_get(sdata, mgd->bssid);
	if (sta)
		authorized = test_sta_flag(sta, WLAN_STA_AUTHORIZED);
	rcu_read_unlock();

	return authorized;
}

/* need to hold RTNL or interface lock */
void ieee80211_recalc_ps(struct ieee80211_local *local, s32 latency)
{
	struct ieee80211_sub_if_data *sdata, *found = NULL;
	int count = 0;
	int timeout;

	if (!(local->hw.flags & IEEE80211_HW_SUPPORTS_PS)) {
		local->ps_sdata = NULL;
		return;
	}

	if (!list_empty(&local->work_list)) {
		local->ps_sdata = NULL;
		goto change;
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
		struct ieee80211_conf *conf = &local->hw.conf;
		s32 beaconint_us;

		if (latency < 0)
			latency = pm_qos_request(PM_QOS_NETWORK_LATENCY);

		beaconint_us = ieee80211_tu_to_usec(
					found->vif.bss_conf.beacon_int);

		timeout = local->dynamic_ps_forced_timeout;
		if (timeout < 0) {
			/*
			 * Go to full PSM if the user configures a very low
			 * latency requirement.
			 * The 2000 second value is there for compatibility
			 * until the PM_QOS_NETWORK_LATENCY is configured
			 * with real values.
			 */
			if (latency > (1900 * USEC_PER_MSEC) &&
			    latency != (2000 * USEC_PER_SEC))
				timeout = 0;
			else
				timeout = 100;
		}
		local->dynamic_ps_user_timeout = timeout;
		if (!local->disable_dynamic_ps)
			conf->dynamic_ps_timeout =
				local->dynamic_ps_user_timeout;

		if (beaconint_us > latency) {
			local->ps_sdata = NULL;
		} else {
			struct ieee80211_bss *bss;
			int maxslp = 1;
			u8 dtimper;

			bss = (void *)found->u.mgd.associated->priv;
			dtimper = bss->dtim_period;

			/* If the TIM IE is invalid, pretend the value is 1 */
			if (!dtimper)
				dtimper = 1;
			else if (dtimper > 1)
				maxslp = min_t(int, dtimper,
						    latency / beaconint_us);

			local->hw.conf.max_sleep_period = maxslp;
			local->hw.conf.ps_dtim_period = dtimper;
			local->ps_sdata = found;
		}
	} else {
		local->ps_sdata = NULL;
	}

 change:
	ieee80211_change_ps(local);
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
					IEEE80211_QUEUE_STOP_REASON_PS);
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

	if (!local->disable_dynamic_ps &&
	    local->hw.conf.dynamic_ps_timeout > 0) {
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

	if ((local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK) &&
	    !(ifmgd->flags & IEEE80211_STA_NULLFUNC_ACKED)) {
		netif_tx_stop_all_queues(sdata->dev);

		if (drv_tx_frames_pending(local))
			mod_timer(&local->dynamic_ps_timer, jiffies +
				  msecs_to_jiffies(
				  local->hw.conf.dynamic_ps_timeout));
		else {
			ieee80211_send_nullfunc(local, sdata, 1);
			/* Flush to get the tx status of nullfunc frame */
			drv_flush(local, false);
		}
	}

	if (!((local->hw.flags & IEEE80211_HW_REPORTS_TX_ACK_STATUS) &&
	      (local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK)) ||
	    (ifmgd->flags & IEEE80211_STA_NULLFUNC_ACKED)) {
		ifmgd->flags &= ~IEEE80211_STA_NULLFUNC_ACKED;
		local->hw.conf.flags |= IEEE80211_CONF_PS;
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
	}

	if (local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK)
		netif_tx_wake_all_queues(sdata->dev);
}

void ieee80211_dynamic_ps_timer(unsigned long data)
{
	struct ieee80211_local *local = (void *) data;

	if (local->quiescing || local->suspended)
		return;

	ieee80211_queue_work(&local->hw, &local->dynamic_ps_enable_work);
}

/* MLME */
static void ieee80211_sta_wmm_params(struct ieee80211_local *local,
				     struct ieee80211_sub_if_data *sdata,
				     u8 *wmm_param, size_t wmm_param_len)
{
	struct ieee80211_tx_queue_params params;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	size_t left;
	int count;
	u8 *pos, uapsd_queues = 0;

	if (!local->ops->conf_tx)
		return;

	if (local->hw.queues < 4)
		return;

	if (!wmm_param)
		return;

	if (wmm_param_len < 8 || wmm_param[5] /* version */ != 1)
		return;

	if (ifmgd->flags & IEEE80211_STA_UAPSD_ENABLED)
		uapsd_queues = local->uapsd_queues;

	count = wmm_param[6] & 0x0f;
	if (count == ifmgd->wmm_last_param_set)
		return;
	ifmgd->wmm_last_param_set = count;

	pos = wmm_param + 8;
	left = wmm_param_len - 8;

	memset(&params, 0, sizeof(params));

	local->wmm_acm = 0;
	for (; left >= 4; left -= 4, pos += 4) {
		int aci = (pos[0] >> 5) & 0x03;
		int acm = (pos[0] >> 4) & 0x01;
		bool uapsd = false;
		int queue;

		switch (aci) {
		case 1: /* AC_BK */
			queue = 3;
			if (acm)
				local->wmm_acm |= BIT(1) | BIT(2); /* BK/- */
			if (uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BK)
				uapsd = true;
			break;
		case 2: /* AC_VI */
			queue = 1;
			if (acm)
				local->wmm_acm |= BIT(4) | BIT(5); /* CL/VI */
			if (uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VI)
				uapsd = true;
			break;
		case 3: /* AC_VO */
			queue = 0;
			if (acm)
				local->wmm_acm |= BIT(6) | BIT(7); /* VO/NC */
			if (uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_VO)
				uapsd = true;
			break;
		case 0: /* AC_BE */
		default:
			queue = 2;
			if (acm)
				local->wmm_acm |= BIT(0) | BIT(3); /* BE/EE */
			if (uapsd_queues & IEEE80211_WMM_IE_STA_QOSINFO_AC_BE)
				uapsd = true;
			break;
		}

		params.aifs = pos[0] & 0x0f;
		params.cw_max = ecw2cw((pos[1] & 0xf0) >> 4);
		params.cw_min = ecw2cw(pos[1] & 0x0f);
		params.txop = get_unaligned_le16(pos + 2);
		params.uapsd = uapsd;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		wiphy_debug(local->hw.wiphy,
			    "WMM queue=%d aci=%d acm=%d aifs=%d "
			    "cWmin=%d cWmax=%d txop=%d uapsd=%d\n",
			    queue, aci, acm,
			    params.aifs, params.cw_min, params.cw_max,
			    params.txop, params.uapsd);
#endif
		sdata->tx_conf[queue] = params;
		if (drv_conf_tx(local, sdata, queue, &params))
			wiphy_debug(local->hw.wiphy,
				    "failed to set TX queue parameters for queue %d\n",
				    queue);
	}

	/* enable WMM or activate new settings */
	sdata->vif.bss_conf.qos = true;
	ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_QOS);
}

static u32 ieee80211_handle_bss_capability(struct ieee80211_sub_if_data *sdata,
					   u16 capab, bool erp_valid, u8 erp)
{
	struct ieee80211_bss_conf *bss_conf = &sdata->vif.bss_conf;
	u32 changed = 0;
	bool use_protection;
	bool use_short_preamble;
	bool use_short_slot;

	if (erp_valid) {
		use_protection = (erp & WLAN_ERP_USE_PROTECTION) != 0;
		use_short_preamble = (erp & WLAN_ERP_BARKER_PREAMBLE) == 0;
	} else {
		use_protection = false;
		use_short_preamble = !!(capab & WLAN_CAPABILITY_SHORT_PREAMBLE);
	}

	use_short_slot = !!(capab & WLAN_CAPABILITY_SHORT_SLOT_TIME);
	if (sdata->local->hw.conf.channel->band == IEEE80211_BAND_5GHZ)
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

static void ieee80211_set_associated(struct ieee80211_sub_if_data *sdata,
				     struct cfg80211_bss *cbss,
				     u32 bss_info_changed)
{
	struct ieee80211_bss *bss = (void *)cbss->priv;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_bss_conf *bss_conf = &sdata->vif.bss_conf;

	bss_info_changed |= BSS_CHANGED_ASSOC;
	/* set timing information */
	bss_conf->beacon_int = cbss->beacon_interval;
	bss_conf->timestamp = cbss->tsf;

	bss_info_changed |= BSS_CHANGED_BEACON_INT;
	bss_info_changed |= ieee80211_handle_bss_capability(sdata,
		cbss->capability, bss->has_erp_value, bss->erp_value);

	sdata->u.mgd.beacon_timeout = usecs_to_jiffies(ieee80211_tu_to_usec(
		IEEE80211_BEACON_LOSS_COUNT * bss_conf->beacon_int));

	sdata->u.mgd.associated = cbss;
	memcpy(sdata->u.mgd.bssid, cbss->bssid, ETH_ALEN);

	sdata->u.mgd.flags |= IEEE80211_STA_RESET_SIGNAL_AVE;

	/* just to be sure */
	sdata->u.mgd.flags &= ~(IEEE80211_STA_CONNECTION_POLL |
				IEEE80211_STA_BEACON_POLL);

	ieee80211_led_assoc(local, 1);

	if (local->hw.flags & IEEE80211_HW_NEED_DTIM_PERIOD)
		bss_conf->dtim_period = bss->dtim_period;
	else
		bss_conf->dtim_period = 0;

	bss_conf->assoc = 1;
	/*
	 * For now just always ask the driver to update the basic rateset
	 * when we have associated, we aren't checking whether it actually
	 * changed or not.
	 */
	bss_info_changed |= BSS_CHANGED_BASIC_RATES;

	/* And the BSSID changed - we're associated now */
	bss_info_changed |= BSS_CHANGED_BSSID;

	/* Tell the driver to monitor connection quality (if supported) */
	if (sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_CQM_RSSI &&
	    bss_conf->cqm_rssi_thold)
		bss_info_changed |= BSS_CHANGED_CQM;

	/* Enable ARP filtering */
	if (bss_conf->arp_filter_enabled != sdata->arp_filter_state) {
		bss_conf->arp_filter_enabled = sdata->arp_filter_state;
		bss_info_changed |= BSS_CHANGED_ARP_FILTER;
	}

	ieee80211_bss_info_change_notify(sdata, bss_info_changed);

	mutex_lock(&local->iflist_mtx);
	ieee80211_recalc_ps(local, -1);
	ieee80211_recalc_smps(local);
	mutex_unlock(&local->iflist_mtx);

	netif_tx_start_all_queues(sdata->dev);
	netif_carrier_on(sdata->dev);
}

static void ieee80211_set_disassoc(struct ieee80211_sub_if_data *sdata,
				   bool remove_sta, bool tx)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	u32 changed = 0, config_changed = 0;
	u8 bssid[ETH_ALEN];

	ASSERT_MGD_MTX(ifmgd);

	if (WARN_ON(!ifmgd->associated))
		return;

	memcpy(bssid, ifmgd->associated->bssid, ETH_ALEN);

	ifmgd->associated = NULL;
	memset(ifmgd->bssid, 0, ETH_ALEN);

	/*
	 * we need to commit the associated = NULL change because the
	 * scan code uses that to determine whether this iface should
	 * go to/wake up from powersave or not -- and could otherwise
	 * wake the queues erroneously.
	 */
	smp_mb();

	/*
	 * Thus, we can only afterwards stop the queues -- to account
	 * for the case where another CPU is finishing a scan at this
	 * time -- we don't want the scan code to enable queues.
	 */

	netif_tx_stop_all_queues(sdata->dev);
	netif_carrier_off(sdata->dev);

	mutex_lock(&local->sta_mtx);
	sta = sta_info_get(sdata, bssid);
	if (sta) {
		set_sta_flag(sta, WLAN_STA_BLOCK_BA);
		ieee80211_sta_tear_down_BA_sessions(sta, tx);
	}
	mutex_unlock(&local->sta_mtx);

	changed |= ieee80211_reset_erp_info(sdata);

	ieee80211_led_assoc(local, 0);
	changed |= BSS_CHANGED_ASSOC;
	sdata->vif.bss_conf.assoc = false;

	ieee80211_set_wmm_default(sdata);

	/* channel(_type) changes are handled by ieee80211_hw_config */
	WARN_ON(!ieee80211_set_channel_type(local, sdata, NL80211_CHAN_NO_HT));

	/* on the next assoc, re-program HT parameters */
	sdata->ht_opmode_valid = false;
	memset(&ifmgd->ht_capa, 0, sizeof(ifmgd->ht_capa));
	memset(&ifmgd->ht_capa_mask, 0, sizeof(ifmgd->ht_capa_mask));

	local->power_constr_level = 0;

	del_timer_sync(&local->dynamic_ps_timer);
	cancel_work_sync(&local->dynamic_ps_enable_work);

	if (local->hw.conf.flags & IEEE80211_CONF_PS) {
		local->hw.conf.flags &= ~IEEE80211_CONF_PS;
		config_changed |= IEEE80211_CONF_CHANGE_PS;
	}
	local->ps_sdata = NULL;

	ieee80211_hw_config(local, config_changed);

	/* Disable ARP filtering */
	if (sdata->vif.bss_conf.arp_filter_enabled) {
		sdata->vif.bss_conf.arp_filter_enabled = false;
		changed |= BSS_CHANGED_ARP_FILTER;
	}

	/* The BSSID (not really interesting) and HT changed */
	changed |= BSS_CHANGED_BSSID | BSS_CHANGED_HT;
	ieee80211_bss_info_change_notify(sdata, changed);

	/* remove AP and TDLS peers */
	if (remove_sta)
		sta_info_flush(local, sdata);

	del_timer_sync(&sdata->u.mgd.conn_mon_timer);
	del_timer_sync(&sdata->u.mgd.bcn_mon_timer);
	del_timer_sync(&sdata->u.mgd.timer);
	del_timer_sync(&sdata->u.mgd.chswitch_timer);
}

void ieee80211_sta_rx_notify(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_hdr *hdr)
{
	/*
	 * We can postpone the mgd.timer whenever receiving unicast frames
	 * from AP because we know that the connection is working both ways
	 * at that time. But multicast frames (and hence also beacons) must
	 * be ignored here, because we need to trigger the timer during
	 * data idle periods for sending the periodic probe request to the
	 * AP we're connected to.
	 */
	if (is_multicast_ether_addr(hdr->addr1))
		return;

	ieee80211_sta_reset_conn_monitor(sdata);
}

static void ieee80211_reset_ap_probe(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (!(ifmgd->flags & (IEEE80211_STA_BEACON_POLL |
			      IEEE80211_STA_CONNECTION_POLL)))
	    return;

	ifmgd->flags &= ~(IEEE80211_STA_CONNECTION_POLL |
			  IEEE80211_STA_BEACON_POLL);
	mutex_lock(&sdata->local->iflist_mtx);
	ieee80211_recalc_ps(sdata->local, -1);
	mutex_unlock(&sdata->local->iflist_mtx);

	if (sdata->local->hw.flags & IEEE80211_HW_CONNECTION_MONITOR)
		return;

	/*
	 * We've received a probe response, but are not sure whether
	 * we have or will be receiving any beacons or data, so let's
	 * schedule the timers again, just in case.
	 */
	ieee80211_sta_reset_beacon_monitor(sdata);

	mod_timer(&ifmgd->conn_mon_timer,
		  round_jiffies_up(jiffies +
				   IEEE80211_CONNECTION_IDLE_TIME));
}

void ieee80211_sta_tx_notify(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_hdr *hdr, bool ack)
{
	if (!ieee80211_is_data(hdr->frame_control))
	    return;

	if (ack)
		ieee80211_sta_reset_conn_monitor(sdata);

	if (ieee80211_is_nullfunc(hdr->frame_control) &&
	    sdata->u.mgd.probe_send_count > 0) {
		if (ack)
			sdata->u.mgd.probe_send_count = 0;
		else
			sdata->u.mgd.nullfunc_failed = true;
		ieee80211_queue_work(&sdata->local->hw, &sdata->work);
	}
}

static void ieee80211_mgd_probe_ap_send(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	const u8 *ssid;
	u8 *dst = ifmgd->associated->bssid;
	u8 unicast_limit = max(1, max_probe_tries - 3);

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
	if (sdata->local->hw.flags & IEEE80211_HW_REPORTS_TX_ACK_STATUS) {
		ifmgd->nullfunc_failed = false;
		ieee80211_send_nullfunc(sdata->local, sdata, 0);
	} else {
		ssid = ieee80211_bss_get_ie(ifmgd->associated, WLAN_EID_SSID);
		ieee80211_send_probe_req(sdata, dst, ssid + 2, ssid[1], NULL, 0,
					 (u32) -1, true, false);
	}

	ifmgd->probe_send_count++;
	ifmgd->probe_timeout = jiffies + msecs_to_jiffies(probe_wait_ms);
	run_again(ifmgd, ifmgd->probe_timeout);
}

static void ieee80211_mgd_probe_ap(struct ieee80211_sub_if_data *sdata,
				   bool beacon)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	bool already = false;

	if (!ieee80211_sdata_running(sdata))
		return;

	if (sdata->local->scanning)
		return;

	if (sdata->local->tmp_channel)
		return;

	mutex_lock(&ifmgd->mtx);

	if (!ifmgd->associated)
		goto out;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	if (beacon && net_ratelimit())
		printk(KERN_DEBUG "%s: detected beacon loss from AP "
		       "- sending probe request\n", sdata->name);
#endif

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
	if (ifmgd->flags & (IEEE80211_STA_BEACON_POLL |
			    IEEE80211_STA_CONNECTION_POLL))
		already = true;

	if (beacon)
		ifmgd->flags |= IEEE80211_STA_BEACON_POLL;
	else
		ifmgd->flags |= IEEE80211_STA_CONNECTION_POLL;

	if (already)
		goto out;

	mutex_lock(&sdata->local->iflist_mtx);
	ieee80211_recalc_ps(sdata->local, -1);
	mutex_unlock(&sdata->local->iflist_mtx);

	ifmgd->probe_send_count = 0;
	ieee80211_mgd_probe_ap_send(sdata);
 out:
	mutex_unlock(&ifmgd->mtx);
}

struct sk_buff *ieee80211_ap_probereq_get(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct sk_buff *skb;
	const u8 *ssid;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return NULL;

	ASSERT_MGD_MTX(ifmgd);

	if (!ifmgd->associated)
		return NULL;

	ssid = ieee80211_bss_get_ie(ifmgd->associated, WLAN_EID_SSID);
	skb = ieee80211_build_probe_req(sdata, ifmgd->associated->bssid,
					(u32) -1, ssid + 2, ssid[1],
					NULL, 0, true);

	return skb;
}
EXPORT_SYMBOL(ieee80211_ap_probereq_get);

static void __ieee80211_connection_loss(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	u8 bssid[ETH_ALEN];
	u8 frame_buf[DEAUTH_DISASSOC_LEN];

	mutex_lock(&ifmgd->mtx);
	if (!ifmgd->associated) {
		mutex_unlock(&ifmgd->mtx);
		return;
	}

	memcpy(bssid, ifmgd->associated->bssid, ETH_ALEN);

	printk(KERN_DEBUG "%s: Connection to AP %pM lost.\n",
	       sdata->name, bssid);

	ieee80211_set_disassoc(sdata, true, false);
	mutex_unlock(&ifmgd->mtx);

	/*
	 * must be outside lock due to cfg80211,
	 * but that's not a problem.
	 */
	ieee80211_send_deauth_disassoc(sdata, bssid,
				       IEEE80211_STYPE_DEAUTH,
				       WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY,
				       false, frame_buf);
	cfg80211_send_deauth(sdata->dev, frame_buf, DEAUTH_DISASSOC_LEN);

	mutex_lock(&local->mtx);
	ieee80211_recalc_idle(local);
	mutex_unlock(&local->mtx);
}

void ieee80211_beacon_connection_loss_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data,
			     u.mgd.beacon_connection_loss_work);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct sta_info *sta;

	if (ifmgd->associated) {
		rcu_read_lock();
		sta = sta_info_get(sdata, ifmgd->bssid);
		if (sta)
			sta->beacon_loss_count++;
		rcu_read_unlock();
	}

	if (sdata->local->hw.flags & IEEE80211_HW_CONNECTION_MONITOR)
		__ieee80211_connection_loss(sdata);
	else
		ieee80211_mgd_probe_ap(sdata, true);
}

void ieee80211_beacon_loss(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_hw *hw = &sdata->local->hw;

	trace_api_beacon_loss(sdata);

	WARN_ON(hw->flags & IEEE80211_HW_CONNECTION_MONITOR);
	ieee80211_queue_work(hw, &sdata->u.mgd.beacon_connection_loss_work);
}
EXPORT_SYMBOL(ieee80211_beacon_loss);

void ieee80211_connection_loss(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_hw *hw = &sdata->local->hw;

	trace_api_connection_loss(sdata);

	WARN_ON(!(hw->flags & IEEE80211_HW_CONNECTION_MONITOR));
	ieee80211_queue_work(hw, &sdata->u.mgd.beacon_connection_loss_work);
}
EXPORT_SYMBOL(ieee80211_connection_loss);


static void ieee80211_destroy_auth_data(struct ieee80211_sub_if_data *sdata,
					bool assoc)
{
	struct ieee80211_mgd_auth_data *auth_data = sdata->u.mgd.auth_data;

	lockdep_assert_held(&sdata->u.mgd.mtx);

	if (auth_data->synced)
		drv_finish_tx_sync(sdata->local, sdata,
				   auth_data->bss->bssid,
				   IEEE80211_TX_SYNC_AUTH);

	if (!assoc) {
		sta_info_destroy_addr(sdata, auth_data->bss->bssid);

		memset(sdata->u.mgd.bssid, 0, ETH_ALEN);
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_BSSID);
	}

	cfg80211_put_bss(auth_data->bss);
	kfree(auth_data);
	sdata->u.mgd.auth_data = NULL;
}

static void ieee80211_auth_challenge(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_mgd_auth_data *auth_data = sdata->u.mgd.auth_data;
	u8 *pos;
	struct ieee802_11_elems elems;

	pos = mgmt->u.auth.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), &elems);
	if (!elems.challenge)
		return;
	auth_data->expected_transaction = 4;
	ieee80211_send_auth(sdata, 3, auth_data->algorithm,
			    elems.challenge - 2, elems.challenge_len + 2,
			    auth_data->bss->bssid, auth_data->bss->bssid,
			    auth_data->key, auth_data->key_len,
			    auth_data->key_idx);
}

static enum rx_mgmt_action __must_check
ieee80211_rx_mgmt_auth(struct ieee80211_sub_if_data *sdata,
		       struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 bssid[ETH_ALEN];
	u16 auth_alg, auth_transaction, status_code;
	struct sta_info *sta;

	lockdep_assert_held(&ifmgd->mtx);

	if (len < 24 + 6)
		return RX_MGMT_NONE;

	if (!ifmgd->auth_data || ifmgd->auth_data->done)
		return RX_MGMT_NONE;

	memcpy(bssid, ifmgd->auth_data->bss->bssid, ETH_ALEN);

	if (memcmp(bssid, mgmt->bssid, ETH_ALEN))
		return RX_MGMT_NONE;

	auth_alg = le16_to_cpu(mgmt->u.auth.auth_alg);
	auth_transaction = le16_to_cpu(mgmt->u.auth.auth_transaction);
	status_code = le16_to_cpu(mgmt->u.auth.status_code);

	if (auth_alg != ifmgd->auth_data->algorithm ||
	    auth_transaction != ifmgd->auth_data->expected_transaction)
		return RX_MGMT_NONE;

	if (status_code != WLAN_STATUS_SUCCESS) {
		printk(KERN_DEBUG "%s: %pM denied authentication (status %d)\n",
		       sdata->name, mgmt->sa, status_code);
		goto out;
	}

	switch (ifmgd->auth_data->algorithm) {
	case WLAN_AUTH_OPEN:
	case WLAN_AUTH_LEAP:
	case WLAN_AUTH_FT:
		break;
	case WLAN_AUTH_SHARED_KEY:
		if (ifmgd->auth_data->expected_transaction != 4) {
			ieee80211_auth_challenge(sdata, mgmt, len);
			/* need another frame */
			return RX_MGMT_NONE;
		}
		break;
	default:
		WARN_ONCE(1, "invalid auth alg %d",
			  ifmgd->auth_data->algorithm);
		return RX_MGMT_NONE;
	}

	printk(KERN_DEBUG "%s: authenticated\n", sdata->name);
 out:
	if (ifmgd->auth_data->synced)
		drv_finish_tx_sync(sdata->local, sdata, bssid,
				   IEEE80211_TX_SYNC_AUTH);
	ifmgd->auth_data->synced = false;
	ifmgd->auth_data->done = true;
	ifmgd->auth_data->timeout = jiffies + IEEE80211_AUTH_WAIT_ASSOC;
	run_again(ifmgd, ifmgd->auth_data->timeout);

	/* move station state to auth */
	mutex_lock(&sdata->local->sta_mtx);
	sta = sta_info_get(sdata, bssid);
	if (!sta) {
		WARN_ONCE(1, "%s: STA %pM not found", sdata->name, bssid);
		goto out_err;
	}
	if (sta_info_move_state(sta, IEEE80211_STA_AUTH)) {
		printk(KERN_DEBUG "%s: failed moving %pM to auth\n",
		       sdata->name, bssid);
		goto out_err;
	}
	mutex_unlock(&sdata->local->sta_mtx);

	return RX_MGMT_CFG80211_RX_AUTH;
 out_err:
	mutex_unlock(&sdata->local->sta_mtx);
	/* ignore frame -- wait for timeout */
	return RX_MGMT_NONE;
}


static enum rx_mgmt_action __must_check
ieee80211_rx_mgmt_deauth(struct ieee80211_sub_if_data *sdata,
			 struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	const u8 *bssid = NULL;
	u16 reason_code;

	lockdep_assert_held(&ifmgd->mtx);

	if (len < 24 + 2)
		return RX_MGMT_NONE;

	if (!ifmgd->associated ||
	    memcmp(mgmt->bssid, ifmgd->associated->bssid, ETH_ALEN))
		return RX_MGMT_NONE;

	bssid = ifmgd->associated->bssid;

	reason_code = le16_to_cpu(mgmt->u.deauth.reason_code);

	printk(KERN_DEBUG "%s: deauthenticated from %pM (Reason: %u)\n",
			sdata->name, bssid, reason_code);

	ieee80211_set_disassoc(sdata, true, false);
	mutex_lock(&sdata->local->mtx);
	ieee80211_recalc_idle(sdata->local);
	mutex_unlock(&sdata->local->mtx);

	return RX_MGMT_CFG80211_DEAUTH;
}


static enum rx_mgmt_action __must_check
ieee80211_rx_mgmt_disassoc(struct ieee80211_sub_if_data *sdata,
			   struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u16 reason_code;

	lockdep_assert_held(&ifmgd->mtx);

	if (len < 24 + 2)
		return RX_MGMT_NONE;

	if (!ifmgd->associated ||
	    memcmp(mgmt->bssid, ifmgd->associated->bssid, ETH_ALEN))
		return RX_MGMT_NONE;

	reason_code = le16_to_cpu(mgmt->u.disassoc.reason_code);

	printk(KERN_DEBUG "%s: disassociated from %pM (Reason: %u)\n",
			sdata->name, mgmt->sa, reason_code);

	ieee80211_set_disassoc(sdata, true, false);
	mutex_lock(&sdata->local->mtx);
	ieee80211_recalc_idle(sdata->local);
	mutex_unlock(&sdata->local->mtx);
	return RX_MGMT_CFG80211_DISASSOC;
}

static void ieee80211_get_rates(struct ieee80211_supported_band *sband,
				u8 *supp_rates, unsigned int supp_rates_len,
				u32 *rates, u32 *basic_rates,
				bool *have_higher_than_11mbit,
				int *min_rate, int *min_rate_index)
{
	int i, j;

	for (i = 0; i < supp_rates_len; i++) {
		int rate = (supp_rates[i] & 0x7f) * 5;
		bool is_basic = !!(supp_rates[i] & 0x80);

		if (rate > 110)
			*have_higher_than_11mbit = true;

		/*
		 * BSS_MEMBERSHIP_SELECTOR_HT_PHY is defined in 802.11n-2009
		 * 7.3.2.2 as a magic value instead of a rate. Hence, skip it.
		 *
		 * Note: Even through the membership selector and the basic
		 *	 rate flag share the same bit, they are not exactly
		 *	 the same.
		 */
		if (!!(supp_rates[i] & 0x80) &&
		    (supp_rates[i] & 0x7f) == BSS_MEMBERSHIP_SELECTOR_HT_PHY)
			continue;

		for (j = 0; j < sband->n_bitrates; j++) {
			if (sband->bitrates[j].bitrate == rate) {
				*rates |= BIT(j);
				if (is_basic)
					*basic_rates |= BIT(j);
				if (rate < *min_rate) {
					*min_rate = rate;
					*min_rate_index = j;
				}
				break;
			}
		}
	}
}

static void ieee80211_destroy_assoc_data(struct ieee80211_sub_if_data *sdata,
					 bool assoc)
{
	struct ieee80211_mgd_assoc_data *assoc_data = sdata->u.mgd.assoc_data;

	lockdep_assert_held(&sdata->u.mgd.mtx);

	if (assoc_data->synced)
		drv_finish_tx_sync(sdata->local, sdata,
				   assoc_data->bss->bssid,
				   IEEE80211_TX_SYNC_ASSOC);

	if (!assoc) {
		sta_info_destroy_addr(sdata, assoc_data->bss->bssid);

		memset(sdata->u.mgd.bssid, 0, ETH_ALEN);
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_BSSID);
	}

	kfree(assoc_data);
	sdata->u.mgd.assoc_data = NULL;
}

static bool ieee80211_assoc_success(struct ieee80211_sub_if_data *sdata,
				    struct cfg80211_bss *cbss,
				    struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	struct sta_info *sta;
	u8 *pos;
	u32 rates, basic_rates;
	u16 capab_info, aid;
	struct ieee802_11_elems elems;
	struct ieee80211_bss_conf *bss_conf = &sdata->vif.bss_conf;
	u32 changed = 0;
	int err;
	bool have_higher_than_11mbit = false;
	u16 ap_ht_cap_flags;
	int min_rate = INT_MAX, min_rate_index = -1;

	/* AssocResp and ReassocResp have identical structure */

	aid = le16_to_cpu(mgmt->u.assoc_resp.aid);
	capab_info = le16_to_cpu(mgmt->u.assoc_resp.capab_info);

	if ((aid & (BIT(15) | BIT(14))) != (BIT(15) | BIT(14)))
		printk(KERN_DEBUG
		       "%s: invalid AID value 0x%x; bits 15:14 not set\n",
		       sdata->name, aid);
	aid &= ~(BIT(15) | BIT(14));

	ifmgd->broken_ap = false;

	if (aid == 0 || aid > IEEE80211_MAX_AID) {
		printk(KERN_DEBUG
		       "%s: invalid AID value %d (out of range), turn off PS\n",
		       sdata->name, aid);
		aid = 0;
		ifmgd->broken_ap = true;
	}

	pos = mgmt->u.assoc_resp.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), &elems);

	if (!elems.supp_rates) {
		printk(KERN_DEBUG "%s: no SuppRates element in AssocResp\n",
		       sdata->name);
		return false;
	}

	ifmgd->aid = aid;

	mutex_lock(&sdata->local->sta_mtx);
	/*
	 * station info was already allocated and inserted before
	 * the association and should be available to us
	 */
	sta = sta_info_get(sdata, cbss->bssid);
	if (WARN_ON(!sta)) {
		mutex_unlock(&sdata->local->sta_mtx);
		return false;
	}

	rates = 0;
	basic_rates = 0;
	sband = local->hw.wiphy->bands[local->oper_channel->band];

	ieee80211_get_rates(sband, elems.supp_rates, elems.supp_rates_len,
			    &rates, &basic_rates, &have_higher_than_11mbit,
			    &min_rate, &min_rate_index);

	ieee80211_get_rates(sband, elems.ext_supp_rates,
			    elems.ext_supp_rates_len, &rates, &basic_rates,
			    &have_higher_than_11mbit,
			    &min_rate, &min_rate_index);

	/*
	 * some buggy APs don't advertise basic_rates. use the lowest
	 * supported rate instead.
	 */
	if (unlikely(!basic_rates) && min_rate_index >= 0) {
		printk(KERN_DEBUG "%s: No basic rates in AssocResp. "
		       "Using min supported rate instead.\n", sdata->name);
		basic_rates = BIT(min_rate_index);
	}

	sta->sta.supp_rates[local->oper_channel->band] = rates;
	sdata->vif.bss_conf.basic_rates = basic_rates;

	/* cf. IEEE 802.11 9.2.12 */
	if (local->oper_channel->band == IEEE80211_BAND_2GHZ &&
	    have_higher_than_11mbit)
		sdata->flags |= IEEE80211_SDATA_OPERATING_GMODE;
	else
		sdata->flags &= ~IEEE80211_SDATA_OPERATING_GMODE;

	if (elems.ht_cap_elem && !(ifmgd->flags & IEEE80211_STA_DISABLE_11N))
		ieee80211_ht_cap_ie_to_sta_ht_cap(sdata, sband,
				elems.ht_cap_elem, &sta->sta.ht_cap);

	ap_ht_cap_flags = sta->sta.ht_cap.cap;

	rate_control_rate_init(sta);

	if (ifmgd->flags & IEEE80211_STA_MFP_ENABLED)
		set_sta_flag(sta, WLAN_STA_MFP);

	if (elems.wmm_param)
		set_sta_flag(sta, WLAN_STA_WME);

	err = sta_info_move_state(sta, IEEE80211_STA_AUTH);
	if (!err)
		err = sta_info_move_state(sta, IEEE80211_STA_ASSOC);
	if (!err && !(ifmgd->flags & IEEE80211_STA_CONTROL_PORT))
		err = sta_info_move_state(sta, IEEE80211_STA_AUTHORIZED);
	if (err) {
		printk(KERN_DEBUG
		       "%s: failed to move station %pM to desired state\n",
		       sdata->name, sta->sta.addr);
		WARN_ON(__sta_info_destroy(sta));
		mutex_unlock(&sdata->local->sta_mtx);
		return false;
	}

	mutex_unlock(&sdata->local->sta_mtx);

	/*
	 * Always handle WMM once after association regardless
	 * of the first value the AP uses. Setting -1 here has
	 * that effect because the AP values is an unsigned
	 * 4-bit value.
	 */
	ifmgd->wmm_last_param_set = -1;

	if (elems.wmm_param)
		ieee80211_sta_wmm_params(local, sdata, elems.wmm_param,
					 elems.wmm_param_len);
	else
		ieee80211_set_wmm_default(sdata);

	if (elems.ht_info_elem && elems.wmm_param &&
	    (sdata->local->hw.queues >= 4) &&
	    !(ifmgd->flags & IEEE80211_STA_DISABLE_11N))
		changed |= ieee80211_enable_ht(sdata, elems.ht_info_elem,
					       cbss->bssid, ap_ht_cap_flags,
					       false);

	/* set AID and assoc capability,
	 * ieee80211_set_associated() will tell the driver */
	bss_conf->aid = aid;
	bss_conf->assoc_capability = capab_info;
	ieee80211_set_associated(sdata, cbss, changed);

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
	ieee80211_sta_rx_notify(sdata, (struct ieee80211_hdr *)mgmt);
	ieee80211_sta_reset_beacon_monitor(sdata);

	return true;
}

static enum rx_mgmt_action __must_check
ieee80211_rx_mgmt_assoc_resp(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_mgmt *mgmt, size_t len,
			     struct cfg80211_bss **bss)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_assoc_data *assoc_data = ifmgd->assoc_data;
	u16 capab_info, status_code, aid;
	struct ieee802_11_elems elems;
	u8 *pos;
	bool reassoc;

	lockdep_assert_held(&ifmgd->mtx);

	if (!assoc_data)
		return RX_MGMT_NONE;
	if (memcmp(assoc_data->bss->bssid, mgmt->bssid, ETH_ALEN))
		return RX_MGMT_NONE;

	/*
	 * AssocResp and ReassocResp have identical structure, so process both
	 * of them in this function.
	 */

	if (len < 24 + 6)
		return RX_MGMT_NONE;

	reassoc = ieee80211_is_reassoc_req(mgmt->frame_control);
	capab_info = le16_to_cpu(mgmt->u.assoc_resp.capab_info);
	status_code = le16_to_cpu(mgmt->u.assoc_resp.status_code);
	aid = le16_to_cpu(mgmt->u.assoc_resp.aid);

	printk(KERN_DEBUG "%s: RX %sssocResp from %pM (capab=0x%x "
	       "status=%d aid=%d)\n",
	       sdata->name, reassoc ? "Rea" : "A", mgmt->sa,
	       capab_info, status_code, (u16)(aid & ~(BIT(15) | BIT(14))));

	pos = mgmt->u.assoc_resp.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), &elems);

	if (status_code == WLAN_STATUS_ASSOC_REJECTED_TEMPORARILY &&
	    elems.timeout_int && elems.timeout_int_len == 5 &&
	    elems.timeout_int[0] == WLAN_TIMEOUT_ASSOC_COMEBACK) {
		u32 tu, ms;
		tu = get_unaligned_le32(elems.timeout_int + 1);
		ms = tu * 1024 / 1000;
		printk(KERN_DEBUG "%s: %pM rejected association temporarily; "
		       "comeback duration %u TU (%u ms)\n",
		       sdata->name, mgmt->sa, tu, ms);
		assoc_data->timeout = jiffies + msecs_to_jiffies(ms);
		if (ms > IEEE80211_ASSOC_TIMEOUT)
			run_again(ifmgd, assoc_data->timeout);
		return RX_MGMT_NONE;
	}

	*bss = assoc_data->bss;

	if (status_code != WLAN_STATUS_SUCCESS) {
		printk(KERN_DEBUG "%s: %pM denied association (code=%d)\n",
		       sdata->name, mgmt->sa, status_code);
		ieee80211_destroy_assoc_data(sdata, false);
	} else {
		printk(KERN_DEBUG "%s: associated\n", sdata->name);

		/* tell driver about sync done first */
		if (assoc_data->synced) {
			drv_finish_tx_sync(sdata->local, sdata,
					   assoc_data->bss->bssid,
					   IEEE80211_TX_SYNC_ASSOC);
			assoc_data->synced = false;
		}

		if (!ieee80211_assoc_success(sdata, *bss, mgmt, len)) {
			/* oops -- internal error -- send timeout for now */
			ieee80211_destroy_assoc_data(sdata, true);
			sta_info_destroy_addr(sdata, mgmt->bssid);
			cfg80211_put_bss(*bss);
			return RX_MGMT_CFG80211_ASSOC_TIMEOUT;
		}

		/*
		 * destroy assoc_data afterwards, as otherwise an idle
		 * recalc after assoc_data is NULL but before associated
		 * is set can cause the interface to go idle
		 */
		ieee80211_destroy_assoc_data(sdata, true);
	}

	return RX_MGMT_CFG80211_RX_ASSOC;
}
static void ieee80211_rx_bss_info(struct ieee80211_sub_if_data *sdata,
				  struct ieee80211_mgmt *mgmt,
				  size_t len,
				  struct ieee80211_rx_status *rx_status,
				  struct ieee802_11_elems *elems,
				  bool beacon)
{
	struct ieee80211_local *local = sdata->local;
	int freq;
	struct ieee80211_bss *bss;
	struct ieee80211_channel *channel;
	bool need_ps = false;

	if (sdata->u.mgd.associated &&
	    memcmp(mgmt->bssid, sdata->u.mgd.associated->bssid,
		   ETH_ALEN) == 0) {
		bss = (void *)sdata->u.mgd.associated->priv;
		/* not previously set so we may need to recalc */
		need_ps = !bss->dtim_period;
	}

	if (elems->ds_params && elems->ds_params_len == 1)
		freq = ieee80211_channel_to_frequency(elems->ds_params[0],
						      rx_status->band);
	else
		freq = rx_status->freq;

	channel = ieee80211_get_channel(local->hw.wiphy, freq);

	if (!channel || channel->flags & IEEE80211_CHAN_DISABLED)
		return;

	bss = ieee80211_bss_info_update(local, rx_status, mgmt, len, elems,
					channel, beacon);
	if (bss)
		ieee80211_rx_bss_put(local, bss);

	if (!sdata->u.mgd.associated)
		return;

	if (need_ps) {
		mutex_lock(&local->iflist_mtx);
		ieee80211_recalc_ps(local, -1);
		mutex_unlock(&local->iflist_mtx);
	}

	if (elems->ch_switch_elem && (elems->ch_switch_elem_len == 3) &&
	    (memcmp(mgmt->bssid, sdata->u.mgd.associated->bssid,
							ETH_ALEN) == 0)) {
		struct ieee80211_channel_sw_ie *sw_elem =
			(struct ieee80211_channel_sw_ie *)elems->ch_switch_elem;
		ieee80211_sta_process_chanswitch(sdata, sw_elem,
						 bss, rx_status->mactime);
	}
}


static void ieee80211_rx_mgmt_probe_resp(struct ieee80211_sub_if_data *sdata,
					 struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct ieee80211_if_managed *ifmgd;
	struct ieee80211_rx_status *rx_status = (void *) skb->cb;
	size_t baselen, len = skb->len;
	struct ieee802_11_elems elems;

	ifmgd = &sdata->u.mgd;

	ASSERT_MGD_MTX(ifmgd);

	if (memcmp(mgmt->da, sdata->vif.addr, ETH_ALEN))
		return; /* ignore ProbeResp to foreign address */

	baselen = (u8 *) mgmt->u.probe_resp.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	ieee802_11_parse_elems(mgmt->u.probe_resp.variable, len - baselen,
				&elems);

	ieee80211_rx_bss_info(sdata, mgmt, len, rx_status, &elems, false);

	if (ifmgd->associated &&
	    memcmp(mgmt->bssid, ifmgd->associated->bssid, ETH_ALEN) == 0)
		ieee80211_reset_ap_probe(sdata);

	if (ifmgd->auth_data && !ifmgd->auth_data->bss->proberesp_ies &&
	    memcmp(mgmt->bssid, ifmgd->auth_data->bss->bssid, ETH_ALEN) == 0) {
		/* got probe response, continue with auth */
		printk(KERN_DEBUG "%s: direct probe responded\n", sdata->name);
		ifmgd->auth_data->tries = 0;
		ifmgd->auth_data->timeout = jiffies;
		run_again(ifmgd, ifmgd->auth_data->timeout);
	}
}

/*
 * This is the canonical list of information elements we care about,
 * the filter code also gives us all changes to the Microsoft OUI
 * (00:50:F2) vendor IE which is used for WMM which we need to track.
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
	(1ULL << WLAN_EID_HT_INFORMATION);

static void ieee80211_rx_mgmt_beacon(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgmt *mgmt,
				     size_t len,
				     struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_bss_conf *bss_conf = &sdata->vif.bss_conf;
	size_t baselen;
	struct ieee802_11_elems elems;
	struct ieee80211_local *local = sdata->local;
	u32 changed = 0;
	bool erp_valid, directed_tim = false;
	u8 erp_value = 0;
	u32 ncrc;
	u8 *bssid;

	lockdep_assert_held(&ifmgd->mtx);

	/* Process beacon from the current BSS */
	baselen = (u8 *) mgmt->u.beacon.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	if (rx_status->freq != local->hw.conf.channel->center_freq)
		return;

	if (ifmgd->assoc_data && !ifmgd->assoc_data->have_beacon &&
	    memcmp(mgmt->bssid, ifmgd->assoc_data->bss->bssid, ETH_ALEN) == 0) {
		ieee802_11_parse_elems(mgmt->u.beacon.variable,
				       len - baselen, &elems);

		ieee80211_rx_bss_info(sdata, mgmt, len, rx_status, &elems,
				      false);
		ifmgd->assoc_data->have_beacon = true;
		ifmgd->assoc_data->sent_assoc = false;
		/* continue assoc process */
		ifmgd->assoc_data->timeout = jiffies;
		run_again(ifmgd, ifmgd->assoc_data->timeout);
		return;
	}

	if (!ifmgd->associated ||
	    memcmp(mgmt->bssid, ifmgd->associated->bssid, ETH_ALEN))
		return;
	bssid = ifmgd->associated->bssid;

	/* Track average RSSI from the Beacon frames of the current AP */
	ifmgd->last_beacon_signal = rx_status->signal;
	if (ifmgd->flags & IEEE80211_STA_RESET_SIGNAL_AVE) {
		ifmgd->flags &= ~IEEE80211_STA_RESET_SIGNAL_AVE;
		ifmgd->ave_beacon_signal = rx_status->signal * 16;
		ifmgd->last_cqm_event_signal = 0;
		ifmgd->count_beacon_signal = 1;
		ifmgd->last_ave_beacon_signal = 0;
	} else {
		ifmgd->ave_beacon_signal =
			(IEEE80211_SIGNAL_AVE_WEIGHT * rx_status->signal * 16 +
			 (16 - IEEE80211_SIGNAL_AVE_WEIGHT) *
			 ifmgd->ave_beacon_signal) / 16;
		ifmgd->count_beacon_signal++;
	}

	if (ifmgd->rssi_min_thold != ifmgd->rssi_max_thold &&
	    ifmgd->count_beacon_signal >= IEEE80211_SIGNAL_AVE_MIN_COUNT) {
		int sig = ifmgd->ave_beacon_signal;
		int last_sig = ifmgd->last_ave_beacon_signal;

		/*
		 * if signal crosses either of the boundaries, invoke callback
		 * with appropriate parameters
		 */
		if (sig > ifmgd->rssi_max_thold &&
		    (last_sig <= ifmgd->rssi_min_thold || last_sig == 0)) {
			ifmgd->last_ave_beacon_signal = sig;
			drv_rssi_callback(local, RSSI_EVENT_HIGH);
		} else if (sig < ifmgd->rssi_min_thold &&
			   (last_sig >= ifmgd->rssi_max_thold ||
			   last_sig == 0)) {
			ifmgd->last_ave_beacon_signal = sig;
			drv_rssi_callback(local, RSSI_EVENT_LOW);
		}
	}

	if (bss_conf->cqm_rssi_thold &&
	    ifmgd->count_beacon_signal >= IEEE80211_SIGNAL_AVE_MIN_COUNT &&
	    !(sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_CQM_RSSI)) {
		int sig = ifmgd->ave_beacon_signal / 16;
		int last_event = ifmgd->last_cqm_event_signal;
		int thold = bss_conf->cqm_rssi_thold;
		int hyst = bss_conf->cqm_rssi_hyst;
		if (sig < thold &&
		    (last_event == 0 || sig < last_event - hyst)) {
			ifmgd->last_cqm_event_signal = sig;
			ieee80211_cqm_rssi_notify(
				&sdata->vif,
				NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW,
				GFP_KERNEL);
		} else if (sig > thold &&
			   (last_event == 0 || sig > last_event + hyst)) {
			ifmgd->last_cqm_event_signal = sig;
			ieee80211_cqm_rssi_notify(
				&sdata->vif,
				NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
				GFP_KERNEL);
		}
	}

	if (ifmgd->flags & IEEE80211_STA_BEACON_POLL) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: cancelling probereq poll due "
			       "to a received beacon\n", sdata->name);
		}
#endif
		ifmgd->flags &= ~IEEE80211_STA_BEACON_POLL;
		mutex_lock(&local->iflist_mtx);
		ieee80211_recalc_ps(local, -1);
		mutex_unlock(&local->iflist_mtx);
	}

	/*
	 * Push the beacon loss detection into the future since
	 * we are processing a beacon from the AP just now.
	 */
	ieee80211_sta_reset_beacon_monitor(sdata);

	ncrc = crc32_be(0, (void *)&mgmt->u.beacon.beacon_int, 4);
	ncrc = ieee802_11_parse_elems_crc(mgmt->u.beacon.variable,
					  len - baselen, &elems,
					  care_about_ies, ncrc);

	if (local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK)
		directed_tim = ieee80211_check_tim(elems.tim, elems.tim_len,
						   ifmgd->aid);

	if (ncrc != ifmgd->beacon_crc || !ifmgd->beacon_crc_valid) {
		ieee80211_rx_bss_info(sdata, mgmt, len, rx_status, &elems,
				      true);

		ieee80211_sta_wmm_params(local, sdata, elems.wmm_param,
					 elems.wmm_param_len);
	}

	if (local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK) {
		if (directed_tim) {
			if (local->hw.conf.dynamic_ps_timeout > 0) {
				local->hw.conf.flags &= ~IEEE80211_CONF_PS;
				ieee80211_hw_config(local,
						    IEEE80211_CONF_CHANGE_PS);
				ieee80211_send_nullfunc(local, sdata, 0);
			} else {
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
	}

	if (ncrc == ifmgd->beacon_crc && ifmgd->beacon_crc_valid)
		return;
	ifmgd->beacon_crc = ncrc;
	ifmgd->beacon_crc_valid = true;

	if (elems.erp_info && elems.erp_info_len >= 1) {
		erp_valid = true;
		erp_value = elems.erp_info[0];
	} else {
		erp_valid = false;
	}
	changed |= ieee80211_handle_bss_capability(sdata,
			le16_to_cpu(mgmt->u.beacon.capab_info),
			erp_valid, erp_value);


	if (elems.ht_cap_elem && elems.ht_info_elem && elems.wmm_param &&
	    !(ifmgd->flags & IEEE80211_STA_DISABLE_11N)) {
		struct sta_info *sta;
		struct ieee80211_supported_band *sband;
		u16 ap_ht_cap_flags;

		rcu_read_lock();

		sta = sta_info_get(sdata, bssid);
		if (WARN_ON(!sta)) {
			rcu_read_unlock();
			return;
		}

		sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

		ieee80211_ht_cap_ie_to_sta_ht_cap(sdata, sband,
				elems.ht_cap_elem, &sta->sta.ht_cap);

		ap_ht_cap_flags = sta->sta.ht_cap.cap;

		rcu_read_unlock();

		changed |= ieee80211_enable_ht(sdata, elems.ht_info_elem,
					       bssid, ap_ht_cap_flags, true);
	}

	/* Note: country IE parsing is done for us by cfg80211 */
	if (elems.country_elem) {
		/* TODO: IBSS also needs this */
		if (elems.pwr_constr_elem)
			ieee80211_handle_pwr_constr(sdata,
				le16_to_cpu(mgmt->u.probe_resp.capab_info),
				elems.pwr_constr_elem,
				elems.pwr_constr_elem_len);
	}

	ieee80211_bss_info_change_notify(sdata, changed);
}

void ieee80211_sta_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
				  struct sk_buff *skb)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_mgmt *mgmt;
	struct cfg80211_bss *bss = NULL;
	enum rx_mgmt_action rma = RX_MGMT_NONE;
	u16 fc;

	rx_status = (struct ieee80211_rx_status *) skb->cb;
	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	mutex_lock(&ifmgd->mtx);

	switch (fc & IEEE80211_FCTL_STYPE) {
	case IEEE80211_STYPE_BEACON:
		ieee80211_rx_mgmt_beacon(sdata, mgmt, skb->len, rx_status);
		break;
	case IEEE80211_STYPE_PROBE_RESP:
		ieee80211_rx_mgmt_probe_resp(sdata, skb);
		break;
	case IEEE80211_STYPE_AUTH:
		rma = ieee80211_rx_mgmt_auth(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_DEAUTH:
		rma = ieee80211_rx_mgmt_deauth(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_DISASSOC:
		rma = ieee80211_rx_mgmt_disassoc(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_ASSOC_RESP:
	case IEEE80211_STYPE_REASSOC_RESP:
		rma = ieee80211_rx_mgmt_assoc_resp(sdata, mgmt, skb->len, &bss);
		break;
	case IEEE80211_STYPE_ACTION:
		switch (mgmt->u.action.category) {
		case WLAN_CATEGORY_SPECTRUM_MGMT:
			ieee80211_sta_process_chanswitch(sdata,
					&mgmt->u.action.u.chan_switch.sw_elem,
					(void *)ifmgd->associated->priv,
					rx_status->mactime);
			break;
		}
	}
	mutex_unlock(&ifmgd->mtx);

	switch (rma) {
	case RX_MGMT_NONE:
		/* no action */
		break;
	case RX_MGMT_CFG80211_DEAUTH:
		cfg80211_send_deauth(sdata->dev, (u8 *)mgmt, skb->len);
		break;
	case RX_MGMT_CFG80211_DISASSOC:
		cfg80211_send_disassoc(sdata->dev, (u8 *)mgmt, skb->len);
		break;
	case RX_MGMT_CFG80211_RX_AUTH:
		cfg80211_send_rx_auth(sdata->dev, (u8 *)mgmt, skb->len);
		break;
	case RX_MGMT_CFG80211_RX_ASSOC:
		cfg80211_send_rx_assoc(sdata->dev, bss, (u8 *)mgmt, skb->len);
		break;
	case RX_MGMT_CFG80211_ASSOC_TIMEOUT:
		cfg80211_send_assoc_timeout(sdata->dev, mgmt->bssid);
		break;
	default:
		WARN(1, "unexpected: %d", rma);
	}
}

static void ieee80211_sta_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata =
		(struct ieee80211_sub_if_data *) data;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;

	if (local->quiescing) {
		set_bit(TMR_RUNNING_TIMER, &ifmgd->timers_running);
		return;
	}

	ieee80211_queue_work(&local->hw, &sdata->work);
}

static void ieee80211_sta_connection_lost(struct ieee80211_sub_if_data *sdata,
					  u8 *bssid, u8 reason)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 frame_buf[DEAUTH_DISASSOC_LEN];

	ifmgd->flags &= ~(IEEE80211_STA_CONNECTION_POLL |
			  IEEE80211_STA_BEACON_POLL);

	ieee80211_set_disassoc(sdata, true, false);
	mutex_unlock(&ifmgd->mtx);
	/*
	 * must be outside lock due to cfg80211,
	 * but that's not a problem.
	 */
	ieee80211_send_deauth_disassoc(sdata, bssid,
				       IEEE80211_STYPE_DEAUTH,
				       reason, false, frame_buf);
	cfg80211_send_deauth(sdata->dev, frame_buf, DEAUTH_DISASSOC_LEN);

	mutex_lock(&local->mtx);
	ieee80211_recalc_idle(local);
	mutex_unlock(&local->mtx);

	mutex_lock(&ifmgd->mtx);
}

static int ieee80211_probe_auth(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_auth_data *auth_data = ifmgd->auth_data;

	lockdep_assert_held(&ifmgd->mtx);

	if (WARN_ON_ONCE(!auth_data))
		return -EINVAL;

	if (!auth_data->synced) {
		int ret = drv_tx_sync(local, sdata, auth_data->bss->bssid,
				      IEEE80211_TX_SYNC_AUTH);
		if (ret)
			return ret;
	}
	auth_data->synced = true;

	auth_data->tries++;

	if (auth_data->tries > IEEE80211_AUTH_MAX_TRIES) {
		printk(KERN_DEBUG "%s: authentication with %pM timed out\n",
		       sdata->name, auth_data->bss->bssid);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss struct for that AP.
		 */
		cfg80211_unlink_bss(local->hw.wiphy, auth_data->bss);

		return -ETIMEDOUT;
	}

	if (auth_data->bss->proberesp_ies) {
		printk(KERN_DEBUG "%s: send auth to %pM (try %d/%d)\n",
		       sdata->name, auth_data->bss->bssid, auth_data->tries,
		       IEEE80211_AUTH_MAX_TRIES);

		auth_data->expected_transaction = 2;
		ieee80211_send_auth(sdata, 1, auth_data->algorithm,
				    auth_data->ie, auth_data->ie_len,
				    auth_data->bss->bssid,
				    auth_data->bss->bssid, NULL, 0, 0);
	} else {
		const u8 *ssidie;

		printk(KERN_DEBUG "%s: direct probe to %pM (try %d/%i)\n",
		       sdata->name, auth_data->bss->bssid, auth_data->tries,
		       IEEE80211_AUTH_MAX_TRIES);

		ssidie = ieee80211_bss_get_ie(auth_data->bss, WLAN_EID_SSID);
		if (!ssidie)
			return -EINVAL;
		/*
		 * Direct probe is sent to broadcast address as some APs
		 * will not answer to direct packet in unassociated state.
		 */
		ieee80211_send_probe_req(sdata, NULL, ssidie + 2, ssidie[1],
					 NULL, 0, (u32) -1, true, false);
	}

	auth_data->timeout = jiffies + IEEE80211_AUTH_TIMEOUT;
	run_again(ifmgd, auth_data->timeout);

	return 0;
}

static int ieee80211_do_assoc(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_mgd_assoc_data *assoc_data = sdata->u.mgd.assoc_data;
	struct ieee80211_local *local = sdata->local;

	lockdep_assert_held(&sdata->u.mgd.mtx);

	if (!assoc_data->synced) {
		int ret = drv_tx_sync(local, sdata, assoc_data->bss->bssid,
				      IEEE80211_TX_SYNC_ASSOC);
		if (ret)
			return ret;
	}
	assoc_data->synced = true;

	assoc_data->tries++;
	if (assoc_data->tries > IEEE80211_ASSOC_MAX_TRIES) {
		printk(KERN_DEBUG "%s: association with %pM timed out\n",
		       sdata->name, assoc_data->bss->bssid);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss struct for that AP.
		 */
		cfg80211_unlink_bss(local->hw.wiphy, assoc_data->bss);

		return -ETIMEDOUT;
	}

	printk(KERN_DEBUG "%s: associate with %pM (try %d/%d)\n",
	       sdata->name, assoc_data->bss->bssid, assoc_data->tries,
	       IEEE80211_ASSOC_MAX_TRIES);
	ieee80211_send_assoc(sdata);

	assoc_data->timeout = jiffies + IEEE80211_ASSOC_TIMEOUT;
	run_again(&sdata->u.mgd, assoc_data->timeout);

	return 0;
}

void ieee80211_sta_work(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	mutex_lock(&ifmgd->mtx);

	if (ifmgd->auth_data &&
	    time_after(jiffies, ifmgd->auth_data->timeout)) {
		if (ifmgd->auth_data->done) {
			/*
			 * ok ... we waited for assoc but userspace didn't,
			 * so let's just kill the auth data
			 */
			ieee80211_destroy_auth_data(sdata, false);
		} else if (ieee80211_probe_auth(sdata)) {
			u8 bssid[ETH_ALEN];

			memcpy(bssid, ifmgd->auth_data->bss->bssid, ETH_ALEN);

			ieee80211_destroy_auth_data(sdata, false);

			mutex_unlock(&ifmgd->mtx);
			cfg80211_send_auth_timeout(sdata->dev, bssid);
			mutex_lock(&ifmgd->mtx);
		}
	} else if (ifmgd->auth_data)
		run_again(ifmgd, ifmgd->auth_data->timeout);

	if (ifmgd->assoc_data &&
	    time_after(jiffies, ifmgd->assoc_data->timeout)) {
		if (!ifmgd->assoc_data->have_beacon ||
		    ieee80211_do_assoc(sdata)) {
			u8 bssid[ETH_ALEN];

			memcpy(bssid, ifmgd->assoc_data->bss->bssid, ETH_ALEN);

			ieee80211_destroy_assoc_data(sdata, false);

			mutex_unlock(&ifmgd->mtx);
			cfg80211_send_assoc_timeout(sdata->dev, bssid);
			mutex_lock(&ifmgd->mtx);
		}
	} else if (ifmgd->assoc_data)
		run_again(ifmgd, ifmgd->assoc_data->timeout);

	if (ifmgd->flags & (IEEE80211_STA_BEACON_POLL |
			    IEEE80211_STA_CONNECTION_POLL) &&
	    ifmgd->associated) {
		u8 bssid[ETH_ALEN];
		int max_tries;

		memcpy(bssid, ifmgd->associated->bssid, ETH_ALEN);

		if (local->hw.flags & IEEE80211_HW_REPORTS_TX_ACK_STATUS)
			max_tries = max_nullfunc_tries;
		else
			max_tries = max_probe_tries;

		/* ACK received for nullfunc probing frame */
		if (!ifmgd->probe_send_count)
			ieee80211_reset_ap_probe(sdata);
		else if (ifmgd->nullfunc_failed) {
			if (ifmgd->probe_send_count < max_tries) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
				wiphy_debug(local->hw.wiphy,
					    "%s: No ack for nullfunc frame to"
					    " AP %pM, try %d/%i\n",
					    sdata->name, bssid,
					    ifmgd->probe_send_count, max_tries);
#endif
				ieee80211_mgd_probe_ap_send(sdata);
			} else {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
				wiphy_debug(local->hw.wiphy,
					    "%s: No ack for nullfunc frame to"
					    " AP %pM, disconnecting.\n",
					    sdata->name, bssid);
#endif
				ieee80211_sta_connection_lost(sdata, bssid,
					WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY);
			}
		} else if (time_is_after_jiffies(ifmgd->probe_timeout))
			run_again(ifmgd, ifmgd->probe_timeout);
		else if (local->hw.flags & IEEE80211_HW_REPORTS_TX_ACK_STATUS) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
			wiphy_debug(local->hw.wiphy,
				    "%s: Failed to send nullfunc to AP %pM"
				    " after %dms, disconnecting.\n",
				    sdata->name,
				    bssid, probe_wait_ms);
#endif
			ieee80211_sta_connection_lost(sdata, bssid,
				WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY);
		} else if (ifmgd->probe_send_count < max_tries) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
			wiphy_debug(local->hw.wiphy,
				    "%s: No probe response from AP %pM"
				    " after %dms, try %d/%i\n",
				    sdata->name,
				    bssid, probe_wait_ms,
				    ifmgd->probe_send_count, max_tries);
#endif
			ieee80211_mgd_probe_ap_send(sdata);
		} else {
			/*
			 * We actually lost the connection ... or did we?
			 * Let's make sure!
			 */
			wiphy_debug(local->hw.wiphy,
				    "%s: No probe response from AP %pM"
				    " after %dms, disconnecting.\n",
				    sdata->name,
				    bssid, probe_wait_ms);

			ieee80211_sta_connection_lost(sdata, bssid,
				WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY);
		}
	}

	mutex_unlock(&ifmgd->mtx);

	mutex_lock(&local->mtx);
	ieee80211_recalc_idle(local);
	mutex_unlock(&local->mtx);
}

static void ieee80211_sta_bcn_mon_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata =
		(struct ieee80211_sub_if_data *) data;
	struct ieee80211_local *local = sdata->local;

	if (local->quiescing)
		return;

	ieee80211_queue_work(&sdata->local->hw,
			     &sdata->u.mgd.beacon_connection_loss_work);
}

static void ieee80211_sta_conn_mon_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata =
		(struct ieee80211_sub_if_data *) data;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;

	if (local->quiescing)
		return;

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
	u32 flags;

	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		sdata->u.mgd.flags &= ~(IEEE80211_STA_BEACON_POLL |
					IEEE80211_STA_CONNECTION_POLL);

		/* let's probe the connection once */
		flags = sdata->local->hw.flags;
		if (!(flags & IEEE80211_HW_CONNECTION_MONITOR))
			ieee80211_queue_work(&sdata->local->hw,
					     &sdata->u.mgd.monitor_work);
		/* and do all the other regular work too */
		ieee80211_queue_work(&sdata->local->hw, &sdata->work);
	}
}

#ifdef CONFIG_PM
void ieee80211_sta_quiesce(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	/*
	 * we need to use atomic bitops for the running bits
	 * only because both timers might fire at the same
	 * time -- the code here is properly synchronised.
	 */

	cancel_work_sync(&ifmgd->request_smps_work);

	cancel_work_sync(&ifmgd->monitor_work);
	cancel_work_sync(&ifmgd->beacon_connection_loss_work);
	if (del_timer_sync(&ifmgd->timer))
		set_bit(TMR_RUNNING_TIMER, &ifmgd->timers_running);

	cancel_work_sync(&ifmgd->chswitch_work);
	if (del_timer_sync(&ifmgd->chswitch_timer))
		set_bit(TMR_RUNNING_CHANSW, &ifmgd->timers_running);

	/* these will just be re-established on connection */
	del_timer_sync(&ifmgd->conn_mon_timer);
	del_timer_sync(&ifmgd->bcn_mon_timer);
}

void ieee80211_sta_restart(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (!ifmgd->associated)
		return;

	if (sdata->flags & IEEE80211_SDATA_DISCONNECT_RESUME) {
		sdata->flags &= ~IEEE80211_SDATA_DISCONNECT_RESUME;
		mutex_lock(&ifmgd->mtx);
		if (ifmgd->associated) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
			wiphy_debug(sdata->local->hw.wiphy,
				    "%s: driver requested disconnect after resume.\n",
				    sdata->name);
#endif
			ieee80211_sta_connection_lost(sdata,
				ifmgd->associated->bssid,
				WLAN_REASON_UNSPECIFIED);
			mutex_unlock(&ifmgd->mtx);
			return;
		}
		mutex_unlock(&ifmgd->mtx);
	}

	if (test_and_clear_bit(TMR_RUNNING_TIMER, &ifmgd->timers_running))
		add_timer(&ifmgd->timer);
	if (test_and_clear_bit(TMR_RUNNING_CHANSW, &ifmgd->timers_running))
		add_timer(&ifmgd->chswitch_timer);
	ieee80211_sta_reset_beacon_monitor(sdata);
	ieee80211_restart_sta_timer(sdata);
}
#endif

/* interface setup */
void ieee80211_sta_setup_sdata(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd;

	ifmgd = &sdata->u.mgd;
	INIT_WORK(&ifmgd->monitor_work, ieee80211_sta_monitor_work);
	INIT_WORK(&ifmgd->chswitch_work, ieee80211_chswitch_work);
	INIT_WORK(&ifmgd->beacon_connection_loss_work,
		  ieee80211_beacon_connection_loss_work);
	INIT_WORK(&ifmgd->request_smps_work, ieee80211_request_smps_work);
	setup_timer(&ifmgd->timer, ieee80211_sta_timer,
		    (unsigned long) sdata);
	setup_timer(&ifmgd->bcn_mon_timer, ieee80211_sta_bcn_mon_timer,
		    (unsigned long) sdata);
	setup_timer(&ifmgd->conn_mon_timer, ieee80211_sta_conn_mon_timer,
		    (unsigned long) sdata);
	setup_timer(&ifmgd->chswitch_timer, ieee80211_chswitch_timer,
		    (unsigned long) sdata);

	ifmgd->flags = 0;
	ifmgd->powersave = sdata->wdev.ps;

	mutex_init(&ifmgd->mtx);

	if (sdata->local->hw.flags & IEEE80211_HW_SUPPORTS_DYNAMIC_SMPS)
		ifmgd->req_smps = IEEE80211_SMPS_AUTOMATIC;
	else
		ifmgd->req_smps = IEEE80211_SMPS_OFF;
}

/* scan finished notification */
void ieee80211_mlme_notify_scan_completed(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata = local->scan_sdata;

	/* Restart STA timers */
	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list)
		ieee80211_restart_sta_timer(sdata);
	rcu_read_unlock();
}

int ieee80211_max_network_latency(struct notifier_block *nb,
				  unsigned long data, void *dummy)
{
	s32 latency_usec = (s32) data;
	struct ieee80211_local *local =
		container_of(nb, struct ieee80211_local,
			     network_latency_notifier);

	mutex_lock(&local->iflist_mtx);
	ieee80211_recalc_ps(local, latency_usec);
	mutex_unlock(&local->iflist_mtx);

	return 0;
}

/* config hooks */
int ieee80211_mgd_auth(struct ieee80211_sub_if_data *sdata,
		       struct cfg80211_auth_request *req)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_auth_data *auth_data;
	struct sta_info *sta;
	u16 auth_alg;
	int err;

	/* prepare auth data structure */

	switch (req->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		auth_alg = WLAN_AUTH_OPEN;
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		if (IS_ERR(local->wep_tx_tfm))
			return -EOPNOTSUPP;
		auth_alg = WLAN_AUTH_SHARED_KEY;
		break;
	case NL80211_AUTHTYPE_FT:
		auth_alg = WLAN_AUTH_FT;
		break;
	case NL80211_AUTHTYPE_NETWORK_EAP:
		auth_alg = WLAN_AUTH_LEAP;
		break;
	default:
		return -EOPNOTSUPP;
	}

	auth_data = kzalloc(sizeof(*auth_data) + req->ie_len, GFP_KERNEL);
	if (!auth_data)
		return -ENOMEM;

	auth_data->bss = req->bss;

	if (req->ie && req->ie_len) {
		memcpy(auth_data->ie, req->ie, req->ie_len);
		auth_data->ie_len = req->ie_len;
	}

	if (req->key && req->key_len) {
		auth_data->key_len = req->key_len;
		auth_data->key_idx = req->key_idx;
		memcpy(auth_data->key, req->key, req->key_len);
	}

	auth_data->algorithm = auth_alg;

	/* try to authenticate/probe */

	mutex_lock(&ifmgd->mtx);

	if ((ifmgd->auth_data && !ifmgd->auth_data->done) ||
	    ifmgd->assoc_data) {
		err = -EBUSY;
		goto err_free;
	}

	if (ifmgd->auth_data)
		ieee80211_destroy_auth_data(sdata, false);

	/* prep auth_data so we don't go into idle on disassoc */
	ifmgd->auth_data = auth_data;

	if (ifmgd->associated)
		ieee80211_set_disassoc(sdata, true, false);

	printk(KERN_DEBUG "%s: authenticate with %pM\n",
	       sdata->name, req->bss->bssid);

	mutex_lock(&local->mtx);
	ieee80211_recalc_idle(sdata->local);
	mutex_unlock(&local->mtx);

	/* switch to the right channel */
	local->oper_channel = req->bss->channel;
	ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);

	/* set BSSID */
	memcpy(ifmgd->bssid, req->bss->bssid, ETH_ALEN);
	ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_BSSID);

	/* add station entry */
	sta = sta_info_alloc(sdata, req->bss->bssid, GFP_KERNEL);
	if (!sta) {
		err = -ENOMEM;
		goto err_clear;
	}

	err = sta_info_insert(sta);
	if (err) {
		printk(KERN_DEBUG
		       "%s: failed to insert STA entry for the AP %pM (error %d)\n",
		       sdata->name, req->bss->bssid, err);
		goto err_clear;
	}

	err = ieee80211_probe_auth(sdata);
	if (err) {
		if (auth_data->synced)
			drv_finish_tx_sync(local, sdata, req->bss->bssid,
					   IEEE80211_TX_SYNC_AUTH);
		sta_info_destroy_addr(sdata, req->bss->bssid);
		goto err_clear;
	}

	/* hold our own reference */
	cfg80211_ref_bss(auth_data->bss);
	err = 0;
	goto out_unlock;

 err_clear:
	ifmgd->auth_data = NULL;
 err_free:
	kfree(auth_data);
 out_unlock:
	mutex_unlock(&ifmgd->mtx);

	return err;
}

int ieee80211_mgd_assoc(struct ieee80211_sub_if_data *sdata,
			struct cfg80211_assoc_request *req)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_bss *bss = (void *)req->bss->priv;
	struct ieee80211_mgd_assoc_data *assoc_data;
	struct sta_info *sta;
	const u8 *ssidie;
	int i, err;

	ssidie = ieee80211_bss_get_ie(req->bss, WLAN_EID_SSID);
	if (!ssidie)
		return -EINVAL;

	assoc_data = kzalloc(sizeof(*assoc_data) + req->ie_len, GFP_KERNEL);
	if (!assoc_data)
		return -ENOMEM;

	mutex_lock(&ifmgd->mtx);

	if (ifmgd->associated)
		ieee80211_set_disassoc(sdata, true, false);

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
		match = memcmp(ifmgd->bssid, req->bss->bssid, ETH_ALEN) == 0;
		ieee80211_destroy_auth_data(sdata, match);
	}

	/* prepare assoc data */

	ifmgd->flags &= ~IEEE80211_STA_DISABLE_11N;
	ifmgd->flags &= ~IEEE80211_STA_NULLFUNC_ACKED;

	ifmgd->beacon_crc_valid = false;

	for (i = 0; i < req->crypto.n_ciphers_pairwise; i++)
		if (req->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_WEP40 ||
		    req->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_TKIP ||
		    req->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_WEP104)
			ifmgd->flags |= IEEE80211_STA_DISABLE_11N;

	if (req->flags & ASSOC_REQ_DISABLE_HT)
		ifmgd->flags |= IEEE80211_STA_DISABLE_11N;

	memcpy(&ifmgd->ht_capa, &req->ht_capa, sizeof(ifmgd->ht_capa));
	memcpy(&ifmgd->ht_capa_mask, &req->ht_capa_mask,
	       sizeof(ifmgd->ht_capa_mask));

	if (req->ie && req->ie_len) {
		memcpy(assoc_data->ie, req->ie, req->ie_len);
		assoc_data->ie_len = req->ie_len;
	}

	assoc_data->bss = req->bss;

	if (ifmgd->req_smps == IEEE80211_SMPS_AUTOMATIC) {
		if (ifmgd->powersave)
			ifmgd->ap_smps = IEEE80211_SMPS_DYNAMIC;
		else
			ifmgd->ap_smps = IEEE80211_SMPS_OFF;
	} else
		ifmgd->ap_smps = ifmgd->req_smps;

	/*
	 * IEEE802.11n does not allow TKIP/WEP as pairwise ciphers in HT mode.
	 * We still associate in non-HT mode (11a/b/g) if any one of these
	 * ciphers is configured as pairwise.
	 * We can set this to true for non-11n hardware, that'll be checked
	 * separately along with the peer capabilities.
	 */
	assoc_data->capability = req->bss->capability;
	assoc_data->wmm_used = bss->wmm_used;
	assoc_data->supp_rates = bss->supp_rates;
	assoc_data->supp_rates_len = bss->supp_rates_len;
	assoc_data->ht_information_ie =
		ieee80211_bss_get_ie(req->bss, WLAN_EID_HT_INFORMATION);

	if (bss->wmm_used && bss->uapsd_supported &&
	    (sdata->local->hw.flags & IEEE80211_HW_SUPPORTS_UAPSD)) {
		assoc_data->uapsd_used = true;
		ifmgd->flags |= IEEE80211_STA_UAPSD_ENABLED;
	} else {
		assoc_data->uapsd_used = false;
		ifmgd->flags &= ~IEEE80211_STA_UAPSD_ENABLED;
	}

	memcpy(assoc_data->ssid, ssidie + 2, ssidie[1]);
	assoc_data->ssid_len = ssidie[1];

	if (req->prev_bssid)
		memcpy(assoc_data->prev_bssid, req->prev_bssid, ETH_ALEN);

	if (req->use_mfp) {
		ifmgd->mfp = IEEE80211_MFP_REQUIRED;
		ifmgd->flags |= IEEE80211_STA_MFP_ENABLED;
	} else {
		ifmgd->mfp = IEEE80211_MFP_DISABLED;
		ifmgd->flags &= ~IEEE80211_STA_MFP_ENABLED;
	}

	if (req->crypto.control_port)
		ifmgd->flags |= IEEE80211_STA_CONTROL_PORT;
	else
		ifmgd->flags &= ~IEEE80211_STA_CONTROL_PORT;

	sdata->control_port_protocol = req->crypto.control_port_ethertype;
	sdata->control_port_no_encrypt = req->crypto.control_port_no_encrypt;

	/* kick off associate process */

	ifmgd->assoc_data = assoc_data;

	mutex_lock(&local->mtx);
	ieee80211_recalc_idle(sdata->local);
	mutex_unlock(&local->mtx);

	/* switch to the right channel */
	local->oper_channel = req->bss->channel;
	ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);

	rcu_read_lock();
	sta = sta_info_get(sdata, req->bss->bssid);
	rcu_read_unlock();

	if (!sta) {
		/* set BSSID */
		memcpy(ifmgd->bssid, req->bss->bssid, ETH_ALEN);
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_BSSID);

		sta = sta_info_alloc(sdata, req->bss->bssid, GFP_KERNEL);
		if (!sta) {
			err = -ENOMEM;
			goto err_clear;
		}

		sta_info_pre_move_state(sta, IEEE80211_STA_AUTH);

		err = sta_info_insert(sta);
		sta = NULL;
		if (err) {
			printk(KERN_DEBUG
			       "%s: failed to insert STA entry for the AP (error %d)\n",
			       sdata->name, err);
			goto err_clear;
		}
	} else
		WARN_ON_ONCE(memcmp(ifmgd->bssid, req->bss->bssid, ETH_ALEN));

	if (!bss->dtim_period &&
	    sdata->local->hw.flags & IEEE80211_HW_NEED_DTIM_PERIOD) {
		/*
		 * Wait up to one beacon interval ...
		 * should this be more if we miss one?
		 */
		printk(KERN_DEBUG "%s: waiting for beacon from %pM\n",
		       sdata->name, ifmgd->bssid);
		assoc_data->timeout = jiffies +
				TU_TO_EXP_TIME(req->bss->beacon_interval);
	} else {
		assoc_data->have_beacon = true;
		assoc_data->sent_assoc = false;
		assoc_data->timeout = jiffies;
	}
	run_again(ifmgd, assoc_data->timeout);

	err = 0;
	goto out;
 err_clear:
	ifmgd->assoc_data = NULL;
 err_free:
	kfree(assoc_data);
 out:
	mutex_unlock(&ifmgd->mtx);

	return err;
}

int ieee80211_mgd_deauth(struct ieee80211_sub_if_data *sdata,
			 struct cfg80211_deauth_request *req)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	bool assoc_bss = false;
	u8 frame_buf[DEAUTH_DISASSOC_LEN];

	mutex_lock(&ifmgd->mtx);

	if (ifmgd->associated &&
	    memcmp(ifmgd->associated->bssid, req->bssid, ETH_ALEN) == 0) {
		ieee80211_set_disassoc(sdata, false, true);
		assoc_bss = true;
	} else if (ifmgd->auth_data) {
		ieee80211_destroy_auth_data(sdata, false);
		mutex_unlock(&ifmgd->mtx);
		return 0;
	}
	mutex_unlock(&ifmgd->mtx);

	printk(KERN_DEBUG "%s: deauthenticating from %pM by local choice (reason=%d)\n",
	       sdata->name, req->bssid, req->reason_code);

	ieee80211_send_deauth_disassoc(sdata, req->bssid,
				       IEEE80211_STYPE_DEAUTH,
				       req->reason_code, true, frame_buf);
	__cfg80211_send_deauth(sdata->dev, frame_buf, DEAUTH_DISASSOC_LEN);
	if (assoc_bss)
		sta_info_flush(sdata->local, sdata);

	mutex_lock(&sdata->local->mtx);
	ieee80211_recalc_idle(sdata->local);
	mutex_unlock(&sdata->local->mtx);

	return 0;
}

int ieee80211_mgd_disassoc(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_disassoc_request *req)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u8 bssid[ETH_ALEN];
	u8 frame_buf[DEAUTH_DISASSOC_LEN];

	mutex_lock(&ifmgd->mtx);

	/*
	 * cfg80211 should catch this ... but it's racy since
	 * we can receive a disassoc frame, process it, hand it
	 * to cfg80211 while that's in a locked section already
	 * trying to tell us that the user wants to disconnect.
	 */
	if (ifmgd->associated != req->bss) {
		mutex_unlock(&ifmgd->mtx);
		return -ENOLINK;
	}

	printk(KERN_DEBUG "%s: disassociating from %pM by local choice (reason=%d)\n",
	       sdata->name, req->bss->bssid, req->reason_code);

	memcpy(bssid, req->bss->bssid, ETH_ALEN);
	ieee80211_set_disassoc(sdata, false, !req->local_state_change);

	mutex_unlock(&ifmgd->mtx);

	ieee80211_send_deauth_disassoc(sdata, req->bss->bssid,
				       IEEE80211_STYPE_DISASSOC,
				       req->reason_code,
				       !req->local_state_change, frame_buf);
	__cfg80211_send_disassoc(sdata->dev, frame_buf, DEAUTH_DISASSOC_LEN);
	sta_info_flush(sdata->local, sdata);

	mutex_lock(&sdata->local->mtx);
	ieee80211_recalc_idle(sdata->local);
	mutex_unlock(&sdata->local->mtx);

	return 0;
}

void ieee80211_mgd_teardown(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	mutex_lock(&ifmgd->mtx);
	if (ifmgd->assoc_data)
		ieee80211_destroy_assoc_data(sdata, false);
	if (ifmgd->auth_data)
		ieee80211_destroy_auth_data(sdata, false);
	del_timer_sync(&ifmgd->timer);
	mutex_unlock(&ifmgd->mtx);
}

void ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif,
			       enum nl80211_cqm_rssi_threshold_event rssi_event,
			       gfp_t gfp)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	trace_api_cqm_rssi_notify(sdata, rssi_event);

	cfg80211_cqm_rssi_notify(sdata->dev, rssi_event, gfp);
}
EXPORT_SYMBOL(ieee80211_cqm_rssi_notify);

unsigned char ieee80211_get_operstate(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	return sdata->dev->operstate;
}
EXPORT_SYMBOL(ieee80211_get_operstate);
