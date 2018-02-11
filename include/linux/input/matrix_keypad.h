/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MATRIX_KEYPAD_H
#define _MATRIX_KEYPAD_H

#include <linux/types.h>
#include <linux/input.h>
#include <linux/of.h>

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

/**
 * struct matrix_keypad_platform_data - platform-dependent keypad data
 * @keymap_data: pointer to &matrix_keymap_data
 * @row_gpios: pointer to array of gpio numbers representing rows
 * @col_gpios: pointer to array of gpio numbers reporesenting colums
 * @num_row_gpios: actual number of row gpios used by device
 * @num_col_gpios: actual number of col gpios used by device
 * @col_scan_delay_us: delay, measured in microseconds, that is
 *	needed before we can keypad after activating column gpio
 * @debounce_ms: debounce interval in milliseconds
 * @clustered_irq: may be specified if interrupts of all row/column GPIOs
 *	are bundled to one single irq
 * @clustered_irq_flags: flags that are needed for the clustered irq
 * @active_low: gpio polarity
 * @wakeup: controls whether the device should be set up as wakeup
 *	source
 * @no_autorepeat: disable key autorepeat
 * @drive_inactive_cols: drive inactive columns during scan, rather than
 *	making them inputs.
 *
 * This structure represents platform-specific data that use used by
 * matrix_keypad driver to perform proper initialization.
 */
struct matrix_keypad_platform_data {
	const struct matrix_keymap_data *keymap_data;

	const unsigned int *row_gpios;
	const unsigned int *col_gpios;

	unsigned int	num_row_gpios;
	unsigned int	num_col_gpios;

	unsigned int	col_scan_delay_us;

	/* key debounce interval in milli-second */
	unsigned int	debounce_ms;

	unsigned int	clustered_irq;
	unsigned int	clustered_irq_flags;

	bool		active_low;
	bool		wakeup;
	bool		no_autorepeat;
	bool		drive_inactive_cols;
};

int matrix_keypad_build_keymap(const struct matrix_keymap_data *keymap_data,
			       const char *keymap_name,
			       unsigned int rows, unsigned int cols,
			       unsigned short *keymap,
			       struct input_dev *input_dev);
int matrix_keypad_parse_properties(struct device *dev,
				   unsigned int *rows, unsigned int *cols);

#define matrix_keypad_parse_of_params matrix_keypad_parse_properties

#endif /* _MATRIX_KEYPAD_H */
