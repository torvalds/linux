/*
 * Analog Devices ADP5589 I/O Expander and QWERTY Keypad Controller
 *
 * Copyright 2010-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef _ADP5589_H
#define _ADP5589_H

#define ADP5589_ID			0x00
#define ADP5589_INT_STATUS		0x01
#define ADP5589_STATUS			0x02
#define ADP5589_FIFO_1			0x03
#define ADP5589_FIFO_2			0x04
#define ADP5589_FIFO_3			0x05
#define ADP5589_FIFO_4			0x06
#define ADP5589_FIFO_5			0x07
#define ADP5589_FIFO_6			0x08
#define ADP5589_FIFO_7			0x09
#define ADP5589_FIFO_8			0x0A
#define ADP5589_FIFO_9			0x0B
#define ADP5589_FIFO_10			0x0C
#define ADP5589_FIFO_11			0x0D
#define ADP5589_FIFO_12			0x0E
#define ADP5589_FIFO_13			0x0F
#define ADP5589_FIFO_14			0x10
#define ADP5589_FIFO_15			0x11
#define ADP5589_FIFO_16			0x12
#define ADP5589_GPI_INT_STAT_A		0x13
#define ADP5589_GPI_INT_STAT_B		0x14
#define ADP5589_GPI_INT_STAT_C		0x15
#define ADP5589_GPI_STATUS_A		0x16
#define ADP5589_GPI_STATUS_B		0x17
#define ADP5589_GPI_STATUS_C		0x18
#define ADP5589_RPULL_CONFIG_A		0x19
#define ADP5589_RPULL_CONFIG_B		0x1A
#define ADP5589_RPULL_CONFIG_C		0x1B
#define ADP5589_RPULL_CONFIG_D		0x1C
#define ADP5589_RPULL_CONFIG_E		0x1D
#define ADP5589_GPI_INT_LEVEL_A		0x1E
#define ADP5589_GPI_INT_LEVEL_B		0x1F
#define ADP5589_GPI_INT_LEVEL_C		0x20
#define ADP5589_GPI_EVENT_EN_A		0x21
#define ADP5589_GPI_EVENT_EN_B		0x22
#define ADP5589_GPI_EVENT_EN_C		0x23
#define ADP5589_GPI_INTERRUPT_EN_A	0x24
#define ADP5589_GPI_INTERRUPT_EN_B	0x25
#define ADP5589_GPI_INTERRUPT_EN_C	0x26
#define ADP5589_DEBOUNCE_DIS_A		0x27
#define ADP5589_DEBOUNCE_DIS_B		0x28
#define ADP5589_DEBOUNCE_DIS_C		0x29
#define ADP5589_GPO_DATA_OUT_A		0x2A
#define ADP5589_GPO_DATA_OUT_B		0x2B
#define ADP5589_GPO_DATA_OUT_C		0x2C
#define ADP5589_GPO_OUT_MODE_A		0x2D
#define ADP5589_GPO_OUT_MODE_B		0x2E
#define ADP5589_GPO_OUT_MODE_C		0x2F
#define ADP5589_GPIO_DIRECTION_A	0x30
#define ADP5589_GPIO_DIRECTION_B	0x31
#define ADP5589_GPIO_DIRECTION_C	0x32
#define ADP5589_UNLOCK1			0x33
#define ADP5589_UNLOCK2			0x34
#define ADP5589_EXT_LOCK_EVENT		0x35
#define ADP5589_UNLOCK_TIMERS		0x36
#define ADP5589_LOCK_CFG		0x37
#define ADP5589_RESET1_EVENT_A		0x38
#define ADP5589_RESET1_EVENT_B		0x39
#define ADP5589_RESET1_EVENT_C		0x3A
#define ADP5589_RESET2_EVENT_A		0x3B
#define ADP5589_RESET2_EVENT_B		0x3C
#define ADP5589_RESET_CFG		0x3D
#define ADP5589_PWM_OFFT_LOW		0x3E
#define ADP5589_PWM_OFFT_HIGH		0x3F
#define ADP5589_PWM_ONT_LOW		0x40
#define ADP5589_PWM_ONT_HIGH		0x41
#define ADP5589_PWM_CFG			0x42
#define ADP5589_CLOCK_DIV_CFG		0x43
#define ADP5589_LOGIC_1_CFG		0x44
#define ADP5589_LOGIC_2_CFG		0x45
#define ADP5589_LOGIC_FF_CFG		0x46
#define ADP5589_LOGIC_INT_EVENT_EN	0x47
#define ADP5589_POLL_PTIME_CFG		0x48
#define ADP5589_PIN_CONFIG_A		0x49
#define ADP5589_PIN_CONFIG_B		0x4A
#define ADP5589_PIN_CONFIG_C		0x4B
#define ADP5589_PIN_CONFIG_D		0x4C
#define ADP5589_GENERAL_CFG		0x4D
#define ADP5589_INT_EN			0x4E

#define ADP5589_DEVICE_ID_MASK	0xF

/* Put one of these structures in i2c_board_info platform_data */

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

/* Mask Bits:
 * C C C C C C C C C C C | R R R R R R R R
 * 1 9 8 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0
 * 0
 * ---------------- BIT ------------------
 * 1 1 1 1 1 1 1 1 1 0 0 | 0 0 0 0 0 0 0 0
 * 8 7 6 5 4 3 2 1 0 9 8 | 7 6 5 4 3 2 1 0
 */

#define ADP_ROW(x)			(1 << (x))
#define ADP_COL(x)			(1 << (x + 8))

struct adp5589_kpad_platform_data {
	unsigned keypad_en_mask;	/* Keypad (Rows/Columns) enable mask */
	const unsigned short *keymap;	/* Pointer to keymap */
	unsigned short keymapsize;	/* Keymap size */
	bool repeat;			/* Enable key repeat */
	bool en_keylock;		/* Enable key lock feature */
	unsigned char unlock_key1;	/* Unlock Key 1 */
	unsigned char unlock_key2;	/* Unlock Key 2 */
	unsigned char unlock_timer;	/* Time in seconds [0..7] between the two unlock keys 0=disable */
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
