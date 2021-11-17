/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AD7266/65 SPI ADC driver
 *
 * Copyright 2012 Analog Devices Inc.
 */

#ifndef __IIO_ADC_AD7266_H__
#define __IIO_ADC_AD7266_H__

/**
 * enum ad7266_range - AD7266 reference voltage range
 * @AD7266_RANGE_VREF: Device is configured for input range 0V - VREF
 *			(RANGE pin set to low)
 * @AD7266_RANGE_2VREF: Device is configured for input range 0V - 2VREF
 *			(RANGE pin set to high)
 */
enum ad7266_range {
	AD7266_RANGE_VREF,
	AD7266_RANGE_2VREF,
};

/**
 * enum ad7266_mode - AD7266 sample mode
 * @AD7266_MODE_DIFF: Device is configured for full differential mode
 *				(SGL/DIFF pin set to low, AD0 pin set to low)
 * @AD7266_MODE_PSEUDO_DIFF: Device is configured for pseudo differential mode
 *				(SGL/DIFF pin set to low, AD0 pin set to high)
 * @AD7266_MODE_SINGLE_ENDED: Device is configured for single-ended mode
 *				(SGL/DIFF pin set to high)
 */
enum ad7266_mode {
	AD7266_MODE_DIFF,
	AD7266_MODE_PSEUDO_DIFF,
	AD7266_MODE_SINGLE_ENDED,
};

/**
 * struct ad7266_platform_data - Platform data for the AD7266 driver
 * @range: Reference voltage range the device is configured for
 * @mode: Sample mode the device is configured for
 * @fixed_addr: Whether the address pins are hard-wired
 */
struct ad7266_platform_data {
	enum ad7266_range range;
	enum ad7266_mode mode;
	bool fixed_addr;
};

#endif
