/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2007-2011 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MVREG_H_
#define _MVREG_H_

#include <arm/mv/mvwin.h>

#if defined(SOC_MV_DISCOVERY)
#define IRQ_CAUSE_ERROR		0x0
#define IRQ_CAUSE		0x4
#define IRQ_CAUSE_HI		0x8
#define IRQ_MASK_ERROR		0xC
#define IRQ_MASK		0x10
#define IRQ_MASK_HI		0x14
#define IRQ_CAUSE_SELECT	0x18
#define FIQ_MASK_ERROR		0x1C
#define FIQ_MASK		0x20
#define FIQ_MASK_HI		0x24
#define FIQ_CAUSE_SELECT	0x28
#define ENDPOINT_IRQ_MASK_ERROR(n) 0x2C
#define ENDPOINT_IRQ_MASK(n)	0x30
#define ENDPOINT_IRQ_MASK_HI(n)	0x34
#define ENDPOINT_IRQ_CAUSE_SELECT 0x38
#else
#define IRQ_CAUSE		0x0
#define IRQ_MASK		0x4
#define FIQ_MASK		0x8
#define ENDPOINT_IRQ_MASK(n)	0xC
#define IRQ_CAUSE_HI		0x10
#define IRQ_MASK_HI		0x14
#define FIQ_MASK_HI		0x18
#define ENDPOINT_IRQ_MASK_HI(n)	0x1C
#define ENDPOINT_IRQ_MASK_ERROR(n) (-1)
#define IRQ_CAUSE_ERROR		(-1)		/* Fake defines for unified */
#define IRQ_MASK_ERROR		(-1)		/* interrupt controller code */
#endif

#define MAIN_IRQ_NUM		116
#define ERR_IRQ_NUM		32
#define ERR_IRQ			(MAIN_IRQ_NUM)
#define MSI_IRQ			(ERR_IRQ + ERR_IRQ_NUM)

#define MSI_IRQ_NUM		32

#define IRQ_CPU_SELF		0x00000001
#define	BRIDGE_IRQ_CAUSE_ARMADAXP	0x68
#define	IRQ_TIMER0_ARMADAXP		0x00000001
#define	IRQ_TIMER1_ARMADAXP		0x00000002
#define	IRQ_TIMER_WD_ARMADAXP		0x00000004

#define BRIDGE_IRQ_CAUSE	0x10
#define IRQ_CPU_SELF		0x00000001
#define IRQ_TIMER0		0x00000002
#define IRQ_TIMER1		0x00000004
#define IRQ_TIMER_WD		0x00000008

#define BRIDGE_IRQ_MASK		0x14
#define IRQ_CPU_MASK		0x00000001
#define IRQ_TIMER0_MASK		0x00000002
#define IRQ_TIMER1_MASK		0x00000004
#define IRQ_TIMER_WD_MASK	0x00000008

#define IRQ_CPU_SELF_CLR	(~IRQ_CPU_SELF)
#define IRQ_TIMER0_CLR		(~IRQ_TIMER0)
#define IRQ_TIMER_WD_CLR	(~IRQ_TIMER_WD)

#define	IRQ_TIMER0_CLR_ARMADAXP		(~IRQ_TIMER0_ARMADAXP)
#define	IRQ_TIMER_WD_CLR_ARMADAXP	(~IRQ_TIMER_WD_ARMADAXP)

/*
 * System reset
 */
#define	RSTOUTn_MASK_ARMV7	0x60
#define	SYSTEM_SOFT_RESET_ARMV7	0x64
#define	SOFT_RST_OUT_EN_ARMV7	0x00000001
#define	SYS_SOFT_RST_ARMV7	0x00000001

#define RSTOUTn_MASK		0x8
#define SOFT_RST_OUT_EN		0x00000004
#define SYSTEM_SOFT_RESET	0xc
#define SYS_SOFT_RST		0x00000001
#define RSTOUTn_MASK_WD		0x400
#define WD_RSTOUTn_MASK		0x4
#define WD_GLOBAL_MASK		0x00000100
#define WD_CPU0_MASK		0x00000001
#define WD_RST_OUT_EN		0x00000002

/*
 * Power Control
 */
#if defined(SOC_MV_KIRKWOOD)
#define CPU_PM_CTRL		0x18
#else
#define CPU_PM_CTRL		0x1C
#endif
#define CPU_PM_CTRL_NONE	0
#define CPU_PM_CTRL_ALL		~0x0

#if defined(SOC_MV_KIRKWOOD)
#define CPU_PM_CTRL_GE0		(1 << 0)
#define CPU_PM_CTRL_PEX0_PHY	(1 << 1)
#define CPU_PM_CTRL_PEX0	(1 << 2)
#define CPU_PM_CTRL_USB0	(1 << 3)
#define CPU_PM_CTRL_SDIO	(1 << 4)
#define CPU_PM_CTRL_TSU		(1 << 5)
#define CPU_PM_CTRL_DUNIT	(1 << 6)
#define CPU_PM_CTRL_RUNIT	(1 << 7)
#define CPU_PM_CTRL_XOR0	(1 << 8)
#define CPU_PM_CTRL_AUDIO	(1 << 9)
#define CPU_PM_CTRL_SATA0	(1 << 14)
#define CPU_PM_CTRL_SATA1	(1 << 15)
#define CPU_PM_CTRL_XOR1	(1 << 16)
#define CPU_PM_CTRL_CRYPTO	(1 << 17)
#define CPU_PM_CTRL_GE1		(1 << 19)
#define CPU_PM_CTRL_TDM		(1 << 20)
#define CPU_PM_CTRL_XOR		(CPU_PM_CTRL_XOR0 | CPU_PM_CTRL_XOR1)
#define CPU_PM_CTRL_USB(u)	(CPU_PM_CTRL_USB0)
#define CPU_PM_CTRL_SATA	(CPU_PM_CTRL_SATA0 | CPU_PM_CTRL_SATA1)
#define CPU_PM_CTRL_GE(u)	(CPU_PM_CTRL_GE1 * (u) | CPU_PM_CTRL_GE0 * \
				(1 - (u)))
#define CPU_PM_CTRL_IDMA	(CPU_PM_CTRL_NONE)
#elif defined(SOC_MV_DISCOVERY)
#define CPU_PM_CTRL_GE0		(1 << 1)
#define CPU_PM_CTRL_GE1		(1 << 2)
#define CPU_PM_CTRL_PEX00	(1 << 5)
#define CPU_PM_CTRL_PEX01	(1 << 6)
#define CPU_PM_CTRL_PEX02	(1 << 7)
#define CPU_PM_CTRL_PEX03	(1 << 8)
#define CPU_PM_CTRL_PEX10	(1 << 9)
#define CPU_PM_CTRL_PEX11	(1 << 10)
#define CPU_PM_CTRL_PEX12	(1 << 11)
#define CPU_PM_CTRL_PEX13	(1 << 12)
#define CPU_PM_CTRL_SATA0_PHY	(1 << 13)
#define CPU_PM_CTRL_SATA0	(1 << 14)
#define CPU_PM_CTRL_SATA1_PHY	(1 << 15)
#define CPU_PM_CTRL_SATA1	(1 << 16)
#define CPU_PM_CTRL_USB0	(1 << 17)
#define CPU_PM_CTRL_USB1	(1 << 18)
#define CPU_PM_CTRL_USB2	(1 << 19)
#define CPU_PM_CTRL_IDMA	(1 << 20)
#define CPU_PM_CTRL_XOR		(1 << 21)
#define CPU_PM_CTRL_CRYPTO	(1 << 22)
#define CPU_PM_CTRL_DEVICE	(1 << 23)
#define CPU_PM_CTRL_USB(u)	(1 << (17 + (u)))
#define CPU_PM_CTRL_SATA	(CPU_PM_CTRL_SATA0 | CPU_PM_CTRL_SATA1)
#define CPU_PM_CTRL_GE(u)	(CPU_PM_CTRL_GE1 * (u) | CPU_PM_CTRL_GE0 * \
				(1 - (u)))
#else
#define CPU_PM_CTRL_CRYPTO	(CPU_PM_CTRL_NONE)
#define CPU_PM_CTRL_IDMA	(CPU_PM_CTRL_NONE)
#define CPU_PM_CTRL_XOR		(CPU_PM_CTRL_NONE)
#define CPU_PM_CTRL_SATA	(CPU_PM_CTRL_NONE)
#define CPU_PM_CTRL_USB(u)	(CPU_PM_CTRL_NONE)
#define CPU_PM_CTRL_GE(u)	(CPU_PM_CTRL_NONE)
#endif

/*
 * Timers
 */
#define CPU_TIMERS_BASE		0x300
#define CPU_TIMER_CONTROL	0x0
#define CPU_TIMER0_EN		0x00000001
#define CPU_TIMER0_AUTO		0x00000002
#define CPU_TIMER1_EN		0x00000004
#define CPU_TIMER1_AUTO		0x00000008
#define	CPU_TIMER2_EN		0x00000010
#define	CPU_TIMER2_AUTO		0x00000020
#define	CPU_TIMER_WD_EN		0x00000100
#define	CPU_TIMER_WD_AUTO	0x00000200
/* 25MHz mode is Armada XP - specific */
#define CPU_TIMER_WD_25MHZ_EN	0x00000400
#define CPU_TIMER0_25MHZ_EN	0x00000800
#define CPU_TIMER1_25MHZ_EN	0x00001000
#define CPU_TIMER0_REL		0x10
#define CPU_TIMER0		0x14

/*
 * SATA
 */
#define SATA_CHAN_NUM			2

#define EDMA_REGISTERS_OFFSET		0x2000
#define EDMA_REGISTERS_SIZE		0x2000
#define SATA_EDMA_BASE(ch)		(EDMA_REGISTERS_OFFSET + \
    ((ch) * EDMA_REGISTERS_SIZE))

/* SATAHC registers */
#define SATA_CR				0x000 /* Configuration Reg. */
#define SATA_CR_NODMABS			(1 << 8)
#define SATA_CR_NOEDMABS		(1 << 9)
#define SATA_CR_NOPRDPBS		(1 << 10)
#define SATA_CR_COALDIS(ch)		(1 << (24 + ch))

/* Interrupt Coalescing Threshold Reg. */
#define SATA_ICTR			0x00C
#define SATA_ICTR_MAX			((1 << 8) - 1)

/* Interrupt Time Threshold Reg. */
#define SATA_ITTR			0x010
#define SATA_ITTR_MAX			((1 << 24) - 1)

#define SATA_ICR			0x014 /* Interrupt Cause Reg. */
#define SATA_ICR_DMADONE(ch)		(1 << (ch))
#define SATA_ICR_COAL			(1 << 4)
#define SATA_ICR_DEV(ch)		(1 << (8 + ch))

#define SATA_MICR			0x020 /* Main Interrupt Cause Reg. */
#define SATA_MICR_ERR(ch)		(1 << (2 * ch))
#define SATA_MICR_DONE(ch)		(1 << ((2 * ch) + 1))
#define SATA_MICR_DMADONE(ch)		(1 << (4 + ch))
#define SATA_MICR_COAL			(1 << 8)

#define SATA_MIMR			0x024 /*  Main Interrupt Mask Reg. */

/* Shadow registers */
#define SATA_SHADOWR_BASE(ch)		(SATA_EDMA_BASE(ch) + 0x100)
#define SATA_SHADOWR_CONTROL(ch)	(SATA_EDMA_BASE(ch) + 0x120)

/* SATA registers */
#define SATA_SATA_SSTATUS(ch)		(SATA_EDMA_BASE(ch) + 0x300)
#define SATA_SATA_SERROR(ch)		(SATA_EDMA_BASE(ch) + 0x304)
#define SATA_SATA_SCONTROL(ch)		(SATA_EDMA_BASE(ch) + 0x308)
#define SATA_SATA_FISICR(ch)		(SATA_EDMA_BASE(ch) + 0x364)

/* EDMA registers */
#define SATA_EDMA_CFG(ch)		(SATA_EDMA_BASE(ch) + 0x000)
#define SATA_EDMA_CFG_QL128		(1 << 19)
#define SATA_EDMA_CFG_HQCACHE		(1 << 22)

#define SATA_EDMA_IECR(ch)		(SATA_EDMA_BASE(ch) + 0x008)

#define SATA_EDMA_IEMR(ch)		(SATA_EDMA_BASE(ch) + 0x00C)
#define SATA_EDMA_REQBAHR(ch)		(SATA_EDMA_BASE(ch) + 0x010)
#define SATA_EDMA_REQIPR(ch)		(SATA_EDMA_BASE(ch) + 0x014)
#define SATA_EDMA_REQOPR(ch)		(SATA_EDMA_BASE(ch) + 0x018)
#define SATA_EDMA_RESBAHR(ch)		(SATA_EDMA_BASE(ch) + 0x01C)
#define SATA_EDMA_RESIPR(ch)		(SATA_EDMA_BASE(ch) + 0x020)
#define SATA_EDMA_RESOPR(ch)		(SATA_EDMA_BASE(ch) + 0x024)

#define SATA_EDMA_CMD(ch)		(SATA_EDMA_BASE(ch) + 0x028)
#define SATA_EDMA_CMD_ENABLE		(1 << 0)
#define SATA_EDMA_CMD_DISABLE		(1 << 1)
#define SATA_EDMA_CMD_RESET		(1 << 2)

#define SATA_EDMA_STATUS(ch)		(SATA_EDMA_BASE(ch) + 0x030)
#define SATA_EDMA_STATUS_IDLE		(1 << 7)

/* Offset to extract input slot from REQIPR register */
#define SATA_EDMA_REQIS_OFS		5

/* Offset to extract input slot from RESOPR register */
#define SATA_EDMA_RESOS_OFS		3

/*
 * GPIO
 */
#define GPIO_DATA_OUT		0x00
#define GPIO_DATA_OUT_EN_CTRL	0x04
#define GPIO_BLINK_EN		0x08
#define GPIO_DATA_IN_POLAR	0x0c
#define GPIO_DATA_IN		0x10
#define GPIO_INT_CAUSE		0x14
#define GPIO_INT_EDGE_MASK	0x18
#define GPIO_INT_LEV_MASK	0x1c

#define GPIO(n)			(1 << (n))
#define MV_GPIO_MAX_NPINS	64

#define MV_GPIO_IN_NONE			0x0
#define MV_GPIO_IN_POL_LOW		(1 << 16)
#define MV_GPIO_IN_IRQ_EDGE		(2 << 16)
#define MV_GPIO_IN_IRQ_LEVEL		(4 << 16)
#define MV_GPIO_IN_IRQ_DOUBLE_EDGE	(8 << 16)
#define MV_GPIO_IN_DEBOUNCE		(16 << 16)
#define MV_GPIO_OUT_NONE		0x0
#define MV_GPIO_OUT_BLINK		0x1
#define MV_GPIO_OUT_OPEN_DRAIN		0x2
#define MV_GPIO_OUT_OPEN_SRC		0x4

#define IS_GPIO_IRQ(irq)	((irq) >= NIRQ && (irq) < NIRQ + MV_GPIO_MAX_NPINS)
#define GPIO2IRQ(gpio)		((gpio) + NIRQ)
#define IRQ2GPIO(irq)		((irq) - NIRQ)

#if defined(SOC_MV_ORION)
#define SAMPLE_AT_RESET		0x10
#elif defined(SOC_MV_KIRKWOOD)
#define SAMPLE_AT_RESET		0x30
#endif
#define	SAMPLE_AT_RESET_ARMADA38X	0x400
#define	SAMPLE_AT_RESET_LO		0x30
#define	SAMPLE_AT_RESET_HI		0x34

/*
 * Clocks
 */
#if defined(SOC_MV_ORION)
#define TCLK_MASK		0x00000300
#define TCLK_SHIFT		0x08
#elif defined(SOC_MV_DISCOVERY)
#define TCLK_MASK		0x00000180
#define TCLK_SHIFT		0x07
#endif

#define	TCLK_MASK_ARMADA38X		0x00008000
#define	TCLK_SHIFT_ARMADA38X		15

#define TCLK_100MHZ		100000000
#define TCLK_125MHZ		125000000
#define TCLK_133MHZ		133333333
#define TCLK_150MHZ		150000000
#define TCLK_166MHZ		166666667
#define TCLK_200MHZ		200000000
#define TCLK_250MHZ		250000000
#define TCLK_300MHZ		300000000
#define TCLK_667MHZ		667000000

#define	A38X_CPU_DDR_CLK_MASK	0x00007c00
#define	A38X_CPU_DDR_CLK_SHIFT	10

/*
 * CPU Cache Configuration
 */

#define CPU_CONFIG		0x00000000
#define CPU_CONFIG_IC_PREF	0x00010000
#define CPU_CONFIG_DC_PREF	0x00020000
#define CPU_CONTROL		0x00000004
#define CPU_CONTROL_L2_SIZE	0x00200000	/* Only on Discovery */
#define CPU_CONTROL_L2_MODE	0x00020000	/* Only on Discovery */
#define CPU_L2_CONFIG		0x00000028	/* Only on Kirkwood */
#define CPU_L2_CONFIG_MODE	0x00000010	/* Only on Kirkwood */

/*
 * PCI Express port control (CPU Control registers)
 */
#define CPU_CONTROL_PCIE_DISABLE(n)	(1 << (3 * (n)))

/*
 * Vendor ID
 */
#define PCI_VENDORID_MRVL	0x11AB
#define PCI_VENDORID_MRVL2	0x1B4B

/*
 * Chip ID
 */
#define MV_DEV_88F5181		0x5181
#define MV_DEV_88F5182		0x5182
#define MV_DEV_88F5281		0x5281
#define MV_DEV_88F6281		0x6281
#define MV_DEV_88F6282		0x6282
#define MV_DEV_88F6781		0x6781
#define MV_DEV_88F6828		0x6828
#define MV_DEV_88F6820		0x6820
#define MV_DEV_88F6810		0x6810
#define MV_DEV_MV78100_Z0	0x6381
#define MV_DEV_MV78100		0x7810
#define MV_DEV_MV78130		0x7813
#define MV_DEV_MV78160		0x7816
#define MV_DEV_MV78230		0x7823
#define MV_DEV_MV78260		0x7826
#define MV_DEV_MV78460		0x7846
#define MV_DEV_88RC8180		0x8180
#define MV_DEV_88RC9480		0x9480
#define MV_DEV_88RC9580		0x9580

#define MV_DEV_FAMILY_MASK	0xff00
#define MV_DEV_DISCOVERY	0x7800
#define	MV_DEV_ARMADA38X	0x6800

/*
 * Doorbell register control
 */
#define MV_DRBL_PCIE_TO_CPU	0
#define MV_DRBL_CPU_TO_PCIE	1

#define MV_DRBL_CAUSE(d,u)	(0x10 * (u) + 0x8 * (d))
#define MV_DRBL_MASK(d,u)	(0x10 * (u) + 0x8 * (d) + 0x4)
#define MV_DRBL_MSG(m,d,u)	(0x10 * (u) + 0x8 * (d) + 0x4 * (m) + 0x30)

/*
 * SCU
 */
#define	MV_SCU_BASE		(MV_BASE + 0xc000)
#define	MV_SCU_REGS_LEN		0x100
#define	MV_SCU_REG_CTRL		0x00
#define	MV_SCU_REG_CONFIG	0x04
#define	MV_SCU_ENABLE		(1 << 0)
#define	MV_SCU_SL_L2_ENABLE	(1 << 3)
#define	SCU_CFG_REG_NCPU_MASK	0x3

/*
 * PMSU
 */
#define	MV_PMSU_BASE		(MV_BASE + 0x22000)
#define	MV_PMSU_REGS_LEN	0x1000
#define	PMSU_BOOT_ADDR_REDIRECT_OFFSET(cpu)	(((cpu) * 0x100) + 0x124)

/*
 * CPU RESET
 */
#define	MV_CPU_RESET_BASE	(MV_BASE + 0x20800)
#define	MV_CPU_RESET_REGS_LEN	0x8
#define	CPU_RESET_OFFSET(cpu)	((cpu) * 0x8)
#define	CPU_RESET_ASSERT	0x1

#define	MV_MBUS_CTRL_BASE	(MV_BASE + 0x20420)
#define	MV_MBUS_CTRL_REGS_LEN	0x10

#endif /* _MVREG_H_ */
