/*
 * Analog Devices ADP5589/ADP5585 I/O Expander and QWERTY Keypad Controller
 *
 * Copyright 2010-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef _ADP5589_H
#define _ADP5589_H

/*
 * ADP5589 specific GPI and Keymap defines
 */

#define ADP5589_KEYMAPSIZE	88

#define ADP5589_GPI_PIN_ROW0 97
#define ADP5589_GPI_PIN_ROW1 98
#define ADP5589_GPI_PIN_ROW2 99
#define ADP5589_GPI_PIN_ROW3 100
#define ADP5589_GPI_PIN_ROW4 101
#define ADP5589_GPI_PIN_ROW5 102
#define ADP5589_GPI_PIN_ROW6 103
#define ADP5589_GPI_PIN_ROW7 104
#define ADP5589_GPI_PIN_COL0 105
#define ADP5589_GPI_PIN_COL1 106
#define ADP5589_GPI_PIN_COL2 107
#define ADP5589_GPI_PIN_COL3 108
#define ADP5589_GPI_PIN_COL4 109
#define ADP5589_GPI_PIN_COL5 110
#define ADP5589_GPI_PIN_COL6 111
#define ADP5589_GPI_PIN_COL7 112
#define ADP5589_GPI_PIN_COL8 113
#define ADP5589_GPI_PIN_COL9 114
#define ADP5589_GPI_PIN_COL10 115
#define GPI_LOGIC1 116
#define GPI_LOGIC2 117

#define ADP5589_GPI_PIN_ROW_BASE ADP5589_GPI_PIN_ROW0
#define ADP5589_GPI_PIN_ROW_END ADP5589_GPI_PIN_ROW7
#define ADP5589_GPI_PIN_COL_BASE ADP5589_GPI_PIN_COL0
#define ADP5589_GPI_PIN_COL_END ADP5589_GPI_PIN_COL10

#define ADP5589_GPI_PIN_BASE ADP5589_GPI_PIN_ROW_BASE
#define ADP5589_GPI_PIN_END ADP5589_GPI_PIN_COL_END

#define ADP5589_GPIMAPSIZE_MAX (ADP5589_GPI_PIN_END - ADP5589_GPI_PIN_BASE + 1)

/*
 * ADP5585 specific GPI and Keymap defines
 */

#define ADP5585_KEYMAPSIZE	30

#define ADP5585_GPI_PIN_ROW0 37
#define ADP5585_GPI_PIN_ROW1 38
#define ADP5585_GPI_PIN_ROW2 39
#define ADP5585_GPI_PIN_ROW3 40
#define ADP5585_GPI_PIN_ROW4 41
#define ADP5585_GPI_PIN_ROW5 42
#define ADP5585_GPI_PIN_COL0 43
#define ADP5585_GPI_PIN_COL1 44
#define ADP5585_GPI_PIN_COL2 45
#define ADP5585_GPI_PIN_COL3 46
#define ADP5585_GPI_PIN_COL4 47
#define GPI_LOGIC 48

#define ADP5585_GPI_PIN_ROW_BASE ADP5585_GPI_PIN_ROW0
#define ADP5585_GPI_PIN_ROW_END ADP5585_GPI_PIN_ROW5
#define ADP5585_GPI_PIN_COL_BASE ADP5585_GPI_PIN_COL0
#define ADP5585_GPI_PIN_COL_END ADP5585_GPI_PIN_COL4

#define ADP5585_GPI_PIN_BASE ADP5585_GPI_PIN_ROW_BASE
#define ADP5585_GPI_PIN_END ADP5585_GPI_PIN_COL_END

#define ADP5585_GPIMAPSIZE_MAX (ADP5585_GPI_PIN_END - ADP5585_GPI_PIN_BASE + 1)

struct adp5589_gpi_map {
	unsigned short pin;
	unsigned short sw_evt;
};

/* scan_cycle_time */
#define ADP5589_SCAN_CYCLE_10ms		0
#define ADP5589_SCAN_CYCLE_20ms		1
#define ADP5589_SCAN_CYCLE_30ms		2
#define ADP5589_SCAN_CYCLE_40ms		3

/* RESET_CFG */
#define RESET_PULSE_WIDTH_500us		0
#define RESET_PULSE_WIDTH_1ms		1
#define RESET_PULSE_WIDTH_2ms		2
#define RESET_PULSE_WIDTH_10ms		3

#define RESET_TRIG_TIME_0ms		(0 << 2)
#define RESET_TRIG_TIME_1000ms		(1 << 2)
#define RESET_TRIG_TIME_1500ms		(2 << 2)
#define RESET_TRIG_TIME_2000ms		(3 << 2)
#define RESET_TRIG_TIME_2500ms		(4 << 2)
#define RESET_TRIG_TIME_3000ms		(5 << 2)
#define RESET_TRIG_TIME_3500ms		(6 << 2)
#define RESET_TRIG_TIME_4000ms		(7 << 2)

#define RESET_PASSTHRU_EN		(1 << 5)
#define RESET1_POL_HIGH			(1 << 6)
#define RESET1_POL_LOW			(0 << 6)
#define RESET2_POL_HIGH			(1 << 7)
#define RESET2_POL_LOW			(0 << 7)

/* ADP5589 Mask Bits:
 * C C C C C C C C C C C | R R R R R R R R
 * 1 9 8 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0
 * 0
 * ---------------- BIT ------------------
 * 1 1 1 1 1 1 1 1 1 0 0 | 0 0 0 0 0 0 0 0
 * 8 7 6 5 4 3 2 1 0 9 8 | 7 6 5 4 3 2 1 0
 */

#define ADP_ROW(x)	(1 << (x))
#define ADP_COL(x)	(1 << (x + 8))
#define ADP5589_ROW_MASK		0xFF
#define ADP5589_COL_MASK		0xFF
#define ADP5589_COL_SHIFT		8
#define ADP5589_MAX_ROW_NUM		7
#define ADP5589_MAX_COL_NUM		10

/* ADP5585 Mask Bits:
 * C C C C C | R R R R R R
 * 4 3 2 1 0 | 5 4 3 2 1 0
 *
 * ---- BIT -- -----------
 * 1 0 0 0 0 | 0 0 0 0 0 0
 * 0 9 8 7 6 | 5 4 3 2 1 0
 */

#define ADP5585_ROW_MASK		0x3F
#define ADP5585_COL_MASK		0x1F
#define ADP5585_ROW_SHIFT		0
#define ADP5585_COL_SHIFT		6
#define ADP5585_MAX_ROW_NUM		5
#define ADP5585_MAX_COL_NUM		4

#define ADP5585_ROW(x)	(1 << ((x) & ADP5585_ROW_MASK))
#define ADP5585_COL(x)	(1 << (((x) & ADP5585_COL_MASK) + ADP5585_COL_SHIFT))

/* Put one of these structures in i2c_board_info platform_data */

struct adp5589_kpad_platform_data {
	unsigned keypad_en_mask;	/* Keypad (Rows/Columns) enable mask */
	const unsigned short *keymap;	/* Pointer to keymap */
	unsigned short keymapsize;	/* Keymap size */
	bool repeat;			/* Enable key repeat */
	bool en_keylock;		/* Enable key lock feature (ADP5589 only)*/
	unsigned char unlock_key1;	/* Unlock Key 1 (ADP5589 only) */
	unsigned char unlock_key2;	/* Unlock Key 2 (ADP5589 only) */
	unsigned char unlock_timer;	/* Time in seconds [0..7] between the two unlock keys 0=disable (ADP5589 only) */
	unsigned char scan_cycle_time;	/* Time between consecutive scan cycles */
	unsigned char reset_cfg;	/* Reset config */
	unsigned short reset1_key_1;	/* Reset Key 1 */
	unsigned short reset1_key_2;	/* Reset Key 2 */
	unsigned short reset1_key_3;	/* Reset Key 3 */
	unsigned short reset2_key_1;	/* Reset Key 1 */
	unsigned short reset2_key_2;	/* Reset Key 2 */
	unsigned debounce_dis_mask;	/* Disable debounce mask */
	unsigned pull_dis_mask;		/* Disable all pull resistors mask */
	unsigned pullup_en_100k;	/* Pull-Up 100k Enable Mask */
	unsigned pullup_en_300k;	/* Pull-Up 300k Enable Mask */
	unsigned pulldown_en_300k;	/* Pull-Down 300k Enable Mask */
	const struct adp5589_gpi_map *gpimap;
	unsigned short gpimapsize;
	const struct adp5589_gpio_platform_data *gpio_data;
};

struct i2c_client; /* forward declaration */

struct adp5589_gpio_platform_data {
	int	gpio_start;	/* GPIO Chip base # */
	int	(*setup)(struct i2c_client *client,
				int gpio, unsigned ngpio,
				void *context);
	int	(*teardown)(struct i2c_client *client,
				int gpio, unsigned ngpio,
				void *context);
	void	*context;
};

#endif
