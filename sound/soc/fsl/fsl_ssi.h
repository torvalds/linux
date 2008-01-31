/*
 * fsl_ssi.h - ALSA SSI interface for the Freescale MPC8610 SoC
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2007-2008 Freescale Semiconductor, Inc.  This file is licensed
 * under the terms of the GNU General Public License version 2.  This
 * program is licensed "as is" without any warranty of any kind, whether
 * express or implied.
 */

#ifndef _MPC8610_I2S_H
#define _MPC8610_I2S_H

/* SSI Register Map */
struct ccsr_ssi {
	__be32 stx0;	/* 0x.0000 - SSI Transmit Data Register 0 */
	__be32 stx1;	/* 0x.0004 - SSI Transmit Data Register 1 */
	__be32 srx0;	/* 0x.0008 - SSI Receive Data Register 0 */
	__be32 srx1;	/* 0x.000C - SSI Receive Data Register 1 */
	__be32 scr;	/* 0x.0010 - SSI Control Register */
	__be32 sisr;	/* 0x.0014 - SSI Interrupt Status Register Mixed */
	__be32 sier;	/* 0x.0018 - SSI Interrupt Enable Register */
	__be32 stcr;	/* 0x.001C - SSI Transmit Configuration Register */
	__be32 srcr;	/* 0x.0020 - SSI Receive Configuration Register */
	__be32 stccr;	/* 0x.0024 - SSI Transmit Clock Control Register */
	__be32 srccr;	/* 0x.0028 - SSI Receive Clock Control Register */
	__be32 sfcsr;	/* 0x.002C - SSI FIFO Control/Status Register */
	__be32 str;	/* 0x.0030 - SSI Test Register */
	__be32 sor;	/* 0x.0034 - SSI Option Register */
	__be32 sacnt;	/* 0x.0038 - SSI AC97 Control Register */
	__be32 sacadd;	/* 0x.003C - SSI AC97 Command Address Register */
	__be32 sacdat;	/* 0x.0040 - SSI AC97 Command Data Register */
	__be32 satag;	/* 0x.0044 - SSI AC97 Tag Register */
	__be32 stmsk;	/* 0x.0048 - SSI Transmit Time Slot Mask Register */
	__be32 srmsk;	/* 0x.004C - SSI Receive Time Slot Mask Register */
	__be32 saccst;	/* 0x.0050 - SSI AC97 Channel Status Register */
	__be32 saccen;	/* 0x.0054 - SSI AC97 Channel Enable Register */
	__be32 saccdis; /* 0x.0058 - SSI AC97 Channel Disable Register */
};

#define CCSR_SSI_SCR_RFR_CLK_DIS	0x00000800
#define CCSR_SSI_SCR_TFR_CLK_DIS	0x00000400
#define CCSR_SSI_SCR_TCH_EN		0x00000100
#define CCSR_SSI_SCR_SYS_CLK_EN		0x00000080
#define CCSR_SSI_SCR_I2S_MODE_MASK	0x00000060
#define CCSR_SSI_SCR_I2S_MODE_NORMAL	0x00000000
#define CCSR_SSI_SCR_I2S_MODE_MASTER	0x00000020
#define CCSR_SSI_SCR_I2S_MODE_SLAVE	0x00000040
#define CCSR_SSI_SCR_SYN		0x00000010
#define CCSR_SSI_SCR_NET		0x00000008
#define CCSR_SSI_SCR_RE			0x00000004
#define CCSR_SSI_SCR_TE			0x00000002
#define CCSR_SSI_SCR_SSIEN		0x00000001

#define CCSR_SSI_SISR_RFRC		0x01000000
#define CCSR_SSI_SISR_TFRC		0x00800000
#define CCSR_SSI_SISR_CMDAU		0x00040000
#define CCSR_SSI_SISR_CMDDU		0x00020000
#define CCSR_SSI_SISR_RXT		0x00010000
#define CCSR_SSI_SISR_RDR1		0x00008000
#define CCSR_SSI_SISR_RDR0		0x00004000
#define CCSR_SSI_SISR_TDE1		0x00002000
#define CCSR_SSI_SISR_TDE0		0x00001000
#define CCSR_SSI_SISR_ROE1		0x00000800
#define CCSR_SSI_SISR_ROE0		0x00000400
#define CCSR_SSI_SISR_TUE1		0x00000200
#define CCSR_SSI_SISR_TUE0		0x00000100
#define CCSR_SSI_SISR_TFS		0x00000080
#define CCSR_SSI_SISR_RFS		0x00000040
#define CCSR_SSI_SISR_TLS		0x00000020
#define CCSR_SSI_SISR_RLS		0x00000010
#define CCSR_SSI_SISR_RFF1		0x00000008
#define CCSR_SSI_SISR_RFF0		0x00000004
#define CCSR_SSI_SISR_TFE1		0x00000002
#define CCSR_SSI_SISR_TFE0		0x00000001

#define CCSR_SSI_SIER_RFRC_EN		0x01000000
#define CCSR_SSI_SIER_TFRC_EN		0x00800000
#define CCSR_SSI_SIER_RDMAE		0x00400000
#define CCSR_SSI_SIER_RIE		0x00200000
#define CCSR_SSI_SIER_TDMAE		0x00100000
#define CCSR_SSI_SIER_TIE		0x00080000
#define CCSR_SSI_SIER_CMDAU_EN		0x00040000
#define CCSR_SSI_SIER_CMDDU_EN		0x00020000
#define CCSR_SSI_SIER_RXT_EN		0x00010000
#define CCSR_SSI_SIER_RDR1_EN		0x00008000
#define CCSR_SSI_SIER_RDR0_EN		0x00004000
#define CCSR_SSI_SIER_TDE1_EN		0x00002000
#define CCSR_SSI_SIER_TDE0_EN		0x00001000
#define CCSR_SSI_SIER_ROE1_EN		0x00000800
#define CCSR_SSI_SIER_ROE0_EN		0x00000400
#define CCSR_SSI_SIER_TUE1_EN		0x00000200
#define CCSR_SSI_SIER_TUE0_EN		0x00000100
#define CCSR_SSI_SIER_TFS_EN		0x00000080
#define CCSR_SSI_SIER_RFS_EN		0x00000040
#define CCSR_SSI_SIER_TLS_EN		0x00000020
#define CCSR_SSI_SIER_RLS_EN		0x00000010
#define CCSR_SSI_SIER_RFF1_EN		0x00000008
#define CCSR_SSI_SIER_RFF0_EN		0x00000004
#define CCSR_SSI_SIER_TFE1_EN		0x00000002
#define CCSR_SSI_SIER_TFE0_EN		0x00000001

#define CCSR_SSI_STCR_TXBIT0		0x00000200
#define CCSR_SSI_STCR_TFEN1		0x00000100
#define CCSR_SSI_STCR_TFEN0		0x00000080
#define CCSR_SSI_STCR_TFDIR		0x00000040
#define CCSR_SSI_STCR_TXDIR		0x00000020
#define CCSR_SSI_STCR_TSHFD		0x00000010
#define CCSR_SSI_STCR_TSCKP		0x00000008
#define CCSR_SSI_STCR_TFSI		0x00000004
#define CCSR_SSI_STCR_TFSL		0x00000002
#define CCSR_SSI_STCR_TEFS		0x00000001

#define CCSR_SSI_SRCR_RXEXT		0x00000400
#define CCSR_SSI_SRCR_RXBIT0		0x00000200
#define CCSR_SSI_SRCR_RFEN1		0x00000100
#define CCSR_SSI_SRCR_RFEN0		0x00000080
#define CCSR_SSI_SRCR_RFDIR		0x00000040
#define CCSR_SSI_SRCR_RXDIR		0x00000020
#define CCSR_SSI_SRCR_RSHFD		0x00000010
#define CCSR_SSI_SRCR_RSCKP		0x00000008
#define CCSR_SSI_SRCR_RFSI		0x00000004
#define CCSR_SSI_SRCR_RFSL		0x00000002
#define CCSR_SSI_SRCR_REFS		0x00000001

/* STCCR and SRCCR */
#define CCSR_SSI_SxCCR_DIV2		0x00040000
#define CCSR_SSI_SxCCR_PSR		0x00020000
#define CCSR_SSI_SxCCR_WL_SHIFT		13
#define CCSR_SSI_SxCCR_WL_MASK		0x0001E000
#define CCSR_SSI_SxCCR_WL(x) \
	(((((x) / 2) - 1) << CCSR_SSI_SxCCR_WL_SHIFT) & CCSR_SSI_SxCCR_WL_MASK)
#define CCSR_SSI_SxCCR_DC_SHIFT		8
#define CCSR_SSI_SxCCR_DC_MASK		0x00001F00
#define CCSR_SSI_SxCCR_DC(x) \
	((((x) - 1) << CCSR_SSI_SxCCR_DC_SHIFT) & CCSR_SSI_SxCCR_DC_MASK)
#define CCSR_SSI_SxCCR_PM_SHIFT		0
#define CCSR_SSI_SxCCR_PM_MASK		0x000000FF
#define CCSR_SSI_SxCCR_PM(x) \
	((((x) - 1) << CCSR_SSI_SxCCR_PM_SHIFT) & CCSR_SSI_SxCCR_PM_MASK)

/*
 * The xFCNT bits are read-only, and the xFWM bits are read/write.  Use the
 * CCSR_SSI_SFCSR_xFCNTy() macros to read the FIFO counters, and use the
 * CCSR_SSI_SFCSR_xFWMy() macros to set the watermarks.
 */
#define CCSR_SSI_SFCSR_RFCNT1_SHIFT	28
#define CCSR_SSI_SFCSR_RFCNT1_MASK	0xF0000000
#define CCSR_SSI_SFCSR_RFCNT1(x) \
	(((x) & CCSR_SSI_SFCSR_RFCNT1_MASK) >> CCSR_SSI_SFCSR_RFCNT1_SHIFT)
#define CCSR_SSI_SFCSR_TFCNT1_SHIFT	24
#define CCSR_SSI_SFCSR_TFCNT1_MASK	0x0F000000
#define CCSR_SSI_SFCSR_TFCNT1(x) \
	(((x) & CCSR_SSI_SFCSR_TFCNT1_MASK) >> CCSR_SSI_SFCSR_TFCNT1_SHIFT)
#define CCSR_SSI_SFCSR_RFWM1_SHIFT	20
#define CCSR_SSI_SFCSR_RFWM1_MASK	0x00F00000
#define CCSR_SSI_SFCSR_RFWM1(x)	\
	(((x) << CCSR_SSI_SFCSR_RFWM1_SHIFT) & CCSR_SSI_SFCSR_RFWM1_MASK)
#define CCSR_SSI_SFCSR_TFWM1_SHIFT	16
#define CCSR_SSI_SFCSR_TFWM1_MASK	0x000F0000
#define CCSR_SSI_SFCSR_TFWM1(x)	\
	(((x) << CCSR_SSI_SFCSR_TFWM1_SHIFT) & CCSR_SSI_SFCSR_TFWM1_MASK)
#define CCSR_SSI_SFCSR_RFCNT0_SHIFT	12
#define CCSR_SSI_SFCSR_RFCNT0_MASK	0x0000F000
#define CCSR_SSI_SFCSR_RFCNT0(x) \
	(((x) & CCSR_SSI_SFCSR_RFCNT0_MASK) >> CCSR_SSI_SFCSR_RFCNT0_SHIFT)
#define CCSR_SSI_SFCSR_TFCNT0_SHIFT	8
#define CCSR_SSI_SFCSR_TFCNT0_MASK	0x00000F00
#define CCSR_SSI_SFCSR_TFCNT0(x) \
	(((x) & CCSR_SSI_SFCSR_TFCNT0_MASK) >> CCSR_SSI_SFCSR_TFCNT0_SHIFT)
#define CCSR_SSI_SFCSR_RFWM0_SHIFT	4
#define CCSR_SSI_SFCSR_RFWM0_MASK	0x000000F0
#define CCSR_SSI_SFCSR_RFWM0(x)	\
	(((x) << CCSR_SSI_SFCSR_RFWM0_SHIFT) & CCSR_SSI_SFCSR_RFWM0_MASK)
#define CCSR_SSI_SFCSR_TFWM0_SHIFT	0
#define CCSR_SSI_SFCSR_TFWM0_MASK	0x0000000F
#define CCSR_SSI_SFCSR_TFWM0(x)	\
	(((x) << CCSR_SSI_SFCSR_TFWM0_SHIFT) & CCSR_SSI_SFCSR_TFWM0_MASK)

#define CCSR_SSI_STR_TEST		0x00008000
#define CCSR_SSI_STR_RCK2TCK		0x00004000
#define CCSR_SSI_STR_RFS2TFS		0x00002000
#define CCSR_SSI_STR_RXSTATE(x) (((x) >> 8) & 0x1F)
#define CCSR_SSI_STR_TXD2RXD		0x00000080
#define CCSR_SSI_STR_TCK2RCK		0x00000040
#define CCSR_SSI_STR_TFS2RFS		0x00000020
#define CCSR_SSI_STR_TXSTATE(x) ((x) & 0x1F)

#define CCSR_SSI_SOR_CLKOFF		0x00000040
#define CCSR_SSI_SOR_RX_CLR		0x00000020
#define CCSR_SSI_SOR_TX_CLR		0x00000010
#define CCSR_SSI_SOR_INIT		0x00000008
#define CCSR_SSI_SOR_WAIT_SHIFT		1
#define CCSR_SSI_SOR_WAIT_MASK		0x00000006
#define CCSR_SSI_SOR_WAIT(x) (((x) & 3) << CCSR_SSI_SOR_WAIT_SHIFT)
#define CCSR_SSI_SOR_SYNRST 		0x00000001

/* Instantiation data for an SSI interface
 *
 * This structure contains all the information that the the SSI driver needs
 * to instantiate an SSI interface with ALSA.  The machine driver should
 * create this structure, fill it in, call fsl_ssi_create_dai(), and then
 * delete the structure.
 *
 * id: which SSI this is (0, 1, etc. )
 * ssi: pointer to the SSI's registers
 * ssi_phys: physical address of the SSI registers
 * irq: IRQ of this SSI
 * dev: struct device, used to create the sysfs statistics file
*/
struct fsl_ssi_info {
	unsigned int id;
	struct ccsr_ssi __iomem *ssi;
	dma_addr_t ssi_phys;
	unsigned int irq;
	struct device *dev;
};

struct snd_soc_cpu_dai *fsl_ssi_create_dai(struct fsl_ssi_info *ssi_info);
void fsl_ssi_destroy_dai(struct snd_soc_cpu_dai *fsl_ssi_dai);

#endif

