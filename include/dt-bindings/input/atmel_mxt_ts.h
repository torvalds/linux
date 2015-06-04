/*
 * Atmel maXTouch Touchscreen driver
 *
 * Copyright (C) 2015 Atmel Corporation
 * Author: Nick Dyer <nick.dyer@itdev.co.uk>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __DT_BINDINGS_ATMEL_MXT_TS_H
#define __DT_BINDINGS_ATMEL_MXT_TS_H

enum mxt_suspend_mode {
	MXT_SUSPEND_DEEP_SLEEP = 0,
	MXT_SUSPEND_T9_CTRL = 1,
	MXT_SUSPEND_REGULATOR = 2,
};

#endif /* __DT_BINDINGS_ATMEL_MXT_TS_H */
