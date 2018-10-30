// SPDX-License-Identifier: GPL-2.0
#include <linux/utsname.h>
#include <net/cfg80211.h>
#include "core.h"
#include "rdev-ops.h"

void cfg80211_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	strlcpy(info->driver, wiphy_dev(wdev->wiphy)->driver->name,
		sizeof(info->driver));

	strlcpy(info->version, init_utsname()->release, sizeof(info->version));

	if (wdev->wiphy->fw_version[0])
		strlcpy(info->fw_version, wdev->wiphy->fw_version,
			sizeof(info->fw_version));
	else
		strlcpy(info->fw_version, "N/A", sizeof(info->fw_version));

	strlcpy(info->bus_info, dev_name(wiphy_dev(wdev->wiphy)),
		sizeof(info->bus_info));
}
EXPORT_SYMBOL(cfg80211_get_drvinfo);
