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

void cfg80211_send_rx_deauth(struct net_device *dev, const u8 *buf, size_t len)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	nl80211_send_rx_deauth(rdev, dev, buf, len);
}
EXPORT_SYMBOL(cfg80211_send_rx_deauth);

void cfg80211_send_rx_disassoc(struct net_device *dev, const u8 *buf,
			       size_t len)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	nl80211_send_rx_disassoc(rdev, dev, buf, len);
}
EXPORT_SYMBOL(cfg80211_send_rx_disassoc);
