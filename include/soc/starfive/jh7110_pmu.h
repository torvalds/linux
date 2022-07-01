/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Reset driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2022 samin <samin.guo@starfivetech.com>
 */

#ifndef __SOC_STARFIVE_JH7110_PMU_H__
#define __SOC_STARFIVE_JH7110_PMU_H__

#include <linux/bits.h>
#include <linux/types.h>

/* SW/HW Power domain id  */
enum PMU_POWER_DOMAIN {
	POWER_DOMAIN_SYSTOP	= BIT(0),
	POWER_DOMAIN_CPU	= BIT(1),
	POWER_DOMAIN_GPUA	= BIT(2),
	POWER_DOMAIN_VDEC	= BIT(3),
	POWER_DOMAIN_JPU	= POWER_DOMAIN_VDEC,
	POWER_DOMAIN_VOUT	= BIT(4),
	POWER_DOMAIN_ISP	= BIT(5),
	POWER_DOMAIN_VENC	= BIT(6),
	POWER_DOMAIN_GPUB	= BIT(7),
	POWER_DOMAIN_ALL	= GENMASK(7, 0),
};

enum PMU_HARD_EVENT {
	PMU_HW_EVENT_RTC	= BIT(0),
	PMU_HW_EVENT_GMAC	= BIT(1),
	PMU_HW_EVENT_RFU	= BIT(2),
	PMU_HW_EVENT_RGPIO0	= BIT(3),
	PMU_HW_EVENT_RGPIO1	= BIT(4),
	PMU_HW_EVENT_RGPIO2	= BIT(5),
	PMU_HW_EVENT_RGPIO3	= BIT(6),
	PMU_HW_EVENT_GPU	= BIT(7),
	PMU_HW_EVENT_ALL	= GENMASK(7, 0),
};

/*
 * @func: starfive_power_domain_set
 * @dec: power domain turn-on/off by software
 * @domain: power domain id
 *	POWER_DOMAIN_SYSTOP:
 *	POWER_DOMAIN_CPU
 *	POWER_DOMAIN_GPUA
 *	POWER_DOMAIN_VDEC
 *	POWER_DOMAIN_VOUT
 *	POWER_DOMAIN_ISP
 *	POWER_DOMAIN_VENC
 *	POWER_DOMAIN_GPUB
 * @enable: 1:enable 0:disable
 */
//void starfive_power_domain_set(u32 domain, bool enable);

/*
 * @func: starfive_pmu_hw_encourage
 * @dec: power domain turn-on/off by HW envent(interrupt)
 * @domain: power domain id
 * @event: Hardware trigger event. PMU_HARD_EVENT:
	PMU_HW_EVENT_RTC,
	PMU_HW_EVENT_GMAC,
	PMU_HW_EVENT_RFU,
	PMU_HW_EVENT_RGPIO0,
	PMU_HW_EVENT_RGPIO1,
	PMU_HW_EVENT_RGPIO2,
	PMU_HW_EVENT_RGPIO3,
	PMU_HW_EVENT_GPU,
 * @enable: 1:enable 0:disable
 *
 * @for example:
 *	starfive_power_domain_set_by_hwevent(POWER_DOMAIN_VDEC, RTC_EVENT, 0);
 *
 *	Means that when the RTC alarm is interrupted, the hardware
 *	is triggered to close the power domain of VDEC.
 */
void starfive_power_domain_set_by_hwevent(u32 domain, u32 event, bool enable);

/*
 * @func: starfive_power_domain_order_on_get
 * @dec: PMU power domian power on order get.
 * @domian: powerff domain id
 */
int starfive_power_domain_order_on_get(u32 domain);

/*
 * @func: starfive_power_domain_order_off_get
 * @dec: PMU power domian power off order get.
 * @domian: power domain id
 */
int starfive_power_domain_order_off_get(u32 domain);

/*
 * @func: starfive_power_domain_order_on_set
 * @dec: PMU power domian power on order set.
 * @domian: powerff domain id
 * @order: the poweron order of domain
 */
void starfive_power_domain_order_on_set(u32 domain, u32 order);

/*
 * @func: starfive_power_domain_order_off_set
 * @dec: PMU power domian power off order set.
 * @domian: power domain id
 * @order: the poweroff order of domain
 */
void starfive_power_domain_order_off_set(u32 domain, u32 order);

void starfive_pmu_hw_event_turn_off_mask(u32 mask);

#endif /* __SOC_STARFIVE_JH7110_PMU_H__ */
