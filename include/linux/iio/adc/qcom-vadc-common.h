/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Code shared between the different Qualcomm PMIC voltage ADCs
 */

#ifndef QCOM_VADC_COMMON_H
#define QCOM_VADC_COMMON_H

#include <linux/adc-tm-clients.h>
#include <linux/math.h>
#include <linux/types.h>

#define VADC_CONV_TIME_MIN_US			2000
#define VADC_CONV_TIME_MAX_US			2100

/* Min ADC code represents 0V */
#define VADC_MIN_ADC_CODE			0x6000
/* Max ADC code represents full-scale range of 1.8V */
#define VADC_MAX_ADC_CODE			0xa800

#define VADC_ABSOLUTE_RANGE_UV			625000
#define VADC_RATIOMETRIC_RANGE			1800

#define VADC_DEF_PRESCALING			0 /* 1:1 */
#define VADC_DEF_DECIMATION			0 /* 512 */
#define VADC_DEF_HW_SETTLE_TIME			0 /* 0 us */
#define VADC_DEF_AVG_SAMPLES			0 /* 1 sample */
#define VADC_DEF_CALIB_TYPE			VADC_CALIB_ABSOLUTE

#define VADC_DECIMATION_MIN			512
#define VADC_DECIMATION_MAX			4096
#define ADC5_DEF_VBAT_PRESCALING		1 /* 1:3 */
#define ADC5_DECIMATION_SHORT			250
#define ADC5_DECIMATION_MEDIUM			420
#define ADC5_DECIMATION_LONG			840
/* Default decimation - 1024 for rev2, 840 for pmic5 */
#define ADC5_DECIMATION_DEFAULT			2
#define ADC5_DECIMATION_SAMPLES_MAX		3

#define VADC_HW_SETTLE_DELAY_MAX		10000
#define VADC_HW_SETTLE_SAMPLES_MAX		16
#define VADC_AVG_SAMPLES_MAX			512
#define ADC5_AVG_SAMPLES_MAX			16

#define PMIC5_CHG_TEMP_SCALE_FACTOR		377500
#define PMIC5_SMB_TEMP_CONSTANT			419400
#define PMIC5_SMB_TEMP_SCALE_FACTOR		356
#define PMIC5_SMB1398_TEMP_CONSTANT		268235
#define PMIC5_SMB1398_TEMP_SCALE_FACTOR		340

#define PMIC5_PM2250_S3_DIE_TEMP_SCALE_FACTOR	187263
#define PMIC5_PM2250_S3_DIE_TEMP_CONSTANT	720100

#define PMI_CHG_SCALE_1				-138890
#define PMI_CHG_SCALE_2				391750000000LL

#define VADC5_MAX_CODE				0x7fff
#define ADC5_FULL_SCALE_CODE			0x70e4
#define ADC5_USR_DATA_CHECK			0x8000

#define R_PU_100K				100000
#define RATIO_MAX_ADC7				BIT(14)

#define PMIC5_GEN3_USB_IN_I_SCALE_FACTOR	9248

#define ADC_VDD_REF				1875000

/*
 * VADC_CALIB_ABSOLUTE: uses the 625mV and 1.25V as reference channels.
 * VADC_CALIB_RATIOMETRIC: uses the reference voltage (1.8V) and GND for
 * calibration.
 */
enum vadc_calibration {
	VADC_CALIB_ABSOLUTE = 0,
	VADC_CALIB_RATIOMETRIC
};

/**
 * struct vadc_linear_graph - Represent ADC characteristics.
 * @dy: numerator slope to calculate the gain.
 * @dx: denominator slope to calculate the gain.
 * @gnd: A/D word of the ground reference used for the channel.
 *
 * Each ADC device has different offset and gain parameters which are
 * computed to calibrate the device.
 */
struct vadc_linear_graph {
	s32 dy;
	s32 dx;
	s32 gnd;
};

/**
 * enum adc_tm_rscale_fn_type - Scaling function used to convert the
 *	channels input voltage/temperature to corresponding ADC code that is
 *	applied for thresholds. Check the corresponding channels scaling to
 *	determine the appropriate temperature/voltage units that are passed
 *	to the scaling function. Example battery follows the power supply
 *	framework that needs its units to be in decidegreesC so it passes
 *	deci-degreesC. PA_THERM clients pass the temperature in degrees.
 *	The order below should match the one in the driver for
 *	adc_tm_rscale_fn[].
 */
enum adc_tm_rscale_fn_type {
	SCALE_R_ABSOLUTE = 0,
	SCALE_RSCALE_NONE,
};

/**
 * struct adc_tm_config - Represent ADC Thermal Monitor configuration.
 * @high_thr_temp: Temperature at which high threshold notification is required.
 * @low_thr_temp: Temperature at which low threshold notification is required.
 * @low_thr_voltage : Low threshold voltage ADC code used for reverse
 *			calibration.
 * @high_thr_voltage: High threshold voltage ADC code used for reverse
 *			calibration.
 */
struct adc_tm_config {
	int	high_thr_temp;
	int	low_thr_temp;
	int64_t	high_thr_voltage;
	int64_t	low_thr_voltage;
};

struct adc_tm_reverse_scale_fn {
	int32_t (*chan)(struct adc_tm_config *tm_config);
};

struct adc_tm_client_info {
	struct list_head			list;
	struct adc_tm_param			*param;
	int32_t						low_thr_requested;
	int32_t						high_thr_requested;
	bool						notify_low_thr;
	bool						notify_high_thr;
	bool						high_thr_set;
	bool						low_thr_set;
	enum adc_tm_state_request	state_request;
};

/**
 * enum vadc_scale_fn_type - Scaling function to convert ADC code to
 *				physical scaled units for the channel.
 * SCALE_DEFAULT: Default scaling to convert raw adc code to voltage (uV).
 * SCALE_THERM_100K_PULLUP: Returns temperature in millidegC.
 *				 Uses a mapping table with 100K pullup.
 * SCALE_PMIC_THERM: Returns result in milli degree's Centigrade.
 * SCALE_XOTHERM: Returns XO thermistor voltage in millidegC.
 * SCALE_PMI_CHG_TEMP: Conversion for PMI CHG temp
 * SCALE_HW_CALIB_DEFAULT: Default scaling to convert raw adc code to
 *	voltage (uV) with hardware applied offset/slope values to adc code.
 * SCALE_HW_CALIB_THERM_100K_PULLUP: Returns temperature in millidegC using
 *	lookup table. The hardware applies offset/slope to adc code.
 * SCALE_HW_CALIB_XOTHERM: Returns XO thermistor voltage in millidegC using
 *	100k pullup. The hardware applies offset/slope to adc code.
 * SCALE_HW_CALIB_THERM_100K_PU_PM7: Returns temperature in millidegC using
 *	lookup table for PMIC7. The hardware applies offset/slope to adc code.
 * SCALE_HW_CALIB_PMIC_THERM: Returns result in milli degree's Centigrade.
 *	The hardware applies offset/slope to adc code.
 * SCALE_HW_CALIB_PMIC_THERM_PM7: Returns result in milli degree's Centigrade.
 *	The hardware applies offset/slope to adc code. This is for PMIC7.
 * SCALE_HW_CALIB_PM5_CHG_TEMP: Returns result in millidegrees for PMIC5
 *	charger temperature.
 * SCALE_HW_CALIB_PM5_SMB_TEMP: Returns result in millidegrees for PMIC5
 *	SMB1390 temperature.
 * SCALE_HW_CALIB_BATT_THERM_100K: Returns battery thermistor temperature in
 *	decidegC using 100k pullup. The hardware applies offset/slope to adc
 *	code.
 * SCALE_HW_CALIB_BATT_THERM_30K: Returns battery thermistor temperature in
 *	decidegC using 30k pullup. The hardware applies offset/slope to adc
 *	code.
 * SCALE_HW_CALIB_BATT_THERM_400K: Returns battery thermistor temperature in
 *	decidegC using 400k pullup. The hardware applies offset/slope to adc
 *	code.
 * SCALE_HW_CALIB_PM5_SMB1398_TEMP: Returns result in millidegrees for PMIC5
 *	SMB1398 temperature.
 * SCALE_HW_CALIB_PM7_SMB_TEMP: Returns result in millidegrees for PMIC7
 *	SMB139x temperature.
 * SCALE_HW_CALIB_PM7_CHG_TEMP: Returns result in millidegrees for PMIC7
 *	charger temperature.
 * SCALE_HW_CALIB_CUR: Returns result in microamperes for PMIC7 channels that
 *	use voltage scaling.
 * SCALE_HW_CALIB_CUR_RAW: Returns result in microamperes for PMIC7 channels
 *	that use raw ADC code.
 * SCALE_HW_CALIB_PM2250_S3_DIE_TEMP: Returns result in millidegrees for
 *	S3 die temperature channel on PM2250.
 * SCALE_HW_CALIB_PM5_CUR: Returns result in microamperes for PMIC5 channels
 *	that use voltage scaling.
 * SCALE_HW_CALIB_PM5_GEN3_BATT_THERM_100K: Returns battery thermistor
 *	temperature in decidegC using 100k pullup. The hardware applies
 *	offset/slope to adc code.
 * SCALE_HW_CALIB_PM5_GEN3_BATT_ID_100K: Returns battery ID resistance
 *	in ohms using 100k pullup. The hardware applies offset/slope to
 *	adc code.
 * SCALE_HW_CALIB_PM5_GEN3_USB_IN_I: Returns USB input current in microamperes.
 */
enum vadc_scale_fn_type {
	SCALE_DEFAULT = 0,
	SCALE_THERM_100K_PULLUP,
	SCALE_PMIC_THERM,
	SCALE_XOTHERM,
	SCALE_PMI_CHG_TEMP,
	SCALE_HW_CALIB_DEFAULT,
	SCALE_HW_CALIB_THERM_100K_PULLUP,
	SCALE_HW_CALIB_XOTHERM,
	SCALE_HW_CALIB_THERM_100K_PU_PM7,
	SCALE_HW_CALIB_PMIC_THERM,
	SCALE_HW_CALIB_PMIC_THERM_PM7,
	SCALE_HW_CALIB_PM5_CHG_TEMP,
	SCALE_HW_CALIB_PM5_SMB_TEMP,
	SCALE_HW_CALIB_BATT_THERM_100K,
	SCALE_HW_CALIB_BATT_THERM_30K,
	SCALE_HW_CALIB_BATT_THERM_400K,
	SCALE_HW_CALIB_PM5_SMB1398_TEMP,
	SCALE_HW_CALIB_PM7_SMB_TEMP,
	SCALE_HW_CALIB_PM7_CHG_TEMP,
	SCALE_HW_CALIB_CUR,
	SCALE_HW_CALIB_CUR_RAW,
	SCALE_HW_CALIB_PM2250_S3_DIE_TEMP,
	SCALE_HW_CALIB_PM5_CUR,
	SCALE_HW_CALIB_PM5_GEN3_BATT_THERM_100K,
	SCALE_HW_CALIB_PM5_GEN3_BATT_ID_100K,
	SCALE_HW_CALIB_PM5_GEN3_USB_IN_I,
	SCALE_HW_CALIB_INVALID,
};

struct adc5_data {
	const char	*name;
	const u32	full_scale_code_volt;
	const u32	full_scale_code_cur;
	const struct adc5_channels *adc_chans;
	const struct iio_info *info;
	unsigned int	*decimation;
	unsigned int	*hw_settle_1;
	unsigned int	*hw_settle_2;
};

int qcom_vadc_scale(enum vadc_scale_fn_type scaletype,
		    const struct vadc_linear_graph *calib_graph,
		    const struct u32_fract *prescale,
		    bool absolute,
		    u16 adc_code, int *result_mdec);

struct qcom_adc5_scale_type {
	int (*scale_fn)(const struct u32_fract *prescale,
		const struct adc5_data *data, u16 adc_code, int *result);
};

int qcom_adc5_hw_scale(enum vadc_scale_fn_type scaletype,
		    unsigned int prescale_ratio,
		    const struct adc5_data *data,
		    u16 adc_code, int *result_mdec);

u16 qcom_adc_tm5_temp_volt_scale(unsigned int prescale_ratio,
				 u32 full_scale_code_volt, int temp);

u16 qcom_adc_tm5_gen2_temp_res_scale(int temp);

int qcom_adc5_prescaling_from_dt(u32 num, u32 den);

int qcom_adc5_hw_settle_time_from_dt(u32 value, const unsigned int *hw_settle);

int qcom_adc5_avg_samples_from_dt(u32 value);

int qcom_adc5_decimation_from_dt(u32 value, const unsigned int *decimation);

int qcom_vadc_decimation_from_dt(u32 value);

void adc_tm_scale_therm_voltage_100k_gen3(struct adc_tm_config *param);

int32_t adc_tm_absolute_rthr_gen3(struct adc_tm_config *tm_config);

#endif /* QCOM_VADC_COMMON_H */
