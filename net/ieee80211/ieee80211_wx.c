/******************************************************************************

  Copyright(c) 2004-2005 Intel Corporation. All rights reserved.

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
#include <linux/jiffies.h>

#include <net/ieee80211.h>
#include <linux/wireless.h>

static const char *ieee80211_modes[] = {
	"?", "a", "b", "ab", "g", "ag", "bg", "abg"
};

#define MAX_CUSTOM_LEN 64
static char *ieee80211_translate_scan(struct ieee80211_device *ieee,
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
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated = IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED |
	    IW_QUAL_NOISE_UPDATED;

	if (!(network->stats.mask & IEEE80211_STATMASK_RSSI)) {
		iwe.u.qual.updated |= IW_QUAL_QUAL_INVALID |
		    IW_QUAL_LEVEL_INVALID;
		iwe.u.qual.qual = 0;
	} else {
		if (ieee->perfect_rssi == ieee->worst_rssi)
			iwe.u.qual.qual = 100;
		else
			iwe.u.qual.qual =
			    (100 *
			     (ieee->perfect_rssi - ieee->worst_rssi) *
			     (ieee->perfect_rssi - ieee->worst_rssi) -
			     (ieee->perfect_rssi - network->stats.rssi) *
			     (15 * (ieee->perfect_rssi - ieee->worst_rssi) +
			      62 * (ieee->perfect_rssi -
				    network->stats.rssi))) /
			    ((ieee->perfect_rssi -
			      ieee->worst_rssi) * (ieee->perfect_rssi -
						   ieee->worst_rssi));
		if (iwe.u.qual.qual > 100)
			iwe.u.qual.qual = 100;
		else if (iwe.u.qual.qual < 1)
			iwe.u.qual.qual = 0;
	}

	if (!(network->stats.mask & IEEE80211_STATMASK_NOISE)) {
		iwe.u.qual.updated |= IW_QUAL_NOISE_INVALID;
		iwe.u.qual.noise = 0;
	} else {
		iwe.u.qual.noise = network->stats.noise;
	}

	if (!(network->stats.mask & IEEE80211_STATMASK_SIGNAL)) {
		iwe.u.qual.updated |= IW_QUAL_LEVEL_INVALID;
		iwe.u.qual.level = 0;
	} else {
		iwe.u.qual.level = network->stats.signal;
	}

	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_QUAL_LEN);

	iwe.cmd = IWEVCUSTOM;
	p = custom;

	iwe.u.data.length = p - custom;
	if (iwe.u.data.length)
		start = iwe_stream_add_point(start, stop, &iwe, custom);

	memset(&iwe, 0, sizeof(iwe));
	if (network->wpa_ie_len) {
		char buf[MAX_WPA_IE_LEN];
		memcpy(buf, network->wpa_ie, network->wpa_ie_len);
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = network->wpa_ie_len;
		start = iwe_stream_add_point(start, stop, &iwe, buf);
	}

	memset(&iwe, 0, sizeof(iwe));
	if (network->rsn_ie_len) {
		char buf[MAX_WPA_IE_LEN];
		memcpy(buf, network->rsn_ie, network->rsn_ie_len);
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = network->rsn_ie_len;
		start = iwe_stream_add_point(start, stop, &iwe, buf);
	}

	/* Add EXTRA: Age to display seconds since last beacon/probe response
	 * for given network. */
	iwe.cmd = IWEVCUSTOM;
	p = custom;
	p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
		      " Last beacon: %dms ago",
		      jiffies_to_msecs(jiffies - network->last_scanned));
	iwe.u.data.length = p - custom;
	if (iwe.u.data.length)
		start = iwe_stream_add_point(start, stop, &iwe, custom);

	/* Add spectrum management information */
	iwe.cmd = -1;
	p = custom;
	p += snprintf(p, MAX_CUSTOM_LEN - (p - custom), " Channel flags: ");

	if (ieee80211_get_channel_flags(ieee, network->channel) &
	    IEEE80211_CH_INVALID) {
		iwe.cmd = IWEVCUSTOM;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom), "INVALID ");
	}

	if (ieee80211_get_channel_flags(ieee, network->channel) &
	    IEEE80211_CH_RADAR_DETECT) {
		iwe.cmd = IWEVCUSTOM;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom), "DFS ");
	}

	if (iwe.cmd == IWEVCUSTOM) {
		iwe.u.data.length = p - custom;
		start = iwe_stream_add_point(start, stop, &iwe, custom);
	}

	return start;
}

#define SCAN_ITEM_SIZE 128

int ieee80211_wx_get_scan(struct ieee80211_device *ieee,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct ieee80211_network *network;
	unsigned long flags;
	int err = 0;

	char *ev = extra;
	char *stop = ev + wrqu->data.length;
	int i = 0;

	IEEE80211_DEBUG_WX("Getting scan\n");

	spin_lock_irqsave(&ieee->lock, flags);

	list_for_each_entry(network, &ieee->network_list, list) {
		i++;
		if (stop - ev < SCAN_ITEM_SIZE) {
			err = -E2BIG;
			break;
		}

		if (ieee->scan_age == 0 ||
		    time_after(network->last_scanned + ieee->scan_age, jiffies))
			ev = ieee80211_translate_scan(ieee, ev, stop, network);
		else
			IEEE80211_DEBUG_SCAN("Not showing network '%s ("
					     MAC_FMT ")' due to age (%dms).\n",
					     escape_essid(network->ssid,
							  network->ssid_len),
					     MAC_ARG(network->bssid),
					     jiffies_to_msecs(jiffies -
							      network->
							      last_scanned));
	}

	spin_unlock_irqrestore(&ieee->lock, flags);

	wrqu->data.length = ev - extra;
	wrqu->data.flags = 0;

	IEEE80211_DEBUG_WX("exit: %d networks returned.\n", i);

	return err;
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
	int host_crypto = ieee->host_encrypt || ieee->host_decrypt || ieee->host_build_iv;

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
			sec.encrypt = 0;
			sec.level = SEC_LEVEL_0;
			sec.flags |= SEC_ENABLED | SEC_LEVEL | SEC_ENCRYPT;
		}

		goto done;
	}

	sec.enabled = 1;
	sec.encrypt = 1;
	sec.flags |= SEC_ENABLED | SEC_ENCRYPT;

	if (*crypt != NULL && (*crypt)->ops != NULL &&
	    strcmp((*crypt)->ops->name, "WEP") != 0) {
		/* changing to use WEP; deinit previously used algorithm
		 * on this key */
		ieee80211_crypt_delayed_deinit(ieee, crypt);
	}

	if (*crypt == NULL && host_crypto) {
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
		if (*crypt)
			(*crypt)->ops->set_key(sec.keys[key], len, NULL,
					       (*crypt)->priv);
		sec.flags |= (1 << key);
		/* This ensures a key will be activated if no key is
		 * explicitely set */
		if (key == sec.active_key)
			sec.flags |= SEC_ACTIVE_KEY;

	} else {
		if (host_crypto) {
			len = (*crypt)->ops->get_key(sec.keys[key], WEP_KEY_LEN,
						     NULL, (*crypt)->priv);
			if (len == 0) {
				/* Set a default key of all 0 */
				IEEE80211_DEBUG_WX("Setting key %d to all "
						   "zero.\n", key);
				memset(sec.keys[key], 0, 13);
				(*crypt)->ops->set_key(sec.keys[key], 13, NULL,
						       (*crypt)->priv);
				sec.key_sizes[key] = 13;
				sec.flags |= (1 << key);
			}
		}
		/* No key data - just set the default TX key index */
		if (key_provided) {
			IEEE80211_DEBUG_WX("Setting key %d to default Tx "
					   "key.\n", key);
			ieee->tx_keyidx = key;
			sec.active_key = key;
			sec.flags |= SEC_ACTIVE_KEY;
		}
	}
	if (erq->flags & (IW_ENCODE_OPEN | IW_ENCODE_RESTRICTED)) {
		ieee->open_wep = !(erq->flags & IW_ENCODE_RESTRICTED);
		sec.auth_mode = ieee->open_wep ? WLAN_AUTH_OPEN :
		    WLAN_AUTH_SHARED_KEY;
		sec.flags |= SEC_AUTH_MODE;
		IEEE80211_DEBUG_WX("Auth: %s\n",
				   sec.auth_mode == WLAN_AUTH_OPEN ?
				   "OPEN" : "SHARED KEY");
	}

	/* For now we just support WEP, so only set that security level...
	 * TODO: When WPA is added this is one place that needs to change */
	sec.flags |= SEC_LEVEL;
	sec.level = SEC_LEVEL_1;	/* 40 and 104 bit WEP */
	sec.encode_alg[key] = SEC_ALG_WEP;

      done:
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
	struct ieee80211_security *sec = &ieee->sec;

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

	if (!sec->enabled) {
		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
		return 0;
	}

	len = sec->key_sizes[key];
	memcpy(keybuf, sec->keys[key], len);

	erq->length = (len >= 0 ? len : 0);
	erq->flags |= IW_ENCODE_ENABLED;

	if (ieee->open_wep)
		erq->flags |= IW_ENCODE_OPEN;
	else
		erq->flags |= IW_ENCODE_RESTRICTED;

	return 0;
}

int ieee80211_wx_set_encodeext(struct ieee80211_device *ieee,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct net_device *dev = ieee->dev;
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int i, idx, ret = 0;
	int group_key = 0;
	const char *alg, *module;
	struct ieee80211_crypto_ops *ops;
	struct ieee80211_crypt_data **crypt;

	struct ieee80211_security sec = {
		.flags = 0,
	};

	idx = encoding->flags & IW_ENCODE_INDEX;
	if (idx) {
		if (idx < 1 || idx > WEP_KEYS)
			return -EINVAL;
		idx--;
	} else
		idx = ieee->tx_keyidx;

	if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
		crypt = &ieee->crypt[idx];
		group_key = 1;
	} else {
		/* some Cisco APs use idx>0 for unicast in dynamic WEP */
		if (idx != 0 && ext->alg != IW_ENCODE_ALG_WEP)
			return -EINVAL;
		if (ieee->iw_mode == IW_MODE_INFRA)
			crypt = &ieee->crypt[idx];
		else
			return -EINVAL;
	}

	sec.flags |= SEC_ENABLED | SEC_ENCRYPT;
	if ((encoding->flags & IW_ENCODE_DISABLED) ||
	    ext->alg == IW_ENCODE_ALG_NONE) {
		if (*crypt)
			ieee80211_crypt_delayed_deinit(ieee, crypt);

		for (i = 0; i < WEP_KEYS; i++)
			if (ieee->crypt[i] != NULL)
				break;

		if (i == WEP_KEYS) {
			sec.enabled = 0;
			sec.encrypt = 0;
			sec.level = SEC_LEVEL_0;
			sec.flags |= SEC_LEVEL;
		}
		goto done;
	}

	sec.enabled = 1;
	sec.encrypt = 1;

	if (group_key ? !ieee->host_mc_decrypt :
	    !(ieee->host_encrypt || ieee->host_decrypt ||
	      ieee->host_encrypt_msdu))
		goto skip_host_crypt;

	switch (ext->alg) {
	case IW_ENCODE_ALG_WEP:
		alg = "WEP";
		module = "ieee80211_crypt_wep";
		break;
	case IW_ENCODE_ALG_TKIP:
		alg = "TKIP";
		module = "ieee80211_crypt_tkip";
		break;
	case IW_ENCODE_ALG_CCMP:
		alg = "CCMP";
		module = "ieee80211_crypt_ccmp";
		break;
	default:
		IEEE80211_DEBUG_WX("%s: unknown crypto alg %d\n",
				   dev->name, ext->alg);
		ret = -EINVAL;
		goto done;
	}

	ops = ieee80211_get_crypto_ops(alg);
	if (ops == NULL) {
		request_module(module);
		ops = ieee80211_get_crypto_ops(alg);
	}
	if (ops == NULL) {
		IEEE80211_DEBUG_WX("%s: unknown crypto alg %d\n",
				   dev->name, ext->alg);
		ret = -EINVAL;
		goto done;
	}

	if (*crypt == NULL || (*crypt)->ops != ops) {
		struct ieee80211_crypt_data *new_crypt;

		ieee80211_crypt_delayed_deinit(ieee, crypt);

		new_crypt = (struct ieee80211_crypt_data *)
		    kmalloc(sizeof(*new_crypt), GFP_KERNEL);
		if (new_crypt == NULL) {
			ret = -ENOMEM;
			goto done;
		}
		memset(new_crypt, 0, sizeof(struct ieee80211_crypt_data));
		new_crypt->ops = ops;
		if (new_crypt->ops && try_module_get(new_crypt->ops->owner))
			new_crypt->priv = new_crypt->ops->init(idx);
		if (new_crypt->priv == NULL) {
			kfree(new_crypt);
			ret = -EINVAL;
			goto done;
		}
		*crypt = new_crypt;
	}

	if (ext->key_len > 0 && (*crypt)->ops->set_key &&
	    (*crypt)->ops->set_key(ext->key, ext->key_len, ext->rx_seq,
				   (*crypt)->priv) < 0) {
		IEEE80211_DEBUG_WX("%s: key setting failed\n", dev->name);
		ret = -EINVAL;
		goto done;
	}

      skip_host_crypt:
	if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
		ieee->tx_keyidx = idx;
		sec.active_key = idx;
		sec.flags |= SEC_ACTIVE_KEY;
	}

	if (ext->alg != IW_ENCODE_ALG_NONE) {
		memcpy(sec.keys[idx], ext->key, ext->key_len);
		sec.key_sizes[idx] = ext->key_len;
		sec.flags |= (1 << idx);
		if (ext->alg == IW_ENCODE_ALG_WEP) {
			sec.encode_alg[idx] = SEC_ALG_WEP;
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_1;
		} else if (ext->alg == IW_ENCODE_ALG_TKIP) {
			sec.encode_alg[idx] = SEC_ALG_TKIP;
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_2;
		} else if (ext->alg == IW_ENCODE_ALG_CCMP) {
			sec.encode_alg[idx] = SEC_ALG_CCMP;
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_3;
		}
		/* Don't set sec level for group keys. */
		if (group_key)
			sec.flags &= ~SEC_LEVEL;
	}
      done:
	if (ieee->set_security)
		ieee->set_security(ieee->dev, &sec);

	/*
	 * Do not reset port if card is in Managed mode since resetting will
	 * generate new IEEE 802.11 authentication which may end up in looping
	 * with IEEE 802.1X. If your hardware requires a reset after WEP
	 * configuration (for example... Prism2), implement the reset_port in
	 * the callbacks structures used to initialize the 802.11 stack.
	 */
	if (ieee->reset_on_keychange &&
	    ieee->iw_mode != IW_MODE_INFRA &&
	    ieee->reset_port && ieee->reset_port(dev)) {
		IEEE80211_DEBUG_WX("%s: reset_port failed\n", dev->name);
		return -EINVAL;
	}

	return ret;
}

int ieee80211_wx_get_encodeext(struct ieee80211_device *ieee,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	struct ieee80211_security *sec = &ieee->sec;
	int idx, max_key_len;

	max_key_len = encoding->length - sizeof(*ext);
	if (max_key_len < 0)
		return -EINVAL;

	idx = encoding->flags & IW_ENCODE_INDEX;
	if (idx) {
		if (idx < 1 || idx > WEP_KEYS)
			return -EINVAL;
		idx--;
	} else
		idx = ieee->tx_keyidx;

	if (!ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY &&
	    ext->alg != IW_ENCODE_ALG_WEP)
		if (idx != 0 || ieee->iw_mode != IW_MODE_INFRA)
			return -EINVAL;

	encoding->flags = idx + 1;
	memset(ext, 0, sizeof(*ext));

	if (!sec->enabled) {
		ext->alg = IW_ENCODE_ALG_NONE;
		ext->key_len = 0;
		encoding->flags |= IW_ENCODE_DISABLED;
	} else {
		if (sec->encode_alg[idx] == SEC_ALG_WEP)
			ext->alg = IW_ENCODE_ALG_WEP;
		else if (sec->encode_alg[idx] == SEC_ALG_TKIP)
			ext->alg = IW_ENCODE_ALG_TKIP;
		else if (sec->encode_alg[idx] == SEC_ALG_CCMP)
			ext->alg = IW_ENCODE_ALG_CCMP;
		else
			return -EINVAL;

		ext->key_len = sec->key_sizes[idx];
		memcpy(ext->key, sec->keys[idx], ext->key_len);
		encoding->flags |= IW_ENCODE_ENABLED;
		if (ext->key_len &&
		    (ext->alg == IW_ENCODE_ALG_TKIP ||
		     ext->alg == IW_ENCODE_ALG_CCMP))
			ext->ext_flags |= IW_ENCODE_EXT_TX_SEQ_VALID;

	}

	return 0;
}

int ieee80211_wx_set_auth(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu,
			  char *extra)
{
	struct ieee80211_device *ieee = netdev_priv(dev);
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&ieee->lock, flags);
	
	switch (wrqu->param.flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_KEY_MGMT:
		/*
		 * Host AP driver does not use these parameters and allows
		 * wpa_supplicant to control them internally.
		 */
		break;
	case IW_AUTH_TKIP_COUNTERMEASURES:
		break;		/* FIXME */
	case IW_AUTH_DROP_UNENCRYPTED:
		ieee->drop_unencrypted = !!wrqu->param.value;
		break;
	case IW_AUTH_80211_AUTH_ALG:
		break;		/* FIXME */
	case IW_AUTH_WPA_ENABLED:
		ieee->privacy_invoked = ieee->wpa_enabled = !!wrqu->param.value;
		break;
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		ieee->ieee802_1x = !!wrqu->param.value;
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		ieee->privacy_invoked = !!wrqu->param.value;
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
	return err;
}

int ieee80211_wx_get_auth(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu,
			  char *extra)
{
	struct ieee80211_device *ieee = netdev_priv(dev);
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&ieee->lock, flags);
	
	switch (wrqu->param.flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_KEY_MGMT:
	case IW_AUTH_TKIP_COUNTERMEASURES:		/* FIXME */
	case IW_AUTH_80211_AUTH_ALG:			/* FIXME */
		/*
		 * Host AP driver does not use these parameters and allows
		 * wpa_supplicant to control them internally.
		 */
		err = -EOPNOTSUPP;
		break;
	case IW_AUTH_DROP_UNENCRYPTED:
		wrqu->param.value = ieee->drop_unencrypted;
		break;
	case IW_AUTH_WPA_ENABLED:
		wrqu->param.value = ieee->wpa_enabled;
		break;
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		wrqu->param.value = ieee->ieee802_1x;
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
	return err;
}

EXPORT_SYMBOL(ieee80211_wx_set_encodeext);
EXPORT_SYMBOL(ieee80211_wx_get_encodeext);

EXPORT_SYMBOL(ieee80211_wx_get_scan);
EXPORT_SYMBOL(ieee80211_wx_set_encode);
EXPORT_SYMBOL(ieee80211_wx_get_encode);

EXPORT_SYMBOL_GPL(ieee80211_wx_set_auth);
EXPORT_SYMBOL_GPL(ieee80211_wx_get_auth);
