/*
 * idma.h  --  I2S0's Internal Dma driver
 *
 * Copyright (c) 2010 Samsung Electronics Co. Ltd
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __S3C_IDMA_H_
#define __S3C_IDMA_H_

#ifdef CONFIG_SND_SAMSUNG_ALP
#include "srp_alp/srp_alp.h"
#endif

#define I2SAHB			0x20
#define I2SSTR0			0x24
#define I2SSIZE			0x28
#define I2STRNCNT		0x2c
#define I2SLVL0ADDR		0x30
#define I2SLVL1ADDR		0x34
#define I2SLVL2ADDR		0x38
#define I2SLVL3ADDR		0x3c
#define I2SSTR1			0x40

#define AHB_DMAEN		(1 << 0)
#define AHB_DMACLR		(1 << 1)
#define AHB_INTMASK		(1 << 3)
#define AHB_DMARLD		(1 << 5)
#define AHB_DMA_STRADDRTOG	(1 << 6)
#define AHB_DMA_STRADDRRST	(1 << 7)
#define AHB_CLRLVL0INT		(1 << 16)
#define AHB_CLRLVL1INT		(1 << 17)
#define AHB_CLRLVL2INT		(1 << 18)
#define AHB_CLRLVL3INT		(1 << 19)
#define AHB_LVL0INT		(1 << 20)
#define AHB_LVL1INT		(1 << 21)
#define AHB_LVL2INT		(1 << 22)
#define AHB_LVL3INT		(1 << 23)
#define AHB_INTENLVL0		(1 << 24)
#define AHB_INTENLVL1		(1 << 25)
#define AHB_INTENLVL2		(1 << 26)
#define AHB_INTENLVL3		(1 << 27)
#define AHB_LVLINTMASK		(0xf << 20)

#define I2SSIZE_TRNMSK		(0xffff)
#define I2SSIZE_SHIFT		(16)

/* If enabled ALP Audio */
#if defined(CONFIG_SND_SAMSUNG_ALP)
#if defined(CONFIG_ARCH_EXYNOS4)
#define LP_TXBUFF_OFFSET	((soc_is_exynos4412() || soc_is_exynos4212()) ? \
				(0x38000) : (0x18000))
#elif defined(CONFIG_ARCH_EXYNOS5)
#define LP_TXBUFF_OFFSET	(0x4)
#endif

#define LP_TXBUFF_ADDR		(soc_is_exynos5250() ? \
				(SRP_DMEM_BASE + LP_TXBUFF_OFFSET) : \
				(SRP_IRAM_BASE + LP_TXBUFF_OFFSET))
#define LP_TXBUFF_MAX		(32 * 1024)
/* If only enabled LP Audio */
#else
#define LP_TXBUFF_ADDR		(0x03000000)
#define LP_TXBUFF_MAX		(128 * 1024)
#endif

/* idma_state */
#define LPAM_DMA_STOP		0
#define LPAM_DMA_START		1

#ifdef CONFIG_SND_SOC_SAMSUNG_USE_DMA_WRAPPER
extern struct snd_soc_platform_driver samsung_asoc_idma_platform;
#endif
extern void idma_init(void *regs);

/* These functions are used for srp driver. */
extern int idma_irq_callback(void);
extern void idma_stop(void);
#endif /* __S3C_IDMA_H_ */
