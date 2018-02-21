/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Meson-AXG clock tree IDs
 *
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 */

#ifndef __AXG_CLKC_H
#define __AXG_CLKC_H

#define CLKID_SYS_PLL				0
#define CLKID_FIXED_PLL				1
#define CLKID_FCLK_DIV2				2
#define CLKID_FCLK_DIV3				3
#define CLKID_FCLK_DIV4				4
#define CLKID_FCLK_DIV5				5
#define CLKID_FCLK_DIV7				6
#define CLKID_GP0_PLL				7
#define CLKID_CLK81				10
#define CLKID_MPLL0				11
#define CLKID_MPLL1				12
#define CLKID_MPLL2				13
#define CLKID_MPLL3				14
#define CLKID_DDR				15
#define CLKID_AUDIO_LOCKER			16
#define CLKID_MIPI_DSI_HOST			17
#define CLKID_ISA				18
#define CLKID_PL301				19
#define CLKID_PERIPHS				20
#define CLKID_SPICC0				21
#define CLKID_I2C				22
#define CLKID_RNG0				23
#define CLKID_UART0				24
#define CLKID_MIPI_DSI_PHY			25
#define CLKID_SPICC1				26
#define CLKID_PCIE_A				27
#define CLKID_PCIE_B				28
#define CLKID_HIU_IFACE				29
#define CLKID_ASSIST_MISC			30
#define CLKID_SD_EMMC_B				31
#define CLKID_SD_EMMC_C				32
#define CLKID_DMA				33
#define CLKID_SPI				34
#define CLKID_AUDIO				35
#define CLKID_ETH				36
#define CLKID_UART1				37
#define CLKID_G2D				38
#define CLKID_USB0				39
#define CLKID_USB1				40
#define CLKID_RESET				41
#define CLKID_USB				42
#define CLKID_AHB_ARB0				43
#define CLKID_EFUSE				44
#define CLKID_BOOT_ROM				45
#define CLKID_AHB_DATA_BUS			46
#define CLKID_AHB_CTRL_BUS			47
#define CLKID_USB1_DDR_BRIDGE			48
#define CLKID_USB0_DDR_BRIDGE			49
#define CLKID_MMC_PCLK				50
#define CLKID_VPU_INTR				51
#define CLKID_SEC_AHB_AHB3_BRIDGE		52
#define CLKID_GIC				53
#define CLKID_AO_MEDIA_CPU			54
#define CLKID_AO_AHB_SRAM			55
#define CLKID_AO_AHB_BUS			56
#define CLKID_AO_IFACE				57
#define CLKID_AO_I2C				58
#define CLKID_SD_EMMC_B_CLK0			59
#define CLKID_SD_EMMC_C_CLK0			60

#endif /* __AXG_CLKC_H */
