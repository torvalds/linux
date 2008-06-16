#ifndef __LINUX_SPI_MMC_SPI_H
#define __LINUX_SPI_MMC_SPI_H

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

	/* sense switch on sd cards */
	int (*get_ro)(struct device *);

	/* how long to debounce card detect, in msecs */
	u16 detect_delay;

	/* power management */
	u16 powerup_msecs;		/* delay of up to 250 msec */
	u32 ocr_mask;			/* available voltages */
	void (*setpower)(struct device *, unsigned int maskval);
};

#endif /* __LINUX_SPI_MMC_SPI_H */
