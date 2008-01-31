#ifndef __ASM_ARCH_PXA27x_KEYPAD_H
#define __ASM_ARCH_PXA27x_KEYPAD_H

#include <linux/input.h>

#define MAX_MATRIX_KEY_ROWS	(8)
#define MAX_MATRIX_KEY_COLS	(8)

struct pxa27x_keypad_platform_data {

	/* code map for the matrix keys */
	unsigned int	matrix_key_rows;
	unsigned int	matrix_key_cols;
	unsigned int	*matrix_key_map;
	int		matrix_key_map_size;
};

#define KEY(row, col, val)	(((row) << 28) | ((col) << 24) | (val))

#endif /* __ASM_ARCH_PXA27x_KEYPAD_H */
