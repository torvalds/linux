/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_MCI_H
#define _ARCH_MCI_H

/**
 * struct s3c24xx_mci_pdata - sd/mmc controller platform data
 * @no_wprotect: Set this to indicate there is no write-protect switch.
 * @no_detect: Set this if there is no detect switch.
 * @wprotect_invert: Invert the default sense of the write protect switch.
 * @use_dma: Set to allow the use of DMA.
 * @gpio_detect: GPIO number for the card detect line.
 * @gpio_wprotect: GPIO number for the write protect line.
 * @ocr_avail: The mask of the available power states, non-zero to use.
 * @set_power: Callback to control the power mode.
 *
 * The @gpio_detect is used for card detection when @no_wprotect is unset,
 * and the default sense is that 0 returned from gpio_get_value() means
 * that a card is inserted. If @detect_invert is set, then the value from
 * gpio_get_value() is inverted, which makes 1 mean card inserted.
 *
 * The driver will use @gpio_wprotect to signal whether the card is write
 * protected if @no_wprotect is not set. A 0 returned from gpio_get_value()
 * means the card is read/write, and 1 means read-only. The @wprotect_invert
 * will invert the value returned from gpio_get_value().
 *
 * Card power is set by @ocr_availa, using MCC_VDD_ constants if it is set
 * to a non-zero value, otherwise the default of 3.2-3.4V is used.
 */
struct s3c24xx_mci_pdata {
	unsigned int	no_wprotect:1;
	unsigned int	no_detect:1;
	unsigned int	wprotect_invert:1;
	unsigned int	use_dma:1;

	unsigned long	ocr_avail;
	void		(*set_power)(unsigned char power_mode,
				     unsigned short vdd);
};

/**
 * s3c24xx_mci_set_platdata - set platform data for mmc/sdi device
 * @pdata: The platform data
 *
 * Copy the platform data supplied by @pdata so that this can be marked
 * __initdata.
 */
extern void s3c24xx_mci_set_platdata(struct s3c24xx_mci_pdata *pdata);

#endif /* _ARCH_NCI_H */
