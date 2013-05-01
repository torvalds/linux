#include <linux/ieee80211.h>
#include <linux/export.h>
#include <net/cfg80211.h>
#include "nl80211.h"
#include "core.h"
#include "rdev-ops.h"


static int __cfg80211_stop_ap(struct cfg80211_registered_device *rdev,
			      struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (!rdev->ops->stop_ap)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
		return -EOPNOTSUPP;

	if (!wdev->beacon_interval)
		return -ENOENT;

	err = rdev_stop_ap(rdev, dev);
	if (!err) {
		wdev->beacon_interval = 0;
		wdev->channel = NULL;
		wdev->ssid_len = 0;
	}

	return err;
}

int cfg80211_stop_ap(struct cfg80211_registered_device *rdev,
		     struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_stop_ap(rdev, dev);
	wdev_unlock(wdev);

	return err;
}

void cfg80211_ch_switch_notify(struct net_device *dev,
			       struct cfg80211_chan_def *chandef)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	trace_cfg80211_ch_switch_notify(dev, chandef);

	wdev_lock(wdev);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_AP &&
		    wdev->iftype != NL80211_IFTYPE_P2P_GO))
		goto out;

	wdev->channel = chandef->chan;
	nl80211_ch_switch_notify(rdev, dev, chandef, GFP_KERNEL);
out:
	wdev_unlock(wdev);
	return;
}
EXPORT_SYMBOL(cfg80211_ch_switch_notify);

bool cfg80211_rx_spurious_frame(struct net_device *dev,
				const u8 *addr, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	bool ret;

	trace_cfg80211_rx_spurious_frame(dev, addr);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_AP &&
		    wdev->iftype != NL80211_IFTYPE_P2P_GO)) {
		trace_cfg80211_return_bool(false);
		return false;
	}
	ret = nl80211_unexpected_frame(dev, addr, gfp);
	trace_cfg80211_return_bool(ret);
	return ret;
}
EXPORT_SYMBOL(cfg80211_rx_spurious_frame);

bool cfg80211_rx_unexpected_4addr_frame(struct net_device *dev,
					const u8 *addr, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	bool ret;

	trace_cfg80211_rx_unexpected_4addr_frame(dev, addr);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_AP &&
		    wdev->iftype != NL80211_IFTYPE_P2P_GO &&
		    wdev->iftype != NL80211_IFTYPE_AP_VLAN)) {
		trace_cfg80211_return_bool(false);
		return false;
	}
	ret = nl80211_unexpected_4addr_frame(dev, addr, gfp);
	trace_cfg80211_return_bool(ret);
	return ret;
}
EXPORT_SYMBOL(cfg80211_rx_unexpected_4addr_frame);
