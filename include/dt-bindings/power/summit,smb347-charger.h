/* SPDX-License-Identifier: (GPL-2.0-or-later OR MIT) */
/*
 * Author: David Heidelberg <david@ixit.cz>
 */

#ifndef _DT_BINDINGS_SMB347_CHARGER_H
#define _DT_BINDINGS_SMB347_CHARGER_H

/* Charging compensation method */
#define SMB3XX_SOFT_TEMP_COMPENSATE_NONE	0
#define SMB3XX_SOFT_TEMP_COMPENSATE_CURRENT	1
#define SMB3XX_SOFT_TEMP_COMPENSATE_VOLTAGE	2

/* Charging enable control */
#define SMB3XX_CHG_ENABLE_SW			0
#define SMB3XX_CHG_ENABLE_PIN_ACTIVE_LOW	1
#define SMB3XX_CHG_ENABLE_PIN_ACTIVE_HIGH	2

/* Polarity of INOK signal */
#define SMB3XX_SYSOK_INOK_ACTIVE_LOW		0
#define SMB3XX_SYSOK_INOK_ACTIVE_HIGH		1

#endif
