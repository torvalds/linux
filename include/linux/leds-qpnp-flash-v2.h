/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016, 2020, The Linux Foundation. All rights reserved.
 */

#ifndef __LEDS_QPNP_FLASH_V2_H
#define __LEDS_QPNP_FLASH_V2_H

#include <linux/leds.h>
#include <linux/notifier.h>

enum flash_led_irq_type {
	LED_FAULT_IRQ = BIT(0),
	MITIGATION_IRQ = BIT(1),
	FLASH_TIMER_EXP_IRQ = BIT(2),
	ALL_RAMP_DOWN_DONE_IRQ = BIT(3),
	ALL_RAMP_UP_DONE_IRQ = BIT(4),
	LED3_RAMP_UP_DONE_IRQ = BIT(5),
	LED2_RAMP_UP_DONE_IRQ = BIT(6),
	LED1_RAMP_UP_DONE_IRQ = BIT(7),
	INVALID_IRQ = BIT(8),
};

int qpnp_flash_led_register_irq_notifier(struct notifier_block *nb);
int qpnp_flash_led_unregister_irq_notifier(struct notifier_block *nb);

#endif
