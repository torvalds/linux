#include <linux/utsname.h>
#include <net/cfg80211.h>
#include "ethtool.h"

static void cfg80211_get_drvinfo(struct net_device *dev,
					struct ethtool_drvinfo *info)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	strlcpy(info->driver, wiphy_dev(wdev->wiphy)->driver->name,
		sizeof(info->driver));

	strlcpy(info->version, init_utsname()->release, sizeof(info->version));

	strlcpy(info->fw_version, "N/A", sizeof(info->fw_version));

	strlcpy(info->bus_info, dev_name(wiphy_dev(wdev->wiphy)),
		sizeof(info->bus_info));
}

const struct ethtool_ops cfg80211_ethtool_ops = {
	.get_drvinfo = cfg80211_get_drvinfo,
	.get_link = ethtool_op_get_link,
};
