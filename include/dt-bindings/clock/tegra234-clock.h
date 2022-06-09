/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018-2022, NVIDIA CORPORATION. All rights reserved. */

#ifndef DT_BINDINGS_CLOCK_TEGRA234_CLOCK_H
#define DT_BINDINGS_CLOCK_TEGRA234_CLOCK_H

/**
 * @file
 * @defgroup bpmp_clock_ids Clock ID's
 * @{
 */
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AHUB */
#define TEGRA234_CLK_AHUB			4U
/** @brief output of gate CLK_ENB_APB2APE */
#define TEGRA234_CLK_APB2APE			5U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_APE */
#define TEGRA234_CLK_APE			6U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AUD_MCLK */
#define TEGRA234_CLK_AUD_MCLK			7U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC1 */
#define TEGRA234_CLK_DMIC1			15U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC2 */
#define TEGRA234_CLK_DMIC2			16U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC3 */
#define TEGRA234_CLK_DMIC3			17U
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC4 */
#define TEGRA234_CLK_DMIC4			18U
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
/** @brief output of gate CLK_ENB_FUSE */
#define TEGRA234_CLK_FUSE			40U
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
/** PLL controlled by CLK_RST_CONTROLLER_PLLA_BASE for use by audio clocks */
#define TEGRA234_CLK_PLLA			93U
/** @brief PLLP clk output */
#define TEGRA234_CLK_PLLP_OUT0			102U
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
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SDMMC4 */
#define TEGRA234_CLK_SDMMC4			123U
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
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTA */
#define TEGRA234_CLK_UARTA			155U
/** @brief output of gate CLK_ENB_PEX1_CORE_6 */
#define TEGRA234_CLK_PEX1_C6_CORE		161U
/** @brief output of gate CLK_ENB_PEX2_CORE_7 */
#define TEGRA234_CLK_PEX2_C7_CORE		171U
/** @brief output of gate CLK_ENB_PEX2_CORE_8 */
#define TEGRA234_CLK_PEX2_C8_CORE		172U
/** @brief output of gate CLK_ENB_PEX2_CORE_9 */
#define TEGRA234_CLK_PEX2_C9_CORE		173U
/** @brief output of gate CLK_ENB_PEX2_CORE_10 */
#define TEGRA234_CLK_PEX2_C10_CORE		187U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_QSPI0 switch divider output */
#define TEGRA234_CLK_QSPI0_2X_PM		192U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_QSPI1 switch divider output */
#define TEGRA234_CLK_QSPI1_2X_PM		193U
/** @brief output of the divider QSPI_CLK_DIV2_SEL in CLK_RST_CONTROLLER_CLK_SOURCE_QSPI0 */
#define TEGRA234_CLK_QSPI0_PM			194U
/** @brief output of the divider QSPI_CLK_DIV2_SEL in CLK_RST_CONTROLLER_CLK_SOURCE_QSPI1 */
#define TEGRA234_CLK_QSPI1_PM			195U
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
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLC4_BASE */
#define TEGRA234_CLK_PLLC4			237U
/** @brief 32K input clock provided by PMIC */
#define TEGRA234_CLK_CLK_32K			289U
/** @brief CLK_RST_CONTROLLER_AZA2XBITCLK_OUT_SWITCH_DIVIDER switch divider output (aza_2xbitclk) */
#define TEGRA234_CLK_AZA_2XBIT			457U
/** @brief aza_2xbitclk / 2 (aza_bitclk) */
#define TEGRA234_CLK_AZA_BIT			458U
#endif
