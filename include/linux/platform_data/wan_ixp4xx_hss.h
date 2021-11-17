/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PLATFORM_DATA_WAN_IXP4XX_HSS_H
#define __PLATFORM_DATA_WAN_IXP4XX_HSS_H

#include <linux/types.h>

/* Information about built-in HSS (synchronous serial) interfaces */
struct hss_plat_info {
	int (*set_clock)(int port, unsigned int clock_type);
	int (*open)(int port, void *pdev,
		    void (*set_carrier_cb)(void *pdev, int carrier));
	void (*close)(int port, void *pdev);
	u8 txreadyq;
	u32 timer_freq;
};

#endif
