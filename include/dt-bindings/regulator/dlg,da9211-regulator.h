/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _DT_BINDINGS_REGULATOR_DLG_DA9211_H
#define _DT_BINDINGS_REGULATOR_DLG_DA9211_H

/*
 * These buck mode constants may be used to specify values in device tree
 * properties (e.g. regulator-initial-mode, regulator-allowed-modes).
 * A description of the following modes is in the manufacturers datasheet.
 */

#define DA9211_BUCK_MODE_SLEEP		1
#define DA9211_BUCK_MODE_SYNC		2
#define DA9211_BUCK_MODE_AUTO		3

#endif
