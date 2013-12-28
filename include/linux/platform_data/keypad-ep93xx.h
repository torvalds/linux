#ifndef __KEYPAD_EP93XX_H
#define __KEYPAD_EP93XX_H

struct matrix_keymap_data;

/* flags for the ep93xx_keypad driver */
#define EP93XX_KEYPAD_DISABLE_3_KEY	(1<<0)	/* disable 3-key reset */
#define EP93XX_KEYPAD_DIAG_MODE		(1<<1)	/* diagnostic mode */
#define EP93XX_KEYPAD_BACK_DRIVE	(1<<2)	/* back driving mode */
#define EP93XX_KEYPAD_TEST_MODE		(1<<3)	/* scan only column 0 */
#define EP93XX_KEYPAD_KDIV		(1<<4)	/* 1/4 clock or 1/16 clock */
#define EP93XX_KEYPAD_AUTOREPEAT	(1<<5)	/* enable key autorepeat */

/**
 * struct ep93xx_keypad_platform_data - platform specific device structure
 * @keymap_data:	pointer to &matrix_keymap_data
 * @debounce:		debounce start count; terminal count is 0xff
 * @prescale:		row/column counter pre-scaler load value
 * @flags:		see above
 */
struct ep93xx_keypad_platform_data {
	struct matrix_keymap_data *keymap_data;
	unsigned int	debounce;
	unsigned int	prescale;
	unsigned int	flags;
};

#define EP93XX_MATRIX_ROWS		(8)
#define EP93XX_MATRIX_COLS		(8)

#endif	/* __KEYPAD_EP93XX_H */
