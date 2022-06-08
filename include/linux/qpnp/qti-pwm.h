/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _QTI_PWM_H
#define _QTI_PWM_H

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/pwm.h>
#include <linux/types.h>

/**
 * enum pwm_output_type - output type of the PWM signal
 * @PWM_OUTPUT_FIXED: PWM output is fixed until a change request
 * @PWM_OUTPUT_MODULATED: PWM output is modulated in hardware
 * autonomously with a predefined pattern
 */
enum pwm_output_type {
	PWM_OUTPUT_FIXED = BIT(0),
	PWM_OUTPUT_MODULATED = BIT(1),
};

#if IS_ENABLED(CONFIG_PWM_QTI_LPG)
int qpnp_lpg_pwm_get_output_types_supported(struct pwm_device *pwm);
int qpnp_lpg_pwm_set_output_type(struct pwm_device *pwm,
					enum pwm_output_type output_type);
#else
static inline int
qpnp_lpg_pwm_get_output_types_supported(struct pwm_device *pwm)
{
	return -EINVAL;
}
static inline int qpnp_lpg_pwm_set_output_type(struct pwm_device *pwm,
					enum pwm_output_type output_type)
{
	return -EINVAL;
}
#endif

#endif
