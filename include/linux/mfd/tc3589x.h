/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License, version 2
 */

#ifndef __LINUX_MFD_TC3589x_H
#define __LINUX_MFD_TC3589x_H

struct device;

enum tx3589x_block {
	TC3589x_BLOCK_GPIO        = 1 << 0,
	TC3589x_BLOCK_KEYPAD      = 1 << 1,
};

#define TC3589x_RSTCTRL_IRQRST	(1 << 4)
#define TC3589x_RSTCTRL_TIMRST	(1 << 3)
#define TC3589x_RSTCTRL_ROTRST	(1 << 2)
#define TC3589x_RSTCTRL_KBDRST	(1 << 1)
#define TC3589x_RSTCTRL_GPIRST	(1 << 0)

/* Keyboard Configuration Registers */
#define TC3589x_KBDSETTLE_REG   0x01
#define TC3589x_KBDBOUNCE       0x02
#define TC3589x_KBDSIZE         0x03
#define TC3589x_KBCFG_LSB       0x04
#define TC3589x_KBCFG_MSB       0x05
#define TC3589x_KBDIC           0x08
#define TC3589x_KBDMSK          0x09
#define TC3589x_EVTCODE_FIFO    0x10
#define TC3589x_KBDMFS		0x8F

#define TC3589x_IRQST		0x91

#define TC3589x_MANFCODE_MAGIC	0x03
#define TC3589x_MANFCODE	0x80
#define TC3589x_VERSION		0x81
#define TC3589x_IOCFG		0xA7

#define TC3589x_CLKMODE		0x88
#define TC3589x_CLKCFG		0x89
#define TC3589x_CLKEN		0x8A

#define TC3589x_RSTCTRL		0x82
#define TC3589x_EXTRSTN		0x83
#define TC3589x_RSTINTCLR	0x84

/* Pull up/down configuration registers */
#define TC3589x_IOCFG           0xA7
#define TC3589x_IOPULLCFG0_LSB  0xAA
#define TC3589x_IOPULLCFG0_MSB  0xAB
#define TC3589x_IOPULLCFG1_LSB  0xAC
#define TC3589x_IOPULLCFG1_MSB  0xAD
#define TC3589x_IOPULLCFG2_LSB  0xAE

#define TC3589x_GPIOIS0		0xC9
#define TC3589x_GPIOIS1		0xCA
#define TC3589x_GPIOIS2		0xCB
#define TC3589x_GPIOIBE0	0xCC
#define TC3589x_GPIOIBE1	0xCD
#define TC3589x_GPIOIBE2	0xCE
#define TC3589x_GPIOIEV0	0xCF
#define TC3589x_GPIOIEV1	0xD0
#define TC3589x_GPIOIEV2	0xD1
#define TC3589x_GPIOIE0		0xD2
#define TC3589x_GPIOIE1		0xD3
#define TC3589x_GPIOIE2		0xD4
#define TC3589x_GPIORIS0	0xD6
#define TC3589x_GPIORIS1	0xD7
#define TC3589x_GPIORIS2	0xD8
#define TC3589x_GPIOMIS0	0xD9
#define TC3589x_GPIOMIS1	0xDA
#define TC3589x_GPIOMIS2	0xDB
#define TC3589x_GPIOIC0		0xDC
#define TC3589x_GPIOIC1		0xDD
#define TC3589x_GPIOIC2		0xDE

#define TC3589x_GPIODATA0	0xC0
#define TC3589x_GPIOMASK0	0xc1
#define TC3589x_GPIODATA1	0xC2
#define TC3589x_GPIOMASK1	0xc3
#define TC3589x_GPIODATA2	0xC4
#define TC3589x_GPIOMASK2	0xC5

#define TC3589x_GPIODIR0	0xC6
#define TC3589x_GPIODIR1	0xC7
#define TC3589x_GPIODIR2	0xC8

#define TC3589x_GPIOSYNC0	0xE6
#define TC3589x_GPIOSYNC1	0xE7
#define TC3589x_GPIOSYNC2	0xE8

#define TC3589x_GPIOWAKE0	0xE9
#define TC3589x_GPIOWAKE1	0xEA
#define TC3589x_GPIOWAKE2	0xEB

#define TC3589x_GPIOODM0	0xE0
#define TC3589x_GPIOODE0	0xE1
#define TC3589x_GPIOODM1	0xE2
#define TC3589x_GPIOODE1	0xE3
#define TC3589x_GPIOODM2	0xE4
#define TC3589x_GPIOODE2	0xE5

#define TC3589x_INT_GPIIRQ	0
#define TC3589x_INT_TI0IRQ	1
#define TC3589x_INT_TI1IRQ	2
#define TC3589x_INT_TI2IRQ	3
#define TC3589x_INT_ROTIRQ	5
#define TC3589x_INT_KBDIRQ	6
#define TC3589x_INT_PORIRQ	7

#define TC3589x_NR_INTERNAL_IRQS	8

struct tc3589x {
	struct mutex lock;
	struct device *dev;
	struct i2c_client *i2c;
	struct irq_domain *domain;

	int irq_base;
	int num_gpio;
	struct tc3589x_platform_data *pdata;
};

extern int tc3589x_reg_write(struct tc3589x *tc3589x, u8 reg, u8 data);
extern int tc3589x_reg_read(struct tc3589x *tc3589x, u8 reg);
extern int tc3589x_block_read(struct tc3589x *tc3589x, u8 reg, u8 length,
			      u8 *values);
extern int tc3589x_block_write(struct tc3589x *tc3589x, u8 reg, u8 length,
			       const u8 *values);
extern int tc3589x_set_bits(struct tc3589x *tc3589x, u8 reg, u8 mask, u8 val);

/*
 * Keypad related platform specific constants
 * These values may be modified for fine tuning
 */
#define TC_KPD_ROWS             0x8
#define TC_KPD_COLUMNS          0x8
#define TC_KPD_DEBOUNCE_PERIOD  0xA3
#define TC_KPD_SETTLE_TIME      0xA3


/**
 * struct tc3589x_platform_data - TC3589x platform data
 * @block: bitmask of blocks to enable (use TC3589x_BLOCK_*)
 */
struct tc3589x_platform_data {
	unsigned int block;
};

#endif
