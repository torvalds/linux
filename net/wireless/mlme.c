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
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
	u8 *bssid = mgmt->bssid;
	int i;
	u16 status = le16_to_cpu(mgmt->u.auth.status_code);
	bool done = false;

	for (i = 0; i < MAX_AUTH_BSSES; i++) {
		if (wdev->authtry_bsses[i] &&
		    memcmp(wdev->authtry_bsses[i]->pub.bssid, bssid,
							ETH_ALEN) == 0) {
			if (status == WLAN_STATUS_SUCCESS) {
				wdev->auth_bsses[i] = wdev->authtry_bsses[i];
			} else {
				cfg80211_unhold_bss(wdev->authtry_bsses[i]);
				cfg80211_put_bss(&wdev->authtry_bsses[i]->pub);
			}
			wdev->authtry_bsses[i] = NULL;
			done = true;
			break;
		}
	}

	WARN_ON(!done);

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
	int i, ieoffs = offsetof(struct ieee80211_mgmt, u.assoc_resp.variable);
	bool done;

	status_code = le16_to_cpu(mgmt->u.assoc_resp.status_code);

	nl80211_send_rx_assoc(rdev, dev, buf, len, gfp);

	cfg80211_connect_result(dev, mgmt->bssid, NULL, 0, ie, len - ieoffs,
				status_code, gfp);

	if (status_code == WLAN_STATUS_SUCCESS) {
		for (i = 0; wdev->current_bss && i < MAX_AUTH_BSSES; i++) {
			if (wdev->auth_bsses[i] == wdev->current_bss) {
				cfg80211_unhold_bss(wdev->auth_bsses[i]);
				cfg80211_put_bss(&wdev->auth_bsses[i]->pub);
				wdev->auth_bsses[i] = NULL;
				done = true;
				break;
			}
		}

		WARN_ON(!done);
	}
}
EXPORT_SYMBOL(cfg80211_send_rx_assoc);

void cfg80211_send_deauth(struct net_device *dev, const u8 *buf, size_t len, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
	const u8 *bssid = mgmt->bssid;
	int i;
	bool done = false;

	nl80211_send_deauth(rdev, dev, buf, len, gfp);

	if (wdev->current_bss &&
	    memcmp(wdev->current_bss->pub.bssid, bssid, ETH_ALEN) == 0) {
		done = true;
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(&wdev->current_bss->pub);
		wdev->current_bss = NULL;
	} else for (i = 0; i < MAX_AUTH_BSSES; i++) {
		if (wdev->auth_bsses[i] &&
		    memcmp(wdev->auth_bsses[i]->pub.bssid, bssid, ETH_ALEN) == 0) {
			cfg80211_unhold_bss(wdev->auth_bsses[i]);
			cfg80211_put_bss(&wdev->auth_bsses[i]->pub);
			wdev->auth_bsses[i] = NULL;
			done = true;
			break;
		}
		if (wdev->authtry_bsses[i] &&
		    memcmp(wdev->authtry_bsses[i]->pub.bssid, bssid, ETH_ALEN) == 0) {
			cfg80211_unhold_bss(wdev->authtry_bsses[i]);
			cfg80211_put_bss(&wdev->authtry_bsses[i]->pub);
			wdev->authtry_bsses[i] = NULL;
			done = true;
			break;
		}
	}
/*
 * mac80211 currently triggers this warning,
 * so disable for now (it's harmless, just
 * means that we got a spurious event)

	WARN_ON(!done);

 */

	if (wdev->sme_state == CFG80211_SME_CONNECTED) {
		u16 reason_code;
		bool from_ap;

		reason_code = le16_to_cpu(mgmt->u.deauth.reason_code);

		from_ap = memcmp(mgmt->da, dev->dev_addr, ETH_ALEN) == 0;
		__cfg80211_disconnected(dev, gfp, NULL, 0,
					reason_code, from_ap);
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
	const u8 *bssid = mgmt->bssid;
	int i;
	u16 reason_code;
	bool from_ap;
	bool done = false;

	nl80211_send_disassoc(rdev, dev, buf, len, gfp);

	if (!wdev->sme_state == CFG80211_SME_CONNECTED)
		return;

	if (wdev->current_bss &&
	    memcmp(wdev->current_bss, bssid, ETH_ALEN) == 0) {
		for (i = 0; i < MAX_AUTH_BSSES; i++) {
			if (wdev->authtry_bsses[i] || wdev->auth_bsses[i])
				continue;
			wdev->auth_bsses[i] = wdev->current_bss;
			wdev->current_bss = NULL;
			done = true;
			cfg80211_sme_disassoc(dev, i);
			break;
		}
		WARN_ON(!done);
	} else
		WARN_ON(1);


	reason_code = le16_to_cpu(mgmt->u.disassoc.reason_code);

	from_ap = memcmp(mgmt->da, dev->dev_addr, ETH_ALEN) == 0;
	__cfg80211_disconnected(dev, gfp, NULL, 0,
				reason_code, from_ap);
}
EXPORT_SYMBOL(cfg80211_send_disassoc);

void cfg80211_send_auth_timeout(struct net_device *dev, const u8 *addr, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	int i;
	bool done = false;

	nl80211_send_auth_timeout(rdev, dev, addr, gfp);
	if (wdev->sme_state == CFG80211_SME_CONNECTING)
		cfg80211_connect_result(dev, addr, NULL, 0, NULL, 0,
					WLAN_STATUS_UNSPECIFIED_FAILURE, gfp);

	for (i = 0; addr && i < MAX_AUTH_BSSES; i++) {
		if (wdev->authtry_bsses[i] &&
		    memcmp(wdev->authtry_bsses[i]->pub.bssid,
			   addr, ETH_ALEN) == 0) {
			cfg80211_unhold_bss(wdev->authtry_bsses[i]);
			cfg80211_put_bss(&wdev->authtry_bsses[i]->pub);
			wdev->authtry_bsses[i] = NULL;
			done = true;
			break;
		}
	}

	WARN_ON(!done);
}
EXPORT_SYMBOL(cfg80211_send_auth_timeout);

void cfg80211_send_assoc_timeout(struct net_device *dev, const u8 *addr, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	int i;
	bool done = false;

	nl80211_send_assoc_timeout(rdev, dev, addr, gfp);
	if (wdev->sme_state == CFG80211_SME_CONNECTING)
		cfg80211_connect_result(dev, addr, NULL, 0, NULL, 0,
					WLAN_STATUS_UNSPECIFIED_FAILURE, gfp);

	for (i = 0; addr && i < MAX_AUTH_BSSES; i++) {
		if (wdev->auth_bsses[i] &&
		    memcmp(wdev->auth_bsses[i]->pub.bssid,
			   addr, ETH_ALEN) == 0) {
			cfg80211_unhold_bss(wdev->auth_bsses[i]);
			cfg80211_put_bss(&wdev->auth_bsses[i]->pub);
			wdev->auth_bsses[i] = NULL;
			done = true;
			break;
		}
	}

	WARN_ON(!done);
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

/* some MLME handling for userspace SME */
int cfg80211_mlme_auth(struct cfg80211_registered_device *rdev,
		       struct net_device *dev, struct ieee80211_channel *chan,
		       enum nl80211_auth_type auth_type, const u8 *bssid,
		       const u8 *ssid, int ssid_len,
		       const u8 *ie, int ie_len)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_auth_request req;
	struct cfg80211_internal_bss *bss;
	int i, err, slot = -1, nfree = 0;

	if (wdev->current_bss &&
	    memcmp(bssid, wdev->current_bss->pub.bssid, ETH_ALEN) == 0)
		return -EALREADY;

	for (i = 0; i < MAX_AUTH_BSSES; i++) {
		if (wdev->authtry_bsses[i] &&
		    memcmp(bssid, wdev->authtry_bsses[i]->pub.bssid,
						ETH_ALEN) == 0)
			return -EALREADY;
		if (wdev->auth_bsses[i] &&
		    memcmp(bssid, wdev->auth_bsses[i]->pub.bssid,
						ETH_ALEN) == 0)
			return -EALREADY;
	}

	memset(&req, 0, sizeof(req));

	req.ie = ie;
	req.ie_len = ie_len;
	req.auth_type = auth_type;
	req.bss = cfg80211_get_bss(&rdev->wiphy, chan, bssid, ssid, ssid_len,
				   WLAN_CAPABILITY_ESS, WLAN_CAPABILITY_ESS);
	if (!req.bss)
		return -ENOENT;

	bss = bss_from_pub(req.bss);

	for (i = 0; i < MAX_AUTH_BSSES; i++) {
		if (!wdev->auth_bsses[i] && !wdev->authtry_bsses[i]) {
			slot = i;
			nfree++;
		}
	}

	/* we need one free slot for disassoc and one for this auth */
	if (nfree < 2) {
		err = -ENOSPC;
		goto out;
	}

	wdev->authtry_bsses[slot] = bss;
	cfg80211_hold_bss(bss);

	err = rdev->ops->auth(&rdev->wiphy, dev, &req);
	if (err) {
		wdev->authtry_bsses[slot] = NULL;
		cfg80211_unhold_bss(bss);
	}

 out:
	if (err)
		cfg80211_put_bss(req.bss);
	return err;
}

int cfg80211_mlme_assoc(struct cfg80211_registered_device *rdev,
			struct net_device *dev, struct ieee80211_channel *chan,
			const u8 *bssid, const u8 *ssid, int ssid_len,
			const u8 *ie, int ie_len, bool use_mfp,
			struct cfg80211_crypto_settings *crypt)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_assoc_request req;
	struct cfg80211_internal_bss *bss;
	int i, err, slot = -1;

	memset(&req, 0, sizeof(req));

	if (wdev->current_bss)
		return -EALREADY;

	req.ie = ie;
	req.ie_len = ie_len;
	memcpy(&req.crypto, crypt, sizeof(req.crypto));
	req.use_mfp = use_mfp;
	req.bss = cfg80211_get_bss(&rdev->wiphy, chan, bssid, ssid, ssid_len,
				   WLAN_CAPABILITY_ESS, WLAN_CAPABILITY_ESS);
	if (!req.bss)
		return -ENOENT;

	bss = bss_from_pub(req.bss);

	for (i = 0; i < MAX_AUTH_BSSES; i++) {
		if (bss == wdev->auth_bsses[i]) {
			slot = i;
			break;
		}
	}

	if (slot < 0) {
		err = -ENOTCONN;
		goto out;
	}

	err = rdev->ops->assoc(&rdev->wiphy, dev, &req);
 out:
	/* still a reference in wdev->auth_bsses[slot] */
	cfg80211_put_bss(req.bss);
	return err;
}

int cfg80211_mlme_deauth(struct cfg80211_registered_device *rdev,
			 struct net_device *dev, const u8 *bssid,
			 const u8 *ie, int ie_len, u16 reason)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_deauth_request req;
	int i;

	memset(&req, 0, sizeof(req));
	req.reason_code = reason;
	req.ie = ie;
	req.ie_len = ie_len;
	if (wdev->current_bss &&
	    memcmp(wdev->current_bss->pub.bssid, bssid, ETH_ALEN) == 0) {
		req.bss = &wdev->current_bss->pub;
	} else for (i = 0; i < MAX_AUTH_BSSES; i++) {
		if (wdev->auth_bsses[i] &&
		    memcmp(bssid, wdev->auth_bsses[i]->pub.bssid, ETH_ALEN) == 0) {
			req.bss = &wdev->auth_bsses[i]->pub;
			break;
		}
		if (wdev->authtry_bsses[i] &&
		    memcmp(bssid, wdev->authtry_bsses[i]->pub.bssid, ETH_ALEN) == 0) {
			req.bss = &wdev->authtry_bsses[i]->pub;
			break;
		}
	}

	if (!req.bss)
		return -ENOTCONN;

	return rdev->ops->deauth(&rdev->wiphy, dev, &req);
}

int cfg80211_mlme_disassoc(struct cfg80211_registered_device *rdev,
			   struct net_device *dev, const u8 *bssid,
			   const u8 *ie, int ie_len, u16 reason)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_disassoc_request req;

	memset(&req, 0, sizeof(req));
	req.reason_code = reason;
	req.ie = ie;
	req.ie_len = ie_len;
	if (memcmp(wdev->current_bss->pub.bssid, bssid, ETH_ALEN) == 0)
		req.bss = &wdev->current_bss->pub;
	else
		return -ENOTCONN;

	return rdev->ops->disassoc(&rdev->wiphy, dev, &req);
}

void cfg80211_mlme_down(struct cfg80211_registered_device *rdev,
			struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_deauth_request req;
	int i;

	if (!rdev->ops->deauth)
		return;

	memset(&req, 0, sizeof(req));
	req.reason_code = WLAN_REASON_DEAUTH_LEAVING;
	req.ie = NULL;
	req.ie_len = 0;

	if (wdev->current_bss) {
		req.bss = &wdev->current_bss->pub;
		rdev->ops->deauth(&rdev->wiphy, dev, &req);
		if (wdev->current_bss) {
			cfg80211_unhold_bss(wdev->current_bss);
			cfg80211_put_bss(&wdev->current_bss->pub);
			wdev->current_bss = NULL;
		}
	}

	for (i = 0; i < MAX_AUTH_BSSES; i++) {
		if (wdev->auth_bsses[i]) {
			req.bss = &wdev->auth_bsses[i]->pub;
			rdev->ops->deauth(&rdev->wiphy, dev, &req);
			if (wdev->auth_bsses[i]) {
				cfg80211_unhold_bss(wdev->auth_bsses[i]);
				cfg80211_put_bss(&wdev->auth_bsses[i]->pub);
				wdev->auth_bsses[i] = NULL;
			}
		}
		if (wdev->authtry_bsses[i]) {
			req.bss = &wdev->authtry_bsses[i]->pub;
			rdev->ops->deauth(&rdev->wiphy, dev, &req);
			if (wdev->authtry_bsses[i]) {
				cfg80211_unhold_bss(wdev->authtry_bsses[i]);
				cfg80211_put_bss(&wdev->authtry_bsses[i]->pub);
				wdev->authtry_bsses[i] = NULL;
			}
		}
	}
}
