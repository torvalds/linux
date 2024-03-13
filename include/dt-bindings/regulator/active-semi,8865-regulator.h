/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Device Tree binding constants for the ACT8865 PMIC regulators
 */

#ifndef _DT_BINDINGS_REGULATOR_ACT8865_H
#define _DT_BINDINGS_REGULATOR_ACT8865_H

/*
 * These constants should be used to specify regulator modes in device tree for
 * ACT8865 regulators as follows:
 * ACT8865_REGULATOR_MODE_FIXED:	It is specific to DCDC regulators and it
 *					specifies the usage of fixed-frequency
 *					PWM.
 *
 * ACT8865_REGULATOR_MODE_NORMAL:	It is specific to LDO regulators and it
 *					specifies the usage of normal mode.
 *
 * ACT8865_REGULATOR_MODE_LOWPOWER:	For DCDC and LDO regulators; it specify
 *					the usage of proprietary power-saving
 *					mode.
 */

#define ACT8865_REGULATOR_MODE_FIXED		1
#define ACT8865_REGULATOR_MODE_NORMAL		2
#define ACT8865_REGULATOR_MODE_LOWPOWER	3

#endif
