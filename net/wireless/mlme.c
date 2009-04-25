/*
 * cfg80211 MLME SAP interface
 *
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/nl80211.h>
#include <net/cfg80211.h>
#include "core.h"
#include "nl80211.h"

void cfg80211_send_rx_auth(struct net_device *dev, const u8 *buf, size_t len)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	nl80211_send_rx_auth(rdev, dev, buf, len);
}
EXPORT_SYMBOL(cfg80211_send_rx_auth);

void cfg80211_send_rx_assoc(struct net_device *dev, const u8 *buf, size_t len)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	nl80211_send_rx_assoc(rdev, dev, buf, len);
}
EXPORT_SYMBOL(cfg80211_send_rx_assoc);

void cfg80211_send_deauth(struct net_device *dev, const u8 *buf, size_t len)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	nl80211_send_deauth(rdev, dev, buf, len);
}
EXPORT_SYMBOL(cfg80211_send_deauth);

void cfg80211_send_disassoc(struct net_device *dev, const u8 *buf, size_t len)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	nl80211_send_disassoc(rdev, dev, buf, len);
}
EXPORT_SYMBOL(cfg80211_send_disassoc);

static void cfg80211_wext_disconnected(struct net_device *dev)
{
#ifdef CONFIG_WIRELESS_EXT
	union iwreq_data wrqu;
	memset(&wrqu, 0, sizeof(wrqu));
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
#endif
}

void cfg80211_send_auth_timeout(struct net_device *dev, const u8 *addr)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	nl80211_send_auth_timeout(rdev, dev, addr);
	cfg80211_wext_disconnected(dev);
}
EXPORT_SYMBOL(cfg80211_send_auth_timeout);

void cfg80211_send_assoc_timeout(struct net_device *dev, const u8 *addr)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	nl80211_send_assoc_timeout(rdev, dev, addr);
	cfg80211_wext_disconnected(dev);
}
EXPORT_SYMBOL(cfg80211_send_assoc_timeout);

void cfg80211_michael_mic_failure(struct net_device *dev, const u8 *addr,
				  enum nl80211_key_type key_type, int key_id,
				  const u8 *tsc)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	nl80211_michael_mic_failure(rdev, dev, addr, key_type, key_id, tsc);
}
EXPORT_SYMBOL(cfg80211_michael_mic_failure);
