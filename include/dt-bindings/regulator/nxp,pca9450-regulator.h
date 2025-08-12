/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Device Tree binding constants for the NXP PCA9450A/B/C PMIC regulators
 */

#ifndef _DT_BINDINGS_REGULATORS_NXP_PCA9450_H
#define _DT_BINDINGS_REGULATORS_NXP_PCA9450_H

/*
 * Buck mode constants which may be used in devicetree properties (eg.
 * regulator-initial-mode, regulator-allowed-modes).
 * See the manufacturer's datasheet for more information on these modes.
 */

#define PCA9450_BUCK_MODE_AUTO		0
#define PCA9450_BUCK_MODE_FORCE_PWM	1

#endif
