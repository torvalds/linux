/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MATRIX_KEYPAD_H
#define _MATRIX_KEYPAD_H

#include <linux/types.h>

struct device;
struct input_dev;

#define MATRIX_MAX_ROWS		32
#define MATRIX_MAX_COLS		32

#define KEY(row, col, val)	((((row) & (MATRIX_MAX_ROWS - 1)) << 24) |\
				 (((col) & (MATRIX_MAX_COLS - 1)) << 16) |\
				 ((val) & 0xffff))

#define KEY_ROW(k)		(((k) >> 24) & 0xff)
#define KEY_COL(k)		(((k) >> 16) & 0xff)
#define KEY_VAL(k)		((k) & 0xffff)

#define MATRIX_SCAN_CODE(row, col, row_shift)	(((row) << (row_shift)) + (col))

/**
 * struct matrix_keymap_data - keymap for matrix keyboards
 * @keymap: pointer to array of uint32 values encoded with KEY() macro
 *	representing keymap
 * @keymap_size: number of entries (initialized) in this keymap
 *
 * This structure is supposed to be used by platform code to supply
 * keymaps to drivers that implement matrix-like keypads/keyboards.
 */
struct matrix_keymap_data {
	const uint32_t *keymap;
	unsigned int	keymap_size;
};

int matrix_keypad_build_keymap(const struct matrix_keymap_data *keymap_data,
			       const char *keymap_name,
			       unsigned int rows, unsigned int cols,
			       unsigned short *keymap,
			       struct input_dev *input_dev);
int matrix_keypad_parse_properties(struct device *dev,
				   unsigned int *rows, unsigned int *cols);

#endif /* _MATRIX_KEYPAD_H */
