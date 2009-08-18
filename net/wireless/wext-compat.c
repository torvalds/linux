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

	if (!wdev)
		return -EOPNOTSUPP;

	rdev = wiphy_to_dev(wdev->wiphy);

	if (!rdev->ops->change_virtual_intf)
		return -EOPNOTSUPP;

	/* don't support changing VLANs, you just re-create them */
	if (wdev->iftype == NL80211_IFTYPE_AP_VLAN)
		return -EOPNOTSUPP;

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

	ret = rdev->ops->change_virtual_intf(wdev->wiphy, dev->ifindex, type,
					     NULL, &vifparams);
	WARN_ON(!ret && wdev->iftype != type);

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
	int c = 0;

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

	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;
	range->num_encoding_sizes = 2;
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

	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
			  IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;

	for (band = 0; band < IEEE80211_NUM_BANDS; band ++) {
		int i;
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

	range->scan_capa |= IW_SCAN_CAPA_ESSID;

	return 0;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwrange);

int cfg80211_wext_siwmlme(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *data, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct iw_mlme *mlme = (struct iw_mlme *)extra;
	struct cfg80211_registered_device *rdev;
	union {
		struct cfg80211_disassoc_request disassoc;
		struct cfg80211_deauth_request deauth;
	} cmd;

	if (!wdev)
		return -EOPNOTSUPP;

	rdev = wiphy_to_dev(wdev->wiphy);

	if (wdev->iftype != NL80211_IFTYPE_STATION)
		return -EINVAL;

	if (mlme->addr.sa_family != ARPHRD_ETHER)
		return -EINVAL;

	memset(&cmd, 0, sizeof(cmd));

	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		if (!rdev->ops->deauth)
			return -EOPNOTSUPP;
		cmd.deauth.peer_addr = mlme->addr.sa_data;
		cmd.deauth.reason_code = mlme->reason_code;
		return rdev->ops->deauth(wdev->wiphy, dev, &cmd.deauth);
	case IW_MLME_DISASSOC:
		if (!rdev->ops->disassoc)
			return -EOPNOTSUPP;
		cmd.disassoc.peer_addr = mlme->addr.sa_data;
		cmd.disassoc.reason_code = mlme->reason_code;
		return rdev->ops->disassoc(wdev->wiphy, dev, &cmd.disassoc);
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwmlme);


/**
 * cfg80211_wext_freq - get wext frequency for non-"auto"
 * @wiphy: the wiphy
 * @freq: the wext freq encoding
 *
 * Returns a channel, %NULL for auto, or an ERR_PTR for errors!
 */
struct ieee80211_channel *cfg80211_wext_freq(struct wiphy *wiphy,
					     struct iw_freq *freq)
{
	struct ieee80211_channel *chan;
	int f;

	/*
	 * Parse frequency - return NULL for auto and
	 * -EINVAL for impossible things.
	 */
	if (freq->e == 0) {
		if (freq->m < 0)
			return NULL;
		f = ieee80211_channel_to_frequency(freq->m);
	} else {
		int i, div = 1000000;
		for (i = 0; i < freq->e; i++)
			div /= 10;
		if (div <= 0)
			return ERR_PTR(-EINVAL);
		f = freq->m / div;
	}

	/*
	 * Look up channel struct and return -EINVAL when
	 * it cannot be found.
	 */
	chan = ieee80211_get_channel(wiphy, f);
	if (!chan)
		return ERR_PTR(-EINVAL);
	return chan;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_freq);

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

static int cfg80211_set_encryption(struct cfg80211_registered_device *rdev,
				   struct net_device *dev, const u8 *addr,
				   bool remove, bool tx_key, int idx,
				   struct key_params *params)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	if (params->cipher == WLAN_CIPHER_SUITE_AES_CMAC) {
		if (!rdev->ops->set_default_mgmt_key)
			return -EOPNOTSUPP;

		if (idx < 4 || idx > 5)
			return -EINVAL;
	} else if (idx < 0 || idx > 3)
		return -EINVAL;

	if (remove) {
		err = rdev->ops->del_key(&rdev->wiphy, dev, idx, addr);
		if (!err) {
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
	} else {
		if (addr)
			tx_key = false;

		if (cfg80211_validate_key_settings(params, idx, addr))
			return -EINVAL;

		err = rdev->ops->add_key(&rdev->wiphy, dev, idx, addr, params);
		if (err)
			return err;

		if (tx_key || (!addr && wdev->wext.default_key == -1)) {
			err = rdev->ops->set_default_key(&rdev->wiphy,
							 dev, idx);
			if (!err)
				wdev->wext.default_key = idx;
			return err;
		}

		if (params->cipher == WLAN_CIPHER_SUITE_AES_CMAC &&
		    (tx_key || (!addr && wdev->wext.default_mgmt_key == -1))) {
			err = rdev->ops->set_default_mgmt_key(&rdev->wiphy,
							      dev, idx);
			if (!err)
				wdev->wext.default_mgmt_key = idx;
			return err;
		}

		return 0;
	}
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
		err = rdev->ops->set_default_key(&rdev->wiphy, dev, idx);
		if (!err)
			wdev->wext.default_key = idx;
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

struct giwencode_cookie {
	size_t buflen;
	char *keybuf;
};

static void giwencode_get_key_cb(void *cookie, struct key_params *params)
{
	struct giwencode_cookie *data = cookie;

	if (!params->key) {
		data->buflen = 0;
		return;
	}

	data->buflen = min_t(size_t, data->buflen, params->key_len);
	memcpy(data->keybuf, params->key, data->buflen);
}

int cfg80211_wext_giwencode(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_point *erq, char *keybuf)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	int idx, err;
	struct giwencode_cookie data = {
		.keybuf = keybuf,
		.buflen = erq->length,
	};

	if (!rdev->ops->get_key)
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

	err = rdev->ops->get_key(&rdev->wiphy, dev, idx, NULL, &data,
				 giwencode_get_key_cb);
	if (!err) {
		erq->length = data.buflen;
		erq->flags |= IW_ENCODE_ENABLED;
		return 0;
	}

	if (err == -ENOENT) {
		erq->flags |= IW_ENCODE_DISABLED;
		erq->length = 0;
		return 0;
	}

	return err;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwencode);

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

	return rdev->ops->set_tx_power(wdev->wiphy, type, dbm);;
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
