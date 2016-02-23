/*
 * AD7298 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef __LINUX_PLATFORM_DATA_AD7298_H__
#define __LINUX_PLATFORM_DATA_AD7298_H__

/**
 * struct ad7298_platform_data - Platform data for the ad7298 ADC driver
 * @ext_ref: Whether to use an external reference voltage.
 **/
struct ad7298_platform_data {
	bool ext_ref;
};

#endif /* IIO_ADC_AD7298_H_ */
