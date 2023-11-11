/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Texas Instruments
 *
 * Simple driver for Texas Instruments LM355x LED driver chip
 *
 * Author: G.Shark Jeong <gshark.jeong@gmail.com>
 *         Daniel Jeong <daniel.jeong@ti.com>
 */

#define LM355x_NAME "leds-lm355x"
#define LM3554_NAME "leds-lm3554"
#define LM3556_NAME "leds-lm3556"

/* lm3554 : strobe def. on */
enum lm355x_strobe {
	LM355x_PIN_STROBE_DISABLE = 0x00,
	LM355x_PIN_STROBE_ENABLE = 0x01,
};

enum lm355x_torch {
	LM355x_PIN_TORCH_DISABLE = 0,
	LM3554_PIN_TORCH_ENABLE = 0x80,
	LM3556_PIN_TORCH_ENABLE = 0x10,
};

enum lm355x_tx2 {
	LM355x_PIN_TX_DISABLE = 0,
	LM3554_PIN_TX_ENABLE = 0x20,
	LM3556_PIN_TX_ENABLE = 0x40,
};

enum lm355x_ntc {
	LM355x_PIN_NTC_DISABLE = 0,
	LM3554_PIN_NTC_ENABLE = 0x08,
	LM3556_PIN_NTC_ENABLE = 0x80,
};

enum lm355x_pmode {
	LM355x_PMODE_DISABLE = 0,
	LM355x_PMODE_ENABLE = 0x04,
};

/*
 * struct lm3554_platform_data
 * @pin_strobe: strobe input
 * @pin_torch : input pin
 *              lm3554-tx1/torch/gpio1
 *              lm3556-torch
 * @pin_tx2   : input pin
 *              lm3554-envm/tx2/gpio2
 *              lm3556-tx pin
 * @ntc_pin  : output pin
 *              lm3554-ledi/ntc
 *              lm3556-temp pin
 * @pass_mode : pass mode
 */
struct lm355x_platform_data {
	enum lm355x_strobe pin_strobe;
	enum lm355x_torch pin_tx1;
	enum lm355x_tx2 pin_tx2;
	enum lm355x_ntc ntc_pin;

	enum lm355x_pmode pass_mode;
};
