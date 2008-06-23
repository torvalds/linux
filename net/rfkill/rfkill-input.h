/*
 * Copyright (C) 2007 Ivo van Doorn
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef __RFKILL_INPUT_H
#define __RFKILL_INPUT_H

void rfkill_switch_all(enum rfkill_type type, enum rfkill_state state);
void rfkill_epo(void);

#endif /* __RFKILL_INPUT_H */
