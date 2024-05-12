/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018-2022, NVIDIA CORPORATION. All rights reserved. */

#ifndef DT_BINDINGS_CLOCK_TEGRA234_CLOCK_H
#define DT_BINDINGS_CLOCK_TEGRA234_CLOCK_H

/**
 * @file
 * @defgroup bpmp_clock_ids Clock ID's
 * @{
 */
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_ACTMON */
#define TEGRA234_CLK_ACTMON			1U
/** @brief output of gate CLK_ENB_ADSP */
#define TEGRA234_CLK_ADSP			2U
/** @brief output of gate CLK_ENB_ADSPNEON */
#define TEGRA234_CLK_ADSPNEON			3U
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AHUB */
#define TEGRA234_CLK_AHUB			4U
/** @brief output of gate CLK_ENB_APB2APE */
#define TEGRA234_CLK_APB2APE			5U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_APE */
#define TEGRA234_CLK_APE			6U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AUD_MCLK */
#define TEGRA234_CLK_AUD_MCLK			7U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AXI_CBB */
#define TEGRA234_CLK_AXI_CBB			8U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_CAN1 */
#define TEGRA234_CLK_CAN1			9U
/** @brief output of gate CLK_ENB_CAN1_HOST */
#define TEGRA234_CLK_CAN1_HOST			10U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_CAN2 */
#define TEGRA234_CLK_CAN2			11U
/** @brief output of gate CLK_ENB_CAN2_HOST */
#define TEGRA234_CLK_CAN2_HOST			12U
/** @brief output of divider CLK_RST_CONTROLLER_CLK_M_DIVIDE */
#define TEGRA234_CLK_CLK_M			14U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC1 */
#define TEGRA234_CLK_DMIC1			15U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC2 */
#define TEGRA234_CLK_DMIC2			16U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC3 */
#define TEGRA234_CLK_DMIC3			17U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC4 */
#define TEGRA234_CLK_DMIC4			18U
/** @brief output of gate CLK_ENB_DPAUX */
#define TEGRA234_CLK_DPAUX			19U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVJPG1 */
#define TEGRA234_CLK_NVJPG1			20U
/**
 * @brief output of mux controlled by CLK_RST_CONTROLLER_ACLK_BURST_POLICY
 * divided by the divider controlled by ACLK_CLK_DIVISOR in
 * CLK_RST_CONTROLLER_SUPER_ACLK_DIVIDER
 */
#define TEGRA234_CLK_ACLK			21U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_MSS_ENCRYPT switch divider output */
#define TEGRA234_CLK_MSS_ENCRYPT		22U
/** @brief clock recovered from EAVB input */
#define TEGRA234_CLK_EQOS_RX_INPUT		23U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_AON_APB switch divider output */
#define TEGRA234_CLK_AON_APB			25U
/** @brief CLK_RST_CONTROLLER_AON_NIC_RATE divider output */
#define TEGRA234_CLK_AON_NIC			26U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_AON_CPU_NIC switch divider output */
#define TEGRA234_CLK_AON_CPU_NIC		27U
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLA1_BASE for use by audio clocks */
#define TEGRA234_CLK_PLLA1			28U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DSPK1 */
#define TEGRA234_CLK_DSPK1			29U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DSPK2 */
#define TEGRA234_CLK_DSPK2			30U
/**
 * @brief controls the EMC clock frequency.
 * @details Doing a clk_set_rate on this clock will select the
 * appropriate clock source, program the source rate and execute a
 * specific sequence to switch to the new clock source for both memory
 * controllers. This can be used to control the balance between memory
 * throughput and memory controller power.
 */
#define TEGRA234_CLK_EMC			31U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_EQOS_AXI_CLK_0 divider gated output */
#define TEGRA234_CLK_EQOS_AXI			32U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_EQOS_PTP_REF_CLK_0 divider gated output */
#define TEGRA234_CLK_EQOS_PTP_REF		33U
/** @brief output of gate CLK_ENB_EQOS_RX */
#define TEGRA234_CLK_EQOS_RX			34U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_EQOS_TX_CLK divider gated output */
#define TEGRA234_CLK_EQOS_TX			35U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_EXTPERIPH1 */
#define TEGRA234_CLK_EXTPERIPH1			36U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_EXTPERIPH2 */
#define TEGRA234_CLK_EXTPERIPH2			37U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_EXTPERIPH3 */
#define TEGRA234_CLK_EXTPERIPH3			38U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_EXTPERIPH4 */
#define TEGRA234_CLK_EXTPERIPH4			39U
/** @brief output of gate CLK_ENB_FUSE */
#define TEGRA234_CLK_FUSE			40U
/** @brief output of GPU GPC0 clkGen (in 1x mode same rate as GPC0 MUX2 out) */
#define TEGRA234_CLK_GPC0CLK			41U
/** @brief TODO */
#define TEGRA234_CLK_GPU_PWR			42U
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_HDA2CODEC_2X */
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_HOST1X */
#define TEGRA234_CLK_HOST1X			46U
/** @brief xusb_hs_hsicp_clk */
#define TEGRA234_CLK_XUSB_HS_HSICP		47U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C1 */
#define TEGRA234_CLK_I2C1			48U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C2 */
#define TEGRA234_CLK_I2C2			49U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C3 */
#define TEGRA234_CLK_I2C3			50U
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C4 */
#define TEGRA234_CLK_I2C4			51U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C6 */
#define TEGRA234_CLK_I2C6			52U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C7 */
#define TEGRA234_CLK_I2C7			53U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C8 */
#define TEGRA234_CLK_I2C8			54U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C9 */
#define TEGRA234_CLK_I2C9			55U
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S1 */
#define TEGRA234_CLK_I2S1			56U
/** @brief clock recovered from I2S1 input */
#define TEGRA234_CLK_I2S1_SYNC_INPUT		57U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S2 */
#define TEGRA234_CLK_I2S2			58U
/** @brief clock recovered from I2S2 input */
#define TEGRA234_CLK_I2S2_SYNC_INPUT		59U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S3 */
#define TEGRA234_CLK_I2S3			60U
/** @brief clock recovered from I2S3 input */
#define TEGRA234_CLK_I2S3_SYNC_INPUT		61U
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S4 */
#define TEGRA234_CLK_I2S4			62U
/** @brief clock recovered from I2S4 input */
#define TEGRA234_CLK_I2S4_SYNC_INPUT		63U
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S5 */
#define TEGRA234_CLK_I2S5			64U
/** @brief clock recovered from I2S5 input */
#define TEGRA234_CLK_I2S5_SYNC_INPUT		65U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S6 */
#define TEGRA234_CLK_I2S6			66U
/** @brief clock recovered from I2S6 input */
#define TEGRA234_CLK_I2S6_SYNC_INPUT		67U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_ISP */
#define TEGRA234_CLK_ISP			69U
/** @brief Monitored branch of EQOS_RX clock */
#define TEGRA234_CLK_EQOS_RX_M			70U
/** @brief CLK_RST_CONTROLLER_MAUDCLK_OUT_SWITCH_DIVIDER switch divider output (maudclk) */
#define TEGRA234_CLK_MAUD			71U
/** @brief output of gate CLK_ENB_MIPI_CAL */
#define TEGRA234_CLK_MIPI_CAL			72U
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_MPHY_CORE_PLL_FIXED */
#define TEGRA234_CLK_MPHY_CORE_PLL_FIXED	73U
/** @brief output of gate CLK_ENB_MPHY_L0_RX_ANA */
#define TEGRA234_CLK_MPHY_L0_RX_ANA		74U
/** @brief output of gate CLK_ENB_MPHY_L0_RX_LS_BIT */
#define TEGRA234_CLK_MPHY_L0_RX_LS_BIT		75U
/** @brief output of gate CLK_ENB_MPHY_L0_RX_SYMB */
#define TEGRA234_CLK_MPHY_L0_RX_SYMB		76U
/** @brief output of gate CLK_ENB_MPHY_L0_TX_LS_3XBIT */
#define TEGRA234_CLK_MPHY_L0_TX_LS_3XBIT	77U
/** @brief output of gate CLK_ENB_MPHY_L0_TX_SYMB */
#define TEGRA234_CLK_MPHY_L0_TX_SYMB		78U
/** @brief output of gate CLK_ENB_MPHY_L1_RX_ANA */
#define TEGRA234_CLK_MPHY_L1_RX_ANA		79U
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_MPHY_TX_1MHZ_REF */
#define TEGRA234_CLK_MPHY_TX_1MHZ_REF		80U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVCSI */
#define TEGRA234_CLK_NVCSI			81U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVCSILP */
#define TEGRA234_CLK_NVCSILP			82U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVDEC */
#define TEGRA234_CLK_NVDEC			83U
/** @brief CLK_RST_CONTROLLER_HUBCLK_OUT_SWITCH_DIVIDER switch divider output (hubclk) */
#define TEGRA234_CLK_HUB			84U
/** @brief CLK_RST_CONTROLLER_DISPCLK_SWITCH_DIVIDER switch divider output (dispclk) */
#define TEGRA234_CLK_DISP			85U
/** @brief RG_CLK_CTRL__0_DIV divider output (nvdisplay_p0_clk) */
#define TEGRA234_CLK_NVDISPLAY_P0		86U
/** @brief RG_CLK_CTRL__1_DIV divider output (nvdisplay_p1_clk) */
#define TEGRA234_CLK_NVDISPLAY_P1		87U
/** @brief DSC_CLK (DISPCLK ÷ 3) */
#define TEGRA234_CLK_DSC			88U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVENC */
#define TEGRA234_CLK_NVENC			89U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVJPG */
#define TEGRA234_CLK_NVJPG			90U
/** @brief input from Tegra's XTAL_IN */
#define TEGRA234_CLK_OSC			91U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_AON_TOUCH switch divider output */
#define TEGRA234_CLK_AON_TOUCH			92U
/** PLL controlled by CLK_RST_CONTROLLER_PLLA_BASE for use by audio clocks */
#define TEGRA234_CLK_PLLA			93U
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLAON_BASE for use by IP blocks in the AON domain */
#define TEGRA234_CLK_PLLAON			94U
/** Fixed 100MHz PLL for PCIe, SATA and superspeed USB */
#define TEGRA234_CLK_PLLE			100U
/** @brief PLLP vco output */
#define TEGRA234_CLK_PLLP			101U
/** @brief PLLP clk output */
#define TEGRA234_CLK_PLLP_OUT0			102U
/** Fixed frequency 960MHz PLL for USB and EAVB */
#define TEGRA234_CLK_UTMIP_PLL			103U
/** @brief output of the divider CLK_RST_CONTROLLER_PLLA_OUT */
#define TEGRA234_CLK_PLLA_OUT0			104U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM1 */
#define TEGRA234_CLK_PWM1			105U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM2 */
#define TEGRA234_CLK_PWM2			106U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM3 */
#define TEGRA234_CLK_PWM3			107U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM4 */
#define TEGRA234_CLK_PWM4			108U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM5 */
#define TEGRA234_CLK_PWM5			109U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM6 */
#define TEGRA234_CLK_PWM6			110U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM7 */
#define TEGRA234_CLK_PWM7			111U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM8 */
#define TEGRA234_CLK_PWM8			112U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_RCE_CPU_NIC output */
#define TEGRA234_CLK_RCE_CPU_NIC		113U
/** @brief CLK_RST_CONTROLLER_RCE_NIC_RATE divider output */
#define TEGRA234_CLK_RCE_NIC			114U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_AON_I2C_SLOW switch divider output */
#define TEGRA234_CLK_AON_I2C_SLOW		117U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SCE_CPU_NIC */
#define TEGRA234_CLK_SCE_CPU_NIC		118U
/** @brief output of divider CLK_RST_CONTROLLER_SCE_NIC_RATE */
#define TEGRA234_CLK_SCE_NIC			119U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SDMMC1 */
#define TEGRA234_CLK_SDMMC1			120U
/** @brief Logical clk for setting the UPHY PLL3 rate */
#define TEGRA234_CLK_UPHY_PLL3			121U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SDMMC4 */
#define TEGRA234_CLK_SDMMC4			123U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_SE switch divider gated output */
#define TEGRA234_CLK_SE				124U
/** @brief VPLL select for sor0_ref clk driven by disp_2clk_sor0_head_sel signal */
#define TEGRA234_CLK_SOR0_PLL_REF		125U
/** @brief Output of mux controlled by disp_2clk_sor0_pll_ref_clk_safe signal (sor0_ref_clk) */
#define TEGRA234_CLK_SOR0_REF			126U
/** @brief VPLL select for sor1_ref clk driven by disp_2clk_sor0_head_sel signal */
#define TEGRA234_CLK_SOR1_PLL_REF		127U
/** @brief SOR_PLL_REF_CLK_CTRL__0_DIV divider output */
#define TEGRA234_CLK_PRE_SOR0_REF		128U
/** @brief Output of mux controlled by disp_2clk_sor1_pll_ref_clk_safe signal (sor1_ref_clk) */
#define TEGRA234_CLK_SOR1_REF			129U
/** @brief SOR_PLL_REF_CLK_CTRL__1_DIV divider output */
#define TEGRA234_CLK_PRE_SOR1_REF		130U
/** @brief output of gate CLK_ENB_SOR_SAFE */
#define TEGRA234_CLK_SOR_SAFE			131U
/** @brief SOR_CLK_CTRL__0_DIV divider output */
#define TEGRA234_CLK_SOR0_DIV			132U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC5 */
#define TEGRA234_CLK_DMIC5			134U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SPI1 */
#define TEGRA234_CLK_SPI1			135U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SPI2 */
#define TEGRA234_CLK_SPI2			136U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SPI3 */
#define TEGRA234_CLK_SPI3			137U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C_SLOW */
#define TEGRA234_CLK_I2C_SLOW			138U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_DMIC1 */
#define TEGRA234_CLK_SYNC_DMIC1			139U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_DMIC2 */
#define TEGRA234_CLK_SYNC_DMIC2			140U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_DMIC3 */
#define TEGRA234_CLK_SYNC_DMIC3			141U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_DMIC4 */
#define TEGRA234_CLK_SYNC_DMIC4			142U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_DSPK1 */
#define TEGRA234_CLK_SYNC_DSPK1			143U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_DSPK2 */
#define TEGRA234_CLK_SYNC_DSPK2			144U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S1 */
#define TEGRA234_CLK_SYNC_I2S1			145U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S2 */
#define TEGRA234_CLK_SYNC_I2S2			146U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S3 */
#define TEGRA234_CLK_SYNC_I2S3			147U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S4 */
#define TEGRA234_CLK_SYNC_I2S4			148U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S5 */
#define TEGRA234_CLK_SYNC_I2S5			149U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S6 */
#define TEGRA234_CLK_SYNC_I2S6			150U
/** @brief controls MPHY_FORCE_LS_MODE upon enable & disable */
#define TEGRA234_CLK_MPHY_FORCE_LS_MODE		151U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_TACH0 */
#define TEGRA234_CLK_TACH0			152U
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_TSEC */
#define TEGRA234_CLK_TSEC			153U
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PKA */
#define TEGRA234_CLK_TSEC_PKA			154U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTA */
#define TEGRA234_CLK_UARTA			155U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTB */
#define TEGRA234_CLK_UARTB			156U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTC */
#define TEGRA234_CLK_UARTC			157U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTD */
#define TEGRA234_CLK_UARTD			158U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTE */
#define TEGRA234_CLK_UARTE			159U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTF */
#define TEGRA234_CLK_UARTF			160U
/** @brief output of gate CLK_ENB_PEX1_CORE_6 */
#define TEGRA234_CLK_PEX1_C6_CORE		161U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UART_FST_MIPI_CAL */
#define TEGRA234_CLK_UART_FST_MIPI_CAL		162U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UFSDEV_REF */
#define TEGRA234_CLK_UFSDEV_REF			163U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UFSHC_CG_SYS */
#define TEGRA234_CLK_UFSHC			164U
/** @brief output of gate CLK_ENB_USB2_TRK */
#define TEGRA234_CLK_USB2_TRK			165U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_VI */
#define TEGRA234_CLK_VI				166U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_VIC */
#define TEGRA234_CLK_VIC			167U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_CSITE switch divider output */
#define TEGRA234_CLK_CSITE			168U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_IST switch divider output */
#define TEGRA234_CLK_IST			169U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_IST_JTAG_REG_CLK_SEL */
#define TEGRA234_CLK_JTAG_INTFC_PRE_CG		170U
/** @brief output of gate CLK_ENB_PEX2_CORE_7 */
#define TEGRA234_CLK_PEX2_C7_CORE		171U
/** @brief output of gate CLK_ENB_PEX2_CORE_8 */
#define TEGRA234_CLK_PEX2_C8_CORE		172U
/** @brief output of gate CLK_ENB_PEX2_CORE_9 */
#define TEGRA234_CLK_PEX2_C9_CORE		173U
/** @brief dla0_falcon_clk */
#define TEGRA234_CLK_DLA0_FALCON		174U
/** @brief dla0_core_clk */
#define TEGRA234_CLK_DLA0_CORE			175U
/** @brief dla1_falcon_clk */
#define TEGRA234_CLK_DLA1_FALCON		176U
/** @brief dla1_core_clk */
#define TEGRA234_CLK_DLA1_CORE			177U
/** @brief Output of mux controlled by disp_2clk_sor0_clk_safe signal (sor0_clk) */
#define TEGRA234_CLK_SOR0			178U
/** @brief Output of mux controlled by disp_2clk_sor1_clk_safe signal (sor1_clk) */
#define TEGRA234_CLK_SOR1			179U
/** @brief DP macro feedback clock (same as LINKA_SYM CLKOUT) */
#define TEGRA234_CLK_SOR_PAD_INPUT		180U
/** @brief Output of mux controlled by disp_2clk_h0_dsi_sel signal in sf0_clk path */
#define TEGRA234_CLK_PRE_SF0			181U
/** @brief Output of mux controlled by disp_2clk_sf0_clk_safe signal (sf0_clk) */
#define TEGRA234_CLK_SF0			182U
/** @brief Output of mux controlled by disp_2clk_sf1_clk_safe signal (sf1_clk) */
#define TEGRA234_CLK_SF1			183U
/** @brief CLKOUT_AB output from DSI BRICK A (dsi_clkout_ab) */
#define TEGRA234_CLK_DSI_PAD_INPUT		184U
/** @brief output of gate CLK_ENB_PEX2_CORE_10 */
#define TEGRA234_CLK_PEX2_C10_CORE		187U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_UARTI switch divider output (uarti_r_clk) */
#define TEGRA234_CLK_UARTI			188U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_UARTJ switch divider output (uartj_r_clk) */
#define TEGRA234_CLK_UARTJ			189U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_UARTH switch divider output */
#define TEGRA234_CLK_UARTH			190U
/** @brief ungated version of fuse clk */
#define TEGRA234_CLK_FUSE_SERIAL		191U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_QSPI0 switch divider output (qspi0_2x_pm_clk) */
#define TEGRA234_CLK_QSPI0_2X_PM		192U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_QSPI1 switch divider output (qspi1_2x_pm_clk) */
#define TEGRA234_CLK_QSPI1_2X_PM		193U
/** @brief output of the divider QSPI_CLK_DIV2_SEL in CLK_RST_CONTROLLER_CLK_SOURCE_QSPI0 (qspi0_pm_clk) */
#define TEGRA234_CLK_QSPI0_PM			194U
/** @brief output of the divider QSPI_CLK_DIV2_SEL in CLK_RST_CONTROLLER_CLK_SOURCE_QSPI1 (qspi1_pm_clk) */
#define TEGRA234_CLK_QSPI1_PM			195U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_VI_CONST switch divider output */
#define TEGRA234_CLK_VI_CONST			196U
/** @brief NAFLL clock source for BPMP */
#define TEGRA234_CLK_NAFLL_BPMP			197U
/** @brief NAFLL clock source for SCE */
#define TEGRA234_CLK_NAFLL_SCE			198U
/** @brief NAFLL clock source for NVDEC */
#define TEGRA234_CLK_NAFLL_NVDEC		199U
/** @brief NAFLL clock source for NVJPG */
#define TEGRA234_CLK_NAFLL_NVJPG		200U
/** @brief NAFLL clock source for TSEC */
#define TEGRA234_CLK_NAFLL_TSEC			201U
/** @brief NAFLL clock source for VI */
#define TEGRA234_CLK_NAFLL_VI			203U
/** @brief NAFLL clock source for SE */
#define TEGRA234_CLK_NAFLL_SE			204U
/** @brief NAFLL clock source for NVENC */
#define TEGRA234_CLK_NAFLL_NVENC		205U
/** @brief NAFLL clock source for ISP */
#define TEGRA234_CLK_NAFLL_ISP			206U
/** @brief NAFLL clock source for VIC */
#define TEGRA234_CLK_NAFLL_VIC			207U
/** @brief NAFLL clock source for AXICBB */
#define TEGRA234_CLK_NAFLL_AXICBB		209U
/** @brief NAFLL clock source for NVJPG1 */
#define TEGRA234_CLK_NAFLL_NVJPG1		210U
/** @brief NAFLL clock source for PVA core */
#define TEGRA234_CLK_NAFLL_PVA0_CORE		211U
/** @brief NAFLL clock source for PVA VPS */
#define TEGRA234_CLK_NAFLL_PVA0_VPS		212U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_DBGAPB_0 switch divider output (dbgapb_clk) */
#define TEGRA234_CLK_DBGAPB			213U
/** @brief NAFLL clock source for RCE */
#define TEGRA234_CLK_NAFLL_RCE			214U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_LA switch divider output (la_r_clk) */
#define TEGRA234_CLK_LA				215U
/** @brief output of the divider CLK_RST_CONTROLLER_PLLP_OUTD */
#define TEGRA234_CLK_PLLP_OUT_JTAG		216U
/** @brief AXI_CBB branch sharing gate control with SDMMC4 */
#define TEGRA234_CLK_SDMMC4_AXICIF		217U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_SDMMC_LEGACY_TM switch divider output */
#define TEGRA234_CLK_SDMMC_LEGACY_TM		219U
/** @brief output of gate CLK_ENB_PEX0_CORE_0 */
#define TEGRA234_CLK_PEX0_C0_CORE		220U
/** @brief output of gate CLK_ENB_PEX0_CORE_1 */
#define TEGRA234_CLK_PEX0_C1_CORE		221U
/** @brief output of gate CLK_ENB_PEX0_CORE_2 */
#define TEGRA234_CLK_PEX0_C2_CORE		222U
/** @brief output of gate CLK_ENB_PEX0_CORE_3 */
#define TEGRA234_CLK_PEX0_C3_CORE		223U
/** @brief output of gate CLK_ENB_PEX0_CORE_4 */
#define TEGRA234_CLK_PEX0_C4_CORE		224U
/** @brief output of gate CLK_ENB_PEX1_CORE_5 */
#define TEGRA234_CLK_PEX1_C5_CORE		225U
/** @brief Monitored branch of PEX0_C0_CORE clock */
#define TEGRA234_CLK_PEX0_C0_CORE_M		229U
/** @brief Monitored branch of PEX0_C1_CORE clock */
#define TEGRA234_CLK_PEX0_C1_CORE_M		230U
/** @brief Monitored branch of PEX0_C2_CORE clock */
#define TEGRA234_CLK_PEX0_C2_CORE_M		231U
/** @brief Monitored branch of PEX0_C3_CORE clock */
#define TEGRA234_CLK_PEX0_C3_CORE_M		232U
/** @brief Monitored branch of PEX0_C4_CORE clock */
#define TEGRA234_CLK_PEX0_C4_CORE_M		233U
/** @brief Monitored branch of PEX1_C5_CORE clock */
#define TEGRA234_CLK_PEX1_C5_CORE_M		234U
/** @brief Monitored branch of PEX1_C6_CORE clock */
#define TEGRA234_CLK_PEX1_C6_CORE_M		235U
/** @brief output of GPU GPC1 clkGen (in 1x mode same rate as GPC1 MUX2 out) */
#define TEGRA234_CLK_GPC1CLK			236U
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLC4_BASE */
#define TEGRA234_CLK_PLLC4			237U
/** @brief PLLC4 VCO followed by DIV3 path */
#define TEGRA234_CLK_PLLC4_OUT1			239U
/** @brief PLLC4 VCO followed by DIV5 path */
#define TEGRA234_CLK_PLLC4_OUT2			240U
/** @brief output of the mux controlled by PLLC4_CLK_SEL */
#define TEGRA234_CLK_PLLC4_MUXED		241U
/** @brief PLLC4 VCO followed by DIV2 path */
#define TEGRA234_CLK_PLLC4_VCO_DIV2		242U
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLNVHS_BASE */
#define TEGRA234_CLK_PLLNVHS			243U
/** @brief Monitored branch of PEX2_C7_CORE clock */
#define TEGRA234_CLK_PEX2_C7_CORE_M		244U
/** @brief Monitored branch of PEX2_C8_CORE clock */
#define TEGRA234_CLK_PEX2_C8_CORE_M		245U
/** @brief Monitored branch of PEX2_C9_CORE clock */
#define TEGRA234_CLK_PEX2_C9_CORE_M		246U
/** @brief Monitored branch of PEX2_C10_CORE clock */
#define TEGRA234_CLK_PEX2_C10_CORE_M		247U
/** @brief RX clock recovered from MGBE0 lane input */
#define TEGRA234_CLK_MGBE0_RX_INPUT		248U
/** @brief RX clock recovered from MGBE1 lane input */
#define TEGRA234_CLK_MGBE1_RX_INPUT		249U
/** @brief RX clock recovered from MGBE2 lane input */
#define TEGRA234_CLK_MGBE2_RX_INPUT		250U
/** @brief RX clock recovered from MGBE3 lane input */
#define TEGRA234_CLK_MGBE3_RX_INPUT		251U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_PEX_SATA_USB_RX_BYP switch divider output */
#define TEGRA234_CLK_PEX_SATA_USB_RX_BYP	254U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_PEX_USB_PAD_PLL0_MGMT switch divider output */
#define TEGRA234_CLK_PEX_USB_PAD_PLL0_MGMT	255U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_PEX_USB_PAD_PLL1_MGMT switch divider output */
#define TEGRA234_CLK_PEX_USB_PAD_PLL1_MGMT	256U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_PEX_USB_PAD_PLL2_MGMT switch divider output */
#define TEGRA234_CLK_PEX_USB_PAD_PLL2_MGMT	257U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_PEX_USB_PAD_PLL3_MGMT switch divider output */
#define TEGRA234_CLK_PEX_USB_PAD_PLL3_MGMT	258U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_NVHS_RX_BYP switch divider output */
#define TEGRA234_CLK_NVHS_RX_BYP_REF		263U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_NVHS_PLL0_MGMT switch divider output */
#define TEGRA234_CLK_NVHS_PLL0_MGMT		264U
/** @brief xusb_core_dev_clk */
#define TEGRA234_CLK_XUSB_CORE_DEV		265U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_XUSB_CORE_HOST switch divider output  */
#define TEGRA234_CLK_XUSB_CORE_MUX		266U
/** @brief xusb_core_host_clk */
#define TEGRA234_CLK_XUSB_CORE_HOST		267U
/** @brief xusb_core_superspeed_clk */
#define TEGRA234_CLK_XUSB_CORE_SS		268U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_XUSB_FALCON switch divider output */
#define TEGRA234_CLK_XUSB_FALCON		269U
/** @brief xusb_falcon_host_clk */
#define TEGRA234_CLK_XUSB_FALCON_HOST		270U
/** @brief xusb_falcon_superspeed_clk */
#define TEGRA234_CLK_XUSB_FALCON_SS		271U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_XUSB_FS switch divider output */
#define TEGRA234_CLK_XUSB_FS			272U
/** @brief xusb_fs_host_clk */
#define TEGRA234_CLK_XUSB_FS_HOST		273U
/** @brief xusb_fs_dev_clk */
#define TEGRA234_CLK_XUSB_FS_DEV		274U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_XUSB_SS switch divider output */
#define TEGRA234_CLK_XUSB_SS			275U
/** @brief xusb_ss_dev_clk */
#define TEGRA234_CLK_XUSB_SS_DEV		276U
/** @brief xusb_ss_superspeed_clk */
#define TEGRA234_CLK_XUSB_SS_SUPERSPEED		277U
/** @brief NAFLL clock source for CPU cluster 0 */
#define TEGRA234_CLK_NAFLL_CLUSTER0		280U /* TODO: remove */
#define TEGRA234_CLK_NAFLL_CLUSTER0_CORE	280U
/** @brief NAFLL clock source for CPU cluster 1 */
#define TEGRA234_CLK_NAFLL_CLUSTER1		281U /* TODO: remove */
#define TEGRA234_CLK_NAFLL_CLUSTER1_CORE	281U
/** @brief NAFLL clock source for CPU cluster 2 */
#define TEGRA234_CLK_NAFLL_CLUSTER2		282U /* TODO: remove */
#define TEGRA234_CLK_NAFLL_CLUSTER2_CORE	282U
/** @brief CLK_RST_CONTROLLER_CAN1_CORE_RATE divider output */
#define TEGRA234_CLK_CAN1_CORE			284U
/** @brief CLK_RST_CONTROLLER_CAN2_CORE_RATE divider outputt */
#define TEGRA234_CLK_CAN2_CORE			285U
/** @brief CLK_RST_CONTROLLER_PLLA1_OUT1 switch divider output */
#define TEGRA234_CLK_PLLA1_OUT1			286U
/** @brief NVHS PLL hardware power sequencer (overrides 'manual' programming of PLL) */
#define TEGRA234_CLK_PLLNVHS_HPS		287U
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLREFE_BASE */
#define TEGRA234_CLK_PLLREFE_VCOOUT		288U
/** @brief 32K input clock provided by PMIC */
#define TEGRA234_CLK_CLK_32K			289U
/** @brief Fixed 48MHz clock divided down from utmipll */
#define TEGRA234_CLK_UTMIPLL_CLKOUT48		291U
/** @brief Fixed 480MHz clock divided down from utmipll */
#define TEGRA234_CLK_UTMIPLL_CLKOUT480		292U
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLNVCSI_BASE  */
#define TEGRA234_CLK_PLLNVCSI			294U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_PVA0_CPU_AXI switch divider output */
#define TEGRA234_CLK_PVA0_CPU_AXI		295U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_PVA0_VPS switch divider output */
#define TEGRA234_CLK_PVA0_VPS			297U
/** @brief DLA0_CORE_NAFLL */
#define TEGRA234_CLK_NAFLL_DLA0_CORE		299U
/** @brief DLA0_FALCON_NAFLL */
#define TEGRA234_CLK_NAFLL_DLA0_FALCON		300U
/** @brief DLA1_CORE_NAFLL */
#define TEGRA234_CLK_NAFLL_DLA1_CORE		301U
/** @brief DLA1_FALCON_NAFLL */
#define TEGRA234_CLK_NAFLL_DLA1_FALCON		302U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AON_UART_FST_MIPI_CAL */
#define TEGRA234_CLK_AON_UART_FST_MIPI_CAL	303U
/** @brief GPU system clock */
#define TEGRA234_CLK_GPUSYS			304U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C5 */
#define TEGRA234_CLK_I2C5			305U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_SE switch divider free running clk */
#define TEGRA234_CLK_FR_SE			306U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_BPMP_CPU_NIC switch divider output */
#define TEGRA234_CLK_BPMP_CPU_NIC		307U
/** @brief output of gate CLK_ENB_BPMP_CPU */
#define TEGRA234_CLK_BPMP_CPU			308U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_TSC switch divider output */
#define TEGRA234_CLK_TSC			309U
/** @brief output of mem pll A sync mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_EMC */
#define TEGRA234_CLK_EMCSA_MPLL			310U
/** @brief output of mem pll B sync mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_EMCSB */
#define TEGRA234_CLK_EMCSB_MPLL			311U
/** @brief output of mem pll C sync mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_EMCSC */
#define TEGRA234_CLK_EMCSC_MPLL			312U
/** @brief output of mem pll D sync mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_EMCSD */
#define TEGRA234_CLK_EMCSD_MPLL			313U
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLC_BASE */
#define TEGRA234_CLK_PLLC			314U
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLC2_BASE */
#define TEGRA234_CLK_PLLC2			315U
/** @brief CLK_RST_CONTROLLER_TSC_HS_SUPER_CLK_DIVIDER skip divider output */
#define TEGRA234_CLK_TSC_REF			317U
/** @brief Dummy clock to ensure minimum SoC voltage for fuse burning */
#define TEGRA234_CLK_FUSE_BURN			318U
/** @brief GBE PLL */
#define TEGRA234_CLK_PLLGBE			319U
/** @brief GBE PLL hardware power sequencer */
#define TEGRA234_CLK_PLLGBE_HPS			320U
/** @brief output of EMC CDB side A fixed (DIV4)  divider */
#define TEGRA234_CLK_EMCSA_EMC			321U
/** @brief output of EMC CDB side B fixed (DIV4)  divider */
#define TEGRA234_CLK_EMCSB_EMC			322U
/** @brief output of EMC CDB side C fixed (DIV4)  divider */
#define TEGRA234_CLK_EMCSC_EMC			323U
/** @brief output of EMC CDB side D fixed (DIV4)  divider */
#define TEGRA234_CLK_EMCSD_EMC			324U
/** @brief PLLE hardware power sequencer (overrides 'manual' programming of PLL) */
#define TEGRA234_CLK_PLLE_HPS			326U
/** @brief CLK_ENB_PLLREFE_OUT gate output */
#define TEGRA234_CLK_PLLREFE_VCOOUT_GATED	327U
/** @brief TEGRA234_CLK_SOR_SAFE clk source (PLLP_OUT0 divided by 17) */
#define TEGRA234_CLK_PLLP_DIV17			328U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_SOC_THERM switch divider output */
#define TEGRA234_CLK_SOC_THERM			329U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_TSENSE switch divider output */
#define TEGRA234_CLK_TSENSE			330U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_SEU1 switch divider free running clk */
#define TEGRA234_CLK_FR_SEU1			331U
/** @brief NAFLL clock source for OFA */
#define TEGRA234_CLK_NAFLL_OFA			333U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_OFA switch divider output */
#define TEGRA234_CLK_OFA			334U
/** @brief NAFLL clock source for SEU1 */
#define TEGRA234_CLK_NAFLL_SEU1			335U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_SEU1 switch divider gated output */
#define TEGRA234_CLK_SEU1			336U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SPI4 */
#define TEGRA234_CLK_SPI4			337U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SPI5 */
#define TEGRA234_CLK_SPI5			338U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DCE_CPU_NIC */
#define TEGRA234_CLK_DCE_CPU_NIC		339U
/** @brief output of divider CLK_RST_CONTROLLER_DCE_NIC_RATE */
#define TEGRA234_CLK_DCE_NIC			340U
/** @brief NAFLL clock source for DCE */
#define TEGRA234_CLK_NAFLL_DCE			341U
/** @brief Monitored branch of MPHY_L0_RX_ANA clock */
#define TEGRA234_CLK_MPHY_L0_RX_ANA_M		342U
/** @brief Monitored branch of MPHY_L1_RX_ANA clock */
#define TEGRA234_CLK_MPHY_L1_RX_ANA_M		343U
/** @brief ungated version of TX symbol clock after fixed 1/2 divider */
#define TEGRA234_CLK_MPHY_L0_TX_PRE_SYMB	344U
/** @brief output of divider CLK_RST_CONTROLLER_CLK_SOURCE_MPHY_L0_TX_LS_SYMB */
#define TEGRA234_CLK_MPHY_L0_TX_LS_SYMB_DIV	345U
/** @brief output of gate CLK_ENB_MPHY_L0_TX_2X_SYMB */
#define TEGRA234_CLK_MPHY_L0_TX_2X_SYMB		346U
/** @brief output of SW_MPHY_L0_TX_HS_SYMB divider in CLK_RST_CONTROLLER_MPHY_L0_TX_CLK_CTRL_0 */
#define TEGRA234_CLK_MPHY_L0_TX_HS_SYMB_DIV	347U
/** @brief output of SW_MPHY_L0_TX_LS_3XBIT divider in CLK_RST_CONTROLLER_MPHY_L0_TX_CLK_CTRL_0 */
#define TEGRA234_CLK_MPHY_L0_TX_LS_3XBIT_DIV	348U
/** @brief LS/HS divider mux SW_MPHY_L0_TX_LS_HS_SEL in CLK_RST_CONTROLLER_MPHY_L0_TX_CLK_CTRL_0 */
#define TEGRA234_CLK_MPHY_L0_TX_MUX_SYMB_DIV	349U
/** @brief Monitored branch of MPHY_L0_TX_SYMB clock */
#define TEGRA234_CLK_MPHY_L0_TX_SYMB_M		350U
/** @brief output of divider CLK_RST_CONTROLLER_CLK_SOURCE_MPHY_L0_RX_LS_SYMB */
#define TEGRA234_CLK_MPHY_L0_RX_LS_SYMB_DIV	351U
/** @brief output of SW_MPHY_L0_RX_HS_SYMB divider in CLK_RST_CONTROLLER_MPHY_L0_RX_CLK_CTRL_0 */
#define TEGRA234_CLK_MPHY_L0_RX_HS_SYMB_DIV	352U
/** @brief output of SW_MPHY_L0_RX_LS_BIT divider in  CLK_RST_CONTROLLER_MPHY_L0_RX_CLK_CTRL_0 */
#define TEGRA234_CLK_MPHY_L0_RX_LS_BIT_DIV	353U
/** @brief LS/HS divider mux SW_MPHY_L0_RX_LS_HS_SEL in CLK_RST_CONTROLLER_MPHY_L0_RX_CLK_CTRL_0 */
#define TEGRA234_CLK_MPHY_L0_RX_MUX_SYMB_DIV	354U
/** @brief Monitored branch of MPHY_L0_RX_SYMB clock */
#define TEGRA234_CLK_MPHY_L0_RX_SYMB_M		355U
/** @brief Monitored branch of MBGE0 RX input clock */
#define TEGRA234_CLK_MGBE0_RX_INPUT_M		357U
/** @brief Monitored branch of MBGE1 RX input clock */
#define TEGRA234_CLK_MGBE1_RX_INPUT_M		358U
/** @brief Monitored branch of MBGE2 RX input clock */
#define TEGRA234_CLK_MGBE2_RX_INPUT_M		359U
/** @brief Monitored branch of MBGE3 RX input clock */
#define TEGRA234_CLK_MGBE3_RX_INPUT_M		360U
/** @brief Monitored branch of MGBE0 RX PCS mux output */
#define TEGRA234_CLK_MGBE0_RX_PCS_M		361U
/** @brief Monitored branch of MGBE1 RX PCS mux output */
#define TEGRA234_CLK_MGBE1_RX_PCS_M		362U
/** @brief Monitored branch of MGBE2 RX PCS mux output */
#define TEGRA234_CLK_MGBE2_RX_PCS_M		363U
/** @brief Monitored branch of MGBE3 RX PCS mux output */
#define TEGRA234_CLK_MGBE3_RX_PCS_M		364U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_TACH1 */
#define TEGRA234_CLK_TACH1			365U
/** @brief GBE_UPHY_MGBES_APP_CLK switch divider gated output */
#define TEGRA234_CLK_MGBES_APP			366U
/** @brief Logical clk for setting GBE UPHY PLL2 TX_REF rate */
#define TEGRA234_CLK_UPHY_GBE_PLL2_TX_REF	367U
/** @brief Logical clk for setting GBE UPHY PLL2 XDIG rate */
#define TEGRA234_CLK_UPHY_GBE_PLL2_XDIG		368U
/** @brief RX PCS clock recovered from MGBE0 lane input */
#define TEGRA234_CLK_MGBE0_RX_PCS_INPUT		369U
/** @brief RX PCS clock recovered from MGBE1 lane input */
#define TEGRA234_CLK_MGBE1_RX_PCS_INPUT		370U
/** @brief RX PCS clock recovered from MGBE2 lane input */
#define TEGRA234_CLK_MGBE2_RX_PCS_INPUT		371U
/** @brief RX PCS clock recovered from MGBE3 lane input */
#define TEGRA234_CLK_MGBE3_RX_PCS_INPUT		372U
/** @brief output of mux controlled by GBE_UPHY_MGBE0_RX_PCS_CLK_SRC_SEL */
#define TEGRA234_CLK_MGBE0_RX_PCS		373U
/** @brief GBE_UPHY_MGBE0_TX_CLK divider gated output */
#define TEGRA234_CLK_MGBE0_TX			374U
/** @brief GBE_UPHY_MGBE0_TX_PCS_CLK divider gated output */
#define TEGRA234_CLK_MGBE0_TX_PCS		375U
/** @brief GBE_UPHY_MGBE0_MAC_CLK divider output */
#define TEGRA234_CLK_MGBE0_MAC_DIVIDER		376U
/** @brief GBE_UPHY_MGBE0_MAC_CLK gate output */
#define TEGRA234_CLK_MGBE0_MAC			377U
/** @brief GBE_UPHY_MGBE0_MACSEC_CLK gate output */
#define TEGRA234_CLK_MGBE0_MACSEC		378U
/** @brief GBE_UPHY_MGBE0_EEE_PCS_CLK gate output */
#define TEGRA234_CLK_MGBE0_EEE_PCS		379U
/** @brief GBE_UPHY_MGBE0_APP_CLK gate output */
#define TEGRA234_CLK_MGBE0_APP			380U
/** @brief GBE_UPHY_MGBE0_PTP_REF_CLK divider gated output */
#define TEGRA234_CLK_MGBE0_PTP_REF		381U
/** @brief output of mux controlled by GBE_UPHY_MGBE1_RX_PCS_CLK_SRC_SEL */
#define TEGRA234_CLK_MGBE1_RX_PCS		382U
/** @brief GBE_UPHY_MGBE1_TX_CLK divider gated output */
#define TEGRA234_CLK_MGBE1_TX			383U
/** @brief GBE_UPHY_MGBE1_TX_PCS_CLK divider gated output */
#define TEGRA234_CLK_MGBE1_TX_PCS		384U
/** @brief GBE_UPHY_MGBE1_MAC_CLK divider output */
#define TEGRA234_CLK_MGBE1_MAC_DIVIDER		385U
/** @brief GBE_UPHY_MGBE1_MAC_CLK gate output */
#define TEGRA234_CLK_MGBE1_MAC			386U
/** @brief GBE_UPHY_MGBE1_MACSEC_CLK gate output */
#define TEGRA234_CLK_MGBE1_MACSEC		387U
/** @brief GBE_UPHY_MGBE1_EEE_PCS_CLK gate output */
#define TEGRA234_CLK_MGBE1_EEE_PCS		388U
/** @brief GBE_UPHY_MGBE1_APP_CLK gate output */
#define TEGRA234_CLK_MGBE1_APP			389U
/** @brief GBE_UPHY_MGBE1_PTP_REF_CLK divider gated output */
#define TEGRA234_CLK_MGBE1_PTP_REF		390U
/** @brief output of mux controlled by GBE_UPHY_MGBE2_RX_PCS_CLK_SRC_SEL */
#define TEGRA234_CLK_MGBE2_RX_PCS		391U
/** @brief GBE_UPHY_MGBE2_TX_CLK divider gated output */
#define TEGRA234_CLK_MGBE2_TX			392U
/** @brief GBE_UPHY_MGBE2_TX_PCS_CLK divider gated output */
#define TEGRA234_CLK_MGBE2_TX_PCS		393U
/** @brief GBE_UPHY_MGBE2_MAC_CLK divider output */
#define TEGRA234_CLK_MGBE2_MAC_DIVIDER		394U
/** @brief GBE_UPHY_MGBE2_MAC_CLK gate output */
#define TEGRA234_CLK_MGBE2_MAC			395U
/** @brief GBE_UPHY_MGBE2_MACSEC_CLK gate output */
#define TEGRA234_CLK_MGBE2_MACSEC		396U
/** @brief GBE_UPHY_MGBE2_EEE_PCS_CLK gate output */
#define TEGRA234_CLK_MGBE2_EEE_PCS		397U
/** @brief GBE_UPHY_MGBE2_APP_CLK gate output */
#define TEGRA234_CLK_MGBE2_APP			398U
/** @brief GBE_UPHY_MGBE2_PTP_REF_CLK divider gated output */
#define TEGRA234_CLK_MGBE2_PTP_REF		399U
/** @brief output of mux controlled by GBE_UPHY_MGBE3_RX_PCS_CLK_SRC_SEL */
#define TEGRA234_CLK_MGBE3_RX_PCS		400U
/** @brief GBE_UPHY_MGBE3_TX_CLK divider gated output */
#define TEGRA234_CLK_MGBE3_TX			401U
/** @brief GBE_UPHY_MGBE3_TX_PCS_CLK divider gated output */
#define TEGRA234_CLK_MGBE3_TX_PCS		402U
/** @brief GBE_UPHY_MGBE3_MAC_CLK divider output */
#define TEGRA234_CLK_MGBE3_MAC_DIVIDER		403U
/** @brief GBE_UPHY_MGBE3_MAC_CLK gate output */
#define TEGRA234_CLK_MGBE3_MAC			404U
/** @brief GBE_UPHY_MGBE3_MACSEC_CLK gate output */
#define TEGRA234_CLK_MGBE3_MACSEC		405U
/** @brief GBE_UPHY_MGBE3_EEE_PCS_CLK gate output */
#define TEGRA234_CLK_MGBE3_EEE_PCS		406U
/** @brief GBE_UPHY_MGBE3_APP_CLK gate output */
#define TEGRA234_CLK_MGBE3_APP			407U
/** @brief GBE_UPHY_MGBE3_PTP_REF_CLK divider gated output */
#define TEGRA234_CLK_MGBE3_PTP_REF		408U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_GBE_RX_BYP switch divider output */
#define TEGRA234_CLK_GBE_RX_BYP_REF		409U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_GBE_PLL0_MGMT switch divider output */
#define TEGRA234_CLK_GBE_PLL0_MGMT		410U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_GBE_PLL1_MGMT switch divider output */
#define TEGRA234_CLK_GBE_PLL1_MGMT		411U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_GBE_PLL2_MGMT switch divider output */
#define TEGRA234_CLK_GBE_PLL2_MGMT		412U
/** @brief output of gate CLK_ENB_EQOS_MACSEC_RX */
#define TEGRA234_CLK_EQOS_MACSEC_RX		413U
/** @brief output of gate CLK_ENB_EQOS_MACSEC_TX */
#define TEGRA234_CLK_EQOS_MACSEC_TX		414U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_EQOS_TX_CLK divider ungated output */
#define TEGRA234_CLK_EQOS_TX_DIVIDER		415U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_NVHS_PLL1_MGMT switch divider output */
#define TEGRA234_CLK_NVHS_PLL1_MGMT		416U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_EMCHUB mux output */
#define TEGRA234_CLK_EMCHUB			417U
/** @brief clock recovered from I2S7 input */
#define TEGRA234_CLK_I2S7_SYNC_INPUT		418U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S7 */
#define TEGRA234_CLK_SYNC_I2S7			419U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S7 */
#define TEGRA234_CLK_I2S7			420U
/** @brief Monitored output of I2S7 pad macro mux */
#define TEGRA234_CLK_I2S7_PAD_M			421U
/** @brief clock recovered from I2S8 input */
#define TEGRA234_CLK_I2S8_SYNC_INPUT		422U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S8 */
#define TEGRA234_CLK_SYNC_I2S8			423U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S8 */
#define TEGRA234_CLK_I2S8			424U
/** @brief Monitored output of I2S8 pad macro mux */
#define TEGRA234_CLK_I2S8_PAD_M			425U
/** @brief NAFLL clock source for GPU GPC0 */
#define TEGRA234_CLK_NAFLL_GPC0			426U
/** @brief NAFLL clock source for GPU GPC1 */
#define TEGRA234_CLK_NAFLL_GPC1			427U
/** @brief NAFLL clock source for GPU SYSCLK */
#define TEGRA234_CLK_NAFLL_GPUSYS		428U
/** @brief NAFLL clock source for CPU cluster 0 DSUCLK */
#define TEGRA234_CLK_NAFLL_DSU0			429U /* TODO: remove */
#define TEGRA234_CLK_NAFLL_CLUSTER0_DSU		429U
/** @brief NAFLL clock source for CPU cluster 1 DSUCLK */
#define TEGRA234_CLK_NAFLL_DSU1			430U /* TODO: remove */
#define TEGRA234_CLK_NAFLL_CLUSTER1_DSU		430U
/** @brief NAFLL clock source for CPU cluster 2 DSUCLK */
#define TEGRA234_CLK_NAFLL_DSU2			431U /* TODO: remove */
#define TEGRA234_CLK_NAFLL_CLUSTER2_DSU		431U
/** @brief output of gate CLK_ENB_SCE_CPU */
#define TEGRA234_CLK_SCE_CPU			432U
/** @brief output of gate CLK_ENB_RCE_CPU */
#define TEGRA234_CLK_RCE_CPU			433U
/** @brief output of gate CLK_ENB_DCE_CPU */
#define TEGRA234_CLK_DCE_CPU			434U
/** @brief DSIPLL VCO output */
#define TEGRA234_CLK_DSIPLL_VCO			435U
/** @brief DSIPLL SYNC_CLKOUTP/N differential output */
#define TEGRA234_CLK_DSIPLL_CLKOUTPN		436U
/** @brief DSIPLL SYNC_CLKOUTA output */
#define TEGRA234_CLK_DSIPLL_CLKOUTA		437U
/** @brief SPPLL0 VCO output */
#define TEGRA234_CLK_SPPLL0_VCO			438U
/** @brief SPPLL0 SYNC_CLKOUTP/N differential output */
#define TEGRA234_CLK_SPPLL0_CLKOUTPN		439U
/** @brief SPPLL0 SYNC_CLKOUTA output */
#define TEGRA234_CLK_SPPLL0_CLKOUTA		440U
/** @brief SPPLL0 SYNC_CLKOUTB output */
#define TEGRA234_CLK_SPPLL0_CLKOUTB		441U
/** @brief SPPLL0 CLKOUT_DIVBY10 output */
#define TEGRA234_CLK_SPPLL0_DIV10		442U
/** @brief SPPLL0 CLKOUT_DIVBY25 output */
#define TEGRA234_CLK_SPPLL0_DIV25		443U
/** @brief SPPLL0 CLKOUT_DIVBY27P/N differential output */
#define TEGRA234_CLK_SPPLL0_DIV27PN		444U
/** @brief SPPLL1 VCO output */
#define TEGRA234_CLK_SPPLL1_VCO			445U
/** @brief SPPLL1 SYNC_CLKOUTP/N differential output */
#define TEGRA234_CLK_SPPLL1_CLKOUTPN		446U
/** @brief SPPLL1 CLKOUT_DIVBY27P/N differential output */
#define TEGRA234_CLK_SPPLL1_DIV27PN		447U
/** @brief VPLL0 reference clock */
#define TEGRA234_CLK_VPLL0_REF			448U
/** @brief VPLL0 */
#define TEGRA234_CLK_VPLL0			449U
/** @brief VPLL1 */
#define TEGRA234_CLK_VPLL1			450U
/** @brief NVDISPLAY_P0_CLK reference select */
#define TEGRA234_CLK_NVDISPLAY_P0_REF		451U
/** @brief RG0_PCLK */
#define TEGRA234_CLK_RG0			452U
/** @brief RG1_PCLK */
#define TEGRA234_CLK_RG1			453U
/** @brief DISPPLL output */
#define TEGRA234_CLK_DISPPLL			454U
/** @brief DISPHUBPLL output */
#define TEGRA234_CLK_DISPHUBPLL			455U
/** @brief CLK_RST_CONTROLLER_DSI_LP_SWITCH_DIVIDER switch divider output (dsi_lp_clk) */
#define TEGRA234_CLK_DSI_LP			456U
/** @brief CLK_RST_CONTROLLER_AZA2XBITCLK_OUT_SWITCH_DIVIDER switch divider output (aza_2xbitclk) */
#define TEGRA234_CLK_AZA_2XBIT			457U
/** @brief aza_2xbitclk / 2 (aza_bitclk) */
#define TEGRA234_CLK_AZA_BIT			458U
/** @brief SWITCH_DSI_CORE_PIXEL_MISC_DSI_CORE_CLK_SRC switch output (dsi_core_clk) */
#define TEGRA234_CLK_DSI_CORE			459U
/** @brief Output of mux controlled by pkt_wr_fifo_signal from dsi (dsi_pixel_clk) */
#define TEGRA234_CLK_DSI_PIXEL			460U
/** @brief Output of mux controlled by disp_2clk_sor0_dp_sel (pre_sor0_clk) */
#define TEGRA234_CLK_PRE_SOR0			461U
/** @brief Output of mux controlled by disp_2clk_sor1_dp_sel (pre_sor1_clk) */
#define TEGRA234_CLK_PRE_SOR1			462U
/** @brief CLK_RST_CONTROLLER_LINK_REFCLK_CFG__0 output */
#define TEGRA234_CLK_DP_LINK_REF		463U
/** @brief Link clock input from DP macro brick PLL */
#define TEGRA234_CLK_SOR_LINKA_INPUT		464U
/** @brief SOR AFIFO clock outut */
#define TEGRA234_CLK_SOR_LINKA_AFIFO		465U
/** @brief Monitored branch of linka_afifo_clk */
#define TEGRA234_CLK_SOR_LINKA_AFIFO_M		466U
/** @brief Monitored branch of rg0_pclk */
#define TEGRA234_CLK_RG0_M			467U
/** @brief Monitored branch of rg1_pclk */
#define TEGRA234_CLK_RG1_M			468U
/** @brief Monitored branch of sor0_clk */
#define TEGRA234_CLK_SOR0_M			469U
/** @brief Monitored branch of sor1_clk */
#define TEGRA234_CLK_SOR1_M			470U
/** @brief EMC PLLHUB output */
#define TEGRA234_CLK_PLLHUB			471U
/** @brief output of fixed (DIV2) MC HUB divider */
#define TEGRA234_CLK_MCHUB			472U
/** @brief output of divider controlled by EMC side A MC_EMC_SAFE_SAME_FREQ */
#define TEGRA234_CLK_EMCSA_MC			473U
/** @brief output of divider controlled by EMC side B MC_EMC_SAFE_SAME_FREQ */
#define TEGRA234_CLK_EMCSB_MC			474U
/** @brief output of divider controlled by EMC side C MC_EMC_SAFE_SAME_FREQ */
#define TEGRA234_CLK_EMCSC_MC			475U
/** @brief output of divider controlled by EMC side D MC_EMC_SAFE_SAME_FREQ */
#define TEGRA234_CLK_EMCSD_MC			476U

/** @} */

#endif
