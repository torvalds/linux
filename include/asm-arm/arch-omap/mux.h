/*
 * linux/include/asm-arm/arch-omap/mux.h
 *
 * Table of the Omap register configurations for the FUNC_MUX and
 * PULL_DWN combinations.
 *
 * Copyright (C) 2003 - 2005 Nokia Corporation
 *
 * Written by Tony Lindgren <tony.lindgren@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * NOTE: Please use the following naming style for new pin entries.
 *	 For example, W8_1610_MMC2_DAT0, where:
 *	 - W8	     = ball
 *	 - 1610	     = 1510 or 1610, none if common for both 1510 and 1610
 *	 - MMC2_DAT0 = function
 *
 * Change log:
 *   Added entry for the I2C interface. (02Feb 2004)
 *   Copyright (C) 2004 Texas Instruments
 *
 *   Added entry for the keypad and uwire CS1. (09Mar 2004)
 *   Copyright (C) 2004 Texas Instruments
 *
 */

#ifndef __ASM_ARCH_MUX_H
#define __ASM_ARCH_MUX_H

#define PU_PD_SEL_NA		0	/* No pu_pd reg available */
#define PULL_DWN_CTRL_NA	0	/* No pull-down control needed */

#ifdef	CONFIG_OMAP_MUX_DEBUG
#define MUX_REG(reg, mode_offset, mode) .mux_reg_name = "FUNC_MUX_CTRL_"#reg, \
					.mux_reg = FUNC_MUX_CTRL_##reg, \
					.mask_offset = mode_offset, \
					.mask = mode,

#define PULL_REG(reg, bit, status)	.pull_name = "PULL_DWN_CTRL_"#reg, \
					.pull_reg = PULL_DWN_CTRL_##reg, \
					.pull_bit = bit, \
					.pull_val = status,

#define PU_PD_REG(reg, status)		.pu_pd_name = "PU_PD_SEL_"#reg, \
					.pu_pd_reg = PU_PD_SEL_##reg, \
					.pu_pd_val = status,

#define MUX_REG_730(reg, mode_offset, mode) .mux_reg_name = "OMAP730_IO_CONF_"#reg, \
					.mux_reg = OMAP730_IO_CONF_##reg, \
					.mask_offset = mode_offset, \
					.mask = mode,

#define PULL_REG_730(reg, bit, status)	.pull_name = "OMAP730_IO_CONF_"#reg, \
					.pull_reg = OMAP730_IO_CONF_##reg, \
					.pull_bit = bit, \
					.pull_val = status,

#else

#define MUX_REG(reg, mode_offset, mode) .mux_reg = FUNC_MUX_CTRL_##reg, \
					.mask_offset = mode_offset, \
					.mask = mode,

#define PULL_REG(reg, bit, status)	.pull_reg = PULL_DWN_CTRL_##reg, \
					.pull_bit = bit, \
					.pull_val = status,

#define PU_PD_REG(reg, status)		.pu_pd_reg = PU_PD_SEL_##reg, \
					.pu_pd_val = status,

#define MUX_REG_730(reg, mode_offset, mode) \
					.mux_reg = OMAP730_IO_CONF_##reg, \
					.mask_offset = mode_offset, \
					.mask = mode,

#define PULL_REG_730(reg, bit, status)	.pull_reg = OMAP730_IO_CONF_##reg, \
					.pull_bit = bit, \
					.pull_val = status,

#endif /* CONFIG_OMAP_MUX_DEBUG */

#define MUX_CFG(desc, mux_reg, mode_offset, mode,	\
		pull_reg, pull_bit, pull_status,	\
		pu_pd_reg, pu_pd_status, debug_status)	\
{							\
	.name =	 desc,					\
	.debug = debug_status,				\
	MUX_REG(mux_reg, mode_offset, mode)		\
	PULL_REG(pull_reg, pull_bit, pull_status)	\
	PU_PD_REG(pu_pd_reg, pu_pd_status)		\
},


/*
 * OMAP730 has a slightly different config for the pin mux.
 * - config regs are the OMAP730_IO_CONF_x regs (see omap730.h) regs and
 *   not the FUNC_MUX_CTRL_x regs from hardware.h
 * - for pull-up/down, only has one enable bit which is is in the same register
 *   as mux config
 */
#define MUX_CFG_730(desc, mux_reg, mode_offset, mode,	\
		   pull_bit, pull_status, debug_status)\
{							\
	.name =	 desc,					\
	.debug = debug_status,				\
	MUX_REG_730(mux_reg, mode_offset, mode)		\
	PULL_REG_730(mux_reg, pull_bit, pull_status)	\
	PU_PD_REG(NA, 0)		\
},

#define MUX_CFG_24XX(desc, reg_offset, mode,			\
				pull_en, pull_mode, dbg)	\
{								\
	.name		= desc,					\
	.debug		= dbg,					\
	.mux_reg	= reg_offset,				\
	.mask		= mode,					\
	.pull_val	= pull_en,				\
	.pu_pd_val	= pull_mode,				\
},


#define PULL_DISABLED	0
#define PULL_ENABLED	1

#define PULL_DOWN	0
#define PULL_UP		1

struct pin_config {
	char *name;
	unsigned char busy;
	unsigned char debug;

	const char *mux_reg_name;
	const unsigned int mux_reg;
	const unsigned char mask_offset;
	const unsigned char mask;

	const char *pull_name;
	const unsigned int pull_reg;
	const unsigned char pull_val;
	const unsigned char pull_bit;

	const char *pu_pd_name;
	const unsigned int pu_pd_reg;
	const unsigned char pu_pd_val;
};

enum omap730_index {
	/* OMAP 730 keyboard */
	E2_730_KBR0,
	J7_730_KBR1,
	E1_730_KBR2,
	F3_730_KBR3,
	D2_730_KBR4,
	C2_730_KBC0,
	D3_730_KBC1,
	E4_730_KBC2,
	F4_730_KBC3,
	E3_730_KBC4,

	/* USB */
	AA17_730_USB_DM,
	W16_730_USB_PU_EN,
	W17_730_USB_VBUSI,
};

enum omap1xxx_index {
	/* UART1 (BT_UART_GATING)*/
	UART1_TX = 0,
	UART1_RTS,

	/* UART2 (COM_UART_GATING)*/
	UART2_TX,
	UART2_RX,
	UART2_CTS,
	UART2_RTS,

	/* UART3 (GIGA_UART_GATING) */
	UART3_TX,
	UART3_RX,
	UART3_CTS,
	UART3_RTS,
	UART3_CLKREQ,
	UART3_BCLK,	/* 12MHz clock out */
	Y15_1610_UART3_RTS,

	/* PWT & PWL */
	PWT,
	PWL,

	/* USB master generic */
	R18_USB_VBUS,
	R18_1510_USB_GPIO0,
	W4_USB_PUEN,
	W4_USB_CLKO,
	W4_USB_HIGHZ,
	W4_GPIO58,

	/* USB1 master */
	USB1_SUSP,
	USB1_SEO,
	W13_1610_USB1_SE0,
	USB1_TXEN,
	USB1_TXD,
	USB1_VP,
	USB1_VM,
	USB1_RCV,
	USB1_SPEED,
	R13_1610_USB1_SPEED,
	R13_1710_USB1_SE0,

	/* USB2 master */
	USB2_SUSP,
	USB2_VP,
	USB2_TXEN,
	USB2_VM,
	USB2_RCV,
	USB2_SEO,
	USB2_TXD,

	/* OMAP-1510 GPIO */
	R18_1510_GPIO0,
	R19_1510_GPIO1,
	M14_1510_GPIO2,

	/* OMAP1610 GPIO */
	P18_1610_GPIO3,
	Y15_1610_GPIO17,

	/* OMAP-1710 GPIO */
	R18_1710_GPIO0,
	V2_1710_GPIO10,
	N21_1710_GPIO14,
	W15_1710_GPIO40,

	/* MPUIO */
	MPUIO2,
	N15_1610_MPUIO2,
	MPUIO4,
	MPUIO5,
	T20_1610_MPUIO5,
	W11_1610_MPUIO6,
	V10_1610_MPUIO7,
	W11_1610_MPUIO9,
	V10_1610_MPUIO10,
	W10_1610_MPUIO11,
	E20_1610_MPUIO13,
	U20_1610_MPUIO14,
	E19_1610_MPUIO15,

	/* MCBSP2 */
	MCBSP2_CLKR,
	MCBSP2_CLKX,
	MCBSP2_DR,
	MCBSP2_DX,
	MCBSP2_FSR,
	MCBSP2_FSX,

	/* MCBSP3 */
	MCBSP3_CLKX,

	/* Misc ballouts */
	BALLOUT_V8_ARMIO3,
	N20_HDQ,

	/* OMAP-1610 MMC2 */
	W8_1610_MMC2_DAT0,
	V8_1610_MMC2_DAT1,
	W15_1610_MMC2_DAT2,
	R10_1610_MMC2_DAT3,
	Y10_1610_MMC2_CLK,
	Y8_1610_MMC2_CMD,
	V9_1610_MMC2_CMDDIR,
	V5_1610_MMC2_DATDIR0,
	W19_1610_MMC2_DATDIR1,
	R18_1610_MMC2_CLKIN,

	/* OMAP-1610 External Trace Interface */
	M19_1610_ETM_PSTAT0,
	L15_1610_ETM_PSTAT1,
	L18_1610_ETM_PSTAT2,
	L19_1610_ETM_D0,
	J19_1610_ETM_D6,
	J18_1610_ETM_D7,

	/* OMAP16XX GPIO */
	P20_1610_GPIO4,
	V9_1610_GPIO7,
	W8_1610_GPIO9,
	N20_1610_GPIO11,
	N19_1610_GPIO13,
	P10_1610_GPIO22,
	V5_1610_GPIO24,
	AA20_1610_GPIO_41,
	W19_1610_GPIO48,
	M7_1610_GPIO62,
	V14_16XX_GPIO37,
	R9_16XX_GPIO18,
	L14_16XX_GPIO49,

	/* OMAP-1610 uWire */
	V19_1610_UWIRE_SCLK,
	U18_1610_UWIRE_SDI,
	W21_1610_UWIRE_SDO,
	N14_1610_UWIRE_CS0,
	P15_1610_UWIRE_CS3,
	N15_1610_UWIRE_CS1,

	/* OMAP-1610 Flash */
	L3_1610_FLASH_CS2B_OE,
	M8_1610_FLASH_CS2B_WE,

	/* First MMC */
	MMC_CMD,
	MMC_DAT1,
	MMC_DAT2,
	MMC_DAT0,
	MMC_CLK,
	MMC_DAT3,

	/* OMAP-1710 MMC CMDDIR and DATDIR0 */
	M15_1710_MMC_CLKI,
	P19_1710_MMC_CMDDIR,
	P20_1710_MMC_DATDIR0,

	/* OMAP-1610 USB0 alternate pin configuration */
	W9_USB0_TXEN,
	AA9_USB0_VP,
	Y5_USB0_RCV,
	R9_USB0_VM,
	V6_USB0_TXD,
	W5_USB0_SE0,
	V9_USB0_SPEED,
	V9_USB0_SUSP,

	/* USB2 */
	W9_USB2_TXEN,
	AA9_USB2_VP,
	Y5_USB2_RCV,
	R9_USB2_VM,
	V6_USB2_TXD,
	W5_USB2_SE0,

	/* 16XX UART */
	R13_1610_UART1_TX,
	V14_16XX_UART1_RX,
	R14_1610_UART1_CTS,
	AA15_1610_UART1_RTS,
	R9_16XX_UART2_RX,
	L14_16XX_UART3_RX,

	/* I2C OMAP-1610 */
	I2C_SCL,
	I2C_SDA,

	/* Keypad */
	F18_1610_KBC0,
	D20_1610_KBC1,
	D19_1610_KBC2,
	E18_1610_KBC3,
	C21_1610_KBC4,
	G18_1610_KBR0,
	F19_1610_KBR1,
	H14_1610_KBR2,
	E20_1610_KBR3,
	E19_1610_KBR4,
	N19_1610_KBR5,

	/* Power management */
	T20_1610_LOW_PWR,

	/* MCLK Settings */
	V5_1710_MCLK_ON,
	V5_1710_MCLK_OFF,
	R10_1610_MCLK_ON,
	R10_1610_MCLK_OFF,

	/* CompactFlash controller */
	P11_1610_CF_CD2,
	R11_1610_CF_IOIS16,
	V10_1610_CF_IREQ,
	W10_1610_CF_RESET,
	W11_1610_CF_CD1,
};

enum omap24xx_index {
	/* 24xx I2C */
	M19_24XX_I2C1_SCL,
	L15_24XX_I2C1_SDA,
	J15_24XX_I2C2_SCL,
	H19_24XX_I2C2_SDA,

	/* 24xx Menelaus interrupt */
	W19_24XX_SYS_NIRQ,

	/* 24xx clock */
	W14_24XX_SYS_CLKOUT,

	/* 242X McBSP */
	Y15_24XX_MCBSP2_CLKX,
	R14_24XX_MCBSP2_FSX,
	W15_24XX_MCBSP2_DR,
	V15_24XX_MCBSP2_DX,

	/* 24xx GPIO */
	M21_242X_GPIO11,
	AA10_242X_GPIO13,
	AA6_242X_GPIO14,
	AA4_242X_GPIO15,
	Y11_242X_GPIO16,
	AA12_242X_GPIO17,
	AA8_242X_GPIO58,
	Y20_24XX_GPIO60,
	W4__24XX_GPIO74,
	M15_24XX_GPIO92,
	V14_24XX_GPIO117,

	P20_24XX_TSC_IRQ,

	/* UART3 */
	K15_24XX_UART3_TX,
	K14_24XX_UART3_RX,

	/* Keypad GPIO*/
	T19_24XX_KBR0,
	R19_24XX_KBR1,
	V18_24XX_KBR2,
	M21_24XX_KBR3,
	E5__24XX_KBR4,
	M18_24XX_KBR5,
	R20_24XX_KBC0,
	M14_24XX_KBC1,
	H19_24XX_KBC2,
	V17_24XX_KBC3,
	P21_24XX_KBC4,
	L14_24XX_KBC5,
	N19_24XX_KBC6,

	/* 24xx Menelaus Keypad GPIO */
	B3__24XX_KBR5,
	AA4_24XX_KBC2,
	B13_24XX_KBC6,
};

#ifdef	CONFIG_OMAP_MUX
/* setup pin muxing in Linux */
extern int omap1_mux_init(void);
extern int omap2_mux_init(void);
extern int omap_mux_register(struct pin_config * pins, unsigned long size);
extern int omap_cfg_reg(unsigned long reg_cfg);
#else
/* boot loader does it all (no warnings from CONFIG_OMAP_MUX_WARNINGS) */
static inline int omap1_mux_init(void) { return 0; }
static inline int omap2_mux_init(void) { return 0; }
static inline int omap_cfg_reg(unsigned long reg_cfg) { return 0; }
#endif

#endif
