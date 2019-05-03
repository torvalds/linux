/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SPI_MMC_SPI_H
#define __LINUX_SPI_MMC_SPI_H

#include <linux/spi/spi.h>
#include <linux/interrupt.h>

struct device;
struct mmc_host;

/* Put this in platform_data of a device being used to manage an MMC/SD
 * card slot.  (Modeled after PXA mmc glue; see that for usage examples.)
 *
 * REVISIT This is not a spi-specific notion.  Any card slot should be
 * able to handle it.  If the MMC core doesn't adopt this kind of notion,
 * switch the "struct device *" parameters over to "struct spi_device *".
 */
struct mmc_spi_platform_data {
	/* driver activation and (optional) card detect irq hookup */
	int (*init)(struct device *,
		irqreturn_t (*)(int, void *),
		void *);
	void (*exit)(struct device *, void *);

	/* Capabilities to pass into mmc core (e.g. MMC_CAP_NEEDS_POLL). */
	unsigned long caps;
	unsigned long caps2;

	/* how long to debounce card detect, in msecs */
	u16 detect_delay;

	/* power management */
	u16 powerup_msecs;		/* delay of up to 250 msec */
	u32 ocr_mask;			/* available voltages */
	void (*setpower)(struct device *, unsigned int maskval);
};

#ifdef CONFIG_OF
extern struct mmc_spi_platform_data *mmc_spi_get_pdata(struct spi_device *spi);
extern void mmc_spi_put_pdata(struct spi_device *spi);
#else
static inline struct mmc_spi_platform_data *
mmc_spi_get_pdata(struct spi_device *spi)
{
	return spi->dev.platform_data;
}
static inline void mmc_spi_put_pdata(struct spi_device *spi) {}
#endif /* CONFIG_OF */

#endif /* __LINUX_SPI_MMC_SPI_H */
