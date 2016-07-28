/*
 *	LED driver for TI lp3952 controller
 *
 *	Copyright (C) 2016, DAQRI, LLC.
 *	Author: Tony Makkiel <tony.makkiel@daqri.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef LEDS_LP3952_H_
#define LEDS_LP3952_H_

#define LP3952_NAME                         "lp3952"
#define LP3952_CMD_REG_COUNT                8
#define LP3952_BRIGHT_MAX                   4
#define LP3952_LABEL_MAX_LEN                15

#define LP3952_REG_LED_CTRL                 0x00
#define LP3952_REG_R1_BLNK_TIME_CTRL        0x01
#define LP3952_REG_R1_BLNK_CYCLE_CTRL       0x02
#define LP3952_REG_G1_BLNK_TIME_CTRL        0x03
#define LP3952_REG_G1_BLNK_CYCLE_CTRL       0x04
#define LP3952_REG_B1_BLNK_TIME_CTRL        0x05
#define LP3952_REG_B1_BLNK_CYCLE_CTRL       0x06
#define LP3952_REG_ENABLES                  0x0B
#define LP3952_REG_PAT_GEN_CTRL             0x11
#define LP3952_REG_RGB1_MAX_I_CTRL          0x12
#define LP3952_REG_RGB2_MAX_I_CTRL          0x13
#define LP3952_REG_CMD_0                    0x50
#define LP3952_REG_RESET                    0x60
#define REG_MAX                             LP3952_REG_RESET

#define LP3952_PATRN_LOOP                   BIT(1)
#define LP3952_PATRN_GEN_EN                 BIT(2)
#define LP3952_INT_B00ST_LDR                BIT(2)
#define LP3952_ACTIVE_MODE                  BIT(6)
#define LP3952_LED_MASK_ALL                 0x3f

/* Transition Time in ms */
enum lp3952_tt {
	TT0,
	TT55,
	TT110,
	TT221,
	TT422,
	TT885,
	TT1770,
	TT3539
};

/* Command Execution Time in ms */
enum lp3952_cet {
	CET197,
	CET393,
	CET590,
	CET786,
	CET1180,
	CET1376,
	CET1573,
	CET1769,
	CET1966,
	CET2163,
	CET2359,
	CET2556,
	CET2763,
	CET2949,
	CET3146
};

/* Max Current in % */
enum lp3952_colour_I_log_0 {
	I0,
	I7,
	I14,
	I21,
	I32,
	I46,
	I71,
	I100
};

enum lp3952_leds {
	LP3952_BLUE_2,
	LP3952_GREEN_2,
	LP3952_RED_2,
	LP3952_BLUE_1,
	LP3952_GREEN_1,
	LP3952_RED_1,
	LP3952_LED_ALL
};

struct lp3952_ctrl_hdl {
	struct led_classdev cdev;
	char name[LP3952_LABEL_MAX_LEN];
	enum lp3952_leds channel;
	void *priv;
};

struct ptrn_gen_cmd {
	union {
		struct {
			u16 tt:3;
			u16 b:3;
			u16 cet:4;
			u16 g:3;
			u16 r:3;
		};
		struct {
			u8 lsb;
			u8 msb;
		} bytes;
	};
} __packed;

struct lp3952_led_array {
	struct regmap *regmap;
	struct i2c_client *client;
	struct gpio_desc *enable_gpio;
	struct lp3952_ctrl_hdl leds[LP3952_LED_ALL];
};

#endif /* LEDS_LP3952_H_ */
