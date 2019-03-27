/*	$NetBSD: ingenic_regs.h,v 1.22 2015/10/08 17:54:30 macallan Exp $ */

/*-
 * Copyright (c) 2014 Michael Lorenz
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef JZ4780_REGS_H
#define JZ4780_REGS_H

/* for mips_wbflush() */
#include <machine/locore.h>

/* UARTs, mostly 16550 compatible with 32bit spaced registers */
#define JZ_UART0 0x10030000
#define JZ_UART1 0x10031000
#define JZ_UART2 0x10032000
#define JZ_UART3 0x10033000
#define JZ_UART4 0x10034000

/* LCD controller base addresses, registers are in jzfb_regs.h */
#define JZ_LCDC0_BASE 0x13050000
#define JZ_LCDC1_BASE 0x130a0000

/* TCU unit base address */
#define JZ_TCU_BASE	0x10002000

/* Watchdog */
#define JZ_WDOG_TDR	0x00000000	/* compare */
#define JZ_WDOG_TCER	0x00000004
	#define TCER_ENABLE	0x01	/* enable counter */
#define JZ_WDOG_TCNT	0x00000008	/* 16bit up count */
#define JZ_WDOG_TCSR	0x0000000c
	#define TCSR_PCK_EN	0x01	/* PCLK */
	#define TCSR_RTC_EN	0x02	/* RTCCLK - 32.768kHz */
	#define TCSR_EXT_EN	0x04	/* EXTCLK - 48MHz */
	#define TCSR_PRESCALE_M	0x38
	#define TCSR_DIV_1	0x00
	#define TCSR_DIV_4	0x08
	#define TCSR_DIV_16	0x10
	#define TCSR_DIV_64	0x18
	#define TCSR_DIV_256	0x20
	#define TCSR_DIV_1024	0x28

/* timers and PWMs */
#define JZ_TC_TER	0x00000010	/* TC enable reg, ro */
#define JZ_TC_TESR	0x00000014	/* TC enable set reg. */
	#define TESR_TCST0	0x0001	/* enable counter 0 */
	#define TESR_TCST1	0x0002	/* enable counter 1 */
	#define TESR_TCST2	0x0004	/* enable counter 2 */
	#define TESR_TCST3	0x0008	/* enable counter 3 */
	#define TESR_TCST4	0x0010	/* enable counter 4 */
	#define TESR_TCST5	0x0020	/* enable counter 5 */
	#define TESR_TCST6	0x0040	/* enable counter 6 */
	#define TESR_TCST7	0x0080	/* enable counter 7 */
	#define TESR_OST	0x8000	/* enable OST */
#define JZ_TC_TECR	0x00000018	/* TC enable clear reg. */
#define JZ_TC_TFR	0x00000020
	#define TFR_FFLAG0	0x00000001	/* channel 0 */
	#define TFR_FFLAG1	0x00000002	/* channel 1 */
	#define TFR_FFLAG2	0x00000004	/* channel 2 */
	#define TFR_FFLAG3	0x00000008	/* channel 3 */
	#define TFR_FFLAG4	0x00000010	/* channel 4 */
	#define TFR_FFLAG5	0x00000020	/* channel 5 */
	#define TFR_FFLAG6	0x00000040	/* channel 6 */
	#define TFR_FFLAG7	0x00000080	/* channel 7 */
	#define TFR_OSTFLAG	0x00008000	/* OS timer */
#define JZ_TC_TFSR	0x00000024	/* timer flag set */
#define JZ_TC_TFCR	0x00000028	/* timer flag clear */
#define JZ_TC_TMR	0x00000030	/* timer flag mask */
	#define TMR_FMASK(n)	(1 << (n))
	#define TMR_HMASK(n)	(1 << ((n) + 16))
#define JZ_TC_TMSR	0x00000034	/* timer flag mask set */
#define JZ_TC_TMCR	0x00000038	/* timer flag mask clear*/

#define JZ_TC_TDFR(n)	(0x00000040 + (n * 0x10))	/* FULL compare */
#define JZ_TC_TDHR(n)	(0x00000044 + (n * 0x10))	/* HALF compare */
#define JZ_TC_TCNT(n)	(0x00000048 + (n * 0x10))	/* count */

#define JZ_TC_TCSR(n)	(0x0000004c + (n * 0x10))
/* same bits as in JZ_WDOG_TCSR	*/

/* operating system timer */
#define JZ_OST_DATA	0x000000e0	/* compare */
#define JZ_OST_CNT_LO	0x000000e4
#define JZ_OST_CNT_HI	0x000000e8
#define JZ_OST_CTRL	0x000000ec
	#define OSTC_PCK_EN	0x0001	/* use PCLK */
	#define OSTC_RTC_EN	0x0002	/* use RTCCLK */
	#define OSTC_EXT_EN	0x0004	/* use EXTCLK */
	#define OSTC_PRESCALE_M	0x0038
	#define OSTC_DIV_1	0x0000
	#define OSTC_DIV_4	0x0008
	#define OSTC_DIV_16	0x0010
	#define OSTC_DIV_64	0x0018
	#define OSTC_DIV_256	0x0020
	#define OSTC_DIV_1024	0x0028
	#define OSTC_SHUTDOWN	0x0200
	#define OSTC_MODE	0x8000	/* 0 - reset to 0 when = OST_DATA */
#define JZ_OST_CNT_U32	0x000000fc	/* copy of CNT_HI when reading CNT_LO */

static inline void
writereg(uint32_t reg, uint32_t val)
{
	*(volatile int32_t *)MIPS_PHYS_TO_KSEG1(reg) = val;
	mips_wbflush();
}

static inline uint32_t
readreg(uint32_t reg)
{
	mips_wbflush();
	return *(volatile int32_t *)MIPS_PHYS_TO_KSEG1(reg);
}

/* Clock management */
#define JZ_CGU_BASE	0x10000000

#define JZ_CPCCR	0x00000000	/* Clock Control Register */
	#define JZ_PDIV_M	0x000f0000	/* PCLK divider mask */
	#define JZ_PDIV_S	16		/* PCLK divider shift */
	#define JZ_CDIV_M	0x0000000f	/* CPU clock divider mask */
	#define JZ_CDIV_S	0		/* CPU clock divider shift */
#define JZ_CPAPCR	0x00000010	/* APLL */
#define JZ_CPMPCR	0x00000014	/* MPLL */
#define JZ_CPEPCR	0x00000018	/* EPLL */
#define JZ_CPVPCR	0x0000001C	/* VPLL */
	#define JZ_PLLM_S	19		/* PLL multiplier shift */
	#define JZ_PLLM_M	0xfff80000	/* PLL multiplier mask */
	#define JZ_PLLN_S	13		/* PLL divider shift */
	#define JZ_PLLN_M	0x0007e000	/* PLL divider mask */
	#define JZ_PLLP_S	9		/* PLL postdivider shift */
	#define JZ_PLLP_M	0x00001700	/* PLL postdivider mask */
	#define JZ_PLLON	0x00000010	/* PLL is on and stable */
	#define JZ_PLLBP	0x00000002	/* PLL bypass */
	#define JZ_PLLEN	0x00000001	/* PLL enable */
#define JZ_CLKGR0	0x00000020	/* Clock Gating Registers */
	#define CLK_NEMC	(1 << 0)
	#define CLK_BCH		(1 << 1)
	#define CLK_OTG0	(1 << 2)
	#define CLK_MSC0	(1 << 3)
	#define CLK_SSI0	(1 << 4)
	#define CLK_SMB0	(1 << 5)
	#define CLK_SMB1	(1 << 6)
	#define CLK_SCC		(1 << 7)
	#define CLK_AIC		(1 << 8)
	#define CLK_TSSI0	(1 << 9)
	#define CLK_OWI		(1 << 10)
	#define CLK_MSC1	(1 << 11)
	#define CLK_MSC2	(1 << 12)
	#define CLK_KBC		(1 << 13)
	#define CLK_SADC	(1 << 14)
	#define CLK_UART0	(1 << 15)
	#define CLK_UART1	(1 << 16)
	#define CLK_UART2	(1 << 17)
	#define CLK_UART3	(1 << 18)
	#define CLK_SSI1	(1 << 19)
	#define CLK_SSI2	(1 << 20)
	#define CLK_PDMA	(1 << 21)
	#define CLK_GPS		(1 << 22)
	#define CLK_MAC		(1 << 23)
	#define CLK_UHC		(1 << 24)
	#define CLK_SMB2	(1 << 25)
	#define CLK_CIM		(1 << 26)
	#define CLK_TVE		(1 << 27)
	#define CLK_LCD		(1 << 28)
	#define CLK_IPU		(1 << 29)
	#define CLK_DDR0	(1 << 30)
	#define CLK_DDR1	(1 << 31)
#define JZ_CLKGR1	0x00000028	/* Clock Gating Registers */
	#define CLK_SMB3	(1 << 0)
	#define CLK_TSSI1	(1 << 1)
	#define CLK_VPU		(1 << 2)
	#define CLK_PCM		(1 << 3)
	#define CLK_GPU		(1 << 4)
	#define CLK_COMPRESS	(1 << 5)
	#define CLK_AIC1	(1 << 6)
	#define CLK_GPVLC	(1 << 7)
	#define CLK_OTG1	(1 << 8)
	#define CLK_HDMI	(1 << 9)
	#define CLK_UART4	(1 << 10)
	#define CLK_AHB_MON	(1 << 11)
	#define CLK_SMB4	(1 << 12)
	#define CLK_DES		(1 << 13)
	#define CLK_X2D		(1 << 14)
	#define CLK_P1		(1 << 15)
#define JZ_DDCDR	0x0000002c	/* DDR clock divider register */
#define JZ_VPUCDR	0x00000030	/* VPU clock divider register */
#define JZ_I2SCDR	0x00000060	/* I2S device clock divider register */
#define JZ_I2S1CDR	0x000000a0	/* I2S device clock divider register */
#define JZ_USBCDR	0x00000050	/* OTG PHY clock divider register */
#define JZ_LP0CDR	0x00000054	/* LCD0 pix clock divider register */
#define JZ_LP1CDR	0x00000064	/* LCD1 pix clock divider register */
#define JZ_MSC0CDR	0x00000068	/* MSC0 clock divider register */
#define JZ_MSC1CDR	0x000000a4	/* MSC1 clock divider register */
#define JZ_MSC2CDR	0x000000a8	/* MSC2 clock divider register */
	#define MSCCDR_SCLK_A	0x40000000
	#define MSCCDR_MPLL	0x80000000
	#define MSCCDR_CE	0x20000000
	#define MSCCDR_BUSY	0x10000000
	#define MSCCDR_STOP	0x08000000
	#define MSCCDR_PHASE	0x00008000	/* 0 - 90deg phase, 1 - 180 */
	#define MSCCDR_DIV_M	0x000000ff	/* src / ((div + 1) * 2) */
	#define UHCCDR_DIV_M	0x000000ff
#define JZ_UHCCDR	0x0000006c	/* UHC 48M clock divider register */
	#define UHCCDR_SCLK_A	0x00000000
	#define UHCCDR_MPLL	0x40000000
	#define UHCCDR_EPLL	0x80000000
	#define UHCCDR_OTG_PHY	0xc0000000
	#define UHCCDR_CLK_MASK	0xc0000000
	#define UHCCDR_CE	0x20000000
	#define UHCCDR_BUSY	0x10000000
	#define UHCCDR_STOP	0x08000000
	#define UHCCDR_DIV_M	0x000000ff
	#define UHCCDR_DIV(d)	(d)
#define JZ_SSICDR	0x00000074	/* SSI clock divider register */
#define JZ_CIMCDR	0x0000007c	/* CIM MCLK clock divider register */
#define JZ_PCMCDR	0x00000084	/* PCM device clock divider register */
#define JZ_GPUCDR	0x00000088	/* GPU clock divider register */
#define JZ_HDMICDR	0x0000008c	/* HDMI clock divider register */
#define JZ_BCHCDR	0x000000ac	/* BCH clock divider register */
#define JZ_CPM_INTR	0x000000b0	/* CPM interrupt register */
#define JZ_CPM_INTRE	0x000000b4	/* CPM interrupt enable register */
#define JZ_CPSPR	0x00000034	/* CPM scratch register */
#define JZ_CPSRPR	0x00000038	/* CPM scratch protected register */
#define JZ_USBPCR	0x0000003c	/* USB parameter control register */
	#define PCR_USB_MODE		0x80000000	/* 1 - otg */
	#define PCR_AVLD_REG		0x40000000
	#define PCR_IDPULLUP_MASK	0x30000000
	#define PCR_INCR_MASK		0x08000000
	#define PCR_TCRISETUNE		0x04000000
	#define PCR_COMMONONN		0x02000000
	#define PCR_VBUSVLDEXT		0x01000000
	#define PCR_VBUSVLDEXTSEL	0x00800000
	#define PCR_POR			0x00400000
	#define PCR_SIDDQ		0x00200000
	#define PCR_OTG_DISABLE		0x00100000
	#define PCR_COMPDISTN_M		0x000e0000
	#define PCR_OTGTUNE		0x0001c000
	#define PCR_SQRXTUNE		0x00003800
	#define PCR_TXFSLSTUNE		0x00000780
	#define PCR_TXPREEMPHTUNE	0x00000040
	#define PCR_TXHSXVTUNE		0x00000030
	#define PCR_TXVREFTUNE		0x0000000f
#define JZ_USBRDT	0x00000040	/* Reset detect timer register */
	#define USBRDT_USBRDT_SHIFT	0
	#define USBRDT_USBRDT_WIDTH	23
	#define USBRDT_VBFIL_LD_EN	0x01000000
#define JZ_USBVBFIL	0x00000044	/* USB jitter filter register */
	#define USBVBFIL_IDDIGFIL_SHIFT 16
	#define USBVBFIL_IDDIGFIL_WIDTH 16
	#define USBVBFIL_USBVBFIL_SHIFT 0
	#define USBVBFIL_USBVBFIL_WIDTH 16
#define JZ_USBPCR1	0x00000048	/* USB parameter control register 1 */
	#define PCR_SYNOPSYS	0x10000000	/* Mentor mode otherwise */
	#define PCR_REFCLK_CORE	0x08000000
	#define PCR_REFCLK_XO25	0x04000000
	#define PCR_REFCLK_CO	0x00000000
	#define PCR_REFCLK_M	0x0c000000
	#define PCR_CLK_M	0x03000000	/* clock */
	#define PCR_CLK_192	0x03000000	/* 19.2MHz */
	#define PCR_CLK_48	0x02000000	/* 48MHz */
	#define PCR_CLK_24	0x01000000	/* 24MHz */
	#define PCR_CLK_12	0x00000000	/* 12MHz */
	#define PCR_DMPD1	0x00800000	/* pull down D- on port 1 */
	#define PCR_DPPD1	0x00400000	/* pull down D+ on port 1 */
	#define PCR_PORT0_RST	0x00200000	/* port 0 reset */
	#define PCR_PORT1_RST	0x00100000	/* port 1 reset */
	#define PCR_WORD_I_F0	0x00080000	/* 1: 16bit/30M, 8/60 otherw. */
	#define PCR_WORD_I_F1	0x00040000	/* same for port 1 */
	#define PCR_COMPDISTUNE	0x00038000	/* disconnect threshold */
	#define PCR_SQRXTUNE1	0x00007000	/* squelch threshold */
	#define PCR_TXFSLSTUNE1	0x00000f00	/* FS/LS impedance adj. */
	#define PCR_TXPREEMPH	0x00000080	/* HS transm. pre-emphasis */
	#define PCR_TXHSXVTUNE1	0x00000060	/* dp/dm voltage adj. */
	#define PCR_TXVREFTUNE1	0x00000017	/* HS DC voltage adj. */
	#define PCR_TXRISETUNE1	0x00000001	/* rise/fall wave adj. */

/* power manager */
#define JZ_LPCR		0x00000004
	#define LPCR_PD_SCPU	(1u << 31)	/* CPU1 power down */
	#define LPCR_PD_VPU	(1u << 30)	/* VPU power down */
	#define LPCR_PD_GPU	(1u << 29)	/* GPU power down */
	#define LPCR_PD_GPS	(1u << 28)	/* GPS power down */
	#define LPCR_SCPUS	(1u << 27)	/* CPU1 power down status */
	#define LPCR_VPUS	(1u << 26)	/* VPU power down status */
	#define LPCR_GPUS	(1u << 25)	/* GPU power down status */
	#define LPCR_GPSS	(1u << 24)	/* GPS power down status */
	#define LPCR_GPU_IDLE	(1u << 20)	/* GPU idle status */
	#define LPCR_PST_SHIFT	8		/* Power stability time */
	#define LPCR_PST_MASK	(0xFFFu << 8)
	#define LPCR_DUTY_SHIFT	3		/* CPU clock duty */
	#define LPCR_DUTY_MASK	(0x1Fu << 3)
	#define LPCR_DOZE	(1u << 2)	/* Doze mode */
	#define LPCR_LPM_SHIFT	0		/* Low power mode */
	#define LPCR_LPM_MASK	(0x03u << 0)

#define JZ_OPCR		0x00000024	/* Oscillator Power Control Reg. */
	#define OPCR_IDLE_DIS	0x80000000	/* don't stop CPU clk on idle */
	#define OPCR_GPU_CLK_ST	0x40000000	/* stop GPU clock */
	#define OPCR_L2CM_M	0x0c000000
	#define OPCR_L2CM_ON	0x00000000	/* L2 stays on in sleep */
	#define OPCR_L2CM_RET	0x04000000	/* L2 retention mode in sleep */
	#define OPCR_L2CM_OFF	0x08000000	/* L2 powers down in sleep */
	#define OPCR_SPENDN0	0x00000080	/* 0 - OTG port forced down */
	#define OPCR_SPENDN1	0x00000040	/* 0 - UHC port forced down */
	#define OPCR_BUS_MODE	0x00000020	/* 1 - bursts */
	#define OPCR_O1SE	0x00000010	/* EXTCLK on in sleep */
	#define OPCR_PD		0x00000008	/* P0 down in sleep */
	#define OPCR_ERCS	0x00000004	/* 1 RTCCLK, 0 EXTCLK/512 */
	#define OPCR_CPU_MODE	0x00000002	/* 1 access 'accelerated' */
	#define OPCR_OSE	0x00000001	/* disable EXTCLK */

#define JZ_SPCR0	0x000000b8	/* SRAM Power Control Registers */
#define JZ_SPCR1	0x000000bc
#define JZ_SRBC		0x000000c4	/* Soft Reset & Bus Control */
	#define SRBC_UHC_SR	0x00004000	/* UHC soft reset*/

/*
 * random number generator
 *
 * Its function currently isn't documented by Ingenic.
 * However, testing suggests that it works as expected.
 */
#define JZ_ERNG	0x000000d8
#define JZ_RNG	0x000000dc

/* Interrupt controller */
#define JZ_ICBASE	0x10001000	/* IC base address */
#define JZ_ICSR0	0x00000000	/* raw IRQ line status */
#define JZ_ICMR0	0x00000004	/* IRQ mask, 1 masks IRQ */
#define JZ_ICMSR0	0x00000008	/* sets bits in mask register */
#define JZ_ICMCR0	0x0000000c	/* clears bits in mask register */
#define JZ_ICPR0	0x00000010	/* line status after masking */

#define JZ_ICSR1	0x00000020	/* raw IRQ line status */
#define JZ_ICMR1	0x00000024	/* IRQ mask, 1 masks IRQ */
#define JZ_ICMSR1	0x00000028	/* sets bits in mask register */
#define JZ_ICMCR1	0x0000002c	/* clears bits in maks register */
#define JZ_ICPR1	0x00000030	/* line status after masking */

#define JZ_DSR0		0x00000034	/* source for PDMA */
#define JZ_DMR0		0x00000038	/* mask for PDMA */
#define JZ_DPR0		0x0000003c	/* pending for PDMA */

#define JZ_DSR1		0x00000040	/* source for PDMA */
#define JZ_DMR1		0x00000044	/* mask for PDMA */
#define JZ_DPR1		0x00000048	/* pending for PDMA */

/* memory controller */
#define JZ_DMMAP0	0x13010024
#define JZ_DMMAP1	0x13010028
	#define	DMMAP_BASE	0x0000ff00	/* base PADDR of memory chunk */
	#define DMMAP_MASK	0x000000ff	/* mask which bits of PADDR are
						 * constant */
/* USB controllers */
#define JZ_EHCI_BASE	0x13490000
#define JZ_EHCI_REG_UTMI_BUS 0x000000b0
	#define UTMI_BUS_WIDTH	0x00000040
#define JZ_OHCI_BASE	0x134a0000

#define JZ_DWC2_BASE	0x13500000
#define JZ_DWC2_GUSBCFG  0

/* Ethernet */
#define JZ_DME_BASE	0x16000000
#define JZ_DME_IO	0
#define JZ_DME_DATA	2

/* GPIO */
#define JZ_GPIO_A_BASE	0x10010000
#define JZ_GPIO_B_BASE	0x10010100
#define JZ_GPIO_C_BASE	0x10010200
#define JZ_GPIO_D_BASE	0x10010300
#define JZ_GPIO_E_BASE	0x10010400
#define JZ_GPIO_F_BASE	0x10010500

/* GPIO registers per port */
#define JZ_GPIO_PIN	0x00000000	/* pin level register */
/* 0 - normal gpio, 1 - interrupt */
#define JZ_GPIO_INT	0x00000010	/* interrupt register */
#define JZ_GPIO_INTS	0x00000014	/* interrupt set register */
#define JZ_GPIO_INTC	0x00000018	/* interrupt clear register */
/*
 * INT == 1: 1 disables interrupt
 * INT == 0: device select, see below
 */
#define JZ_GPIO_MASK	0x00000020	/* port mask register */
#define JZ_GPIO_MASKS	0x00000024	/* port mask set register */
#define JZ_GPIO_MASKC	0x00000028	/* port mask clear register */
/*
 * INT == 1: 0 - level triggered, 1 - edge triggered
 * INT == 0: 0 - device select, see below
 */
#define JZ_GPIO_PAT1	0x00000030	/* pattern 1 register */
#define JZ_GPIO_PAT1S	0x00000034	/* pattern 1 set register */
#define JZ_GPIO_PAT1C	0x00000038	/* pattern 1 clear register */
/*
 * INT == 1:
 *   PAT1 == 0: 0 - trigger on low, 1 - trigger on high
 *   PAT0 == 1: 0 - trigger on falling edge, 1 - trigger on rising edge
 * INT == 0:
 *   MASK == 0:
 *     PAT1 == 0: 0 - device 0, 1 - device 1
 *     PAT0 == 1: 0 - device 2, 1 - device 3
 *   MASK == 1:
 *     PAT1 == 0: set gpio output
 *     PAT1 == 1: pin is input
 */
#define JZ_GPIO_PAT0	0x00000040	/* pattern 0 register */
#define JZ_GPIO_PAT0S	0x00000044	/* pattern 0 set register */
#define JZ_GPIO_PAT0C	0x00000048	/* pattern 0 clear register */
/* 1 - interrupt happened */
#define JZ_GPIO_FLAG	0x00000050	/* flag register */
#define JZ_GPIO_FLAGC	0x00000058	/* flag clear register */
/* 1 - disable pull up/down resistors */
#define JZ_GPIO_DPULL	0x00000070	/* pull disable register */
#define JZ_GPIO_DPULLS	0x00000074	/* pull disable set register */
#define JZ_GPIO_DPULLC	0x00000078	/* pull disable clear register */
/* the following are uncommented in the manual */
#define JZ_GPIO_DRVL	0x00000080	/* drive low register */
#define JZ_GPIO_DRVLS	0x00000084	/* drive low set register */
#define JZ_GPIO_DRVLC	0x00000088	/* drive low clear register */
#define JZ_GPIO_DIR	0x00000090	/* direction register */
#define JZ_GPIO_DIRS	0x00000094	/* direction register */
#define JZ_GPIO_DIRC	0x00000098	/* direction register */
#define JZ_GPIO_DRVH	0x000000a0	/* drive high register */
#define JZ_GPIO_DRVHS	0x000000a4	/* drive high set register */
#define JZ_GPIO_DRVHC	0x000000a8	/* drive high clear register */

/* I2C / SMBus */
#define JZ_SMB0_BASE	0x10050000
#define JZ_SMB1_BASE	0x10051000
#define JZ_SMB2_BASE	0x10052000
#define JZ_SMB3_BASE	0x10053000
#define JZ_SMB4_BASE	0x10054000

/* SMBus register offsets, per port */
#define JZ_SMBCON	0x00 /* SMB control */
	#define JZ_STPHLD	0x80 /* Stop Hold Enable bit */
	#define JZ_SLVDIS	0x40 /* 1 - slave disabled */
	#define JZ_REST		0x20 /* 1 - allow RESTART */
	#define JZ_MATP		0x10 /* 1 - enable 10bit addr. for master */
	#define JZ_SATP		0x08 /* 1 - enable 10bit addr. for slave */
	#define JZ_SPD_M	0x06 /* bus speed control */
	#define JZ_SPD_100KB	0x02 /* 100kBit/s mode */
	#define JZ_SPD_400KB	0x04 /* 400kBit/s mode */
	#define JZ_MD		0x01 /* enable master */
#define JZ_SMBTAR	0x04 /* SMB target address */
	#define JZ_SMATP	0x1000 /* enable 10bit master addr */
	#define JZ_SPECIAL	0x0800 /* 1 - special command */
	#define JZ_START	0x0400 /* 1 - send START */
	#define JZ_SMBTAR_M	0x03ff /* target address */
#define JZ_SMBSAR	0x08 /* SMB slave address */
#define JZ_SMBDC	0x10 /* SMB data buffer and command */
	#define JZ_CMD	0x100 /* 1 - read, 0 - write */
	#define JZ_DATA	0x0ff
#define JZ_SMBSHCNT	0x14 /* Standard speed SMB SCL high count */
#define JZ_SMBSLCNT	0x18 /* Standard speed SMB SCL low count */
#define JZ_SMBFHCNT	0x1C /* Fast speed SMB SCL high count */
#define JZ_SMBFLCNT	0x20 /* Fast speed SMB SCL low count */
#define JZ_SMBINTST	0x2C /* SMB Interrupt Status */
	#define JZ_ISTT		0x400	/* START or RESTART occured */
	#define JZ_ISTP		0x200	/* STOP occured */
	#define JZ_TXABT	0x40	/* ABORT occured */
	#define JZ_TXEMP	0x10	/* TX FIFO is low */
	#define JZ_TXOF		0x08	/* TX FIFO is high */
	#define JZ_RXFL		0x04	/* RX FIFO is at  JZ_SMBRXTL*/
	#define JZ_RXOF		0x02	/* RX FIFO is high */
	#define JZ_RXUF		0x01	/* RX FIFO underflow */
#define JZ_SMBINTM	0x30 /* SMB Interrupt Mask */
#define JZ_SMBRXTL	0x38 /* SMB RxFIFO Threshold */
#define JZ_SMBTXTL	0x3C /* SMB TxFIFO Threshold */
#define JZ_SMBCINT	0x40 /* Clear Interrupts */
	#define JZ_CLEARALL	0x01
#define JZ_SMBCRXUF	0x44 /* Clear RXUF Interrupt */
#define JZ_SMBCRXOF	0x48 /* Clear RX_OVER Interrupt */
#define JZ_SMBCTXOF	0x4C /* Clear TX_OVER Interrupt */
#define JZ_SMBCRXREQ	0x50 /* Clear RDREQ Interrupt */
#define JZ_SMBCTXABT	0x54 /* Clear TX_ABRT Interrupt */
#define JZ_SMBCRXDN	0x58 /* Clear RX_DONE Interrupt */
#define JZ_SMBCACT	0x5c /* Clear ACTIVITY Interrupt */
#define JZ_SMBCSTP	0x60 /* Clear STOP Interrupt */
#define JZ_SMBCSTT	0x64 /* Clear START Interrupt */
#define JZ_SMBCGC	0x68 /* Clear GEN_CALL Interrupt */
#define JZ_SMBENB	0x6C /* SMB Enable */
	#define JZ_ENABLE	0x01
#define JZ_SMBST	0x70 /* SMB Status register */
	#define JZ_SLVACT	0x40 /* slave is active */
	#define JZ_MSTACT	0x20 /* master is active */
	#define JZ_RFF		0x10 /* RX FIFO is full */
	#define JZ_RFNE		0x08 /* RX FIFO not empty */
	#define JZ_TFE		0x04 /* TX FIFO is empty */
	#define JZ_TFNF		0x02 /* TX FIFO is not full */
	#define JZ_ACT		0x01 /* JZ_SLVACT | JZ_MSTACT */
#define JZ_SMBABTSRC	0x80 /* SMB Transmit Abort Status Register */
#define JZ_SMBDMACR	0x88 /* DMA Control Register */
#define JZ_SMBDMATDL	0x8c /* DMA Transmit Data Level */
#define JZ_SMBDMARDL	0x90 /* DMA Receive Data Level */
#define JZ_SMBSDASU	0x94 /* SMB SDA Setup Register */
#define JZ_SMBACKGC	0x98 /* SMB ACK General Call Register */
#define JZ_SMBENBST	0x9C /* SMB Enable Status Register */
#define JZ_SMBSDAHD	0xD0 /* SMB SDA HolD time Register */
	#define JZ_HDENB	0x100	/* enable hold time */

/* SD/MMC hosts */
#define JZ_MSC0_BASE	0x13450000
#define JZ_MSC1_BASE	0x13460000
#define JZ_MSC2_BASE	0x13470000

#define JZ_MSC_CTRL	0x00
	#define JZ_SEND_CCSD		0x8000
	#define JZ_SEND_AS_CCSD		0x4000
	#define JZ_EXIT_MULTIPLE	0x0080
	#define JZ_EXIT_TRANSFER	0x0040
	#define JZ_START_READWAIT	0x0020
	#define JZ_STOP_READWAIT	0x0010
	#define JZ_RESET		0x0008
	#define JZ_START_OP		0x0004
	#define JZ_CLOCK_CTRL_M		0x0003
	#define JZ_CLOCK_START		0x0002
	#define JZ_CLOCK_STOP		0x0001
#define JZ_MSC_STAT	0x04
	#define JZ_AUTO_CMD12_DONE	0x80000000
	#define JZ_AUTO_CMD23_DONE	0x40000000
	#define JZ_SVS			0x20000000
	#define JZ_PIN_LEVEL_M		0x1f000000
	#define JZ_BCE			0x00100000 /* boot CRC error */
	#define JZ_BDE			0x00080000 /* boot data end */
	#define JZ_BAE			0x00040000 /* boot acknowledge error */
	#define JZ_BAR			0x00020000 /* boot ack. received */
	#define JZ_DMAEND		0x00010000
	#define JZ_IS_RESETTING		0x00008000
	#define JZ_SDIO_INT_ACTIVE	0x00004000
	#define JZ_PRG_DONE		0x00002000
	#define JZ_DATA_TRAN_DONE	0x00001000
	#define JZ_END_CMD_RES		0x00000800
	#define JZ_DATA_FIFO_AFULL	0x00000400
	#define JZ_IS_READWAIT		0x00000200
	#define JZ_CLK_EN		0x00000100
	#define JZ_DATA_FIFO_FULL	0x00000080
	#define JZ_DATA_FIFO_EMPTY	0x00000040
	#define JZ_CRC_RES_ERR		0x00000020
	#define JZ_CRC_READ_ERR		0x00000010
	#define JZ_CRC_WRITE_ERR_M	0x0000000c
	#define JZ_CRC_WRITE_OK		0x00000000
	#define JZ_CRC_CARD_ERR		0x00000004
	#define JZ_CRC_NO_STATUS	0x00000008
	#define JZ_TIME_OUT_RES		0x00000002
	#define JZ_TIME_OUT_READ	0x00000001
#define JZ_MSC_CLKRT	0x08
	#define JZ_DEV_CLK	0x0
	#define JZ_DEV_CLK_2	0x1	/* DEV_CLK / 2 */
	#define JZ_DEV_CLK_4	0x2	/* DEV_CLK / 4 */
	#define JZ_DEV_CLK_8	0x3	/* DEV_CLK / 8 */
	#define JZ_DEV_CLK_16	0x4	/* DEV_CLK / 16 */
	#define JZ_DEV_CLK_32	0x5	/* DEV_CLK / 32 */
	#define JZ_DEV_CLK_64	0x6	/* DEV_CLK / 64 */
	#define JZ_DEV_CLK_128	0x7	/* DEV_CLK / 128 */
#define JZ_MSC_CMDAT	0x0c
	#define JZ_CCS_EXPECTED	0x80000000
	#define JZ_READ_CEATA	0x40000000
	#define JZ_DIS_BOOT	0x08000000
	#define JZ_ENA_BOOT	0x04000000
	#define JZ_EXP_BOOT_ACK	0x02000000
	#define JZ_BOOT_MODE	0x01000000
	#define JZ_AUTO_CMD23	0x00040000
	#define JZ_SDIO_PRDT	0x00020000
	#define JZ_AUTO_CMD12	0x00010000
	#define JZ_RTRG_M	0x0000c000 /* receive FIFO trigger */
	#define JZ_RTRG_16	0x00000000 /* >= 16 */
	#define JZ_RTRG_32	0x00004000 /* >= 32 */
	#define JZ_RTRG_64	0x00008000 /* >= 64 */
	#define JZ_RTRG_96	0x0000c000 /* >= 96 */
	#define JZ_TTRG_M	0x00003000 /* transmit FIFO trigger */
	#define JZ_TTRG_16	0x00000000 /* >= 16 */
	#define JZ_TTRG_32	0x00001000 /* >= 32 */
	#define JZ_TTRG_64	0x00002000 /* >= 64 */
	#define JZ_TTRG_96	0x00003000 /* >= 96 */
	#define JZ_IO_ABORT	0x00000800
	#define JZ_BUS_WIDTH_M	0x00000600
	#define JZ_BUS_1BIT	0x00000000
	#define JZ_BUS_4BIT	0x00000400
	#define JZ_BUS_8BIT	0x00000600
	#define JZ_INIT		0x00000080 /* send 80 clk init before cmd */
	#define JZ_BUSY		0x00000040
	#define JZ_STREAM	0x00000020
	#define JZ_WRITE	0x00000010 /* read otherwise */
	#define JZ_DATA_EN	0x00000008
	#define JZ_RESPONSE_M	0x00000007 /* response format */
	#define JZ_RES_NONE	0x00000000
	#define JZ_RES_R1	0x00000001 /* R1 and R1b */
	#define JZ_RES_R2	0x00000002
	#define JZ_RES_R3	0x00000003
	#define JZ_RES_R4	0x00000004
	#define JZ_RES_R5	0x00000005
	#define JZ_RES_R6	0x00000006
	#define JZ_RES_R7	0x00000007
#define JZ_MSC_RESTO	0x10 /* 16bit response timeout in MSC_CLK */
#define JZ_MSC_RDTO 	0x14 /* 32bit read timeout in MSC_CLK */
#define JZ_MSC_BLKLEN	0x18 /* 16bit block length */
#define JZ_MSC_NOB	0x1c /* 16bit block counter */
#define JZ_MSC_SNOB	0x20 /* 16bit successful block counter */
#define JZ_MSC_IMASK	0x24 /* interrupt mask */
	#define JZ_INT_AUTO_CMD23_DONE	0x40000000
	#define JZ_INT_SVS		0x20000000
	#define JZ_INT_PIN_LEVEL_M	0x1f000000
	#define JZ_INT_BCE		0x00100000
	#define JZ_INT_BDE		0x00080000
	#define JZ_INT_BAE		0x00040000
	#define JZ_INT_BAR		0x00020000
	#define JZ_INT_DMAEND		0x00010000
	#define JZ_INT_AUTO_CMD12_DONE	0x00008000
	#define JZ_INT_DATA_FIFO_FULL	0x00004000
	#define JZ_INT_DATA_FIFO_EMPTY	0x00002000
	#define JZ_INT_CRC_RES_ERR	0x00001000
	#define JZ_INT_CRC_READ_ERR	0x00000800
	#define JZ_INT_CRC_WRITE_ERR	0x00000400
	#define JZ_INT_TIMEOUT_RES	0x00000200
	#define JZ_INT_TIMEOUT_READ	0x00000100
	#define JZ_INT_SDIO		0x00000080
	#define JZ_INT_TXFIFO_WR_REQ	0x00000040
	#define JZ_INT_RXFIFO_RD_REQ	0x00000020
	#define JZ_INT_END_CMD_RES	0x00000004
	#define JZ_INT_PRG_DONE		0x00000002
	#define JZ_INT_DATA_TRAN_DONE	0x00000001
#define JZ_MSC_IFLG	0x28 /* interrupt flags */
#define JZ_MSC_CMD	0x2c /* 6bit CMD index */
#define JZ_MSC_ARG	0x30 /* 32bit argument */
#define JZ_MSC_RES	0x34 /* 8x16bit response data FIFO */
#define JZ_MSC_RXFIFO	0x38
#define JZ_MSC_TXFIFO	0x3c
#define JZ_MSC_LPM	0x40
	#define JZ_DRV_SEL_M	0xc0000000
	#define JZ_FALLING_EDGE	0x00000000
	#define JZ_RISING_1NS	0x40000000 /* 1ns delay */
	#define JZ_RISING_4	0x80000000 /* 1/4 MSC_CLK delay */
	#define JZ_SMP_SEL	0x20000000 /* 1 - rising edge */
	#define JZ_LPM		0x00000001 /* low power mode */
#define JZ_MSC_DMAC	0x44
	#define JZ_MODE_SEL	0x80 /* 1 - specify transfer length */
	#define JZ_AOFST_M	0x60 /* address offset in bytes */
	#define JZ_AOFST_S	6    /* addrress offset shift */
	#define JZ_ALIGNEN	0x10 /* allow non-32bit-aligned transfers */
	#define JZ_INCR_M	0x0c /* burst type */
	#define JZ_INCR_16	0x00
	#define JZ_INCR_32	0x04
	#define JZ_INCR_64	0x08
	#define JZ_DMASEL	0x02 /* 1 - SoC DMAC, 0 - MSC built-in */
	#define JZ_DMAEN	0x01 /* enable DMA */
#define JZ_MSC_DMANDA	0x48 /* next descriptor paddr */
#define JZ_MSC_DMADA	0x4c /* current descriptor */
#define JZ_MSC_DMALEN	0x50 /* transfer tength */
#define JZ_MSC_DMACMD	0x54
	#define JZ_DMA_IDI_M	0xff000000
	#define JZ_DMA_ID_M	0x00ff0000
	#define JZ_DMA_AOFST_M	0x00000600
	#define JZ_DMA_ALIGN	0x00000100
	#define JZ_DMA_ENDI	0x00000002
	#define JZ_DMA_LINK	0x00000001
#define JZ_MSC_CTRL2	0x58
	#define JZ_PIP		0x1f000000	/* 1 - intr trigger on high */
	#define JZ_RST_EN	0x00800000
	#define JZ_STPRM	0x00000010
	#define JZ_SVC		0x00000008
	#define JZ_SMS_M	0x00000007
	#define JZ_SMS_DEF	0x00000000	/* default speed */
	#define JZ_SMS_HIGH	0x00000001	/* high speed */
	#define JZ_SMS_SDR12	0x00000002
	#define JZ_SMS_SDR25	0x00000003
	#define JZ_SMS_SDR50	0x00000004
#define JZ_MSC_RTCNT	0x5c /* RT FIFO count */

/* EFUSE Slave Interface */
#define JZ_EFUSE	0x134100D0
#define JZ_EFUCTRL	0x00
	#define JZ_EFUSE_BANK	0x40000000	/* select upper 4KBit */
	#define JZ_EFUSE_ADDR_M	0x3fe00000	/* in bytes */
	#define JZ_EFUSE_ADDR_SHIFT	21
	#define JZ_EFUSE_SIZE_M	0x001f0000	/* in bytes */
	#define JZ_EFUSE_SIZE_SHIFT	16
	#define JZ_EFUSE_PROG	0x00008000	/* enable programming */
	#define JZ_EFUSE_WRITE	0x00000002	/* write enable */
	#define JZ_EFUSE_READ	0x00000001	/* read enable */
#define JZ_EFUCFG	0x04
	#define JZ_EFUSE_INT_E		0x80000000	/* which IRQ? */
	#define JZ_EFUSE_RD_ADJ_M	0x00f00000
	#define JZ_EFUSE_RD_STROBE	0x000f0000
	#define JZ_EFUSE_WR_ADJUST	0x0000f000
	#define JZ_EFUSE_WR_STROBE	0x00000fff
#define JZ_EFUSTATE	0x08
	#define JZ_EFUSE_GLOBAL_P	0x00008000	/* wr protect bits */
	#define JZ_EFUSE_CHIPID_P	0x00004000
	#define JZ_EFUSE_CUSTID_P	0x00002000
	#define JZ_EFUSE_SECWR_EN	0x00001000
	#define JZ_EFUSE_PC_P		0x00000800
	#define JZ_EFUSE_HDMIKEY_P	0x00000400
	#define JZ_EFUSE_SECKEY_P	0x00000200
	#define JZ_EFUSE_SECBOOT_EN	0x00000100
	#define JZ_EFUSE_HDMI_BUSY	0x00000004
	#define JZ_EFUSE_WR_DONE	0x00000002
	#define JZ_EFUSE_RD_DONE	0x00000001
#define JZ_EFUDATA0	0x0C
#define JZ_EFUDATA1	0x10
#define JZ_EFUDATA2	0x14
#define JZ_EFUDATA3	0x18
#define JZ_EFUDATA4	0x1C
#define JZ_EFUDATA5	0x20
#define JZ_EFUDATA6	0x24
#define JZ_EFUDATA7	0x28

/* NEMC */
#define JZ_NEMC_BASE	0x13410000
#define JZ_NEMC_SMCR(n) (0x10 + (n) * 4)

# define JZ_NEMC_SMCR_SMT_SHIFT	0
# define JZ_NEMC_SMCR_SMT_WIDTH	1
# define JZ_NEMC_SMCR_SMT_MASK	(((1 << JZ_NEMC_SMCR_SMT_WIDTH) - 1) << JZ_NEMC_SMCR_SMT_SHIFT)
# define JZ_NEMC_SMCR_SMT_NORMAL (0 << JZ_NEMC_SMCR_SMT_SHIFT)
# define JZ_NEMC_SMCR_SMT_BROM	 (1 << JZ_NEMC_SMCR_SMT_SHIFT)

# define JZ_NEMC_SMCR_BL_SHIFT	1
# define JZ_NEMC_SMCR_BL_WIDTH	2
# define JZ_NEMC_SMCR_BL_MASK	(((1 << JZ_NEMC_SMCR_BL_WIDTH) - 1) << JZ_NEMC_SMCR_BL_SHIFT)
# define JZ_NEMC_SMCR_BL(n)	(((n) << JZ_NEMC_SMCR_BL_SHIFT)

# define JZ_NEMC_SMCR_BW_SHIFT	6
# define JZ_NEMC_SMCR_BW_WIDTH	2
# define JZ_NEMC_SMCR_BW_MASK	(((1 << JZ_NEMC_SMCR_BW_WIDTH) - 1) << JZ_NEMC_SMCR_BW_SHIFT)
# define JZ_NEMC_SMCR_BW_8	(0 << JZ_NEMC_SMCR_BW_SHIFT)

# define JZ_NEMC_SMCR_TAS_SHIFT	8
# define JZ_NEMC_SMCR_TAS_WIDTH	4
# define JZ_NEMC_SMCR_TAS_MASK	(((1 << JZ_NEMC_SMCR_TAS_WIDTH) - 1) << JZ_NEMC_SMCR_TAS_SHIFT)

# define JZ_NEMC_SMCR_TAH_SHIFT	12
# define JZ_NEMC_SMCR_TAH_WIDTH	4
# define JZ_NEMC_SMCR_TAH_MASK	(((1 << JZ_NEMC_SMCR_TAH_WIDTH) - 1) << JZ_NEMC_SMCR_TAH_SHIFT)

# define JZ_NEMC_SMCR_TBP_SHIFT	16
# define JZ_NEMC_SMCR_TBP_WIDTH	4
# define JZ_NEMC_SMCR_TBP_MASK	(((1 << JZ_NEMC_SMCR_TBP_WIDTH) - 1) << JZ_NEMC_SMCR_TBP_SHIFT)

# define JZ_NEMC_SMCR_TAW_SHIFT	20
# define JZ_NEMC_SMCR_TAW_WIDTH	4
# define JZ_NEMC_SMCR_TAW_MASK	(((1 << JZ_NEMC_SMCR_TAW_WIDTH) - 1) << JZ_NEMC_SMCR_TAW_SHIFT)

# define JZ_NEMC_SMCR_STRV_SHIFT	24
# define JZ_NEMC_SMCR_STRV_WIDTH	4
# define JZ_NEMC_SMCR_STRV_MASK	(((1 << JZ_NEMC_SMCR_STRV_WIDTH) - 1) << JZ_NEMC_SMCR_STRV_SHIFT)

#define JZ_NEMC_SACR(n) (0x30 + (n) * 4)

# define JZ_NEMC_SACR_MASK_SHIFT	0
# define JZ_NEMC_SACR_MASK_WIDTH	8
# define JZ_NEMC_SACR_MASK_MASK	(((1 << JZ_NEMC_SACR_MASK_WIDTH) - 1) << JZ_NEMC_SACR_MASK_SHIFT)

# define JZ_NEMC_SACR_ADDR_SHIFT	0
# define JZ_NEMC_SACR_ADDR_WIDTH	8
# define JZ_NEMC_SACR_ADDR_MASK	(((1 << JZ_NEMC_SACR_ADDR_WIDTH) - 1) << JZ_NEMC_SACR_ADDR_SHIFT)

#define JC_NEMC_NFSCR	0x50

#endif /* JZ4780_REGS_H */
