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
#include <linux/rcupdate.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "cfg.h"
#include "rate.h"
#include "mesh.h"

struct ieee80211_hw *wiphy_to_hw(struct wiphy *wiphy)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	return &local->hw;
}
EXPORT_SYMBOL(wiphy_to_hw);

static bool nl80211_type_check(enum nl80211_iftype type)
{
	switch (type) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_MONITOR:
#ifdef CONFIG_MAC80211_MESH
	case NL80211_IFTYPE_MESH_POINT:
#endif
	case NL80211_IFTYPE_WDS:
		return true;
	default:
		return false;
	}
}

static int ieee80211_add_iface(struct wiphy *wiphy, char *name,
			       enum nl80211_iftype type, u32 *flags,
			       struct vif_params *params)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct net_device *dev;
	struct ieee80211_sub_if_data *sdata;
	int err;

	if (!nl80211_type_check(type))
		return -EINVAL;

	err = ieee80211_if_add(local, name, &dev, type, params);
	if (err || type != NL80211_IFTYPE_MONITOR || !flags)
		return err;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	sdata->u.mntr_flags = *flags;
	return 0;
}

static int ieee80211_del_iface(struct wiphy *wiphy, int ifindex)
{
	struct net_device *dev;
	struct ieee80211_sub_if_data *sdata;

	/* we're under RTNL */
	dev = __dev_get_by_index(&init_net, ifindex);
	if (!dev)
		return -ENODEV;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	ieee80211_if_remove(sdata);

	return 0;
}

static int ieee80211_change_iface(struct wiphy *wiphy, int ifindex,
				  enum nl80211_iftype type, u32 *flags,
				  struct vif_params *params)
{
	struct net_device *dev;
	struct ieee80211_sub_if_data *sdata;
	int ret;

	/* we're under RTNL */
	dev = __dev_get_by_index(&init_net, ifindex);
	if (!dev)
		return -ENODEV;

	if (!nl80211_type_check(type))
		return -EINVAL;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	ret = ieee80211_if_change_type(sdata, type);
	if (ret)
		return ret;

	if (netif_running(sdata->dev))
		return -EBUSY;

	if (ieee80211_vif_is_mesh(&sdata->vif) && params->mesh_id_len)
		ieee80211_sdata_set_mesh_id(sdata,
					    params->mesh_id_len,
					    params->mesh_id);

	if (sdata->vif.type != NL80211_IFTYPE_MONITOR || !flags)
		return 0;

	sdata->u.mntr_flags = *flags;
	return 0;
}

static int ieee80211_add_key(struct wiphy *wiphy, struct net_device *dev,
			     u8 key_idx, u8 *mac_addr,
			     struct key_params *params)
{
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta = NULL;
	enum ieee80211_key_alg alg;
	struct ieee80211_key *key;
	int err;

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

	key = ieee80211_key_alloc(alg, key_idx, params->key_len, params->key);
	if (!key)
		return -ENOMEM;

	rcu_read_lock();

	if (mac_addr) {
		sta = sta_info_get(sdata->local, mac_addr);
		if (!sta) {
			ieee80211_key_free(key);
			err = -ENOENT;
			goto out_unlock;
		}
	}

	ieee80211_key_link(key, sdata, sta);

	err = 0;
 out_unlock:
	rcu_read_unlock();

	return err;
}

static int ieee80211_del_key(struct wiphy *wiphy, struct net_device *dev,
			     u8 key_idx, u8 *mac_addr)
{
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	int ret;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();

	if (mac_addr) {
		ret = -ENOENT;

		sta = sta_info_get(sdata->local, mac_addr);
		if (!sta)
			goto out_unlock;

		if (sta->key) {
			ieee80211_key_free(sta->key);
			WARN_ON(sta->key);
			ret = 0;
		}

		goto out_unlock;
	}

	if (!sdata->keys[key_idx]) {
		ret = -ENOENT;
		goto out_unlock;
	}

	ieee80211_key_free(sdata->keys[key_idx]);
	WARN_ON(sdata->keys[key_idx]);

	ret = 0;
 out_unlock:
	rcu_read_unlock();

	return ret;
}

static int ieee80211_get_key(struct wiphy *wiphy, struct net_device *dev,
			     u8 key_idx, u8 *mac_addr, void *cookie,
			     void (*callback)(void *cookie,
					      struct key_params *params))
{
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta = NULL;
	u8 seq[6] = {0};
	struct key_params params;
	struct ieee80211_key *key;
	u32 iv32;
	u16 iv16;
	int err = -ENOENT;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	rcu_read_lock();

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

		iv32 = key->u.tkip.tx.iv32;
		iv16 = key->u.tkip.tx.iv16;

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
	rcu_read_unlock();
	return err;
}

static int ieee80211_config_default_key(struct wiphy *wiphy,
					struct net_device *dev,
					u8 key_idx)
{
	struct ieee80211_sub_if_data *sdata;

	rcu_read_lock();

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	ieee80211_set_default_key(sdata, key_idx);

	rcu_read_unlock();

	return 0;
}

static void sta_set_sinfo(struct sta_info *sta, struct station_info *sinfo)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;

	sinfo->filled = STATION_INFO_INACTIVE_TIME |
			STATION_INFO_RX_BYTES |
			STATION_INFO_TX_BYTES;

	sinfo->inactive_time = jiffies_to_msecs(jiffies - sta->last_rx);
	sinfo->rx_bytes = sta->rx_bytes;
	sinfo->tx_bytes = sta->tx_bytes;

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
}


static int ieee80211_dump_station(struct wiphy *wiphy, struct net_device *dev,
				 int idx, u8 *mac, struct station_info *sinfo)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	int ret = -ENOENT;

	rcu_read_lock();

	sta = sta_info_get_by_idx(local, idx, dev);
	if (sta) {
		ret = 0;
		memcpy(mac, sta->sta.addr, ETH_ALEN);
		sta_set_sinfo(sta, sinfo);
	}

	rcu_read_unlock();

	return ret;
}

static int ieee80211_get_station(struct wiphy *wiphy, struct net_device *dev,
				 u8 *mac, struct station_info *sinfo)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	int ret = -ENOENT;

	rcu_read_lock();

	/* XXX: verify sta->dev == dev */

	sta = sta_info_get(local, mac);
	if (sta) {
		ret = 0;
		sta_set_sinfo(sta, sinfo);
	}

	rcu_read_unlock();

	return ret;
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

	old = sdata->u.ap.beacon;

	/* head must not be zero-length */
	if (params->head && !params->head_len)
		return -EINVAL;

	/*
	 * This is a kludge. beacon interval should really be part
	 * of the beacon information.
	 */
	if (params->interval) {
		sdata->local->hw.conf.beacon_int = params->interval;
		if (ieee80211_hw_config(sdata->local))
			return -EINVAL;
		/*
		 * We updated some parameter so if below bails out
		 * it's not an error.
		 */
		err = 0;
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

	rcu_assign_pointer(sdata->u.ap.beacon, new);

	synchronize_rcu();

	kfree(old);

	return ieee80211_if_config(sdata, IEEE80211_IFCC_BEACON);
}

static int ieee80211_add_beacon(struct wiphy *wiphy, struct net_device *dev,
				struct beacon_parameters *params)
{
	struct ieee80211_sub_if_data *sdata;
	struct beacon_data *old;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type != NL80211_IFTYPE_AP)
		return -EINVAL;

	old = sdata->u.ap.beacon;

	if (old)
		return -EALREADY;

	return ieee80211_config_beacon(sdata, params);
}

static int ieee80211_set_beacon(struct wiphy *wiphy, struct net_device *dev,
				struct beacon_parameters *params)
{
	struct ieee80211_sub_if_data *sdata;
	struct beacon_data *old;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type != NL80211_IFTYPE_AP)
		return -EINVAL;

	old = sdata->u.ap.beacon;

	if (!old)
		return -ENOENT;

	return ieee80211_config_beacon(sdata, params);
}

static int ieee80211_del_beacon(struct wiphy *wiphy, struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata;
	struct beacon_data *old;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type != NL80211_IFTYPE_AP)
		return -EINVAL;

	old = sdata->u.ap.beacon;

	if (!old)
		return -ENOENT;

	rcu_assign_pointer(sdata->u.ap.beacon, NULL);
	synchronize_rcu();
	kfree(old);

	return ieee80211_if_config(sdata, IEEE80211_IFCC_BEACON);
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
} __attribute__ ((packed));

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
	netif_rx(skb);
}

static void sta_apply_parameters(struct ieee80211_local *local,
				 struct sta_info *sta,
				 struct station_parameters *params)
{
	u32 rates;
	int i, j;
	struct ieee80211_supported_band *sband;
	struct ieee80211_sub_if_data *sdata = sta->sdata;

	/*
	 * FIXME: updating the flags is racy when this function is
	 *	  called from ieee80211_change_station(), this will
	 *	  be resolved in a future patch.
	 */

	if (params->station_flags & STATION_FLAG_CHANGED) {
		spin_lock_bh(&sta->lock);
		sta->flags &= ~WLAN_STA_AUTHORIZED;
		if (params->station_flags & STATION_FLAG_AUTHORIZED)
			sta->flags |= WLAN_STA_AUTHORIZED;

		sta->flags &= ~WLAN_STA_SHORT_PREAMBLE;
		if (params->station_flags & STATION_FLAG_SHORT_PREAMBLE)
			sta->flags |= WLAN_STA_SHORT_PREAMBLE;

		sta->flags &= ~WLAN_STA_WME;
		if (params->station_flags & STATION_FLAG_WME)
			sta->flags |= WLAN_STA_WME;
		spin_unlock_bh(&sta->lock);
	}

	/*
	 * FIXME: updating the following information is racy when this
	 *	  function is called from ieee80211_change_station().
	 *	  However, all this information should be static so
	 *	  maybe we should just reject attemps to change it.
	 */

	if (params->aid) {
		sta->sta.aid = params->aid;
		if (sta->sta.aid > IEEE80211_MAX_AID)
			sta->sta.aid = 0; /* XXX: should this be an error? */
	}

	if (params->listen_interval >= 0)
		sta->listen_interval = params->listen_interval;

	if (params->supported_rates) {
		rates = 0;
		sband = local->hw.wiphy->bands[local->oper_channel->band];

		for (i = 0; i < params->supported_rates_len; i++) {
			int rate = (params->supported_rates[i] & 0x7f) * 5;
			for (j = 0; j < sband->n_bitrates; j++) {
				if (sband->bitrates[j].bitrate == rate)
					rates |= BIT(j);
			}
		}
		sta->sta.supp_rates[local->oper_channel->band] = rates;
	}

	if (params->ht_capa) {
		ieee80211_ht_cap_ie_to_ht_info(params->ht_capa,
					       &sta->sta.ht_info);
	}

	if (ieee80211_vif_is_mesh(&sdata->vif) && params->plink_action) {
		switch (params->plink_action) {
		case PLINK_ACTION_OPEN:
			mesh_plink_open(sta);
			break;
		case PLINK_ACTION_BLOCK:
			mesh_plink_block(sta);
			break;
		}
	}
}

static int ieee80211_add_station(struct wiphy *wiphy, struct net_device *dev,
				 u8 *mac, struct station_parameters *params)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata;
	int err;

	/* Prevent a race with changing the rate control algorithm */
	if (!netif_running(dev))
		return -ENETDOWN;

	if (params->vlan) {
		sdata = IEEE80211_DEV_TO_SUB_IF(params->vlan);

		if (sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    sdata->vif.type != NL80211_IFTYPE_AP)
			return -EINVAL;
	} else
		sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (compare_ether_addr(mac, dev->dev_addr) == 0)
		return -EINVAL;

	if (is_multicast_ether_addr(mac))
		return -EINVAL;

	sta = sta_info_alloc(sdata, mac, GFP_KERNEL);
	if (!sta)
		return -ENOMEM;

	sta->flags = WLAN_STA_AUTH | WLAN_STA_ASSOC;

	sta_apply_parameters(local, sta, params);

	rate_control_rate_init(sta);

	rcu_read_lock();

	err = sta_info_insert(sta);
	if (err) {
		/* STA has been freed */
		rcu_read_unlock();
		return err;
	}

	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN ||
	    sdata->vif.type == NL80211_IFTYPE_AP)
		ieee80211_send_layer2_update(sta);

	rcu_read_unlock();

	return 0;
}

static int ieee80211_del_station(struct wiphy *wiphy, struct net_device *dev,
				 u8 *mac)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (mac) {
		rcu_read_lock();

		/* XXX: get sta belonging to dev */
		sta = sta_info_get(local, mac);
		if (!sta) {
			rcu_read_unlock();
			return -ENOENT;
		}

		sta_info_unlink(&sta);
		rcu_read_unlock();

		sta_info_destroy(sta);
	} else
		sta_info_flush(local, sdata);

	return 0;
}

static int ieee80211_change_station(struct wiphy *wiphy,
				    struct net_device *dev,
				    u8 *mac,
				    struct station_parameters *params)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *vlansdata;

	rcu_read_lock();

	/* XXX: get sta belonging to dev */
	sta = sta_info_get(local, mac);
	if (!sta) {
		rcu_read_unlock();
		return -ENOENT;
	}

	if (params->vlan && params->vlan != sta->sdata->dev) {
		vlansdata = IEEE80211_DEV_TO_SUB_IF(params->vlan);

		if (vlansdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    vlansdata->vif.type != NL80211_IFTYPE_AP) {
			rcu_read_unlock();
			return -EINVAL;
		}

		sta->sdata = vlansdata;
		ieee80211_send_layer2_update(sta);
	}

	sta_apply_parameters(local, sta, params);

	rcu_read_unlock();

	return 0;
}

#ifdef CONFIG_MAC80211_MESH
static int ieee80211_add_mpath(struct wiphy *wiphy, struct net_device *dev,
				 u8 *dst, u8 *next_hop)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata;
	struct mesh_path *mpath;
	struct sta_info *sta;
	int err;

	if (!netif_running(dev))
		return -ENETDOWN;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	rcu_read_lock();
	sta = sta_info_get(local, next_hop);
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

	mesh_path_flush(sdata);
	return 0;
}

static int ieee80211_change_mpath(struct wiphy *wiphy,
				    struct net_device *dev,
				    u8 *dst, u8 *next_hop)
{
	struct ieee80211_local *local = wiphy_priv(wiphy);
	struct ieee80211_sub_if_data *sdata;
	struct mesh_path *mpath;
	struct sta_info *sta;

	if (!netif_running(dev))
		return -ENETDOWN;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	rcu_read_lock();

	sta = sta_info_get(local, next_hop);
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
	if (mpath->next_hop)
		memcpy(next_hop, mpath->next_hop->sta.addr, ETH_ALEN);
	else
		memset(next_hop, 0, ETH_ALEN);

	pinfo->filled = MPATH_INFO_FRAME_QLEN |
			MPATH_INFO_DSN |
			MPATH_INFO_METRIC |
			MPATH_INFO_EXPTIME |
			MPATH_INFO_DISCOVERY_TIMEOUT |
			MPATH_INFO_DISCOVERY_RETRIES |
			MPATH_INFO_FLAGS;

	pinfo->frame_qlen = mpath->frame_queue.qlen;
	pinfo->dsn = mpath->dsn;
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
	if (mpath->flags & MESH_PATH_DSN_VALID)
		pinfo->flags |= NL80211_MPATH_FLAG_DSN_VALID;
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

	if (sdata->vif.type != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

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

	if (sdata->vif.type != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

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
#endif

static int ieee80211_change_bss(struct wiphy *wiphy,
				struct net_device *dev,
				struct bss_parameters *params)
{
	struct ieee80211_sub_if_data *sdata;
	u32 changed = 0;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type != NL80211_IFTYPE_AP)
		return -EINVAL;

	if (params->use_cts_prot >= 0) {
		sdata->bss_conf.use_cts_prot = params->use_cts_prot;
		changed |= BSS_CHANGED_ERP_CTS_PROT;
	}
	if (params->use_short_preamble >= 0) {
		sdata->bss_conf.use_short_preamble =
			params->use_short_preamble;
		changed |= BSS_CHANGED_ERP_PREAMBLE;
	}
	if (params->use_short_slot_time >= 0) {
		sdata->bss_conf.use_short_slot =
			params->use_short_slot_time;
		changed |= BSS_CHANGED_ERP_SLOT;
	}

	ieee80211_bss_info_change_notify(sdata, changed);

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
	.add_beacon = ieee80211_add_beacon,
	.set_beacon = ieee80211_set_beacon,
	.del_beacon = ieee80211_del_beacon,
	.add_station = ieee80211_add_station,
	.del_station = ieee80211_del_station,
	.change_station = ieee80211_change_station,
	.get_station = ieee80211_get_station,
	.dump_station = ieee80211_dump_station,
#ifdef CONFIG_MAC80211_MESH
	.add_mpath = ieee80211_add_mpath,
	.del_mpath = ieee80211_del_mpath,
	.change_mpath = ieee80211_change_mpath,
	.get_mpath = ieee80211_get_mpath,
	.dump_mpath = ieee80211_dump_mpath,
#endif
	.change_bss = ieee80211_change_bss,
};
