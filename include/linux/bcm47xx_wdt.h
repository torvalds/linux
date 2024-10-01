/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_BCM47XX_WDT_H_
#define LINUX_BCM47XX_WDT_H_

#include <linux/timer.h>
#include <linux/types.h>
#include <linux/watchdog.h>


struct bcm47xx_wdt {
	u32 (*timer_set)(struct bcm47xx_wdt *, u32);
	u32 (*timer_set_ms)(struct bcm47xx_wdt *, u32);
	u32 max_timer_ms;

	void *driver_data;

	struct watchdog_device wdd;

	struct timer_list soft_timer;
	atomic_t soft_ticks;
};

static inline void *bcm47xx_wdt_get_drvdata(struct bcm47xx_wdt *wdt)
{
	return wdt->driver_data;
}
#endif /* LINUX_BCM47XX_WDT_H_ */
