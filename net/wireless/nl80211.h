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
extern void nl80211_send_rx_deauth(struct cfg80211_registered_device *rdev,
				   struct net_device *netdev,
				   const u8 *buf, size_t len);
extern void nl80211_send_rx_disassoc(struct cfg80211_registered_device *rdev,
				     struct net_device *netdev,
				     const u8 *buf, size_t len);

#endif /* __NET_WIRELESS_NL80211_H */
