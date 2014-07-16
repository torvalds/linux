/*
 *  include/linux/amba/mmci.h
 */
#ifndef AMBA_MMCI_H
#define AMBA_MMCI_H

#include <linux/mmc/host.h>

/**
 * struct mmci_platform_data - platform configuration for the MMCI
 * (also known as PL180) block.
 * @ocr_mask: available voltages on the 4 pins from the block, this
 * is ignored if a regulator is used, see the MMC_VDD_* masks in
 * mmc/host.h
 * @ios_handler: a callback function to act on specfic ios changes,
 * used for example to control a levelshifter
 * mask into a value to be binary (or set some other custom bits
 * in MMCIPWR) or:ed and written into the MMCIPWR register of the
 * block.  May also control external power based on the power_mode.
 * @status: if no GPIO read function was given to the block in
 * gpio_wp (below) this function will be called to determine
 * whether a card is present in the MMC slot or not
 * @gpio_wp: read this GPIO pin to see if the card is write protected
 * @gpio_cd: read this GPIO pin to detect card insertion
 * @cd_invert: true if the gpio_cd pin value is active low
 */
struct mmci_platform_data {
	unsigned int ocr_mask;
	int (*ios_handler)(struct device *, struct mmc_ios *);
	unsigned int (*status)(struct device *);
	int	gpio_wp;
	int	gpio_cd;
	bool	cd_invert;
};

#endif
