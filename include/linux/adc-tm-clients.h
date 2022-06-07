/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 */

#ifndef __QCOM_ADC_TM_H_CLIENTS__
#define __QCOM_ADC_TM_H_CLIENTS__

#include <linux/err.h>
#include <linux/types.h>

struct adc_tm_chip;
struct adc5_chip;

/**
 * enum adc_tm_state - This lets the client know whether the threshold
 *		that was crossed was high/low.
 * %ADC_TM_HIGH_STATE: Client is notified of crossing the requested high
 *			voltage threshold.
 * %ADC_TM_COOL_STATE: Client is notified of crossing the requested cool
 *			temperature threshold.
 * %ADC_TM_LOW_STATE: Client is notified of crossing the requested low
 *			voltage threshold.
 * %ADC_TM_WARM_STATE: Client is notified of crossing the requested high
 *			temperature threshold.
 */
enum adc_tm_state {
	ADC_TM_HIGH_STATE = 0,
	ADC_TM_COOL_STATE = ADC_TM_HIGH_STATE,
	ADC_TM_LOW_STATE,
	ADC_TM_WARM_STATE = ADC_TM_LOW_STATE,
	ADC_TM_STATE_NUM,
};

/**
 * enum adc_tm_state_request - Request to enable/disable the corresponding
 *			high/low voltage/temperature thresholds.
 * %ADC_TM_HIGH_THR_ENABLE: Enable high voltage threshold.
 * %ADC_TM_COOL_THR_ENABLE = Enables cool temperature threshold.
 * %ADC_TM_LOW_THR_ENABLE: Enable low voltage/temperature threshold.
 * %ADC_TM_WARM_THR_ENABLE = Enables warm temperature threshold.
 * %ADC_TM_HIGH_LOW_THR_ENABLE: Enable high and low voltage/temperature
 *				threshold.
 * %ADC_TM_HIGH_THR_DISABLE: Disable high voltage/temperature threshold.
 * %ADC_TM_COOL_THR_ENABLE = Disables cool temperature threshold.
 * %ADC_TM_LOW_THR_DISABLE: Disable low voltage/temperature threshold.
 * %ADC_TM_WARM_THR_ENABLE = Disables warm temperature threshold.
 * %ADC_TM_HIGH_THR_DISABLE: Disable high and low voltage/temperature
 *				threshold.
 */
enum adc_tm_state_request {
	ADC_TM_HIGH_THR_ENABLE = 0,
	ADC_TM_COOL_THR_ENABLE = ADC_TM_HIGH_THR_ENABLE,
	ADC_TM_LOW_THR_ENABLE,
	ADC_TM_WARM_THR_ENABLE = ADC_TM_LOW_THR_ENABLE,
	ADC_TM_HIGH_LOW_THR_ENABLE,
	ADC_TM_HIGH_THR_DISABLE,
	ADC_TM_COOL_THR_DISABLE = ADC_TM_HIGH_THR_DISABLE,
	ADC_TM_LOW_THR_DISABLE,
	ADC_TM_WARM_THR_DISABLE = ADC_TM_LOW_THR_DISABLE,
	ADC_TM_HIGH_LOW_THR_DISABLE,
	ADC_TM_THR_NUM,
};

struct adc_tm_param {
	unsigned long		id;
	int			low_thr;
	int			high_thr;
	uint32_t				channel;
	enum adc_tm_state_request	state_request;
	void					*btm_ctx;
	void	(*threshold_notification)(enum adc_tm_state state,
						void *ctx);
};

struct device;

/* Public API */

#if IS_ENABLED(CONFIG_QCOM_SPMI_ADC5_GEN3)
struct adc5_chip *get_adc_tm_gen3(struct device *dev, const char *name);
int32_t adc_tm_channel_measure_gen3(struct adc5_chip *chip,
					struct adc_tm_param *param);
int32_t adc_tm_disable_chan_meas_gen3(struct adc5_chip *chip,
					struct adc_tm_param *param);
#else
static inline struct adc5_chip *get_adc_tm_gen3(
	struct device *dev, const char *name)
{ return ERR_PTR(-ENXIO); }

static inline int32_t adc_tm_channel_measure_gen3(
					struct adc5_chip *chip,
					struct adc_tm_param *param)
{ return -ENXIO; }
static inline int32_t adc_tm_disable_chan_meas_gen3(
					struct adc5_chip *chip,
					struct adc_tm_param *param)
{ return -ENXIO; }

#endif

#endif /* __QCOM_ADC_TM_H_CLIENTS__ */
