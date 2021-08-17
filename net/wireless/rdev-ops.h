/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CFG80211_RDEV_OPS
#define __CFG80211_RDEV_OPS

#include <linux/rtnetlink.h>
#include <net/cfg80211.h>
#include "core.h"
#include "trace.h"

static inline int rdev_suspend(struct cfg80211_registered_device *rdev,
			       struct cfg80211_wowlan *wowlan)
{
	int ret;
	trace_rdev_suspend(&rdev->wiphy, wowlan);
	ret = rdev->ops->suspend(&rdev->wiphy, wowlan);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_resume(struct cfg80211_registered_device *rdev)
{
	int ret;
	trace_rdev_resume(&rdev->wiphy);
	ret = rdev->ops->resume(&rdev->wiphy);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline void rdev_set_wakeup(struct cfg80211_registered_device *rdev,
				   bool enabled)
{
	trace_rdev_set_wakeup(&rdev->wiphy, enabled);
	rdev->ops->set_wakeup(&rdev->wiphy, enabled);
	trace_rdev_return_void(&rdev->wiphy);
}

static inline struct wireless_dev
*rdev_add_virtual_intf(struct cfg80211_registered_device *rdev, char *name,
		       unsigned char name_assign_type,
		       enum nl80211_iftype type,
		       struct vif_params *params)
{
	struct wireless_dev *ret;
	trace_rdev_add_virtual_intf(&rdev->wiphy, name, type);
	ret = rdev->ops->add_virtual_intf(&rdev->wiphy, name, name_assign_type,
					  type, params);
	trace_rdev_return_wdev(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_del_virtual_intf(struct cfg80211_registered_device *rdev,
		      struct wireless_dev *wdev)
{
	int ret;
	trace_rdev_del_virtual_intf(&rdev->wiphy, wdev);
	ret = rdev->ops->del_virtual_intf(&rdev->wiphy, wdev);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_change_virtual_intf(struct cfg80211_registered_device *rdev,
			 struct net_device *dev, enum nl80211_iftype type,
			 struct vif_params *params)
{
	int ret;
	trace_rdev_change_virtual_intf(&rdev->wiphy, dev, type);
	ret = rdev->ops->change_virtual_intf(&rdev->wiphy, dev, type, params);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_add_key(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, u8 key_index,
			       bool pairwise, const u8 *mac_addr,
			       struct key_params *params)
{
	int ret;
	trace_rdev_add_key(&rdev->wiphy, netdev, key_index, pairwise,
			   mac_addr, params->mode);
	ret = rdev->ops->add_key(&rdev->wiphy, netdev, key_index, pairwise,
				  mac_addr, params);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_get_key(struct cfg80211_registered_device *rdev, struct net_device *netdev,
	     u8 key_index, bool pairwise, const u8 *mac_addr, void *cookie,
	     void (*callback)(void *cookie, struct key_params*))
{
	int ret;
	trace_rdev_get_key(&rdev->wiphy, netdev, key_index, pairwise, mac_addr);
	ret = rdev->ops->get_key(&rdev->wiphy, netdev, key_index, pairwise,
				  mac_addr, cookie, callback);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_del_key(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, u8 key_index,
			       bool pairwise, const u8 *mac_addr)
{
	int ret;
	trace_rdev_del_key(&rdev->wiphy, netdev, key_index, pairwise, mac_addr);
	ret = rdev->ops->del_key(&rdev->wiphy, netdev, key_index, pairwise,
				  mac_addr);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_set_default_key(struct cfg80211_registered_device *rdev,
		     struct net_device *netdev, u8 key_index, bool unicast,
		     bool multicast)
{
	int ret;
	trace_rdev_set_default_key(&rdev->wiphy, netdev, key_index,
				   unicast, multicast);
	ret = rdev->ops->set_default_key(&rdev->wiphy, netdev, key_index,
					  unicast, multicast);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_set_default_mgmt_key(struct cfg80211_registered_device *rdev,
			  struct net_device *netdev, u8 key_index)
{
	int ret;
	trace_rdev_set_default_mgmt_key(&rdev->wiphy, netdev, key_index);
	ret = rdev->ops->set_default_mgmt_key(&rdev->wiphy, netdev,
					       key_index);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_set_default_beacon_key(struct cfg80211_registered_device *rdev,
			    struct net_device *netdev, u8 key_index)
{
	int ret;

	trace_rdev_set_default_beacon_key(&rdev->wiphy, netdev, key_index);
	ret = rdev->ops->set_default_beacon_key(&rdev->wiphy, netdev,
						key_index);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_start_ap(struct cfg80211_registered_device *rdev,
				struct net_device *dev,
				struct cfg80211_ap_settings *settings)
{
	int ret;
	trace_rdev_start_ap(&rdev->wiphy, dev, settings);
	ret = rdev->ops->start_ap(&rdev->wiphy, dev, settings);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_change_beacon(struct cfg80211_registered_device *rdev,
				     struct net_device *dev,
				     struct cfg80211_beacon_data *info)
{
	int ret;
	trace_rdev_change_beacon(&rdev->wiphy, dev, info);
	ret = rdev->ops->change_beacon(&rdev->wiphy, dev, info);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_stop_ap(struct cfg80211_registered_device *rdev,
			       struct net_device *dev)
{
	int ret;
	trace_rdev_stop_ap(&rdev->wiphy, dev);
	ret = rdev->ops->stop_ap(&rdev->wiphy, dev);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_add_station(struct cfg80211_registered_device *rdev,
				   struct net_device *dev, u8 *mac,
				   struct station_parameters *params)
{
	int ret;
	trace_rdev_add_station(&rdev->wiphy, dev, mac, params);
	ret = rdev->ops->add_station(&rdev->wiphy, dev, mac, params);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_del_station(struct cfg80211_registered_device *rdev,
				   struct net_device *dev,
				   struct station_del_parameters *params)
{
	int ret;
	trace_rdev_del_station(&rdev->wiphy, dev, params);
	ret = rdev->ops->del_station(&rdev->wiphy, dev, params);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_change_station(struct cfg80211_registered_device *rdev,
				      struct net_device *dev, u8 *mac,
				      struct station_parameters *params)
{
	int ret;
	trace_rdev_change_station(&rdev->wiphy, dev, mac, params);
	ret = rdev->ops->change_station(&rdev->wiphy, dev, mac, params);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_get_station(struct cfg80211_registered_device *rdev,
				   struct net_device *dev, const u8 *mac,
				   struct station_info *sinfo)
{
	int ret;
	trace_rdev_get_station(&rdev->wiphy, dev, mac);
	ret = rdev->ops->get_station(&rdev->wiphy, dev, mac, sinfo);
	trace_rdev_return_int_station_info(&rdev->wiphy, ret, sinfo);
	return ret;
}

static inline int rdev_dump_station(struct cfg80211_registered_device *rdev,
				    struct net_device *dev, int idx, u8 *mac,
				    struct station_info *sinfo)
{
	int ret;
	trace_rdev_dump_station(&rdev->wiphy, dev, idx, mac);
	ret = rdev->ops->dump_station(&rdev->wiphy, dev, idx, mac, sinfo);
	trace_rdev_return_int_station_info(&rdev->wiphy, ret, sinfo);
	return ret;
}

static inline int rdev_add_mpath(struct cfg80211_registered_device *rdev,
				 struct net_device *dev, u8 *dst, u8 *next_hop)
{
	int ret;
	trace_rdev_add_mpath(&rdev->wiphy, dev, dst, next_hop);
	ret = rdev->ops->add_mpath(&rdev->wiphy, dev, dst, next_hop);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_del_mpath(struct cfg80211_registered_device *rdev,
				 struct net_device *dev, u8 *dst)
{
	int ret;
	trace_rdev_del_mpath(&rdev->wiphy, dev, dst);
	ret = rdev->ops->del_mpath(&rdev->wiphy, dev, dst);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_change_mpath(struct cfg80211_registered_device *rdev,
				    struct net_device *dev, u8 *dst,
				    u8 *next_hop)
{
	int ret;
	trace_rdev_change_mpath(&rdev->wiphy, dev, dst, next_hop);
	ret = rdev->ops->change_mpath(&rdev->wiphy, dev, dst, next_hop);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_get_mpath(struct cfg80211_registered_device *rdev,
				 struct net_device *dev, u8 *dst, u8 *next_hop,
				 struct mpath_info *pinfo)
{
	int ret;
	trace_rdev_get_mpath(&rdev->wiphy, dev, dst, next_hop);
	ret = rdev->ops->get_mpath(&rdev->wiphy, dev, dst, next_hop, pinfo);
	trace_rdev_return_int_mpath_info(&rdev->wiphy, ret, pinfo);
	return ret;

}

static inline int rdev_get_mpp(struct cfg80211_registered_device *rdev,
			       struct net_device *dev, u8 *dst, u8 *mpp,
			       struct mpath_info *pinfo)
{
	int ret;

	trace_rdev_get_mpp(&rdev->wiphy, dev, dst, mpp);
	ret = rdev->ops->get_mpp(&rdev->wiphy, dev, dst, mpp, pinfo);
	trace_rdev_return_int_mpath_info(&rdev->wiphy, ret, pinfo);
	return ret;
}

static inline int rdev_dump_mpath(struct cfg80211_registered_device *rdev,
				  struct net_device *dev, int idx, u8 *dst,
				  u8 *next_hop, struct mpath_info *pinfo)

{
	int ret;
	trace_rdev_dump_mpath(&rdev->wiphy, dev, idx, dst, next_hop);
	ret = rdev->ops->dump_mpath(&rdev->wiphy, dev, idx, dst, next_hop,
				    pinfo);
	trace_rdev_return_int_mpath_info(&rdev->wiphy, ret, pinfo);
	return ret;
}

static inline int rdev_dump_mpp(struct cfg80211_registered_device *rdev,
				struct net_device *dev, int idx, u8 *dst,
				u8 *mpp, struct mpath_info *pinfo)

{
	int ret;

	trace_rdev_dump_mpp(&rdev->wiphy, dev, idx, dst, mpp);
	ret = rdev->ops->dump_mpp(&rdev->wiphy, dev, idx, dst, mpp, pinfo);
	trace_rdev_return_int_mpath_info(&rdev->wiphy, ret, pinfo);
	return ret;
}

static inline int
rdev_get_mesh_config(struct cfg80211_registered_device *rdev,
		     struct net_device *dev, struct mesh_config *conf)
{
	int ret;
	trace_rdev_get_mesh_config(&rdev->wiphy, dev);
	ret = rdev->ops->get_mesh_config(&rdev->wiphy, dev, conf);
	trace_rdev_return_int_mesh_config(&rdev->wiphy, ret, conf);
	return ret;
}

static inline int
rdev_update_mesh_config(struct cfg80211_registered_device *rdev,
			struct net_device *dev, u32 mask,
			const struct mesh_config *nconf)
{
	int ret;
	trace_rdev_update_mesh_config(&rdev->wiphy, dev, mask, nconf);
	ret = rdev->ops->update_mesh_config(&rdev->wiphy, dev, mask, nconf);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_join_mesh(struct cfg80211_registered_device *rdev,
				 struct net_device *dev,
				 const struct mesh_config *conf,
				 const struct mesh_setup *setup)
{
	int ret;
	trace_rdev_join_mesh(&rdev->wiphy, dev, conf, setup);
	ret = rdev->ops->join_mesh(&rdev->wiphy, dev, conf, setup);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}


static inline int rdev_leave_mesh(struct cfg80211_registered_device *rdev,
				  struct net_device *dev)
{
	int ret;
	trace_rdev_leave_mesh(&rdev->wiphy, dev);
	ret = rdev->ops->leave_mesh(&rdev->wiphy, dev);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_join_ocb(struct cfg80211_registered_device *rdev,
				struct net_device *dev,
				struct ocb_setup *setup)
{
	int ret;
	trace_rdev_join_ocb(&rdev->wiphy, dev, setup);
	ret = rdev->ops->join_ocb(&rdev->wiphy, dev, setup);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_leave_ocb(struct cfg80211_registered_device *rdev,
				 struct net_device *dev)
{
	int ret;
	trace_rdev_leave_ocb(&rdev->wiphy, dev);
	ret = rdev->ops->leave_ocb(&rdev->wiphy, dev);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_change_bss(struct cfg80211_registered_device *rdev,
				  struct net_device *dev,
				  struct bss_parameters *params)

{
	int ret;
	trace_rdev_change_bss(&rdev->wiphy, dev, params);
	ret = rdev->ops->change_bss(&rdev->wiphy, dev, params);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_set_txq_params(struct cfg80211_registered_device *rdev,
				      struct net_device *dev,
				      struct ieee80211_txq_params *params)

{
	int ret;
	trace_rdev_set_txq_params(&rdev->wiphy, dev, params);
	ret = rdev->ops->set_txq_params(&rdev->wiphy, dev, params);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_libertas_set_mesh_channel(struct cfg80211_registered_device *rdev,
			       struct net_device *dev,
			       struct ieee80211_channel *chan)
{
	int ret;
	trace_rdev_libertas_set_mesh_channel(&rdev->wiphy, dev, chan);
	ret = rdev->ops->libertas_set_mesh_channel(&rdev->wiphy, dev, chan);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_set_monitor_channel(struct cfg80211_registered_device *rdev,
			 struct cfg80211_chan_def *chandef)
{
	int ret;
	trace_rdev_set_monitor_channel(&rdev->wiphy, chandef);
	ret = rdev->ops->set_monitor_channel(&rdev->wiphy, chandef);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_scan(struct cfg80211_registered_device *rdev,
			    struct cfg80211_scan_request *request)
{
	int ret;
	trace_rdev_scan(&rdev->wiphy, request);
	ret = rdev->ops->scan(&rdev->wiphy, request);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline void rdev_abort_scan(struct cfg80211_registered_device *rdev,
				   struct wireless_dev *wdev)
{
	trace_rdev_abort_scan(&rdev->wiphy, wdev);
	rdev->ops->abort_scan(&rdev->wiphy, wdev);
	trace_rdev_return_void(&rdev->wiphy);
}

static inline int rdev_auth(struct cfg80211_registered_device *rdev,
			    struct net_device *dev,
			    struct cfg80211_auth_request *req)
{
	int ret;
	trace_rdev_auth(&rdev->wiphy, dev, req);
	ret = rdev->ops->auth(&rdev->wiphy, dev, req);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_assoc(struct cfg80211_registered_device *rdev,
			     struct net_device *dev,
			     struct cfg80211_assoc_request *req)
{
	const struct cfg80211_bss_ies *bss_ies;
	int ret;

	/*
	 * Note: we might trace not exactly the data that's processed,
	 * due to races and the driver/mac80211 getting a newer copy.
	 */
	rcu_read_lock();
	bss_ies = rcu_dereference(req->bss->ies);
	trace_rdev_assoc(&rdev->wiphy, dev, req, bss_ies);
	rcu_read_unlock();

	ret = rdev->ops->assoc(&rdev->wiphy, dev, req);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_deauth(struct cfg80211_registered_device *rdev,
			      struct net_device *dev,
			      struct cfg80211_deauth_request *req)
{
	int ret;
	trace_rdev_deauth(&rdev->wiphy, dev, req);
	ret = rdev->ops->deauth(&rdev->wiphy, dev, req);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_disassoc(struct cfg80211_registered_device *rdev,
				struct net_device *dev,
				struct cfg80211_disassoc_request *req)
{
	int ret;
	trace_rdev_disassoc(&rdev->wiphy, dev, req);
	ret = rdev->ops->disassoc(&rdev->wiphy, dev, req);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_connect(struct cfg80211_registered_device *rdev,
			       struct net_device *dev,
			       struct cfg80211_connect_params *sme)
{
	int ret;
	trace_rdev_connect(&rdev->wiphy, dev, sme);
	ret = rdev->ops->connect(&rdev->wiphy, dev, sme);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_update_connect_params(struct cfg80211_registered_device *rdev,
			   struct net_device *dev,
			   struct cfg80211_connect_params *sme, u32 changed)
{
	int ret;
	trace_rdev_update_connect_params(&rdev->wiphy, dev, sme, changed);
	ret = rdev->ops->update_connect_params(&rdev->wiphy, dev, sme, changed);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_disconnect(struct cfg80211_registered_device *rdev,
				  struct net_device *dev, u16 reason_code)
{
	int ret;
	trace_rdev_disconnect(&rdev->wiphy, dev, reason_code);
	ret = rdev->ops->disconnect(&rdev->wiphy, dev, reason_code);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_join_ibss(struct cfg80211_registered_device *rdev,
				 struct net_device *dev,
				 struct cfg80211_ibss_params *params)
{
	int ret;
	trace_rdev_join_ibss(&rdev->wiphy, dev, params);
	ret = rdev->ops->join_ibss(&rdev->wiphy, dev, params);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_leave_ibss(struct cfg80211_registered_device *rdev,
				  struct net_device *dev)
{
	int ret;
	trace_rdev_leave_ibss(&rdev->wiphy, dev);
	ret = rdev->ops->leave_ibss(&rdev->wiphy, dev);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_set_wiphy_params(struct cfg80211_registered_device *rdev, u32 changed)
{
	int ret;

	if (!rdev->ops->set_wiphy_params)
		return -EOPNOTSUPP;

	trace_rdev_set_wiphy_params(&rdev->wiphy, changed);
	ret = rdev->ops->set_wiphy_params(&rdev->wiphy, changed);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_set_tx_power(struct cfg80211_registered_device *rdev,
				    struct wireless_dev *wdev,
				    enum nl80211_tx_power_setting type, int mbm)
{
	int ret;
	trace_rdev_set_tx_power(&rdev->wiphy, wdev, type, mbm);
	ret = rdev->ops->set_tx_power(&rdev->wiphy, wdev, type, mbm);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_get_tx_power(struct cfg80211_registered_device *rdev,
				    struct wireless_dev *wdev, int *dbm)
{
	int ret;
	trace_rdev_get_tx_power(&rdev->wiphy, wdev);
	ret = rdev->ops->get_tx_power(&rdev->wiphy, wdev, dbm);
	trace_rdev_return_int_int(&rdev->wiphy, ret, *dbm);
	return ret;
}

static inline int
rdev_set_multicast_to_unicast(struct cfg80211_registered_device *rdev,
			      struct net_device *dev,
			      const bool enabled)
{
	int ret;
	trace_rdev_set_multicast_to_unicast(&rdev->wiphy, dev, enabled);
	ret = rdev->ops->set_multicast_to_unicast(&rdev->wiphy, dev, enabled);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_get_txq_stats(struct cfg80211_registered_device *rdev,
		   struct wireless_dev *wdev,
		   struct cfg80211_txq_stats *txqstats)
{
	int ret;
	trace_rdev_get_txq_stats(&rdev->wiphy, wdev);
	ret = rdev->ops->get_txq_stats(&rdev->wiphy, wdev, txqstats);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline void rdev_rfkill_poll(struct cfg80211_registered_device *rdev)
{
	trace_rdev_rfkill_poll(&rdev->wiphy);
	rdev->ops->rfkill_poll(&rdev->wiphy);
	trace_rdev_return_void(&rdev->wiphy);
}


#ifdef CONFIG_NL80211_TESTMODE
static inline int rdev_testmode_cmd(struct cfg80211_registered_device *rdev,
				    struct wireless_dev *wdev,
				    void *data, int len)
{
	int ret;
	trace_rdev_testmode_cmd(&rdev->wiphy, wdev);
	ret = rdev->ops->testmode_cmd(&rdev->wiphy, wdev, data, len);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_testmode_dump(struct cfg80211_registered_device *rdev,
				     struct sk_buff *skb,
				     struct netlink_callback *cb, void *data,
				     int len)
{
	int ret;
	trace_rdev_testmode_dump(&rdev->wiphy);
	ret = rdev->ops->testmode_dump(&rdev->wiphy, skb, cb, data, len);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}
#endif

static inline int
rdev_set_bitrate_mask(struct cfg80211_registered_device *rdev,
		      struct net_device *dev, const u8 *peer,
		      const struct cfg80211_bitrate_mask *mask)
{
	int ret;
	trace_rdev_set_bitrate_mask(&rdev->wiphy, dev, peer, mask);
	ret = rdev->ops->set_bitrate_mask(&rdev->wiphy, dev, peer, mask);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_dump_survey(struct cfg80211_registered_device *rdev,
				   struct net_device *netdev, int idx,
				   struct survey_info *info)
{
	int ret;
	trace_rdev_dump_survey(&rdev->wiphy, netdev, idx);
	ret = rdev->ops->dump_survey(&rdev->wiphy, netdev, idx, info);
	if (ret < 0)
		trace_rdev_return_int(&rdev->wiphy, ret);
	else
		trace_rdev_return_int_survey_info(&rdev->wiphy, ret, info);
	return ret;
}

static inline int rdev_set_pmksa(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev,
				 struct cfg80211_pmksa *pmksa)
{
	int ret;
	trace_rdev_set_pmksa(&rdev->wiphy, netdev, pmksa);
	ret = rdev->ops->set_pmksa(&rdev->wiphy, netdev, pmksa);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_del_pmksa(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev,
				 struct cfg80211_pmksa *pmksa)
{
	int ret;
	trace_rdev_del_pmksa(&rdev->wiphy, netdev, pmksa);
	ret = rdev->ops->del_pmksa(&rdev->wiphy, netdev, pmksa);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_flush_pmksa(struct cfg80211_registered_device *rdev,
				   struct net_device *netdev)
{
	int ret;
	trace_rdev_flush_pmksa(&rdev->wiphy, netdev);
	ret = rdev->ops->flush_pmksa(&rdev->wiphy, netdev);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_remain_on_channel(struct cfg80211_registered_device *rdev,
		       struct wireless_dev *wdev,
		       struct ieee80211_channel *chan,
		       unsigned int duration, u64 *cookie)
{
	int ret;
	trace_rdev_remain_on_channel(&rdev->wiphy, wdev, chan, duration);
	ret = rdev->ops->remain_on_channel(&rdev->wiphy, wdev, chan,
					   duration, cookie);
	trace_rdev_return_int_cookie(&rdev->wiphy, ret, *cookie);
	return ret;
}

static inline int
rdev_cancel_remain_on_channel(struct cfg80211_registered_device *rdev,
			      struct wireless_dev *wdev, u64 cookie)
{
	int ret;
	trace_rdev_cancel_remain_on_channel(&rdev->wiphy, wdev, cookie);
	ret = rdev->ops->cancel_remain_on_channel(&rdev->wiphy, wdev, cookie);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_mgmt_tx(struct cfg80211_registered_device *rdev,
			       struct wireless_dev *wdev,
			       struct cfg80211_mgmt_tx_params *params,
			       u64 *cookie)
{
	int ret;
	trace_rdev_mgmt_tx(&rdev->wiphy, wdev, params);
	ret = rdev->ops->mgmt_tx(&rdev->wiphy, wdev, params, cookie);
	trace_rdev_return_int_cookie(&rdev->wiphy, ret, *cookie);
	return ret;
}

static inline int rdev_tx_control_port(struct cfg80211_registered_device *rdev,
				       struct net_device *dev,
				       const void *buf, size_t len,
				       const u8 *dest, __be16 proto,
				       const bool noencrypt, u64 *cookie)
{
	int ret;
	trace_rdev_tx_control_port(&rdev->wiphy, dev, buf, len,
				   dest, proto, noencrypt);
	ret = rdev->ops->tx_control_port(&rdev->wiphy, dev, buf, len,
					 dest, proto, noencrypt, cookie);
	if (cookie)
		trace_rdev_return_int_cookie(&rdev->wiphy, ret, *cookie);
	else
		trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_mgmt_tx_cancel_wait(struct cfg80211_registered_device *rdev,
			 struct wireless_dev *wdev, u64 cookie)
{
	int ret;
	trace_rdev_mgmt_tx_cancel_wait(&rdev->wiphy, wdev, cookie);
	ret = rdev->ops->mgmt_tx_cancel_wait(&rdev->wiphy, wdev, cookie);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_set_power_mgmt(struct cfg80211_registered_device *rdev,
				      struct net_device *dev, bool enabled,
				      int timeout)
{
	int ret;
	trace_rdev_set_power_mgmt(&rdev->wiphy, dev, enabled, timeout);
	ret = rdev->ops->set_power_mgmt(&rdev->wiphy, dev, enabled, timeout);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_set_cqm_rssi_config(struct cfg80211_registered_device *rdev,
			 struct net_device *dev, s32 rssi_thold, u32 rssi_hyst)
{
	int ret;
	trace_rdev_set_cqm_rssi_config(&rdev->wiphy, dev, rssi_thold,
				       rssi_hyst);
	ret = rdev->ops->set_cqm_rssi_config(&rdev->wiphy, dev, rssi_thold,
				       rssi_hyst);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_set_cqm_rssi_range_config(struct cfg80211_registered_device *rdev,
			       struct net_device *dev, s32 low, s32 high)
{
	int ret;
	trace_rdev_set_cqm_rssi_range_config(&rdev->wiphy, dev, low, high);
	ret = rdev->ops->set_cqm_rssi_range_config(&rdev->wiphy, dev,
						   low, high);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_set_cqm_txe_config(struct cfg80211_registered_device *rdev,
			struct net_device *dev, u32 rate, u32 pkts, u32 intvl)
{
	int ret;
	trace_rdev_set_cqm_txe_config(&rdev->wiphy, dev, rate, pkts, intvl);
	ret = rdev->ops->set_cqm_txe_config(&rdev->wiphy, dev, rate, pkts,
					     intvl);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline void
rdev_update_mgmt_frame_registrations(struct cfg80211_registered_device *rdev,
				     struct wireless_dev *wdev,
				     struct mgmt_frame_regs *upd)
{
	might_sleep();

	trace_rdev_update_mgmt_frame_registrations(&rdev->wiphy, wdev, upd);
	if (rdev->ops->update_mgmt_frame_registrations)
		rdev->ops->update_mgmt_frame_registrations(&rdev->wiphy, wdev,
							   upd);
	trace_rdev_return_void(&rdev->wiphy);
}

static inline int rdev_set_antenna(struct cfg80211_registered_device *rdev,
				   u32 tx_ant, u32 rx_ant)
{
	int ret;
	trace_rdev_set_antenna(&rdev->wiphy, tx_ant, rx_ant);
	ret = rdev->ops->set_antenna(&rdev->wiphy, tx_ant, rx_ant);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_get_antenna(struct cfg80211_registered_device *rdev,
				   u32 *tx_ant, u32 *rx_ant)
{
	int ret;
	trace_rdev_get_antenna(&rdev->wiphy);
	ret = rdev->ops->get_antenna(&rdev->wiphy, tx_ant, rx_ant);
	if (ret)
		trace_rdev_return_int(&rdev->wiphy, ret);
	else
		trace_rdev_return_int_tx_rx(&rdev->wiphy, ret, *tx_ant,
					    *rx_ant);
	return ret;
}

static inline int
rdev_sched_scan_start(struct cfg80211_registered_device *rdev,
		      struct net_device *dev,
		      struct cfg80211_sched_scan_request *request)
{
	int ret;
	trace_rdev_sched_scan_start(&rdev->wiphy, dev, request->reqid);
	ret = rdev->ops->sched_scan_start(&rdev->wiphy, dev, request);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_sched_scan_stop(struct cfg80211_registered_device *rdev,
				       struct net_device *dev, u64 reqid)
{
	int ret;
	trace_rdev_sched_scan_stop(&rdev->wiphy, dev, reqid);
	ret = rdev->ops->sched_scan_stop(&rdev->wiphy, dev, reqid);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_set_rekey_data(struct cfg80211_registered_device *rdev,
				      struct net_device *dev,
				      struct cfg80211_gtk_rekey_data *data)
{
	int ret;
	trace_rdev_set_rekey_data(&rdev->wiphy, dev);
	ret = rdev->ops->set_rekey_data(&rdev->wiphy, dev, data);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_tdls_mgmt(struct cfg80211_registered_device *rdev,
				 struct net_device *dev, u8 *peer,
				 u8 action_code, u8 dialog_token,
				 u16 status_code, u32 peer_capability,
				 bool initiator, const u8 *buf, size_t len)
{
	int ret;
	trace_rdev_tdls_mgmt(&rdev->wiphy, dev, peer, action_code,
			     dialog_token, status_code, peer_capability,
			     initiator, buf, len);
	ret = rdev->ops->tdls_mgmt(&rdev->wiphy, dev, peer, action_code,
				   dialog_token, status_code, peer_capability,
				   initiator, buf, len);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_tdls_oper(struct cfg80211_registered_device *rdev,
				 struct net_device *dev, u8 *peer,
				 enum nl80211_tdls_operation oper)
{
	int ret;
	trace_rdev_tdls_oper(&rdev->wiphy, dev, peer, oper);
	ret = rdev->ops->tdls_oper(&rdev->wiphy, dev, peer, oper);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_probe_client(struct cfg80211_registered_device *rdev,
				    struct net_device *dev, const u8 *peer,
				    u64 *cookie)
{
	int ret;
	trace_rdev_probe_client(&rdev->wiphy, dev, peer);
	ret = rdev->ops->probe_client(&rdev->wiphy, dev, peer, cookie);
	trace_rdev_return_int_cookie(&rdev->wiphy, ret, *cookie);
	return ret;
}

static inline int rdev_set_noack_map(struct cfg80211_registered_device *rdev,
				     struct net_device *dev, u16 noack_map)
{
	int ret;
	trace_rdev_set_noack_map(&rdev->wiphy, dev, noack_map);
	ret = rdev->ops->set_noack_map(&rdev->wiphy, dev, noack_map);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_get_channel(struct cfg80211_registered_device *rdev,
		 struct wireless_dev *wdev,
		 struct cfg80211_chan_def *chandef)
{
	int ret;

	trace_rdev_get_channel(&rdev->wiphy, wdev);
	ret = rdev->ops->get_channel(&rdev->wiphy, wdev, chandef);
	trace_rdev_return_chandef(&rdev->wiphy, ret, chandef);

	return ret;
}

static inline int rdev_start_p2p_device(struct cfg80211_registered_device *rdev,
					struct wireless_dev *wdev)
{
	int ret;

	trace_rdev_start_p2p_device(&rdev->wiphy, wdev);
	ret = rdev->ops->start_p2p_device(&rdev->wiphy, wdev);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline void rdev_stop_p2p_device(struct cfg80211_registered_device *rdev,
					struct wireless_dev *wdev)
{
	trace_rdev_stop_p2p_device(&rdev->wiphy, wdev);
	rdev->ops->stop_p2p_device(&rdev->wiphy, wdev);
	trace_rdev_return_void(&rdev->wiphy);
}

static inline int rdev_start_nan(struct cfg80211_registered_device *rdev,
				 struct wireless_dev *wdev,
				 struct cfg80211_nan_conf *conf)
{
	int ret;

	trace_rdev_start_nan(&rdev->wiphy, wdev, conf);
	ret = rdev->ops->start_nan(&rdev->wiphy, wdev, conf);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline void rdev_stop_nan(struct cfg80211_registered_device *rdev,
				 struct wireless_dev *wdev)
{
	trace_rdev_stop_nan(&rdev->wiphy, wdev);
	rdev->ops->stop_nan(&rdev->wiphy, wdev);
	trace_rdev_return_void(&rdev->wiphy);
}

static inline int
rdev_add_nan_func(struct cfg80211_registered_device *rdev,
		  struct wireless_dev *wdev,
		  struct cfg80211_nan_func *nan_func)
{
	int ret;

	trace_rdev_add_nan_func(&rdev->wiphy, wdev, nan_func);
	ret = rdev->ops->add_nan_func(&rdev->wiphy, wdev, nan_func);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline void rdev_del_nan_func(struct cfg80211_registered_device *rdev,
				    struct wireless_dev *wdev, u64 cookie)
{
	trace_rdev_del_nan_func(&rdev->wiphy, wdev, cookie);
	rdev->ops->del_nan_func(&rdev->wiphy, wdev, cookie);
	trace_rdev_return_void(&rdev->wiphy);
}

static inline int
rdev_nan_change_conf(struct cfg80211_registered_device *rdev,
		     struct wireless_dev *wdev,
		     struct cfg80211_nan_conf *conf, u32 changes)
{
	int ret;

	trace_rdev_nan_change_conf(&rdev->wiphy, wdev, conf, changes);
	if (rdev->ops->nan_change_conf)
		ret = rdev->ops->nan_change_conf(&rdev->wiphy, wdev, conf,
						 changes);
	else
		ret = -ENOTSUPP;
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_set_mac_acl(struct cfg80211_registered_device *rdev,
				   struct net_device *dev,
				   struct cfg80211_acl_data *params)
{
	int ret;

	trace_rdev_set_mac_acl(&rdev->wiphy, dev, params);
	ret = rdev->ops->set_mac_acl(&rdev->wiphy, dev, params);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_update_ft_ies(struct cfg80211_registered_device *rdev,
				     struct net_device *dev,
				     struct cfg80211_update_ft_ies_params *ftie)
{
	int ret;

	trace_rdev_update_ft_ies(&rdev->wiphy, dev, ftie);
	ret = rdev->ops->update_ft_ies(&rdev->wiphy, dev, ftie);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_crit_proto_start(struct cfg80211_registered_device *rdev,
					struct wireless_dev *wdev,
					enum nl80211_crit_proto_id protocol,
					u16 duration)
{
	int ret;

	trace_rdev_crit_proto_start(&rdev->wiphy, wdev, protocol, duration);
	ret = rdev->ops->crit_proto_start(&rdev->wiphy, wdev,
					  protocol, duration);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline void rdev_crit_proto_stop(struct cfg80211_registered_device *rdev,
				       struct wireless_dev *wdev)
{
	trace_rdev_crit_proto_stop(&rdev->wiphy, wdev);
	rdev->ops->crit_proto_stop(&rdev->wiphy, wdev);
	trace_rdev_return_void(&rdev->wiphy);
}

static inline int rdev_channel_switch(struct cfg80211_registered_device *rdev,
				      struct net_device *dev,
				      struct cfg80211_csa_settings *params)
{
	int ret;

	trace_rdev_channel_switch(&rdev->wiphy, dev, params);
	ret = rdev->ops->channel_switch(&rdev->wiphy, dev, params);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_set_qos_map(struct cfg80211_registered_device *rdev,
				   struct net_device *dev,
				   struct cfg80211_qos_map *qos_map)
{
	int ret = -EOPNOTSUPP;

	if (rdev->ops->set_qos_map) {
		trace_rdev_set_qos_map(&rdev->wiphy, dev, qos_map);
		ret = rdev->ops->set_qos_map(&rdev->wiphy, dev, qos_map);
		trace_rdev_return_int(&rdev->wiphy, ret);
	}

	return ret;
}

static inline int
rdev_set_ap_chanwidth(struct cfg80211_registered_device *rdev,
		      struct net_device *dev, struct cfg80211_chan_def *chandef)
{
	int ret;

	trace_rdev_set_ap_chanwidth(&rdev->wiphy, dev, chandef);
	ret = rdev->ops->set_ap_chanwidth(&rdev->wiphy, dev, chandef);
	trace_rdev_return_int(&rdev->wiphy, ret);

	return ret;
}

static inline int
rdev_add_tx_ts(struct cfg80211_registered_device *rdev,
	       struct net_device *dev, u8 tsid, const u8 *peer,
	       u8 user_prio, u16 admitted_time)
{
	int ret = -EOPNOTSUPP;

	trace_rdev_add_tx_ts(&rdev->wiphy, dev, tsid, peer,
			     user_prio, admitted_time);
	if (rdev->ops->add_tx_ts)
		ret = rdev->ops->add_tx_ts(&rdev->wiphy, dev, tsid, peer,
					   user_prio, admitted_time);
	trace_rdev_return_int(&rdev->wiphy, ret);

	return ret;
}

static inline int
rdev_del_tx_ts(struct cfg80211_registered_device *rdev,
	       struct net_device *dev, u8 tsid, const u8 *peer)
{
	int ret = -EOPNOTSUPP;

	trace_rdev_del_tx_ts(&rdev->wiphy, dev, tsid, peer);
	if (rdev->ops->del_tx_ts)
		ret = rdev->ops->del_tx_ts(&rdev->wiphy, dev, tsid, peer);
	trace_rdev_return_int(&rdev->wiphy, ret);

	return ret;
}

static inline int
rdev_tdls_channel_switch(struct cfg80211_registered_device *rdev,
			 struct net_device *dev, const u8 *addr,
			 u8 oper_class, struct cfg80211_chan_def *chandef)
{
	int ret;

	trace_rdev_tdls_channel_switch(&rdev->wiphy, dev, addr, oper_class,
				       chandef);
	ret = rdev->ops->tdls_channel_switch(&rdev->wiphy, dev, addr,
					     oper_class, chandef);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline void
rdev_tdls_cancel_channel_switch(struct cfg80211_registered_device *rdev,
				struct net_device *dev, const u8 *addr)
{
	trace_rdev_tdls_cancel_channel_switch(&rdev->wiphy, dev, addr);
	rdev->ops->tdls_cancel_channel_switch(&rdev->wiphy, dev, addr);
	trace_rdev_return_void(&rdev->wiphy);
}

static inline int
rdev_start_radar_detection(struct cfg80211_registered_device *rdev,
			   struct net_device *dev,
			   struct cfg80211_chan_def *chandef,
			   u32 cac_time_ms)
{
	int ret = -ENOTSUPP;

	trace_rdev_start_radar_detection(&rdev->wiphy, dev, chandef,
					 cac_time_ms);
	if (rdev->ops->start_radar_detection)
		ret = rdev->ops->start_radar_detection(&rdev->wiphy, dev,
						       chandef, cac_time_ms);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline void
rdev_end_cac(struct cfg80211_registered_device *rdev,
	     struct net_device *dev)
{
	trace_rdev_end_cac(&rdev->wiphy, dev);
	if (rdev->ops->end_cac)
		rdev->ops->end_cac(&rdev->wiphy, dev);
	trace_rdev_return_void(&rdev->wiphy);
}

static inline int
rdev_set_mcast_rate(struct cfg80211_registered_device *rdev,
		    struct net_device *dev,
		    int mcast_rate[NUM_NL80211_BANDS])
{
	int ret = -ENOTSUPP;

	trace_rdev_set_mcast_rate(&rdev->wiphy, dev, mcast_rate);
	if (rdev->ops->set_mcast_rate)
		ret = rdev->ops->set_mcast_rate(&rdev->wiphy, dev, mcast_rate);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_set_coalesce(struct cfg80211_registered_device *rdev,
		  struct cfg80211_coalesce *coalesce)
{
	int ret = -ENOTSUPP;

	trace_rdev_set_coalesce(&rdev->wiphy, coalesce);
	if (rdev->ops->set_coalesce)
		ret = rdev->ops->set_coalesce(&rdev->wiphy, coalesce);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_set_pmk(struct cfg80211_registered_device *rdev,
			       struct net_device *dev,
			       struct cfg80211_pmk_conf *pmk_conf)
{
	int ret = -EOPNOTSUPP;

	trace_rdev_set_pmk(&rdev->wiphy, dev, pmk_conf);
	if (rdev->ops->set_pmk)
		ret = rdev->ops->set_pmk(&rdev->wiphy, dev, pmk_conf);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_del_pmk(struct cfg80211_registered_device *rdev,
			       struct net_device *dev, const u8 *aa)
{
	int ret = -EOPNOTSUPP;

	trace_rdev_del_pmk(&rdev->wiphy, dev, aa);
	if (rdev->ops->del_pmk)
		ret = rdev->ops->del_pmk(&rdev->wiphy, dev, aa);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_external_auth(struct cfg80211_registered_device *rdev,
		   struct net_device *dev,
		   struct cfg80211_external_auth_params *params)
{
	int ret = -EOPNOTSUPP;

	trace_rdev_external_auth(&rdev->wiphy, dev, params);
	if (rdev->ops->external_auth)
		ret = rdev->ops->external_auth(&rdev->wiphy, dev, params);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_get_ftm_responder_stats(struct cfg80211_registered_device *rdev,
			     struct net_device *dev,
			     struct cfg80211_ftm_responder_stats *ftm_stats)
{
	int ret = -EOPNOTSUPP;

	trace_rdev_get_ftm_responder_stats(&rdev->wiphy, dev, ftm_stats);
	if (rdev->ops->get_ftm_responder_stats)
		ret = rdev->ops->get_ftm_responder_stats(&rdev->wiphy, dev,
							ftm_stats);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_start_pmsr(struct cfg80211_registered_device *rdev,
		struct wireless_dev *wdev,
		struct cfg80211_pmsr_request *request)
{
	int ret = -EOPNOTSUPP;

	trace_rdev_start_pmsr(&rdev->wiphy, wdev, request->cookie);
	if (rdev->ops->start_pmsr)
		ret = rdev->ops->start_pmsr(&rdev->wiphy, wdev, request);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline void
rdev_abort_pmsr(struct cfg80211_registered_device *rdev,
		struct wireless_dev *wdev,
		struct cfg80211_pmsr_request *request)
{
	trace_rdev_abort_pmsr(&rdev->wiphy, wdev, request->cookie);
	if (rdev->ops->abort_pmsr)
		rdev->ops->abort_pmsr(&rdev->wiphy, wdev, request);
	trace_rdev_return_void(&rdev->wiphy);
}

static inline int rdev_update_owe_info(struct cfg80211_registered_device *rdev,
				       struct net_device *dev,
				       struct cfg80211_update_owe_info *oweinfo)
{
	int ret = -EOPNOTSUPP;

	trace_rdev_update_owe_info(&rdev->wiphy, dev, oweinfo);
	if (rdev->ops->update_owe_info)
		ret = rdev->ops->update_owe_info(&rdev->wiphy, dev, oweinfo);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int
rdev_probe_mesh_link(struct cfg80211_registered_device *rdev,
		     struct net_device *dev, const u8 *dest,
		     const void *buf, size_t len)
{
	int ret;

	trace_rdev_probe_mesh_link(&rdev->wiphy, dev, dest, buf, len);
	ret = rdev->ops->probe_mesh_link(&rdev->wiphy, dev, buf, len);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_set_tid_config(struct cfg80211_registered_device *rdev,
				      struct net_device *dev,
				      struct cfg80211_tid_config *tid_conf)
{
	int ret;

	trace_rdev_set_tid_config(&rdev->wiphy, dev, tid_conf);
	ret = rdev->ops->set_tid_config(&rdev->wiphy, dev, tid_conf);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_reset_tid_config(struct cfg80211_registered_device *rdev,
					struct net_device *dev, const u8 *peer,
					u8 tids)
{
	int ret;

	trace_rdev_reset_tid_config(&rdev->wiphy, dev, peer, tids);
	ret = rdev->ops->reset_tid_config(&rdev->wiphy, dev, peer, tids);
	trace_rdev_return_int(&rdev->wiphy, ret);
	return ret;
}

static inline int rdev_set_sar_specs(struct cfg80211_registered_device *rdev,
				     struct cfg80211_sar_specs *sar)
{
	int ret;

	trace_rdev_set_sar_specs(&rdev->wiphy, sar);
	ret = rdev->ops->set_sar_specs(&rdev->wiphy, sar);
	trace_rdev_return_int(&rdev->wiphy, ret);

	return ret;
}

#endif /* __CFG80211_RDEV_OPS */
