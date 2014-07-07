/*
 * ADAU1977/ADAU1978/ADAU1979 driver
 *
 * Copyright 2014 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2.
 */

#ifndef __LINUX_PLATFORM_DATA_ADAU1977_H__
#define __LINUX_PLATFORM_DATA_ADAU1977_H__

/**
 * enum adau1977_micbias - ADAU1977 MICBIAS pin voltage setting
 * @ADAU1977_MICBIAS_5V0: MICBIAS is set to 5.0 V
 * @ADAU1977_MICBIAS_5V5: MICBIAS is set to 5.5 V
 * @ADAU1977_MICBIAS_6V0: MICBIAS is set to 6.0 V
 * @ADAU1977_MICBIAS_6V5: MICBIAS is set to 6.5 V
 * @ADAU1977_MICBIAS_7V0: MICBIAS is set to 7.0 V
 * @ADAU1977_MICBIAS_7V5: MICBIAS is set to 7.5 V
 * @ADAU1977_MICBIAS_8V0: MICBIAS is set to 8.0 V
 * @ADAU1977_MICBIAS_8V5: MICBIAS is set to 8.5 V
 * @ADAU1977_MICBIAS_9V0: MICBIAS is set to 9.0 V
 */
enum adau1977_micbias {
	ADAU1977_MICBIAS_5V0 = 0x0,
	ADAU1977_MICBIAS_5V5 = 0x1,
	ADAU1977_MICBIAS_6V0 = 0x2,
	ADAU1977_MICBIAS_6V5 = 0x3,
	ADAU1977_MICBIAS_7V0 = 0x4,
	ADAU1977_MICBIAS_7V5 = 0x5,
	ADAU1977_MICBIAS_8V0 = 0x6,
	ADAU1977_MICBIAS_8V5 = 0x7,
	ADAU1977_MICBIAS_9V0 = 0x8,
};

/**
 * struct adau1977_platform_data - Platform configuration data for the ADAU1977
 * @micbias: Specifies the voltage for the MICBIAS pin
 */
struct adau1977_platform_data {
	enum adau1977_micbias micbias;
};

#endif
