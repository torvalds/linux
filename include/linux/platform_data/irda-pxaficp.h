/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASMARM_ARCH_IRDA_H
#define ASMARM_ARCH_IRDA_H

/* board specific transceiver capabilities */

#define IR_OFF		1
#define IR_SIRMODE	2
#define IR_FIRMODE	4

struct pxaficp_platform_data {
	int transceiver_cap;
	void (*transceiver_mode)(struct device *dev, int mode);
	int (*startup)(struct device *dev);
	void (*shutdown)(struct device *dev);
	int gpio_pwdown;		/* powerdown GPIO for the IrDA chip */
	bool gpio_pwdown_inverted;	/* gpio_pwdown is inverted */
};

extern void pxa_set_ficp_info(struct pxaficp_platform_data *info);

#if defined(CONFIG_PXA25x) || defined(CONFIG_PXA27x)
void pxa2xx_transceiver_mode(struct device *dev, int mode);
#endif

#endif
