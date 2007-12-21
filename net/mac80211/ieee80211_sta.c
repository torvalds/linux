/*
 * BSS client mode implementation
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* TODO:
 * order BSS list by RSSI(?) ("quality of AP")
 * scan result table filtering (by capability (privacy, IBSS/BSS, WPA/RSN IE,
 *    SSID)
 */
#include <linux/delay.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <linux/random.h>
#include <linux/etherdevice.h>
#include <net/iw_handler.h>
#include <asm/types.h>

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "ieee80211_rate.h"
#include "ieee80211_led.h"

#define IEEE80211_AUTH_TIMEOUT (HZ / 5)
#define IEEE80211_AUTH_MAX_TRIES 3
#define IEEE80211_ASSOC_TIMEOUT (HZ / 5)
#define IEEE80211_ASSOC_MAX_TRIES 3
#define IEEE80211_MONITORING_INTERVAL (2 * HZ)
#define IEEE80211_PROBE_INTERVAL (60 * HZ)
#define IEEE80211_RETRY_AUTH_INTERVAL (1 * HZ)
#define IEEE80211_SCAN_INTERVAL (2 * HZ)
#define IEEE80211_SCAN_INTERVAL_SLOW (15 * HZ)
#define IEEE80211_IBSS_JOIN_TIMEOUT (20 * HZ)

#define IEEE80211_PROBE_DELAY (HZ / 33)
#define IEEE80211_CHANNEL_TIME (HZ / 33)
#define IEEE80211_PASSIVE_CHANNEL_TIME (HZ / 5)
#define IEEE80211_SCAN_RESULT_EXPIRE (10 * HZ)
#define IEEE80211_IBSS_MERGE_INTERVAL (30 * HZ)
#define IEEE80211_IBSS_INACTIVITY_LIMIT (60 * HZ)

#define IEEE80211_IBSS_MAX_STA_ENTRIES 128


#define IEEE80211_FC(type, stype) cpu_to_le16(type | stype)

#define ERP_INFO_USE_PROTECTION BIT(1)

static void ieee80211_send_probe_req(struct net_device *dev, u8 *dst,
				     u8 *ssid, size_t ssid_len);
static struct ieee80211_sta_bss *
ieee80211_rx_bss_get(struct net_device *dev, u8 *bssid, int channel,
		     u8 *ssid, u8 ssid_len);
static void ieee80211_rx_bss_put(struct net_device *dev,
				 struct ieee80211_sta_bss *bss);
static int ieee80211_sta_find_ibss(struct net_device *dev,
				   struct ieee80211_if_sta *ifsta);
static int ieee80211_sta_wep_configured(struct net_device *dev);
static int ieee80211_sta_start_scan(struct net_device *dev,
				    u8 *ssid, size_t ssid_len);
static int ieee80211_sta_config_auth(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta);


/* Parsed Information Elements */
struct ieee802_11_elems {
	/* pointers to IEs */
	u8 *ssid;
	u8 *supp_rates;
	u8 *fh_params;
	u8 *ds_params;
	u8 *cf_params;
	u8 *tim;
	u8 *ibss_params;
	u8 *challenge;
	u8 *wpa;
	u8 *rsn;
	u8 *erp_info;
	u8 *ext_supp_rates;
	u8 *wmm_info;
	u8 *wmm_param;

	/* length of them, respectively */
	u8 ssid_len;
	u8 supp_rates_len;
	u8 fh_params_len;
	u8 ds_params_len;
	u8 cf_params_len;
	u8 tim_len;
	u8 ibss_params_len;
	u8 challenge_len;
	u8 wpa_len;
	u8 rsn_len;
	u8 erp_info_len;
	u8 ext_supp_rates_len;
	u8 wmm_info_len;
	u8 wmm_param_len;
};

static void ieee802_11_parse_elems(u8 *start, size_t len,
				   struct ieee802_11_elems *elems)
{
	size_t left = len;
	u8 *pos = start;

	memset(elems, 0, sizeof(*elems));

	while (left >= 2) {
		u8 id, elen;

		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left)
			return;

		switch (id) {
		case WLAN_EID_SSID:
			elems->ssid = pos;
			elems->ssid_len = elen;
			break;
		case WLAN_EID_SUPP_RATES:
			elems->supp_rates = pos;
			elems->supp_rates_len = elen;
			break;
		case WLAN_EID_FH_PARAMS:
			elems->fh_params = pos;
			elems->fh_params_len = elen;
			break;
		case WLAN_EID_DS_PARAMS:
			elems->ds_params = pos;
			elems->ds_params_len = elen;
			break;
		case WLAN_EID_CF_PARAMS:
			elems->cf_params = pos;
			elems->cf_params_len = elen;
			break;
		case WLAN_EID_TIM:
			elems->tim = pos;
			elems->tim_len = elen;
			break;
		case WLAN_EID_IBSS_PARAMS:
			elems->ibss_params = pos;
			elems->ibss_params_len = elen;
			break;
		case WLAN_EID_CHALLENGE:
			elems->challenge = pos;
			elems->challenge_len = elen;
			break;
		case WLAN_EID_WPA:
			if (elen >= 4 && pos[0] == 0x00 && pos[1] == 0x50 &&
			    pos[2] == 0xf2) {
				/* Microsoft OUI (00:50:F2) */
				if (pos[3] == 1) {
					/* OUI Type 1 - WPA IE */
					elems->wpa = pos;
					elems->wpa_len = elen;
				} else if (elen >= 5 && pos[3] == 2) {
					if (pos[4] == 0) {
						elems->wmm_info = pos;
						elems->wmm_info_len = elen;
					} else if (pos[4] == 1) {
						elems->wmm_param = pos;
						elems->wmm_param_len = elen;
					}
				}
			}
			break;
		case WLAN_EID_RSN:
			elems->rsn = pos;
			elems->rsn_len = elen;
			break;
		case WLAN_EID_ERP_INFO:
			elems->erp_info = pos;
			elems->erp_info_len = elen;
			break;
		case WLAN_EID_EXT_SUPP_RATES:
			elems->ext_supp_rates = pos;
			elems->ext_supp_rates_len = elen;
			break;
		default:
			break;
		}

		left -= elen;
		pos += elen;
	}
}


static int ecw2cw(int ecw)
{
	int cw = 1;
	while (ecw > 0) {
		cw <<= 1;
		ecw--;
	}
	return cw - 1;
}

static void ieee80211_sta_wmm_params(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta,
				     u8 *wmm_param, size_t wmm_param_len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_tx_queue_params params;
	size_t left;
	int count;
	u8 *pos;

	if (wmm_param_len < 8 || wmm_param[5] /* version */ != 1)
		return;
	count = wmm_param[6] & 0x0f;
	if (count == ifsta->wmm_last_param_set)
		return;
	ifsta->wmm_last_param_set = count;

	pos = wmm_param + 8;
	left = wmm_param_len - 8;

	memset(&params, 0, sizeof(params));

	if (!local->ops->conf_tx)
		return;

	local->wmm_acm = 0;
	for (; left >= 4; left -= 4, pos += 4) {
		int aci = (pos[0] >> 5) & 0x03;
		int acm = (pos[0] >> 4) & 0x01;
		int queue;

		switch (aci) {
		case 1:
			queue = IEEE80211_TX_QUEUE_DATA3;
			if (acm) {
				local->wmm_acm |= BIT(0) | BIT(3);
			}
			break;
		case 2:
			queue = IEEE80211_TX_QUEUE_DATA1;
			if (acm) {
				local->wmm_acm |= BIT(4) | BIT(5);
			}
			break;
		case 3:
			queue = IEEE80211_TX_QUEUE_DATA0;
			if (acm) {
				local->wmm_acm |= BIT(6) | BIT(7);
			}
			break;
		case 0:
		default:
			queue = IEEE80211_TX_QUEUE_DATA2;
			if (acm) {
				local->wmm_acm |= BIT(1) | BIT(2);
			}
			break;
		}

		params.aifs = pos[0] & 0x0f;
		params.cw_max = ecw2cw((pos[1] & 0xf0) >> 4);
		params.cw_min = ecw2cw(pos[1] & 0x0f);
		/* TXOP is in units of 32 usec; burst_time in 0.1 ms */
		params.burst_time = (pos[2] | (pos[3] << 8)) * 32 / 100;
		printk(KERN_DEBUG "%s: WMM queue=%d aci=%d acm=%d aifs=%d "
		       "cWmin=%d cWmax=%d burst=%d\n",
		       dev->name, queue, aci, acm, params.aifs, params.cw_min,
		       params.cw_max, params.burst_time);
		/* TODO: handle ACM (block TX, fallback to next lowest allowed
		 * AC for now) */
		if (local->ops->conf_tx(local_to_hw(local), queue, &params)) {
			printk(KERN_DEBUG "%s: failed to set TX queue "
			       "parameters for queue %d\n", dev->name, queue);
		}
	}
}


static void ieee80211_handle_erp_ie(struct net_device *dev, u8 erp_value)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	int use_protection = (erp_value & WLAN_ERP_USE_PROTECTION) != 0;
	int preamble_mode = (erp_value & WLAN_ERP_BARKER_PREAMBLE) != 0;
	u8 changes = 0;
	DECLARE_MAC_BUF(mac);

	if (use_protection != !!(sdata->flags & IEEE80211_SDATA_USE_PROTECTION)) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: CTS protection %s (BSSID="
			       "%s)\n",
			       dev->name,
			       use_protection ? "enabled" : "disabled",
			       print_mac(mac, ifsta->bssid));
		}
		if (use_protection)
			sdata->flags |= IEEE80211_SDATA_USE_PROTECTION;
		else
			sdata->flags &= ~IEEE80211_SDATA_USE_PROTECTION;
		changes |= IEEE80211_ERP_CHANGE_PROTECTION;
	}

	if (preamble_mode != !(sdata->flags & IEEE80211_SDATA_SHORT_PREAMBLE)) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: switched to %s barker preamble"
			       " (BSSID=%s)\n",
			       dev->name,
			       (preamble_mode == WLAN_ERP_PREAMBLE_SHORT) ?
					"short" : "long",
			       print_mac(mac, ifsta->bssid));
		}
		if (preamble_mode)
			sdata->flags &= ~IEEE80211_SDATA_SHORT_PREAMBLE;
		else
			sdata->flags |= IEEE80211_SDATA_SHORT_PREAMBLE;
		changes |= IEEE80211_ERP_CHANGE_PREAMBLE;
	}

	if (changes)
		ieee80211_erp_info_change_notify(dev, changes);
}


static void ieee80211_sta_send_associnfo(struct net_device *dev,
					 struct ieee80211_if_sta *ifsta)
{
	char *buf;
	size_t len;
	int i;
	union iwreq_data wrqu;

	if (!ifsta->assocreq_ies && !ifsta->assocresp_ies)
		return;

	buf = kmalloc(50 + 2 * (ifsta->assocreq_ies_len +
				ifsta->assocresp_ies_len), GFP_KERNEL);
	if (!buf)
		return;

	len = sprintf(buf, "ASSOCINFO(");
	if (ifsta->assocreq_ies) {
		len += sprintf(buf + len, "ReqIEs=");
		for (i = 0; i < ifsta->assocreq_ies_len; i++) {
			len += sprintf(buf + len, "%02x",
				       ifsta->assocreq_ies[i]);
		}
	}
	if (ifsta->assocresp_ies) {
		if (ifsta->assocreq_ies)
			len += sprintf(buf + len, " ");
		len += sprintf(buf + len, "RespIEs=");
		for (i = 0; i < ifsta->assocresp_ies_len; i++) {
			len += sprintf(buf + len, "%02x",
				       ifsta->assocresp_ies[i]);
		}
	}
	len += sprintf(buf + len, ")");

	if (len > IW_CUSTOM_MAX) {
		len = sprintf(buf, "ASSOCRESPIE=");
		for (i = 0; i < ifsta->assocresp_ies_len; i++) {
			len += sprintf(buf + len, "%02x",
				       ifsta->assocresp_ies[i]);
		}
	}

	memset(&wrqu, 0, sizeof(wrqu));
	wrqu.data.length = len;
	wireless_send_event(dev, IWEVCUSTOM, &wrqu, buf);

	kfree(buf);
}


static void ieee80211_set_associated(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta,
				     bool assoc)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	union iwreq_data wrqu;

	if (!!(ifsta->flags & IEEE80211_STA_ASSOCIATED) == assoc)
		return;

	if (assoc) {
		struct ieee80211_sub_if_data *sdata;
		struct ieee80211_sta_bss *bss;

		ifsta->flags |= IEEE80211_STA_ASSOCIATED;

		sdata = IEEE80211_DEV_TO_SUB_IF(dev);
		if (sdata->type != IEEE80211_IF_TYPE_STA)
			return;

		bss = ieee80211_rx_bss_get(dev, ifsta->bssid,
					   local->hw.conf.channel,
					   ifsta->ssid, ifsta->ssid_len);
		if (bss) {
			if (bss->has_erp_value)
				ieee80211_handle_erp_ie(dev, bss->erp_value);
			ieee80211_rx_bss_put(dev, bss);
		}

		netif_carrier_on(dev);
		ifsta->flags |= IEEE80211_STA_PREV_BSSID_SET;
		memcpy(ifsta->prev_bssid, sdata->u.sta.bssid, ETH_ALEN);
		memcpy(wrqu.ap_addr.sa_data, sdata->u.sta.bssid, ETH_ALEN);
		ieee80211_sta_send_associnfo(dev, ifsta);
	} else {
		ifsta->flags &= ~IEEE80211_STA_ASSOCIATED;

		netif_carrier_off(dev);
		ieee80211_reset_erp_info(dev);
		memset(wrqu.ap_addr.sa_data, 0, ETH_ALEN);
	}
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
	ifsta->last_probe = jiffies;
	ieee80211_led_assoc(local, assoc);
}

static void ieee80211_set_disassoc(struct net_device *dev,
				   struct ieee80211_if_sta *ifsta, int deauth)
{
	if (deauth)
		ifsta->auth_tries = 0;
	ifsta->assoc_tries = 0;
	ieee80211_set_associated(dev, ifsta, 0);
}

static void ieee80211_sta_tx(struct net_device *dev, struct sk_buff *skb,
			     int encrypt)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_tx_packet_data *pkt_data;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	skb->dev = sdata->local->mdev;
	skb_set_mac_header(skb, 0);
	skb_set_network_header(skb, 0);
	skb_set_transport_header(skb, 0);

	pkt_data = (struct ieee80211_tx_packet_data *) skb->cb;
	memset(pkt_data, 0, sizeof(struct ieee80211_tx_packet_data));
	pkt_data->ifindex = sdata->dev->ifindex;
	if (!encrypt)
		pkt_data->flags |= IEEE80211_TXPD_DO_NOT_ENCRYPT;

	dev_queue_xmit(skb);
}


static void ieee80211_send_auth(struct net_device *dev,
				struct ieee80211_if_sta *ifsta,
				int transaction, u8 *extra, size_t extra_len,
				int encrypt)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    sizeof(*mgmt) + 6 + extra_len);
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for auth "
		       "frame\n", dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24 + 6);
	memset(mgmt, 0, 24 + 6);
	mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					   IEEE80211_STYPE_AUTH);
	if (encrypt)
		mgmt->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	memcpy(mgmt->da, ifsta->bssid, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);
	mgmt->u.auth.auth_alg = cpu_to_le16(ifsta->auth_alg);
	mgmt->u.auth.auth_transaction = cpu_to_le16(transaction);
	ifsta->auth_transaction = transaction + 1;
	mgmt->u.auth.status_code = cpu_to_le16(0);
	if (extra)
		memcpy(skb_put(skb, extra_len), extra, extra_len);

	ieee80211_sta_tx(dev, skb, encrypt);
}


static void ieee80211_authenticate(struct net_device *dev,
				   struct ieee80211_if_sta *ifsta)
{
	DECLARE_MAC_BUF(mac);

	ifsta->auth_tries++;
	if (ifsta->auth_tries > IEEE80211_AUTH_MAX_TRIES) {
		printk(KERN_DEBUG "%s: authentication with AP %s"
		       " timed out\n",
		       dev->name, print_mac(mac, ifsta->bssid));
		ifsta->state = IEEE80211_DISABLED;
		return;
	}

	ifsta->state = IEEE80211_AUTHENTICATE;
	printk(KERN_DEBUG "%s: authenticate with AP %s\n",
	       dev->name, print_mac(mac, ifsta->bssid));

	ieee80211_send_auth(dev, ifsta, 1, NULL, 0, 0);

	mod_timer(&ifsta->timer, jiffies + IEEE80211_AUTH_TIMEOUT);
}


static void ieee80211_send_assoc(struct net_device *dev,
				 struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hw_mode *mode;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u8 *pos, *ies;
	int i, len;
	u16 capab;
	struct ieee80211_sta_bss *bss;
	int wmm = 0;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    sizeof(*mgmt) + 200 + ifsta->extra_ie_len +
			    ifsta->ssid_len);
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for assoc "
		       "frame\n", dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	mode = local->oper_hw_mode;
	capab = ifsta->capab;
	if (mode->mode == MODE_IEEE80211G) {
		capab |= WLAN_CAPABILITY_SHORT_SLOT_TIME |
			WLAN_CAPABILITY_SHORT_PREAMBLE;
	}
	bss = ieee80211_rx_bss_get(dev, ifsta->bssid, local->hw.conf.channel,
				   ifsta->ssid, ifsta->ssid_len);
	if (bss) {
		if (bss->capability & WLAN_CAPABILITY_PRIVACY)
			capab |= WLAN_CAPABILITY_PRIVACY;
		if (bss->wmm_ie) {
			wmm = 1;
		}
		ieee80211_rx_bss_put(dev, bss);
	}

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, ifsta->bssid, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);

	if (ifsta->flags & IEEE80211_STA_PREV_BSSID_SET) {
		skb_put(skb, 10);
		mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
						   IEEE80211_STYPE_REASSOC_REQ);
		mgmt->u.reassoc_req.capab_info = cpu_to_le16(capab);
		mgmt->u.reassoc_req.listen_interval = cpu_to_le16(1);
		memcpy(mgmt->u.reassoc_req.current_ap, ifsta->prev_bssid,
		       ETH_ALEN);
	} else {
		skb_put(skb, 4);
		mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
						   IEEE80211_STYPE_ASSOC_REQ);
		mgmt->u.assoc_req.capab_info = cpu_to_le16(capab);
		mgmt->u.assoc_req.listen_interval = cpu_to_le16(1);
	}

	/* SSID */
	ies = pos = skb_put(skb, 2 + ifsta->ssid_len);
	*pos++ = WLAN_EID_SSID;
	*pos++ = ifsta->ssid_len;
	memcpy(pos, ifsta->ssid, ifsta->ssid_len);

	len = mode->num_rates;
	if (len > 8)
		len = 8;
	pos = skb_put(skb, len + 2);
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = len;
	for (i = 0; i < len; i++) {
		int rate = mode->rates[i].rate;
		*pos++ = (u8) (rate / 5);
	}

	if (mode->num_rates > len) {
		pos = skb_put(skb, mode->num_rates - len + 2);
		*pos++ = WLAN_EID_EXT_SUPP_RATES;
		*pos++ = mode->num_rates - len;
		for (i = len; i < mode->num_rates; i++) {
			int rate = mode->rates[i].rate;
			*pos++ = (u8) (rate / 5);
		}
	}

	if (ifsta->extra_ie) {
		pos = skb_put(skb, ifsta->extra_ie_len);
		memcpy(pos, ifsta->extra_ie, ifsta->extra_ie_len);
	}

	if (wmm && (ifsta->flags & IEEE80211_STA_WMM_ENABLED)) {
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

	kfree(ifsta->assocreq_ies);
	ifsta->assocreq_ies_len = (skb->data + skb->len) - ies;
	ifsta->assocreq_ies = kmalloc(ifsta->assocreq_ies_len, GFP_KERNEL);
	if (ifsta->assocreq_ies)
		memcpy(ifsta->assocreq_ies, ies, ifsta->assocreq_ies_len);

	ieee80211_sta_tx(dev, skb, 0);
}


static void ieee80211_send_deauth(struct net_device *dev,
				  struct ieee80211_if_sta *ifsta, u16 reason)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*mgmt));
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for deauth "
		       "frame\n", dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, ifsta->bssid, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);
	mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					   IEEE80211_STYPE_DEAUTH);
	skb_put(skb, 2);
	mgmt->u.deauth.reason_code = cpu_to_le16(reason);

	ieee80211_sta_tx(dev, skb, 0);
}


static void ieee80211_send_disassoc(struct net_device *dev,
				    struct ieee80211_if_sta *ifsta, u16 reason)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*mgmt));
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for disassoc "
		       "frame\n", dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, ifsta->bssid, ETH_ALEN);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);
	mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					   IEEE80211_STYPE_DISASSOC);
	skb_put(skb, 2);
	mgmt->u.disassoc.reason_code = cpu_to_le16(reason);

	ieee80211_sta_tx(dev, skb, 0);
}


static int ieee80211_privacy_mismatch(struct net_device *dev,
				      struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss;
	int bss_privacy;
	int wep_privacy;
	int privacy_invoked;

	if (!ifsta || (ifsta->flags & IEEE80211_STA_MIXED_CELL))
		return 0;

	bss = ieee80211_rx_bss_get(dev, ifsta->bssid, local->hw.conf.channel,
				   ifsta->ssid, ifsta->ssid_len);
	if (!bss)
		return 0;

	bss_privacy = !!(bss->capability & WLAN_CAPABILITY_PRIVACY);
	wep_privacy = !!ieee80211_sta_wep_configured(dev);
	privacy_invoked = !!(ifsta->flags & IEEE80211_STA_PRIVACY_INVOKED);

	ieee80211_rx_bss_put(dev, bss);

	if ((bss_privacy == wep_privacy) || (bss_privacy == privacy_invoked))
		return 0;

	return 1;
}


static void ieee80211_associate(struct net_device *dev,
				struct ieee80211_if_sta *ifsta)
{
	DECLARE_MAC_BUF(mac);

	ifsta->assoc_tries++;
	if (ifsta->assoc_tries > IEEE80211_ASSOC_MAX_TRIES) {
		printk(KERN_DEBUG "%s: association with AP %s"
		       " timed out\n",
		       dev->name, print_mac(mac, ifsta->bssid));
		ifsta->state = IEEE80211_DISABLED;
		return;
	}

	ifsta->state = IEEE80211_ASSOCIATE;
	printk(KERN_DEBUG "%s: associate with AP %s\n",
	       dev->name, print_mac(mac, ifsta->bssid));
	if (ieee80211_privacy_mismatch(dev, ifsta)) {
		printk(KERN_DEBUG "%s: mismatch in privacy configuration and "
		       "mixed-cell disabled - abort association\n", dev->name);
		ifsta->state = IEEE80211_DISABLED;
		return;
	}

	ieee80211_send_assoc(dev, ifsta);

	mod_timer(&ifsta->timer, jiffies + IEEE80211_ASSOC_TIMEOUT);
}


static void ieee80211_associated(struct net_device *dev,
				 struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	int disassoc;
	DECLARE_MAC_BUF(mac);

	/* TODO: start monitoring current AP signal quality and number of
	 * missed beacons. Scan other channels every now and then and search
	 * for better APs. */
	/* TODO: remove expired BSSes */

	ifsta->state = IEEE80211_ASSOCIATED;

	sta = sta_info_get(local, ifsta->bssid);
	if (!sta) {
		printk(KERN_DEBUG "%s: No STA entry for own AP %s\n",
		       dev->name, print_mac(mac, ifsta->bssid));
		disassoc = 1;
	} else {
		disassoc = 0;
		if (time_after(jiffies,
			       sta->last_rx + IEEE80211_MONITORING_INTERVAL)) {
			if (ifsta->flags & IEEE80211_STA_PROBEREQ_POLL) {
				printk(KERN_DEBUG "%s: No ProbeResp from "
				       "current AP %s - assume out of "
				       "range\n",
				       dev->name, print_mac(mac, ifsta->bssid));
				disassoc = 1;
				sta_info_free(sta);
			} else
				ieee80211_send_probe_req(dev, ifsta->bssid,
							 local->scan_ssid,
							 local->scan_ssid_len);
			ifsta->flags ^= IEEE80211_STA_PROBEREQ_POLL;
		} else {
			ifsta->flags &= ~IEEE80211_STA_PROBEREQ_POLL;
			if (time_after(jiffies, ifsta->last_probe +
				       IEEE80211_PROBE_INTERVAL)) {
				ifsta->last_probe = jiffies;
				ieee80211_send_probe_req(dev, ifsta->bssid,
							 ifsta->ssid,
							 ifsta->ssid_len);
			}
		}
		sta_info_put(sta);
	}
	if (disassoc) {
		ifsta->state = IEEE80211_DISABLED;
		ieee80211_set_associated(dev, ifsta, 0);
	} else {
		mod_timer(&ifsta->timer, jiffies +
				      IEEE80211_MONITORING_INTERVAL);
	}
}


static void ieee80211_send_probe_req(struct net_device *dev, u8 *dst,
				     u8 *ssid, size_t ssid_len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hw_mode *mode;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	u8 *pos, *supp_rates, *esupp_rates = NULL;
	int i;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*mgmt) + 200);
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for probe "
		       "request\n", dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24);
	memset(mgmt, 0, 24);
	mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					   IEEE80211_STYPE_PROBE_REQ);
	memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
	if (dst) {
		memcpy(mgmt->da, dst, ETH_ALEN);
		memcpy(mgmt->bssid, dst, ETH_ALEN);
	} else {
		memset(mgmt->da, 0xff, ETH_ALEN);
		memset(mgmt->bssid, 0xff, ETH_ALEN);
	}
	pos = skb_put(skb, 2 + ssid_len);
	*pos++ = WLAN_EID_SSID;
	*pos++ = ssid_len;
	memcpy(pos, ssid, ssid_len);

	supp_rates = skb_put(skb, 2);
	supp_rates[0] = WLAN_EID_SUPP_RATES;
	supp_rates[1] = 0;
	mode = local->oper_hw_mode;
	for (i = 0; i < mode->num_rates; i++) {
		struct ieee80211_rate *rate = &mode->rates[i];
		if (!(rate->flags & IEEE80211_RATE_SUPPORTED))
			continue;
		if (esupp_rates) {
			pos = skb_put(skb, 1);
			esupp_rates[1]++;
		} else if (supp_rates[1] == 8) {
			esupp_rates = skb_put(skb, 3);
			esupp_rates[0] = WLAN_EID_EXT_SUPP_RATES;
			esupp_rates[1] = 1;
			pos = &esupp_rates[2];
		} else {
			pos = skb_put(skb, 1);
			supp_rates[1]++;
		}
		*pos = rate->rate / 5;
	}

	ieee80211_sta_tx(dev, skb, 0);
}


static int ieee80211_sta_wep_configured(struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (!sdata || !sdata->default_key ||
	    sdata->default_key->conf.alg != ALG_WEP)
		return 0;
	return 1;
}


static void ieee80211_auth_completed(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta)
{
	printk(KERN_DEBUG "%s: authenticated\n", dev->name);
	ifsta->flags |= IEEE80211_STA_AUTHENTICATED;
	ieee80211_associate(dev, ifsta);
}


static void ieee80211_auth_challenge(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta,
				     struct ieee80211_mgmt *mgmt,
				     size_t len)
{
	u8 *pos;
	struct ieee802_11_elems elems;

	printk(KERN_DEBUG "%s: replying to auth challenge\n", dev->name);
	pos = mgmt->u.auth.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), &elems);
	if (!elems.challenge) {
		printk(KERN_DEBUG "%s: no challenge IE in shared key auth "
		       "frame\n", dev->name);
		return;
	}
	ieee80211_send_auth(dev, ifsta, 3, elems.challenge - 2,
			    elems.challenge_len + 2, 1);
}


static void ieee80211_rx_mgmt_auth(struct net_device *dev,
				   struct ieee80211_if_sta *ifsta,
				   struct ieee80211_mgmt *mgmt,
				   size_t len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	u16 auth_alg, auth_transaction, status_code;
	DECLARE_MAC_BUF(mac);

	if (ifsta->state != IEEE80211_AUTHENTICATE &&
	    sdata->type != IEEE80211_IF_TYPE_IBSS) {
		printk(KERN_DEBUG "%s: authentication frame received from "
		       "%s, but not in authenticate state - ignored\n",
		       dev->name, print_mac(mac, mgmt->sa));
		return;
	}

	if (len < 24 + 6) {
		printk(KERN_DEBUG "%s: too short (%zd) authentication frame "
		       "received from %s - ignored\n",
		       dev->name, len, print_mac(mac, mgmt->sa));
		return;
	}

	if (sdata->type != IEEE80211_IF_TYPE_IBSS &&
	    memcmp(ifsta->bssid, mgmt->sa, ETH_ALEN) != 0) {
		printk(KERN_DEBUG "%s: authentication frame received from "
		       "unknown AP (SA=%s BSSID=%s) - "
		       "ignored\n", dev->name, print_mac(mac, mgmt->sa),
		       print_mac(mac, mgmt->bssid));
		return;
	}

	if (sdata->type != IEEE80211_IF_TYPE_IBSS &&
	    memcmp(ifsta->bssid, mgmt->bssid, ETH_ALEN) != 0) {
		printk(KERN_DEBUG "%s: authentication frame received from "
		       "unknown BSSID (SA=%s BSSID=%s) - "
		       "ignored\n", dev->name, print_mac(mac, mgmt->sa),
		       print_mac(mac, mgmt->bssid));
		return;
	}

	auth_alg = le16_to_cpu(mgmt->u.auth.auth_alg);
	auth_transaction = le16_to_cpu(mgmt->u.auth.auth_transaction);
	status_code = le16_to_cpu(mgmt->u.auth.status_code);

	printk(KERN_DEBUG "%s: RX authentication from %s (alg=%d "
	       "transaction=%d status=%d)\n",
	       dev->name, print_mac(mac, mgmt->sa), auth_alg,
	       auth_transaction, status_code);

	if (sdata->type == IEEE80211_IF_TYPE_IBSS) {
		/* IEEE 802.11 standard does not require authentication in IBSS
		 * networks and most implementations do not seem to use it.
		 * However, try to reply to authentication attempts if someone
		 * has actually implemented this.
		 * TODO: Could implement shared key authentication. */
		if (auth_alg != WLAN_AUTH_OPEN || auth_transaction != 1) {
			printk(KERN_DEBUG "%s: unexpected IBSS authentication "
			       "frame (alg=%d transaction=%d)\n",
			       dev->name, auth_alg, auth_transaction);
			return;
		}
		ieee80211_send_auth(dev, ifsta, 2, NULL, 0, 0);
	}

	if (auth_alg != ifsta->auth_alg ||
	    auth_transaction != ifsta->auth_transaction) {
		printk(KERN_DEBUG "%s: unexpected authentication frame "
		       "(alg=%d transaction=%d)\n",
		       dev->name, auth_alg, auth_transaction);
		return;
	}

	if (status_code != WLAN_STATUS_SUCCESS) {
		printk(KERN_DEBUG "%s: AP denied authentication (auth_alg=%d "
		       "code=%d)\n", dev->name, ifsta->auth_alg, status_code);
		if (status_code == WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG) {
			u8 algs[3];
			const int num_algs = ARRAY_SIZE(algs);
			int i, pos;
			algs[0] = algs[1] = algs[2] = 0xff;
			if (ifsta->auth_algs & IEEE80211_AUTH_ALG_OPEN)
				algs[0] = WLAN_AUTH_OPEN;
			if (ifsta->auth_algs & IEEE80211_AUTH_ALG_SHARED_KEY)
				algs[1] = WLAN_AUTH_SHARED_KEY;
			if (ifsta->auth_algs & IEEE80211_AUTH_ALG_LEAP)
				algs[2] = WLAN_AUTH_LEAP;
			if (ifsta->auth_alg == WLAN_AUTH_OPEN)
				pos = 0;
			else if (ifsta->auth_alg == WLAN_AUTH_SHARED_KEY)
				pos = 1;
			else
				pos = 2;
			for (i = 0; i < num_algs; i++) {
				pos++;
				if (pos >= num_algs)
					pos = 0;
				if (algs[pos] == ifsta->auth_alg ||
				    algs[pos] == 0xff)
					continue;
				if (algs[pos] == WLAN_AUTH_SHARED_KEY &&
				    !ieee80211_sta_wep_configured(dev))
					continue;
				ifsta->auth_alg = algs[pos];
				printk(KERN_DEBUG "%s: set auth_alg=%d for "
				       "next try\n",
				       dev->name, ifsta->auth_alg);
				break;
			}
		}
		return;
	}

	switch (ifsta->auth_alg) {
	case WLAN_AUTH_OPEN:
	case WLAN_AUTH_LEAP:
		ieee80211_auth_completed(dev, ifsta);
		break;
	case WLAN_AUTH_SHARED_KEY:
		if (ifsta->auth_transaction == 4)
			ieee80211_auth_completed(dev, ifsta);
		else
			ieee80211_auth_challenge(dev, ifsta, mgmt, len);
		break;
	}
}


static void ieee80211_rx_mgmt_deauth(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta,
				     struct ieee80211_mgmt *mgmt,
				     size_t len)
{
	u16 reason_code;
	DECLARE_MAC_BUF(mac);

	if (len < 24 + 2) {
		printk(KERN_DEBUG "%s: too short (%zd) deauthentication frame "
		       "received from %s - ignored\n",
		       dev->name, len, print_mac(mac, mgmt->sa));
		return;
	}

	if (memcmp(ifsta->bssid, mgmt->sa, ETH_ALEN) != 0) {
		printk(KERN_DEBUG "%s: deauthentication frame received from "
		       "unknown AP (SA=%s BSSID=%s) - "
		       "ignored\n", dev->name, print_mac(mac, mgmt->sa),
		       print_mac(mac, mgmt->bssid));
		return;
	}

	reason_code = le16_to_cpu(mgmt->u.deauth.reason_code);

	printk(KERN_DEBUG "%s: RX deauthentication from %s"
	       " (reason=%d)\n",
	       dev->name, print_mac(mac, mgmt->sa), reason_code);

	if (ifsta->flags & IEEE80211_STA_AUTHENTICATED) {
		printk(KERN_DEBUG "%s: deauthenticated\n", dev->name);
	}

	if (ifsta->state == IEEE80211_AUTHENTICATE ||
	    ifsta->state == IEEE80211_ASSOCIATE ||
	    ifsta->state == IEEE80211_ASSOCIATED) {
		ifsta->state = IEEE80211_AUTHENTICATE;
		mod_timer(&ifsta->timer, jiffies +
				      IEEE80211_RETRY_AUTH_INTERVAL);
	}

	ieee80211_set_disassoc(dev, ifsta, 1);
	ifsta->flags &= ~IEEE80211_STA_AUTHENTICATED;
}


static void ieee80211_rx_mgmt_disassoc(struct net_device *dev,
				       struct ieee80211_if_sta *ifsta,
				       struct ieee80211_mgmt *mgmt,
				       size_t len)
{
	u16 reason_code;
	DECLARE_MAC_BUF(mac);

	if (len < 24 + 2) {
		printk(KERN_DEBUG "%s: too short (%zd) disassociation frame "
		       "received from %s - ignored\n",
		       dev->name, len, print_mac(mac, mgmt->sa));
		return;
	}

	if (memcmp(ifsta->bssid, mgmt->sa, ETH_ALEN) != 0) {
		printk(KERN_DEBUG "%s: disassociation frame received from "
		       "unknown AP (SA=%s BSSID=%s) - "
		       "ignored\n", dev->name, print_mac(mac, mgmt->sa),
		       print_mac(mac, mgmt->bssid));
		return;
	}

	reason_code = le16_to_cpu(mgmt->u.disassoc.reason_code);

	printk(KERN_DEBUG "%s: RX disassociation from %s"
	       " (reason=%d)\n",
	       dev->name, print_mac(mac, mgmt->sa), reason_code);

	if (ifsta->flags & IEEE80211_STA_ASSOCIATED)
		printk(KERN_DEBUG "%s: disassociated\n", dev->name);

	if (ifsta->state == IEEE80211_ASSOCIATED) {
		ifsta->state = IEEE80211_ASSOCIATE;
		mod_timer(&ifsta->timer, jiffies +
				      IEEE80211_RETRY_AUTH_INTERVAL);
	}

	ieee80211_set_disassoc(dev, ifsta, 0);
}


static void ieee80211_rx_mgmt_assoc_resp(struct net_device *dev,
					 struct ieee80211_if_sta *ifsta,
					 struct ieee80211_mgmt *mgmt,
					 size_t len,
					 int reassoc)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hw_mode *mode;
	struct sta_info *sta;
	u32 rates;
	u16 capab_info, status_code, aid;
	struct ieee802_11_elems elems;
	u8 *pos;
	int i, j;
	DECLARE_MAC_BUF(mac);

	/* AssocResp and ReassocResp have identical structure, so process both
	 * of them in this function. */

	if (ifsta->state != IEEE80211_ASSOCIATE) {
		printk(KERN_DEBUG "%s: association frame received from "
		       "%s, but not in associate state - ignored\n",
		       dev->name, print_mac(mac, mgmt->sa));
		return;
	}

	if (len < 24 + 6) {
		printk(KERN_DEBUG "%s: too short (%zd) association frame "
		       "received from %s - ignored\n",
		       dev->name, len, print_mac(mac, mgmt->sa));
		return;
	}

	if (memcmp(ifsta->bssid, mgmt->sa, ETH_ALEN) != 0) {
		printk(KERN_DEBUG "%s: association frame received from "
		       "unknown AP (SA=%s BSSID=%s) - "
		       "ignored\n", dev->name, print_mac(mac, mgmt->sa),
		       print_mac(mac, mgmt->bssid));
		return;
	}

	capab_info = le16_to_cpu(mgmt->u.assoc_resp.capab_info);
	status_code = le16_to_cpu(mgmt->u.assoc_resp.status_code);
	aid = le16_to_cpu(mgmt->u.assoc_resp.aid);

	printk(KERN_DEBUG "%s: RX %sssocResp from %s (capab=0x%x "
	       "status=%d aid=%d)\n",
	       dev->name, reassoc ? "Rea" : "A", print_mac(mac, mgmt->sa),
	       capab_info, status_code, (u16)(aid & ~(BIT(15) | BIT(14))));

	if (status_code != WLAN_STATUS_SUCCESS) {
		printk(KERN_DEBUG "%s: AP denied association (code=%d)\n",
		       dev->name, status_code);
		/* if this was a reassociation, ensure we try a "full"
		 * association next time. This works around some broken APs
		 * which do not correctly reject reassociation requests. */
		ifsta->flags &= ~IEEE80211_STA_PREV_BSSID_SET;
		return;
	}

	if ((aid & (BIT(15) | BIT(14))) != (BIT(15) | BIT(14)))
		printk(KERN_DEBUG "%s: invalid aid value %d; bits 15:14 not "
		       "set\n", dev->name, aid);
	aid &= ~(BIT(15) | BIT(14));

	pos = mgmt->u.assoc_resp.variable;
	ieee802_11_parse_elems(pos, len - (pos - (u8 *) mgmt), &elems);

	if (!elems.supp_rates) {
		printk(KERN_DEBUG "%s: no SuppRates element in AssocResp\n",
		       dev->name);
		return;
	}

	/* it probably doesn't, but if the frame includes an ERP value then
	 * update our stored copy */
	if (elems.erp_info && elems.erp_info_len >= 1) {
		struct ieee80211_sta_bss *bss
			= ieee80211_rx_bss_get(dev, ifsta->bssid,
					       local->hw.conf.channel,
					       ifsta->ssid, ifsta->ssid_len);
		if (bss) {
			bss->erp_value = elems.erp_info[0];
			bss->has_erp_value = 1;
			ieee80211_rx_bss_put(dev, bss);
		}
	}

	printk(KERN_DEBUG "%s: associated\n", dev->name);
	ifsta->aid = aid;
	ifsta->ap_capab = capab_info;

	kfree(ifsta->assocresp_ies);
	ifsta->assocresp_ies_len = len - (pos - (u8 *) mgmt);
	ifsta->assocresp_ies = kmalloc(ifsta->assocresp_ies_len, GFP_KERNEL);
	if (ifsta->assocresp_ies)
		memcpy(ifsta->assocresp_ies, pos, ifsta->assocresp_ies_len);

	ieee80211_set_associated(dev, ifsta, 1);

	/* Add STA entry for the AP */
	sta = sta_info_get(local, ifsta->bssid);
	if (!sta) {
		struct ieee80211_sta_bss *bss;
		sta = sta_info_add(local, dev, ifsta->bssid, GFP_KERNEL);
		if (!sta) {
			printk(KERN_DEBUG "%s: failed to add STA entry for the"
			       " AP\n", dev->name);
			return;
		}
		bss = ieee80211_rx_bss_get(dev, ifsta->bssid,
					   local->hw.conf.channel,
					   ifsta->ssid, ifsta->ssid_len);
		if (bss) {
			sta->last_rssi = bss->rssi;
			sta->last_signal = bss->signal;
			sta->last_noise = bss->noise;
			ieee80211_rx_bss_put(dev, bss);
		}
	}

	sta->dev = dev;
	sta->flags |= WLAN_STA_AUTH | WLAN_STA_ASSOC | WLAN_STA_ASSOC_AP;

	rates = 0;
	mode = local->oper_hw_mode;
	for (i = 0; i < elems.supp_rates_len; i++) {
		int rate = (elems.supp_rates[i] & 0x7f) * 5;
		for (j = 0; j < mode->num_rates; j++)
			if (mode->rates[j].rate == rate)
				rates |= BIT(j);
	}
	for (i = 0; i < elems.ext_supp_rates_len; i++) {
		int rate = (elems.ext_supp_rates[i] & 0x7f) * 5;
		for (j = 0; j < mode->num_rates; j++)
			if (mode->rates[j].rate == rate)
				rates |= BIT(j);
	}
	sta->supp_rates = rates;

	rate_control_rate_init(sta, local);

	if (elems.wmm_param && (ifsta->flags & IEEE80211_STA_WMM_ENABLED)) {
		sta->flags |= WLAN_STA_WME;
		ieee80211_sta_wmm_params(dev, ifsta, elems.wmm_param,
					 elems.wmm_param_len);
	}


	sta_info_put(sta);

	ieee80211_associated(dev, ifsta);
}


/* Caller must hold local->sta_bss_lock */
static void __ieee80211_rx_bss_hash_add(struct net_device *dev,
					struct ieee80211_sta_bss *bss)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	bss->hnext = local->sta_bss_hash[STA_HASH(bss->bssid)];
	local->sta_bss_hash[STA_HASH(bss->bssid)] = bss;
}


/* Caller must hold local->sta_bss_lock */
static void __ieee80211_rx_bss_hash_del(struct net_device *dev,
					struct ieee80211_sta_bss *bss)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *b, *prev = NULL;
	b = local->sta_bss_hash[STA_HASH(bss->bssid)];
	while (b) {
		if (b == bss) {
			if (!prev)
				local->sta_bss_hash[STA_HASH(bss->bssid)] =
					bss->hnext;
			else
				prev->hnext = bss->hnext;
			break;
		}
		prev = b;
		b = b->hnext;
	}
}


static struct ieee80211_sta_bss *
ieee80211_rx_bss_add(struct net_device *dev, u8 *bssid, int channel,
		     u8 *ssid, u8 ssid_len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss;

	bss = kzalloc(sizeof(*bss), GFP_ATOMIC);
	if (!bss)
		return NULL;
	atomic_inc(&bss->users);
	atomic_inc(&bss->users);
	memcpy(bss->bssid, bssid, ETH_ALEN);
	bss->channel = channel;
	if (ssid && ssid_len <= IEEE80211_MAX_SSID_LEN) {
		memcpy(bss->ssid, ssid, ssid_len);
		bss->ssid_len = ssid_len;
	}

	spin_lock_bh(&local->sta_bss_lock);
	/* TODO: order by RSSI? */
	list_add_tail(&bss->list, &local->sta_bss_list);
	__ieee80211_rx_bss_hash_add(dev, bss);
	spin_unlock_bh(&local->sta_bss_lock);
	return bss;
}


static struct ieee80211_sta_bss *
ieee80211_rx_bss_get(struct net_device *dev, u8 *bssid, int channel,
		     u8 *ssid, u8 ssid_len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss;

	spin_lock_bh(&local->sta_bss_lock);
	bss = local->sta_bss_hash[STA_HASH(bssid)];
	while (bss) {
		if (!memcmp(bss->bssid, bssid, ETH_ALEN) &&
		    bss->channel == channel &&
		    bss->ssid_len == ssid_len &&
		    (ssid_len == 0 || !memcmp(bss->ssid, ssid, ssid_len))) {
			atomic_inc(&bss->users);
			break;
		}
		bss = bss->hnext;
	}
	spin_unlock_bh(&local->sta_bss_lock);
	return bss;
}


static void ieee80211_rx_bss_free(struct ieee80211_sta_bss *bss)
{
	kfree(bss->wpa_ie);
	kfree(bss->rsn_ie);
	kfree(bss->wmm_ie);
	kfree(bss);
}


static void ieee80211_rx_bss_put(struct net_device *dev,
				 struct ieee80211_sta_bss *bss)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	if (!atomic_dec_and_test(&bss->users))
		return;

	spin_lock_bh(&local->sta_bss_lock);
	__ieee80211_rx_bss_hash_del(dev, bss);
	list_del(&bss->list);
	spin_unlock_bh(&local->sta_bss_lock);
	ieee80211_rx_bss_free(bss);
}


void ieee80211_rx_bss_list_init(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	spin_lock_init(&local->sta_bss_lock);
	INIT_LIST_HEAD(&local->sta_bss_list);
}


void ieee80211_rx_bss_list_deinit(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss, *tmp;

	list_for_each_entry_safe(bss, tmp, &local->sta_bss_list, list)
		ieee80211_rx_bss_put(dev, bss);
}


static void ieee80211_rx_bss_info(struct net_device *dev,
				  struct ieee80211_mgmt *mgmt,
				  size_t len,
				  struct ieee80211_rx_status *rx_status,
				  int beacon)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee802_11_elems elems;
	size_t baselen;
	int channel, clen;
	struct ieee80211_sta_bss *bss;
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	u64 timestamp;
	DECLARE_MAC_BUF(mac);
	DECLARE_MAC_BUF(mac2);

	if (!beacon && memcmp(mgmt->da, dev->dev_addr, ETH_ALEN))
		return; /* ignore ProbeResp to foreign address */

#if 0
	printk(KERN_DEBUG "%s: RX %s from %s to %s\n",
	       dev->name, beacon ? "Beacon" : "Probe Response",
	       print_mac(mac, mgmt->sa), print_mac(mac2, mgmt->da));
#endif

	baselen = (u8 *) mgmt->u.beacon.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	timestamp = le64_to_cpu(mgmt->u.beacon.timestamp);

	if (sdata->type == IEEE80211_IF_TYPE_IBSS && beacon &&
	    memcmp(mgmt->bssid, sdata->u.sta.bssid, ETH_ALEN) == 0) {
#ifdef CONFIG_MAC80211_IBSS_DEBUG
		static unsigned long last_tsf_debug = 0;
		u64 tsf;
		if (local->ops->get_tsf)
			tsf = local->ops->get_tsf(local_to_hw(local));
		else
			tsf = -1LLU;
		if (time_after(jiffies, last_tsf_debug + 5 * HZ)) {
			printk(KERN_DEBUG "RX beacon SA=%s BSSID="
			       "%s TSF=0x%llx BCN=0x%llx diff=%lld "
			       "@%lu\n",
			       print_mac(mac, mgmt->sa), print_mac(mac2, mgmt->bssid),
			       (unsigned long long)tsf,
			       (unsigned long long)timestamp,
			       (unsigned long long)(tsf - timestamp),
			       jiffies);
			last_tsf_debug = jiffies;
		}
#endif /* CONFIG_MAC80211_IBSS_DEBUG */
	}

	ieee802_11_parse_elems(mgmt->u.beacon.variable, len - baselen, &elems);

	if (sdata->type == IEEE80211_IF_TYPE_IBSS && elems.supp_rates &&
	    memcmp(mgmt->bssid, sdata->u.sta.bssid, ETH_ALEN) == 0 &&
	    (sta = sta_info_get(local, mgmt->sa))) {
		struct ieee80211_hw_mode *mode;
		struct ieee80211_rate *rates;
		size_t num_rates;
		u32 supp_rates, prev_rates;
		int i, j;

		mode = local->sta_scanning ?
		       local->scan_hw_mode : local->oper_hw_mode;
		rates = mode->rates;
		num_rates = mode->num_rates;

		supp_rates = 0;
		for (i = 0; i < elems.supp_rates_len +
			     elems.ext_supp_rates_len; i++) {
			u8 rate = 0;
			int own_rate;
			if (i < elems.supp_rates_len)
				rate = elems.supp_rates[i];
			else if (elems.ext_supp_rates)
				rate = elems.ext_supp_rates
					[i - elems.supp_rates_len];
			own_rate = 5 * (rate & 0x7f);
			for (j = 0; j < num_rates; j++)
				if (rates[j].rate == own_rate)
					supp_rates |= BIT(j);
		}

		prev_rates = sta->supp_rates;
		sta->supp_rates &= supp_rates;
		if (sta->supp_rates == 0) {
			/* No matching rates - this should not really happen.
			 * Make sure that at least one rate is marked
			 * supported to avoid issues with TX rate ctrl. */
			sta->supp_rates = sdata->u.sta.supp_rates_bits;
		}
		if (sta->supp_rates != prev_rates) {
			printk(KERN_DEBUG "%s: updated supp_rates set for "
			       "%s based on beacon info (0x%x & 0x%x -> "
			       "0x%x)\n",
			       dev->name, print_mac(mac, sta->addr), prev_rates,
			       supp_rates, sta->supp_rates);
		}
		sta_info_put(sta);
	}

	if (!elems.ssid)
		return;

	if (elems.ds_params && elems.ds_params_len == 1)
		channel = elems.ds_params[0];
	else
		channel = rx_status->channel;

	bss = ieee80211_rx_bss_get(dev, mgmt->bssid, channel,
				   elems.ssid, elems.ssid_len);
	if (!bss) {
		bss = ieee80211_rx_bss_add(dev, mgmt->bssid, channel,
					   elems.ssid, elems.ssid_len);
		if (!bss)
			return;
	} else {
#if 0
		/* TODO: order by RSSI? */
		spin_lock_bh(&local->sta_bss_lock);
		list_move_tail(&bss->list, &local->sta_bss_list);
		spin_unlock_bh(&local->sta_bss_lock);
#endif
	}

	if (bss->probe_resp && beacon) {
		/* Do not allow beacon to override data from Probe Response. */
		ieee80211_rx_bss_put(dev, bss);
		return;
	}

	/* save the ERP value so that it is available at association time */
	if (elems.erp_info && elems.erp_info_len >= 1) {
		bss->erp_value = elems.erp_info[0];
		bss->has_erp_value = 1;
	}

	bss->beacon_int = le16_to_cpu(mgmt->u.beacon.beacon_int);
	bss->capability = le16_to_cpu(mgmt->u.beacon.capab_info);

	bss->supp_rates_len = 0;
	if (elems.supp_rates) {
		clen = IEEE80211_MAX_SUPP_RATES - bss->supp_rates_len;
		if (clen > elems.supp_rates_len)
			clen = elems.supp_rates_len;
		memcpy(&bss->supp_rates[bss->supp_rates_len], elems.supp_rates,
		       clen);
		bss->supp_rates_len += clen;
	}
	if (elems.ext_supp_rates) {
		clen = IEEE80211_MAX_SUPP_RATES - bss->supp_rates_len;
		if (clen > elems.ext_supp_rates_len)
			clen = elems.ext_supp_rates_len;
		memcpy(&bss->supp_rates[bss->supp_rates_len],
		       elems.ext_supp_rates, clen);
		bss->supp_rates_len += clen;
	}

	if (elems.wpa &&
	    (!bss->wpa_ie || bss->wpa_ie_len != elems.wpa_len ||
	     memcmp(bss->wpa_ie, elems.wpa, elems.wpa_len))) {
		kfree(bss->wpa_ie);
		bss->wpa_ie = kmalloc(elems.wpa_len + 2, GFP_ATOMIC);
		if (bss->wpa_ie) {
			memcpy(bss->wpa_ie, elems.wpa - 2, elems.wpa_len + 2);
			bss->wpa_ie_len = elems.wpa_len + 2;
		} else
			bss->wpa_ie_len = 0;
	} else if (!elems.wpa && bss->wpa_ie) {
		kfree(bss->wpa_ie);
		bss->wpa_ie = NULL;
		bss->wpa_ie_len = 0;
	}

	if (elems.rsn &&
	    (!bss->rsn_ie || bss->rsn_ie_len != elems.rsn_len ||
	     memcmp(bss->rsn_ie, elems.rsn, elems.rsn_len))) {
		kfree(bss->rsn_ie);
		bss->rsn_ie = kmalloc(elems.rsn_len + 2, GFP_ATOMIC);
		if (bss->rsn_ie) {
			memcpy(bss->rsn_ie, elems.rsn - 2, elems.rsn_len + 2);
			bss->rsn_ie_len = elems.rsn_len + 2;
		} else
			bss->rsn_ie_len = 0;
	} else if (!elems.rsn && bss->rsn_ie) {
		kfree(bss->rsn_ie);
		bss->rsn_ie = NULL;
		bss->rsn_ie_len = 0;
	}

	if (elems.wmm_param &&
	    (!bss->wmm_ie || bss->wmm_ie_len != elems.wmm_param_len ||
	     memcmp(bss->wmm_ie, elems.wmm_param, elems.wmm_param_len))) {
		kfree(bss->wmm_ie);
		bss->wmm_ie = kmalloc(elems.wmm_param_len + 2, GFP_ATOMIC);
		if (bss->wmm_ie) {
			memcpy(bss->wmm_ie, elems.wmm_param - 2,
			       elems.wmm_param_len + 2);
			bss->wmm_ie_len = elems.wmm_param_len + 2;
		} else
			bss->wmm_ie_len = 0;
	} else if (!elems.wmm_param && bss->wmm_ie) {
		kfree(bss->wmm_ie);
		bss->wmm_ie = NULL;
		bss->wmm_ie_len = 0;
	}


	bss->hw_mode = rx_status->phymode;
	bss->freq = rx_status->freq;
	if (channel != rx_status->channel &&
	    (bss->hw_mode == MODE_IEEE80211G ||
	     bss->hw_mode == MODE_IEEE80211B) &&
	    channel >= 1 && channel <= 14) {
		static const int freq_list[] = {
			2412, 2417, 2422, 2427, 2432, 2437, 2442,
			2447, 2452, 2457, 2462, 2467, 2472, 2484
		};
		/* IEEE 802.11g/b mode can receive packets from neighboring
		 * channels, so map the channel into frequency. */
		bss->freq = freq_list[channel - 1];
	}
	bss->timestamp = timestamp;
	bss->last_update = jiffies;
	bss->rssi = rx_status->ssi;
	bss->signal = rx_status->signal;
	bss->noise = rx_status->noise;
	if (!beacon)
		bss->probe_resp++;
	ieee80211_rx_bss_put(dev, bss);
}


static void ieee80211_rx_mgmt_probe_resp(struct net_device *dev,
					 struct ieee80211_mgmt *mgmt,
					 size_t len,
					 struct ieee80211_rx_status *rx_status)
{
	ieee80211_rx_bss_info(dev, mgmt, len, rx_status, 0);
}


static void ieee80211_rx_mgmt_beacon(struct net_device *dev,
				     struct ieee80211_mgmt *mgmt,
				     size_t len,
				     struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_sta *ifsta;
	size_t baselen;
	struct ieee802_11_elems elems;

	ieee80211_rx_bss_info(dev, mgmt, len, rx_status, 1);

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->type != IEEE80211_IF_TYPE_STA)
		return;
	ifsta = &sdata->u.sta;

	if (!(ifsta->flags & IEEE80211_STA_ASSOCIATED) ||
	    memcmp(ifsta->bssid, mgmt->bssid, ETH_ALEN) != 0)
		return;

	/* Process beacon from the current BSS */
	baselen = (u8 *) mgmt->u.beacon.variable - (u8 *) mgmt;
	if (baselen > len)
		return;

	ieee802_11_parse_elems(mgmt->u.beacon.variable, len - baselen, &elems);

	if (elems.erp_info && elems.erp_info_len >= 1)
		ieee80211_handle_erp_ie(dev, elems.erp_info[0]);

	if (elems.wmm_param && (ifsta->flags & IEEE80211_STA_WMM_ENABLED)) {
		ieee80211_sta_wmm_params(dev, ifsta, elems.wmm_param,
					 elems.wmm_param_len);
	}
}


static void ieee80211_rx_mgmt_probe_req(struct net_device *dev,
					struct ieee80211_if_sta *ifsta,
					struct ieee80211_mgmt *mgmt,
					size_t len,
					struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int tx_last_beacon;
	struct sk_buff *skb;
	struct ieee80211_mgmt *resp;
	u8 *pos, *end;
	DECLARE_MAC_BUF(mac);
#ifdef CONFIG_MAC80211_IBSS_DEBUG
	DECLARE_MAC_BUF(mac2);
	DECLARE_MAC_BUF(mac3);
#endif

	if (sdata->type != IEEE80211_IF_TYPE_IBSS ||
	    ifsta->state != IEEE80211_IBSS_JOINED ||
	    len < 24 + 2 || !ifsta->probe_resp)
		return;

	if (local->ops->tx_last_beacon)
		tx_last_beacon = local->ops->tx_last_beacon(local_to_hw(local));
	else
		tx_last_beacon = 1;

#ifdef CONFIG_MAC80211_IBSS_DEBUG
	printk(KERN_DEBUG "%s: RX ProbeReq SA=%s DA=%s BSSID="
	       "%s (tx_last_beacon=%d)\n",
	       dev->name, print_mac(mac, mgmt->sa), print_mac(mac2, mgmt->da),
	       print_mac(mac3, mgmt->bssid), tx_last_beacon);
#endif /* CONFIG_MAC80211_IBSS_DEBUG */

	if (!tx_last_beacon)
		return;

	if (memcmp(mgmt->bssid, ifsta->bssid, ETH_ALEN) != 0 &&
	    memcmp(mgmt->bssid, "\xff\xff\xff\xff\xff\xff", ETH_ALEN) != 0)
		return;

	end = ((u8 *) mgmt) + len;
	pos = mgmt->u.probe_req.variable;
	if (pos[0] != WLAN_EID_SSID ||
	    pos + 2 + pos[1] > end) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: Invalid SSID IE in ProbeReq "
			       "from %s\n",
			       dev->name, print_mac(mac, mgmt->sa));
		}
		return;
	}
	if (pos[1] != 0 &&
	    (pos[1] != ifsta->ssid_len ||
	     memcmp(pos + 2, ifsta->ssid, ifsta->ssid_len) != 0)) {
		/* Ignore ProbeReq for foreign SSID */
		return;
	}

	/* Reply with ProbeResp */
	skb = skb_copy(ifsta->probe_resp, GFP_KERNEL);
	if (!skb)
		return;

	resp = (struct ieee80211_mgmt *) skb->data;
	memcpy(resp->da, mgmt->sa, ETH_ALEN);
#ifdef CONFIG_MAC80211_IBSS_DEBUG
	printk(KERN_DEBUG "%s: Sending ProbeResp to %s\n",
	       dev->name, print_mac(mac, resp->da));
#endif /* CONFIG_MAC80211_IBSS_DEBUG */
	ieee80211_sta_tx(dev, skb, 0);
}


void ieee80211_sta_rx_mgmt(struct net_device *dev, struct sk_buff *skb,
			   struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_sta *ifsta;
	struct ieee80211_mgmt *mgmt;
	u16 fc;

	if (skb->len < 24)
		goto fail;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	ifsta = &sdata->u.sta;

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
		skb_queue_tail(&ifsta->skb_queue, skb);
		queue_work(local->hw.workqueue, &ifsta->work);
		return;
	default:
		printk(KERN_DEBUG "%s: received unknown management frame - "
		       "stype=%d\n", dev->name,
		       (fc & IEEE80211_FCTL_STYPE) >> 4);
		break;
	}

 fail:
	kfree_skb(skb);
}


static void ieee80211_sta_rx_queued_mgmt(struct net_device *dev,
					 struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_sta *ifsta;
	struct ieee80211_mgmt *mgmt;
	u16 fc;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	ifsta = &sdata->u.sta;

	rx_status = (struct ieee80211_rx_status *) skb->cb;
	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	switch (fc & IEEE80211_FCTL_STYPE) {
	case IEEE80211_STYPE_PROBE_REQ:
		ieee80211_rx_mgmt_probe_req(dev, ifsta, mgmt, skb->len,
					    rx_status);
		break;
	case IEEE80211_STYPE_PROBE_RESP:
		ieee80211_rx_mgmt_probe_resp(dev, mgmt, skb->len, rx_status);
		break;
	case IEEE80211_STYPE_BEACON:
		ieee80211_rx_mgmt_beacon(dev, mgmt, skb->len, rx_status);
		break;
	case IEEE80211_STYPE_AUTH:
		ieee80211_rx_mgmt_auth(dev, ifsta, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_ASSOC_RESP:
		ieee80211_rx_mgmt_assoc_resp(dev, ifsta, mgmt, skb->len, 0);
		break;
	case IEEE80211_STYPE_REASSOC_RESP:
		ieee80211_rx_mgmt_assoc_resp(dev, ifsta, mgmt, skb->len, 1);
		break;
	case IEEE80211_STYPE_DEAUTH:
		ieee80211_rx_mgmt_deauth(dev, ifsta, mgmt, skb->len);
		break;
	case IEEE80211_STYPE_DISASSOC:
		ieee80211_rx_mgmt_disassoc(dev, ifsta, mgmt, skb->len);
		break;
	}

	kfree_skb(skb);
}


void ieee80211_sta_rx_scan(struct net_device *dev, struct sk_buff *skb,
			   struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_mgmt *mgmt;
	u16 fc;

	if (skb->len < 24) {
		dev_kfree_skb(skb);
		return;
	}

	mgmt = (struct ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	if ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) {
		if ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PROBE_RESP) {
			ieee80211_rx_mgmt_probe_resp(dev, mgmt,
						     skb->len, rx_status);
		} else if ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_BEACON) {
			ieee80211_rx_mgmt_beacon(dev, mgmt, skb->len,
						 rx_status);
		}
	}

	dev_kfree_skb(skb);
}


static int ieee80211_sta_active_ibss(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	int active = 0;
	struct sta_info *sta;

	read_lock_bh(&local->sta_lock);
	list_for_each_entry(sta, &local->sta_list, list) {
		if (sta->dev == dev &&
		    time_after(sta->last_rx + IEEE80211_IBSS_MERGE_INTERVAL,
			       jiffies)) {
			active++;
			break;
		}
	}
	read_unlock_bh(&local->sta_lock);

	return active;
}


static void ieee80211_sta_expire(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta, *tmp;
	LIST_HEAD(tmp_list);
	DECLARE_MAC_BUF(mac);

	write_lock_bh(&local->sta_lock);
	list_for_each_entry_safe(sta, tmp, &local->sta_list, list)
		if (time_after(jiffies, sta->last_rx +
			       IEEE80211_IBSS_INACTIVITY_LIMIT)) {
			printk(KERN_DEBUG "%s: expiring inactive STA %s\n",
			       dev->name, print_mac(mac, sta->addr));
			__sta_info_get(sta);
			sta_info_remove(sta);
			list_add(&sta->list, &tmp_list);
		}
	write_unlock_bh(&local->sta_lock);

	list_for_each_entry_safe(sta, tmp, &tmp_list, list) {
		sta_info_free(sta);
		sta_info_put(sta);
	}
}


static void ieee80211_sta_merge_ibss(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta)
{
	mod_timer(&ifsta->timer, jiffies + IEEE80211_IBSS_MERGE_INTERVAL);

	ieee80211_sta_expire(dev);
	if (ieee80211_sta_active_ibss(dev))
		return;

	printk(KERN_DEBUG "%s: No active IBSS STAs - trying to scan for other "
	       "IBSS networks with same SSID (merge)\n", dev->name);
	ieee80211_sta_req_scan(dev, ifsta->ssid, ifsta->ssid_len);
}


void ieee80211_sta_timer(unsigned long data)
{
	struct ieee80211_sub_if_data *sdata =
		(struct ieee80211_sub_if_data *) data;
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	struct ieee80211_local *local = wdev_priv(&sdata->wdev);

	set_bit(IEEE80211_STA_REQ_RUN, &ifsta->request);
	queue_work(local->hw.workqueue, &ifsta->work);
}


void ieee80211_sta_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data, u.sta.work);
	struct net_device *dev = sdata->dev;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_if_sta *ifsta;
	struct sk_buff *skb;

	if (!netif_running(dev))
		return;

	if (local->sta_scanning)
		return;

	if (sdata->type != IEEE80211_IF_TYPE_STA &&
	    sdata->type != IEEE80211_IF_TYPE_IBSS) {
		printk(KERN_DEBUG "%s: ieee80211_sta_work: non-STA interface "
		       "(type=%d)\n", dev->name, sdata->type);
		return;
	}
	ifsta = &sdata->u.sta;

	while ((skb = skb_dequeue(&ifsta->skb_queue)))
		ieee80211_sta_rx_queued_mgmt(dev, skb);

	if (ifsta->state != IEEE80211_AUTHENTICATE &&
	    ifsta->state != IEEE80211_ASSOCIATE &&
	    test_and_clear_bit(IEEE80211_STA_REQ_SCAN, &ifsta->request)) {
		if (ifsta->scan_ssid_len)
			ieee80211_sta_start_scan(dev, ifsta->scan_ssid, ifsta->scan_ssid_len);
		else
			ieee80211_sta_start_scan(dev, NULL, 0);
		return;
	}

	if (test_and_clear_bit(IEEE80211_STA_REQ_AUTH, &ifsta->request)) {
		if (ieee80211_sta_config_auth(dev, ifsta))
			return;
		clear_bit(IEEE80211_STA_REQ_RUN, &ifsta->request);
	} else if (!test_and_clear_bit(IEEE80211_STA_REQ_RUN, &ifsta->request))
		return;

	switch (ifsta->state) {
	case IEEE80211_DISABLED:
		break;
	case IEEE80211_AUTHENTICATE:
		ieee80211_authenticate(dev, ifsta);
		break;
	case IEEE80211_ASSOCIATE:
		ieee80211_associate(dev, ifsta);
		break;
	case IEEE80211_ASSOCIATED:
		ieee80211_associated(dev, ifsta);
		break;
	case IEEE80211_IBSS_SEARCH:
		ieee80211_sta_find_ibss(dev, ifsta);
		break;
	case IEEE80211_IBSS_JOINED:
		ieee80211_sta_merge_ibss(dev, ifsta);
		break;
	default:
		printk(KERN_DEBUG "ieee80211_sta_work: Unknown state %d\n",
		       ifsta->state);
		break;
	}

	if (ieee80211_privacy_mismatch(dev, ifsta)) {
		printk(KERN_DEBUG "%s: privacy configuration mismatch and "
		       "mixed-cell disabled - disassociate\n", dev->name);

		ieee80211_send_disassoc(dev, ifsta, WLAN_REASON_UNSPECIFIED);
		ieee80211_set_disassoc(dev, ifsta, 0);
	}
}


static void ieee80211_sta_reset_auth(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (local->ops->reset_tsf) {
		/* Reset own TSF to allow time synchronization work. */
		local->ops->reset_tsf(local_to_hw(local));
	}

	ifsta->wmm_last_param_set = -1; /* allow any WMM update */


	if (ifsta->auth_algs & IEEE80211_AUTH_ALG_OPEN)
		ifsta->auth_alg = WLAN_AUTH_OPEN;
	else if (ifsta->auth_algs & IEEE80211_AUTH_ALG_SHARED_KEY)
		ifsta->auth_alg = WLAN_AUTH_SHARED_KEY;
	else if (ifsta->auth_algs & IEEE80211_AUTH_ALG_LEAP)
		ifsta->auth_alg = WLAN_AUTH_LEAP;
	else
		ifsta->auth_alg = WLAN_AUTH_OPEN;
	printk(KERN_DEBUG "%s: Initial auth_alg=%d\n", dev->name,
	       ifsta->auth_alg);
	ifsta->auth_transaction = -1;
	ifsta->flags &= ~IEEE80211_STA_ASSOCIATED;
	ifsta->auth_tries = ifsta->assoc_tries = 0;
	netif_carrier_off(dev);
}


void ieee80211_sta_req_auth(struct net_device *dev,
			    struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->type != IEEE80211_IF_TYPE_STA)
		return;

	if ((ifsta->flags & (IEEE80211_STA_BSSID_SET |
				IEEE80211_STA_AUTO_BSSID_SEL)) &&
	    (ifsta->flags & (IEEE80211_STA_SSID_SET |
				IEEE80211_STA_AUTO_SSID_SEL))) {
		set_bit(IEEE80211_STA_REQ_AUTH, &ifsta->request);
		queue_work(local->hw.workqueue, &ifsta->work);
	}
}

static int ieee80211_sta_match_ssid(struct ieee80211_if_sta *ifsta,
				    const char *ssid, int ssid_len)
{
	int tmp, hidden_ssid;

	if (ssid_len == ifsta->ssid_len &&
	    !memcmp(ifsta->ssid, ssid, ssid_len))
		return 1;

	if (ifsta->flags & IEEE80211_STA_AUTO_BSSID_SEL)
		return 0;

	hidden_ssid = 1;
	tmp = ssid_len;
	while (tmp--) {
		if (ssid[tmp] != '\0') {
			hidden_ssid = 0;
			break;
		}
	}

	if (hidden_ssid && ifsta->ssid_len == ssid_len)
		return 1;

	if (ssid_len == 1 && ssid[0] == ' ')
		return 1;

	return 0;
}

static int ieee80211_sta_config_auth(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_sta_bss *bss, *selected = NULL;
	int top_rssi = 0, freq;

	if (!(ifsta->flags & (IEEE80211_STA_AUTO_SSID_SEL |
	    IEEE80211_STA_AUTO_BSSID_SEL | IEEE80211_STA_AUTO_CHANNEL_SEL))) {
		ifsta->state = IEEE80211_AUTHENTICATE;
		ieee80211_sta_reset_auth(dev, ifsta);
		return 0;
	}

	spin_lock_bh(&local->sta_bss_lock);
	freq = local->oper_channel->freq;
	list_for_each_entry(bss, &local->sta_bss_list, list) {
		if (!(bss->capability & WLAN_CAPABILITY_ESS))
			continue;

		if (!!(bss->capability & WLAN_CAPABILITY_PRIVACY) ^
		    !!sdata->default_key)
			continue;

		if (!(ifsta->flags & IEEE80211_STA_AUTO_CHANNEL_SEL) &&
		    bss->freq != freq)
			continue;

		if (!(ifsta->flags & IEEE80211_STA_AUTO_BSSID_SEL) &&
		    memcmp(bss->bssid, ifsta->bssid, ETH_ALEN))
			continue;

		if (!(ifsta->flags & IEEE80211_STA_AUTO_SSID_SEL) &&
		    !ieee80211_sta_match_ssid(ifsta, bss->ssid, bss->ssid_len))
			continue;

		if (!selected || top_rssi < bss->rssi) {
			selected = bss;
			top_rssi = bss->rssi;
		}
	}
	if (selected)
		atomic_inc(&selected->users);
	spin_unlock_bh(&local->sta_bss_lock);

	if (selected) {
		ieee80211_set_channel(local, -1, selected->freq);
		if (!(ifsta->flags & IEEE80211_STA_SSID_SET))
			ieee80211_sta_set_ssid(dev, selected->ssid,
					       selected->ssid_len);
		ieee80211_sta_set_bssid(dev, selected->bssid);
		ieee80211_rx_bss_put(dev, selected);
		ifsta->state = IEEE80211_AUTHENTICATE;
		ieee80211_sta_reset_auth(dev, ifsta);
		return 0;
	} else {
		if (ifsta->state != IEEE80211_AUTHENTICATE) {
			if (ifsta->flags & IEEE80211_STA_AUTO_SSID_SEL)
				ieee80211_sta_start_scan(dev, NULL, 0);
			else
				ieee80211_sta_start_scan(dev, ifsta->ssid,
							 ifsta->ssid_len);
			ifsta->state = IEEE80211_AUTHENTICATE;
			set_bit(IEEE80211_STA_REQ_AUTH, &ifsta->request);
		} else
			ifsta->state = IEEE80211_DISABLED;
	}
	return -1;
}

static int ieee80211_sta_join_ibss(struct net_device *dev,
				   struct ieee80211_if_sta *ifsta,
				   struct ieee80211_sta_bss *bss)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	int res, rates, i, j;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_tx_control control;
	struct ieee80211_rate *rate;
	struct ieee80211_hw_mode *mode;
	struct rate_control_extra extra;
	u8 *pos;
	struct ieee80211_sub_if_data *sdata;

	/* Remove possible STA entries from other IBSS networks. */
	sta_info_flush(local, NULL);

	if (local->ops->reset_tsf) {
		/* Reset own TSF to allow time synchronization work. */
		local->ops->reset_tsf(local_to_hw(local));
	}
	memcpy(ifsta->bssid, bss->bssid, ETH_ALEN);
	res = ieee80211_if_config(dev);
	if (res)
		return res;

	local->hw.conf.beacon_int = bss->beacon_int >= 10 ? bss->beacon_int : 10;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	sdata->drop_unencrypted = bss->capability &
		WLAN_CAPABILITY_PRIVACY ? 1 : 0;

	res = ieee80211_set_channel(local, -1, bss->freq);

	if (!(local->oper_channel->flag & IEEE80211_CHAN_W_IBSS)) {
		printk(KERN_DEBUG "%s: IBSS not allowed on channel %d "
		       "(%d MHz)\n", dev->name, local->hw.conf.channel,
		       local->hw.conf.freq);
		return -1;
	}

	/* Set beacon template based on scan results */
	skb = dev_alloc_skb(local->hw.extra_tx_headroom + 400);
	do {
		if (!skb)
			break;

		skb_reserve(skb, local->hw.extra_tx_headroom);

		mgmt = (struct ieee80211_mgmt *)
			skb_put(skb, 24 + sizeof(mgmt->u.beacon));
		memset(mgmt, 0, 24 + sizeof(mgmt->u.beacon));
		mgmt->frame_control = IEEE80211_FC(IEEE80211_FTYPE_MGMT,
						   IEEE80211_STYPE_BEACON);
		memset(mgmt->da, 0xff, ETH_ALEN);
		memcpy(mgmt->sa, dev->dev_addr, ETH_ALEN);
		memcpy(mgmt->bssid, ifsta->bssid, ETH_ALEN);
		mgmt->u.beacon.beacon_int =
			cpu_to_le16(local->hw.conf.beacon_int);
		mgmt->u.beacon.capab_info = cpu_to_le16(bss->capability);

		pos = skb_put(skb, 2 + ifsta->ssid_len);
		*pos++ = WLAN_EID_SSID;
		*pos++ = ifsta->ssid_len;
		memcpy(pos, ifsta->ssid, ifsta->ssid_len);

		rates = bss->supp_rates_len;
		if (rates > 8)
			rates = 8;
		pos = skb_put(skb, 2 + rates);
		*pos++ = WLAN_EID_SUPP_RATES;
		*pos++ = rates;
		memcpy(pos, bss->supp_rates, rates);

		pos = skb_put(skb, 2 + 1);
		*pos++ = WLAN_EID_DS_PARAMS;
		*pos++ = 1;
		*pos++ = bss->channel;

		pos = skb_put(skb, 2 + 2);
		*pos++ = WLAN_EID_IBSS_PARAMS;
		*pos++ = 2;
		/* FIX: set ATIM window based on scan results */
		*pos++ = 0;
		*pos++ = 0;

		if (bss->supp_rates_len > 8) {
			rates = bss->supp_rates_len - 8;
			pos = skb_put(skb, 2 + rates);
			*pos++ = WLAN_EID_EXT_SUPP_RATES;
			*pos++ = rates;
			memcpy(pos, &bss->supp_rates[8], rates);
		}

		memset(&control, 0, sizeof(control));
		memset(&extra, 0, sizeof(extra));
		extra.mode = local->oper_hw_mode;
		rate = rate_control_get_rate(local, dev, skb, &extra);
		if (!rate) {
			printk(KERN_DEBUG "%s: Failed to determine TX rate "
			       "for IBSS beacon\n", dev->name);
			break;
		}
		control.tx_rate =
			((sdata->flags & IEEE80211_SDATA_SHORT_PREAMBLE) &&
			(rate->flags & IEEE80211_RATE_PREAMBLE2)) ?
			rate->val2 : rate->val;
		control.antenna_sel_tx = local->hw.conf.antenna_sel_tx;
		control.power_level = local->hw.conf.power_level;
		control.flags |= IEEE80211_TXCTL_NO_ACK;
		control.retry_limit = 1;

		ifsta->probe_resp = skb_copy(skb, GFP_ATOMIC);
		if (ifsta->probe_resp) {
			mgmt = (struct ieee80211_mgmt *)
				ifsta->probe_resp->data;
			mgmt->frame_control =
				IEEE80211_FC(IEEE80211_FTYPE_MGMT,
					     IEEE80211_STYPE_PROBE_RESP);
		} else {
			printk(KERN_DEBUG "%s: Could not allocate ProbeResp "
			       "template for IBSS\n", dev->name);
		}

		if (local->ops->beacon_update &&
		    local->ops->beacon_update(local_to_hw(local),
					     skb, &control) == 0) {
			printk(KERN_DEBUG "%s: Configured IBSS beacon "
			       "template based on scan results\n", dev->name);
			skb = NULL;
		}

		rates = 0;
		mode = local->oper_hw_mode;
		for (i = 0; i < bss->supp_rates_len; i++) {
			int bitrate = (bss->supp_rates[i] & 0x7f) * 5;
			for (j = 0; j < mode->num_rates; j++)
				if (mode->rates[j].rate == bitrate)
					rates |= BIT(j);
		}
		ifsta->supp_rates_bits = rates;
	} while (0);

	if (skb) {
		printk(KERN_DEBUG "%s: Failed to configure IBSS beacon "
		       "template\n", dev->name);
		dev_kfree_skb(skb);
	}

	ifsta->state = IEEE80211_IBSS_JOINED;
	mod_timer(&ifsta->timer, jiffies + IEEE80211_IBSS_MERGE_INTERVAL);

	ieee80211_rx_bss_put(dev, bss);

	return res;
}


static int ieee80211_sta_create_ibss(struct net_device *dev,
				     struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_hw_mode *mode;
	u8 bssid[ETH_ALEN], *pos;
	int i;
	DECLARE_MAC_BUF(mac);

#if 0
	/* Easier testing, use fixed BSSID. */
	memset(bssid, 0xfe, ETH_ALEN);
#else
	/* Generate random, not broadcast, locally administered BSSID. Mix in
	 * own MAC address to make sure that devices that do not have proper
	 * random number generator get different BSSID. */
	get_random_bytes(bssid, ETH_ALEN);
	for (i = 0; i < ETH_ALEN; i++)
		bssid[i] ^= dev->dev_addr[i];
	bssid[0] &= ~0x01;
	bssid[0] |= 0x02;
#endif

	printk(KERN_DEBUG "%s: Creating new IBSS network, BSSID %s\n",
	       dev->name, print_mac(mac, bssid));

	bss = ieee80211_rx_bss_add(dev, bssid, local->hw.conf.channel,
				   sdata->u.sta.ssid, sdata->u.sta.ssid_len);
	if (!bss)
		return -ENOMEM;

	mode = local->oper_hw_mode;

	if (local->hw.conf.beacon_int == 0)
		local->hw.conf.beacon_int = 100;
	bss->beacon_int = local->hw.conf.beacon_int;
	bss->hw_mode = local->hw.conf.phymode;
	bss->freq = local->hw.conf.freq;
	bss->last_update = jiffies;
	bss->capability = WLAN_CAPABILITY_IBSS;
	if (sdata->default_key) {
		bss->capability |= WLAN_CAPABILITY_PRIVACY;
	} else
		sdata->drop_unencrypted = 0;
	bss->supp_rates_len = mode->num_rates;
	pos = bss->supp_rates;
	for (i = 0; i < mode->num_rates; i++) {
		int rate = mode->rates[i].rate;
		*pos++ = (u8) (rate / 5);
	}

	return ieee80211_sta_join_ibss(dev, ifsta, bss);
}


static int ieee80211_sta_find_ibss(struct net_device *dev,
				   struct ieee80211_if_sta *ifsta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sta_bss *bss;
	int found = 0;
	u8 bssid[ETH_ALEN];
	int active_ibss;
	DECLARE_MAC_BUF(mac);
	DECLARE_MAC_BUF(mac2);

	if (ifsta->ssid_len == 0)
		return -EINVAL;

	active_ibss = ieee80211_sta_active_ibss(dev);
#ifdef CONFIG_MAC80211_IBSS_DEBUG
	printk(KERN_DEBUG "%s: sta_find_ibss (active_ibss=%d)\n",
	       dev->name, active_ibss);
#endif /* CONFIG_MAC80211_IBSS_DEBUG */
	spin_lock_bh(&local->sta_bss_lock);
	list_for_each_entry(bss, &local->sta_bss_list, list) {
		if (ifsta->ssid_len != bss->ssid_len ||
		    memcmp(ifsta->ssid, bss->ssid, bss->ssid_len) != 0
		    || !(bss->capability & WLAN_CAPABILITY_IBSS))
			continue;
#ifdef CONFIG_MAC80211_IBSS_DEBUG
		printk(KERN_DEBUG "   bssid=%s found\n",
		       print_mac(mac, bss->bssid));
#endif /* CONFIG_MAC80211_IBSS_DEBUG */
		memcpy(bssid, bss->bssid, ETH_ALEN);
		found = 1;
		if (active_ibss || memcmp(bssid, ifsta->bssid, ETH_ALEN) != 0)
			break;
	}
	spin_unlock_bh(&local->sta_bss_lock);

#ifdef CONFIG_MAC80211_IBSS_DEBUG
	printk(KERN_DEBUG "   sta_find_ibss: selected %s current "
	       "%s\n", print_mac(mac, bssid), print_mac(mac2, ifsta->bssid));
#endif /* CONFIG_MAC80211_IBSS_DEBUG */
	if (found && memcmp(ifsta->bssid, bssid, ETH_ALEN) != 0 &&
	    (bss = ieee80211_rx_bss_get(dev, bssid, local->hw.conf.channel,
					ifsta->ssid, ifsta->ssid_len))) {
		printk(KERN_DEBUG "%s: Selected IBSS BSSID %s"
		       " based on configured SSID\n",
		       dev->name, print_mac(mac, bssid));
		return ieee80211_sta_join_ibss(dev, ifsta, bss);
	}
#ifdef CONFIG_MAC80211_IBSS_DEBUG
	printk(KERN_DEBUG "   did not try to join ibss\n");
#endif /* CONFIG_MAC80211_IBSS_DEBUG */

	/* Selected IBSS not found in current scan results - try to scan */
	if (ifsta->state == IEEE80211_IBSS_JOINED &&
	    !ieee80211_sta_active_ibss(dev)) {
		mod_timer(&ifsta->timer, jiffies +
				      IEEE80211_IBSS_MERGE_INTERVAL);
	} else if (time_after(jiffies, local->last_scan_completed +
			      IEEE80211_SCAN_INTERVAL)) {
		printk(KERN_DEBUG "%s: Trigger new scan to find an IBSS to "
		       "join\n", dev->name);
		return ieee80211_sta_req_scan(dev, ifsta->ssid,
					      ifsta->ssid_len);
	} else if (ifsta->state != IEEE80211_IBSS_JOINED) {
		int interval = IEEE80211_SCAN_INTERVAL;

		if (time_after(jiffies, ifsta->ibss_join_req +
			       IEEE80211_IBSS_JOIN_TIMEOUT)) {
			if ((ifsta->flags & IEEE80211_STA_CREATE_IBSS) &&
			    local->oper_channel->flag & IEEE80211_CHAN_W_IBSS)
				return ieee80211_sta_create_ibss(dev, ifsta);
			if (ifsta->flags & IEEE80211_STA_CREATE_IBSS) {
				printk(KERN_DEBUG "%s: IBSS not allowed on the"
				       " configured channel %d (%d MHz)\n",
				       dev->name, local->hw.conf.channel,
				       local->hw.conf.freq);
			}

			/* No IBSS found - decrease scan interval and continue
			 * scanning. */
			interval = IEEE80211_SCAN_INTERVAL_SLOW;
		}

		ifsta->state = IEEE80211_IBSS_SEARCH;
		mod_timer(&ifsta->timer, jiffies + interval);
		return 0;
	}

	return 0;
}


int ieee80211_sta_set_ssid(struct net_device *dev, char *ssid, size_t len)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_sta *ifsta;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (len > IEEE80211_MAX_SSID_LEN)
		return -EINVAL;

	/* TODO: This should always be done for IBSS, even if IEEE80211_QOS is
	 * not defined. */
	if (local->ops->conf_tx) {
		struct ieee80211_tx_queue_params qparam;
		int i;

		memset(&qparam, 0, sizeof(qparam));
		/* TODO: are these ok defaults for all hw_modes? */
		qparam.aifs = 2;
		qparam.cw_min =
			local->hw.conf.phymode == MODE_IEEE80211B ? 31 : 15;
		qparam.cw_max = 1023;
		qparam.burst_time = 0;
		for (i = IEEE80211_TX_QUEUE_DATA0; i < NUM_TX_DATA_QUEUES; i++)
		{
			local->ops->conf_tx(local_to_hw(local),
					   i + IEEE80211_TX_QUEUE_DATA0,
					   &qparam);
		}
		/* IBSS uses different parameters for Beacon sending */
		qparam.cw_min++;
		qparam.cw_min *= 2;
		qparam.cw_min--;
		local->ops->conf_tx(local_to_hw(local),
				   IEEE80211_TX_QUEUE_BEACON, &qparam);
	}

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	ifsta = &sdata->u.sta;

	if (ifsta->ssid_len != len || memcmp(ifsta->ssid, ssid, len) != 0)
		ifsta->flags &= ~IEEE80211_STA_PREV_BSSID_SET;
	memcpy(ifsta->ssid, ssid, len);
	memset(ifsta->ssid + len, 0, IEEE80211_MAX_SSID_LEN - len);
	ifsta->ssid_len = len;

	if (len)
		ifsta->flags |= IEEE80211_STA_SSID_SET;
	else
		ifsta->flags &= ~IEEE80211_STA_SSID_SET;
	if (sdata->type == IEEE80211_IF_TYPE_IBSS &&
	    !(ifsta->flags & IEEE80211_STA_BSSID_SET)) {
		ifsta->ibss_join_req = jiffies;
		ifsta->state = IEEE80211_IBSS_SEARCH;
		return ieee80211_sta_find_ibss(dev, ifsta);
	}
	return 0;
}


int ieee80211_sta_get_ssid(struct net_device *dev, char *ssid, size_t *len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	memcpy(ssid, ifsta->ssid, ifsta->ssid_len);
	*len = ifsta->ssid_len;
	return 0;
}


int ieee80211_sta_set_bssid(struct net_device *dev, u8 *bssid)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_sta *ifsta;
	int res;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	ifsta = &sdata->u.sta;

	if (memcmp(ifsta->bssid, bssid, ETH_ALEN) != 0) {
		memcpy(ifsta->bssid, bssid, ETH_ALEN);
		res = ieee80211_if_config(dev);
		if (res) {
			printk(KERN_DEBUG "%s: Failed to config new BSSID to "
			       "the low-level driver\n", dev->name);
			return res;
		}
	}

	if (is_valid_ether_addr(bssid))
		ifsta->flags |= IEEE80211_STA_BSSID_SET;
	else
		ifsta->flags &= ~IEEE80211_STA_BSSID_SET;

	return 0;
}


static void ieee80211_send_nullfunc(struct ieee80211_local *local,
				    struct ieee80211_sub_if_data *sdata,
				    int powersave)
{
	struct sk_buff *skb;
	struct ieee80211_hdr *nullfunc;
	u16 fc;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + 24);
	if (!skb) {
		printk(KERN_DEBUG "%s: failed to allocate buffer for nullfunc "
		       "frame\n", sdata->dev->name);
		return;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	nullfunc = (struct ieee80211_hdr *) skb_put(skb, 24);
	memset(nullfunc, 0, 24);
	fc = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC |
	     IEEE80211_FCTL_TODS;
	if (powersave)
		fc |= IEEE80211_FCTL_PM;
	nullfunc->frame_control = cpu_to_le16(fc);
	memcpy(nullfunc->addr1, sdata->u.sta.bssid, ETH_ALEN);
	memcpy(nullfunc->addr2, sdata->dev->dev_addr, ETH_ALEN);
	memcpy(nullfunc->addr3, sdata->u.sta.bssid, ETH_ALEN);

	ieee80211_sta_tx(sdata->dev, skb, 0);
}


void ieee80211_scan_completed(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct net_device *dev = local->scan_dev;
	struct ieee80211_sub_if_data *sdata;
	union iwreq_data wrqu;

	local->last_scan_completed = jiffies;
	wmb();
	local->sta_scanning = 0;

	if (ieee80211_hw_config(local))
		printk(KERN_DEBUG "%s: failed to restore operational "
		       "channel after scan\n", dev->name);


	netif_tx_lock_bh(local->mdev);
	local->filter_flags &= ~FIF_BCN_PRBRESP_PROMISC;
	local->ops->configure_filter(local_to_hw(local),
				     FIF_BCN_PRBRESP_PROMISC,
				     &local->filter_flags,
				     local->mdev->mc_count,
				     local->mdev->mc_list);

	netif_tx_unlock_bh(local->mdev);

	memset(&wrqu, 0, sizeof(wrqu));
	wireless_send_event(dev, SIOCGIWSCAN, &wrqu, NULL);

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {

		/* No need to wake the master device. */
		if (sdata->dev == local->mdev)
			continue;

		if (sdata->type == IEEE80211_IF_TYPE_STA) {
			if (sdata->u.sta.flags & IEEE80211_STA_ASSOCIATED)
				ieee80211_send_nullfunc(local, sdata, 0);
			ieee80211_sta_timer((unsigned long)sdata);
		}

		netif_wake_queue(sdata->dev);
	}
	rcu_read_unlock();

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->type == IEEE80211_IF_TYPE_IBSS) {
		struct ieee80211_if_sta *ifsta = &sdata->u.sta;
		if (!(ifsta->flags & IEEE80211_STA_BSSID_SET) ||
		    (!ifsta->state == IEEE80211_IBSS_JOINED &&
		    !ieee80211_sta_active_ibss(dev)))
			ieee80211_sta_find_ibss(dev, ifsta);
	}
}
EXPORT_SYMBOL(ieee80211_scan_completed);

void ieee80211_sta_scan_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, scan_work.work);
	struct net_device *dev = local->scan_dev;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_hw_mode *mode;
	struct ieee80211_channel *chan;
	int skip;
	unsigned long next_delay = 0;

	if (!local->sta_scanning)
		return;

	switch (local->scan_state) {
	case SCAN_SET_CHANNEL:
		mode = local->scan_hw_mode;
		if (local->scan_hw_mode->list.next == &local->modes_list &&
		    local->scan_channel_idx >= mode->num_channels) {
			ieee80211_scan_completed(local_to_hw(local));
			return;
		}
		skip = !(local->enabled_modes & (1 << mode->mode));
		chan = &mode->channels[local->scan_channel_idx];
		if (!(chan->flag & IEEE80211_CHAN_W_SCAN) ||
		    (sdata->type == IEEE80211_IF_TYPE_IBSS &&
		     !(chan->flag & IEEE80211_CHAN_W_IBSS)) ||
		    (local->hw_modes & local->enabled_modes &
		     (1 << MODE_IEEE80211G) && mode->mode == MODE_IEEE80211B))
			skip = 1;

		if (!skip) {
#if 0
			printk(KERN_DEBUG "%s: scan channel %d (%d MHz)\n",
			       dev->name, chan->chan, chan->freq);
#endif

			local->scan_channel = chan;
			if (ieee80211_hw_config(local)) {
				printk(KERN_DEBUG "%s: failed to set channel "
				       "%d (%d MHz) for scan\n", dev->name,
				       chan->chan, chan->freq);
				skip = 1;
			}
		}

		local->scan_channel_idx++;
		if (local->scan_channel_idx >= local->scan_hw_mode->num_channels) {
			if (local->scan_hw_mode->list.next != &local->modes_list) {
				local->scan_hw_mode = list_entry(local->scan_hw_mode->list.next,
								 struct ieee80211_hw_mode,
								 list);
				local->scan_channel_idx = 0;
			}
		}

		if (skip)
			break;

		next_delay = IEEE80211_PROBE_DELAY +
			     usecs_to_jiffies(local->hw.channel_change_time);
		local->scan_state = SCAN_SEND_PROBE;
		break;
	case SCAN_SEND_PROBE:
		if (local->scan_channel->flag & IEEE80211_CHAN_W_ACTIVE_SCAN) {
			ieee80211_send_probe_req(dev, NULL, local->scan_ssid,
						 local->scan_ssid_len);
			next_delay = IEEE80211_CHANNEL_TIME;
		} else
			next_delay = IEEE80211_PASSIVE_CHANNEL_TIME;
		local->scan_state = SCAN_SET_CHANNEL;
		break;
	}

	if (local->sta_scanning)
		queue_delayed_work(local->hw.workqueue, &local->scan_work,
				   next_delay);
}


static int ieee80211_sta_start_scan(struct net_device *dev,
				    u8 *ssid, size_t ssid_len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;

	if (ssid_len > IEEE80211_MAX_SSID_LEN)
		return -EINVAL;

	/* MLME-SCAN.request (page 118)  page 144 (11.1.3.1)
	 * BSSType: INFRASTRUCTURE, INDEPENDENT, ANY_BSS
	 * BSSID: MACAddress
	 * SSID
	 * ScanType: ACTIVE, PASSIVE
	 * ProbeDelay: delay (in microseconds) to be used prior to transmitting
	 *    a Probe frame during active scanning
	 * ChannelList
	 * MinChannelTime (>= ProbeDelay), in TU
	 * MaxChannelTime: (>= MinChannelTime), in TU
	 */

	 /* MLME-SCAN.confirm
	  * BSSDescriptionSet
	  * ResultCode: SUCCESS, INVALID_PARAMETERS
	 */

	if (local->sta_scanning) {
		if (local->scan_dev == dev)
			return 0;
		return -EBUSY;
	}

	if (local->ops->hw_scan) {
		int rc = local->ops->hw_scan(local_to_hw(local),
					    ssid, ssid_len);
		if (!rc) {
			local->sta_scanning = 1;
			local->scan_dev = dev;
		}
		return rc;
	}

	local->sta_scanning = 1;

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {

		/* Don't stop the master interface, otherwise we can't transmit
		 * probes! */
		if (sdata->dev == local->mdev)
			continue;

		netif_stop_queue(sdata->dev);
		if (sdata->type == IEEE80211_IF_TYPE_STA &&
		    (sdata->u.sta.flags & IEEE80211_STA_ASSOCIATED))
			ieee80211_send_nullfunc(local, sdata, 1);
	}
	rcu_read_unlock();

	if (ssid) {
		local->scan_ssid_len = ssid_len;
		memcpy(local->scan_ssid, ssid, ssid_len);
	} else
		local->scan_ssid_len = 0;
	local->scan_state = SCAN_SET_CHANNEL;
	local->scan_hw_mode = list_entry(local->modes_list.next,
					 struct ieee80211_hw_mode,
					 list);
	local->scan_channel_idx = 0;
	local->scan_dev = dev;

	netif_tx_lock_bh(local->mdev);
	local->filter_flags |= FIF_BCN_PRBRESP_PROMISC;
	local->ops->configure_filter(local_to_hw(local),
				     FIF_BCN_PRBRESP_PROMISC,
				     &local->filter_flags,
				     local->mdev->mc_count,
				     local->mdev->mc_list);
	netif_tx_unlock_bh(local->mdev);

	/* TODO: start scan as soon as all nullfunc frames are ACKed */
	queue_delayed_work(local->hw.workqueue, &local->scan_work,
			   IEEE80211_CHANNEL_TIME);

	return 0;
}


int ieee80211_sta_req_scan(struct net_device *dev, u8 *ssid, size_t ssid_len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (sdata->type != IEEE80211_IF_TYPE_STA)
		return ieee80211_sta_start_scan(dev, ssid, ssid_len);

	if (local->sta_scanning) {
		if (local->scan_dev == dev)
			return 0;
		return -EBUSY;
	}

	ifsta->scan_ssid_len = ssid_len;
	if (ssid_len)
		memcpy(ifsta->scan_ssid, ssid, ssid_len);
	set_bit(IEEE80211_STA_REQ_SCAN, &ifsta->request);
	queue_work(local->hw.workqueue, &ifsta->work);
	return 0;
}

static char *
ieee80211_sta_scan_result(struct net_device *dev,
			  struct ieee80211_sta_bss *bss,
			  char *current_ev, char *end_buf)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct iw_event iwe;

	if (time_after(jiffies,
		       bss->last_update + IEEE80211_SCAN_RESULT_EXPIRE))
		return current_ev;

	if (!(local->enabled_modes & (1 << bss->hw_mode)))
		return current_ev;

	if (local->scan_flags & IEEE80211_SCAN_WPA_ONLY &&
	    !bss->wpa_ie && !bss->rsn_ie)
		return current_ev;

	if (local->scan_flags & IEEE80211_SCAN_MATCH_SSID &&
	    (local->scan_ssid_len != bss->ssid_len ||
	     memcmp(local->scan_ssid, bss->ssid, bss->ssid_len) != 0))
		return current_ev;

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, bss->bssid, ETH_ALEN);
	current_ev = iwe_stream_add_event(current_ev, end_buf, &iwe,
					  IW_EV_ADDR_LEN);

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.length = bss->ssid_len;
	iwe.u.data.flags = 1;
	current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe,
					  bss->ssid);

	if (bss->capability & (WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_IBSS)) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWMODE;
		if (bss->capability & WLAN_CAPABILITY_ESS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;
		current_ev = iwe_stream_add_event(current_ev, end_buf, &iwe,
						  IW_EV_UINT_LEN);
	}

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = bss->channel;
	iwe.u.freq.e = 0;
	current_ev = iwe_stream_add_event(current_ev, end_buf, &iwe,
					  IW_EV_FREQ_LEN);
	iwe.u.freq.m = bss->freq * 100000;
	iwe.u.freq.e = 1;
	current_ev = iwe_stream_add_event(current_ev, end_buf, &iwe,
					  IW_EV_FREQ_LEN);

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.qual = bss->signal;
	iwe.u.qual.level = bss->rssi;
	iwe.u.qual.noise = bss->noise;
	iwe.u.qual.updated = local->wstats_flags;
	current_ev = iwe_stream_add_event(current_ev, end_buf, &iwe,
					  IW_EV_QUAL_LEN);

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWENCODE;
	if (bss->capability & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe, "");

	if (bss && bss->wpa_ie) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss->wpa_ie_len;
		current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe,
						  bss->wpa_ie);
	}

	if (bss && bss->rsn_ie) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = bss->rsn_ie_len;
		current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe,
						  bss->rsn_ie);
	}

	if (bss && bss->supp_rates_len > 0) {
		/* display all supported rates in readable format */
		char *p = current_ev + IW_EV_LCP_LEN;
		int i;

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWRATE;
		/* Those two flags are ignored... */
		iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;

		for (i = 0; i < bss->supp_rates_len; i++) {
			iwe.u.bitrate.value = ((bss->supp_rates[i] &
							0x7f) * 500000);
			p = iwe_stream_add_value(current_ev, p,
					end_buf, &iwe, IW_EV_PARAM_LEN);
		}
		current_ev = p;
	}

	if (bss) {
		char *buf;
		buf = kmalloc(30, GFP_ATOMIC);
		if (buf) {
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			sprintf(buf, "tsf=%016llx", (unsigned long long)(bss->timestamp));
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(current_ev, end_buf,
							  &iwe, buf);
			kfree(buf);
		}
	}

	do {
		char *buf;

		if (!(local->scan_flags & IEEE80211_SCAN_EXTRA_INFO))
			break;

		buf = kmalloc(100, GFP_ATOMIC);
		if (!buf)
			break;

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		sprintf(buf, "bcn_int=%d", bss->beacon_int);
		iwe.u.data.length = strlen(buf);
		current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe,
						  buf);

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		sprintf(buf, "capab=0x%04x", bss->capability);
		iwe.u.data.length = strlen(buf);
		current_ev = iwe_stream_add_point(current_ev, end_buf, &iwe,
						  buf);

		kfree(buf);
		break;
	} while (0);

	return current_ev;
}


int ieee80211_sta_scan_results(struct net_device *dev, char *buf, size_t len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	char *current_ev = buf;
	char *end_buf = buf + len;
	struct ieee80211_sta_bss *bss;

	spin_lock_bh(&local->sta_bss_lock);
	list_for_each_entry(bss, &local->sta_bss_list, list) {
		if (buf + len - current_ev <= IW_EV_ADDR_LEN) {
			spin_unlock_bh(&local->sta_bss_lock);
			return -E2BIG;
		}
		current_ev = ieee80211_sta_scan_result(dev, bss, current_ev,
						       end_buf);
	}
	spin_unlock_bh(&local->sta_bss_lock);
	return current_ev - buf;
}


int ieee80211_sta_set_extra_ie(struct net_device *dev, char *ie, size_t len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;
	kfree(ifsta->extra_ie);
	if (len == 0) {
		ifsta->extra_ie = NULL;
		ifsta->extra_ie_len = 0;
		return 0;
	}
	ifsta->extra_ie = kmalloc(len, GFP_KERNEL);
	if (!ifsta->extra_ie) {
		ifsta->extra_ie_len = 0;
		return -ENOMEM;
	}
	memcpy(ifsta->extra_ie, ie, len);
	ifsta->extra_ie_len = len;
	return 0;
}


struct sta_info * ieee80211_ibss_add_sta(struct net_device *dev,
					 struct sk_buff *skb, u8 *bssid,
					 u8 *addr)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	DECLARE_MAC_BUF(mac);

	/* TODO: Could consider removing the least recently used entry and
	 * allow new one to be added. */
	if (local->num_sta >= IEEE80211_IBSS_MAX_STA_ENTRIES) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s: No room for a new IBSS STA "
			       "entry %s\n", dev->name, print_mac(mac, addr));
		}
		return NULL;
	}

	printk(KERN_DEBUG "%s: Adding new IBSS station %s (dev=%s)\n",
	       wiphy_name(local->hw.wiphy), print_mac(mac, addr), dev->name);

	sta = sta_info_add(local, dev, addr, GFP_ATOMIC);
	if (!sta)
		return NULL;

	sta->supp_rates = sdata->u.sta.supp_rates_bits;

	rate_control_rate_init(sta, local);

	return sta; /* caller will call sta_info_put() */
}


int ieee80211_sta_deauthenticate(struct net_device *dev, u16 reason)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;

	printk(KERN_DEBUG "%s: deauthenticate(reason=%d)\n",
	       dev->name, reason);

	if (sdata->type != IEEE80211_IF_TYPE_STA &&
	    sdata->type != IEEE80211_IF_TYPE_IBSS)
		return -EINVAL;

	ieee80211_send_deauth(dev, ifsta, reason);
	ieee80211_set_disassoc(dev, ifsta, 1);
	return 0;
}


int ieee80211_sta_disassociate(struct net_device *dev, u16 reason)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_sta *ifsta = &sdata->u.sta;

	printk(KERN_DEBUG "%s: disassociate(reason=%d)\n",
	       dev->name, reason);

	if (sdata->type != IEEE80211_IF_TYPE_STA)
		return -EINVAL;

	if (!(ifsta->flags & IEEE80211_STA_ASSOCIATED))
		return -1;

	ieee80211_send_disassoc(dev, ifsta, reason);
	ieee80211_set_disassoc(dev, ifsta, 0);
	return 0;
}
