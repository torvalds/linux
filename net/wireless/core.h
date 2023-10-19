/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wireless configuration interface internals.
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright (C) 2018-2022 Intel Corporation
 */
#ifndef __NET_WIRELESS_CORE_H
#define __NET_WIRELESS_CORE_H
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/rbtree.h>
#include <linux/debugfs.h>
#include <linux/rfkill.h>
#include <linux/workqueue.h>
#include <linux/rtnetlink.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include "reg.h"


#define WIPHY_IDX_INVALID	-1

struct cfg80211_registered_device {
	const struct cfg80211_ops *ops;
	struct list_head list;

	/* rfkill support */
	struct rfkill_ops rfkill_ops;
	struct work_struct rfkill_block;

	/* ISO / IEC 3166 alpha2 for which this device is receiving
	 * country IEs on, this can help disregard country IEs from APs
	 * on the same alpha2 quickly. The alpha2 may differ from
	 * cfg80211_regdomain's alpha2 when an intersection has occurred.
	 * If the AP is reconfigured this can also be used to tell us if
	 * the country on the country IE changed. */
	char country_ie_alpha2[2];

	/*
	 * the driver requests the regulatory core to set this regulatory
	 * domain as the wiphy's. Only used for %REGULATORY_WIPHY_SELF_MANAGED
	 * devices using the regulatory_set_wiphy_regd() API
	 */
	const struct ieee80211_regdomain *requested_regd;

	/* If a Country IE has been received this tells us the environment
	 * which its telling us its in. This defaults to ENVIRON_ANY */
	enum environment_cap env;

	/* wiphy index, internal only */
	int wiphy_idx;

	/* protected by RTNL */
	int devlist_generation, wdev_id;
	int opencount;
	wait_queue_head_t dev_wait;

	struct list_head beacon_registrations;
	spinlock_t beacon_registrations_lock;

	/* protected by RTNL only */
	int num_running_ifaces;
	int num_running_monitor_ifaces;
	u64 cookie_counter;

	/* BSSes/scanning */
	spinlock_t bss_lock;
	struct list_head bss_list;
	struct rb_root bss_tree;
	u32 bss_generation;
	u32 bss_entries;
	struct cfg80211_scan_request *scan_req; /* protected by RTNL */
	struct cfg80211_scan_request *int_scan_req;
	struct sk_buff *scan_msg;
	struct list_head sched_scan_req_list;
	time64_t suspend_at;
	struct work_struct scan_done_wk;

	struct genl_info *cur_cmd_info;

	struct work_struct conn_work;
	struct work_struct event_work;

	struct delayed_work dfs_update_channels_wk;

	struct wireless_dev *background_radar_wdev;
	struct cfg80211_chan_def background_radar_chandef;
	struct delayed_work background_cac_done_wk;
	struct work_struct background_cac_abort_wk;

	/* netlink port which started critical protocol (0 means not started) */
	u32 crit_proto_nlportid;

	struct cfg80211_coalesce *coalesce;

	struct work_struct destroy_work;
	struct work_struct sched_scan_stop_wk;
	struct work_struct sched_scan_res_wk;

	struct cfg80211_chan_def radar_chandef;
	struct work_struct propagate_radar_detect_wk;

	struct cfg80211_chan_def cac_done_chandef;
	struct work_struct propagate_cac_done_wk;

	struct work_struct mgmt_registrations_update_wk;
	/* lock for all wdev lists */
	spinlock_t mgmt_registrations_lock;

	/* must be last because of the way we do wiphy_priv(),
	 * and it should at least be aligned to NETDEV_ALIGN */
	struct wiphy wiphy __aligned(NETDEV_ALIGN);
};

static inline
struct cfg80211_registered_device *wiphy_to_rdev(struct wiphy *wiphy)
{
	BUG_ON(!wiphy);
	return container_of(wiphy, struct cfg80211_registered_device, wiphy);
}

static inline void
cfg80211_rdev_free_wowlan(struct cfg80211_registered_device *rdev)
{
#ifdef CONFIG_PM
	int i;

	if (!rdev->wiphy.wowlan_config)
		return;
	for (i = 0; i < rdev->wiphy.wowlan_config->n_patterns; i++)
		kfree(rdev->wiphy.wowlan_config->patterns[i].mask);
	kfree(rdev->wiphy.wowlan_config->patterns);
	if (rdev->wiphy.wowlan_config->tcp &&
	    rdev->wiphy.wowlan_config->tcp->sock)
		sock_release(rdev->wiphy.wowlan_config->tcp->sock);
	kfree(rdev->wiphy.wowlan_config->tcp);
	kfree(rdev->wiphy.wowlan_config->nd_config);
	kfree(rdev->wiphy.wowlan_config);
#endif
}

static inline u64 cfg80211_assign_cookie(struct cfg80211_registered_device *rdev)
{
	u64 r = ++rdev->cookie_counter;

	if (WARN_ON(r == 0))
		r = ++rdev->cookie_counter;

	return r;
}

extern struct workqueue_struct *cfg80211_wq;
extern struct list_head cfg80211_rdev_list;
extern int cfg80211_rdev_list_generation;

struct cfg80211_internal_bss {
	struct list_head list;
	struct list_head hidden_list;
	struct rb_node rbn;
	u64 ts_boottime;
	unsigned long ts;
	unsigned long refcount;
	atomic_t hold;

	/* time at the start of the reception of the first octet of the
	 * timestamp field of the last beacon/probe received for this BSS.
	 * The time is the TSF of the BSS specified by %parent_bssid.
	 */
	u64 parent_tsf;

	/* the BSS according to which %parent_tsf is set. This is set to
	 * the BSS that the interface that requested the scan was connected to
	 * when the beacon/probe was received.
	 */
	u8 parent_bssid[ETH_ALEN] __aligned(2);

	/* must be last because of priv member */
	struct cfg80211_bss pub;
};

static inline struct cfg80211_internal_bss *bss_from_pub(struct cfg80211_bss *pub)
{
	return container_of(pub, struct cfg80211_internal_bss, pub);
}

static inline void cfg80211_hold_bss(struct cfg80211_internal_bss *bss)
{
	atomic_inc(&bss->hold);
	if (bss->pub.transmitted_bss) {
		bss = container_of(bss->pub.transmitted_bss,
				   struct cfg80211_internal_bss, pub);
		atomic_inc(&bss->hold);
	}
}

static inline void cfg80211_unhold_bss(struct cfg80211_internal_bss *bss)
{
	int r = atomic_dec_return(&bss->hold);
	WARN_ON(r < 0);
	if (bss->pub.transmitted_bss) {
		bss = container_of(bss->pub.transmitted_bss,
				   struct cfg80211_internal_bss, pub);
		r = atomic_dec_return(&bss->hold);
		WARN_ON(r < 0);
	}
}


struct cfg80211_registered_device *cfg80211_rdev_by_wiphy_idx(int wiphy_idx);
int get_wiphy_idx(struct wiphy *wiphy);

struct wiphy *wiphy_idx_to_wiphy(int wiphy_idx);

int cfg80211_switch_netns(struct cfg80211_registered_device *rdev,
			  struct net *net);

void cfg80211_init_wdev(struct wireless_dev *wdev);
void cfg80211_register_wdev(struct cfg80211_registered_device *rdev,
			    struct wireless_dev *wdev);

static inline void wdev_lock(struct wireless_dev *wdev)
	__acquires(wdev)
{
	mutex_lock(&wdev->mtx);
	__acquire(wdev->mtx);
}

static inline void wdev_unlock(struct wireless_dev *wdev)
	__releases(wdev)
{
	__release(wdev->mtx);
	mutex_unlock(&wdev->mtx);
}

#define ASSERT_WDEV_LOCK(wdev) lockdep_assert_held(&(wdev)->mtx)

static inline bool cfg80211_has_monitors_only(struct cfg80211_registered_device *rdev)
{
	lockdep_assert_held(&rdev->wiphy.mtx);

	return rdev->num_running_ifaces == rdev->num_running_monitor_ifaces &&
	       rdev->num_running_ifaces > 0;
}

enum cfg80211_event_type {
	EVENT_CONNECT_RESULT,
	EVENT_ROAMED,
	EVENT_DISCONNECTED,
	EVENT_IBSS_JOINED,
	EVENT_STOPPED,
	EVENT_PORT_AUTHORIZED,
};

struct cfg80211_event {
	struct list_head list;
	enum cfg80211_event_type type;

	union {
		struct cfg80211_connect_resp_params cr;
		struct cfg80211_roam_info rm;
		struct {
			const u8 *ie;
			size_t ie_len;
			u16 reason;
			bool locally_generated;
		} dc;
		struct {
			u8 bssid[ETH_ALEN];
			struct ieee80211_channel *channel;
		} ij;
		struct {
			u8 bssid[ETH_ALEN];
			const u8 *td_bitmap;
			u8 td_bitmap_len;
		} pa;
	};
};

struct cfg80211_cached_keys {
	struct key_params params[CFG80211_MAX_WEP_KEYS];
	u8 data[CFG80211_MAX_WEP_KEYS][WLAN_KEY_LEN_WEP104];
	int def;
};

struct cfg80211_beacon_registration {
	struct list_head list;
	u32 nlportid;
};

struct cfg80211_cqm_config {
	u32 rssi_hyst;
	s32 last_rssi_event_value;
	int n_rssi_thresholds;
	s32 rssi_thresholds[];
};

void cfg80211_destroy_ifaces(struct cfg80211_registered_device *rdev);

/* free object */
void cfg80211_dev_free(struct cfg80211_registered_device *rdev);

int cfg80211_dev_rename(struct cfg80211_registered_device *rdev,
			char *newname);

void ieee80211_set_bitrate_flags(struct wiphy *wiphy);

void cfg80211_bss_expire(struct cfg80211_registered_device *rdev);
void cfg80211_bss_age(struct cfg80211_registered_device *rdev,
                      unsigned long age_secs);
void cfg80211_update_assoc_bss_entry(struct wireless_dev *wdev,
				     unsigned int link,
				     struct ieee80211_channel *channel);

/* IBSS */
int __cfg80211_join_ibss(struct cfg80211_registered_device *rdev,
			 struct net_device *dev,
			 struct cfg80211_ibss_params *params,
			 struct cfg80211_cached_keys *connkeys);
void cfg80211_clear_ibss(struct net_device *dev, bool nowext);
int __cfg80211_leave_ibss(struct cfg80211_registered_device *rdev,
			  struct net_device *dev, bool nowext);
int cfg80211_leave_ibss(struct cfg80211_registered_device *rdev,
			struct net_device *dev, bool nowext);
void __cfg80211_ibss_joined(struct net_device *dev, const u8 *bssid,
			    struct ieee80211_channel *channel);
int cfg80211_ibss_wext_join(struct cfg80211_registered_device *rdev,
			    struct wireless_dev *wdev);

/* mesh */
extern const struct mesh_config default_mesh_config;
extern const struct mesh_setup default_mesh_setup;
int __cfg80211_join_mesh(struct cfg80211_registered_device *rdev,
			 struct net_device *dev,
			 struct mesh_setup *setup,
			 const struct mesh_config *conf);
int __cfg80211_leave_mesh(struct cfg80211_registered_device *rdev,
			  struct net_device *dev);
int cfg80211_leave_mesh(struct cfg80211_registered_device *rdev,
			struct net_device *dev);
int cfg80211_set_mesh_channel(struct cfg80211_registered_device *rdev,
			      struct wireless_dev *wdev,
			      struct cfg80211_chan_def *chandef);

/* OCB */
int __cfg80211_join_ocb(struct cfg80211_registered_device *rdev,
			struct net_device *dev,
			struct ocb_setup *setup);
int cfg80211_join_ocb(struct cfg80211_registered_device *rdev,
		      struct net_device *dev,
		      struct ocb_setup *setup);
int __cfg80211_leave_ocb(struct cfg80211_registered_device *rdev,
			 struct net_device *dev);
int cfg80211_leave_ocb(struct cfg80211_registered_device *rdev,
		       struct net_device *dev);

/* AP */
int __cfg80211_stop_ap(struct cfg80211_registered_device *rdev,
		       struct net_device *dev, int link,
		       bool notify);
int cfg80211_stop_ap(struct cfg80211_registered_device *rdev,
		     struct net_device *dev, int link,
		     bool notify);

/* MLME */
int cfg80211_mlme_auth(struct cfg80211_registered_device *rdev,
		       struct net_device *dev,
		       struct cfg80211_auth_request *req);
int cfg80211_mlme_assoc(struct cfg80211_registered_device *rdev,
			struct net_device *dev,
			struct cfg80211_assoc_request *req);
int cfg80211_mlme_deauth(struct cfg80211_registered_device *rdev,
			 struct net_device *dev, const u8 *bssid,
			 const u8 *ie, int ie_len, u16 reason,
			 bool local_state_change);
int cfg80211_mlme_disassoc(struct cfg80211_registered_device *rdev,
			   struct net_device *dev, const u8 *ap_addr,
			   const u8 *ie, int ie_len, u16 reason,
			   bool local_state_change);
void cfg80211_mlme_down(struct cfg80211_registered_device *rdev,
			struct net_device *dev);
int cfg80211_mlme_register_mgmt(struct wireless_dev *wdev, u32 snd_pid,
				u16 frame_type, const u8 *match_data,
				int match_len, bool multicast_rx,
				struct netlink_ext_ack *extack);
void cfg80211_mgmt_registrations_update_wk(struct work_struct *wk);
void cfg80211_mlme_unregister_socket(struct wireless_dev *wdev, u32 nlpid);
void cfg80211_mlme_purge_registrations(struct wireless_dev *wdev);
int cfg80211_mlme_mgmt_tx(struct cfg80211_registered_device *rdev,
			  struct wireless_dev *wdev,
			  struct cfg80211_mgmt_tx_params *params,
			  u64 *cookie);
void cfg80211_oper_and_ht_capa(struct ieee80211_ht_cap *ht_capa,
			       const struct ieee80211_ht_cap *ht_capa_mask);
void cfg80211_oper_and_vht_capa(struct ieee80211_vht_cap *vht_capa,
				const struct ieee80211_vht_cap *vht_capa_mask);

/* SME events */
int cfg80211_connect(struct cfg80211_registered_device *rdev,
		     struct net_device *dev,
		     struct cfg80211_connect_params *connect,
		     struct cfg80211_cached_keys *connkeys,
		     const u8 *prev_bssid);
void __cfg80211_connect_result(struct net_device *dev,
			       struct cfg80211_connect_resp_params *params,
			       bool wextev);
void __cfg80211_disconnected(struct net_device *dev, const u8 *ie,
			     size_t ie_len, u16 reason, bool from_ap);
int cfg80211_disconnect(struct cfg80211_registered_device *rdev,
			struct net_device *dev, u16 reason,
			bool wextev);
void __cfg80211_roamed(struct wireless_dev *wdev,
		       struct cfg80211_roam_info *info);
void __cfg80211_port_authorized(struct wireless_dev *wdev, const u8 *bssid,
				const u8 *td_bitmap, u8 td_bitmap_len);
int cfg80211_mgd_wext_connect(struct cfg80211_registered_device *rdev,
			      struct wireless_dev *wdev);
void cfg80211_autodisconnect_wk(struct work_struct *work);

/* SME implementation */
void cfg80211_conn_work(struct work_struct *work);
void cfg80211_sme_scan_done(struct net_device *dev);
bool cfg80211_sme_rx_assoc_resp(struct wireless_dev *wdev, u16 status);
void cfg80211_sme_rx_auth(struct wireless_dev *wdev, const u8 *buf, size_t len);
void cfg80211_sme_disassoc(struct wireless_dev *wdev);
void cfg80211_sme_deauth(struct wireless_dev *wdev);
void cfg80211_sme_auth_timeout(struct wireless_dev *wdev);
void cfg80211_sme_assoc_timeout(struct wireless_dev *wdev);
void cfg80211_sme_abandon_assoc(struct wireless_dev *wdev);

/* internal helpers */
bool cfg80211_supported_cipher_suite(struct wiphy *wiphy, u32 cipher);
bool cfg80211_valid_key_idx(struct cfg80211_registered_device *rdev,
			    int key_idx, bool pairwise);
int cfg80211_validate_key_settings(struct cfg80211_registered_device *rdev,
				   struct key_params *params, int key_idx,
				   bool pairwise, const u8 *mac_addr);
void __cfg80211_scan_done(struct work_struct *wk);
void ___cfg80211_scan_done(struct cfg80211_registered_device *rdev,
			   bool send_message);
void cfg80211_add_sched_scan_req(struct cfg80211_registered_device *rdev,
				 struct cfg80211_sched_scan_request *req);
int cfg80211_sched_scan_req_possible(struct cfg80211_registered_device *rdev,
				     bool want_multi);
void cfg80211_sched_scan_results_wk(struct work_struct *work);
int cfg80211_stop_sched_scan_req(struct cfg80211_registered_device *rdev,
				 struct cfg80211_sched_scan_request *req,
				 bool driver_initiated);
int __cfg80211_stop_sched_scan(struct cfg80211_registered_device *rdev,
			       u64 reqid, bool driver_initiated);
void cfg80211_upload_connect_keys(struct wireless_dev *wdev);
int cfg80211_change_iface(struct cfg80211_registered_device *rdev,
			  struct net_device *dev, enum nl80211_iftype ntype,
			  struct vif_params *params);
void cfg80211_process_rdev_events(struct cfg80211_registered_device *rdev);
void cfg80211_process_wdev_events(struct wireless_dev *wdev);

bool cfg80211_does_bw_fit_range(const struct ieee80211_freq_range *freq_range,
				u32 center_freq_khz, u32 bw_khz);

int cfg80211_scan(struct cfg80211_registered_device *rdev);

extern struct work_struct cfg80211_disconnect_work;

/**
 * cfg80211_chandef_dfs_usable - checks if chandef is DFS usable
 * @wiphy: the wiphy to validate against
 * @chandef: the channel definition to check
 *
 * Checks if chandef is usable and we can/need start CAC on such channel.
 *
 * Return: true if all channels available and at least
 *	   one channel requires CAC (NL80211_DFS_USABLE)
 */
bool cfg80211_chandef_dfs_usable(struct wiphy *wiphy,
				 const struct cfg80211_chan_def *chandef);

void cfg80211_set_dfs_state(struct wiphy *wiphy,
			    const struct cfg80211_chan_def *chandef,
			    enum nl80211_dfs_state dfs_state);

void cfg80211_dfs_channels_update_work(struct work_struct *work);

unsigned int
cfg80211_chandef_dfs_cac_time(struct wiphy *wiphy,
			      const struct cfg80211_chan_def *chandef);

void cfg80211_sched_dfs_chan_update(struct cfg80211_registered_device *rdev);

int
cfg80211_start_background_radar_detection(struct cfg80211_registered_device *rdev,
					  struct wireless_dev *wdev,
					  struct cfg80211_chan_def *chandef);

void cfg80211_stop_background_radar_detection(struct wireless_dev *wdev);

void cfg80211_background_cac_done_wk(struct work_struct *work);

void cfg80211_background_cac_abort_wk(struct work_struct *work);

bool cfg80211_any_wiphy_oper_chan(struct wiphy *wiphy,
				  struct ieee80211_channel *chan);

bool cfg80211_beaconing_iface_active(struct wireless_dev *wdev);

bool cfg80211_is_sub_chan(struct cfg80211_chan_def *chandef,
			  struct ieee80211_channel *chan,
			  bool primary_only);
bool cfg80211_wdev_on_sub_chan(struct wireless_dev *wdev,
			       struct ieee80211_channel *chan,
			       bool primary_only);

static inline unsigned int elapsed_jiffies_msecs(unsigned long start)
{
	unsigned long end = jiffies;

	if (end >= start)
		return jiffies_to_msecs(end - start);

	return jiffies_to_msecs(end + (ULONG_MAX - start) + 1);
}

int cfg80211_set_monitor_channel(struct cfg80211_registered_device *rdev,
				 struct cfg80211_chan_def *chandef);

int ieee80211_get_ratemask(struct ieee80211_supported_band *sband,
			   const u8 *rates, unsigned int n_rates,
			   u32 *mask);

int cfg80211_validate_beacon_int(struct cfg80211_registered_device *rdev,
				 enum nl80211_iftype iftype, u32 beacon_int);

void cfg80211_update_iface_num(struct cfg80211_registered_device *rdev,
			       enum nl80211_iftype iftype, int num);

void __cfg80211_leave(struct cfg80211_registered_device *rdev,
		      struct wireless_dev *wdev);
void cfg80211_leave(struct cfg80211_registered_device *rdev,
		    struct wireless_dev *wdev);

void cfg80211_stop_p2p_device(struct cfg80211_registered_device *rdev,
			      struct wireless_dev *wdev);

void cfg80211_stop_nan(struct cfg80211_registered_device *rdev,
		       struct wireless_dev *wdev);

struct cfg80211_internal_bss *
cfg80211_bss_update(struct cfg80211_registered_device *rdev,
		    struct cfg80211_internal_bss *tmp,
		    bool signal_valid, unsigned long ts);
#ifdef CONFIG_CFG80211_DEVELOPER_WARNINGS
#define CFG80211_DEV_WARN_ON(cond)	WARN_ON(cond)
#else
/*
 * Trick to enable using it as a condition,
 * and also not give a warning when it's
 * not used that way.
 */
#define CFG80211_DEV_WARN_ON(cond)	({bool __r = (cond); __r; })
#endif

void cfg80211_cqm_config_free(struct wireless_dev *wdev);

void cfg80211_release_pmsr(struct wireless_dev *wdev, u32 portid);
void cfg80211_pmsr_wdev_down(struct wireless_dev *wdev);
void cfg80211_pmsr_free_wk(struct work_struct *work);

void cfg80211_remove_link(struct wireless_dev *wdev, unsigned int link_id);
void cfg80211_remove_links(struct wireless_dev *wdev);
int cfg80211_remove_virtual_intf(struct cfg80211_registered_device *rdev,
				 struct wireless_dev *wdev);

#endif /* __NET_WIRELESS_CORE_H */
