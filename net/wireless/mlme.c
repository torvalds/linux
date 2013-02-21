/*
 * cfg80211 MLME SAP interface
 *
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/nl80211.h>
#include <linux/slab.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>
#include <net/iw_handler.h>
#include "core.h"
#include "nl80211.h"
#include "rdev-ops.h"


void cfg80211_send_rx_auth(struct net_device *dev, const u8 *buf, size_t len)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	trace_cfg80211_send_rx_auth(dev);
	wdev_lock(wdev);

	nl80211_send_rx_auth(rdev, dev, buf, len, GFP_KERNEL);
	cfg80211_sme_rx_auth(dev, buf, len);

	wdev_unlock(wdev);
}
EXPORT_SYMBOL(cfg80211_send_rx_auth);

void cfg80211_send_rx_assoc(struct net_device *dev, struct cfg80211_bss *bss,
			    const u8 *buf, size_t len)
{
	u16 status_code;
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
	u8 *ie = mgmt->u.assoc_resp.variable;
	int ieoffs = offsetof(struct ieee80211_mgmt, u.assoc_resp.variable);

	trace_cfg80211_send_rx_assoc(dev, bss);
	wdev_lock(wdev);

	status_code = le16_to_cpu(mgmt->u.assoc_resp.status_code);

	/*
	 * This is a bit of a hack, we don't notify userspace of
	 * a (re-)association reply if we tried to send a reassoc
	 * and got a reject -- we only try again with an assoc
	 * frame instead of reassoc.
	 */
	if (status_code != WLAN_STATUS_SUCCESS && wdev->conn &&
	    cfg80211_sme_failed_reassoc(wdev)) {
		cfg80211_put_bss(wiphy, bss);
		goto out;
	}

	nl80211_send_rx_assoc(rdev, dev, buf, len, GFP_KERNEL);

	if (status_code != WLAN_STATUS_SUCCESS && wdev->conn) {
		cfg80211_sme_failed_assoc(wdev);
		/*
		 * do not call connect_result() now because the
		 * sme will schedule work that does it later.
		 */
		cfg80211_put_bss(wiphy, bss);
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

	/* this consumes the bss reference */
	__cfg80211_connect_result(dev, mgmt->bssid, NULL, 0, ie, len - ieoffs,
				  status_code,
				  status_code == WLAN_STATUS_SUCCESS, bss);
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
	bool was_current = false;

	trace___cfg80211_send_deauth(dev);
	ASSERT_WDEV_LOCK(wdev);

	if (wdev->current_bss &&
	    ether_addr_equal(wdev->current_bss->pub.bssid, bssid)) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(wiphy, &wdev->current_bss->pub);
		wdev->current_bss = NULL;
		was_current = true;
	}

	nl80211_send_deauth(rdev, dev, buf, len, GFP_KERNEL);

	if (wdev->sme_state == CFG80211_SME_CONNECTED && was_current) {
		u16 reason_code;
		bool from_ap;

		reason_code = le16_to_cpu(mgmt->u.deauth.reason_code);

		from_ap = !ether_addr_equal(mgmt->sa, dev->dev_addr);
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
	u16 reason_code;
	bool from_ap;

	trace___cfg80211_send_disassoc(dev);
	ASSERT_WDEV_LOCK(wdev);

	nl80211_send_disassoc(rdev, dev, buf, len, GFP_KERNEL);

	if (wdev->sme_state != CFG80211_SME_CONNECTED)
		return;

	if (wdev->current_bss &&
	    ether_addr_equal(wdev->current_bss->pub.bssid, bssid)) {
		cfg80211_sme_disassoc(dev, wdev->current_bss);
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(wiphy, &wdev->current_bss->pub);
		wdev->current_bss = NULL;
	} else
		WARN_ON(1);


	reason_code = le16_to_cpu(mgmt->u.disassoc.reason_code);

	from_ap = !ether_addr_equal(mgmt->sa, dev->dev_addr);
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

void cfg80211_send_auth_timeout(struct net_device *dev, const u8 *addr)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	trace_cfg80211_send_auth_timeout(dev, addr);
	wdev_lock(wdev);

	nl80211_send_auth_timeout(rdev, dev, addr, GFP_KERNEL);
	if (wdev->sme_state == CFG80211_SME_CONNECTING)
		__cfg80211_connect_result(dev, addr, NULL, 0, NULL, 0,
					  WLAN_STATUS_UNSPECIFIED_FAILURE,
					  false, NULL);

	wdev_unlock(wdev);
}
EXPORT_SYMBOL(cfg80211_send_auth_timeout);

void cfg80211_send_assoc_timeout(struct net_device *dev, const u8 *addr)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	trace_cfg80211_send_assoc_timeout(dev, addr);
	wdev_lock(wdev);

	nl80211_send_assoc_timeout(rdev, dev, addr, GFP_KERNEL);
	if (wdev->sme_state == CFG80211_SME_CONNECTING)
		__cfg80211_connect_result(dev, addr, NULL, 0, NULL, 0,
					  WLAN_STATUS_UNSPECIFIED_FAILURE,
					  false, NULL);

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

	trace_cfg80211_michael_mic_failure(dev, addr, key_type, key_id, tsc);
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
			 const u8 *key, int key_len, int key_idx,
			 const u8 *sae_data, int sae_data_len)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_auth_request req;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (auth_type == NL80211_AUTHTYPE_SHARED_KEY)
		if (!key || !key_len || key_idx < 0 || key_idx > 4)
			return -EINVAL;

	if (wdev->current_bss &&
	    ether_addr_equal(bssid, wdev->current_bss->pub.bssid))
		return -EALREADY;

	memset(&req, 0, sizeof(req));

	req.ie = ie;
	req.ie_len = ie_len;
	req.sae_data = sae_data;
	req.sae_data_len = sae_data_len;
	req.auth_type = auth_type;
	req.bss = cfg80211_get_bss(&rdev->wiphy, chan, bssid, ssid, ssid_len,
				   WLAN_CAPABILITY_ESS, WLAN_CAPABILITY_ESS);
	req.key = key;
	req.key_len = key_len;
	req.key_idx = key_idx;
	if (!req.bss)
		return -ENOENT;

	err = cfg80211_can_use_chan(rdev, wdev, req.bss->channel,
				    CHAN_MODE_SHARED);
	if (err)
		goto out;

	err = rdev_auth(rdev, dev, &req);

out:
	cfg80211_put_bss(&rdev->wiphy, req.bss);
	return err;
}

int cfg80211_mlme_auth(struct cfg80211_registered_device *rdev,
		       struct net_device *dev, struct ieee80211_channel *chan,
		       enum nl80211_auth_type auth_type, const u8 *bssid,
		       const u8 *ssid, int ssid_len,
		       const u8 *ie, int ie_len,
		       const u8 *key, int key_len, int key_idx,
		       const u8 *sae_data, int sae_data_len)
{
	int err;

	mutex_lock(&rdev->devlist_mtx);
	wdev_lock(dev->ieee80211_ptr);
	err = __cfg80211_mlme_auth(rdev, dev, chan, auth_type, bssid,
				   ssid, ssid_len, ie, ie_len,
				   key, key_len, key_idx,
				   sae_data, sae_data_len);
	wdev_unlock(dev->ieee80211_ptr);
	mutex_unlock(&rdev->devlist_mtx);

	return err;
}

/*  Do a logical ht_capa &= ht_capa_mask.  */
void cfg80211_oper_and_ht_capa(struct ieee80211_ht_cap *ht_capa,
			       const struct ieee80211_ht_cap *ht_capa_mask)
{
	int i;
	u8 *p1, *p2;
	if (!ht_capa_mask) {
		memset(ht_capa, 0, sizeof(*ht_capa));
		return;
	}

	p1 = (u8*)(ht_capa);
	p2 = (u8*)(ht_capa_mask);
	for (i = 0; i<sizeof(*ht_capa); i++)
		p1[i] &= p2[i];
}

/*  Do a logical ht_capa &= ht_capa_mask.  */
void cfg80211_oper_and_vht_capa(struct ieee80211_vht_cap *vht_capa,
				const struct ieee80211_vht_cap *vht_capa_mask)
{
	int i;
	u8 *p1, *p2;
	if (!vht_capa_mask) {
		memset(vht_capa, 0, sizeof(*vht_capa));
		return;
	}

	p1 = (u8*)(vht_capa);
	p2 = (u8*)(vht_capa_mask);
	for (i = 0; i < sizeof(*vht_capa); i++)
		p1[i] &= p2[i];
}

int __cfg80211_mlme_assoc(struct cfg80211_registered_device *rdev,
			  struct net_device *dev,
			  struct ieee80211_channel *chan,
			  const u8 *bssid, const u8 *prev_bssid,
			  const u8 *ssid, int ssid_len,
			  const u8 *ie, int ie_len, bool use_mfp,
			  struct cfg80211_crypto_settings *crypt,
			  u32 assoc_flags, struct ieee80211_ht_cap *ht_capa,
			  struct ieee80211_ht_cap *ht_capa_mask,
			  struct ieee80211_vht_cap *vht_capa,
			  struct ieee80211_vht_cap *vht_capa_mask)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_assoc_request req;
	int err;
	bool was_connected = false;

	ASSERT_WDEV_LOCK(wdev);

	memset(&req, 0, sizeof(req));

	if (wdev->current_bss && prev_bssid &&
	    ether_addr_equal(wdev->current_bss->pub.bssid, prev_bssid)) {
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
	req.flags = assoc_flags;
	if (ht_capa)
		memcpy(&req.ht_capa, ht_capa, sizeof(req.ht_capa));
	if (ht_capa_mask)
		memcpy(&req.ht_capa_mask, ht_capa_mask,
		       sizeof(req.ht_capa_mask));
	cfg80211_oper_and_ht_capa(&req.ht_capa_mask,
				  rdev->wiphy.ht_capa_mod_mask);
	if (vht_capa)
		memcpy(&req.vht_capa, vht_capa, sizeof(req.vht_capa));
	if (vht_capa_mask)
		memcpy(&req.vht_capa_mask, vht_capa_mask,
		       sizeof(req.vht_capa_mask));
	cfg80211_oper_and_vht_capa(&req.vht_capa_mask,
				   rdev->wiphy.vht_capa_mod_mask);

	req.bss = cfg80211_get_bss(&rdev->wiphy, chan, bssid, ssid, ssid_len,
				   WLAN_CAPABILITY_ESS, WLAN_CAPABILITY_ESS);
	if (!req.bss) {
		if (was_connected)
			wdev->sme_state = CFG80211_SME_CONNECTED;
		return -ENOENT;
	}

	err = cfg80211_can_use_chan(rdev, wdev, req.bss->channel,
				    CHAN_MODE_SHARED);
	if (err)
		goto out;

	err = rdev_assoc(rdev, dev, &req);

out:
	if (err) {
		if (was_connected)
			wdev->sme_state = CFG80211_SME_CONNECTED;
		cfg80211_put_bss(&rdev->wiphy, req.bss);
	}

	return err;
}

int cfg80211_mlme_assoc(struct cfg80211_registered_device *rdev,
			struct net_device *dev,
			struct ieee80211_channel *chan,
			const u8 *bssid, const u8 *prev_bssid,
			const u8 *ssid, int ssid_len,
			const u8 *ie, int ie_len, bool use_mfp,
			struct cfg80211_crypto_settings *crypt,
			u32 assoc_flags, struct ieee80211_ht_cap *ht_capa,
			struct ieee80211_ht_cap *ht_capa_mask,
			struct ieee80211_vht_cap *vht_capa,
			struct ieee80211_vht_cap *vht_capa_mask)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	mutex_lock(&rdev->devlist_mtx);
	wdev_lock(wdev);
	err = __cfg80211_mlme_assoc(rdev, dev, chan, bssid, prev_bssid,
				    ssid, ssid_len, ie, ie_len, use_mfp, crypt,
				    assoc_flags, ht_capa, ht_capa_mask,
				    vht_capa, vht_capa_mask);
	wdev_unlock(wdev);
	mutex_unlock(&rdev->devlist_mtx);

	return err;
}

int __cfg80211_mlme_deauth(struct cfg80211_registered_device *rdev,
			   struct net_device *dev, const u8 *bssid,
			   const u8 *ie, int ie_len, u16 reason,
			   bool local_state_change)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_deauth_request req = {
		.bssid = bssid,
		.reason_code = reason,
		.ie = ie,
		.ie_len = ie_len,
		.local_state_change = local_state_change,
	};

	ASSERT_WDEV_LOCK(wdev);

	if (local_state_change && (!wdev->current_bss ||
	    !ether_addr_equal(wdev->current_bss->pub.bssid, bssid)))
		return 0;

	return rdev_deauth(rdev, dev, &req);
}

int cfg80211_mlme_deauth(struct cfg80211_registered_device *rdev,
			 struct net_device *dev, const u8 *bssid,
			 const u8 *ie, int ie_len, u16 reason,
			 bool local_state_change)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_mlme_deauth(rdev, dev, bssid, ie, ie_len, reason,
				     local_state_change);
	wdev_unlock(wdev);

	return err;
}

static int __cfg80211_mlme_disassoc(struct cfg80211_registered_device *rdev,
				    struct net_device *dev, const u8 *bssid,
				    const u8 *ie, int ie_len, u16 reason,
				    bool local_state_change)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_disassoc_request req;

	ASSERT_WDEV_LOCK(wdev);

	if (wdev->sme_state != CFG80211_SME_CONNECTED)
		return -ENOTCONN;

	if (WARN(!wdev->current_bss, "sme_state=%d\n", wdev->sme_state))
		return -ENOTCONN;

	memset(&req, 0, sizeof(req));
	req.reason_code = reason;
	req.local_state_change = local_state_change;
	req.ie = ie;
	req.ie_len = ie_len;
	if (ether_addr_equal(wdev->current_bss->pub.bssid, bssid))
		req.bss = &wdev->current_bss->pub;
	else
		return -ENOTCONN;

	return rdev_disassoc(rdev, dev, &req);
}

int cfg80211_mlme_disassoc(struct cfg80211_registered_device *rdev,
			   struct net_device *dev, const u8 *bssid,
			   const u8 *ie, int ie_len, u16 reason,
			   bool local_state_change)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_mlme_disassoc(rdev, dev, bssid, ie, ie_len, reason,
				       local_state_change);
	wdev_unlock(wdev);

	return err;
}

void cfg80211_mlme_down(struct cfg80211_registered_device *rdev,
			struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_deauth_request req;
	u8 bssid[ETH_ALEN];

	ASSERT_WDEV_LOCK(wdev);

	if (!rdev->ops->deauth)
		return;

	memset(&req, 0, sizeof(req));
	req.reason_code = WLAN_REASON_DEAUTH_LEAVING;
	req.ie = NULL;
	req.ie_len = 0;

	if (!wdev->current_bss)
		return;

	memcpy(bssid, wdev->current_bss->pub.bssid, ETH_ALEN);
	req.bssid = bssid;
	rdev_deauth(rdev, dev, &req);

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(&rdev->wiphy, &wdev->current_bss->pub);
		wdev->current_bss = NULL;
	}
}

struct cfg80211_mgmt_registration {
	struct list_head list;

	u32 nlportid;

	int match_len;

	__le16 frame_type;

	u8 match[];
};

int cfg80211_mlme_register_mgmt(struct wireless_dev *wdev, u32 snd_portid,
				u16 frame_type, const u8 *match_data,
				int match_len)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct cfg80211_mgmt_registration *reg, *nreg;
	int err = 0;
	u16 mgmt_type;

	if (!wdev->wiphy->mgmt_stypes)
		return -EOPNOTSUPP;

	if ((frame_type & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_MGMT)
		return -EINVAL;

	if (frame_type & ~(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE))
		return -EINVAL;

	mgmt_type = (frame_type & IEEE80211_FCTL_STYPE) >> 4;
	if (!(wdev->wiphy->mgmt_stypes[wdev->iftype].rx & BIT(mgmt_type)))
		return -EINVAL;

	nreg = kzalloc(sizeof(*reg) + match_len, GFP_KERNEL);
	if (!nreg)
		return -ENOMEM;

	spin_lock_bh(&wdev->mgmt_registrations_lock);

	list_for_each_entry(reg, &wdev->mgmt_registrations, list) {
		int mlen = min(match_len, reg->match_len);

		if (frame_type != le16_to_cpu(reg->frame_type))
			continue;

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
	nreg->nlportid = snd_portid;
	nreg->frame_type = cpu_to_le16(frame_type);
	list_add(&nreg->list, &wdev->mgmt_registrations);

	if (rdev->ops->mgmt_frame_register)
		rdev_mgmt_frame_register(rdev, wdev, frame_type, true);

 out:
	spin_unlock_bh(&wdev->mgmt_registrations_lock);

	return err;
}

void cfg80211_mlme_unregister_socket(struct wireless_dev *wdev, u32 nlportid)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct cfg80211_mgmt_registration *reg, *tmp;

	spin_lock_bh(&wdev->mgmt_registrations_lock);

	list_for_each_entry_safe(reg, tmp, &wdev->mgmt_registrations, list) {
		if (reg->nlportid != nlportid)
			continue;

		if (rdev->ops->mgmt_frame_register) {
			u16 frame_type = le16_to_cpu(reg->frame_type);

			rdev_mgmt_frame_register(rdev, wdev,
						 frame_type, false);
		}

		list_del(&reg->list);
		kfree(reg);
	}

	spin_unlock_bh(&wdev->mgmt_registrations_lock);

	if (nlportid == wdev->ap_unexpected_nlportid)
		wdev->ap_unexpected_nlportid = 0;
}

void cfg80211_mlme_purge_registrations(struct wireless_dev *wdev)
{
	struct cfg80211_mgmt_registration *reg, *tmp;

	spin_lock_bh(&wdev->mgmt_registrations_lock);

	list_for_each_entry_safe(reg, tmp, &wdev->mgmt_registrations, list) {
		list_del(&reg->list);
		kfree(reg);
	}

	spin_unlock_bh(&wdev->mgmt_registrations_lock);
}

int cfg80211_mlme_mgmt_tx(struct cfg80211_registered_device *rdev,
			  struct wireless_dev *wdev,
			  struct ieee80211_channel *chan, bool offchan,
			  unsigned int wait, const u8 *buf, size_t len,
			  bool no_cck, bool dont_wait_for_ack, u64 *cookie)
{
	const struct ieee80211_mgmt *mgmt;
	u16 stype;

	if (!wdev->wiphy->mgmt_stypes)
		return -EOPNOTSUPP;

	if (!rdev->ops->mgmt_tx)
		return -EOPNOTSUPP;

	if (len < 24 + 1)
		return -EINVAL;

	mgmt = (const struct ieee80211_mgmt *) buf;

	if (!ieee80211_is_mgmt(mgmt->frame_control))
		return -EINVAL;

	stype = le16_to_cpu(mgmt->frame_control) & IEEE80211_FCTL_STYPE;
	if (!(wdev->wiphy->mgmt_stypes[wdev->iftype].tx & BIT(stype >> 4)))
		return -EINVAL;

	if (ieee80211_is_action(mgmt->frame_control) &&
	    mgmt->u.action.category != WLAN_CATEGORY_PUBLIC) {
		int err = 0;

		wdev_lock(wdev);

		switch (wdev->iftype) {
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_P2P_CLIENT:
			if (!wdev->current_bss) {
				err = -ENOTCONN;
				break;
			}

			if (!ether_addr_equal(wdev->current_bss->pub.bssid,
					      mgmt->bssid)) {
				err = -ENOTCONN;
				break;
			}

			/*
			 * check for IBSS DA must be done by driver as
			 * cfg80211 doesn't track the stations
			 */
			if (wdev->iftype == NL80211_IFTYPE_ADHOC)
				break;

			/* for station, check that DA is the AP */
			if (!ether_addr_equal(wdev->current_bss->pub.bssid,
					      mgmt->da)) {
				err = -ENOTCONN;
				break;
			}
			break;
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_P2P_GO:
		case NL80211_IFTYPE_AP_VLAN:
			if (!ether_addr_equal(mgmt->bssid, wdev_address(wdev)))
				err = -EINVAL;
			break;
		case NL80211_IFTYPE_MESH_POINT:
			if (!ether_addr_equal(mgmt->sa, mgmt->bssid)) {
				err = -EINVAL;
				break;
			}
			/*
			 * check for mesh DA must be done by driver as
			 * cfg80211 doesn't track the stations
			 */
			break;
		case NL80211_IFTYPE_P2P_DEVICE:
			/*
			 * fall through, P2P device only supports
			 * public action frames
			 */
		default:
			err = -EOPNOTSUPP;
			break;
		}
		wdev_unlock(wdev);

		if (err)
			return err;
	}

	if (!ether_addr_equal(mgmt->sa, wdev_address(wdev)))
		return -EINVAL;

	/* Transmit the Action frame as requested by user space */
	return rdev_mgmt_tx(rdev, wdev, chan, offchan,
			    wait, buf, len, no_cck, dont_wait_for_ack,
			    cookie);
}

bool cfg80211_rx_mgmt(struct wireless_dev *wdev, int freq, int sig_mbm,
		      const u8 *buf, size_t len, gfp_t gfp)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct cfg80211_mgmt_registration *reg;
	const struct ieee80211_txrx_stypes *stypes =
		&wiphy->mgmt_stypes[wdev->iftype];
	struct ieee80211_mgmt *mgmt = (void *)buf;
	const u8 *data;
	int data_len;
	bool result = false;
	__le16 ftype = mgmt->frame_control &
		cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE);
	u16 stype;

	trace_cfg80211_rx_mgmt(wdev, freq, sig_mbm);
	stype = (le16_to_cpu(mgmt->frame_control) & IEEE80211_FCTL_STYPE) >> 4;

	if (!(stypes->rx & BIT(stype))) {
		trace_cfg80211_return_bool(false);
		return false;
	}

	data = buf + ieee80211_hdrlen(mgmt->frame_control);
	data_len = len - ieee80211_hdrlen(mgmt->frame_control);

	spin_lock_bh(&wdev->mgmt_registrations_lock);

	list_for_each_entry(reg, &wdev->mgmt_registrations, list) {
		if (reg->frame_type != ftype)
			continue;

		if (reg->match_len > data_len)
			continue;

		if (memcmp(reg->match, data, reg->match_len))
			continue;

		/* found match! */

		/* Indicate the received Action frame to user space */
		if (nl80211_send_mgmt(rdev, wdev, reg->nlportid,
				      freq, sig_mbm,
				      buf, len, gfp))
			continue;

		result = true;
		break;
	}

	spin_unlock_bh(&wdev->mgmt_registrations_lock);

	trace_cfg80211_return_bool(result);
	return result;
}
EXPORT_SYMBOL(cfg80211_rx_mgmt);

void cfg80211_dfs_channels_update_work(struct work_struct *work)
{
	struct delayed_work *delayed_work;
	struct cfg80211_registered_device *rdev;
	struct cfg80211_chan_def chandef;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *c;
	struct wiphy *wiphy;
	bool check_again = false;
	unsigned long timeout, next_time = 0;
	int bandid, i;

	delayed_work = container_of(work, struct delayed_work, work);
	rdev = container_of(delayed_work, struct cfg80211_registered_device,
			    dfs_update_channels_wk);
	wiphy = &rdev->wiphy;

	mutex_lock(&cfg80211_mutex);
	for (bandid = 0; bandid < IEEE80211_NUM_BANDS; bandid++) {
		sband = wiphy->bands[bandid];
		if (!sband)
			continue;

		for (i = 0; i < sband->n_channels; i++) {
			c = &sband->channels[i];

			if (c->dfs_state != NL80211_DFS_UNAVAILABLE)
				continue;

			timeout = c->dfs_state_entered +
				  IEEE80211_DFS_MIN_NOP_TIME_MS;

			if (time_after_eq(jiffies, timeout)) {
				c->dfs_state = NL80211_DFS_USABLE;
				cfg80211_chandef_create(&chandef, c,
							NL80211_CHAN_NO_HT);

				nl80211_radar_notify(rdev, &chandef,
						     NL80211_RADAR_NOP_FINISHED,
						     NULL, GFP_ATOMIC);
				continue;
			}

			if (!check_again)
				next_time = timeout - jiffies;
			else
				next_time = min(next_time, timeout - jiffies);
			check_again = true;
		}
	}
	mutex_unlock(&cfg80211_mutex);

	/* reschedule if there are other channels waiting to be cleared again */
	if (check_again)
		queue_delayed_work(cfg80211_wq, &rdev->dfs_update_channels_wk,
				   next_time);
}


void cfg80211_radar_event(struct wiphy *wiphy,
			  struct cfg80211_chan_def *chandef,
			  gfp_t gfp)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	unsigned long timeout;

	trace_cfg80211_radar_event(wiphy, chandef);

	/* only set the chandef supplied channel to unavailable, in
	 * case the radar is detected on only one of multiple channels
	 * spanned by the chandef.
	 */
	cfg80211_set_dfs_state(wiphy, chandef, NL80211_DFS_UNAVAILABLE);

	timeout = msecs_to_jiffies(IEEE80211_DFS_MIN_NOP_TIME_MS);
	queue_delayed_work(cfg80211_wq, &rdev->dfs_update_channels_wk,
			   timeout);

	nl80211_radar_notify(rdev, chandef, NL80211_RADAR_DETECTED, NULL, gfp);
}
EXPORT_SYMBOL(cfg80211_radar_event);

void cfg80211_cac_event(struct net_device *netdev,
			enum nl80211_radar_event event, gfp_t gfp)
{
	struct wireless_dev *wdev = netdev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct cfg80211_chan_def chandef;
	unsigned long timeout;

	trace_cfg80211_cac_event(netdev, event);

	if (WARN_ON(!wdev->cac_started))
		return;

	if (WARN_ON(!wdev->channel))
		return;

	cfg80211_chandef_create(&chandef, wdev->channel, NL80211_CHAN_NO_HT);

	switch (event) {
	case NL80211_RADAR_CAC_FINISHED:
		timeout = wdev->cac_start_time +
			  msecs_to_jiffies(IEEE80211_DFS_MIN_CAC_TIME_MS);
		WARN_ON(!time_after_eq(jiffies, timeout));
		cfg80211_set_dfs_state(wiphy, &chandef, NL80211_DFS_AVAILABLE);
		break;
	case NL80211_RADAR_CAC_ABORTED:
		break;
	default:
		WARN_ON(1);
		return;
	}
	wdev->cac_started = false;

	nl80211_radar_notify(rdev, &chandef, event, netdev, gfp);
}
EXPORT_SYMBOL(cfg80211_cac_event);
