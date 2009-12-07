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
#include <linux/rtnetlink.h>
#include <linux/pm_qos_params.h>
#include <linux/crc32.h>
#include <net/mac80211.h>
#include <asm/unaligned.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "led.h"

#define IEEE80211_AUTH_TIMEOUT (HZ / 5)
#define IEEE80211_AUTH_MAX_TRIES 3
#define IEEE80211_ASSOC_TIMEOUT (HZ / 5)
#define IEEE80211_ASSOC_MAX_TRIES 3
#define IEEE80211_MAX_PROBE_TRIES 5

/*
 * beacon loss detection timeout
 * XXX: should depend on beacon interval
 */
#define IEEE80211_BEACON_LOSS_TIME	(2 * HZ)
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
#define IEEE80211_PROBE_WAIT		(HZ / 2)

#define TMR_RUNNING_TIMER	0
#define TMR_RUNNING_CHANSW	1

/*
 * All cfg80211 functions have to be called outside a locked
 * section so that they can acquire a lock themselves... This
 * is much simpler than queuing up things in cfg80211, but we
 * do need some indirection for that here.
 */
enum rx_mgmt_action {
	/* no action required */
	RX_MGMT_NONE,

	/* caller must call cfg80211_send_rx_auth() */
	RX_MGMT_CFG80211_AUTH,

	/* caller must call cfg80211_send_rx_assoc() */
	RX_MGMT_CFG80211_ASSOC,

	/* caller must call cfg80211_send_deauth() */
	RX_MGMT_CFG80211_DEAUTH,

	/* caller must call cfg80211_send_disassoc() */
	RX_MGMT_CFG80211_DISASSOC,

	/* caller must call cfg80211_auth_timeout() & free work */
	RX_MGMT_CFG80211_AUTH_TO,

	/* caller must call cfg80211_assoc_timeout() & free work */
	RX_MGMT_CFG80211_ASSOC_TO,
};

/* utils */
static inline void ASSERT_MGD_MTX(struct ieee80211_if_managed *ifmgd)
{
	WARN_ON(!mutex_is_locked(&ifmgd->mtx));
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
static void run_again(struct ieee80211_if_managed *ifmgd,
			     unsigned long timeout)
{
	ASSERT_MGD_MTX(ifmgd);

	if (!timer_pending(&ifmgd->timer) ||
	    time_before(timeout, ifmgd->timer.expires))
		mod_timer(&ifmgd->timer, timeout);
}

static void mod_beacon_timer(struct ieee80211_sub_if_data *sdata)
{
	if (sdata->local->hw.flags & IEEE80211_HW_BEACON_FILTER)
		return;

	mod_timer(&sdata->u.mgd.bcn_mon_timer,
		  round_jiffies_up(jiffies + IEEE80211_BEACON_LOSS_TIME));
}

static int ecw2cw(int ecw)
{
	return (1 << ecw) - 1;
}

static int ieee80211_compatible_rates(struct ieee80211_bss *bss,
				      struct ieee80211_supported_band *sband,
				      u32 *rates)
{
	int i, j, count;
	*rates = 0;
	count = 0;
	for (i = 0; i < bss->supp_rates_len; i++) {
		int rate = (bss->supp_rates[i] & 0x7F) * 5;

		for (j = 0; j < sband->n_bitrates; j++)
			if (sband->bitrates[j].bitrate == rate) {
				*rates |= BIT(j);
				count++;
				break;
			}
	}

	return count;
}

/*
 * ieee80211_enable_ht should be called only after the operating band
 * has been determined as ht configuration depends on the hw's
 * HT abilities for a specific band.
 */
static u32 ieee80211_enable_ht(struct ieee80211_sub_if_data *sdata,
			       struct ieee80211_ht_info *hti,
			       const u8 *bssid, u16 ap_ht_cap_flags)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	struct sta_info *sta;
	u32 changed = 0;
	u16 ht_opmode;
	bool enable_ht = true, ht_changed;
	enum nl80211_channel_type channel_type = NL80211_CHAN_NO_HT;

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	/* HT is not supported */
	if (!sband->ht_cap.ht_supported)
		enable_ht = false;

	/* check that channel matches the right operating channel */
	if (local->hw.conf.channel->center_freq !=
	    ieee80211_channel_to_frequency(hti->control_chan))
		enable_ht = false;

	if (enable_ht) {
		channel_type = NL80211_CHAN_HT20;

		if (!(ap_ht_cap_flags & IEEE80211_HT_CAP_40MHZ_INTOLERANT) &&
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

	ht_changed = conf_is_ht(&local->hw.conf) != enable_ht ||
		     channel_type != local->hw.conf.channel_type;

	local->oper_channel_type = channel_type;

	if (ht_changed) {
                /* channel_type change automatically detected */
		ieee80211_hw_config(local, 0);

		rcu_read_lock();
		sta = sta_info_get(local, bssid);
		if (sta)
			rate_control_rate_update(local, sband, sta,
						 IEEE80211_RC_HT_CHANGED);
		rcu_read_unlock();
        }

	/* disable HT */
	if (!enable_ht)
		return 0;

	ht_opmode = le16_to_cpu(hti->operation_mode);

	/* if bss configuration changed store the new one */
	if (!sdata->ht_opmode_valid ||
	    sdata->vif.bss_conf.ht_operation_mode != ht_opmode) {
		changed |= BSS_CHANGED_HT;
		sdata->vif.bss_conf.ht_operation_mode = ht_opmode;
		sdata->ht_opmode_valid = true;
	}

	return changed;
}

/* frame sending functions */

static void ieee80211_send_assoc(struct ieee80211_sub_if_data *sdata,
				 struct ieee80211_mgd_work *wk)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u8 *pos;
	const u8 *ies, *ht_ie;
	int i, len, count, rates_len, supp_rates_len;
	u16 capab;
	int wmm = 0;
	struct ieee80211_supported_band *sband;
	u32 rates = 0;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    sizeof(*mgmt) + 200 + wk->ie_len +
			    wk->ssid_len);
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for assoc "
		       "frame\n", sdata->dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	capab = ifmgd->capab;

	if (local->hw.conf.channel->band == IEEE80211_BAND_2GHZ) {
		if (!(local->hw.flags & IEEE80211_HW_2GHZ_SHORT_SLOT_INCAPABLE))
			capab |= WLAN_CAPABILITY_SHORT_SLOT_TIME;
		if (!(local->hw.flags & IEEE80211_HW_2GHZ_SHORT_PREAMBLE_INCAPABLE))
			capab |= WLAN_CAPABILITY_SHORT_PREAMBLE;
	}

	if (wk->bss->cbss.capability & WLAN_CAPABILITY_PRIVACY)
		capab |= WLAN_CAPABILITY_PRIVACY;
	if (wk->bss->wmm_used)
		wmm = 1;

	/* get all rates supported by the device and the AP as
	 * some APs don't like getting a superset of their rates
	 * in the association request (e.g. D-Link DAP 1353 in
	 * b-only mode) */
	rates_len = ieee80211_compatible_rates(wk->bss, sband, &rates);

	if ((wk->bss->cbss.capability & WLAN_CAPABILITY_SPECTRUM_MGMT) &&
	    (local->hw.flags & IEEE80211_HW_SPECTRUM_MGMT))
		capab |= WLAN_CAPABILITY_SPECTRUM_MGMT;

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, wk->bss->cbss.bssid, ETH_ALEN);
	memcpy(mgmt->sa, sdata->dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, wk->bss->cbss.bssid, ETH_ALEN);

	if (!is_zero_ether_addr(wk->prev_bssid)) {
		skb_put(skb, 10);
		mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						  IEEE80211_STYPE_REASSOC_REQ);
		mgmt->u.reassoc_req.capab_info = cpu_to_le16(capab);
		mgmt->u.reassoc_req.listen_interval =
				cpu_to_le16(local->hw.conf.listen_interval);
		memcpy(mgmt->u.reassoc_req.current_ap, wk->prev_bssid,
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
	ies = pos = skb_put(skb, 2 + wk->ssid_len);
	*pos++ = WLAN_EID_SSID;
	*pos++ = wk->ssid_len;
	memcpy(pos, wk->ssid, wk->ssid_len);

	/* add all rates which were marked to be used above */
	supp_rates_len = rates_len;
	if (supp_rates_len > 8)
		supp_rates_len = 8;

	len = sband->n_bitrates;
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
		*pos++ = local->hw.conf.channel->max_power; /* max tx power */

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

	if (wk->ie_len && wk->ie) {
		pos = skb_put(skb, wk->ie_len);
		memcpy(pos, wk->ie, wk->ie_len);
	}

	if (wmm && (ifmgd->flags & IEEE80211_STA_WMM_ENABLED)) {
		pos = skb_put(skb, 9);
		*pos++ = WLAN_EID_VENDOR_SPECIFIC;
		*pos++ = 7; /* len */
		*pos++ = 0x00; /* Microsoft OUI 00:50:F2 */
		*pos++ = 0x50;
		*pos++ = 0xf2;
		*pos++ = 2; /* WME */
		*pos++ = 0; /* WME info */
		*pos++ = 1; /* WME ver */
		*pos++ = 0;
	}

	/* wmm support is a must to HT */
	/*
	 * IEEE802.11n does not allow TKIP/WEP as pairwise
	 * ciphers in HT mode. We still associate in non-ht
	 * mode (11a/b/g) if any one of these ciphers is
	 * configured as pairwise.
	 */
	if (wmm && (ifmgd->flags & IEEE80211_STA_WMM_ENABLED) &&
	    sband->ht_cap.ht_supported &&
	    (ht_ie = ieee80211_bss_get_ie(&wk->bss->cbss, WLAN_EID_HT_INFORMATION)) &&
	    ht_ie[1] >= sizeof(struct ieee80211_ht_info) &&
	    (!(ifmgd->flags & IEEE80211_STA_DISABLE_11N))) {
		struct ieee80211_ht_info *ht_info =
			(struct ieee80211_ht_info *)(ht_ie + 2);
		u16 cap = sband->ht_cap.cap;
		__le16 tmp;
		u32 flags = local->hw.conf.channel->flags;

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

		tmp = cpu_to_le16(cap);
		pos = skb_put(skb, sizeof(struct ieee80211_ht_cap)+2);
		*pos++ = WLAN_EID_HT_CAPABILITY;
		*pos++ = sizeof(struct ieee80211_ht_cap);
		memset(pos, 0, sizeof(struct ieee80211_ht_cap));
		memcpy(pos, &tmp, sizeof(u16));
		pos += sizeof(u16);
		/* TODO: needs a define here for << 2 */
		*pos++ = sband->ht_cap.ampdu_factor |
			 (sband->ht_cap.ampdu_density << 2);
		memcpy(pos, &sband->ht_cap.mcs, sizeof(sband->ht_cap.mcs));
	}

	ieee80211_tx_skb(sdata, skb, 0);
}


static void ieee80211_send_deauth_disassoc(struct ieee80211_sub_if_data *sdata,
					   const u8 *bssid, u16 stype, u16 reason,
					   void *cookie)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*mgmt));
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for "
		       "deauth/disassoc frame\n", sdata->dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, bssid, ETH_ALEN);
	memcpy(mgmt->sa, sdata->dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | stype);
	skb_put(skb, 2);
	/* u.deauth.reason_code == u.disassoc.reason_code */
	mgmt->u.deauth.reason_code = cpu_to_le16(reason);

	if (stype == IEEE80211_STYPE_DEAUTH)
		cfg80211_send_deauth(sdata->dev, (u8 *)mgmt, skb->len, cookie);
	else
		cfg80211_send_disassoc(sdata->dev, (u8 *)mgmt, skb->len, cookie);
	ieee80211_tx_skb(sdata, skb, ifmgd->flags & IEEE80211_STA_MFP_ENABLED);
}

void ieee80211_send_pspoll(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_pspoll *pspoll;
	struct sk_buff *skb;
	u16 fc;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*pspoll));
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for "
		       "pspoll frame\n", sdata->dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	pspoll = (struct ieee80211_pspoll *) skb_put(skb, sizeof(*pspoll));
	memset(pspoll, 0, sizeof(*pspoll));
	fc = IEEE80211_FTYPE_CTL | IEEE80211_STYPE_PSPOLL | IEEE80211_FCTL_PM;
	pspoll->frame_control = cpu_to_le16(fc);
	pspoll->aid = cpu_to_le16(ifmgd->aid);

	/* aid in PS-Poll has its two MSBs each set to 1 */
	pspoll->aid |= cpu_to_le16(1 << 15 | 1 << 14);

	memcpy(pspoll->bssid, ifmgd->bssid, ETH_ALEN);
	memcpy(pspoll->ta, sdata->dev->dev_addr, ETH_ALEN);

	ieee80211_tx_skb(sdata, skb, 0);
}

void ieee80211_send_nullfunc(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *sdata,
			     int powersave)
{
	struct sk_buff *skb;
	struct ieee80211_hdr *nullfunc;
	__le16 fc;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + 24);
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for nullfunc "
		       "frame\n", sdata->dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	nullfunc = (struct ieee80211_hdr *) skb_put(skb, 24);
	memset(nullfunc, 0, 24);
	fc = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC |
			 IEEE80211_FCTL_TODS);
	if (powersave)
		fc |= cpu_to_le16(IEEE80211_FCTL_PM);
	nullfunc->frame_control = fc;
	memcpy(nullfunc->addr1, sdata->u.mgd.bssid, ETH_ALEN);
	memcpy(nullfunc->addr2, sdata->dev->dev_addr, ETH_ALEN);
	memcpy(nullfunc->addr3, sdata->u.mgd.bssid, ETH_ALEN);

	ieee80211_tx_skb(sdata, skb, 0);
}

/* spectrum management related things */
static void ieee80211_chswitch_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data, u.mgd.chswitch_work);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (!netif_running(sdata->dev))
		return;

	mutex_lock(&ifmgd->mtx);
	if (!ifmgd->associated)
		goto out;

	sdata->local->oper_channel = sdata->local->csa_channel;
	ieee80211_hw_config(sdata->local, IEEE80211_CONF_CHANGE_CHANNEL);

	/* XXX: shouldn't really modify cfg80211-owned data! */
	ifmgd->associated->cbss.channel = sdata->local->oper_channel;

	ieee80211_wake_queues_by_reason(&sdata->local->hw,
					IEEE80211_QUEUE_STOP_REASON_CSA);
 out:
	ifmgd->flags &= ~IEEE80211_STA_CSA_RECEIVED;
	mutex_unlock(&ifmgd->mtx);
}

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
				      struct ieee80211_bss *bss)
{
	struct ieee80211_channel *new_ch;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	int new_freq = ieee80211_channel_to_frequency(sw_elem->new_ch_num);

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

	if (sw_elem->count <= 1) {
		ieee80211_queue_work(&sdata->local->hw, &ifmgd->chswitch_work);
	} else {
		ieee80211_stop_queues_by_reason(&sdata->local->hw,
					IEEE80211_QUEUE_STOP_REASON_CSA);
		ifmgd->flags |= IEEE80211_STA_CSA_RECEIVED;
		mod_timer(&ifmgd->chswitch_timer,
			  jiffies +
			  msecs_to_jiffies(sw_elem->count *
					   bss->cbss.beacon_interval));
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

	if ((*pwr_constr_elem <= conf->channel->max_power) &&
	    (*pwr_constr_elem != sdata->local->power_constr_level)) {
		sdata->local->power_constr_level = *pwr_constr_elem;
		ieee80211_hw_config(sdata->local, 0);
	}
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
	    !(local->hw.flags & IEEE80211_HW_SUPPORTS_DYNAMIC_PS)) {
		mod_timer(&local->dynamic_ps_timer, jiffies +
			  msecs_to_jiffies(conf->dynamic_ps_timeout));
	} else {
		if (local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK)
			ieee80211_send_nullfunc(local, sdata, 1);
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

/* need to hold RTNL or interface lock */
void ieee80211_recalc_ps(struct ieee80211_local *local, s32 latency)
{
	struct ieee80211_sub_if_data *sdata, *found = NULL;
	int count = 0;

	if (!(local->hw.flags & IEEE80211_HW_SUPPORTS_PS)) {
		local->ps_sdata = NULL;
		return;
	}

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!netif_running(sdata->dev))
			continue;
		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			continue;
		found = sdata;
		count++;
	}

	if (count == 1 && found->u.mgd.powersave &&
	    found->u.mgd.associated && list_empty(&found->u.mgd.work_list) &&
	    !(found->u.mgd.flags & (IEEE80211_STA_BEACON_POLL |
				    IEEE80211_STA_CONNECTION_POLL))) {
		s32 beaconint_us;

		if (latency < 0)
			latency = pm_qos_requirement(PM_QOS_NETWORK_LATENCY);

		beaconint_us = ieee80211_tu_to_usec(
					found->vif.bss_conf.beacon_int);

		if (beaconint_us > latency) {
			local->ps_sdata = NULL;
		} else {
			u8 dtimper = found->vif.bss_conf.dtim_period;
			int maxslp = 1;

			if (dtimper > 1)
				maxslp = min_t(int, dtimper,
						    latency / beaconint_us);

			local->hw.conf.max_sleep_period = maxslp;
			local->ps_sdata = found;
		}
	} else {
		local->ps_sdata = NULL;
	}

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

	/* can only happen when PS was just disabled anyway */
	if (!sdata)
		return;

	if (local->hw.conf.flags & IEEE80211_CONF_PS)
		return;

	if (local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK)
		ieee80211_send_nullfunc(local, sdata, 1);

	local->hw.conf.flags |= IEEE80211_CONF_PS;
	ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);
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
				     struct ieee80211_if_managed *ifmgd,
				     u8 *wmm_param, size_t wmm_param_len)
{
	struct ieee80211_tx_queue_params params;
	size_t left;
	int count;
	u8 *pos;

	if (!(ifmgd->flags & IEEE80211_STA_WMM_ENABLED))
		return;

	if (!wmm_param)
		return;

	if (wmm_param_len < 8 || wmm_param[5] /* version */ != 1)
		return;
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
		int queue;

		switch (aci) {
		case 1: /* AC_BK */
			queue = 3;
			if (acm)
				local->wmm_acm |= BIT(1) | BIT(2); /* BK/- */
			break;
		case 2: /* AC_VI */
			queue = 1;
			if (acm)
				local->wmm_acm |= BIT(4) | BIT(5); /* CL/VI */
			break;
		case 3: /* AC_VO */
			queue = 0;
			if (acm)
				local->wmm_acm |= BIT(6) | BIT(7); /* VO/NC */
			break;
		case 0: /* AC_BE */
		default:
			queue = 2;
			if (acm)
				local->wmm_acm |= BIT(0) | BIT(3); /* BE/EE */
			break;
		}

		params.aifs = pos[0] & 0x0f;
		params.cw_max = ecw2cw((pos[1] & 0xf0) >> 4);
		params.cw_min = ecw2cw(pos[1] & 0x0f);
		params.txop = get_unaligned_le16(pos + 2);
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		printk(KERN_DEBUG "%s: WMM queue=%d aci=%d acm=%d aifs=%d "
		       "cWmin=%d cWmax=%d txop=%d\n",
		       wiphy_name(local->hw.wiphy), queue, aci, acm,
		       params.aifs, params.cw_min, params.cw_max, params.txop);
#endif
		if (drv_conf_tx(local, queue, &params) && local->ops->conf_tx)
			printk(KERN_DEBUG "%s: failed to set TX queue "
			       "parameters for queue %d\n",
			       wiphy_name(local->hw.wiphy), queue);
	}
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
				     struct ieee80211_mgd_work *wk,
				     u32 bss_info_changed)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_bss *bss = wk->bss;

	bss_info_changed |= BSS_CHANGED_ASSOC;
	/* set timing information */
	sdata->vif.bss_conf.beacon_int = bss->cbss.beacon_interval;
	sdata->vif.bss_conf.timestamp = bss->cbss.tsf;
	sdata->vif.bss_conf.dtim_period = bss->dtim_period;

	bss_info_changed |= BSS_CHANGED_BEACON_INT;
	bss_info_changed |= ieee80211_handle_bss_capability(sdata,
		bss->cbss.capability, bss->has_erp_value, bss->erp_value);

	sdata->u.mgd.associated = bss;
	sdata->u.mgd.old_associate_work = wk;
	memcpy(sdata->u.mgd.bssid, bss->cbss.bssid, ETH_ALEN);

	/* just to be sure */
	sdata->u.mgd.flags &= ~(IEEE80211_STA_CONNECTION_POLL |
				IEEE80211_STA_BEACON_POLL);

	ieee80211_led_assoc(local, 1);

	sdata->vif.bss_conf.assoc = 1;
	/*
	 * For now just always ask the driver to update the basic rateset
	 * when we have associated, we aren't checking whether it actually
	 * changed or not.
	 */
	bss_info_changed |= BSS_CHANGED_BASIC_RATES;

	/* And the BSSID changed - we're associated now */
	bss_info_changed |= BSS_CHANGED_BSSID;

	ieee80211_bss_info_change_notify(sdata, bss_info_changed);

	mutex_lock(&local->iflist_mtx);
	ieee80211_recalc_ps(local, -1);
	mutex_unlock(&local->iflist_mtx);

	netif_tx_start_all_queues(sdata->dev);
	netif_carrier_on(sdata->dev);
}

static enum rx_mgmt_action __must_check
ieee80211_direct_probe(struct ieee80211_sub_if_data *sdata,
		       struct ieee80211_mgd_work *wk)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;

	wk->tries++;
	if (wk->tries > IEEE80211_AUTH_MAX_TRIES) {
		printk(KERN_DEBUG "%s: direct probe to AP %pM timed out\n",
		       sdata->dev->name, wk->bss->cbss.bssid);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss struct for that AP.
		 */
		cfg80211_unlink_bss(local->hw.wiphy, &wk->bss->cbss);

		/*
		 * We might have a pending scan which had no chance to run yet
		 * due to work needing to be done. Hence, queue the STAs work
		 * again for that.
		 */
		ieee80211_queue_work(&local->hw, &ifmgd->work);
		return RX_MGMT_CFG80211_AUTH_TO;
	}

	printk(KERN_DEBUG "%s: direct probe to AP %pM (try %d)\n",
			sdata->dev->name, wk->bss->cbss.bssid,
			wk->tries);

	/*
	 * Direct probe is sent to broadcast address as some APs
	 * will not answer to direct packet in unassociated state.
	 */
	ieee80211_send_probe_req(sdata, NULL, wk->ssid, wk->ssid_len, NULL, 0);

	wk->timeout = jiffies + IEEE80211_AUTH_TIMEOUT;
	run_again(ifmgd, wk->timeout);

	return RX_MGMT_NONE;
}


static enum rx_mgmt_action __must_check
ieee80211_authenticate(struct ieee80211_sub_if_data *sdata,
		       struct ieee80211_mgd_work *wk)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;

	wk->tries++;
	if (wk->tries > IEEE80211_AUTH_MAX_TRIES) {
		printk(KERN_DEBUG "%s: authentication with AP %pM"
		       " timed out\n",
		       sdata->dev->name, wk->bss->cbss.bssid);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss struct for that AP.
		 */
		cfg80211_unlink_bss(local->hw.wiphy, &wk->bss->cbss);

		/*
		 * We might have a pending scan which had no chance to run yet
		 * due to work needing to be done. Hence, queue the STAs work
		 * again for that.
		 */
		ieee80211_queue_work(&local->hw, &ifmgd->work);
		return RX_MGMT_CFG80211_AUTH_TO;
	}

	printk(KERN_DEBUG "%s: authenticate with AP %pM (try %d)\n",
	       sdata->dev->name, wk->bss->cbss.bssid, wk->tries);

	ieee80211_send_auth(sdata, 1, wk->auth_alg, wk->ie, wk->ie_len,
			    wk->bss->cbss.bssid, NULL, 0, 0);
	wk->auth_transaction = 2;

	wk->timeout = jiffies + IEEE80211_AUTH_TIMEOUT;
	run_again(ifmgd, wk->timeout);

	return RX_MGMT_NONE;
}

static void ieee80211_set_disassoc(struct ieee80211_sub_if_data *sdata,
				   bool deauth)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	u32 changed = 0, config_changed = 0;
	u8 bssid[ETH_ALEN];

	ASSERT_MGD_MTX(ifmgd);

	if (WARN_ON(!ifmgd->associated))
		return;

	memcpy(bssid, ifmgd->associated->cbss.bssid, ETH_ALEN);

	ifmgd->associated = NULL;
	memset(ifmgd->bssid, 0, ETH_ALEN);

	if (deauth) {
		kfree(ifmgd->old_associate_work);
		ifmgd->old_associate_work = NULL;
	} else {
		struct ieee80211_mgd_work *wk = ifmgd->old_associate_work;

		wk->state = IEEE80211_MGD_STATE_IDLE;
		list_add(&wk->list, &ifmgd->work_list);
	}

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

	rcu_read_lock();
	sta = sta_info_get(local, bssid);
	if (sta)
		ieee80211_sta_tear_down_BA_sessions(sta);
	rcu_read_unlock();

	changed |= ieee80211_reset_erp_info(sdata);

	ieee80211_led_assoc(local, 0);
	changed |= BSS_CHANGED_ASSOC;
	sdata->vif.bss_conf.assoc = false;

	ieee80211_set_wmm_default(sdata);

	ieee80211_recalc_idle(local);

	/* channel(_type) changes are handled by ieee80211_hw_config */
	local->oper_channel_type = NL80211_CHAN_NO_HT;

	/* on the next assoc, re-program HT parameters */
	sdata->ht_opmode_valid = false;

	local->power_constr_level = 0;

	del_timer_sync(&local->dynamic_ps_timer);
	cancel_work_sync(&local->dynamic_ps_enable_work);

	if (local->hw.conf.flags & IEEE80211_CONF_PS) {
		local->hw.conf.flags &= ~IEEE80211_CONF_PS;
		config_changed |= IEEE80211_CONF_CHANGE_PS;
	}

	ieee80211_hw_config(local, config_changed);

	/* And the BSSID changed -- not very interesting here */
	changed |= BSS_CHANGED_BSSID;
	ieee80211_bss_info_change_notify(sdata, changed);

	rcu_read_lock();

	sta = sta_info_get(local, bssid);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	sta_info_unlink(&sta);

	rcu_read_unlock();

	sta_info_destroy(sta);
}

static enum rx_mgmt_action __must_check
ieee80211_associate(struct ieee80211_sub_if_data *sdata,
		    struct ieee80211_mgd_work *wk)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;

	wk->tries++;
	if (wk->tries > IEEE80211_ASSOC_MAX_TRIES) {
		printk(KERN_DEBUG "%s: association with AP %pM"
		       " timed out\n",
		       sdata->dev->name, wk->bss->cbss.bssid);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss struct for that AP.
		 */
		cfg80211_unlink_bss(local->hw.wiphy, &wk->bss->cbss);

		/*
		 * We might have a pending scan which had no chance to run yet
		 * due to work needing to be done. Hence, queue the STAs work
		 * again for that.
		 */
		ieee80211_queue_work(&local->hw, &ifmgd->work);
		return RX_MGMT_CFG80211_ASSOC_TO;
	}

	printk(KERN_DEBUG "%s: associate with AP %pM (try %d)\n",
	       sdata->dev->name, wk->bss->cbss.bssid, wk->tries);
	ieee80211_send_assoc(sdata, wk);

	wk->timeout = jiffies + IEEE80211_ASSOC_TIMEOUT;
	run_again(ifmgd, wk->timeout);

	return RX_MGMT_NONE;
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

	mod_timer(&sdata->u.mgd.conn_mon_timer,
		  round_jiffies_up(jiffies + IEEE80211_CONNECTION_IDLE_TIME));
}

static void ieee80211_mgd_probe_ap_send(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	const u8 *ssid;

	ssid = ieee80211_bss_get_ie(&ifmgd->associated->cbss, WLAN_EID_SSID);
	ieee80211_send_probe_req(sdata, ifmgd->associated->cbss.bssid,
				 ssid + 2, ssid[1], NULL, 0);

	ifmgd->probe_send_count++;
	ifmgd->probe_timeout = jiffies + IEEE80211_PROBE_WAIT;
	run_again(ifmgd, ifmgd->probe_timeout);
}

static void ieee80211_mgd_probe_ap(struct ieee80211_sub_if_data *sdata,
				   bool beacon)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	bool already = false;

	if (!netif_running(sdata->dev))
		return;

	if (sdata->local->scanning)
		return;

	mutex_lock(&ifmgd->mtx);

	if (!ifmgd->associated)
		goto out;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	if (beacon && net_ratelimit())
		printk(KERN_DEBUG "%s: detected beacon loss from AP "
		       "- sending probe request\n", sdata->dev->name);
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

void ieee80211_beacon_loss_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data,
			     u.mgd.beacon_loss_work);

	ieee80211_mgd_probe_ap(sdata, true);
}

void ieee80211_beacon_loss(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	ieee80211_queue_work(&sdata->local->hw, &sdata->u.mgd.beacon_loss_work);
}
EXPORT_SYMBOL(ieee80211_beacon_loss);

static void ieee80211_auth_completed(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgd_work *wk)
{
	wk->state = IEEE80211_MGD_STATE_IDLE;
	printk(KERN_DEBUG "%s: authenticated\n", sdata->dev->name);
}


static void ieee80211_auth_challenge(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgd_work *wk,
				     struct ieee80211_mgmt *mgmt,
				     size_t len)
{
	u8 *pos;
	struct ieee802_11_elems elems;

	pos = mgmt->u.auth.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), &elems);
	if (!elems.challenge)
		return;
	ieee80211_send_auth(sdata, 3, wk->auth_alg,
			    elems.challenge - 2, elems.challenge_len + 2,
			    wk->bss->cbss.bssid,
			    wk->key, wk->key_len, wk->key_idx);
	wk->auth_transaction = 4;
}

static enum rx_mgmt_action __must_check
ieee80211_rx_mgmt_auth(struct ieee80211_sub_if_data *sdata,
		       struct ieee80211_mgd_work *wk,
		       struct ieee80211_mgmt *mgmt, size_t len)
{
	u16 auth_alg, auth_transaction, status_code;

	if (wk->state != IEEE80211_MGD_STATE_AUTH)
		return RX_MGMT_NONE;

	if (len < 24 + 6)
		return RX_MGMT_NONE;

	if (memcmp(wk->bss->cbss.bssid, mgmt->sa, ETH_ALEN) != 0)
		return RX_MGMT_NONE;

	if (memcmp(wk->bss->cbss.bssid, mgmt->bssid, ETH_ALEN) != 0)
		return RX_MGMT_NONE;

	auth_alg = le16_to_cpu(mgmt->u.auth.auth_alg);
	auth_transaction = le16_to_cpu(mgmt->u.auth.auth_transaction);
	status_code = le16_to_cpu(mgmt->u.auth.status_code);

	if (auth_alg != wk->auth_alg ||
	    auth_transaction != wk->auth_transaction)
		return RX_MGMT_NONE;

	if (status_code != WLAN_STATUS_SUCCESS) {
		list_del(&wk->list);
		kfree(wk);
		return RX_MGMT_CFG80211_AUTH;
	}

	switch (wk->auth_alg) {
	case WLAN_AUTH_OPEN:
	case WLAN_AUTH_LEAP:
	case WLAN_AUTH_FT:
		ieee80211_auth_completed(sdata, wk);
		return RX_MGMT_CFG80211_AUTH;
	case WLAN_AUTH_SHARED_KEY:
		if (wk->auth_transaction == 4) {
			ieee80211_auth_completed(sdata, wk);
			return RX_MGMT_CFG80211_AUTH;
		} else
			ieee80211_auth_challenge(sdata, wk, mgmt, len);
		break;
	}

	return RX_MGMT_NONE;
}


static enum rx_mgmt_action __must_check
ieee80211_rx_mgmt_deauth(struct ieee80211_sub_if_data *sdata,
			 struct ieee80211_mgd_work *wk,
			 struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	const u8 *bssid = NULL;
	u16 reason_code;

	if (len < 24 + 2)
		return RX_MGMT_NONE;

	ASSERT_MGD_MTX(ifmgd);

	if (wk)
		bssid = wk->bss->cbss.bssid;
	else
		bssid = ifmgd->associated->cbss.bssid;

	reason_code = le16_to_cpu(mgmt->u.deauth.reason_code);

	printk(KERN_DEBUG "%s: deauthenticated from %pM (Reason: %u)\n",
			sdata->dev->name, bssid, reason_code);

	if (!wk) {
		ieee80211_set_disassoc(sdata, true);
	} else {
		list_del(&wk->list);
		kfree(wk);
	}

	return RX_MGMT_CFG80211_DEAUTH;
}


static enum rx_mgmt_action __must_check
ieee80211_rx_mgmt_disassoc(struct ieee80211_sub_if_data *sdata,
			   struct ieee80211_mgmt *mgmt, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u16 reason_code;

	if (len < 24 + 2)
		return RX_MGMT_NONE;

	ASSERT_MGD_MTX(ifmgd);

	if (WARN_ON(!ifmgd->associated))
		return RX_MGMT_NONE;

	if (WARN_ON(memcmp(ifmgd->associated->cbss.bssid, mgmt->sa, ETH_ALEN)))
		return RX_MGMT_NONE;

	reason_code = le16_to_cpu(mgmt->u.disassoc.reason_code);

	printk(KERN_DEBUG "%s: disassociated from %pM (Reason: %u)\n",
			sdata->dev->name, mgmt->sa, reason_code);

	ieee80211_set_disassoc(sdata, false);
	return RX_MGMT_CFG80211_DISASSOC;
}


static enum rx_mgmt_action __must_check
ieee80211_rx_mgmt_assoc_resp(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_mgd_work *wk,
			     struct ieee80211_mgmt *mgmt, size_t len,
			     bool reassoc)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	struct sta_info *sta;
	u32 rates, basic_rates;
	u16 capab_info, status_code, aid;
	struct ieee802_11_elems elems;
	struct ieee80211_bss_conf *bss_conf = &sdata->vif.bss_conf;
	u8 *pos;
	u32 changed = 0;
	int i, j;
	bool have_higher_than_11mbit = false, newsta = false;
	u16 ap_ht_cap_flags;

	/*
	 * AssocResp and ReassocResp have identical structure, so process both
	 * of them in this function.
	 */

	if (len < 24 + 6)
		return RX_MGMT_NONE;

	if (memcmp(wk->bss->cbss.bssid, mgmt->sa, ETH_ALEN) != 0)
		return RX_MGMT_NONE;

	capab_info = le16_to_cpu(mgmt->u.assoc_resp.capab_info);
	status_code = le16_to_cpu(mgmt->u.assoc_resp.status_code);
	aid = le16_to_cpu(mgmt->u.assoc_resp.aid);

	printk(KERN_DEBUG "%s: RX %sssocResp from %pM (capab=0x%x "
	       "status=%d aid=%d)\n",
	       sdata->dev->name, reassoc ? "Rea" : "A", mgmt->sa,
	       capab_info, status_code, (u16)(aid & ~(BIT(15) | BIT(14))));

	pos = mgmt->u.assoc_resp.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), &elems);

	if (status_code == WLAN_STATUS_ASSOC_REJECTED_TEMPORARILY &&
	    elems.timeout_int && elems.timeout_int_len == 5 &&
	    elems.timeout_int[0] == WLAN_TIMEOUT_ASSOC_COMEBACK) {
		u32 tu, ms;
		tu = get_unaligned_le32(elems.timeout_int + 1);
		ms = tu * 1024 / 1000;
		printk(KERN_DEBUG "%s: AP rejected association temporarily; "
		       "comeback duration %u TU (%u ms)\n",
		       sdata->dev->name, tu, ms);
		wk->timeout = jiffies + msecs_to_jiffies(ms);
		if (ms > IEEE80211_ASSOC_TIMEOUT)
			run_again(ifmgd, jiffies + msecs_to_jiffies(ms));
		return RX_MGMT_NONE;
	}

	if (status_code != WLAN_STATUS_SUCCESS) {
		printk(KERN_DEBUG "%s: AP denied association (code=%d)\n",
		       sdata->dev->name, status_code);
		wk->state = IEEE80211_MGD_STATE_IDLE;
		return RX_MGMT_CFG80211_ASSOC;
	}

	if ((aid & (BIT(15) | BIT(14))) != (BIT(15) | BIT(14)))
		printk(KERN_DEBUG "%s: invalid aid value %d; bits 15:14 not "
		       "set\n", sdata->dev->name, aid);
	aid &= ~(BIT(15) | BIT(14));

	if (!elems.supp_rates) {
		printk(KERN_DEBUG "%s: no SuppRates element in AssocResp\n",
		       sdata->dev->name);
		return RX_MGMT_NONE;
	}

	printk(KERN_DEBUG "%s: associated\n", sdata->dev->name);
	ifmgd->aid = aid;

	rcu_read_lock();

	/* Add STA entry for the AP */
	sta = sta_info_get(local, wk->bss->cbss.bssid);
	if (!sta) {
		newsta = true;

		rcu_read_unlock();

		sta = sta_info_alloc(sdata, wk->bss->cbss.bssid, GFP_KERNEL);
		if (!sta) {
			printk(KERN_DEBUG "%s: failed to alloc STA entry for"
			       " the AP\n", sdata->dev->name);
			return RX_MGMT_NONE;
		}

		set_sta_flags(sta, WLAN_STA_AUTH | WLAN_STA_ASSOC |
				   WLAN_STA_ASSOC_AP);
		if (!(ifmgd->flags & IEEE80211_STA_CONTROL_PORT))
			set_sta_flags(sta, WLAN_STA_AUTHORIZED);

		rcu_read_lock();
	}

	rates = 0;
	basic_rates = 0;
	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	for (i = 0; i < elems.supp_rates_len; i++) {
		int rate = (elems.supp_rates[i] & 0x7f) * 5;
		bool is_basic = !!(elems.supp_rates[i] & 0x80);

		if (rate > 110)
			have_higher_than_11mbit = true;

		for (j = 0; j < sband->n_bitrates; j++) {
			if (sband->bitrates[j].bitrate == rate) {
				rates |= BIT(j);
				if (is_basic)
					basic_rates |= BIT(j);
				break;
			}
		}
	}

	for (i = 0; i < elems.ext_supp_rates_len; i++) {
		int rate = (elems.ext_supp_rates[i] & 0x7f) * 5;
		bool is_basic = !!(elems.ext_supp_rates[i] & 0x80);

		if (rate > 110)
			have_higher_than_11mbit = true;

		for (j = 0; j < sband->n_bitrates; j++) {
			if (sband->bitrates[j].bitrate == rate) {
				rates |= BIT(j);
				if (is_basic)
					basic_rates |= BIT(j);
				break;
			}
		}
	}

	sta->sta.supp_rates[local->hw.conf.channel->band] = rates;
	sdata->vif.bss_conf.basic_rates = basic_rates;

	/* cf. IEEE 802.11 9.2.12 */
	if (local->hw.conf.channel->band == IEEE80211_BAND_2GHZ &&
	    have_higher_than_11mbit)
		sdata->flags |= IEEE80211_SDATA_OPERATING_GMODE;
	else
		sdata->flags &= ~IEEE80211_SDATA_OPERATING_GMODE;

	if (elems.ht_cap_elem && !(ifmgd->flags & IEEE80211_STA_DISABLE_11N))
		ieee80211_ht_cap_ie_to_sta_ht_cap(sband,
				elems.ht_cap_elem, &sta->sta.ht_cap);

	ap_ht_cap_flags = sta->sta.ht_cap.cap;

	rate_control_rate_init(sta);

	if (ifmgd->flags & IEEE80211_STA_MFP_ENABLED)
		set_sta_flags(sta, WLAN_STA_MFP);

	if (elems.wmm_param)
		set_sta_flags(sta, WLAN_STA_WME);

	if (newsta) {
		int err = sta_info_insert(sta);
		if (err) {
			printk(KERN_DEBUG "%s: failed to insert STA entry for"
			       " the AP (error %d)\n", sdata->dev->name, err);
			rcu_read_unlock();
			return RX_MGMT_NONE;
		}
	}

	rcu_read_unlock();

	if (elems.wmm_param)
		ieee80211_sta_wmm_params(local, ifmgd, elems.wmm_param,
					 elems.wmm_param_len);
	else
		ieee80211_set_wmm_default(sdata);

	if (elems.ht_info_elem && elems.wmm_param &&
	    (ifmgd->flags & IEEE80211_STA_WMM_ENABLED) &&
	    !(ifmgd->flags & IEEE80211_STA_DISABLE_11N))
		changed |= ieee80211_enable_ht(sdata, elems.ht_info_elem,
					       wk->bss->cbss.bssid,
					       ap_ht_cap_flags);

        /* delete work item -- must be before set_associated for PS */
	list_del(&wk->list);

	/* set AID and assoc capability,
	 * ieee80211_set_associated() will tell the driver */
	bss_conf->aid = aid;
	bss_conf->assoc_capability = capab_info;
	/* this will take ownership of wk */
	ieee80211_set_associated(sdata, wk, changed);

	/*
	 * Start timer to probe the connection to the AP now.
	 * Also start the timer that will detect beacon loss.
	 */
	ieee80211_sta_rx_notify(sdata, (struct ieee80211_hdr *)mgmt);
	mod_beacon_timer(sdata);

	return RX_MGMT_CFG80211_ASSOC;
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

	if (elems->ds_params && elems->ds_params_len == 1)
		freq = ieee80211_channel_to_frequency(elems->ds_params[0]);
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

	if (elems->ch_switch_elem && (elems->ch_switch_elem_len == 3) &&
	    (memcmp(mgmt->bssid, sdata->u.mgd.associated->cbss.bssid,
							ETH_ALEN) == 0)) {
		struct ieee80211_channel_sw_ie *sw_elem =
			(struct ieee80211_channel_sw_ie *)elems->ch_switch_elem;
		ieee80211_sta_process_chanswitch(sdata, sw_elem, bss);
	}
}


static void ieee80211_rx_mgmt_probe_resp(struct ieee80211_sub_if_data *sdata,
					 struct ieee80211_mgd_work *wk,
					 struct ieee80211_mgmt *mgmt, size_t len,
					 struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_if_managed *ifmgd;
	size_t baselen;
	struct ieee802_11_elems elems;

	ifmgd = &sdata->u.mgd;

	ASSERT_MGD_MTX(ifmgd);

	if (memcmp(mgmt->da, sdata->dev->dev_addr, ETH_ALEN))
		return; /* ignore ProbeResp to foreign address */

	baselen = (u8 *) mgmt->u.probe_resp.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	ieee802_11_parse_elems(mgmt->u.probe_resp.variable, len - baselen,
				&elems);

	ieee80211_rx_bss_info(sdata, mgmt, len, rx_status, &elems, false);

	/* direct probe may be part of the association flow */
	if (wk && wk->state == IEEE80211_MGD_STATE_PROBE) {
		printk(KERN_DEBUG "%s: direct probe responded\n",
		       sdata->dev->name);
		wk->tries = 0;
		wk->state = IEEE80211_MGD_STATE_AUTH;
		WARN_ON(ieee80211_authenticate(sdata, wk) != RX_MGMT_NONE);
	}

	if (ifmgd->associated &&
	    memcmp(mgmt->bssid, ifmgd->associated->cbss.bssid, ETH_ALEN) == 0 &&
	    ifmgd->flags & (IEEE80211_STA_BEACON_POLL |
			    IEEE80211_STA_CONNECTION_POLL)) {
		ifmgd->flags &= ~(IEEE80211_STA_CONNECTION_POLL |
				  IEEE80211_STA_BEACON_POLL);
		mutex_lock(&sdata->local->iflist_mtx);
		ieee80211_recalc_ps(sdata->local, -1);
		mutex_unlock(&sdata->local->iflist_mtx);
		/*
		 * We've received a probe response, but are not sure whether
		 * we have or will be receiving any beacons or data, so let's
		 * schedule the timers again, just in case.
		 */
		mod_beacon_timer(sdata);
		mod_timer(&ifmgd->conn_mon_timer,
			  round_jiffies_up(jiffies +
					   IEEE80211_CONNECTION_IDLE_TIME));
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
	size_t baselen;
	struct ieee802_11_elems elems;
	struct ieee80211_local *local = sdata->local;
	u32 changed = 0;
	bool erp_valid, directed_tim = false;
	u8 erp_value = 0;
	u32 ncrc;
	u8 *bssid;

	ASSERT_MGD_MTX(ifmgd);

	/* Process beacon from the current BSS */
	baselen = (u8 *) mgmt->u.beacon.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	if (rx_status->freq != local->hw.conf.channel->center_freq)
		return;

	/*
	 * We might have received a number of frames, among them a
	 * disassoc frame and a beacon...
	 */
	if (!ifmgd->associated)
		return;

	bssid = ifmgd->associated->cbss.bssid;

	/*
	 * And in theory even frames from a different AP we were just
	 * associated to a split-second ago!
	 */
	if (memcmp(bssid, mgmt->bssid, ETH_ALEN) != 0)
		return;

	if (ifmgd->flags & IEEE80211_STA_BEACON_POLL) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: cancelling probereq poll due "
			       "to a received beacon\n", sdata->dev->name);
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
	mod_beacon_timer(sdata);

	ncrc = crc32_be(0, (void *)&mgmt->u.beacon.beacon_int, 4);
	ncrc = ieee802_11_parse_elems_crc(mgmt->u.beacon.variable,
					  len - baselen, &elems,
					  care_about_ies, ncrc);

	if (local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK)
		directed_tim = ieee80211_check_tim(elems.tim, elems.tim_len,
						   ifmgd->aid);

	if (ncrc != ifmgd->beacon_crc) {
		ieee80211_rx_bss_info(sdata, mgmt, len, rx_status, &elems,
				      true);

		ieee80211_sta_wmm_params(local, ifmgd, elems.wmm_param,
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

	if (ncrc == ifmgd->beacon_crc)
		return;
	ifmgd->beacon_crc = ncrc;

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

		sta = sta_info_get(local, bssid);
		if (WARN_ON(!sta)) {
			rcu_read_unlock();
			return;
		}

		sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

		ieee80211_ht_cap_ie_to_sta_ht_cap(sband,
				elems.ht_cap_elem, &sta->sta.ht_cap);

		ap_ht_cap_flags = sta->sta.ht_cap.cap;

		rcu_read_unlock();

		changed |= ieee80211_enable_ht(sdata, elems.ht_info_elem,
					       bssid, ap_ht_cap_flags);
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

ieee80211_rx_result ieee80211_sta_rx_mgmt(struct ieee80211_sub_if_data *sdata,
					  struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_mgmt *mgmt;
	u16 fc;

	if (skb->len < 24)
		return RX_DROP_MONITOR;

	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	switch (fc & IEEE80211_FCTL_STYPE) {
	case IEEE80211_STYPE_PROBE_REQ:
	case IEEE80211_STYPE_PROBE_RESP:
	case IEEE80211_STYPE_BEACON:
	case IEEE80211_STYPE_AUTH:
	case IEEE80211_STYPE_ASSOC_RESP:
	case IEEE80211_STYPE_REASSOC_RESP:
	case IEEE80211_STYPE_DEAUTH:
	case IEEE80211_STYPE_DISASSOC:
	case IEEE80211_STYPE_ACTION:
		skb_queue_tail(&sdata->u.mgd.skb_queue, skb);
		ieee80211_queue_work(&local->hw, &sdata->u.mgd.work);
		return RX_QUEUED;
	}

	return RX_DROP_MONITOR;
}

static void ieee80211_sta_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
					 struct sk_buff *skb)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_mgd_work *wk;
	enum rx_mgmt_action rma = RX_MGMT_NONE;
	u16 fc;

	rx_status = (struct ieee80211_rx_status *) skb->cb;
	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	mutex_lock(&ifmgd->mtx);

	if (ifmgd->associated &&
	    memcmp(ifmgd->associated->cbss.bssid, mgmt->bssid,
							ETH_ALEN) == 0) {
		switch (fc & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_BEACON:
			ieee80211_rx_mgmt_beacon(sdata, mgmt, skb->len,
						 rx_status);
			break;
		case IEEE80211_STYPE_PROBE_RESP:
			ieee80211_rx_mgmt_probe_resp(sdata, NULL, mgmt,
						     skb->len, rx_status);
			break;
		case IEEE80211_STYPE_DEAUTH:
			rma = ieee80211_rx_mgmt_deauth(sdata, NULL,
						       mgmt, skb->len);
			break;
		case IEEE80211_STYPE_DISASSOC:
			rma = ieee80211_rx_mgmt_disassoc(sdata, mgmt, skb->len);
			break;
		case IEEE80211_STYPE_ACTION:
			/* XXX: differentiate, can only happen for CSA now! */
			ieee80211_sta_process_chanswitch(sdata,
					&mgmt->u.action.u.chan_switch.sw_elem,
					ifmgd->associated);
			break;
		}
		mutex_unlock(&ifmgd->mtx);

		switch (rma) {
		case RX_MGMT_NONE:
			/* no action */
			break;
		case RX_MGMT_CFG80211_DEAUTH:
			cfg80211_send_deauth(sdata->dev, (u8 *)mgmt, skb->len,
					     NULL);
			break;
		case RX_MGMT_CFG80211_DISASSOC:
			cfg80211_send_disassoc(sdata->dev, (u8 *)mgmt, skb->len,
					       NULL);
			break;
		default:
			WARN(1, "unexpected: %d", rma);
		}
		goto out;
	}

	list_for_each_entry(wk, &ifmgd->work_list, list) {
		if (memcmp(wk->bss->cbss.bssid, mgmt->bssid, ETH_ALEN) != 0)
			continue;

		switch (fc & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_PROBE_RESP:
			ieee80211_rx_mgmt_probe_resp(sdata, wk, mgmt, skb->len,
						     rx_status);
			break;
		case IEEE80211_STYPE_AUTH:
			rma = ieee80211_rx_mgmt_auth(sdata, wk, mgmt, skb->len);
			break;
		case IEEE80211_STYPE_ASSOC_RESP:
			rma = ieee80211_rx_mgmt_assoc_resp(sdata, wk, mgmt,
							   skb->len, false);
			break;
		case IEEE80211_STYPE_REASSOC_RESP:
			rma = ieee80211_rx_mgmt_assoc_resp(sdata, wk, mgmt,
							   skb->len, true);
			break;
		case IEEE80211_STYPE_DEAUTH:
			rma = ieee80211_rx_mgmt_deauth(sdata, wk, mgmt,
						       skb->len);
			break;
		}
		/*
		 * We've processed this frame for that work, so it can't
		 * belong to another work struct.
		 * NB: this is also required for correctness because the
		 * called functions can free 'wk', and for 'rma'!
		 */
		break;
	}

	mutex_unlock(&ifmgd->mtx);

	switch (rma) {
	case RX_MGMT_NONE:
		/* no action */
		break;
	case RX_MGMT_CFG80211_AUTH:
		cfg80211_send_rx_auth(sdata->dev, (u8 *) mgmt, skb->len);
		break;
	case RX_MGMT_CFG80211_ASSOC:
		cfg80211_send_rx_assoc(sdata->dev, (u8 *) mgmt, skb->len);
		break;
	case RX_MGMT_CFG80211_DEAUTH:
		cfg80211_send_deauth(sdata->dev, (u8 *)mgmt, skb->len, NULL);
		break;
	default:
		WARN(1, "unexpected: %d", rma);
	}

 out:
	kfree_skb(skb);
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

	ieee80211_queue_work(&local->hw, &ifmgd->work);
}

static void ieee80211_sta_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data, u.mgd.work);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd;
	struct sk_buff *skb;
	struct ieee80211_mgd_work *wk, *tmp;
	LIST_HEAD(free_work);
	enum rx_mgmt_action rma;
	bool anybusy = false;

	if (!netif_running(sdata->dev))
		return;

	if (local->scanning)
		return;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return;

	/*
	 * ieee80211_queue_work() should have picked up most cases,
	 * here we'll pick the the rest.
	 */
	if (WARN(local->suspended, "STA MLME work scheduled while "
		 "going to suspend\n"))
		return;

	ifmgd = &sdata->u.mgd;

	/* first process frames to avoid timing out while a frame is pending */
	while ((skb = skb_dequeue(&ifmgd->skb_queue)))
		ieee80211_sta_rx_queued_mgmt(sdata, skb);

	/* then process the rest of the work */
	mutex_lock(&ifmgd->mtx);

	if (ifmgd->flags & (IEEE80211_STA_BEACON_POLL |
			    IEEE80211_STA_CONNECTION_POLL) &&
	    ifmgd->associated) {
		u8 bssid[ETH_ALEN];

		memcpy(bssid, ifmgd->associated->cbss.bssid, ETH_ALEN);
		if (time_is_after_jiffies(ifmgd->probe_timeout))
			run_again(ifmgd, ifmgd->probe_timeout);

		else if (ifmgd->probe_send_count < IEEE80211_MAX_PROBE_TRIES) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
			printk(KERN_DEBUG "No probe response from AP %pM"
				" after %dms, try %d\n", bssid,
				(1000 * IEEE80211_PROBE_WAIT)/HZ,
				ifmgd->probe_send_count);
#endif
			ieee80211_mgd_probe_ap_send(sdata);
		} else {
			/*
			 * We actually lost the connection ... or did we?
			 * Let's make sure!
			 */
			ifmgd->flags &= ~(IEEE80211_STA_CONNECTION_POLL |
					  IEEE80211_STA_BEACON_POLL);
			printk(KERN_DEBUG "No probe response from AP %pM"
				" after %dms, disconnecting.\n",
				bssid, (1000 * IEEE80211_PROBE_WAIT)/HZ);
			ieee80211_set_disassoc(sdata, true);
			mutex_unlock(&ifmgd->mtx);
			/*
			 * must be outside lock due to cfg80211,
			 * but that's not a problem.
			 */
			ieee80211_send_deauth_disassoc(sdata, bssid,
					IEEE80211_STYPE_DEAUTH,
					WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY,
					NULL);
			mutex_lock(&ifmgd->mtx);
		}
	}


	ieee80211_recalc_idle(local);

	list_for_each_entry_safe(wk, tmp, &ifmgd->work_list, list) {
		if (time_is_after_jiffies(wk->timeout)) {
			/*
			 * This work item isn't supposed to be worked on
			 * right now, but take care to adjust the timer
			 * properly.
			 */
			run_again(ifmgd, wk->timeout);
			continue;
		}

		switch (wk->state) {
		default:
			WARN_ON(1);
			/* fall through */
		case IEEE80211_MGD_STATE_IDLE:
			/* nothing */
			rma = RX_MGMT_NONE;
			break;
		case IEEE80211_MGD_STATE_PROBE:
			rma = ieee80211_direct_probe(sdata, wk);
			break;
		case IEEE80211_MGD_STATE_AUTH:
			rma = ieee80211_authenticate(sdata, wk);
			break;
		case IEEE80211_MGD_STATE_ASSOC:
			rma = ieee80211_associate(sdata, wk);
			break;
		}

		switch (rma) {
		case RX_MGMT_NONE:
			/* no action required */
			break;
		case RX_MGMT_CFG80211_AUTH_TO:
		case RX_MGMT_CFG80211_ASSOC_TO:
			list_del(&wk->list);
			list_add(&wk->list, &free_work);
			wk->tries = rma; /* small abuse but only local */
			break;
		default:
			WARN(1, "unexpected: %d", rma);
		}
	}

	list_for_each_entry(wk, &ifmgd->work_list, list) {
		if (wk->state != IEEE80211_MGD_STATE_IDLE) {
			anybusy = true;
			break;
		}
	}
	if (!anybusy &&
	    test_and_clear_bit(IEEE80211_STA_REQ_SCAN, &ifmgd->request))
		ieee80211_queue_delayed_work(&local->hw,
					     &local->scan_work,
					     round_jiffies_relative(0));

	mutex_unlock(&ifmgd->mtx);

	list_for_each_entry_safe(wk, tmp, &free_work, list) {
		switch (wk->tries) {
		case RX_MGMT_CFG80211_AUTH_TO:
			cfg80211_send_auth_timeout(sdata->dev,
						   wk->bss->cbss.bssid);
			break;
		case RX_MGMT_CFG80211_ASSOC_TO:
			cfg80211_send_assoc_timeout(sdata->dev,
						    wk->bss->cbss.bssid);
			break;
		default:
			WARN(1, "unexpected: %d", wk->tries);
		}

		list_del(&wk->list);
		kfree(wk);
	}

	ieee80211_recalc_idle(local);
}

static void ieee80211_sta_bcn_mon_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata =
		(struct ieee80211_sub_if_data *) data;
	struct ieee80211_local *local = sdata->local;

	if (local->quiescing)
		return;

	ieee80211_queue_work(&sdata->local->hw, &sdata->u.mgd.beacon_loss_work);
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
	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		sdata->u.mgd.flags &= ~(IEEE80211_STA_BEACON_POLL |
					IEEE80211_STA_CONNECTION_POLL);

		/* let's probe the connection once */
		ieee80211_queue_work(&sdata->local->hw,
			   &sdata->u.mgd.monitor_work);
		/* and do all the other regular work too */
		ieee80211_queue_work(&sdata->local->hw,
			   &sdata->u.mgd.work);
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

	cancel_work_sync(&ifmgd->work);
	cancel_work_sync(&ifmgd->beacon_loss_work);
	if (del_timer_sync(&ifmgd->timer))
		set_bit(TMR_RUNNING_TIMER, &ifmgd->timers_running);

	cancel_work_sync(&ifmgd->chswitch_work);
	if (del_timer_sync(&ifmgd->chswitch_timer))
		set_bit(TMR_RUNNING_CHANSW, &ifmgd->timers_running);

	cancel_work_sync(&ifmgd->monitor_work);
	/* these will just be re-established on connection */
	del_timer_sync(&ifmgd->conn_mon_timer);
	del_timer_sync(&ifmgd->bcn_mon_timer);
}

void ieee80211_sta_restart(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (test_and_clear_bit(TMR_RUNNING_TIMER, &ifmgd->timers_running))
		add_timer(&ifmgd->timer);
	if (test_and_clear_bit(TMR_RUNNING_CHANSW, &ifmgd->timers_running))
		add_timer(&ifmgd->chswitch_timer);
}
#endif

/* interface setup */
void ieee80211_sta_setup_sdata(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd;

	ifmgd = &sdata->u.mgd;
	INIT_WORK(&ifmgd->work, ieee80211_sta_work);
	INIT_WORK(&ifmgd->monitor_work, ieee80211_sta_monitor_work);
	INIT_WORK(&ifmgd->chswitch_work, ieee80211_chswitch_work);
	INIT_WORK(&ifmgd->beacon_loss_work, ieee80211_beacon_loss_work);
	setup_timer(&ifmgd->timer, ieee80211_sta_timer,
		    (unsigned long) sdata);
	setup_timer(&ifmgd->bcn_mon_timer, ieee80211_sta_bcn_mon_timer,
		    (unsigned long) sdata);
	setup_timer(&ifmgd->conn_mon_timer, ieee80211_sta_conn_mon_timer,
		    (unsigned long) sdata);
	setup_timer(&ifmgd->chswitch_timer, ieee80211_chswitch_timer,
		    (unsigned long) sdata);
	skb_queue_head_init(&ifmgd->skb_queue);

	INIT_LIST_HEAD(&ifmgd->work_list);

	ifmgd->capab = WLAN_CAPABILITY_ESS;
	ifmgd->flags = 0;
	if (sdata->local->hw.queues >= 4)
		ifmgd->flags |= IEEE80211_STA_WMM_ENABLED;

	mutex_init(&ifmgd->mtx);
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
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	const u8 *ssid;
	struct ieee80211_mgd_work *wk;
	u16 auth_alg;

	switch (req->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		auth_alg = WLAN_AUTH_OPEN;
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
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

	wk = kzalloc(sizeof(*wk) + req->ie_len, GFP_KERNEL);
	if (!wk)
		return -ENOMEM;

	wk->bss = (void *)req->bss;

	if (req->ie && req->ie_len) {
		memcpy(wk->ie, req->ie, req->ie_len);
		wk->ie_len = req->ie_len;
	}

	if (req->key && req->key_len) {
		wk->key_len = req->key_len;
		wk->key_idx = req->key_idx;
		memcpy(wk->key, req->key, req->key_len);
	}

	ssid = ieee80211_bss_get_ie(req->bss, WLAN_EID_SSID);
	memcpy(wk->ssid, ssid + 2, ssid[1]);
	wk->ssid_len = ssid[1];

	wk->state = IEEE80211_MGD_STATE_PROBE;
	wk->auth_alg = auth_alg;
	wk->timeout = jiffies; /* run right away */

	/*
	 * XXX: if still associated need to tell AP that we're going
	 *	to sleep and then change channel etc.
	 */
	sdata->local->oper_channel = req->bss->channel;
	ieee80211_hw_config(sdata->local, 0);

	mutex_lock(&ifmgd->mtx);
	list_add(&wk->list, &sdata->u.mgd.work_list);
	mutex_unlock(&ifmgd->mtx);

	ieee80211_queue_work(&sdata->local->hw, &sdata->u.mgd.work);
	return 0;
}

int ieee80211_mgd_assoc(struct ieee80211_sub_if_data *sdata,
			struct cfg80211_assoc_request *req)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_work *wk, *found = NULL;
	int i, err;

	mutex_lock(&ifmgd->mtx);

	list_for_each_entry(wk, &ifmgd->work_list, list) {
		if (&wk->bss->cbss == req->bss &&
		    wk->state == IEEE80211_MGD_STATE_IDLE) {
			found = wk;
			break;
		}
	}

	if (!found) {
		err = -ENOLINK;
		goto out;
	}

	list_del(&found->list);

	wk = krealloc(found, sizeof(*wk) + req->ie_len, GFP_KERNEL);
	if (!wk) {
		list_add(&found->list, &ifmgd->work_list);
		err = -ENOMEM;
		goto out;
	}

	list_add(&wk->list, &ifmgd->work_list);

	ifmgd->flags &= ~IEEE80211_STA_DISABLE_11N;

	for (i = 0; i < req->crypto.n_ciphers_pairwise; i++)
		if (req->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_WEP40 ||
		    req->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_TKIP ||
		    req->crypto.ciphers_pairwise[i] == WLAN_CIPHER_SUITE_WEP104)
			ifmgd->flags |= IEEE80211_STA_DISABLE_11N;

	sdata->local->oper_channel = req->bss->channel;
	ieee80211_hw_config(sdata->local, 0);

	if (req->ie && req->ie_len) {
		memcpy(wk->ie, req->ie, req->ie_len);
		wk->ie_len = req->ie_len;
	} else
		wk->ie_len = 0;

	if (req->prev_bssid)
		memcpy(wk->prev_bssid, req->prev_bssid, ETH_ALEN);

	wk->state = IEEE80211_MGD_STATE_ASSOC;
	wk->tries = 0;
	wk->timeout = jiffies; /* run right away */

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

	ieee80211_queue_work(&sdata->local->hw, &sdata->u.mgd.work);

	err = 0;

 out:
	mutex_unlock(&ifmgd->mtx);
	return err;
}

int ieee80211_mgd_deauth(struct ieee80211_sub_if_data *sdata,
			 struct cfg80211_deauth_request *req,
			 void *cookie)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_mgd_work *wk;
	const u8 *bssid = NULL;

	mutex_lock(&ifmgd->mtx);

	if (ifmgd->associated && &ifmgd->associated->cbss == req->bss) {
		bssid = req->bss->bssid;
		ieee80211_set_disassoc(sdata, true);
	} else list_for_each_entry(wk, &ifmgd->work_list, list) {
		if (&wk->bss->cbss == req->bss) {
			bssid = req->bss->bssid;
			list_del(&wk->list);
			kfree(wk);
			break;
		}
	}

	/*
	 * cfg80211 should catch this ... but it's racy since
	 * we can receive a deauth frame, process it, hand it
	 * to cfg80211 while that's in a locked section already
	 * trying to tell us that the user wants to disconnect.
	 */
	if (!bssid) {
		mutex_unlock(&ifmgd->mtx);
		return -ENOLINK;
	}

	mutex_unlock(&ifmgd->mtx);

	printk(KERN_DEBUG "%s: deauthenticating from %pM by local choice (reason=%d)\n",
	       sdata->dev->name, bssid, req->reason_code);

	ieee80211_send_deauth_disassoc(sdata, bssid,
			IEEE80211_STYPE_DEAUTH, req->reason_code,
			cookie);

	return 0;
}

int ieee80211_mgd_disassoc(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_disassoc_request *req,
			   void *cookie)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	mutex_lock(&ifmgd->mtx);

	/*
	 * cfg80211 should catch this ... but it's racy since
	 * we can receive a disassoc frame, process it, hand it
	 * to cfg80211 while that's in a locked section already
	 * trying to tell us that the user wants to disconnect.
	 */
	if (&ifmgd->associated->cbss != req->bss) {
		mutex_unlock(&ifmgd->mtx);
		return -ENOLINK;
	}

	printk(KERN_DEBUG "%s: disassociating from %pM by local choice (reason=%d)\n",
	       sdata->dev->name, req->bss->bssid, req->reason_code);

	ieee80211_set_disassoc(sdata, false);

	mutex_unlock(&ifmgd->mtx);

	ieee80211_send_deauth_disassoc(sdata, req->bss->bssid,
			IEEE80211_STYPE_DISASSOC, req->reason_code,
			cookie);
	return 0;
}
