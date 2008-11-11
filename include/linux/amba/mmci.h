/*
 *  include/linux/amba/mmci.h
 */
#ifndef AMBA_MMCI_H
#define AMBA_MMCI_H

#include <linux/mmc/host.h>

struct mmci_platform_data {
	unsigned int ocr_mask;			/* available voltages */
	u32 (*translate_vdd)(struct device *, unsigned int);
	unsigned int (*status)(struct device *);
	int	gpio_wp;
	int	gpio_cd;
	unsigned long capabilities;
	unsigned int status_irq;
	int (*register_status_notify)(void (*callback)(int card_present, void *dev_id), void *dev_id);
};

#endif
