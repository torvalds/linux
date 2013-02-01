/*
 * cfg80211 scan result handling
 *
 * Copyright 2008 Johannes Berg <johannes@sipsolutions.net>
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/nl80211.h>
#include <linux/etherdevice.h>
#include <net/arp.h>
#include <net/cfg80211.h>
#include <net/cfg80211-wext.h>
#include <net/iw_handler.h>
#include "core.h"
#include "nl80211.h"
#include "wext-compat.h"
#include "rdev-ops.h"

#define IEEE80211_SCAN_RESULT_EXPIRE	(30 * HZ)

static void bss_release(struct kref *ref)
{
	struct cfg80211_bss_ies *ies;
	struct cfg80211_internal_bss *bss;

	bss = container_of(ref, struct cfg80211_internal_bss, ref);

	if (WARN_ON(atomic_read(&bss->hold)))
		return;

	ies = (void *)rcu_access_pointer(bss->pub.beacon_ies);
	if (ies)
		kfree_rcu(ies, rcu_head);
	ies = (void *)rcu_access_pointer(bss->pub.proberesp_ies);
	if (ies)
		kfree_rcu(ies, rcu_head);

	kfree(bss);
}

static inline void bss_ref_get(struct cfg80211_internal_bss *bss)
{
	kref_get(&bss->ref);
}

static inline void bss_ref_put(struct cfg80211_internal_bss *bss)
{
	kref_put(&bss->ref, bss_release);
}

static void __cfg80211_unlink_bss(struct cfg80211_registered_device *dev,
				  struct cfg80211_internal_bss *bss)
{
	lockdep_assert_held(&dev->bss_lock);

	list_del_init(&bss->list);
	rb_erase(&bss->rbn, &dev->bss_tree);
	bss_ref_put(bss);
}

static void __cfg80211_bss_expire(struct cfg80211_registered_device *dev,
				  unsigned long expire_time)
{
	struct cfg80211_internal_bss *bss, *tmp;
	bool expired = false;

	lockdep_assert_held(&dev->bss_lock);

	list_for_each_entry_safe(bss, tmp, &dev->bss_list, list) {
		if (atomic_read(&bss->hold))
			continue;
		if (!time_after(expire_time, bss->ts))
			continue;

		__cfg80211_unlink_bss(dev, bss);
		expired = true;
	}

	if (expired)
		dev->bss_generation++;
}

void ___cfg80211_scan_done(struct cfg80211_registered_device *rdev, bool leak)
{
	struct cfg80211_scan_request *request;
	struct wireless_dev *wdev;
#ifdef CONFIG_CFG80211_WEXT
	union iwreq_data wrqu;
#endif

	ASSERT_RDEV_LOCK(rdev);

	request = rdev->scan_req;

	if (!request)
		return;

	wdev = request->wdev;

	/*
	 * This must be before sending the other events!
	 * Otherwise, wpa_supplicant gets completely confused with
	 * wext events.
	 */
	if (wdev->netdev)
		cfg80211_sme_scan_done(wdev->netdev);

	if (request->aborted) {
		nl80211_send_scan_aborted(rdev, wdev);
	} else {
		if (request->flags & NL80211_SCAN_FLAG_FLUSH) {
			/* flush entries from previous scans */
			spin_lock_bh(&rdev->bss_lock);
			__cfg80211_bss_expire(rdev, request->scan_start);
			spin_unlock_bh(&rdev->bss_lock);
		}
		nl80211_send_scan_done(rdev, wdev);
	}

#ifdef CONFIG_CFG80211_WEXT
	if (wdev->netdev && !request->aborted) {
		memset(&wrqu, 0, sizeof(wrqu));

		wireless_send_event(wdev->netdev, SIOCGIWSCAN, &wrqu, NULL);
	}
#endif

	if (wdev->netdev)
		dev_put(wdev->netdev);

	rdev->scan_req = NULL;

	/*
	 * OK. If this is invoked with "leak" then we can't
	 * free this ... but we've cleaned it up anyway. The
	 * driver failed to call the scan_done callback, so
	 * all bets are off, it might still be trying to use
	 * the scan request or not ... if it accesses the dev
	 * in there (it shouldn't anyway) then it may crash.
	 */
	if (!leak)
		kfree(request);
}

void __cfg80211_scan_done(struct work_struct *wk)
{
	struct cfg80211_registered_device *rdev;

	rdev = container_of(wk, struct cfg80211_registered_device,
			    scan_done_wk);

	cfg80211_lock_rdev(rdev);
	___cfg80211_scan_done(rdev, false);
	cfg80211_unlock_rdev(rdev);
}

void cfg80211_scan_done(struct cfg80211_scan_request *request, bool aborted)
{
	trace_cfg80211_scan_done(request, aborted);
	WARN_ON(request != wiphy_to_dev(request->wiphy)->scan_req);

	request->aborted = aborted;
	queue_work(cfg80211_wq, &wiphy_to_dev(request->wiphy)->scan_done_wk);
}
EXPORT_SYMBOL(cfg80211_scan_done);

void __cfg80211_sched_scan_results(struct work_struct *wk)
{
	struct cfg80211_registered_device *rdev;
	struct cfg80211_sched_scan_request *request;

	rdev = container_of(wk, struct cfg80211_registered_device,
			    sched_scan_results_wk);

	request = rdev->sched_scan_req;

	mutex_lock(&rdev->sched_scan_mtx);

	/* we don't have sched_scan_req anymore if the scan is stopping */
	if (request) {
		if (request->flags & NL80211_SCAN_FLAG_FLUSH) {
			/* flush entries from previous scans */
			spin_lock_bh(&rdev->bss_lock);
			__cfg80211_bss_expire(rdev, request->scan_start);
			spin_unlock_bh(&rdev->bss_lock);
			request->scan_start =
				jiffies + msecs_to_jiffies(request->interval);
		}
		nl80211_send_sched_scan_results(rdev, request->dev);
	}

	mutex_unlock(&rdev->sched_scan_mtx);
}

void cfg80211_sched_scan_results(struct wiphy *wiphy)
{
	trace_cfg80211_sched_scan_results(wiphy);
	/* ignore if we're not scanning */
	if (wiphy_to_dev(wiphy)->sched_scan_req)
		queue_work(cfg80211_wq,
			   &wiphy_to_dev(wiphy)->sched_scan_results_wk);
}
EXPORT_SYMBOL(cfg80211_sched_scan_results);

void cfg80211_sched_scan_stopped(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	trace_cfg80211_sched_scan_stopped(wiphy);

	mutex_lock(&rdev->sched_scan_mtx);
	__cfg80211_stop_sched_scan(rdev, true);
	mutex_unlock(&rdev->sched_scan_mtx);
}
EXPORT_SYMBOL(cfg80211_sched_scan_stopped);

int __cfg80211_stop_sched_scan(struct cfg80211_registered_device *rdev,
			       bool driver_initiated)
{
	struct net_device *dev;

	lockdep_assert_held(&rdev->sched_scan_mtx);

	if (!rdev->sched_scan_req)
		return -ENOENT;

	dev = rdev->sched_scan_req->dev;

	if (!driver_initiated) {
		int err = rdev_sched_scan_stop(rdev, dev);
		if (err)
			return err;
	}

	nl80211_send_sched_scan(rdev, dev, NL80211_CMD_SCHED_SCAN_STOPPED);

	kfree(rdev->sched_scan_req);
	rdev->sched_scan_req = NULL;

	return 0;
}

void cfg80211_bss_age(struct cfg80211_registered_device *dev,
                      unsigned long age_secs)
{
	struct cfg80211_internal_bss *bss;
	unsigned long age_jiffies = msecs_to_jiffies(age_secs * MSEC_PER_SEC);

	spin_lock_bh(&dev->bss_lock);
	list_for_each_entry(bss, &dev->bss_list, list)
		bss->ts -= age_jiffies;
	spin_unlock_bh(&dev->bss_lock);
}

void cfg80211_bss_expire(struct cfg80211_registered_device *dev)
{
	__cfg80211_bss_expire(dev, jiffies - IEEE80211_SCAN_RESULT_EXPIRE);
}

const u8 *cfg80211_find_ie(u8 eid, const u8 *ies, int len)
{
	while (len > 2 && ies[0] != eid) {
		len -= ies[1] + 2;
		ies += ies[1] + 2;
	}
	if (len < 2)
		return NULL;
	if (len < 2 + ies[1])
		return NULL;
	return ies;
}
EXPORT_SYMBOL(cfg80211_find_ie);

const u8 *cfg80211_find_vendor_ie(unsigned int oui, u8 oui_type,
				  const u8 *ies, int len)
{
	struct ieee80211_vendor_ie *ie;
	const u8 *pos = ies, *end = ies + len;
	int ie_oui;

	while (pos < end) {
		pos = cfg80211_find_ie(WLAN_EID_VENDOR_SPECIFIC, pos,
				       end - pos);
		if (!pos)
			return NULL;

		if (end - pos < sizeof(*ie))
			return NULL;

		ie = (struct ieee80211_vendor_ie *)pos;
		ie_oui = ie->oui[0] << 16 | ie->oui[1] << 8 | ie->oui[2];
		if (ie_oui == oui && ie->oui_type == oui_type)
			return pos;

		pos += 2 + ie->len;
	}
	return NULL;
}
EXPORT_SYMBOL(cfg80211_find_vendor_ie);

static bool is_bss(struct cfg80211_bss *a, const u8 *bssid,
		   const u8 *ssid, size_t ssid_len)
{
	const struct cfg80211_bss_ies *ies;
	const u8 *ssidie;

	if (bssid && !ether_addr_equal(a->bssid, bssid))
		return false;

	if (!ssid)
		return true;

	ies = rcu_access_pointer(a->ies);
	if (!ies)
		return false;
	ssidie = cfg80211_find_ie(WLAN_EID_SSID, ies->data, ies->len);
	if (!ssidie)
		return false;
	if (ssidie[1] != ssid_len)
		return false;
	return memcmp(ssidie + 2, ssid, ssid_len) == 0;
}

/**
 * enum bss_compare_mode - BSS compare mode
 * @BSS_CMP_REGULAR: regular compare mode (for insertion and normal find)
 * @BSS_CMP_HIDE_ZLEN: find hidden SSID with zero-length mode
 * @BSS_CMP_HIDE_NUL: find hidden SSID with NUL-ed out mode
 */
enum bss_compare_mode {
	BSS_CMP_REGULAR,
	BSS_CMP_HIDE_ZLEN,
	BSS_CMP_HIDE_NUL,
};

static int cmp_bss(struct cfg80211_bss *a,
		   struct cfg80211_bss *b,
		   enum bss_compare_mode mode)
{
	const struct cfg80211_bss_ies *a_ies, *b_ies;
	const u8 *ie1 = NULL;
	const u8 *ie2 = NULL;
	int i, r;

	if (a->channel != b->channel)
		return b->channel->center_freq - a->channel->center_freq;

	a_ies = rcu_access_pointer(a->ies);
	if (!a_ies)
		return -1;
	b_ies = rcu_access_pointer(b->ies);
	if (!b_ies)
		return 1;

	if (WLAN_CAPABILITY_IS_STA_BSS(a->capability))
		ie1 = cfg80211_find_ie(WLAN_EID_MESH_ID,
				       a_ies->data, a_ies->len);
	if (WLAN_CAPABILITY_IS_STA_BSS(b->capability))
		ie2 = cfg80211_find_ie(WLAN_EID_MESH_ID,
				       b_ies->data, b_ies->len);
	if (ie1 && ie2) {
		int mesh_id_cmp;

		if (ie1[1] == ie2[1])
			mesh_id_cmp = memcmp(ie1 + 2, ie2 + 2, ie1[1]);
		else
			mesh_id_cmp = ie2[1] - ie1[1];

		ie1 = cfg80211_find_ie(WLAN_EID_MESH_CONFIG,
				       a_ies->data, a_ies->len);
		ie2 = cfg80211_find_ie(WLAN_EID_MESH_CONFIG,
				       b_ies->data, b_ies->len);
		if (ie1 && ie2) {
			if (mesh_id_cmp)
				return mesh_id_cmp;
			if (ie1[1] != ie2[1])
				return ie2[1] - ie1[1];
			return memcmp(ie1 + 2, ie2 + 2, ie1[1]);
		}
	}

	/*
	 * we can't use compare_ether_addr here since we need a < > operator.
	 * The binary return value of compare_ether_addr isn't enough
	 */
	r = memcmp(a->bssid, b->bssid, sizeof(a->bssid));
	if (r)
		return r;

	ie1 = cfg80211_find_ie(WLAN_EID_SSID, a_ies->data, a_ies->len);
	ie2 = cfg80211_find_ie(WLAN_EID_SSID, b_ies->data, b_ies->len);

	if (!ie1 && !ie2)
		return 0;

	/*
	 * Note that with "hide_ssid", the function returns a match if
	 * the already-present BSS ("b") is a hidden SSID beacon for
	 * the new BSS ("a").
	 */

	/* sort missing IE before (left of) present IE */
	if (!ie1)
		return -1;
	if (!ie2)
		return 1;

	switch (mode) {
	case BSS_CMP_HIDE_ZLEN:
		/*
		 * In ZLEN mode we assume the BSS entry we're
		 * looking for has a zero-length SSID. So if
		 * the one we're looking at right now has that,
		 * return 0. Otherwise, return the difference
		 * in length, but since we're looking for the
		 * 0-length it's really equivalent to returning
		 * the length of the one we're looking at.
		 *
		 * No content comparison is needed as we assume
		 * the content length is zero.
		 */
		return ie2[1];
	case BSS_CMP_REGULAR:
	default:
		/* sort by length first, then by contents */
		if (ie1[1] != ie2[1])
			return ie2[1] - ie1[1];
		return memcmp(ie1 + 2, ie2 + 2, ie1[1]);
	case BSS_CMP_HIDE_NUL:
		if (ie1[1] != ie2[1])
			return ie2[1] - ie1[1];
		/* this is equivalent to memcmp(zeroes, ie2 + 2, len) */
		for (i = 0; i < ie2[1]; i++)
			if (ie2[i + 2])
				return -1;
		return 0;
	}
}

struct cfg80211_bss *cfg80211_get_bss(struct wiphy *wiphy,
				      struct ieee80211_channel *channel,
				      const u8 *bssid,
				      const u8 *ssid, size_t ssid_len,
				      u16 capa_mask, u16 capa_val)
{
	struct cfg80211_registered_device *dev = wiphy_to_dev(wiphy);
	struct cfg80211_internal_bss *bss, *res = NULL;
	unsigned long now = jiffies;

	trace_cfg80211_get_bss(wiphy, channel, bssid, ssid, ssid_len, capa_mask,
			       capa_val);

	spin_lock_bh(&dev->bss_lock);

	list_for_each_entry(bss, &dev->bss_list, list) {
		if ((bss->pub.capability & capa_mask) != capa_val)
			continue;
		if (channel && bss->pub.channel != channel)
			continue;
		/* Don't get expired BSS structs */
		if (time_after(now, bss->ts + IEEE80211_SCAN_RESULT_EXPIRE) &&
		    !atomic_read(&bss->hold))
			continue;
		if (is_bss(&bss->pub, bssid, ssid, ssid_len)) {
			res = bss;
			bss_ref_get(res);
			break;
		}
	}

	spin_unlock_bh(&dev->bss_lock);
	if (!res)
		return NULL;
	trace_cfg80211_return_bss(&res->pub);
	return &res->pub;
}
EXPORT_SYMBOL(cfg80211_get_bss);

static void rb_insert_bss(struct cfg80211_registered_device *dev,
			  struct cfg80211_internal_bss *bss)
{
	struct rb_node **p = &dev->bss_tree.rb_node;
	struct rb_node *parent = NULL;
	struct cfg80211_internal_bss *tbss;
	int cmp;

	while (*p) {
		parent = *p;
		tbss = rb_entry(parent, struct cfg80211_internal_bss, rbn);

		cmp = cmp_bss(&bss->pub, &tbss->pub, BSS_CMP_REGULAR);

		if (WARN_ON(!cmp)) {
			/* will sort of leak this BSS */
			return;
		}

		if (cmp < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&bss->rbn, parent, p);
	rb_insert_color(&bss->rbn, &dev->bss_tree);
}

static struct cfg80211_internal_bss *
rb_find_bss(struct cfg80211_registered_device *dev,
	    struct cfg80211_internal_bss *res,
	    enum bss_compare_mode mode)
{
	struct rb_node *n = dev->bss_tree.rb_node;
	struct cfg80211_internal_bss *bss;
	int r;

	while (n) {
		bss = rb_entry(n, struct cfg80211_internal_bss, rbn);
		r = cmp_bss(&res->pub, &bss->pub, mode);

		if (r == 0)
			return bss;
		else if (r < 0)
			n = n->rb_left;
		else
			n = n->rb_right;
	}

	return NULL;
}

static void
copy_hidden_ies(struct cfg80211_internal_bss *res,
		struct cfg80211_internal_bss *hidden)
{
	const struct cfg80211_bss_ies *ies;

	if (rcu_access_pointer(res->pub.beacon_ies))
		return;

	ies = rcu_access_pointer(hidden->pub.beacon_ies);
	if (WARN_ON(!ies))
		return;

	ies = kmemdup(ies, sizeof(*ies) + ies->len, GFP_ATOMIC);
	if (unlikely(!ies))
		return;
	rcu_assign_pointer(res->pub.beacon_ies, ies);
}

static struct cfg80211_internal_bss *
cfg80211_bss_update(struct cfg80211_registered_device *dev,
		    struct cfg80211_internal_bss *tmp)
{
	struct cfg80211_internal_bss *found = NULL;

	if (WARN_ON(!tmp->pub.channel))
		return NULL;

	tmp->ts = jiffies;

	spin_lock_bh(&dev->bss_lock);

	if (WARN_ON(!rcu_access_pointer(tmp->pub.ies))) {
		spin_unlock_bh(&dev->bss_lock);
		return NULL;
	}

	found = rb_find_bss(dev, tmp, BSS_CMP_REGULAR);

	if (found) {
		found->pub.beacon_interval = tmp->pub.beacon_interval;
		found->pub.tsf = tmp->pub.tsf;
		found->pub.signal = tmp->pub.signal;
		found->pub.capability = tmp->pub.capability;
		found->ts = tmp->ts;

		/* Update IEs */
		if (rcu_access_pointer(tmp->pub.proberesp_ies)) {
			const struct cfg80211_bss_ies *old;

			old = rcu_access_pointer(found->pub.proberesp_ies);

			rcu_assign_pointer(found->pub.proberesp_ies,
					   tmp->pub.proberesp_ies);
			/* Override possible earlier Beacon frame IEs */
			rcu_assign_pointer(found->pub.ies,
					   tmp->pub.proberesp_ies);
			if (old)
				kfree_rcu((struct cfg80211_bss_ies *)old,
					  rcu_head);
		} else if (rcu_access_pointer(tmp->pub.beacon_ies)) {
			const struct cfg80211_bss_ies *old;

			old = rcu_access_pointer(found->pub.beacon_ies);

			rcu_assign_pointer(found->pub.beacon_ies,
					   tmp->pub.beacon_ies);

			/* Override IEs if they were from a beacon before */
			if (old == rcu_access_pointer(found->pub.ies))
				rcu_assign_pointer(found->pub.ies,
						   tmp->pub.beacon_ies);

			if (old)
				kfree_rcu((struct cfg80211_bss_ies *)old,
					  rcu_head);
		}
	} else {
		struct cfg80211_internal_bss *new;
		struct cfg80211_internal_bss *hidden;
		struct cfg80211_bss_ies *ies;

		/* First check if the beacon is a probe response from
		 * a hidden bss. If so, copy beacon ies (with nullified
		 * ssid) into the probe response bss entry (with real ssid).
		 * It is required basically for PSM implementation
		 * (probe responses do not contain tim ie) */

		/* TODO: The code is not trying to update existing probe
		 * response bss entries when beacon ies are
		 * getting changed. */
		hidden = rb_find_bss(dev, tmp, BSS_CMP_HIDE_ZLEN);
		if (hidden) {
			copy_hidden_ies(tmp, hidden);
		} else {
			hidden = rb_find_bss(dev, tmp, BSS_CMP_HIDE_NUL);
			if (hidden)
				copy_hidden_ies(tmp, hidden);
		}

		/*
		 * create a copy -- the "res" variable that is passed in
		 * is allocated on the stack since it's not needed in the
		 * more common case of an update
		 */
		new = kzalloc(sizeof(*new) + dev->wiphy.bss_priv_size,
			      GFP_ATOMIC);
		if (!new) {
			ies = (void *)rcu_dereference(tmp->pub.beacon_ies);
			if (ies)
				kfree_rcu(ies, rcu_head);
			ies = (void *)rcu_dereference(tmp->pub.proberesp_ies);
			if (ies)
				kfree_rcu(ies, rcu_head);
			spin_unlock_bh(&dev->bss_lock);
			return NULL;
		}
		memcpy(new, tmp, sizeof(*new));
		kref_init(&new->ref);
		list_add_tail(&new->list, &dev->bss_list);
		rb_insert_bss(dev, new);
		found = new;
	}

	dev->bss_generation++;
	spin_unlock_bh(&dev->bss_lock);

	bss_ref_get(found);
	return found;
}

static struct ieee80211_channel *
cfg80211_get_bss_channel(struct wiphy *wiphy, const u8 *ie, size_t ielen,
			 struct ieee80211_channel *channel)
{
	const u8 *tmp;
	u32 freq;
	int channel_number = -1;

	tmp = cfg80211_find_ie(WLAN_EID_DS_PARAMS, ie, ielen);
	if (tmp && tmp[1] == 1) {
		channel_number = tmp[2];
	} else {
		tmp = cfg80211_find_ie(WLAN_EID_HT_OPERATION, ie, ielen);
		if (tmp && tmp[1] >= sizeof(struct ieee80211_ht_operation)) {
			struct ieee80211_ht_operation *htop = (void *)(tmp + 2);

			channel_number = htop->primary_chan;
		}
	}

	if (channel_number < 0)
		return channel;

	freq = ieee80211_channel_to_frequency(channel_number, channel->band);
	channel = ieee80211_get_channel(wiphy, freq);
	if (!channel)
		return NULL;
	if (channel->flags & IEEE80211_CHAN_DISABLED)
		return NULL;
	return channel;
}

struct cfg80211_bss*
cfg80211_inform_bss(struct wiphy *wiphy,
		    struct ieee80211_channel *channel,
		    const u8 *bssid, u64 tsf, u16 capability,
		    u16 beacon_interval, const u8 *ie, size_t ielen,
		    s32 signal, gfp_t gfp)
{
	struct cfg80211_bss_ies *ies;
	struct cfg80211_internal_bss tmp = {}, *res;

	if (WARN_ON(!wiphy))
		return NULL;

	if (WARN_ON(wiphy->signal_type == CFG80211_SIGNAL_TYPE_UNSPEC &&
			(signal < 0 || signal > 100)))
		return NULL;

	channel = cfg80211_get_bss_channel(wiphy, ie, ielen, channel);
	if (!channel)
		return NULL;

	memcpy(tmp.pub.bssid, bssid, ETH_ALEN);
	tmp.pub.channel = channel;
	tmp.pub.signal = signal;
	tmp.pub.tsf = tsf;
	tmp.pub.beacon_interval = beacon_interval;
	tmp.pub.capability = capability;
	/*
	 * Since we do not know here whether the IEs are from a Beacon or Probe
	 * Response frame, we need to pick one of the options and only use it
	 * with the driver that does not provide the full Beacon/Probe Response
	 * frame. Use Beacon frame pointer to avoid indicating that this should
	 * override the IEs pointer should we have received an earlier
	 * indication of Probe Response data.
	 */
	ies = kmalloc(sizeof(*ies) + ielen, gfp);
	if (!ies)
		return NULL;
	ies->len = ielen;
	memcpy(ies->data, ie, ielen);

	rcu_assign_pointer(tmp.pub.beacon_ies, ies);
	rcu_assign_pointer(tmp.pub.ies, ies);

	res = cfg80211_bss_update(wiphy_to_dev(wiphy), &tmp);
	if (!res)
		return NULL;

	if (res->pub.capability & WLAN_CAPABILITY_ESS)
		regulatory_hint_found_beacon(wiphy, channel, gfp);

	trace_cfg80211_return_bss(&res->pub);
	/* cfg80211_bss_update gives us a referenced result */
	return &res->pub;
}
EXPORT_SYMBOL(cfg80211_inform_bss);

struct cfg80211_bss *
cfg80211_inform_bss_frame(struct wiphy *wiphy,
			  struct ieee80211_channel *channel,
			  struct ieee80211_mgmt *mgmt, size_t len,
			  s32 signal, gfp_t gfp)
{
	struct cfg80211_internal_bss tmp = {}, *res;
	struct cfg80211_bss_ies *ies;
	size_t ielen = len - offsetof(struct ieee80211_mgmt,
				      u.probe_resp.variable);

	BUILD_BUG_ON(offsetof(struct ieee80211_mgmt, u.probe_resp.variable) !=
			offsetof(struct ieee80211_mgmt, u.beacon.variable));

	trace_cfg80211_inform_bss_frame(wiphy, channel, mgmt, len, signal);

	if (WARN_ON(!mgmt))
		return NULL;

	if (WARN_ON(!wiphy))
		return NULL;

	if (WARN_ON(wiphy->signal_type == CFG80211_SIGNAL_TYPE_UNSPEC &&
		    (signal < 0 || signal > 100)))
		return NULL;

	if (WARN_ON(len < offsetof(struct ieee80211_mgmt, u.probe_resp.variable)))
		return NULL;

	channel = cfg80211_get_bss_channel(wiphy, mgmt->u.beacon.variable,
					   ielen, channel);
	if (!channel)
		return NULL;

	ies = kmalloc(sizeof(*ies) + ielen, gfp);
	if (!ies)
		return NULL;
	ies->len = ielen;
	memcpy(ies->data, mgmt->u.probe_resp.variable, ielen);

	if (ieee80211_is_probe_resp(mgmt->frame_control))
		rcu_assign_pointer(tmp.pub.proberesp_ies, ies);
	else
		rcu_assign_pointer(tmp.pub.beacon_ies, ies);
	rcu_assign_pointer(tmp.pub.ies, ies);
	
	memcpy(tmp.pub.bssid, mgmt->bssid, ETH_ALEN);
	tmp.pub.channel = channel;
	tmp.pub.signal = signal;
	tmp.pub.tsf = le64_to_cpu(mgmt->u.probe_resp.timestamp);
	tmp.pub.beacon_interval = le16_to_cpu(mgmt->u.probe_resp.beacon_int);
	tmp.pub.capability = le16_to_cpu(mgmt->u.probe_resp.capab_info);

	res = cfg80211_bss_update(wiphy_to_dev(wiphy), &tmp);
	if (!res)
		return NULL;

	if (res->pub.capability & WLAN_CAPABILITY_ESS)
		regulatory_hint_found_beacon(wiphy, channel, gfp);

	trace_cfg80211_return_bss(&res->pub);
	/* cfg80211_bss_update gives us a referenced result */
	return &res->pub;
}
EXPORT_SYMBOL(cfg80211_inform_bss_frame);

void cfg80211_ref_bss(struct wiphy *wiphy, struct cfg80211_bss *pub)
{
	struct cfg80211_internal_bss *bss;

	if (!pub)
		return;

	bss = container_of(pub, struct cfg80211_internal_bss, pub);
	bss_ref_get(bss);
}
EXPORT_SYMBOL(cfg80211_ref_bss);

void cfg80211_put_bss(struct wiphy *wiphy, struct cfg80211_bss *pub)
{
	struct cfg80211_internal_bss *bss;

	if (!pub)
		return;

	bss = container_of(pub, struct cfg80211_internal_bss, pub);
	bss_ref_put(bss);
}
EXPORT_SYMBOL(cfg80211_put_bss);

void cfg80211_unlink_bss(struct wiphy *wiphy, struct cfg80211_bss *pub)
{
	struct cfg80211_registered_device *dev = wiphy_to_dev(wiphy);
	struct cfg80211_internal_bss *bss;

	if (WARN_ON(!pub))
		return;

	bss = container_of(pub, struct cfg80211_internal_bss, pub);

	spin_lock_bh(&dev->bss_lock);
	if (!list_empty(&bss->list)) {
		__cfg80211_unlink_bss(dev, bss);
		dev->bss_generation++;
	}
	spin_unlock_bh(&dev->bss_lock);
}
EXPORT_SYMBOL(cfg80211_unlink_bss);

#ifdef CONFIG_CFG80211_WEXT
int cfg80211_wext_siwscan(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct cfg80211_registered_device *rdev;
	struct wiphy *wiphy;
	struct iw_scan_req *wreq = NULL;
	struct cfg80211_scan_request *creq = NULL;
	int i, err, n_channels = 0;
	enum ieee80211_band band;

	if (!netif_running(dev))
		return -ENETDOWN;

	if (wrqu->data.length == sizeof(struct iw_scan_req))
		wreq = (struct iw_scan_req *)extra;

	rdev = cfg80211_get_dev_from_ifindex(dev_net(dev), dev->ifindex);

	if (IS_ERR(rdev))
		return PTR_ERR(rdev);

	if (rdev->scan_req) {
		err = -EBUSY;
		goto out;
	}

	wiphy = &rdev->wiphy;

	/* Determine number of channels, needed to allocate creq */
	if (wreq && wreq->num_channels)
		n_channels = wreq->num_channels;
	else {
		for (band = 0; band < IEEE80211_NUM_BANDS; band++)
			if (wiphy->bands[band])
				n_channels += wiphy->bands[band]->n_channels;
	}

	creq = kzalloc(sizeof(*creq) + sizeof(struct cfg80211_ssid) +
		       n_channels * sizeof(void *),
		       GFP_ATOMIC);
	if (!creq) {
		err = -ENOMEM;
		goto out;
	}

	creq->wiphy = wiphy;
	creq->wdev = dev->ieee80211_ptr;
	/* SSIDs come after channels */
	creq->ssids = (void *)&creq->channels[n_channels];
	creq->n_channels = n_channels;
	creq->n_ssids = 1;
	creq->scan_start = jiffies;

	/* translate "Scan on frequencies" request */
	i = 0;
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		int j;

		if (!wiphy->bands[band])
			continue;

		for (j = 0; j < wiphy->bands[band]->n_channels; j++) {
			/* ignore disabled channels */
			if (wiphy->bands[band]->channels[j].flags &
						IEEE80211_CHAN_DISABLED)
				continue;

			/* If we have a wireless request structure and the
			 * wireless request specifies frequencies, then search
			 * for the matching hardware channel.
			 */
			if (wreq && wreq->num_channels) {
				int k;
				int wiphy_freq = wiphy->bands[band]->channels[j].center_freq;
				for (k = 0; k < wreq->num_channels; k++) {
					int wext_freq = cfg80211_wext_freq(wiphy, &wreq->channel_list[k]);
					if (wext_freq == wiphy_freq)
						goto wext_freq_found;
				}
				goto wext_freq_not_found;
			}

		wext_freq_found:
			creq->channels[i] = &wiphy->bands[band]->channels[j];
			i++;
		wext_freq_not_found: ;
		}
	}
	/* No channels found? */
	if (!i) {
		err = -EINVAL;
		goto out;
	}

	/* Set real number of channels specified in creq->channels[] */
	creq->n_channels = i;

	/* translate "Scan for SSID" request */
	if (wreq) {
		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			if (wreq->essid_len > IEEE80211_MAX_SSID_LEN) {
				err = -EINVAL;
				goto out;
			}
			memcpy(creq->ssids[0].ssid, wreq->essid, wreq->essid_len);
			creq->ssids[0].ssid_len = wreq->essid_len;
		}
		if (wreq->scan_type == IW_SCAN_TYPE_PASSIVE)
			creq->n_ssids = 0;
	}

	for (i = 0; i < IEEE80211_NUM_BANDS; i++)
		if (wiphy->bands[i])
			creq->rates[i] = (1 << wiphy->bands[i]->n_bitrates) - 1;

	rdev->scan_req = creq;
	err = rdev_scan(rdev, creq);
	if (err) {
		rdev->scan_req = NULL;
		/* creq will be freed below */
	} else {
		nl80211_send_scan_start(rdev, dev->ieee80211_ptr);
		/* creq now owned by driver */
		creq = NULL;
		dev_hold(dev);
	}
 out:
	kfree(creq);
	cfg80211_unlock_rdev(rdev);
	return err;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_siwscan);

static void ieee80211_scan_add_ies(struct iw_request_info *info,
				   const struct cfg80211_bss_ies *ies,
				   char **current_ev, char *end_buf)
{
	const u8 *pos, *end, *next;
	struct iw_event iwe;

	if (!ies)
		return;

	/*
	 * If needed, fragment the IEs buffer (at IE boundaries) into short
	 * enough fragments to fit into IW_GENERIC_IE_MAX octet messages.
	 */
	pos = ies->data;
	end = pos + ies->len;

	while (end - pos > IW_GENERIC_IE_MAX) {
		next = pos + 2 + pos[1];
		while (next + 2 + next[1] - pos < IW_GENERIC_IE_MAX)
			next = next + 2 + next[1];

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = next - pos;
		*current_ev = iwe_stream_add_point(info, *current_ev,
						   end_buf, &iwe,
						   (void *)pos);

		pos = next;
	}

	if (end > pos) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = end - pos;
		*current_ev = iwe_stream_add_point(info, *current_ev,
						   end_buf, &iwe,
						   (void *)pos);
	}
}

static inline unsigned int elapsed_jiffies_msecs(unsigned long start)
{
	unsigned long end = jiffies;

	if (end >= start)
		return jiffies_to_msecs(end - start);

	return jiffies_to_msecs(end + (MAX_JIFFY_OFFSET - start) + 1);
}

static char *
ieee80211_bss(struct wiphy *wiphy, struct iw_request_info *info,
	      struct cfg80211_internal_bss *bss, char *current_ev,
	      char *end_buf)
{
	const struct cfg80211_bss_ies *ies;
	struct iw_event iwe;
	const u8 *ie;
	u8 *buf, *cfg, *p;
	int rem, i, sig;
	bool ismesh = false;

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, bss->pub.bssid, ETH_ALEN);
	current_ev = iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					  IW_EV_ADDR_LEN);

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = ieee80211_frequency_to_channel(bss->pub.channel->center_freq);
	iwe.u.freq.e = 0;
	current_ev = iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					  IW_EV_FREQ_LEN);

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = bss->pub.channel->center_freq;
	iwe.u.freq.e = 6;
	current_ev = iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					  IW_EV_FREQ_LEN);

	if (wiphy->signal_type != CFG80211_SIGNAL_TYPE_NONE) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVQUAL;
		iwe.u.qual.updated = IW_QUAL_LEVEL_UPDATED |
				     IW_QUAL_NOISE_INVALID |
				     IW_QUAL_QUAL_UPDATED;
		switch (wiphy->signal_type) {
		case CFG80211_SIGNAL_TYPE_MBM:
			sig = bss->pub.signal / 100;
			iwe.u.qual.level = sig;
			iwe.u.qual.updated |= IW_QUAL_DBM;
			if (sig < -110)		/* rather bad */
				sig = -110;
			else if (sig > -40)	/* perfect */
				sig = -40;
			/* will give a range of 0 .. 70 */
			iwe.u.qual.qual = sig + 110;
			break;
		case CFG80211_SIGNAL_TYPE_UNSPEC:
			iwe.u.qual.level = bss->pub.signal;
			/* will give range 0 .. 100 */
			iwe.u.qual.qual = bss->pub.signal;
			break;
		default:
			/* not reached */
			break;
		}
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_QUAL_LEN);
	}

	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = SIOCGIWENCODE;
	if (bss->pub.capability & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	current_ev = iwe_stream_add_point(info, current_ev, end_buf,
					  &iwe, "");

	rcu_read_lock();
	ies = rcu_dereference(bss->pub.ies);
	if (ies) {
		rem = ies->len;
		ie = ies->data;
	} else {
		rem = 0;
		ie = NULL;
	}

	while (ies && rem >= 2) {
		/* invalid data */
		if (ie[1] > rem - 2)
			break;

		switch (ie[0]) {
		case WLAN_EID_SSID:
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = SIOCGIWESSID;
			iwe.u.data.length = ie[1];
			iwe.u.data.flags = 1;
			current_ev = iwe_stream_add_point(info, current_ev, end_buf,
							  &iwe, (u8 *)ie + 2);
			break;
		case WLAN_EID_MESH_ID:
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = SIOCGIWESSID;
			iwe.u.data.length = ie[1];
			iwe.u.data.flags = 1;
			current_ev = iwe_stream_add_point(info, current_ev, end_buf,
							  &iwe, (u8 *)ie + 2);
			break;
		case WLAN_EID_MESH_CONFIG:
			ismesh = true;
			if (ie[1] != sizeof(struct ieee80211_meshconf_ie))
				break;
			buf = kmalloc(50, GFP_ATOMIC);
			if (!buf)
				break;
			cfg = (u8 *)ie + 2;
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			sprintf(buf, "Mesh Network Path Selection Protocol ID: "
				"0x%02X", cfg[0]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			sprintf(buf, "Path Selection Metric ID: 0x%02X",
				cfg[1]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			sprintf(buf, "Congestion Control Mode ID: 0x%02X",
				cfg[2]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			sprintf(buf, "Synchronization ID: 0x%02X", cfg[3]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			sprintf(buf, "Authentication ID: 0x%02X", cfg[4]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			sprintf(buf, "Formation Info: 0x%02X", cfg[5]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			sprintf(buf, "Capabilities: 0x%02X", cfg[6]);
			iwe.u.data.length = strlen(buf);
			current_ev = iwe_stream_add_point(info, current_ev,
							  end_buf,
							  &iwe, buf);
			kfree(buf);
			break;
		case WLAN_EID_SUPP_RATES:
		case WLAN_EID_EXT_SUPP_RATES:
			/* display all supported rates in readable format */
			p = current_ev + iwe_stream_lcp_len(info);

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = SIOCGIWRATE;
			/* Those two flags are ignored... */
			iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;

			for (i = 0; i < ie[1]; i++) {
				iwe.u.bitrate.value =
					((ie[i + 2] & 0x7f) * 500000);
				p = iwe_stream_add_value(info, current_ev, p,
						end_buf, &iwe, IW_EV_PARAM_LEN);
			}
			current_ev = p;
			break;
		}
		rem -= ie[1] + 2;
		ie += ie[1] + 2;
	}

	if (bss->pub.capability & (WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_IBSS) ||
	    ismesh) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWMODE;
		if (ismesh)
			iwe.u.mode = IW_MODE_MESH;
		else if (bss->pub.capability & WLAN_CAPABILITY_ESS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;
		current_ev = iwe_stream_add_event(info, current_ev, end_buf,
						  &iwe, IW_EV_UINT_LEN);
	}

	buf = kmalloc(30, GFP_ATOMIC);
	if (buf) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		sprintf(buf, "tsf=%016llx", (unsigned long long)(bss->pub.tsf));
		iwe.u.data.length = strlen(buf);
		current_ev = iwe_stream_add_point(info, current_ev, end_buf,
						  &iwe, buf);
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		sprintf(buf, " Last beacon: %ums ago",
			elapsed_jiffies_msecs(bss->ts));
		iwe.u.data.length = strlen(buf);
		current_ev = iwe_stream_add_point(info, current_ev,
						  end_buf, &iwe, buf);
		kfree(buf);
	}

	ieee80211_scan_add_ies(info, ies, &current_ev, end_buf);
	rcu_read_unlock();

	return current_ev;
}


static int ieee80211_scan_results(struct cfg80211_registered_device *dev,
				  struct iw_request_info *info,
				  char *buf, size_t len)
{
	char *current_ev = buf;
	char *end_buf = buf + len;
	struct cfg80211_internal_bss *bss;

	spin_lock_bh(&dev->bss_lock);
	cfg80211_bss_expire(dev);

	list_for_each_entry(bss, &dev->bss_list, list) {
		if (buf + len - current_ev <= IW_EV_ADDR_LEN) {
			spin_unlock_bh(&dev->bss_lock);
			return -E2BIG;
		}
		current_ev = ieee80211_bss(&dev->wiphy, info, bss,
					   current_ev, end_buf);
	}
	spin_unlock_bh(&dev->bss_lock);
	return current_ev - buf;
}


int cfg80211_wext_giwscan(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *data, char *extra)
{
	struct cfg80211_registered_device *rdev;
	int res;

	if (!netif_running(dev))
		return -ENETDOWN;

	rdev = cfg80211_get_dev_from_ifindex(dev_net(dev), dev->ifindex);

	if (IS_ERR(rdev))
		return PTR_ERR(rdev);

	if (rdev->scan_req) {
		res = -EAGAIN;
		goto out;
	}

	res = ieee80211_scan_results(rdev, info, extra, data->length);
	data->length = 0;
	if (res >= 0) {
		data->length = res;
		res = 0;
	}

 out:
	cfg80211_unlock_rdev(rdev);
	return res;
}
EXPORT_SYMBOL_GPL(cfg80211_wext_giwscan);
#endif
