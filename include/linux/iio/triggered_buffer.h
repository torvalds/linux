/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IIO_TRIGGERED_BUFFER_H_
#define _LINUX_IIO_TRIGGERED_BUFFER_H_

#include <linux/interrupt.h>

struct attribute;
struct iio_dev;
struct iio_buffer_setup_ops;

int iio_triggered_buffer_setup_ext(struct iio_dev *indio_dev,
	irqreturn_t (*h)(int irq, void *p),
	irqreturn_t (*thread)(int irq, void *p),
	const struct iio_buffer_setup_ops *setup_ops,
	const struct attribute **buffer_attrs);
void iio_triggered_buffer_cleanup(struct iio_dev *indio_dev);

#define iio_triggered_buffer_setup(indio_dev, h, thread, setup_ops)		\
	iio_triggered_buffer_setup_ext((indio_dev), (h), (thread), (setup_ops), NULL)

int devm_iio_triggered_buffer_setup_ext(struct device *dev,
					struct iio_dev *indio_dev,
					irqreturn_t (*h)(int irq, void *p),
					irqreturn_t (*thread)(int irq, void *p),
					const struct iio_buffer_setup_ops *ops,
					const struct attribute **buffer_attrs);

#define devm_iio_triggered_buffer_setup(dev, indio_dev, h, thread, setup_ops)	\
	devm_iio_triggered_buffer_setup_ext((dev), (indio_dev), (h), (thread), (setup_ops), NULL)

#endif
