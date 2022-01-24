/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018-2019, NVIDIA CORPORATION. All rights reserved. */

#ifndef DT_BINDINGS_CLOCK_TEGRA234_CLOCK_H
#define DT_BINDINGS_CLOCK_TEGRA234_CLOCK_H

/**
 * @file
 * @defgroup bpmp_clock_ids Clock ID's
 * @{
 */
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
/** @brief PLLP clk output */
#define TEGRA234_CLK_PLLP_OUT0			102U
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
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTA */
#define TEGRA234_CLK_UARTA			155U
/** @brief CLK_RST_CONTROLLER_CLK_SOURCE_SDMMC_LEGACY_TM switch divider output */
#define TEGRA234_CLK_SDMMC_LEGACY_TM		219U
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLC4_BASE */
#define TEGRA234_CLK_PLLC4			237U
/** @brief 32K input clock provided by PMIC */
#define TEGRA234_CLK_CLK_32K			289U
#endif
