/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _TEGRA124_CAR_
#define	_TEGRA124_CAR_

#include "clkdev_if.h"

#define	RD4(sc, reg, val)	CLKDEV_READ_4((sc)->clkdev, reg, val)
#define	WR4(sc, reg, val)	CLKDEV_WRITE_4((sc)->clkdev, reg, val)
#define	MD4(sc, reg, mask, set)	CLKDEV_MODIFY_4((sc)->clkdev, reg, mask, set)
#define	DEVICE_LOCK(sc)		CLKDEV_DEVICE_LOCK((sc)->clkdev)
#define	DEVICE_UNLOCK(sc)	CLKDEV_DEVICE_UNLOCK((sc)->clkdev)

#define	RST_DEVICES_L			0x004
#define	RST_DEVICES_H			0x008
#define	RST_DEVICES_U			0x00C
#define	CLK_OUT_ENB_L			0x010
#define	CLK_OUT_ENB_H			0x014
#define	CLK_OUT_ENB_U			0x018
#define	CCLK_BURST_POLICY		0x020
#define	SUPER_CCLK_DIVIDER		0x024
#define	SCLK_BURST_POLICY		0x028
#define	SUPER_SCLK_DIVIDER		0x02c
#define	CLK_SYSTEM_RATE			0x030

#define	OSC_CTRL			0x050
 #define	OSC_CTRL_OSC_FREQ_SHIFT		28
 #define	OSC_CTRL_PLL_REF_DIV_SHIFT		26

#define	PLLE_SS_CNTL 			0x068
#define	 PLLE_SS_CNTL_SSCINCINTRV_MASK		(0x3f << 24)
#define	 PLLE_SS_CNTL_SSCINCINTRV_VAL 		(0x20 << 24)
#define	 PLLE_SS_CNTL_SSCINC_MASK 		(0xff << 16)
#define	 PLLE_SS_CNTL_SSCINC_VAL 		(0x1 << 16)
#define	 PLLE_SS_CNTL_SSCINVERT 		(1 << 15)
#define	 PLLE_SS_CNTL_SSCCENTER 		(1 << 14)
#define	 PLLE_SS_CNTL_SSCBYP 			(1 << 12)
#define	 PLLE_SS_CNTL_INTERP_RESET 		(1 << 11)
#define	 PLLE_SS_CNTL_BYPASS_SS 		(1 << 10)
#define	 PLLE_SS_CNTL_SSCMAX_MASK		0x1ff
#define	 PLLE_SS_CNTL_SSCMAX_VAL 		0x25
#define	 PLLE_SS_CNTL_DISABLE 			(PLLE_SS_CNTL_BYPASS_SS |    \
						 PLLE_SS_CNTL_INTERP_RESET | \
						 PLLE_SS_CNTL_SSCBYP)
#define	 PLLE_SS_CNTL_COEFFICIENTS_MASK 	(PLLE_SS_CNTL_SSCMAX_MASK |  \
						 PLLE_SS_CNTL_SSCINC_MASK |  \
						 PLLE_SS_CNTL_SSCINCINTRV_MASK)
#define	 PLLE_SS_CNTL_COEFFICIENTS_VAL 		(PLLE_SS_CNTL_SSCMAX_VAL |   \
						 PLLE_SS_CNTL_SSCINC_VAL |   \
						 PLLE_SS_CNTL_SSCINCINTRV_VAL)

#define	PLLC_BASE			0x080
#define	PLLC_OUT			0x084
#define	PLLC_MISC2			0x088
#define	PLLC_MISC			0x08c
#define	PLLM_BASE			0x090
#define	PLLM_OUT			0x094
#define	PLLM_MISC			0x09c
#define	PLLP_BASE			0x0a0
#define	PLLP_MISC			0x0ac
#define	PLLP_OUTA			0x0a4
#define	PLLP_OUTB			0x0a8
#define	PLLA_BASE			0x0b0
#define	PLLA_OUT			0x0b4
#define	PLLA_MISC			0x0bc
#define	PLLU_BASE			0x0c0
#define	PLLU_MISC			0x0cc
#define	PLLD_BASE			0x0d0
#define	PLLD_MISC			0x0dc
#define	PLLX_BASE			0x0e0
#define	PLLX_MISC			0x0e4
#define	PLLE_BASE			0x0e8
#define	 PLLE_BASE_LOCK_OVERRIDE		(1 << 29)
#define	 PLLE_BASE_DIVCML_SHIFT 		24
#define	 PLLE_BASE_DIVCML_MASK 			0xf

#define	PLLE_MISC			0x0ec
#define	 PLLE_MISC_SETUP_BASE_SHIFT 		16
#define	 PLLE_MISC_SETUP_BASE_MASK 		(0xffff << PLLE_MISC_SETUP_BASE_SHIFT)
#define	 PLLE_MISC_READY 			(1 << 15)
#define	 PLLE_MISC_IDDQ_SWCTL			(1 << 14)
#define	 PLLE_MISC_IDDQ_OVERRIDE_VALUE		(1 << 13)
#define	 PLLE_MISC_LOCK 			(1 << 11)
#define	 PLLE_MISC_REF_ENABLE 			(1 << 10)
#define	 PLLE_MISC_LOCK_ENABLE 			(1 << 9)
#define	 PLLE_MISC_PTS 				(1 << 8)
#define	 PLLE_MISC_VREG_BG_CTRL_SHIFT		4
#define	 PLLE_MISC_VREG_BG_CTRL_MASK		(3 << PLLE_MISC_VREG_BG_CTRL_SHIFT)
#define	 PLLE_MISC_VREG_CTRL_SHIFT		2
#define	 PLLE_MISC_VREG_CTRL_MASK		(2 << PLLE_MISC_VREG_CTRL_SHIFT)

#define	CLK_SOURCE_I2S1			0x100
#define	CLK_SOURCE_I2S2			0x104
#define	CLK_SOURCE_SPDIF_OUT		0x108
#define	CLK_SOURCE_SPDIF_IN		0x10c
#define	CLK_SOURCE_PWM			0x110
#define	CLK_SOURCE_SPI2			0x118
#define	CLK_SOURCE_SPI3			0x11c
#define	CLK_SOURCE_I2C1			0x124
#define	CLK_SOURCE_I2C5			0x128
#define	CLK_SOURCE_SPI1			0x134
#define	CLK_SOURCE_DISP1		0x138
#define	CLK_SOURCE_DISP2		0x13c
#define	CLK_SOURCE_ISP			0x144
#define	CLK_SOURCE_VI			0x148
#define	CLK_SOURCE_SDMMC1		0x150
#define	CLK_SOURCE_SDMMC2		0x154
#define	CLK_SOURCE_SDMMC4		0x164
#define	CLK_SOURCE_VFIR			0x168
#define	CLK_SOURCE_HSI			0x174
#define	CLK_SOURCE_UARTA		0x178
#define	CLK_SOURCE_UARTB		0x17c
#define	CLK_SOURCE_HOST1X		0x180
#define	CLK_SOURCE_HDMI			0x18c
#define	CLK_SOURCE_I2C2			0x198
#define	CLK_SOURCE_EMC			0x19c
#define	CLK_SOURCE_UARTC		0x1a0
#define	CLK_SOURCE_VI_SENSOR		0x1a8
#define	CLK_SOURCE_SPI4			0x1b4
#define	CLK_SOURCE_I2C3			0x1b8
#define	CLK_SOURCE_SDMMC3		0x1bc
#define	CLK_SOURCE_UARTD		0x1c0
#define	CLK_SOURCE_VDE			0x1c8
#define	CLK_SOURCE_OWR			0x1cc
#define	CLK_SOURCE_NOR			0x1d0
#define	CLK_SOURCE_CSITE		0x1d4
#define	CLK_SOURCE_I2S0			0x1d8
#define	CLK_SOURCE_DTV			0x1dc
#define	CLK_SOURCE_MSENC		0x1f0
#define	CLK_SOURCE_TSEC			0x1f4
#define	CLK_SOURCE_SPARE2		0x1f8

#define	CLK_OUT_ENB_X			0x280
#define	RST_DEVICES_X			0x28C

#define	RST_DEVICES_V			0x358
#define	RST_DEVICES_W			0x35C
#define	CLK_OUT_ENB_V			0x360
#define	CLK_OUT_ENB_W			0x364
#define	CCLKG_BURST_POLICY		0x368
#define	SUPER_CCLKG_DIVIDER		0x36C
#define	CCLKLP_BURST_POLICY		0x370
#define	SUPER_CCLKLP_DIVIDER		0x374

#define	CLK_SOURCE_MSELECT		0x3b4
#define	CLK_SOURCE_TSENSOR		0x3b8
#define	CLK_SOURCE_I2S3			0x3bc
#define	CLK_SOURCE_I2S4			0x3c0
#define	CLK_SOURCE_I2C4			0x3c4
#define	CLK_SOURCE_SPI5			0x3c8
#define	CLK_SOURCE_SPI6			0x3cc
#define	CLK_SOURCE_AUDIO		0x3d0
#define	CLK_SOURCE_DAM0			0x3d8
#define	CLK_SOURCE_DAM1			0x3dc
#define	CLK_SOURCE_DAM2			0x3e0
#define	CLK_SOURCE_HDA2CODEC_2X		0x3e4
#define	CLK_SOURCE_ACTMON		0x3e8
#define	CLK_SOURCE_EXTPERIPH1		0x3ec
#define	CLK_SOURCE_EXTPERIPH2		0x3f0
#define	CLK_SOURCE_EXTPERIPH3		0x3f4
#define	CLK_SOURCE_I2C_SLOW		0x3fc

#define	CLK_SOURCE_SYS			0x400
#define	CLK_SOURCE_SOR0			0x414
#define	CLK_SOURCE_SATA_OOB		0x420
#define	CLK_SOURCE_SATA			0x424
#define	CLK_SOURCE_HDA			0x428
#define	UTMIP_PLL_CFG0			0x480
#define	UTMIP_PLL_CFG1			0x484
#define	 UTMIP_PLL_CFG1_FORCE_PLLU_POWERUP		(1 << 17)
#define	 UTMIP_PLL_CFG1_FORCE_PLLU_POWERDOWN		(1 << 16)
#define	 UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERUP	(1 << 15)
#define	 UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERDOWN	(1 << 14)
#define	 UTMIP_PLL_CFG1_FORCE_PLL_ACTIVE_POWERDOWN 	(1 << 12)
#define	 UTMIP_PLL_CFG1_ENABLE_DLY_COUNT(x)		(((x) & 0x1f) << 6)
#define	 UTMIP_PLL_CFG1_XTAL_FREQ_COUNT(x)		(((x) & 0xfff) << 0)

#define	UTMIP_PLL_CFG2			0x488
#define	 UTMIP_PLL_CFG2_ACTIVE_DLY_COUNT(x)		(((x) & 0x3f) << 18)
#define	 UTMIP_PLL_CFG2_STABLE_COUNT(x)			(((x) & 0xffff) << 6)
#define	 UTMIP_PLL_CFG2_FORCE_PD_SAMP_C_POWERDOWN 	(1 << 4)
#define	 UTMIP_PLL_CFG2_FORCE_PD_SAMP_B_POWERDOWN 	(1 << 2)
#define	 UTMIP_PLL_CFG2_FORCE_PD_SAMP_A_POWERDOWN	(1 << 0)

#define	PLLE_AUX			0x48c
#define	 PLLE_AUX_PLLRE_SEL				(1 << 28)
#define	 PLLE_AUX_SEQ_START_STATE 			(1 << 25)
#define	 PLLE_AUX_SEQ_ENABLE				(1 << 24)
#define	 PLLE_AUX_SS_SWCTL				(1 << 6)
#define	 PLLE_AUX_ENABLE_SWCTL				(1 << 4)
#define	 PLLE_AUX_USE_LOCKDET				(1 << 3)
#define	 PLLE_AUX_PLLP_SEL				(1 << 2)

#define	SATA_PLL_CFG0			0x490
#define	SATA_PLL_CFG0_SEQ_START_STATE			(1 << 25)
#define	SATA_PLL_CFG0_SEQ_ENABLE			(1 << 24)
#define	SATA_PLL_CFG0_SEQ_PADPLL_PD_INPUT_VALUE		(1 << 7)
#define	SATA_PLL_CFG0_SEQ_LANE_PD_INPUT_VALUE		(1 << 6)
#define	SATA_PLL_CFG0_SEQ_RESET_INPUT_VALUE		(1 << 5)
#define	SATA_PLL_CFG0_SEQ_IN_SWCTL			(1 << 4)
#define	SATA_PLL_CFG0_PADPLL_USE_LOCKDET		(1 << 2)
#define	SATA_PLL_CFG0_PADPLL_RESET_OVERRIDE_VALUE	(1 << 1)
#define	SATA_PLL_CFG0_PADPLL_RESET_SWCTL		(1 << 0)

#define	SATA_PLL_CFG1			0x494
#define	PCIE_PLL_CFG0			0x498
#define	PCIE_PLL_CFG0_SEQ_START_STATE			(1 << 25)
#define	PCIE_PLL_CFG0_SEQ_ENABLE			(1 << 24)

#define	PLLD2_BASE			0x4b8
#define	PLLD2_MISC			0x4bc
#define	UTMIP_PLL_CFG3			0x4c0
#define	PLLRE_BASE			0x4c4
#define	PLLRE_MISC			0x4c8
#define	PLLC2_BASE			0x4e8
#define	PLLC2_MISC			0x4ec
#define	PLLC3_BASE			0x4fc

#define	PLLC3_MISC			0x500
#define	PLLX_MISC2			0x514
#define	PLLX_MISC2			0x514
#define	PLLX_MISC3			0x518
#define	 PLLX_MISC3_DYNRAMP_STEPB_MASK		0xFF
#define	 PLLX_MISC3_DYNRAMP_STEPB_SHIFT		24
#define	 PLLX_MISC3_DYNRAMP_STEPA_MASK		0xFF
#define	 PLLX_MISC3_DYNRAMP_STEPA_SHIFT		16
#define	 PLLX_MISC3_NDIV_NEW_MASK		0xFF
#define	 PLLX_MISC3_NDIV_NEW_SHIFT		8
#define	 PLLX_MISC3_EN_FSTLCK			(1 << 5)
#define	 PLLX_MISC3_LOCK_OVERRIDE		(1 << 4)
#define	 PLLX_MISC3_PLL_FREQLOCK		(1 << 3)
#define	 PLLX_MISC3_DYNRAMP_DONE		(1 << 2)
#define	 PLLX_MISC3_CLAMP_NDIV			(1 << 1)
#define	 PLLX_MISC3_EN_DYNRAMP			(1 << 0)
#define	XUSBIO_PLL_CFG0			0x51c
#define	 XUSBIO_PLL_CFG0_SEQ_START_STATE		(1 << 25)
#define	 XUSBIO_PLL_CFG0_SEQ_ENABLE			(1 << 24)
#define	 XUSBIO_PLL_CFG0_PADPLL_USE_LOCKDET		(1 << 6)
#define	 XUSBIO_PLL_CFG0_CLK_ENABLE_SWCTL		(1 << 2)
#define	 XUSBIO_PLL_CFG0_PADPLL_RESET_SWCTL		(1 << 0)

#define	PLLP_RESHIFT			0x528
#define	UTMIPLL_HW_PWRDN_CFG0		0x52c
#define	 UTMIPLL_HW_PWRDN_CFG0_SEQ_START_STATE		(1 << 25)
#define	 UTMIPLL_HW_PWRDN_CFG0_SEQ_ENABLE		(1 << 24)
#define	 UTMIPLL_HW_PWRDN_CFG0_USE_LOCKDET		(1 << 6)
#define	 UTMIPLL_HW_PWRDN_CFG0_SEQ_RESET_INPUT_VALUE	(1 << 5)
#define	 UTMIPLL_HW_PWRDN_CFG0_SEQ_IN_SWCTL		(1 << 4)
#define	 UTMIPLL_HW_PWRDN_CFG0_CLK_ENABLE_SWCTL		(1 << 2)
#define	 UTMIPLL_HW_PWRDN_CFG0_IDDQ_OVERRIDE		(1 << 1)
#define	 UTMIPLL_HW_PWRDN_CFG0_IDDQ_SWCTL		(1 << 0)

#define	PLLDP_BASE			0x590
#define	PLLDP_MISC			0x594
#define	PLLC4_BASE			0x5a4
#define	PLLC4_MISC			0x5a8

#define	CLK_SOURCE_XUSB_CORE_HOST	0x600
#define	CLK_SOURCE_XUSB_FALCON		0x604
#define	CLK_SOURCE_XUSB_FS		0x608
#define	CLK_SOURCE_XUSB_CORE_DEV	0x60c
#define	CLK_SOURCE_XUSB_SS		0x610
#define	CLK_SOURCE_CILAB		0x614
#define	CLK_SOURCE_CILCD		0x618
#define	CLK_SOURCE_CILE			0x61c
#define	CLK_SOURCE_DSIA_LP		0x620
#define	CLK_SOURCE_DSIB_LP		0x624
#define	CLK_SOURCE_ENTROPY		0x628
#define	CLK_SOURCE_DVFS_REF		0x62c
#define	CLK_SOURCE_DVFS_SOC		0x630
#define	CLK_SOURCE_TRACECLKIN		0x634
#define	CLK_SOURCE_ADX			0x638
#define	CLK_SOURCE_AMX			0x63c
#define	CLK_SOURCE_EMC_LATENCY		0x640
#define	CLK_SOURCE_SOC_THERM		0x644
#define	CLK_SOURCE_VI_SENSOR2		0x658
#define	CLK_SOURCE_I2C6			0x65c
#define	CLK_SOURCE_EMC_DLL		0x664
#define	CLK_SOURCE_HDMI_AUDIO		0x668
#define	CLK_SOURCE_CLK72MHZ		0x66c
#define	CLK_SOURCE_ADX1			0x670
#define	CLK_SOURCE_AMX1			0x674
#define	CLK_SOURCE_VIC			0x678
#define	PLLP_OUTC			0x67c
#define	PLLP_MISC1			0x680


struct tegra124_car_softc {
	device_t		dev;
	struct resource *	mem_res;
	struct mtx		mtx;
	struct clkdom 		*clkdom;
	int			type;
};

struct tegra124_init_item {
	char 		*name;
	char 		*parent;
	uint64_t	frequency;
	int 		enable;
};

void tegra124_init_plls(struct tegra124_car_softc *sc);

void tegra124_periph_clock(struct tegra124_car_softc *sc);
void tegra124_super_mux_clock(struct tegra124_car_softc *sc);

int tegra124_hwreset_by_idx(struct tegra124_car_softc *sc, intptr_t idx,
    bool reset);

#endif /*_TEGRA124_CAR_*/