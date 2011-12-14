/*
 * mac80211 configuration hooks for cfg80211
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
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

static struct net_device *ieee80211_add_iface(struct wiphy *wiphy, char *name,
					      enum nl80211_iftype type,
					      u32 *flags,
					      struct vif_params *params)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct net_device *dev;
	struct ieee80211_sub_if_data *sdata;
	int err;

	err = ieee80211_if_add(local, name, &dev, type, params);
	if (err)
		return ERR_PTR(err);

	if (type == NL80211_IFTYPE_MONITOR && flags) {
		sdata = IEEE80211_DEV_TO_SUB_IF(dev);
		sdata->u.mntr_flags = *flags;
	}

	return dev;
}

static int ieee80211_del_iface(struct wiphy *wiphy, struct net_device *dev)
{
	ieee80211_if_remove(IEEE80211_DEV_TO_SUB_IF(dev));

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
			/*
			 * Prohibit MONITOR_FLAG_COOK_FRAMES to be
			 * changed while the interface is up.
			 * Else we would need to add a lot of cruft
			 * to update everything:
			 *	cooked_mntrs, monitor and all fif_* counters
			 *	reconfigure hardware
			 */
			if ((*flags & MONITOR_FLAG_COOK_FRAMES) !=
			    (sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES))
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
	struct sta_info *sta = NULL;
	struct ieee80211_key *key;
	int err;

	if (!ieee80211_sdata_running(sdata))
		return -ENETDOWN;

	/* reject WEP and TKIP keys if WEP failed to initialize */
	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_WEP104:
		if (IS_ERR(sdata->local->wep_tx_tfm))
			return -EINVAL;
		break;
	default:
		break;
	}

	key = ieee80211_key_alloc(params->cipher, key_idx, params->key_len,
				  params->key, params->seq_len, params->seq);
	if (IS_ERR(key))
		return PTR_ERR(key);

	if (pairwise)
		key->conf.flags |= IEEE80211_KEY_FLAG_PAIRWISE;

	mutex_lock(&sdata->local->sta_mtx);

	if (mac_addr) {
		if (ieee80211_vif_is_mesh(&sdata->vif))
			sta = sta_info_get(sdata, mac_addr);
		else
			sta = sta_info_get_bss(sdata, mac_addr);
		if (!sta) {
			ieee80211_key_free(sdata->local, key);
			err = -ENOENT;
			goto out_unlock;
		}
	}

	err = ieee80211_key_link(key, sdata, sta);
	if (err)
		ieee80211_key_free(sdata->local, key);

 out_unlock:
	mutex_unlock(&sdata->local->sta_mtx);

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
			key = key_mtx_dereference(local, sta->ptk);
		else
			key = key_mtx_dereference(local, sta->gtk[key_idx]);
	} else
		key = key_mtx_dereference(local, sdata->keys[key_idx]);

	if (!key) {
		ret = -ENOENT;
		goto out_unlock;
	}

	__ieee80211_key_free(key);

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

		if (pairwise)
			key = rcu_dereference(sta->ptk);
		else if (key_idx < NUM_DEFAULT_KEYS)
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

static void rate_idx_to_bitrate(struct rate_info *rate, struct sta_info *sta, int idx)
{
	if (!(rate->flags & RATE_INFO_FLAGS_MCS)) {
		struct ieee80211_supported_band *sband;
		sband = sta->local->hw.wiphy->bands[
				sta->local->hw.conf.channel->band];
		rate->legacy = sband->bitrates[idx].bitrate;
	} else
		rate->mcs = idx;
}

static void sta_set_sinfo(struct sta_info *sta, struct station_info *sinfo)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct timespec uptime;

	sinfo->generation = sdata->local->sta_generation;

	sinfo->filled = STATION_INFO_INACTIVE_TIME |
			STATION_INFO_RX_BYTES |
			STATION_INFO_TX_BYTES |
			STATION_INFO_RX_PACKETS |
			STATION_INFO_TX_PACKETS |
			STATION_INFO_TX_RETRIES |
			STATION_INFO_TX_FAILED |
			STATION_INFO_TX_BITRATE |
			STATION_INFO_RX_BITRATE |
			STATION_INFO_RX_DROP_MISC |
			STATION_INFO_BSS_PARAM |
			STATION_INFO_CONNECTED_TIME |
			STATION_INFO_STA_FLAGS;

	do_posix_clock_monotonic_gettime(&uptime);
	sinfo->connected_time = uptime.tv_sec - sta->last_connected;

	sinfo->inactive_time = jiffies_to_msecs(jiffies - sta->last_rx);
	sinfo->rx_bytes = sta->rx_bytes;
	sinfo->tx_bytes = sta->tx_bytes;
	sinfo->rx_packets = sta->rx_packets;
	sinfo->tx_packets = sta->tx_packets;
	sinfo->tx_retries = sta->tx_retry_count;
	sinfo->tx_failed = sta->tx_retry_failed;
	sinfo->rx_dropped_misc = sta->rx_dropped;

	if ((sta->local->hw.flags & IEEE80211_HW_SIGNAL_DBM) ||
	    (sta->local->hw.flags & IEEE80211_HW_SIGNAL_UNSPEC)) {
		sinfo->filled |= STATION_INFO_SIGNAL | STATION_INFO_SIGNAL_AVG;
		sinfo->signal = (s8)sta->last_signal;
		sinfo->signal_avg = (s8) -ewma_read(&sta->avg_signal);
	}

	sinfo->txrate.flags = 0;
	if (sta->last_tx_rate.flags & IEEE80211_TX_RC_MCS)
		sinfo->txrate.flags |= RATE_INFO_FLAGS_MCS;
	if (sta->last_tx_rate.flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		sinfo->txrate.flags |= RATE_INFO_FLAGS_40_MHZ_WIDTH;
	if (sta->last_tx_rate.flags & IEEE80211_TX_RC_SHORT_GI)
		sinfo->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
	rate_idx_to_bitrate(&sinfo->txrate, sta, sta->last_tx_rate.idx);

	sinfo->rxrate.flags = 0;
	if (sta->last_rx_rate_flag & RX_FLAG_HT)
		sinfo->rxrate.flags |= RATE_INFO_FLAGS_MCS;
	if (sta->last_rx_rate_flag & RX_FLAG_40MHZ)
		sinfo->rxrate.flags |= RATE_INFO_FLAGS_40_MHZ_WIDTH;
	if (sta->last_rx_rate_flag & RX_FLAG_SHORT_GI)
		sinfo->rxrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
	rate_idx_to_bitrate(&sinfo->rxrate, sta, sta->last_rx_rate_idx);

	if (ieee80211_vif_is_mesh(&sdata->vif)) {
#ifdef CONFIG_MAC80211_MESH
		sinfo->filled |= STATION_INFO_LLID |
				 STATION_INFO_PLID |
				 STATION_INFO_PLINK_STATE;

		sinfo->llid = le16_to_cpu(sta->llid);
		sinfo->plid = le16_to_cpu(sta->plid);
		sinfo->plink_state = sta->plink_state;
#endif
	}

	sinfo->bss_param.flags = 0;
	if (sdata->vif.bss_conf.use_cts_prot)
		sinfo->bss_param.flags |= BSS_PARAM_FLAGS_CTS_PROT;
	if (sdata->vif.bss_conf.use_short_preamble)
		sinfo->bss_param.flags |= BSS_PARAM_FLAGS_SHORT_PREAMBLE;
	if (sdata->vif.bss_conf.use_short_slot)
		sinfo->bss_param.flags |= BSS_PARAM_FLAGS_SHORT_SLOT_TIME;
	sinfo->bss_param.dtim_period = sdata->local->hw.conf.ps_dtim_period;
	sinfo->bss_param.beacon_interval = sdata->vif.bss_conf.beacon_int;

	sinfo->sta_flags.set = 0;
	sinfo->sta_flags.mask = BIT(NL80211_STA_FLAG_AUTHORIZED) |
				BIT(NL80211_STA_FLAG_SHORT_PREAMBLE) |
				BIT(NL80211_STA_FLAG_WME) |
				BIT(NL80211_STA_FLAG_MFP) |
				BIT(NL80211_STA_FLAG_AUTHENTICATED) |
				BIT(NL80211_STA_FLAG_TDLS_PEER);
	if (test_sta_flag(sta, WLAN_STA_AUTHORIZED))
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_AUTHORIZED);
	if (test_sta_flag(sta, WLAN_STA_SHORT_PREAMBLE))
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_SHORT_PREAMBLE);
	if (test_sta_flag(sta, WLAN_STA_WME))
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_WME);
	if (test_sta_flag(sta, WLAN_STA_MFP))
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_MFP);
	if (test_sta_flag(sta, WLAN_STA_AUTH))
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_AUTHENTICATED);
	if (test_sta_flag(sta, WLAN_STA_TDLS_PEER))
		sinfo->sta_flags.set |= BIT(NL80211_STA_FLAG_TDLS_PEER);
}


static int ieee80211_dump_station(struct wiphy *wiphy, struct net_device *dev,
				 int idx, u8 *mac, struct station_info *sinfo)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sta_info *sta;
	int ret = -ENOENT;

	rcu_read_lock();

	sta = sta_info_get_by_idx(sdata, idx);
	if (sta) {
		ret = 0;
		memcpy(mac, sta->sta.addr, ETH_ALEN);
		sta_set_sinfo(sta, sinfo);
	}

	rcu_read_unlock();

	return ret;
}

static int ieee80211_dump_survey(struct wiphy *wiphy, struct net_device *dev,
				 int idx, struct survey_info *survey)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	return drv_get_survey(local, idx, survey);
}

static int ieee80211_get_station(struct wiphy *wiphy, struct net_device *dev,
				 u8 *mac, struct station_info *sinfo)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sta_info *sta;
	int ret = -ENOENT;

	rcu_read_lock();

	sta = sta_info_get_bss(sdata, mac);
	if (sta) {
		ret = 0;
		sta_set_sinfo(sta, sinfo);
	}

	rcu_read_unlock();

	return ret;
}

static void ieee80211_config_ap_ssid(struct ieee80211_sub_if_data *sdata,
				     struct beacon_parameters *params)
{
	struct ieee80211_bss_conf *bss_conf = &sdata->vif.bss_conf;

	bss_conf->ssid_len = params->ssid_len;

	if (params->ssid_len)
		memcpy(bss_conf->ssid, params->ssid, params->ssid_len);

	bss_conf->hidden_ssid =
		(params->hidden_ssid != NL80211_HIDDEN_SSID_NOT_IN_USE);
}

static int ieee80211_set_probe_resp(struct ieee80211_sub_if_data *sdata,
				    u8 *resp, size_t resp_len)
{
	struct sk_buff *new, *old;

	if (!resp || !resp_len)
		return -EINVAL;

	old = rtnl_dereference(sdata->u.ap.probe_resp);

	new = dev_alloc_skb(resp_len);
	if (!new)
		return -ENOMEM;

	memcpy(skb_put(new, resp_len), resp, resp_len);

	rcu_assign_pointer(sdata->u.ap.probe_resp, new);
	synchronize_rcu();

	if (old)
		dev_kfree_skb(old);

	return 0;
}

/*
 * This handles both adding a beacon and setting new beacon info
 */
static int ieee80211_config_beacon(struct ieee80211_sub_if_data *sdata,
				   struct beacon_parameters *params)
{
	struct beacon_data *new, *old;
	int new_head_len, new_tail_len;
	int size;
	int err = -EINVAL;
	u32 changed = 0;

	old = rtnl_dereference(sdata->u.ap.beacon);

	/* head must not be zero-length */
	if (params->head && !params->head_len)
		return -EINVAL;

	/*
	 * This is a kludge. beacon interval should really be part
	 * of the beacon information.
	 */
	if (params->interval &&
	    (sdata->vif.bss_conf.beacon_int != params->interval)) {
		sdata->vif.bss_conf.beacon_int = params->interval;
		ieee80211_bss_info_change_notify(sdata,
						 BSS_CHANGED_BEACON_INT);
	}

	/* Need to have a beacon head if we don't have one yet */
	if (!params->head && !old)
		return err;

	/* sorry, no way to start beaconing without dtim period */
	if (!params->dtim_period && !old)
		return err;

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

	/* new or old dtim period? */
	if (params->dtim_period)
		new->dtim_period = params->dtim_period;
	else
		new->dtim_period = old->dtim_period;

	/*
	 * pointers go into the block we allocated,
	 * memory is | beacon_data | head | tail |
	 */
	new->head = ((u8 *) new) + sizeof(*new);
	new->tail = new->head + new_head_len;
	new->head_len = new_head_len;
	new->tail_len = new_tail_len;

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

	sdata->vif.bss_conf.dtim_period = new->dtim_period;

	RCU_INIT_POINTER(sdata->u.ap.beacon, new);

	synchronize_rcu();

	kfree(old);

	err = ieee80211_set_probe_resp(sdata, params->probe_resp,
				       params->probe_resp_len);
	if (!err)
		changed |= BSS_CHANGED_AP_PROBE_RESP;

	ieee80211_config_ap_ssid(sdata, params);
	changed |= BSS_CHANGED_BEACON_ENABLED |
		   BSS_CHANGED_BEACON |
		   BSS_CHANGED_SSID;

	ieee80211_bss_info_change_notify(sdata, changed);
	return 0;
}

static int ieee80211_add_beacon(struct wiphy *wiphy, struct net_device *dev,
				struct beacon_parameters *params)
{
	struct ieee80211_sub_if_data *sdata;
	struct beacon_data *old;
	struct ieee80211_sub_if_data *vlan;
	int ret;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	old = rtnl_dereference(sdata->u.ap.beacon);
	if (old)
		return -EALREADY;

	ret = ieee80211_config_beacon(sdata, params);
	if (ret)
		return ret;

	/*
	 * Apply control port protocol, this allows us to
	 * not encrypt dynamic WEP control frames.
	 */
	sdata->control_port_protocol = params->crypto.control_port_ethertype;
	sdata->control_port_no_encrypt = params->crypto.control_port_no_encrypt;
	list_for_each_entry(vlan, &sdata->u.ap.vlans, u.vlan.list) {
		vlan->control_port_protocol =
			params->crypto.control_port_ethertype;
		vlan->control_port_no_encrypt =
			params->crypto.control_port_no_encrypt;
	}

	return 0;
}

static int ieee80211_set_beacon(struct wiphy *wiphy, struct net_device *dev,
				struct beacon_parameters *params)
{
	struct ieee80211_sub_if_data *sdata;
	struct beacon_data *old;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	old = rtnl_dereference(sdata->u.ap.beacon);
	if (!old)
		return -ENOENT;

	return ieee80211_config_beacon(sdata, params);
}

static int ieee80211_del_beacon(struct wiphy *wiphy, struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata;
	struct beacon_data *old;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	old = rtnl_dereference(sdata->u.ap.beacon);
	if (!old)
		return -ENOENT;

	RCU_INIT_POINTER(sdata->u.ap.beacon, NULL);
	synchronize_rcu();
	kfree(old);

	ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_BEACON_ENABLED);
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

	memset(msg->da, 0xff, ETH_ALEN);
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

static void sta_apply_parameters(struct ieee80211_local *local,
				 struct sta_info *sta,
				 struct station_parameters *params)
{
	u32 rates;
	int i, j;
	struct ieee80211_supported_band *sband;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	u32 mask, set;

	sband = local->hw.wiphy->bands[local->oper_channel->band];

	mask = params->sta_flags_mask;
	set = params->sta_flags_set;

	if (mask & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
		if (set & BIT(NL80211_STA_FLAG_AUTHORIZED))
			set_sta_flag(sta, WLAN_STA_AUTHORIZED);
		else
			clear_sta_flag(sta, WLAN_STA_AUTHORIZED);
	}

	if (mask & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE)) {
		if (set & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE))
			set_sta_flag(sta, WLAN_STA_SHORT_PREAMBLE);
		else
			clear_sta_flag(sta, WLAN_STA_SHORT_PREAMBLE);
	}

	if (mask & BIT(NL80211_STA_FLAG_WME)) {
		if (set & BIT(NL80211_STA_FLAG_WME)) {
			set_sta_flag(sta, WLAN_STA_WME);
			sta->sta.wme = true;
		} else {
			clear_sta_flag(sta, WLAN_STA_WME);
			sta->sta.wme = false;
		}
	}

	if (mask & BIT(NL80211_STA_FLAG_MFP)) {
		if (set & BIT(NL80211_STA_FLAG_MFP))
			set_sta_flag(sta, WLAN_STA_MFP);
		else
			clear_sta_flag(sta, WLAN_STA_MFP);
	}

	if (mask & BIT(NL80211_STA_FLAG_AUTHENTICATED)) {
		if (set & BIT(NL80211_STA_FLAG_AUTHENTICATED))
			set_sta_flag(sta, WLAN_STA_AUTH);
		else
			clear_sta_flag(sta, WLAN_STA_AUTH);
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
	 * FIXME: updating the following information is racy when this
	 *	  function is called from ieee80211_change_station().
	 *	  However, all this information should be static so
	 *	  maybe we should just reject attemps to change it.
	 */

	if (params->listen_interval >= 0)
		sta->listen_interval = params->listen_interval;

	if (params->supported_rates) {
		rates = 0;

		for (i = 0; i < params->supported_rates_len; i++) {
			int rate = (params->supported_rates[i] & 0x7f) * 5;
			for (j = 0; j < sband->n_bitrates; j++) {
				if (sband->bitrates[j].bitrate == rate)
					rates |= BIT(j);
			}
		}
		sta->sta.supp_rates[local->oper_channel->band] = rates;
	}

	if (params->ht_capa)
		ieee80211_ht_cap_ie_to_sta_ht_cap(sdata, sband,
						  params->ht_capa,
						  &sta->sta.ht_cap);

	if (ieee80211_vif_is_mesh(&sdata->vif)) {
#ifdef CONFIG_MAC80211_MESH
		if (sdata->u.mesh.security & IEEE80211_MESH_SEC_SECURED)
			switch (params->plink_state) {
			case NL80211_PLINK_LISTEN:
			case NL80211_PLINK_ESTAB:
			case NL80211_PLINK_BLOCKED:
				sta->plink_state = params->plink_state;
				break;
			default:
				/*  nothing  */
				break;
			}
		else
			switch (params->plink_action) {
			case PLINK_ACTION_OPEN:
				mesh_plink_open(sta);
				break;
			case PLINK_ACTION_BLOCK:
				mesh_plink_block(sta);
				break;
			}
#endif
	}
}

static int ieee80211_add_station(struct wiphy *wiphy, struct net_device *dev,
				 u8 *mac, struct station_parameters *params)
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

	if (compare_ether_addr(mac, sdata->vif.addr) == 0)
		return -EINVAL;

	if (is_multicast_ether_addr(mac))
		return -EINVAL;

	sta = sta_info_alloc(sdata, mac, GFP_KERNEL);
	if (!sta)
		return -ENOMEM;

	set_sta_flag(sta, WLAN_STA_AUTH);
	set_sta_flag(sta, WLAN_STA_ASSOC);

	sta_apply_parameters(local, sta, params);

	/*
	 * for TDLS, rate control should be initialized only when supported
	 * rates are known.
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
				 u8 *mac)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (mac)
		return sta_info_destroy_addr_bss(sdata, mac);

	sta_info_flush(local, sdata);
	return 0;
}

static int ieee80211_change_station(struct wiphy *wiphy,
				    struct net_device *dev,
				    u8 *mac,
				    struct station_parameters *params)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *vlansdata;

	rcu_read_lock();

	sta = sta_info_get_bss(sdata, mac);
	if (!sta) {
		rcu_read_unlock();
		return -ENOENT;
	}

	/* in station mode, supported rates are only valid with TDLS */
	if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	    params->supported_rates &&
	    !test_sta_flag(sta, WLAN_STA_TDLS_PEER)) {
		rcu_read_unlock();
		return -EINVAL;
	}

	if (params->vlan && params->vlan != sta->sdata->dev) {
		vlansdata = IEEE80211_DEV_TO_SUB_IF(params->vlan);

		if (vlansdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    vlansdata->vif.type != NL80211_IFTYPE_AP) {
			rcu_read_unlock();
			return -EINVAL;
		}

		if (params->vlan->ieee80211_ptr->use_4addr) {
			if (vlansdata->u.vlan.sta) {
				rcu_read_unlock();
				return -EBUSY;
			}

			RCU_INIT_POINTER(vlansdata->u.vlan.sta, sta);
		}

		sta->sdata = vlansdata;
		ieee80211_send_layer2_update(sta);
	}

	sta_apply_parameters(local, sta, params);

	if (test_sta_flag(sta, WLAN_STA_TDLS_PEER) && params->supported_rates)
		rate_control_rate_init(sta);

	rcu_read_unlock();

	if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	    params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED))
		ieee80211_recalc_ps(local, -1);

	return 0;
}

#ifdef CONFIG_MAC80211_MESH
static int ieee80211_add_mpath(struct wiphy *wiphy, struct net_device *dev,
				 u8 *dst, u8 *next_hop)
{
	struct ieee80211_sub_if_data *sdata;
	struct mesh_path *mpath;
	struct sta_info *sta;
	int err;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();
	sta = sta_info_get(sdata, next_hop);
	if (!sta) {
		rcu_read_unlock();
		return -ENOENT;
	}

	err = mesh_path_add(dst, sdata);
	if (err) {
		rcu_read_unlock();
		return err;
	}

	mpath = mesh_path_lookup(dst, sdata);
	if (!mpath) {
		rcu_read_unlock();
		return -ENXIO;
	}
	mesh_path_fix_nexthop(mpath, sta);

	rcu_read_unlock();
	return 0;
}

static int ieee80211_del_mpath(struct wiphy *wiphy, struct net_device *dev,
				 u8 *dst)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (dst)
		return mesh_path_del(dst, sdata);

	mesh_path_flush_by_iface(sdata);
	return 0;
}

static int ieee80211_change_mpath(struct wiphy *wiphy,
				    struct net_device *dev,
				    u8 *dst, u8 *next_hop)
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

	mpath = mesh_path_lookup(dst, sdata);
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
	pinfo->flags = 0;
	if (mpath->flags & MESH_PATH_ACTIVE)
		pinfo->flags |= NL80211_MPATH_FLAG_ACTIVE;
	if (mpath->flags & MESH_PATH_RESOLVING)
		pinfo->flags |= NL80211_MPATH_FLAG_RESOLVING;
	if (mpath->flags & MESH_PATH_SN_VALID)
		pinfo->flags |= NL80211_MPATH_FLAG_SN_VALID;
	if (mpath->flags & MESH_PATH_FIXED)
		pinfo->flags |= NL80211_MPATH_FLAG_FIXED;
	if (mpath->flags & MESH_PATH_RESOLVING)
		pinfo->flags |= NL80211_MPATH_FLAG_RESOLVING;

	pinfo->flags = mpath->flags;
}

static int ieee80211_get_mpath(struct wiphy *wiphy, struct net_device *dev,
			       u8 *dst, u8 *next_hop, struct mpath_info *pinfo)

{
	struct ieee80211_sub_if_data *sdata;
	struct mesh_path *mpath;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();
	mpath = mesh_path_lookup(dst, sdata);
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
	mpath = mesh_path_lookup_by_idx(idx, sdata);
	if (!mpath) {
		rcu_read_unlock();
		return -ENOENT;
	}
	memcpy(dst, mpath->dst, ETH_ALEN);
	mpath_set_pinfo(mpath, next_hop, pinfo);
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
	ifmsh->mesh_pp_id = setup->path_sel_proto;
	ifmsh->mesh_pm_id = setup->path_metric;
	ifmsh->security = IEEE80211_MESH_SEC_NONE;
	if (setup->is_authenticated)
		ifmsh->security |= IEEE80211_MESH_SEC_AUTHED;
	if (setup->is_secure)
		ifmsh->security |= IEEE80211_MESH_SEC_SECURED;

	/* mcast rate setting in Mesh Node */
	memcpy(sdata->vif.bss_conf.mcast_rate, setup->mcast_rate,
						sizeof(setup->mcast_rate));

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
		conf->dot11MeshTTL = nconf->element_ttl;
	if (_chg_mesh_attr(NL80211_MESHCONF_AUTO_OPEN_PLINKS, mask))
		conf->auto_open_plinks = nconf->auto_open_plinks;
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
		    !conf->dot11MeshHWMPRootMode) {
			conf->dot11MeshHWMPRootMode = 1;
			ieee80211_mesh_root_setup(ifmsh);
		}
		conf->dot11MeshGateAnnouncementProtocol =
			nconf->dot11MeshGateAnnouncementProtocol;
	}
	if (_chg_mesh_attr(NL80211_MESHCONF_HWMP_RANN_INTERVAL, mask)) {
		conf->dot11MeshHWMPRannInterval =
			nconf->dot11MeshHWMPRannInterval;
	}
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
	ieee80211_start_mesh(sdata);

	return 0;
}

static int ieee80211_leave_mesh(struct wiphy *wiphy, struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	ieee80211_stop_mesh(sdata);

	return 0;
}
#endif

static int ieee80211_change_bss(struct wiphy *wiphy,
				struct net_device *dev,
				struct bss_parameters *params)
{
	struct ieee80211_sub_if_data *sdata;
	u32 changed = 0;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

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
	    sdata->local->hw.conf.channel->band == IEEE80211_BAND_5GHZ) {
		sdata->vif.bss_conf.use_short_slot = true;
		changed |= BSS_CHANGED_ERP_SLOT;
	}

	if (params->use_short_slot_time >= 0) {
		sdata->vif.bss_conf.use_short_slot =
			params->use_short_slot_time;
		changed |= BSS_CHANGED_ERP_SLOT;
	}

	if (params->basic_rates) {
		int i, j;
		u32 rates = 0;
		struct ieee80211_local *local = wiphy_priv(wiphy);
		struct ieee80211_supported_band *sband =
			wiphy->bands[local->oper_channel->band];

		for (i = 0; i < params->basic_rates_len; i++) {
			int rate = (params->basic_rates[i] & 0x7f) * 5;
			for (j = 0; j < sband->n_bitrates; j++) {
				if (sband->bitrates[j].bitrate == rate)
					rates |= BIT(j);
			}
		}
		sdata->vif.bss_conf.basic_rates = rates;
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

	if (params->queue >= local->hw.queues)
		return -EINVAL;

	sdata->tx_conf[params->queue] = p;
	if (drv_conf_tx(local, sdata, params->queue, &p)) {
		wiphy_debug(local->hw.wiphy,
			    "failed to set TX queue parameters for queue %d\n",
			    params->queue);
		return -EINVAL;
	}

	return 0;
}

static int ieee80211_set_channel(struct wiphy *wiphy,
				 struct net_device *netdev,
				 struct ieee80211_channel *chan,
				 enum nl80211_channel_type channel_type)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata = NULL;
	struct ieee80211_channel *old_oper;
	enum nl80211_channel_type old_oper_type;
	enum nl80211_channel_type old_vif_oper_type= NL80211_CHAN_NO_HT;

	if (netdev)
		sdata = IEEE80211_DEV_TO_SUB_IF(netdev);

	switch (ieee80211_get_channel_mode(local, NULL)) {
	case CHAN_MODE_HOPPING:
		return -EBUSY;
	case CHAN_MODE_FIXED:
		if (local->oper_channel != chan)
			return -EBUSY;
		if (!sdata && local->_oper_channel_type == channel_type)
			return 0;
		break;
	case CHAN_MODE_UNDEFINED:
		break;
	}

	if (sdata)
		old_vif_oper_type = sdata->vif.bss_conf.channel_type;
	old_oper_type = local->_oper_channel_type;

	if (!ieee80211_set_channel_type(local, sdata, channel_type))
		return -EBUSY;

	old_oper = local->oper_channel;
	local->oper_channel = chan;

	/* Update driver if changes were actually made. */
	if ((old_oper != local->oper_channel) ||
	    (old_oper_type != local->_oper_channel_type))
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);

	if (sdata && sdata->vif.type != NL80211_IFTYPE_MONITOR &&
	    old_vif_oper_type != sdata->vif.bss_conf.channel_type)
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_HT);

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
			  struct net_device *dev,
			  struct cfg80211_scan_request *req)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	switch (ieee80211_vif_type_p2p(&sdata->vif)) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_P2P_CLIENT:
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
		if (sdata->u.ap.beacon)
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
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	switch (ieee80211_get_channel_mode(local, sdata)) {
	case CHAN_MODE_HOPPING:
		return -EBUSY;
	case CHAN_MODE_FIXED:
		if (local->oper_channel == req->bss->channel)
			break;
		return -EBUSY;
	case CHAN_MODE_UNDEFINED:
		break;
	}

	return ieee80211_mgd_assoc(IEEE80211_DEV_TO_SUB_IF(dev), req);
}

static int ieee80211_deauth(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_deauth_request *req,
			    void *cookie)
{
	return ieee80211_mgd_deauth(IEEE80211_DEV_TO_SUB_IF(dev),
				    req, cookie);
}

static int ieee80211_disassoc(struct wiphy *wiphy, struct net_device *dev,
			      struct cfg80211_disassoc_request *req,
			      void *cookie)
{
	return ieee80211_mgd_disassoc(IEEE80211_DEV_TO_SUB_IF(dev),
				      req, cookie);
}

static int ieee80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
			       struct cfg80211_ibss_params *params)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	switch (ieee80211_get_channel_mode(local, sdata)) {
	case CHAN_MODE_HOPPING:
		return -EBUSY;
	case CHAN_MODE_FIXED:
		if (!params->channel_fixed)
			return -EBUSY;
		if (local->oper_channel == params->channel)
			break;
		return -EBUSY;
	case CHAN_MODE_UNDEFINED:
		break;
	}

	return ieee80211_ibss_join(sdata, params);
}

static int ieee80211_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	return ieee80211_ibss_leave(sdata);
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

	if (changed & WIPHY_PARAM_COVERAGE_CLASS) {
		err = drv_set_coverage_class(local, wiphy->coverage_class);

		if (err)
			return err;
	}

	if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
		err = drv_set_rts_threshold(local, wiphy->rts_threshold);

		if (err)
			return err;
	}

	if (changed & WIPHY_PARAM_RETRY_SHORT)
		local->hw.conf.short_frame_max_tx_count = wiphy->retry_short;
	if (changed & WIPHY_PARAM_RETRY_LONG)
		local->hw.conf.long_frame_max_tx_count = wiphy->retry_long;
	if (changed &
	    (WIPHY_PARAM_RETRY_SHORT | WIPHY_PARAM_RETRY_LONG))
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_RETRY_LIMITS);

	return 0;
}

static int ieee80211_set_tx_power(struct wiphy *wiphy,
				  enum nl80211_tx_power_setting type, int mbm)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_channel *chan = local->hw.conf.channel;
	u32 changes = 0;

	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		local->user_power_level = -1;
		break;
	case NL80211_TX_POWER_LIMITED:
		if (mbm < 0 || (mbm % 100))
			return -EOPNOTSUPP;
		local->user_power_level = MBM_TO_DBM(mbm);
		break;
	case NL80211_TX_POWER_FIXED:
		if (mbm < 0 || (mbm % 100))
			return -EOPNOTSUPP;
		/* TODO: move to cfg80211 when it knows the channel */
		if (MBM_TO_DBM(mbm) > chan->max_power)
			return -EINVAL;
		local->user_power_level = MBM_TO_DBM(mbm);
		break;
	}

	ieee80211_hw_config(local, changes);

	return 0;
}

static int ieee80211_get_tx_power(struct wiphy *wiphy, int *dbm)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	*dbm = local->hw.conf.power_level;

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
static int ieee80211_testmode_cmd(struct wiphy *wiphy, void *data, int len)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	if (!local->ops->testmode_cmd)
		return -EOPNOTSUPP;

	return local->ops->testmode_cmd(&local->hw, data, len);
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

int __ieee80211_request_smps(struct ieee80211_sub_if_data *sdata,
			     enum ieee80211_smps_mode smps_mode)
{
	const u8 *ap;
	enum ieee80211_smps_mode old_req;
	int err;

	lockdep_assert_held(&sdata->u.mgd.mtx);

	old_req = sdata->u.mgd.req_smps;
	sdata->u.mgd.req_smps = smps_mode;

	if (old_req == smps_mode &&
	    smps_mode != IEEE80211_SMPS_AUTOMATIC)
		return 0;

	/*
	 * If not associated, or current association is not an HT
	 * association, there's no need to send an action frame.
	 */
	if (!sdata->u.mgd.associated ||
	    sdata->vif.bss_conf.channel_type == NL80211_CHAN_NO_HT) {
		mutex_lock(&sdata->local->iflist_mtx);
		ieee80211_recalc_smps(sdata->local);
		mutex_unlock(&sdata->local->iflist_mtx);
		return 0;
	}

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
	mutex_lock(&sdata->u.mgd.mtx);
	__ieee80211_request_smps(sdata, sdata->u.mgd.req_smps);
	mutex_unlock(&sdata->u.mgd.mtx);

	if (local->hw.flags & IEEE80211_HW_SUPPORTS_DYNAMIC_PS)
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);

	ieee80211_recalc_ps(local, -1);

	return 0;
}

static int ieee80211_set_cqm_rssi_config(struct wiphy *wiphy,
					 struct net_device *dev,
					 s32 rssi_thold, u32 rssi_hyst)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_vif *vif = &sdata->vif;
	struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;

	if (rssi_thold == bss_conf->cqm_rssi_thold &&
	    rssi_hyst == bss_conf->cqm_rssi_hyst)
		return 0;

	bss_conf->cqm_rssi_thold = rssi_thold;
	bss_conf->cqm_rssi_hyst = rssi_hyst;

	if (!(local->hw.flags & IEEE80211_HW_SUPPORTS_CQM_RSSI)) {
		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			return -EOPNOTSUPP;
		return 0;
	}

	/* tell the driver upon association, unless already associated */
	if (sdata->u.mgd.associated)
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

	if (local->hw.flags & IEEE80211_HW_HAS_RATE_CONTROL) {
		ret = drv_set_bitrate_mask(local, sdata, mask);
		if (ret)
			return ret;
	}

	for (i = 0; i < IEEE80211_NUM_BANDS; i++)
		sdata->rc_rateidx_mask[i] = mask->control[i].legacy;

	return 0;
}

static int ieee80211_remain_on_channel_hw(struct ieee80211_local *local,
					  struct net_device *dev,
					  struct ieee80211_channel *chan,
					  enum nl80211_channel_type chantype,
					  unsigned int duration, u64 *cookie)
{
	int ret;
	u32 random_cookie;

	lockdep_assert_held(&local->mtx);

	if (local->hw_roc_cookie)
		return -EBUSY;
	/* must be nonzero */
	random_cookie = random32() | 1;

	*cookie = random_cookie;
	local->hw_roc_dev = dev;
	local->hw_roc_cookie = random_cookie;
	local->hw_roc_channel = chan;
	local->hw_roc_channel_type = chantype;
	local->hw_roc_duration = duration;
	ret = drv_remain_on_channel(local, chan, chantype, duration);
	if (ret) {
		local->hw_roc_channel = NULL;
		local->hw_roc_cookie = 0;
	}

	return ret;
}

static int ieee80211_remain_on_channel(struct wiphy *wiphy,
				       struct net_device *dev,
				       struct ieee80211_channel *chan,
				       enum nl80211_channel_type channel_type,
				       unsigned int duration,
				       u64 *cookie)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;

	if (local->ops->remain_on_channel) {
		int ret;

		mutex_lock(&local->mtx);
		ret = ieee80211_remain_on_channel_hw(local, dev,
						     chan, channel_type,
						     duration, cookie);
		local->hw_roc_for_tx = false;
		mutex_unlock(&local->mtx);

		return ret;
	}

	return ieee80211_wk_remain_on_channel(sdata, chan, channel_type,
					      duration, cookie);
}

static int ieee80211_cancel_remain_on_channel_hw(struct ieee80211_local *local,
						 u64 cookie)
{
	int ret;

	lockdep_assert_held(&local->mtx);

	if (local->hw_roc_cookie != cookie)
		return -ENOENT;

	ret = drv_cancel_remain_on_channel(local);
	if (ret)
		return ret;

	local->hw_roc_cookie = 0;
	local->hw_roc_channel = NULL;

	ieee80211_recalc_idle(local);

	return 0;
}

static int ieee80211_cancel_remain_on_channel(struct wiphy *wiphy,
					      struct net_device *dev,
					      u64 cookie)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;

	if (local->ops->cancel_remain_on_channel) {
		int ret;

		mutex_lock(&local->mtx);
		ret = ieee80211_cancel_remain_on_channel_hw(local, cookie);
		mutex_unlock(&local->mtx);

		return ret;
	}

	return ieee80211_wk_cancel_remain_on_channel(sdata, cookie);
}

static enum work_done_result
ieee80211_offchan_tx_done(struct ieee80211_work *wk, struct sk_buff *skb)
{
	/*
	 * Use the data embedded in the work struct for reporting
	 * here so if the driver mangled the SKB before dropping
	 * it (which is the only way we really should get here)
	 * then we don't report mangled data.
	 *
	 * If there was no wait time, then by the time we get here
	 * the driver will likely not have reported the status yet,
	 * so in that case userspace will have to deal with it.
	 */

	if (wk->offchan_tx.wait && !wk->offchan_tx.status)
		cfg80211_mgmt_tx_status(wk->sdata->dev,
					(unsigned long) wk->offchan_tx.frame,
					wk->ie, wk->ie_len, false, GFP_KERNEL);

	return WORK_DONE_DESTROY;
}

static int ieee80211_mgmt_tx(struct wiphy *wiphy, struct net_device *dev,
			     struct ieee80211_channel *chan, bool offchan,
			     enum nl80211_channel_type channel_type,
			     bool channel_type_valid, unsigned int wait,
			     const u8 *buf, size_t len, bool no_cck,
			     bool dont_wait_for_ack, u64 *cookie)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct sta_info *sta;
	struct ieee80211_work *wk;
	const struct ieee80211_mgmt *mgmt = (void *)buf;
	u32 flags;
	bool is_offchan = false;

	if (dont_wait_for_ack)
		flags = IEEE80211_TX_CTL_NO_ACK;
	else
		flags = IEEE80211_TX_INTFL_NL80211_FRAME_TX |
			IEEE80211_TX_CTL_REQ_TX_STATUS;

	/* Check that we are on the requested channel for transmission */
	if (chan != local->tmp_channel &&
	    chan != local->oper_channel)
		is_offchan = true;
	if (channel_type_valid &&
	    (channel_type != local->tmp_channel_type &&
	     channel_type != local->_oper_channel_type))
		is_offchan = true;

	if (chan == local->hw_roc_channel) {
		/* TODO: check channel type? */
		is_offchan = false;
		flags |= IEEE80211_TX_CTL_TX_OFFCHAN;
	}

	if (no_cck)
		flags |= IEEE80211_TX_CTL_NO_CCK_RATE;

	if (is_offchan && !offchan)
		return -EBUSY;

	switch (sdata->vif.type) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_MESH_POINT:
		if (!ieee80211_is_action(mgmt->frame_control) ||
		    mgmt->u.action.category == WLAN_CATEGORY_PUBLIC)
			break;
		rcu_read_lock();
		sta = sta_info_get(sdata, mgmt->da);
		rcu_read_unlock();
		if (!sta)
			return -ENOLINK;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		break;
	default:
		return -EOPNOTSUPP;
	}

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + len);
	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, local->hw.extra_tx_headroom);

	memcpy(skb_put(skb, len), buf, len);

	IEEE80211_SKB_CB(skb)->flags = flags;

	skb->dev = sdata->dev;

	*cookie = (unsigned long) skb;

	if (is_offchan && local->ops->remain_on_channel) {
		unsigned int duration;
		int ret;

		mutex_lock(&local->mtx);
		/*
		 * If the duration is zero, then the driver
		 * wouldn't actually do anything. Set it to
		 * 100 for now.
		 *
		 * TODO: cancel the off-channel operation
		 *       when we get the SKB's TX status and
		 *       the wait time was zero before.
		 */
		duration = 100;
		if (wait)
			duration = wait;
		ret = ieee80211_remain_on_channel_hw(local, dev, chan,
						     channel_type,
						     duration, cookie);
		if (ret) {
			kfree_skb(skb);
			mutex_unlock(&local->mtx);
			return ret;
		}

		local->hw_roc_for_tx = true;
		local->hw_roc_duration = wait;

		/*
		 * queue up frame for transmission after
		 * ieee80211_ready_on_channel call
		 */

		/* modify cookie to prevent API mismatches */
		*cookie ^= 2;
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_TX_OFFCHAN;
		local->hw_roc_skb = skb;
		local->hw_roc_skb_for_status = skb;
		mutex_unlock(&local->mtx);

		return 0;
	}

	/*
	 * Can transmit right away if the channel was the
	 * right one and there's no wait involved... If a
	 * wait is involved, we might otherwise not be on
	 * the right channel for long enough!
	 */
	if (!is_offchan && !wait && !sdata->vif.bss_conf.idle) {
		ieee80211_tx_skb(sdata, skb);
		return 0;
	}

	wk = kzalloc(sizeof(*wk) + len, GFP_KERNEL);
	if (!wk) {
		kfree_skb(skb);
		return -ENOMEM;
	}

	wk->type = IEEE80211_WORK_OFFCHANNEL_TX;
	wk->chan = chan;
	wk->chan_type = channel_type;
	wk->sdata = sdata;
	wk->done = ieee80211_offchan_tx_done;
	wk->offchan_tx.frame = skb;
	wk->offchan_tx.wait = wait;
	wk->ie_len = len;
	memcpy(wk->ie, buf, len);

	ieee80211_add_work(wk);
	return 0;
}

static int ieee80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
					 struct net_device *dev,
					 u64 cookie)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_work *wk;
	int ret = -ENOENT;

	mutex_lock(&local->mtx);

	if (local->ops->cancel_remain_on_channel) {
		cookie ^= 2;
		ret = ieee80211_cancel_remain_on_channel_hw(local, cookie);

		if (ret == 0) {
			kfree_skb(local->hw_roc_skb);
			local->hw_roc_skb = NULL;
			local->hw_roc_skb_for_status = NULL;
		}

		mutex_unlock(&local->mtx);

		return ret;
	}

	list_for_each_entry(wk, &local->work_list, list) {
		if (wk->sdata != sdata)
			continue;

		if (wk->type != IEEE80211_WORK_OFFCHANNEL_TX)
			continue;

		if (cookie != (unsigned long) wk->offchan_tx.frame)
			continue;

		wk->timeout = jiffies;

		ieee80211_queue_work(&local->hw, &local->work_work);
		ret = 0;
		break;
	}
	mutex_unlock(&local->mtx);

	return ret;
}

static void ieee80211_mgmt_frame_register(struct wiphy *wiphy,
					  struct net_device *dev,
					  u16 frame_type, bool reg)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	if (frame_type != (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ))
		return;

	if (reg)
		local->probe_req_reg++;
	else
		local->probe_req_reg--;

	ieee80211_queue_work(&local->hw, &local->reconfig_filter);
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

static int ieee80211_set_ringparam(struct wiphy *wiphy, u32 tx, u32 rx)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	return drv_set_ringparam(local, tx, rx);
}

static void ieee80211_get_ringparam(struct wiphy *wiphy,
				    u32 *tx, u32 *tx_max, u32 *rx, u32 *rx_max)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	drv_get_ringparam(local, tx, tx_max, rx, rx_max);
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

static void ieee80211_tdls_add_ext_capab(struct sk_buff *skb)
{
	u8 *pos = (void *)skb_put(skb, 7);

	*pos++ = WLAN_EID_EXT_CAPABILITY;
	*pos++ = 5; /* len */
	*pos++ = 0x0;
	*pos++ = 0x0;
	*pos++ = 0x0;
	*pos++ = 0x0;
	*pos++ = WLAN_EXT_CAPA5_TDLS_ENABLED;
}

static u16 ieee80211_get_tdls_sta_capab(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	u16 capab;

	capab = 0;
	if (local->oper_channel->band != IEEE80211_BAND_2GHZ)
		return capab;

	if (!(local->hw.flags & IEEE80211_HW_2GHZ_SHORT_SLOT_INCAPABLE))
		capab |= WLAN_CAPABILITY_SHORT_SLOT_TIME;
	if (!(local->hw.flags & IEEE80211_HW_2GHZ_SHORT_PREAMBLE_INCAPABLE))
		capab |= WLAN_CAPABILITY_SHORT_PREAMBLE;

	return capab;
}

static void ieee80211_tdls_add_link_ie(struct sk_buff *skb, u8 *src_addr,
				       u8 *peer, u8 *bssid)
{
	struct ieee80211_tdls_lnkie *lnkid;

	lnkid = (void *)skb_put(skb, sizeof(struct ieee80211_tdls_lnkie));

	lnkid->ie_type = WLAN_EID_LINK_ID;
	lnkid->ie_len = sizeof(struct ieee80211_tdls_lnkie) - 2;

	memcpy(lnkid->bssid, bssid, ETH_ALEN);
	memcpy(lnkid->init_sta, src_addr, ETH_ALEN);
	memcpy(lnkid->resp_sta, peer, ETH_ALEN);
}

static int
ieee80211_prep_tdls_encap_data(struct wiphy *wiphy, struct net_device *dev,
			       u8 *peer, u8 action_code, u8 dialog_token,
			       u16 status_code, struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_tdls_data *tf;

	tf = (void *)skb_put(skb, offsetof(struct ieee80211_tdls_data, u));

	memcpy(tf->da, peer, ETH_ALEN);
	memcpy(tf->sa, sdata->vif.addr, ETH_ALEN);
	tf->ether_type = cpu_to_be16(ETH_P_TDLS);
	tf->payload_type = WLAN_TDLS_SNAP_RFTYPE;

	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_REQUEST;

		skb_put(skb, sizeof(tf->u.setup_req));
		tf->u.setup_req.dialog_token = dialog_token;
		tf->u.setup_req.capability =
			cpu_to_le16(ieee80211_get_tdls_sta_capab(sdata));

		ieee80211_add_srates_ie(&sdata->vif, skb);
		ieee80211_add_ext_srates_ie(&sdata->vif, skb);
		ieee80211_tdls_add_ext_capab(skb);
		break;
	case WLAN_TDLS_SETUP_RESPONSE:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_RESPONSE;

		skb_put(skb, sizeof(tf->u.setup_resp));
		tf->u.setup_resp.status_code = cpu_to_le16(status_code);
		tf->u.setup_resp.dialog_token = dialog_token;
		tf->u.setup_resp.capability =
			cpu_to_le16(ieee80211_get_tdls_sta_capab(sdata));

		ieee80211_add_srates_ie(&sdata->vif, skb);
		ieee80211_add_ext_srates_ie(&sdata->vif, skb);
		ieee80211_tdls_add_ext_capab(skb);
		break;
	case WLAN_TDLS_SETUP_CONFIRM:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_CONFIRM;

		skb_put(skb, sizeof(tf->u.setup_cfm));
		tf->u.setup_cfm.status_code = cpu_to_le16(status_code);
		tf->u.setup_cfm.dialog_token = dialog_token;
		break;
	case WLAN_TDLS_TEARDOWN:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_TEARDOWN;

		skb_put(skb, sizeof(tf->u.teardown));
		tf->u.teardown.reason_code = cpu_to_le16(status_code);
		break;
	case WLAN_TDLS_DISCOVERY_REQUEST:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_DISCOVERY_REQUEST;

		skb_put(skb, sizeof(tf->u.discover_req));
		tf->u.discover_req.dialog_token = dialog_token;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
ieee80211_prep_tdls_direct(struct wiphy *wiphy, struct net_device *dev,
			   u8 *peer, u8 action_code, u8 dialog_token,
			   u16 status_code, struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_mgmt *mgmt;

	mgmt = (void *)skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, peer, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, sdata->u.mgd.bssid, ETH_ALEN);

	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);

	switch (action_code) {
	case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
		skb_put(skb, 1 + sizeof(mgmt->u.action.u.tdls_discover_resp));
		mgmt->u.action.category = WLAN_CATEGORY_PUBLIC;
		mgmt->u.action.u.tdls_discover_resp.action_code =
			WLAN_PUB_ACTION_TDLS_DISCOVER_RES;
		mgmt->u.action.u.tdls_discover_resp.dialog_token =
			dialog_token;
		mgmt->u.action.u.tdls_discover_resp.capability =
			cpu_to_le16(ieee80211_get_tdls_sta_capab(sdata));

		ieee80211_add_srates_ie(&sdata->vif, skb);
		ieee80211_add_ext_srates_ie(&sdata->vif, skb);
		ieee80211_tdls_add_ext_capab(skb);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ieee80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
			       u8 *peer, u8 action_code, u8 dialog_token,
			       u16 status_code, const u8 *extra_ies,
			       size_t extra_ies_len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb = NULL;
	bool send_direct;
	int ret;

	if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return -ENOTSUPP;

	/* make sure we are in managed mode, and associated */
	if (sdata->vif.type != NL80211_IFTYPE_STATION ||
	    !sdata->u.mgd.associated)
		return -EINVAL;

#ifdef CONFIG_MAC80211_VERBOSE_TDLS_DEBUG
	printk(KERN_DEBUG "TDLS mgmt action %d peer %pM\n", action_code, peer);
#endif

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    max(sizeof(struct ieee80211_mgmt),
				sizeof(struct ieee80211_tdls_data)) +
			    50 + /* supported rates */
			    7 + /* ext capab */
			    extra_ies_len +
			    sizeof(struct ieee80211_tdls_lnkie));
	if (!skb)
		return -ENOMEM;

	info = IEEE80211_SKB_CB(skb);
	skb_reserve(skb, local->hw.extra_tx_headroom);

	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_RESPONSE:
	case WLAN_TDLS_SETUP_CONFIRM:
	case WLAN_TDLS_TEARDOWN:
	case WLAN_TDLS_DISCOVERY_REQUEST:
		ret = ieee80211_prep_tdls_encap_data(wiphy, dev, peer,
						     action_code, dialog_token,
						     status_code, skb);
		send_direct = false;
		break;
	case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
		ret = ieee80211_prep_tdls_direct(wiphy, dev, peer, action_code,
						 dialog_token, status_code,
						 skb);
		send_direct = true;
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}

	if (ret < 0)
		goto fail;

	if (extra_ies_len)
		memcpy(skb_put(skb, extra_ies_len), extra_ies, extra_ies_len);

	/* the TDLS link IE is always added last */
	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_CONFIRM:
	case WLAN_TDLS_TEARDOWN:
	case WLAN_TDLS_DISCOVERY_REQUEST:
		/* we are the initiator */
		ieee80211_tdls_add_link_ie(skb, sdata->vif.addr, peer,
					   sdata->u.mgd.bssid);
		break;
	case WLAN_TDLS_SETUP_RESPONSE:
	case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
		/* we are the responder */
		ieee80211_tdls_add_link_ie(skb, peer, sdata->vif.addr,
					   sdata->u.mgd.bssid);
		break;
	default:
		ret = -ENOTSUPP;
		goto fail;
	}

	if (send_direct) {
		ieee80211_tx_skb(sdata, skb);
		return 0;
	}

	/*
	 * According to 802.11z: Setup req/resp are sent in AC_BK, otherwise
	 * we should default to AC_VI.
	 */
	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_RESPONSE:
		skb_set_queue_mapping(skb, IEEE80211_AC_BK);
		skb->priority = 2;
		break;
	default:
		skb_set_queue_mapping(skb, IEEE80211_AC_VI);
		skb->priority = 5;
		break;
	}

	/* disable bottom halves when entering the Tx path */
	local_bh_disable();
	ret = ieee80211_subif_start_xmit(skb, dev);
	local_bh_enable();

	return ret;

fail:
	dev_kfree_skb(skb);
	return ret;
}

static int ieee80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
			       u8 *peer, enum nl80211_tdls_operation oper)
{
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return -ENOTSUPP;

	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return -EINVAL;

#ifdef CONFIG_MAC80211_VERBOSE_TDLS_DEBUG
	printk(KERN_DEBUG "TDLS oper %d peer %pM\n", oper, peer);
#endif

	switch (oper) {
	case NL80211_TDLS_ENABLE_LINK:
		rcu_read_lock();
		sta = sta_info_get(sdata, peer);
		if (!sta) {
			rcu_read_unlock();
			return -ENOLINK;
		}

		set_sta_flag(sta, WLAN_STA_TDLS_PEER_AUTH);
		rcu_read_unlock();
		break;
	case NL80211_TDLS_DISABLE_LINK:
		return sta_info_destroy_addr(sdata, peer);
	case NL80211_TDLS_TEARDOWN:
	case NL80211_TDLS_SETUP:
	case NL80211_TDLS_DISCOVERY_REQ:
		/* We don't support in-driver setup/teardown/discovery */
		return -ENOTSUPP;
	default:
		return -ENOTSUPP;
	}

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

	rcu_read_lock();
	sta = sta_info_get(sdata, peer);
	if (sta) {
		qos = test_sta_flag(sta, WLAN_STA_WME);
		rcu_read_unlock();
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
	if (!skb)
		return -ENOMEM;

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
	ieee80211_xmit(sdata, skb);
	local_bh_enable();

	*cookie = (unsigned long) skb;
	return 0;
}

static struct ieee80211_channel *
ieee80211_wiphy_get_channel(struct wiphy *wiphy)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);

	return local->oper_channel;
}

struct cfg80211_ops mac80211_config_ops = {
	.add_virtual_intf = ieee80211_add_iface,
	.del_virtual_intf = ieee80211_del_iface,
	.change_virtual_intf = ieee80211_change_iface,
	.add_key = ieee80211_add_key,
	.del_key = ieee80211_del_key,
	.get_key = ieee80211_get_key,
	.set_default_key = ieee80211_config_default_key,
	.set_default_mgmt_key = ieee80211_config_default_mgmt_key,
	.add_beacon = ieee80211_add_beacon,
	.set_beacon = ieee80211_set_beacon,
	.del_beacon = ieee80211_del_beacon,
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
	.update_mesh_config = ieee80211_update_mesh_config,
	.get_mesh_config = ieee80211_get_mesh_config,
	.join_mesh = ieee80211_join_mesh,
	.leave_mesh = ieee80211_leave_mesh,
#endif
	.change_bss = ieee80211_change_bss,
	.set_txq_params = ieee80211_set_txq_params,
	.set_channel = ieee80211_set_channel,
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
	.set_ringparam = ieee80211_set_ringparam,
	.get_ringparam = ieee80211_get_ringparam,
	.set_rekey_data = ieee80211_set_rekey_data,
	.tdls_oper = ieee80211_tdls_oper,
	.tdls_mgmt = ieee80211_tdls_mgmt,
	.probe_client = ieee80211_probe_client,
	.get_channel = ieee80211_wiphy_get_channel,
	.set_noack_map = ieee80211_set_noack_map,
};
