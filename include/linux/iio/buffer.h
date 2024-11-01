/* SPDX-License-Identifier: GPL-2.0-only */
/* The industrial I/O core - generic buffer interfaces.
 *
 * Copyright (c) 2008 Jonathan Cameron
 */

#ifndef _IIO_BUFFER_GENERIC_H_
#define _IIO_BUFFER_GENERIC_H_
#include <linux/sysfs.h>
#include <linux/iio/iio.h>

struct iio_buffer;

enum iio_buffer_direction {
	IIO_BUFFER_DIRECTION_IN,
	IIO_BUFFER_DIRECTION_OUT,
};

int iio_push_to_buffers(struct iio_dev *indio_dev, const void *data);

int iio_pop_from_buffer(struct iio_buffer *buffer, void *data);

/**
 * iio_push_to_buffers_with_timestamp() - push data and timestamp to buffers
 * @indio_dev:		iio_dev structure for device.
 * @data:		sample data
 * @timestamp:		timestamp for the sample data
 *
 * Pushes data to the IIO device's buffers. If timestamps are enabled for the
 * device the function will store the supplied timestamp as the last element in
 * the sample data buffer before pushing it to the device buffers. The sample
 * data buffer needs to be large enough to hold the additional timestamp
 * (usually the buffer should be indio->scan_bytes bytes large).
 *
 * Returns 0 on success, a negative error code otherwise.
 */
static inline int iio_push_to_buffers_with_timestamp(struct iio_dev *indio_dev,
	void *data, int64_t timestamp)
{
	if (indio_dev->scan_timestamp) {
		size_t ts_offset = indio_dev->scan_bytes / sizeof(int64_t) - 1;
		((int64_t *)data)[ts_offset] = timestamp;
	}

	return iio_push_to_buffers(indio_dev, data);
}

int iio_push_to_buffers_with_ts_unaligned(struct iio_dev *indio_dev,
					  const void *data, size_t data_sz,
					  int64_t timestamp);

bool iio_validate_scan_mask_onehot(struct iio_dev *indio_dev,
				   const unsigned long *mask);

int iio_device_attach_buffer(struct iio_dev *indio_dev,
			     struct iio_buffer *buffer);

#endif /* _IIO_BUFFER_GENERIC_H_ */
