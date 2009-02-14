#ifndef __NET_WIRELESS_NL80211_H
#define __NET_WIRELESS_NL80211_H

#include "core.h"

#ifdef CONFIG_NL80211
extern int nl80211_init(void);
extern void nl80211_exit(void);
extern void nl80211_notify_dev_rename(struct cfg80211_registered_device *rdev);
extern void nl80211_send_scan_done(struct cfg80211_registered_device *rdev,
				   struct net_device *netdev);
extern void nl80211_send_scan_aborted(struct cfg80211_registered_device *rdev,
				      struct net_device *netdev);
#else
static inline int nl80211_init(void)
{
	return 0;
}
static inline void nl80211_exit(void)
{
}
static inline void nl80211_notify_dev_rename(
	struct cfg80211_registered_device *rdev)
{
}
static inline void
nl80211_send_scan_done(struct cfg80211_registered_device *rdev,
		       struct net_device *netdev)
{}
static inline void nl80211_send_scan_aborted(
					struct cfg80211_registered_device *rdev,
					struct net_device *netdev)
{}
#endif /* CONFIG_NL80211 */

#endif /* __NET_WIRELESS_NL80211_H */
