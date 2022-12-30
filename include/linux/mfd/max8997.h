/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * max8997.h - Driver for the Maxim 8997/8966
 *
 *  Copyright (C) 2009-2010 Samsung Electrnoics
 *  MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This driver is based on max8998.h
 *
 * MAX8997 has PMIC, MUIC, HAPTIC, RTC, FLASH, and Fuel Gauge devices.
 * Except Fuel Gauge, every device shares the same I2C bus and included in
 * this mfd driver. Although the fuel gauge is included in the chip, it is
 * excluded from the driver because a) it has a different I2C bus from
 * others and b) it can be enabled simply by using MAX17042 driver.
 */

#ifndef __LINUX_MFD_MAX8997_H
#define __LINUX_MFD_MAX8997_H

#include <linux/regulator/consumer.h>

/* MAX8997/8966 regulator IDs */
enum max8997_regulators {
	MAX8997_LDO1 = 0,
	MAX8997_LDO2,
	MAX8997_LDO3,
	MAX8997_LDO4,
	MAX8997_LDO5,
	MAX8997_LDO6,
	MAX8997_LDO7,
	MAX8997_LDO8,
	MAX8997_LDO9,
	MAX8997_LDO10,
	MAX8997_LDO11,
	MAX8997_LDO12,
	MAX8997_LDO13,
	MAX8997_LDO14,
	MAX8997_LDO15,
	MAX8997_LDO16,
	MAX8997_LDO17,
	MAX8997_LDO18,
	MAX8997_LDO21,
	MAX8997_BUCK1,
	MAX8997_BUCK2,
	MAX8997_BUCK3,
	MAX8997_BUCK4,
	MAX8997_BUCK5,
	MAX8997_BUCK6,
	MAX8997_BUCK7,
	MAX8997_EN32KHZ_AP,
	MAX8997_EN32KHZ_CP,
	MAX8997_ENVICHG,
	MAX8997_ESAFEOUT1,
	MAX8997_ESAFEOUT2,
	MAX8997_CHARGER_CV, /* control MBCCV of MBCCTRL3 */
	MAX8997_CHARGER, /* charger current, MBCCTRL4 */
	MAX8997_CHARGER_TOPOFF, /* MBCCTRL5 */

	MAX8997_REG_MAX,
};

struct max8997_regulator_data {
	int id;
	struct regulator_init_data *initdata;
	struct device_node *reg_node;
};

struct max8997_muic_reg_data {
	u8 addr;
	u8 data;
};

/**
 * struct max8997_muic_platform_data
 * @init_data: array of max8997_muic_reg_data
 *	       used for initializing registers of MAX8997 MUIC device
 * @num_init_data: array size of init_data
 */
struct max8997_muic_platform_data {
	struct max8997_muic_reg_data *init_data;
	int num_init_data;

	/* Check cable state after certain delay */
	int detcable_delay_ms;

	/*
	 * Default usb/uart path whether UART/USB or AUX_UART/AUX_USB
	 * h/w path of COMP2/COMN1 on CONTROL1 register.
	 */
	int path_usb;
	int path_uart;
};

enum max8997_haptic_motor_type {
	MAX8997_HAPTIC_ERM,
	MAX8997_HAPTIC_LRA,
};

enum max8997_haptic_pulse_mode {
	MAX8997_EXTERNAL_MODE,
	MAX8997_INTERNAL_MODE,
};

enum max8997_haptic_pwm_divisor {
	MAX8997_PWM_DIVISOR_32,
	MAX8997_PWM_DIVISOR_64,
	MAX8997_PWM_DIVISOR_128,
	MAX8997_PWM_DIVISOR_256,
};

/**
 * max8997_haptic_platform_data
 * @pwm_period: period in nano second for PWM device
 *		valid for MAX8997_EXTERNAL_MODE
 * @type: motor type
 * @mode: pulse mode
 *     MAX8997_EXTERNAL_MODE: external PWM device is used to control motor
 *     MAX8997_INTERNAL_MODE: internal pulse generator is used to control motor
 * @pwm_divisor: divisor for external PWM device
 * @internal_mode_pattern: internal mode pattern for internal mode
 *     [0 - 3]: valid pattern number
 * @pattern_cycle: the number of cycles of the waveform
 *		   for the internal mode pattern
 *     [0 - 15]: available cycles
 * @pattern_signal_period: period of the waveform for the internal mode pattern
 *     [0 - 255]: available period
 */
struct max8997_haptic_platform_data {
	unsigned int pwm_period;

	enum max8997_haptic_motor_type type;
	enum max8997_haptic_pulse_mode mode;
	enum max8997_haptic_pwm_divisor pwm_divisor;

	unsigned int internal_mode_pattern;
	unsigned int pattern_cycle;
	unsigned int pattern_signal_period;
};

enum max8997_led_mode {
	MAX8997_NONE,
	MAX8997_FLASH_MODE,
	MAX8997_MOVIE_MODE,
	MAX8997_FLASH_PIN_CONTROL_MODE,
	MAX8997_MOVIE_PIN_CONTROL_MODE,
};

/**
 *  struct max8997_led_platform_data
 *  The number of LED devices for MAX8997 is two
 *  @mode: LED mode for each LED device
 *  @brightness: initial brightness for each LED device
 *	range:
 *	[0 - 31]: MAX8997_FLASH_MODE and MAX8997_FLASH_PIN_CONTROL_MODE
 *	[0 - 15]: MAX8997_MOVIE_MODE and MAX8997_MOVIE_PIN_CONTROL_MODE
 */
struct max8997_led_platform_data {
	enum max8997_led_mode mode[2];
	u8 brightness[2];
};

struct max8997_platform_data {
	/* IRQ */
	int ono;

	/* ---- PMIC ---- */
	struct max8997_regulator_data *regulators;
	int num_regulators;

	/*
	 * SET1~3 DVS GPIOs control Buck1, 2, and 5 simultaneously. Therefore,
	 * With buckx_gpiodvs enabled, the buckx cannot be controlled
	 * independently. To control buckx (of 1, 2, and 5) independently,
	 * disable buckx_gpiodvs and control with BUCKxDVS1 register.
	 *
	 * When buckx_gpiodvs and bucky_gpiodvs are both enabled, set_voltage
	 * on buckx will change the voltage of bucky at the same time.
	 *
	 */
	bool ignore_gpiodvs_side_effect;
	int buck125_gpios[3]; /* GPIO of [0]SET1, [1]SET2, [2]SET3 */
	int buck125_default_idx; /* Default value of SET1, 2, 3 */
	unsigned int buck1_voltage[8]; /* buckx_voltage in uV */
	bool buck1_gpiodvs;
	unsigned int buck2_voltage[8];
	bool buck2_gpiodvs;
	unsigned int buck5_voltage[8];
	bool buck5_gpiodvs;

	/* ---- Charger control ---- */
	/* eoc stands for 'end of charge' */
	int eoc_mA; /* 50 ~ 200mA by 10mA step */
	/* charge Full Timeout */
	int timeout; /* 0 (no timeout), 5, 6, 7 hours */

	/* ---- MUIC ---- */
	struct max8997_muic_platform_data *muic_pdata;

	/* ---- HAPTIC ---- */
	struct max8997_haptic_platform_data *haptic_pdata;

	/* RTC: Not implemented */
	/* ---- LED ---- */
	struct max8997_led_platform_data *led_pdata;
};

#endif /* __LINUX_MFD_MAX8997_H */
