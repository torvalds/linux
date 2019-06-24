/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 */
#ifndef __CLOCKSOURCE_SAMSUNG_PWM_H
#define __CLOCKSOURCE_SAMSUNG_PWM_H

#include <linux/spinlock.h>

#define SAMSUNG_PWM_NUM		5

/*
 * Following declaration must be in an ifdef due to this symbol being static
 * in pwm-samsung driver if the clocksource driver is not compiled in and the
 * spinlock is not shared between both drivers.
 */
#ifdef CONFIG_CLKSRC_SAMSUNG_PWM
extern spinlock_t samsung_pwm_lock;
#endif

struct samsung_pwm_variant {
	u8 bits;
	u8 div_base;
	u8 tclk_mask;
	u8 output_mask;
	bool has_tint_cstat;
};

void samsung_pwm_clocksource_init(void __iomem *base,
		unsigned int *irqs, struct samsung_pwm_variant *variant);

#endif /* __CLOCKSOURCE_SAMSUNG_PWM_H */
