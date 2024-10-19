/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Industrial I/O in kernel access map interface.
 *
 * Copyright (c) 2011 Jonathan Cameron
 */

#ifndef _IIO_INKERN_H_
#define _IIO_INKERN_H_

struct device;
struct iio_dev;
struct iio_map;

/**
 * iio_map_array_register() - tell the core about inkernel consumers
 * @indio_dev:	provider device
 * @map:	array of mappings specifying association of channel with client
 */
int iio_map_array_register(struct iio_dev *indio_dev,
			   const struct iio_map *map);

/**
 * iio_map_array_unregister() - tell the core to remove consumer mappings for
 *				the given provider device
 * @indio_dev:	provider device
 */
int iio_map_array_unregister(struct iio_dev *indio_dev);

/**
 * devm_iio_map_array_register - device-managed version of iio_map_array_register
 * @dev:	Device object to which to bind the unwinding of this registration
 * @indio_dev:	Pointer to the iio_dev structure
 * @maps:	Pointer to an IIO map object which is to be registered to this IIO device
 *
 * This function will call iio_map_array_register() to register an IIO map object
 * and will also hook a callback to the iio_map_array_unregister() function to
 * handle de-registration of the IIO map object when the device's refcount goes to
 * zero.
 */
int devm_iio_map_array_register(struct device *dev, struct iio_dev *indio_dev,
				const struct iio_map *maps);

#endif
