#ifndef __NET_WIRELESS_NL80211_H
#define __NET_WIRELESS_NL80211_H

#include "core.h"

extern int nl80211_init(void);
extern void nl80211_exit(void);
extern void nl80211_notify_dev_rename(struct cfg80211_registered_device *rdev);
extern void nl80211_send_scan_done(struct cfg80211_registered_device *rdev,
				   struct net_device *netdev);
extern void nl80211_send_scan_aborted(struct cfg80211_registered_device *rdev,
				      struct net_device *netdev);
extern void nl80211_send_reg_change_event(struct regulatory_request *request);
extern void nl80211_send_rx_auth(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev,
				 const u8 *buf, size_t len);
extern void nl80211_send_rx_assoc(struct cfg80211_registered_device *rdev,
				  struct net_device *netdev,
				  const u8 *buf, size_t len);
extern void nl80211_send_deauth(struct cfg80211_registered_device *rdev,
				struct net_device *netdev,
				const u8 *buf, size_t len);
extern void nl80211_send_disassoc(struct cfg80211_registered_device *rdev,
				  struct net_device *netdev,
				  const u8 *buf, size_t len);
extern void nl80211_send_auth_timeout(struct cfg80211_registered_device *rdev,
				      struct net_device *netdev,
				      const u8 *addr);
extern void nl80211_send_assoc_timeout(struct cfg80211_registered_device *rdev,
				       struct net_device *netdev,
				       const u8 *addr);
extern void
nl80211_michael_mic_failure(struct cfg80211_registered_device *rdev,
			    struct net_device *netdev, const u8 *addr,
			    enum nl80211_key_type key_type,
			    int key_id, const u8 *tsc);

extern void
nl80211_send_beacon_hint_event(struct wiphy *wiphy,
			       struct ieee80211_channel *channel_before,
			       struct ieee80211_channel *channel_after);

void nl80211_send_ibss_bssid(struct cfg80211_registered_device *rdev,
			     struct net_device *netdev, const u8 *bssid,
			     gfp_t gfp);

#endif /* __NET_WIRELESS_NL80211_H */
