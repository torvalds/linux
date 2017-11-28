/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _L3_H_
#define _L3_H_ 1

struct l3_pins {
	void (*setdat)(struct l3_pins *, int);
	void (*setclk)(struct l3_pins *, int);
	void (*setmode)(struct l3_pins *, int);

	int gpio_data;
	int gpio_clk;
	int gpio_mode;
	int use_gpios;

	int data_hold;
	int data_setup;
	int clock_high;
	int mode_hold;
	int mode;
	int mode_setup;
};

struct device;

int l3_write(struct l3_pins *adap, u8 addr, u8 *data, int len);
int l3_set_gpio_ops(struct device *dev, struct l3_pins *adap);

#endif
