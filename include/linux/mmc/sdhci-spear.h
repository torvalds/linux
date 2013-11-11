/*
 * include/linux/mmc/sdhci-spear.h
 *
 * SDHCI declarations specific to ST SPEAr platform
 *
 * Copyright (C) 2010 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef LINUX_MMC_SDHCI_SPEAR_H
#define LINUX_MMC_SDHCI_SPEAR_H

#include <linux/platform_device.h>
/*
 * struct sdhci_plat_data: spear sdhci platform data structure
 *
 * @card_power_gpio: gpio pin for enabling/disabling power to sdhci socket
 * @power_active_high: if set, enable power to sdhci socket by setting
 *			card_power_gpio
 * @power_always_enb: If set, then enable power on probe, otherwise enable only
 *			on card insertion and disable on card removal.
 * card_int_gpio: gpio pin used for card detection
 */
struct sdhci_plat_data {
	int card_power_gpio;
	int power_active_high;
	int power_always_enb;
	int card_int_gpio;
};

/* This function is used to set platform_data field of pdev->dev */
static inline void
sdhci_set_plat_data(struct platform_device *pdev, struct sdhci_plat_data *data)
{
	pdev->dev.platform_data = data;
}

#endif /* LINUX_MMC_SDHCI_SPEAR_H */
