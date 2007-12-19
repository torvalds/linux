/*
 * mac80211 configuration hooks for cfg80211
 *
 * Copyright 2006, 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This file is GPLv2 as found in COPYING.
 */

#include <linux/ieee80211.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "cfg.h"

static enum ieee80211_if_types
nl80211_type_to_mac80211_type(enum nl80211_iftype type)
{
	switch (type) {
	case NL80211_IFTYPE_UNSPECIFIED:
		return IEEE80211_IF_TYPE_STA;
	case NL80211_IFTYPE_ADHOC:
		return IEEE80211_IF_TYPE_IBSS;
	case NL80211_IFTYPE_STATION:
		return IEEE80211_IF_TYPE_STA;
	case NL80211_IFTYPE_MONITOR:
		return IEEE80211_IF_TYPE_MNTR;
	default:
		return IEEE80211_IF_TYPE_INVALID;
	}
}

static int ieee80211_add_iface(struct wiphy *wiphy, char *name,
			       enum nl80211_iftype type)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	enum ieee80211_if_types itype;

	if (unlikely(local->reg_state != IEEE80211_DEV_REGISTERED))
		return -ENODEV;

	itype = nl80211_type_to_mac80211_type(type);
	if (itype == IEEE80211_IF_TYPE_INVALID)
		return -EINVAL;

	return ieee80211_if_add(local->mdev, name, NULL, itype);
}

static int ieee80211_del_iface(struct wiphy *wiphy, int ifindex)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct net_device *dev;
	char *name;

	if (unlikely(local->reg_state != IEEE80211_DEV_REGISTERED))
		return -ENODEV;

	/* we're under RTNL */
	dev = __dev_get_by_index(&init_net, ifindex);
	if (!dev)
		return 0;

	name = dev->name;

	return ieee80211_if_remove(local->mdev, name, -1);
}

static int ieee80211_change_iface(struct wiphy *wiphy, int ifindex,
				  enum nl80211_iftype type)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct net_device *dev;
	enum ieee80211_if_types itype;
	struct ieee80211_sub_if_data *sdata;

	if (unlikely(local->reg_state != IEEE80211_DEV_REGISTERED))
		return -ENODEV;

	/* we're under RTNL */
	dev = __dev_get_by_index(&init_net, ifindex);
	if (!dev)
		return -ENODEV;

	if (netif_running(dev))
		return -EBUSY;

	itype = nl80211_type_to_mac80211_type(type);
	if (itype == IEEE80211_IF_TYPE_INVALID)
		return -EINVAL;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

        if (sdata->type == IEEE80211_IF_TYPE_VLAN)
		return -EOPNOTSUPP;

	ieee80211_if_reinit(dev);
	ieee80211_if_set_type(dev, itype);

	return 0;
}

static int ieee80211_add_key(struct wiphy *wiphy, struct net_device *dev,
			     u8 key_idx, u8 *mac_addr,
			     struct key_params *params)
{
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta = NULL;
	enum ieee80211_key_alg alg;
	int ret;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		alg = ALG_WEP;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		alg = ALG_TKIP;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		alg = ALG_CCMP;
		break;
	default:
		return -EINVAL;
	}

	if (mac_addr) {
		sta = sta_info_get(sdata->local, mac_addr);
		if (!sta)
			return -ENOENT;
	}

	ret = 0;
	if (!ieee80211_key_alloc(sdata, sta, alg, key_idx,
				 params->key_len, params->key))
		ret = -ENOMEM;

	if (sta)
		sta_info_put(sta);

	return ret;
}

static int ieee80211_del_key(struct wiphy *wiphy, struct net_device *dev,
			     u8 key_idx, u8 *mac_addr)
{
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	int ret;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (mac_addr) {
		sta = sta_info_get(sdata->local, mac_addr);
		if (!sta)
			return -ENOENT;

		ret = 0;
		if (sta->key)
			ieee80211_key_free(sta->key);
		else
			ret = -ENOENT;

		sta_info_put(sta);
		return ret;
	}

	if (!sdata->keys[key_idx])
		return -ENOENT;

	ieee80211_key_free(sdata->keys[key_idx]);

	return 0;
}

static int ieee80211_get_key(struct wiphy *wiphy, struct net_device *dev,
			     u8 key_idx, u8 *mac_addr, void *cookie,
			     void (*callback)(void *cookie,
					      struct key_params *params))
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sta_info *sta = NULL;
	u8 seq[6] = {0};
	struct key_params params;
	struct ieee80211_key *key;
	u32 iv32;
	u16 iv16;
	int err = -ENOENT;

	if (mac_addr) {
		sta = sta_info_get(sdata->local, mac_addr);
		if (!sta)
			goto out;

		key = sta->key;
	} else
		key = sdata->keys[key_idx];

	if (!key)
		goto out;

	memset(&params, 0, sizeof(params));

	switch (key->conf.alg) {
	case ALG_TKIP:
		params.cipher = WLAN_CIPHER_SUITE_TKIP;

		iv32 = key->u.tkip.iv32;
		iv16 = key->u.tkip.iv16;

		if (key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE &&
		    sdata->local->ops->get_tkip_seq)
			sdata->local->ops->get_tkip_seq(
				local_to_hw(sdata->local),
				key->conf.hw_key_idx,
				&iv32, &iv16);

		seq[0] = iv16 & 0xff;
		seq[1] = (iv16 >> 8) & 0xff;
		seq[2] = iv32 & 0xff;
		seq[3] = (iv32 >> 8) & 0xff;
		seq[4] = (iv32 >> 16) & 0xff;
		seq[5] = (iv32 >> 24) & 0xff;
		params.seq = seq;
		params.seq_len = 6;
		break;
	case ALG_CCMP:
		params.cipher = WLAN_CIPHER_SUITE_CCMP;
		seq[0] = key->u.ccmp.tx_pn[5];
		seq[1] = key->u.ccmp.tx_pn[4];
		seq[2] = key->u.ccmp.tx_pn[3];
		seq[3] = key->u.ccmp.tx_pn[2];
		seq[4] = key->u.ccmp.tx_pn[1];
		seq[5] = key->u.ccmp.tx_pn[0];
		params.seq = seq;
		params.seq_len = 6;
		break;
	case ALG_WEP:
		if (key->conf.keylen == 5)
			params.cipher = WLAN_CIPHER_SUITE_WEP40;
		else
			params.cipher = WLAN_CIPHER_SUITE_WEP104;
		break;
	}

	params.key = key->conf.key;
	params.key_len = key->conf.keylen;

	callback(cookie, &params);
	err = 0;

 out:
	if (sta)
		sta_info_put(sta);
	return err;
}

static int ieee80211_config_default_key(struct wiphy *wiphy,
					struct net_device *dev,
					u8 key_idx)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	ieee80211_set_default_key(sdata, key_idx);

	return 0;
}

struct cfg80211_ops mac80211_config_ops = {
	.add_virtual_intf = ieee80211_add_iface,
	.del_virtual_intf = ieee80211_del_iface,
	.change_virtual_intf = ieee80211_change_iface,
	.add_key = ieee80211_add_key,
	.del_key = ieee80211_del_key,
	.get_key = ieee80211_get_key,
	.set_default_key = ieee80211_config_default_key,
};
