/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2013, 2015, 2017, 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_REGULATOR_RPM_SMD_H
#define _LINUX_REGULATOR_RPM_SMD_H

#include <linux/device.h>

struct rpm_regulator;

/**
 * enum rpm_regulator_voltage_corner - possible voltage corner values
 *
 * These should be used in regulator_set_voltage() and
 * rpm_regulator_set_voltage() calls for corner type regulators as if they had
 * units of uV.
 *
 * Note, the meaning of corner values is set by the RPM.  It is possible that
 * future platforms will utilize different corner values.  The values specified
 * in this enum correspond to MSM8974 for PMIC PM8841 SMPS 2 (VDD_Dig).
 */
enum rpm_regulator_voltage_corner {
	RPM_REGULATOR_CORNER_NONE = 1,
	RPM_REGULATOR_CORNER_RETENTION,
	RPM_REGULATOR_CORNER_SVS_KRAIT,
	RPM_REGULATOR_CORNER_SVS_SOC,
	RPM_REGULATOR_CORNER_NORMAL,
	RPM_REGULATOR_CORNER_TURBO,
	RPM_REGULATOR_CORNER_SUPER_TURBO,
};

/**
 * enum rpm_regulator_voltage_level - possible voltage level values
 *
 * These should be used in regulator_set_voltage() and
 * rpm_regulator_set_voltage() calls for level type regulators as if they had
 * units of uV.
 *
 * Note: the meaning of level values is set by the RPM.
 */
enum rpm_regulator_voltage_level {
	RPM_REGULATOR_LEVEL_NONE		= 0,
	RPM_REGULATOR_LEVEL_RETENTION		= 16,
	RPM_REGULATOR_LEVEL_RETENTION_PLUS	= 32,
	RPM_REGULATOR_LEVEL_MIN_SVS		= 48,
	RPM_REGULATOR_LEVEL_LOW_SVS		= 64,
	RPM_REGULATOR_LEVEL_SVS			= 128,
	RPM_REGULATOR_LEVEL_SVS_PLUS		= 192,
	RPM_REGULATOR_LEVEL_NOM			= 256,
	RPM_REGULATOR_LEVEL_NOM_PLUS		= 320,
	RPM_REGULATOR_LEVEL_TURBO		= 384,
	RPM_REGULATOR_LEVEL_TURBO_NO_CPR	= 416,
	RPM_REGULATOR_LEVEL_BINNING		= 512,
	RPM_REGULATOR_LEVEL_MAX			= 65535,
};

/**
 * enum rpm_regulator_mode - control mode for LDO or SMPS type regulators
 * %RPM_REGULATOR_MODE_AUTO:	For SMPS type regulators, use SMPS auto mode so
 *				that the hardware can automatically switch
 *				between PFM and PWM modes based on realtime
 *				load.
 *				LDO type regulators do not support this mode.
 * %RPM_REGULATOR_MODE_IPEAK:	For SMPS type regulators, use aggregated
 *				software current requests to determine
 *				usage of PFM or PWM mode.
 *				For LDO type regulators, use aggregated
 *				software current requests to determine
 *				usage of LPM or HPM mode.
 * %RPM_REGULATOR_MODE_HPM:	For SMPS type regulators, force the
 *				usage of PWM mode.
 *				For LDO type regulators, force the
 *				usage of HPM mode.
 *
 * These values should be used in calls to rpm_regulator_set_mode().
 */
enum rpm_regulator_mode {
	RPM_REGULATOR_MODE_AUTO,
	RPM_REGULATOR_MODE_IPEAK,
	RPM_REGULATOR_MODE_HPM,
};

#ifdef CONFIG_DEBUG_FS

static void rpm_vreg_create_debugfs(struct rpm_regulator *reg);

#else

static inline void rpm_vreg_create_debugfs(struct rpm_regulator *reg)
{
}
#endif
#endif
