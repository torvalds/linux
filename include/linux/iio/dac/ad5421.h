#ifndef __IIO_DAC_AD5421_H__
#define __IIO_DAC_AD5421_H__

/**
 * enum ad5421_current_range - Current range the AD5421 is configured for.
 * @AD5421_CURRENT_RANGE_4mA_20mA: 4 mA to 20 mA (RANGE1,0 pins = 00)
 * @AD5421_CURRENT_RANGE_3mA8_21mA: 3.8 mA to 21 mA (RANGE1,0 pins = x1)
 * @AD5421_CURRENT_RANGE_3mA2_24mA: 3.2 mA to 24 mA (RANGE1,0 pins = 10)
 */

enum ad5421_current_range {
	AD5421_CURRENT_RANGE_4mA_20mA,
	AD5421_CURRENT_RANGE_3mA8_21mA,
	AD5421_CURRENT_RANGE_3mA2_24mA,
};

/**
 * struct ad5421_platform_data - AD5421 DAC driver platform data
 * @external_vref: whether an external reference voltage is used or not
 * @current_range: Current range the AD5421 is configured for
 */

struct ad5421_platform_data {
	bool external_vref;
	enum ad5421_current_range current_range;
};

#endif
