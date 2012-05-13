/*
 * include/linux/mfd/wm8994/core.h -- Core interface for WM8994
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __MFD_WM8994_CORE_H__
#define __MFD_WM8994_CORE_H__

#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>

enum wm8994_type {
	WM8994 = 0,
	WM8958 = 1,
	WM1811 = 2,
};

struct regulator_dev;
struct regulator_bulk_data;

#define WM8994_NUM_GPIO_REGS 11
#define WM8994_NUM_LDO_REGS   2
#define WM8994_NUM_IRQ_REGS   2

#define WM8994_IRQ_TEMP_SHUT		0
#define WM8994_IRQ_MIC1_DET		1
#define WM8994_IRQ_MIC1_SHRT		2
#define WM8994_IRQ_MIC2_DET		3
#define WM8994_IRQ_MIC2_SHRT		4
#define WM8994_IRQ_FLL1_LOCK		5
#define WM8994_IRQ_FLL2_LOCK		6
#define WM8994_IRQ_SRC1_LOCK		7
#define WM8994_IRQ_SRC2_LOCK		8
#define WM8994_IRQ_AIF1DRC1_SIG_DET	9
#define WM8994_IRQ_AIF1DRC2_SIG_DET	10
#define WM8994_IRQ_AIF2DRC_SIG_DET	11
#define WM8994_IRQ_FIFOS_ERR		12
#define WM8994_IRQ_WSEQ_DONE		13
#define WM8994_IRQ_DCS_DONE		14
#define WM8994_IRQ_TEMP_WARN		15

/* GPIOs in the chip are numbered from 1-11 */
#define WM8994_IRQ_GPIO(x) (x + WM8994_IRQ_TEMP_WARN)

struct wm8994 {
	struct mutex irq_lock;

	enum wm8994_type type;
	int revision;

	struct device *dev;
	struct regmap *regmap;

	bool ldo_ena_always_driven;

	int gpio_base;
	int irq_base;

	int irq;
	struct regmap_irq_chip_data *irq_data;

	/* Used over suspend/resume */
	bool suspended;

	struct regulator_dev *dbvdd;
	int num_supplies;
	struct regulator_bulk_data *supplies;
};

/* Device I/O API */
int wm8994_reg_read(struct wm8994 *wm8994, unsigned short reg);
int wm8994_reg_write(struct wm8994 *wm8994, unsigned short reg,
		 unsigned short val);
int wm8994_set_bits(struct wm8994 *wm8994, unsigned short reg,
		    unsigned short mask, unsigned short val);
int wm8994_bulk_read(struct wm8994 *wm8994, unsigned short reg,
		     int count, u16 *buf);
int wm8994_bulk_write(struct wm8994 *wm8994, unsigned short reg,
		     int count, const u16 *buf);


/* Helper to save on boilerplate */
static inline int wm8994_request_irq(struct wm8994 *wm8994, int irq,
				     irq_handler_t handler, const char *name,
				     void *data)
{
	if (!wm8994->irq_data)
		return -EINVAL;
	return request_threaded_irq(regmap_irq_get_virq(wm8994->irq_data, irq),
				    NULL, handler, IRQF_TRIGGER_RISING, name,
				    data);
}
static inline void wm8994_free_irq(struct wm8994 *wm8994, int irq, void *data)
{
	if (!wm8994->irq_data)
		return;
	free_irq(regmap_irq_get_virq(wm8994->irq_data, irq), data);
}

int wm8994_irq_init(struct wm8994 *wm8994);
void wm8994_irq_exit(struct wm8994 *wm8994);

#endif
