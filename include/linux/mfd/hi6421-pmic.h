/*
 * Header file for device driver Hi6421 PMIC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (C) 2011 Hisilicon.
 *
 * Guodong Xu <guodong.xu@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef	__HI6421_PMIC_H
#define	__HI6421_PMIC_H

#include <linux/irqdomain.h>
#include <linux/mutex.h>
#include <linux/time.h>

#define	OCP_DEB_CTRL_REG		(0x51)
#define	OCP_DEB_SEL_MASK		(0x0C)
#define OCP_DEB_SEL_8MS			(0x00)
#define OCP_DEB_SEL_16MS		(0x04)
#define OCP_DEB_SEL_32MS		(0x08)
#define OCP_DEB_SEL_64MS		(0x0C)
#define OCP_EN_DEBOUNCE_MASK		(0x02)
#define OCP_EN_DEBOUNCE_ENABLE		(0x02)
#define OCP_AUTO_STOP_MASK		(0x01)
#define OCP_AUTO_STOP_ENABLE		(0x01)
#define HI6421_REGS_ENA_PROTECT_TIME	(100) 	/* in microseconds */
#define HI6421_ECO_MODE_ENABLE		(1)
#define HI6421_ECO_MODE_DISABLE		(0)

#define HI6421_NR_IRQ			24
#define HI6421_MASK_FIELD		0xFF
#define HI6421_BITS			8

#define HI6421_IRQ_ALARM		0	/* RTC Alarm */
#define HI6421_IRQ_OTMP			1	/* Temperature too high */
#define HI6421_IRQ_OCP			2	/* BUCK/LDO overload */
#define HI6421_IRQ_RESET_IN		3	/* RESETIN_N signal */
#define HI6421_IRQ_ONKEY_10S		4	/* PWRON key hold for 10s */
#define HI6421_IRQ_ONKEY_1S		5	/* PWRON key hold for 1s */
#define HI6421_IRQ_ONKEY_UP		6	/* PWRON key released */
#define HI6421_IRQ_ONKEY_DOWN		7	/* PWRON key pressed */
#define HI6421_IRQ_HEADSET_OUT		8	/* Headset plugged out */
#define HI6421_IRQ_HEADSET_IN		9	/* Headset plugged in */
#define HI6421_IRQ_COULOMB		10	/* Coulomb Counter */
#define HI6421_IRQ_VBUS_UP		11	/* VBUS rising */
#define HI6421_IRQ_VBUS_DOWN		12	/* VBUS falling */
#define HI6421_IRQ_VBAT_LOW		13	/* VBattery too low */
#define HI6421_IRQ_VBAT_HIGH		14	/* VBattery overvoltage */
#define HI6421_IRQ_CHARGE_IN1		15	/* Charger plugged in */
#define HI6421_IRQ_CHARGE_IN3		16	/* Charger plugged in */
#define HI6421_IRQ_CHARGE_IN2		17	/* Charger plugged in */
#define HI6421_IRQ_HS_BTN_DOWN		18	/* Headset button pressed */
#define HI6421_IRQ_HS_BTN_UP		19	/* Headset button released */
#define HI6421_IRQ_BATTERY_ON		20	/* Battery inserted */
#define HI6421_IRQ_RESET		21	/* Reset */

/**
 * struct hi6421_pmic - Hi6421 PMIC chip-level data structure.
 *
 * This struct describes Hi6421 PMIC chip-level data.
 *
 * @enable_mutex: Used by regulators, to make sure that only one regulator is
 *                in the turning-on phase at any time. See also @last_enabled
 *                and hi6421_regulator_enable().
 * @last_enabled: Used by regulators, to make sure enough distance in time
 *                between enabling of any of two regulators.
 */
struct hi6421_pmic {
	struct resource		*res;
	struct device		*dev;
	void __iomem		*regs;
	spinlock_t		lock;
	struct irq_domain	*domain;
	int			irq;
	int			gpio;
	struct mutex		enable_mutex;
	struct timeval		last_enabled;
};

/* Register Access Helpers */
u32 hi6421_pmic_read(struct hi6421_pmic *pmic, int reg);
void hi6421_pmic_write(struct hi6421_pmic *pmic, int reg, u32 val);
void hi6421_pmic_rmw(struct hi6421_pmic *pmic, int reg, u32 mask, u32 bits);

#endif		/* __HI6421_PMIC_H */
