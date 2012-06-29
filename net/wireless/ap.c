#include <linux/ieee80211.h>
#include <linux/export.h>
#include <net/cfg80211.h>
#include "nl80211.h"
#include "core.h"


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

	err = rdev->ops->stop_ap(&rdev->wiphy, dev);
	if (!err)
		wdev->beacon_interval = 0;

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
