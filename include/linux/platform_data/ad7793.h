/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AD7792/AD7793 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 */
#ifndef __LINUX_PLATFORM_DATA_AD7793_H__
#define __LINUX_PLATFORM_DATA_AD7793_H__

/**
 * enum ad7793_clock_source - AD7793 clock source selection
 * @AD7793_CLK_SRC_INT: Internal 64 kHz clock, not available at the CLK pin.
 * @AD7793_CLK_SRC_INT_CO: Internal 64 kHz clock, available at the CLK pin.
 * @AD7793_CLK_SRC_EXT: Use external clock.
 * @AD7793_CLK_SRC_EXT_DIV2: Use external clock divided by 2.
 */
enum ad7793_clock_source {
	AD7793_CLK_SRC_INT,
	AD7793_CLK_SRC_INT_CO,
	AD7793_CLK_SRC_EXT,
	AD7793_CLK_SRC_EXT_DIV2,
};

/**
 * enum ad7793_bias_voltage - AD7793 bias voltage selection
 * @AD7793_BIAS_VOLTAGE_DISABLED: Bias voltage generator disabled
 * @AD7793_BIAS_VOLTAGE_AIN1: Bias voltage connected to AIN1(-).
 * @AD7793_BIAS_VOLTAGE_AIN2: Bias voltage connected to AIN2(-).
 * @AD7793_BIAS_VOLTAGE_AIN3: Bias voltage connected to AIN3(-).
 *	Only valid for AD7795/AD7796.
 */
enum ad7793_bias_voltage {
	AD7793_BIAS_VOLTAGE_DISABLED,
	AD7793_BIAS_VOLTAGE_AIN1,
	AD7793_BIAS_VOLTAGE_AIN2,
	AD7793_BIAS_VOLTAGE_AIN3,
};

/**
 * enum ad7793_refsel - AD7793 reference voltage selection
 * @AD7793_REFSEL_REFIN1: External reference applied between REFIN1(+)
 *	and REFIN1(-).
 * @AD7793_REFSEL_REFIN2: External reference applied between REFIN2(+) and
 *	and REFIN1(-). Only valid for AD7795/AD7796.
 * @AD7793_REFSEL_INTERNAL: Internal 1.17 V reference.
 */
enum ad7793_refsel {
	AD7793_REFSEL_REFIN1 = 0,
	AD7793_REFSEL_REFIN2 = 1,
	AD7793_REFSEL_INTERNAL = 2,
};

/**
 * enum ad7793_current_source_direction - AD7793 excitation current direction
 * @AD7793_IEXEC1_IOUT1_IEXEC2_IOUT2: Current source IEXC1 connected to pin
 *	IOUT1, current source IEXC2 connected to pin IOUT2.
 * @AD7793_IEXEC1_IOUT2_IEXEC2_IOUT1: Current source IEXC2 connected to pin
 *	IOUT1, current source IEXC1 connected to pin IOUT2.
 * @AD7793_IEXEC1_IEXEC2_IOUT1: Both current sources connected to pin IOUT1.
 *	Only valid when the current sources are set to 10 uA or 210 uA.
 * @AD7793_IEXEC1_IEXEC2_IOUT2: Both current sources connected to Pin IOUT2.
 *	Only valid when the current ources are set to 10 uA or 210 uA.
 */
enum ad7793_current_source_direction {
	AD7793_IEXEC1_IOUT1_IEXEC2_IOUT2 = 0,
	AD7793_IEXEC1_IOUT2_IEXEC2_IOUT1 = 1,
	AD7793_IEXEC1_IEXEC2_IOUT1 = 2,
	AD7793_IEXEC1_IEXEC2_IOUT2 = 3,
};

/**
 * enum ad7793_excitation_current - AD7793 excitation current selection
 * @AD7793_IX_DISABLED: Excitation current Disabled.
 * @AD7793_IX_10uA: Enable 10 micro-ampere excitation current.
 * @AD7793_IX_210uA: Enable 210 micro-ampere excitation current.
 * @AD7793_IX_1mA: Enable 1 milli-Ampere excitation current.
 */
enum ad7793_excitation_current {
	AD7793_IX_DISABLED = 0,
	AD7793_IX_10uA = 1,
	AD7793_IX_210uA = 2,
	AD7793_IX_1mA = 3,
};

/**
 * struct ad7793_platform_data - AD7793 platform data
 * @clock_src: Clock source selection
 * @burnout_current: If set to true the 100nA burnout current is enabled.
 * @boost_enable: Enable boost for the bias voltage generator.
 * @buffered: If set to true configure the device for buffered input mode.
 * @unipolar: If set to true sample in unipolar mode, if set to false sample in
 *		bipolar mode.
 * @refsel: Reference voltage selection
 * @bias_voltage: Bias voltage selection
 * @exitation_current: Excitation current selection
 * @current_source_direction: Excitation current direction selection
 */
struct ad7793_platform_data {
	enum ad7793_clock_source clock_src;
	bool burnout_current;
	bool boost_enable;
	bool buffered;
	bool unipolar;

	enum ad7793_refsel refsel;
	enum ad7793_bias_voltage bias_voltage;
	enum ad7793_excitation_current exitation_current;
	enum ad7793_current_source_direction current_source_direction;
};

#endif /* IIO_ADC_AD7793_H_ */
