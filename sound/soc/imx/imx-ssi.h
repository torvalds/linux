/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IMX_SSI_H
#define _IMX_SSI_H

#define SSI_STX0	0x00
#define SSI_STX1	0x04
#define SSI_SRX0	0x08
#define SSI_SRX1	0x0c

#define SSI_SCR		0x10
#define SSI_SCR_CLK_IST		(1 << 9)
#define SSI_SCR_CLK_IST_SHIFT	9
#define SSI_SCR_TCH_EN		(1 << 8)
#define SSI_SCR_SYS_CLK_EN	(1 << 7)
#define SSI_SCR_I2S_MODE_NORM	(0 << 5)
#define SSI_SCR_I2S_MODE_MSTR	(1 << 5)
#define SSI_SCR_I2S_MODE_SLAVE	(2 << 5)
#define SSI_I2S_MODE_MASK	(3 << 5)
#define SSI_SCR_SYN		(1 << 4)
#define SSI_SCR_NET		(1 << 3)
#define SSI_SCR_RE		(1 << 2)
#define SSI_SCR_TE		(1 << 1)
#define SSI_SCR_SSIEN		(1 << 0)

#define SSI_SISR	0x14
#define SSI_SISR_MASK		((1 << 19) - 1)
#define SSI_SISR_CMDAU		(1 << 18)
#define SSI_SISR_CMDDU		(1 << 17)
#define SSI_SISR_RXT		(1 << 16)
#define SSI_SISR_RDR1		(1 << 15)
#define SSI_SISR_RDR0		(1 << 14)
#define SSI_SISR_TDE1		(1 << 13)
#define SSI_SISR_TDE0		(1 << 12)
#define SSI_SISR_ROE1		(1 << 11)
#define SSI_SISR_ROE0		(1 << 10)
#define SSI_SISR_TUE1		(1 << 9)
#define SSI_SISR_TUE0		(1 << 8)
#define SSI_SISR_TFS		(1 << 7)
#define SSI_SISR_RFS		(1 << 6)
#define SSI_SISR_TLS		(1 << 5)
#define SSI_SISR_RLS		(1 << 4)
#define SSI_SISR_RFF1		(1 << 3)
#define SSI_SISR_RFF0		(1 << 2)
#define SSI_SISR_TFE1		(1 << 1)
#define SSI_SISR_TFE0		(1 << 0)

#define SSI_SIER	0x18
#define SSI_SIER_RDMAE		(1 << 22)
#define SSI_SIER_RIE		(1 << 21)
#define SSI_SIER_TDMAE		(1 << 20)
#define SSI_SIER_TIE		(1 << 19)
#define SSI_SIER_CMDAU_EN	(1 << 18)
#define SSI_SIER_CMDDU_EN	(1 << 17)
#define SSI_SIER_RXT_EN		(1 << 16)
#define SSI_SIER_RDR1_EN	(1 << 15)
#define SSI_SIER_RDR0_EN	(1 << 14)
#define SSI_SIER_TDE1_EN	(1 << 13)
#define SSI_SIER_TDE0_EN	(1 << 12)
#define SSI_SIER_ROE1_EN	(1 << 11)
#define SSI_SIER_ROE0_EN	(1 << 10)
#define SSI_SIER_TUE1_EN	(1 << 9)
#define SSI_SIER_TUE0_EN	(1 << 8)
#define SSI_SIER_TFS_EN		(1 << 7)
#define SSI_SIER_RFS_EN		(1 << 6)
#define SSI_SIER_TLS_EN		(1 << 5)
#define SSI_SIER_RLS_EN		(1 << 4)
#define SSI_SIER_RFF1_EN	(1 << 3)
#define SSI_SIER_RFF0_EN	(1 << 2)
#define SSI_SIER_TFE1_EN	(1 << 1)
#define SSI_SIER_TFE0_EN	(1 << 0)

#define SSI_STCR	0x1c
#define SSI_STCR_TXBIT0		(1 << 9)
#define SSI_STCR_TFEN1		(1 << 8)
#define SSI_STCR_TFEN0		(1 << 7)
#define SSI_FIFO_ENABLE_0_SHIFT 7
#define SSI_STCR_TFDIR		(1 << 6)
#define SSI_STCR_TXDIR		(1 << 5)
#define SSI_STCR_TSHFD		(1 << 4)
#define SSI_STCR_TSCKP		(1 << 3)
#define SSI_STCR_TFSI		(1 << 2)
#define SSI_STCR_TFSL		(1 << 1)
#define SSI_STCR_TEFS		(1 << 0)

#define SSI_SRCR	0x20
#define SSI_SRCR_RXBIT0		(1 << 9)
#define SSI_SRCR_RFEN1		(1 << 8)
#define SSI_SRCR_RFEN0		(1 << 7)
#define SSI_FIFO_ENABLE_0_SHIFT 7
#define SSI_SRCR_RFDIR		(1 << 6)
#define SSI_SRCR_RXDIR		(1 << 5)
#define SSI_SRCR_RSHFD		(1 << 4)
#define SSI_SRCR_RSCKP		(1 << 3)
#define SSI_SRCR_RFSI		(1 << 2)
#define SSI_SRCR_RFSL		(1 << 1)
#define SSI_SRCR_REFS		(1 << 0)

#define SSI_SRCCR		0x28
#define SSI_SRCCR_DIV2		(1 << 18)
#define SSI_SRCCR_PSR		(1 << 17)
#define SSI_SRCCR_WL(x)		((((x) - 2) >> 1) << 13)
#define SSI_SRCCR_DC(x)		(((x) & 0x1f) << 8)
#define SSI_SRCCR_PM(x)		(((x) & 0xff) << 0)
#define SSI_SRCCR_WL_MASK	(0xf << 13)
#define SSI_SRCCR_DC_MASK	(0x1f << 8)
#define SSI_SRCCR_PM_MASK	(0xff << 0)

#define SSI_STCCR		0x24
#define SSI_STCCR_DIV2		(1 << 18)
#define SSI_STCCR_PSR		(1 << 17)
#define SSI_STCCR_WL(x)		((((x) - 2) >> 1) << 13)
#define SSI_STCCR_DC(x)		(((x) & 0x1f) << 8)
#define SSI_STCCR_PM(x)		(((x) & 0xff) << 0)
#define SSI_STCCR_WL_MASK	(0xf << 13)
#define SSI_STCCR_DC_MASK	(0x1f << 8)
#define SSI_STCCR_PM_MASK	(0xff << 0)

#define SSI_SFCSR	0x2c
#define SSI_SFCSR_RFCNT1(x)	(((x) & 0xf) << 28)
#define SSI_RX_FIFO_1_COUNT_SHIFT 28
#define SSI_SFCSR_TFCNT1(x)	(((x) & 0xf) << 24)
#define SSI_TX_FIFO_1_COUNT_SHIFT 24
#define SSI_SFCSR_RFWM1(x)	(((x) & 0xf) << 20)
#define SSI_SFCSR_TFWM1(x)	(((x) & 0xf) << 16)
#define SSI_SFCSR_RFCNT0(x)	(((x) & 0xf) << 12)
#define SSI_RX_FIFO_0_COUNT_SHIFT 12
#define SSI_SFCSR_TFCNT0(x)	(((x) & 0xf) <<  8)
#define SSI_TX_FIFO_0_COUNT_SHIFT 8
#define SSI_SFCSR_RFWM0(x)	(((x) & 0xf) <<  4)
#define SSI_SFCSR_TFWM0(x)	(((x) & 0xf) <<  0)
#define SSI_SFCSR_RFWM0_MASK	(0xf <<  4)
#define SSI_SFCSR_TFWM0_MASK	(0xf <<  0)

#define SSI_STR		0x30
#define SSI_STR_TEST		(1 << 15)
#define SSI_STR_RCK2TCK		(1 << 14)
#define SSI_STR_RFS2TFS		(1 << 13)
#define SSI_STR_RXSTATE(x)	(((x) & 0xf) << 8)
#define SSI_STR_TXD2RXD		(1 <<  7)
#define SSI_STR_TCK2RCK		(1 <<  6)
#define SSI_STR_TFS2RFS		(1 <<  5)
#define SSI_STR_TXSTATE(x)	(((x) & 0xf) << 0)

#define SSI_SOR		0x34
#define SSI_SOR_CLKOFF		(1 << 6)
#define SSI_SOR_RX_CLR		(1 << 5)
#define SSI_SOR_TX_CLR		(1 << 4)
#define SSI_SOR_INIT		(1 << 3)
#define SSI_SOR_WAIT(x)		(((x) & 0x3) << 1)
#define SSI_SOR_WAIT_MASK	(0x3 << 1)
#define SSI_SOR_SYNRST		(1 << 0)

#define SSI_SACNT	0x38
#define SSI_SACNT_FRDIV(x)	(((x) & 0x3f) << 5)
#define SSI_SACNT_WR		(1 << 4)
#define SSI_SACNT_RD		(1 << 3)
#define SSI_SACNT_TIF		(1 << 2)
#define SSI_SACNT_FV		(1 << 1)
#define SSI_SACNT_AC97EN	(1 << 0)

#define SSI_SACADD	0x3c
#define SSI_SACDAT	0x40
#define SSI_SATAG	0x44
#define SSI_STMSK	0x48
#define SSI_SRMSK	0x4c
#define SSI_SACCST	0x50
#define SSI_SACCEN	0x54
#define SSI_SACCDIS	0x58

/* SSI clock sources */
#define IMX_SSP_SYS_CLK		0

/* SSI audio dividers */
#define IMX_SSI_TX_DIV_2	0
#define IMX_SSI_TX_DIV_PSR	1
#define IMX_SSI_TX_DIV_PM	2
#define IMX_SSI_RX_DIV_2	3
#define IMX_SSI_RX_DIV_PSR	4
#define IMX_SSI_RX_DIV_PM	5

#define DRV_NAME "imx-ssi"

#include <linux/dmaengine.h>
#include <mach/dma.h>

struct imx_pcm_dma_params {
	int dma;
	unsigned long dma_addr;
	int burstsize;
};

struct imx_ssi {
	struct platform_device *ac97_dev;

	struct snd_soc_dai *imx_ac97;
	struct clk *clk;
	void __iomem *base;
	int irq;
	int fiq_enable;
	unsigned int offset;

	unsigned int flags;

	void (*ac97_reset) (struct snd_ac97 *ac97);
	void (*ac97_warm_reset)(struct snd_ac97 *ac97);

	struct imx_pcm_dma_params	dma_params_rx;
	struct imx_pcm_dma_params	dma_params_tx;

	int enabled;

	struct platform_device *soc_platform_pdev;
	struct platform_device *soc_platform_pdev_fiq;
};

struct snd_soc_platform *imx_ssi_fiq_init(struct platform_device *pdev,
		struct imx_ssi *ssi);
void imx_ssi_fiq_exit(struct platform_device *pdev, struct imx_ssi *ssi);
struct snd_soc_platform *imx_ssi_dma_mx2_init(struct platform_device *pdev,
		struct imx_ssi *ssi);

int snd_imx_pcm_mmap(struct snd_pcm_substream *substream, struct vm_area_struct *vma);
int imx_pcm_new(struct snd_card *card, struct snd_soc_dai *dai,
	struct snd_pcm *pcm);
void imx_pcm_free(struct snd_pcm *pcm);

/*
 * Do not change this as the FIQ handler depends on this size
 */
#define IMX_SSI_DMABUF_SIZE	(64 * 1024)

#define DMA_RXFIFO_BURST      0x4
#define DMA_TXFIFO_BURST      0x6

#endif /* _IMX_SSI_H */
