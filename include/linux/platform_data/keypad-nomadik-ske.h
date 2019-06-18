/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Naveen Kumar Gaddipati <naveen.gaddipati@stericsson.com>
 *
 * ux500 Scroll key and Keypad Encoder (SKE) header
 */

#ifndef __SKE_H
#define __SKE_H

#include <linux/input/matrix_keypad.h>

/* register definitions for SKE peripheral */
#define SKE_CR		0x00
#define SKE_VAL0	0x04
#define SKE_VAL1	0x08
#define SKE_DBCR	0x0C
#define SKE_IMSC	0x10
#define SKE_RIS		0x14
#define SKE_MIS		0x18
#define SKE_ICR		0x1C

/*
 * Keypad module
 */

/**
 * struct keypad_platform_data - structure for platform specific data
 * @init:	pointer to keypad init function
 * @exit:	pointer to keypad deinitialisation function
 * @keymap_data: matrix scan code table for keycodes
 * @krow:	maximum number of rows
 * @kcol:	maximum number of columns
 * @debounce_ms: platform specific debounce time
 * @no_autorepeat: flag for auto repetition
 * @wakeup_enable: allow waking up the system
 */
struct ske_keypad_platform_data {
	int (*init)(void);
	int (*exit)(void);
	const struct matrix_keymap_data *keymap_data;
	u8 krow;
	u8 kcol;
	u8 debounce_ms;
	bool no_autorepeat;
	bool wakeup_enable;
};
#endif	/*__SKE_KPD_H*/
