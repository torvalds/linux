/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2026 Arturia - All rights reserved.
 *
 * Device Tree binding constants for the FAN53555 PMIC regulator
 */

#ifndef _DT_BINDINGS_REGULATOR_FAN53555_H
#define _DT_BINDINGS_REGULATOR_FAN53555_H

/*
 * Constants to specify regulator modes in device tree for SYR82X regulators
 * FAN53555_REGULATOR_MODE_FORCE_PWM:	Force fixed PWM mode
 * FAN53555_REGULATOR_MODE_AUTO:	Allow auto-PFM mode during light load
 */

#define FAN53555_REGULATOR_MODE_FORCE_PWM	1
#define FAN53555_REGULATOR_MODE_AUTO		2

#endif
