/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License, version 2
 */

#ifndef __LINUX_MFD_TC35892_H
#define __LINUX_MFD_TC35892_H

#include <linux/device.h>

#define TC35892_RSTCTRL_IRQRST	(1 << 4)
#define TC35892_RSTCTRL_TIMRST	(1 << 3)
#define TC35892_RSTCTRL_ROTRST	(1 << 2)
#define TC35892_RSTCTRL_KBDRST	(1 << 1)
#define TC35892_RSTCTRL_GPIRST	(1 << 0)

#define TC35892_IRQST		0x91

#define TC35892_MANFCODE_MAGIC	0x03
#define TC35892_MANFCODE	0x80
#define TC35892_VERSION		0x81
#define TC35892_IOCFG		0xA7

#define TC35892_CLKMODE		0x88
#define TC35892_CLKCFG		0x89
#define TC35892_CLKEN		0x8A

#define TC35892_RSTCTRL		0x82
#define TC35892_EXTRSTN		0x83
#define TC35892_RSTINTCLR	0x84

#define TC35892_GPIOIS0		0xC9
#define TC35892_GPIOIS1		0xCA
#define TC35892_GPIOIS2		0xCB
#define TC35892_GPIOIBE0	0xCC
#define TC35892_GPIOIBE1	0xCD
#define TC35892_GPIOIBE2	0xCE
#define TC35892_GPIOIEV0	0xCF
#define TC35892_GPIOIEV1	0xD0
#define TC35892_GPIOIEV2	0xD1
#define TC35892_GPIOIE0		0xD2
#define TC35892_GPIOIE1		0xD3
#define TC35892_GPIOIE2		0xD4
#define TC35892_GPIORIS0	0xD6
#define TC35892_GPIORIS1	0xD7
#define TC35892_GPIORIS2	0xD8
#define TC35892_GPIOMIS0	0xD9
#define TC35892_GPIOMIS1	0xDA
#define TC35892_GPIOMIS2	0xDB
#define TC35892_GPIOIC0		0xDC
#define TC35892_GPIOIC1		0xDD
#define TC35892_GPIOIC2		0xDE

#define TC35892_GPIODATA0	0xC0
#define TC35892_GPIOMASK0	0xc1
#define TC35892_GPIODATA1	0xC2
#define TC35892_GPIOMASK1	0xc3
#define TC35892_GPIODATA2	0xC4
#define TC35892_GPIOMASK2	0xC5

#define TC35892_GPIODIR0	0xC6
#define TC35892_GPIODIR1	0xC7
#define TC35892_GPIODIR2	0xC8

#define TC35892_GPIOSYNC0	0xE6
#define TC35892_GPIOSYNC1	0xE7
#define TC35892_GPIOSYNC2	0xE8

#define TC35892_GPIOWAKE0	0xE9
#define TC35892_GPIOWAKE1	0xEA
#define TC35892_GPIOWAKE2	0xEB

#define TC35892_GPIOODM0	0xE0
#define TC35892_GPIOODE0	0xE1
#define TC35892_GPIOODM1	0xE2
#define TC35892_GPIOODE1	0xE3
#define TC35892_GPIOODM2	0xE4
#define TC35892_GPIOODE2	0xE5

#define TC35892_INT_GPIIRQ	0
#define TC35892_INT_TI0IRQ	1
#define TC35892_INT_TI1IRQ	2
#define TC35892_INT_TI2IRQ	3
#define TC35892_INT_ROTIRQ	5
#define TC35892_INT_KBDIRQ	6
#define TC35892_INT_PORIRQ	7

#define TC35892_NR_INTERNAL_IRQS	8
#define TC35892_INT_GPIO(x)	(TC35892_NR_INTERNAL_IRQS + (x))

struct tc35892 {
	struct mutex lock;
	struct device *dev;
	struct i2c_client *i2c;

	int irq_base;
	int num_gpio;
	struct tc35892_platform_data *pdata;
};

extern int tc35892_reg_write(struct tc35892 *tc35892, u8 reg, u8 data);
extern int tc35892_reg_read(struct tc35892 *tc35892, u8 reg);
extern int tc35892_block_read(struct tc35892 *tc35892, u8 reg, u8 length,
			      u8 *values);
extern int tc35892_block_write(struct tc35892 *tc35892, u8 reg, u8 length,
			       const u8 *values);
extern int tc35892_set_bits(struct tc35892 *tc35892, u8 reg, u8 mask, u8 val);

/**
 * struct tc35892_gpio_platform_data - TC35892 GPIO platform data
 * @gpio_base: first gpio number assigned to TC35892.  A maximum of
 *	       %TC35892_NR_GPIOS GPIOs will be allocated.
 */
struct tc35892_gpio_platform_data {
	int gpio_base;
};

/**
 * struct tc35892_platform_data - TC35892 platform data
 * @irq_base: base IRQ number.  %TC35892_NR_IRQS irqs will be used.
 * @gpio: GPIO-specific platform data
 */
struct tc35892_platform_data {
	int irq_base;
	struct tc35892_gpio_platform_data *gpio;
};

#define TC35892_NR_GPIOS	24
#define TC35892_NR_IRQS		TC35892_INT_GPIO(TC35892_NR_GPIOS)

#endif
