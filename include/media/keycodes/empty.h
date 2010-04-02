/* empty.h - Keytable for empty Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* empty keytable, can be used as placeholder for not-yet created keytables */

#ifdef IR_KEYMAPS
static struct ir_scancode empty[] = {
	{ 0x2a, KEY_COFFEE },
};
DEFINE_LEGACY_IR_KEYTABLE(empty);
#else
DECLARE_IR_KEYTABLE(empty);
#endif
