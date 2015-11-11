#ifndef __IIO_AD7291_H__
#define __IIO_AD7291_H__

/**
 * struct ad7291_platform_data - AD7291 platform data
 * @use_external_ref: Whether to use an external or internal reference voltage
 */
struct ad7291_platform_data {
	bool use_external_ref;
};

#endif
