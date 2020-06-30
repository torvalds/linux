/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _INDUSTRIAL_IO_OPAQUE_H_
#define _INDUSTRIAL_IO_OPAQUE_H_

/**
 * struct iio_dev_opaque - industrial I/O device opaque information
 * @indio_dev:			public industrial I/O device information
 */
struct iio_dev_opaque {
	struct iio_dev			indio_dev;
};

#define to_iio_dev_opaque(indio_dev)		\
	container_of(indio_dev, struct iio_dev_opaque, indio_dev)

#endif
