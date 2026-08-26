/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Code used in the main and auxiliary Qualcomm PMIC voltage ADCs
 * of type ADC5 Gen3.
 */

#ifndef QCOM_ADC5_GEN3_COMMON_H
#define QCOM_ADC5_GEN3_COMMON_H

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/iio/adc/qcom-vadc-common.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define ADC5_GEN3_HS				0x45
#define ADC5_GEN3_HS_BUSY			BIT(7)
#define ADC5_GEN3_HS_READY			BIT(0)

#define ADC5_GEN3_STATUS1			0x46
#define ADC5_GEN3_STATUS1_CONV_FAULT		BIT(7)
#define ADC5_GEN3_STATUS1_THR_CROSS		BIT(6)
#define ADC5_GEN3_STATUS1_EOC			BIT(0)

#define ADC5_GEN3_TM_EN_STS			0x47
#define ADC5_GEN3_TM_HIGH_STS			0x48
#define ADC5_GEN3_TM_LOW_STS			0x49

#define ADC5_GEN3_EOC_STS			0x4a
#define ADC5_GEN3_EOC_CHAN_0			BIT(0)

#define ADC5_GEN3_EOC_CLR			0x4b
#define ADC5_GEN3_TM_HIGH_STS_CLR		0x4c
#define ADC5_GEN3_TM_LOW_STS_CLR		0x4d
#define ADC5_GEN3_CONV_ERR_CLR			0x4e
#define ADC5_GEN3_CONV_ERR_CLR_REQ		BIT(0)

#define ADC5_GEN3_SID				0x4f
#define ADC5_GEN3_SID_MASK			GENMASK(3, 0)

#define ADC5_GEN3_PERPH_CH			0x50
#define ADC5_GEN3_CHAN_CONV_REQ			BIT(7)

#define ADC5_GEN3_TIMER_SEL			0x51
#define ADC5_GEN3_TIME_IMMEDIATE		0x1

#define ADC5_GEN3_DIG_PARAM			0x52
#define ADC5_GEN3_DIG_PARAM_CAL_SEL_MASK	GENMASK(5, 4)
#define ADC5_GEN3_DIG_PARAM_DEC_RATIO_SEL_MASK	GENMASK(3, 2)

#define ADC5_GEN3_FAST_AVG			0x53
#define ADC5_GEN3_FAST_AVG_CTL_EN		BIT(7)
#define ADC5_GEN3_FAST_AVG_CTL_SAMPLES_MASK	GENMASK(2, 0)

#define ADC5_GEN3_ADC_CH_SEL_CTL		0x54
#define ADC5_GEN3_DELAY_CTL			0x55
#define ADC5_GEN3_HW_SETTLE_DELAY_MASK		GENMASK(3, 0)

#define ADC5_GEN3_CH_EN				0x56
#define ADC5_GEN3_HIGH_THR_INT_EN		BIT(1)
#define ADC5_GEN3_LOW_THR_INT_EN		BIT(0)

#define ADC5_GEN3_LOW_THR0			0x57
#define ADC5_GEN3_LOW_THR1			0x58
#define ADC5_GEN3_HIGH_THR0			0x59
#define ADC5_GEN3_HIGH_THR1			0x5a

#define ADC5_GEN3_CH_DATA0(channel)	(0x5c + (channel) * 2)
#define ADC5_GEN3_CH_DATA1(channel)	(0x5d + (channel) * 2)

#define ADC5_GEN3_CONV_REQ			0xe5
#define ADC5_GEN3_CONV_REQ_REQ			BIT(0)

#define ADC5_GEN3_VIRTUAL_SID_MASK		GENMASK(15, 8)
#define ADC5_GEN3_CHANNEL_MASK			GENMASK(7, 0)
#define ADC5_GEN3_V_CHAN(x)		\
	(FIELD_PREP(ADC5_GEN3_VIRTUAL_SID_MASK, (x).sid) | (x).channel)

/* ADC channels for PMIC5 Gen3 */
#define ADC5_GEN3_REF_GND			0x00
#define ADC5_GEN3_1P25VREF			0x01
#define ADC5_GEN3_DIE_TEMP			0x03
#define ADC5_GEN3_USB_SNS_V_16			0x11
#define ADC5_GEN3_VIN_DIV16_MUX			0x12
#define ADC5_GEN3_VPH_PWR			0x8e
#define ADC5_GEN3_VBAT_SNS_QBG			0x8f
/* 100k pull-up channels */
#define ADC5_GEN3_AMUX1_THM_100K_PU		0x44
#define ADC5_GEN3_AMUX2_THM_100K_PU		0x45
#define ADC5_GEN3_AMUX3_THM_100K_PU		0x46
#define ADC5_GEN3_AMUX4_THM_100K_PU		0x47
#define ADC5_GEN3_AMUX5_THM_100K_PU		0x48
#define ADC5_GEN3_AMUX6_THM_100K_PU		0x49
#define ADC5_GEN3_AMUX1_GPIO_100K_PU		0x4a
#define ADC5_GEN3_AMUX2_GPIO_100K_PU		0x4b
#define ADC5_GEN3_AMUX3_GPIO_100K_PU		0x4c
#define ADC5_GEN3_AMUX4_GPIO_100K_PU		0x4d

#define ADC5_MAX_CHANNEL			0xc0

enum adc5_cal_method {
	ADC5_NO_CAL = 0,
	ADC5_RATIOMETRIC_CAL,
	ADC5_ABSOLUTE_CAL,
};

enum adc5_time_select {
	MEAS_INT_DISABLE = 0,
	MEAS_INT_IMMEDIATE,
	MEAS_INT_50MS,
	MEAS_INT_100MS,
	MEAS_INT_1S,
	MEAS_INT_NONE,
};

/**
 * struct adc5_sdam_data - data per SDAM allocated for adc usage
 * @base_addr: base address for the ADC SDAM peripheral.
 * @irq_name: ADC IRQ name.
 * @irq: ADC IRQ number.
 */
struct adc5_sdam_data {
	u16 base_addr;
	const char *irq_name;
	int irq;
};

/**
 * struct adc5_device_data - Top-level ADC device data
 * @regmap: ADC peripheral register map field.
 * @base: array of SDAM data.
 * @num_sdams: number of ADC SDAM peripherals.
 */
struct adc5_device_data {
	struct regmap *regmap;
	struct adc5_sdam_data *base;
	int num_sdams;
};

/**
 * struct adc5_channel_common_prop - ADC channel properties (common to ADC and TM).
 * @channel: channel number, refer to the channel list.
 * @cal_method: calibration method.
 * @decimation: sampling rate supported for the channel.
 * @sid: ID of PMIC owning the channel.
 * @label: Channel name used in device tree.
 * @prescale: channel scaling performed on the input signal.
 * @hw_settle_time_us: the time between AMUX being configured and the
 *	start of conversion in uS.
 * @avg_samples: ability to provide single result from the ADC
 *	that is an average of multiple measurements.
 * @scale_fn_type: Represents the scaling function to convert voltage
 *	physical units desired by the client for the channel.
 */
struct adc5_channel_common_prop {
	unsigned int channel;
	enum adc5_cal_method cal_method;
	unsigned int decimation;
	unsigned int sid;
	const char *label;
	unsigned int prescale;
	unsigned int hw_settle_time_us;
	unsigned int avg_samples;
	enum vadc_scale_fn_type scale_fn_type;
};

/**
 * struct tm5_aux_dev_wrapper - wrapper structure around TM auxiliary device
 * @aux_dev: TM auxiliary device structure.
 * @dev_data: Top-level ADC device data.
 * @tm_props: Array of common ADC channel properties for TM channels.
 * @n_tm_channels: number of TM channels.
 */
struct tm5_aux_dev_wrapper {
	struct auxiliary_device aux_dev;
	struct adc5_device_data *dev_data;
	struct adc5_channel_common_prop *tm_props;
	unsigned int n_tm_channels;
};

int adc5_gen3_read(struct adc5_device_data *adc, unsigned int sdam_index,
		   u16 offset, u8 *data, int len);

int adc5_gen3_write(struct adc5_device_data *adc, unsigned int sdam_index,
		    u16 offset, u8 *data, int len);

int adc5_gen3_poll_wait_hs(struct adc5_device_data *adc,
			   unsigned int sdam_index);

void adc5_gen3_update_dig_param(struct adc5_channel_common_prop *prop,
				u8 *data);

int adc5_gen3_status_clear(struct adc5_device_data *adc,
			   int sdam_index, u16 offset, u8 *val, int len);

void adc5_gen3_mutex_lock(struct device *dev);
void adc5_gen3_mutex_unlock(struct device *dev);
int adc5_gen3_get_scaled_reading(struct device *dev,
				 struct adc5_channel_common_prop *common_props,
				 int *val);
int adc5_gen3_therm_code_to_temp(struct device *dev,
				 struct adc5_channel_common_prop *common_props,
				 u16 code, int *val);

#endif /* QCOM_ADC5_GEN3_COMMON_H */
