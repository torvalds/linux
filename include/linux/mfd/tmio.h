/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MFD_TMIO_H
#define MFD_TMIO_H

#include <linux/device.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/mmc/card.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define tmio_ioread8(addr) readb(addr)
#define tmio_ioread16(addr) readw(addr)
#define tmio_ioread16_rep(r, b, l) readsw(r, b, l)
#define tmio_ioread32(addr) \
	(((u32)readw((addr))) | (((u32)readw((addr) + 2)) << 16))

#define tmio_iowrite8(val, addr) writeb((val), (addr))
#define tmio_iowrite16(val, addr) writew((val), (addr))
#define tmio_iowrite16_rep(r, b, l) writesw(r, b, l)
#define tmio_iowrite32(val, addr) \
	do { \
		writew((val),       (addr)); \
		writew((val) >> 16, (addr) + 2); \
	} while (0)

#define sd_config_write8(base, shift, reg, val) \
	tmio_iowrite8((val), (base) + ((reg) << (shift)))
#define sd_config_write16(base, shift, reg, val) \
	tmio_iowrite16((val), (base) + ((reg) << (shift)))
#define sd_config_write32(base, shift, reg, val) \
	do { \
		tmio_iowrite16((val), (base) + ((reg) << (shift)));   \
		tmio_iowrite16((val) >> 16, (base) + ((reg + 2) << (shift))); \
	} while (0)

/* tmio MMC platform flags */
/*
 * Some controllers can support a 2-byte block size when the bus width
 * is configured in 4-bit mode.
 */
#define TMIO_MMC_BLKSZ_2BYTES		BIT(1)
/*
 * Some controllers can support SDIO IRQ signalling.
 */
#define TMIO_MMC_SDIO_IRQ		BIT(2)

/* Some features are only available or tested on R-Car Gen2 or later */
#define TMIO_MMC_MIN_RCAR2		BIT(3)

/*
 * Some controllers require waiting for the SD bus to become
 * idle before writing to some registers.
 */
#define TMIO_MMC_HAS_IDLE_WAIT		BIT(4)
/*
 * A GPIO is used for card hotplug detection. We need an extra flag for this,
 * because 0 is a valid GPIO number too, and requiring users to specify
 * cd_gpio < 0 to disable GPIO hotplug would break backwards compatibility.
 */
#define TMIO_MMC_USE_GPIO_CD		BIT(5)

/*
 * Some controllers have CMD12 automatically
 * issue/non-issue register
 */
#define TMIO_MMC_HAVE_CMD12_CTRL	BIT(7)

/* Controller has some SDIO status bits which must be 1 */
#define TMIO_MMC_SDIO_STATUS_SETBITS	BIT(8)

/*
 * Some controllers have a 32-bit wide data port register
 */
#define TMIO_MMC_32BIT_DATA_PORT	BIT(9)

/*
 * Some controllers allows to set SDx actual clock
 */
#define TMIO_MMC_CLK_ACTUAL		BIT(10)

/* Some controllers have a CBSY bit */
#define TMIO_MMC_HAVE_CBSY		BIT(11)

/* Some controllers that support HS400 use 4 taps while others use 8. */
#define TMIO_MMC_HAVE_4TAP_HS400	BIT(13)

int tmio_core_mmc_enable(void __iomem *cnf, int shift, unsigned long base);
int tmio_core_mmc_resume(void __iomem *cnf, int shift, unsigned long base);
void tmio_core_mmc_pwr(void __iomem *cnf, int shift, int state);
void tmio_core_mmc_clk_div(void __iomem *cnf, int shift, int state);

struct dma_chan;

/*
 * data for the MMC controller
 */
struct tmio_mmc_data {
	void				*chan_priv_tx;
	void				*chan_priv_rx;
	unsigned int			hclk;
	unsigned long			capabilities;
	unsigned long			capabilities2;
	unsigned long			flags;
	u32				ocr_mask;	/* available voltages */
	unsigned int			cd_gpio;
	int				alignment_shift;
	dma_addr_t			dma_rx_offset;
	unsigned int			max_blk_count;
	unsigned short			max_segs;
	void (*set_pwr)(struct platform_device *host, int state);
	void (*set_clk_div)(struct platform_device *host, int state);
};

/*
 * data for the NAND controller
 */
struct tmio_nand_data {
	struct nand_bbt_descr	*badblock_pattern;
	struct mtd_partition	*partition;
	unsigned int		num_partitions;
	const char *const	*part_parsers;
};

#define FBIO_TMIO_ACC_WRITE	0x7C639300
#define FBIO_TMIO_ACC_SYNC	0x7C639301

struct tmio_fb_data {
	int			(*lcd_set_power)(struct platform_device *fb_dev,
						 bool on);
	int			(*lcd_mode)(struct platform_device *fb_dev,
					    const struct fb_videomode *mode);
	int			num_modes;
	struct fb_videomode	*modes;

	/* in mm: size of screen */
	int			height;
	int			width;
};

#endif
