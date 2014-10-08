/*
 * mac80211 configuration hooks for cfg80211
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 *
 * This file is GPLv2 as found in COPYING.
 */

#include <linux/ieee80211.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <net/net_namespace.h>
#include <linux/rcupdate.h>
#include <linux/if_ether.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"
#include "cfg.h"
#include "rate.h"
#include "mesh.h"

static struct wireless_dev *ieee80211_add_iface(struct wiphy *wiphy,
						const char *name,
						enum nl80211_iftype type,
						u32 *flags,
						struct vif_params *params)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct wireless_dev *wdev;
	struct ieee80211_sub_if_data *sdata;
	int err;

	err = ieee80211_if_add(local, name, &wdev, type, params);
	if (err)
		return ERR_PTR(err);

	if (type == NL80211_IFTYPE_MONITOR && flags) {
		sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
		sdata->u.mntr_flags = *flags;
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
				  enum nl80211_iftype type, u32 *flags,
				  struct vif_params *params)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int ret;

	ret = ieee80211_if_change_type(sdata, type);
	if (ret)
		return ret;

	if (type == NL80211_IFTYPE_AP_VLAN &&
	    params && params->use_4addr == 0)
		RCU_INIT_POINTER(sdata->u.vlan.sta, NULL);
	else if (type == NL80211_IFTYPE_STATION &&
		 params && params->use_4addr >= 0)
		sdata->u.mgd.use_4addr = params->use_4addr;

	if (sdata->vif.type == NL80211_IFTYPE_MONITOR && flags) {
		struct ieee80211_local *local = sdata->local;

		if (ieee80211_sdata_running(sdata)) {
			u32 mask = MONITOR_FLAG_COOK_FRAMES |
				   MONITOR_FLAG_ACTIVE;

			/*
			 * Prohibit MONITOR_FLAG_COOK_FRAMES and
			 * MONITOR_FLAG_ACTIVE to be changed while the
			 * interface is up.
			 * Else we would need to add a lot of cruft
			 * to update everything:
			 *	cooked_mntrs, monitor and all fif_* counters
			 *	reconfigure hardware
			 */
			if ((*flags & mask) != (sdata->u.mntr_flags & mask))
				return -EBUSY;

			ieee80211_adjust_monitor_flags(sdata, -1);
			sdata->u.mntr_flags = *flags;
			ieee80211_adjust_monitor_flags(sdata, 1);

			ieee80211_configure_filter(local);
		} else {
			/*
			 * Because the interface is down, ieee80211_do_stop
			 * and ieee80211_do_open take care of "everything"
			 * mentioned in the comment above.
			 */
			sdata->u.mntr_flags = *flags;
		}
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

static int ieee80211_set_noack_map(struct wiphy *wiphy,
				  struct net_device *dev,
				  u16 noack_map)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	sdata->noack_map = noack_map;
	return 0;
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

	/* reject WEP and TKIP keys if WEP failed to initialize */
	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_WEP104:
		if (IS_ERR(local->wep_tx_tfm))
			return -EINVAL;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_GCMP:
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

	mutex_lock(&local->sta_mtx);

	if (mac_addr) {
		if (ieee80211_vif_is_mesh(&sdata->vif))
			sta = sta_info_get(sdata, mac_addr);
		else
			sta = sta_info_get_bss(sdata, mac_addr);
		/*
		 * The ASSOC test makes sure the driver is ready to
		 * receive the key. When wpa_supplicant has roamed
		 * using FT, it attempts to set the key before
		 * association has completed, this rejects that attempt
		 * so it will set the key again after assocation.
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
		if (key->sta && test_sta_flag(key->sta, WLAN_STA_MFP))
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
	case NL80211_IFTYPE_UNSPECIFIED:
	case NUM_NL80211_IFTYPES:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_GO:
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

	ieee80211_key_free(key, true);

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

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();

	if (mac_addr) {
		sta = sta_info_get_bss(sdata, mac_addr);
		if (!sta)
			goto out;

		if (pairwise && key_idx < NUM_DEFAULT_KEYS)
			key = rcu_dereference(sta->ptk[key_idx]);
		else if (!pairwise &&
			 key_idx < NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS)
			key = rcu_dereference(sta->gtk[key_idx]);
	} else
		key = rcu_dereference(sdata->keys[key_idx]);

	if (!key)
		goto out;

	memset(&params, 0, sizeof(params));

	params.cipher = key->conf.cipher;

	switch (key->conf.cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		iv32 = key->u.tkip.tx.iv32;
		iv16 = key->u.tkip.tx.iv16;

		if (key->flags & KEY_FLAG_UPLOADED_TO_HARDWARE)
			drv_get_tkip_seq(sdata->local,
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
	case WLAN_CIPHER_SUITE_CCMP:
		pn64 = atomic64_read(&key->u.ccmp.tx_pn);
		seq[0] = pn64;
		seq[1] = pn64 >> 8;
		seq[2] = pn64 >> 16;
		seq[3] = pn64 >> 24;
		seq[4] = pn64 >> 32;
		seq[5] = pn64 >> 40;
		params.seq = seq;
		params.seq_len = 6;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		pn64 = atomic64_read(&key->u.aes_cmac.tx_pn);
		seq[0] = pn64;
		seq[1] = pn64 >> 8;
		seq[2] = pn64 >> 16;
		seq[3] = pn64 >> 24;
		seq[4] = pn64 >> 32;
		seq[5] = pn64 >> 40;
		params.seq = seq;
		params.seq_len = 6;
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

		sband = sta->local->hw.wiphy->bands[
				ieee80211_get_sdata_band(sta->sdata)];
		brate = sband->bitrates[rate->idx].bitrate;
		rinfo->legacy = DIV_ROUND_UP(brate, 1 << shift);
	}
	if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		rinfo->flags |= RATE_INFO_FLAGS_40_MHZ_WIDTH;
	if (rate->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
		rinfo->flags |= RATE_INFO_FLAGS_80_MHZ_WIDTH;
	if (rate->flags & IEEE80211_TX_RC_160_MHZ_WIDTH)
		rinfo->flags |= RATE_INFO_FLAGS_160_MHZ_WIDTH;
	if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
		rinfo->flags |= RATE_INFO_FLAGS_SHORT_GI;
}

void sta_set_rate_info_rx(struct sta_info *sta, struct rate_info *rinfo)
{
	rinfo->flags = 0;

	if (sta->last_rx_rate_flag & RX_FLAG_HT) {
		rinfo->flags |= RATE_INFO_FLAGS_MCS;
		rinfo->mcs = sta->last_rx_rate_idx;
	} else if (sta->last_rx_rate_flag & RX_FLAG_VHT) {
		rinfo->flags |= RATE_INFO_FLAGS_VHT_MCS;
		rinfo->nss = sta->last_rx_rate_vht_nss;
		rinfo->mcs = sta->last_rx_rate_idx;
	} else {
		struct ieee80211_supported_band *sband;
		int shift = ieee80211_vif_get_shift(&sta->sdata->vif);
		u16 brate;

		sband = sta->local->hw.wiphy->bands[
				ieee80211_get_sdata_band(sta->sdata)];
		brate = sband->bitrates[sta->last_rx_rate_idx].bitrate;
		rinfo->legacy = DIV_ROUND_UP(brate, 1 << shift);
	}

	if (sta->last_rx_rate_flag & RX_FLAG_40MHZ)
		rinfo->flags |= RATE_INFO_FLAGS_40_MHZ_WIDTH;
	if (sta->last_rx_rate_flag & RX_FLAG_SHORT_GI)
		rinfo->flags |= RATE_INFO_FLAGS_SHORT_GI;
	if (sta->last_rx_rate_vht_flag & RX_VHT_FLAG_80MHZ)
		rinfo->flags |= RATE_INFO_FLAGS_80_MHZ_WIDTH;
	if (sta->last_rx_rate_vht_flag & RX_VHT_FLAG_80P80MHZ)
		rinfo->flags |= RATE_INFO_FLAGS_80P80_MHZ_WIDTH;
	if (sta->last_rx_rate_vht_flag & RX_VHT_FLAG_160MHZ)
		rinfo->flags |= RATE_INFO_FLAGS_160_MHZ_WIDTH;
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
		sta_set_sinfo(sta, sinfo);
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
		sta_set_sinfo(sta, sinfo);
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
	mutex_lock(&local->iflist_mtx);
	if (local->use_chanctx) {
		sdata = rcu_dereference_protected(
				local->monitor_sdata,
				lockdep_is_held(&local->iflist_mtx));
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
	mutex_unlock(&local->iflist_mtx);
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
	if (err < 0)
		return err;
	if (err == 0)
		changed |= BSS_CHANGED_AP_PROBE_RESP;

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
		      BSS_CHANGED_P2P_PS;
	int err;

	old = sdata_dereference(sdata->u.ap.beacon, sdata);
	if (old)
		return -EALREADY;

	switch (params->smps_mode) {
	case NL80211_SMPS_OFF:
		sdata->smps_mode = IEEE80211_SMPS_OFF;
		break;
	case NL80211_SMPS_STATIC:
		sdata->smps_mode = IEEE80211_SMPS_STATIC;
		break;
	case NL80211_SMPS_DYNAMIC:
		sdata->smps_mode = IEEE80211_SMPS_DYNAMIC;
		break;
	default:
		return -EINVAL;
	}
	sdata->needed_rx_chains = sdata->local->rx_chains;

	mutex_lock(&local->mtx);
	err = ieee80211_vif_use_channel(sdata, &params->chandef,
					IEEE80211_CHANCTX_SHARED);
	if (!err)
		ieee80211_vif_copy_chanctx_to_vlans(sdata, false);
	mutex_unlock(&local->mtx);
	if (err)
		return err;

	/*
	 * Apply control port protocol, this allows us to
	 * not encrypt dynamic WEP control frames.
	 */
	sdata->control_port_protocol = params->crypto.control_port_ethertype;
	sdata->control_port_no_encrypt = params->crypto.control_port_no_encrypt;
	sdata->encrypt_headroom = ieee80211_cs_headroom(sdata->local,
							&params->crypto,
							sdata->vif.type);

	list_for_each_entry(vlan, &sdata->u.ap.vlans, u.vlan.list) {
		vlan->control_port_protocol =
			params->crypto.control_port_ethertype;
		vlan->control_port_no_encrypt =
			params->crypto.control_port_no_encrypt;
		vlan->encrypt_headroom =
			ieee80211_cs_headroom(sdata->local,
					      &params->crypto,
					      vlan->vif.type);
	}

	sdata->vif.bss_conf.beacon_int = params->beacon_interval;
	sdata->vif.bss_conf.dtim_period = params->dtim_period;
	sdata->vif.bss_conf.enable_beacon = true;

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
	sdata->u.ap.driver_smps_mode = IEEE80211_SMPS_OFF;

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
	skb_queue_purge(&sdata->u.ap.ps.bc_buf);

	mutex_lock(&local->mtx);
	ieee80211_vif_copy_chanctx_to_vlans(sdata, true);
	ieee80211_vif_release_channel(sdata);
	mutex_unlock(&local->mtx);

	return 0;
}

/* Layer 2 Update frame (802.2 Type 1 LLC XID Update response) */
struct iapp_layer2_update {
	u8 da[ETH_ALEN];	/* broadcast */
	u8 sa[ETH_ALEN];	/* STA addr */
	__be16 len;		/* 6 */
	u8 dsap;		/* 0 */
	u8 ssap;		/* 0 */
	u8 control;
	u8 xid_info[3];
} __packed;

static void ieee80211_send_layer2_update(struct sta_info *sta)
{
	struct iapp_layer2_update *msg;
	struct sk_buff *skb;

	/* Send Level 2 Update Frame to update forwarding tables in layer 2
	 * bridge devices */

	skb = dev_alloc_skb(sizeof(*msg));
	if (!skb)
		return;
	msg = (struct iapp_layer2_update *)skb_put(skb, sizeof(*msg));

	/* 802.2 Type 1 Logical Link Control (LLC) Exchange Identifier (XID)
	 * Update response frame; IEEE Std 802.2-1998, 5.4.1.2.1 */

	eth_broadcast_addr(msg->da);
	memcpy(msg->sa, sta->sta.addr, ETH_ALEN);
	msg->len = htons(6);
	msg->dsap = 0;
	msg->ssap = 0x01;	/* NULL LSAP, CR Bit: Response */
	msg->control = 0xaf;	/* XID response lsb.1111F101.
				 * F=0 (no poll command; unsolicited frame) */
	msg->xid_info[0] = 0x81;	/* XID format identifier */
	msg->xid_info[1] = 1;	/* LLC types/classes: Type 1 LLC */
	msg->xid_info[2] = 0;	/* XID sender's receive window size (RW) */

	skb->dev = sta->sdata->dev;
	skb->protocol = eth_type_trans(skb, sta->sdata->dev);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx_ni(skb);
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

static int sta_apply_parameters(struct ieee80211_local *local,
				struct sta_info *sta,
				struct station_parameters *params)
{
	int ret = 0;
	struct ieee80211_supported_band *sband;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	enum ieee80211_band band = ieee80211_get_sdata_band(sdata);
	u32 mask, set;

	sband = local->hw.wiphy->bands[band];

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

	/* auth flags will be set later for TDLS stations */
	if (!test_sta_flag(sta, WLAN_STA_TDLS_PEER)) {
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

	if (mask & BIT(NL80211_STA_FLAG_WME))
		sta->sta.wme = set & BIT(NL80211_STA_FLAG_WME);

	if (mask & BIT(NL80211_STA_FLAG_MFP)) {
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

	if (params->sta_modify_mask & STATION_PARAM_APPLY_UAPSD) {
		sta->sta.uapsd_queues = params->uapsd_queues;
		sta->sta.max_sp = params->max_sp;
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

	if (params->supported_rates) {
		ieee80211_parse_bitrates(&sdata->vif.bss_conf.chandef,
					 sband, params->supported_rates,
					 params->supported_rates_len,
					 &sta->sta.supp_rates[band]);
	}

	if (params->ht_capa)
		ieee80211_ht_cap_ie_to_sta_ht_cap(sdata, sband,
						  params->ht_capa, sta);

	if (params->vht_capa)
		ieee80211_vht_cap_ie_to_sta_vht_cap(sdata, sband,
						    params->vht_capa, sta);

	if (params->opmode_notif_used) {
		/* returned value is only needed for rc update, but the
		 * rc isn't initialized here yet, so ignore it
		 */
		__ieee80211_vht_handle_opmode(sdata, sta,
					      params->opmode_notif,
					      band, false);
	}

	if (ieee80211_vif_is_mesh(&sdata->vif)) {
#ifdef CONFIG_MAC80211_MESH
		u32 changed = 0;

		if (params->sta_modify_mask & STATION_PARAM_APPLY_PLINK_STATE) {
			switch (params->plink_state) {
			case NL80211_PLINK_ESTAB:
				if (sta->plink_state != NL80211_PLINK_ESTAB)
					changed = mesh_plink_inc_estab_count(
							sdata);
				sta->plink_state = params->plink_state;

				ieee80211_mps_sta_status_update(sta);
				changed |= ieee80211_mps_set_sta_local_pm(sta,
					      sdata->u.mesh.mshcfg.power_mode);
				break;
			case NL80211_PLINK_LISTEN:
			case NL80211_PLINK_BLOCKED:
			case NL80211_PLINK_OPN_SNT:
			case NL80211_PLINK_OPN_RCVD:
			case NL80211_PLINK_CNF_RCVD:
			case NL80211_PLINK_HOLDING:
				if (sta->plink_state == NL80211_PLINK_ESTAB)
					changed = mesh_plink_dec_estab_count(
							sdata);
				sta->plink_state = params->plink_state;

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
			changed |=
			      ieee80211_mps_set_sta_local_pm(sta,
							     params->local_pm);
		ieee80211_mbss_info_change_notify(sdata, changed);
#endif
	}

	/* set the STA state after all sta info from usermode has been set */
	if (test_sta_flag(sta, WLAN_STA_TDLS_PEER)) {
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
	int layer2_update;

	if (params->vlan) {
		sdata = IEEE80211_DEV_TO_SUB_IF(params->vlan);

		if (sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    sdata->vif.type != NL80211_IFTYPE_AP)
			return -EINVAL;
	} else
		sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (ether_addr_equal(mac, sdata->vif.addr))
		return -EINVAL;

	if (is_multicast_ether_addr(mac))
		return -EINVAL;

	sta = sta_info_alloc(sdata, mac, GFP_KERNEL);
	if (!sta)
		return -ENOMEM;

	/*
	 * defaults -- if userspace wants something else we'll
	 * change it accordingly in sta_apply_parameters()
	 */
	if (!(params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER))) {
		sta_info_pre_move_state(sta, IEEE80211_STA_AUTH);
		sta_info_pre_move_state(sta, IEEE80211_STA_ASSOC);
	} else {
		sta->sta.tdls = true;
	}

	err = sta_apply_parameters(local, sta, params);
	if (err) {
		sta_info_free(local, sta);
		return err;
	}

	/*
	 * for TDLS, rate control should be initialized only when
	 * rates are known and station is marked authorized
	 */
	if (!test_sta_flag(sta, WLAN_STA_TDLS_PEER))
		rate_control_rate_init(sta);

	layer2_update = sdata->vif.type == NL80211_IFTYPE_AP_VLAN ||
		sdata->vif.type == NL80211_IFTYPE_AP;

	err = sta_info_insert_rcu(sta);
	if (err) {
		rcu_read_unlock();
		return err;
	}

	if (layer2_update)
		ieee80211_send_layer2_update(sta);

	rcu_read_unlock();

	return 0;
}

static int ieee80211_del_station(struct wiphy *wiphy, struct net_device *dev,
				 const u8 *mac)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (mac)
		return sta_info_destroy_addr_bss(sdata, mac);

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
		statype = CFG80211_STA_AP_CLIENT;
		break;
	default:
		err = -EOPNOTSUPP;
		goto out_err;
	}

	err = cfg80211_check_station_change(wiphy, params, statype);
	if (err)
		goto out_err;

	if (params->vlan && params->vlan != sta->sdata->dev) {
		bool prev_4addr = false;
		bool new_4addr = false;

		vlansdata = IEEE80211_DEV_TO_SUB_IF(params->vlan);

		if (params->vlan->ieee80211_ptr->use_4addr) {
			if (vlansdata->u.vlan.sta) {
				err = -EBUSY;
				goto out_err;
			}

			rcu_assign_pointer(vlansdata->u.vlan.sta, sta);
			new_4addr = true;
		}

		if (sta->sdata->vif.type == NL80211_IFTYPE_AP_VLAN &&
		    sta->sdata->u.vlan.sta) {
			RCU_INIT_POINTER(sta->sdata->u.vlan.sta, NULL);
			prev_4addr = true;
		}

		sta->sdata = vlansdata;

		if (sta->sta_state == IEEE80211_STA_AUTHORIZED &&
		    prev_4addr != new_4addr) {
			if (new_4addr)
				atomic_dec(&sta->sdata->bss->num_mcast_sta);
			else
				atomic_inc(&sta->sdata->bss->num_mcast_sta);
		}

		ieee80211_send_layer2_update(sta);
	}

	err = sta_apply_parameters(local, sta, params);
	if (err)
		goto out_err;

	/* When peer becomes authorized, init rate control as well */
	if (test_sta_flag(sta, WLAN_STA_TDLS_PEER) &&
	    test_sta_flag(sta, WLAN_STA_AUTHORIZED))
		rate_control_rate_init(sta);

	mutex_unlock(&local->sta_mtx);

	if ((sdata->vif.type == NL80211_IFTYPE_AP ||
	     sdata->vif.type == NL80211_IFTYPE_AP_VLAN) &&
	    sta->known_smps_mode != sta->sdata->bss->req_smps &&
	    test_sta_flag(sta, WLAN_STA_AUTHORIZED) &&
	    sta_info_tx_streams(sta) != 1) {
		ht_dbg(sta->sdata,
		       "%pM just authorized and MIMO capable - update SMPS\n",
		       sta->sta.addr);
		ieee80211_send_smps_action(sta->sdata,
			sta->sdata->bss->req_smps,
			sta->sta.addr,
			sta->sdata->vif.bss_conf.bssid);
	}

	if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	    params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
		ieee80211_recalc_ps(local, -1);
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
		memset(next_hop, 0, ETH_ALEN);

	memset(pinfo, 0, sizeof(*pinfo));

	pinfo->generation = mesh_paths_generation;

	pinfo->filled = MPATH_INFO_FRAME_QLEN |
			MPATH_INFO_SN |
			MPATH_INFO_METRIC |
			MPATH_INFO_EXPTIME |
			MPATH_INFO_DISCOVERY_TIMEOUT |
			MPATH_INFO_DISCOVERY_RETRIES |
			MPATH_INFO_FLAGS;

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

	pinfo->generation = mpp_paths_generation;
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
		if (!(sdata->local->hw.flags & IEEE80211_HW_SIGNAL_DBM))
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
	enum ieee80211_band band;
	u32 changed = 0;

	if (!sdata_dereference(sdata->u.ap.beacon, sdata))
		return -ENOENT;

	band = ieee80211_get_sdata_band(sdata);

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
	    band == IEEE80211_BAND_5GHZ) {
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
					 wiphy->bands[band],
					 params->basic_rates,
					 params->basic_rates_len,
					 &sdata->vif.bss_conf.basic_rates);
		changed |= BSS_CHANGED_BASIC_RATES;
	}

	if (params->ap_isolate >= 0) {
		if (params->ap_isolate)
			sdata->flags |= IEEE80211_SDATA_DONT_BRIDGE_PACKETS;
		else
			sdata->flags &= ~IEEE80211_SDATA_DONT_BRIDGE_PACKETS;
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
	default:
		return -EOPNOTSUPP;
	}

	return ieee80211_request_scan(sdata, req);
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
ieee80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (!sdata->local->ops->sched_scan_stop)
		return -EOPNOTSUPP;

	return ieee80211_request_sched_scan_stop(sdata);
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

static int ieee80211_set_mcast_rate(struct wiphy *wiphy, struct net_device *dev,
				    int rate[IEEE80211_NUM_BANDS])
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	memcpy(sdata->vif.bss_conf.mcast_rate, rate,
	       sizeof(int) * IEEE80211_NUM_BANDS);

	return 0;
}

static int ieee80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	int err;

	if (changed & WIPHY_PARAM_FRAG_THRESHOLD) {
		err = drv_set_frag_threshold(local, wiphy->frag_threshold);

		if (err)
			return err;
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

	return 0;
}

static int ieee80211_set_tx_power(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  enum nl80211_tx_power_setting type, int mbm)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata;

	if (wdev) {
		sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);

		switch (type) {
		case NL80211_TX_POWER_AUTOMATIC:
			sdata->user_power_level = IEEE80211_UNSET_POWER_LEVEL;
			break;
		case NL80211_TX_POWER_LIMITED:
		case NL80211_TX_POWER_FIXED:
			if (mbm < 0 || (mbm % 100))
				return -EOPNOTSUPP;
			sdata->user_power_level = MBM_TO_DBM(mbm);
			break;
		}

		ieee80211_recalc_txpower(sdata);

		return 0;
	}

	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		local->user_power_level = IEEE80211_UNSET_POWER_LEVEL;
		break;
	case NL80211_TX_POWER_LIMITED:
	case NL80211_TX_POWER_FIXED:
		if (mbm < 0 || (mbm % 100))
			return -EOPNOTSUPP;
		local->user_power_level = MBM_TO_DBM(mbm);
		break;
	}

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list)
		sdata->user_power_level = local->user_power_level;
	list_for_each_entry(sdata, &local->interfaces, list)
		ieee80211_recalc_txpower(sdata);
	mutex_unlock(&local->iflist_mtx);

	return 0;
}

static int ieee80211_get_tx_power(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  int *dbm)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);

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

int __ieee80211_request_smps_ap(struct ieee80211_sub_if_data *sdata,
				enum ieee80211_smps_mode smps_mode)
{
	struct sta_info *sta;
	enum ieee80211_smps_mode old_req;
	int i;

	if (WARN_ON_ONCE(sdata->vif.type != NL80211_IFTYPE_AP))
		return -EINVAL;

	if (sdata->vif.bss_conf.chandef.width == NL80211_CHAN_WIDTH_20_NOHT)
		return 0;

	old_req = sdata->u.ap.req_smps;
	sdata->u.ap.req_smps = smps_mode;

	/* AUTOMATIC doesn't mean much for AP - don't allow it */
	if (old_req == smps_mode ||
	    smps_mode == IEEE80211_SMPS_AUTOMATIC)
		return 0;

	 /* If no associated stations, there's no need to do anything */
	if (!atomic_read(&sdata->u.ap.num_mcast_sta)) {
		sdata->smps_mode = smps_mode;
		ieee80211_queue_work(&sdata->local->hw, &sdata->recalc_smps);
		return 0;
	}

	ht_dbg(sdata,
	       "SMSP %d requested in AP mode, sending Action frame to %d stations\n",
	       smps_mode, atomic_read(&sdata->u.ap.num_mcast_sta));

	mutex_lock(&sdata->local->sta_mtx);
	for (i = 0; i < STA_HASH_SIZE; i++) {
		for (sta = rcu_dereference_protected(sdata->local->sta_hash[i],
				lockdep_is_held(&sdata->local->sta_mtx));
		     sta;
		     sta = rcu_dereference_protected(sta->hnext,
				lockdep_is_held(&sdata->local->sta_mtx))) {
			/*
			 * Only stations associated to our AP and
			 * associated VLANs
			 */
			if (sta->sdata->bss != &sdata->u.ap)
				continue;

			/* This station doesn't support MIMO - skip it */
			if (sta_info_tx_streams(sta) == 1)
				continue;

			/*
			 * Don't wake up a STA just to send the action frame
			 * unless we are getting more restrictive.
			 */
			if (test_sta_flag(sta, WLAN_STA_PS_STA) &&
			    !ieee80211_smps_is_restrictive(sta->known_smps_mode,
							   smps_mode)) {
				ht_dbg(sdata,
				       "Won't send SMPS to sleeping STA %pM\n",
				       sta->sta.addr);
				continue;
			}

			/*
			 * If the STA is not authorized, wait until it gets
			 * authorized and the action frame will be sent then.
			 */
			if (!test_sta_flag(sta, WLAN_STA_AUTHORIZED))
				continue;

			ht_dbg(sdata, "Sending SMPS to %pM\n", sta->sta.addr);
			ieee80211_send_smps_action(sdata, smps_mode,
						   sta->sta.addr,
						   sdata->vif.bss_conf.bssid);
		}
	}
	mutex_unlock(&sdata->local->sta_mtx);

	sdata->smps_mode = smps_mode;
	ieee80211_queue_work(&sdata->local->hw, &sdata->recalc_smps);

	return 0;
}

int __ieee80211_request_smps_mgd(struct ieee80211_sub_if_data *sdata,
				 enum ieee80211_smps_mode smps_mode)
{
	const u8 *ap;
	enum ieee80211_smps_mode old_req;
	int err;

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

	if (smps_mode == IEEE80211_SMPS_AUTOMATIC) {
		if (sdata->u.mgd.powersave)
			smps_mode = IEEE80211_SMPS_DYNAMIC;
		else
			smps_mode = IEEE80211_SMPS_OFF;
	}

	/* send SM PS frame to AP */
	err = ieee80211_send_smps_action(sdata, smps_mode,
					 ap, ap);
	if (err)
		sdata->u.mgd.req_smps = old_req;

	return err;
}

static int ieee80211_set_power_mgmt(struct wiphy *wiphy, struct net_device *dev,
				    bool enabled, int timeout)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	if (!(local->hw.flags & IEEE80211_HW_SUPPORTS_PS))
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

	if (local->hw.flags & IEEE80211_HW_SUPPORTS_DYNAMIC_PS)
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);

	ieee80211_recalc_ps(local, -1);
	ieee80211_recalc_ps_vif(sdata);

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

	bss_conf->cqm_rssi_thold = rssi_thold;
	bss_conf->cqm_rssi_hyst = rssi_hyst;

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

	if (local->hw.flags & IEEE80211_HW_HAS_RATE_CONTROL) {
		ret = drv_set_bitrate_mask(local, sdata, mask);
		if (ret)
			return ret;
	}

	for (i = 0; i < IEEE80211_NUM_BANDS; i++) {
		struct ieee80211_supported_band *sband = wiphy->bands[i];
		int j;

		sdata->rc_rateidx_mask[i] = mask->control[i].legacy;
		memcpy(sdata->rc_rateidx_mcs_mask[i], mask->control[i].ht_mcs,
		       sizeof(mask->control[i].ht_mcs));

		sdata->rc_has_mcs_mask[i] = false;
		if (!sband)
			continue;

		for (j = 0; j < IEEE80211_HT_MCS_MASK_LEN; j++)
			if (~sdata->rc_rateidx_mcs_mask[i][j]) {
				sdata->rc_has_mcs_mask[i] = true;
				break;
			}
	}

	return 0;
}

static bool ieee80211_coalesce_started_roc(struct ieee80211_local *local,
					   struct ieee80211_roc_work *new_roc,
					   struct ieee80211_roc_work *cur_roc)
{
	unsigned long j = jiffies;
	unsigned long cur_roc_end = cur_roc->hw_start_time +
				    msecs_to_jiffies(cur_roc->duration);
	struct ieee80211_roc_work *next_roc;
	int new_dur;

	if (WARN_ON(!cur_roc->started || !cur_roc->hw_begun))
		return false;

	if (time_after(j + IEEE80211_ROC_MIN_LEFT, cur_roc_end))
		return false;

	ieee80211_handle_roc_started(new_roc);

	new_dur = new_roc->duration - jiffies_to_msecs(cur_roc_end - j);

	/* cur_roc is long enough - add new_roc to the dependents list. */
	if (new_dur <= 0) {
		list_add_tail(&new_roc->list, &cur_roc->dependents);
		return true;
	}

	new_roc->duration = new_dur;

	/*
	 * if cur_roc was already coalesced before, we might
	 * want to extend the next roc instead of adding
	 * a new one.
	 */
	next_roc = list_entry(cur_roc->list.next,
			      struct ieee80211_roc_work, list);
	if (&next_roc->list != &local->roc_list &&
	    next_roc->chan == new_roc->chan &&
	    next_roc->sdata == new_roc->sdata &&
	    !WARN_ON(next_roc->started)) {
		list_add_tail(&new_roc->list, &next_roc->dependents);
		next_roc->duration = max(next_roc->duration,
					 new_roc->duration);
		next_roc->type = max(next_roc->type, new_roc->type);
		return true;
	}

	/* add right after cur_roc */
	list_add(&new_roc->list, &cur_roc->list);

	return true;
}

static int ieee80211_start_roc_work(struct ieee80211_local *local,
				    struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_channel *channel,
				    unsigned int duration, u64 *cookie,
				    struct sk_buff *txskb,
				    enum ieee80211_roc_type type)
{
	struct ieee80211_roc_work *roc, *tmp;
	bool queued = false;
	int ret;

	lockdep_assert_held(&local->mtx);

	if (local->use_chanctx && !local->ops->remain_on_channel)
		return -EOPNOTSUPP;

	roc = kzalloc(sizeof(*roc), GFP_KERNEL);
	if (!roc)
		return -ENOMEM;

	/*
	 * If the duration is zero, then the driver
	 * wouldn't actually do anything. Set it to
	 * 10 for now.
	 *
	 * TODO: cancel the off-channel operation
	 *       when we get the SKB's TX status and
	 *       the wait time was zero before.
	 */
	if (!duration)
		duration = 10;

	roc->chan = channel;
	roc->duration = duration;
	roc->req_duration = duration;
	roc->frame = txskb;
	roc->type = type;
	roc->mgmt_tx_cookie = (unsigned long)txskb;
	roc->sdata = sdata;
	INIT_DELAYED_WORK(&roc->work, ieee80211_sw_roc_work);
	INIT_LIST_HEAD(&roc->dependents);

	/*
	 * cookie is either the roc cookie (for normal roc)
	 * or the SKB (for mgmt TX)
	 */
	if (!txskb) {
		/* local->mtx protects this */
		local->roc_cookie_counter++;
		roc->cookie = local->roc_cookie_counter;
		/* wow, you wrapped 64 bits ... more likely a bug */
		if (WARN_ON(roc->cookie == 0)) {
			roc->cookie = 1;
			local->roc_cookie_counter++;
		}
		*cookie = roc->cookie;
	} else {
		*cookie = (unsigned long)txskb;
	}

	/* if there's one pending or we're scanning, queue this one */
	if (!list_empty(&local->roc_list) ||
	    local->scanning || local->radar_detect_enabled)
		goto out_check_combine;

	/* if not HW assist, just queue & schedule work */
	if (!local->ops->remain_on_channel) {
		ieee80211_queue_delayed_work(&local->hw, &roc->work, 0);
		goto out_queue;
	}

	/* otherwise actually kick it off here (for error handling) */

	ret = drv_remain_on_channel(local, sdata, channel, duration, type);
	if (ret) {
		kfree(roc);
		return ret;
	}

	roc->started = true;
	goto out_queue;

 out_check_combine:
	list_for_each_entry(tmp, &local->roc_list, list) {
		if (tmp->chan != channel || tmp->sdata != sdata)
			continue;

		/*
		 * Extend this ROC if possible:
		 *
		 * If it hasn't started yet, just increase the duration
		 * and add the new one to the list of dependents.
		 * If the type of the new ROC has higher priority, modify the
		 * type of the previous one to match that of the new one.
		 */
		if (!tmp->started) {
			list_add_tail(&roc->list, &tmp->dependents);
			tmp->duration = max(tmp->duration, roc->duration);
			tmp->type = max(tmp->type, roc->type);
			queued = true;
			break;
		}

		/* If it has already started, it's more difficult ... */
		if (local->ops->remain_on_channel) {
			/*
			 * In the offloaded ROC case, if it hasn't begun, add
			 * this new one to the dependent list to be handled
			 * when the master one begins. If it has begun,
			 * check that there's still a minimum time left and
			 * if so, start this one, transmitting the frame, but
			 * add it to the list directly after this one with
			 * a reduced time so we'll ask the driver to execute
			 * it right after finishing the previous one, in the
			 * hope that it'll also be executed right afterwards,
			 * effectively extending the old one.
			 * If there's no minimum time left, just add it to the
			 * normal list.
			 * TODO: the ROC type is ignored here, assuming that it
			 * is better to immediately use the current ROC.
			 */
			if (!tmp->hw_begun) {
				list_add_tail(&roc->list, &tmp->dependents);
				queued = true;
				break;
			}

			if (ieee80211_coalesce_started_roc(local, roc, tmp))
				queued = true;
		} else if (del_timer_sync(&tmp->work.timer)) {
			unsigned long new_end;

			/*
			 * In the software ROC case, cancel the timer, if
			 * that fails then the finish work is already
			 * queued/pending and thus we queue the new ROC
			 * normally, if that succeeds then we can extend
			 * the timer duration and TX the frame (if any.)
			 */

			list_add_tail(&roc->list, &tmp->dependents);
			queued = true;

			new_end = jiffies + msecs_to_jiffies(roc->duration);

			/* ok, it was started & we canceled timer */
			if (time_after(new_end, tmp->work.timer.expires))
				mod_timer(&tmp->work.timer, new_end);
			else
				add_timer(&tmp->work.timer);

			ieee80211_handle_roc_started(roc);
		}
		break;
	}

 out_queue:
	if (!queued)
		list_add_tail(&roc->list, &local->roc_list);

	return 0;
}

static int ieee80211_remain_on_channel(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       struct ieee80211_channel *chan,
				       unsigned int duration,
				       u64 *cookie)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	struct ieee80211_local *local = sdata->local;
	int ret;

	mutex_lock(&local->mtx);
	ret = ieee80211_start_roc_work(local, sdata, chan,
				       duration, cookie, NULL,
				       IEEE80211_ROC_TYPE_NORMAL);
	mutex_unlock(&local->mtx);

	return ret;
}

static int ieee80211_cancel_roc(struct ieee80211_local *local,
				u64 cookie, bool mgmt_tx)
{
	struct ieee80211_roc_work *roc, *tmp, *found = NULL;
	int ret;

	mutex_lock(&local->mtx);
	list_for_each_entry_safe(roc, tmp, &local->roc_list, list) {
		struct ieee80211_roc_work *dep, *tmp2;

		list_for_each_entry_safe(dep, tmp2, &roc->dependents, list) {
			if (!mgmt_tx && dep->cookie != cookie)
				continue;
			else if (mgmt_tx && dep->mgmt_tx_cookie != cookie)
				continue;
			/* found dependent item -- just remove it */
			list_del(&dep->list);
			mutex_unlock(&local->mtx);

			ieee80211_roc_notify_destroy(dep, true);
			return 0;
		}

		if (!mgmt_tx && roc->cookie != cookie)
			continue;
		else if (mgmt_tx && roc->mgmt_tx_cookie != cookie)
			continue;

		found = roc;
		break;
	}

	if (!found) {
		mutex_unlock(&local->mtx);
		return -ENOENT;
	}

	/*
	 * We found the item to cancel, so do that. Note that it
	 * may have dependents, which we also cancel (and send
	 * the expired signal for.) Not doing so would be quite
	 * tricky here, but we may need to fix it later.
	 */

	if (local->ops->remain_on_channel) {
		if (found->started) {
			ret = drv_cancel_remain_on_channel(local);
			if (WARN_ON_ONCE(ret)) {
				mutex_unlock(&local->mtx);
				return ret;
			}
		}

		list_del(&found->list);

		if (found->started)
			ieee80211_start_next_roc(local);
		mutex_unlock(&local->mtx);

		ieee80211_roc_notify_destroy(found, true);
	} else {
		/* work may be pending so use it all the time */
		found->abort = true;
		ieee80211_queue_delayed_work(&local->hw, &found->work, 0);

		mutex_unlock(&local->mtx);

		/* work will clean up etc */
		flush_delayed_work(&found->work);
		WARN_ON(!found->to_be_freed);
		kfree(found);
	}

	return 0;
}

static int ieee80211_cancel_remain_on_channel(struct wiphy *wiphy,
					      struct wireless_dev *wdev,
					      u64 cookie)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	struct ieee80211_local *local = sdata->local;

	return ieee80211_cancel_roc(local, cookie, false);
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

static struct cfg80211_beacon_data *
cfg80211_beacon_dup(struct cfg80211_beacon_data *beacon)
{
	struct cfg80211_beacon_data *new_beacon;
	u8 *pos;
	int len;

	len = beacon->head_len + beacon->tail_len + beacon->beacon_ies_len +
	      beacon->proberesp_ies_len + beacon->assocresp_ies_len +
	      beacon->probe_resp_len;

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
		beacon->probe_resp = pos;
		memcpy(pos, beacon->probe_resp, beacon->probe_resp_len);
		pos += beacon->probe_resp_len;
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

		err = ieee80211_vif_use_reserved_context(sdata);
		if (err)
			return err;

		return 0;
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
	int err, changed = 0;

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
	if (!chanctx) {
		err = -EBUSY;
		goto out;
	}

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

	ch_switch.timestamp = 0;
	ch_switch.device_timestamp = 0;
	ch_switch.block_tx = params->block_tx;
	ch_switch.chandef = params->chandef;
	ch_switch.count = params->count;

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

static int ieee80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
			     struct cfg80211_mgmt_tx_params *params,
			     u64 *cookie)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_WDEV_TO_SUB_IF(wdev);
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct sta_info *sta;
	const struct ieee80211_mgmt *mgmt = (void *)params->buf;
	bool need_offchan = false;
	u32 flags;
	int ret;
	u8 *data;

	if (params->dont_wait_for_ack)
		flags = IEEE80211_TX_CTL_NO_ACK;
	else
		flags = IEEE80211_TX_INTFL_NL80211_FRAME_TX |
			IEEE80211_TX_CTL_REQ_TX_STATUS;

	if (params->no_cck)
		flags |= IEEE80211_TX_CTL_NO_CCK_RATE;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_ADHOC:
		if (!sdata->vif.bss_conf.ibss_joined)
			need_offchan = true;
		/* fall through */
#ifdef CONFIG_MAC80211_MESH
	case NL80211_IFTYPE_MESH_POINT:
		if (ieee80211_vif_is_mesh(&sdata->vif) &&
		    !sdata->u.mesh.mesh_id_len)
			need_offchan = true;
		/* fall through */
#endif
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
		if (sdata->vif.type != NL80211_IFTYPE_ADHOC &&
		    !ieee80211_vif_is_mesh(&sdata->vif) &&
		    !rcu_access_pointer(sdata->bss->beacon))
			need_offchan = true;
		if (!ieee80211_is_action(mgmt->frame_control) ||
		    mgmt->u.action.category == WLAN_CATEGORY_PUBLIC ||
		    mgmt->u.action.category == WLAN_CATEGORY_SELF_PROTECTED ||
		    mgmt->u.action.category == WLAN_CATEGORY_SPECTRUM_MGMT)
			break;
		rcu_read_lock();
		sta = sta_info_get(sdata, mgmt->da);
		rcu_read_unlock();
		if (!sta)
			return -ENOLINK;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		if (!sdata->u.mgd.associated)
			need_offchan = true;
		break;
	case NL80211_IFTYPE_P2P_DEVICE:
		need_offchan = true;
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* configurations requiring offchan cannot work if no channel has been
	 * specified
	 */
	if (need_offchan && !params->chan)
		return -EINVAL;

	mutex_lock(&local->mtx);

	/* Check if the operating channel is the requested channel */
	if (!need_offchan) {
		struct ieee80211_chanctx_conf *chanctx_conf;

		rcu_read_lock();
		chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);

		if (chanctx_conf) {
			need_offchan = params->chan &&
				       (params->chan !=
					chanctx_conf->def.chan);
		} else if (!params->chan) {
			ret = -EINVAL;
			rcu_read_unlock();
			goto out_unlock;
		} else {
			need_offchan = true;
		}
		rcu_read_unlock();
	}

	if (need_offchan && !params->offchan) {
		ret = -EBUSY;
		goto out_unlock;
	}

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + params->len);
	if (!skb) {
		ret = -ENOMEM;
		goto out_unlock;
	}
	skb_reserve(skb, local->hw.extra_tx_headroom);

	data = skb_put(skb, params->len);
	memcpy(data, params->buf, params->len);

	/* Update CSA counters */
	if (sdata->vif.csa_active &&
	    (sdata->vif.type == NL80211_IFTYPE_AP ||
	     sdata->vif.type == NL80211_IFTYPE_ADHOC) &&
	    params->n_csa_offsets) {
		int i;
		struct beacon_data *beacon = NULL;

		rcu_read_lock();

		if (sdata->vif.type == NL80211_IFTYPE_AP)
			beacon = rcu_dereference(sdata->u.ap.beacon);
		else if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
			beacon = rcu_dereference(sdata->u.ibss.presp);
		else if (ieee80211_vif_is_mesh(&sdata->vif))
			beacon = rcu_dereference(sdata->u.mesh.beacon);

		if (beacon)
			for (i = 0; i < params->n_csa_offsets; i++)
				data[params->csa_offsets[i]] =
					beacon->csa_current_counter;

		rcu_read_unlock();
	}

	IEEE80211_SKB_CB(skb)->flags = flags;

	skb->dev = sdata->dev;

	if (!need_offchan) {
		*cookie = (unsigned long) skb;
		ieee80211_tx_skb(sdata, skb);
		ret = 0;
		goto out_unlock;
	}

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_TX_OFFCHAN |
					IEEE80211_TX_INTFL_OFFCHAN_TX_OK;
	if (local->hw.flags & IEEE80211_HW_QUEUE_CONTROL)
		IEEE80211_SKB_CB(skb)->hw_queue =
			local->hw.offchannel_tx_hw_queue;

	/* This will handle all kinds of coalescing and immediate TX */
	ret = ieee80211_start_roc_work(local, sdata, params->chan,
				       params->wait, cookie, skb,
				       IEEE80211_ROC_TYPE_MGMT_TX);
	if (ret)
		kfree_skb(skb);
 out_unlock:
	mutex_unlock(&local->mtx);
	return ret;
}

static int ieee80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
					 struct wireless_dev *wdev,
					 u64 cookie)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	return ieee80211_cancel_roc(local, cookie, true);
}

static void ieee80211_mgmt_frame_register(struct wiphy *wiphy,
					  struct wireless_dev *wdev,
					  u16 frame_type, bool reg)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	switch (frame_type) {
	case IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ:
		if (reg)
			local->probe_req_reg++;
		else
			local->probe_req_reg--;

		if (!local->open_count)
			break;

		ieee80211_queue_work(&local->hw, &local->reconfig_filter);
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
	enum ieee80211_band band;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (WARN_ON(!chanctx_conf)) {
		rcu_read_unlock();
		return -EINVAL;
	}
	band = chanctx_conf->def.chan->band;
	sta = sta_info_get_bss(sdata, peer);
	if (sta) {
		qos = sta->sta.wme;
	} else {
		rcu_read_unlock();
		return -ENOLINK;
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
		rcu_read_unlock();
		return -ENOMEM;
	}

	skb->dev = dev;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	nullfunc = (void *) skb_put(skb, size);
	nullfunc->frame_control = fc;
	nullfunc->duration_id = 0;
	memcpy(nullfunc->addr1, sta->sta.addr, ETH_ALEN);
	memcpy(nullfunc->addr2, sdata->vif.addr, ETH_ALEN);
	memcpy(nullfunc->addr3, sdata->vif.addr, ETH_ALEN);
	nullfunc->seq_ctrl = 0;

	info = IEEE80211_SKB_CB(skb);

	info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS |
		       IEEE80211_TX_INTFL_NL80211_FRAME_TX;

	skb_set_queue_mapping(skb, IEEE80211_AC_VO);
	skb->priority = 7;
	if (qos)
		nullfunc->qos_ctrl = cpu_to_le16(7);

	local_bh_disable();
	ieee80211_xmit(sdata, skb, band);
	local_bh_enable();
	rcu_read_unlock();

	*cookie = (unsigned long) skb;
	return 0;
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
		*chandef = chanctx_conf->def;
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
	.change_bss = ieee80211_change_bss,
	.set_txq_params = ieee80211_set_txq_params,
	.set_monitor_channel = ieee80211_set_monitor_channel,
	.suspend = ieee80211_suspend,
	.resume = ieee80211_resume,
	.scan = ieee80211_scan,
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
	.mgmt_frame_register = ieee80211_mgmt_frame_register,
	.set_antenna = ieee80211_set_antenna,
	.get_antenna = ieee80211_get_antenna,
	.set_rekey_data = ieee80211_set_rekey_data,
	.tdls_oper = ieee80211_tdls_oper,
	.tdls_mgmt = ieee80211_tdls_mgmt,
	.probe_client = ieee80211_probe_client,
	.set_noack_map = ieee80211_set_noack_map,
#ifdef CONFIG_PM
	.set_wakeup = ieee80211_set_wakeup,
#endif
	.get_channel = ieee80211_cfg_get_channel,
	.start_radar_detection = ieee80211_start_radar_detection,
	.channel_switch = ieee80211_channel_switch,
	.set_qos_map = ieee80211_set_qos_map,
	.set_ap_chanwidth = ieee80211_set_ap_chanwidth,
};
