#ifndef __CFG80211_RDEV_OPS
#define __CFG80211_RDEV_OPS

#include <linux/rtnetlink.h>
#include <net/cfg80211.h>
#include "core.h"

static inline int rdev_suspend(struct cfg80211_registered_device *rdev)
{
	return rdev->ops->suspend(&rdev->wiphy, rdev->wowlan);
}

static inline int rdev_resume(struct cfg80211_registered_device *rdev)
{
	return rdev->ops->resume(&rdev->wiphy);
}

static inline void rdev_set_wakeup(struct cfg80211_registered_device *rdev,
				   bool enabled)
{
	rdev->ops->set_wakeup(&rdev->wiphy, enabled);
}

static inline struct wireless_dev
*rdev_add_virtual_intf(struct cfg80211_registered_device *rdev, char *name,
		       enum nl80211_iftype type, u32 *flags,
		       struct vif_params *params)
{
	return rdev->ops->add_virtual_intf(&rdev->wiphy, name, type, flags,
					   params);
}

static inline int
rdev_del_virtual_intf(struct cfg80211_registered_device *rdev,
		      struct wireless_dev *wdev)
{
	return rdev->ops->del_virtual_intf(&rdev->wiphy, wdev);
}

static inline int
rdev_change_virtual_intf(struct cfg80211_registered_device *rdev,
			 struct net_device *dev, enum nl80211_iftype type,
			 u32 *flags, struct vif_params *params)
{
	return rdev->ops->change_virtual_intf(&rdev->wiphy, dev, type, flags,
					      params);
}

static inline int rdev_add_key(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, u8 key_index,
			       bool pairwise, const u8 *mac_addr,
			       struct key_params *params)
{
	return rdev->ops->add_key(&rdev->wiphy, netdev, key_index, pairwise,
				  mac_addr, params);
}

static inline int
rdev_get_key(struct cfg80211_registered_device *rdev, struct net_device *netdev,
	     u8 key_index, bool pairwise, const u8 *mac_addr, void *cookie,
	     void (*callback)(void *cookie, struct key_params*))
{
	return rdev->ops->get_key(&rdev->wiphy, netdev, key_index, pairwise,
				  mac_addr, cookie, callback);
}

static inline int rdev_del_key(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, u8 key_index,
			       bool pairwise, const u8 *mac_addr)
{
	return rdev->ops->del_key(&rdev->wiphy, netdev, key_index, pairwise,
				  mac_addr);
}

static inline int
rdev_set_default_key(struct cfg80211_registered_device *rdev,
		     struct net_device *netdev, u8 key_index, bool unicast,
		     bool multicast)
{
	return rdev->ops->set_default_key(&rdev->wiphy, netdev, key_index,
					  unicast, multicast);
}

static inline int
rdev_set_default_mgmt_key(struct cfg80211_registered_device *rdev,
			  struct net_device *netdev, u8 key_index)
{
	return rdev->ops->set_default_mgmt_key(&rdev->wiphy, netdev,
					       key_index);
}

static inline int rdev_start_ap(struct cfg80211_registered_device *rdev,
				struct net_device *dev,
				struct cfg80211_ap_settings *settings)
{
	return rdev->ops->start_ap(&rdev->wiphy, dev, settings);
}

static inline int rdev_change_beacon(struct cfg80211_registered_device *rdev,
				     struct net_device *dev,
				     struct cfg80211_beacon_data *info)
{
	return rdev->ops->change_beacon(&rdev->wiphy, dev, info);
}

static inline int rdev_stop_ap(struct cfg80211_registered_device *rdev,
			       struct net_device *dev)
{
	return rdev->ops->stop_ap(&rdev->wiphy, dev);
}

static inline int rdev_add_station(struct cfg80211_registered_device *rdev,
				   struct net_device *dev, u8 *mac,
				   struct station_parameters *params)
{
	return rdev->ops->add_station(&rdev->wiphy, dev, mac, params);
}

static inline int rdev_del_station(struct cfg80211_registered_device *rdev,
				   struct net_device *dev, u8 *mac)
{
	return rdev->ops->del_station(&rdev->wiphy, dev, mac);
}

static inline int rdev_change_station(struct cfg80211_registered_device *rdev,
				      struct net_device *dev, u8 *mac,
				      struct station_parameters *params)
{
	return rdev->ops->change_station(&rdev->wiphy, dev, mac, params);
}

static inline int rdev_get_station(struct cfg80211_registered_device *rdev,
				   struct net_device *dev, u8 *mac,
				   struct station_info *sinfo)
{
	return rdev->ops->get_station(&rdev->wiphy, dev, mac, sinfo);
}

static inline int rdev_dump_station(struct cfg80211_registered_device *rdev,
				    struct net_device *dev, int idx, u8 *mac,
				    struct station_info *sinfo)
{
	return rdev->ops->dump_station(&rdev->wiphy, dev, idx, mac, sinfo);
}

static inline int rdev_add_mpath(struct cfg80211_registered_device *rdev,
				 struct net_device *dev, u8 *dst, u8 *next_hop)
{
	return rdev->ops->add_mpath(&rdev->wiphy, dev, dst, next_hop);
}

static inline int rdev_del_mpath(struct cfg80211_registered_device *rdev,
				 struct net_device *dev, u8 *dst)
{
	return rdev->ops->del_mpath(&rdev->wiphy, dev, dst);
}

static inline int rdev_change_mpath(struct cfg80211_registered_device *rdev,
				    struct net_device *dev, u8 *dst,
				    u8 *next_hop)
{
	return rdev->ops->change_mpath(&rdev->wiphy, dev, dst, next_hop);
}

static inline int rdev_get_mpath(struct cfg80211_registered_device *rdev,
				 struct net_device *dev, u8 *dst, u8 *next_hop,
				 struct mpath_info *pinfo)
{
	return rdev->ops->get_mpath(&rdev->wiphy, dev, dst, next_hop, pinfo);
}

static inline int rdev_dump_mpath(struct cfg80211_registered_device *rdev,
				  struct net_device *dev, int idx, u8 *dst,
				  u8 *next_hop, struct mpath_info *pinfo)

{
	return rdev->ops->dump_mpath(&rdev->wiphy, dev, idx, dst, next_hop,
				     pinfo);
}

static inline int
rdev_get_mesh_config(struct cfg80211_registered_device *rdev,
		     struct net_device *dev, struct mesh_config *conf)
{
	return rdev->ops->get_mesh_config(&rdev->wiphy, dev, conf);
}

static inline int
rdev_update_mesh_config(struct cfg80211_registered_device *rdev,
			struct net_device *dev, u32 mask,
			const struct mesh_config *nconf)
{
	return rdev->ops->update_mesh_config(&rdev->wiphy, dev, mask, nconf);
}

static inline int rdev_join_mesh(struct cfg80211_registered_device *rdev,
				 struct net_device *dev,
				 const struct mesh_config *conf,
				 const struct mesh_setup *setup)
{
	return rdev->ops->join_mesh(&rdev->wiphy, dev, conf, setup);
}


static inline int rdev_leave_mesh(struct cfg80211_registered_device *rdev,
				  struct net_device *dev)
{
	return rdev->ops->leave_mesh(&rdev->wiphy, dev);
}

static inline int rdev_change_bss(struct cfg80211_registered_device *rdev,
				  struct net_device *dev,
				  struct bss_parameters *params)

{
	return rdev->ops->change_bss(&rdev->wiphy, dev, params);
}

static inline int rdev_set_txq_params(struct cfg80211_registered_device *rdev,
				      struct net_device *dev,
				      struct ieee80211_txq_params *params)

{
	return rdev->ops->set_txq_params(&rdev->wiphy, dev, params);
}

static inline int
rdev_libertas_set_mesh_channel(struct cfg80211_registered_device *rdev,
			       struct net_device *dev,
			       struct ieee80211_channel *chan)
{
	return rdev->ops->libertas_set_mesh_channel(&rdev->wiphy, dev, chan);
}

static inline int
rdev_set_monitor_channel(struct cfg80211_registered_device *rdev,
			 struct ieee80211_channel *chan,
			 enum nl80211_channel_type channel_type)
{
	return rdev->ops->set_monitor_channel(&rdev->wiphy, chan,
					      channel_type);
}

static inline int rdev_scan(struct cfg80211_registered_device *rdev,
			    struct cfg80211_scan_request *request)
{
	return rdev->ops->scan(&rdev->wiphy, request);
}

static inline int rdev_auth(struct cfg80211_registered_device *rdev,
			    struct net_device *dev,
			    struct cfg80211_auth_request *req)
{
	return rdev->ops->auth(&rdev->wiphy, dev, req);
}

static inline int rdev_assoc(struct cfg80211_registered_device *rdev,
			     struct net_device *dev,
			     struct cfg80211_assoc_request *req)
{
	return rdev->ops->assoc(&rdev->wiphy, dev, req);
}

static inline int rdev_deauth(struct cfg80211_registered_device *rdev,
			      struct net_device *dev,
			      struct cfg80211_deauth_request *req)
{
	return rdev->ops->deauth(&rdev->wiphy, dev, req);
}

static inline int rdev_disassoc(struct cfg80211_registered_device *rdev,
				struct net_device *dev,
				struct cfg80211_disassoc_request *req)
{
	return rdev->ops->disassoc(&rdev->wiphy, dev, req);
}

static inline int rdev_connect(struct cfg80211_registered_device *rdev,
			       struct net_device *dev,
			       struct cfg80211_connect_params *sme)
{
	return rdev->ops->connect(&rdev->wiphy, dev, sme);
}

static inline int rdev_disconnect(struct cfg80211_registered_device *rdev,
				  struct net_device *dev, u16 reason_code)
{
	return rdev->ops->disconnect(&rdev->wiphy, dev, reason_code);
}

static inline int rdev_join_ibss(struct cfg80211_registered_device *rdev,
				 struct net_device *dev,
				 struct cfg80211_ibss_params *params)
{
	return rdev->ops->join_ibss(&rdev->wiphy, dev, params);
}

static inline int rdev_leave_ibss(struct cfg80211_registered_device *rdev,
				  struct net_device *dev)
{
	return rdev->ops->leave_ibss(&rdev->wiphy, dev);
}

static inline int
rdev_set_wiphy_params(struct cfg80211_registered_device *rdev, u32 changed)
{
	return rdev->ops->set_wiphy_params(&rdev->wiphy, changed);
}

static inline int rdev_set_tx_power(struct cfg80211_registered_device *rdev,
				    enum nl80211_tx_power_setting type, int mbm)
{
	return rdev->ops->set_tx_power(&rdev->wiphy, type, mbm);
}

static inline int rdev_get_tx_power(struct cfg80211_registered_device *rdev,
				    int *dbm)
{
	return rdev->ops->get_tx_power(&rdev->wiphy, dbm);
}

static inline int rdev_set_wds_peer(struct cfg80211_registered_device *rdev,
				    struct net_device *dev, const u8 *addr)
{
	return rdev->ops->set_wds_peer(&rdev->wiphy, dev, addr);
}

static inline void rdev_rfkill_poll(struct cfg80211_registered_device *rdev)
{
	rdev->ops->rfkill_poll(&rdev->wiphy);
}


#ifdef CONFIG_NL80211_TESTMODE
static inline int rdev_testmode_cmd(struct cfg80211_registered_device *rdev,
				    void *data, int len)
{
	return rdev->ops->testmode_cmd(&rdev->wiphy, data, len);
}

static inline int rdev_testmode_dump(struct cfg80211_registered_device *rdev,
				     struct sk_buff *skb,
				     struct netlink_callback *cb, void *data,
				     int len)
{
	return rdev->ops->testmode_dump(&rdev->wiphy, skb, cb, data,
					len);
}
#endif

static inline int
rdev_set_bitrate_mask(struct cfg80211_registered_device *rdev,
		      struct net_device *dev, const u8 *peer,
		      const struct cfg80211_bitrate_mask *mask)
{
	return rdev->ops->set_bitrate_mask(&rdev->wiphy, dev, peer, mask);
}

static inline int rdev_dump_survey(struct cfg80211_registered_device *rdev,
				   struct net_device *netdev, int idx,
				   struct survey_info *info)
{
	return rdev->ops->dump_survey(&rdev->wiphy, netdev, idx, info);
}

static inline int rdev_set_pmksa(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev,
				 struct cfg80211_pmksa *pmksa)
{
	return rdev->ops->set_pmksa(&rdev->wiphy, netdev, pmksa);
}

static inline int rdev_del_pmksa(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev,
				 struct cfg80211_pmksa *pmksa)
{
	return rdev->ops->del_pmksa(&rdev->wiphy, netdev, pmksa);
}

static inline int rdev_flush_pmksa(struct cfg80211_registered_device *rdev,
				   struct net_device *netdev)
{
	return rdev->ops->flush_pmksa(&rdev->wiphy, netdev);
}

static inline int
rdev_remain_on_channel(struct cfg80211_registered_device *rdev,
		       struct wireless_dev *wdev,
		       struct ieee80211_channel *chan,
		       enum nl80211_channel_type channel_type,
		       unsigned int duration, u64 *cookie)
{
	return rdev->ops->remain_on_channel(&rdev->wiphy, wdev, chan,
					    channel_type, duration, cookie);
}

static inline int
rdev_cancel_remain_on_channel(struct cfg80211_registered_device *rdev,
			      struct wireless_dev *wdev, u64 cookie)
{
	return rdev->ops->cancel_remain_on_channel(&rdev->wiphy, wdev, cookie);
}

static inline int rdev_mgmt_tx(struct cfg80211_registered_device *rdev,
			       struct wireless_dev *wdev,
			       struct ieee80211_channel *chan, bool offchan,
			       enum nl80211_channel_type channel_type,
			       bool channel_type_valid, unsigned int wait,
			       const u8 *buf, size_t len, bool no_cck,
			       bool dont_wait_for_ack, u64 *cookie)
{
	return rdev->ops->mgmt_tx(&rdev->wiphy, wdev, chan, offchan,
				  channel_type, channel_type_valid, wait, buf,
				  len, no_cck, dont_wait_for_ack, cookie);
}

static inline int
rdev_mgmt_tx_cancel_wait(struct cfg80211_registered_device *rdev,
			 struct wireless_dev *wdev, u64 cookie)
{
	return rdev->ops->mgmt_tx_cancel_wait(&rdev->wiphy, wdev, cookie);
}

static inline int rdev_set_power_mgmt(struct cfg80211_registered_device *rdev,
				      struct net_device *dev, bool enabled,
				      int timeout)
{
	return rdev->ops->set_power_mgmt(&rdev->wiphy, dev, enabled, timeout);
}

static inline int
rdev_set_cqm_rssi_config(struct cfg80211_registered_device *rdev,
			 struct net_device *dev, s32 rssi_thold, u32 rssi_hyst)
{
	return rdev->ops->set_cqm_rssi_config(&rdev->wiphy, dev, rssi_thold,
					      rssi_hyst);
}

static inline int
rdev_set_cqm_txe_config(struct cfg80211_registered_device *rdev,
			struct net_device *dev, u32 rate, u32 pkts, u32 intvl)
{
	return rdev->ops->set_cqm_txe_config(&rdev->wiphy, dev, rate, pkts,
					     intvl);
}

static inline void
rdev_mgmt_frame_register(struct cfg80211_registered_device *rdev,
			 struct wireless_dev *wdev, u16 frame_type, bool reg)
{
	rdev->ops->mgmt_frame_register(&rdev->wiphy, wdev , frame_type,
					      reg);
}

static inline int rdev_set_antenna(struct cfg80211_registered_device *rdev,
				   u32 tx_ant, u32 rx_ant)
{
	return rdev->ops->set_antenna(&rdev->wiphy, tx_ant, rx_ant);
}

static inline int rdev_get_antenna(struct cfg80211_registered_device *rdev,
				   u32 *tx_ant, u32 *rx_ant)
{
	return rdev->ops->get_antenna(&rdev->wiphy, tx_ant, rx_ant);
}

static inline int rdev_set_ringparam(struct cfg80211_registered_device *rdev,
				     u32 tx, u32 rx)
{
	return rdev->ops->set_ringparam(&rdev->wiphy, tx, rx);
}

static inline void rdev_get_ringparam(struct cfg80211_registered_device *rdev,
				      u32 *tx, u32 *tx_max, u32 *rx,
				      u32 *rx_max)
{
	rdev->ops->get_ringparam(&rdev->wiphy, tx, tx_max, rx, rx_max);
}

static inline int
rdev_sched_scan_start(struct cfg80211_registered_device *rdev,
		      struct net_device *dev,
		      struct cfg80211_sched_scan_request *request)
{
	return rdev->ops->sched_scan_start(&rdev->wiphy, dev, request);
}

static inline int rdev_sched_scan_stop(struct cfg80211_registered_device *rdev,
				       struct net_device *dev)
{
	return rdev->ops->sched_scan_stop(&rdev->wiphy, dev);
}

static inline int rdev_set_rekey_data(struct cfg80211_registered_device *rdev,
				      struct net_device *dev,
				      struct cfg80211_gtk_rekey_data *data)
{
	return rdev->ops->set_rekey_data(&rdev->wiphy, dev, data);
}

static inline int rdev_tdls_mgmt(struct cfg80211_registered_device *rdev,
				 struct net_device *dev, u8 *peer,
				 u8 action_code, u8 dialog_token,
				 u16 status_code, const u8 *buf, size_t len)
{
	return rdev->ops->tdls_mgmt(&rdev->wiphy, dev, peer, action_code,
				    dialog_token, status_code, buf, len);
}

static inline int rdev_tdls_oper(struct cfg80211_registered_device *rdev,
				 struct net_device *dev, u8 *peer,
				 enum nl80211_tdls_operation oper)
{
	return rdev->ops->tdls_oper(&rdev->wiphy, dev, peer, oper);
}

static inline int rdev_probe_client(struct cfg80211_registered_device *rdev,
				    struct net_device *dev, const u8 *peer,
				    u64 *cookie)
{
	return rdev->ops->probe_client(&rdev->wiphy, dev, peer, cookie);
}

static inline int rdev_set_noack_map(struct cfg80211_registered_device *rdev,
				     struct net_device *dev, u16 noack_map)
{
	return rdev->ops->set_noack_map(&rdev->wiphy, dev, noack_map);
}

static inline int
rdev_get_et_sset_count(struct cfg80211_registered_device *rdev,
		       struct net_device *dev, int sset)
{
	return rdev->ops->get_et_sset_count(&rdev->wiphy, dev, sset);
}

static inline void rdev_get_et_stats(struct cfg80211_registered_device *rdev,
				     struct net_device *dev,
				     struct ethtool_stats *stats, u64 *data)
{
	rdev->ops->get_et_stats(&rdev->wiphy, dev, stats, data);
}

static inline void rdev_get_et_strings(struct cfg80211_registered_device *rdev,
				       struct net_device *dev, u32 sset,
				       u8 *data)
{
	rdev->ops->get_et_strings(&rdev->wiphy, dev, sset, data);
}

static inline struct ieee80211_channel
*rdev_get_channel(struct cfg80211_registered_device *rdev,
		  struct wireless_dev *wdev, enum nl80211_channel_type *type)
{
	return rdev->ops->get_channel(&rdev->wiphy, wdev, type);
}

#endif /* __CFG80211_RDEV_OPS */
