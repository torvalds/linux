/*
 * MDIO-GPIO bus platform data structures
 *
 * Copyright (C) 2008, Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __LINUX_MDIO_GPIO_H
#define __LINUX_MDIO_GPIO_H

#include <linux/mdio-bitbang.h>

struct mdio_gpio_platform_data {
	/* GPIO numbers for bus pins */
	unsigned int mdc;
	unsigned int mdio;
	unsigned int mdo;

	bool mdc_active_low;
	bool mdio_active_low;
	bool mdo_active_low;

	u32 phy_mask;
	u32 phy_ignore_ta_mask;
	int irqs[PHY_MAX_ADDR];
	/* reset callback */
	int (*reset)(struct mii_bus *bus);
};

#endif /* __LINUX_MDIO_GPIO_H */
