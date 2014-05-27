/*
 * Driver for ADAU1761/ADAU1461/ADAU1761/ADAU1961/ADAU1781/ADAU1781 codecs
 *
 * Copyright 2011-2014 Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2 or later.
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

#endif
