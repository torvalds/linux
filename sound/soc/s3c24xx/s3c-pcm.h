/*  sound/soc/s3c24xx/s3c-pcm.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __S3C_PCM_H
#define __S3C_PCM_H __FILE__

/*Register Offsets */
#define S3C_PCM_CTL	(0x00)
#define S3C_PCM_CLKCTL	(0x04)
#define S3C_PCM_TXFIFO	(0x08)
#define S3C_PCM_RXFIFO	(0x0C)
#define S3C_PCM_IRQCTL	(0x10)
#define S3C_PCM_IRQSTAT	(0x14)
#define S3C_PCM_FIFOSTAT	(0x18)
#define S3C_PCM_CLRINT	(0x20)

/* PCM_CTL Bit-Fields */
#define S3C_PCM_CTL_TXDIPSTICK_MASK		(0x3f)
#define S3C_PCM_CTL_TXDIPSTICK_SHIFT	(13)
#define S3C_PCM_CTL_RXDIPSTICK_MSK		(0x3f<<7)
#define S3C_PCM_CTL_TXDMA_EN		(0x1<<6)
#define S3C_PCM_CTL_RXDMA_EN		(0x1<<5)
#define S3C_PCM_CTL_TXMSB_AFTER_FSYNC	(0x1<<4)
#define S3C_PCM_CTL_RXMSB_AFTER_FSYNC	(0x1<<3)
#define S3C_PCM_CTL_TXFIFO_EN		(0x1<<2)
#define S3C_PCM_CTL_RXFIFO_EN		(0x1<<1)
#define S3C_PCM_CTL_ENABLE			(0x1<<0)

/* PCM_CLKCTL Bit-Fields */
#define S3C_PCM_CLKCTL_SERCLK_EN		(0x1<<19)
#define S3C_PCM_CLKCTL_SERCLKSEL_PCLK	(0x1<<18)
#define S3C_PCM_CLKCTL_SCLKDIV_MASK		(0x1ff)
#define S3C_PCM_CLKCTL_SYNCDIV_MASK		(0x1ff)
#define S3C_PCM_CLKCTL_SCLKDIV_SHIFT	(9)
#define S3C_PCM_CLKCTL_SYNCDIV_SHIFT	(0)

/* PCM_TXFIFO Bit-Fields */
#define S3C_PCM_TXFIFO_DVALID	(0x1<<16)
#define S3C_PCM_TXFIFO_DATA_MSK	(0xffff<<0)

/* PCM_RXFIFO Bit-Fields */
#define S3C_PCM_RXFIFO_DVALID	(0x1<<16)
#define S3C_PCM_RXFIFO_DATA_MSK	(0xffff<<0)

/* PCM_IRQCTL Bit-Fields */
#define S3C_PCM_IRQCTL_IRQEN		(0x1<<14)
#define S3C_PCM_IRQCTL_WRDEN		(0x1<<12)
#define S3C_PCM_IRQCTL_TXEMPTYEN		(0x1<<11)
#define S3C_PCM_IRQCTL_TXALMSTEMPTYEN	(0x1<<10)
#define S3C_PCM_IRQCTL_TXFULLEN		(0x1<<9)
#define S3C_PCM_IRQCTL_TXALMSTFULLEN	(0x1<<8)
#define S3C_PCM_IRQCTL_TXSTARVEN		(0x1<<7)
#define S3C_PCM_IRQCTL_TXERROVRFLEN		(0x1<<6)
#define S3C_PCM_IRQCTL_RXEMPTEN		(0x1<<5)
#define S3C_PCM_IRQCTL_RXALMSTEMPTEN	(0x1<<4)
#define S3C_PCM_IRQCTL_RXFULLEN		(0x1<<3)
#define S3C_PCM_IRQCTL_RXALMSTFULLEN	(0x1<<2)
#define S3C_PCM_IRQCTL_RXSTARVEN		(0x1<<1)
#define S3C_PCM_IRQCTL_RXERROVRFLEN		(0x1<<0)

/* PCM_IRQSTAT Bit-Fields */
#define S3C_PCM_IRQSTAT_IRQPND		(0x1<<13)
#define S3C_PCM_IRQSTAT_WRD_XFER		(0x1<<12)
#define S3C_PCM_IRQSTAT_TXEMPTY		(0x1<<11)
#define S3C_PCM_IRQSTAT_TXALMSTEMPTY	(0x1<<10)
#define S3C_PCM_IRQSTAT_TXFULL		(0x1<<9)
#define S3C_PCM_IRQSTAT_TXALMSTFULL		(0x1<<8)
#define S3C_PCM_IRQSTAT_TXSTARV		(0x1<<7)
#define S3C_PCM_IRQSTAT_TXERROVRFL		(0x1<<6)
#define S3C_PCM_IRQSTAT_RXEMPT		(0x1<<5)
#define S3C_PCM_IRQSTAT_RXALMSTEMPT		(0x1<<4)
#define S3C_PCM_IRQSTAT_RXFULL		(0x1<<3)
#define S3C_PCM_IRQSTAT_RXALMSTFULL		(0x1<<2)
#define S3C_PCM_IRQSTAT_RXSTARV		(0x1<<1)
#define S3C_PCM_IRQSTAT_RXERROVRFL		(0x1<<0)

/* PCM_FIFOSTAT Bit-Fields */
#define S3C_PCM_FIFOSTAT_TXCNT_MSK		(0x3f<<14)
#define S3C_PCM_FIFOSTAT_TXFIFOEMPTY	(0x1<<13)
#define S3C_PCM_FIFOSTAT_TXFIFOALMSTEMPTY	(0x1<<12)
#define S3C_PCM_FIFOSTAT_TXFIFOFULL		(0x1<<11)
#define S3C_PCM_FIFOSTAT_TXFIFOALMSTFULL	(0x1<<10)
#define S3C_PCM_FIFOSTAT_RXCNT_MSK		(0x3f<<4)
#define S3C_PCM_FIFOSTAT_RXFIFOEMPTY	(0x1<<3)
#define S3C_PCM_FIFOSTAT_RXFIFOALMSTEMPTY	(0x1<<2)
#define S3C_PCM_FIFOSTAT_RXFIFOFULL		(0x1<<1)
#define S3C_PCM_FIFOSTAT_RXFIFOALMSTFULL	(0x1<<0)

#define S3C_PCM_CLKSRC_PCLK	0
#define S3C_PCM_CLKSRC_MUX	1

#define S3C_PCM_SCLK_PER_FS	0

/**
 * struct s3c_pcm_info - S3C PCM Controller information
 * @dev: The parent device passed to use from the probe.
 * @regs: The pointer to the device register block.
 * @dma_playback: DMA information for playback channel.
 * @dma_capture: DMA information for capture channel.
 */
struct s3c_pcm_info {
	spinlock_t lock;
	struct device	*dev;
	void __iomem	*regs;

	unsigned int sclk_per_fs;

	/* Whether to keep PCMSCLK enabled even when idle(no active xfer) */
	unsigned int idleclk;

	struct clk	*pclk;
	struct clk	*cclk;

	struct s3c_dma_params	*dma_playback;
	struct s3c_dma_params	*dma_capture;
};

#endif /* __S3C_PCM_H */
