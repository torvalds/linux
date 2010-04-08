/*
 *  include/linux/amba/mmci.h
 */
#ifndef AMBA_MMCI_H
#define AMBA_MMCI_H

#include <linux/mmc/host.h>

/**
 * struct mmci_platform_data - platform configuration for the MMCI
 * (also known as PL180) block.
 * @f_max: the maximum operational frequency for this host in this
 * platform configuration. When this is specified it takes precedence
 * over the module parameter for the same frequency.
 * @ocr_mask: available voltages on the 4 pins from the block, this
 * is ignored if a regulator is used, see the MMC_VDD_* masks in
 * mmc/host.h
 * @translate_vdd: a callback function to translate a MMC_VDD_*
 * mask into a value to be binary or:ed and written into the
 * MMCIPWR register of the block
 * @status: if no GPIO read function was given to the block in
 * gpio_wp (below) this function will be called to determine
 * whether a card is present in the MMC slot or not
 * @gpio_wp: read this GPIO pin to see if the card is write protected
 * @gpio_cd: read this GPIO pin to detect card insertion
 * @capabilities: the capabilities of the block as implemented in
 * this platform, signify anything MMC_CAP_* from mmc/host.h
 */
struct mmci_platform_data {
	unsigned int f_max;
	unsigned int ocr_mask;
	u32 (*translate_vdd)(struct device *, unsigned int);
	unsigned int (*status)(struct device *);
	int	gpio_wp;
	int	gpio_cd;
	unsigned long capabilities;
};

#endif
