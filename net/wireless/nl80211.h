#ifndef __NET_WIRELESS_NL80211_H
#define __NET_WIRELESS_NL80211_H

#include "core.h"

int nl80211_init(void);
void nl80211_exit(void);
void nl80211_notify_dev_rename(struct cfg80211_registered_device *rdev);
void nl80211_send_scan_start(struct cfg80211_registered_device *rdev,
			     struct net_device *netdev);
void nl80211_send_scan_done(struct cfg80211_registered_device *rdev,
			    struct net_device *netdev);
void nl80211_send_scan_aborted(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev);
void nl80211_send_sched_scan(struct cfg80211_registered_device *rdev,
			     struct net_device *netdev, u32 cmd);
void nl80211_send_sched_scan_results(struct cfg80211_registered_device *rdev,
				     struct net_device *netdev);
void nl80211_send_reg_change_event(struct regulatory_request *request);
void nl80211_send_rx_auth(struct cfg80211_registered_device *rdev,
			  struct net_device *netdev,
			  const u8 *buf, size_t len, gfp_t gfp);
void nl80211_send_rx_assoc(struct cfg80211_registered_device *rdev,
			   struct net_device *netdev,
			   const u8 *buf, size_t len, gfp_t gfp);
void nl80211_send_deauth(struct cfg80211_registered_device *rdev,
			 struct net_device *netdev,
			 const u8 *buf, size_t len, gfp_t gfp);
void nl80211_send_disassoc(struct cfg80211_registered_device *rdev,
			   struct net_device *netdev,
			   const u8 *buf, size_t len, gfp_t gfp);
void nl80211_send_unprot_deauth(struct cfg80211_registered_device *rdev,
				struct net_device *netdev,
				const u8 *buf, size_t len, gfp_t gfp);
void nl80211_send_unprot_disassoc(struct cfg80211_registered_device *rdev,
				  struct net_device *netdev,
				  const u8 *buf, size_t len, gfp_t gfp);
void nl80211_send_auth_timeout(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev,
			       const u8 *addr, gfp_t gfp);
void nl80211_send_assoc_timeout(struct cfg80211_registered_device *rdev,
				struct net_device *netdev,
				const u8 *addr, gfp_t gfp);
void nl80211_send_connect_result(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev, const u8 *bssid,
				 const u8 *req_ie, size_t req_ie_len,
				 const u8 *resp_ie, size_t resp_ie_len,
				 u16 status, gfp_t gfp);
void nl80211_send_roamed(struct cfg80211_registered_device *rdev,
			 struct net_device *netdev, const u8 *bssid,
			 const u8 *req_ie, size_t req_ie_len,
			 const u8 *resp_ie, size_t resp_ie_len, gfp_t gfp);
void nl80211_send_disconnected(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, u16 reason,
			       const u8 *ie, size_t ie_len, bool from_ap);

void nl80211_send_new_peer_candidate(struct cfg80211_registered_device *rdev,
				     struct net_device *netdev,
				     const u8 *macaddr, const u8* ie, u8 ie_len,
				     gfp_t gfp);
void
nl80211_michael_mic_failure(struct cfg80211_registered_device *rdev,
			    struct net_device *netdev, const u8 *addr,
			    enum nl80211_key_type key_type,
			    int key_id, const u8 *tsc, gfp_t gfp);

void
nl80211_send_beacon_hint_event(struct wiphy *wiphy,
			       struct ieee80211_channel *channel_before,
			       struct ieee80211_channel *channel_after);

void nl80211_send_ibss_bssid(struct cfg80211_registered_device *rdev,
			     struct net_device *netdev, const u8 *bssid,
			     gfp_t gfp);

void nl80211_send_remain_on_channel(struct cfg80211_registered_device *rdev,
				    struct wireless_dev *wdev, u64 cookie,
				    struct ieee80211_channel *chan,
				    enum nl80211_channel_type channel_type,
				    unsigned int duration, gfp_t gfp);
void nl80211_send_remain_on_channel_cancel(
	struct cfg80211_registered_device *rdev,
	struct wireless_dev *wdev,
	u64 cookie, struct ieee80211_channel *chan,
	enum nl80211_channel_type channel_type, gfp_t gfp);

void nl80211_send_sta_event(struct cfg80211_registered_device *rdev,
			    struct net_device *dev, const u8 *mac_addr,
			    struct station_info *sinfo, gfp_t gfp);
void nl80211_send_sta_del_event(struct cfg80211_registered_device *rdev,
				struct net_device *dev, const u8 *mac_addr,
				gfp_t gfp);

int nl80211_send_mgmt(struct cfg80211_registered_device *rdev,
		      struct wireless_dev *wdev, u32 nlpid,
		      int freq, int sig_dbm,
		      const u8 *buf, size_t len, gfp_t gfp);
void nl80211_send_mgmt_tx_status(struct cfg80211_registered_device *rdev,
				 struct wireless_dev *wdev, u64 cookie,
				 const u8 *buf, size_t len, bool ack,
				 gfp_t gfp);

void
nl80211_send_cqm_rssi_notify(struct cfg80211_registered_device *rdev,
			     struct net_device *netdev,
			     enum nl80211_cqm_rssi_threshold_event rssi_event,
			     gfp_t gfp);
void
nl80211_send_cqm_pktloss_notify(struct cfg80211_registered_device *rdev,
				struct net_device *netdev, const u8 *peer,
				u32 num_packets, gfp_t gfp);

void nl80211_gtk_rekey_notify(struct cfg80211_registered_device *rdev,
			      struct net_device *netdev, const u8 *bssid,
			      const u8 *replay_ctr, gfp_t gfp);

void nl80211_pmksa_candidate_notify(struct cfg80211_registered_device *rdev,
				    struct net_device *netdev, int index,
				    const u8 *bssid, bool preauth, gfp_t gfp);

void nl80211_ch_switch_notify(struct cfg80211_registered_device *rdev,
			      struct net_device *dev, int freq,
			      enum nl80211_channel_type type, gfp_t gfp);

bool nl80211_unexpected_frame(struct net_device *dev,
			      const u8 *addr, gfp_t gfp);
bool nl80211_unexpected_4addr_frame(struct net_device *dev,
				    const u8 *addr, gfp_t gfp);

#endif /* __NET_WIRELESS_NL80211_H */
