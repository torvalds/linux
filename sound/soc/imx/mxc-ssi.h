/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IMX_SSI_H
#define _IMX_SSI_H

#include <mach/hardware.h>

/* SSI regs definition - MOVE to /arch/arm/plat-mxc/include/mach/ when stable */
#define SSI1_IO_BASE_ADDR	IO_ADDRESS(SSI1_BASE_ADDR)
#define SSI2_IO_BASE_ADDR	IO_ADDRESS(SSI2_BASE_ADDR)

#define STX0   0x00
#define STX1   0x04
#define SRX0   0x08
#define SRX1   0x0c
#define SCR    0x10
#define SISR   0x14
#define SIER   0x18
#define STCR   0x1c
#define SRCR   0x20
#define STCCR  0x24
#define SRCCR  0x28
#define SFCSR  0x2c
#define STR    0x30
#define SOR    0x34
#define SACNT  0x38
#define SACADD 0x3c
#define SACDAT 0x40
#define SATAG  0x44
#define STMSK  0x48
#define SRMSK  0x4c

#define SSI1_STX0	(*((volatile u32 *)(SSI1_IO_BASE_ADDR + STX0)))
#define SSI1_STX1   (*((volatile u32 *)(SSI1_IO_BASE_ADDR + STX1)))
#define SSI1_SRX0   (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SRX0)))
#define SSI1_SRX1   (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SRX1)))
#define SSI1_SCR    (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SCR)))
#define SSI1_SISR   (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SISR)))
#define SSI1_SIER   (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SIER)))
#define SSI1_STCR   (*((volatile u32 *)(SSI1_IO_BASE_ADDR + STCR)))
#define SSI1_SRCR   (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SRCR)))
#define SSI1_STCCR  (*((volatile u32 *)(SSI1_IO_BASE_ADDR + STCCR)))
#define SSI1_SRCCR  (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SRCCR)))
#define SSI1_SFCSR  (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SFCSR)))
#define SSI1_STR    (*((volatile u32 *)(SSI1_IO_BASE_ADDR + STR)))
#define SSI1_SOR    (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SOR)))
#define SSI1_SACNT  (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SACNT)))
#define SSI1_SACADD (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SACADD)))
#define SSI1_SACDAT (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SACDAT)))
#define SSI1_SATAG  (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SATAG)))
#define SSI1_STMSK  (*((volatile u32 *)(SSI1_IO_BASE_ADDR + STMSK)))
#define SSI1_SRMSK  (*((volatile u32 *)(SSI1_IO_BASE_ADDR + SRMSK)))


#define SSI2_STX0	(*((volatile u32 *)(SSI2_IO_BASE_ADDR + STX0)))
#define SSI2_STX1   (*((volatile u32 *)(SSI2_IO_BASE_ADDR + STX1)))
#define SSI2_SRX0   (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SRX0)))
#define SSI2_SRX1   (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SRX1)))
#define SSI2_SCR    (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SCR)))
#define SSI2_SISR   (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SISR)))
#define SSI2_SIER   (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SIER)))
#define SSI2_STCR   (*((volatile u32 *)(SSI2_IO_BASE_ADDR + STCR)))
#define SSI2_SRCR   (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SRCR)))
#define SSI2_STCCR  (*((volatile u32 *)(SSI2_IO_BASE_ADDR + STCCR)))
#define SSI2_SRCCR  (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SRCCR)))
#define SSI2_SFCSR  (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SFCSR)))
#define SSI2_STR    (*((volatile u32 *)(SSI2_IO_BASE_ADDR + STR)))
#define SSI2_SOR    (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SOR)))
#define SSI2_SACNT  (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SACNT)))
#define SSI2_SACADD (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SACADD)))
#define SSI2_SACDAT (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SACDAT)))
#define SSI2_SATAG  (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SATAG)))
#define SSI2_STMSK  (*((volatile u32 *)(SSI2_IO_BASE_ADDR + STMSK)))
#define SSI2_SRMSK  (*((volatile u32 *)(SSI2_IO_BASE_ADDR + SRMSK)))

#define SSI_SCR_CLK_IST        (1 << 9)
#define SSI_SCR_TCH_EN         (1 << 8)
#define SSI_SCR_SYS_CLK_EN     (1 << 7)
#define SSI_SCR_I2S_MODE_NORM  (0 << 5)
#define SSI_SCR_I2S_MODE_MSTR  (1 << 5)
#define SSI_SCR_I2S_MODE_SLAVE (2 << 5)
#define SSI_SCR_SYN            (1 << 4)
#define SSI_SCR_NET            (1 << 3)
#define SSI_SCR_RE             (1 << 2)
#define SSI_SCR_TE             (1 << 1)
#define SSI_SCR_SSIEN          (1 << 0)

#define SSI_SISR_CMDAU         (1 << 18)
#define SSI_SISR_CMDDU         (1 << 17)
#define SSI_SISR_RXT           (1 << 16)
#define SSI_SISR_RDR1          (1 << 15)
#define SSI_SISR_RDR0          (1 << 14)
#define SSI_SISR_TDE1          (1 << 13)
#define SSI_SISR_TDE0          (1 << 12)
#define SSI_SISR_ROE1          (1 << 11)
#define SSI_SISR_ROE0          (1 << 10)
#define SSI_SISR_TUE1          (1 << 9)
#define SSI_SISR_TUE0          (1 << 8)
#define SSI_SISR_TFS           (1 << 7)
#define SSI_SISR_RFS           (1 << 6)
#define SSI_SISR_TLS           (1 << 5)
#define SSI_SISR_RLS           (1 << 4)
#define SSI_SISR_RFF1          (1 << 3)
#define SSI_SISR_RFF0          (1 << 2)
#define SSI_SISR_TFE1          (1 << 1)
#define SSI_SISR_TFE0          (1 << 0)

#define SSI_SIER_RDMAE         (1 << 22)
#define SSI_SIER_RIE           (1 << 21)
#define SSI_SIER_TDMAE         (1 << 20)
#define SSI_SIER_TIE           (1 << 19)
#define SSI_SIER_CMDAU_EN      (1 << 18)
#define SSI_SIER_CMDDU_EN      (1 << 17)
#define SSI_SIER_RXT_EN        (1 << 16)
#define SSI_SIER_RDR1_EN       (1 << 15)
#define SSI_SIER_RDR0_EN       (1 << 14)
#define SSI_SIER_TDE1_EN       (1 << 13)
#define SSI_SIER_TDE0_EN       (1 << 12)
#define SSI_SIER_ROE1_EN       (1 << 11)
#define SSI_SIER_ROE0_EN       (1 << 10)
#define SSI_SIER_TUE1_EN       (1 << 9)
#define SSI_SIER_TUE0_EN       (1 << 8)
#define SSI_SIER_TFS_EN        (1 << 7)
#define SSI_SIER_RFS_EN        (1 << 6)
#define SSI_SIER_TLS_EN        (1 << 5)
#define SSI_SIER_RLS_EN        (1 << 4)
#define SSI_SIER_RFF1_EN       (1 << 3)
#define SSI_SIER_RFF0_EN       (1 << 2)
#define SSI_SIER_TFE1_EN       (1 << 1)
#define SSI_SIER_TFE0_EN       (1 << 0)

#define SSI_STCR_TXBIT0        (1 << 9)
#define SSI_STCR_TFEN1         (1 << 8)
#define SSI_STCR_TFEN0         (1 << 7)
#define SSI_STCR_TFDIR         (1 << 6)
#define SSI_STCR_TXDIR         (1 << 5)
#define SSI_STCR_TSHFD         (1 << 4)
#define SSI_STCR_TSCKP         (1 << 3)
#define SSI_STCR_TFSI          (1 << 2)
#define SSI_STCR_TFSL          (1 << 1)
#define SSI_STCR_TEFS          (1 << 0)

#define SSI_SRCR_RXBIT0        (1 << 9)
#define SSI_SRCR_RFEN1         (1 << 8)
#define SSI_SRCR_RFEN0         (1 << 7)
#define SSI_SRCR_RFDIR         (1 << 6)
#define SSI_SRCR_RXDIR         (1 << 5)
#define SSI_SRCR_RSHFD         (1 << 4)
#define SSI_SRCR_RSCKP         (1 << 3)
#define SSI_SRCR_RFSI          (1 << 2)
#define SSI_SRCR_RFSL          (1 << 1)
#define SSI_SRCR_REFS          (1 << 0)

#define SSI_STCCR_DIV2         (1 << 18)
#define SSI_STCCR_PSR          (1 << 15)
#define SSI_STCCR_WL(x)        ((((x) - 2) >> 1) << 13)
#define SSI_STCCR_DC(x)        (((x) & 0x1f) << 8)
#define SSI_STCCR_PM(x)        (((x) & 0xff) << 0)
#define SSI_STCCR_WL_MASK        (0xf << 13)
#define SSI_STCCR_DC_MASK        (0x1f << 8)
#define SSI_STCCR_PM_MASK        (0xff << 0)

#define SSI_SRCCR_DIV2         (1 << 18)
#define SSI_SRCCR_PSR          (1 << 15)
#define SSI_SRCCR_WL(x)        ((((x) - 2) >> 1) << 13)
#define SSI_SRCCR_DC(x)        (((x) & 0x1f) << 8)
#define SSI_SRCCR_PM(x)        (((x) & 0xff) << 0)
#define SSI_SRCCR_WL_MASK        (0xf << 13)
#define SSI_SRCCR_DC_MASK        (0x1f << 8)
#define SSI_SRCCR_PM_MASK        (0xff << 0)


#define SSI_SFCSR_RFCNT1(x)   (((x) & 0xf) << 28)
#define SSI_SFCSR_TFCNT1(x)   (((x) & 0xf) << 24)
#define SSI_SFCSR_RFWM1(x)    (((x) & 0xf) << 20)
#define SSI_SFCSR_TFWM1(x)    (((x) & 0xf) << 16)
#define SSI_SFCSR_RFCNT0(x)   (((x) & 0xf) << 12)
#define SSI_SFCSR_TFCNT0(x)   (((x) & 0xf) <<  8)
#define SSI_SFCSR_RFWM0(x)    (((x) & 0xf) <<  4)
#define SSI_SFCSR_TFWM0(x)    (((x) & 0xf) <<  0)

#define SSI_STR_TEST          (1 << 15)
#define SSI_STR_RCK2TCK       (1 << 14)
#define SSI_STR_RFS2TFS       (1 << 13)
#define SSI_STR_RXSTATE(x)    (((x) & 0xf) << 8)
#define SSI_STR_TXD2RXD       (1 <<  7)
#define SSI_STR_TCK2RCK       (1 <<  6)
#define SSI_STR_TFS2RFS       (1 <<  5)
#define SSI_STR_TXSTATE(x)    (((x) & 0xf) << 0)

#define SSI_SOR_CLKOFF        (1 << 6)
#define SSI_SOR_RX_CLR        (1 << 5)
#define SSI_SOR_TX_CLR        (1 << 4)
#define SSI_SOR_INIT          (1 << 3)
#define SSI_SOR_WAIT(x)       (((x) & 0x3) << 1)
#define SSI_SOR_SYNRST        (1 << 0)

#define SSI_SACNT_FRDIV(x)    (((x) & 0x3f) << 5)
#define SSI_SACNT_WR          (x << 4)
#define SSI_SACNT_RD          (x << 3)
#define SSI_SACNT_TIF         (x << 2)
#define SSI_SACNT_FV          (x << 1)
#define SSI_SACNT_AC97EN      (x << 0)

/* Watermarks for FIFO's */
#define TXFIFO_WATERMARK				0x4
#define RXFIFO_WATERMARK				0x4

/* i.MX DAI SSP ID's */
#define IMX_DAI_SSI0			0 /* SSI1 FIFO 0 */
#define IMX_DAI_SSI1			1 /* SSI1 FIFO 1 */
#define IMX_DAI_SSI2			2 /* SSI2 FIFO 0 */
#define IMX_DAI_SSI3			3 /* SSI2 FIFO 1 */

/* SSI clock sources */
#define IMX_SSP_SYS_CLK		0

/* SSI audio dividers */
#define IMX_SSI_TX_DIV_2			0
#define IMX_SSI_TX_DIV_PSR			1
#define IMX_SSI_TX_DIV_PM			2
#define IMX_SSI_RX_DIV_2			3
#define IMX_SSI_RX_DIV_PSR			4
#define IMX_SSI_RX_DIV_PM			5


/* SSI Div 2 */
#define IMX_SSI_DIV_2_OFF		(~SSI_STCCR_DIV2)
#define IMX_SSI_DIV_2_ON		SSI_STCCR_DIV2

extern struct snd_soc_dai imx_ssi_pcm_dai[4];
extern int get_ssi_clk(int ssi, struct device *dev);
extern void put_ssi_clk(int ssi);
#endif
