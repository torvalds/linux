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

#define IEEE80211_ASSOC_SCANS_MAX_TRIES 2
#define IEEE80211_AUTH_TIMEOUT (HZ / 5)
#define IEEE80211_AUTH_MAX_TRIES 3
#define IEEE80211_ASSOC_TIMEOUT (HZ / 5)
#define IEEE80211_ASSOC_MAX_TRIES 3
#define IEEE80211_MONITORING_INTERVAL (2 * HZ)
#define IEEE80211_PROBE_WAIT (HZ / 5)
#define IEEE80211_PROBE_IDLE_TIME (60 * HZ)
#define IEEE80211_RETRY_AUTH_INTERVAL (1 * HZ)

#define TMR_RUNNING_TIMER	0
#define TMR_RUNNING_CHANSW	1

/* utils */
static int ecw2cw(int ecw)
{
	return (1 << ecw) - 1;
}

static u8 *ieee80211_bss_get_ie(struct ieee80211_bss *bss, u8 ie)
{
	u8 *end, *pos;

	pos = bss->cbss.information_elements;
	if (pos == NULL)
		return NULL;
	end = pos + bss->cbss.len_information_elements;

	while (pos + 1 < end) {
		if (pos + 2 + pos[1] > end)
			break;
		if (pos[0] == ie)
			return pos;
		pos += 2 + pos[1];
	}

	return NULL;
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
			       u16 ap_ht_cap_flags)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
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

		sta = sta_info_get(local, ifmgd->bssid);
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

static void ieee80211_send_assoc(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u8 *pos, *ies, *ht_ie;
	int i, len, count, rates_len, supp_rates_len;
	u16 capab;
	struct ieee80211_bss *bss;
	int wmm = 0;
	struct ieee80211_supported_band *sband;
	u32 rates = 0;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    sizeof(*mgmt) + 200 + ifmgd->extra_ie_len +
			    ifmgd->ssid_len);
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

	bss = ieee80211_rx_bss_get(local, ifmgd->bssid,
				   local->hw.conf.channel->center_freq,
				   ifmgd->ssid, ifmgd->ssid_len);
	if (bss) {
		if (bss->cbss.capability & WLAN_CAPABILITY_PRIVACY)
			capab |= WLAN_CAPABILITY_PRIVACY;
		if (bss->wmm_used)
			wmm = 1;

		/* get all rates supported by the device and the AP as
		 * some APs don't like getting a superset of their rates
		 * in the association request (e.g. D-Link DAP 1353 in
		 * b-only mode) */
		rates_len = ieee80211_compatible_rates(bss, sband, &rates);

		if ((bss->cbss.capability & WLAN_CAPABILITY_SPECTRUM_MGMT) &&
		    (local->hw.flags & IEEE80211_HW_SPECTRUM_MGMT))
			capab |= WLAN_CAPABILITY_SPECTRUM_MGMT;

		ieee80211_rx_bss_put(local, bss);
	} else {
		rates = ~0;
		rates_len = sband->n_bitrates;
	}

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, ifmgd->bssid, ETH_ALEN);
	memcpy(mgmt->sa, sdata->dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, ifmgd->bssid, ETH_ALEN);

	if (ifmgd->flags & IEEE80211_STA_PREV_BSSID_SET) {
		skb_put(skb, 10);
		mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						  IEEE80211_STYPE_REASSOC_REQ);
		mgmt->u.reassoc_req.capab_info = cpu_to_le16(capab);
		mgmt->u.reassoc_req.listen_interval =
				cpu_to_le16(local->hw.conf.listen_interval);
		memcpy(mgmt->u.reassoc_req.current_ap, ifmgd->prev_bssid,
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
	ies = pos = skb_put(skb, 2 + ifmgd->ssid_len);
	*pos++ = WLAN_EID_SSID;
	*pos++ = ifmgd->ssid_len;
	memcpy(pos, ifmgd->ssid, ifmgd->ssid_len);

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

	if (ifmgd->extra_ie) {
		pos = skb_put(skb, ifmgd->extra_ie_len);
		memcpy(pos, ifmgd->extra_ie, ifmgd->extra_ie_len);
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
	    (ht_ie = ieee80211_bss_get_ie(bss, WLAN_EID_HT_INFORMATION)) &&
	    ht_ie[1] >= sizeof(struct ieee80211_ht_info) &&
	    (!(ifmgd->flags & IEEE80211_STA_TKIP_WEP_USED))) {
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

	kfree(ifmgd->assocreq_ies);
	ifmgd->assocreq_ies_len = (skb->data + skb->len) - ies;
	ifmgd->assocreq_ies = kmalloc(ifmgd->assocreq_ies_len, GFP_KERNEL);
	if (ifmgd->assocreq_ies)
		memcpy(ifmgd->assocreq_ies, ies, ifmgd->assocreq_ies_len);

	ieee80211_tx_skb(sdata, skb, 0);
}


static void ieee80211_send_deauth_disassoc(struct ieee80211_sub_if_data *sdata,
					   u16 stype, u16 reason)
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
	memcpy(mgmt->da, ifmgd->bssid, ETH_ALEN);
	memcpy(mgmt->sa, sdata->dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, ifmgd->bssid, ETH_ALEN);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | stype);
	skb_put(skb, 2);
	/* u.deauth.reason_code == u.disassoc.reason_code */
	mgmt->u.deauth.reason_code = cpu_to_le16(reason);

	if (stype == IEEE80211_STYPE_DEAUTH)
		cfg80211_send_deauth(sdata->dev, (u8 *) mgmt, skb->len);
	else
		cfg80211_send_disassoc(sdata->dev, (u8 *) mgmt, skb->len);
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
	struct ieee80211_bss *bss;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (!netif_running(sdata->dev))
		return;

	bss = ieee80211_rx_bss_get(sdata->local, ifmgd->bssid,
				   sdata->local->hw.conf.channel->center_freq,
				   ifmgd->ssid, ifmgd->ssid_len);
	if (!bss)
		goto exit;

	sdata->local->oper_channel = sdata->local->csa_channel;
	/* XXX: shouldn't really modify cfg80211-owned data! */
	if (!ieee80211_hw_config(sdata->local, IEEE80211_CONF_CHANGE_CHANNEL))
		bss->cbss.channel = sdata->local->oper_channel;

	ieee80211_rx_bss_put(sdata->local, bss);
exit:
	ifmgd->flags &= ~IEEE80211_STA_CSA_RECEIVED;
	ieee80211_wake_queues_by_reason(&sdata->local->hw,
					IEEE80211_QUEUE_STOP_REASON_CSA);
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

	queue_work(sdata->local->hw.workqueue, &ifmgd->chswitch_work);
}

void ieee80211_sta_process_chanswitch(struct ieee80211_sub_if_data *sdata,
				      struct ieee80211_channel_sw_ie *sw_elem,
				      struct ieee80211_bss *bss)
{
	struct ieee80211_channel *new_ch;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	int new_freq = ieee80211_channel_to_frequency(sw_elem->new_ch_num);

	if (ifmgd->state != IEEE80211_STA_MLME_ASSOCIATED)
		return;

	if (sdata->local->sw_scanning || sdata->local->hw_scanning)
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
		queue_work(sdata->local->hw.workqueue, &ifmgd->chswitch_work);
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
	if (local->hw_scanning || local->sw_scanning)
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
	    (found->u.mgd.flags & IEEE80211_STA_ASSOCIATED) &&
	    !(found->u.mgd.flags & IEEE80211_STA_PROBEREQ_POLL)) {
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

	if (local->quiescing)
		return;

	queue_work(local->hw.workqueue, &local->dynamic_ps_enable_work);
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
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
#endif
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
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: CTS protection %s (BSSID=%pM)\n",
			       sdata->dev->name,
			       use_protection ? "enabled" : "disabled",
			       ifmgd->bssid);
		}
#endif
		bss_conf->use_cts_prot = use_protection;
		changed |= BSS_CHANGED_ERP_CTS_PROT;
	}

	if (use_short_preamble != bss_conf->use_short_preamble) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: switched to %s barker preamble"
			       " (BSSID=%pM)\n",
			       sdata->dev->name,
			       use_short_preamble ? "short" : "long",
			       ifmgd->bssid);
		}
#endif
		bss_conf->use_short_preamble = use_short_preamble;
		changed |= BSS_CHANGED_ERP_PREAMBLE;
	}

	if (use_short_slot != bss_conf->use_short_slot) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: switched to %s slot time"
			       " (BSSID=%pM)\n",
			       sdata->dev->name,
			       use_short_slot ? "short" : "long",
			       ifmgd->bssid);
		}
#endif
		bss_conf->use_short_slot = use_short_slot;
		changed |= BSS_CHANGED_ERP_SLOT;
	}

	return changed;
}

static void ieee80211_sta_send_apinfo(struct ieee80211_sub_if_data *sdata)
{
	union iwreq_data wrqu;

	memset(&wrqu, 0, sizeof(wrqu));
	if (sdata->u.mgd.flags & IEEE80211_STA_ASSOCIATED)
		memcpy(wrqu.ap_addr.sa_data, sdata->u.mgd.bssid, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(sdata->dev, SIOCGIWAP, &wrqu, NULL);
}

static void ieee80211_sta_send_associnfo(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	char *buf;
	size_t len;
	int i;
	union iwreq_data wrqu;

	if (!ifmgd->assocreq_ies && !ifmgd->assocresp_ies)
		return;

	buf = kmalloc(50 + 2 * (ifmgd->assocreq_ies_len +
				ifmgd->assocresp_ies_len), GFP_KERNEL);
	if (!buf)
		return;

	len = sprintf(buf, "ASSOCINFO(");
	if (ifmgd->assocreq_ies) {
		len += sprintf(buf + len, "ReqIEs=");
		for (i = 0; i < ifmgd->assocreq_ies_len; i++) {
			len += sprintf(buf + len, "%02x",
				       ifmgd->assocreq_ies[i]);
		}
	}
	if (ifmgd->assocresp_ies) {
		if (ifmgd->assocreq_ies)
			len += sprintf(buf + len, " ");
		len += sprintf(buf + len, "RespIEs=");
		for (i = 0; i < ifmgd->assocresp_ies_len; i++) {
			len += sprintf(buf + len, "%02x",
				       ifmgd->assocresp_ies[i]);
		}
	}
	len += sprintf(buf + len, ")");

	if (len > IW_CUSTOM_MAX) {
		len = sprintf(buf, "ASSOCRESPIE=");
		for (i = 0; i < ifmgd->assocresp_ies_len; i++) {
			len += sprintf(buf + len, "%02x",
				       ifmgd->assocresp_ies[i]);
		}
	}

	if (len <= IW_CUSTOM_MAX) {
		memset(&wrqu, 0, sizeof(wrqu));
		wrqu.data.length = len;
		wireless_send_event(sdata->dev, IWEVCUSTOM, &wrqu, buf);
	}

	kfree(buf);
}


static void ieee80211_set_associated(struct ieee80211_sub_if_data *sdata,
				     u32 bss_info_changed)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_conf *conf = &local_to_hw(local)->conf;

	struct ieee80211_bss *bss;

	bss_info_changed |= BSS_CHANGED_ASSOC;
	ifmgd->flags |= IEEE80211_STA_ASSOCIATED;

	bss = ieee80211_rx_bss_get(local, ifmgd->bssid,
				   conf->channel->center_freq,
				   ifmgd->ssid, ifmgd->ssid_len);
	if (bss) {
		/* set timing information */
		sdata->vif.bss_conf.beacon_int = bss->cbss.beacon_interval;
		sdata->vif.bss_conf.timestamp = bss->cbss.tsf;
		sdata->vif.bss_conf.dtim_period = bss->dtim_period;

		bss_info_changed |= BSS_CHANGED_BEACON_INT;
		bss_info_changed |= ieee80211_handle_bss_capability(sdata,
			bss->cbss.capability, bss->has_erp_value, bss->erp_value);

		cfg80211_hold_bss(&bss->cbss);

		ieee80211_rx_bss_put(local, bss);
	}

	ifmgd->flags |= IEEE80211_STA_PREV_BSSID_SET;
	memcpy(ifmgd->prev_bssid, sdata->u.mgd.bssid, ETH_ALEN);
	ieee80211_sta_send_associnfo(sdata);

	ifmgd->last_probe = jiffies;
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

	/* will be same as sdata */
	if (local->ps_sdata) {
		mutex_lock(&local->iflist_mtx);
		ieee80211_recalc_ps(local, -1);
		mutex_unlock(&local->iflist_mtx);
	}

	netif_tx_start_all_queues(sdata->dev);
	netif_carrier_on(sdata->dev);

	ieee80211_sta_send_apinfo(sdata);
}

static void ieee80211_direct_probe(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;

	ifmgd->direct_probe_tries++;
	if (ifmgd->direct_probe_tries > IEEE80211_AUTH_MAX_TRIES) {
		printk(KERN_DEBUG "%s: direct probe to AP %pM timed out\n",
		       sdata->dev->name, ifmgd->bssid);
		ifmgd->state = IEEE80211_STA_MLME_DISABLED;
		ieee80211_recalc_idle(local);
		cfg80211_send_auth_timeout(sdata->dev, ifmgd->bssid);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss information associated to the AP
		 */
		ieee80211_rx_bss_remove(sdata, ifmgd->bssid,
				sdata->local->hw.conf.channel->center_freq,
				ifmgd->ssid, ifmgd->ssid_len);

		/*
		 * We might have a pending scan which had no chance to run yet
		 * due to state == IEEE80211_STA_MLME_DIRECT_PROBE.
		 * Hence, queue the STAs work again
		 */
		queue_work(local->hw.workqueue, &ifmgd->work);
		return;
	}

	printk(KERN_DEBUG "%s: direct probe to AP %pM try %d\n",
			sdata->dev->name, ifmgd->bssid,
			ifmgd->direct_probe_tries);

	ifmgd->state = IEEE80211_STA_MLME_DIRECT_PROBE;

	/* Direct probe is sent to broadcast address as some APs
	 * will not answer to direct packet in unassociated state.
	 */
	ieee80211_send_probe_req(sdata, NULL,
				 ifmgd->ssid, ifmgd->ssid_len, NULL, 0);

	mod_timer(&ifmgd->timer, jiffies + IEEE80211_AUTH_TIMEOUT);
}


static void ieee80211_authenticate(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	u8 *ies;
	size_t ies_len;

	ifmgd->auth_tries++;
	if (ifmgd->auth_tries > IEEE80211_AUTH_MAX_TRIES) {
		printk(KERN_DEBUG "%s: authentication with AP %pM"
		       " timed out\n",
		       sdata->dev->name, ifmgd->bssid);
		ifmgd->state = IEEE80211_STA_MLME_DISABLED;
		ieee80211_recalc_idle(local);
		cfg80211_send_auth_timeout(sdata->dev, ifmgd->bssid);
		ieee80211_rx_bss_remove(sdata, ifmgd->bssid,
				sdata->local->hw.conf.channel->center_freq,
				ifmgd->ssid, ifmgd->ssid_len);

		/*
		 * We might have a pending scan which had no chance to run yet
		 * due to state == IEEE80211_STA_MLME_AUTHENTICATE.
		 * Hence, queue the STAs work again
		 */
		queue_work(local->hw.workqueue, &ifmgd->work);
		return;
	}

	ifmgd->state = IEEE80211_STA_MLME_AUTHENTICATE;
	printk(KERN_DEBUG "%s: authenticate with AP %pM\n",
	       sdata->dev->name, ifmgd->bssid);

	if (ifmgd->flags & IEEE80211_STA_EXT_SME) {
		ies = ifmgd->sme_auth_ie;
		ies_len = ifmgd->sme_auth_ie_len;
	} else {
		ies = NULL;
		ies_len = 0;
	}
	ieee80211_send_auth(sdata, 1, ifmgd->auth_alg, ies, ies_len,
			    ifmgd->bssid, 0);
	ifmgd->auth_transaction = 2;

	mod_timer(&ifmgd->timer, jiffies + IEEE80211_AUTH_TIMEOUT);
}

/*
 * The disassoc 'reason' argument can be either our own reason
 * if self disconnected or a reason code from the AP.
 */
static void ieee80211_set_disassoc(struct ieee80211_sub_if_data *sdata,
				   bool deauth, bool self_disconnected,
				   u16 reason)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_conf *conf = &local_to_hw(local)->conf;
	struct ieee80211_bss *bss;
	struct sta_info *sta;
	u32 changed = 0, config_changed = 0;

	if (deauth) {
		ifmgd->direct_probe_tries = 0;
		ifmgd->auth_tries = 0;
	}
	ifmgd->assoc_scan_tries = 0;
	ifmgd->assoc_tries = 0;

	netif_tx_stop_all_queues(sdata->dev);
	netif_carrier_off(sdata->dev);

	rcu_read_lock();
	sta = sta_info_get(local, ifmgd->bssid);
	if (sta)
		ieee80211_sta_tear_down_BA_sessions(sta);
	rcu_read_unlock();

	bss = ieee80211_rx_bss_get(local, ifmgd->bssid,
				   conf->channel->center_freq,
				   ifmgd->ssid, ifmgd->ssid_len);

	if (bss) {
		cfg80211_unhold_bss(&bss->cbss);
		ieee80211_rx_bss_put(local, bss);
	}

	if (self_disconnected) {
		if (deauth)
			ieee80211_send_deauth_disassoc(sdata,
				IEEE80211_STYPE_DEAUTH, reason);
		else
			ieee80211_send_deauth_disassoc(sdata,
				IEEE80211_STYPE_DISASSOC, reason);
	}

	ifmgd->flags &= ~IEEE80211_STA_ASSOCIATED;
	changed |= ieee80211_reset_erp_info(sdata);

	ieee80211_led_assoc(local, 0);
	changed |= BSS_CHANGED_ASSOC;
	sdata->vif.bss_conf.assoc = false;

	ieee80211_sta_send_apinfo(sdata);

	if (self_disconnected || reason == WLAN_REASON_DISASSOC_STA_HAS_LEFT) {
		ifmgd->state = IEEE80211_STA_MLME_DISABLED;
		ieee80211_rx_bss_remove(sdata, ifmgd->bssid,
				sdata->local->hw.conf.channel->center_freq,
				ifmgd->ssid, ifmgd->ssid_len);
	}

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

	sta = sta_info_get(local, ifmgd->bssid);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	sta_info_unlink(&sta);

	rcu_read_unlock();

	sta_info_destroy(sta);
}

static int ieee80211_sta_wep_configured(struct ieee80211_sub_if_data *sdata)
{
	if (!sdata || !sdata->default_key ||
	    sdata->default_key->conf.alg != ALG_WEP)
		return 0;
	return 1;
}

static int ieee80211_privacy_mismatch(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_bss *bss;
	int bss_privacy;
	int wep_privacy;
	int privacy_invoked;

	if (!ifmgd || (ifmgd->flags & IEEE80211_STA_EXT_SME))
		return 0;

	bss = ieee80211_rx_bss_get(local, ifmgd->bssid,
				   local->hw.conf.channel->center_freq,
				   ifmgd->ssid, ifmgd->ssid_len);
	if (!bss)
		return 0;

	bss_privacy = !!(bss->cbss.capability & WLAN_CAPABILITY_PRIVACY);
	wep_privacy = !!ieee80211_sta_wep_configured(sdata);
	privacy_invoked = !!(ifmgd->flags & IEEE80211_STA_PRIVACY_INVOKED);

	ieee80211_rx_bss_put(local, bss);

	if ((bss_privacy == wep_privacy) || (bss_privacy == privacy_invoked))
		return 0;

	return 1;
}

static void ieee80211_associate(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;

	ifmgd->assoc_tries++;
	if (ifmgd->assoc_tries > IEEE80211_ASSOC_MAX_TRIES) {
		printk(KERN_DEBUG "%s: association with AP %pM"
		       " timed out\n",
		       sdata->dev->name, ifmgd->bssid);
		ifmgd->state = IEEE80211_STA_MLME_DISABLED;
		ieee80211_recalc_idle(local);
		cfg80211_send_assoc_timeout(sdata->dev, ifmgd->bssid);
		ieee80211_rx_bss_remove(sdata, ifmgd->bssid,
				sdata->local->hw.conf.channel->center_freq,
				ifmgd->ssid, ifmgd->ssid_len);
		/*
		 * We might have a pending scan which had no chance to run yet
		 * due to state == IEEE80211_STA_MLME_ASSOCIATE.
		 * Hence, queue the STAs work again
		 */
		queue_work(local->hw.workqueue, &ifmgd->work);
		return;
	}

	ifmgd->state = IEEE80211_STA_MLME_ASSOCIATE;
	printk(KERN_DEBUG "%s: associate with AP %pM\n",
	       sdata->dev->name, ifmgd->bssid);
	if (ieee80211_privacy_mismatch(sdata)) {
		printk(KERN_DEBUG "%s: mismatch in privacy configuration and "
		       "mixed-cell disabled - abort association\n", sdata->dev->name);
		ifmgd->state = IEEE80211_STA_MLME_DISABLED;
		ieee80211_recalc_idle(local);
		return;
	}

	ieee80211_send_assoc(sdata);

	mod_timer(&ifmgd->timer, jiffies + IEEE80211_ASSOC_TIMEOUT);
}

void ieee80211_sta_rx_notify(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_hdr *hdr)
{
	/*
	 * We can postpone the mgd.timer whenever receiving unicast frames
	 * from AP because we know that the connection is working both ways
	 * at that time. But multicast frames (and hence also beacons) must
	 * be ignored here, because we need to trigger the timer during
	 * data idle periods for sending the periodical probe request to
	 * the AP.
	 */
	if (!is_multicast_ether_addr(hdr->addr1))
		mod_timer(&sdata->u.mgd.timer,
			  jiffies + IEEE80211_MONITORING_INTERVAL);
}

void ieee80211_beacon_loss_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data,
			     u.mgd.beacon_loss_work);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	/*
	 * The driver has already reported this event and we have
	 * already sent a probe request. Maybe the AP died and the
	 * driver keeps reporting until we disassociate... We have
	 * to ignore that because otherwise we would continually
	 * reset the timer and never check whether we received a
	 * probe response!
	 */
	if (ifmgd->flags & IEEE80211_STA_PROBEREQ_POLL)
		return;

#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
	if (net_ratelimit()) {
		printk(KERN_DEBUG "%s: driver reports beacon loss from AP %pM "
		       "- sending probe request\n", sdata->dev->name,
		       sdata->u.mgd.bssid);
	}
#endif

	ifmgd->flags |= IEEE80211_STA_PROBEREQ_POLL;

	mutex_lock(&sdata->local->iflist_mtx);
	ieee80211_recalc_ps(sdata->local, -1);
	mutex_unlock(&sdata->local->iflist_mtx);

	ieee80211_send_probe_req(sdata, ifmgd->bssid, ifmgd->ssid,
				 ifmgd->ssid_len, NULL, 0);

	mod_timer(&ifmgd->timer, jiffies + IEEE80211_PROBE_WAIT);
}

void ieee80211_beacon_loss(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	queue_work(sdata->local->hw.workqueue,
		   &sdata->u.mgd.beacon_loss_work);
}
EXPORT_SYMBOL(ieee80211_beacon_loss);

static void ieee80211_associated(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	unsigned long last_rx;
	bool disassoc = false;

	/* TODO: start monitoring current AP signal quality and number of
	 * missed beacons. Scan other channels every now and then and search
	 * for better APs. */
	/* TODO: remove expired BSSes */

	ifmgd->state = IEEE80211_STA_MLME_ASSOCIATED;

	rcu_read_lock();

	sta = sta_info_get(local, ifmgd->bssid);
	if (!sta) {
		printk(KERN_DEBUG "%s: No STA entry for own AP %pM\n",
		       sdata->dev->name, ifmgd->bssid);
		disassoc = true;
		rcu_read_unlock();
		goto out;
	}

	last_rx = sta->last_rx;
	rcu_read_unlock();

	if ((ifmgd->flags & IEEE80211_STA_PROBEREQ_POLL) &&
	    time_after(jiffies, last_rx + IEEE80211_PROBE_WAIT)) {
		printk(KERN_DEBUG "%s: no probe response from AP %pM "
		       "- disassociating\n",
		       sdata->dev->name, ifmgd->bssid);
		disassoc = true;
		ifmgd->flags &= ~IEEE80211_STA_PROBEREQ_POLL;
		goto out;
	}

	/*
	 * Beacon filtering is only enabled with power save and then the
	 * stack should not check for beacon loss.
	 */
	if (!((local->hw.flags & IEEE80211_HW_BEACON_FILTER) &&
	      (local->hw.conf.flags & IEEE80211_CONF_PS)) &&
	    time_after(jiffies,
		       ifmgd->last_beacon + IEEE80211_MONITORING_INTERVAL)) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: beacon loss from AP %pM "
			       "- sending probe request\n",
			       sdata->dev->name, ifmgd->bssid);
		}
#endif
		ifmgd->flags |= IEEE80211_STA_PROBEREQ_POLL;
		mutex_lock(&local->iflist_mtx);
		ieee80211_recalc_ps(local, -1);
		mutex_unlock(&local->iflist_mtx);
		ieee80211_send_probe_req(sdata, ifmgd->bssid, ifmgd->ssid,
					 ifmgd->ssid_len, NULL, 0);
		mod_timer(&ifmgd->timer, jiffies + IEEE80211_PROBE_WAIT);
		goto out;
	}

	if (time_after(jiffies, last_rx + IEEE80211_PROBE_IDLE_TIME)) {
		ifmgd->flags |= IEEE80211_STA_PROBEREQ_POLL;
		mutex_lock(&local->iflist_mtx);
		ieee80211_recalc_ps(local, -1);
		mutex_unlock(&local->iflist_mtx);
		ieee80211_send_probe_req(sdata, ifmgd->bssid, ifmgd->ssid,
					 ifmgd->ssid_len, NULL, 0);
	}

 out:
	if (!disassoc)
		mod_timer(&ifmgd->timer,
			  jiffies + IEEE80211_MONITORING_INTERVAL);
	else
		ieee80211_set_disassoc(sdata, true, true,
					WLAN_REASON_PREV_AUTH_NOT_VALID);
}


static void ieee80211_auth_completed(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	printk(KERN_DEBUG "%s: authenticated\n", sdata->dev->name);
	ifmgd->flags |= IEEE80211_STA_AUTHENTICATED;
	if (ifmgd->flags & IEEE80211_STA_EXT_SME) {
		/* Wait for SME to request association */
		ifmgd->state = IEEE80211_STA_MLME_DISABLED;
		ieee80211_recalc_idle(sdata->local);
	} else
		ieee80211_associate(sdata);
}


static void ieee80211_auth_challenge(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgmt *mgmt,
				     size_t len)
{
	u8 *pos;
	struct ieee802_11_elems elems;

	pos = mgmt->u.auth.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), &elems);
	if (!elems.challenge)
		return;
	ieee80211_send_auth(sdata, 3, sdata->u.mgd.auth_alg,
			    elems.challenge - 2, elems.challenge_len + 2,
			    sdata->u.mgd.bssid, 1);
	sdata->u.mgd.auth_transaction = 4;
}

static void ieee80211_rx_mgmt_auth(struct ieee80211_sub_if_data *sdata,
				   struct ieee80211_mgmt *mgmt,
				   size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u16 auth_alg, auth_transaction, status_code;

	if (ifmgd->state != IEEE80211_STA_MLME_AUTHENTICATE)
		return;

	if (len < 24 + 6)
		return;

	if (memcmp(ifmgd->bssid, mgmt->sa, ETH_ALEN) != 0)
		return;

	if (memcmp(ifmgd->bssid, mgmt->bssid, ETH_ALEN) != 0)
		return;

	auth_alg = le16_to_cpu(mgmt->u.auth.auth_alg);
	auth_transaction = le16_to_cpu(mgmt->u.auth.auth_transaction);
	status_code = le16_to_cpu(mgmt->u.auth.status_code);

	if (auth_alg != ifmgd->auth_alg ||
	    auth_transaction != ifmgd->auth_transaction)
		return;

	if (status_code != WLAN_STATUS_SUCCESS) {
		if (status_code == WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG) {
			u8 algs[3];
			const int num_algs = ARRAY_SIZE(algs);
			int i, pos;
			algs[0] = algs[1] = algs[2] = 0xff;
			if (ifmgd->auth_algs & IEEE80211_AUTH_ALG_OPEN)
				algs[0] = WLAN_AUTH_OPEN;
			if (ifmgd->auth_algs & IEEE80211_AUTH_ALG_SHARED_KEY)
				algs[1] = WLAN_AUTH_SHARED_KEY;
			if (ifmgd->auth_algs & IEEE80211_AUTH_ALG_LEAP)
				algs[2] = WLAN_AUTH_LEAP;
			if (ifmgd->auth_alg == WLAN_AUTH_OPEN)
				pos = 0;
			else if (ifmgd->auth_alg == WLAN_AUTH_SHARED_KEY)
				pos = 1;
			else
				pos = 2;
			for (i = 0; i < num_algs; i++) {
				pos++;
				if (pos >= num_algs)
					pos = 0;
				if (algs[pos] == ifmgd->auth_alg ||
				    algs[pos] == 0xff)
					continue;
				if (algs[pos] == WLAN_AUTH_SHARED_KEY &&
				    !ieee80211_sta_wep_configured(sdata))
					continue;
				ifmgd->auth_alg = algs[pos];
				break;
			}
		}
		return;
	}

	switch (ifmgd->auth_alg) {
	case WLAN_AUTH_OPEN:
	case WLAN_AUTH_LEAP:
	case WLAN_AUTH_FT:
		ieee80211_auth_completed(sdata);
		cfg80211_send_rx_auth(sdata->dev, (u8 *) mgmt, len);
		break;
	case WLAN_AUTH_SHARED_KEY:
		if (ifmgd->auth_transaction == 4) {
			ieee80211_auth_completed(sdata);
			cfg80211_send_rx_auth(sdata->dev, (u8 *) mgmt, len);
		} else
			ieee80211_auth_challenge(sdata, mgmt, len);
		break;
	}
}


static void ieee80211_rx_mgmt_deauth(struct ieee80211_sub_if_data *sdata,
				     struct ieee80211_mgmt *mgmt,
				     size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u16 reason_code;

	if (len < 24 + 2)
		return;

	if (memcmp(ifmgd->bssid, mgmt->sa, ETH_ALEN))
		return;

	reason_code = le16_to_cpu(mgmt->u.deauth.reason_code);

	if (ifmgd->flags & IEEE80211_STA_AUTHENTICATED)
		printk(KERN_DEBUG "%s: deauthenticated (Reason: %u)\n",
				sdata->dev->name, reason_code);

	if (!(ifmgd->flags & IEEE80211_STA_EXT_SME) &&
	    (ifmgd->state == IEEE80211_STA_MLME_AUTHENTICATE ||
	     ifmgd->state == IEEE80211_STA_MLME_ASSOCIATE ||
	     ifmgd->state == IEEE80211_STA_MLME_ASSOCIATED)) {
		ifmgd->state = IEEE80211_STA_MLME_DIRECT_PROBE;
		mod_timer(&ifmgd->timer, jiffies +
				      IEEE80211_RETRY_AUTH_INTERVAL);
	}

	ieee80211_set_disassoc(sdata, true, false, 0);
	ifmgd->flags &= ~IEEE80211_STA_AUTHENTICATED;
	cfg80211_send_deauth(sdata->dev, (u8 *) mgmt, len);
}


static void ieee80211_rx_mgmt_disassoc(struct ieee80211_sub_if_data *sdata,
				       struct ieee80211_mgmt *mgmt,
				       size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	u16 reason_code;

	if (len < 24 + 2)
		return;

	if (memcmp(ifmgd->bssid, mgmt->sa, ETH_ALEN))
		return;

	reason_code = le16_to_cpu(mgmt->u.disassoc.reason_code);

	if (ifmgd->flags & IEEE80211_STA_ASSOCIATED)
		printk(KERN_DEBUG "%s: disassociated (Reason: %u)\n",
				sdata->dev->name, reason_code);

	if (!(ifmgd->flags & IEEE80211_STA_EXT_SME) &&
	    ifmgd->state == IEEE80211_STA_MLME_ASSOCIATED) {
		ifmgd->state = IEEE80211_STA_MLME_ASSOCIATE;
		mod_timer(&ifmgd->timer, jiffies +
				      IEEE80211_RETRY_AUTH_INTERVAL);
	}

	ieee80211_set_disassoc(sdata, false, false, reason_code);
	cfg80211_send_disassoc(sdata->dev, (u8 *) mgmt, len);
}


static void ieee80211_rx_mgmt_assoc_resp(struct ieee80211_sub_if_data *sdata,
					 struct ieee80211_mgmt *mgmt,
					 size_t len,
					 int reassoc)
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

	/* AssocResp and ReassocResp have identical structure, so process both
	 * of them in this function. */

	if (ifmgd->state != IEEE80211_STA_MLME_ASSOCIATE)
		return;

	if (len < 24 + 6)
		return;

	if (memcmp(ifmgd->bssid, mgmt->sa, ETH_ALEN) != 0)
		return;

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
		if (ms > IEEE80211_ASSOC_TIMEOUT)
			mod_timer(&ifmgd->timer,
				  jiffies + msecs_to_jiffies(ms));
		return;
	}

	if (status_code != WLAN_STATUS_SUCCESS) {
		printk(KERN_DEBUG "%s: AP denied association (code=%d)\n",
		       sdata->dev->name, status_code);
		/* if this was a reassociation, ensure we try a "full"
		 * association next time. This works around some broken APs
		 * which do not correctly reject reassociation requests. */
		ifmgd->flags &= ~IEEE80211_STA_PREV_BSSID_SET;
		cfg80211_send_rx_assoc(sdata->dev, (u8 *) mgmt, len);
		if (ifmgd->flags & IEEE80211_STA_EXT_SME) {
			/* Wait for SME to decide what to do next */
			ifmgd->state = IEEE80211_STA_MLME_DISABLED;
			ieee80211_recalc_idle(local);
		}
		return;
	}

	if ((aid & (BIT(15) | BIT(14))) != (BIT(15) | BIT(14)))
		printk(KERN_DEBUG "%s: invalid aid value %d; bits 15:14 not "
		       "set\n", sdata->dev->name, aid);
	aid &= ~(BIT(15) | BIT(14));

	if (!elems.supp_rates) {
		printk(KERN_DEBUG "%s: no SuppRates element in AssocResp\n",
		       sdata->dev->name);
		return;
	}

	printk(KERN_DEBUG "%s: associated\n", sdata->dev->name);
	ifmgd->aid = aid;
	ifmgd->ap_capab = capab_info;

	kfree(ifmgd->assocresp_ies);
	ifmgd->assocresp_ies_len = len - (pos - (u8 *) mgmt);
	ifmgd->assocresp_ies = kmalloc(ifmgd->assocresp_ies_len, GFP_KERNEL);
	if (ifmgd->assocresp_ies)
		memcpy(ifmgd->assocresp_ies, pos, ifmgd->assocresp_ies_len);

	rcu_read_lock();

	/* Add STA entry for the AP */
	sta = sta_info_get(local, ifmgd->bssid);
	if (!sta) {
		newsta = true;

		sta = sta_info_alloc(sdata, ifmgd->bssid, GFP_ATOMIC);
		if (!sta) {
			printk(KERN_DEBUG "%s: failed to alloc STA entry for"
			       " the AP\n", sdata->dev->name);
			rcu_read_unlock();
			return;
		}

		/* update new sta with its last rx activity */
		sta->last_rx = jiffies;
	}

	/*
	 * FIXME: Do we really need to update the sta_info's information here?
	 *	  We already know about the AP (we found it in our list) so it
	 *	  should already be filled with the right info, no?
	 *	  As is stands, all this is racy because typically we assume
	 *	  the information that is filled in here (except flags) doesn't
	 *	  change while a STA structure is alive. As such, it should move
	 *	  to between the sta_info_alloc() and sta_info_insert() above.
	 */

	set_sta_flags(sta, WLAN_STA_AUTH | WLAN_STA_ASSOC | WLAN_STA_ASSOC_AP);
	if (!(ifmgd->flags & IEEE80211_STA_CONTROL_PORT))
		set_sta_flags(sta, WLAN_STA_AUTHORIZED);

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

	/* If TKIP/WEP is used, no need to parse AP's HT capabilities */
	if (elems.ht_cap_elem && !(ifmgd->flags & IEEE80211_STA_TKIP_WEP_USED))
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
			return;
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
	    !(ifmgd->flags & IEEE80211_STA_TKIP_WEP_USED))
		changed |= ieee80211_enable_ht(sdata, elems.ht_info_elem,
					       ap_ht_cap_flags);

	/* set AID and assoc capability,
	 * ieee80211_set_associated() will tell the driver */
	bss_conf->aid = aid;
	bss_conf->assoc_capability = capab_info;
	ieee80211_set_associated(sdata, changed);

	/*
	 * initialise the time of last beacon to be the association time,
	 * otherwise beacon loss check will trigger immediately
	 */
	ifmgd->last_beacon = jiffies;

	ieee80211_associated(sdata);
	cfg80211_send_rx_assoc(sdata->dev, (u8 *) mgmt, len);
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
	if (!bss)
		return;

	if (elems->ch_switch_elem && (elems->ch_switch_elem_len == 3) &&
	    (memcmp(mgmt->bssid, sdata->u.mgd.bssid, ETH_ALEN) == 0)) {
		struct ieee80211_channel_sw_ie *sw_elem =
			(struct ieee80211_channel_sw_ie *)elems->ch_switch_elem;
		ieee80211_sta_process_chanswitch(sdata, sw_elem, bss);
	}

	ieee80211_rx_bss_put(local, bss);
}


static void ieee80211_rx_mgmt_probe_resp(struct ieee80211_sub_if_data *sdata,
					 struct ieee80211_mgmt *mgmt,
					 size_t len,
					 struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_if_managed *ifmgd;
	size_t baselen;
	struct ieee802_11_elems elems;

	ifmgd = &sdata->u.mgd;

	if (memcmp(mgmt->da, sdata->dev->dev_addr, ETH_ALEN))
		return; /* ignore ProbeResp to foreign address */

	baselen = (u8 *) mgmt->u.probe_resp.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	ieee802_11_parse_elems(mgmt->u.probe_resp.variable, len - baselen,
				&elems);

	ieee80211_rx_bss_info(sdata, mgmt, len, rx_status, &elems, false);

	/* direct probe may be part of the association flow */
	if (ifmgd->state == IEEE80211_STA_MLME_DIRECT_PROBE) {
		printk(KERN_DEBUG "%s direct probe responded\n",
		       sdata->dev->name);
		ieee80211_authenticate(sdata);
	}

	if (ifmgd->flags & IEEE80211_STA_PROBEREQ_POLL) {
		ifmgd->flags &= ~IEEE80211_STA_PROBEREQ_POLL;
		mutex_lock(&sdata->local->iflist_mtx);
		ieee80211_recalc_ps(sdata->local, -1);
		mutex_unlock(&sdata->local->iflist_mtx);
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

	/* Process beacon from the current BSS */
	baselen = (u8 *) mgmt->u.beacon.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	if (rx_status->freq != local->hw.conf.channel->center_freq)
		return;

	if (!(ifmgd->flags & IEEE80211_STA_ASSOCIATED) ||
	    memcmp(ifmgd->bssid, mgmt->bssid, ETH_ALEN) != 0)
		return;

	if (ifmgd->flags & IEEE80211_STA_PROBEREQ_POLL) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: cancelling probereq poll due "
			       "to a received beacon\n", sdata->dev->name);
		}
#endif
		ifmgd->flags &= ~IEEE80211_STA_PROBEREQ_POLL;
		mutex_lock(&local->iflist_mtx);
		ieee80211_recalc_ps(local, -1);
		mutex_unlock(&local->iflist_mtx);
	}

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
	    !(ifmgd->flags & IEEE80211_STA_TKIP_WEP_USED)) {
		struct sta_info *sta;
		struct ieee80211_supported_band *sband;
		u16 ap_ht_cap_flags;

		rcu_read_lock();

		sta = sta_info_get(local, ifmgd->bssid);
		if (!sta) {
			rcu_read_unlock();
			return;
		}

		sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

		ieee80211_ht_cap_ie_to_sta_ht_cap(sband,
				elems.ht_cap_elem, &sta->sta.ht_cap);

		ap_ht_cap_flags = sta->sta.ht_cap.cap;

		rcu_read_unlock();

		changed |= ieee80211_enable_ht(sdata, elems.ht_info_elem,
					       ap_ht_cap_flags);
	}

	if (elems.country_elem) {
		/* Note we are only reviewing this on beacons
		 * for the BSSID we are associated to */
		regulatory_hint_11d(local->hw.wiphy,
			elems.country_elem, elems.country_elem_len);

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
					  struct sk_buff *skb,
					  struct ieee80211_rx_status *rx_status)
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
		memcpy(skb->cb, rx_status, sizeof(*rx_status));
	case IEEE80211_STYPE_AUTH:
	case IEEE80211_STYPE_ASSOC_RESP:
	case IEEE80211_STYPE_REASSOC_RESP:
	case IEEE80211_STYPE_DEAUTH:
	case IEEE80211_STYPE_DISASSOC:
		skb_queue_tail(&sdata->u.mgd.skb_queue, skb);
		queue_work(local->hw.workqueue, &sdata->u.mgd.work);
		return RX_QUEUED;
	}

	return RX_DROP_MONITOR;
}

static void ieee80211_sta_rx_queued_mgmt(struct ieee80211_sub_if_data *sdata,
					 struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_mgmt *mgmt;
	u16 fc;

	rx_status = (struct ieee80211_rx_status *) skb->cb;
	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	switch (fc & IEEE80211_FCTL_STYPE) {
	case IEEE80211_STYPE_PROBE_RESP:
		ieee80211_rx_mgmt_probe_resp(sdata, mgmt, skb->len,
					     rx_status);
		break;
	case IEEE80211_STYPE_BEACON:
		ieee80211_rx_mgmt_beacon(sdata, mgmt, skb->len,
					 rx_status);
		break;
	case IEEE80211_STYPE_AUTH:
		ieee80211_rx_mgmt_auth(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_ASSOC_RESP:
		ieee80211_rx_mgmt_assoc_resp(sdata, mgmt, skb->len, 0);
		break;
	case IEEE80211_STYPE_REASSOC_RESP:
		ieee80211_rx_mgmt_assoc_resp(sdata, mgmt, skb->len, 1);
		break;
	case IEEE80211_STYPE_DEAUTH:
		ieee80211_rx_mgmt_deauth(sdata, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_DISASSOC:
		ieee80211_rx_mgmt_disassoc(sdata, mgmt, skb->len);
		break;
	}

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

	set_bit(IEEE80211_STA_REQ_RUN, &ifmgd->request);
	queue_work(local->hw.workqueue, &ifmgd->work);
}

static void ieee80211_sta_reset_auth(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;

	/* Reset own TSF to allow time synchronization work. */
	drv_reset_tsf(local);

	ifmgd->wmm_last_param_set = -1; /* allow any WMM update */


	if (ifmgd->auth_algs & IEEE80211_AUTH_ALG_OPEN)
		ifmgd->auth_alg = WLAN_AUTH_OPEN;
	else if (ifmgd->auth_algs & IEEE80211_AUTH_ALG_SHARED_KEY)
		ifmgd->auth_alg = WLAN_AUTH_SHARED_KEY;
	else if (ifmgd->auth_algs & IEEE80211_AUTH_ALG_LEAP)
		ifmgd->auth_alg = WLAN_AUTH_LEAP;
	else if (ifmgd->auth_algs & IEEE80211_AUTH_ALG_FT)
		ifmgd->auth_alg = WLAN_AUTH_FT;
	else
		ifmgd->auth_alg = WLAN_AUTH_OPEN;
	ifmgd->auth_transaction = -1;
	ifmgd->flags &= ~IEEE80211_STA_ASSOCIATED;
	ifmgd->assoc_scan_tries = 0;
	ifmgd->direct_probe_tries = 0;
	ifmgd->auth_tries = 0;
	ifmgd->assoc_tries = 0;
	netif_tx_stop_all_queues(sdata->dev);
	netif_carrier_off(sdata->dev);
}

static int ieee80211_sta_config_auth(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_bss *bss;
	u8 *bssid = ifmgd->bssid, *ssid = ifmgd->ssid;
	u8 ssid_len = ifmgd->ssid_len;
	u16 capa_mask = WLAN_CAPABILITY_ESS;
	u16 capa_val = WLAN_CAPABILITY_ESS;
	struct ieee80211_channel *chan = local->oper_channel;

	if (!(ifmgd->flags & IEEE80211_STA_EXT_SME) &&
	    ifmgd->flags & (IEEE80211_STA_AUTO_SSID_SEL |
			    IEEE80211_STA_AUTO_BSSID_SEL |
			    IEEE80211_STA_AUTO_CHANNEL_SEL)) {
		capa_mask |= WLAN_CAPABILITY_PRIVACY;
		if (sdata->default_key)
			capa_val |= WLAN_CAPABILITY_PRIVACY;
	}

	if (ifmgd->flags & IEEE80211_STA_AUTO_CHANNEL_SEL)
		chan = NULL;

	if (ifmgd->flags & IEEE80211_STA_AUTO_BSSID_SEL)
		bssid = NULL;

	if (ifmgd->flags & IEEE80211_STA_AUTO_SSID_SEL) {
		ssid = NULL;
		ssid_len = 0;
	}

	bss = (void *)cfg80211_get_bss(local->hw.wiphy, chan,
				       bssid, ssid, ssid_len,
				       capa_mask, capa_val);

	if (bss) {
		local->oper_channel = bss->cbss.channel;
		local->oper_channel_type = NL80211_CHAN_NO_HT;
		ieee80211_hw_config(local, 0);

		if (!(ifmgd->flags & IEEE80211_STA_SSID_SET))
			ieee80211_sta_set_ssid(sdata, bss->ssid,
					       bss->ssid_len);
		ieee80211_sta_set_bssid(sdata, bss->cbss.bssid);
		ieee80211_sta_def_wmm_params(sdata, bss->supp_rates_len,
						    bss->supp_rates);
		if (sdata->u.mgd.mfp == IEEE80211_MFP_REQUIRED)
			sdata->u.mgd.flags |= IEEE80211_STA_MFP_ENABLED;
		else
			sdata->u.mgd.flags &= ~IEEE80211_STA_MFP_ENABLED;

		/* Send out direct probe if no probe resp was received or
		 * the one we have is outdated
		 */
		if (!bss->last_probe_resp ||
		    time_after(jiffies, bss->last_probe_resp
					+ IEEE80211_SCAN_RESULT_EXPIRE))
			ifmgd->state = IEEE80211_STA_MLME_DIRECT_PROBE;
		else
			ifmgd->state = IEEE80211_STA_MLME_AUTHENTICATE;

		ieee80211_rx_bss_put(local, bss);
		ieee80211_sta_reset_auth(sdata);
		return 0;
	} else {
		if (ifmgd->assoc_scan_tries < IEEE80211_ASSOC_SCANS_MAX_TRIES) {

			ifmgd->assoc_scan_tries++;

			ieee80211_request_internal_scan(sdata, ifmgd->ssid,
							ssid_len);

			ifmgd->state = IEEE80211_STA_MLME_AUTHENTICATE;
			set_bit(IEEE80211_STA_REQ_AUTH, &ifmgd->request);
		} else {
			ifmgd->assoc_scan_tries = 0;
			ifmgd->state = IEEE80211_STA_MLME_DISABLED;
			ieee80211_recalc_idle(local);
		}
	}
	return -1;
}


static void ieee80211_sta_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data, u.mgd.work);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd;
	struct sk_buff *skb;

	if (!netif_running(sdata->dev))
		return;

	if (local->sw_scanning || local->hw_scanning)
		return;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return;

	/*
	 * Nothing should have been stuffed into the workqueue during
	 * the suspend->resume cycle. If this WARN is seen then there
	 * is a bug with either the driver suspend or something in
	 * mac80211 stuffing into the workqueue which we haven't yet
	 * cleared during mac80211's suspend cycle.
	 */
	if (WARN_ON(local->suspended))
		return;

	ifmgd = &sdata->u.mgd;

	while ((skb = skb_dequeue(&ifmgd->skb_queue)))
		ieee80211_sta_rx_queued_mgmt(sdata, skb);

	if (ifmgd->state != IEEE80211_STA_MLME_DIRECT_PROBE &&
	    ifmgd->state != IEEE80211_STA_MLME_AUTHENTICATE &&
	    ifmgd->state != IEEE80211_STA_MLME_ASSOCIATE &&
	    test_and_clear_bit(IEEE80211_STA_REQ_SCAN, &ifmgd->request)) {
		queue_delayed_work(local->hw.workqueue, &local->scan_work,
				   round_jiffies_relative(0));
		return;
	}

	if (test_and_clear_bit(IEEE80211_STA_REQ_AUTH, &ifmgd->request)) {
		if (ieee80211_sta_config_auth(sdata))
			return;
		clear_bit(IEEE80211_STA_REQ_RUN, &ifmgd->request);
	} else if (!test_and_clear_bit(IEEE80211_STA_REQ_RUN, &ifmgd->request))
		return;

	ieee80211_recalc_idle(local);

	switch (ifmgd->state) {
	case IEEE80211_STA_MLME_DISABLED:
		break;
	case IEEE80211_STA_MLME_DIRECT_PROBE:
		ieee80211_direct_probe(sdata);
		break;
	case IEEE80211_STA_MLME_AUTHENTICATE:
		ieee80211_authenticate(sdata);
		break;
	case IEEE80211_STA_MLME_ASSOCIATE:
		ieee80211_associate(sdata);
		break;
	case IEEE80211_STA_MLME_ASSOCIATED:
		ieee80211_associated(sdata);
		break;
	default:
		WARN_ON(1);
		break;
	}

	if (ieee80211_privacy_mismatch(sdata)) {
		printk(KERN_DEBUG "%s: privacy configuration mismatch and "
		       "mixed-cell disabled - disassociate\n", sdata->dev->name);

		ieee80211_set_disassoc(sdata, false, true,
					WLAN_REASON_UNSPECIFIED);
	}
}

static void ieee80211_restart_sta_timer(struct ieee80211_sub_if_data *sdata)
{
	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		/*
		 * Need to update last_beacon to avoid beacon loss
		 * test to trigger.
		 */
		sdata->u.mgd.last_beacon = jiffies;


		queue_work(sdata->local->hw.workqueue,
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
	u32 hw_flags;

	ifmgd = &sdata->u.mgd;
	INIT_WORK(&ifmgd->work, ieee80211_sta_work);
	INIT_WORK(&ifmgd->chswitch_work, ieee80211_chswitch_work);
	INIT_WORK(&ifmgd->beacon_loss_work, ieee80211_beacon_loss_work);
	setup_timer(&ifmgd->timer, ieee80211_sta_timer,
		    (unsigned long) sdata);
	setup_timer(&ifmgd->chswitch_timer, ieee80211_chswitch_timer,
		    (unsigned long) sdata);
	skb_queue_head_init(&ifmgd->skb_queue);

	ifmgd->capab = WLAN_CAPABILITY_ESS;
	ifmgd->auth_algs = IEEE80211_AUTH_ALG_OPEN |
		IEEE80211_AUTH_ALG_SHARED_KEY;
	ifmgd->flags |= IEEE80211_STA_CREATE_IBSS |
		IEEE80211_STA_AUTO_BSSID_SEL |
		IEEE80211_STA_AUTO_CHANNEL_SEL;
	if (sdata->local->hw.queues >= 4)
		ifmgd->flags |= IEEE80211_STA_WMM_ENABLED;

	hw_flags = sdata->local->hw.flags;

	if (hw_flags & IEEE80211_HW_SUPPORTS_PS) {
		ifmgd->powersave = CONFIG_MAC80211_DEFAULT_PS_VALUE;
		sdata->local->hw.conf.dynamic_ps_timeout = 500;
	}
}

/* configuration hooks */
void ieee80211_sta_req_auth(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = sdata->local;

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return;

	if ((ifmgd->flags & (IEEE80211_STA_BSSID_SET |
			     IEEE80211_STA_AUTO_BSSID_SEL)) &&
	    (ifmgd->flags & (IEEE80211_STA_SSID_SET |
			     IEEE80211_STA_AUTO_SSID_SEL))) {

		if (ifmgd->state == IEEE80211_STA_MLME_ASSOCIATED)
			ieee80211_set_disassoc(sdata, true, true,
					       WLAN_REASON_DEAUTH_LEAVING);

		if (ifmgd->ssid_len == 0) {
			/*
			 * Only allow association to be started if a valid SSID
			 * is configured.
			 */
			return;
		}

		if (!(ifmgd->flags & IEEE80211_STA_EXT_SME) ||
		    ifmgd->state != IEEE80211_STA_MLME_ASSOCIATE)
			set_bit(IEEE80211_STA_REQ_AUTH, &ifmgd->request);
		else if (ifmgd->flags & IEEE80211_STA_EXT_SME)
			set_bit(IEEE80211_STA_REQ_RUN, &ifmgd->request);
		queue_work(local->hw.workqueue, &ifmgd->work);
	}
}

int ieee80211_sta_commit(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (ifmgd->ssid_len)
		ifmgd->flags |= IEEE80211_STA_SSID_SET;
	else
		ifmgd->flags &= ~IEEE80211_STA_SSID_SET;

	return 0;
}

int ieee80211_sta_set_ssid(struct ieee80211_sub_if_data *sdata, char *ssid, size_t len)
{
	struct ieee80211_if_managed *ifmgd;

	if (len > IEEE80211_MAX_SSID_LEN)
		return -EINVAL;

	ifmgd = &sdata->u.mgd;

	if (ifmgd->ssid_len != len || memcmp(ifmgd->ssid, ssid, len) != 0) {
		if (ifmgd->state == IEEE80211_STA_MLME_ASSOCIATED)
			ieee80211_set_disassoc(sdata, true, true,
					       WLAN_REASON_DEAUTH_LEAVING);

		/*
		 * Do not use reassociation if SSID is changed (different ESS).
		 */
		ifmgd->flags &= ~IEEE80211_STA_PREV_BSSID_SET;
		memset(ifmgd->ssid, 0, sizeof(ifmgd->ssid));
		memcpy(ifmgd->ssid, ssid, len);
		ifmgd->ssid_len = len;
	}

	return ieee80211_sta_commit(sdata);
}

int ieee80211_sta_get_ssid(struct ieee80211_sub_if_data *sdata, char *ssid, size_t *len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	memcpy(ssid, ifmgd->ssid, ifmgd->ssid_len);
	*len = ifmgd->ssid_len;
	return 0;
}

int ieee80211_sta_set_bssid(struct ieee80211_sub_if_data *sdata, u8 *bssid)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (compare_ether_addr(bssid, ifmgd->bssid) != 0 &&
	    ifmgd->state == IEEE80211_STA_MLME_ASSOCIATED)
		ieee80211_set_disassoc(sdata, true, true,
				       WLAN_REASON_DEAUTH_LEAVING);

	if (is_valid_ether_addr(bssid)) {
		memcpy(ifmgd->bssid, bssid, ETH_ALEN);
		ifmgd->flags |= IEEE80211_STA_BSSID_SET;
	} else {
		memset(ifmgd->bssid, 0, ETH_ALEN);
		ifmgd->flags &= ~IEEE80211_STA_BSSID_SET;
	}

	return ieee80211_sta_commit(sdata);
}

int ieee80211_sta_set_extra_ie(struct ieee80211_sub_if_data *sdata,
			       const char *ie, size_t len)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	if (len == 0 && ifmgd->extra_ie_len == 0)
		return -EALREADY;

	if (len == ifmgd->extra_ie_len && ifmgd->extra_ie &&
	    memcmp(ifmgd->extra_ie, ie, len) == 0)
		return -EALREADY;

	kfree(ifmgd->extra_ie);
	if (len == 0) {
		ifmgd->extra_ie = NULL;
		ifmgd->extra_ie_len = 0;
		return 0;
	}
	ifmgd->extra_ie = kmalloc(len, GFP_KERNEL);
	if (!ifmgd->extra_ie) {
		ifmgd->extra_ie_len = 0;
		return -ENOMEM;
	}
	memcpy(ifmgd->extra_ie, ie, len);
	ifmgd->extra_ie_len = len;
	return 0;
}

int ieee80211_sta_deauthenticate(struct ieee80211_sub_if_data *sdata, u16 reason)
{
	printk(KERN_DEBUG "%s: deauthenticating by local choice (reason=%d)\n",
	       sdata->dev->name, reason);

	ieee80211_set_disassoc(sdata, true, true, reason);
	return 0;
}

int ieee80211_sta_disassociate(struct ieee80211_sub_if_data *sdata, u16 reason)
{
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	printk(KERN_DEBUG "%s: disassociating by local choice (reason=%d)\n",
	       sdata->dev->name, reason);

	if (!(ifmgd->flags & IEEE80211_STA_ASSOCIATED))
		return -ENOLINK;

	ieee80211_set_disassoc(sdata, false, true, reason);
	return 0;
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
