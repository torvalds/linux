/*
 * core.h  -- Core driver for NXP PCF50633
 *
 * (C) 2006-2008 by Openmoko, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __LINUX_MFD_PCF50633_CORE_H
#define __LINUX_MFD_PCF50633_CORE_H

#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/power_supply.h>
#include <linux/mfd/pcf50633/backlight.h>

struct pcf50633;

#define PCF50633_NUM_REGULATORS	11

struct pcf50633_platform_data {
	struct regulator_init_data reg_init_data[PCF50633_NUM_REGULATORS];

	char **batteries;
	int num_batteries;

	/*
	 * Should be set accordingly to the reference resistor used, see
	 * I_{ch(ref)} charger reference current in the pcf50633 User
	 * Manual.
	 */
	int charger_reference_current_ma;

	/* Callbacks */
	void (*probe_done)(struct pcf50633 *);
	void (*mbc_event_callback)(struct pcf50633 *, int);
	void (*regulator_registered)(struct pcf50633 *, int);
	void (*force_shutdown)(struct pcf50633 *);

	u8 resumers[5];

	struct pcf50633_bl_platform_data *backlight_data;
};

struct pcf50633_irq {
	void (*handler) (int, void *);
	void *data;
};

int pcf50633_register_irq(struct pcf50633 *pcf, int irq,
			void (*handler) (int, void *), void *data);
int pcf50633_free_irq(struct pcf50633 *pcf, int irq);

int pcf50633_irq_mask(struct pcf50633 *pcf, int irq);
int pcf50633_irq_unmask(struct pcf50633 *pcf, int irq);
int pcf50633_irq_mask_get(struct pcf50633 *pcf, int irq);

int pcf50633_read_block(struct pcf50633 *, u8 reg,
					int nr_regs, u8 *data);
int pcf50633_write_block(struct pcf50633 *pcf, u8 reg,
					int nr_regs, u8 *data);
u8 pcf50633_reg_read(struct pcf50633 *, u8 reg);
int pcf50633_reg_write(struct pcf50633 *pcf, u8 reg, u8 val);

int pcf50633_reg_set_bit_mask(struct pcf50633 *pcf, u8 reg, u8 mask, u8 val);
int pcf50633_reg_clear_bits(struct pcf50633 *pcf, u8 reg, u8 bits);

/* Interrupt registers */

#define PCF50633_REG_INT1	0x02
#define PCF50633_REG_INT2	0x03
#define PCF50633_REG_INT3	0x04
#define PCF50633_REG_INT4	0x05
#define PCF50633_REG_INT5	0x06

#define PCF50633_REG_INT1M	0x07
#define PCF50633_REG_INT2M	0x08
#define PCF50633_REG_INT3M	0x09
#define PCF50633_REG_INT4M	0x0a
#define PCF50633_REG_INT5M	0x0b

enum {
	/* Chip IRQs */
	PCF50633_IRQ_ADPINS,
	PCF50633_IRQ_ADPREM,
	PCF50633_IRQ_USBINS,
	PCF50633_IRQ_USBREM,
	PCF50633_IRQ_RESERVED1,
	PCF50633_IRQ_RESERVED2,
	PCF50633_IRQ_ALARM,
	PCF50633_IRQ_SECOND,
	PCF50633_IRQ_ONKEYR,
	PCF50633_IRQ_ONKEYF,
	PCF50633_IRQ_EXTON1R,
	PCF50633_IRQ_EXTON1F,
	PCF50633_IRQ_EXTON2R,
	PCF50633_IRQ_EXTON2F,
	PCF50633_IRQ_EXTON3R,
	PCF50633_IRQ_EXTON3F,
	PCF50633_IRQ_BATFULL,
	PCF50633_IRQ_CHGHALT,
	PCF50633_IRQ_THLIMON,
	PCF50633_IRQ_THLIMOFF,
	PCF50633_IRQ_USBLIMON,
	PCF50633_IRQ_USBLIMOFF,
	PCF50633_IRQ_ADCRDY,
	PCF50633_IRQ_ONKEY1S,
	PCF50633_IRQ_LOWSYS,
	PCF50633_IRQ_LOWBAT,
	PCF50633_IRQ_HIGHTMP,
	PCF50633_IRQ_AUTOPWRFAIL,
	PCF50633_IRQ_DWN1PWRFAIL,
	PCF50633_IRQ_DWN2PWRFAIL,
	PCF50633_IRQ_LEDPWRFAIL,
	PCF50633_IRQ_LEDOVP,
	PCF50633_IRQ_LDO1PWRFAIL,
	PCF50633_IRQ_LDO2PWRFAIL,
	PCF50633_IRQ_LDO3PWRFAIL,
	PCF50633_IRQ_LDO4PWRFAIL,
	PCF50633_IRQ_LDO5PWRFAIL,
	PCF50633_IRQ_LDO6PWRFAIL,
	PCF50633_IRQ_HCLDOPWRFAIL,
	PCF50633_IRQ_HCLDOOVL,

	/* Always last */
	PCF50633_NUM_IRQ,
};

struct pcf50633 {
	struct device *dev;
	struct i2c_client *i2c_client;

	struct pcf50633_platform_data *pdata;
	int irq;
	struct pcf50633_irq irq_handler[PCF50633_NUM_IRQ];
	struct work_struct irq_work;
	struct workqueue_struct *work_queue;
	struct mutex lock;

	u8 mask_regs[5];

	u8 suspend_irq_masks[5];
	u8 resume_reason[5];
	int is_suspended;

	int onkey1s_held;

	struct platform_device *rtc_pdev;
	struct platform_device *mbc_pdev;
	struct platform_device *adc_pdev;
	struct platform_device *input_pdev;
	struct platform_device *bl_pdev;
	struct platform_device *regulator_pdev[PCF50633_NUM_REGULATORS];
};

enum pcf50633_reg_int1 {
	PCF50633_INT1_ADPINS	= 0x01,	/* Adapter inserted */
	PCF50633_INT1_ADPREM	= 0x02,	/* Adapter removed */
	PCF50633_INT1_USBINS	= 0x04,	/* USB inserted */
	PCF50633_INT1_USBREM	= 0x08,	/* USB removed */
	/* reserved */
	PCF50633_INT1_ALARM	= 0x40, /* RTC alarm time is reached */
	PCF50633_INT1_SECOND	= 0x80,	/* RTC periodic second interrupt */
};

enum pcf50633_reg_int2 {
	PCF50633_INT2_ONKEYR	= 0x01, /* ONKEY rising edge */
	PCF50633_INT2_ONKEYF	= 0x02, /* ONKEY falling edge */
	PCF50633_INT2_EXTON1R	= 0x04, /* EXTON1 rising edge */
	PCF50633_INT2_EXTON1F	= 0x08, /* EXTON1 falling edge */
	PCF50633_INT2_EXTON2R	= 0x10, /* EXTON2 rising edge */
	PCF50633_INT2_EXTON2F	= 0x20, /* EXTON2 falling edge */
	PCF50633_INT2_EXTON3R	= 0x40, /* EXTON3 rising edge */
	PCF50633_INT2_EXTON3F	= 0x80, /* EXTON3 falling edge */
};

enum pcf50633_reg_int3 {
	PCF50633_INT3_BATFULL	= 0x01, /* Battery full */
	PCF50633_INT3_CHGHALT	= 0x02,	/* Charger halt */
	PCF50633_INT3_THLIMON	= 0x04,
	PCF50633_INT3_THLIMOFF	= 0x08,
	PCF50633_INT3_USBLIMON	= 0x10,
	PCF50633_INT3_USBLIMOFF	= 0x20,
	PCF50633_INT3_ADCRDY	= 0x40, /* ADC result ready */
	PCF50633_INT3_ONKEY1S	= 0x80,	/* ONKEY pressed 1 second */
};

enum pcf50633_reg_int4 {
	PCF50633_INT4_LOWSYS		= 0x01,
	PCF50633_INT4_LOWBAT		= 0x02,
	PCF50633_INT4_HIGHTMP		= 0x04,
	PCF50633_INT4_AUTOPWRFAIL	= 0x08,
	PCF50633_INT4_DWN1PWRFAIL	= 0x10,
	PCF50633_INT4_DWN2PWRFAIL	= 0x20,
	PCF50633_INT4_LEDPWRFAIL	= 0x40,
	PCF50633_INT4_LEDOVP		= 0x80,
};

enum pcf50633_reg_int5 {
	PCF50633_INT5_LDO1PWRFAIL	= 0x01,
	PCF50633_INT5_LDO2PWRFAIL	= 0x02,
	PCF50633_INT5_LDO3PWRFAIL	= 0x04,
	PCF50633_INT5_LDO4PWRFAIL	= 0x08,
	PCF50633_INT5_LDO5PWRFAIL	= 0x10,
	PCF50633_INT5_LDO6PWRFAIL	= 0x20,
	PCF50633_INT5_HCLDOPWRFAIL	= 0x40,
	PCF50633_INT5_HCLDOOVL		= 0x80,
};

/* misc. registers */
#define PCF50633_REG_OOCSHDWN	0x0c

/* LED registers */
#define PCF50633_REG_LEDOUT 0x28
#define PCF50633_REG_LEDENA 0x29
#define PCF50633_REG_LEDCTL 0x2a
#define PCF50633_REG_LEDDIM 0x2b

static inline struct pcf50633 *dev_to_pcf50633(struct device *dev)
{
	return dev_get_drvdata(dev);
}

int pcf50633_irq_init(struct pcf50633 *pcf, int irq);
void pcf50633_irq_free(struct pcf50633 *pcf);
#ifdef CONFIG_PM
int pcf50633_irq_suspend(struct pcf50633 *pcf);
int pcf50633_irq_resume(struct pcf50633 *pcf);
#endif

#endif
