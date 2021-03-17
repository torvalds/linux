/* SPDX-License-Identifier: GPL-2.0 */
/* include/net/virt_wifi.h
 *
 * Define the extension interface for the network data simulation
 *
 * Copyright (C) 2019 Google, Inc.
 *
 * Author: lesl@google.com
 */
#ifndef __VIRT_WIFI_H
#define __VIRT_WIFI_H

struct virt_wifi_network_simulation {
	void (*notify_device_open)(struct net_device *dev);
	void (*notify_device_stop)(struct net_device *dev);
	void (*notify_scan_trigger)(struct wiphy *wiphy,
				    struct cfg80211_scan_request *request);
	int (*generate_virt_scan_result)(struct wiphy *wiphy);
};

int virt_wifi_register_network_simulation(
	    struct virt_wifi_network_simulation *ops);
int virt_wifi_unregister_network_simulation(void);
#endif

