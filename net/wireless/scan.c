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

#define IEEE80211_SCAN_RESULT_EXPIRE	(15 * HZ)

void ___cfg80211_scan_done(struct cfg80211_registered_device *rdev, bool leak)
{
	struct cfg80211_scan_request *request;
	struct net_device *dev;
#ifdef CONFIG_CFG80211_WEXT
	union iwreq_data wrqu;
#endif

	ASSERT_RDEV_LOCK(rdev);

	request = rdev->scan_req;

	if (!request)
		return;

	dev = request->dev;

	/*
	 * This must be before sending the other events!
	 * Otherwise, wpa_supplicant gets completely confused with
	 * wext events.
	 */
	cfg80211_sme_scan_done(dev);

	if (request->aborted)
		nl80211_send_scan_aborted(rdev, dev);
	else
		nl80211_send_scan_done(rdev, dev);

#ifdef CONFIG_CFG80211_WEXT
	if (!request->aborted) {
		memset(&wrqu, 0, sizeof(wrqu));

		wireless_send_event(dev, SIOCGIWSCAN, &wrqu, NULL);
	}
#endif

	dev_put(dev);

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
	WARN_ON(request != wiphy_to_dev(request->wiphy)->scan_req);

	request->aborted = aborted;
	queue_work(cfg80211_wq, &wiphy_to_dev(request->wiphy)->scan_done_wk);
}
EXPORT_SYMBOL(cfg80211_scan_done);

void __cfg80211_sched_scan_results(struct work_struct *wk)
{
	struct cfg80211_registered_device *rdev;

	rdev = container_of(wk, struct cfg80211_registered_device,
			    sched_scan_results_wk);

	mutex_lock(&rdev->sched_scan_mtx);

	/* we don't have sched_scan_req anymore if the scan is stopping */
	if (rdev->sched_scan_req)
		nl80211_send_sched_scan_results(rdev,
						rdev->sched_scan_req->dev);

	mutex_unlock(&rdev->sched_scan_mtx);
}

void cfg80211_sched_scan_results(struct wiphy *wiphy)
{
	/* ignore if we're not scanning */
	if (wiphy_to_dev(wiphy)->sched_scan_req)
		queue_work(cfg80211_wq,
			   &wiphy_to_dev(wiphy)->sched_scan_results_wk);
}
EXPORT_SYMBOL(cfg80211_sched_scan_results);

void cfg80211_sched_scan_stopped(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

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
		int err = rdev->ops->sched_scan_stop(&rdev->wiphy, dev);
		if (err)
			return err;
	}

	nl80211_send_sched_scan(rdev, dev, NL80211_CMD_SCHED_SCAN_STOPPED);

	kfree(rdev->sched_scan_req);
	rdev->sched_scan_req = NULL;

	return 0;
}

static void bss_release(struct kref *ref)
{
	struct cfg80211_internal_bss *bss;

	bss = container_of(ref, struct cfg80211_internal_bss, ref);
	if (bss->pub.free_priv)
		bss->pub.free_priv(&bss->pub);

	if (bss->beacon_ies_allocated)
		kfree(bss->pub.beacon_ies);
	if (bss->proberesp_ies_allocated)
		kfree(bss->pub.proberesp_ies);

	BUG_ON(atomic_read(&bss->hold));

	kfree(bss);
}

/* must hold dev->bss_lock! */
void cfg80211_bss_age(struct cfg80211_registered_device *dev,
                      unsigned long age_secs)
{
	struct cfg80211_internal_bss *bss;
	unsigned long age_jiffies = msecs_to_jiffies(age_secs * MSEC_PER_SEC);

	list_for_each_entry(bss, &dev->bss_list, list) {
		bss->ts -= age_jiffies;
	}
}

/* must hold dev->bss_lock! */
static void __cfg80211_unlink_bss(struct cfg80211_registered_device *dev,
				  struct cfg80211_internal_bss *bss)
{
	list_del_init(&bss->list);
	rb_erase(&bss->rbn, &dev->bss_tree);
	kref_put(&bss->ref, bss_release);
}

/* must hold dev->bss_lock! */
void cfg80211_bss_expire(struct cfg80211_registered_device *dev)
{
	struct cfg80211_internal_bss *bss, *tmp;
	bool expired = false;

	list_for_each_entry_safe(bss, tmp, &dev->bss_list, list) {
		if (atomic_read(&bss->hold))
			continue;
		if (!time_after(jiffies, bss->ts + IEEE80211_SCAN_RESULT_EXPIRE))
			continue;
		__cfg80211_unlink_bss(dev, bss);
		expired = true;
	}

	if (expired)
		dev->bss_generation++;
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

static int cmp_ies(u8 num, u8 *ies1, size_t len1, u8 *ies2, size_t len2)
{
	const u8 *ie1 = cfg80211_find_ie(num, ies1, len1);
	const u8 *ie2 = cfg80211_find_ie(num, ies2, len2);

	/* equal if both missing */
	if (!ie1 && !ie2)
		return 0;
	/* sort missing IE before (left of) present IE */
	if (!ie1)
		return -1;
	if (!ie2)
		return 1;

	/* sort by length first, then by contents */
	if (ie1[1] != ie2[1])
		return ie2[1] - ie1[1];
	return memcmp(ie1 + 2, ie2 + 2, ie1[1]);
}

static bool is_bss(struct cfg80211_bss *a,
		   const u8 *bssid,
		   const u8 *ssid, size_t ssid_len)
{
	const u8 *ssidie;

	if (bssid && compare_ether_addr(a->bssid, bssid))
		return false;

	if (!ssid)
		return true;

	ssidie = cfg80211_find_ie(WLAN_EID_SSID,
				  a->information_elements,
				  a->len_information_elements);
	if (!ssidie)
		return false;
	if (ssidie[1] != ssid_len)
		return false;
	return memcmp(ssidie + 2, ssid, ssid_len) == 0;
}

static bool is_mesh_bss(struct cfg80211_bss *a)
{
	const u8 *ie;

	if (!WLAN_CAPABILITY_IS_STA_BSS(a->capability))
		return false;

	ie = cfg80211_find_ie(WLAN_EID_MESH_ID,
			      a->information_elements,
			      a->len_information_elements);
	if (!ie)
		return false;

	ie = cfg80211_find_ie(WLAN_EID_MESH_CONFIG,
			      a->information_elements,
			      a->len_information_elements);
	if (!ie)
		return false;

	return true;
}

static bool is_mesh(struct cfg80211_bss *a,
		    const u8 *meshid, size_t meshidlen,
		    const u8 *meshcfg)
{
	const u8 *ie;

	if (!WLAN_CAPABILITY_IS_STA_BSS(a->capability))
		return false;

	ie = cfg80211_find_ie(WLAN_EID_MESH_ID,
			      a->information_elements,
			      a->len_information_elements);
	if (!ie)
		return false;
	if (ie[1] != meshidlen)
		return false;
	if (memcmp(ie + 2, meshid, meshidlen))
		return false;

	ie = cfg80211_find_ie(WLAN_EID_MESH_CONFIG,
			      a->information_elements,
			      a->len_information_elements);
	if (!ie)
		return false;
	if (ie[1] != sizeof(struct ieee80211_meshconf_ie))
		return false;

	/*
	 * Ignore mesh capability (last two bytes of the IE) when
	 * comparing since that may differ between stations taking
	 * part in the same mesh.
	 */
	return memcmp(ie + 2, meshcfg,
	    sizeof(struct ieee80211_meshconf_ie) - 2) == 0;
}

static int cmp_bss_core(struct cfg80211_bss *a,
			struct cfg80211_bss *b)
{
	int r;

	if (a->channel != b->channel)
		return b->channel->center_freq - a->channel->center_freq;

	if (is_mesh_bss(a) && is_mesh_bss(b)) {
		r = cmp_ies(WLAN_EID_MESH_ID,
			    a->information_elements,
			    a->len_information_elements,
			    b->information_elements,
			    b->len_information_elements);
		if (r)
			return r;
		return cmp_ies(WLAN_EID_MESH_CONFIG,
			       a->information_elements,
			       a->len_information_elements,
			       b->information_elements,
			       b->len_information_elements);
	}

	return memcmp(a->bssid, b->bssid, ETH_ALEN);
}

static int cmp_bss(struct cfg80211_bss *a,
		   struct cfg80211_bss *b)
{
	int r;

	r = cmp_bss_core(a, b);
	if (r)
		return r;

	return cmp_ies(WLAN_EID_SSID,
		       a->information_elements,
		       a->len_information_elements,
		       b->information_elements,
		       b->len_information_elements);
}

static int cmp_hidden_bss(struct cfg80211_bss *a,
		   struct cfg80211_bss *b)
{
	const u8 *ie1;
	const u8 *ie2;
	int i;
	int r;

	r = cmp_bss_core(a, b);
	if (r)
		return r;

	ie1 = cfg80211_find_ie(WLAN_EID_SSID,
			a->information_elements,
			a->len_information_elements);
	ie2 = cfg80211_find_ie(WLAN_EID_SSID,
			b->information_elements,
			b->len_information_elements);

	/* Key comparator must use same algorithm in any rb-tree
	 * search function (order is important), otherwise ordering
	 * of items in the tree is broken and search gives incorrect
	 * results. This code uses same order as cmp_ies() does. */

	/* sort missing IE before (left of) present IE */
	if (!ie1)
		return -1;
	if (!ie2)
		return 1;

	/* zero-size SSID is used as an indication of the hidden bss */
	if (!ie2[1])
		return 0;

	/* sort by length first, then by contents */
	if (ie1[1] != ie2[1])
		return ie2[1] - ie1[1];

	/* zeroed SSID ie is another indication of a hidden bss */
	for (i = 0; i < ie2[1]; i++)
		if (ie2[i + 2])
			return -1;

	return 0;
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
			kref_get(&res->ref);
			break;
		}
	}

	spin_unlock_bh(&dev->bss_lock);
	if (!res)
		return NULL;
	return &res->pub;
}
EXPORT_SYMBOL(cfg80211_get_bss);

struct cfg80211_bss *cfg80211_get_mesh(struct wiphy *wiphy,
				       struct ieee80211_channel *channel,
				       const u8 *meshid, size_t meshidlen,
				       const u8 *meshcfg)
{
	struct cfg80211_registered_device *dev = wiphy_to_dev(wiphy);
	struct cfg80211_internal_bss *bss, *res = NULL;

	spin_lock_bh(&dev->bss_lock);

	list_for_each_entry(bss, &dev->bss_list, list) {
		if (channel && bss->pub.channel != channel)
			continue;
		if (is_mesh(&bss->pub, meshid, meshidlen, meshcfg)) {
			res = bss;
			kref_get(&res->ref);
			break;
		}
	}

	spin_unlock_bh(&dev->bss_lock);
	if (!res)
		return NULL;
	return &res->pub;
}
EXPORT_SYMBOL(cfg80211_get_mesh);


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

		cmp = cmp_bss(&bss->pub, &tbss->pub);

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
	    struct cfg80211_internal_bss *res)
{
	struct rb_node *n = dev->bss_tree.rb_node;
	struct cfg80211_internal_bss *bss;
	int r;

	while (n) {
		bss = rb_entry(n, struct cfg80211_internal_bss, rbn);
		r = cmp_bss(&res->pub, &bss->pub);

		if (r == 0)
			return bss;
		else if (r < 0)
			n = n->rb_left;
		else
			n = n->rb_right;
	}

	return NULL;
}

static struct cfg80211_internal_bss *
rb_find_hidden_bss(struct cfg80211_registered_device *dev,
	    struct cfg80211_internal_bss *res)
{
	struct rb_node *n = dev->bss_tree.rb_node;
	struct cfg80211_internal_bss *bss;
	int r;

	while (n) {
		bss = rb_entry(n, struct cfg80211_internal_bss, rbn);
		r = cmp_hidden_bss(&res->pub, &bss->pub);

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
	if (unlikely(res->pub.beacon_ies))
		return;
	if (WARN_ON(!hidden->pub.beacon_ies))
		return;

	res->pub.beacon_ies = kmalloc(hidden->pub.len_beacon_ies, GFP_ATOMIC);
	if (unlikely(!res->pub.beacon_ies))
		return;

	res->beacon_ies_allocated = true;
	res->pub.len_beacon_ies = hidden->pub.len_beacon_ies;
	memcpy(res->pub.beacon_ies, hidden->pub.beacon_ies,
			res->pub.len_beacon_ies);
}

static struct cfg80211_internal_bss *
cfg80211_bss_update(struct cfg80211_registered_device *dev,
		    struct cfg80211_internal_bss *res)
{
	struct cfg80211_internal_bss *found = NULL;

	/*
	 * The reference to "res" is donated to this function.
	 */

	if (WARN_ON(!res->pub.channel)) {
		kref_put(&res->ref, bss_release);
		return NULL;
	}

	res->ts = jiffies;

	spin_lock_bh(&dev->bss_lock);

	found = rb_find_bss(dev, res);

	if (found) {
		found->pub.beacon_interval = res->pub.beacon_interval;
		found->pub.tsf = res->pub.tsf;
		found->pub.signal = res->pub.signal;
		found->pub.capability = res->pub.capability;
		found->ts = res->ts;

		/* Update IEs */
		if (res->pub.proberesp_ies) {
			size_t used = dev->wiphy.bss_priv_size + sizeof(*res);
			size_t ielen = res->pub.len_proberesp_ies;

			if (found->pub.proberesp_ies &&
			    !found->proberesp_ies_allocated &&
			    ksize(found) >= used + ielen) {
				memcpy(found->pub.proberesp_ies,
				       res->pub.proberesp_ies, ielen);
				found->pub.len_proberesp_ies = ielen;
			} else {
				u8 *ies = found->pub.proberesp_ies;

				if (found->proberesp_ies_allocated)
					ies = krealloc(ies, ielen, GFP_ATOMIC);
				else
					ies = kmalloc(ielen, GFP_ATOMIC);

				if (ies) {
					memcpy(ies, res->pub.proberesp_ies,
					       ielen);
					found->proberesp_ies_allocated = true;
					found->pub.proberesp_ies = ies;
					found->pub.len_proberesp_ies = ielen;
				}
			}

			/* Override possible earlier Beacon frame IEs */
			found->pub.information_elements =
				found->pub.proberesp_ies;
			found->pub.len_information_elements =
				found->pub.len_proberesp_ies;
		}
		if (res->pub.beacon_ies) {
			size_t used = dev->wiphy.bss_priv_size + sizeof(*res);
			size_t ielen = res->pub.len_beacon_ies;
			bool information_elements_is_beacon_ies =
				(found->pub.information_elements ==
				 found->pub.beacon_ies);

			if (found->pub.beacon_ies &&
			    !found->beacon_ies_allocated &&
			    ksize(found) >= used + ielen) {
				memcpy(found->pub.beacon_ies,
				       res->pub.beacon_ies, ielen);
				found->pub.len_beacon_ies = ielen;
			} else {
				u8 *ies = found->pub.beacon_ies;

				if (found->beacon_ies_allocated)
					ies = krealloc(ies, ielen, GFP_ATOMIC);
				else
					ies = kmalloc(ielen, GFP_ATOMIC);

				if (ies) {
					memcpy(ies, res->pub.beacon_ies,
					       ielen);
					found->beacon_ies_allocated = true;
					found->pub.beacon_ies = ies;
					found->pub.len_beacon_ies = ielen;
				}
			}

			/* Override IEs if they were from a beacon before */
			if (information_elements_is_beacon_ies) {
				found->pub.information_elements =
					found->pub.beacon_ies;
				found->pub.len_information_elements =
					found->pub.len_beacon_ies;
			}
		}

		kref_put(&res->ref, bss_release);
	} else {
		struct cfg80211_internal_bss *hidden;

		/* First check if the beacon is a probe response from
		 * a hidden bss. If so, copy beacon ies (with nullified
		 * ssid) into the probe response bss entry (with real ssid).
		 * It is required basically for PSM implementation
		 * (probe responses do not contain tim ie) */

		/* TODO: The code is not trying to update existing probe
		 * response bss entries when beacon ies are
		 * getting changed. */
		hidden = rb_find_hidden_bss(dev, res);
		if (hidden)
			copy_hidden_ies(res, hidden);

		/* this "consumes" the reference */
		list_add_tail(&res->list, &dev->bss_list);
		rb_insert_bss(dev, res);
		found = res;
	}

	dev->bss_generation++;
	spin_unlock_bh(&dev->bss_lock);

	kref_get(&found->ref);
	return found;
}

struct cfg80211_bss*
cfg80211_inform_bss(struct wiphy *wiphy,
		    struct ieee80211_channel *channel,
		    const u8 *bssid, u64 tsf, u16 capability,
		    u16 beacon_interval, const u8 *ie, size_t ielen,
		    s32 signal, gfp_t gfp)
{
	struct cfg80211_internal_bss *res;
	size_t privsz;

	if (WARN_ON(!wiphy))
		return NULL;

	privsz = wiphy->bss_priv_size;

	if (WARN_ON(wiphy->signal_type == CFG80211_SIGNAL_TYPE_UNSPEC &&
			(signal < 0 || signal > 100)))
		return NULL;

	res = kzalloc(sizeof(*res) + privsz + ielen, gfp);
	if (!res)
		return NULL;

	memcpy(res->pub.bssid, bssid, ETH_ALEN);
	res->pub.channel = channel;
	res->pub.signal = signal;
	res->pub.tsf = tsf;
	res->pub.beacon_interval = beacon_interval;
	res->pub.capability = capability;
	/*
	 * Since we do not know here whether the IEs are from a Beacon or Probe
	 * Response frame, we need to pick one of the options and only use it
	 * with the driver that does not provide the full Beacon/Probe Response
	 * frame. Use Beacon frame pointer to avoid indicating that this should
	 * override the information_elements pointer should we have received an
	 * earlier indication of Probe Response data.
	 *
	 * The initial buffer for the IEs is allocated with the BSS entry and
	 * is located after the private area.
	 */
	res->pub.beacon_ies = (u8 *)res + sizeof(*res) + privsz;
	memcpy(res->pub.beacon_ies, ie, ielen);
	res->pub.len_beacon_ies = ielen;
	res->pub.information_elements = res->pub.beacon_ies;
	res->pub.len_information_elements = res->pub.len_beacon_ies;

	kref_init(&res->ref);

	res = cfg80211_bss_update(wiphy_to_dev(wiphy), res);
	if (!res)
		return NULL;

	if (res->pub.capability & WLAN_CAPABILITY_ESS)
		regulatory_hint_found_beacon(wiphy, channel, gfp);

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
	struct cfg80211_internal_bss *res;
	size_t ielen = len - offsetof(struct ieee80211_mgmt,
				      u.probe_resp.variable);
	size_t privsz;

	if (WARN_ON(!mgmt))
		return NULL;

	if (WARN_ON(!wiphy))
		return NULL;

	if (WARN_ON(wiphy->signal_type == CFG80211_SIGNAL_TYPE_UNSPEC &&
	            (signal < 0 || signal > 100)))
		return NULL;

	if (WARN_ON(len < offsetof(struct ieee80211_mgmt, u.probe_resp.variable)))
		return NULL;

	privsz = wiphy->bss_priv_size;

	res = kzalloc(sizeof(*res) + privsz + ielen, gfp);
	if (!res)
		return NULL;

	memcpy(res->pub.bssid, mgmt->bssid, ETH_ALEN);
	res->pub.channel = channel;
	res->pub.signal = signal;
	res->pub.tsf = le64_to_cpu(mgmt->u.probe_resp.timestamp);
	res->pub.beacon_interval = le16_to_cpu(mgmt->u.probe_resp.beacon_int);
	res->pub.capability = le16_to_cpu(mgmt->u.probe_resp.capab_info);
	/*
	 * The initial buffer for the IEs is allocated with the BSS entry and
	 * is located after the private area.
	 */
	if (ieee80211_is_probe_resp(mgmt->frame_control)) {
		res->pub.proberesp_ies = (u8 *) res + sizeof(*res) + privsz;
		memcpy(res->pub.proberesp_ies, mgmt->u.probe_resp.variable,
		       ielen);
		res->pub.len_proberesp_ies = ielen;
		res->pub.information_elements = res->pub.proberesp_ies;
		res->pub.len_information_elements = res->pub.len_proberesp_ies;
	} else {
		res->pub.beacon_ies = (u8 *) res + sizeof(*res) + privsz;
		memcpy(res->pub.beacon_ies, mgmt->u.beacon.variable, ielen);
		res->pub.len_beacon_ies = ielen;
		res->pub.information_elements = res->pub.beacon_ies;
		res->pub.len_information_elements = res->pub.len_beacon_ies;
	}

	kref_init(&res->ref);

	res = cfg80211_bss_update(wiphy_to_dev(wiphy), res);
	if (!res)
		return NULL;

	if (res->pub.capability & WLAN_CAPABILITY_ESS)
		regulatory_hint_found_beacon(wiphy, channel, gfp);

	/* cfg80211_bss_update gives us a referenced result */
	return &res->pub;
}
EXPORT_SYMBOL(cfg80211_inform_bss_frame);

void cfg80211_ref_bss(struct cfg80211_bss *pub)
{
	struct cfg80211_internal_bss *bss;

	if (!pub)
		return;

	bss = container_of(pub, struct cfg80211_internal_bss, pub);
	kref_get(&bss->ref);
}
EXPORT_SYMBOL(cfg80211_ref_bss);

void cfg80211_put_bss(struct cfg80211_bss *pub)
{
	struct cfg80211_internal_bss *bss;

	if (!pub)
		return;

	bss = container_of(pub, struct cfg80211_internal_bss, pub);
	kref_put(&bss->ref, bss_release);
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
	creq->dev = dev;
	/* SSIDs come after channels */
	creq->ssids = (void *)&creq->channels[n_channels];
	creq->n_channels = n_channels;
	creq->n_ssids = 1;

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
	err = rdev->ops->scan(wiphy, dev, creq);
	if (err) {
		rdev->scan_req = NULL;
		/* creq will be freed below */
	} else {
		nl80211_send_scan_start(rdev, dev);
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
				   struct cfg80211_bss *bss,
				   char **current_ev, char *end_buf)
{
	u8 *pos, *end, *next;
	struct iw_event iwe;

	if (!bss->information_elements ||
	    !bss->len_information_elements)
		return;

	/*
	 * If needed, fragment the IEs buffer (at IE boundaries) into short
	 * enough fragments to fit into IW_GENERIC_IE_MAX octet messages.
	 */
	pos = bss->information_elements;
	end = pos + bss->len_information_elements;

	while (end - pos > IW_GENERIC_IE_MAX) {
		next = pos + 2 + pos[1];
		while (next + 2 + next[1] - pos < IW_GENERIC_IE_MAX)
			next = next + 2 + next[1];

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = next - pos;
		*current_ev = iwe_stream_add_point(info, *current_ev,
						   end_buf, &iwe, pos);

		pos = next;
	}

	if (end > pos) {
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVGENIE;
		iwe.u.data.length = end - pos;
		*current_ev = iwe_stream_add_point(info, *current_ev,
						   end_buf, &iwe, pos);
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
	struct iw_event iwe;
	u8 *buf, *cfg, *p;
	u8 *ie = bss->pub.information_elements;
	int rem = bss->pub.len_information_elements, i, sig;
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

	while (rem >= 2) {
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
							  &iwe, ie + 2);
			break;
		case WLAN_EID_MESH_ID:
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = SIOCGIWESSID;
			iwe.u.data.length = ie[1];
			iwe.u.data.flags = 1;
			current_ev = iwe_stream_add_point(info, current_ev, end_buf,
							  &iwe, ie + 2);
			break;
		case WLAN_EID_MESH_CONFIG:
			ismesh = true;
			if (ie[1] != sizeof(struct ieee80211_meshconf_ie))
				break;
			buf = kmalloc(50, GFP_ATOMIC);
			if (!buf)
				break;
			cfg = ie + 2;
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

	ieee80211_scan_add_ies(info, &bss->pub, &current_ev, end_buf);

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
