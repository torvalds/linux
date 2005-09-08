/******************************************************************************

  Copyright(c) 2004 Intel Corporation. All rights reserved.

  Portions of this file are based on the WEP enablement code provided by the
  Host AP project hostap-drivers v0.1.3
  Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
  <jkmaline@cc.hut.fi>
  Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Contact Information:
  James P. Ketrenos <ipw2100-admin@linux.intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

******************************************************************************/

#include <linux/kmod.h>
#include <linux/module.h>

#include <net/ieee80211.h>
#include <linux/wireless.h>

static const char *ieee80211_modes[] = {
	"?", "a", "b", "ab", "g", "ag", "bg", "abg"
};

#define MAX_CUSTOM_LEN 64
static inline char *ipw2100_translate_scan(struct ieee80211_device *ieee,
					   char *start, char *stop,
					   struct ieee80211_network *network)
{
	char custom[MAX_CUSTOM_LEN];
	char *p;
	struct iw_event iwe;
	int i, j;
	u8 max_rate, rate;

	/* First entry *MUST* be the AP MAC address */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, network->bssid, ETH_ALEN);
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_ADDR_LEN);

	/* Remaining entries will be displayed in the order we provide them */

	/* Add the ESSID */
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	if (network->flags & NETWORK_EMPTY_ESSID) {
		iwe.u.data.length = sizeof("<hidden>");
		start = iwe_stream_add_point(start, stop, &iwe, "<hidden>");
	} else {
		iwe.u.data.length = min(network->ssid_len, (u8) 32);
		start = iwe_stream_add_point(start, stop, &iwe, network->ssid);
	}

	/* Add the protocol name */
	iwe.cmd = SIOCGIWNAME;
	snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11%s",
		 ieee80211_modes[network->mode]);
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_CHAR_LEN);

	/* Add mode */
	iwe.cmd = SIOCGIWMODE;
	if (network->capability & (WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_IBSS)) {
		if (network->capability & WLAN_CAPABILITY_ESS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;

		start = iwe_stream_add_event(start, stop, &iwe, IW_EV_UINT_LEN);
	}

	/* Add frequency/channel */
	iwe.cmd = SIOCGIWFREQ;
/*	iwe.u.freq.m = ieee80211_frequency(network->channel, network->mode);
	iwe.u.freq.e = 3; */
	iwe.u.freq.m = network->channel;
	iwe.u.freq.e = 0;
	iwe.u.freq.i = 0;
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_FREQ_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (network->capability & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	start = iwe_stream_add_point(start, stop, &iwe, network->ssid);

	/* Add basic and extended rates */
	max_rate = 0;
	p = custom;
	p += snprintf(p, MAX_CUSTOM_LEN - (p - custom), " Rates (Mb/s): ");
	for (i = 0, j = 0; i < network->rates_len;) {
		if (j < network->rates_ex_len &&
		    ((network->rates_ex[j] & 0x7F) <
		     (network->rates[i] & 0x7F)))
			rate = network->rates_ex[j++] & 0x7F;
		else
			rate = network->rates[i++] & 0x7F;
		if (rate > max_rate)
			max_rate = rate;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
			      "%d%s ", rate >> 1, (rate & 1) ? ".5" : "");
	}
	for (; j < network->rates_ex_len; j++) {
		rate = network->rates_ex[j] & 0x7F;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
			      "%d%s ", rate >> 1, (rate & 1) ? ".5" : "");
		if (rate > max_rate)
			max_rate = rate;
	}

	iwe.cmd = SIOCGIWRATE;
	iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
	iwe.u.bitrate.value = max_rate * 500000;
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_PARAM_LEN);

	iwe.cmd = IWEVCUSTOM;
	iwe.u.data.length = p - custom;
	if (iwe.u.data.length)
		start = iwe_stream_add_point(start, stop, &iwe, custom);

	/* Add quality statistics */
	/* TODO: Fix these values... */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.qual = network->stats.signal;
	iwe.u.qual.level = network->stats.rssi;
	iwe.u.qual.noise = network->stats.noise;
	iwe.u.qual.updated = network->stats.mask & IEEE80211_STATMASK_WEMASK;
	if (!(network->stats.mask & IEEE80211_STATMASK_RSSI))
		iwe.u.qual.updated |= IW_QUAL_LEVEL_INVALID;
	if (!(network->stats.mask & IEEE80211_STATMASK_NOISE))
		iwe.u.qual.updated |= IW_QUAL_NOISE_INVALID;
	if (!(network->stats.mask & IEEE80211_STATMASK_SIGNAL))
		iwe.u.qual.updated |= IW_QUAL_QUAL_INVALID;

	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_QUAL_LEN);

	iwe.cmd = IWEVCUSTOM;
	p = custom;

	iwe.u.data.length = p - custom;
	if (iwe.u.data.length)
		start = iwe_stream_add_point(start, stop, &iwe, custom);

	if (ieee->wpa_enabled && network->wpa_ie_len) {
		char buf[MAX_WPA_IE_LEN * 2 + 30];

		u8 *p = buf;
		p += sprintf(p, "wpa_ie=");
		for (i = 0; i < network->wpa_ie_len; i++) {
			p += sprintf(p, "%02x", network->wpa_ie[i]);
		}

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		iwe.u.data.length = strlen(buf);
		start = iwe_stream_add_point(start, stop, &iwe, buf);
	}

	if (ieee->wpa_enabled && network->rsn_ie_len) {
		char buf[MAX_WPA_IE_LEN * 2 + 30];

		u8 *p = buf;
		p += sprintf(p, "rsn_ie=");
		for (i = 0; i < network->rsn_ie_len; i++) {
			p += sprintf(p, "%02x", network->rsn_ie[i]);
		}

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		iwe.u.data.length = strlen(buf);
		start = iwe_stream_add_point(start, stop, &iwe, buf);
	}

	/* Add EXTRA: Age to display seconds since last beacon/probe response
	 * for given network. */
	iwe.cmd = IWEVCUSTOM;
	p = custom;
	p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
		      " Last beacon: %lums ago",
		      (jiffies - network->last_scanned) / (HZ / 100));
	iwe.u.data.length = p - custom;
	if (iwe.u.data.length)
		start = iwe_stream_add_point(start, stop, &iwe, custom);

	return start;
}

int ieee80211_wx_get_scan(struct ieee80211_device *ieee,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct ieee80211_network *network;
	unsigned long flags;

	char *ev = extra;
	char *stop = ev + IW_SCAN_MAX_DATA;
	int i = 0;

	IEEE80211_DEBUG_WX("Getting scan\n");

	spin_lock_irqsave(&ieee->lock, flags);

	list_for_each_entry(network, &ieee->network_list, list) {
		i++;
		if (ieee->scan_age == 0 ||
		    time_after(network->last_scanned + ieee->scan_age, jiffies))
			ev = ipw2100_translate_scan(ieee, ev, stop, network);
		else
			IEEE80211_DEBUG_SCAN("Not showing network '%s ("
					     MAC_FMT ")' due to age (%lums).\n",
					     escape_essid(network->ssid,
							  network->ssid_len),
					     MAC_ARG(network->bssid),
					     (jiffies -
					      network->last_scanned) / (HZ /
									100));
	}

	spin_unlock_irqrestore(&ieee->lock, flags);

	wrqu->data.length = ev - extra;
	wrqu->data.flags = 0;

	IEEE80211_DEBUG_WX("exit: %d networks returned.\n", i);

	return 0;
}

int ieee80211_wx_set_encode(struct ieee80211_device *ieee,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *keybuf)
{
	struct iw_point *erq = &(wrqu->encoding);
	struct net_device *dev = ieee->dev;
	struct ieee80211_security sec = {
		.flags = 0
	};
	int i, key, key_provided, len;
	struct ieee80211_crypt_data **crypt;

	IEEE80211_DEBUG_WX("SET_ENCODE\n");

	key = erq->flags & IW_ENCODE_INDEX;
	if (key) {
		if (key > WEP_KEYS)
			return -EINVAL;
		key--;
		key_provided = 1;
	} else {
		key_provided = 0;
		key = ieee->tx_keyidx;
	}

	IEEE80211_DEBUG_WX("Key: %d [%s]\n", key, key_provided ?
			   "provided" : "default");

	crypt = &ieee->crypt[key];

	if (erq->flags & IW_ENCODE_DISABLED) {
		if (key_provided && *crypt) {
			IEEE80211_DEBUG_WX("Disabling encryption on key %d.\n",
					   key);
			ieee80211_crypt_delayed_deinit(ieee, crypt);
		} else
			IEEE80211_DEBUG_WX("Disabling encryption.\n");

		/* Check all the keys to see if any are still configured,
		 * and if no key index was provided, de-init them all */
		for (i = 0; i < WEP_KEYS; i++) {
			if (ieee->crypt[i] != NULL) {
				if (key_provided)
					break;
				ieee80211_crypt_delayed_deinit(ieee,
							       &ieee->crypt[i]);
			}
		}

		if (i == WEP_KEYS) {
			sec.enabled = 0;
			sec.level = SEC_LEVEL_0;
			sec.flags |= SEC_ENABLED | SEC_LEVEL;
		}

		goto done;
	}

	sec.enabled = 1;
	sec.flags |= SEC_ENABLED;

	if (*crypt != NULL && (*crypt)->ops != NULL &&
	    strcmp((*crypt)->ops->name, "WEP") != 0) {
		/* changing to use WEP; deinit previously used algorithm
		 * on this key */
		ieee80211_crypt_delayed_deinit(ieee, crypt);
	}

	if (*crypt == NULL) {
		struct ieee80211_crypt_data *new_crypt;

		/* take WEP into use */
		new_crypt = kmalloc(sizeof(struct ieee80211_crypt_data),
				    GFP_KERNEL);
		if (new_crypt == NULL)
			return -ENOMEM;
		memset(new_crypt, 0, sizeof(struct ieee80211_crypt_data));
		new_crypt->ops = ieee80211_get_crypto_ops("WEP");
		if (!new_crypt->ops) {
			request_module("ieee80211_crypt_wep");
			new_crypt->ops = ieee80211_get_crypto_ops("WEP");
		}

		if (new_crypt->ops && try_module_get(new_crypt->ops->owner))
			new_crypt->priv = new_crypt->ops->init(key);

		if (!new_crypt->ops || !new_crypt->priv) {
			kfree(new_crypt);
			new_crypt = NULL;

			printk(KERN_WARNING "%s: could not initialize WEP: "
			       "load module ieee80211_crypt_wep\n", dev->name);
			return -EOPNOTSUPP;
		}
		*crypt = new_crypt;
	}

	/* If a new key was provided, set it up */
	if (erq->length > 0) {
		len = erq->length <= 5 ? 5 : 13;
		memcpy(sec.keys[key], keybuf, erq->length);
		if (len > erq->length)
			memset(sec.keys[key] + erq->length, 0,
			       len - erq->length);
		IEEE80211_DEBUG_WX("Setting key %d to '%s' (%d:%d bytes)\n",
				   key, escape_essid(sec.keys[key], len),
				   erq->length, len);
		sec.key_sizes[key] = len;
		(*crypt)->ops->set_key(sec.keys[key], len, NULL,
				       (*crypt)->priv);
		sec.flags |= (1 << key);
		/* This ensures a key will be activated if no key is
		 * explicitely set */
		if (key == sec.active_key)
			sec.flags |= SEC_ACTIVE_KEY;
	} else {
		len = (*crypt)->ops->get_key(sec.keys[key], WEP_KEY_LEN,
					     NULL, (*crypt)->priv);
		if (len == 0) {
			/* Set a default key of all 0 */
			IEEE80211_DEBUG_WX("Setting key %d to all zero.\n",
					   key);
			memset(sec.keys[key], 0, 13);
			(*crypt)->ops->set_key(sec.keys[key], 13, NULL,
					       (*crypt)->priv);
			sec.key_sizes[key] = 13;
			sec.flags |= (1 << key);
		}

		/* No key data - just set the default TX key index */
		if (key_provided) {
			IEEE80211_DEBUG_WX
			    ("Setting key %d to default Tx key.\n", key);
			ieee->tx_keyidx = key;
			sec.active_key = key;
			sec.flags |= SEC_ACTIVE_KEY;
		}
	}

      done:
	ieee->open_wep = !(erq->flags & IW_ENCODE_RESTRICTED);
	sec.auth_mode = ieee->open_wep ? WLAN_AUTH_OPEN : WLAN_AUTH_SHARED_KEY;
	sec.flags |= SEC_AUTH_MODE;
	IEEE80211_DEBUG_WX("Auth: %s\n", sec.auth_mode == WLAN_AUTH_OPEN ?
			   "OPEN" : "SHARED KEY");

	/* For now we just support WEP, so only set that security level...
	 * TODO: When WPA is added this is one place that needs to change */
	sec.flags |= SEC_LEVEL;
	sec.level = SEC_LEVEL_1;	/* 40 and 104 bit WEP */

	if (ieee->set_security)
		ieee->set_security(dev, &sec);

	/* Do not reset port if card is in Managed mode since resetting will
	 * generate new IEEE 802.11 authentication which may end up in looping
	 * with IEEE 802.1X.  If your hardware requires a reset after WEP
	 * configuration (for example... Prism2), implement the reset_port in
	 * the callbacks structures used to initialize the 802.11 stack. */
	if (ieee->reset_on_keychange &&
	    ieee->iw_mode != IW_MODE_INFRA &&
	    ieee->reset_port && ieee->reset_port(dev)) {
		printk(KERN_DEBUG "%s: reset_port failed\n", dev->name);
		return -EINVAL;
	}
	return 0;
}

int ieee80211_wx_get_encode(struct ieee80211_device *ieee,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *keybuf)
{
	struct iw_point *erq = &(wrqu->encoding);
	int len, key;
	struct ieee80211_crypt_data *crypt;

	IEEE80211_DEBUG_WX("GET_ENCODE\n");

	key = erq->flags & IW_ENCODE_INDEX;
	if (key) {
		if (key > WEP_KEYS)
			return -EINVAL;
		key--;
	} else
		key = ieee->tx_keyidx;

	crypt = ieee->crypt[key];
	erq->flags = key + 1;

	if (crypt == NULL || crypt->ops == NULL) {
		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
		return 0;
	}

	if (strcmp(crypt->ops->name, "WEP") != 0) {
		/* only WEP is supported with wireless extensions, so just
		 * report that encryption is used */
		erq->length = 0;
		erq->flags |= IW_ENCODE_ENABLED;
		return 0;
	}

	len = crypt->ops->get_key(keybuf, WEP_KEY_LEN, NULL, crypt->priv);
	erq->length = (len >= 0 ? len : 0);

	erq->flags |= IW_ENCODE_ENABLED;

	if (ieee->open_wep)
		erq->flags |= IW_ENCODE_OPEN;
	else
		erq->flags |= IW_ENCODE_RESTRICTED;

	return 0;
}

EXPORT_SYMBOL(ieee80211_wx_get_scan);
EXPORT_SYMBOL(ieee80211_wx_set_encode);
EXPORT_SYMBOL(ieee80211_wx_get_encode);
