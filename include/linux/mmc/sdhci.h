/*
 *  linux/include/linux/mmc/sdhci.h - Secure Digital Host Controller Interface
 *
 *  Copyright (C) 2005-2008 Pierre Ossman, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */
#ifndef __SDHCI_H
#define __SDHCI_H

#include <linux/scatterlist.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/mmc/host.h>

struct sdhci_host {
	/* Data set by hardware interface driver */
	const char *hw_name;	/* Hardware bus name */

	unsigned int quirks;	/* Deviations from spec. */

/* Controller doesn't honor resets unless we touch the clock register */
#define SDHCI_QUIRK_CLOCK_BEFORE_RESET			(1<<0)
/* Controller has bad caps bits, but really supports DMA */
#define SDHCI_QUIRK_FORCE_DMA				(1<<1)
/* Controller doesn't like to be reset when there is no card inserted. */
#define SDHCI_QUIRK_NO_CARD_NO_RESET			(1<<2)
/* Controller doesn't like clearing the power reg before a change */
#define SDHCI_QUIRK_SINGLE_POWER_WRITE			(1<<3)
/* Controller has flaky internal state so reset it on each ios change */
#define SDHCI_QUIRK_RESET_CMD_DATA_ON_IOS		(1<<4)
/* Controller has an unusable DMA engine */
#define SDHCI_QUIRK_BROKEN_DMA				(1<<5)
/* Controller has an unusable ADMA engine */
#define SDHCI_QUIRK_BROKEN_ADMA				(1<<6)
/* Controller can only DMA from 32-bit aligned addresses */
#define SDHCI_QUIRK_32BIT_DMA_ADDR			(1<<7)
/* Controller can only DMA chunk sizes that are a multiple of 32 bits */
#define SDHCI_QUIRK_32BIT_DMA_SIZE			(1<<8)
/* Controller can only ADMA chunks that are a multiple of 32 bits */
#define SDHCI_QUIRK_32BIT_ADMA_SIZE			(1<<9)
/* Controller needs to be reset after each request to stay stable */
#define SDHCI_QUIRK_RESET_AFTER_REQUEST			(1<<10)
/* Controller needs voltage and power writes to happen separately */
#define SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER		(1<<11)
/* Controller provides an incorrect timeout value for transfers */
#define SDHCI_QUIRK_BROKEN_TIMEOUT_VAL			(1<<12)
/* Controller has an issue with buffer bits for small transfers */
#define SDHCI_QUIRK_BROKEN_SMALL_PIO			(1<<13)
/* Controller does not provide transfer-complete interrupt when not busy */
#define SDHCI_QUIRK_NO_BUSY_IRQ				(1<<14)
/* Controller has unreliable card detection */
#define SDHCI_QUIRK_BROKEN_CARD_DETECTION		(1<<15)
/* Controller reports inverted write-protect state */
#define SDHCI_QUIRK_INVERTED_WRITE_PROTECT		(1<<16)
/* Controller has nonstandard clock management */
#define SDHCI_QUIRK_NONSTANDARD_CLOCK			(1<<17)
/* Controller does not like fast PIO transfers */
#define SDHCI_QUIRK_PIO_NEEDS_DELAY			(1<<18)
/* Controller losing signal/interrupt enable states after reset */
#define SDHCI_QUIRK_RESTORE_IRQS_AFTER_RESET		(1<<19)
/* Controller has to be forced to use block size of 2048 bytes */
#define SDHCI_QUIRK_FORCE_BLK_SZ_2048			(1<<20)
/* Controller cannot do multi-block transfers */
#define SDHCI_QUIRK_NO_MULTIBLOCK			(1<<21)
/* Controller can only handle 1-bit data transfers */
#define SDHCI_QUIRK_FORCE_1_BIT_DATA			(1<<22)
/* Controller needs 10ms delay between applying power and clock */
#define SDHCI_QUIRK_DELAY_AFTER_POWER			(1<<23)
/* Controller uses SDCLK instead of TMCLK for data timeouts */
#define SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK		(1<<24)
/* Controller reports wrong base clock capability */
#define SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN		(1<<25)
/* Controller cannot support End Attribute in NOP ADMA descriptor */
#define SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC		(1<<26)
/* Controller is missing device caps. Use caps provided by host */
#define SDHCI_QUIRK_MISSING_CAPS			(1<<27)
/* Controller uses Auto CMD12 command to stop the transfer */
#define SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12		(1<<28)
/* Controller doesn't have HISPD bit field in HI-SPEED SD card */
#define SDHCI_QUIRK_NO_HISPD_BIT			(1<<29)
/* Controller treats ADMA descriptors with length 0000h incorrectly */
#define SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC		(1<<30)

	int irq;		/* Device IRQ */
	void __iomem *ioaddr;	/* Mapped address */

	const struct sdhci_ops *ops;	/* Low level hw interface */

	struct regulator *vmmc;	/* Power regulator */

	/* Internal data */
	struct mmc_host *mmc;	/* MMC structure */
	u64 dma_mask;		/* custom DMA mask */

#if defined(CONFIG_LEDS_CLASS) || defined(CONFIG_LEDS_CLASS_MODULE)
	struct led_classdev led;	/* LED control */
	char led_name[32];
#endif

	spinlock_t lock;	/* Mutex */

	int flags;		/* Host attributes */
#define SDHCI_USE_SDMA		(1<<0)	/* Host is SDMA capable */
#define SDHCI_USE_ADMA		(1<<1)	/* Host is ADMA capable */
#define SDHCI_REQ_USE_DMA	(1<<2)	/* Use DMA for this req. */
#define SDHCI_DEVICE_DEAD	(1<<3)	/* Device unresponsive */

	unsigned int version;	/* SDHCI spec. version */

	unsigned int max_clk;	/* Max possible freq (MHz) */
	unsigned int timeout_clk;	/* Timeout freq (KHz) */

	unsigned int clock;	/* Current clock (MHz) */
	u8 pwr;			/* Current voltage */

	struct mmc_request *mrq;	/* Current request */
	struct mmc_command *cmd;	/* Current command */
	struct mmc_data *data;	/* Current data request */
	unsigned int data_early:1;	/* Data finished before cmd */

	struct sg_mapping_iter sg_miter;	/* SG state for PIO */
	unsigned int blocks;	/* remaining PIO blocks */

	int sg_count;		/* Mapped sg entries */

	u8 *adma_desc;		/* ADMA descriptor table */
	u8 *align_buffer;	/* Bounce buffer */

	dma_addr_t adma_addr;	/* Mapped ADMA descr. table */
	dma_addr_t align_addr;	/* Mapped bounce buffer */

	struct tasklet_struct card_tasklet;	/* Tasklet structures */
	struct tasklet_struct finish_tasklet;

	struct timer_list timer;	/* Timer for timeouts */

	unsigned int caps;	/* Alternative capabilities */

	unsigned int            ocr_avail_sdio;	/* OCR bit masks */
	unsigned int            ocr_avail_sd;
	unsigned int            ocr_avail_mmc;

	unsigned long private[0] ____cacheline_aligned;
};
#endif /* __SDHCI_H */
