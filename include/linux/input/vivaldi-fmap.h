/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VIVALDI_FMAP_H
#define _VIVALDI_FMAP_H

#include <linux/types.h>

#define VIVALDI_MAX_FUNCTION_ROW_KEYS	24

/**
 * struct vivaldi_data - Function row map data for ChromeOS Vivaldi keyboards
 * @function_row_physmap: An array of scancodes or their equivalent (HID usage
 *                        codes, encoded rows/columns, etc) for the top
 *                        row function keys, in an order from left to right
 * @num_function_row_keys: The number of top row keys in a custom keyboard
 *
 * This structure is supposed to be used by ChromeOS keyboards using
 * the Vivaldi keyboard function row design.
 */
struct vivaldi_data {
	u32 function_row_physmap[VIVALDI_MAX_FUNCTION_ROW_KEYS];
	unsigned int num_function_row_keys;
};

ssize_t vivaldi_function_row_physmap_show(const struct vivaldi_data *data,
					  char *buf);

#endif /* _VIVALDI_FMAP_H */
