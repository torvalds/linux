/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_IIO_KFIFO_BUF_H__
#define __LINUX_IIO_KFIFO_BUF_H__

struct iio_buffer;
struct device;

struct iio_buffer *iio_kfifo_allocate(void);
void iio_kfifo_free(struct iio_buffer *r);

struct iio_buffer *devm_iio_kfifo_allocate(struct device *dev);

#endif
