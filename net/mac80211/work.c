/*
 * mac80211 work implementation
 *
 * Copyright 2003-2008, Jouni Malinen <j@w1.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2009, Johannes Berg <johannes@sipsolutions.net>
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
#include <linux/crc32.h>
#include <net/mac80211.h>
#include <asm/unaligned.h>

#include "ieee80211_i.h"
#include "rate.h"

#define IEEE80211_AUTH_TIMEOUT (HZ / 5)
#define IEEE80211_AUTH_MAX_TRIES 3
#define IEEE80211_ASSOC_TIMEOUT (HZ / 5)
#define IEEE80211_ASSOC_MAX_TRIES 3
#define IEEE80211_MAX_PROBE_TRIES 5

enum work_action {
	WORK_ACT_NONE,
	WORK_ACT_TIMEOUT,
	WORK_ACT_DONE,
};


/* utils */
static inline void ASSERT_WORK_MTX(struct ieee80211_local *local)
{
	WARN_ON(!mutex_is_locked(&local->work_mtx));
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
static void run_again(struct ieee80211_local *local,
		      unsigned long timeout)
{
	ASSERT_WORK_MTX(local);

	if (!timer_pending(&local->work_timer) ||
	    time_before(timeout, local->work_timer.expires))
		mod_timer(&local->work_timer, timeout);
}

static void work_free_rcu(struct rcu_head *head)
{
	struct ieee80211_work *wk =
		container_of(head, struct ieee80211_work, rcu_head);

	kfree(wk);
}

void free_work(struct ieee80211_work *wk)
{
	call_rcu(&wk->rcu_head, work_free_rcu);
}

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

/* frame sending functions */

static void ieee80211_send_assoc(struct ieee80211_sub_if_data *sdata,
				 struct ieee80211_work *wk)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u8 *pos;
	const u8 *ies, *ht_ie;
	int i, len, count, rates_len, supp_rates_len;
	u16 capab;
	struct ieee80211_supported_band *sband;
	u32 rates = 0;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    sizeof(*mgmt) + 200 + wk->ie_len +
			    wk->assoc.ssid_len);
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for assoc "
		       "frame\n", sdata->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	sband = local->hw.wiphy->bands[wk->chan->band];

	capab = WLAN_CAPABILITY_ESS;

	if (sband->band == IEEE80211_BAND_2GHZ) {
		if (!(local->hw.flags & IEEE80211_HW_2GHZ_SHORT_SLOT_INCAPABLE))
			capab |= WLAN_CAPABILITY_SHORT_SLOT_TIME;
		if (!(local->hw.flags & IEEE80211_HW_2GHZ_SHORT_PREAMBLE_INCAPABLE))
			capab |= WLAN_CAPABILITY_SHORT_PREAMBLE;
	}

	if (wk->assoc.capability & WLAN_CAPABILITY_PRIVACY)
		capab |= WLAN_CAPABILITY_PRIVACY;

	/*
	 * Get all rates supported by the device and the AP as
	 * some APs don't like getting a superset of their rates
	 * in the association request (e.g. D-Link DAP 1353 in
	 * b-only mode)...
	 */
	rates_len = ieee80211_compatible_rates(wk->assoc.supp_rates,
					       wk->assoc.supp_rates_len,
					       sband, &rates);

	if ((wk->assoc.capability & WLAN_CAPABILITY_SPECTRUM_MGMT) &&
	    (local->hw.flags & IEEE80211_HW_SPECTRUM_MGMT))
		capab |= WLAN_CAPABILITY_SPECTRUM_MGMT;

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, wk->filter_ta, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, wk->filter_ta, ETH_ALEN);

	if (!is_zero_ether_addr(wk->assoc.prev_bssid)) {
		skb_put(skb, 10);
		mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						  IEEE80211_STYPE_REASSOC_REQ);
		mgmt->u.reassoc_req.capab_info = cpu_to_le16(capab);
		mgmt->u.reassoc_req.listen_interval =
				cpu_to_le16(local->hw.conf.listen_interval);
		memcpy(mgmt->u.reassoc_req.current_ap, wk->assoc.prev_bssid,
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
	ies = pos = skb_put(skb, 2 + wk->assoc.ssid_len);
	*pos++ = WLAN_EID_SSID;
	*pos++ = wk->assoc.ssid_len;
	memcpy(pos, wk->assoc.ssid, wk->assoc.ssid_len);

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

	if (wk->assoc.wmm_used && local->hw.queues >= 4) {
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
	if (wk->assoc.use_11n && wk->assoc.wmm_used &&
	    (local->hw.queues >= 4) &&
	    sband->ht_cap.ht_supported &&
	    (ht_ie = wk->assoc.ht_information_ie) &&
	    ht_ie[1] >= sizeof(struct ieee80211_ht_info)) {
		struct ieee80211_ht_info *ht_info =
			(struct ieee80211_ht_info *)(ht_ie + 2);
		u16 cap = sband->ht_cap.cap;
		__le16 tmp;
		u32 flags = local->hw.conf.channel->flags;

		/* determine capability flags */

		if (ieee80211_disable_40mhz_24ghz &&
		    sband->band == IEEE80211_BAND_2GHZ) {
			cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
			cap &= ~IEEE80211_HT_CAP_SGI_40;
		}

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
		switch (wk->assoc.smps) {
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
		*pos++ = WLAN_EID_HT_CAPABILITY;
		*pos++ = sizeof(struct ieee80211_ht_cap);
		memset(pos, 0, sizeof(struct ieee80211_ht_cap));

		/* capability flags */
		tmp = cpu_to_le16(cap);
		memcpy(pos, &tmp, sizeof(u16));
		pos += sizeof(u16);

		/* AMPDU parameters */
		*pos++ = sband->ht_cap.ampdu_factor |
			 (sband->ht_cap.ampdu_density <<
				IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT);

		/* MCS set */
		memcpy(pos, &sband->ht_cap.mcs, sizeof(sband->ht_cap.mcs));
		pos += sizeof(sband->ht_cap.mcs);

		/* extended capabilities */
		pos += sizeof(__le16);

		/* BF capabilities */
		pos += sizeof(__le32);

		/* antenna selection */
		pos += sizeof(u8);
	}

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	ieee80211_tx_skb(sdata, skb);
}

static void ieee80211_remove_auth_bss(struct ieee80211_local *local,
				      struct ieee80211_work *wk)
{
	struct cfg80211_bss *cbss;
	u16 capa_val = WLAN_CAPABILITY_ESS;

	if (wk->probe_auth.privacy)
		capa_val |= WLAN_CAPABILITY_PRIVACY;

	cbss = cfg80211_get_bss(local->hw.wiphy, wk->chan, wk->filter_ta,
				wk->probe_auth.ssid, wk->probe_auth.ssid_len,
				WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_PRIVACY,
				capa_val);
	if (!cbss)
		return;

	cfg80211_unlink_bss(local->hw.wiphy, cbss);
	cfg80211_put_bss(cbss);
}

static enum work_action __must_check
ieee80211_direct_probe(struct ieee80211_work *wk)
{
	struct ieee80211_sub_if_data *sdata = wk->sdata;
	struct ieee80211_local *local = sdata->local;

	wk->probe_auth.tries++;
	if (wk->probe_auth.tries > IEEE80211_AUTH_MAX_TRIES) {
		printk(KERN_DEBUG "%s: direct probe to AP %pM timed out\n",
		       sdata->name, wk->filter_ta);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss struct for that AP.
		 */
		ieee80211_remove_auth_bss(local, wk);

		/*
		 * We might have a pending scan which had no chance to run yet
		 * due to work needing to be done. Hence, queue the STAs work
		 * again for that.
		 */
		ieee80211_queue_work(&local->hw, &local->work_work);
		return WORK_ACT_TIMEOUT;
	}

	printk(KERN_DEBUG "%s: direct probe to AP %pM (try %d)\n",
			sdata->name, wk->filter_ta, wk->probe_auth.tries);

	/*
	 * Direct probe is sent to broadcast address as some APs
	 * will not answer to direct packet in unassociated state.
	 */
	ieee80211_send_probe_req(sdata, NULL, wk->probe_auth.ssid,
				 wk->probe_auth.ssid_len, NULL, 0);

	wk->timeout = jiffies + IEEE80211_AUTH_TIMEOUT;
	run_again(local, wk->timeout);

	return WORK_ACT_NONE;
}


static enum work_action __must_check
ieee80211_authenticate(struct ieee80211_work *wk)
{
	struct ieee80211_sub_if_data *sdata = wk->sdata;
	struct ieee80211_local *local = sdata->local;

	wk->probe_auth.tries++;
	if (wk->probe_auth.tries > IEEE80211_AUTH_MAX_TRIES) {
		printk(KERN_DEBUG "%s: authentication with AP %pM"
		       " timed out\n", sdata->name, wk->filter_ta);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss struct for that AP.
		 */
		ieee80211_remove_auth_bss(local, wk);

		/*
		 * We might have a pending scan which had no chance to run yet
		 * due to work needing to be done. Hence, queue the STAs work
		 * again for that.
		 */
		ieee80211_queue_work(&local->hw, &local->work_work);
		return WORK_ACT_TIMEOUT;
	}

	printk(KERN_DEBUG "%s: authenticate with AP %pM (try %d)\n",
	       sdata->name, wk->filter_ta, wk->probe_auth.tries);

	ieee80211_send_auth(sdata, 1, wk->probe_auth.algorithm, wk->ie,
			    wk->ie_len, wk->filter_ta, NULL, 0, 0);
	wk->probe_auth.transaction = 2;

	wk->timeout = jiffies + IEEE80211_AUTH_TIMEOUT;
	run_again(local, wk->timeout);

	return WORK_ACT_NONE;
}

static enum work_action __must_check
ieee80211_associate(struct ieee80211_work *wk)
{
	struct ieee80211_sub_if_data *sdata = wk->sdata;
	struct ieee80211_local *local = sdata->local;

	wk->assoc.tries++;
	if (wk->assoc.tries > IEEE80211_ASSOC_MAX_TRIES) {
		printk(KERN_DEBUG "%s: association with AP %pM"
		       " timed out\n",
		       sdata->name, wk->filter_ta);

		/*
		 * Most likely AP is not in the range so remove the
		 * bss struct for that AP.
		 */
		if (wk->assoc.bss)
			cfg80211_unlink_bss(local->hw.wiphy,
					    &wk->assoc.bss->cbss);

		/*
		 * We might have a pending scan which had no chance to run yet
		 * due to work needing to be done. Hence, queue the STAs work
		 * again for that.
		 */
		ieee80211_queue_work(&local->hw, &local->work_work);
		return WORK_ACT_TIMEOUT;
	}

	printk(KERN_DEBUG "%s: associate with AP %pM (try %d)\n",
	       sdata->name, wk->filter_ta, wk->assoc.tries);
	ieee80211_send_assoc(sdata, wk);

	wk->timeout = jiffies + IEEE80211_ASSOC_TIMEOUT;
	run_again(local, wk->timeout);

	return WORK_ACT_NONE;
}

static void ieee80211_auth_challenge(struct ieee80211_work *wk,
				     struct ieee80211_mgmt *mgmt,
				     size_t len)
{
	struct ieee80211_sub_if_data *sdata = wk->sdata;
	u8 *pos;
	struct ieee802_11_elems elems;

	pos = mgmt->u.auth.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), &elems);
	if (!elems.challenge)
		return;
	ieee80211_send_auth(sdata, 3, wk->probe_auth.algorithm,
			    elems.challenge - 2, elems.challenge_len + 2,
			    wk->filter_ta, wk->probe_auth.key,
			    wk->probe_auth.key_len, wk->probe_auth.key_idx);
	wk->probe_auth.transaction = 4;
}

static enum work_action __must_check
ieee80211_rx_mgmt_auth(struct ieee80211_work *wk,
		       struct ieee80211_mgmt *mgmt, size_t len)
{
	u16 auth_alg, auth_transaction, status_code;

	if (wk->type != IEEE80211_WORK_AUTH)
		return WORK_ACT_NONE;

	if (len < 24 + 6)
		return WORK_ACT_NONE;

	auth_alg = le16_to_cpu(mgmt->u.auth.auth_alg);
	auth_transaction = le16_to_cpu(mgmt->u.auth.auth_transaction);
	status_code = le16_to_cpu(mgmt->u.auth.status_code);

	if (auth_alg != wk->probe_auth.algorithm ||
	    auth_transaction != wk->probe_auth.transaction)
		return WORK_ACT_NONE;

	if (status_code != WLAN_STATUS_SUCCESS) {
		printk(KERN_DEBUG "%s: %pM denied authentication (status %d)\n",
		       wk->sdata->name, mgmt->sa, status_code);
		return WORK_ACT_DONE;
	}

	switch (wk->probe_auth.algorithm) {
	case WLAN_AUTH_OPEN:
	case WLAN_AUTH_LEAP:
	case WLAN_AUTH_FT:
		break;
	case WLAN_AUTH_SHARED_KEY:
		if (wk->probe_auth.transaction != 4) {
			ieee80211_auth_challenge(wk, mgmt, len);
			/* need another frame */
			return WORK_ACT_NONE;
		}
		break;
	default:
		WARN_ON(1);
		return WORK_ACT_NONE;
	}

	printk(KERN_DEBUG "%s: authenticated\n", wk->sdata->name);
	return WORK_ACT_DONE;
}

static enum work_action __must_check
ieee80211_rx_mgmt_assoc_resp(struct ieee80211_work *wk,
			     struct ieee80211_mgmt *mgmt, size_t len,
			     bool reassoc)
{
	struct ieee80211_sub_if_data *sdata = wk->sdata;
	struct ieee80211_local *local = sdata->local;
	u16 capab_info, status_code, aid;
	struct ieee802_11_elems elems;
	u8 *pos;

	/*
	 * AssocResp and ReassocResp have identical structure, so process both
	 * of them in this function.
	 */

	if (len < 24 + 6)
		return WORK_ACT_NONE;

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
		printk(KERN_DEBUG "%s: AP rejected association temporarily; "
		       "comeback duration %u TU (%u ms)\n",
		       sdata->name, tu, ms);
		wk->timeout = jiffies + msecs_to_jiffies(ms);
		if (ms > IEEE80211_ASSOC_TIMEOUT)
			run_again(local, wk->timeout);
		return WORK_ACT_NONE;
	}

	if (status_code != WLAN_STATUS_SUCCESS)
		printk(KERN_DEBUG "%s: AP denied association (code=%d)\n",
		       sdata->name, status_code);
	else
		printk(KERN_DEBUG "%s: associated\n", sdata->name);

	return WORK_ACT_DONE;
}

static enum work_action __must_check
ieee80211_rx_mgmt_probe_resp(struct ieee80211_work *wk,
			     struct ieee80211_mgmt *mgmt, size_t len,
			     struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_sub_if_data *sdata = wk->sdata;
	struct ieee80211_local *local = sdata->local;
	size_t baselen;

	ASSERT_WORK_MTX(local);

	baselen = (u8 *) mgmt->u.probe_resp.variable - (u8 *) mgmt;
	if (baselen > len)
		return WORK_ACT_NONE;

	printk(KERN_DEBUG "%s: direct probe responded\n", sdata->name);
	return WORK_ACT_DONE;
}

static void ieee80211_work_rx_queued_mgmt(struct ieee80211_local *local,
					  struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_work *wk;
	enum work_action rma = WORK_ACT_NONE;
	u16 fc;

	rx_status = (struct ieee80211_rx_status *) skb->cb;
	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	mutex_lock(&local->work_mtx);

	list_for_each_entry(wk, &local->work_list, list) {
		const u8 *bssid = NULL;

		switch (wk->type) {
		case IEEE80211_WORK_DIRECT_PROBE:
		case IEEE80211_WORK_AUTH:
		case IEEE80211_WORK_ASSOC:
			bssid = wk->filter_ta;
			break;
		default:
			continue;
		}

		/*
		 * Before queuing, we already verified mgmt->sa,
		 * so this is needed just for matching.
		 */
		if (compare_ether_addr(bssid, mgmt->bssid))
			continue;

		switch (fc & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_PROBE_RESP:
			rma = ieee80211_rx_mgmt_probe_resp(wk, mgmt, skb->len,
							   rx_status);
			break;
		case IEEE80211_STYPE_AUTH:
			rma = ieee80211_rx_mgmt_auth(wk, mgmt, skb->len);
			break;
		case IEEE80211_STYPE_ASSOC_RESP:
			rma = ieee80211_rx_mgmt_assoc_resp(wk, mgmt,
							   skb->len, false);
			break;
		case IEEE80211_STYPE_REASSOC_RESP:
			rma = ieee80211_rx_mgmt_assoc_resp(wk, mgmt,
							   skb->len, true);
			break;
		default:
			WARN_ON(1);
		}
		/*
		 * We've processed this frame for that work, so it can't
		 * belong to another work struct.
		 * NB: this is also required for correctness for 'rma'!
		 */
		break;
	}

	switch (rma) {
	case WORK_ACT_NONE:
		break;
	case WORK_ACT_DONE:
		list_del_rcu(&wk->list);
		break;
	default:
		WARN(1, "unexpected: %d", rma);
	}

	mutex_unlock(&local->work_mtx);

	if (rma != WORK_ACT_DONE)
		goto out;

	switch (wk->done(wk, skb)) {
	case WORK_DONE_DESTROY:
		free_work(wk);
		break;
	case WORK_DONE_REQUEUE:
		synchronize_rcu();
		wk->timeout = jiffies; /* run again directly */
		mutex_lock(&local->work_mtx);
		list_add_tail(&wk->list, &local->work_list);
		mutex_unlock(&local->work_mtx);
	}

 out:
	kfree_skb(skb);
}

static void ieee80211_work_timer(unsigned long data)
{
	struct ieee80211_local *local = (void *) data;

	if (local->quiescing)
		return;

	ieee80211_queue_work(&local->hw, &local->work_work);
}

static void ieee80211_work_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, work_work);
	struct sk_buff *skb;
	struct ieee80211_work *wk, *tmp;
	LIST_HEAD(free_work);
	enum work_action rma;

	if (local->scanning)
		return;

	/*
	 * ieee80211_queue_work() should have picked up most cases,
	 * here we'll pick the the rest.
	 */
	if (WARN(local->suspended, "work scheduled while going to suspend\n"))
		return;

	/* first process frames to avoid timing out while a frame is pending */
	while ((skb = skb_dequeue(&local->work_skb_queue)))
		ieee80211_work_rx_queued_mgmt(local, skb);

	ieee80211_recalc_idle(local);

	mutex_lock(&local->work_mtx);

	list_for_each_entry_safe(wk, tmp, &local->work_list, list) {
		if (time_is_after_jiffies(wk->timeout)) {
			/*
			 * This work item isn't supposed to be worked on
			 * right now, but take care to adjust the timer
			 * properly.
			 */
			run_again(local, wk->timeout);
			continue;
		}

		switch (wk->type) {
		default:
			WARN_ON(1);
			/* nothing */
			rma = WORK_ACT_NONE;
			break;
		case IEEE80211_WORK_DIRECT_PROBE:
			rma = ieee80211_direct_probe(wk);
			break;
		case IEEE80211_WORK_AUTH:
			rma = ieee80211_authenticate(wk);
			break;
		case IEEE80211_WORK_ASSOC:
			rma = ieee80211_associate(wk);
			break;
		}

		switch (rma) {
		case WORK_ACT_NONE:
			/* no action required */
			break;
		case WORK_ACT_TIMEOUT:
			list_del_rcu(&wk->list);
			synchronize_rcu();
			list_add(&wk->list, &free_work);
			break;
		default:
			WARN(1, "unexpected: %d", rma);
		}
	}

	if (list_empty(&local->work_list) && local->scan_req)
		ieee80211_queue_delayed_work(&local->hw,
					     &local->scan_work,
					     round_jiffies_relative(0));

	mutex_unlock(&local->work_mtx);

	list_for_each_entry_safe(wk, tmp, &free_work, list) {
		wk->done(wk, NULL);
		list_del(&wk->list);
		kfree(wk);
	}
}

void ieee80211_add_work(struct ieee80211_work *wk)
{
	struct ieee80211_local *local;

	if (WARN_ON(!wk->chan))
		return;

	if (WARN_ON(!wk->sdata))
		return;

	if (WARN_ON(!wk->done))
		return;

	wk->timeout = jiffies;

	local = wk->sdata->local;
	mutex_lock(&local->work_mtx);
	list_add_tail(&wk->list, &local->work_list);
	mutex_unlock(&local->work_mtx);

	ieee80211_queue_work(&local->hw, &local->work_work);
}

void ieee80211_work_init(struct ieee80211_local *local)
{
	mutex_init(&local->work_mtx);
	INIT_LIST_HEAD(&local->work_list);
	setup_timer(&local->work_timer, ieee80211_work_timer,
		    (unsigned long)local);
	INIT_WORK(&local->work_work, ieee80211_work_work);
	skb_queue_head_init(&local->work_skb_queue);
}

void ieee80211_work_purge(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_work *wk, *tmp;

	mutex_lock(&local->work_mtx);
	list_for_each_entry_safe(wk, tmp, &local->work_list, list) {
		if (wk->sdata != sdata)
			continue;
		list_del(&wk->list);
		free_work(wk);
	}
	mutex_unlock(&local->work_mtx);
}

ieee80211_rx_result ieee80211_work_rx_mgmt(struct ieee80211_sub_if_data *sdata,
					   struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_work *wk;
	u16 fc;

	if (skb->len < 24)
		return RX_DROP_MONITOR;

	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	list_for_each_entry_rcu(wk, &local->work_list, list) {
		if (sdata != wk->sdata)
			continue;
		if (compare_ether_addr(wk->filter_ta, mgmt->sa))
			continue;
		if (compare_ether_addr(wk->filter_ta, mgmt->bssid))
			continue;

		switch (fc & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_AUTH:
		case IEEE80211_STYPE_PROBE_RESP:
		case IEEE80211_STYPE_ASSOC_RESP:
		case IEEE80211_STYPE_REASSOC_RESP:
		case IEEE80211_STYPE_DEAUTH:
		case IEEE80211_STYPE_DISASSOC:
			skb_queue_tail(&local->work_skb_queue, skb);
			ieee80211_queue_work(&local->hw, &local->work_work);
			return RX_QUEUED;
		}
	}

	return RX_CONTINUE;
}
