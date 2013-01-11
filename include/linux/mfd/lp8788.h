/*
 * TI LP8788 MFD Device
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __MFD_LP8788_H__
#define __MFD_LP8788_H__

#include <linux/gpio.h>
#include <linux/irqdomain.h>
#include <linux/regmap.h>

#define LP8788_DEV_BUCK		"lp8788-buck"
#define LP8788_DEV_DLDO		"lp8788-dldo"
#define LP8788_DEV_ALDO		"lp8788-aldo"
#define LP8788_DEV_CHARGER	"lp8788-charger"
#define LP8788_DEV_RTC		"lp8788-rtc"
#define LP8788_DEV_BACKLIGHT	"lp8788-backlight"
#define LP8788_DEV_VIBRATOR	"lp8788-vibrator"
#define LP8788_DEV_KEYLED	"lp8788-keyled"
#define LP8788_DEV_ADC		"lp8788-adc"

#define LP8788_NUM_BUCKS	4
#define LP8788_NUM_DLDOS	12
#define LP8788_NUM_ALDOS	10
#define LP8788_NUM_BUCK2_DVS	2

#define LP8788_CHG_IRQ		"CHG_IRQ"
#define LP8788_PRSW_IRQ		"PRSW_IRQ"
#define LP8788_BATT_IRQ		"BATT_IRQ"
#define LP8788_ALM_IRQ		"ALARM_IRQ"

enum lp8788_int_id {
	/* interrup register 1 : Addr 00h */
	LP8788_INT_TSDL,
	LP8788_INT_TSDH,
	LP8788_INT_UVLO,
	LP8788_INT_FLAGMON,
	LP8788_INT_PWRON_TIME,
	LP8788_INT_PWRON,
	LP8788_INT_COMP1,
	LP8788_INT_COMP2,

	/* interrupt register 2 : Addr 01h */
	LP8788_INT_CHG_INPUT_STATE,
	LP8788_INT_CHG_STATE,
	LP8788_INT_EOC,
	LP8788_INT_CHG_RESTART,
	LP8788_INT_RESTART_TIMEOUT,
	LP8788_INT_FULLCHG_TIMEOUT,
	LP8788_INT_PRECHG_TIMEOUT,

	/* interrupt register 3 : Addr 02h */
	LP8788_INT_RTC_ALARM1 = 17,
	LP8788_INT_RTC_ALARM2,
	LP8788_INT_ENTER_SYS_SUPPORT,
	LP8788_INT_EXIT_SYS_SUPPORT,
	LP8788_INT_BATT_LOW,
	LP8788_INT_NO_BATT,

	LP8788_INT_MAX = 24,
};

enum lp8788_dvs_sel {
	DVS_SEL_V0,
	DVS_SEL_V1,
	DVS_SEL_V2,
	DVS_SEL_V3,
};

enum lp8788_ext_ldo_en_id {
	EN_ALDO1,
	EN_ALDO234,
	EN_ALDO5,
	EN_ALDO7,
	EN_DLDO7,
	EN_DLDO911,
	EN_LDOS_MAX,
};

enum lp8788_charger_event {
	NO_CHARGER,
	CHARGER_DETECTED,
};

enum lp8788_bl_ctrl_mode {
	LP8788_BL_REGISTER_ONLY,
	LP8788_BL_COMB_PWM_BASED,	/* PWM + I2C, changed by PWM input */
	LP8788_BL_COMB_REGISTER_BASED,	/* PWM + I2C, changed by I2C */
};

enum lp8788_bl_dim_mode {
	LP8788_DIM_EXPONENTIAL,
	LP8788_DIM_LINEAR,
};

enum lp8788_bl_full_scale_current {
	LP8788_FULLSCALE_5000uA,
	LP8788_FULLSCALE_8500uA,
	LP8788_FULLSCALE_1200uA,
	LP8788_FULLSCALE_1550uA,
	LP8788_FULLSCALE_1900uA,
	LP8788_FULLSCALE_2250uA,
	LP8788_FULLSCALE_2600uA,
	LP8788_FULLSCALE_2950uA,
};

enum lp8788_bl_ramp_step {
	LP8788_RAMP_8us,
	LP8788_RAMP_1024us,
	LP8788_RAMP_2048us,
	LP8788_RAMP_4096us,
	LP8788_RAMP_8192us,
	LP8788_RAMP_16384us,
	LP8788_RAMP_32768us,
	LP8788_RAMP_65538us,
};

enum lp8788_bl_pwm_polarity {
	LP8788_PWM_ACTIVE_HIGH,
	LP8788_PWM_ACTIVE_LOW,
};

enum lp8788_isink_scale {
	LP8788_ISINK_SCALE_100mA,
	LP8788_ISINK_SCALE_120mA,
};

enum lp8788_isink_number {
	LP8788_ISINK_1,
	LP8788_ISINK_2,
	LP8788_ISINK_3,
};

enum lp8788_alarm_sel {
	LP8788_ALARM_1,
	LP8788_ALARM_2,
	LP8788_ALARM_MAX,
};

enum lp8788_adc_id {
	LPADC_VBATT_5P5,
	LPADC_VIN_CHG,
	LPADC_IBATT,
	LPADC_IC_TEMP,
	LPADC_VBATT_6P0,
	LPADC_VBATT_5P0,
	LPADC_ADC1,
	LPADC_ADC2,
	LPADC_VDD,
	LPADC_VCOIN,
	LPADC_VDD_LDO,
	LPADC_ADC3,
	LPADC_ADC4,
	LPADC_MAX,
};

struct lp8788;

/*
 * lp8788_buck1_dvs
 * @gpio         : gpio pin number for dvs control
 * @vsel         : dvs selector for buck v1 register
 */
struct lp8788_buck1_dvs {
	int gpio;
	enum lp8788_dvs_sel vsel;
};

/*
 * lp8788_buck2_dvs
 * @gpio         : two gpio pin numbers are used for dvs
 * @vsel         : dvs selector for buck v2 register
 */
struct lp8788_buck2_dvs {
	int gpio[LP8788_NUM_BUCK2_DVS];
	enum lp8788_dvs_sel vsel;
};

/*
 * struct lp8788_ldo_enable_pin
 *
 *   Basically, all LDOs are enabled through the I2C commands.
 *   But ALDO 1 ~ 5, 7, DLDO 7, 9, 11 can be enabled by external gpio pins.
 *
 * @gpio         : gpio number which is used for enabling ldos
 * @init_state   : initial gpio state (ex. GPIOF_OUT_INIT_LOW)
 */
struct lp8788_ldo_enable_pin {
	int gpio;
	int init_state;
};

/*
 * struct lp8788_chg_param
 * @addr         : charging control register address (range : 0x11 ~ 0x1C)
 * @val          : charging parameter value
 */
struct lp8788_chg_param {
	u8 addr;
	u8 val;
};

/*
 * struct lp8788_charger_platform_data
 * @adc_vbatt         : adc channel name for battery voltage
 * @adc_batt_temp     : adc channel name for battery temperature
 * @max_vbatt_mv      : used for calculating battery capacity
 * @chg_params        : initial charging parameters
 * @num_chg_params    : numbers of charging parameters
 * @charger_event     : the charger event can be reported to the platform side
 */
struct lp8788_charger_platform_data {
	const char *adc_vbatt;
	const char *adc_batt_temp;
	unsigned int max_vbatt_mv;
	struct lp8788_chg_param *chg_params;
	int num_chg_params;
	void (*charger_event) (struct lp8788 *lp,
				enum lp8788_charger_event event);
};

/*
 * struct lp8788_bl_pwm_data
 * @pwm_set_intensity     : set duty of pwm
 * @pwm_get_intensity     : get current duty of pwm
 */
struct lp8788_bl_pwm_data {
	void (*pwm_set_intensity) (int brightness, int max_brightness);
	int (*pwm_get_intensity) (int max_brightness);
};

/*
 * struct lp8788_backlight_platform_data
 * @name                  : backlight driver name. (default: "lcd-backlight")
 * @initial_brightness    : initial value of backlight brightness
 * @bl_mode               : brightness control by pwm or lp8788 register
 * @dim_mode              : dimming mode selection
 * @full_scale            : full scale current setting
 * @rise_time             : brightness ramp up step time
 * @fall_time             : brightness ramp down step time
 * @pwm_pol               : pwm polarity setting when bl_mode is pwm based
 * @pwm_data              : platform specific pwm generation functions
 *                          only valid when bl_mode is pwm based
 */
struct lp8788_backlight_platform_data {
	char *name;
	int initial_brightness;
	enum lp8788_bl_ctrl_mode bl_mode;
	enum lp8788_bl_dim_mode dim_mode;
	enum lp8788_bl_full_scale_current full_scale;
	enum lp8788_bl_ramp_step rise_time;
	enum lp8788_bl_ramp_step fall_time;
	enum lp8788_bl_pwm_polarity pwm_pol;
	struct lp8788_bl_pwm_data pwm_data;
};

/*
 * struct lp8788_led_platform_data
 * @name         : led driver name. (default: "keyboard-backlight")
 * @scale        : current scale
 * @num          : current sink number
 * @iout_code    : current output value (Addr 9Ah ~ 9Bh)
 */
struct lp8788_led_platform_data {
	char *name;
	enum lp8788_isink_scale scale;
	enum lp8788_isink_number num;
	int iout_code;
};

/*
 * struct lp8788_vib_platform_data
 * @name         : vibrator driver name
 * @scale        : current scale
 * @num          : current sink number
 * @iout_code    : current output value (Addr 9Ah ~ 9Bh)
 * @pwm_code     : PWM code value (Addr 9Ch ~ 9Eh)
 */
struct lp8788_vib_platform_data {
	char *name;
	enum lp8788_isink_scale scale;
	enum lp8788_isink_number num;
	int iout_code;
	int pwm_code;
};

/*
 * struct lp8788_platform_data
 * @init_func    : used for initializing registers
 *                 before mfd driver is registered
 * @buck_data    : regulator initial data for buck
 * @dldo_data    : regulator initial data for digital ldo
 * @aldo_data    : regulator initial data for analog ldo
 * @buck1_dvs    : gpio configurations for buck1 dvs
 * @buck2_dvs    : gpio configurations for buck2 dvs
 * @ldo_pin      : gpio configurations for enabling LDOs
 * @chg_pdata    : platform data for charger driver
 * @alarm_sel    : rtc alarm selection (1 or 2)
 * @bl_pdata     : configurable data for backlight driver
 * @led_pdata    : configurable data for led driver
 * @vib_pdata    : configurable data for vibrator driver
 * @adc_pdata    : iio map data for adc driver
 */
struct lp8788_platform_data {
	/* general system information */
	int (*init_func) (struct lp8788 *lp);

	/* regulators */
	struct regulator_init_data *buck_data[LP8788_NUM_BUCKS];
	struct regulator_init_data *dldo_data[LP8788_NUM_DLDOS];
	struct regulator_init_data *aldo_data[LP8788_NUM_ALDOS];
	struct lp8788_buck1_dvs *buck1_dvs;
	struct lp8788_buck2_dvs *buck2_dvs;
	struct lp8788_ldo_enable_pin *ldo_pin[EN_LDOS_MAX];

	/* charger */
	struct lp8788_charger_platform_data *chg_pdata;

	/* rtc alarm */
	enum lp8788_alarm_sel alarm_sel;

	/* backlight */
	struct lp8788_backlight_platform_data *bl_pdata;

	/* current sinks */
	struct lp8788_led_platform_data *led_pdata;
	struct lp8788_vib_platform_data *vib_pdata;

	/* adc iio map data */
	struct iio_map *adc_pdata;
};

/*
 * struct lp8788
 * @dev          : parent device pointer
 * @regmap       : used for i2c communcation on accessing registers
 * @irqdm        : interrupt domain for handling nested interrupt
 * @irq          : pin number of IRQ_N
 * @pdata        : lp8788 platform specific data
 */
struct lp8788 {
	struct device *dev;
	struct regmap *regmap;
	struct irq_domain *irqdm;
	int irq;
	struct lp8788_platform_data *pdata;
};

int lp8788_irq_init(struct lp8788 *lp, int chip_irq);
void lp8788_irq_exit(struct lp8788 *lp);
int lp8788_read_byte(struct lp8788 *lp, u8 reg, u8 *data);
int lp8788_read_multi_bytes(struct lp8788 *lp, u8 reg, u8 *data, size_t count);
int lp8788_write_byte(struct lp8788 *lp, u8 reg, u8 data);
int lp8788_update_bits(struct lp8788 *lp, u8 reg, u8 mask, u8 data);
#endif
