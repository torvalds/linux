/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, The Linux Foundation. All rights reserved. */

#ifndef __QCOM_RPMH_REGULATOR_H
#define __QCOM_RPMH_REGULATOR_H

/*
 * These mode constants may be used to specify modes for various RPMh regulator
 * device tree properties (e.g. regulator-initial-mode).  Each type of regulator
 * supports a subset of the possible modes.
 *
 * %RPMH_REGULATOR_MODE_RET:	Retention mode in which only an extremely small
 *				load current is allowed.  This mode is supported
 *				by LDO and SMPS type regulators.
 * %RPMH_REGULATOR_MODE_LPM:	Low power mode in which a small load current is
 *				allowed.  This mode corresponds to PFM for SMPS
 *				and BOB type regulators.  This mode is supported
 *				by LDO, HFSMPS, BOB, and PMIC4 FTSMPS type
 *				regulators.
 * %RPMH_REGULATOR_MODE_AUTO:	Auto mode in which the regulator hardware
 *				automatically switches between LPM and HPM based
 *				upon the real-time load current.  This mode is
 *				supported by HFSMPS, BOB, and PMIC4 FTSMPS type
 *				regulators.
 * %RPMH_REGULATOR_MODE_HPM:	High power mode in which the full rated current
 *				of the regulator is allowed.  This mode
 *				corresponds to PWM for SMPS and BOB type
 *				regulators.  This mode is supported by all types
 *				of regulators.
 */
#define RPMH_REGULATOR_MODE_RET		0
#define RPMH_REGULATOR_MODE_LPM		1
#define RPMH_REGULATOR_MODE_AUTO	2
#define RPMH_REGULATOR_MODE_HPM		3

#endif
