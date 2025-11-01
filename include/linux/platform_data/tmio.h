/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MFD_TMIO_H
#define MFD_TMIO_H

#include <linux/platform_device.h>
#include <linux/types.h>

/* TMIO MMC platform flags */

/*
 * Some controllers can support a 2-byte block size when the bus width is
 * configured in 4-bit mode.
 */
#define TMIO_MMC_BLKSZ_2BYTES		BIT(1)

/* Some controllers can support SDIO IRQ signalling */
#define TMIO_MMC_SDIO_IRQ		BIT(2)

/* Some features are only available or tested on R-Car Gen2 or later */
#define TMIO_MMC_MIN_RCAR2		BIT(3)

/*
 * Some controllers require waiting for the SD bus to become idle before
 * writing to some registers.
 */
#define TMIO_MMC_HAS_IDLE_WAIT		BIT(4)

/*
 * Use the busy timeout feature. Probably all TMIO versions support it. Yet,
 * we don't have documentation for old variants, so we enable only known good
 * variants with this flag. Can be removed once all variants are known good.
 */
#define TMIO_MMC_USE_BUSY_TIMEOUT	BIT(5)

/* Some controllers have CMD12 automatically issue/non-issue register */
#define TMIO_MMC_HAVE_CMD12_CTRL	BIT(7)

/* Controller has some SDIO status bits which must be 1 */
#define TMIO_MMC_SDIO_STATUS_SETBITS	BIT(8)

/* Some controllers have a 32-bit wide data port register */
#define TMIO_MMC_32BIT_DATA_PORT	BIT(9)

/* Some controllers allows to set SDx actual clock */
#define TMIO_MMC_CLK_ACTUAL		BIT(10)

/* Some controllers have a CBSY bit */
#define TMIO_MMC_HAVE_CBSY		BIT(11)

/* Some controllers have a 64-bit wide data port register */
#define TMIO_MMC_64BIT_DATA_PORT	BIT(12)

struct tmio_mmc_data {
	void				*chan_priv_tx;
	void				*chan_priv_rx;
	unsigned int			hclk;
	unsigned long			capabilities;
	unsigned long			capabilities2;
	unsigned long			flags;
	u32				ocr_mask;	/* available voltages */
	dma_addr_t			dma_rx_offset;
	unsigned int			max_blk_count;
	unsigned short			max_segs;
};
#endif
