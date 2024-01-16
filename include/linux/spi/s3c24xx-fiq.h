/* SPDX-License-Identifier: GPL-2.0-only */
/* linux/drivers/spi/spi_s3c24xx_fiq.h
 *
 * Copyright 2009 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX SPI - FIQ pseudo-DMA transfer support
*/

#ifndef __LINUX_SPI_S3C24XX_FIQ_H
#define __LINUX_SPI_S3C24XX_FIQ_H __FILE__

/* We have R8 through R13 to play with */

#ifdef __ASSEMBLY__
#define __REG_NR(x)     r##x
#else

extern struct spi_fiq_code s3c24xx_spi_fiq_txrx;
extern struct spi_fiq_code s3c24xx_spi_fiq_tx;
extern struct spi_fiq_code s3c24xx_spi_fiq_rx;

#define __REG_NR(x)     (x)
#endif

#define fiq_rspi	__REG_NR(8)
#define fiq_rtmp	__REG_NR(9)
#define fiq_rrx		__REG_NR(10)
#define fiq_rtx		__REG_NR(11)
#define fiq_rcount	__REG_NR(12)
#define fiq_rirq	__REG_NR(13)

#endif /* __LINUX_SPI_S3C24XX_FIQ_H */
