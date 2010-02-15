/*
 * cfg80211 MLME SAP interface
 *
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/nl80211.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>
#include <net/iw_handler.h>
#include "core.h"
#include "nl80211.h"

void cfg80211_send_rx_auth(struct net_device *dev, const u8 *buf, size_t len)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
	u8 *bssid = mgmt->bssid;
	int i;
	u16 status = le16_to_cpu(mgmt->u.auth.status_code);
	bool done = false;

	wdev_lock(wdev);

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

	nl80211_send_rx_auth(rdev, dev, buf, len, GFP_KERNEL);
	cfg80211_sme_rx_auth(dev, buf, len);

	wdev_unlock(wdev);
}
EXPORT_SYMBOL(cfg80211_send_rx_auth);

void cfg80211_send_rx_assoc(struct net_device *dev, const u8 *buf, size_t len)
{
	u16 status_code;
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
	u8 *ie = mgmt->u.assoc_resp.variable;
	int i, ieoffs = offsetof(struct ieee80211_mgmt, u.assoc_resp.variable);
	struct cfg80211_internal_bss *bss = NULL;

	wdev_lock(wdev);

	status_code = le16_to_cpu(mgmt->u.assoc_resp.status_code);

	/*
	 * This is a bit of a hack, we don't notify userspace of
	 * a (re-)association reply if we tried to send a reassoc
	 * and got a reject -- we only try again with an assoc
	 * frame instead of reassoc.
	 */
	if (status_code != WLAN_STATUS_SUCCESS && wdev->conn &&
	    cfg80211_sme_failed_reassoc(wdev))
		goto out;

	nl80211_send_rx_assoc(rdev, dev, buf, len, GFP_KERNEL);

	if (status_code == WLAN_STATUS_SUCCESS) {
		for (i = 0; i < MAX_AUTH_BSSES; i++) {
			if (!wdev->auth_bsses[i])
				continue;
			if (memcmp(wdev->auth_bsses[i]->pub.bssid, mgmt->bssid,
				   ETH_ALEN) == 0) {
				bss = wdev->auth_bsses[i];
				wdev->auth_bsses[i] = NULL;
				/* additional reference to drop hold */
				cfg80211_ref_bss(bss);
				break;
			}
		}

		/*
		 * We might be coming here because the driver reported
		 * a successful association at the same time as the
		 * user requested a deauth. In that case, we will have
		 * removed the BSS from the auth_bsses list due to the
		 * deauth request when the assoc response makes it. If
		 * the two code paths acquire the lock the other way
		 * around, that's just the standard situation of a
		 * deauth being requested while connected.
		 */
		if (!bss)
			goto out;
	} else if (wdev->conn) {
		cfg80211_sme_failed_assoc(wdev);
		/*
		 * do not call connect_result() now because the
		 * sme will schedule work that does it later.
		 */
		goto out;
	}

	if (!wdev->conn && wdev->sme_state == CFG80211_SME_IDLE) {
		/*
		 * This is for the userspace SME, the CONNECTING
		 * state will be changed to CONNECTED by
		 * __cfg80211_connect_result() below.
		 */
		wdev->sme_state = CFG80211_SME_CONNECTING;
	}

	/* this consumes one bss reference (unless bss is NULL) */
	__cfg80211_connect_result(dev, mgmt->bssid, NULL, 0, ie, len - ieoffs,
				  status_code,
				  status_code == WLAN_STATUS_SUCCESS,
				  bss ? &bss->pub : NULL);
	/* drop hold now, and also reference acquired above */
	if (bss) {
		cfg80211_unhold_bss(bss);
		cfg80211_put_bss(&bss->pub);
	}

 out:
	wdev_unlock(wdev);
}
EXPORT_SYMBOL(cfg80211_send_rx_assoc);

void __cfg80211_send_deauth(struct net_device *dev,
				   const u8 *buf, size_t len)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
	const u8 *bssid = mgmt->bssid;
	int i;
	bool found = false;

	ASSERT_WDEV_LOCK(wdev);

	if (wdev->current_bss &&
	    memcmp(wdev->current_bss->pub.bssid, bssid, ETH_ALEN) == 0) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(&wdev->current_bss->pub);
		wdev->current_bss = NULL;
		found = true;
	} else for (i = 0; i < MAX_AUTH_BSSES; i++) {
		if (wdev->auth_bsses[i] &&
		    memcmp(wdev->auth_bsses[i]->pub.bssid, bssid, ETH_ALEN) == 0) {
			cfg80211_unhold_bss(wdev->auth_bsses[i]);
			cfg80211_put_bss(&wdev->auth_bsses[i]->pub);
			wdev->auth_bsses[i] = NULL;
			found = true;
			break;
		}
		if (wdev->authtry_bsses[i] &&
		    memcmp(wdev->authtry_bsses[i]->pub.bssid, bssid, ETH_ALEN) == 0) {
			cfg80211_unhold_bss(wdev->authtry_bsses[i]);
			cfg80211_put_bss(&wdev->authtry_bsses[i]->pub);
			wdev->authtry_bsses[i] = NULL;
			found = true;
			break;
		}
	}

	if (!found)
		return;

	nl80211_send_deauth(rdev, dev, buf, len, GFP_KERNEL);

	if (wdev->sme_state == CFG80211_SME_CONNECTED) {
		u16 reason_code;
		bool from_ap;

		reason_code = le16_to_cpu(mgmt->u.deauth.reason_code);

		from_ap = memcmp(mgmt->sa, dev->dev_addr, ETH_ALEN) != 0;
		__cfg80211_disconnected(dev, NULL, 0, reason_code, from_ap);
	} else if (wdev->sme_state == CFG80211_SME_CONNECTING) {
		__cfg80211_connect_result(dev, mgmt->bssid, NULL, 0, NULL, 0,
					  WLAN_STATUS_UNSPECIFIED_FAILURE,
					  false, NULL);
	}
}
EXPORT_SYMBOL(__cfg80211_send_deauth);

void cfg80211_send_deauth(struct net_device *dev, const u8 *buf, size_t len)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	wdev_lock(wdev);
	__cfg80211_send_deauth(dev, buf, len);
	wdev_unlock(wdev);
}
EXPORT_SYMBOL(cfg80211_send_deauth);

void __cfg80211_send_disassoc(struct net_device *dev,
				     const u8 *buf, size_t len)
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

	ASSERT_WDEV_LOCK(wdev);

	nl80211_send_disassoc(rdev, dev, buf, len, GFP_KERNEL);

	if (wdev->sme_state != CFG80211_SME_CONNECTED)
		return;

	if (wdev->current_bss &&
	    memcmp(wdev->current_bss->pub.bssid, bssid, ETH_ALEN) == 0) {
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

	from_ap = memcmp(mgmt->sa, dev->dev_addr, ETH_ALEN) != 0;
	__cfg80211_disconnected(dev, NULL, 0, reason_code, from_ap);
}
EXPORT_SYMBOL(__cfg80211_send_disassoc);

void cfg80211_send_disassoc(struct net_device *dev, const u8 *buf, size_t len)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	wdev_lock(wdev);
	__cfg80211_send_disassoc(dev, buf, len);
	wdev_unlock(wdev);
}
EXPORT_SYMBOL(cfg80211_send_disassoc);

static void __cfg80211_auth_remove(struct wireless_dev *wdev, const u8 *addr)
{
	int i;
	bool done = false;

	ASSERT_WDEV_LOCK(wdev);

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

void __cfg80211_auth_canceled(struct net_device *dev, const u8 *addr)
{
	__cfg80211_auth_remove(dev->ieee80211_ptr, addr);
}
EXPORT_SYMBOL(__cfg80211_auth_canceled);

void cfg80211_send_auth_timeout(struct net_device *dev, const u8 *addr)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	wdev_lock(wdev);

	nl80211_send_auth_timeout(rdev, dev, addr, GFP_KERNEL);
	if (wdev->sme_state == CFG80211_SME_CONNECTING)
		__cfg80211_connect_result(dev, addr, NULL, 0, NULL, 0,
					  WLAN_STATUS_UNSPECIFIED_FAILURE,
					  false, NULL);

	__cfg80211_auth_remove(wdev, addr);

	wdev_unlock(wdev);
}
EXPORT_SYMBOL(cfg80211_send_auth_timeout);

void cfg80211_send_assoc_timeout(struct net_device *dev, const u8 *addr)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	int i;
	bool done = false;

	wdev_lock(wdev);

	nl80211_send_assoc_timeout(rdev, dev, addr, GFP_KERNEL);
	if (wdev->sme_state == CFG80211_SME_CONNECTING)
		__cfg80211_connect_result(dev, addr, NULL, 0, NULL, 0,
					  WLAN_STATUS_UNSPECIFIED_FAILURE,
					  false, NULL);

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

	wdev_unlock(wdev);
}
EXPORT_SYMBOL(cfg80211_send_assoc_timeout);

void cfg80211_michael_mic_failure(struct net_device *dev, const u8 *addr,
				  enum nl80211_key_type key_type, int key_id,
				  const u8 *tsc, gfp_t gfp)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
#ifdef CONFIG_CFG80211_WEXT
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
int __cfg80211_mlme_auth(struct cfg80211_registered_device *rdev,
			 struct net_device *dev,
			 struct ieee80211_channel *chan,
			 enum nl80211_auth_type auth_type,
			 const u8 *bssid,
			 const u8 *ssid, int ssid_len,
			 const u8 *ie, int ie_len,
			 const u8 *key, int key_len, int key_idx)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_auth_request req;
	struct cfg80211_internal_bss *bss;
	int i, err, slot = -1, nfree = 0;

	ASSERT_WDEV_LOCK(wdev);

	if (auth_type == NL80211_AUTHTYPE_SHARED_KEY)
		if (!key || !key_len || key_idx < 0 || key_idx > 4)
			return -EINVAL;

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
	req.key = key;
	req.key_len = key_len;
	req.key_idx = key_idx;
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

int cfg80211_mlme_auth(struct cfg80211_registered_device *rdev,
		       struct net_device *dev, struct ieee80211_channel *chan,
		       enum nl80211_auth_type auth_type, const u8 *bssid,
		       const u8 *ssid, int ssid_len,
		       const u8 *ie, int ie_len,
		       const u8 *key, int key_len, int key_idx)
{
	int err;

	wdev_lock(dev->ieee80211_ptr);
	err = __cfg80211_mlme_auth(rdev, dev, chan, auth_type, bssid,
				   ssid, ssid_len, ie, ie_len,
				   key, key_len, key_idx);
	wdev_unlock(dev->ieee80211_ptr);

	return err;
}

int __cfg80211_mlme_assoc(struct cfg80211_registered_device *rdev,
			  struct net_device *dev,
			  struct ieee80211_channel *chan,
			  const u8 *bssid, const u8 *prev_bssid,
			  const u8 *ssid, int ssid_len,
			  const u8 *ie, int ie_len, bool use_mfp,
			  struct cfg80211_crypto_settings *crypt)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_assoc_request req;
	struct cfg80211_internal_bss *bss;
	int i, err, slot = -1;
	bool was_connected = false;

	ASSERT_WDEV_LOCK(wdev);

	memset(&req, 0, sizeof(req));

	if (wdev->current_bss && prev_bssid &&
	    memcmp(wdev->current_bss->pub.bssid, prev_bssid, ETH_ALEN) == 0) {
		/*
		 * Trying to reassociate: Allow this to proceed and let the old
		 * association to be dropped when the new one is completed.
		 */
		if (wdev->sme_state == CFG80211_SME_CONNECTED) {
			was_connected = true;
			wdev->sme_state = CFG80211_SME_CONNECTING;
		}
	} else if (wdev->current_bss)
		return -EALREADY;

	req.ie = ie;
	req.ie_len = ie_len;
	memcpy(&req.crypto, crypt, sizeof(req.crypto));
	req.use_mfp = use_mfp;
	req.prev_bssid = prev_bssid;
	req.bss = cfg80211_get_bss(&rdev->wiphy, chan, bssid, ssid, ssid_len,
				   WLAN_CAPABILITY_ESS, WLAN_CAPABILITY_ESS);
	if (!req.bss) {
		if (was_connected)
			wdev->sme_state = CFG80211_SME_CONNECTED;
		return -ENOENT;
	}

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
	if (err && was_connected)
		wdev->sme_state = CFG80211_SME_CONNECTED;
	/* still a reference in wdev->auth_bsses[slot] */
	cfg80211_put_bss(req.bss);
	return err;
}

int cfg80211_mlme_assoc(struct cfg80211_registered_device *rdev,
			struct net_device *dev,
			struct ieee80211_channel *chan,
			const u8 *bssid, const u8 *prev_bssid,
			const u8 *ssid, int ssid_len,
			const u8 *ie, int ie_len, bool use_mfp,
			struct cfg80211_crypto_settings *crypt)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_mlme_assoc(rdev, dev, chan, bssid, prev_bssid,
				    ssid, ssid_len, ie, ie_len, use_mfp, crypt);
	wdev_unlock(wdev);

	return err;
}

int __cfg80211_mlme_deauth(struct cfg80211_registered_device *rdev,
			   struct net_device *dev, const u8 *bssid,
			   const u8 *ie, int ie_len, u16 reason)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_deauth_request req;
	int i;

	ASSERT_WDEV_LOCK(wdev);

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

	return rdev->ops->deauth(&rdev->wiphy, dev, &req, wdev);
}

int cfg80211_mlme_deauth(struct cfg80211_registered_device *rdev,
			 struct net_device *dev, const u8 *bssid,
			 const u8 *ie, int ie_len, u16 reason)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_mlme_deauth(rdev, dev, bssid, ie, ie_len, reason);
	wdev_unlock(wdev);

	return err;
}

static int __cfg80211_mlme_disassoc(struct cfg80211_registered_device *rdev,
				    struct net_device *dev, const u8 *bssid,
				    const u8 *ie, int ie_len, u16 reason)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_disassoc_request req;

	ASSERT_WDEV_LOCK(wdev);

	if (wdev->sme_state != CFG80211_SME_CONNECTED)
		return -ENOTCONN;

	if (WARN_ON(!wdev->current_bss))
		return -ENOTCONN;

	memset(&req, 0, sizeof(req));
	req.reason_code = reason;
	req.ie = ie;
	req.ie_len = ie_len;
	if (memcmp(wdev->current_bss->pub.bssid, bssid, ETH_ALEN) == 0)
		req.bss = &wdev->current_bss->pub;
	else
		return -ENOTCONN;

	return rdev->ops->disassoc(&rdev->wiphy, dev, &req, wdev);
}

int cfg80211_mlme_disassoc(struct cfg80211_registered_device *rdev,
			   struct net_device *dev, const u8 *bssid,
			   const u8 *ie, int ie_len, u16 reason)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_mlme_disassoc(rdev, dev, bssid, ie, ie_len, reason);
	wdev_unlock(wdev);

	return err;
}

void cfg80211_mlme_down(struct cfg80211_registered_device *rdev,
			struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_deauth_request req;
	int i;

	ASSERT_WDEV_LOCK(wdev);

	if (!rdev->ops->deauth)
		return;

	memset(&req, 0, sizeof(req));
	req.reason_code = WLAN_REASON_DEAUTH_LEAVING;
	req.ie = NULL;
	req.ie_len = 0;

	if (wdev->current_bss) {
		req.bss = &wdev->current_bss->pub;
		rdev->ops->deauth(&rdev->wiphy, dev, &req, wdev);
		if (wdev->current_bss) {
			cfg80211_unhold_bss(wdev->current_bss);
			cfg80211_put_bss(&wdev->current_bss->pub);
			wdev->current_bss = NULL;
		}
	}

	for (i = 0; i < MAX_AUTH_BSSES; i++) {
		if (wdev->auth_bsses[i]) {
			req.bss = &wdev->auth_bsses[i]->pub;
			rdev->ops->deauth(&rdev->wiphy, dev, &req, wdev);
			if (wdev->auth_bsses[i]) {
				cfg80211_unhold_bss(wdev->auth_bsses[i]);
				cfg80211_put_bss(&wdev->auth_bsses[i]->pub);
				wdev->auth_bsses[i] = NULL;
			}
		}
		if (wdev->authtry_bsses[i]) {
			req.bss = &wdev->authtry_bsses[i]->pub;
			rdev->ops->deauth(&rdev->wiphy, dev, &req, wdev);
			if (wdev->authtry_bsses[i]) {
				cfg80211_unhold_bss(wdev->authtry_bsses[i]);
				cfg80211_put_bss(&wdev->authtry_bsses[i]->pub);
				wdev->authtry_bsses[i] = NULL;
			}
		}
	}
}

void cfg80211_ready_on_channel(struct net_device *dev, u64 cookie,
			       struct ieee80211_channel *chan,
			       enum nl80211_channel_type channel_type,
			       unsigned int duration, gfp_t gfp)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	nl80211_send_remain_on_channel(rdev, dev, cookie, chan, channel_type,
				       duration, gfp);
}
EXPORT_SYMBOL(cfg80211_ready_on_channel);

void cfg80211_remain_on_channel_expired(struct net_device *dev,
					u64 cookie,
					struct ieee80211_channel *chan,
					enum nl80211_channel_type channel_type,
					gfp_t gfp)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	nl80211_send_remain_on_channel_cancel(rdev, dev, cookie, chan,
					      channel_type, gfp);
}
EXPORT_SYMBOL(cfg80211_remain_on_channel_expired);

void cfg80211_new_sta(struct net_device *dev, const u8 *mac_addr,
		      struct station_info *sinfo, gfp_t gfp)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	nl80211_send_sta_event(rdev, dev, mac_addr, sinfo, gfp);
}
EXPORT_SYMBOL(cfg80211_new_sta);

struct cfg80211_action_registration {
	struct list_head list;

	u32 nlpid;

	int match_len;

	u8 match[];
};

int cfg80211_mlme_register_action(struct wireless_dev *wdev, u32 snd_pid,
				  const u8 *match_data, int match_len)
{
	struct cfg80211_action_registration *reg, *nreg;
	int err = 0;

	nreg = kzalloc(sizeof(*reg) + match_len, GFP_KERNEL);
	if (!nreg)
		return -ENOMEM;

	spin_lock_bh(&wdev->action_registrations_lock);

	list_for_each_entry(reg, &wdev->action_registrations, list) {
		int mlen = min(match_len, reg->match_len);

		if (memcmp(reg->match, match_data, mlen) == 0) {
			err = -EALREADY;
			break;
		}
	}

	if (err) {
		kfree(nreg);
		goto out;
	}

	memcpy(nreg->match, match_data, match_len);
	nreg->match_len = match_len;
	nreg->nlpid = snd_pid;
	list_add(&nreg->list, &wdev->action_registrations);

 out:
	spin_unlock_bh(&wdev->action_registrations_lock);
	return err;
}

void cfg80211_mlme_unregister_actions(struct wireless_dev *wdev, u32 nlpid)
{
	struct cfg80211_action_registration *reg, *tmp;

	spin_lock_bh(&wdev->action_registrations_lock);

	list_for_each_entry_safe(reg, tmp, &wdev->action_registrations, list) {
		if (reg->nlpid == nlpid) {
			list_del(&reg->list);
			kfree(reg);
		}
	}

	spin_unlock_bh(&wdev->action_registrations_lock);
}

void cfg80211_mlme_purge_actions(struct wireless_dev *wdev)
{
	struct cfg80211_action_registration *reg, *tmp;

	spin_lock_bh(&wdev->action_registrations_lock);

	list_for_each_entry_safe(reg, tmp, &wdev->action_registrations, list) {
		list_del(&reg->list);
		kfree(reg);
	}

	spin_unlock_bh(&wdev->action_registrations_lock);
}

int cfg80211_mlme_action(struct cfg80211_registered_device *rdev,
			 struct net_device *dev,
			 struct ieee80211_channel *chan,
			 enum nl80211_channel_type channel_type,
			 const u8 *buf, size_t len, u64 *cookie)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	const struct ieee80211_mgmt *mgmt;

	if (rdev->ops->action == NULL)
		return -EOPNOTSUPP;
	if (len < 24 + 1)
		return -EINVAL;

	mgmt = (const struct ieee80211_mgmt *) buf;
	if (!ieee80211_is_action(mgmt->frame_control))
		return -EINVAL;
	if (mgmt->u.action.category != WLAN_CATEGORY_PUBLIC) {
		/* Verify that we are associated with the destination AP */
		if (!wdev->current_bss ||
		    memcmp(wdev->current_bss->pub.bssid, mgmt->bssid,
			   ETH_ALEN) != 0 ||
		    memcmp(wdev->current_bss->pub.bssid, mgmt->da,
			   ETH_ALEN) != 0)
			return -ENOTCONN;
	}

	if (memcmp(mgmt->sa, dev->dev_addr, ETH_ALEN) != 0)
		return -EINVAL;

	/* Transmit the Action frame as requested by user space */
	return rdev->ops->action(&rdev->wiphy, dev, chan, channel_type,
				 buf, len, cookie);
}

bool cfg80211_rx_action(struct net_device *dev, int freq, const u8 *buf,
			size_t len, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct cfg80211_action_registration *reg;
	const u8 *action_data;
	int action_data_len;
	bool result = false;

	/* frame length - min size excluding category */
	action_data_len = len - (IEEE80211_MIN_ACTION_SIZE - 1);

	/* action data starts with category */
	action_data = buf + IEEE80211_MIN_ACTION_SIZE - 1;

	spin_lock_bh(&wdev->action_registrations_lock);

	list_for_each_entry(reg, &wdev->action_registrations, list) {
		if (reg->match_len > action_data_len)
			continue;

		if (memcmp(reg->match, action_data, reg->match_len))
			continue;

		/* found match! */

		/* Indicate the received Action frame to user space */
		if (nl80211_send_action(rdev, dev, reg->nlpid, freq,
					buf, len, gfp))
			continue;

		result = true;
		break;
	}

	spin_unlock_bh(&wdev->action_registrations_lock);

	return result;
}
EXPORT_SYMBOL(cfg80211_rx_action);

void cfg80211_action_tx_status(struct net_device *dev, u64 cookie,
			       const u8 *buf, size_t len, bool ack, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	/* Indicate TX status of the Action frame to user space */
	nl80211_send_action_tx_status(rdev, dev, cookie, buf, len, ack, gfp);
}
EXPORT_SYMBOL(cfg80211_action_tx_status);
