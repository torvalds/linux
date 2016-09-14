#ifndef __IIO_MAGNETOMETER_AK8975_H__
#define __IIO_MAGNETOMETER_AK8975_H__

#include <linux/iio/iio.h>

/**
 * struct ak8975_platform_data - AK8975 magnetometer driver platform data
 * @eoc_gpio:    data ready event gpio
 * @orientation: mounting matrix relative to main hardware
 */
struct ak8975_platform_data {
	int                     eoc_gpio;
	struct iio_mount_matrix orientation;
};

#endif
