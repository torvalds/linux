// SPDX-License-Identifier: GPL-2.0-only
/*
 * mac80211 configuration hooks for cfg80211
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2015  Intel Mobile Communications GmbH
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2020 Intel Corporation
 */

#include <linux/ieee80211.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <net/net_namespace.h>
#include <linux/rcupdate.h>
#include <linux/fips.h>
#include <linux/if_ether.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "mesh.h"
#include "wme.h"

static void ieee80211_set_mu_mimo_follow(struct ieee80211_sub_if_data *sdata,
					 struct vif_params *params)
{
	bool mu_mimo_groups = false;
	bool mu_mimo_follow = false;

	if (params->vht_mumimo_groups) {
		u64 membership;

		BUILD_BUG_ON(sizeof(membership) != WLAN_MEMBERSHIP_LEN);

		memcpy(sdata->vif.bss_conf.mu_group.membership,
		       params->vht_mumimo_groups, WLAN_MEMBERSHIP_LEN);
		memcpy(sdata->vif.bss_conf.mu_group.position,
		       params->vht_mumimo_groups + WLAN_MEMBERSHIP_LEN,
		       WLAN_USER_POSITION_LEN);
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_MU_GROUPS);
		/* don't care about endianness - just check for 0 */
		memcpy(&membership, params->vht_mumimo_groups,
		       WLAN_MEMBERSHIP_LEN);
		mu_mimo_groups = membership != 0;
	}

	if (params->vht_mumimo_follow_addr) {
		mu_mimo_follow =
			is_valid_ether_addr(params->vht_mumimo_follow_addr);
		ether_addr_copy(sdata->u.mntr.mu_follow_addr,
				params->vht_mumimo_follow_addr);
	}

	sdata->vif.mu_mimo_owner = mu_mimo_groups || mu_mimo_follow;
}

static int ieee80211_set_mon_options(struct ieee80211_sub_if_data *sdata,
				     struct vif_params *params)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_sub_if_data *monitor_sdata;

	/* check flags first */
	if (params->flags && ieee80211_sdata_running(sdata)) {
		u32 mask = MONITOR_FLAG_COOK_FRAMES | MONITOR_FLAG_ACTIVE;

		/*
		 * Prohibit MONITOR_FLAG_COOK_FRAMES and
		 * MONITOR_FLAG_ACTIVE to be changed while the
		 * interface is up.
		 * Else we would need to add a lot of cruft
		 * to update everything:
		 *	cooked_mntrs, monitor and all fif_* counters
		 *	reconfigure hardware
		 */
		if ((params->flags & mask) != (sdata->u.mntr.flags & mask))
			return -EBUSY;
	}

	/* also validate MU-MIMO change */
	monitor_sdata = rtnl_dereference(local->monitor_sdata);

	if (!monitor_sdata &&
	    (params->vht_mumimo_groups || params->vht_mumimo_follow_addr))
		return -EOPNOTSUPP;

	/* apply all changes now - no failures allowed */

	if (monitor_sdata)
		ieee80211_set_mu_mimo_follow(monitor_sdata, params);

	if (params->flags) {
		if (ieee80211_sdata_running(sdata)) {
			ieee80211_adjust_monitor_flags(sdata, -1);
			sdata->u.mntr.flags = params->flags;
			ieee80211_adjust_monitor_flags(sdata, 1);

			ieee80211_configure_filter(local);
		} else {
			/*
			 * Because the interface is down, ieee80211_do_stop
			 * and ieee80211_do_open take care of "everything"
			 * mentioned in the comment above.
			 */
			sdata->u.mntr.flags = params->flags;
		}
	}

	return 0;
}

static struct wireless_dev *ieee80211_add_iface(struct wiphy *wiphy,
						const char *name,
						unsigned char name_assign_type,
						enum nl80211_iftype type,
						struct vif_params *params)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct wireless_dev *wdev;
	struct ieee80211_sub_if_data *sdata;
	int err;

	err = ieee80211_if_add(local, name, name_assign_type, &wdev, type, params);
	if (err)
		return ERR_PTR(err);

	sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);

	if (type == NL80211_IFTYPE_MONITOR) {
		err = ieee80211_set_mon_options(sdata, params);
		if (err) {
			ieee80211_if_remove(sdata);
			return NULL;
		}
	}

	return wdev;
}

static int ieee80211_del_iface(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	ieee80211_if_remove(IEEE80211_WDEV_TO_SUB_IF(wdev));

	return 0;
}

static int ieee80211_change_iface(struct wiphy *wiphy,
				  struct net_device *dev,
				  enum nl80211_iftype type,
				  struct vif_params *params)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int ret;

	ret = ieee80211_if_change_type(sdata, type);
	if (ret)
		return ret;

	if (type == NL80211_IFTYPE_AP_VLAN && params->use_4addr == 0) {
		RCU_INIT_POINTER(sdata->u.vlan.sta, NULL);
		ieee80211_check_fast_rx_iface(sdata);
	} else if (type == NL80211_IFTYPE_STATION && params->use_4addr >= 0) {
		sdata->u.mgd.use_4addr = params->use_4addr;
	}

	if (sdata->vif.type == NL80211_IFTYPE_MONITOR) {
		ret = ieee80211_set_mon_options(sdata, params);
		if (ret)
			return ret;
	}

	return 0;
}

static int ieee80211_start_p2p_device(struct wiphy *wiphy,
				      struct wireless_dev *wdev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	int ret;

	mutex_lock(&sdata->local->chanctx_mtx);
	ret = ieee80211_check_combinations(sdata, NULL, 0, 0);
	mutex_unlock(&sdata->local->chanctx_mtx);
	if (ret < 0)
		return ret;

	return ieee80211_do_open(wdev, true);
}

static void ieee80211_stop_p2p_device(struct wiphy *wiphy,
				      struct wireless_dev *wdev)
{
	ieee80211_sdata_stop(IEEE80211_WDEV_TO_SUB_IF(wdev));
}

static int ieee80211_start_nan(struct wiphy *wiphy,
			       struct wireless_dev *wdev,
			       struct cfg80211_nan_conf *conf)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	int ret;

	mutex_lock(&sdata->local->chanctx_mtx);
	ret = ieee80211_check_combinations(sdata, NULL, 0, 0);
	mutex_unlock(&sdata->local->chanctx_mtx);
	if (ret < 0)
		return ret;

	ret = ieee80211_do_open(wdev, true);
	if (ret)
		return ret;

	ret = drv_start_nan(sdata->local, sdata, conf);
	if (ret)
		ieee80211_sdata_stop(sdata);

	sdata->u.nan.conf = *conf;

	return ret;
}

static void ieee80211_stop_nan(struct wiphy *wiphy,
			       struct wireless_dev *wdev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);

	drv_stop_nan(sdata->local, sdata);
	ieee80211_sdata_stop(sdata);
}

static int ieee80211_nan_change_conf(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     struct cfg80211_nan_conf *conf,
				     u32 changes)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	struct cfg80211_nan_conf new_conf;
	int ret = 0;

	if (sdata->vif.type != NL80211_IFTYPE_NAN)
		return -EOPNOTSUPP;

	if (!ieee80211_sdata_running(sdata))
		return -ENETDOWN;

	new_conf = sdata->u.nan.conf;

	if (changes & CFG80211_NAN_CONF_CHANGED_PREF)
		new_conf.master_pref = conf->master_pref;

	if (changes & CFG80211_NAN_CONF_CHANGED_BANDS)
		new_conf.bands = conf->bands;

	ret = drv_nan_change_conf(sdata->local, sdata, &new_conf, changes);
	if (!ret)
		sdata->u.nan.conf = new_conf;

	return ret;
}

static int ieee80211_add_nan_func(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  struct cfg80211_nan_func *nan_func)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	int ret;

	if (sdata->vif.type != NL80211_IFTYPE_NAN)
		return -EOPNOTSUPP;

	if (!ieee80211_sdata_running(sdata))
		return -ENETDOWN;

	spin_lock_bh(&sdata->u.nan.func_lock);

	ret = idr_alloc(&sdata->u.nan.function_inst_ids,
			nan_func, 1, sdata->local->hw.max_nan_de_entries + 1,
			GFP_ATOMIC);
	spin_unlock_bh(&sdata->u.nan.func_lock);

	if (ret < 0)
		return ret;

	nan_func->instance_id = ret;

	WARN_ON(nan_func->instance_id == 0);

	ret = drv_add_nan_func(sdata->local, sdata, nan_func);
	if (ret) {
		spin_lock_bh(&sdata->u.nan.func_lock);
		idr_remove(&sdata->u.nan.function_inst_ids,
			   nan_func->instance_id);
		spin_unlock_bh(&sdata->u.nan.func_lock);
	}

	return ret;
}

static struct cfg80211_nan_func *
ieee80211_find_nan_func_by_cookie(struct ieee80211_sub_if_data *sdata,
				  u64 cookie)
{
	struct cfg80211_nan_func *func;
	int id;

	lockdep_assert_held(&sdata->u.nan.func_lock);

	idr_for_each_entry(&sdata->u.nan.function_inst_ids, func, id) {
		if (func->cookie == cookie)
			return func;
	}

	return NULL;
}

static void ieee80211_del_nan_func(struct wiphy *wiphy,
				  struct wireless_dev *wdev, u64 cookie)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	struct cfg80211_nan_func *func;
	u8 instance_id = 0;

	if (sdata->vif.type != NL80211_IFTYPE_NAN ||
	    !ieee80211_sdata_running(sdata))
		return;

	spin_lock_bh(&sdata->u.nan.func_lock);

	func = ieee80211_find_nan_func_by_cookie(sdata, cookie);
	if (func)
		instance_id = func->instance_id;

	spin_unlock_bh(&sdata->u.nan.func_lock);

	if (instance_id)
		drv_del_nan_func(sdata->local, sdata, instance_id);
}

static int ieee80211_set_noack_map(struct wiphy *wiphy,
				  struct net_device *dev,
				  u16 noack_map)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	sdata->noack_map = noack_map;

	ieee80211_check_fast_xmit_iface(sdata);

	return 0;
}

static int ieee80211_set_tx(struct ieee80211_sub_if_data *sdata,
			    const u8 *mac_addr, u8 key_idx)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_key *key;
	struct sta_info *sta;
	int ret = -EINVAL;

	if (!wiphy_ext_feature_isset(local->hw.wiphy,
				     NL80211_EXT_FEATURE_EXT_KEY_ID))
		return -EINVAL;

	sta = sta_info_get_bss(sdata, mac_addr);

	if (!sta)
		return -EINVAL;

	if (sta->ptk_idx == key_idx)
		return 0;

	mutex_lock(&local->key_mtx);
	key = key_mtx_dereference(local, sta->ptk[key_idx]);

	if (key && key->conf.flags & IEEE80211_KEY_FLAG_NO_AUTO_TX)
		ret = ieee80211_set_tx_key(key);

	mutex_unlock(&local->key_mtx);
	return ret;
}

static int ieee80211_add_key(struct wiphy *wiphy, struct net_device *dev,
			     u8 key_idx, bool pairwise, const u8 *mac_addr,
			     struct key_params *params)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta = NULL;
	const struct ieee80211_cipher_scheme *cs = NULL;
	struct ieee80211_key *key;
	int err;

	if (!ieee80211_sdata_running(sdata))
		return -ENETDOWN;

	if (pairwise && params->mode == NL80211_KEY_SET_TX)
		return ieee80211_set_tx(sdata, mac_addr, key_idx);

	/* reject WEP and TKIP keys if WEP failed to initialize */
	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_WEP104:
		if (WARN_ON_ONCE(fips_enabled))
			return -EINVAL;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		break;
	default:
		cs = ieee80211_cs_get(local, params->cipher, sdata->vif.type);
		break;
	}

	key = ieee80211_key_alloc(params->cipher, key_idx, params->key_len,
				  params->key, params->seq_len, params->seq,
				  cs);
	if (IS_ERR(key))
		return PTR_ERR(key);

	if (pairwise)
		key->conf.flags |= IEEE80211_KEY_FLAG_PAIRWISE;

	if (params->mode == NL80211_KEY_NO_TX)
		key->conf.flags |= IEEE80211_KEY_FLAG_NO_AUTO_TX;

	mutex_lock(&local->sta_mtx);

	if (mac_addr) {
		sta = sta_info_get_bss(sdata, mac_addr);
		/*
		 * The ASSOC test makes sure the driver is ready to
		 * receive the key. When wpa_supplicant has roamed
		 * using FT, it attempts to set the key before
		 * association has completed, this rejects that attempt
		 * so it will set the key again after association.
		 *
		 * TODO: accept the key if we have a station entry and
		 *       add it to the device after the station.
		 */
		if (!sta || !test_sta_flag(sta, WLAN_STA_ASSOC)) {
			ieee80211_key_free_unused(key);
			err = -ENOENT;
			goto out_unlock;
		}
	}

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_STATION:
		if (sdata->u.mgd.mfp != IEEE80211_MFP_DISABLED)
			key->conf.flags |= IEEE80211_KEY_FLAG_RX_MGMT;
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
		/* Keys without a station are used for TX only */
		if (sta && test_sta_flag(sta, WLAN_STA_MFP))
			key->conf.flags |= IEEE80211_KEY_FLAG_RX_MGMT;
		break;
	case NL80211_IFTYPE_ADHOC:
		/* no MFP (yet) */
		break;
	case NL80211_IFTYPE_MESH_POINT:
#ifdef CONFIG_MAC80211_MESH
		if (sdata->u.mesh.security != IEEE80211_MESH_SEC_NONE)
			key->conf.flags |= IEEE80211_KEY_FLAG_RX_MGMT;
		break;
#endif
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_P2P_DEVICE:
	case NL80211_IFTYPE_NAN:
	case NL80211_IFTYPE_UNSPECIFIED:
	case NUM_NL80211_IFTYPES:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_OCB:
		/* shouldn't happen */
		WARN_ON_ONCE(1);
		break;
	}

	if (sta)
		sta->cipher_scheme = cs;

	err = ieee80211_key_link(key, sdata, sta);

 out_unlock:
	mutex_unlock(&local->sta_mtx);

	return err;
}

static int ieee80211_del_key(struct wiphy *wiphy, struct net_device *dev,
			     u8 key_idx, bool pairwise, const u8 *mac_addr)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	struct ieee80211_key *key = NULL;
	int ret;

	mutex_lock(&local->sta_mtx);
	mutex_lock(&local->key_mtx);

	if (mac_addr) {
		ret = -ENOENT;

		sta = sta_info_get_bss(sdata, mac_addr);
		if (!sta)
			goto out_unlock;

		if (pairwise)
			key = key_mtx_dereference(local, sta->ptk[key_idx]);
		else
			key = key_mtx_dereference(local, sta->gtk[key_idx]);
	} else
		key = key_mtx_dereference(local, sdata->keys[key_idx]);

	if (!key) {
		ret = -ENOENT;
		goto out_unlock;
	}

	ieee80211_key_free(key, sdata->vif.type == NL80211_IFTYPE_STATION);

	ret = 0;
 out_unlock:
	mutex_unlock(&local->key_mtx);
	mutex_unlock(&local->sta_mtx);

	return ret;
}

static int ieee80211_get_key(struct wiphy *wiphy, struct net_device *dev,
			     u8 key_idx, bool pairwise, const u8 *mac_addr,
			     void *cookie,
			     void (*callback)(void *cookie,
					      struct key_params *params))
{
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta = NULL;
	u8 seq[6] = {0};
	struct key_params params;
	struct ieee80211_key *key = NULL;
	u64 pn64;
	u32 iv32;
	u16 iv16;
	int err = -ENOENT;
	struct ieee80211_key_seq kseq = {};

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();

	if (mac_addr) {
		sta = sta_info_get_bss(sdata, mac_addr);
		if (!sta)
			goto out;

		if (pairwise && key_idx < NUM_DEFAULT_KEYS)
			key = rcu_dereference(sta->ptk[key_idx]);
		else if (!pairwise &&
			 key_idx < NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS +
			 NUM_DEFAULT_BEACON_KEYS)
			key = rcu_dereference(sta->gtk[key_idx]);
	} else
		key = rcu_dereference(sdata->keys[key_idx]);

	if (!key)
		goto out;

	memset(&params, 0, sizeof(params));

	params.cipher = key->conf.cipher;

	switch (key->conf.cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		pn64 = atomic64_read(&key->conf.tx_pn);
		iv32 = TKIP_PN_TO_IV32(pn64);
		iv16 = TKIP_PN_TO_IV16(pn64);

		if (key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE &&
		    !(key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV)) {
			drv_get_key_seq(sdata->local, key, &kseq);
			iv32 = kseq.tkip.iv32;
			iv16 = kseq.tkip.iv16;
		}

		seq[0] = iv16 & 0xff;
		seq[1] = (iv16 >> 8) & 0xff;
		seq[2] = iv32 & 0xff;
		seq[3] = (iv32 >> 8) & 0xff;
		seq[4] = (iv32 >> 16) & 0xff;
		seq[5] = (iv32 >> 24) & 0xff;
		params.seq = seq;
		params.seq_len = 6;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		BUILD_BUG_ON(offsetof(typeof(kseq), ccmp) !=
			     offsetof(typeof(kseq), aes_cmac));
		/* fall through */
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		BUILD_BUG_ON(offsetof(typeof(kseq), ccmp) !=
			     offsetof(typeof(kseq), aes_gmac));
		/* fall through */
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		BUILD_BUG_ON(offsetof(typeof(kseq), ccmp) !=
			     offsetof(typeof(kseq), gcmp));

		if (key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE &&
		    !(key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV)) {
			drv_get_key_seq(sdata->local, key, &kseq);
			memcpy(seq, kseq.ccmp.pn, 6);
		} else {
			pn64 = atomic64_read(&key->conf.tx_pn);
			seq[0] = pn64;
			seq[1] = pn64 >> 8;
			seq[2] = pn64 >> 16;
			seq[3] = pn64 >> 24;
			seq[4] = pn64 >> 32;
			seq[5] = pn64 >> 40;
		}
		params.seq = seq;
		params.seq_len = 6;
		break;
	default:
		if (!(key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE))
			break;
		if (WARN_ON(key->conf.flags & IEEE80211_KEY_FLAG_GENERATE_IV))
			break;
		drv_get_key_seq(sdata->local, key, &kseq);
		params.seq = kseq.hw.seq;
		params.seq_len = kseq.hw.seq_len;
		break;
	}

	params.key = key->conf.key;
	params.key_len = key->conf.keylen;

	callback(cookie, &params);
	err = 0;

 out:
	rcu_read_unlock();
	return err;
}

static int ieee80211_config_default_key(struct wiphy *wiphy,
					struct net_device *dev,
					u8 key_idx, bool uni,
					bool multi)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	ieee80211_set_default_key(sdata, key_idx, uni, multi);

	return 0;
}

static int ieee80211_config_default_mgmt_key(struct wiphy *wiphy,
					     struct net_device *dev,
					     u8 key_idx)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	ieee80211_set_default_mgmt_key(sdata, key_idx);

	return 0;
}

static int ieee80211_config_default_beacon_key(struct wiphy *wiphy,
					       struct net_device *dev,
					       u8 key_idx)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	ieee80211_set_default_beacon_key(sdata, key_idx);

	return 0;
}

void sta_set_rate_info_tx(struct sta_info *sta,
			  const struct ieee80211_tx_rate *rate,
			  struct rate_info *rinfo)
{
	rinfo->flags = 0;
	if (rate->flags & IEEE80211_TX_RC_MCS) {
		rinfo->flags |= RATE_INFO_FLAGS_MCS;
		rinfo->mcs = rate->idx;
	} else if (rate->flags & IEEE80211_TX_RC_VHT_MCS) {
		rinfo->flags |= RATE_INFO_FLAGS_VHT_MCS;
		rinfo->mcs = ieee80211_rate_get_vht_mcs(rate);
		rinfo->nss = ieee80211_rate_get_vht_nss(rate);
	} else {
		struct ieee80211_supported_band *sband;
		int shift = ieee80211_vif_get_shift(&sta->sdata->vif);
		u16 brate;

		sband = ieee80211_get_sband(sta->sdata);
		if (sband) {
			brate = sband->bitrates[rate->idx].bitrate;
			rinfo->legacy = DIV_ROUND_UP(brate, 1 << shift);
		}
	}
	if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		rinfo->bw = RATE_INFO_BW_40;
	else if (rate->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
		rinfo->bw = RATE_INFO_BW_80;
	else if (rate->flags & IEEE80211_TX_RC_160_MHZ_WIDTH)
		rinfo->bw = RATE_INFO_BW_160;
	else
		rinfo->bw = RATE_INFO_BW_20;
	if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
		rinfo->flags |= RATE_INFO_FLAGS_SHORT_GI;
}

static int ieee80211_dump_station(struct wiphy *wiphy, struct net_device *dev,
				  int idx, u8 *mac, struct station_info *sinfo)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	int ret = -ENOENT;

	mutex_lock(&local->sta_mtx);

	sta = sta_info_get_by_idx(sdata, idx);
	if (sta) {
		ret = 0;
		memcpy(mac, sta->sta.addr, ETH_ALEN);
		sta_set_sinfo(sta, sinfo, true);
	}

	mutex_unlock(&local->sta_mtx);

	return ret;
}

static int ieee80211_dump_survey(struct wiphy *wiphy, struct net_device *dev,
				 int idx, struct survey_info *survey)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	return drv_get_survey(local, idx, survey);
}

static int ieee80211_get_station(struct wiphy *wiphy, struct net_device *dev,
				 const u8 *mac, struct station_info *sinfo)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	int ret = -ENOENT;

	mutex_lock(&local->sta_mtx);

	sta = sta_info_get_bss(sdata, mac);
	if (sta) {
		ret = 0;
		sta_set_sinfo(sta, sinfo, true);
	}

	mutex_unlock(&local->sta_mtx);

	return ret;
}

static int ieee80211_set_monitor_channel(struct wiphy *wiphy,
					 struct cfg80211_chan_def *chandef)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata;
	int ret = 0;

	if (cfg80211_chandef_identical(&local->monitor_chandef, chandef))
		return 0;

	mutex_lock(&local->mtx);
	if (local->use_chanctx) {
		sdata = rtnl_dereference(local->monitor_sdata);
		if (sdata) {
			ieee80211_vif_release_channel(sdata);
			ret = ieee80211_vif_use_channel(sdata, chandef,
					IEEE80211_CHANCTX_EXCLUSIVE);
		}
	} else if (local->open_count == local->monitors) {
		local->_oper_chandef = *chandef;
		ieee80211_hw_config(local, 0);
	}

	if (ret == 0)
		local->monitor_chandef = *chandef;
	mutex_unlock(&local->mtx);

	return ret;
}

static int ieee80211_set_probe_resp(struct ieee80211_sub_if_data *sdata,
				    const u8 *resp, size_t resp_len,
				    const struct ieee80211_csa_settings *csa)
{
	struct probe_resp *new, *old;

	if (!resp || !resp_len)
		return 1;

	old = sdata_dereference(sdata->u.ap.probe_resp, sdata);

	new = kzalloc(sizeof(struct probe_resp) + resp_len, GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	new->len = resp_len;
	memcpy(new->data, resp, resp_len);

	if (csa)
		memcpy(new->csa_counter_offsets, csa->counter_offsets_presp,
		       csa->n_counter_offsets_presp *
		       sizeof(new->csa_counter_offsets[0]));

	rcu_assign_pointer(sdata->u.ap.probe_resp, new);
	if (old)
		kfree_rcu(old, rcu_head);

	return 0;
}

static int ieee80211_set_ftm_responder_params(
				struct ieee80211_sub_if_data *sdata,
				const u8 *lci, size_t lci_len,
				const u8 *civicloc, size_t civicloc_len)
{
	struct ieee80211_ftm_responder_params *new, *old;
	struct ieee80211_bss_conf *bss_conf;
	u8 *pos;
	int len;

	if (!lci_len && !civicloc_len)
		return 0;

	bss_conf = &sdata->vif.bss_conf;
	old = bss_conf->ftmr_params;
	len = lci_len + civicloc_len;

	new = kzalloc(sizeof(*new) + len, GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	pos = (u8 *)(new + 1);
	if (lci_len) {
		new->lci_len = lci_len;
		new->lci = pos;
		memcpy(pos, lci, lci_len);
		pos += lci_len;
	}

	if (civicloc_len) {
		new->civicloc_len = civicloc_len;
		new->civicloc = pos;
		memcpy(pos, civicloc, civicloc_len);
		pos += civicloc_len;
	}

	bss_conf->ftmr_params = new;
	kfree(old);

	return 0;
}

static int ieee80211_assign_beacon(struct ieee80211_sub_if_data *sdata,
				   struct cfg80211_beacon_data *params,
				   const struct ieee80211_csa_settings *csa)
{
	struct beacon_data *new, *old;
	int new_head_len, new_tail_len;
	int size, err;
	u32 changed = BSS_CHANGED_BEACON;

	old = sdata_dereference(sdata->u.ap.beacon, sdata);


	/* Need to have a beacon head if we don't have one yet */
	if (!params->head && !old)
		return -EINVAL;

	/* new or old head? */
	if (params->head)
		new_head_len = params->head_len;
	else
		new_head_len = old->head_len;

	/* new or old tail? */
	if (params->tail || !old)
		/* params->tail_len will be zero for !params->tail */
		new_tail_len = params->tail_len;
	else
		new_tail_len = old->tail_len;

	size = sizeof(*new) + new_head_len + new_tail_len;

	new = kzalloc(size, GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	/* start filling the new info now */

	/*
	 * pointers go into the block we allocated,
	 * memory is | beacon_data | head | tail |
	 */
	new->head = ((u8 *) new) + sizeof(*new);
	new->tail = new->head + new_head_len;
	new->head_len = new_head_len;
	new->tail_len = new_tail_len;

	if (csa) {
		new->csa_current_counter = csa->count;
		memcpy(new->csa_counter_offsets, csa->counter_offsets_beacon,
		       csa->n_counter_offsets_beacon *
		       sizeof(new->csa_counter_offsets[0]));
	}

	/* copy in head */
	if (params->head)
		memcpy(new->head, params->head, new_head_len);
	else
		memcpy(new->head, old->head, new_head_len);

	/* copy in optional tail */
	if (params->tail)
		memcpy(new->tail, params->tail, new_tail_len);
	else
		if (old)
			memcpy(new->tail, old->tail, new_tail_len);

	err = ieee80211_set_probe_resp(sdata, params->probe_resp,
				       params->probe_resp_len, csa);
	if (err < 0) {
		kfree(new);
		return err;
	}
	if (err == 0)
		changed |= BSS_CHANGED_AP_PROBE_RESP;

	if (params->ftm_responder != -1) {
		sdata->vif.bss_conf.ftm_responder = params->ftm_responder;
		err = ieee80211_set_ftm_responder_params(sdata,
							 params->lci,
							 params->lci_len,
							 params->civicloc,
							 params->civicloc_len);

		if (err < 0) {
			kfree(new);
			return err;
		}

		changed |= BSS_CHANGED_FTM_RESPONDER;
	}

	rcu_assign_pointer(sdata->u.ap.beacon, new);

	if (old)
		kfree_rcu(old, rcu_head);

	return changed;
}

static int ieee80211_start_ap(struct wiphy *wiphy, struct net_device *dev,
			      struct cfg80211_ap_settings *params)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct beacon_data *old;
	struct ieee80211_sub_if_data *vlan;
	u32 changed = BSS_CHANGED_BEACON_INT |
		      BSS_CHANGED_BEACON_ENABLED |
		      BSS_CHANGED_BEACON |
		      BSS_CHANGED_SSID |
		      BSS_CHANGED_P2P_PS |
		      BSS_CHANGED_TXPOWER |
		      BSS_CHANGED_TWT |
		      BSS_CHANGED_HE_OBSS_PD |
		      BSS_CHANGED_HE_BSS_COLOR;
	int err;
	int prev_beacon_int;

	old = sdata_dereference(sdata->u.ap.beacon, sdata);
	if (old)
		return -EALREADY;

	if (params->smps_mode != NL80211_SMPS_OFF)
		return -ENOTSUPP;

	sdata->smps_mode = IEEE80211_SMPS_OFF;

	sdata->needed_rx_chains = sdata->local->rx_chains;

	prev_beacon_int = sdata->vif.bss_conf.beacon_int;
	sdata->vif.bss_conf.beacon_int = params->beacon_interval;

	if (params->he_cap && params->he_oper) {
		sdata->vif.bss_conf.he_support = true;
		sdata->vif.bss_conf.htc_trig_based_pkt_ext =
			le32_get_bits(params->he_oper->he_oper_params,
			      IEEE80211_HE_OPERATION_DFLT_PE_DURATION_MASK);
		sdata->vif.bss_conf.frame_time_rts_th =
			le32_get_bits(params->he_oper->he_oper_params,
			      IEEE80211_HE_OPERATION_RTS_THRESHOLD_MASK);
	}

	mutex_lock(&local->mtx);
	err = ieee80211_vif_use_channel(sdata, &params->chandef,
					IEEE80211_CHANCTX_SHARED);
	if (!err)
		ieee80211_vif_copy_chanctx_to_vlans(sdata, false);
	mutex_unlock(&local->mtx);
	if (err) {
		sdata->vif.bss_conf.beacon_int = prev_beacon_int;
		return err;
	}

	/*
	 * Apply control port protocol, this allows us to
	 * not encrypt dynamic WEP control frames.
	 */
	sdata->control_port_protocol = params->crypto.control_port_ethertype;
	sdata->control_port_no_encrypt = params->crypto.control_port_no_encrypt;
	sdata->control_port_over_nl80211 =
				params->crypto.control_port_over_nl80211;
	sdata->control_port_no_preauth =
				params->crypto.control_port_no_preauth;
	sdata->encrypt_headroom = ieee80211_cs_headroom(sdata->local,
							&params->crypto,
							sdata->vif.type);

	list_for_each_entry(vlan, &sdata->u.ap.vlans, u.vlan.list) {
		vlan->control_port_protocol =
			params->crypto.control_port_ethertype;
		vlan->control_port_no_encrypt =
			params->crypto.control_port_no_encrypt;
		vlan->control_port_over_nl80211 =
			params->crypto.control_port_over_nl80211;
		vlan->control_port_no_preauth =
			params->crypto.control_port_no_preauth;
		vlan->encrypt_headroom =
			ieee80211_cs_headroom(sdata->local,
					      &params->crypto,
					      vlan->vif.type);
	}

	sdata->vif.bss_conf.dtim_period = params->dtim_period;
	sdata->vif.bss_conf.enable_beacon = true;
	sdata->vif.bss_conf.allow_p2p_go_ps = sdata->vif.p2p;
	sdata->vif.bss_conf.twt_responder = params->twt_responder;
	memcpy(&sdata->vif.bss_conf.he_obss_pd, &params->he_obss_pd,
	       sizeof(struct ieee80211_he_obss_pd));
	memcpy(&sdata->vif.bss_conf.he_bss_color, &params->he_bss_color,
	       sizeof(struct ieee80211_he_bss_color));

	sdata->vif.bss_conf.ssid_len = params->ssid_len;
	if (params->ssid_len)
		memcpy(sdata->vif.bss_conf.ssid, params->ssid,
		       params->ssid_len);
	sdata->vif.bss_conf.hidden_ssid =
		(params->hidden_ssid != NL80211_HIDDEN_SSID_NOT_IN_USE);

	memset(&sdata->vif.bss_conf.p2p_noa_attr, 0,
	       sizeof(sdata->vif.bss_conf.p2p_noa_attr));
	sdata->vif.bss_conf.p2p_noa_attr.oppps_ctwindow =
		params->p2p_ctwindow & IEEE80211_P2P_OPPPS_CTWINDOW_MASK;
	if (params->p2p_opp_ps)
		sdata->vif.bss_conf.p2p_noa_attr.oppps_ctwindow |=
					IEEE80211_P2P_OPPPS_ENABLE_BIT;

	err = ieee80211_assign_beacon(sdata, &params->beacon, NULL);
	if (err < 0) {
		ieee80211_vif_release_channel(sdata);
		return err;
	}
	changed |= err;

	err = drv_start_ap(sdata->local, sdata);
	if (err) {
		old = sdata_dereference(sdata->u.ap.beacon, sdata);

		if (old)
			kfree_rcu(old, rcu_head);
		RCU_INIT_POINTER(sdata->u.ap.beacon, NULL);
		ieee80211_vif_release_channel(sdata);
		return err;
	}

	ieee80211_recalc_dtim(local, sdata);
	ieee80211_bss_info_change_notify(sdata, changed);

	netif_carrier_on(dev);
	list_for_each_entry(vlan, &sdata->u.ap.vlans, u.vlan.list)
		netif_carrier_on(vlan->dev);

	return 0;
}

static int ieee80211_change_beacon(struct wiphy *wiphy, struct net_device *dev,
				   struct cfg80211_beacon_data *params)
{
	struct ieee80211_sub_if_data *sdata;
	struct beacon_data *old;
	int err;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	sdata_assert_lock(sdata);

	/* don't allow changing the beacon while CSA is in place - offset
	 * of channel switch counter may change
	 */
	if (sdata->vif.csa_active)
		return -EBUSY;

	old = sdata_dereference(sdata->u.ap.beacon, sdata);
	if (!old)
		return -ENOENT;

	err = ieee80211_assign_beacon(sdata, params, NULL);
	if (err < 0)
		return err;
	ieee80211_bss_info_change_notify(sdata, err);
	return 0;
}

static int ieee80211_stop_ap(struct wiphy *wiphy, struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_sub_if_data *vlan;
	struct ieee80211_local *local = sdata->local;
	struct beacon_data *old_beacon;
	struct probe_resp *old_probe_resp;
	struct cfg80211_chan_def chandef;

	sdata_assert_lock(sdata);

	old_beacon = sdata_dereference(sdata->u.ap.beacon, sdata);
	if (!old_beacon)
		return -ENOENT;
	old_probe_resp = sdata_dereference(sdata->u.ap.probe_resp, sdata);

	/* abort any running channel switch */
	mutex_lock(&local->mtx);
	sdata->vif.csa_active = false;
	if (sdata->csa_block_tx) {
		ieee80211_wake_vif_queues(local, sdata,
					  IEEE80211_QUEUE_STOP_REASON_CSA);
		sdata->csa_block_tx = false;
	}

	mutex_unlock(&local->mtx);

	kfree(sdata->u.ap.next_beacon);
	sdata->u.ap.next_beacon = NULL;

	/* turn off carrier for this interface and dependent VLANs */
	list_for_each_entry(vlan, &sdata->u.ap.vlans, u.vlan.list)
		netif_carrier_off(vlan->dev);
	netif_carrier_off(dev);

	/* remove beacon and probe response */
	RCU_INIT_POINTER(sdata->u.ap.beacon, NULL);
	RCU_INIT_POINTER(sdata->u.ap.probe_resp, NULL);
	kfree_rcu(old_beacon, rcu_head);
	if (old_probe_resp)
		kfree_rcu(old_probe_resp, rcu_head);

	kfree(sdata->vif.bss_conf.ftmr_params);
	sdata->vif.bss_conf.ftmr_params = NULL;

	__sta_info_flush(sdata, true);
	ieee80211_free_keys(sdata, true);

	sdata->vif.bss_conf.enable_beacon = false;
	sdata->vif.bss_conf.ssid_len = 0;
	clear_bit(SDATA_STATE_OFFCHANNEL_BEACON_STOPPED, &sdata->state);
	ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_BEACON_ENABLED);

	if (sdata->wdev.cac_started) {
		chandef = sdata->vif.bss_conf.chandef;
		cancel_delayed_work_sync(&sdata->dfs_cac_timer_work);
		cfg80211_cac_event(sdata->dev, &chandef,
				   NL80211_RADAR_CAC_ABORTED,
				   GFP_KERNEL);
	}

	drv_stop_ap(sdata->local, sdata);

	/* free all potentially still buffered bcast frames */
	local->total_ps_buffered -= skb_queue_len(&sdata->u.ap.ps.bc_buf);
	ieee80211_purge_tx_queue(&local->hw, &sdata->u.ap.ps.bc_buf);

	mutex_lock(&local->mtx);
	ieee80211_vif_copy_chanctx_to_vlans(sdata, true);
	ieee80211_vif_release_channel(sdata);
	mutex_unlock(&local->mtx);

	return 0;
}

static int sta_apply_auth_flags(struct ieee80211_local *local,
				struct sta_info *sta,
				u32 mask, u32 set)
{
	int ret;

	if (mask & BIT(NL80211_STA_FLAG_AUTHENTICATED) &&
	    set & BIT(NL80211_STA_FLAG_AUTHENTICATED) &&
	    !test_sta_flag(sta, WLAN_STA_AUTH)) {
		ret = sta_info_move_state(sta, IEEE80211_STA_AUTH);
		if (ret)
			return ret;
	}

	if (mask & BIT(NL80211_STA_FLAG_ASSOCIATED) &&
	    set & BIT(NL80211_STA_FLAG_ASSOCIATED) &&
	    !test_sta_flag(sta, WLAN_STA_ASSOC)) {
		/*
		 * When peer becomes associated, init rate control as
		 * well. Some drivers require rate control initialized
		 * before drv_sta_state() is called.
		 */
		if (!test_sta_flag(sta, WLAN_STA_RATE_CONTROL))
			rate_control_rate_init(sta);

		ret = sta_info_move_state(sta, IEEE80211_STA_ASSOC);
		if (ret)
			return ret;
	}

	if (mask & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
		if (set & BIT(NL80211_STA_FLAG_AUTHORIZED))
			ret = sta_info_move_state(sta, IEEE80211_STA_AUTHORIZED);
		else if (test_sta_flag(sta, WLAN_STA_AUTHORIZED))
			ret = sta_info_move_state(sta, IEEE80211_STA_ASSOC);
		else
			ret = 0;
		if (ret)
			return ret;
	}

	if (mask & BIT(NL80211_STA_FLAG_ASSOCIATED) &&
	    !(set & BIT(NL80211_STA_FLAG_ASSOCIATED)) &&
	    test_sta_flag(sta, WLAN_STA_ASSOC)) {
		ret = sta_info_move_state(sta, IEEE80211_STA_AUTH);
		if (ret)
			return ret;
	}

	if (mask & BIT(NL80211_STA_FLAG_AUTHENTICATED) &&
	    !(set & BIT(NL80211_STA_FLAG_AUTHENTICATED)) &&
	    test_sta_flag(sta, WLAN_STA_AUTH)) {
		ret = sta_info_move_state(sta, IEEE80211_STA_NONE);
		if (ret)
			return ret;
	}

	return 0;
}

static void sta_apply_mesh_params(struct ieee80211_local *local,
				  struct sta_info *sta,
				  struct station_parameters *params)
{
#ifdef CONFIG_MAC80211_MESH
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	u32 changed = 0;

	if (params->sta_modify_mask & STATION_PARAM_APPLY_PLINK_STATE) {
		switch (params->plink_state) {
		case NL80211_PLINK_ESTAB:
			if (sta->mesh->plink_state != NL80211_PLINK_ESTAB)
				changed = mesh_plink_inc_estab_count(sdata);
			sta->mesh->plink_state = params->plink_state;
			sta->mesh->aid = params->peer_aid;

			ieee80211_mps_sta_status_update(sta);
			changed |= ieee80211_mps_set_sta_local_pm(sta,
				      sdata->u.mesh.mshcfg.power_mode);

			ewma_mesh_tx_rate_avg_init(&sta->mesh->tx_rate_avg);
			/* init at low value */
			ewma_mesh_tx_rate_avg_add(&sta->mesh->tx_rate_avg, 10);

			break;
		case NL80211_PLINK_LISTEN:
		case NL80211_PLINK_BLOCKED:
		case NL80211_PLINK_OPN_SNT:
		case NL80211_PLINK_OPN_RCVD:
		case NL80211_PLINK_CNF_RCVD:
		case NL80211_PLINK_HOLDING:
			if (sta->mesh->plink_state == NL80211_PLINK_ESTAB)
				changed = mesh_plink_dec_estab_count(sdata);
			sta->mesh->plink_state = params->plink_state;

			ieee80211_mps_sta_status_update(sta);
			changed |= ieee80211_mps_set_sta_local_pm(sta,
					NL80211_MESH_POWER_UNKNOWN);
			break;
		default:
			/*  nothing  */
			break;
		}
	}

	switch (params->plink_action) {
	case NL80211_PLINK_ACTION_NO_ACTION:
		/* nothing */
		break;
	case NL80211_PLINK_ACTION_OPEN:
		changed |= mesh_plink_open(sta);
		break;
	case NL80211_PLINK_ACTION_BLOCK:
		changed |= mesh_plink_block(sta);
		break;
	}

	if (params->local_pm)
		changed |= ieee80211_mps_set_sta_local_pm(sta,
							  params->local_pm);

	ieee80211_mbss_info_change_notify(sdata, changed);
#endif
}

static int sta_apply_parameters(struct ieee80211_local *local,
				struct sta_info *sta,
				struct station_parameters *params)
{
	int ret = 0;
	struct ieee80211_supported_band *sband;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	u32 mask, set;

	sband = ieee80211_get_sband(sdata);
	if (!sband)
		return -EINVAL;

	mask = params->sta_flags_mask;
	set = params->sta_flags_set;

	if (ieee80211_vif_is_mesh(&sdata->vif)) {
		/*
		 * In mesh mode, ASSOCIATED isn't part of the nl80211
		 * API but must follow AUTHENTICATED for driver state.
		 */
		if (mask & BIT(NL80211_STA_FLAG_AUTHENTICATED))
			mask |= BIT(NL80211_STA_FLAG_ASSOCIATED);
		if (set & BIT(NL80211_STA_FLAG_AUTHENTICATED))
			set |= BIT(NL80211_STA_FLAG_ASSOCIATED);
	} else if (test_sta_flag(sta, WLAN_STA_TDLS_PEER)) {
		/*
		 * TDLS -- everything follows authorized, but
		 * only becoming authorized is possible, not
		 * going back
		 */
		if (set & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
			set |= BIT(NL80211_STA_FLAG_AUTHENTICATED) |
			       BIT(NL80211_STA_FLAG_ASSOCIATED);
			mask |= BIT(NL80211_STA_FLAG_AUTHENTICATED) |
				BIT(NL80211_STA_FLAG_ASSOCIATED);
		}
	}

	if (mask & BIT(NL80211_STA_FLAG_WME) &&
	    local->hw.queues >= IEEE80211_NUM_ACS)
		sta->sta.wme = set & BIT(NL80211_STA_FLAG_WME);

	/* auth flags will be set later for TDLS,
	 * and for unassociated stations that move to assocaited */
	if (!test_sta_flag(sta, WLAN_STA_TDLS_PEER) &&
	    !((mask & BIT(NL80211_STA_FLAG_ASSOCIATED)) &&
	      (set & BIT(NL80211_STA_FLAG_ASSOCIATED)))) {
		ret = sta_apply_auth_flags(local, sta, mask, set);
		if (ret)
			return ret;
	}

	if (mask & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE)) {
		if (set & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE))
			set_sta_flag(sta, WLAN_STA_SHORT_PREAMBLE);
		else
			clear_sta_flag(sta, WLAN_STA_SHORT_PREAMBLE);
	}

	if (mask & BIT(NL80211_STA_FLAG_MFP)) {
		sta->sta.mfp = !!(set & BIT(NL80211_STA_FLAG_MFP));
		if (set & BIT(NL80211_STA_FLAG_MFP))
			set_sta_flag(sta, WLAN_STA_MFP);
		else
			clear_sta_flag(sta, WLAN_STA_MFP);
	}

	if (mask & BIT(NL80211_STA_FLAG_TDLS_PEER)) {
		if (set & BIT(NL80211_STA_FLAG_TDLS_PEER))
			set_sta_flag(sta, WLAN_STA_TDLS_PEER);
		else
			clear_sta_flag(sta, WLAN_STA_TDLS_PEER);
	}

	/* mark TDLS channel switch support, if the AP allows it */
	if (test_sta_flag(sta, WLAN_STA_TDLS_PEER) &&
	    !sdata->u.mgd.tdls_chan_switch_prohibited &&
	    params->ext_capab_len >= 4 &&
	    params->ext_capab[3] & WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH)
		set_sta_flag(sta, WLAN_STA_TDLS_CHAN_SWITCH);

	if (test_sta_flag(sta, WLAN_STA_TDLS_PEER) &&
	    !sdata->u.mgd.tdls_wider_bw_prohibited &&
	    ieee80211_hw_check(&local->hw, TDLS_WIDER_BW) &&
	    params->ext_capab_len >= 8 &&
	    params->ext_capab[7] & WLAN_EXT_CAPA8_TDLS_WIDE_BW_ENABLED)
		set_sta_flag(sta, WLAN_STA_TDLS_WIDER_BW);

	if (params->sta_modify_mask & STATION_PARAM_APPLY_UAPSD) {
		sta->sta.uapsd_queues = params->uapsd_queues;
		sta->sta.max_sp = params->max_sp;
	}

	/* The sender might not have sent the last bit, consider it to be 0 */
	if (params->ext_capab_len >= 8) {
		u8 val = (params->ext_capab[7] &
			  WLAN_EXT_CAPA8_MAX_MSDU_IN_AMSDU_LSB) >> 7;

		/* we did get all the bits, take the MSB as well */
		if (params->ext_capab_len >= 9) {
			u8 val_msb = params->ext_capab[8] &
				WLAN_EXT_CAPA9_MAX_MSDU_IN_AMSDU_MSB;
			val_msb <<= 1;
			val |= val_msb;
		}

		switch (val) {
		case 1:
			sta->sta.max_amsdu_subframes = 32;
			break;
		case 2:
			sta->sta.max_amsdu_subframes = 16;
			break;
		case 3:
			sta->sta.max_amsdu_subframes = 8;
			break;
		default:
			sta->sta.max_amsdu_subframes = 0;
		}
	}

	/*
	 * cfg80211 validates this (1-2007) and allows setting the AID
	 * only when creating a new station entry
	 */
	if (params->aid)
		sta->sta.aid = params->aid;

	/*
	 * Some of the following updates would be racy if called on an
	 * existing station, via ieee80211_change_station(). However,
	 * all such changes are rejected by cfg80211 except for updates
	 * changing the supported rates on an existing but not yet used
	 * TDLS peer.
	 */

	if (params->listen_interval >= 0)
		sta->listen_interval = params->listen_interval;

	if (params->sta_modify_mask & STATION_PARAM_APPLY_STA_TXPOWER) {
		sta->sta.txpwr.type = params->txpwr.type;
		if (params->txpwr.type == NL80211_TX_POWER_LIMITED)
			sta->sta.txpwr.power = params->txpwr.power;
		ret = drv_sta_set_txpwr(local, sdata, sta);
		if (ret)
			return ret;
	}

	if (params->supported_rates && params->supported_rates_len) {
		ieee80211_parse_bitrates(&sdata->vif.bss_conf.chandef,
					 sband, params->supported_rates,
					 params->supported_rates_len,
					 &sta->sta.supp_rates[sband->band]);
	}

	if (params->ht_capa)
		ieee80211_ht_cap_ie_to_sta_ht_cap(sdata, sband,
						  params->ht_capa, sta);

	/* VHT can override some HT caps such as the A-MSDU max length */
	if (params->vht_capa)
		ieee80211_vht_cap_ie_to_sta_vht_cap(sdata, sband,
						    params->vht_capa, sta);

	if (params->he_capa)
		ieee80211_he_cap_ie_to_sta_he_cap(sdata, sband,
						  (void *)params->he_capa,
						  params->he_capa_len, sta);

	if (params->opmode_notif_used) {
		/* returned value is only needed for rc update, but the
		 * rc isn't initialized here yet, so ignore it
		 */
		__ieee80211_vht_handle_opmode(sdata, sta, params->opmode_notif,
					      sband->band);
	}

	if (params->support_p2p_ps >= 0)
		sta->sta.support_p2p_ps = params->support_p2p_ps;

	if (ieee80211_vif_is_mesh(&sdata->vif))
		sta_apply_mesh_params(local, sta, params);

	if (params->airtime_weight)
		sta->airtime_weight = params->airtime_weight;

	/* set the STA state after all sta info from usermode has been set */
	if (test_sta_flag(sta, WLAN_STA_TDLS_PEER) ||
	    set & BIT(NL80211_STA_FLAG_ASSOCIATED)) {
		ret = sta_apply_auth_flags(local, sta, mask, set);
		if (ret)
			return ret;
	}

	return 0;
}

static int ieee80211_add_station(struct wiphy *wiphy, struct net_device *dev,
				 const u8 *mac,
				 struct station_parameters *params)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata;
	int err;

	if (params->vlan) {
		sdata = IEEE80211_DEV_TO_SUB_IF(params->vlan);

		if (sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    sdata->vif.type != NL80211_IFTYPE_AP)
			return -EINVAL;
	} else
		sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (ether_addr_equal(mac, sdata->vif.addr))
		return -EINVAL;

	if (!is_valid_ether_addr(mac))
		return -EINVAL;

	if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER) &&
	    sdata->vif.type == NL80211_IFTYPE_STATION &&
	    !sdata->u.mgd.associated)
		return -EINVAL;

	sta = sta_info_alloc(sdata, mac, GFP_KERNEL);
	if (!sta)
		return -ENOMEM;

	if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER))
		sta->sta.tdls = true;

	err = sta_apply_parameters(local, sta, params);
	if (err) {
		sta_info_free(local, sta);
		return err;
	}

	/*
	 * for TDLS and for unassociated station, rate control should be
	 * initialized only when rates are known and station is marked
	 * authorized/associated
	 */
	if (!test_sta_flag(sta, WLAN_STA_TDLS_PEER) &&
	    test_sta_flag(sta, WLAN_STA_ASSOC))
		rate_control_rate_init(sta);

	err = sta_info_insert_rcu(sta);
	if (err) {
		rcu_read_unlock();
		return err;
	}

	rcu_read_unlock();

	return 0;
}

static int ieee80211_del_station(struct wiphy *wiphy, struct net_device *dev,
				 struct station_del_parameters *params)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (params->mac)
		return sta_info_destroy_addr_bss(sdata, params->mac);

	sta_info_flush(sdata);
	return 0;
}

static int ieee80211_change_station(struct wiphy *wiphy,
				    struct net_device *dev, const u8 *mac,
				    struct station_parameters *params)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *vlansdata;
	enum cfg80211_station_type statype;
	int err;

	mutex_lock(&local->sta_mtx);

	sta = sta_info_get_bss(sdata, mac);
	if (!sta) {
		err = -ENOENT;
		goto out_err;
	}

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_MESH_POINT:
		if (sdata->u.mesh.user_mpm)
			statype = CFG80211_STA_MESH_PEER_USER;
		else
			statype = CFG80211_STA_MESH_PEER_KERNEL;
		break;
	case NL80211_IFTYPE_ADHOC:
		statype = CFG80211_STA_IBSS;
		break;
	case NL80211_IFTYPE_STATION:
		if (!test_sta_flag(sta, WLAN_STA_TDLS_PEER)) {
			statype = CFG80211_STA_AP_STA;
			break;
		}
		if (test_sta_flag(sta, WLAN_STA_AUTHORIZED))
			statype = CFG80211_STA_TDLS_PEER_ACTIVE;
		else
			statype = CFG80211_STA_TDLS_PEER_SETUP;
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
		if (test_sta_flag(sta, WLAN_STA_ASSOC))
			statype = CFG80211_STA_AP_CLIENT;
		else
			statype = CFG80211_STA_AP_CLIENT_UNASSOC;
		break;
	default:
		err = -EOPNOTSUPP;
		goto out_err;
	}

	err = cfg80211_check_station_change(wiphy, params, statype);
	if (err)
		goto out_err;

	if (params->vlan && params->vlan != sta->sdata->dev) {
		vlansdata = IEEE80211_DEV_TO_SUB_IF(params->vlan);

		if (params->vlan->ieee80211_ptr->use_4addr) {
			if (vlansdata->u.vlan.sta) {
				err = -EBUSY;
				goto out_err;
			}

			rcu_assign_pointer(vlansdata->u.vlan.sta, sta);
			__ieee80211_check_fast_rx_iface(vlansdata);
		}

		if (sta->sdata->vif.type == NL80211_IFTYPE_AP_VLAN &&
		    sta->sdata->u.vlan.sta)
			RCU_INIT_POINTER(sta->sdata->u.vlan.sta, NULL);

		if (test_sta_flag(sta, WLAN_STA_AUTHORIZED))
			ieee80211_vif_dec_num_mcast(sta->sdata);

		sta->sdata = vlansdata;
		ieee80211_check_fast_xmit(sta);

		if (test_sta_flag(sta, WLAN_STA_AUTHORIZED)) {
			ieee80211_vif_inc_num_mcast(sta->sdata);
			cfg80211_send_layer2_update(sta->sdata->dev,
						    sta->sta.addr);
		}
	}

	err = sta_apply_parameters(local, sta, params);
	if (err)
		goto out_err;

	mutex_unlock(&local->sta_mtx);

	if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	    params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
		ieee80211_recalc_ps(local);
		ieee80211_recalc_ps_vif(sdata);
	}

	return 0;
out_err:
	mutex_unlock(&local->sta_mtx);
	return err;
}

#ifdef CONFIG_MAC80211_MESH
static int ieee80211_add_mpath(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *dst, const u8 *next_hop)
{
	struct ieee80211_sub_if_data *sdata;
	struct mesh_path *mpath;
	struct sta_info *sta;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();
	sta = sta_info_get(sdata, next_hop);
	if (!sta) {
		rcu_read_unlock();
		return -ENOENT;
	}

	mpath = mesh_path_add(sdata, dst);
	if (IS_ERR(mpath)) {
		rcu_read_unlock();
		return PTR_ERR(mpath);
	}

	mesh_path_fix_nexthop(mpath, sta);

	rcu_read_unlock();
	return 0;
}

static int ieee80211_del_mpath(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *dst)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (dst)
		return mesh_path_del(sdata, dst);

	mesh_path_flush_by_iface(sdata);
	return 0;
}

static int ieee80211_change_mpath(struct wiphy *wiphy, struct net_device *dev,
				  const u8 *dst, const u8 *next_hop)
{
	struct ieee80211_sub_if_data *sdata;
	struct mesh_path *mpath;
	struct sta_info *sta;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();

	sta = sta_info_get(sdata, next_hop);
	if (!sta) {
		rcu_read_unlock();
		return -ENOENT;
	}

	mpath = mesh_path_lookup(sdata, dst);
	if (!mpath) {
		rcu_read_unlock();
		return -ENOENT;
	}

	mesh_path_fix_nexthop(mpath, sta);

	rcu_read_unlock();
	return 0;
}

static void mpath_set_pinfo(struct mesh_path *mpath, u8 *next_hop,
			    struct mpath_info *pinfo)
{
	struct sta_info *next_hop_sta = rcu_dereference(mpath->next_hop);

	if (next_hop_sta)
		memcpy(next_hop, next_hop_sta->sta.addr, ETH_ALEN);
	else
		eth_zero_addr(next_hop);

	memset(pinfo, 0, sizeof(*pinfo));

	pinfo->generation = mpath->sdata->u.mesh.mesh_paths_generation;

	pinfo->filled = MPATH_INFO_FRAME_QLEN |
			MPATH_INFO_SN |
			MPATH_INFO_METRIC |
			MPATH_INFO_EXPTIME |
			MPATH_INFO_DISCOVERY_TIMEOUT |
			MPATH_INFO_DISCOVERY_RETRIES |
			MPATH_INFO_FLAGS |
			MPATH_INFO_HOP_COUNT |
			MPATH_INFO_PATH_CHANGE;

	pinfo->frame_qlen = mpath->frame_queue.qlen;
	pinfo->sn = mpath->sn;
	pinfo->metric = mpath->metric;
	if (time_before(jiffies, mpath->exp_time))
		pinfo->exptime = jiffies_to_msecs(mpath->exp_time - jiffies);
	pinfo->discovery_timeout =
			jiffies_to_msecs(mpath->discovery_timeout);
	pinfo->discovery_retries = mpath->discovery_retries;
	if (mpath->flags & MESH_PATH_ACTIVE)
		pinfo->flags |= NL80211_MPATH_FLAG_ACTIVE;
	if (mpath->flags & MESH_PATH_RESOLVING)
		pinfo->flags |= NL80211_MPATH_FLAG_RESOLVING;
	if (mpath->flags & MESH_PATH_SN_VALID)
		pinfo->flags |= NL80211_MPATH_FLAG_SN_VALID;
	if (mpath->flags & MESH_PATH_FIXED)
		pinfo->flags |= NL80211_MPATH_FLAG_FIXED;
	if (mpath->flags & MESH_PATH_RESOLVED)
		pinfo->flags |= NL80211_MPATH_FLAG_RESOLVED;
	pinfo->hop_count = mpath->hop_count;
	pinfo->path_change_count = mpath->path_change_count;
}

static int ieee80211_get_mpath(struct wiphy *wiphy, struct net_device *dev,
			       u8 *dst, u8 *next_hop, struct mpath_info *pinfo)

{
	struct ieee80211_sub_if_data *sdata;
	struct mesh_path *mpath;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();
	mpath = mesh_path_lookup(sdata, dst);
	if (!mpath) {
		rcu_read_unlock();
		return -ENOENT;
	}
	memcpy(dst, mpath->dst, ETH_ALEN);
	mpath_set_pinfo(mpath, next_hop, pinfo);
	rcu_read_unlock();
	return 0;
}

static int ieee80211_dump_mpath(struct wiphy *wiphy, struct net_device *dev,
				int idx, u8 *dst, u8 *next_hop,
				struct mpath_info *pinfo)
{
	struct ieee80211_sub_if_data *sdata;
	struct mesh_path *mpath;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();
	mpath = mesh_path_lookup_by_idx(sdata, idx);
	if (!mpath) {
		rcu_read_unlock();
		return -ENOENT;
	}
	memcpy(dst, mpath->dst, ETH_ALEN);
	mpath_set_pinfo(mpath, next_hop, pinfo);
	rcu_read_unlock();
	return 0;
}

static void mpp_set_pinfo(struct mesh_path *mpath, u8 *mpp,
			  struct mpath_info *pinfo)
{
	memset(pinfo, 0, sizeof(*pinfo));
	memcpy(mpp, mpath->mpp, ETH_ALEN);

	pinfo->generation = mpath->sdata->u.mesh.mpp_paths_generation;
}

static int ieee80211_get_mpp(struct wiphy *wiphy, struct net_device *dev,
			     u8 *dst, u8 *mpp, struct mpath_info *pinfo)

{
	struct ieee80211_sub_if_data *sdata;
	struct mesh_path *mpath;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();
	mpath = mpp_path_lookup(sdata, dst);
	if (!mpath) {
		rcu_read_unlock();
		return -ENOENT;
	}
	memcpy(dst, mpath->dst, ETH_ALEN);
	mpp_set_pinfo(mpath, mpp, pinfo);
	rcu_read_unlock();
	return 0;
}

static int ieee80211_dump_mpp(struct wiphy *wiphy, struct net_device *dev,
			      int idx, u8 *dst, u8 *mpp,
			      struct mpath_info *pinfo)
{
	struct ieee80211_sub_if_data *sdata;
	struct mesh_path *mpath;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();
	mpath = mpp_path_lookup_by_idx(sdata, idx);
	if (!mpath) {
		rcu_read_unlock();
		return -ENOENT;
	}
	memcpy(dst, mpath->dst, ETH_ALEN);
	mpp_set_pinfo(mpath, mpp, pinfo);
	rcu_read_unlock();
	return 0;
}

static int ieee80211_get_mesh_config(struct wiphy *wiphy,
				struct net_device *dev,
				struct mesh_config *conf)
{
	struct ieee80211_sub_if_data *sdata;
	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	memcpy(conf, &(sdata->u.mesh.mshcfg), sizeof(struct mesh_config));
	return 0;
}

static inline bool _chg_mesh_attr(enum nl80211_meshconf_params parm, u32 mask)
{
	return (mask >> (parm-1)) & 0x1;
}

static int copy_mesh_setup(struct ieee80211_if_mesh *ifmsh,
		const struct mesh_setup *setup)
{
	u8 *new_ie;
	const u8 *old_ie;
	struct ieee80211_sub_if_data *sdata = container_of(ifmsh,
					struct ieee80211_sub_if_data, u.mesh);

	/* allocate information elements */
	new_ie = NULL;
	old_ie = ifmsh->ie;

	if (setup->ie_len) {
		new_ie = kmemdup(setup->ie, setup->ie_len,
				GFP_KERNEL);
		if (!new_ie)
			return -ENOMEM;
	}
	ifmsh->ie_len = setup->ie_len;
	ifmsh->ie = new_ie;
	kfree(old_ie);

	/* now copy the rest of the setup parameters */
	ifmsh->mesh_id_len = setup->mesh_id_len;
	memcpy(ifmsh->mesh_id, setup->mesh_id, ifmsh->mesh_id_len);
	ifmsh->mesh_sp_id = setup->sync_method;
	ifmsh->mesh_pp_id = setup->path_sel_proto;
	ifmsh->mesh_pm_id = setup->path_metric;
	ifmsh->user_mpm = setup->user_mpm;
	ifmsh->mesh_auth_id = setup->auth_id;
	ifmsh->security = IEEE80211_MESH_SEC_NONE;
	ifmsh->userspace_handles_dfs = setup->userspace_handles_dfs;
	if (setup->is_authenticated)
		ifmsh->security |= IEEE80211_MESH_SEC_AUTHED;
	if (setup->is_secure)
		ifmsh->security |= IEEE80211_MESH_SEC_SECURED;

	/* mcast rate setting in Mesh Node */
	memcpy(sdata->vif.bss_conf.mcast_rate, setup->mcast_rate,
						sizeof(setup->mcast_rate));
	sdata->vif.bss_conf.basic_rates = setup->basic_rates;

	sdata->vif.bss_conf.beacon_int = setup->beacon_interval;
	sdata->vif.bss_conf.dtim_period = setup->dtim_period;

	return 0;
}

static int ieee80211_update_mesh_config(struct wiphy *wiphy,
					struct net_device *dev, u32 mask,
					const struct mesh_config *nconf)
{
	struct mesh_config *conf;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_mesh *ifmsh;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	ifmsh = &sdata->u.mesh;

	/* Set the config options which we are interested in setting */
	conf = &(sdata->u.mesh.mshcfg);
	if (_chg_mesh_attr(NL80211_MESHCONF_RETRY_TIMEOUT, mask))
		conf->dot11MeshRetryTimeout = nconf->dot11MeshRetryTimeout;
	if (_chg_mesh_attr(NL80211_MESHCONF_CONFIRM_TIMEOUT, mask))
		conf->dot11MeshConfirmTimeout = nconf->dot11MeshConfirmTimeout;
	if (_chg_mesh_attr(NL80211_MESHCONF_HOLDING_TIMEOUT, mask))
		conf->dot11MeshHoldingTimeout = nconf->dot11MeshHoldingTimeout;
	if (_chg_mesh_attr(NL80211_MESHCONF_MAX_PEER_LINKS, mask))
		conf->dot11MeshMaxPeerLinks = nconf->dot11MeshMaxPeerLinks;
	if (_chg_mesh_attr(NL80211_MESHCONF_MAX_RETRIES, mask))
		conf->dot11MeshMaxRetries = nconf->dot11MeshMaxRetries;
	if (_chg_mesh_attr(NL80211_MESHCONF_TTL, mask))
		conf->dot11MeshTTL = nconf->dot11MeshTTL;
	if (_chg_mesh_attr(NL80211_MESHCONF_ELEMENT_TTL, mask))
		conf->element_ttl = nconf->element_ttl;
	if (_chg_mesh_attr(NL80211_MESHCONF_AUTO_OPEN_PLINKS, mask)) {
		if (ifmsh->user_mpm)
			return -EBUSY;
		conf->auto_open_plinks = nconf->auto_open_plinks;
	}
	if (_chg_mesh_attr(NL80211_MESHCONF_SYNC_OFFSET_MAX_NEIGHBOR, mask))
		conf->dot11MeshNbrOffsetMaxNeighbor =
			nconf->dot11MeshNbrOffsetMaxNeighbor;
	if (_chg_mesh_attr(NL80211_MESHCONF_HWMP_MAX_PREQ_RETRIES, mask))
		conf->dot11MeshHWMPmaxPREQretries =
			nconf->dot11MeshHWMPmaxPREQretries;
	if (_chg_mesh_attr(NL80211_MESHCONF_PATH_REFRESH_TIME, mask))
		conf->path_refresh_time = nconf->path_refresh_time;
	if (_chg_mesh_attr(NL80211_MESHCONF_MIN_DISCOVERY_TIMEOUT, mask))
		conf->min_discovery_timeout = nconf->min_discovery_timeout;
	if (_chg_mesh_attr(NL80211_MESHCONF_HWMP_ACTIVE_PATH_TIMEOUT, mask))
		conf->dot11MeshHWMPactivePathTimeout =
			nconf->dot11MeshHWMPactivePathTimeout;
	if (_chg_mesh_attr(NL80211_MESHCONF_HWMP_PREQ_MIN_INTERVAL, mask))
		conf->dot11MeshHWMPpreqMinInterval =
			nconf->dot11MeshHWMPpreqMinInterval;
	if (_chg_mesh_attr(NL80211_MESHCONF_HWMP_PERR_MIN_INTERVAL, mask))
		conf->dot11MeshHWMPperrMinInterval =
			nconf->dot11MeshHWMPperrMinInterval;
	if (_chg_mesh_attr(NL80211_MESHCONF_HWMP_NET_DIAM_TRVS_TIME,
			   mask))
		conf->dot11MeshHWMPnetDiameterTraversalTime =
			nconf->dot11MeshHWMPnetDiameterTraversalTime;
	if (_chg_mesh_attr(NL80211_MESHCONF_HWMP_ROOTMODE, mask)) {
		conf->dot11MeshHWMPRootMode = nconf->dot11MeshHWMPRootMode;
		ieee80211_mesh_root_setup(ifmsh);
	}
	if (_chg_mesh_attr(NL80211_MESHCONF_GATE_ANNOUNCEMENTS, mask)) {
		/* our current gate announcement implementation rides on root
		 * announcements, so require this ifmsh to also be a root node
		 * */
		if (nconf->dot11MeshGateAnnouncementProtocol &&
		    !(conf->dot11MeshHWMPRootMode > IEEE80211_ROOTMODE_ROOT)) {
			conf->dot11MeshHWMPRootMode = IEEE80211_PROACTIVE_RANN;
			ieee80211_mesh_root_setup(ifmsh);
		}
		conf->dot11MeshGateAnnouncementProtocol =
			nconf->dot11MeshGateAnnouncementProtocol;
	}
	if (_chg_mesh_attr(NL80211_MESHCONF_HWMP_RANN_INTERVAL, mask))
		conf->dot11MeshHWMPRannInterval =
			nconf->dot11MeshHWMPRannInterval;
	if (_chg_mesh_attr(NL80211_MESHCONF_FORWARDING, mask))
		conf->dot11MeshForwarding = nconf->dot11MeshForwarding;
	if (_chg_mesh_attr(NL80211_MESHCONF_RSSI_THRESHOLD, mask)) {
		/* our RSSI threshold implementation is supported only for
		 * devices that report signal in dBm.
		 */
		if (!ieee80211_hw_check(&sdata->local->hw, SIGNAL_DBM))
			return -ENOTSUPP;
		conf->rssi_threshold = nconf->rssi_threshold;
	}
	if (_chg_mesh_attr(NL80211_MESHCONF_HT_OPMODE, mask)) {
		conf->ht_opmode = nconf->ht_opmode;
		sdata->vif.bss_conf.ht_operation_mode = nconf->ht_opmode;
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_HT);
	}
	if (_chg_mesh_attr(NL80211_MESHCONF_HWMP_PATH_TO_ROOT_TIMEOUT, mask))
		conf->dot11MeshHWMPactivePathToRootTimeout =
			nconf->dot11MeshHWMPactivePathToRootTimeout;
	if (_chg_mesh_attr(NL80211_MESHCONF_HWMP_ROOT_INTERVAL, mask))
		conf->dot11MeshHWMProotInterval =
			nconf->dot11MeshHWMProotInterval;
	if (_chg_mesh_attr(NL80211_MESHCONF_HWMP_CONFIRMATION_INTERVAL, mask))
		conf->dot11MeshHWMPconfirmationInterval =
			nconf->dot11MeshHWMPconfirmationInterval;
	if (_chg_mesh_attr(NL80211_MESHCONF_POWER_MODE, mask)) {
		conf->power_mode = nconf->power_mode;
		ieee80211_mps_local_status_update(sdata);
	}
	if (_chg_mesh_attr(NL80211_MESHCONF_AWAKE_WINDOW, mask))
		conf->dot11MeshAwakeWindowDuration =
			nconf->dot11MeshAwakeWindowDuration;
	if (_chg_mesh_attr(NL80211_MESHCONF_PLINK_TIMEOUT, mask))
		conf->plink_timeout = nconf->plink_timeout;
	if (_chg_mesh_attr(NL80211_MESHCONF_CONNECTED_TO_GATE, mask))
		conf->dot11MeshConnectedToMeshGate =
			nconf->dot11MeshConnectedToMeshGate;
	ieee80211_mbss_info_change_notify(sdata, BSS_CHANGED_BEACON);
	return 0;
}

static int ieee80211_join_mesh(struct wiphy *wiphy, struct net_device *dev,
			       const struct mesh_config *conf,
			       const struct mesh_setup *setup)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;
	int err;

	memcpy(&ifmsh->mshcfg, conf, sizeof(struct mesh_config));
	err = copy_mesh_setup(ifmsh, setup);
	if (err)
		return err;

	sdata->control_port_over_nl80211 = setup->control_port_over_nl80211;

	/* can mesh use other SMPS modes? */
	sdata->smps_mode = IEEE80211_SMPS_OFF;
	sdata->needed_rx_chains = sdata->local->rx_chains;

	mutex_lock(&sdata->local->mtx);
	err = ieee80211_vif_use_channel(sdata, &setup->chandef,
					IEEE80211_CHANCTX_SHARED);
	mutex_unlock(&sdata->local->mtx);
	if (err)
		return err;

	return ieee80211_start_mesh(sdata);
}

static int ieee80211_leave_mesh(struct wiphy *wiphy, struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	ieee80211_stop_mesh(sdata);
	mutex_lock(&sdata->local->mtx);
	ieee80211_vif_release_channel(sdata);
	mutex_unlock(&sdata->local->mtx);

	return 0;
}
#endif

static int ieee80211_change_bss(struct wiphy *wiphy,
				struct net_device *dev,
				struct bss_parameters *params)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_supported_band *sband;
	u32 changed = 0;

	if (!sdata_dereference(sdata->u.ap.beacon, sdata))
		return -ENOENT;

	sband = ieee80211_get_sband(sdata);
	if (!sband)
		return -EINVAL;

	if (params->use_cts_prot >= 0) {
		sdata->vif.bss_conf.use_cts_prot = params->use_cts_prot;
		changed |= BSS_CHANGED_ERP_CTS_PROT;
	}
	if (params->use_short_preamble >= 0) {
		sdata->vif.bss_conf.use_short_preamble =
			params->use_short_preamble;
		changed |= BSS_CHANGED_ERP_PREAMBLE;
	}

	if (!sdata->vif.bss_conf.use_short_slot &&
	    sband->band == NL80211_BAND_5GHZ) {
		sdata->vif.bss_conf.use_short_slot = true;
		changed |= BSS_CHANGED_ERP_SLOT;
	}

	if (params->use_short_slot_time >= 0) {
		sdata->vif.bss_conf.use_short_slot =
			params->use_short_slot_time;
		changed |= BSS_CHANGED_ERP_SLOT;
	}

	if (params->basic_rates) {
		ieee80211_parse_bitrates(&sdata->vif.bss_conf.chandef,
					 wiphy->bands[sband->band],
					 params->basic_rates,
					 params->basic_rates_len,
					 &sdata->vif.bss_conf.basic_rates);
		changed |= BSS_CHANGED_BASIC_RATES;
		ieee80211_check_rate_mask(sdata);
	}

	if (params->ap_isolate >= 0) {
		if (params->ap_isolate)
			sdata->flags |= IEEE80211_SDATA_DONT_BRIDGE_PACKETS;
		else
			sdata->flags &= ~IEEE80211_SDATA_DONT_BRIDGE_PACKETS;
		ieee80211_check_fast_rx_iface(sdata);
	}

	if (params->ht_opmode >= 0) {
		sdata->vif.bss_conf.ht_operation_mode =
			(u16) params->ht_opmode;
		changed |= BSS_CHANGED_HT;
	}

	if (params->p2p_ctwindow >= 0) {
		sdata->vif.bss_conf.p2p_noa_attr.oppps_ctwindow &=
					~IEEE80211_P2P_OPPPS_CTWINDOW_MASK;
		sdata->vif.bss_conf.p2p_noa_attr.oppps_ctwindow |=
			params->p2p_ctwindow & IEEE80211_P2P_OPPPS_CTWINDOW_MASK;
		changed |= BSS_CHANGED_P2P_PS;
	}

	if (params->p2p_opp_ps > 0) {
		sdata->vif.bss_conf.p2p_noa_attr.oppps_ctwindow |=
					IEEE80211_P2P_OPPPS_ENABLE_BIT;
		changed |= BSS_CHANGED_P2P_PS;
	} else if (params->p2p_opp_ps == 0) {
		sdata->vif.bss_conf.p2p_noa_attr.oppps_ctwindow &=
					~IEEE80211_P2P_OPPPS_ENABLE_BIT;
		changed |= BSS_CHANGED_P2P_PS;
	}

	ieee80211_bss_info_change_notify(sdata, changed);

	return 0;
}

static int ieee80211_set_txq_params(struct wiphy *wiphy,
				    struct net_device *dev,
				    struct ieee80211_txq_params *params)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_tx_queue_params p;

	if (!local->ops->conf_tx)
		return -EOPNOTSUPP;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		return -EOPNOTSUPP;

	memset(&p, 0, sizeof(p));
	p.aifs = params->aifs;
	p.cw_max = params->cwmax;
	p.cw_min = params->cwmin;
	p.txop = params->txop;

	/*
	 * Setting tx queue params disables u-apsd because it's only
	 * called in master mode.
	 */
	p.uapsd = false;

	ieee80211_regulatory_limit_wmm_params(sdata, &p, params->ac);

	sdata->tx_conf[params->ac] = p;
	if (drv_conf_tx(local, sdata, params->ac, &p)) {
		wiphy_debug(local->hw.wiphy,
			    "failed to set TX queue parameters for AC %d\n",
			    params->ac);
		return -EINVAL;
	}

	ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_QOS);

	return 0;
}

#ifdef CONFIG_PM
static int ieee80211_suspend(struct wiphy *wiphy,
			     struct cfg80211_wowlan *wowlan)
{
	return __ieee80211_suspend(wiphy_priv(wiphy), wowlan);
}

static int ieee80211_resume(struct wiphy *wiphy)
{
	return __ieee80211_resume(wiphy_priv(wiphy));
}
#else
#define ieee80211_suspend NULL
#define ieee80211_resume NULL
#endif

static int ieee80211_scan(struct wiphy *wiphy,
			  struct cfg80211_scan_request *req)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_WDEV_TO_SUB_IF(req->wdev);

	switch (ieee80211_vif_type_p2p(&sdata->vif)) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_DEVICE:
		break;
	case NL80211_IFTYPE_P2P_GO:
		if (sdata->local->ops->hw_scan)
			break;
		/*
		 * FIXME: implement NoA while scanning in software,
		 * for now fall through to allow scanning only when
		 * beaconing hasn't been configured yet
		 */
		/* fall through */
	case NL80211_IFTYPE_AP:
		/*
		 * If the scan has been forced (and the driver supports
		 * forcing), don't care about being beaconing already.
		 * This will create problems to the attached stations (e.g. all
		 * the  frames sent while scanning on other channel will be
		 * lost)
		 */
		if (sdata->u.ap.beacon &&
		    (!(wiphy->features & NL80211_FEATURE_AP_SCAN) ||
		     !(req->flags & NL80211_SCAN_FLAG_AP)))
			return -EOPNOTSUPP;
		break;
	case NL80211_IFTYPE_NAN:
	default:
		return -EOPNOTSUPP;
	}

	return ieee80211_request_scan(sdata, req);
}

static void ieee80211_abort_scan(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	ieee80211_scan_cancel(wiphy_priv(wiphy));
}

static int
ieee80211_sched_scan_start(struct wiphy *wiphy,
			   struct net_device *dev,
			   struct cfg80211_sched_scan_request *req)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (!sdata->local->ops->sched_scan_start)
		return -EOPNOTSUPP;

	return ieee80211_request_sched_scan_start(sdata, req);
}

static int
ieee80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev,
			  u64 reqid)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	if (!local->ops->sched_scan_stop)
		return -EOPNOTSUPP;

	return ieee80211_request_sched_scan_stop(local);
}

static int ieee80211_auth(struct wiphy *wiphy, struct net_device *dev,
			  struct cfg80211_auth_request *req)
{
	return ieee80211_mgd_auth(IEEE80211_DEV_TO_SUB_IF(dev), req);
}

static int ieee80211_assoc(struct wiphy *wiphy, struct net_device *dev,
			   struct cfg80211_assoc_request *req)
{
	return ieee80211_mgd_assoc(IEEE80211_DEV_TO_SUB_IF(dev), req);
}

static int ieee80211_deauth(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_deauth_request *req)
{
	return ieee80211_mgd_deauth(IEEE80211_DEV_TO_SUB_IF(dev), req);
}

static int ieee80211_disassoc(struct wiphy *wiphy, struct net_device *dev,
			      struct cfg80211_disassoc_request *req)
{
	return ieee80211_mgd_disassoc(IEEE80211_DEV_TO_SUB_IF(dev), req);
}

static int ieee80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
			       struct cfg80211_ibss_params *params)
{
	return ieee80211_ibss_join(IEEE80211_DEV_TO_SUB_IF(dev), params);
}

static int ieee80211_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
	return ieee80211_ibss_leave(IEEE80211_DEV_TO_SUB_IF(dev));
}

static int ieee80211_join_ocb(struct wiphy *wiphy, struct net_device *dev,
			      struct ocb_setup *setup)
{
	return ieee80211_ocb_join(IEEE80211_DEV_TO_SUB_IF(dev), setup);
}

static int ieee80211_leave_ocb(struct wiphy *wiphy, struct net_device *dev)
{
	return ieee80211_ocb_leave(IEEE80211_DEV_TO_SUB_IF(dev));
}

static int ieee80211_set_mcast_rate(struct wiphy *wiphy, struct net_device *dev,
				    int rate[NUM_NL80211_BANDS])
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	memcpy(sdata->vif.bss_conf.mcast_rate, rate,
	       sizeof(int) * NUM_NL80211_BANDS);

	ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_MCAST_RATE);

	return 0;
}

static int ieee80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	int err;

	if (changed & WIPHY_PARAM_FRAG_THRESHOLD) {
		ieee80211_check_fast_xmit_all(local);

		err = drv_set_frag_threshold(local, wiphy->frag_threshold);

		if (err) {
			ieee80211_check_fast_xmit_all(local);
			return err;
		}
	}

	if ((changed & WIPHY_PARAM_COVERAGE_CLASS) ||
	    (changed & WIPHY_PARAM_DYN_ACK)) {
		s16 coverage_class;

		coverage_class = changed & WIPHY_PARAM_COVERAGE_CLASS ?
					wiphy->coverage_class : -1;
		err = drv_set_coverage_class(local, coverage_class);

		if (err)
			return err;
	}

	if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
		err = drv_set_rts_threshold(local, wiphy->rts_threshold);

		if (err)
			return err;
	}

	if (changed & WIPHY_PARAM_RETRY_SHORT) {
		if (wiphy->retry_short > IEEE80211_MAX_TX_RETRY)
			return -EINVAL;
		local->hw.conf.short_frame_max_tx_count = wiphy->retry_short;
	}
	if (changed & WIPHY_PARAM_RETRY_LONG) {
		if (wiphy->retry_long > IEEE80211_MAX_TX_RETRY)
			return -EINVAL;
		local->hw.conf.long_frame_max_tx_count = wiphy->retry_long;
	}
	if (changed &
	    (WIPHY_PARAM_RETRY_SHORT | WIPHY_PARAM_RETRY_LONG))
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_RETRY_LIMITS);

	if (changed & (WIPHY_PARAM_TXQ_LIMIT |
		       WIPHY_PARAM_TXQ_MEMORY_LIMIT |
		       WIPHY_PARAM_TXQ_QUANTUM))
		ieee80211_txq_set_params(local);

	return 0;
}

static int ieee80211_set_tx_power(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  enum nl80211_tx_power_setting type, int mbm)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata;
	enum nl80211_tx_power_setting txp_type = type;
	bool update_txp_type = false;
	bool has_monitor = false;

	if (wdev) {
		sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);

		if (sdata->vif.type == NL80211_IFTYPE_MONITOR) {
			sdata = rtnl_dereference(local->monitor_sdata);
			if (!sdata)
				return -EOPNOTSUPP;
		}

		switch (type) {
		case NL80211_TX_POWER_AUTOMATIC:
			sdata->user_power_level = IEEE80211_UNSET_POWER_LEVEL;
			txp_type = NL80211_TX_POWER_LIMITED;
			break;
		case NL80211_TX_POWER_LIMITED:
		case NL80211_TX_POWER_FIXED:
			if (mbm < 0 || (mbm % 100))
				return -EOPNOTSUPP;
			sdata->user_power_level = MBM_TO_DBM(mbm);
			break;
		}

		if (txp_type != sdata->vif.bss_conf.txpower_type) {
			update_txp_type = true;
			sdata->vif.bss_conf.txpower_type = txp_type;
		}

		ieee80211_recalc_txpower(sdata, update_txp_type);

		return 0;
	}

	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		local->user_power_level = IEEE80211_UNSET_POWER_LEVEL;
		txp_type = NL80211_TX_POWER_LIMITED;
		break;
	case NL80211_TX_POWER_LIMITED:
	case NL80211_TX_POWER_FIXED:
		if (mbm < 0 || (mbm % 100))
			return -EOPNOTSUPP;
		local->user_power_level = MBM_TO_DBM(mbm);
		break;
	}

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type == NL80211_IFTYPE_MONITOR) {
			has_monitor = true;
			continue;
		}
		sdata->user_power_level = local->user_power_level;
		if (txp_type != sdata->vif.bss_conf.txpower_type)
			update_txp_type = true;
		sdata->vif.bss_conf.txpower_type = txp_type;
	}
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type == NL80211_IFTYPE_MONITOR)
			continue;
		ieee80211_recalc_txpower(sdata, update_txp_type);
	}
	mutex_unlock(&local->iflist_mtx);

	if (has_monitor) {
		sdata = rtnl_dereference(local->monitor_sdata);
		if (sdata) {
			sdata->user_power_level = local->user_power_level;
			if (txp_type != sdata->vif.bss_conf.txpower_type)
				update_txp_type = true;
			sdata->vif.bss_conf.txpower_type = txp_type;

			ieee80211_recalc_txpower(sdata, update_txp_type);
		}
	}

	return 0;
}

static int ieee80211_get_tx_power(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  int *dbm)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);

	if (local->ops->get_txpower)
		return drv_get_txpower(local, sdata, dbm);

	if (!local->use_chanctx)
		*dbm = local->hw.conf.power_level;
	else
		*dbm = sdata->vif.bss_conf.txpower;

	return 0;
}

static int ieee80211_set_wds_peer(struct wiphy *wiphy, struct net_device *dev,
				  const u8 *addr)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	memcpy(&sdata->u.wds.remote_addr, addr, ETH_ALEN);

	return 0;
}

static void ieee80211_rfkill_poll(struct wiphy *wiphy)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	drv_rfkill_poll(local);
}

#ifdef CONFIG_NL80211_TESTMODE
static int ieee80211_testmode_cmd(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  void *data, int len)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_vif *vif = NULL;

	if (!local->ops->testmode_cmd)
		return -EOPNOTSUPP;

	if (wdev) {
		struct ieee80211_sub_if_data *sdata;

		sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
		if (sdata->flags & IEEE80211_SDATA_IN_DRIVER)
			vif = &sdata->vif;
	}

	return local->ops->testmode_cmd(&local->hw, vif, data, len);
}

static int ieee80211_testmode_dump(struct wiphy *wiphy,
				   struct sk_buff *skb,
				   struct netlink_callback *cb,
				   void *data, int len)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	if (!local->ops->testmode_dump)
		return -EOPNOTSUPP;

	return local->ops->testmode_dump(&local->hw, skb, cb, data, len);
}
#endif

int __ieee80211_request_smps_mgd(struct ieee80211_sub_if_data *sdata,
				 enum ieee80211_smps_mode smps_mode)
{
	const u8 *ap;
	enum ieee80211_smps_mode old_req;
	int err;
	struct sta_info *sta;
	bool tdls_peer_found = false;

	lockdep_assert_held(&sdata->wdev.mtx);

	if (WARN_ON_ONCE(sdata->vif.type != NL80211_IFTYPE_STATION))
		return -EINVAL;

	old_req = sdata->u.mgd.req_smps;
	sdata->u.mgd.req_smps = smps_mode;

	if (old_req == smps_mode &&
	    smps_mode != IEEE80211_SMPS_AUTOMATIC)
		return 0;

	/*
	 * If not associated, or current association is not an HT
	 * association, there's no need to do anything, just store
	 * the new value until we associate.
	 */
	if (!sdata->u.mgd.associated ||
	    sdata->vif.bss_conf.chandef.width == NL80211_CHAN_WIDTH_20_NOHT)
		return 0;

	ap = sdata->u.mgd.associated->bssid;

	rcu_read_lock();
	list_for_each_entry_rcu(sta, &sdata->local->sta_list, list) {
		if (!sta->sta.tdls || sta->sdata != sdata || !sta->uploaded ||
		    !test_sta_flag(sta, WLAN_STA_AUTHORIZED))
			continue;

		tdls_peer_found = true;
		break;
	}
	rcu_read_unlock();

	if (smps_mode == IEEE80211_SMPS_AUTOMATIC) {
		if (tdls_peer_found || !sdata->u.mgd.powersave)
			smps_mode = IEEE80211_SMPS_OFF;
		else
			smps_mode = IEEE80211_SMPS_DYNAMIC;
	}

	/* send SM PS frame to AP */
	err = ieee80211_send_smps_action(sdata, smps_mode,
					 ap, ap);
	if (err)
		sdata->u.mgd.req_smps = old_req;
	else if (smps_mode != IEEE80211_SMPS_OFF && tdls_peer_found)
		ieee80211_teardown_tdls_peers(sdata);

	return err;
}

static int ieee80211_set_power_mgmt(struct wiphy *wiphy, struct net_device *dev,
				    bool enabled, int timeout)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	if (!ieee80211_hw_check(&local->hw, SUPPORTS_PS))
		return -EOPNOTSUPP;

	if (enabled == sdata->u.mgd.powersave &&
	    timeout == local->dynamic_ps_forced_timeout)
		return 0;

	sdata->u.mgd.powersave = enabled;
	local->dynamic_ps_forced_timeout = timeout;

	/* no change, but if automatic follow powersave */
	sdata_lock(sdata);
	__ieee80211_request_smps_mgd(sdata, sdata->u.mgd.req_smps);
	sdata_unlock(sdata);

	if (ieee80211_hw_check(&local->hw, SUPPORTS_DYNAMIC_PS))
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);

	ieee80211_recalc_ps(local);
	ieee80211_recalc_ps_vif(sdata);
	ieee80211_check_fast_rx_iface(sdata);

	return 0;
}

static int ieee80211_set_cqm_rssi_config(struct wiphy *wiphy,
					 struct net_device *dev,
					 s32 rssi_thold, u32 rssi_hyst)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_vif *vif = &sdata->vif;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;

	if (rssi_thold == bss_conf->cqm_rssi_thold &&
	    rssi_hyst == bss_conf->cqm_rssi_hyst)
		return 0;

	if (sdata->vif.driver_flags & IEEE80211_VIF_BEACON_FILTER &&
	    !(sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_CQM_RSSI))
		return -EOPNOTSUPP;

	bss_conf->cqm_rssi_thold = rssi_thold;
	bss_conf->cqm_rssi_hyst = rssi_hyst;
	bss_conf->cqm_rssi_low = 0;
	bss_conf->cqm_rssi_high = 0;
	sdata->u.mgd.last_cqm_event_signal = 0;

	/* tell the driver upon association, unless already associated */
	if (sdata->u.mgd.associated &&
	    sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_CQM_RSSI)
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_CQM);

	return 0;
}

static int ieee80211_set_cqm_rssi_range_config(struct wiphy *wiphy,
					       struct net_device *dev,
					       s32 rssi_low, s32 rssi_high)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_vif *vif = &sdata->vif;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;

	if (sdata->vif.driver_flags & IEEE80211_VIF_BEACON_FILTER)
		return -EOPNOTSUPP;

	bss_conf->cqm_rssi_low = rssi_low;
	bss_conf->cqm_rssi_high = rssi_high;
	bss_conf->cqm_rssi_thold = 0;
	bss_conf->cqm_rssi_hyst = 0;
	sdata->u.mgd.last_cqm_event_signal = 0;

	/* tell the driver upon association, unless already associated */
	if (sdata->u.mgd.associated &&
	    sdata->vif.driver_flags & IEEE80211_VIF_SUPPORTS_CQM_RSSI)
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_CQM);

	return 0;
}

static int ieee80211_set_bitrate_mask(struct wiphy *wiphy,
				      struct net_device *dev,
				      const u8 *addr,
				      const struct cfg80211_bitrate_mask *mask)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	int i, ret;

	if (!ieee80211_sdata_running(sdata))
		return -ENETDOWN;

	/*
	 * If active validate the setting and reject it if it doesn't leave
	 * at least one basic rate usable, since we really have to be able
	 * to send something, and if we're an AP we have to be able to do
	 * so at a basic rate so that all clients can receive it.
	 */
	if (rcu_access_pointer(sdata->vif.chanctx_conf) &&
	    sdata->vif.bss_conf.chandef.chan) {
		u32 basic_rates = sdata->vif.bss_conf.basic_rates;
		enum nl80211_band band = sdata->vif.bss_conf.chandef.chan->band;

		if (!(mask->control[band].legacy & basic_rates))
			return -EINVAL;
	}

	if (ieee80211_hw_check(&local->hw, HAS_RATE_CONTROL)) {
		ret = drv_set_bitrate_mask(local, sdata, mask);
		if (ret)
			return ret;
	}

	for (i = 0; i < NUM_NL80211_BANDS; i++) {
		struct ieee80211_supported_band *sband = wiphy->bands[i];
		int j;

		sdata->rc_rateidx_mask[i] = mask->control[i].legacy;
		memcpy(sdata->rc_rateidx_mcs_mask[i], mask->control[i].ht_mcs,
		       sizeof(mask->control[i].ht_mcs));
		memcpy(sdata->rc_rateidx_vht_mcs_mask[i],
		       mask->control[i].vht_mcs,
		       sizeof(mask->control[i].vht_mcs));

		sdata->rc_has_mcs_mask[i] = false;
		sdata->rc_has_vht_mcs_mask[i] = false;
		if (!sband)
			continue;

		for (j = 0; j < IEEE80211_HT_MCS_MASK_LEN; j++) {
			if (~sdata->rc_rateidx_mcs_mask[i][j]) {
				sdata->rc_has_mcs_mask[i] = true;
				break;
			}
		}

		for (j = 0; j < NL80211_VHT_NSS_MAX; j++) {
			if (~sdata->rc_rateidx_vht_mcs_mask[i][j]) {
				sdata->rc_has_vht_mcs_mask[i] = true;
				break;
			}
		}
	}

	return 0;
}

static int ieee80211_start_radar_detection(struct wiphy *wiphy,
					   struct net_device *dev,
					   struct cfg80211_chan_def *chandef,
					   u32 cac_time_ms)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	int err;

	mutex_lock(&local->mtx);
	if (!list_empty(&local->roc_list) || local->scanning) {
		err = -EBUSY;
		goto out_unlock;
	}

	/* whatever, but channel contexts should not complain about that one */
	sdata->smps_mode = IEEE80211_SMPS_OFF;
	sdata->needed_rx_chains = local->rx_chains;

	err = ieee80211_vif_use_channel(sdata, chandef,
					IEEE80211_CHANCTX_SHARED);
	if (err)
		goto out_unlock;

	ieee80211_queue_delayed_work(&sdata->local->hw,
				     &sdata->dfs_cac_timer_work,
				     msecs_to_jiffies(cac_time_ms));

 out_unlock:
	mutex_unlock(&local->mtx);
	return err;
}

static void ieee80211_end_cac(struct wiphy *wiphy,
			      struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;

	mutex_lock(&local->mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		/* it might be waiting for the local->mtx, but then
		 * by the time it gets it, sdata->wdev.cac_started
		 * will no longer be true
		 */
		cancel_delayed_work(&sdata->dfs_cac_timer_work);

		if (sdata->wdev.cac_started) {
			ieee80211_vif_release_channel(sdata);
			sdata->wdev.cac_started = false;
		}
	}
	mutex_unlock(&local->mtx);
}

static struct cfg80211_beacon_data *
cfg80211_beacon_dup(struct cfg80211_beacon_data *beacon)
{
	struct cfg80211_beacon_data *new_beacon;
	u8 *pos;
	int len;

	len = beacon->head_len + beacon->tail_len + beacon->beacon_ies_len +
	      beacon->proberesp_ies_len + beacon->assocresp_ies_len +
	      beacon->probe_resp_len + beacon->lci_len + beacon->civicloc_len;

	new_beacon = kzalloc(sizeof(*new_beacon) + len, GFP_KERNEL);
	if (!new_beacon)
		return NULL;

	pos = (u8 *)(new_beacon + 1);
	if (beacon->head_len) {
		new_beacon->head_len = beacon->head_len;
		new_beacon->head = pos;
		memcpy(pos, beacon->head, beacon->head_len);
		pos += beacon->head_len;
	}
	if (beacon->tail_len) {
		new_beacon->tail_len = beacon->tail_len;
		new_beacon->tail = pos;
		memcpy(pos, beacon->tail, beacon->tail_len);
		pos += beacon->tail_len;
	}
	if (beacon->beacon_ies_len) {
		new_beacon->beacon_ies_len = beacon->beacon_ies_len;
		new_beacon->beacon_ies = pos;
		memcpy(pos, beacon->beacon_ies, beacon->beacon_ies_len);
		pos += beacon->beacon_ies_len;
	}
	if (beacon->proberesp_ies_len) {
		new_beacon->proberesp_ies_len = beacon->proberesp_ies_len;
		new_beacon->proberesp_ies = pos;
		memcpy(pos, beacon->proberesp_ies, beacon->proberesp_ies_len);
		pos += beacon->proberesp_ies_len;
	}
	if (beacon->assocresp_ies_len) {
		new_beacon->assocresp_ies_len = beacon->assocresp_ies_len;
		new_beacon->assocresp_ies = pos;
		memcpy(pos, beacon->assocresp_ies, beacon->assocresp_ies_len);
		pos += beacon->assocresp_ies_len;
	}
	if (beacon->probe_resp_len) {
		new_beacon->probe_resp_len = beacon->probe_resp_len;
		new_beacon->probe_resp = pos;
		memcpy(pos, beacon->probe_resp, beacon->probe_resp_len);
		pos += beacon->probe_resp_len;
	}

	/* might copy -1, meaning no changes requested */
	new_beacon->ftm_responder = beacon->ftm_responder;
	if (beacon->lci) {
		new_beacon->lci_len = beacon->lci_len;
		new_beacon->lci = pos;
		memcpy(pos, beacon->lci, beacon->lci_len);
		pos += beacon->lci_len;
	}
	if (beacon->civicloc) {
		new_beacon->civicloc_len = beacon->civicloc_len;
		new_beacon->civicloc = pos;
		memcpy(pos, beacon->civicloc, beacon->civicloc_len);
		pos += beacon->civicloc_len;
	}

	return new_beacon;
}

void ieee80211_csa_finish(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	ieee80211_queue_work(&sdata->local->hw,
			     &sdata->csa_finalize_work);
}
EXPORT_SYMBOL(ieee80211_csa_finish);

static int ieee80211_set_after_csa_beacon(struct ieee80211_sub_if_data *sdata,
					  u32 *changed)
{
	int err;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP:
		err = ieee80211_assign_beacon(sdata, sdata->u.ap.next_beacon,
					      NULL);
		kfree(sdata->u.ap.next_beacon);
		sdata->u.ap.next_beacon = NULL;

		if (err < 0)
			return err;
		*changed |= err;
		break;
	case NL80211_IFTYPE_ADHOC:
		err = ieee80211_ibss_finish_csa(sdata);
		if (err < 0)
			return err;
		*changed |= err;
		break;
#ifdef CONFIG_MAC80211_MESH
	case NL80211_IFTYPE_MESH_POINT:
		err = ieee80211_mesh_finish_csa(sdata);
		if (err < 0)
			return err;
		*changed |= err;
		break;
#endif
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	return 0;
}

static int __ieee80211_csa_finalize(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	u32 changed = 0;
	int err;

	sdata_assert_lock(sdata);
	lockdep_assert_held(&local->mtx);
	lockdep_assert_held(&local->chanctx_mtx);

	/*
	 * using reservation isn't immediate as it may be deferred until later
	 * with multi-vif. once reservation is complete it will re-schedule the
	 * work with no reserved_chanctx so verify chandef to check if it
	 * completed successfully
	 */

	if (sdata->reserved_chanctx) {
		/*
		 * with multi-vif csa driver may call ieee80211_csa_finish()
		 * many times while waiting for other interfaces to use their
		 * reservations
		 */
		if (sdata->reserved_ready)
			return 0;

		return ieee80211_vif_use_reserved_context(sdata);
	}

	if (!cfg80211_chandef_identical(&sdata->vif.bss_conf.chandef,
					&sdata->csa_chandef))
		return -EINVAL;

	sdata->vif.csa_active = false;

	err = ieee80211_set_after_csa_beacon(sdata, &changed);
	if (err)
		return err;

	ieee80211_bss_info_change_notify(sdata, changed);

	if (sdata->csa_block_tx) {
		ieee80211_wake_vif_queues(local, sdata,
					  IEEE80211_QUEUE_STOP_REASON_CSA);
		sdata->csa_block_tx = false;
	}

	err = drv_post_channel_switch(sdata);
	if (err)
		return err;

	cfg80211_ch_switch_notify(sdata->dev, &sdata->csa_chandef);

	return 0;
}

static void ieee80211_csa_finalize(struct ieee80211_sub_if_data *sdata)
{
	if (__ieee80211_csa_finalize(sdata)) {
		sdata_info(sdata, "failed to finalize CSA, disconnecting\n");
		cfg80211_stop_iface(sdata->local->hw.wiphy, &sdata->wdev,
				    GFP_KERNEL);
	}
}

void ieee80211_csa_finalize_work(struct work_struct *work)
{
	struct ieee80211_sub_if_data *sdata =
		container_of(work, struct ieee80211_sub_if_data,
			     csa_finalize_work);
	struct ieee80211_local *local = sdata->local;

	sdata_lock(sdata);
	mutex_lock(&local->mtx);
	mutex_lock(&local->chanctx_mtx);

	/* AP might have been stopped while waiting for the lock. */
	if (!sdata->vif.csa_active)
		goto unlock;

	if (!ieee80211_sdata_running(sdata))
		goto unlock;

	ieee80211_csa_finalize(sdata);

unlock:
	mutex_unlock(&local->chanctx_mtx);
	mutex_unlock(&local->mtx);
	sdata_unlock(sdata);
}

static int ieee80211_set_csa_beacon(struct ieee80211_sub_if_data *sdata,
				    struct cfg80211_csa_settings *params,
				    u32 *changed)
{
	struct ieee80211_csa_settings csa = {};
	int err;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP:
		sdata->u.ap.next_beacon =
			cfg80211_beacon_dup(&params->beacon_after);
		if (!sdata->u.ap.next_beacon)
			return -ENOMEM;

		/*
		 * With a count of 0, we don't have to wait for any
		 * TBTT before switching, so complete the CSA
		 * immediately.  In theory, with a count == 1 we
		 * should delay the switch until just before the next
		 * TBTT, but that would complicate things so we switch
		 * immediately too.  If we would delay the switch
		 * until the next TBTT, we would have to set the probe
		 * response here.
		 *
		 * TODO: A channel switch with count <= 1 without
		 * sending a CSA action frame is kind of useless,
		 * because the clients won't know we're changing
		 * channels.  The action frame must be implemented
		 * either here or in the userspace.
		 */
		if (params->count <= 1)
			break;

		if ((params->n_counter_offsets_beacon >
		     IEEE80211_MAX_CSA_COUNTERS_NUM) ||
		    (params->n_counter_offsets_presp >
		     IEEE80211_MAX_CSA_COUNTERS_NUM))
			return -EINVAL;

		csa.counter_offsets_beacon = params->counter_offsets_beacon;
		csa.counter_offsets_presp = params->counter_offsets_presp;
		csa.n_counter_offsets_beacon = params->n_counter_offsets_beacon;
		csa.n_counter_offsets_presp = params->n_counter_offsets_presp;
		csa.count = params->count;

		err = ieee80211_assign_beacon(sdata, &params->beacon_csa, &csa);
		if (err < 0) {
			kfree(sdata->u.ap.next_beacon);
			return err;
		}
		*changed |= err;

		break;
	case NL80211_IFTYPE_ADHOC:
		if (!sdata->vif.bss_conf.ibss_joined)
			return -EINVAL;

		if (params->chandef.width != sdata->u.ibss.chandef.width)
			return -EINVAL;

		switch (params->chandef.width) {
		case NL80211_CHAN_WIDTH_40:
			if (cfg80211_get_chandef_type(&params->chandef) !=
			    cfg80211_get_chandef_type(&sdata->u.ibss.chandef))
				return -EINVAL;
		case NL80211_CHAN_WIDTH_5:
		case NL80211_CHAN_WIDTH_10:
		case NL80211_CHAN_WIDTH_20_NOHT:
		case NL80211_CHAN_WIDTH_20:
			break;
		default:
			return -EINVAL;
		}

		/* changes into another band are not supported */
		if (sdata->u.ibss.chandef.chan->band !=
		    params->chandef.chan->band)
			return -EINVAL;

		/* see comments in the NL80211_IFTYPE_AP block */
		if (params->count > 1) {
			err = ieee80211_ibss_csa_beacon(sdata, params);
			if (err < 0)
				return err;
			*changed |= err;
		}

		ieee80211_send_action_csa(sdata, params);

		break;
#ifdef CONFIG_MAC80211_MESH
	case NL80211_IFTYPE_MESH_POINT: {
		struct ieee80211_if_mesh *ifmsh = &sdata->u.mesh;

		if (params->chandef.width != sdata->vif.bss_conf.chandef.width)
			return -EINVAL;

		/* changes into another band are not supported */
		if (sdata->vif.bss_conf.chandef.chan->band !=
		    params->chandef.chan->band)
			return -EINVAL;

		if (ifmsh->csa_role == IEEE80211_MESH_CSA_ROLE_NONE) {
			ifmsh->csa_role = IEEE80211_MESH_CSA_ROLE_INIT;
			if (!ifmsh->pre_value)
				ifmsh->pre_value = 1;
			else
				ifmsh->pre_value++;
		}

		/* see comments in the NL80211_IFTYPE_AP block */
		if (params->count > 1) {
			err = ieee80211_mesh_csa_beacon(sdata, params);
			if (err < 0) {
				ifmsh->csa_role = IEEE80211_MESH_CSA_ROLE_NONE;
				return err;
			}
			*changed |= err;
		}

		if (ifmsh->csa_role == IEEE80211_MESH_CSA_ROLE_INIT)
			ieee80211_send_action_csa(sdata, params);

		break;
		}
#endif
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
__ieee80211_channel_switch(struct wiphy *wiphy, struct net_device *dev,
			   struct cfg80211_csa_settings *params)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_switch ch_switch;
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *chanctx;
	u32 changed = 0;
	int err;

	sdata_assert_lock(sdata);
	lockdep_assert_held(&local->mtx);

	if (!list_empty(&local->roc_list) || local->scanning)
		return -EBUSY;

	if (sdata->wdev.cac_started)
		return -EBUSY;

	if (cfg80211_chandef_identical(&params->chandef,
				       &sdata->vif.bss_conf.chandef))
		return -EINVAL;

	/* don't allow another channel switch if one is already active. */
	if (sdata->vif.csa_active)
		return -EBUSY;

	mutex_lock(&local->chanctx_mtx);
	conf = rcu_dereference_protected(sdata->vif.chanctx_conf,
					 lockdep_is_held(&local->chanctx_mtx));
	if (!conf) {
		err = -EBUSY;
		goto out;
	}

	chanctx = container_of(conf, struct ieee80211_chanctx, conf);

	ch_switch.timestamp = 0;
	ch_switch.device_timestamp = 0;
	ch_switch.block_tx = params->block_tx;
	ch_switch.chandef = params->chandef;
	ch_switch.count = params->count;

	err = drv_pre_channel_switch(sdata, &ch_switch);
	if (err)
		goto out;

	err = ieee80211_vif_reserve_chanctx(sdata, &params->chandef,
					    chanctx->mode,
					    params->radar_required);
	if (err)
		goto out;

	/* if reservation is invalid then this will fail */
	err = ieee80211_check_combinations(sdata, NULL, chanctx->mode, 0);
	if (err) {
		ieee80211_vif_unreserve_chanctx(sdata);
		goto out;
	}

	err = ieee80211_set_csa_beacon(sdata, params, &changed);
	if (err) {
		ieee80211_vif_unreserve_chanctx(sdata);
		goto out;
	}

	sdata->csa_chandef = params->chandef;
	sdata->csa_block_tx = params->block_tx;
	sdata->vif.csa_active = true;

	if (sdata->csa_block_tx)
		ieee80211_stop_vif_queues(local, sdata,
					  IEEE80211_QUEUE_STOP_REASON_CSA);

	cfg80211_ch_switch_started_notify(sdata->dev, &sdata->csa_chandef,
					  params->count);

	if (changed) {
		ieee80211_bss_info_change_notify(sdata, changed);
		drv_channel_switch_beacon(sdata, &params->chandef);
	} else {
		/* if the beacon didn't change, we can finalize immediately */
		ieee80211_csa_finalize(sdata);
	}

out:
	mutex_unlock(&local->chanctx_mtx);
	return err;
}

int ieee80211_channel_switch(struct wiphy *wiphy, struct net_device *dev,
			     struct cfg80211_csa_settings *params)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	int err;

	mutex_lock(&local->mtx);
	err = __ieee80211_channel_switch(wiphy, dev, params);
	mutex_unlock(&local->mtx);

	return err;
}

u64 ieee80211_mgmt_tx_cookie(struct ieee80211_local *local)
{
	lockdep_assert_held(&local->mtx);

	local->roc_cookie_counter++;

	/* wow, you wrapped 64 bits ... more likely a bug */
	if (WARN_ON(local->roc_cookie_counter == 0))
		local->roc_cookie_counter++;

	return local->roc_cookie_counter;
}

int ieee80211_attach_ack_skb(struct ieee80211_local *local, struct sk_buff *skb,
			     u64 *cookie, gfp_t gfp)
{
	unsigned long spin_flags;
	struct sk_buff *ack_skb;
	int id;

	ack_skb = skb_copy(skb, gfp);
	if (!ack_skb)
		return -ENOMEM;

	spin_lock_irqsave(&local->ack_status_lock, spin_flags);
	id = idr_alloc(&local->ack_status_frames, ack_skb,
		       1, 0x2000, GFP_ATOMIC);
	spin_unlock_irqrestore(&local->ack_status_lock, spin_flags);

	if (id < 0) {
		kfree_skb(ack_skb);
		return -ENOMEM;
	}

	IEEE80211_SKB_CB(skb)->ack_frame_id = id;

	*cookie = ieee80211_mgmt_tx_cookie(local);
	IEEE80211_SKB_CB(ack_skb)->ack.cookie = *cookie;

	return 0;
}

static void ieee80211_mgmt_frame_register(struct wiphy *wiphy,
					  struct wireless_dev *wdev,
					  u16 frame_type, bool reg)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);

	switch (frame_type) {
	case IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ:
		if (reg) {
			local->probe_req_reg++;
			sdata->vif.probe_req_reg++;
		} else {
			if (local->probe_req_reg)
				local->probe_req_reg--;

			if (sdata->vif.probe_req_reg)
				sdata->vif.probe_req_reg--;
		}

		if (!local->open_count)
			break;

		if (sdata->vif.probe_req_reg == 1)
			drv_config_iface_filter(local, sdata, FIF_PROBE_REQ,
						FIF_PROBE_REQ);
		else if (sdata->vif.probe_req_reg == 0)
			drv_config_iface_filter(local, sdata, 0,
						FIF_PROBE_REQ);

		ieee80211_configure_filter(local);
		break;
	default:
		break;
	}
}

static int ieee80211_set_antenna(struct wiphy *wiphy, u32 tx_ant, u32 rx_ant)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	if (local->started)
		return -EOPNOTSUPP;

	return drv_set_antenna(local, tx_ant, rx_ant);
}

static int ieee80211_get_antenna(struct wiphy *wiphy, u32 *tx_ant, u32 *rx_ant)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	return drv_get_antenna(local, tx_ant, rx_ant);
}

static int ieee80211_set_rekey_data(struct wiphy *wiphy,
				    struct net_device *dev,
				    struct cfg80211_gtk_rekey_data *data)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (!local->ops->set_rekey_data)
		return -EOPNOTSUPP;

	drv_set_rekey_data(local, sdata, data);

	return 0;
}

static int ieee80211_probe_client(struct wiphy *wiphy, struct net_device *dev,
				  const u8 *peer, u64 *cookie)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_qos_hdr *nullfunc;
	struct sk_buff *skb;
	int size = sizeof(*nullfunc);
	__le16 fc;
	bool qos;
	struct ieee80211_tx_info *info;
	struct sta_info *sta;
	struct ieee80211_chanctx_conf *chanctx_conf;
	enum nl80211_band band;
	int ret;

	/* the lock is needed to assign the cookie later */
	mutex_lock(&local->mtx);

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (WARN_ON(!chanctx_conf)) {
		ret = -EINVAL;
		goto unlock;
	}
	band = chanctx_conf->def.chan->band;
	sta = sta_info_get_bss(sdata, peer);
	if (sta) {
		qos = sta->sta.wme;
	} else {
		ret = -ENOLINK;
		goto unlock;
	}

	if (qos) {
		fc = cpu_to_le16(IEEE80211_FTYPE_DATA |
				 IEEE80211_STYPE_QOS_NULLFUNC |
				 IEEE80211_FCTL_FROMDS);
	} else {
		size -= 2;
		fc = cpu_to_le16(IEEE80211_FTYPE_DATA |
				 IEEE80211_STYPE_NULLFUNC |
				 IEEE80211_FCTL_FROMDS);
	}

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + size);
	if (!skb) {
		ret = -ENOMEM;
		goto unlock;
	}

	skb->dev = dev;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	nullfunc = skb_put(skb, size);
	nullfunc->frame_control = fc;
	nullfunc->duration_id = 0;
	memcpy(nullfunc->addr1, sta->sta.addr, ETH_ALEN);
	memcpy(nullfunc->addr2, sdata->vif.addr, ETH_ALEN);
	memcpy(nullfunc->addr3, sdata->vif.addr, ETH_ALEN);
	nullfunc->seq_ctrl = 0;

	info = IEEE80211_SKB_CB(skb);

	info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS |
		       IEEE80211_TX_INTFL_NL80211_FRAME_TX;
	info->band = band;

	skb_set_queue_mapping(skb, IEEE80211_AC_VO);
	skb->priority = 7;
	if (qos)
		nullfunc->qos_ctrl = cpu_to_le16(7);

	ret = ieee80211_attach_ack_skb(local, skb, cookie, GFP_ATOMIC);
	if (ret) {
		kfree_skb(skb);
		goto unlock;
	}

	local_bh_disable();
	ieee80211_xmit(sdata, sta, skb, 0);
	local_bh_enable();

	ret = 0;
unlock:
	rcu_read_unlock();
	mutex_unlock(&local->mtx);

	return ret;
}

static int ieee80211_cfg_get_channel(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     struct cfg80211_chan_def *chandef)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_chanctx_conf *chanctx_conf;
	int ret = -ENODATA;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (chanctx_conf) {
		*chandef = sdata->vif.bss_conf.chandef;
		ret = 0;
	} else if (local->open_count > 0 &&
		   local->open_count == local->monitors &&
		   sdata->vif.type == NL80211_IFTYPE_MONITOR) {
		if (local->use_chanctx)
			*chandef = local->monitor_chandef;
		else
			*chandef = local->_oper_chandef;
		ret = 0;
	}
	rcu_read_unlock();

	return ret;
}

#ifdef CONFIG_PM
static void ieee80211_set_wakeup(struct wiphy *wiphy, bool enabled)
{
	drv_set_wakeup(wiphy_priv(wiphy), enabled);
}
#endif

static int ieee80211_set_qos_map(struct wiphy *wiphy,
				 struct net_device *dev,
				 struct cfg80211_qos_map *qos_map)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct mac80211_qos_map *new_qos_map, *old_qos_map;

	if (qos_map) {
		new_qos_map = kzalloc(sizeof(*new_qos_map), GFP_KERNEL);
		if (!new_qos_map)
			return -ENOMEM;
		memcpy(&new_qos_map->qos_map, qos_map, sizeof(*qos_map));
	} else {
		/* A NULL qos_map was passed to disable QoS mapping */
		new_qos_map = NULL;
	}

	old_qos_map = sdata_dereference(sdata->qos_map, sdata);
	rcu_assign_pointer(sdata->qos_map, new_qos_map);
	if (old_qos_map)
		kfree_rcu(old_qos_map, rcu_head);

	return 0;
}

static int ieee80211_set_ap_chanwidth(struct wiphy *wiphy,
				      struct net_device *dev,
				      struct cfg80211_chan_def *chandef)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int ret;
	u32 changed = 0;

	ret = ieee80211_vif_change_bandwidth(sdata, chandef, &changed);
	if (ret == 0)
		ieee80211_bss_info_change_notify(sdata, changed);

	return ret;
}

static int ieee80211_add_tx_ts(struct wiphy *wiphy, struct net_device *dev,
			       u8 tsid, const u8 *peer, u8 up,
			       u16 admitted_time)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	int ac = ieee802_1d_to_ac[up];

	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	if (!(sdata->wmm_acm & BIT(up)))
		return -EINVAL;

	if (ifmgd->tx_tspec[ac].admitted_time)
		return -EBUSY;

	if (admitted_time) {
		ifmgd->tx_tspec[ac].admitted_time = 32 * admitted_time;
		ifmgd->tx_tspec[ac].tsid = tsid;
		ifmgd->tx_tspec[ac].up = up;
	}

	return 0;
}

static int ieee80211_del_tx_ts(struct wiphy *wiphy, struct net_device *dev,
			       u8 tsid, const u8 *peer)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct ieee80211_local *local = wiphy_priv(wiphy);
	int ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_sta_tx_tspec *tx_tspec = &ifmgd->tx_tspec[ac];

		/* skip unused entries */
		if (!tx_tspec->admitted_time)
			continue;

		if (tx_tspec->tsid != tsid)
			continue;

		/* due to this new packets will be reassigned to non-ACM ACs */
		tx_tspec->up = -1;

		/* Make sure that all packets have been sent to avoid to
		 * restore the QoS params on packets that are still on the
		 * queues.
		 */
		synchronize_net();
		ieee80211_flush_queues(local, sdata, false);

		/* restore the normal QoS parameters
		 * (unconditionally to avoid races)
		 */
		tx_tspec->action = TX_TSPEC_ACTION_STOP_DOWNGRADE;
		tx_tspec->downgraded = false;
		ieee80211_sta_handle_tspec_ac_params(sdata);

		/* finally clear all the data */
		memset(tx_tspec, 0, sizeof(*tx_tspec));

		return 0;
	}

	return -ENOENT;
}

void ieee80211_nan_func_terminated(struct ieee80211_vif *vif,
				   u8 inst_id,
				   enum nl80211_nan_func_term_reason reason,
				   gfp_t gfp)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct cfg80211_nan_func *func;
	u64 cookie;

	if (WARN_ON(vif->type != NL80211_IFTYPE_NAN))
		return;

	spin_lock_bh(&sdata->u.nan.func_lock);

	func = idr_find(&sdata->u.nan.function_inst_ids, inst_id);
	if (WARN_ON(!func)) {
		spin_unlock_bh(&sdata->u.nan.func_lock);
		return;
	}

	cookie = func->cookie;
	idr_remove(&sdata->u.nan.function_inst_ids, inst_id);

	spin_unlock_bh(&sdata->u.nan.func_lock);

	cfg80211_free_nan_func(func);

	cfg80211_nan_func_terminated(ieee80211_vif_to_wdev(vif), inst_id,
				     reason, cookie, gfp);
}
EXPORT_SYMBOL(ieee80211_nan_func_terminated);

void ieee80211_nan_func_match(struct ieee80211_vif *vif,
			      struct cfg80211_nan_match_params *match,
			      gfp_t gfp)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct cfg80211_nan_func *func;

	if (WARN_ON(vif->type != NL80211_IFTYPE_NAN))
		return;

	spin_lock_bh(&sdata->u.nan.func_lock);

	func = idr_find(&sdata->u.nan.function_inst_ids,  match->inst_id);
	if (WARN_ON(!func)) {
		spin_unlock_bh(&sdata->u.nan.func_lock);
		return;
	}
	match->cookie = func->cookie;

	spin_unlock_bh(&sdata->u.nan.func_lock);

	cfg80211_nan_match(ieee80211_vif_to_wdev(vif), match, gfp);
}
EXPORT_SYMBOL(ieee80211_nan_func_match);

static int ieee80211_set_multicast_to_unicast(struct wiphy *wiphy,
					      struct net_device *dev,
					      const bool enabled)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	sdata->u.ap.multicast_to_unicast = enabled;

	return 0;
}

void ieee80211_fill_txq_stats(struct cfg80211_txq_stats *txqstats,
			      struct txq_info *txqi)
{
	if (!(txqstats->filled & BIT(NL80211_TXQ_STATS_BACKLOG_BYTES))) {
		txqstats->filled |= BIT(NL80211_TXQ_STATS_BACKLOG_BYTES);
		txqstats->backlog_bytes = txqi->tin.backlog_bytes;
	}

	if (!(txqstats->filled & BIT(NL80211_TXQ_STATS_BACKLOG_PACKETS))) {
		txqstats->filled |= BIT(NL80211_TXQ_STATS_BACKLOG_PACKETS);
		txqstats->backlog_packets = txqi->tin.backlog_packets;
	}

	if (!(txqstats->filled & BIT(NL80211_TXQ_STATS_FLOWS))) {
		txqstats->filled |= BIT(NL80211_TXQ_STATS_FLOWS);
		txqstats->flows = txqi->tin.flows;
	}

	if (!(txqstats->filled & BIT(NL80211_TXQ_STATS_DROPS))) {
		txqstats->filled |= BIT(NL80211_TXQ_STATS_DROPS);
		txqstats->drops = txqi->cstats.drop_count;
	}

	if (!(txqstats->filled & BIT(NL80211_TXQ_STATS_ECN_MARKS))) {
		txqstats->filled |= BIT(NL80211_TXQ_STATS_ECN_MARKS);
		txqstats->ecn_marks = txqi->cstats.ecn_mark;
	}

	if (!(txqstats->filled & BIT(NL80211_TXQ_STATS_OVERLIMIT))) {
		txqstats->filled |= BIT(NL80211_TXQ_STATS_OVERLIMIT);
		txqstats->overlimit = txqi->tin.overlimit;
	}

	if (!(txqstats->filled & BIT(NL80211_TXQ_STATS_COLLISIONS))) {
		txqstats->filled |= BIT(NL80211_TXQ_STATS_COLLISIONS);
		txqstats->collisions = txqi->tin.collisions;
	}

	if (!(txqstats->filled & BIT(NL80211_TXQ_STATS_TX_BYTES))) {
		txqstats->filled |= BIT(NL80211_TXQ_STATS_TX_BYTES);
		txqstats->tx_bytes = txqi->tin.tx_bytes;
	}

	if (!(txqstats->filled & BIT(NL80211_TXQ_STATS_TX_PACKETS))) {
		txqstats->filled |= BIT(NL80211_TXQ_STATS_TX_PACKETS);
		txqstats->tx_packets = txqi->tin.tx_packets;
	}
}

static int ieee80211_get_txq_stats(struct wiphy *wiphy,
				   struct wireless_dev *wdev,
				   struct cfg80211_txq_stats *txqstats)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata;
	int ret = 0;

	if (!local->ops->wake_tx_queue)
		return 1;

	spin_lock_bh(&local->fq.lock);
	rcu_read_lock();

	if (wdev) {
		sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
		if (!sdata->vif.txq) {
			ret = 1;
			goto out;
		}
		ieee80211_fill_txq_stats(txqstats, to_txq_info(sdata->vif.txq));
	} else {
		/* phy stats */
		txqstats->filled |= BIT(NL80211_TXQ_STATS_BACKLOG_PACKETS) |
				    BIT(NL80211_TXQ_STATS_BACKLOG_BYTES) |
				    BIT(NL80211_TXQ_STATS_OVERLIMIT) |
				    BIT(NL80211_TXQ_STATS_OVERMEMORY) |
				    BIT(NL80211_TXQ_STATS_COLLISIONS) |
				    BIT(NL80211_TXQ_STATS_MAX_FLOWS);
		txqstats->backlog_packets = local->fq.backlog;
		txqstats->backlog_bytes = local->fq.memory_usage;
		txqstats->overlimit = local->fq.overlimit;
		txqstats->overmemory = local->fq.overmemory;
		txqstats->collisions = local->fq.collisions;
		txqstats->max_flows = local->fq.flows_cnt;
	}

out:
	rcu_read_unlock();
	spin_unlock_bh(&local->fq.lock);

	return ret;
}

static int
ieee80211_get_ftm_responder_stats(struct wiphy *wiphy,
				  struct net_device *dev,
				  struct cfg80211_ftm_responder_stats *ftm_stats)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	return drv_get_ftm_responder_stats(local, sdata, ftm_stats);
}

static int
ieee80211_start_pmsr(struct wiphy *wiphy, struct wireless_dev *dev,
		     struct cfg80211_pmsr_request *request)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(dev);

	return drv_start_pmsr(local, sdata, request);
}

static void
ieee80211_abort_pmsr(struct wiphy *wiphy, struct wireless_dev *dev,
		     struct cfg80211_pmsr_request *request)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(dev);

	return drv_abort_pmsr(local, sdata, request);
}

static int ieee80211_set_tid_config(struct wiphy *wiphy,
				    struct net_device *dev,
				    struct cfg80211_tid_config *tid_conf)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sta_info *sta;
	int ret;

	if (!sdata->local->ops->set_tid_config)
		return -EOPNOTSUPP;

	if (!tid_conf->peer)
		return drv_set_tid_config(sdata->local, sdata, NULL, tid_conf);

	mutex_lock(&sdata->local->sta_mtx);
	sta = sta_info_get_bss(sdata, tid_conf->peer);
	if (!sta) {
		mutex_unlock(&sdata->local->sta_mtx);
		return -ENOENT;
	}

	ret = drv_set_tid_config(sdata->local, sdata, &sta->sta, tid_conf);
	mutex_unlock(&sdata->local->sta_mtx);

	return ret;
}

static int ieee80211_reset_tid_config(struct wiphy *wiphy,
				      struct net_device *dev,
				      const u8 *peer, u8 tid)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sta_info *sta;
	int ret;

	if (!sdata->local->ops->reset_tid_config)
		return -EOPNOTSUPP;

	if (!peer)
		return drv_reset_tid_config(sdata->local, sdata, NULL, tid);

	mutex_lock(&sdata->local->sta_mtx);
	sta = sta_info_get_bss(sdata, peer);
	if (!sta) {
		mutex_unlock(&sdata->local->sta_mtx);
		return -ENOENT;
	}

	ret = drv_reset_tid_config(sdata->local, sdata, &sta->sta, tid);
	mutex_unlock(&sdata->local->sta_mtx);

	return ret;
}

const struct cfg80211_ops mac80211_config_ops = {
	.add_virtual_intf = ieee80211_add_iface,
	.del_virtual_intf = ieee80211_del_iface,
	.change_virtual_intf = ieee80211_change_iface,
	.start_p2p_device = ieee80211_start_p2p_device,
	.stop_p2p_device = ieee80211_stop_p2p_device,
	.add_key = ieee80211_add_key,
	.del_key = ieee80211_del_key,
	.get_key = ieee80211_get_key,
	.set_default_key = ieee80211_config_default_key,
	.set_default_mgmt_key = ieee80211_config_default_mgmt_key,
	.set_default_beacon_key = ieee80211_config_default_beacon_key,
	.start_ap = ieee80211_start_ap,
	.change_beacon = ieee80211_change_beacon,
	.stop_ap = ieee80211_stop_ap,
	.add_station = ieee80211_add_station,
	.del_station = ieee80211_del_station,
	.change_station = ieee80211_change_station,
	.get_station = ieee80211_get_station,
	.dump_station = ieee80211_dump_station,
	.dump_survey = ieee80211_dump_survey,
#ifdef CONFIG_MAC80211_MESH
	.add_mpath = ieee80211_add_mpath,
	.del_mpath = ieee80211_del_mpath,
	.change_mpath = ieee80211_change_mpath,
	.get_mpath = ieee80211_get_mpath,
	.dump_mpath = ieee80211_dump_mpath,
	.get_mpp = ieee80211_get_mpp,
	.dump_mpp = ieee80211_dump_mpp,
	.update_mesh_config = ieee80211_update_mesh_config,
	.get_mesh_config = ieee80211_get_mesh_config,
	.join_mesh = ieee80211_join_mesh,
	.leave_mesh = ieee80211_leave_mesh,
#endif
	.join_ocb = ieee80211_join_ocb,
	.leave_ocb = ieee80211_leave_ocb,
	.change_bss = ieee80211_change_bss,
	.set_txq_params = ieee80211_set_txq_params,
	.set_monitor_channel = ieee80211_set_monitor_channel,
	.suspend = ieee80211_suspend,
	.resume = ieee80211_resume,
	.scan = ieee80211_scan,
	.abort_scan = ieee80211_abort_scan,
	.sched_scan_start = ieee80211_sched_scan_start,
	.sched_scan_stop = ieee80211_sched_scan_stop,
	.auth = ieee80211_auth,
	.assoc = ieee80211_assoc,
	.deauth = ieee80211_deauth,
	.disassoc = ieee80211_disassoc,
	.join_ibss = ieee80211_join_ibss,
	.leave_ibss = ieee80211_leave_ibss,
	.set_mcast_rate = ieee80211_set_mcast_rate,
	.set_wiphy_params = ieee80211_set_wiphy_params,
	.set_tx_power = ieee80211_set_tx_power,
	.get_tx_power = ieee80211_get_tx_power,
	.set_wds_peer = ieee80211_set_wds_peer,
	.rfkill_poll = ieee80211_rfkill_poll,
	CFG80211_TESTMODE_CMD(ieee80211_testmode_cmd)
	CFG80211_TESTMODE_DUMP(ieee80211_testmode_dump)
	.set_power_mgmt = ieee80211_set_power_mgmt,
	.set_bitrate_mask = ieee80211_set_bitrate_mask,
	.remain_on_channel = ieee80211_remain_on_channel,
	.cancel_remain_on_channel = ieee80211_cancel_remain_on_channel,
	.mgmt_tx = ieee80211_mgmt_tx,
	.mgmt_tx_cancel_wait = ieee80211_mgmt_tx_cancel_wait,
	.set_cqm_rssi_config = ieee80211_set_cqm_rssi_config,
	.set_cqm_rssi_range_config = ieee80211_set_cqm_rssi_range_config,
	.mgmt_frame_register = ieee80211_mgmt_frame_register,
	.set_antenna = ieee80211_set_antenna,
	.get_antenna = ieee80211_get_antenna,
	.set_rekey_data = ieee80211_set_rekey_data,
	.tdls_oper = ieee80211_tdls_oper,
	.tdls_mgmt = ieee80211_tdls_mgmt,
	.tdls_channel_switch = ieee80211_tdls_channel_switch,
	.tdls_cancel_channel_switch = ieee80211_tdls_cancel_channel_switch,
	.probe_client = ieee80211_probe_client,
	.set_noack_map = ieee80211_set_noack_map,
#ifdef CONFIG_PM
	.set_wakeup = ieee80211_set_wakeup,
#endif
	.get_channel = ieee80211_cfg_get_channel,
	.start_radar_detection = ieee80211_start_radar_detection,
	.end_cac = ieee80211_end_cac,
	.channel_switch = ieee80211_channel_switch,
	.set_qos_map = ieee80211_set_qos_map,
	.set_ap_chanwidth = ieee80211_set_ap_chanwidth,
	.add_tx_ts = ieee80211_add_tx_ts,
	.del_tx_ts = ieee80211_del_tx_ts,
	.start_nan = ieee80211_start_nan,
	.stop_nan = ieee80211_stop_nan,
	.nan_change_conf = ieee80211_nan_change_conf,
	.add_nan_func = ieee80211_add_nan_func,
	.del_nan_func = ieee80211_del_nan_func,
	.set_multicast_to_unicast = ieee80211_set_multicast_to_unicast,
	.tx_control_port = ieee80211_tx_control_port,
	.get_txq_stats = ieee80211_get_txq_stats,
	.get_ftm_responder_stats = ieee80211_get_ftm_responder_stats,
	.start_pmsr = ieee80211_start_pmsr,
	.abort_pmsr = ieee80211_abort_pmsr,
	.probe_mesh_link = ieee80211_probe_mesh_link,
	.set_tid_config = ieee80211_set_tid_config,
	.reset_tid_config = ieee80211_reset_tid_config,
};
