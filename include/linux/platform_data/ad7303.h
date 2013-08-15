/*
 * Analog Devices AD7303 DAC driver
 *
 * Copyright 2013 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef __IIO_ADC_AD7303_H__
#define __IIO_ADC_AD7303_H__

/**
 * struct ad7303_platform_data - AD7303 platform data
 * @use_external_ref: If set to true use an external voltage reference connected
 * to the REF pin, otherwise use the internal reference derived from Vdd.
 */
struct ad7303_platform_data {
	bool use_external_ref;
};

#endif
