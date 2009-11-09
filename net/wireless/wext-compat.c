/*
 * cfg80211 - wext compat code
 *
 * This is temporary code until all wireless functionality is migrated
 * into cfg80211, when that happens all the exports here go away and
 * we directly assign the wireless handlers of wireless interfaces.
 *
 * Copyright 2008-2009	Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/wireless.h>
#include <linux/nl80211.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <net/iw_handler.h>
#include <net/cfg80211.h>
#include "wext-compat.h"
#include "core.h"

int cfg80211_wext_giwname(struct net_device *dev,
			  struct iw_request_info *info,
			  char *name, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct ieee80211_supported_band *sband;
	bool is_ht = false, is_a = false, is_b = false, is_g = false;

	if (!wdev)
		return -EOPNOTSUPP;

	sband = wdev->wiphy->bands[IEEE80211_BAND_5GHZ];
	if (sband) {
		is_a = true;
		is_ht |= sband->ht_cap.ht_supported;
	}

	sband = wdev->wiphy->bands[IEEE80211_BAND_2GHZ];
	if (sband) {
		int i;
		/* Check for mandatory rates */
		for (i = 0; i < sband->n_bitrates; i++) {
			if (sband->bitrates[i].bitrate == 10)
				is_b = true;
			if (sband->bitrates[i].bitrate == 60)
				is_g = true;
		}
		is_ht |= sband->ht_cap.ht_supported;
	}

	strcpy(name, "IEEE 802.11");
	if (is_a)
		strcat(name, "a");
	if (is_b)
		strcat(name, "b");
	if (is_g)
		strcat(name, "g");
	if (is_ht)
		strcat(name, "n");

	return 0;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwname);

int cfg80211_wext_siwmode(struct net_device *dev, struct iw_request_info *info,
			  u32 *mode, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev;
	struct vif_params vifparams;
	enum nl80211_iftype type;
	int ret;

	rdev = wiphy_to_dev(wdev->wiphy);

	switch (*mode) {
	case IW_MODE_INFRA:
		type = NL80211_IFTYPE_STATION;
		break;
	case IW_MODE_ADHOC:
		type = NL80211_IFTYPE_ADHOC;
		break;
	case IW_MODE_REPEAT:
		type = NL80211_IFTYPE_WDS;
		break;
	case IW_MODE_MONITOR:
		type = NL80211_IFTYPE_MONITOR;
		break;
	default:
		return -EINVAL;
	}

	if (type == wdev->iftype)
		return 0;

	memset(&vifparams, 0, sizeof(vifparams));

	cfg80211_lock_rdev(rdev);
	ret = cfg80211_change_iface(rdev, dev, type, NULL, &vifparams);
	cfg80211_unlock_rdev(rdev);

	return ret;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwmode);

int cfg80211_wext_giwmode(struct net_device *dev, struct iw_request_info *info,
			  u32 *mode, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	if (!wdev)
		return -EOPNOTSUPP;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_AP:
		*mode = IW_MODE_MASTER;
		break;
	case NL80211_IFTYPE_STATION:
		*mode = IW_MODE_INFRA;
		break;
	case NL80211_IFTYPE_ADHOC:
		*mode = IW_MODE_ADHOC;
		break;
	case NL80211_IFTYPE_MONITOR:
		*mode = IW_MODE_MONITOR;
		break;
	case NL80211_IFTYPE_WDS:
		*mode = IW_MODE_REPEAT;
		break;
	case NL80211_IFTYPE_AP_VLAN:
		*mode = IW_MODE_SECOND;		/* FIXME */
		break;
	default:
		*mode = IW_MODE_AUTO;
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwmode);


int cfg80211_wext_giwrange(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct iw_range *range = (struct iw_range *) extra;
	enum ieee80211_band band;
	int i, c = 0;

	if (!wdev)
		return -EOPNOTSUPP;

	data->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 21;
	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->min_retry = 0;
	range->max_retry = 255;
	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	range->max_encoding_tokens = 4;

	range->max_qual.updated = IW_QUAL_NOISE_INVALID;

	switch (wdev->wiphy->signal_type) {
	case CFG80211_SIGNAL_TYPE_NONE:
		break;
	case CFG80211_SIGNAL_TYPE_MBM:
		range->max_qual.level = -110;
		range->max_qual.qual = 70;
		range->avg_qual.qual = 35;
		range->max_qual.updated |= IW_QUAL_DBM;
		range->max_qual.updated |= IW_QUAL_QUAL_UPDATED;
		range->max_qual.updated |= IW_QUAL_LEVEL_UPDATED;
		break;
	case CFG80211_SIGNAL_TYPE_UNSPEC:
		range->max_qual.level = 100;
		range->max_qual.qual = 100;
		range->avg_qual.qual = 50;
		range->max_qual.updated |= IW_QUAL_QUAL_UPDATED;
		range->max_qual.updated |= IW_QUAL_LEVEL_UPDATED;
		break;
	}

	range->avg_qual.level = range->max_qual.level / 2;
	range->avg_qual.noise = range->max_qual.noise / 2;
	range->avg_qual.updated = range->max_qual.updated;

	for (i = 0; i < wdev->wiphy->n_cipher_suites; i++) {
		switch (wdev->wiphy->cipher_suites[i]) {
		case WLAN_CIPHER_SUITE_TKIP:
			range->enc_capa |= (IW_ENC_CAPA_CIPHER_TKIP |
					    IW_ENC_CAPA_WPA);
			break;

		case WLAN_CIPHER_SUITE_CCMP:
			range->enc_capa |= (IW_ENC_CAPA_CIPHER_CCMP |
					    IW_ENC_CAPA_WPA2);
			break;

		case WLAN_CIPHER_SUITE_WEP40:
			range->encoding_size[range->num_encoding_sizes++] =
				WLAN_KEY_LEN_WEP40;
			break;

		case WLAN_CIPHER_SUITE_WEP104:
			range->encoding_size[range->num_encoding_sizes++] =
				WLAN_KEY_LEN_WEP104;
			break;
		}
	}

	for (band = 0; band < IEEE80211_NUM_BANDS; band ++) {
		struct ieee80211_supported_band *sband;

		sband = wdev->wiphy->bands[band];

		if (!sband)
			continue;

		for (i = 0; i < sband->n_channels && c < IW_MAX_FREQUENCIES; i++) {
			struct ieee80211_channel *chan = &sband->channels[i];

			if (!(chan->flags & IEEE80211_CHAN_DISABLED)) {
				range->freq[c].i =
					ieee80211_frequency_to_channel(
						chan->center_freq);
				range->freq[c].m = chan->center_freq;
				range->freq[c].e = 6;
				c++;
			}
		}
	}
	range->num_channels = c;
	range->num_frequency = c;

	IW_EVENT_CAPA_SET_KERNEL(range->event_capa);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWAP);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWSCAN);

	if (wdev->wiphy->max_scan_ssids > 0)
		range->scan_capa |= IW_SCAN_CAPA_ESSID;

	return 0;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwrange);


/**
 * cfg80211_wext_freq - get wext frequency for non-"auto"
 * @wiphy: the wiphy
 * @freq: the wext freq encoding
 *
 * Returns a frequency, or a negative error code, or 0 for auto.
 */
int cfg80211_wext_freq(struct wiphy *wiphy, struct iw_freq *freq)
{
	/*
	 * Parse frequency - return 0 for auto and
	 * -EINVAL for impossible things.
	 */
	if (freq->e == 0) {
		if (freq->m < 0)
			return 0;
		return ieee80211_channel_to_frequency(freq->m);
	} else {
		int i, div = 1000000;
		for (i = 0; i < freq->e; i++)
			div /= 10;
		if (div <= 0)
			return -EINVAL;
		return freq->m / div;
	}
}

int cfg80211_wext_siwrts(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *rts, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	u32 orts = wdev->wiphy->rts_threshold;
	int err;

	if (rts->disabled || !rts->fixed)
		wdev->wiphy->rts_threshold = (u32) -1;
	else if (rts->value < 0)
		return -EINVAL;
	else
		wdev->wiphy->rts_threshold = rts->value;

	err = rdev->ops->set_wiphy_params(wdev->wiphy,
					  WIPHY_PARAM_RTS_THRESHOLD);
	if (err)
		wdev->wiphy->rts_threshold = orts;

	return err;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwrts);

int cfg80211_wext_giwrts(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *rts, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	rts->value = wdev->wiphy->rts_threshold;
	rts->disabled = rts->value == (u32) -1;
	rts->fixed = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwrts);

int cfg80211_wext_siwfrag(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *frag, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	u32 ofrag = wdev->wiphy->frag_threshold;
	int err;

	if (frag->disabled || !frag->fixed)
		wdev->wiphy->frag_threshold = (u32) -1;
	else if (frag->value < 256)
		return -EINVAL;
	else {
		/* Fragment length must be even, so strip LSB. */
		wdev->wiphy->frag_threshold = frag->value & ~0x1;
	}

	err = rdev->ops->set_wiphy_params(wdev->wiphy,
					  WIPHY_PARAM_FRAG_THRESHOLD);
	if (err)
		wdev->wiphy->frag_threshold = ofrag;

	return err;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwfrag);

int cfg80211_wext_giwfrag(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *frag, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	frag->value = wdev->wiphy->frag_threshold;
	frag->disabled = frag->value == (u32) -1;
	frag->fixed = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwfrag);

int cfg80211_wext_siwretry(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *retry, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	u32 changed = 0;
	u8 olong = wdev->wiphy->retry_long;
	u8 oshort = wdev->wiphy->retry_short;
	int err;

	if (retry->disabled ||
	    (retry->flags & IW_RETRY_TYPE) != IW_RETRY_LIMIT)
		return -EINVAL;

	if (retry->flags & IW_RETRY_LONG) {
		wdev->wiphy->retry_long = retry->value;
		changed |= WIPHY_PARAM_RETRY_LONG;
	} else if (retry->flags & IW_RETRY_SHORT) {
		wdev->wiphy->retry_short = retry->value;
		changed |= WIPHY_PARAM_RETRY_SHORT;
	} else {
		wdev->wiphy->retry_short = retry->value;
		wdev->wiphy->retry_long = retry->value;
		changed |= WIPHY_PARAM_RETRY_LONG;
		changed |= WIPHY_PARAM_RETRY_SHORT;
	}

	if (!changed)
		return 0;

	err = rdev->ops->set_wiphy_params(wdev->wiphy, changed);
	if (err) {
		wdev->wiphy->retry_short = oshort;
		wdev->wiphy->retry_long = olong;
	}

	return err;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwretry);

int cfg80211_wext_giwretry(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *retry, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	retry->disabled = 0;

	if (retry->flags == 0 || (retry->flags & IW_RETRY_SHORT)) {
		/*
		 * First return short value, iwconfig will ask long value
		 * later if needed
		 */
		retry->flags |= IW_RETRY_LIMIT;
		retry->value = wdev->wiphy->retry_short;
		if (wdev->wiphy->retry_long != wdev->wiphy->retry_short)
			retry->flags |= IW_RETRY_LONG;

		return 0;
	}

	if (retry->flags & IW_RETRY_LONG) {
		retry->flags = IW_RETRY_LIMIT | IW_RETRY_LONG;
		retry->value = wdev->wiphy->retry_long;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwretry);

static int __cfg80211_set_encryption(struct cfg80211_registered_device *rdev,
				     struct net_device *dev, const u8 *addr,
				     bool remove, bool tx_key, int idx,
				     struct key_params *params)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err, i;

	if (!wdev->wext.keys) {
		wdev->wext.keys = kzalloc(sizeof(*wdev->wext.keys),
					      GFP_KERNEL);
		if (!wdev->wext.keys)
			return -ENOMEM;
		for (i = 0; i < 6; i++)
			wdev->wext.keys->params[i].key =
				wdev->wext.keys->data[i];
	}

	if (wdev->iftype != NL80211_IFTYPE_ADHOC &&
	    wdev->iftype != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	if (params->cipher == WLAN_CIPHER_SUITE_AES_CMAC) {
		if (!wdev->current_bss)
			return -ENOLINK;

		if (!rdev->ops->set_default_mgmt_key)
			return -EOPNOTSUPP;

		if (idx < 4 || idx > 5)
			return -EINVAL;
	} else if (idx < 0 || idx > 3)
		return -EINVAL;

	if (remove) {
		err = 0;
		if (wdev->current_bss)
			err = rdev->ops->del_key(&rdev->wiphy, dev, idx, addr);
		if (!err) {
			if (!addr) {
				wdev->wext.keys->params[idx].key_len = 0;
				wdev->wext.keys->params[idx].cipher = 0;
			}
			if (idx == wdev->wext.default_key)
				wdev->wext.default_key = -1;
			else if (idx == wdev->wext.default_mgmt_key)
				wdev->wext.default_mgmt_key = -1;
		}
		/*
		 * Applications using wireless extensions expect to be
		 * able to delete keys that don't exist, so allow that.
		 */
		if (err == -ENOENT)
			return 0;

		return err;
	}

	if (addr)
		tx_key = false;

	if (cfg80211_validate_key_settings(rdev, params, idx, addr))
		return -EINVAL;

	err = 0;
	if (wdev->current_bss)
		err = rdev->ops->add_key(&rdev->wiphy, dev, idx, addr, params);
	if (err)
		return err;

	if (!addr) {
		wdev->wext.keys->params[idx] = *params;
		memcpy(wdev->wext.keys->data[idx],
			params->key, params->key_len);
		wdev->wext.keys->params[idx].key =
			wdev->wext.keys->data[idx];
	}

	if ((params->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	     params->cipher == WLAN_CIPHER_SUITE_WEP104) &&
	    (tx_key || (!addr && wdev->wext.default_key == -1))) {
		if (wdev->current_bss)
			err = rdev->ops->set_default_key(&rdev->wiphy,
							 dev, idx);
		if (!err)
			wdev->wext.default_key = idx;
		return err;
	}

	if (params->cipher == WLAN_CIPHER_SUITE_AES_CMAC &&
	    (tx_key || (!addr && wdev->wext.default_mgmt_key == -1))) {
		if (wdev->current_bss)
			err = rdev->ops->set_default_mgmt_key(&rdev->wiphy,
							      dev, idx);
		if (!err)
			wdev->wext.default_mgmt_key = idx;
		return err;
	}

	return 0;
}

static int cfg80211_set_encryption(struct cfg80211_registered_device *rdev,
				   struct net_device *dev, const u8 *addr,
				   bool remove, bool tx_key, int idx,
				   struct key_params *params)
{
	int err;

	wdev_lock(dev->ieee80211_ptr);
	err = __cfg80211_set_encryption(rdev, dev, addr, remove,
					tx_key, idx, params);
	wdev_unlock(dev->ieee80211_ptr);

	return err;
}

int cfg80211_wext_siwencode(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_point *erq, char *keybuf)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	int idx, err;
	bool remove = false;
	struct key_params params;

	if (wdev->iftype != NL80211_IFTYPE_STATION &&
	    wdev->iftype != NL80211_IFTYPE_ADHOC)
		return -EOPNOTSUPP;

	/* no use -- only MFP (set_default_mgmt_key) is optional */
	if (!rdev->ops->del_key ||
	    !rdev->ops->add_key ||
	    !rdev->ops->set_default_key)
		return -EOPNOTSUPP;

	idx = erq->flags & IW_ENCODE_INDEX;
	if (idx == 0) {
		idx = wdev->wext.default_key;
		if (idx < 0)
			idx = 0;
	} else if (idx < 1 || idx > 4)
		return -EINVAL;
	else
		idx--;

	if (erq->flags & IW_ENCODE_DISABLED)
		remove = true;
	else if (erq->length == 0) {
		/* No key data - just set the default TX key index */
		err = 0;
		wdev_lock(wdev);
		if (wdev->current_bss)
			err = rdev->ops->set_default_key(&rdev->wiphy,
							 dev, idx);
		if (!err)
			wdev->wext.default_key = idx;
		wdev_unlock(wdev);
		return err;
	}

	memset(&params, 0, sizeof(params));
	params.key = keybuf;
	params.key_len = erq->length;
	if (erq->length == 5)
		params.cipher = WLAN_CIPHER_SUITE_WEP40;
	else if (erq->length == 13)
		params.cipher = WLAN_CIPHER_SUITE_WEP104;
	else if (!remove)
		return -EINVAL;

	return cfg80211_set_encryption(rdev, dev, NULL, remove,
				       wdev->wext.default_key == -1,
				       idx, &params);
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwencode);

int cfg80211_wext_siwencodeext(struct net_device *dev,
			       struct iw_request_info *info,
			       struct iw_point *erq, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	struct iw_encode_ext *ext = (struct iw_encode_ext *) extra;
	const u8 *addr;
	int idx;
	bool remove = false;
	struct key_params params;
	u32 cipher;

	if (wdev->iftype != NL80211_IFTYPE_STATION &&
	    wdev->iftype != NL80211_IFTYPE_ADHOC)
		return -EOPNOTSUPP;

	/* no use -- only MFP (set_default_mgmt_key) is optional */
	if (!rdev->ops->del_key ||
	    !rdev->ops->add_key ||
	    !rdev->ops->set_default_key)
		return -EOPNOTSUPP;

	switch (ext->alg) {
	case IW_ENCODE_ALG_NONE:
		remove = true;
		cipher = 0;
		break;
	case IW_ENCODE_ALG_WEP:
		if (ext->key_len == 5)
			cipher = WLAN_CIPHER_SUITE_WEP40;
		else if (ext->key_len == 13)
			cipher = WLAN_CIPHER_SUITE_WEP104;
		else
			return -EINVAL;
		break;
	case IW_ENCODE_ALG_TKIP:
		cipher = WLAN_CIPHER_SUITE_TKIP;
		break;
	case IW_ENCODE_ALG_CCMP:
		cipher = WLAN_CIPHER_SUITE_CCMP;
		break;
	case IW_ENCODE_ALG_AES_CMAC:
		cipher = WLAN_CIPHER_SUITE_AES_CMAC;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (erq->flags & IW_ENCODE_DISABLED)
		remove = true;

	idx = erq->flags & IW_ENCODE_INDEX;
	if (cipher == WLAN_CIPHER_SUITE_AES_CMAC) {
		if (idx < 4 || idx > 5) {
			idx = wdev->wext.default_mgmt_key;
			if (idx < 0)
				return -EINVAL;
		} else
			idx--;
	} else {
		if (idx < 1 || idx > 4) {
			idx = wdev->wext.default_key;
			if (idx < 0)
				return -EINVAL;
		} else
			idx--;
	}

	addr = ext->addr.sa_data;
	if (is_broadcast_ether_addr(addr))
		addr = NULL;

	memset(&params, 0, sizeof(params));
	params.key = ext->key;
	params.key_len = ext->key_len;
	params.cipher = cipher;

	if (ext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
		params.seq = ext->rx_seq;
		params.seq_len = 6;
	}

	return cfg80211_set_encryption(
			rdev, dev, addr, remove,
			ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY,
			idx, &params);
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwencodeext);

int cfg80211_wext_giwencode(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_point *erq, char *keybuf)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int idx;

	if (wdev->iftype != NL80211_IFTYPE_STATION &&
	    wdev->iftype != NL80211_IFTYPE_ADHOC)
		return -EOPNOTSUPP;

	idx = erq->flags & IW_ENCODE_INDEX;
	if (idx == 0) {
		idx = wdev->wext.default_key;
		if (idx < 0)
			idx = 0;
	} else if (idx < 1 || idx > 4)
		return -EINVAL;
	else
		idx--;

	erq->flags = idx + 1;

	if (!wdev->wext.keys || !wdev->wext.keys->params[idx].cipher) {
		erq->flags |= IW_ENCODE_DISABLED;
		erq->length = 0;
		return 0;
	}

	erq->length = min_t(size_t, erq->length,
			    wdev->wext.keys->params[idx].key_len);
	memcpy(keybuf, wdev->wext.keys->params[idx].key, erq->length);
	erq->flags |= IW_ENCODE_ENABLED;

	return 0;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwencode);

int cfg80211_wext_siwfreq(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_freq *wextfreq, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	int freq, err;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
		return cfg80211_mgd_wext_siwfreq(dev, info, wextfreq, extra);
	case NL80211_IFTYPE_ADHOC:
		return cfg80211_ibss_wext_siwfreq(dev, info, wextfreq, extra);
	default:
		freq = cfg80211_wext_freq(wdev->wiphy, wextfreq);
		if (freq < 0)
			return freq;
		if (freq == 0)
			return -EINVAL;
		mutex_lock(&rdev->devlist_mtx);
		err = rdev_set_freq(rdev, NULL, freq, NL80211_CHAN_NO_HT);
		mutex_unlock(&rdev->devlist_mtx);
		return err;
	}
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwfreq);

int cfg80211_wext_giwfreq(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_freq *freq, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);

	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
		return cfg80211_mgd_wext_giwfreq(dev, info, freq, extra);
	case NL80211_IFTYPE_ADHOC:
		return cfg80211_ibss_wext_giwfreq(dev, info, freq, extra);
	default:
		if (!rdev->channel)
			return -EINVAL;
		freq->m = rdev->channel->center_freq;
		freq->e = 6;
		return 0;
	}
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwfreq);

int cfg80211_wext_siwtxpower(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *data, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	enum tx_power_setting type;
	int dbm = 0;

	if ((data->txpower.flags & IW_TXPOW_TYPE) != IW_TXPOW_DBM)
		return -EINVAL;
	if (data->txpower.flags & IW_TXPOW_RANGE)
		return -EINVAL;

	if (!rdev->ops->set_tx_power)
		return -EOPNOTSUPP;

	/* only change when not disabling */
	if (!data->txpower.disabled) {
		rfkill_set_sw_state(rdev->rfkill, false);

		if (data->txpower.fixed) {
			/*
			 * wext doesn't support negative values, see
			 * below where it's for automatic
			 */
			if (data->txpower.value < 0)
				return -EINVAL;
			dbm = data->txpower.value;
			type = TX_POWER_FIXED;
			/* TODO: do regulatory check! */
		} else {
			/*
			 * Automatic power level setting, max being the value
			 * passed in from userland.
			 */
			if (data->txpower.value < 0) {
				type = TX_POWER_AUTOMATIC;
			} else {
				dbm = data->txpower.value;
				type = TX_POWER_LIMITED;
			}
		}
	} else {
		rfkill_set_sw_state(rdev->rfkill, true);
		schedule_work(&rdev->rfkill_sync);
		return 0;
	}

	return rdev->ops->set_tx_power(wdev->wiphy, type, dbm);
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwtxpower);

int cfg80211_wext_giwtxpower(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *data, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	int err, val;

	if ((data->txpower.flags & IW_TXPOW_TYPE) != IW_TXPOW_DBM)
		return -EINVAL;
	if (data->txpower.flags & IW_TXPOW_RANGE)
		return -EINVAL;

	if (!rdev->ops->get_tx_power)
		return -EOPNOTSUPP;

	err = rdev->ops->get_tx_power(wdev->wiphy, &val);
	if (err)
		return err;

	/* well... oh well */
	data->txpower.fixed = 1;
	data->txpower.disabled = rfkill_blocked(rdev->rfkill);
	data->txpower.value = val;
	data->txpower.flags = IW_TXPOW_DBM;

	return 0;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwtxpower);

static int cfg80211_set_auth_alg(struct wireless_dev *wdev,
				 s32 auth_alg)
{
	int nr_alg = 0;

	if (!auth_alg)
		return -EINVAL;

	if (auth_alg & ~(IW_AUTH_ALG_OPEN_SYSTEM |
			 IW_AUTH_ALG_SHARED_KEY |
			 IW_AUTH_ALG_LEAP))
		return -EINVAL;

	if (auth_alg & IW_AUTH_ALG_OPEN_SYSTEM) {
		nr_alg++;
		wdev->wext.connect.auth_type = NL80211_AUTHTYPE_OPEN_SYSTEM;
	}

	if (auth_alg & IW_AUTH_ALG_SHARED_KEY) {
		nr_alg++;
		wdev->wext.connect.auth_type = NL80211_AUTHTYPE_SHARED_KEY;
	}

	if (auth_alg & IW_AUTH_ALG_LEAP) {
		nr_alg++;
		wdev->wext.connect.auth_type = NL80211_AUTHTYPE_NETWORK_EAP;
	}

	if (nr_alg > 1)
		wdev->wext.connect.auth_type = NL80211_AUTHTYPE_AUTOMATIC;

	return 0;
}

static int cfg80211_set_wpa_version(struct wireless_dev *wdev, u32 wpa_versions)
{
	if (wpa_versions & ~(IW_AUTH_WPA_VERSION_WPA |
			     IW_AUTH_WPA_VERSION_WPA2|
		             IW_AUTH_WPA_VERSION_DISABLED))
		return -EINVAL;

	if ((wpa_versions & IW_AUTH_WPA_VERSION_DISABLED) &&
	    (wpa_versions & (IW_AUTH_WPA_VERSION_WPA|
			     IW_AUTH_WPA_VERSION_WPA2)))
		return -EINVAL;

	if (wpa_versions & IW_AUTH_WPA_VERSION_DISABLED)
		wdev->wext.connect.crypto.wpa_versions &=
			~(NL80211_WPA_VERSION_1|NL80211_WPA_VERSION_2);

	if (wpa_versions & IW_AUTH_WPA_VERSION_WPA)
		wdev->wext.connect.crypto.wpa_versions |=
			NL80211_WPA_VERSION_1;

	if (wpa_versions & IW_AUTH_WPA_VERSION_WPA2)
		wdev->wext.connect.crypto.wpa_versions |=
			NL80211_WPA_VERSION_2;

	return 0;
}

static int cfg80211_set_cipher_group(struct wireless_dev *wdev, u32 cipher)
{
	if (cipher & IW_AUTH_CIPHER_WEP40)
		wdev->wext.connect.crypto.cipher_group =
			WLAN_CIPHER_SUITE_WEP40;
	else if (cipher & IW_AUTH_CIPHER_WEP104)
		wdev->wext.connect.crypto.cipher_group =
			WLAN_CIPHER_SUITE_WEP104;
	else if (cipher & IW_AUTH_CIPHER_TKIP)
		wdev->wext.connect.crypto.cipher_group =
			WLAN_CIPHER_SUITE_TKIP;
	else if (cipher & IW_AUTH_CIPHER_CCMP)
		wdev->wext.connect.crypto.cipher_group =
			WLAN_CIPHER_SUITE_CCMP;
	else if (cipher & IW_AUTH_CIPHER_AES_CMAC)
		wdev->wext.connect.crypto.cipher_group =
			WLAN_CIPHER_SUITE_AES_CMAC;
	else if (cipher & IW_AUTH_CIPHER_NONE)
		wdev->wext.connect.crypto.cipher_group = 0;
	else
		return -EINVAL;

	return 0;
}

static int cfg80211_set_cipher_pairwise(struct wireless_dev *wdev, u32 cipher)
{
	int nr_ciphers = 0;
	u32 *ciphers_pairwise = wdev->wext.connect.crypto.ciphers_pairwise;

	if (cipher & IW_AUTH_CIPHER_WEP40) {
		ciphers_pairwise[nr_ciphers] = WLAN_CIPHER_SUITE_WEP40;
		nr_ciphers++;
	}

	if (cipher & IW_AUTH_CIPHER_WEP104) {
		ciphers_pairwise[nr_ciphers] = WLAN_CIPHER_SUITE_WEP104;
		nr_ciphers++;
	}

	if (cipher & IW_AUTH_CIPHER_TKIP) {
		ciphers_pairwise[nr_ciphers] = WLAN_CIPHER_SUITE_TKIP;
		nr_ciphers++;
	}

	if (cipher & IW_AUTH_CIPHER_CCMP) {
		ciphers_pairwise[nr_ciphers] = WLAN_CIPHER_SUITE_CCMP;
		nr_ciphers++;
	}

	if (cipher & IW_AUTH_CIPHER_AES_CMAC) {
		ciphers_pairwise[nr_ciphers] = WLAN_CIPHER_SUITE_AES_CMAC;
		nr_ciphers++;
	}

	BUILD_BUG_ON(NL80211_MAX_NR_CIPHER_SUITES < 5);

	wdev->wext.connect.crypto.n_ciphers_pairwise = nr_ciphers;

	return 0;
}


static int cfg80211_set_key_mgt(struct wireless_dev *wdev, u32 key_mgt)
{
	int nr_akm_suites = 0;

	if (key_mgt & ~(IW_AUTH_KEY_MGMT_802_1X |
			IW_AUTH_KEY_MGMT_PSK))
		return -EINVAL;

	if (key_mgt & IW_AUTH_KEY_MGMT_802_1X) {
		wdev->wext.connect.crypto.akm_suites[nr_akm_suites] =
			WLAN_AKM_SUITE_8021X;
		nr_akm_suites++;
	}

	if (key_mgt & IW_AUTH_KEY_MGMT_PSK) {
		wdev->wext.connect.crypto.akm_suites[nr_akm_suites] =
			WLAN_AKM_SUITE_PSK;
		nr_akm_suites++;
	}

	wdev->wext.connect.crypto.n_akm_suites = nr_akm_suites;

	return 0;
}

int cfg80211_wext_siwauth(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *data, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	if (wdev->iftype != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	switch (data->flags & IW_AUTH_INDEX) {
	case IW_AUTH_PRIVACY_INVOKED:
		wdev->wext.connect.privacy = data->value;
		return 0;
	case IW_AUTH_WPA_VERSION:
		return cfg80211_set_wpa_version(wdev, data->value);
	case IW_AUTH_CIPHER_GROUP:
		return cfg80211_set_cipher_group(wdev, data->value);
	case IW_AUTH_KEY_MGMT:
		return cfg80211_set_key_mgt(wdev, data->value);
	case IW_AUTH_CIPHER_PAIRWISE:
		return cfg80211_set_cipher_pairwise(wdev, data->value);
	case IW_AUTH_80211_AUTH_ALG:
		return cfg80211_set_auth_alg(wdev, data->value);
	case IW_AUTH_WPA_ENABLED:
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
	case IW_AUTH_DROP_UNENCRYPTED:
	case IW_AUTH_MFP:
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwauth);

int cfg80211_wext_giwauth(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *data, char *extra)
{
	/* XXX: what do we need? */

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwauth);

int cfg80211_wext_siwpower(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *wrq, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	bool ps = wdev->wext.ps;
	int timeout = wdev->wext.ps_timeout;
	int err;

	if (wdev->iftype != NL80211_IFTYPE_STATION)
		return -EINVAL;

	if (!rdev->ops->set_power_mgmt)
		return -EOPNOTSUPP;

	if (wrq->disabled) {
		ps = false;
	} else {
		switch (wrq->flags & IW_POWER_MODE) {
		case IW_POWER_ON:       /* If not specified */
		case IW_POWER_MODE:     /* If set all mask */
		case IW_POWER_ALL_R:    /* If explicitely state all */
			ps = true;
			break;
		default:                /* Otherwise we ignore */
			return -EINVAL;
		}

		if (wrq->flags & ~(IW_POWER_MODE | IW_POWER_TIMEOUT))
			return -EINVAL;

		if (wrq->flags & IW_POWER_TIMEOUT)
			timeout = wrq->value / 1000;
	}

	err = rdev->ops->set_power_mgmt(wdev->wiphy, dev, ps, timeout);
	if (err)
		return err;

	wdev->wext.ps = ps;
	wdev->wext.ps_timeout = timeout;

	return 0;

}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwpower);

int cfg80211_wext_giwpower(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *wrq, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	wrq->disabled = !wdev->wext.ps;

	return 0;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwpower);

static int cfg80211_wds_wext_siwap(struct net_device *dev,
				   struct iw_request_info *info,
				   struct sockaddr *addr, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	int err;

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_WDS))
		return -EINVAL;

	if (addr->sa_family != ARPHRD_ETHER)
		return -EINVAL;

	if (netif_running(dev))
		return -EBUSY;

	if (!rdev->ops->set_wds_peer)
		return -EOPNOTSUPP;

	err = rdev->ops->set_wds_peer(wdev->wiphy, dev, (u8 *) &addr->sa_data);
	if (err)
		return err;

	memcpy(&wdev->wext.bssid, (u8 *) &addr->sa_data, ETH_ALEN);

	return 0;
}

static int cfg80211_wds_wext_giwap(struct net_device *dev,
				   struct iw_request_info *info,
				   struct sockaddr *addr, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_WDS))
		return -EINVAL;

	addr->sa_family = ARPHRD_ETHER;
	memcpy(&addr->sa_data, wdev->wext.bssid, ETH_ALEN);

	return 0;
}

int cfg80211_wext_siwrate(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *rate, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	struct cfg80211_bitrate_mask mask;

	if (!rdev->ops->set_bitrate_mask)
		return -EOPNOTSUPP;

	mask.fixed = 0;
	mask.maxrate = 0;

	if (rate->value < 0) {
		/* nothing */
	} else if (rate->fixed) {
		mask.fixed = rate->value / 1000; /* kbps */
	} else {
		mask.maxrate = rate->value / 1000; /* kbps */
	}

	return rdev->ops->set_bitrate_mask(wdev->wiphy, dev, NULL, &mask);
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwrate);

int cfg80211_wext_giwrate(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *rate, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	/* we are under RTNL - globally locked - so can use a static struct */
	static struct station_info sinfo;
	u8 addr[ETH_ALEN];
	int err;

	if (wdev->iftype != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	if (!rdev->ops->get_station)
		return -EOPNOTSUPP;

	err = 0;
	wdev_lock(wdev);
	if (wdev->current_bss)
		memcpy(addr, wdev->current_bss->pub.bssid, ETH_ALEN);
	else
		err = -EOPNOTSUPP;
	wdev_unlock(wdev);
	if (err)
		return err;

	err = rdev->ops->get_station(&rdev->wiphy, dev, addr, &sinfo);
	if (err)
		return err;

	if (!(sinfo.filled & STATION_INFO_TX_BITRATE))
		return -EOPNOTSUPP;

	rate->value = 0;

	if (!(sinfo.txrate.flags & RATE_INFO_FLAGS_MCS))
		rate->value = 100000 * sinfo.txrate.legacy;

	return 0;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwrate);

/* Get wireless statistics.  Called by /proc/net/wireless and by SIOCGIWSTATS */
struct iw_statistics *cfg80211_wireless_stats(struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	/* we are under RTNL - globally locked - so can use static structs */
	static struct iw_statistics wstats;
	static struct station_info sinfo;
	u8 bssid[ETH_ALEN];

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION)
		return NULL;

	if (!rdev->ops->get_station)
		return NULL;

	/* Grab BSSID of current BSS, if any */
	wdev_lock(wdev);
	if (!wdev->current_bss) {
		wdev_unlock(wdev);
		return NULL;
	}
	memcpy(bssid, wdev->current_bss->pub.bssid, ETH_ALEN);
	wdev_unlock(wdev);

	if (rdev->ops->get_station(&rdev->wiphy, dev, bssid, &sinfo))
		return NULL;

	memset(&wstats, 0, sizeof(wstats));

	switch (rdev->wiphy.signal_type) {
	case CFG80211_SIGNAL_TYPE_MBM:
		if (sinfo.filled & STATION_INFO_SIGNAL) {
			int sig = sinfo.signal;
			wstats.qual.updated |= IW_QUAL_LEVEL_UPDATED;
			wstats.qual.updated |= IW_QUAL_QUAL_UPDATED;
			wstats.qual.updated |= IW_QUAL_DBM;
			wstats.qual.level = sig;
			if (sig < -110)
				sig = -110;
			else if (sig > -40)
				sig = -40;
			wstats.qual.qual = sig + 110;
			break;
		}
	case CFG80211_SIGNAL_TYPE_UNSPEC:
		if (sinfo.filled & STATION_INFO_SIGNAL) {
			wstats.qual.updated |= IW_QUAL_LEVEL_UPDATED;
			wstats.qual.updated |= IW_QUAL_QUAL_UPDATED;
			wstats.qual.level = sinfo.signal;
			wstats.qual.qual = sinfo.signal;
			break;
		}
	default:
		wstats.qual.updated |= IW_QUAL_LEVEL_INVALID;
		wstats.qual.updated |= IW_QUAL_QUAL_INVALID;
	}

	wstats.qual.updated |= IW_QUAL_NOISE_INVALID;

	return &wstats;
}
EXPORT_SYMBOL_GPL(cfg80211_wireless_stats);

int cfg80211_wext_siwap(struct net_device *dev,
			struct iw_request_info *info,
			struct sockaddr *ap_addr, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_ADHOC:
		return cfg80211_ibss_wext_siwap(dev, info, ap_addr, extra);
	case NL80211_IFTYPE_STATION:
		return cfg80211_mgd_wext_siwap(dev, info, ap_addr, extra);
	case NL80211_IFTYPE_WDS:
		return cfg80211_wds_wext_siwap(dev, info, ap_addr, extra);
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwap);

int cfg80211_wext_giwap(struct net_device *dev,
			struct iw_request_info *info,
			struct sockaddr *ap_addr, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_ADHOC:
		return cfg80211_ibss_wext_giwap(dev, info, ap_addr, extra);
	case NL80211_IFTYPE_STATION:
		return cfg80211_mgd_wext_giwap(dev, info, ap_addr, extra);
	case NL80211_IFTYPE_WDS:
		return cfg80211_wds_wext_giwap(dev, info, ap_addr, extra);
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwap);

int cfg80211_wext_siwessid(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *ssid)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_ADHOC:
		return cfg80211_ibss_wext_siwessid(dev, info, data, ssid);
	case NL80211_IFTYPE_STATION:
		return cfg80211_mgd_wext_siwessid(dev, info, data, ssid);
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwessid);

int cfg80211_wext_giwessid(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *ssid)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_ADHOC:
		return cfg80211_ibss_wext_giwessid(dev, info, data, ssid);
	case NL80211_IFTYPE_STATION:
		return cfg80211_mgd_wext_giwessid(dev, info, data, ssid);
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwessid);

static const iw_handler cfg80211_handlers[] = {
	[IW_IOCTL_IDX(SIOCGIWNAME)]	= (iw_handler) cfg80211_wext_giwname,
	[IW_IOCTL_IDX(SIOCSIWFREQ)]	= (iw_handler) cfg80211_wext_siwfreq,
	[IW_IOCTL_IDX(SIOCGIWFREQ)]	= (iw_handler) cfg80211_wext_giwfreq,
	[IW_IOCTL_IDX(SIOCSIWMODE)]	= (iw_handler) cfg80211_wext_siwmode,
	[IW_IOCTL_IDX(SIOCGIWMODE)]	= (iw_handler) cfg80211_wext_giwmode,
	[IW_IOCTL_IDX(SIOCGIWRANGE)]	= (iw_handler) cfg80211_wext_giwrange,
	[IW_IOCTL_IDX(SIOCSIWAP)]	= (iw_handler) cfg80211_wext_siwap,
	[IW_IOCTL_IDX(SIOCGIWAP)]	= (iw_handler) cfg80211_wext_giwap,
	[IW_IOCTL_IDX(SIOCSIWMLME)]	= (iw_handler) cfg80211_wext_siwmlme,
	[IW_IOCTL_IDX(SIOCSIWSCAN)]	= (iw_handler) cfg80211_wext_siwscan,
	[IW_IOCTL_IDX(SIOCGIWSCAN)]	= (iw_handler) cfg80211_wext_giwscan,
	[IW_IOCTL_IDX(SIOCSIWESSID)]	= (iw_handler) cfg80211_wext_siwessid,
	[IW_IOCTL_IDX(SIOCGIWESSID)]	= (iw_handler) cfg80211_wext_giwessid,
	[IW_IOCTL_IDX(SIOCSIWRATE)]	= (iw_handler) cfg80211_wext_siwrate,
	[IW_IOCTL_IDX(SIOCGIWRATE)]	= (iw_handler) cfg80211_wext_giwrate,
	[IW_IOCTL_IDX(SIOCSIWRTS)]	= (iw_handler) cfg80211_wext_siwrts,
	[IW_IOCTL_IDX(SIOCGIWRTS)]	= (iw_handler) cfg80211_wext_giwrts,
	[IW_IOCTL_IDX(SIOCSIWFRAG)]	= (iw_handler) cfg80211_wext_siwfrag,
	[IW_IOCTL_IDX(SIOCGIWFRAG)]	= (iw_handler) cfg80211_wext_giwfrag,
	[IW_IOCTL_IDX(SIOCSIWTXPOW)]	= (iw_handler) cfg80211_wext_siwtxpower,
	[IW_IOCTL_IDX(SIOCGIWTXPOW)]	= (iw_handler) cfg80211_wext_giwtxpower,
	[IW_IOCTL_IDX(SIOCSIWRETRY)]	= (iw_handler) cfg80211_wext_siwretry,
	[IW_IOCTL_IDX(SIOCGIWRETRY)]	= (iw_handler) cfg80211_wext_giwretry,
	[IW_IOCTL_IDX(SIOCSIWENCODE)]	= (iw_handler) cfg80211_wext_siwencode,
	[IW_IOCTL_IDX(SIOCGIWENCODE)]	= (iw_handler) cfg80211_wext_giwencode,
	[IW_IOCTL_IDX(SIOCSIWPOWER)]	= (iw_handler) cfg80211_wext_siwpower,
	[IW_IOCTL_IDX(SIOCGIWPOWER)]	= (iw_handler) cfg80211_wext_giwpower,
	[IW_IOCTL_IDX(SIOCSIWGENIE)]	= (iw_handler) cfg80211_wext_siwgenie,
	[IW_IOCTL_IDX(SIOCSIWAUTH)]	= (iw_handler) cfg80211_wext_siwauth,
	[IW_IOCTL_IDX(SIOCGIWAUTH)]	= (iw_handler) cfg80211_wext_giwauth,
	[IW_IOCTL_IDX(SIOCSIWENCODEEXT)]= (iw_handler) cfg80211_wext_siwencodeext,
};

const struct iw_handler_def cfg80211_wext_handler = {
	.num_standard		= ARRAY_SIZE(cfg80211_handlers),
	.standard		= cfg80211_handlers,
	.get_wireless_stats = cfg80211_wireless_stats,
};
