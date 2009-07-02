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

void cfg80211_send_rx_auth(struct net_device *dev, const u8 *buf, size_t len, gfp_t gfp)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	nl80211_send_rx_auth(rdev, dev, buf, len, gfp);
	cfg80211_sme_rx_auth(dev, buf, len);
}
EXPORT_SYMBOL(cfg80211_send_rx_auth);

void cfg80211_send_rx_assoc(struct net_device *dev, const u8 *buf, size_t len, gfp_t gfp)
{
	u16 status_code;
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
	u8 *ie = mgmt->u.assoc_resp.variable;
	int ieoffs = offsetof(struct ieee80211_mgmt, u.assoc_resp.variable);

	status_code = le16_to_cpu(mgmt->u.assoc_resp.status_code);

	nl80211_send_rx_assoc(rdev, dev, buf, len, gfp);

	cfg80211_connect_result(dev, mgmt->bssid, NULL, 0, ie, len - ieoffs,
				status_code, gfp);
}
EXPORT_SYMBOL(cfg80211_send_rx_assoc);

void cfg80211_send_deauth(struct net_device *dev, const u8 *buf, size_t len, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;

	nl80211_send_deauth(rdev, dev, buf, len, gfp);

	if (wdev->sme_state == CFG80211_SME_CONNECTED) {
		u16 reason_code;
		bool from_ap;

		reason_code = le16_to_cpu(mgmt->u.deauth.reason_code);

		from_ap = memcmp(mgmt->da, dev->dev_addr, ETH_ALEN) == 0;
		__cfg80211_disconnected(dev, gfp, NULL, 0,
					reason_code, from_ap);

		wdev->sme_state = CFG80211_SME_IDLE;
	} else if (wdev->sme_state == CFG80211_SME_CONNECTING) {
		cfg80211_connect_result(dev, mgmt->bssid, NULL, 0, NULL, 0,
					WLAN_STATUS_UNSPECIFIED_FAILURE, gfp);
	}
}
EXPORT_SYMBOL(cfg80211_send_deauth);

void cfg80211_send_disassoc(struct net_device *dev, const u8 *buf, size_t len, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;

	nl80211_send_disassoc(rdev, dev, buf, len, gfp);

	if (wdev->sme_state == CFG80211_SME_CONNECTED) {
		u16 reason_code;
		bool from_ap;

		reason_code = le16_to_cpu(mgmt->u.disassoc.reason_code);

		from_ap = memcmp(mgmt->da, dev->dev_addr, ETH_ALEN) == 0;
		__cfg80211_disconnected(dev, gfp, NULL, 0,
					reason_code, from_ap);

		wdev->sme_state = CFG80211_SME_IDLE;
	}
}
EXPORT_SYMBOL(cfg80211_send_disassoc);

void cfg80211_send_auth_timeout(struct net_device *dev, const u8 *addr, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	nl80211_send_auth_timeout(rdev, dev, addr, gfp);
	if (wdev->sme_state == CFG80211_SME_CONNECTING)
		cfg80211_connect_result(dev, addr, NULL, 0, NULL, 0,
					WLAN_STATUS_UNSPECIFIED_FAILURE, gfp);
	wdev->sme_state = CFG80211_SME_IDLE;
}
EXPORT_SYMBOL(cfg80211_send_auth_timeout);

void cfg80211_send_assoc_timeout(struct net_device *dev, const u8 *addr, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	nl80211_send_assoc_timeout(rdev, dev, addr, gfp);
	if (wdev->sme_state == CFG80211_SME_CONNECTING)
		cfg80211_connect_result(dev, addr, NULL, 0, NULL, 0,
					WLAN_STATUS_UNSPECIFIED_FAILURE, gfp);
	wdev->sme_state = CFG80211_SME_IDLE;
}
EXPORT_SYMBOL(cfg80211_send_assoc_timeout);

void cfg80211_michael_mic_failure(struct net_device *dev, const u8 *addr,
				  enum nl80211_key_type key_type, int key_id,
				  const u8 *tsc, gfp_t gfp)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
#ifdef CONFIG_WIRELESS_EXT
	union iwreq_data wrqu;
	char *buf = kmalloc(128, gfp);

	if (buf) {
		sprintf(buf, "MLME-MICHAELMICFAILURE.indication("
			"keyid=%d %scast addr=%pM)", key_id,
			key_type == NL80211_KEYTYPE_GROUP ? "broad" : "uni",
			addr);
		memset(&wrqu, 0, sizeof(wrqu));
		wrqu.data.length = strlen(buf);
		wireless_send_event(dev, IWEVCUSTOM, &wrqu, buf);
		kfree(buf);
	}
#endif

	nl80211_michael_mic_failure(rdev, dev, addr, key_type, key_id, tsc, gfp);
}
EXPORT_SYMBOL(cfg80211_michael_mic_failure);
