/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for ADAU1361/ADAU1461/ADAU1761/ADAU1961/ADAU1381/ADAU1781 codecs
 *
 * Copyright 2011-2014 Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#ifndef __LINUX_PLATFORM_DATA_ADAU17X1_H__
#define __LINUX_PLATFORM_DATA_ADAU17X1_H__

/**
 * enum adau17x1_micbias_voltage - Microphone bias voltage
 * @ADAU17X1_MICBIAS_0_90_AVDD: 0.9 * AVDD
 * @ADAU17X1_MICBIAS_0_65_AVDD: 0.65 * AVDD
 */
enum adau17x1_micbias_voltage {
	ADAU17X1_MICBIAS_0_90_AVDD = 0,
	ADAU17X1_MICBIAS_0_65_AVDD = 1,
};

/**
 * enum adau1761_digmic_jackdet_pin_mode - Configuration of the JACKDET/MICIN pin
 * @ADAU1761_DIGMIC_JACKDET_PIN_MODE_NONE: Disable the pin
 * @ADAU1761_DIGMIC_JACKDET_PIN_MODE_DIGMIC: Configure the pin for usage as
 *   digital microphone input.
 * @ADAU1761_DIGMIC_JACKDET_PIN_MODE_JACKDETECT: Configure the pin for jack
 *  insertion detection.
 */
enum adau1761_digmic_jackdet_pin_mode {
	ADAU1761_DIGMIC_JACKDET_PIN_MODE_NONE,
	ADAU1761_DIGMIC_JACKDET_PIN_MODE_DIGMIC,
	ADAU1761_DIGMIC_JACKDET_PIN_MODE_JACKDETECT,
};

/**
 * adau1761_jackdetect_debounce_time - Jack insertion detection debounce time
 * @ADAU1761_JACKDETECT_DEBOUNCE_5MS: 5 milliseconds
 * @ADAU1761_JACKDETECT_DEBOUNCE_10MS: 10 milliseconds
 * @ADAU1761_JACKDETECT_DEBOUNCE_20MS: 20 milliseconds
 * @ADAU1761_JACKDETECT_DEBOUNCE_40MS: 40 milliseconds
 */
enum adau1761_jackdetect_debounce_time {
	ADAU1761_JACKDETECT_DEBOUNCE_5MS = 0,
	ADAU1761_JACKDETECT_DEBOUNCE_10MS = 1,
	ADAU1761_JACKDETECT_DEBOUNCE_20MS = 2,
	ADAU1761_JACKDETECT_DEBOUNCE_40MS = 3,
};

/**
 * enum adau1761_output_mode - Output mode configuration
 * @ADAU1761_OUTPUT_MODE_HEADPHONE: Headphone output
 * @ADAU1761_OUTPUT_MODE_HEADPHONE_CAPLESS: Capless headphone output
 * @ADAU1761_OUTPUT_MODE_LINE: Line output
 */
enum adau1761_output_mode {
	ADAU1761_OUTPUT_MODE_HEADPHONE,
	ADAU1761_OUTPUT_MODE_HEADPHONE_CAPLESS,
	ADAU1761_OUTPUT_MODE_LINE,
};

/**
 * struct adau1761_platform_data - ADAU1761 Codec driver platform data
 * @input_differential: If true the input pins will be configured in
 *  differential mode.
 * @lineout_mode: Output mode for the LOUT/ROUT pins
 * @headphone_mode: Output mode for the LHP/RHP pins
 * @digmic_jackdetect_pin_mode: JACKDET/MICIN pin configuration
 * @jackdetect_debounce_time: Jack insertion detection debounce time.
 *  Note: This value will only be used, if the JACKDET/MICIN pin is configured
 *  for jack insertion detection.
 * @jackdetect_active_low: If true the jack insertion detection is active low.
 *  Othwise it will be active high.
 * @micbias_voltage: Microphone voltage bias
 */
struct adau1761_platform_data {
	bool input_differential;
	enum adau1761_output_mode lineout_mode;
	enum adau1761_output_mode headphone_mode;

	enum adau1761_digmic_jackdet_pin_mode digmic_jackdetect_pin_mode;

	enum adau1761_jackdetect_debounce_time jackdetect_debounce_time;
	bool jackdetect_active_low;

	enum adau17x1_micbias_voltage micbias_voltage;
};

/**
 * struct adau1781_platform_data - ADAU1781 Codec driver platform data
 * @left_input_differential: If true configure the left input as
 * differential input.
 * @right_input_differential: If true configure the right input as differntial
 *  input.
 * @use_dmic: If true configure the MIC pins as digital microphone pins instead
 *  of analog microphone pins.
 * @micbias_voltage: Microphone voltage bias
 */
struct adau1781_platform_data {
	bool left_input_differential;
	bool right_input_differential;

	bool use_dmic;

	enum adau17x1_micbias_voltage micbias_voltage;
};

#endif
