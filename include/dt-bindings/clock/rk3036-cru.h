/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2015 Rockchip Electronics Co. Ltd.
 * Author: Xing Zheng <zhengxing@rock-chips.com>
 */

#ifndef _DT_BINDINGS_CLK_ROCKCHIP_RK3036_H
#define _DT_BINDINGS_CLK_ROCKCHIP_RK3036_H

/* core clocks */
#define PLL_APLL		1
#define PLL_DPLL		2
#define PLL_GPLL		3
#define ARMCLK			4

/* sclk gates (special clocks) */
#define SCLK_GPU		64
#define SCLK_SPI		65
#define SCLK_SDMMC		68
#define SCLK_SDIO		69
#define SCLK_EMMC		71
#define SCLK_NANDC		76
#define SCLK_UART0		77
#define SCLK_UART1		78
#define SCLK_UART2		79
#define SCLK_I2S		82
#define SCLK_SPDIF		83
#define SCLK_TIMER0		85
#define SCLK_TIMER1		86
#define SCLK_TIMER2		87
#define SCLK_TIMER3		88
#define SCLK_OTGPHY0		93
#define SCLK_LCDC		100
#define SCLK_HDMI		109
#define SCLK_HEVC		111
#define SCLK_I2S_OUT		113
#define SCLK_SDMMC_DRV		114
#define SCLK_SDIO_DRV		115
#define SCLK_EMMC_DRV		117
#define SCLK_SDMMC_SAMPLE	118
#define SCLK_SDIO_SAMPLE	119
#define SCLK_EMMC_SAMPLE	121
#define SCLK_PVTM_CORE		123
#define SCLK_PVTM_GPU		124
#define SCLK_PVTM_VIDEO		125
#define SCLK_MAC		151
#define SCLK_MACREF		152
#define SCLK_MACPLL		153
#define SCLK_SFC		160
#define SCLK_USB480M		161

/* aclk gates */
#define ACLK_DMAC2		194
#define ACLK_LCDC		197
#define ACLK_VIO		203
#define ACLK_VCODEC		208
#define ACLK_CPU		209
#define ACLK_PERI		210

/* pclk gates */
#define PCLK_GPIO0		320
#define PCLK_GPIO1		321
#define PCLK_GPIO2		322
#define PCLK_GRF		329
#define PCLK_I2C0		332
#define PCLK_I2C1		333
#define PCLK_I2C2		334
#define PCLK_SPI		338
#define PCLK_UART0		341
#define PCLK_UART1		342
#define PCLK_UART2		343
#define PCLK_PWM		350
#define PCLK_TIMER		353
#define PCLK_HDMI		360
#define PCLK_CPU		362
#define PCLK_PERI		363
#define PCLK_DDRUPCTL		364
#define PCLK_WDT		368
#define PCLK_ACODEC		369

/* hclk gates */
#define HCLK_OTG0		449
#define HCLK_OTG1		450
#define HCLK_NANDC		453
#define HCLK_SFC		454
#define HCLK_SDMMC		456
#define HCLK_SDIO		457
#define HCLK_EMMC		459
#define HCLK_MAC		460
#define HCLK_I2S		462
#define HCLK_LCDC		465
#define HCLK_ROM		467
#define HCLK_VIO_BUS		472
#define HCLK_VCODEC		476
#define HCLK_CPU		477
#define HCLK_PERI		478

/* soft-reset indices */
#define SRST_CORE0		0
#define SRST_CORE1		1
#define SRST_CORE0_DBG		4
#define SRST_CORE1_DBG		5
#define SRST_CORE0_POR		8
#define SRST_CORE1_POR		9
#define SRST_L2C		12
#define SRST_TOPDBG		13
#define SRST_STRC_SYS_A		14
#define SRST_PD_CORE_NIU	15

#define SRST_TIMER2		16
#define SRST_CPUSYS_H		17
#define SRST_AHB2APB_H		19
#define SRST_TIMER3		20
#define SRST_INTMEM		21
#define SRST_ROM		22
#define SRST_PERI_NIU		23
#define SRST_I2S		24
#define SRST_DDR_PLL		25
#define SRST_GPU_DLL		26
#define SRST_TIMER0		27
#define SRST_TIMER1		28
#define SRST_CORE_DLL		29
#define SRST_EFUSE_P		30
#define SRST_ACODEC_P		31

#define SRST_GPIO0		32
#define SRST_GPIO1		33
#define SRST_GPIO2		34
#define SRST_UART0		39
#define SRST_UART1		40
#define SRST_UART2		41
#define SRST_I2C0		43
#define SRST_I2C1		44
#define SRST_I2C2		45
#define SRST_SFC		47

#define SRST_PWM0		48
#define SRST_DAP		51
#define SRST_DAP_SYS		52
#define SRST_GRF		55
#define SRST_PERIPHSYS_A	57
#define SRST_PERIPHSYS_H	58
#define SRST_PERIPHSYS_P	59
#define SRST_CPU_PERI		61
#define SRST_EMEM_PERI		62
#define SRST_USB_PERI		63

#define SRST_DMA2		64
#define SRST_MAC		66
#define SRST_NANDC		68
#define SRST_USBOTG0		69
#define SRST_OTGC0		71
#define SRST_USBOTG1		72
#define SRST_OTGC1		74
#define SRST_DDRMSCH		79

#define SRST_MMC0		81
#define SRST_SDIO		82
#define SRST_EMMC		83
#define SRST_SPI0		84
#define SRST_WDT		86
#define SRST_DDRPHY		88
#define SRST_DDRPHY_P		89
#define SRST_DDRCTRL		90
#define SRST_DDRCTRL_P		91

#define SRST_HDMI_P		96
#define SRST_VIO_BUS_H		99
#define SRST_UTMI0		103
#define SRST_UTMI1		104
#define SRST_USBPOR		105

#define SRST_VCODEC_A		112
#define SRST_VCODEC_H		113
#define SRST_VIO1_A		114
#define SRST_HEVC		115
#define SRST_VCODEC_NIU_A	116
#define SRST_LCDC1_A		117
#define SRST_LCDC1_H		118
#define SRST_LCDC1_D		119
#define SRST_GPU		120
#define SRST_GPU_NIU_A		122

#define SRST_DBG_P		131

#endif
