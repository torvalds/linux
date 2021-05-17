/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Atmel Power Management
 *
 * Copyright (C) 2020 Atmel
 *
 * Author: Lee Jones <lee.jones@linaro.org>
 */

#ifndef __SOC_ATMEL_PM_H
#define __SOC_ATMEL_PM_H

void at91_pinctrl_gpio_suspend(void);
void at91_pinctrl_gpio_resume(void);

#endif /* __SOC_ATMEL_PM_H */
