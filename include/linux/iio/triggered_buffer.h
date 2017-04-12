#ifndef _LINUX_IIO_TRIGGERED_BUFFER_H_
#define _LINUX_IIO_TRIGGERED_BUFFER_H_

#include <linux/interrupt.h>

struct iio_dev;
struct iio_buffer_setup_ops;

int iio_triggered_buffer_setup(struct iio_dev *indio_dev,
	irqreturn_t (*h)(int irq, void *p),
	irqreturn_t (*thread)(int irq, void *p),
	const struct iio_buffer_setup_ops *setup_ops);
void iio_triggered_buffer_cleanup(struct iio_dev *indio_dev);

int devm_iio_triggered_buffer_setup(struct device *dev,
				    struct iio_dev *indio_dev,
				    irqreturn_t (*h)(int irq, void *p),
				    irqreturn_t (*thread)(int irq, void *p),
				    const struct iio_buffer_setup_ops *ops);
void devm_iio_triggered_buffer_cleanup(struct device *dev,
				       struct iio_dev *indio_dev);

#endif
