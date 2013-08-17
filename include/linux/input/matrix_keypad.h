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
};

/**
 * matrix_keypad_build_keymap - convert platform keymap into matrix keymap
 * @keymap_data: keymap supplied by the platform code
 * @row_shift: number of bits to shift row value by to advance to the next
 * line in the keymap
 * @keymap: expanded version of keymap that is suitable for use by
 * matrix keyboad driver
 * @keybit: pointer to bitmap of keys supported by input device
 *
 * This function converts platform keymap (encoded with KEY() macro) into
 * an array of keycodes that is suitable for using in a standard matrix
 * keyboard driver that uses row and col as indices.
 */
static inline void
matrix_keypad_build_keymap(const struct matrix_keymap_data *keymap_data,
			   unsigned int row_shift,
			   unsigned short *keymap, unsigned long *keybit)
{
	int i;

	for (i = 0; i < keymap_data->keymap_size; i++) {
		unsigned int key = keymap_data->keymap[i];
		unsigned int row = KEY_ROW(key);
		unsigned int col = KEY_COL(key);
		unsigned short code = KEY_VAL(key);

		keymap[MATRIX_SCAN_CODE(row, col, row_shift)] = code;
		__set_bit(code, keybit);
	}
	__clear_bit(KEY_RESERVED, keybit);
}

#ifdef CONFIG_INPUT_OF_MATRIX_KEYMAP
struct matrix_keymap_data *
matrix_keyboard_of_fill_keymap(struct device_node *np, const char *propname);

void matrix_keyboard_of_free_keymap(const struct matrix_keymap_data *kd);
#else
static inline struct matrix_keymap_data *
matrix_keyboard_of_fill_keymap(struct device_node *np, const char *propname)
{
	return NULL;
}

static inline void
matrix_keyboard_of_free_keymap(const struct matrix_keymap_data *kd)
{
}
#endif

#endif /* _MATRIX_KEYPAD_H */
