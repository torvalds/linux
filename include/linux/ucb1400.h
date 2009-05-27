/*
 * Register definitions and functions for:
 *  Philips UCB1400 driver
 *
 * Based on ucb1400_ts:
 *  Author:	Nicolas Pitre
 *  Created:	September 25, 2006
 *  Copyright:	MontaVista Software, Inc.
 *
 * Spliting done by: Marek Vasut <marek.vasut@gmail.com>
 * If something doesnt work and it worked before spliting, e-mail me,
 * dont bother Nicolas please ;-)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This code is heavily based on ucb1x00-*.c copyrighted by Russell King
 * covering the UCB1100, UCB1200 and UCB1300..  Support for the UCB1400 has
 * been made separate from ucb1x00-core/ucb1x00-ts on Russell's request.
 */

#ifndef _LINUX__UCB1400_H
#define _LINUX__UCB1400_H

#include <sound/ac97_codec.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

/*
 * UCB1400 AC-link registers
 */

#define UCB_IO_DATA		0x5a
#define UCB_IO_DIR		0x5c
#define UCB_IE_RIS		0x5e
#define UCB_IE_FAL		0x60
#define UCB_IE_STATUS		0x62
#define UCB_IE_CLEAR		0x62
#define UCB_IE_ADC		(1 << 11)
#define UCB_IE_TSPX		(1 << 12)

#define UCB_TS_CR		0x64
#define UCB_TS_CR_TSMX_POW	(1 << 0)
#define UCB_TS_CR_TSPX_POW	(1 << 1)
#define UCB_TS_CR_TSMY_POW	(1 << 2)
#define UCB_TS_CR_TSPY_POW	(1 << 3)
#define UCB_TS_CR_TSMX_GND	(1 << 4)
#define UCB_TS_CR_TSPX_GND	(1 << 5)
#define UCB_TS_CR_TSMY_GND	(1 << 6)
#define UCB_TS_CR_TSPY_GND	(1 << 7)
#define UCB_TS_CR_MODE_INT	(0 << 8)
#define UCB_TS_CR_MODE_PRES	(1 << 8)
#define UCB_TS_CR_MODE_POS	(2 << 8)
#define UCB_TS_CR_BIAS_ENA	(1 << 11)
#define UCB_TS_CR_TSPX_LOW	(1 << 12)
#define UCB_TS_CR_TSMX_LOW	(1 << 13)

#define UCB_ADC_CR		0x66
#define UCB_ADC_SYNC_ENA	(1 << 0)
#define UCB_ADC_VREFBYP_CON	(1 << 1)
#define UCB_ADC_INP_TSPX	(0 << 2)
#define UCB_ADC_INP_TSMX	(1 << 2)
#define UCB_ADC_INP_TSPY	(2 << 2)
#define UCB_ADC_INP_TSMY	(3 << 2)
#define UCB_ADC_INP_AD0		(4 << 2)
#define UCB_ADC_INP_AD1		(5 << 2)
#define UCB_ADC_INP_AD2		(6 << 2)
#define UCB_ADC_INP_AD3		(7 << 2)
#define UCB_ADC_EXT_REF		(1 << 5)
#define UCB_ADC_START		(1 << 7)
#define UCB_ADC_ENA		(1 << 15)

#define UCB_ADC_DATA		0x68
#define UCB_ADC_DAT_VALID	(1 << 15)
#define UCB_ADC_DAT_MASK	0x3ff

#define UCB_ID			0x7e
#define UCB_ID_1400             0x4304

struct ucb1400_ts {
	struct input_dev	*ts_idev;
	struct task_struct	*ts_task;
	int			id;
	wait_queue_head_t	ts_wait;
	unsigned int		ts_restart:1;
	int			irq;
	unsigned int		irq_pending;	/* not bit field shared */
	struct snd_ac97		*ac97;
};

struct ucb1400 {
	struct platform_device	*ucb1400_ts;
};

static inline u16 ucb1400_reg_read(struct snd_ac97 *ac97, u16 reg)
{
	return ac97->bus->ops->read(ac97, reg);
}

static inline void ucb1400_reg_write(struct snd_ac97 *ac97, u16 reg, u16 val)
{
	ac97->bus->ops->write(ac97, reg, val);
}

static inline u16 ucb1400_gpio_get_value(struct snd_ac97 *ac97, u16 gpio)
{
	return ucb1400_reg_read(ac97, UCB_IO_DATA) & (1 << gpio);
}

static inline void ucb1400_gpio_set_value(struct snd_ac97 *ac97, u16 gpio,
						u16 val)
{
	ucb1400_reg_write(ac97, UCB_IO_DATA, val ?
			ucb1400_reg_read(ac97, UCB_IO_DATA) | (1 << gpio) :
			ucb1400_reg_read(ac97, UCB_IO_DATA) & ~(1 << gpio));
}

static inline u16 ucb1400_gpio_get_direction(struct snd_ac97 *ac97, u16 gpio)
{
	return ucb1400_reg_read(ac97, UCB_IO_DIR) & (1 << gpio);
}

static inline void ucb1400_gpio_set_direction(struct snd_ac97 *ac97, u16 gpio,
						u16 dir)
{
	ucb1400_reg_write(ac97, UCB_IO_DIR, dir ?
			ucb1400_reg_read(ac97, UCB_IO_DIR) | (1 << gpio) :
			ucb1400_reg_read(ac97, UCB_IO_DIR) & ~(1 << gpio));
}

static inline void ucb1400_adc_enable(struct snd_ac97 *ac97)
{
	ucb1400_reg_write(ac97, UCB_ADC_CR, UCB_ADC_ENA);
}

static inline void ucb1400_adc_disable(struct snd_ac97 *ac97)
{
	ucb1400_reg_write(ac97, UCB_ADC_CR, 0);
}


unsigned int ucb1400_adc_read(struct snd_ac97 *ac97, u16 adc_channel,
			      int adcsync);

#endif
